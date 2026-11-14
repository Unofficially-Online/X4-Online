"""Bounded mouse-input gate observations, not a movement or ownership proof."""
import hashlib
from pathlib import Path
import re

from linux_ui_session_budget import session_budget

SOURCES = tuple('src/native/engine/linux/' + name for name in (
    'gate_player_cursor_input_trace.h', 'player_cursor_input_trace.h',
    'gate_player_cursor_replay.h', 'gate_linux.c',
)) + ('tools/dev/linux_cursor_input_trace.py',)
HELPERS = ('linux_cursor_input_trace.py',)
ENVIRONMENT = 'X4_DEV_CURSOR_INPUT_TRACE'
MAX_ROWS = 256
N = r'(?:0|[1-9][0-9]{0,19})'
P = r'(?:0|[1-9a-f][0-9a-f]{0,15})'
FIELDS = ('tick', 'index', 'worker', 'input', 'device', 'mode33', 'steer36', 'transient37',
          'direct38', 'block39', 'gate42', 'override_a010', 'context8', 'ignored',
          'window_gate_present', 'window_gate', 'window_gate_value', 'window_owner',
          'window_context_present', 'window_context', 'window_modal')
POINTER_FIELDS = frozenset(('input', 'device', 'window_gate', 'window_owner', 'window_context'))
BYTE_FIELDS = frozenset(('mode33', 'steer36', 'transient37', 'direct38', 'block39', 'gate42',
                         'override_a010', 'context8', 'ignored', 'window_gate_value', 'window_modal'))
ROW = re.compile(
    r'X4 INPUT_MOUSE v=1 tick=(?P<tick>' + N + r') index=(?P<index>' + N +
    r') worker=(?P<worker>' + N + r') input=(?P<input>' + P + r') device=(?P<device>' + P +
    r') mode33=(?P<mode33>' + N + r') steer36=(?P<steer36>' + N +
    r') transient37=(?P<transient37>' + N + r') direct38=(?P<direct38>' + N +
    r') block39=(?P<block39>' + N + r') gate42=(?P<gate42>' + N +
    r') override_a010=(?P<override_a010>' + N + r') context8=(?P<context8>' + N +
    r') ignored=(?P<ignored>' + N + r') window_gate_present=(?P<window_gate_present>' + N +
    r') window_gate=(?P<window_gate>' + P + r') window_gate_value=(?P<window_gate_value>' + N +
    r') window_owner=(?P<window_owner>' + P + r') window_context_present=(?P<window_context_present>' + N +
    r') window_context=(?P<window_context>' + P + r') window_modal=(?P<window_modal>' + N +
    r') mutation=0\n')
WORKER = re.compile(r'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=(' + N + r') tail=1 event=(enter|release)\n')
CURSOR = re.compile(r'X4LAN CURSOR v=1 stage=capture tick=(' + N + r') .+\n')


def enabled(options):
    active = options.get('cursor_input_trace', False)
    if type(active) is not bool:
        raise ValueError('mouse input trace admission must be boolean')
    if active:
        if options.get('player_cursor_active_flight') is not True or options.get('player_startup_phases') != 'v1-t40':
            raise ValueError('mouse input trace requires explicit T40 cursor mode')
        budget = session_budget(options.get('player_ui_session_frames'), options.get('player_startup_phases'))
        if options.get('independent_keyboard_frames') != budget['frames']:
            raise ValueError('mouse input trace requires matching independent frames')
    return active


def environment(env, active):
    if type(active) is not bool:
        raise ValueError('mouse input trace activation must be boolean')
    result = dict(env)
    result.pop(ENVIRONMENT, None)
    if active:
        result[ENVIRONMENT] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('mouse input trace source closure incomplete')
    return paths


