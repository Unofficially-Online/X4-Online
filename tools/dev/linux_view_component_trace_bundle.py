"""Source inventory and activation for bounded component identity observation."""
import hashlib
from pathlib import Path
import re

import linux_player_cursor_bundle as previous

SOURCE_VERSION = 6
SOURCES = tuple('src/native/engine/linux/' + name for name in (
    'player_view_component_identity_trace.h', 'player_view_component_trace_preload.h',
    'player_view_component_trace_preload_impl.h', 'gate_player_view_component_trace.h',
    'player_stock_view_policy_guard.h', 'player_stock_view_component_preload.h',
    'player_stock_view_component_preload_impl.h', 'player_stock_view_component_preload_pin.h',
    'player_stock_view_component_bridge.S', 'player_stock_view_ship_preload_pin_core.h',
    'player_stock_view_shared_install.h', 'gate_player_space_native.h',
    'player_stock_space.h', 'player_space_context_vector.h',
)) + ('tools/dev/linux_view_component_trace_bundle.py', 'tools/dev/linux_view_component_trace_evidence.py')
HELPERS = ('linux_view_component_trace_bundle.py', 'linux_view_component_trace_evidence.py')


def enabled(options, ancestry_enabled):
    active = options.get('view_component_trace', False)
    if type(active) is not bool or (active and ancestry_enabled is not True):
        raise ValueError('component trace requires the VIEW ancestry observer')
    return active


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('component trace source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest, manifest.get('view_ancestry_observer', False))
    version = manifest.get('view_query_source_version')
    evidence = manifest.get('view_component_trace_evidence_version')
    if (active and (type(evidence) is not int or evidence != 1)) or (not active and evidence is not None):
        raise ValueError('component trace evidence version mismatch')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('component trace requires source version 6')
        return previous.validate_bundle(root, manifest, network)
    files = manifest.get('files')
    if not isinstance(files, dict):
        raise ValueError('invalid component trace source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        digest = files.get(key)
        if (not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest) or
                path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
            raise ValueError('missing or changed component trace source receipt: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = files.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or files.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed component trace helper: ' + name)
    if active and network is not True:
        raise ValueError('component trace requires independent network physics')
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=5), network)


def environment(env, active):
    if type(active) is not bool:
        raise ValueError('component trace activation must be boolean')
    result = dict(env)
    result.pop('X4_DEV_VIEW_COMPONENT_TRACE', None)
    if active:
        result['X4_DEV_VIEW_COMPONENT_TRACE'] = '1'
    return result
