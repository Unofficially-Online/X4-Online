"""tools/make_release_kit.py: a release kit built from this checkout (git archive HEAD) with a dummy gate DLL and a fake
embeddable Python. Its layout, release.json, master.json, the manifest check, and that nothing private is in it.
No X4 and no real gate DLL are needed; the kit is not installed or run here."""
import hashlib
import importlib.util
import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DUMMY_GATE = b'MZ a dummy gate DLL for the release kit test'
DUMMY_CRYPTO = b'MZ a dummy wire crypto DLL'
BUILT = '2026-11-23T12:34:56Z'
MASTER = 'http://203.0.113.10:47100'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release_kit = load('make_release_kit')


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def fake_embed(path):
    """The files of python.org's embeddable package that the kit builder looks at."""
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('python.exe', b'MZ')
        archive.writestr('pythonw.exe', b'MZ')
        archive.writestr('python312.zip', b'PK')
        archive.writestr('python312._pth', 'python312.zip\n.\n')
        archive.writestr('python.cat', b'a security catalog, not an X4 one')
    return path


def files_under(folder):
    return sorted(p.relative_to(folder).as_posix() for p in Path(folder).rglob('*') if p.is_file())


class FastBuilds(unittest.TestCase):
    """Builds with a small stand-in for the repository export (and the menu), for the refusals and options."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.gate = self.base / 'gate.dll'
        self.gate.write_bytes(DUMMY_GATE)
        self.extra_repo_file = None
        self.export = release_kit.playtest_kit.export_repo
        self.menu = release_kit.write_steam_menu
        release_kit.playtest_kit.export_repo = self.fake_export
        release_kit.write_steam_menu = self.fake_menu

    def tearDown(self):
        release_kit.playtest_kit.export_repo = self.export
        release_kit.write_steam_menu = self.menu
        self.tmp.cleanup()

    def fake_export(self, out, root=None):
        (out / 'tools').mkdir(parents=True)
        (out / 'tools' / 'x4ls.py').write_text('print(1)\n')
        if self.extra_repo_file:
            name, text = self.extra_repo_file
            (out / name).parent.mkdir(parents=True, exist_ok=True)
            (out / name).write_text(text)
        return 'c0ffee' * 6 + 'abcd'

    def fake_menu(self, kit):
        self.menu_kit = kit
        (kit / 'steam-menu').mkdir()
        (kit / 'steam-menu' / 'content.xml').write_text('<content id="x4mp_playtest_menu" />\n')

    def test_a_relative_out_is_made_absolute(self):
        # the menu step runs a child Python with the kit as its cwd: a relative kit path there pointed nowhere
        # ("No module named 'lockstep'" for --out .local/release/rk1, 23 Nov)
        import os
        here = os.getcwd()
        os.chdir(self.base)
        try:
            release_kit.build(self.gate, 'relative-kit', built=BUILT)
        finally:
            os.chdir(here)
        self.assertTrue(self.menu_kit.is_absolute())
        self.assertTrue((self.base / 'relative-kit' / 'release.json').is_file())

    def test_without_master_and_python(self):
        kit = self.base / 'bare'
        manifest = release_kit.build(self.gate, kit)
        self.assertFalse((kit / 'master.json').exists())
        self.assertNotIn('master', manifest)
        self.assertFalse((kit / 'python').exists())
        self.assertNotIn('python_embed_sha256', manifest)
        text = (kit / 'KIT.txt').read_text()
        self.assertIn('lobby      none named', text)
        self.assertIn('Python     not bundled', text)
        release = json.loads((kit / 'release.json').read_text())
        self.assertRegex(release['built'], r'\A\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\Z')   # the build's own time, UTC
        self.assertEqual(release['release'], 'c0ffeec0ffee-' + hashlib.sha256(DUMMY_GATE).hexdigest()[:12])
        self.assertTrue((kit / 'helper.pyw').is_file() and (kit / 'second.pyw').is_file())

    def test_verify_names_what_differs_but_not_what_a_pc_makes(self):
        kit = self.base / 'kit'
        release_kit.build(self.gate, kit, built=BUILT)
        self.assertEqual(release_kit.verify(kit)[1], [])
        # made while playing: the helper's log, a cached session, a master.json put beside a kit built without one
        (kit / 'repo' / '.local').mkdir()
        (kit / 'repo' / '.local' / 'helper.log').write_text('started')
        (kit / 'sessions' / 's1').mkdir(parents=True)
        (kit / 'sessions' / 's1' / 'session.json').write_text('{}')
        (kit / 'master.json').write_text('{"url": "https://master.example.org"}')
        (kit / 'repo' / 'tools' / '__pycache__').mkdir()
        (kit / 'repo' / 'tools' / '__pycache__' / 'x4ls.cpython-312.pyc').write_bytes(b'cache')
        self.assertEqual(release_kit.verify(kit)[1], [])
        (kit / 'native' / 'x4lan_gate_64.dll').write_bytes(b'MZ another gate')
        (kit / 'KIT.txt').unlink()
        (kit / 'stray.txt').write_text('x')
        self.assertEqual(sorted(release_kit.verify(kit)[1]), ['changed native/x4lan_gate_64.dll', 'extra stray.txt', 'missing KIT.txt'])
        self.assertEqual(release_kit.main(['--verify', str(kit)]), 1)
        # a kit built with a master.json reports a changed one
        other = self.base / 'with-master'
        release_kit.build(self.gate, other, master='https://master.example.org/x4mp', built=BUILT)
        (other / 'master.json').write_text('{"url": "https://other.example.org"}')
        self.assertEqual(release_kit.verify(other)[1], ['changed master.json'])

    def test_the_wire_crypto_dll_only_when_asked(self):
        crypto = self.base / 'crypto.dll'
        crypto.write_bytes(DUMMY_CRYPTO)
        manifest = release_kit.build(self.gate, self.base / 'kit', crypto_dll=crypto, built=BUILT)
        self.assertIn('native/x4mp_wire_crypto.dll', manifest['files'])
        self.assertIn('wire DLL   native/x4mp_wire_crypto.dll', (self.base / 'kit' / 'KIT.txt').read_text())
        plain = release_kit.build(self.gate, self.base / 'kit2', built=BUILT)
        self.assertEqual([path for path in plain['files'] if path.startswith('native/')], ['native/x4lan_gate_64.dll'])

    def test_a_private_file_refuses_the_kit_and_nothing_stays(self):
        for name, text in (('.local/runs/relay.log', 'a run'), ('server.json', '{"host": "203.0.113.7", "key": "%s"}' % ('ab' * 32)),
                           ('snapshot/save_009.xml.gz', 'a save'), ('notes/keys.json', '{"secret": "%s"}' % ('cd' * 16))):
            self.extra_repo_file = (name, text)
            out = self.base / ('kit-' + name.replace('/', '-'))
            with self.assertRaises(SystemExit) as refused:
                release_kit.build(self.gate, out, built=BUILT)
            self.assertIn('repo/' + name, str(refused.exception))
            self.assertFalse(out.exists(), name)                    # the refused kit is removed again
        empty = self.base / 'empty'
        empty.mkdir()
        with self.assertRaises(SystemExit):
            release_kit.build(self.gate, empty, built=BUILT)
        self.assertEqual(list(empty.iterdir()), [])                  # a folder that was there stays, empty

    def test_refusals_before_anything_is_written(self):
        busy = self.base / 'busy'
        busy.mkdir()
        (busy / 'old.txt').write_text('another kit')
        with self.assertRaises(SystemExit):
            release_kit.build(self.gate, busy)                       # never over another kit
        self.assertEqual(files_under(busy), ['old.txt'])
        with self.assertRaises(SystemExit):
            release_kit.build(self.base / 'none.dll', self.base / 'k1')
        self.assertFalse((self.base / 'k1').exists())
        not_a_dll = self.base / 'text.dll'
        not_a_dll.write_text('not a DLL')
        with self.assertRaises(SystemExit):
            release_kit.build(not_a_dll, self.base / 'k2')
        with self.assertRaises(SystemExit):
            release_kit.build(self.gate, self.base / 'k3', master='ftp://master.example.org')
        self.assertFalse((self.base / 'k3').exists())


class MasterAddress(unittest.TestCase):
    def test_urls_and_host_port(self):
        cases = {
            '203.0.113.10': 'http://203.0.113.10:47100',
            '203.0.113.10:47110': 'http://203.0.113.10:47110',
            'master.example.org': 'http://master.example.org:47100',
            ' master.example.org:8080 ': 'http://master.example.org:8080',
            '[2001:db8::1]': 'http://[2001:db8::1]:47100',
            'http://203.0.113.10:47100': 'http://203.0.113.10:47100',
            'https://master.example.org/x4mp': 'https://master.example.org/x4mp',
            'HTTPS://master.example.org/x4mp/': 'https://master.example.org/x4mp',
            'https://master.example.org': 'https://master.example.org',
        }
        for text, url in cases.items():
            self.assertEqual(release_kit.master_url(text), url, text)

    def test_refused_addresses(self):
        for text in ('', 'ftp://master.example.org', 'http://user:secret@master.example.org', 'http://master.example.org/?a=1',
                     'https://master.example.org/x4mp#top', '203.0.113.300', '1.2.3', 'bad host!', '2001:db8::1',
                     'master.example.org:0', 'master.example.org:99999', 'master.example.org:http', 'http://', '[2001:db8::1'):
            with self.assertRaises(SystemExit, msg=text):
                release_kit.master_url(text)


class PrivateRules(unittest.TestCase):
    def test_what_a_release_never_holds(self):
        private = {
            'repo/.local/runs/a/summary.json': '{}',
            'repo/.git/config': '[core]',
            'repo/tools/__pycache__/x.cpython-312.pyc': 'cache',
            'server.json': '{}',
            'upper/Server.JSON': '{}',
            'snapshot/save_009.xml.gz': 'save',
            'overlay/native/x4lan_gate_64.dll': 'MZ',
            'sessions/s1/session-save.xml.gz': 'save',
            'saves/a.txt': 'a',
            'repo/logs/today.txt': 'log',
            'game.log': 'log',
            'j6000.x4join': 'join',
            'keys/relay.key': 'key',
            'cert.pem': 'cert',
            'id_ed25519': 'key',
            'id_rsa.pub': 'key',
            '.env': 'TOKEN=1',
            'X4.exe': 'MZ',
            '01.cat': 'catalog',
            'ext_01.dat': 'data',
            'relay-key.json': json.dumps({'format': release_kit.WIRE_KEY_FORMAT, 'relay_seed': '11' * 32, 'master': '22' * 32}),
            'notes/game.json': json.dumps({'game_id': 'g1', 'ticket': 'ab' * 16}),
            'id.txt': '-----BEGIN OPENSSH PRIVATE KEY-----\nAAAA\n-----END OPENSSH PRIVATE KEY-----\n',
        }
        public = {
            'python/python.cat': 'a security catalog',
            'src/lockstep/launcher/session.py': 'print(1)',
            'tools/dev/save_diff.py': 'print(1)',
            'release.json': json.dumps(release_kit.release_of('ab' * 20, 'cd' * 32, BUILT)),
            'master.json': json.dumps({'url': 'https://master.example.org/x4mp'}),
            'docs/WIRE.txt': 'a key file begins with -----BEGIN PRIVATE KEY----- in the middle of a line',
            'tests/fixture.json': '[1, 2, 3]',
            'broken.json': '{not json',
        }
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            for name, text in {**private, **public}.items():
                (tmp / name).parent.mkdir(parents=True, exist_ok=True)
                (tmp / name).write_text(text)
            found = dict(release_kit.private_files(tmp))
            self.assertEqual(sorted(found), sorted(private))
            for name in public:
                self.assertIsNone(release_kit.private_reason(tmp, name), name)
            # what the installer leaves out is not looked at
            skip = ('.local', '.git', '__pycache__', 'sessions')
            kept = [path for path, _ in release_kit.private_files(tmp, skip=skip)]
            self.assertIn('server.json', kept)
            self.assertFalse([path for path in kept if set(path.split('/')[:-1]) & set(skip)])
            self.assertEqual(len(kept), len(private) - 4)

class RealKit(unittest.TestCase):
    """One build from this checkout (git archive HEAD), with --master and --python-embed."""
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.tmp.name)
        cls.gate = cls.base / 'gate.dll'
        cls.gate.write_bytes(DUMMY_GATE)
        cls.kit = cls.base / 'kit'
        embed = fake_embed(cls.base / 'embed.zip')
        cls.manifest = release_kit.build(cls.gate, cls.kit, master='203.0.113.10', python_embed=embed, built=BUILT)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_layout(self):
        top = sorted(p.name for p in self.kit.iterdir())
        self.assertEqual(top, sorted(['repo', 'native', 'python', 'steam-menu', 'helper.pyw', 'second.pyw', 'release.json',
                                      'master.json', 'KIT.txt', 'VERIFY.cmd', 'kit-manifest.json']))
        self.assertEqual(files_under(self.kit / 'native'), ['x4lan_gate_64.dll'])
        self.assertEqual((self.kit / 'native' / 'x4lan_gate_64.dll').read_bytes(), DUMMY_GATE)
        self.assertEqual(files_under(self.kit / 'steam-menu'), ['content.xml', 'ui.xml', 'ui/config.lua', 'ui/native_gate_lobby.lua'])
        self.assertEqual((self.kit / 'steam-menu' / 'ui' / 'native_gate_lobby.lua').read_bytes(),
                         (self.kit / 'repo' / 'tools' / 'dev' / 'native_gate_lobby.lua').read_bytes())   # the commit's page
        # the release's own menu identity (beside a playtest install on one PC) and the netplay page
        self.assertIn('id="x4online_menu"', (self.kit / 'steam-menu' / 'content.xml').read_text())
        config = (self.kit / 'steam-menu' / 'ui' / 'config.lua').read_text()
        self.assertIn("pipe = 'x4online'", config)
        self.assertIn('netplay = true', config)
        self.assertNotIn('server = true', config)
        tracked =sorted(name for name in git('ls-tree', '-r', '-z', '--name-only', 'HEAD').split('\0') if name)
        self.assertEqual(files_under(self.kit / 'repo'), tracked)    # exactly HEAD's tracked files, nothing untracked
        lines = (self.kit / 'python' / 'python312._pth').read_text().split()
        self.assertEqual(lines, ['python312.zip', '.', '..\\repo\\src', '..\\repo\\tools', '..\\repo\\tools\\dev'])
        for name, call in (('helper.pyw', 'steamplay.main(KIT, None)'), ('second.pyw', 'steamplay.second(KIT, None)')):
            text = (self.kit / name).read_text()
            self.assertIn(call, text)
            self.assertIn("sys.path.insert(0, str(KIT / 'repo' / 'src'))", text)
            compile(text, name, 'exec')
        for absent in ('PLAY.cmd', 'overlay', 'snapshot', 'server.json', 'sessions'):
            self.assertFalse((self.kit / absent).exists(), absent)

    def test_release_json(self):
        release = json.loads((self.kit / 'release.json').read_text())
        commit = git('rev-parse', 'HEAD').strip()
        gate_sha = hashlib.sha256(DUMMY_GATE).hexdigest()
        self.assertEqual(release, {'release': commit[:12] + '-' + gate_sha[:12], 'commit': commit, 'gate_dll_sha256': gate_sha,
                                   'built': BUILT})
        self.assertRegex(release['release'], release_kit.RELEASE_PATTERN)
        for key in ('release', 'commit', 'gate_dll_sha256', 'built'):
            self.assertEqual(self.manifest[key], release[key])
        self.assertEqual(self.manifest['schema'], 'x4-online-release-kit-v1')

    def test_master_json(self):   # FastBuilds.test_without_master_and_python: none without --master
        self.assertEqual(json.loads((self.kit / 'master.json').read_text()), {'url': MASTER})
        self.assertEqual(self.manifest['master'], MASTER)
        self.assertIn('lobby      ' + MASTER, (self.kit / 'KIT.txt').read_text())

    def test_nothing_private_and_no_paths_of_this_pc(self):
        self.assertEqual(release_kit.private_files(self.kit), [])   # this checkout's tracked files included
        for path in self.manifest['files']:
            parts = path.lower().split('/')
            self.assertFalse({'.local', '.git', 'snapshot', 'sessions', 'saves', 'runs', '__pycache__'} & set(parts), path)
            self.assertNotIn(parts[-1], ('server.json', 'session.json'))
            self.assertFalse(parts[-1].endswith(('.log', '.xml.gz', '.x4join', '.key', '.pem')), path)
        # the files the builder writes name no folder of the PC that built them
        local = [str(ROOT), str(self.base), str(Path.home())]
        for name in ('release.json', 'master.json', 'kit-manifest.json', 'KIT.txt', 'VERIFY.cmd', 'helper.pyw', 'second.pyw',
                     'steam-menu/content.xml', 'steam-menu/ui.xml', 'steam-menu/ui/config.lua'):
            text = (self.kit / name).read_text(encoding='utf-8')
            for value in local:
                self.assertNotIn(value, text, name)
                self.assertNotIn(value.replace('\\', '/'), text, name)
        text = (self.kit / 'KIT.txt').read_bytes()
        text.decode('ascii')
        self.assertIn(b'\r\n', text)

    def test_manifest_and_verify(self):
        files = dict(self.manifest['files'])
        self.assertEqual(sorted(files), sorted(p for p in files_under(self.kit) if p != 'kit-manifest.json'))
        self.assertEqual(files['native/x4lan_gate_64.dll'], hashlib.sha256(DUMMY_GATE).hexdigest())
        self.assertEqual(self.manifest['python_embed_sha256'], hashlib.sha256((self.base / 'embed.zip').read_bytes()).hexdigest())
        self.assertEqual(release_kit.verify(self.kit)[1], [])
        self.assertEqual(release_kit.main(['--verify', str(self.kit)]), 0)
        verify_cmd = (self.kit / 'VERIFY.cmd').read_text()
        self.assertIn('cd /d "%~dp0"', verify_cmd)
        self.assertIn('if exist "%~dp0python\\python.exe" set PY="%~dp0python\\python.exe"', verify_cmd)
        self.assertIn('%PY% repo\\tools\\make_release_kit.py --verify .', verify_cmd)
        # what VERIFY.cmd runs is in the kit (from the commit)
        self.assertTrue((self.kit / 'repo' / 'tools' / 'make_release_kit.py').is_file())
        self.assertTrue((self.kit / 'repo' / 'tools' / 'make_playtest_kit.py').is_file())


if __name__ == '__main__':
    unittest.main()
