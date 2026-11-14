#!/usr/bin/env python3
"""Strict evidence for the bounded, disposable two-slot network-input probe.

Checks recorded state and wire agreement, not input causation or safe resumption.
"""
import math
import hashlib
import re
import struct
from pathlib import Path

from linux_player_frames_evidence import collect_recurring_physics_evidence, parse_recurring_physics_evidence
from linux_gate_evidence import is_fatal_gate_message

SLOT1 = re.compile(r'X4LAN KEYBOARD_SLOT1 tick=([0-9]+) capture=([0-9]+) saved=([0-9a-f]{16}) '
                   r'record=([0-9a-f]+) held_before=([0-9]+) held_after=([0-9]+)\n')
CAPTURE = re.compile(r'LINUX_KEYBOARD_CAPTURE tick=([0-9]+) record=([0-9a-f]+)\n')
SUPPORTED_STATES = frozenset((2, 3, 4, 5, 6, 7, 8, 9, 13, 14))
SUPPORTED_HELD_MASK = sum(1 << (event_id - 2) for event_id in SUPPORTED_STATES)


def decode_record(text, tick, held, *, scripted_throttle=False, allow_cursor=False, allow_space=False, allow_view=False):
    """Return the held mask after a canonical, supported version-3 record."""
    if type(allow_space) is not bool or (allow_space and allow_cursor is not True):
        raise ValueError('SPACE requires explicit cursor admission')
    if type(allow_view) is not bool or (allow_view and allow_cursor is not True):
        raise ValueError('VIEW requires explicit cursor admission')
    if type(held) is not int or held < 0 or held & ~SUPPORTED_HELD_MASK:
        raise ValueError('unsupported held-state mask')
    if not re.fullmatch(r'[0-9a-f]+', text) or len(text) % 2:
        raise ValueError('noncanonical record hex')
    data = bytes.fromhex(text)
    if len(data) < 43 or len(data) > 229 or data[0] != 3:
        raise ValueError('unsupported record header')
    # Strip VIEW only after explicit admission. The original ACTIVE/CURSOR/
    # SPACE policy remains unchanged for every combination of presence flags.
    flags = data[1] & ~8 if allow_view else data[1]
    allowed = (0, 1, 3) if allow_space else (1, 3)
    if (flags not in allowed if allow_cursor else flags != 0):
        raise ValueError('unsupported record header')
    if int.from_bytes(data[2:4], 'little') != ((tick - 2) & 0xffff):
        raise ValueError('capture tick mismatch')
    if any(data[16:29]) or (not allow_cursor and any(data[4:16])) or data[42] > 31 or len(data) != 43 + 6 * data[42]:
        raise ValueError('noncanonical empty fields or event length')
    if data[1] & 8:
        fov = struct.unpack('<f', data[30:34])[0]
        if data[29] > 3 or not math.isfinite(fov):
            raise ValueError('invalid VIEW bits or FOV')
        # bytes34..41 are the unchanged little-endian portable target uint64.
        # No native object lookup or truncating float conversion is performed.
    elif any(data[29:42]):
        raise ValueError('noncanonical absent VIEW fields')
    if allow_cursor:
        vector = struct.unpack('<3f', data[4:16])
        if any(not math.isfinite(value) or abs(value) > 16 for value in vector) or (not data[1] & 2 and any(data[4:16])):
            raise ValueError('unsupported cursor vector')
    for offset in range(43, len(data), 6):
        word = int.from_bytes(data[offset:offset + 2], 'little')
        kind, event_id = word >> 14, word & 0x3fff
        value = int.from_bytes(data[offset + 2:offset + 6], 'little')
        if allow_space and kind == 3 and event_id == 8 and value in (0, 1):
            continue
        if kind != 1 or event_id not in SUPPORTED_STATES or value not in (0, 1):
            raise ValueError('unsupported STATE event')
        if scripted_throttle and event_id in (13, 14):
            raise ValueError('scripted and network throttle cannot be combined')
        bit = 1 << (event_id - 2)
        held = held | bit if value else held & ~bit
    return held


