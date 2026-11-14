"""Additive v18 source closure and bounded semaphore observation evidence."""
import hashlib
from pathlib import Path
import re

import linux_view_performance_bundle as previous
from linux_ui_session_budget import budget_from_options

SOURCE_VERSION = 18
PROVIDER = 'libx4mp_semaphore_observer.so'
EXTRA_SOURCES = tuple('src/native/engine/linux/' + x for x in (
    'player_semaphore_original_view.h', 'gate_player_semaphore_original_view.h',
    'player_semaphore_maps_transaction.h',
    'player_semaphore_observer_preload.c', 'player_semaphore_observer.md',
    'player_semaphore_dispatch_callback.h', 'player_worker_dispatch_adapter.h',
    'gate_player_presentation_quiet.h', 'test_gate_player_presentation_quiet.c',
    'gate_player_presentation_fork.h', 'gate_player_dispatch_stop.h', 'test_gate_player_dispatch_stop.c', 'gate_player_fork_worker_park.h', 'gate_fork_save.h', 'gate_player_physics_frame_prefix.h', 'gate_player_lifecycle_discard.h', 'gate_player_physics_discard.h',
    'player_worker_dispatch_contract.h', 'player_worker_dispatch_contract.md', 'gate_player_worker_dispatch.h',
    'test_player_worker_dispatch_adapter.c', 'test_player_worker_dispatch_contract.c',
    'test_player_semaphore_dispatch_midwait.c',
    'gate_mass_traffic.h', 'gate_player_control_recurring.h', 'gate_linux.c',
    'gate_player_control_worker_tail.h', 'frame_worker_recurring.h', 'test_gate_mass_traffic.h',
    'test_gate_player_control_recurring.c', 'test_gate_player_control_recurring.sh',
    'gate_travel_determinism.h', 'test_player_semaphore_original_view.c',
    'test_gate_player_semaphore_original_view.c', 'test_player_semaphore_observer_preload.c',
    'test_semaphore_pin_provider.c')) + (
    'tests/test_linux_player_presentation_quiet.py', 'src/native/engine/lockstep_gate_exports.h', 'tests/test_linux_player_dispatch_stop.py', 'tests/test_linux_player_dispatch_fork_park.py', 'tests/test_linux_player_dispatch_log_prefix.py', 'tests/test_linux_player_worker_dispatch_adapter.py', 'tests/test_linux_player_worker_dispatch_contract.py',
    'tools/dev/build_linux_semaphore_observer.py', 'tools/dev/linux_semaphore_observer_bundle.py',
    'tools/dev/exec_linux_semaphore_observer.py', 'tests/test_linux_semaphore_original_view.py',
    'tests/test_linux_semaphore_observer_preload.py', 'tests/test_linux_semaphore_observer_bundle.py',
    'tools/dev/linux_stock_view_fov_observer.py', 'tests/test_linux_stock_view_fov_observer.py',
    'tools/dev/linux_player_frames_evidence.py', 'tools/dev/linux_player_network_evidence.py',
    'tools/dev/linux_gate_live.py', 'tests/test_linux_semaphore_checkpoint_evidence.py',
    'tests/test_linux_t40_evidence.py', 'tests/test_linux_network_long_window.py',
    'tools/dev/test_linux_player_frames_evidence.py', 'tools/dev/test_linux_player_network_evidence.py')
SOURCES = tuple(dict.fromkeys(previous.SOURCES + EXTRA_SOURCES))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_semaphore_observer_bundle.py', 'exec_linux_semaphore_observer.py',
                                                   'linux_stock_view_fov_observer.py', 'linux_player_frames_evidence.py',
                                                   'linux_player_network_evidence.py', 'linux_gate_live.py')))


