"""Versioned source closure for read-only native input observations."""
import hashlib
from pathlib import Path
import re

import linux_player_space_bundle as previous
import linux_cursor_input_trace as mouse
import linux_view_precompare_bundle as precompare

SOURCE_VERSION = 8
SOURCES = tuple(dict.fromkeys(previous.SOURCES + mouse.SOURCES + precompare.SOURCES + tuple(
    'src/native/engine/linux/' + name for name in (
        'gate_player_cursor_input_trace.h', 'player_cursor_input_trace.h',
        'gate_player_cursor_replay.h', 'gate_player_view_component_trace.h',
        'player_view_ancestry_preload.h', 'player_view_ancestry_preload_impl.h',
        'player_view_ancestry_preload_pin.h', 'player_view_component_trace_preload.h',
        'player_view_component_trace_preload_impl.h', 'player_view_query_preload.c',
        'gate_linux.c',
    )) + ('tools/dev/linux_input_observation_bundle.py', 'tools/dev/linux_gate_live.py')))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + mouse.HELPERS + precompare.HELPERS +
                             ('linux_input_observation_bundle.py',)))


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('input observation source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    mouse_enabled = mouse.enabled(manifest)
    precompare_enabled = precompare.enabled(
        manifest, manifest.get('view_component_trace', False),
        manifest.get('view_ancestry_observer', False), network)
    for name, active in (('cursor_input_trace', mouse_enabled),
                         ('view_precompare_trace', precompare_enabled)):
        version = manifest.get(name + '_evidence_version')
        if (active and (type(version) is not int or version != 1)) or (not active and version is not None):
            raise ValueError(name + ' evidence version mismatch')
    source_version = manifest.get('view_query_source_version')
    if type(source_version) is not int or source_version != SOURCE_VERSION:
        if mouse_enabled or precompare_enabled:
            raise ValueError('input observations require source version 8')
        return previous.validate_bundle(root, manifest, network)
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid observation source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed observation source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed observation helper: ' + name)
    if (mouse_enabled or precompare_enabled) and network is not True:
        raise ValueError('input observations require independent network physics')
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=7), network)
