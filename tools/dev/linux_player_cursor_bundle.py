"""Explicit finite active-flight cursor replay; no context, view, aim or fire support."""
import hashlib
from pathlib import Path
import re
import struct

import linux_stock_view_fov_observer as previous
from linux_ui_session_budget import session_budget

SOURCE_VERSION = 5
SOURCES = tuple('src/native/engine/linux/' + name for name in (
    'gate_player_cursor_replay.h', 'gate_player_input_transaction.h', 'player_cursor_convergence.h', 'player_cursor_preload.h',
    'player_cursor_preload_impl.h', 'player_cursor_preload_pin.h', 'player_cursor_bridge.S')) + (
    'tools/dev/linux_player_cursor_bundle.py', 'tools/dev/linux_player_network_evidence.py')
ACTIVE = 'X4 CURSOR PRELOAD ACTIVE version=1\n'
PREFIX = 'X4LAN CURSOR v=1 '
VECTOR = r'([0-9a-f]{8},[0-9a-f]{8},[0-9a-f]{8})'
CAPTURE = re.compile(PREFIX + r'stage=capture tick=([1-9][0-9]{0,19}) focused=([01]) seen=([13]) device=' + VECTOR + ' slot1=' + VECTOR + ' slot2=' + VECTOR + '\n')
TARGETS = re.compile(PREFIX + r'stage=targets tick=38 owner=([1-9][0-9]{0,9}) worker=([1-9][0-9]{0,9}) canonical=([0-9a-f]{1,16}) guest=([0-9a-f]{1,16}) focused=([01]) flight=1\n')


def enabled(options):
    value = options.get('player_cursor_active_flight', False)
    if type(value) is not bool:
        raise ValueError('player cursor active flight must be boolean')
    if value and (options.get('player_startup_phases') != 'v1-t40' or
                  options.get('player_view_query_observer') is not True or
                  type(options.get('player_ui_session_frames')) is not int or
                  not 180 <= options['player_ui_session_frames'] <= 900 or
                  options.get('independent_keyboard_frames') != options['player_ui_session_frames']):
        raise ValueError('player cursor requires finite T40 independent physics and sealed preload')
    return value


def environment(env, active):
    result = dict(env)
    result.pop('X4_DEV_PLAYER_CURSOR', None)
    result.pop('X4_DEV_PLAYER_FOCUS', None)
    if active:
        result.update(X4_DEV_PLAYER_CURSOR='1', X4_DEV_PLAYER_FOCUS='1')
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('cursor source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    evidence = manifest.get('player_cursor_active_flight_evidence_version')
    if (active and (type(evidence) is not int or evidence != 1)) or (not active and evidence is not None):
        raise ValueError('cursor evidence version mismatch')
    if type(manifest.get('view_query_source_version')) is not int or manifest['view_query_source_version'] != SOURCE_VERSION:
        if active:
            raise ValueError('cursor requires source version 5')
        return previous.validate_bundle(root, manifest, network)
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        if path.is_symlink() or not path.is_file() or manifest.get('files', {}).get(key) != hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError('missing or changed cursor source: ' + name)
    if active and network is not True:
        raise ValueError('cursor requires independent network physics')
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=previous.SOURCE_VERSION), network)


def vector_bytes(text):
    raw = b''.join(int(word, 16).to_bytes(4, 'little') for word in text.split(','))
    values = struct.unpack('<3f', raw)
    if any(not (-16 <= value <= 16) for value in values):
        raise ValueError('cursor vector is nonfinite or out of bounds')
    return raw


