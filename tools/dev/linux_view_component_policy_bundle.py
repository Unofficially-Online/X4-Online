"""Additive version 14 source closure for dormant component VIEW policy."""
import hashlib
from pathlib import Path
import re

import linux_selection_parity_bundle as previous

SOURCE_VERSION = 14
SOURCES = tuple(dict.fromkeys(previous.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'player_stock_view_component_policy.h', 'gate_player_stock_view_component_policy.h',
        'test_player_stock_view_component_policy.c', 'test_gate_player_stock_view_component_policy.c',
        'test_gate_player_stock_view_fov.c', 'test_player_stock_view_component_preload.c',
    )) + ('tools/dev/linux_view_component_policy_bundle.py',
          'tests/test_linux_view_component_policy_bundle.py',
          'tests/test_linux_stock_view_component_policy.py',
          'tests/test_linux_gate_player_stock_view_fov.py',
          'tests/test_linux_player_stock_view_component.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_view_component_policy_bundle.py',)))


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('component VIEW policy source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid component VIEW policy inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed component VIEW policy source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed component VIEW policy helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=13), network)