def parse_network_physics_evidence(lines, stderr_lines, engine_lines, returncode, *, frame_count, slot, allow_camera=False, allow_throttle=False, throttle_start_tick=5, allow_presentation=False, session_frames=0, startup=None, allow_cursor=False, allow_space=False, allow_view=False, allow_semaphore_checkpoint=False, allow_worker_dispatch=False):
    result = parse_recurring_physics_evidence(lines, stderr_lines, returncode,
                                               frame_count=frame_count, input_mode='network', allow_camera=allow_camera and str(slot) == '2', allow_throttle=allow_throttle,
                                               throttle_start_tick=throttle_start_tick, presentation_slot=slot if allow_presentation else None, session_frames=session_frames, startup=startup, allow_semaphore_checkpoint=allow_semaphore_checkpoint, allow_worker_dispatch=allow_worker_dispatch)
    errors = result['errors']
    if type(allow_space) is not bool or (allow_space and not allow_cursor):
        errors.append('SPACE requires explicit cursor admission')
    if type(allow_cursor) is not bool or (allow_cursor and (startup != 'v1-t40' or not session_frames)):
        errors.append('cursor requires explicit finite T40 admission')
    if (type(allow_view) is not bool or (allow_view and
            (allow_cursor is not True or startup != 'v1-t40' or
             type(session_frames) is not int or not session_frames))):
        errors.append('VIEW requires explicit finite T40 cursor admission')
    from linux_player_startup_phases import startup_phases
    try:
        phase = startup_phases(startup)
    except ValueError as error:
        errors.append(str(error))
        result['pass'] = False
        return result
    if phase['version']:
        result['startup_phases'] = startup
        result['startup_phase_tuple'] = phase
        result['preapply_frames'] = [frame for frame in result['frames'] if frame['tick'] < phase['apply']]
        result['frames'] = [frame for frame in result['frames'] if frame['tick'] >= phase['apply']]
        if [frame['tick'] for frame in result['preapply_frames']] != [37, 38, 39]:
            errors.append('common startup requires all three distinct pre-apply frames')
    if type(allow_presentation) is not bool:
        errors.append('invalid presentation admission')
    markers = [line[line.index('LINUX_PRESENTATION_PROBE') :].strip()
               for line in engine_lines if 'LINUX_PRESENTATION_PROBE' in line]
    expected_markers = [f'LINUX_PRESENTATION_PROBE tick={phase["presentation_probe"]} slot={slot}'] if allow_presentation else []
    if markers != expected_markers:
        errors.append('presentation Lua marker missing, malformed, duplicated or disabled')
    records = {1: [], 2: []}
    maximum = (session_frames if type(session_frames) is int else 0) if session_frames else 600
    if type(frame_count) is not int or not 2 <= frame_count <= maximum:
        result.update(slot=slot, slot_records={'1': [], '2': []}, **{'pass': False})
        return result
    for line in engine_lines:
        if is_fatal_gate_message(line) or 'PLAYER_CONTROL_RECURRING_FATAL' in line or 'PLAYER_PHASES_FATAL' in line:
            errors.append('fatal engine record: ' + line.strip()[:300])
    if slot not in (1, 2) or type(slot) is not int:
        errors.append('slot must be 1 or 2')
    held = 0
    for frame in result['frames']:
        tick, rows = frame['tick'], frame['inputs']
        if len(rows) != 3 or [row['phase'] for row in rows] != ['before', 'applied', 'updated']:
            errors.append(f'frame {tick}: missing or reordered P2 network rows')
            continue
        try:
            after = decode_record(rows[0]['record'], tick, held, scripted_throttle=allow_throttle, allow_cursor=allow_cursor, allow_space=allow_space, allow_view=allow_view)
            if allow_throttle and tick in (throttle_start_tick, throttle_start_tick + 1) and bytes.fromhex(rows[0]['record'])[42] != 0:
                raise ValueError('scripted throttle requires empty P2 record')
            if any(row['tick'] != tick or row['record'] != rows[0]['record'] or
                   row['held_before'] != held or not math.isfinite(row['digital']) for row in rows):
                raise ValueError('P2 row identity or value mismatch')
            if rows[0]['held_after'] != held or any(row['held_after'] != after for row in rows[1:]):
                raise ValueError('P2 held transition mismatch')
            records[2].append(rows[0]['record'])
            held = after
        except ValueError as error:
            errors.append(f'frame {tick}: {error}')
    if held:
        errors.append('P2 final keys held')

    held, p1_ticks, saved = 0, [], None
    for line in stderr_lines:
        if 'KEYBOARD_SLOT1' not in line:
            continue
        match = SLOT1.fullmatch(line)
        if not match:
            errors.append('malformed P1 keyboard record')
            continue
        tick, capture, identity, record, before, after = match.groups()
        tick, capture, before, after = map(int, (tick, capture, before, after))
        p1_ticks.append(tick)
        try:
            if capture != ((tick - 2) & 0xffff) or before != held or not int(identity, 16):
                raise ValueError('P1 identity, capture or held-before mismatch')
            if saved is not None and identity != saved:
                raise ValueError('P1 saved identity changed')
            expected = decode_record(record, tick, held, scripted_throttle=allow_throttle, allow_cursor=allow_cursor, allow_space=allow_space, allow_view=allow_view)
            if after != expected:
                raise ValueError('P1 held-after mismatch')
            saved, held = identity, expected
            records[1].append(record)
        except ValueError as error:
            errors.append(f'frame {tick}: {error}')
    if p1_ticks != list(range(phase['apply'], phase['apply'] + frame_count)):
        errors.append('P1 records missing, duplicated, outside window or reordered')
    if held:
        errors.append('P1 final keys held')

    captures = []
    for line in engine_lines:
        if 'LINUX_KEYBOARD_CAPTURE' not in line:
            continue
        # The Lua logger can prefix its message; the complete record suffix is strict.
        match = CAPTURE.fullmatch(line[line.index('LINUX_KEYBOARD_CAPTURE'):])
        if not match:
            errors.append('malformed keyboard capture')
            continue
        captures.append((int(match[1]), match[2]))
    expected_ticks = list(range(phase['capture'], phase['capture'] + frame_count))
    if [tick for tick, _ in captures] != expected_ticks:
        errors.append('capture records missing, duplicated, outside window or reordered')
    if slot in (1, 2) and len(records[slot]) == frame_count and len(captures) == frame_count:
        if [record for _, record in captures] != records[slot]:
            errors.append('local capture does not match applied slot records')
    throttle_rows = [row for frame in result['frames'] for row in frame.get('throttle', [])]
    if allow_throttle:
        expected = [(tick, stage, keys) for tick, key_values in ((throttle_start_tick, ('00', '10', '10')),
                                                                  (throttle_start_tick + 1, ('10', '00', '00')))
                    for stage, keys in zip(('before', 'applied', 'updated'), key_values)]
        if [(row['tick'], row['stage'], row['keys']) for row in throttle_rows] != expected:
            errors.append('throttle stages, key sequence or release missing')
        if len(throttle_rows) == 6 and len(result['frames']) >= 2:
            first = throttle_rows[0]
            member_addresses = [member['object'] for member in first['members']]
            identity = (first['ship'], first['controller'], first['collection'], first['member_count'])
            if throttle_start_tick == 6 and (not first.get('factors') or any(
                    not row.get('factors') or row['factors']['table'] != first['factors']['table']
                    for row in throttle_rows)):
                errors.append('throttle factor table identity changed')
            if any((row['ship'], row['controller'], row['collection'], row['member_count']) != identity or
                   [member['object'] for member in row['members']] != member_addresses
                   for row in throttle_rows):
                errors.append('throttle target, collection or member identity changed')
            frame = result['frames'][0]
            taken = frame.get('snapshots', {}).get('taken', {})
            bodies = frame.get('bodies', [])
            if (not taken or int(taken['controller'], 16) != first['controller'] or
                    not bodies or bodies[0]['ship'] != first['ship']):
                errors.append('throttle target disagrees with lifecycle or body identity')
            for row in throttle_rows:
                for member in row['members']:
                    for field in ('field328', 'field2f0'):
                        value = struct.unpack('<f', bytes.fromhex(member[field])[::-1])[0]
                        if not math.isfinite(value):
                            errors.append('nonfinite throttle member field')
        result['throttle'] = {'stages': throttle_rows,
                              'component_coverage': 'observed' if throttle_rows and throttle_rows[0]['member_count'] else 'empty',
                              'movement_proven': False}
    else:
        result['throttle'] = None
    result['slot'] = slot
    result['slot_records'] = {str(k): v for k, v in records.items()}
    result['pass'] = not errors
    return result


