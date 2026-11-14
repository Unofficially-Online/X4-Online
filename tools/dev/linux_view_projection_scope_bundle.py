"""Frozen source closure and bounded evidence for the guest projection lease."""
import hashlib
from pathlib import Path
import re

import linux_view_host_capture_bundle as previous
from linux_ui_session_budget import budget_from_options

SOURCE_VERSION = 12
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'player_projection_fov_scope_preload.h',
        'player_projection_fov_scope_preload_impl.h',
        'player_projection_fov_scope_preload_pin.h',
        'test_player_projection_fov_scope_preload.c', 'test_gate_ui_overlay_probe.c',
        'test_stock_keyboard_view.c',
    )) + ('tools/dev/linux_view_projection_scope_bundle.py',
          'tests/test_linux_projection_fov_scope.py', 'tests/test_linux_stock_keyboard_view.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_view_projection_scope_bundle.py',)))


def enabled(options):
    active = options.get('player_ui_projection_fov_scope', False)
    if type(active) is not bool:
        raise ValueError('projection FOV scope must be boolean')
    if active and (options.get('view_render_capture_shadow') is not True or
                   options.get('player_ui_camera_refill') is not True or
                   not budget_from_options(options)):
        raise ValueError('projection FOV scope requires guest shadow, camera refill and session')
    return active


def environment(env, active, slot):
    if type(active) is not bool or slot not in (1, 2):
        raise ValueError('invalid projection FOV scope environment')
    result = dict(env)
    result.pop('X4_DEV_UI_PROJECTION_FOV_SCOPE', None)
    if active and slot == 2:
        result['X4_DEV_UI_PROJECTION_FOV_SCOPE'] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('projection FOV scope source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('projection FOV scope requires source version 12')
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid projection FOV scope inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed projection FOV scope source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed projection FOV scope helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=11), network)


def collect_order(stderr, options, slot):
    """Validate only provisional raw ordering before physics proof exists."""
    return collect(stderr, None, options, slot, True, provisional=True)


def collect(stderr, physics, options, slot, active, *, provisional=False):
    errors = []
    try:
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        lines = raw.decode('utf-8').splitlines()
        positions = [index for index, line in enumerate(lines) if 'X4 UI PROJECTION_FOV' in line]
        rows = [lines[index] for index in positions]
        if not provisional and (active or physics is not None) and (not isinstance(physics, dict) or
                physics.get('pass') is not True or physics.get('stderr_sha256') != digest):
            errors.append('stderr differs from accepted physics evidence')
        if not active or slot != 2:
            if rows:
                errors.append('unexpected projection FOV receipts')
        else:
            budget = budget_from_options(options)
            if not budget:
                raise ValueError('missing session budget')
            first, last = budget['first'] + 1, budget['last']
            pattern = re.compile(r'X4 UI PROJECTION_FOV v=1 stage=frame tick=([0-9]+) ordinal=([0-9]+) '
                                 r'camera=([0-9a-f]+) bank=([01]) restored=1')
            if len(rows) != last - first + 2:
                errors.append('projection FOV receipt count mismatch')
            camera = None
            for ordinal, line in enumerate(rows[:-1], 1):
                match = pattern.fullmatch(line)
                if not match:
                    errors.append('invalid projection FOV frame')
                    continue
                tick, number = map(int, match.group(1, 2))
                current = int(match.group(3), 16)
                if tick != first + ordinal - 1 or number != ordinal or not current or (camera and camera != current):
                    errors.append('projection FOV frame identity or sequence mismatch')
                camera = current
                before_refill = [index for index, row in enumerate(lines)
                                 if row.startswith(f'X4 UI CAMERA_REFILL v=1 stage=frame tick={tick} ')]
                next_move = [index for index, row in enumerate(lines)
                             if row.startswith(f'X4 UI OVERLAY FRAME v=3 tick={tick} ')]
                worker_enter = [index for index, row in enumerate(lines)
                                if row == f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter']
                worker_release = [index for index, row in enumerate(lines)
                                  if row == f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release']
                point = positions[ordinal - 1]
                if (any(len(found) != 1 for found in (before_refill, next_move, worker_enter, worker_release)) or
                        not worker_enter[0] < before_refill[0] < point < next_move[0] < worker_release[0]):
                    errors.append('projection FOV frame outside refill, Move and worker order')
            close = f'X4 UI PROJECTION_FOV v=1 stage=close tick={last} count={last-first+1} restored=1'
            if not rows or rows[-1] != close:
                errors.append('projection FOV close mismatch')
            # Native tail disarms projection only after final Move/GameRender,
            # then closes pose/correction and finally the refill observer.
            # The physics terminal is in a different FD; stderr establishes
            # worker order, while the final accepted physics proof closes it.
            callback_count = budget['callbacks']
            markers = (
                lambda line: line.startswith(f'X4 UI PROJECTION_FOV v=1 stage=frame tick={last} '),
                lambda line: line.startswith(f'X4 UI OVERLAY FRAME v=3 tick={last} '),
                lambda line: line.startswith(f'X4 UI GAME_RENDER v=1 stage=frame tick={last} '),
                lambda line: line == f'X4 UI GAME_RENDER v=1 stage=close tick={last} count={callback_count} restored=1',
                lambda line: line == close,
                lambda line: line.startswith(f'X4 UI LIVE_POSE v=1 stage=close tick={last} '),
                lambda line: line.startswith(f'X4 UI POSE_SCOPE v=1 stage=close tick={last} '),
                lambda line: line.startswith(f'X4 UI SEAT_CORRECTION v=1 stage=close tick={last} '),
                lambda line: line == f'X4 UI CAMERA_REFILL v=1 stage=close tick={last} count={last-first+1} restored=1',
                lambda line: line == f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={last} tail=1 event=release',
                lambda line: line == f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={budget["recurring_end"]} tail=1 event=enter',
            )
            found = [[index for index, line in enumerate(lines) if predicate(line)] for predicate in markers]
            if (any(len(positions) != 1 for positions in found) or
                    any(left[0] >= right[0] for left, right in zip(found, found[1:]))):
                errors.append('projection FOV close outside final render, restore and physics worker order')
            if not provisional and isinstance(physics, dict):
                frames = physics.get('frames')
                if (physics.get('returncode') != 223 or physics.get('frame_count') != budget['frames'] or
                        not isinstance(frames, list) or not frames or
                        not isinstance(frames[-1], dict) or frames[-1].get('tick') != budget['recurring_end']):
                    errors.append('projection FOV missing accepted final physics closure')
        return {'pass': not errors, 'errors': errors, 'stderr_sha256': digest,
                'receipt_count': len(rows), 'wire_enabled': False,
                'physics_bound': not provisional and isinstance(physics, dict) and
                                 physics.get('pass') is True and physics.get('stderr_sha256') == digest}
    except (OSError, UnicodeError, ValueError) as error:
        return {'pass': False, 'errors': [str(error)], 'wire_enabled': False}
