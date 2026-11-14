"""Version 9 source closure for the observation-only primary-group FIRE probe.

Earlier generations retain their validators and immutable source inventories.
"""
import hashlib
from pathlib import Path
import re

import linux_input_observation_bundle as previous
import linux_player_fire_observation as observation

SOURCE_VERSION = 9
SOURCES = tuple(dict.fromkeys(previous.SOURCES + observation.SOURCES + (
    'src/native/engine/linux/player_stock_view_precompare_preload.h',
    'src/native/engine/linux/player_stock_view_precompare_preload_pin.h',
    'src/native/engine/linux/test_player_stock_view_precompare_preload.c',
    'src/native/engine/linux/test_player_view_ancestry_preload_pin.c',
    'tools/dev/linux_player_fire_observation_bundle.py',
    'tests/test_linux_player_fire_observation_bundle.py',
)))
HELPERS = tuple(dict.fromkeys(previous.HELPERS + observation.HELPERS + (
    'linux_player_fire_observation_bundle.py',
)))


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('FIRE observation source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = observation.enabled(manifest)
    evidence_version = manifest.get('player_fire_observation_evidence_version')
    if ((active and (type(evidence_version) is not int or evidence_version != 2)) or
            (not active and evidence_version is not None)):
        raise ValueError('FIRE observation evidence version mismatch')
    version = manifest.get('view_query_source_version')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('FIRE observation requires source version 9')
        return previous.validate_bundle(root, manifest, network)
    if active and network is not True:
        raise ValueError('FIRE observation requires matched independent network physics')
    inventory = manifest.get('files')
    if not isinstance(inventory, dict):
        raise ValueError('invalid FIRE observation source inventory')
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        expected = inventory.get(key)
        if (not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed FIRE observation source: ' + name)
    for name in HELPERS:
        path = Path(root) / name
        expected = inventory.get('source-receipts/tools/dev/' + name)
        if (path.is_symlink() or not path.is_file() or inventory.get(name) != expected or
                hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('missing or changed FIRE observation helper: ' + name)
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=8), network)
