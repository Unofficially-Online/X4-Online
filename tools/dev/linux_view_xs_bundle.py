"""Additive version 15 closure for dormant XS view routing."""
import hashlib
from pathlib import Path
import re

import linux_view_component_policy_bundle as previous

SOURCE_VERSION = 15
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'player_view_xs_native_pin.h', 'player_view_xs_policy.h', 'player_view_xs_preload.h',
        'player_view_xs_preload_impl.h', 'player_view_xs_preload_pin.h', 'gate_player_view_xs_adapter.h',
        'gate_player_view_xs_readiness.h', 'player_view_rewrite_config.h',
        'test_player_view_xs_policy.c', 'test_player_view_xs_preload.c', 'test_player_view_xs_preload_pin.c',
        'test_gate_player_view_xs_adapter.c', 'test_player_view_rewrite_config.c',
    )) + ('tools/dev/linux_view_xs_bundle.py', 'tests/test_linux_view_xs_policy.py',
          'tests/test_linux_view_xs_bundle.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_view_xs_bundle.py',)))


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('XS VIEW source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid XS VIEW source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed XS VIEW source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed XS VIEW helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=14), network)
