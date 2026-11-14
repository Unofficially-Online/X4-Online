"""Version 16 closure and explicit bounded VIEW record handoff evidence."""
import hashlib
import math
from pathlib import Path
import re
import struct

import linux_view_xs_bundle as previous
from linux_ui_session_budget import budget_from_options
from linux_player_network_evidence import decode_record

SOURCE_VERSION = 16
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'player_stock_view_wire_handoff.h', 'gate_player_stock_view_wire_handoff.h',
        'gate_player_stock_view_wire_runtime.h', 'gate_player_input_transaction.h',
        'gate_player_physics_network.h', 'gate_player_context_policy_adapter.h',
        'player_context_policy.h', 'gate_stock_queue.h', 'stock_keyboard_replay.h',
        'test_player_stock_view_wire_handoff.c', 'test_gate_player_stock_view_wire_handoff.c',
        'test_gate_player_stock_view_wire_runtime.c', 'test_stock_keyboard_replay_adapter.c',
        'test_stock_keyboard_view.c', 'test_player_physics_network.c', 'test_player_context_policy.c',
        'test_player_context_policy_adapter.c', 'test_player_input_transaction.c',
    )) + ('tools/dev/linux_player_view_wire_bundle.py', 'tools/dev/linux_player_network_evidence.py',
          'tests/test_linux_player_view_wire_bundle.py', 'tests/test_linux_network_view_evidence.py',
          'tests/test_linux_view_wire_handoff.py', 'tests/test_linux_view_wire_runtime.py',
          'tests/test_linux_stock_keyboard_replay_adapter.py', 'tests/test_linux_player_physics_network.py',
          'tests/test_linux_cursor_convergence.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_player_view_wire_bundle.py',)))
NEW_ENV = ('X4_DEV_STOCK_VIEW_WIRE', 'X4_DEV_STOCK_VIEW_FOV', 'X4_DEV_STOCK_VIEW_SHARED',
           'X4_DEV_STOCK_VIEW_COMPONENT_POLICY', 'X4_DEV_VIEW_XS')
REQUIRED_ENV = ('X4MP_VIEW_QUERY_OBSERVER', 'X4_DEV_VIEW_ANCESTRY',
                'X4_DEV_VIEW_COMPONENT_TRACE', 'X4_DEV_VIEW_PRECOMPARE_TRACE',
                'X4_DEV_PLAYER_CURSOR', 'X4_DEV_PLAYER_FOCUS')
REQUIRED_OPTIONS = (
    'view_host_capture_shadow', 'view_render_capture_shadow', 'player_ui_projection_fov_scope',
    'player_selection_parity', 'player_cursor_active_flight', 'player_view_query_observer',
    'player_ui_seat_query_probe', 'player_ui_live_pose_observer', 'player_ui_seat_correction',
    'player_ui_render_lease', 'player_ui_camera_refill', 'view_ancestry_observer',
    'view_component_trace', 'view_precompare_trace',
)


def enabled(options):
    active = options.get('player_view_wire', False)
    if type(active) is not bool:
        raise ValueError('player VIEW wire must be boolean')
    if active:
        budget = budget_from_options(options)
        if (options.get('stock_view_fov_observer', False) is not False or
                options.get('player_startup_phases') != 'v1-t40' or not budget or
                type(options.get('independent_keyboard_frames')) is not int or
                options.get('independent_keyboard_frames') != budget['frames'] or
                not all(options.get(name) is True for name in REQUIRED_OPTIONS)):
            raise ValueError('VIEW wire requires rewrite mode and the complete finite T40 capture/consumer chain')
    return active


def environment(env, active, options):
    """Run last: explicit rewrite may never inherit or coexist with observe."""
    if type(active) is not bool or active != enabled(options):
        raise ValueError('invalid VIEW wire environment')
    result = dict(env)
    for name in NEW_ENV:
        result.pop(name, None)
    if active:
        if (any(result.get(name) != '1' for name in REQUIRED_ENV) or
                result.get('X4_DEV_PLAYER_PHASES') != 'v1-t40' or
                result.get('X4_DEV_UI_SESSION_FRAMES') != str(options['player_ui_session_frames'])):
            raise ValueError('VIEW wire constructor prerequisites are absent or inconsistent')
        result.pop('X4_DEV_STOCK_VIEW_FOV_OBSERVE', None)
        result.update({name: '1' for name in NEW_ENV})
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('VIEW wire source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    evidence = manifest.get('player_view_wire_evidence_version')
    if ((active and (type(evidence) is not int or evidence != 1 or network is not True)) or
            (not active and evidence is not None)):
        raise ValueError('VIEW wire evidence version or network mode mismatch')
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('VIEW wire requires source version 16')
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid VIEW wire inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed VIEW wire source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed VIEW wire helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=15), network)


CAPTURE = re.compile(r'X4 VIEW_(HOST|RENDER)_CAPTURE v=1 tick=([0-9]+) present=([01]) '
                     r'bits=([0-3]) fov=([0-9a-f]{8}) target=([0-9a-f]{16})')
ENGINE_CAPTURE = re.compile(r'LINUX_KEYBOARD_CAPTURE tick=([0-9]+) record=([0-9a-f]+)')


