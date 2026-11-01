import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import extension, game, profile, session  # noqa: E402


def symlink_junction(target, link):
    if os.name == 'nt':
        game._junction(target, link)  # real junction: symlinks need a privilege on Windows
    else:
        os.symlink(target, link, target_is_directory=True)


def write_overlay(path, config='X4LAN_CONFIG.port = @X4LANE_PORT@\nX4LAN_CONFIG.token = "@X4LANE_TOKEN@"\n'
                                'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\nX4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n'):
    files = {'ui/native_gate_probe.lua': 'print(1)\n', 'native/x4lan_gate_64.dll': 'MZ', 'md/battle.xml': '<mdscript/>',
             'ui.xml.files': 'ui/native_gate_probe.lua\n', 'ui/config.lua.append': config}
    for name, text in files.items():
        (path / name).parent.mkdir(parents=True, exist_ok=True)
        (path / name).write_text(text)
    manifest = {name: hashlib.sha256((path / name).read_bytes()).hexdigest() for name in files}
    (path / 'x4lane-manifest.json').write_text(json.dumps(manifest))


class Extension(unittest.TestCase):
    def test_compose_substitutes_slot_port_token_clients_and_generates_base(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write_overlay(tmp / 'overlay')
            target = extension.compose(tmp / 'overlay', tmp / 'ext', 2, 50123, 'a' * 32, 3)
            config = (target / 'ui/config.lua').read_text()
            self.assertTrue(config.startswith('X4LAN_CONFIG = {enabled = false}\n'))
            for expected in ('port = 50123', 'token = "' + 'a' * 32 + '"', 'slot = "p2"', 'native_gate_clients = 3'):
                self.assertIn(expected, config)
            ui = (target / 'ui.xml').read_text()
            self.assertLess(ui.index('ui/config.lua'), ui.index('ui/native_gate_probe.lua'))
            self.assertIn('id="x4_lan_lab"', (target / 'content.xml').read_text())
            self.assertTrue((target / 'native/x4lan_gate_64.dll').is_file())
            for leftover in ('x4lane-manifest.json', 'ui.xml.files', 'ui/config.lua.append'):
                self.assertFalse((target / leftover).exists())

    def test_capture_overlay_without_fixedtick_gets_autostart_first(self):
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        from native_gate_scenarios import build_capture_overlay
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            overlay = build_capture_overlay('custom_creative', None, tmp / 'cap')
            self.assertFalse((overlay / 'native').exists() and any((overlay / 'native').iterdir()))
            autostart = ROOT / 'src/lockstep/launcher/autostart.lua'
            target = extension.compose(overlay, tmp / 'ext', 1, 0, '0' * 32, 1, extra={'ui/x4ls_autostart.lua': autostart},
                                       config_tail='X4LAN_CONFIG.x4ls_autostart = "custom_creative"\n')
            config = (target / 'ui/config.lua').read_text()
            self.assertIn('native_gate_snapshot_fixed = false', config)
            self.assertIn('x4ls_autostart = "custom_creative"', config)
            ui = (target / 'ui.xml').read_text()
            self.assertLess(ui.index('ui/x4ls_autostart.lua'), ui.index('ui/native_gate_snapshot.lua'))

    def test_tampered_overlay_and_unknown_placeholder_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write_overlay(tmp / 'o1')
            (tmp / 'o1/ui/native_gate_probe.lua').write_text('changed')
            with self.assertRaisesRegex(SystemExit, 'differs from its manifest'):
                extension.compose(tmp / 'o1', tmp / 'e1', 1, 1, 'a' * 32, 2)
            write_overlay(tmp / 'o2', config='x = "@X4LANE_OTHER@"\n')
            with self.assertRaisesRegex(SystemExit, 'unknown @X4LANE_'):
                extension.compose(tmp / 'o2', tmp / 'e2', 1, 1, 'a' * 32, 2)


class Profiles(unittest.TestCase):
    def test_only_disposable_ids_and_fresh_windowed_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, 'not a disposable'):
                profile.prepare(63389028, [], root=tmp)
            stale = Path(tmp) / '45850/save/autosave_01.xml.gz'
            stale.parent.mkdir(parents=True)
            stale.write_text('old world')
            snap = Path(tmp) / 'snap.xml.gz'
            snap.write_bytes(b'snapshot')
            path = profile.prepare(45850, ['ego_dlc_split', 'x4_lan_lab'], 800, 600, snap, root=tmp)
            self.assertFalse(stale.exists())
            config = (path / 'config.xml').read_text()
            for key in ('<autosave>false', '<uisafemode>false', '<fullscreen>false', '<res_width>800'):
                self.assertIn(key, config)
            self.assertEqual((path / 'save' / profile.SNAPSHOT_SLOT).read_bytes(), b'snapshot')
            self.assertIn('id="x4_lan_lab" enabled="true"', (path / 'content.xml').read_text())


