"""Bundle support for the optional, bounded VIEW ancestry observer.

Version 2 freezes the headers included by the current view-query preload even
when ancestry sampling is disabled. Missing version means a legacy bundle; a
legacy bundle can never enable this observer. This does not prove ownership of
unobserved calls or permit normal simulation resumption.
"""
import hashlib
from pathlib import Path
import re

from linux_view_ancestry_evidence import collect_view_ancestry_evidence

SOURCE_VERSION = 2
EXTRA_SOURCES = (
    'src/native/engine/linux/player_view_ancestry_preload.h',
    'src/native/engine/linux/player_view_ancestry_preload_impl.h',
    'src/native/engine/linux/player_view_ancestry_preload_pin.h',
    'src/native/engine/linux/gate_player_view_ancestry.h',
    'src/native/engine/linux/gate_world_classes.h',
    'tools/dev/linux_view_ancestry_evidence.py',
    'tools/dev/linux_view_ancestry_bundle.py',
)


def source_paths(repo):
    paths = [Path(repo) / name for name in EXTRA_SOURCES]
    if any(not path.is_file() or path.is_symlink() for path in paths):
        raise ValueError('VIEW ancestry source is missing or symlinked')
    return paths


def validate_options(options, network_physics):
    enabled = options.get('view_ancestry_observer', False)
    if type(enabled) is not bool:
        raise ValueError('view_ancestry_observer must be a boolean')
    if enabled and (network_physics is not True or
                    options.get('player_view_query_observer') is not True or
                    type(options.get('independent_keyboard_frames')) is not int or
                    options['independent_keyboard_frames'] < 4):
        raise ValueError('VIEW ancestry requires the sealed view-query preload and independent network physics')
    return enabled


def validate_bundle(root, manifest, network_physics):
    enabled = validate_options(manifest, network_physics)
    version = manifest.get('view_query_source_version')
    if version is None:
        if enabled:
            raise ValueError('legacy source inventory cannot enable VIEW ancestry')
        return False
    if type(version) is not int or version != SOURCE_VERSION:
        raise ValueError('unsupported view-query source inventory version')
    files = manifest.get('files')
    if not isinstance(files, dict):
        raise ValueError('invalid source inventory')
    # The gate includes ancestry headers unconditionally, so every new bundle
    # freezes them, including bundles with the preload and sampling disabled.
    for name in EXTRA_SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        digest = files.get(key)
        if (not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest) or
                path.is_symlink() or not path.is_file() or
                hashlib.sha256(path.read_bytes()).hexdigest() != digest):
            raise ValueError('missing or changed VIEW ancestry source receipt: ' + name)
    return enabled


def environment(env, enabled):
    if type(enabled) is not bool:
        raise ValueError('invalid VIEW ancestry activation')
    result = dict(env)
    # Never inherit a developer flag from the launching shell.
    result.pop('X4_DEV_VIEW_ANCESTRY', None)
    if enabled:
        result['X4_DEV_VIEW_ANCESTRY'] = '1'
    return result


def collect(profile, stderr, slot, enabled, last_tick, expected):
    """Bind receipts to identities supplied by already accepted evidence."""
    return collect_view_ancestry_evidence(Path(profile), Path(stderr), slot,
                                          enabled, last_tick, expected)
