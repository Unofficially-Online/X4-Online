"""tools/make_release_kit.py --sessions / --prebuild-sessions and the installer: a release kit with ready-made host
sessions (so that hosting a game of that many players needs no new session, and X4 restarts once). What a shipped
session holds (its files and overlay, no capture or base overlay, no path of the PC that made it), that it is the kit's
own (gate DLL, probe), the manifest and VERIFY, the private-file rules, and that the installer installs exactly the
shipped sessions. A dummy gate DLL, a small stand-in for the repository export; x4ls new-session is a fake here (it
starts X4)."""
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import extension, netkit  # noqa: E402
import netplay_fakes as fakes  # noqa: E402

GATE = b'MZ a dummy gate DLL for the sessions test'
BUILT = '2026-11-25T20:00:00Z'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release_kit = load('make_release_kit')
msi = load('make_playtest_msi')
try:
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', DeprecationWarning)
        import msilib
except ImportError:   # Python 3.13 or not Windows
    msilib = None


class Sessions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.gate = self.base / 'gate.dll'
        self.gate.write_bytes(GATE)
        self.saved = release_kit.playtest_kit.export_repo, release_kit.write_steam_menu
        release_kit.playtest_kit.export_repo = self.fake_export
        release_kit.write_steam_menu = self.fake_menu
        self.probe = '-- the probe\n'

    def tearDown(self):
        release_kit.playtest_kit.export_repo, release_kit.write_steam_menu = self.saved
        self.tmp.cleanup()

    def fake_export(self, out, root=None):
        (out / 'tools' / 'dev').mkdir(parents=True)
        (out / 'tools' / 'x4ls.py').write_text('print(1)\n')
        (out / 'tools' / 'dev' / 'native_gate_probe.lua').write_text(self.probe)   # what the overlay holds a copy of
        return 'c0ffee' * 6 + 'abcd'

    def fake_menu(self, kit):
        (kit / 'steam-menu').mkdir()
        (kit / 'steam-menu' / 'content.xml').write_text('<content id="x4online_menu" />\n')

    def made_sessions(self, name='made', dll=GATE, players=(2, 3)):
        """sessions/ of a PC that made host sessions with this gate DLL (new-session's folders, paths of that PC)."""
        kit = fakes.release_kit(self.base / name, dll=dll)
        for count in players:
            fakes.host_session(kit, count, work_path=r'C:\Users\someone\AppData\Local\X4 Online\sessions\work\session-save.xml.gz')
        (kit / 'sessions' / 'joined-0123456789abcdef-0123456789ab').mkdir()   # a joined game: not shipped
        (kit / 'sessions' / 'joined-0123456789abcdef-0123456789ab' / 'session.json').write_text('{}')
        return kit / 'sessions'

    def build(self, out='kit', **options):
        return release_kit.build(self.gate, self.base / out, master='https://master.example.org/x4mp', built=BUILT,
                                 **options)

    def test_ready_made_sessions_ship_without_anything_of_the_pc_that_made_them(self):
        manifest = self.build(sessions=self.made_sessions())
        kit = self.base / 'kit'
        dll8 = netkit.sha256_file(self.gate)[:8]
        names = [f'host-{netkit.HOST_SESSION_START}-{n}p-coop-{dll8}-{netkit.HOST_SESSION_FORM}' for n in (2, 3)]
        self.assertEqual(manifest['sessions'], names)
        self.assertEqual(sorted(path.name for path in (kit / 'sessions').iterdir()), names)
        shipped = kit / 'sessions' / names[0]
        self.assertEqual(sorted(path.name for path in shipped.iterdir()),
                         ['overlay', 'session-save.xml.gz', 'session.json', 'spec.json'])   # no capture or base overlay
        for path in (shipped / 'session.json', shipped / 'overlay' / 'session.json', shipped / 'overlay' / 'play-recipe.json'):
            text = path.read_text(encoding='utf-8')
            self.assertNotIn('someone', text)
            self.assertNotIn(':\\', text)
        self.assertEqual(json.loads((shipped / 'session.json').read_text())['snapshot']['path'], 'session-save.xml.gz')
        extension.verify_overlay(shipped / 'overlay')                      # the overlay's manifest follows the rewrite
        files = netkit.cached_session(netkit.Kit(kit), 2)                  # the launcher takes it as it is
        self.assertEqual(files.players, 2)
        self.assertTrue(all(path in manifest['files'] for path in
                            (f'sessions/{names[1]}/session-save.xml.gz', f'sessions/{names[1]}/overlay/play-recipe.json')))
        self.assertIn('2 and 3 players', (kit / 'KIT.txt').read_text())
        _, problems = release_kit.verify(kit)
        self.assertEqual(problems, [])
        # what the PC makes while playing is not the kit's; a shipped session that changed is
        (kit / 'sessions' / 'host-x4ep1_gamestart_discover-5p-coop-00000000-n').mkdir()
        (kit / 'sessions' / 'host-x4ep1_gamestart_discover-5p-coop-00000000-n' / 'session.json').write_text('{}')
        self.assertEqual(release_kit.verify(kit)[1], [])
        (shipped / 'spec.json').write_text('{}')
        self.assertEqual(release_kit.verify(kit)[1], [f'changed sessions/{names[0]}/spec.json'])

    def test_sessions_of_another_gate_or_probe_are_refused(self):
        with self.assertRaises(SystemExit) as refused:
            self.build(out='a', sessions=self.made_sessions('other-gate', dll=b'MZ another gate'))
        self.assertIn('not this kit', str(refused.exception))
        self.assertFalse((self.base / 'a').exists())                         # nothing stays
        sessions = self.made_sessions('other-probe')
        self.probe = '-- another probe\n'
        for folder in sessions.glob('host-*'):
            (folder / 'overlay' / 'ui' / 'native_gate_probe.lua').write_text('-- the probe of another checkout\n')
            fakes.overlay_manifest(folder / 'overlay')
        with self.assertRaises(SystemExit) as refused:
            self.build(out='b', sessions=sessions)
        self.assertIn("overlay/ui/native_gate_probe.lua is not this kit's", str(refused.exception))

    def test_a_private_file_in_a_session_refuses_the_kit(self):
        sessions = self.made_sessions('logged', players=(2,))
        folder = next(sessions.glob('host-*'))
        (folder / 'overlay' / 'x4ls-c1.log').write_text('a log of the PC that made it')
        with self.assertRaises(SystemExit) as refused:
            self.build(out='c', sessions=sessions)
        self.assertIn('x4ls-c1.log', str(refused.exception))
        # each rule on its own
        kit = self.base / 'rules'
        kit.mkdir()
        rows = {'sessions/joined-0123/session.json': 'not a ready-made host session',
                f'sessions/{folder.name}/notes.txt': 'not a file of a ready-made session',
                f'sessions/{folder.name}/overlay/saves/a.lua': 'saves'}
        for relative, why in rows.items():
            (kit / relative).parent.mkdir(parents=True, exist_ok=True)
            (kit / relative).write_text('x')
            self.assertIn(why, release_kit.shipped_session_reason(kit, relative))
        path = kit / 'sessions' / folder.name / 'session.json'
        path.write_text(json.dumps({'snapshot': {'path': 'D:\\Games\\X4\\save.xml.gz'}}))
        self.assertIn('a path of the PC', release_kit.shipped_session_reason(kit, f'sessions/{folder.name}/session.json'))
        save = kit / 'sessions' / folder.name / 'session-save.xml.gz'
        save.write_bytes(fakes.session_save(2, tail=b'<x path="C:\\Users\\someone\\Documents"/>'))
        self.assertIn('a path of the PC', release_kit.shipped_session_reason(kit, f'sessions/{folder.name}/session-save.xml.gz'))
        save.write_bytes(fakes.session_save(2))
        self.assertIsNone(release_kit.shipped_session_reason(kit, f'sessions/{folder.name}/session-save.xml.gz'))
        # the kit's own code names pipes and Linux paths (the probe, 25 Nov): not a path of a PC; other code is read
        code = "local PIPE = '\\\\\\\\.\\\\pipe\\\\' -- and /home/<user>/.steam on Linux\n"
        (kit / 'repo' / 'tools' / 'dev').mkdir(parents=True)
        (kit / 'repo' / 'tools' / 'dev' / 'native_gate_probe.lua').write_text(code)
        for name in ('native_gate_probe.lua', 'generated.lua'):
            (kit / 'sessions' / folder.name / 'overlay' / 'ui').mkdir(parents=True, exist_ok=True)
            (kit / 'sessions' / folder.name / 'overlay' / 'ui' / name).write_text(code)
        self.assertIsNone(release_kit.shipped_session_reason(kit, f'sessions/{folder.name}/overlay/ui/native_gate_probe.lua'))
        self.assertIn('a path of the PC', release_kit.shipped_session_reason(kit, f'sessions/{folder.name}/overlay/ui/generated.lua'))

    def test_prebuild_makes_each_session_with_the_kits_gate(self):
        calls = []

        def make(kit, players, mode, base_start, progress, log, cancelled, steam_fix=False, game=None, lanes=None):
            calls.append((players, Path(kit.gate_dll).read_bytes(), game, lanes))
            fakes.host_session(kit.root, players)   # as make_session leaves it: capture and base overlays too
            return netkit.cached_session(kit, players)
        manifest = self.build(prebuild=(2, 3, 4), session_options={'game': 'D:/X4', 'lanes': 'D:/lanes'}, make=make,
                              clean=lambda root: True)
        self.assertEqual([(players, dll) for players, dll, _game, _lanes in calls], [(2, GATE), (3, GATE), (4, GATE)])
        self.assertEqual(calls[0][2:], ('D:/X4', 'D:/lanes'))
        self.assertEqual(len(manifest['sessions']), 3)
        kit = self.base / 'kit'
        self.assertFalse(any((kit / 'sessions').rglob('capture-overlay')))
        self.assertEqual(sorted(p.name for p in (kit / 'sessions').iterdir()), sorted(manifest['sessions']))
        self.assertEqual(release_kit.verify(kit)[1], [])
        with self.assertRaises(SystemExit) as refused:
            self.build(out='dirty', prebuild=(2,), make=make, clean=lambda root: False)
        self.assertIn('commit first', str(refused.exception))
        with self.assertRaises(SystemExit):
            release_kit.player_counts('2,9')
        self.assertEqual(release_kit.player_counts('2, 3,3,4'), (2, 3, 4))

    @unittest.skipUnless(msilib, 'msilib: Windows, Python 3.12 or older')
    def test_the_installer_installs_the_shipped_sessions_only(self):
        embed = self.base / 'embed.zip'
        with zipfile.ZipFile(embed, 'w') as archive:
            for name in ('python.exe', 'pythonw.exe', 'python312.zip'):
                archive.writestr(name, b'MZ')
            archive.writestr('python312._pth', 'python312.zip\n.\n')
        manifest = self.build(sessions=self.made_sessions(players=(2,)), python_embed=embed)
        kit = self.base / 'kit'
        made_here = kit / 'sessions' / 'joined-0123456789abcdef-0123456789ab'   # a game this kit folder joined
        made_here.mkdir()
        (made_here / 'session-save.xml.gz').write_bytes(b'\x1f\x8b a joined save')
        made = msi.build(kit, self.base / 'x4-online.msi')
        self.assertEqual(made['files'], len(manifest['files']) + 1)
        db = msilib.OpenDatabase(made['msi'], msilib.MSIDBOPEN_READONLY)
        view = db.OpenView('SELECT `DefaultDir` FROM `Directory`')
        view.Execute(None)
        folders = []
        while True:
            record = view.Fetch()
            if record is None:
                break
            folders.append(record.GetString(1).split('|')[-1])
        view.Close()
        del db
        self.assertIn(manifest['sessions'][0], folders)
        self.assertNotIn(made_here.name, folders)
        self.assertNotIn('capture-overlay', folders)
        shutil.rmtree(made_here)


if __name__ == '__main__':
    unittest.main()
