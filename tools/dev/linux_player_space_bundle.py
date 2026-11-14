"""Source closure and strict receipts for opt-in bounded SPACE context replay."""
import hashlib
from pathlib import Path
import re

import linux_view_component_trace_bundle as previous

SOURCE_VERSION = 7
SOURCES = tuple('src/native/engine/linux/' + name for name in (
    'gate_player_space_replay.h', 'gate_player_space_backend.h',
    'gate_player_space_native.h', 'player_stock_space.h', 'player_space_context_vector.h',
    'gate_stock_keyboard_replay.h', 'stock_keyboard_replay.h', 'gate_stock_keyboard_identity.h',
    'gate_player_cursor_replay.h', 'player_cursor_convergence.h',
    'gate_player_physics_network.h', 'gate_player_physics_replay.h',
    'gate_player_input_transaction.h', 'gate_linux.c',
)) + ('tools/dev/linux_player_space_bundle.py', 'tools/dev/linux_player_network_evidence.py',
      'tools/dev/linux_gate_live.py')
HELPERS = ('linux_player_space_bundle.py', 'linux_player_network_evidence.py')
LOCAL = re.compile(r'X4LAN SPACE v=1 stage=local tick=([0-9]+) worker=([0-9]+) owner=([0-9a-f]+) controller=([0-9a-f]+) active=([01]) cf0_before=([01])\n')
REPLAY = re.compile(r'X4LAN SPACE v=1 stage=replay tick=([0-9]+) worker=([0-9]+) slot=([12]) controller=([0-9a-f]+) active=([01]) flight=([01])\n')


def enabled(options):
    value = options.get('player_space_context', False)
    if type(value) is not bool:
        raise ValueError('SPACE context admission must be boolean')
    if value and (options.get('player_cursor_active_flight') is not True or
                  options.get('player_startup_phases') != 'v1-t40' or
                  type(options.get('player_ui_session_frames')) is not int or
                  options.get('independent_keyboard_frames') != options['player_ui_session_frames']):
        raise ValueError('SPACE context requires finite T40 independent cursor mode')
    return value


def environment(env, active):
    if type(active) is not bool:
        raise ValueError('SPACE activation must be boolean')
    result = dict(env)
    result.pop('X4_DEV_PLAYER_SPACE', None)
    if active:
        result['X4_DEV_PLAYER_SPACE'] = 'context8'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('SPACE source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    version = manifest.get('view_query_source_version')
    evidence = manifest.get('player_space_context_evidence_version')
    if (active and (type(evidence) is not int or evidence != 1)) or (not active and evidence is not None):
        raise ValueError('SPACE evidence version mismatch')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('SPACE requires source version 7')
        return previous.validate_bundle(root, manifest, network)
    files = manifest.get('files')
    if not isinstance(files, dict):
        raise ValueError('invalid SPACE source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        digest = files.get(key)
        if (not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest) or
                path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
            raise ValueError('missing or changed SPACE source receipt: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = files.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or files.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed SPACE helper: ' + name)
    if active and network is not True:
        raise ValueError('SPACE requires independent network physics')
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=6), network)


def record_edges(record, before):
    data = bytes.fromhex(record)
    if len(data) < 43 or len(data) != 43 + data[42] * 6 or before not in (0, 1):
        raise ValueError('invalid SPACE record')
    edges = []
    for offset in range(43, len(data), 6):
        word = int.from_bytes(data[offset:offset + 2], 'little')
        value = int.from_bytes(data[offset + 2:offset + 6], 'little')
        if word >> 14 == 3 and (word & 0x3fff) == 8:
            if value not in (0, 1) or value == before:
                raise ValueError('invalid or repeated SPACE edge')
            before = value
            edges.append(value)
    if bool(data[1] & 1) != bool(before) or (not before and data[1] & 2):
        raise ValueError('SPACE active flag does not match ordered edges')
    return before, edges


def collect(stderr, active, physics, cursor):
    errors = []
    try:
        raw = Path(stderr).read_bytes()
        lines = raw.decode('utf-8', errors='strict').splitlines(keepends=True)
        rows = [line for line in lines if 'X4LAN SPACE' in line]
        if not active:
            if rows:
                raise ValueError('unexpected disabled SPACE receipt')
            return {'pass': True, 'enabled': False, 'errors': []}
        if (physics.get('pass') is not True or cursor.get('pass') is not True or
                physics.get('stderr_sha256') != hashlib.sha256(raw).hexdigest()):
            raise ValueError('SPACE lacks hash-bound accepted physics and cursor')
        records = physics['slot_records']
        count = len(records['1'])
        if not count or len(records['2']) != count or physics['slot'] not in (1, 2):
            raise ValueError('SPACE slot streams invalid')
        expected = []
        edges = {1: [], 2: []}
        for slot in (1, 2):
            state = 1
            for record in records[str(slot)]:
                state, event_values = record_edges(record, state)
                edges[slot].append(event_values)
        for tick in range(38, 40 + count):
            if tick >= 40:
                for slot in (1, 2):
                    expected.extend((tick, 'replay', slot, value) for value in edges[slot][tick - 40])
            if tick < 38 + count:
                expected.extend((tick, 'local', 0, value) for value in edges[physics['slot']][tick - 38])
        observed = []
        owner = None
        for row in rows:
            match = LOCAL.fullmatch(row)
            if match:
                tick, worker, current_owner, controller, value, before = match.groups()
                current_owner = int(current_owner, 16)
                if (int(worker) != cursor['worker'] or int(controller, 16) != cursor['canonical'] or
                        not current_owner or (owner is not None and owner != current_owner) or value == before):
                    raise ValueError('SPACE local owner or readback mismatch')
                owner = current_owner
                observed.append((int(tick), 'local', 0, int(value)))
                continue
            match = REPLAY.fullmatch(row)
            if not match:
                raise ValueError('malformed SPACE receipt')
            tick, worker, slot, controller, value, flight = match.groups()
            target = cursor['canonical'] if slot == '1' else cursor['guest']
            if int(worker) != cursor['worker'] or int(controller, 16) != target or value != flight:
                raise ValueError('SPACE replay identity or flight readback mismatch')
            observed.append((int(tick), 'replay', int(slot), int(value)))
        if not any(stage == 'local' for _, stage, _, _ in expected):
            raise ValueError('SPACE proof requires at least one captured local edge and its delayed replay')
        if observed != expected:
            raise ValueError('SPACE native effects missing, duplicated, reordered or inconsistent with wire edges')
        return {'pass': True, 'enabled': True, 'errors': [], 'events': len(expected),
                'stderr_sha256': hashlib.sha256(raw).hexdigest(), 'raw_receipts': rows}
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, IndexError) as error:
        errors.append(str(error))
        return {'pass': False, 'enabled': bool(active), 'errors': errors}