def collect(stderr, trace, active, physics, options):
    errors, hashes, data = [], {}, {}
    for key, path in (('stderr_sha256', stderr), ('log_sha256', trace)):
        try:
            raw = Path(path).read_bytes()
            hashes[key] = hashlib.sha256(raw).hexdigest()
            data[key] = raw.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if active or key == 'stderr_sha256':
                errors.append('cursor raw input unavailable: ' + str(error))
    lines = data.get('stderr_sha256', [])
    rows = [(index, row) for index, row in enumerate(lines) if 'X4LAN CURSOR' in row]
    activations = [index for index, row in enumerate(lines) if row == ACTIVE]
    result = dict(hashes, enabled=active, performed=False, errors=errors,
                  raw_receipts=[row for _, row in rows], supported_scope='active_flight_cursor_only',
                  context_view_aim_fire_supported=False, movement_proven=False)
    if not active:
        if rows or activations or any('CURSOR PRELOAD' in row for row in lines):
            errors.append('unexpected disabled cursor receipts')
        result['pass'] = not errors
        return result
    try:
        budget = session_budget(options['player_ui_session_frames'], options.get('player_startup_phases'))
        if physics.get('pass') is not True or any(physics.get(key) != value for key, value in hashes.items()) or len(hashes) != 2:
            raise ValueError('cursor lacks accepted hash-bound physics')
        if sum('CURSOR PRELOAD' in row for row in lines) != 1 or len(activations) != 1 or len(rows) != budget['frames'] + 2:
            raise ValueError('cursor activation or receipt count mismatch')
        targets = TARGETS.fullmatch(rows[0][1])
        if not targets or not activations[0] < rows[0][0]:
            raise ValueError('cursor targets missing or misordered')
        if lines[:rows[0][0]].count('X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=38 tail=1 event=enter\n') != 1:
            raise ValueError('cursor targets outside first capture worker tail')
        owner, worker, canonical, guest = int(targets[1]), int(targets[2]), int(targets[3], 16), int(targets[4], 16)
        if not 0 < owner <= 0xffffffff or not 0 < worker <= 0xffffffff or canonical == guest or any(not 0x10000 <= pointer <= 0x7fffffffffff or pointer % 8 for pointer in (canonical, guest)):
            raise ValueError('cursor target pointers invalid')
        frames = physics.get('frames', [])
        preapply = physics.get('preapply_frames', [])
        if [frame['tick'] for frame in frames] != list(range(40, budget['recurring_end'] + 1)):
            raise ValueError('cursor physics tick sequence mismatch')
        # Linux syscall TID authenticates preload ownership; the gate's
        # GetCurrentThreadId value is a separate compatibility identity.
        if any(frame['worker_thread'] != worker for frame in frames + preapply):
            raise ValueError('cursor worker owner mismatch')
        taken = next(frame['snapshots']['taken'] for frame in preapply if frame['tick'] == 37)
        if int(taken['controller'], 16) != guest:
            raise ValueError('cursor guest differs from accepted retained controller')
        records = physics['slot_records']
        if any(len(records[str(slot)]) != budget['frames'] for slot in (1, 2)):
            raise ValueError('cursor slot stream length mismatch')
        captures = []
        for offset, (index, row) in enumerate(rows[1:-1]):
            match = CAPTURE.fullmatch(row)
            tick = budget['capture'] + offset
            if not match or int(match[1]) != tick or int(match[3]) != (1 if tick < 40 else 3):
                raise ValueError('cursor capture tick/phase/seen mismatch')
            device, first, second = map(vector_bytes, match.group(4, 5, 6))
            if offset == 0 and match[2] != targets[5]:
                raise ValueError('cursor targets focus differs from first capture')
            if match[2] == '0' and any(device):
                raise ValueError('unfocused cursor must contain positive zero')
            expected_local = bytes.fromhex(records[str(physics['slot'])][offset])[4:16]
            if device != expected_local:
                raise ValueError('cursor device differs from delayed local wire capture')
            expected = [bytes(12), bytes(12)] if tick < 40 else [bytes.fromhex(records[str(slot)][tick-40])[4:16] for slot in (1, 2)]
            if [first, second] != expected:
                raise ValueError('cursor replay differs from accepted slot records')
            enter = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n'
            if lines[:index].count(enter) != 1 or any(line.startswith(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release') for line in lines[:index]):
                raise ValueError('cursor capture outside admitted worker tail')
            captures.append(dict(tick=tick, focused=int(match[2]), device=match[4], slot1=match[5], slot2=match[6]))
        close = PREFIX + f"stage=close tick={budget['recurring_end']} captured={budget['frames']} slot1={budget['frames']+2} slot2={budget['frames']} complete=1\n"
        if rows[-1][1] != close or f"X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={budget['recurring_end']} tail=1 event=enter\n" not in lines[:rows[-1][0]]:
            raise ValueError('cursor final close mismatch')
        if not data.get('log_sha256') or data['log_sha256'][-1] != 'X4LAN LIFECYCLE_DISCARD result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n':
            raise ValueError('cursor lacks final process discard')
        result.update(owner=owner, worker=worker, canonical=canonical, guest=guest, captures=captures)
    except (ValueError, KeyError, TypeError, StopIteration, IndexError) as error:
        errors.append(str(error))
    result.update(performed=not errors, **{'pass': not errors})
    return result
