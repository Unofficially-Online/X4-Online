"""tools/make_playtest_kit.py: the kit's files, its command lines and the manifest check, on a small fake overlay."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('make_playtest_kit', ROOT / 'tools' / 'make_playtest_kit.py')
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)


def fake_overlay(folder, players=2, opened=()):
    (folder / 'native').mkdir(parents=True)
    (folder / 'native' / 'x4lan_gate_64.dll').write_bytes(b'MZ fake gate')
    (folder / 'ui').mkdir()
    (folder / 'ui' / 'config.lua.append').write_text('X4LAN_CONFIG.slot = "p1"\n')
    (folder / 'play-recipe.json').write_text(json.dumps({'players': players, 'mode': 'coop', 'minutes': 40, 'open': list(opened)}))


class Kit(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.overlay, self.snapshot = self.base / 'ov', self.base / 'save_009.xml.gz'
        self.snapshot.write_bytes(b'\x1f\x8b save')
        self.export = kit.export_repo
        kit.export_repo = lambda out, root=None: ((out / 'tools').mkdir(parents=True),
                                                  (out / 'tools' / 'x4ls.py').write_text('print(1)\n'), 'c0ffee' * 6 + 'abcd')[-1]

    def tearDown(self):
        kit.export_repo = self.export
        self.tmp.cleanup()

    def test_two_pc_kit(self):
        fake_overlay(self.overlay)
        out = self.base / 'kit'
        manifest = kit.build(self.overlay, self.snapshot, out)
        self.assertEqual((manifest['players'], manifest['open'], manifest['port']), (2, [], 47000))
        for name in ('HOST.cmd', 'JOIN.cmd', 'SAME-PC.cmd', 'VERIFY.cmd', 'KIT.txt', 'overlay/native/x4lan_gate_64.dll',
                     'overlay/play-recipe.json', 'snapshot/save_009.xml.gz', 'repo/tools/x4ls.py'):
            self.assertIn(name, manifest['files'], name)
        self.assertNotIn('HOTJOIN.cmd', manifest['files'])
        self.assertIn('x4ls.py lobby --overlay overlay --snapshot snapshot\\save_009.xml.gz --port 47000 %*',
                      (out / 'PLAY.cmd').read_text())
        host = (out / 'HOST.cmd').read_text()
        self.assertIn('x4ls.py play --clients 2 --overlay overlay --snapshot snapshot\\save_009.xml.gz --remote-slots 2 '
                      '--bind %1 --allow-from %2 --port 47000', host)
        self.assertIn('cd /d "%~dp0"', host)
        join = (out / 'JOIN.cmd').read_text()
        self.assertIn('x4ls.py join --relay %1:47000 --token %2 --slot %SLOT% --clients 2 --arm-at %3 --overlay overlay '
                      '--snapshot snapshot\\save_009.xml.gz', join)
        self.assertNotIn('--remote-slots', (out / 'SAME-PC.cmd').read_text())
        text = (out / 'KIT.txt').read_text()
        self.assertIn('checkout   ' + 'c0ffee' * 6 + 'abcd', text)
        self.assertIn(manifest['gate_dll_sha256'], text)
        self.assertEqual(kit.verify(out)[1], [])

    def test_verify_names_what_differs(self):
        fake_overlay(self.overlay)
        out = self.base / 'kit'
        kit.build(self.overlay, self.snapshot, out)
        (out / 'overlay' / 'native' / 'x4lan_gate_64.dll').write_bytes(b'MZ another gate')
        (out / 'snapshot' / 'save_009.xml.gz').unlink()
        (out / 'stray.txt').write_text('x')
        (out / 'repo' / '.local' / 'runs').mkdir(parents=True)
        (out / 'repo' / '.local' / 'runs' / 'relay.log').write_text('a run made after the copy')   # not the kit's
        self.assertEqual(sorted(kit.verify(out)[1]), ['changed overlay/native/x4lan_gate_64.dll', 'extra stray.txt',
                                                      'missing snapshot/save_009.xml.gz'])
        self.assertEqual(kit.main(['--verify', str(out)]), 1)

    def test_open_slots_and_three_players(self):
        fake_overlay(self.overlay, players=3, opened=(3,))
        out = self.base / 'kit'
        manifest = kit.build(self.overlay, self.snapshot, out, port=47010)
        self.assertEqual(manifest['open'], [3])
        host = (out / 'HOST.cmd').read_text()
        self.assertIn('--clients 3', host)
        self.assertIn('--open 3 --remote-slots 2 --bind %1 --allow-from %2 --port 47010', host)
        self.assertIn('x4ls.py join --relay %1:47010 --token %2 --slot %SLOT% --overlay overlay', (out / 'HOTJOIN.cmd').read_text())
        self.assertIn('set SLOT=3', (out / 'HOTJOIN.cmd').read_text())
        self.assertFalse((out / 'PLAY.cmd').exists())   # the lobby takes an overlay without open slots

    def test_a_kit_with_a_server(self):
        fake_overlay(self.overlay)
        out = self.base / 'kit'
        manifest = kit.build(self.overlay, self.snapshot, out, server=kit.parse_server('203.0.113.7:47010'))
        self.assertEqual((manifest['server'], manifest['port']), ('203.0.113.7:47010', 47010))
        self.assertEqual(sorted(p.name for p in out.iterdir() if p.is_file()),
                         ['KIT.txt', 'PLAY.cmd', 'VERIFY.cmd', 'helper.pyw', 'kit-manifest.json', 'server.json'])
        # Play in Steam: the helper's start file and the menu extension it puts into X4
        self.assertIn("steamplay.main(KIT, 'snapshot/save_009.xml.gz')", (out / 'helper.pyw').read_text())
        compile((out / 'helper.pyw').read_text(), 'helper.pyw', 'exec')
        self.assertEqual(sorted(p.relative_to(out / 'steam-menu').as_posix() for p in (out / 'steam-menu').rglob('*') if p.is_file()),
                         ['content.xml', 'ui.xml', 'ui/config.lua', 'ui/native_gate_lobby.lua'])
        self.assertIn('steam-menu/ui/native_gate_lobby.lua', manifest['files'])
        self.assertIn('1. Press Play in Steam.', (out / 'KIT.txt').read_text())
        self.assertIn('x4ls.py lobby --overlay overlay --snapshot snapshot' + chr(92) + 'save_009.xml.gz --server-file server.json --close-when-over %*',
                      (out / 'PLAY.cmd').read_text())
        server = json.loads((out / 'server.json').read_text())
        self.assertEqual((server['host'], server['port'], len(server['key'])), ('203.0.113.7', 47010, 64))
        text = (out / 'KIT.txt').read_text()
        self.assertIn('server     203.0.113.7: UDP 47010 (the session) and TCP 47011 (the lobby)', text)
        self.assertIn('2. Choose Join.', text)
        self.assertNotIn('HOST.cmd', text)
        self.assertNotIn(server['key'], text)
        self.assertEqual(kit.verify(out)[1], [])
        other = kit.build(self.overlay, self.snapshot, self.base / 'kit-b', server=('203.0.113.7', 47010))
        self.assertNotEqual(json.loads((self.base / 'kit-b' / 'server.json').read_text())['key'], server['key'])   # a key per kit
        self.assertEqual(other['server'], '203.0.113.7:47010')
        fake_overlay(self.base / 'ov-open', players=3, opened=(3,))
        with self.assertRaises(SystemExit):
            kit.build(self.base / 'ov-open', self.snapshot, self.base / 'kit-c', server=('203.0.113.7', 47010))

    def test_refusals(self):
        fake_overlay(self.overlay)
        out = self.base / 'kit'
        out.mkdir()
        (out / 'old').write_text('x')
        with self.assertRaises(SystemExit):
            kit.build(self.overlay, self.snapshot, out)            # never over another kit
        with self.assertRaises(SystemExit):
            kit.build(self.overlay, self.base / 'none.xml.gz', self.base / 'kit2')
        (self.overlay / 'play-recipe.json').unlink()
        with self.assertRaises(SystemExit):
            kit.build(self.overlay, self.snapshot, self.base / 'kit3')   # not a play overlay


if __name__ == '__main__':
    unittest.main()