def enabled(options):
    active = options.get('semaphore_observer', False)
    dispatch = options.get('worker_dispatch', False)
    if type(dispatch) is not bool or (dispatch and active is not True):
        raise ValueError('worker dispatch requires an explicit semaphore observer')
    if type(active) is not bool:
        raise ValueError('semaphore observer option must be boolean')
    budget = budget_from_options(options) if active else None
    if active and (not budget or options.get('player_startup_phases') != 'v1-t40' or
                   type(options.get('independent_keyboard_frames')) is not int or
                   options.get('independent_keyboard_frames') != budget['frames'] or
                   not (options.get('player_lifetime_preload') is True or options.get('player_lifetime_sha256')) or
                   options.get('serial_pathing') is not True or
                   not any(options.get(key) in ('sync', 'observe') for key in ('mass_traffic', 'region_pass'))):
        raise ValueError('semaphore observer requires finite T40, serial pathing and a mass/region wait hook')
    return active


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(p.is_symlink() or not p.is_file() for p in paths):
        raise ValueError('semaphore observer source closure incomplete')
    return paths


def wrap_program(case, manifest, env, program):
    """Start a non-preloaded helper, then preload only the authenticated X4."""
    if not enabled(manifest) or not isinstance(env, dict) or not isinstance(program, list) or not program:
        raise ValueError('invalid semaphore observer launch')
    case = Path(case)
    if not case.is_absolute() or program[0] != str(case / 'game/X4'):
        raise ValueError('semaphore observer launch is not the staged X4')
    sha, size = manifest.get('semaphore_observer_sha256'), manifest.get('semaphore_observer_size')
    if not isinstance(sha, str) or not re.fullmatch('[0-9a-f]{64}', sha) or type(size) is not int or size <= 0:
        raise ValueError('semaphore observer launch has no frozen provider pin')
    wrapped_env = dict(env)
    wrapped_env.pop('X4_DEV_WORKER_DISPATCH', None)
    if manifest.get('worker_dispatch') is True:
        wrapped_env['X4_DEV_WORKER_DISPATCH'] = '1'
    for name in ('X4MP_SEMAPHORE_OBSERVER', 'X4MP_SEMAPHORE_OBSERVER_FD',
                 'X4MP_SEMAPHORE_OBSERVER_SHA256', 'X4MP_SEMAPHORE_OBSERVER_SIZE'):
        wrapped_env.pop(name, None)
    wrapped_env['X4MP_SEMAPHORE_PRELOAD_CHAIN'] = wrapped_env.pop('LD_PRELOAD', '')
    wrapped_program = ['/usr/bin/python3', str(case / 'exec_linux_semaphore_observer.py'), '--case', str(case),
                       '--provider-sha256', sha, '--provider-size', str(size), '--', *program]
    return wrapped_env, wrapped_program


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    if manifest.get('worker_dispatch_evidence_version') != (1 if manifest.get('worker_dispatch') is True else None):
        raise ValueError('worker dispatch evidence version mismatch')
    if manifest.get('view_query_source_version') != SOURCE_VERSION:
        if active:
            raise ValueError('semaphore observer requires source version 18')
        return previous.validate_bundle(root, manifest, network)
    files = manifest.get('files')
    if not isinstance(files, dict):
        raise ValueError('invalid semaphore source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        if (not re.fullmatch('[0-9a-f]{64}', str(files.get(key))) or path.is_symlink() or
                not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != files[key]):
            raise ValueError('missing or changed semaphore source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        if (path.is_symlink() or not path.is_file() or files.get(name) != files.get('source-receipts/tools/dev/' + name) or
                hashlib.sha256(path.read_bytes()).hexdigest() != files.get(name)):
            raise ValueError('missing or changed semaphore helper: ' + name)
    provider = Path(root) / 'semaphore_observer' / PROVIDER
    key = 'semaphore_observer/' + PROVIDER
    if active:
        if (network is not True or manifest.get('semaphore_observer_evidence_version') != 1 or
                provider.is_symlink() or not provider.is_file() or hashlib.sha256(provider.read_bytes()).hexdigest() != files.get(key) or
                manifest.get('semaphore_observer_sha256') != files.get(key) or
                manifest.get('semaphore_observer_size') != provider.stat().st_size):
            raise ValueError('semaphore observer provider pin or network admission mismatch')
    elif (manifest.get('semaphore_observer_evidence_version') is not None or key in files or
          provider.exists() or provider.is_symlink() or manifest.get('semaphore_observer_sha256') is not None or
          manifest.get('semaphore_observer_size') is not None):
        raise ValueError('inactive semaphore observer has provider or evidence')
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=17), network)

