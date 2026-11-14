"""Additive version 17 provenance for identity/cache and map-batch corrections.

Activation, constructor environment and wire evidence remain version 16 policy.
"""
import hashlib
from pathlib import Path
import re

import linux_player_view_wire_bundle as previous

SOURCE_VERSION = 17
EXTRA_SOURCES = (
    'src/native/engine/linux/gate_player_cursor_replay.h',
    'src/native/engine/linux/player_view_query_preload_pin.h',
    'src/native/engine/linux/player_view_pin_map_batch.h',
    'src/native/engine/linux/test_player_view_pin_map_batch.c',
    'src/native/engine/linux/test_gate_player_cursor_replay.c',
    'src/native/engine/linux/gate_world.h',
    'src/native/engine/linux/gate_player_seat_preflight.h',
    'src/native/engine/linux/test_gate_world.h',
    'src/native/engine/linux/test_gate_linux.c',
    'src/native/engine/linux/test_player_seat_preflight.c',
    'tests/test_linux_view_pin_map_batch.py',
    'tests/test_linux_player_seat_preflight.py',
    'tools/dev/linux_view_performance_bundle.py',
    'tests/test_linux_view_performance_bundle.py',
)
SOURCES = tuple(dict.fromkeys(previous.SOURCES + EXTRA_SOURCES))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + ('linux_view_performance_bundle.py',)))


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('VIEW performance source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid VIEW performance source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed VIEW performance source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed VIEW performance helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=16), network)
