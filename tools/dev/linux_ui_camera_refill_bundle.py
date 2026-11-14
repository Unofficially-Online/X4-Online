"""Versioned source closure and explicit guest-only camera refill activation."""
import hashlib
from pathlib import Path
import re

import linux_view_ancestry_bundle as ancestry
from linux_ui_session_budget import expected_callbacks

SOURCE_VERSION = 3
SOURCES = (
    'src/native/engine/linux/player_camera_refill_preload.h',
    'src/native/engine/linux/player_camera_refill_preload_impl.h',
    'src/native/engine/linux/player_camera_refill_preload_pin.h',
    'src/native/engine/linux/gate_player_stock_focus.h',
    'src/native/engine/linux/player_stock_focus.h',
    'tools/dev/linux_ui_camera_refill_bundle.py',
    'tools/dev/linux_ui_camera_refill_evidence.py',
)
HELPERS = ('linux_ui_camera_refill_bundle.py', 'linux_ui_camera_refill_evidence.py')


def validate_options(options):
    enabled = options.get('player_ui_camera_refill', False)
    if type(enabled) is not bool:
        raise ValueError('player_ui_camera_refill must be a boolean')
    required = ('player_ui_render_lease', 'player_ui_seat_correction',
                'player_ui_live_pose_observer', 'player_ui_seat_query_probe',
                'player_ui_overlay_probe', 'player_presentation_fork_probe',
                'player_presentation_probe', 'player_view_query_observer', 'ui_callback_observer')
    if enabled and (not all(options.get(key) is True for key in required) or
                    options.get('player_ui_overlay_callbacks') != expected_callbacks(options)):
        raise ValueError('camera refill requires the full explicit render/correction presentation chain')
    return enabled


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('camera refill source closure is incomplete')
    return paths


def validate_bundle(root, manifest, network_physics):
    """Validate the source generation and return ancestry activation."""
    enabled = validate_options(manifest)
    version = manifest.get('view_query_source_version')
    evidence_version = manifest.get('player_ui_camera_refill_evidence_version')
    if ((enabled and (type(evidence_version) is not int or evidence_version != 1)) or
            (not enabled and evidence_version is not None)):
        raise ValueError('camera refill evidence version does not match activation')
    if version != SOURCE_VERSION or type(version) is not int:
        if enabled:
            raise ValueError('camera refill requires source inventory version 3')
        return ancestry.validate_bundle(root, manifest, network_physics)
    files = manifest.get('files')
    if not isinstance(files, dict):
        raise ValueError('invalid camera source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        digest = files.get(key)
        if (not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != digest):
            raise ValueError('missing or changed camera source receipt: ' + name)
    # Version 3 extends the version-2 closure, without changing its checks.
    legacy_view = dict(manifest, view_query_source_version=2)
    ancestry_enabled = ancestry.validate_bundle(root, legacy_view, network_physics)
    if enabled and network_physics is not True:
        raise ValueError('camera refill requires disposable independent network physics')
    return ancestry_enabled


def environment(env, enabled, slot):
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        raise ValueError('invalid camera refill activation or slot')
    result = dict(env)
    result.pop('X4_DEV_UI_CAMERA_REFILL', None)
    if enabled and slot == 2:
        result['X4_DEV_UI_CAMERA_REFILL'] = '1'
    return result


def validate_config(config, enabled):
    rows = re.findall(r'^\s*X4LAN_CONFIG\.native_gate_linux_ui_camera_refill\s*=\s*(.*?)\s*$', config, re.M)
    if rows != (['true'] if enabled else []):
        raise ValueError('camera refill config differs from its frozen option')