def collect(stderr, active, physics, cursor, options):
    result = {'pass': False, 'enabled': active, 'errors': [], 'movement_proven': False,
              'classification': 'observed_native_gate_flags', 'writer_exclusion_proven': False}
    errors = result['errors']
    try:
        if type(active) is not bool or active != enabled(options):
            raise ValueError('mouse input trace activation differs from options')
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        lines = raw.decode('utf-8', errors='strict').splitlines(keepends=True)
        rows = [(index, line) for index, line in enumerate(lines) if 'X4 INPUT_MOUSE' in line]
        result.update(stderr_sha256=digest, raw_receipts=[line for _, line in rows])
        if not active:
            if rows:
                raise ValueError('unexpected disabled mouse input receipt')
            result['pass'] = True
            return result
        budget = session_budget(options['player_ui_session_frames'], options['player_startup_phases'])
        if (not isinstance(physics, dict) or physics.get('pass') is not True or
                not isinstance(cursor, dict) or cursor.get('pass') is not True or
                cursor.get('enabled') is not True or cursor.get('performed') is not True or
                physics.get('stderr_sha256') != digest or cursor.get('stderr_sha256') != digest or
                not physics.get('log_sha256') or physics.get('log_sha256') != cursor.get('log_sha256')):
            raise ValueError('mouse trace lacks hash-bound accepted physics and cursor evidence')
        worker = cursor.get('worker')
        if type(worker) is not int or not 0 < worker <= 0xffffffff:
            raise ValueError('mouse trace lacks accepted worker identity')
        preapply = physics.get('preapply_frames', [])
        frames = physics.get('frames', [])
        if (not isinstance(preapply, list) or not isinstance(frames, list) or
                [frame.get('tick') for frame in preapply] != [37, 38, 39] or
                [frame.get('tick') for frame in frames] != list(range(40, budget['recurring_end'] + 1)) or
                any(frame.get('worker_thread') != worker for frame in preapply + frames)):
            raise ValueError('mouse trace worker or physics frame sequence differs')
        accepted_captures = cursor.get('captures')
        if (not isinstance(accepted_captures, list) or
                [capture.get('tick') for capture in accepted_captures] !=
                list(range(budget['capture'], budget['last_capture'] + 1))):
            raise ValueError('mouse trace cursor capture sequence differs')
        if not rows or len(rows) > MAX_ROWS:
            raise ValueError('mouse trace missing or exceeds row limit')
        close = (f"X4LAN CURSOR v=1 stage=close tick={budget['recurring_end']} "
                 f"captured={budget['frames']} slot1={budget['frames'] + 2} "
                 f"slot2={budget['frames']} complete=1\n")
        cursor_receipts = cursor.get('raw_receipts')
        if (not isinstance(cursor_receipts, list) or not cursor_receipts or
                cursor_receipts[-1] != close):
            raise ValueError('mouse trace lacks accepted cursor close')
        close_indices = [index for index, line in enumerate(lines) if line == close]
        if len(close_indices) != 1:
            raise ValueError('mouse trace cursor close missing or repeated')
        worker_events, captures = {}, {}
        for index, line in enumerate(lines):
            event = WORKER.fullmatch(line)
            if event:
                worker_events.setdefault((int(event[1]), event[2]), []).append(index)
            capture = CURSOR.fullmatch(line)
            if capture:
                captures.setdefault(int(capture[1]), []).append(index)
        parsed = []
        owner = None
        for index, (line_number, line) in enumerate(rows):
            match = ROW.fullmatch(line) if len(line) <= 1024 else None
            if not match:
                raise ValueError('malformed mouse input receipt')
            sample = {name: int(match[name], 16 if name in POINTER_FIELDS else 10) for name in FIELDS}
            tick = sample['tick']
            if (sample['index'] != index or sample['worker'] != worker or
                    not budget['capture'] <= tick <= budget['recurring_end'] or
                    (index == 0 and tick != budget['capture']) or
                    (parsed and tick <= parsed[-1]['tick'])):
                raise ValueError('mouse trace index, worker or tick mismatch')
            if (not 0x10000 <= sample['input'] <= 0x7fffffffffff or sample['input'] % 8 or
                    (owner is not None and owner != sample['input']) or
                    any(sample[name] > 0x7fffffffffff for name in POINTER_FIELDS) or
                    any(sample[name] > 255 for name in BYTE_FIELDS)):
                raise ValueError('mouse trace pointer, owner or byte bound mismatch')
            owner = sample['input']
            gate_present = sample['window_gate_present']
            context_present = sample['window_context_present']
            if (gate_present not in (0, 1) or context_present not in (0, 1) or
                    bool(sample['window_gate']) != bool(gate_present) or
                    bool(sample['window_context']) != bool(context_present) or
                    (not gate_present and sample['window_gate_value']) or
                    (not sample['window_owner'] and context_present) or
                    (not context_present and sample['window_modal'])):
                raise ValueError('mouse trace pointer presence mismatch')
            enter = worker_events.get((tick, 'enter'), [])
            release = worker_events.get((tick, 'release'), [])
            if tick == budget['recurring_end']:
                inside_tail = len(enter) == 1 and not release and enter[0] < line_number < close_indices[0]
            else:
                inside_tail = len(enter) == 1 and len(release) == 1 and enter[0] < line_number < release[0]
            if not inside_tail:
                raise ValueError('mouse trace outside authenticated worker tail')
            if tick <= budget['last_capture']:
                capture = captures.get(tick, [])
                if len(capture) != 1 or line_number >= capture[0]:
                    raise ValueError('mouse trace outside accepted cursor capture')
            if parsed and all(sample[name] == parsed[-1][name] for name in FIELDS[3:]):
                raise ValueError('mouse trace repeated an unchanged sample')
            parsed.append(sample)
        result.update(owner=owner, worker=worker, samples=parsed, performed=True)
        result['pass'] = True
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, AttributeError) as error:
        errors.append(str(error))
    return result