def collect_network_physics_evidence(trace, retained, engine, output, returncode, *, frame_count, stderr, slot, allow_camera=False, allow_throttle=False, throttle_start_tick=5, allow_presentation=False, session_frames=0, startup=None, allow_cursor=False, allow_space=False, allow_view=False, allow_semaphore_checkpoint=False, allow_worker_dispatch=False):
    """Retain raw trace before parsing; include the complete generic failure checks."""
    base = collect_recurring_physics_evidence(trace, retained, engine, output, returncode,
                                             frame_count=frame_count, stderr=stderr, input_mode='network', allow_camera=allow_camera and str(slot) == '2', allow_throttle=allow_throttle,
                                               throttle_start_tick=throttle_start_tick, presentation_slot=slot if allow_presentation else None, session_frames=session_frames, startup=startup, allow_semaphore_checkpoint=allow_semaphore_checkpoint, allow_worker_dispatch=allow_worker_dispatch)
    try:
        if 'log_sha256' not in base:
            raise OSError('trace retention or parsing failed')
        lines = Path(retained).read_text(encoding='utf-8').splitlines(keepends=True)
        worker_raw = Path(stderr).read_bytes()
        worker = worker_raw.decode('utf-8').splitlines(keepends=True)
        engine_lines = Path(engine).read_text(encoding='utf-8').splitlines(keepends=True)
        result = parse_network_physics_evidence(lines, worker, engine_lines, returncode,
                                                frame_count=frame_count, slot=slot, allow_camera=allow_camera, allow_throttle=allow_throttle, throttle_start_tick=throttle_start_tick, allow_presentation=allow_presentation, session_frames=session_frames, startup=startup, allow_cursor=allow_cursor, allow_space=allow_space, allow_view=allow_view, allow_semaphore_checkpoint=allow_semaphore_checkpoint, allow_worker_dispatch=allow_worker_dispatch)
        for error in base['errors']:
            if error not in result['errors']:
                result['errors'].append(error)
        result.update(native_fd_path=base['native_fd_path'], retained_log=base['retained_log'])
        result['stderr_sha256'] = hashlib.sha256(worker_raw).hexdigest()
        if result['stderr_sha256'] != base.get('stderr_sha256'):
            result['errors'].append('worker stderr changed between physics evidence passes')
        if 'log_sha256' in base:
            result['log_sha256'] = base['log_sha256']
    except (OSError, UnicodeError) as error:
        result = base
        result['errors'].append('network evidence unavailable/invalid: ' + str(error))
        result['slot_records'] = {'1': [], '2': []}
        result['slot'] = slot
    result['pass'] = not result['errors']
    return result


