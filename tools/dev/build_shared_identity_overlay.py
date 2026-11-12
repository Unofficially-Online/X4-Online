"""Add the explicit two-slot T40 identity setup to a verified Linux overlay.

This creates a new overlay only. The live runner must freeze it in a new bundle;
changing an already frozen bundle is not supported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import extension
import native_gate_player_identity as identity

TARGET = '0000000000175310'
ADAPTERS = ('ui/native_gate_player_identity_core.lua', 'ui/native_gate_player_identity.lua')


def build(base, output, p1, p2, frames):
    base, output = Path(base), Path(output)
    extension.verify_overlay(base)
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists')
    if not isinstance(frames, int) or isinstance(frames, bool) or not 180 <= frames <= 900:
        raise ValueError('invalid shared physics frame count')
    if not identity.saved(p1) or p2 != TARGET or p1 == p2:
        raise ValueError('invalid saved ships for the pinned Linux seat')
    if not (base / 'native/x4lan_gate.so').is_file():
        raise ValueError('base is not a Linux gate overlay')
    config = (base / 'ui/config.lua.append').read_text()
    def has_assignment(name, value):
        rows = re.findall(r'^\s*X4LAN_CONFIG\.' + name + r'\s*=\s*(.*?)\s*$', config, re.M)
        return len(rows) == 1 and re.fullmatch(value, rows[0]) is not None
    if not has_assignment('native_gate_hash', r'[\"\']battle[\"\']') or not has_assignment('native_gate_world_identities', 'true'):
        raise ValueError('identity setup requires battle snapshot and world identities')
    if not has_assignment('native_gate_linux_startup_phases', r'[\"\']v1-t40[\"\']'):
        raise ValueError('identity setup requires explicit common startup phases')
    if (not has_assignment('native_gate_linux_ui_session_frames', str(frames)) or
            not has_assignment('native_gate_lookahead', '0')):
        raise ValueError('Linux session frame count or lookahead differs')
    if re.search(r'^\s*X4LAN_CONFIG\.native_gate_(?:player_identity|shared_identity_t40)', config, re.M):
        raise ValueError('base already contains identity configuration')
    if not has_assignment('native_gate_snapshot_sha', r'[\"\'][0-9a-f]{64}[\"\']'):
        raise ValueError('base lacks an exact snapshot pin')
    keyboard_rows = re.findall(r'^\s*X4LAN_CONFIG\.native_gate_stock_keyboard_replay_probe\s*=\s*(.*?)\s*$', config, re.M)
    keyboard = keyboard_rows[0] if len(keyboard_rows) == 1 else None
    def keyboard_value(name):
        return re.findall(r'\b' + name + r'\s*=\s*([^,}\s]+)', keyboard or '')
    if (not keyboard or keyboard_value('last_capture') != [str(frames + 37)] or
            keyboard_value('independent_players') != ['true'] or
            keyboard_value('neutral_ship') not in [[f"'{TARGET}'"], [f'"{TARGET}"']]):
        raise ValueError('base lacks the matching independent P2 keyboard target')
    files = (base / 'ui.xml.files').read_text().splitlines()
    if files.count('ui/native_gate_probe.lua') != 1 or any(name in files for name in ADAPTERS):
        raise ValueError('base UI script order is not suitable')
    plan = {'version': 1, 'mode': 'coop', 'tick': 20, 'occupied': 'player', 'reputation': 'mirror',
            'slots': [{'faction': 'player', 'ship': p1}, {'faction': 'player', 'ship': p2}]}
    normalized = identity.validate(plan)
    digest = identity.digest(normalized)
    shutil.copytree(base, output, ignore=shutil.ignore_patterns('x4lane-manifest.json'))
    for name in ADAPTERS:
        shutil.copy2(ROOT / 'tools/dev' / Path(name).name, output / name)
    shutil.copy2(ROOT / 'tools/dev/native_gate_player_identity.xml', output / 'md/native_gate_player_identity.xml')
    (output / 'libraries').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'src/lockstep/base/libraries/factions.xml', output / 'libraries/factions.xml')
    index = files.index('ui/native_gate_probe.lua')
    files[index:index] = ADAPTERS
    (output / 'ui.xml.files').write_text('\n'.join(files) + '\n')
    lines = ['X4LAN_CONFIG.native_gate_player_identity = true',
             'X4LAN_CONFIG.native_gate_player_identity_plan = ' + identity.lua_literal(plan),
             'X4LAN_CONFIG.native_gate_shared_identity_t40 = ' + identity.lua_literal([p1, p2, frames])]
    (output / 'ui/config.lua.append').write_text(config.rstrip() + '\n' + '\n'.join(lines) + '\n')
    (output / 'player-identity-plan.json').write_text(json.dumps(plan, indent=2, sort_keys=True) + '\n')
    (output / 'shared-identity-profile.json').write_text(json.dumps({'version': 1, 'plan_sha256': digest, 'physics_frames': frames}, indent=2) + '\n')
    manifest = {path.relative_to(output).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(output.rglob('*')) if path.is_file() and path.name != 'x4lane-manifest.json'}
    (output / 'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    extension.verify_overlay(output)
    return digest


def build_windows(base, output, p1, p2, frames):
    """Bind the same bridge to an already configured stock Windows overlay."""
    base, output = Path(base), Path(output)
    extension.verify_overlay(base)
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists')
    if not isinstance(frames, int) or isinstance(frames, bool) or not 180 <= frames <= 900:
        raise ValueError('invalid shared physics frame count')
    if not identity.saved(p1) or p2 != TARGET or p1 == p2:
        raise ValueError('invalid saved ships for the pinned Linux seat')
    if not (base / 'native/x4lan_gate_64.dll').is_file():
        raise ValueError('base is not a Windows gate overlay')
    plan = json.loads((base / 'player-identity-plan.json').read_text())
    normalized = identity.validate(plan)
    if (normalized['mode'] != 'coop' or normalized['tick'] != 20 or
            normalized['occupied'] != 'player' or normalized['reputation'] != 'mirror' or
            normalized['relations'] or normalized['starts'] or normalized['kits'] or
            len(normalized['slots']) != 2 or
            [row.get('ship') for row in normalized['slots']] != [p1, p2] or
            any(row['faction'] != 'player' for row in normalized['slots'])):
        raise ValueError('Windows identity plan does not match the shared setup')
    config = (base / 'ui/config.lua.append').read_text()
    def exactly(name, pattern):
        rows = re.findall(r'^\s*X4LAN_CONFIG\.' + name + r'\s*=\s*(.*?)\s*$', config, re.M)
        return len(rows) == 1 and re.fullmatch(pattern, rows[0]) is not None
    if not exactly('native_gate_player_identity', 'true') or not exactly('native_gate_pilot_delay', '2') or not exactly('native_gate_lookahead', '0'):
        raise ValueError('Windows overlay needs explicit identity, delay 2 and lookahead 0')
    if (not exactly('native_gate_hash', r'["\']battle["\']') or
            not exactly('native_gate_world_identities', 'true') or
            not exactly('native_gate_snapshot_sha', r'["\'][0-9a-f]{64}["\']') or
            not exactly('native_gate_pilot_inputs', 'true') or
            not exactly('native_gate_pilot_stock', 'true') or
            not exactly('native_gate_pilot_source', r'["\']wire["\']') or
            not exactly('native_gate_pilot_stock_slots', r'["\']all["\']') or
            not exactly('native_gate_pilot_suppress_local', 'true')):
        raise ValueError('Windows overlay needs the explicit stock pilot and snapshot')
    slots = r'\{\s*["\']' + p1 + r'["\']\s*,\s*["\']' + p2 + r'["\']\s*\}'
    sources = r'\{\s*["\']stock["\']\s*,\s*["\']stock["\']\s*\}'
    if not exactly('native_gate_pilot_slots', slots) or not exactly('native_gate_pilot_capture_source', sources):
        raise ValueError('Windows pilot slots or capture sources differ')
    if not exactly('native_gate_player_identity_plan', re.escape(identity.lua_literal(plan))):
        raise ValueError('Windows plan config and saved plan differ')
    if not exactly('native_gate_pilot_window', r'\{\s*40\s*,\s*' + str(frames + 39) + r'\s*\}'):
        raise ValueError('Windows pilot window must start at tick 40')
    if re.search(r'^\s*X4LAN_CONFIG\.native_gate_shared_identity_t40\s*=', config, re.M):
        raise ValueError('Windows overlay already declares the bridge')
    files = (base / 'ui.xml.files').read_text().splitlines()
    if not all(files.count(name) == 1 for name in ADAPTERS) or files.count('ui/native_gate_probe.lua') != 1:
        raise ValueError('Windows identity adapters are missing or repeated')
    if not files.index(ADAPTERS[0]) < files.index(ADAPTERS[1]) < files.index('ui/native_gate_probe.lua'):
        raise ValueError('Windows identity adapters must load before the probe')
    if not (base / 'md/native_gate_player_identity.xml').is_file():
        raise ValueError('Windows identity MD is missing')
    shutil.copytree(base, output, ignore=shutil.ignore_patterns('x4lane-manifest.json'))
    for name in ADAPTERS:
        shutil.copy2(ROOT / 'tools/dev' / Path(name).name, output / name)
    shutil.copy2(ROOT / 'tools/dev/native_gate_player_identity.xml', output / 'md/native_gate_player_identity.xml')
    (output / 'libraries').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'src/lockstep/base/libraries/factions.xml', output / 'libraries/factions.xml')
    (output / 'ui/config.lua.append').write_text(config.rstrip() + '\nX4LAN_CONFIG.native_gate_shared_identity_t40 = ' +
                                                identity.lua_literal([p1, p2, frames]) + '\n')
    digest = identity.digest(normalized)
    (output / 'shared-identity-profile.json').write_text(json.dumps({'version': 1, 'plan_sha256': digest, 'physics_frames': frames}, indent=2) + '\n')
    manifest = {path.relative_to(output).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(output.rglob('*')) if path.is_file() and path.name != 'x4lane-manifest.json'}
    (output / 'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    extension.verify_overlay(output)
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('linux', 'windows'), required=True)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--p1', required=True, help='saved ID of the occupied snapshot ship')
    parser.add_argument('--p2', default=TARGET, help='saved ID of the pinned Linux target ship')
    parser.add_argument('--frames', type=int, required=True, help='number of P2 physics updates')
    args = parser.parse_args()
    builder = build if args.platform == 'linux' else build_windows
    print('identity plan:', builder(args.base, args.out, args.p1, args.p2, args.frames))


if __name__ == '__main__':
    main()
