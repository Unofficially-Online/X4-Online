"""Version 11 inventory and bounded evidence for dormant host VIEW shadow."""
import hashlib
import math
from pathlib import Path
import re
import struct

import linux_view_render_capture_bundle as previous
from linux_ui_session_budget import budget_from_options

SOURCE_VERSION = 11
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'player_stock_view_local_control_proof.h', 'gate_player_stock_view_host_capture.h',
        'gate_player_view_host_capture_runtime.h', 'player_stock_view_fov_preload.h',
        'player_stock_view_fov_preload_impl.h', 'player_stock_view_fov_preload_pin.h',
        'test_player_stock_view_control_proof.c', 'test_gate_player_stock_view_ui_selection.c',
        'test_player_stock_view_local_control_proof.c', 'test_gate_player_stock_view_host_capture.c',
        'test_gate_player_view_host_capture_runtime.c', 'test_player_stock_view_fov_preload.c',
        'test_player_stock_view_fov_preload_pin.c',
    )) + ('tools/dev/linux_view_host_capture_bundle.py', 'tests/test_linux_view_host_capture.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_view_host_capture_bundle.py',)))


def enabled(options):
    active = options.get('view_host_capture_shadow', False)
    if type(active) is not bool:
        raise ValueError('host VIEW shadow must be boolean')
    observer = options.get('stock_view_fov_observer', False)
    wire = options.get('player_view_wire', False)
    if active and (type(observer) is not bool or type(wire) is not bool or observer == wire):
        raise ValueError('host VIEW capture requires exactly one observer or VIEW wire mode')
    if active and (options.get('player_startup_phases') != 'v1-t40' or
                   options.get('player_cursor_active_flight') is not True or
                   options.get('player_view_query_observer') is not True or
                   type(options.get('player_ui_session_frames')) is not int or
                   options.get('independent_keyboard_frames') != options.get('player_ui_session_frames') or
                   not budget_from_options(options)):
        raise ValueError('host VIEW shadow requires T40 independent cursor and stock FOV observer')
    return active


def environment(env, active, slot):
    if type(active) is not bool or slot not in (1, 2):
        raise ValueError('invalid host VIEW environment')
    result = dict(env)
    result.pop('X4_DEV_HOST_VIEW_CAPTURE', None)
    if active and slot == 1:
        result['X4_DEV_HOST_VIEW_CAPTURE'] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('host VIEW shadow source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('host VIEW shadow requires source version 11')
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid host VIEW shadow inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed host VIEW shadow source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed host VIEW shadow helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=10), network)


def collect(stderr, physics, options, slot, active):
    errors = []
    try:
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        rows = [line for line in raw.decode('utf-8').splitlines() if 'X4 VIEW_HOST_CAPTURE' in line]
        if (active or physics is not None) and (not isinstance(physics, dict) or
                physics.get('pass') is not True or physics.get('stderr_sha256') != digest):
            errors.append('stderr differs from accepted physics evidence')
        if not active or slot != 1:
            if rows:
                errors.append('unexpected host VIEW shadow receipts')
        else:
            budget = budget_from_options(options)
            if not budget:
                raise ValueError('missing session budget')
            first, last = budget['capture'], budget['last_capture']
            window_first, window_last = budget['first'] + 6, budget['last']
            pattern = re.compile(r'X4 VIEW_HOST_CAPTURE v=1 tick=(0|[1-9][0-9]*) '
                                 r'present=([01]) bits=([0-3]) fov=([0-9a-f]{8}) target=([0-9a-f]{16})')
            if len(rows) != last - first + 1:
                errors.append('host VIEW shadow receipt count mismatch')
            for tick, line in enumerate(rows, first):
                match = pattern.fullmatch(line)
                if not match:
                    errors.append('invalid host VIEW shadow receipt')
                    continue
                captured_tick, present, bits = map(int, match.group(1, 2, 3))
                fov_bits = int(match.group(4), 16)
                target = int(match.group(5), 16)
                fov = struct.unpack('<f', struct.pack('<I', fov_bits))[0]
                expected = window_first <= tick <= window_last
                if (captured_tick != tick or bool(present) != expected or
                        (present and not math.isfinite(fov)) or
                        (not present and (bits or fov_bits or target))):
                    errors.append('host VIEW shadow tick, presence or value mismatch')
        return {'pass': not errors, 'errors': errors, 'stderr_sha256': digest,
                'receipt_count': len(rows), 'wire_enabled': options.get('player_view_wire') is True}
    except (OSError, UnicodeError, ValueError) as error:
        return {'pass': False, 'errors': [str(error)], 'wire_enabled': options.get('player_view_wire') is True}