class Views(unittest.TestCase):
    def test_view_links_game_and_removal_never_enters_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            install = tmp / 'game'
            (install / 'extensions/ego_dlc_split').mkdir(parents=True)
            (install / 'extensions/thirdparty_mod').mkdir()
            (install / 'aiscripts').mkdir()
            (install / 'aiscripts/keep.xml').write_text('k')
            (install / 'X4.exe').write_text('exe')
            view = game.create_view(install, tmp / 'lanes/c1', junction=symlink_junction)
            self.assertTrue(os.path.samefile(view / 'X4.exe', install / 'X4.exe'))
            self.assertEqual(game.dlc_ids(view), ['ego_dlc_split'])
            self.assertFalse((view / 'extensions/thirdparty_mod').exists())
            (view / 'extensions/x4_lan_lab').mkdir()
            game.remove_view(view)
            self.assertFalse(view.exists())
            self.assertEqual((install / 'aiscripts/keep.xml').read_text(), 'k')
            self.assertTrue((install / 'X4.exe').is_file())
            self.assertTrue((install / 'extensions/ego_dlc_split').is_dir())

    def test_removal_refuses_foreign_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, 'not an x4ls view'):
                game.remove_view(tmp)

    def test_library_paths_parse_escaped_vdf(self):
        vdf = '"libraryfolders"\n{\n "0" { "path" "C:\\\\Program Files (x86)\\\\Steam" }\n "1" { "path" "F:\\\\SteamLibrary" } }'
        self.assertEqual([str(p) for p in game.library_paths(vdf)], ['C:\\Program Files (x86)\\Steam', 'F:\\SteamLibrary'])

    def test_pin_matches_gate_source(self):
        gate = (ROOT / 'src/native/engine/lockstep_gate.c').read_text()
        self.assertIn(f'#define EXE_SHA "{game.EXE_SHA256}"', gate)
        self.assertIn(f'size.QuadPart!={game.EXE_SIZE}', gate)


class Session(unittest.TestCase):
    def test_relay_command_and_log_patterns(self):
        o = session.Options(clients=3, overlay=Path('o'), snapshot=Path('s.xml.gz'), ticks=600, tick_hz=30, observe=True)
        cmd = session.relay_command(o, 5000, 'b' * 32, Path('run'))
        self.assertEqual(cmd[1], str(session.RELAY))
        for expected in (['--clients', '3'], ['--ticks', '600'], ['--tick-hz', '30']):
            self.assertIn(' '.join(expected), ' '.join(cmd))
        for flag in ('--relative-real', '--observe', '--input-journal'):
            self.assertIn(flag, cmd)
        self.assertTrue(session.RELAY.is_file())
        self.assertTrue(session.STOP_OK.search('[X4LAN] NATIVE_GATE STOP tick=600 reason=1 restored=1'))
        self.assertIsNone(session.FAIL.search('[X4LAN] NATIVE_GATE STOP tick=600 reason=1 restored=1'))
        self.assertTrue(session.FAIL.search('[X4LAN] NATIVE_GATE STOP tick=20 reason=91 restored=1'))
        self.assertTrue(session.FAIL.search('[X4LAN] NATIVE_GATE REFUSED exe'))
        args = session.client_command(Path('v'), 45851, 2)
        self.assertEqual(args[1:3], ['-personalfolderid', '45851'])
        self.assertTrue(re.fullmatch(r'x4ls-c2\.log', args[-1]))


if __name__ == '__main__':
    unittest.main()