ROW = re.compile(r'X4 SEMAPHORE OBSERVER v=1 phase=(enter|return) kind=([0-3]) token=([0-9]+) '
                 r'tid=([0-9]+) caller=([0-9a-f]+) arg=([0-9]+) result=([0-9]+) movement=([01]) ns=([0-9]+)')
STATUS = re.compile(r'X4 SEMAPHORE OBSERVER STATUS v=1 tick=([0-9]+) owner=([0-9]+) closed=1 '
                    r'active=([0-9]+) tokens=([0-9]+) receipts=([0-9]+) incomplete=([01]) '
                    r'constructor=([0-9]+) complete=([01])')
CHECKPOINT = re.compile(r'X4LAN SEMAPHORE OBSERVER CHECKPOINT tick=([0-9]+) owner=([0-9]+) main_parked=1 complete=1')


def collect(receipt_path, physics, options, active, *, ancestry=None):
    """Validate a closed paired ledger; never infer a frame owner from it."""
    result = {'pass': False, 'enabled': active, 'frame_ownership_proved': False,
              'bootstrap_proved': False, 'post_close_activity_observed': False, 'errors': []}
    try:
        if type(active) is not bool or active != enabled(options):
            raise ValueError('invalid semaphore collection mode')
        if not active:
            result['pass'] = True
            return result
        budget = budget_from_options(options)
        frames = physics.get('frames') if isinstance(physics, dict) else None
        if (not isinstance(physics, dict) or physics.get('pass') is not True or
                physics.get('returncode') != 223 or physics.get('frame_count') != budget['frames'] or
                not isinstance(frames, list) or not frames or frames[-1].get('tick') != budget['recurring_end']):
            raise ValueError('accepted same-client physics is required')
        lifecycle_path = Path(physics['native_fd_path'])
        lifecycle = lifecycle_path.read_bytes()
        if hashlib.sha256(lifecycle).hexdigest() != physics.get('log_sha256'):
            raise ValueError('lifecycle receipt hash mismatch')
        lines = lifecycle.decode('utf-8').splitlines()
        if options.get('worker_dispatch') is True:
            bootstrap = 'X4LAN WORKER_DISPATCH bootstrap=1 parked=1 count=0 maximum=1'
            if lines.count(bootstrap) != 1:
                raise ValueError('missing unique dispatch bootstrap receipt')
            dispatch_rows = [line for line in lines if line.startswith('X4LAN WORKER_DISPATCH tick=')]
            expected = [f"X4LAN WORKER_DISPATCH tick={tick} ticket={tick} consumed=1 post_returned=1"
                        for tick in range(budget['take'], budget['recurring_end'] + 1)]
            if dispatch_rows != expected:
                raise ValueError('missing, duplicate or mismatched native dispatch tickets')
            result['worker_dispatch'] = {'pass': True, 'evidence_version': 1, 'dispatches': len(expected)}
        checkpoints = [CHECKPOINT.fullmatch(line) for line in lines if 'X4LAN SEMAPHORE OBSERVER CHECKPOINT' in line]
        if len(checkpoints) != 1 or not checkpoints[0] or int(checkpoints[0][1]) != budget['recurring_end']:
            raise ValueError('missing authenticated final worker checkpoint')
        if not 0 < int(checkpoints[0][2]) <= 0xffffffff:
            raise ValueError('invalid checkpoint OS thread owner')
        # Physics thread fields use a pthread-derived identifier, not SYS_gettid.
        # Only the separately validated ancestry receipt supplies an OS tid.
        if (options.get('view_ancestry_observer') is True or ancestry is not None) and (not isinstance(ancestry, dict) or ancestry.get('pass') is not True or
                type(ancestry.get('tid')) is not int or ancestry['tid'] != int(checkpoints[0][2])):
            raise ValueError('semaphore checkpoint differs from validated ancestry OS tid')
        receipt = Path(receipt_path)
        if receipt.is_symlink() or not receipt.is_file() or receipt.stat().st_size > 4 * 1024 * 1024:
            raise ValueError('missing or oversized semaphore receipt')
        raw = receipt.read_bytes()
        rows = raw.decode('utf-8').splitlines()
        if len(rows) < 4 or rows[0] != 'X4 SEMAPHORE OBSERVER ACTIVE version=1':
            raise ValueError('missing or malformed semaphore activation')
        status = STATUS.fullmatch(rows[-1])
        if not status:
            raise ValueError('missing explicit final semaphore status')
        tick, owner, active_calls, token_count, receipt_count, incomplete, constructor_state, complete = map(int, status.groups())
        if (tick != budget['recurring_end'] or owner != int(checkpoints[0][2]) or not owner or active_calls or incomplete or
                constructor_state != 2 or complete != 1 or receipt_count != len(rows) - 2 or
                receipt_count != token_count * 2 or token_count < 2 or receipt_count > 8192):
            raise ValueError('incomplete or mismatched semaphore status')
        pairs = {}
        for position, line in enumerate(rows[1:-1], 1):
            match = ROW.fullmatch(line)
            if not match:
                raise ValueError('malformed semaphore invocation row')
            phase = match[1]
            kind, token, tid, caller, argument, raw_result, movement, timestamp = (
                int(match[2]), int(match[3]), int(match[4]), int(match[5], 16), int(match[6]),
                int(match[7]), int(match[8]), int(match[9]))
            if (not token or not tid or not caller or not timestamp or argument > (1 << 64) - 1 or
                    raw_result > (1 << 64) - 1 or movement and kind != 2):
                raise ValueError('invalid semaphore invocation fields')
            pair = pairs.setdefault(token, {})
            if phase in pair:
                raise ValueError('duplicate semaphore invocation phase')
            pair[phase] = (kind, tid, caller, argument, movement, timestamp, position)
        if set(pairs) != set(range(1, token_count + 1)):
            raise ValueError('missing semaphore invocation tokens')
        for pair in pairs.values():
            if set(pair) != {'enter', 'return'} or pair['enter'][:5] != pair['return'][:5] or \
                    pair['enter'][5] > pair['return'][5] or pair['enter'][6] >= pair['return'][6]:
                raise ValueError('unpaired or inconsistent semaphore invocation')
        constructors = [p for p in pairs.values() if p['enter'][0] == 0]
        if len(constructors) != 1:
            raise ValueError('target constructor was not observed exactly once')
        constructor = constructors[0]
        if constructor['enter'][2] != 0x829a8f or constructor['enter'][3] != 1:
            raise ValueError('unexpected target constructor caller or initial values')
        ctor_end = constructor['return'][5]
        if any(p['enter'][5] < ctor_end for p in pairs.values() if p is not constructor):
            raise ValueError('target call occurred before constructor returned')
        movement = sum(p['enter'][0] == 2 and p['enter'][4] == 1 for p in pairs.values())
        if not movement:
            raise ValueError('no explicitly tagged movement wait observed')
        if receipt.read_bytes() != raw or lifecycle_path.read_bytes() != lifecycle:
            raise ValueError('semaphore evidence changed during collection')
        result.update(pass_=True, receipt_sha256=hashlib.sha256(raw).hexdigest(),
                      token_count=token_count, movement_waits=movement, status_tick=tick)
        result['pass'] = result.pop('pass_')
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, AttributeError) as error:
        result['errors'].append(str(error))
    return result
