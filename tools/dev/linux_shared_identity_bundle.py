"""Default-off shared identity layer, prepared before a Linux bundle is frozen.

The existing overlay builder defines the identical Linux/Windows plan. This
adapter freezes its sources and validates generated files; it never launches X4
or modifies an existing bundle. Runtime PIDENT agreement remains separate.
"""
import hashlib
import json
from pathlib import Path
import re

import native_gate_player_identity as identity
from linux_ui_session_budget import budget_from_options, resolve_session_options

TARGET = '0000000000175310'
ADAPTERS = ('ui/native_gate_player_identity_core.lua', 'ui/native_gate_player_identity.lua')
BUNDLE_HELPERS = ('linux_shared_identity_bundle.py', 'native_gate_player_identity.py')

SOURCES = (
    'tools/dev/linux_shared_identity_bundle.py',
    'tools/dev/build_shared_identity_overlay.py',
    'tools/dev/native_gate_player_identity.py',
    'tools/dev/native_gate_player_identity_core.lua',
    'tools/dev/native_gate_player_identity.lua',
    'tools/dev/native_gate_player_identity.xml',
    'tools/dev/native_gate_probe.lua',
    'src/lockstep/base/libraries/factions.xml',
)
RUNTIME_SOURCES = {
    'ui/native_gate_player_identity_core.lua': SOURCES[3],
    'ui/native_gate_player_identity.lua': SOURCES[4],
    'md/native_gate_player_identity.xml': SOURCES[5],
    'libraries/factions.xml': SOURCES[7],
}


def add_arguments(parser):
    parser.add_argument('--shared-identity-t40', action='store_true',
                        help='explicit identical Linux/Windows identity setup before common T40 startup')
    parser.add_argument('--shared-identity-p1', metavar='SAVED_ID',
                        help='explicit canonical P1 saved ship for --shared-identity-t40')


def validate_options(options):
    enabled = options.get('shared_identity_t40', False)
    if type(enabled) is not bool:
        raise ValueError('shared_identity_t40 must be a boolean')
    p1 = options.get('shared_identity_p1')
    if not enabled:
        if p1 is not None:
            raise ValueError('shared identity P1 requires the explicit T40 identity flag')
        return False
    resolve_session_options(options)
    budget = budget_from_options(options)
    if (options.get('player_startup_phases') != 'v1-t40' or budget is None or
            options.get('independent_keyboard_frames') != budget['frames']):
        raise ValueError('shared identity requires the explicit common finite network budget')
    if not identity.saved(p1) or p1 == TARGET:
        raise ValueError('shared identity requires a distinct explicit P1 saved ship')
    return True