def compare_throttle_peers(first, second):
    """Return errors for semantic differences; never compare process addresses."""
    errors = []
    if any(not isinstance(peer, dict) or peer.get('pass') is not True or
           not isinstance(peer.get('throttle'), dict) for peer in (first, second)):
        return ['throttle peer evidence missing or failed']
    left, right = first['throttle'], second['throttle']
    if (left.get('component_coverage') not in ('empty', 'observed') or
            right.get('component_coverage') not in ('empty', 'observed') or
            not isinstance(left.get('stages'), list) or not isinstance(right.get('stages'), list)):
        return ['throttle peer evidence malformed']
    if left['component_coverage'] != right['component_coverage']:
        errors.append('throttle component coverage differs between peers')
    if len(left['stages']) != 6 or len(right['stages']) != 6:
        return errors + ['throttle peer stages missing']
    keys = ('tick', 'stage', 'keys', 'command', 'lower', 'upper', 'original', 'member_count')
    member_keys = ('field328', 'field2f0', 'field2f4')
    for first_row, second_row in zip(left['stages'], right['stages']):
        if any(not isinstance(row, dict) or any(key not in row for key in keys) or
               not isinstance(row.get('members'), list) or
               any(not isinstance(member, dict) or any(key not in member for key in member_keys)
                   for member in row['members']) for row in (first_row, second_row)):
            return errors + ['throttle peer evidence malformed']
        first_factors, second_factors = first_row.get('factors'), second_row.get('factors')
        if ((first_factors is None) != (second_factors is None) or
                (first_factors is not None and first_factors.get('values') != second_factors.get('values'))):
            errors.append('throttle factor values differ between peers')
        if any(first_row[key] != second_row[key] for key in keys):
            errors.append('throttle stage values differ between peers')
        def fields(row):
            return sorted((member['field328'], member['field2f0'], member['field2f4'])
                          for member in row['members'])
        if fields(first_row) != fields(second_row):
            errors.append('throttle component values differ between peers')
    return errors
