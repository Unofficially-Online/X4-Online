"""Default-off primary-group P2 FIRE observation; no native fire mutation."""
import hashlib
from pathlib import Path
import re

from linux_ui_session_budget import session_budget

HELPERS = ('linux_player_fire_observation.py',)
SOURCES = (
    'src/native/engine/linux/player_weapon_groups.h',
    'src/native/engine/linux/gate_player_fire_observation.h',
    'src/native/engine/linux/gate_player_physics_replay.h',
    'src/native/engine/linux/gate_linux.c',
    'src/native/engine/linux/test_player_weapon_groups.c',
    'src/native/engine/linux/test_gate_player_fire_observation.c',
    'src/native/engine/linux/test_gate_player_physics_replay.c',
    'src/native/engine/linux/test_gate_player_control_recurring.c',
    'tests/test_linux_player_fire_observation.py',
    'tools/dev/linux_player_fire_observation.py',
)
PREFIX = 'X4LAN FIRE_DIAG '
NUMBER = r'(0|[1-9][0-9]{0,19})'
POINTER = r'(0|[1-9a-f][0-9a-f]{0,15})'
RECEIPT = re.compile(
    PREFIX + r'v=2 stage=observe tick=' + NUMBER + ' worker=' + NUMBER + ' ship=' + POINTER +
    ' group=' + NUMBER + ' ids=' + NUMBER + ' weapons=' + NUMBER + ' gun=' + POINTER +
    ' id=' + POINTER + ' controller=' + POINTER + ' vtable=' + POINTER + ' firing=' + NUMBER +
    ' raw91=' + NUMBER + ' raw91_present=' + NUMBER +
    r' mutation=0 outcome=(no_gun|empty_observed|stock_player_observed|incompatible_owner)\n')
FINAL = 'X4LAN LIFECYCLE_DISCARD result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n'


def enabled(options):
    value = options.get('player_fire_observation', False)
    if type(value) is not bool:
        raise ValueError('player_fire_observation must be boolean')
    if value and (options.get('player_startup_phases') != 'v1-t40' or
                  type(options.get('player_ui_session_frames')) is not int or
                  not 180 <= options['player_ui_session_frames'] <= 900 or
                  type(options.get('independent_keyboard_frames')) is not int or
                  options['independent_keyboard_frames'] != options['player_ui_session_frames']):
        raise ValueError('FIRE observation requires a finite T40 independent physics session')
    return value


def environment(env, active):
    if type(active) is not bool:
        raise ValueError('FIRE observation activation must be boolean')
    result = dict(env)
    result.pop('X4_DEV_FIRE_DIAG', None)
    if active:
        result['X4_DEV_FIRE_DIAG'] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('FIRE observation source closure incomplete')
    return paths


def collect(stderr, native_trace, active, physics, options):
    """Call after complete per-client physics validation, before compaction."""
    errors, hashes, lines = [], {}, {}
    for key, path in (('stderr_sha256', stderr), ('log_sha256', native_trace)):
        try:
            raw = Path(path).read_bytes()
            hashes[key] = hashlib.sha256(raw).hexdigest()
            lines[key] = raw.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if active or key == 'stderr_sha256':
                errors.append('FIRE observation input unavailable: ' + str(error))
    stderr_lines = lines.get('stderr_sha256', [])
    rows = [(index, row) for index, row in enumerate(stderr_lines) if PREFIX.strip() in row]
    result = dict(hashes, enabled=active, performed=False, errors=errors,
                  raw_receipts=[row for _, row in rows], scope='primary_group_first_gun_p2_once',
                  mutation_performed=False, fire_replay_supported=False,
                  trigger_semantics_proven=False, lifetime_exclusion_proven=False)
    if not active:
        if rows:
            errors.append('unexpected disabled FIRE observation receipt')
        result['pass'] = not errors
        return result
    try:
        if enabled(options) is not True:
            raise ValueError('FIRE observation differs from frozen activation')
        budget = session_budget(options['player_ui_session_frames'], options['player_startup_phases'])
        if (physics.get('pass') is not True or len(hashes) != 2 or
                any(physics.get(key) != value for key, value in hashes.items())):
            raise ValueError('FIRE observation lacks accepted hash-bound physics')
        frames = physics.get('frames', [])
        if [frame['tick'] for frame in frames] != list(range(budget['apply'], budget['recurring_end'] + 1)):
            raise ValueError('FIRE observation requires the complete accepted physics window')
        if len(rows) != 1:
            raise ValueError('FIRE observation requires exactly one receipt')
        match = RECEIPT.fullmatch(rows[0][1])
        if not match:
            raise ValueError('invalid FIRE observation receipt')
        (tick, worker, ship, group, ids, weapons, gun, runtime, controller, vtable,
         firing, raw91, raw91_present, outcome) = match.groups()
        tick, worker, group, ids, weapons, firing, raw91, raw91_present = map(
            int, (tick, worker, group, ids, weapons, firing, raw91, raw91_present))
        ship, gun, runtime, controller, vtable = (int(value, 16) for value in (ship, gun, runtime, controller, vtable))
        if (tick != budget['apply'] or not 0 < worker <= 0xffffffff or not ship or
                group >= 64 or ids > 512 or weapons > 16 or weapons > ids or
                firing > 1 or raw91 > 255 or raw91_present not in (0, 1)):
            raise ValueError('FIRE observation field bounds mismatch')
        first = frames[0]
        if (first.get('worker_thread') != worker or not first.get('bodies') or
                first['bodies'][0].get('ship') != ship):
            raise ValueError('FIRE observation differs from accepted P2 worker/ship')
        if outcome == 'no_gun':
            if weapons or any((gun, runtime, controller, vtable, firing, raw91, raw91_present)):
                raise ValueError('FIRE no-gun receipt contains gun state')
        elif not weapons or not gun or not runtime:
            raise ValueError('FIRE observation gun identity missing')
        elif outcome == 'empty_observed':
            if any((controller, vtable, raw91, raw91_present)):
                raise ValueError('FIRE empty receipt contains controller state')
        elif outcome == 'stock_player_observed':
            if not controller or vtable != 0x3b2fc38 or raw91_present != 1:
                raise ValueError('FIRE stock-controller observation invalid')
        else:
            raise ValueError('FIRE incompatible controller cannot produce an accepted observation')
        index = rows[0][0]
        enter = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n'
        release = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release\n'
        if stderr_lines[:index].count(enter) != 1 or release in stderr_lines[:index] or stderr_lines[index + 1:].count(release) != 1:
            raise ValueError('FIRE observation outside the accepted worker tail')
        if not lines.get('log_sha256') or lines['log_sha256'][-1] != FINAL:
            raise ValueError('FIRE observation lacks the final native discard')
        result.update(tick=tick, worker=worker, ship=ship, group=group, ids=ids, weapons=weapons,
                      gun=gun, runtime=runtime, controller=controller, vtable=vtable, firing=firing,
                      raw91=raw91, raw91_present=raw91_present, outcome=outcome)
    except (ValueError, KeyError, TypeError, IndexError) as error:
        errors.append(str(error))
    result.update(performed=not errors, **{'pass': not errors})
    return result