def sources(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if not all(path.is_file() for path in paths):
        raise ValueError('shared identity source closure is incomplete')
    return paths


def prepare_overlay(repo, base, output, options):
    """Return the selected overlay and immutable manifest additions."""
    if not validate_options(options):
        return Path(base), {'shared_identity_t40': False}
    import build_shared_identity_overlay as builder
    if (builder.TARGET != TARGET or builder.ADAPTERS != ADAPTERS or
            Path(repo).resolve() != builder.ROOT.resolve()):
        raise ValueError('identity builder must belong to the selected source repository')
    if Path(output).resolve().is_relative_to(Path(base).resolve()):
        raise ValueError('identity output must be separate from its base overlay')
    sources(repo)
    frames = budget_from_options(options)['frames']
    p1 = options['shared_identity_p1']
    digest = builder.build(base, output, p1, TARGET, frames)
    metadata = {
        'shared_identity_t40': True,
        'shared_identity_p1': p1,
        'shared_identity_version': 1,
        'shared_identity_plan_sha256': digest,
    }
    return Path(output), metadata


def _assignment(config, key):
    return re.findall(r'^\s*X4LAN_CONFIG\.' + key + r'\s*=\s*(.*?)\s*$', config, re.M)


def validate_extension(extension, metadata, frames):
    """Validate composition, not actual MD execution or player ownership."""
    extension = Path(extension)
    config = (extension / 'ui/config.lua').read_text()
    if metadata.get('shared_identity_t40') is not True:
        if (_assignment(config, 'native_gate_shared_identity_t40') or
                (extension / 'shared-identity-profile.json').exists()):
            raise ValueError('unexpected shared identity artifacts while disabled')
        return
    p1 = metadata['shared_identity_p1']
    plan = json.loads((extension / 'player-identity-plan.json').read_text())
    expected = {'version': 1, 'mode': 'coop', 'tick': 20, 'occupied': 'player',
                'reputation': 'mirror', 'slots': [{'faction': 'player', 'ship': p1},
                                                {'faction': 'player', 'ship': TARGET}]}
    if plan != expected or identity.digest(identity.validate(plan)) != metadata['shared_identity_plan_sha256']:
        raise ValueError('composed shared identity plan or digest differs')
    profile = json.loads((extension / 'shared-identity-profile.json').read_text())
    if (type(profile.get('version')) is not int or profile['version'] != 1 or
            type(profile.get('physics_frames')) is not int or profile['physics_frames'] != frames or
            profile.get('plan_sha256') != metadata['shared_identity_plan_sha256']):
        raise ValueError('composed shared identity profile differs')
    expected_config = {
        'native_gate_player_identity': 'true',
        'native_gate_player_identity_plan': identity.lua_literal(plan),
        'native_gate_shared_identity_t40': identity.lua_literal([p1, TARGET, frames]),
    }
    if any(_assignment(config, key) != [value] for key, value in expected_config.items()):
        raise ValueError('composed shared identity configuration differs')
    registered = re.findall(r'<file\s+name="([^"]+)"\s*/>', (extension / 'ui.xml').read_text())
    scripts = [*ADAPTERS, 'ui/native_gate_probe.lua']
    if (any(registered.count(name) != 1 for name in scripts) or
            [registered.index(name) for name in scripts] != sorted(registered.index(name) for name in scripts)):
        raise ValueError('composed identity scripts are repeated or out of order')


def validate_bundle(root, manifest):
    """Authenticate this mode independently of the generic inventory check."""
    enabled = validate_options(manifest)
    root = Path(root)
    if not enabled:
        if any(manifest.get(key) is not None for key in
               ('shared_identity_version', 'shared_identity_plan_sha256')):
            raise ValueError('disabled shared identity carries enabled metadata')
        validate_extension(root / 'extension/x4_lan_lab', manifest, None)
        return False
    if type(manifest.get('shared_identity_version')) is not int or manifest['shared_identity_version'] != 1:
        raise ValueError('unsupported shared identity evidence version')
    if not re.fullmatch(r'[0-9a-f]{64}', str(manifest.get('shared_identity_plan_sha256'))):
        raise ValueError('invalid shared identity plan digest')
    files = manifest.get('files', {})
    required = ['source-receipts/' + name for name in SOURCES]
    required += ['extension/x4_lan_lab/' + name for name in
                 (*RUNTIME_SOURCES, 'player-identity-plan.json', 'shared-identity-profile.json',
                  'ui/config.lua', 'ui.xml')]
    for name in required:
        path = root / name
        if not path.is_file() or files.get(name) != hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError('shared identity receipt missing or mismatched: ' + name)
    for name in BUNDLE_HELPERS:
        path = root / name
        source = 'source-receipts/tools/dev/' + name
        if not path.is_file() or files.get(name) != files[source] or files[name] != hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError('shared identity helper receipt differs: ' + name)
    for runtime, source in RUNTIME_SOURCES.items():
        if files['extension/x4_lan_lab/' + runtime] != files['source-receipts/' + source]:
            raise ValueError('shared identity runtime and source receipt differ: ' + runtime)
    validate_extension(root / 'extension/x4_lan_lab', manifest, budget_from_options(manifest)['frames'])
    return True
