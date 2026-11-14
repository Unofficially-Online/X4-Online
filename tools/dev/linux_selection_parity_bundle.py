"""Immutable version 13 closure for read-only local selection comparisons."""
import hashlib
from pathlib import Path
import re

import linux_view_projection_scope_bundle as previous
import linux_view_host_capture_bundle as host
import linux_view_render_capture_bundle as guest
from linux_ui_session_budget import budget_from_options

SOURCE_VERSION = 13
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'player_selection_parity.h', 'gate_player_selection_parity.h',
        'test_player_selection_parity.c', 'test_gate_player_selection_parity.c',
    )) + ('tools/dev/linux_selection_parity_bundle.py',
          'tests/test_linux_selection_parity.py', 'tests/test_linux_selection_parity_bundle.py',
          'tests/test_linux_ui_camera_refill_bundle.py',
          'reports/LINUX-SELECTION-PARITY-CONTRACT-20261113.txt')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_selection_parity_bundle.py',)))


def enabled(options):
    active = options.get('player_selection_parity', False)
    if type(active) is not bool:
        raise ValueError('selection parity must be boolean')
    if active:
        host_active, guest_active = host.enabled(options), guest.enabled(options)
        if not (host_active or guest_active):
            raise ValueError('selection parity requires an authenticated local VIEW capture shadow')
        if (options.get('player_startup_phases') != 'v1-t40' or
                type(options.get('independent_keyboard_frames')) is not int or
                options.get('independent_keyboard_frames') != options.get('player_ui_session_frames') or
                not budget_from_options(options)):
            raise ValueError('selection parity requires matched finite T40 independent physics')
    return active


def environment(env, active, slot, options):
    if type(active) is not bool or slot not in (1, 2) or active != enabled(options):
        raise ValueError('invalid selection parity environment')
    result = dict(env)
    result.pop('X4_DEV_SELECTION_PARITY', None)
    role = 'view_host_capture_shadow' if slot == 1 else 'view_render_capture_shadow'
    if active and options.get(role) is True:
        result['X4_DEV_SELECTION_PARITY'] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('selection parity source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    if active and (network is not True or
                   type(manifest.get('player_selection_parity_evidence_version')) is not int or
                   manifest.get('player_selection_parity_evidence_version') != 1):
        raise ValueError('selection parity requires network physics and evidence version 1')
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('selection parity requires source version 13')
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid selection parity inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed selection parity source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed selection parity helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=12), network)


IDENTITY = re.compile(
    r'X4 SELECTION_PARITY v=1 stage=identity tick=([0-9]+) slot=([12]) tid=([0-9]+) '
    r'local=([0-9a-f]+) controller=([0-9a-f]+) p1=([0-9a-f]+) '
    r'p1_runtime=([0-9a-f]{16}) p2=([0-9a-f]+) p2_runtime=([0-9a-f]{16})')
VALUE = re.compile(
    r'X4 SELECTION_PARITY v=1 tick=([0-9]+) slot=([12]) '
    r'source=(p1_player|p2_player|local_ui) '
    r'status=(absent|valid|unreadable|invalid|unavailable|drift) '
    r'at=([0-9a-f]+) raw=([0-9a-f]{16}) object=([0-9a-f]+) runtime=([0-9a-f]{16}) '
    r'mapping=(none|unavailable|saved|created|unmapped) saved=([0-9a-f]{16}) portable=([0-9a-f]{16})')


def collect(stderr, physics, options, slot, active):
    """Validate diagnostic completeness, never selection equivalence or replay."""
    errors = []
    try:
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        lines = raw.decode('utf-8').splitlines()
        positions = [i for i, line in enumerate(lines) if 'X4 SELECTION_PARITY' in line]
        rows = [lines[i] for i in positions]
        if (active or physics is not None) and (not isinstance(physics, dict) or
                physics.get('pass') is not True or physics.get('stderr_sha256') != digest):
            errors.append('stderr differs from accepted physics evidence')
        role = 'view_host_capture_shadow' if slot == 1 else 'view_render_capture_shadow'
        if not active or options.get(role) is not True:
            if rows:
                errors.append('unexpected selection parity receipts')
        else:
            if not enabled(options):
                raise ValueError('selection parity not admitted')
            budget = budget_from_options(options)
            first, last = budget['first'] + 6, budget['last']
            if len(rows) != 4 * (last - first + 1):
                errors.append('selection parity receipt count mismatch')
            identity = None
            for offset in range(0, len(rows), 4):
                group = rows[offset:offset + 4]
                match = IDENTITY.fullmatch(group[0])
                if not match or len(group) != 4:
                    errors.append('invalid selection parity identity group')
                    continue
                tick, got_slot, tid = map(int, match.group(1, 2, 3))
                local, controller, p1, runtime1, p2, runtime2 = (
                    int(value, 16) for value in match.group(4, 5, 6, 7, 8, 9))
                current = (tid, local, controller, p1, runtime1, p2, runtime2)
                if (tick != first + offset // 4 or got_slot != slot or not all(current) or
                        p1 == p2 or local != (p1 if slot == 1 else p2) or
                        (identity is not None and current != identity)):
                    errors.append('selection parity identity or sequence mismatch')
                identity = current
                for source, line in zip(('p1_player', 'p2_player', 'local_ui'), group[1:]):
                    value = VALUE.fullmatch(line)
                    if (not value or int(value.group(1)) != tick or int(value.group(2)) != slot or
                            value.group(3) != source):
                        errors.append('invalid selection parity source receipt')
                enter = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter'
                release = f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release'
                before = [i for i, line in enumerate(lines) if line == enter]
                after = [i for i, line in enumerate(lines) if line == release]
                if (len(before) != 1 or len(after) != 1 or
                        not before[0] < positions[offset] < positions[offset + 3] < after[0]):
                    errors.append('selection parity outside authenticated worker receipt order')
        return {'pass': not errors, 'errors': errors, 'stderr_sha256': digest,
                'receipt_count': len(rows), 'wire_enabled': False,
                'selection_equivalence_proved': False}
    except (OSError, UnicodeError, ValueError) as error:
        return {'pass': False, 'errors': [str(error)], 'wire_enabled': False}