def collect(run_dir, clients, options, active):
    """Prove bytes/epochs from accepted raw physics, not native XS reachability."""
    out = {'pass': False, 'errors': [], 'wire_enabled': active,
           'wire_records_proved': False, 'cockpit_only': True, 'xs_calls_proved': False,
           'rendered_output_proved': False, 'raw_hashes': {}}
    errors = out['errors']
    try:
        if type(active) is not bool or active != enabled(options):
            raise ValueError('invalid VIEW wire collection mode')
        if not active:
            out['pass'] = True
            return out
        if not isinstance(clients, dict) or set(clients) != {'1', '2'}:
            raise ValueError('VIEW wire requires both accepted physics clients')
        budget = budget_from_options(options)
        canonical = None
        for slot in (1, 2):
            proof = clients[str(slot)]
            stderr = Path(run_dir) / f'game{slot}-stderr.log'
            raw = stderr.read_bytes()
            digest = hashlib.sha256(raw).hexdigest()
            if (not isinstance(proof, dict) or proof.get('pass') is not True or
                    proof.get('stderr_sha256') != digest or proof.get('returncode') != 223 or
                    proof.get('frame_count') != budget['frames']):
                raise ValueError(f'client {slot}: missing accepted same-raw physics')
            frames = proof.get('frames')
            if (not isinstance(frames, list) or not frames or
                    frames[-1].get('tick') != budget['recurring_end']):
                raise ValueError(f'client {slot}: missing final physics frame')
            trace = Path(proof['native_fd_path'])
            lifecycle = trace.read_bytes()
            if hashlib.sha256(lifecycle).hexdigest() != proof.get('log_sha256'):
                raise ValueError(f'client {slot}: lifecycle bytes differ from accepted physics')
            engine_path = trace.parent / 'engine-output.log'
            engine = engine_path.read_bytes()
            streams = proof.get('slot_records')
            if (not isinstance(streams, dict) or set(streams) != {'1', '2'} or
                    any(not isinstance(stream, list) or len(stream) != budget['frames'] for stream in streams.values())):
                raise ValueError(f'client {slot}: missing exact two-slot records')
            for stream in streams.values():
                held = 0
                for index, record in enumerate(stream):
                    held = decode_record(record, budget['apply'] + index, held,
                        allow_cursor=True, allow_space=options.get('player_space_context', False), allow_view=True)
                if held:
                    raise ValueError(f'client {slot}: VIEW stream ends with held keys')
            if canonical is not None and streams != canonical:
                errors.append('peer applied record streams differ')
            canonical = streams
            lines = raw.decode('utf-8').splitlines()
            captures = [(position, line) for position, line in enumerate(lines)
                        if 'X4 VIEW_HOST_CAPTURE' in line or 'X4 VIEW_RENDER_CAPTURE' in line]
            if len(captures) != budget['frames']:
                raise ValueError(f'client {slot}: missing local VIEW capture records')
            expected_role = 'HOST' if slot == 1 else 'RENDER'
            own = streams[str(slot)]
            for index, (position, line) in enumerate(captures):
                match = CAPTURE.fullmatch(line)
                if not match or match[1] != expected_role:
                    raise ValueError(f'client {slot}: invalid local VIEW capture')
                tick, present, bits = map(int, match.group(2, 3, 4))
                fov_bits, target = int(match[5], 16), int(match[6], 16)
                enter = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter'
                release = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release'
                entered = [i for i, row in enumerate(lines) if row == enter]
                released = [i for i, row in enumerate(lines) if row == release]
                if len(entered) != 1 or len(released) != 1 or not entered[0] < position < released[0]:
                    errors.append(f'client {slot}: local VIEW capture outside its worker generation')
                expected_tick = budget['capture'] + index
                required = budget['first'] + 6 <= expected_tick <= budget['last']
                fov = struct.unpack('<f', struct.pack('<I', fov_bits))[0]
                record = bytes.fromhex(own[index])
                if (tick != expected_tick or bool(present) != required or (present and bits != 1) or not math.isfinite(fov) or
                        len(record) < 43 or int.from_bytes(record[2:4], 'little') != (tick & 0xffff) or
                        bool(record[1] & 8) != bool(present) or record[29] != bits or
                        int.from_bytes(record[30:34], 'little') != fov_bits or
                        int.from_bytes(record[34:42], 'little') != target or
                        (not present and (bits or fov_bits or target))):
                    errors.append(f'client {slot}: capture/wire VIEW mismatch at capture {tick}, apply {tick+2}')
            exported = []
            for line in engine.decode('utf-8').splitlines():
                if 'LINUX_KEYBOARD_CAPTURE' not in line:
                    continue
                match = ENGINE_CAPTURE.fullmatch(line[line.index('LINUX_KEYBOARD_CAPTURE'):])
                if not match:
                    raise ValueError(f'client {slot}: malformed exported keyboard capture')
                exported.append((int(match[1]), match[2]))
            if exported != list(zip(range(budget['capture'], budget['last_capture'] + 1), own)):
                errors.append(f'client {slot}: raw local capture does not equal applied own echo')
            out['raw_hashes'][str(slot)] = {'stderr_sha256': digest,
                'lifecycle_sha256': hashlib.sha256(lifecycle).hexdigest(),
                'engine_sha256': hashlib.sha256(engine).hexdigest()}
            if stderr.read_bytes() != raw or trace.read_bytes() != lifecycle or engine_path.read_bytes() != engine:
                errors.append(f'client {slot}: raw data changed during VIEW collection')
        out['pass'] = not errors
        out['wire_records_proved'] = not errors
        return out
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, AttributeError) as error:
        errors.append(str(error))
        return out
