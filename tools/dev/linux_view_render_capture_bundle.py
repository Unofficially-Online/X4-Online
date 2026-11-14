"""Version 10 source closure for dormant native renderer VIEW capture."""
import hashlib
from pathlib import Path
import re
import math
import struct
from linux_ui_session_budget import budget_from_options
import linux_player_fire_observation_bundle as previous

SOURCE_VERSION = 10
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'gate_player_stock_view_capture.h', 'player_stock_view_capture.h',
        'gate_player_stock_view_ui_selection.h', 'player_stock_view_ui_selection.h',
        'gate_player_stock_view_control_proof.h', 'player_stock_view_control_proof.h',
        'player_stock_view_mailbox.h', 'player_stock_view_capture_handoff.h',
        'gate_player_view_render_capture.h', 'player_view_render_fov_observer.h',
        'player_view_render_fov_observer_impl.h', 'player_view_render_fov_observer_pin.h',
        'player_view_render_fov_sample.h', 'gate_player_ui_overlay_probe.h',
        'gate_stock_keyboard_replay.h', 'gate_linux.c', 'player_view_query_preload.c',
        'test_gate_player_view_render_capture.c', 'test_player_stock_view_capture_handoff.c',
        'test_player_view_render_fov_observer.c', 'test_player_view_render_fov_observer_pin.c',
        'test_player_view_render_fov_sample.c', 'test_player_stock_view_mailbox.c',
    )) + ('tools/dev/linux_view_render_capture_bundle.py', 'tools/dev/linux_gate_live.py',
          'tests/test_linux_view_render_capture_bundle.py', 'tests/test_linux_view_render_capture_native.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_view_render_capture_bundle.py',)))


def enabled(options):
    active = options.get('view_render_capture_shadow', False)
    if type(active) is not bool:
        raise ValueError('VIEW render capture shadow must be boolean')
    if active and (options.get('player_startup_phases') != 'v1-t40' or
        options.get('player_ui_render_lease') is not True or
        options.get('player_cursor_active_flight') is not True or
        options.get('player_view_query_observer') is not True or
        type(options.get('player_ui_session_frames')) is not int or
        options.get('independent_keyboard_frames') != options.get('player_ui_session_frames') or
        not budget_from_options(options)):
        raise ValueError('VIEW render shadow requires T40 independent cursor and guest render lease')
    return active


def environment(env, active, slot):
    if type(active) is not bool or slot not in (1, 2):
        raise ValueError('invalid VIEW render shadow environment')
    result = dict(env)
    result.pop('X4_DEV_RENDER_FOV_OBSERVE', None)
    result.pop('X4_DEV_RENDER_FOV_CAPTURE', None)
    if active and slot == 2:
        result.update(X4_DEV_RENDER_FOV_OBSERVE='1', X4_DEV_RENDER_FOV_CAPTURE='1')
    return result


def collect(stderr, physics, options, slot, active):
    errors = []
    try:
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        rows = [line for line in raw.decode('utf-8').splitlines() if 'X4 VIEW_RENDER_CAPTURE' in line]
        if (active or physics is not None) and (not isinstance(physics, dict) or
                physics.get('pass') is not True or physics.get('stderr_sha256') != digest):
            errors.append('stderr differs from accepted physics evidence')
        if not active or slot != 2:
            if rows: errors.append('unexpected VIEW render shadow receipts')
        else:
            budget = budget_from_options(options)
            if not budget: raise ValueError('missing session budget')
            first, last = budget['capture'], budget['last_capture']
            window_first, window_last = budget['first'] + 6, budget['last']
            pattern = re.compile(r'X4 VIEW_RENDER_CAPTURE v=1 tick=(0|[1-9][0-9]*) present=([01]) bits=([0-3]) fov=([0-9a-f]{8}) target=([0-9a-f]{16})')
            if len(rows) != last - first + 1: errors.append('VIEW render shadow receipt count mismatch')
            for tick, line in enumerate(rows, first):
                m = pattern.fullmatch(line)
                if not m:
                    errors.append('invalid VIEW render shadow receipt'); continue
                got, present, bits = map(int, m.group(1, 2, 3))
                fov = int(m.group(4), 16); target = int(m.group(5), 16)
                value = struct.unpack('<f', struct.pack('<I', fov))[0]
                expected = window_first <= tick <= window_last
                if got != tick or bool(present) != expected or (present and not math.isfinite(value)) or (
                        not present and (bits or fov or target)):
                    errors.append('VIEW render shadow tick, presence or value mismatch')
        return {'pass': not errors, 'errors': errors, 'stderr_sha256': digest,
                'receipt_count': len(rows), 'wire_enabled': options.get('player_view_wire') is True}
    except (OSError, UnicodeError, ValueError) as error:
        return {'pass': False, 'errors': [str(error)], 'wire_enabled': options.get('player_view_wire') is True}


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('VIEW render capture source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        if active: raise ValueError('VIEW render shadow requires source version 10')
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid VIEW render capture source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed VIEW render capture source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed VIEW render capture helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=9), network)
