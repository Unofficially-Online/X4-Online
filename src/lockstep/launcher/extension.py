"""Compose a client's x4_lan_lab extension from a gate overlay.

An overlay (tools/dev/build_native_gate_overlay.py and friends) holds ui/*.lua,
native/*.dll, md/*.xml, optional libraries/, a ui.xml.files list, a
ui/config.lua.append with @X4LANE_PORT@ / @X4LANE_TOKEN@ / @X4LANE_SLOT@ /
@X4LANE_CLIENTS@ placeholders, and an x4lane-manifest.json of SHA-256 sums. The
lockstep probe needs only X4LAN_CONFIG and X4LAN_SHA256 from a base, so the base
is src/lockstep/base (sha256.lua, the player-faction libraries and icons, the
x4lan_net DLL for live inputs) plus generated content.xml/ui.xml/config.lua.
"""
import hashlib
import json
from pathlib import Path
import shutil

EXTENSION_ID = 'x4_lan_lab'
BASE = Path(__file__).resolve().parents[1] / 'base'  # sha256.lua, player faction data, x4lan_net DLL
BASE_UI = ['ui/sha256.lua']
PLACEHOLDERS = ('@X4LANE_PORT@', '@X4LANE_TOKEN@', '@X4LANE_SLOT@', '@X4LANE_CLIENTS@')
CONTENT = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<content id="x4_lan_lab" name="X4 lockstep client" description="Disposable lockstep test client" '
           'author="X4 lockstep" version="100" date="2026-11-01" save="false">\n'
           '  <dependency version="900" />\n</content>\n')


def verify_overlay(overlay):
    overlay = Path(overlay)
    manifest_path = overlay / 'x4lane-manifest.json'
    if not manifest_path.is_file():
        raise SystemExit(f'{overlay} has no x4lane-manifest.json; not a built overlay')
    manifest = json.loads(manifest_path.read_text())
    actual = {str(p.relative_to(overlay)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in overlay.rglob('*') if p.is_file() and p.name != 'x4lane-manifest.json'}
    manifest = {k.replace('\\', '/'): v for k, v in manifest.items()}
    if actual != manifest:
        changed = sorted(set(actual.items()) ^ set(manifest.items()))
        raise SystemExit(f'overlay {overlay} differs from its manifest: {changed[:4]}')
    for required in ('ui.xml.files', 'ui/config.lua.append'):
        if required not in actual:
            raise SystemExit(f'overlay {overlay} lacks {required}')
    return manifest


def ui_xml(files):
    rows = ''.join(f'    <file name="{name}" />\n' for name in ['ui/config.lua', *files])
    return ('<?xml version="1.0" encoding="utf-8"?>\n<addon name="x4_lan_lab">\n  <environment type="menus">\n'
            f'    <dependency name="ego_detailmonitor" />\n{rows}  </environment>\n</addon>\n')


def compose(overlay, extensions, slot, port, token, clients, extra=None, config_tail=''):
    """Write <extensions>/x4_lan_lab for client `slot` (1-based).

    extra: {relative ui path: source file} loaded before the overlay's ui files;
    config_tail: Lua appended to ui/config.lua.
    """
    overlay = Path(overlay)
    verify_overlay(overlay)
    target = Path(extensions) / EXTENSION_ID
    if target.exists():
        raise SystemExit(f'{target} already exists')
    shutil.copytree(BASE, target)
    shutil.copytree(overlay, target, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('x4lane-manifest.json', 'ui.xml.files', 'config.lua.append'))
    files = [line.strip() for line in (overlay / 'ui.xml.files').read_text().splitlines() if line.strip()]
    missing = [name for name in files if not (target / name).is_file()]
    if missing:
        raise SystemExit(f'overlay lists missing ui files: {missing}')
    config = (overlay / 'ui/config.lua.append').read_text()
    for placeholder, value in zip(PLACEHOLDERS, (str(port), token, f'p{slot}', str(clients))):
        config = config.replace(placeholder, value)
    if '@X4LANE_' in config:
        raise SystemExit('overlay config has an unknown @X4LANE_ placeholder')
    for name, source in (extra or {}).items():
        shutil.copyfile(source, target / name)
    (target / 'content.xml').write_text(CONTENT)
    (target / 'ui.xml').write_text(ui_xml([*BASE_UI, *(extra or {}), *files]))
    (target / 'ui/config.lua').write_text('X4LAN_CONFIG = {enabled = false}\n' + config + config_tail)
    return target
