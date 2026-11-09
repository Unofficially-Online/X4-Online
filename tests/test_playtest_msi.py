"""tools/make_playtest_msi.py and the kit's bundled Python: a small fake kit becomes an installer whose tables are read
back. Nothing is installed here; a real install was run by hand (docs/PLAYTEST.md)."""
import importlib.util
import json
import sys
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


kit, msi = load('make_playtest_kit'), load('make_playtest_msi')
try:
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', DeprecationWarning)
        import msilib
except ImportError:   # Python 3.13 or not Windows
    msilib = None


def fake_kit(folder):
    (folder / 'repo' / 'tools').mkdir(parents=True)
    (folder / 'repo' / 'tools' / 'x4ls.py').write_text('print(1)\n')
    (folder / 'repo' / '.local' / 'runs').mkdir(parents=True)
    (folder / 'repo' / '.local' / 'runs' / 'old.log').write_text('a run made on the build PC')
    (folder / 'overlay' / 'native').mkdir(parents=True)
    (folder / 'overlay' / 'native' / 'x4lan_gate_64.dll').write_bytes(b'MZ fake gate')
    (folder / 'PLAY.cmd').write_text('@echo off\n')
    (folder / 'server.json').write_text('{}')
    (folder / 'kit-manifest.json').write_text(json.dumps({'commit': 'c0ffee' * 6 + 'abcd'}))


class BundledPython(unittest.TestCase):
    def test_the_embeddable_package_is_told_where_the_launcher_is(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            with zipfile.ZipFile(tmp / 'embed.zip', 'w') as z:
                z.writestr('python.exe', b'MZ')
                z.writestr('python312.zip', b'PK')
                z.writestr('python312._pth', 'python312.zip\n.\n')
            kit.add_python(tmp / 'embed.zip', tmp / 'kit' / 'python')
            lines = (tmp / 'kit' / 'python' / 'python312._pth').read_text().split()
            self.assertEqual(lines, ['python312.zip', '.', '..\\repo\\src', '..\\repo\\tools', '..\\repo\\tools\\dev'])
            with zipfile.ZipFile(tmp / 'other.zip', 'w') as z:
                z.writestr('readme.txt', 'not a Python')
            with self.assertRaises(SystemExit):
                kit.add_python(tmp / 'other.zip', tmp / 'kit2' / 'python')

    def test_every_command_file_prefers_the_kits_python(self):
        for server in (False, True):
            for name, text in kit.cmd_files(2, 47000, [], 'save_009.xml.gz', server).items():
                self.assertIn('if exist "%~dp0python\\python.exe" set PY="%~dp0python\\python.exe"', text, name)
                self.assertNotIn('\r\npython repo', text, name)
                self.assertIn('%PY% repo\\tools\\', text, name)


@unittest.skipUnless(msilib and sys.platform == 'win32', 'needs msilib (Windows, Python 3.12 or older)')
class Installer(unittest.TestCase):
    def rows(self, db, query):
        view = db.OpenView(query)
        view.Execute(None)
        out = []
        while True:
            try:
                record = view.Fetch()
            except msilib.MSIError:
                break
            if record is None:
                break
            out.append(tuple(record.GetString(i) for i in range(1, record.GetFieldCount() + 1)))
        view.Close()
        return out

    def test_the_kit_becomes_a_per_user_installer(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fake_kit(tmp / 'kit')
            made = msi.build(tmp / 'kit', tmp / 'out' / 'kit.msi')
            self.assertEqual(made['files'], 5)                    # the run folder of the build PC is not installed
            db = msilib.OpenDatabase(made['msi'], msilib.MSIDBOPEN_READONLY)
            properties = dict(self.rows(db, 'SELECT `Property`, `Value` FROM `Property`'))
            self.assertEqual((properties['ALLUSERS'], properties['MSIINSTALLPERUSER'], properties['LIMITUI']), ('2', '1', '1'))
            self.assertEqual((properties['ProductName'], properties['UpgradeCode']), (msi.NAME, msi.UPGRADE_CODE))
            self.assertEqual(properties['ProductCode'], made['product'])
            names = sorted(name.split('|')[-1] for (name,) in self.rows(db, 'SELECT `FileName` FROM `File`'))
            self.assertEqual(names, ['PLAY.cmd', 'kit-manifest.json', 'server.json', 'x4lan_gate_64.dll', 'x4ls.py'])
            directories = {row[0]: row[1:] for row in self.rows(db, 'SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`')}
            self.assertEqual(directories['INSTALLDIR'], ('LocalAppDataFolder', 'X4MPPLAY|' + msi.NAME))
            shortcuts = self.rows(db, 'SELECT `Directory_`, `Target`, `WkDir` FROM `Shortcut`')
            self.assertEqual(sorted(shortcuts), [('DesktopFolder', '[INSTALLDIR]PLAY.cmd', 'INSTALLDIR'),
                                                 ('ProgramMenuFolder', '[INSTALLDIR]PLAY.cmd', 'INSTALLDIR')])
            actions = [row[0] for row in self.rows(db, 'SELECT `Action` FROM `InstallUISequence`')]
            self.assertNotIn('ExitDialog', actions)               # no dialog is named that the installer does not have
            self.assertEqual(self.rows(db, 'SELECT `UpgradeCode`, `ActionProperty` FROM `Upgrade`'), [(msi.UPGRADE_CODE, 'OLDPRODUCTS')])
            del db
            other = msi.build(tmp / 'kit', tmp / 'out' / 'kit2.msi')
            self.assertNotEqual(other['product'], made['product'])   # every installer is its own product; the upgrade code joins them

    def test_a_kit_with_a_helper_is_installed_for_play_in_steam(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fake_kit(tmp / 'kit')
            (tmp / 'kit' / 'helper.pyw').write_text('print(1)\n')
            (tmp / 'kit' / 'python').mkdir()
            (tmp / 'kit' / 'python' / 'pythonw.exe').write_bytes(b'MZ')
            made = msi.build(tmp / 'kit', tmp / 'kit.msi')
            self.assertTrue(made['helper'])
            db = msilib.OpenDatabase(made['msi'], msilib.MSIDBOPEN_READONLY)
            run = '"[INSTALLDIR]python\\pythonw.exe" "[INSTALLDIR]helper.pyw"'
            self.assertEqual(self.rows(db, 'SELECT `Root`, `Key`, `Name`, `Value` FROM `Registry`'),
                             [('1', 'Software\\Microsoft\\Windows\\CurrentVersion\\Run', 'X4MultiplayerPlaytest', run + ' --quiet')])
            self.assertEqual(sorted(self.rows(db, 'SELECT `Action`, `Type`, `Source`, `Target` FROM `CustomAction`')),
                             [('StartHelper', '226', 'INSTALLDIR', '"[WindowsFolder]explorer.exe" "[ProgramMenuFolder]' + msi.NAME + '.lnk"'),
                              ('StopHelper', '98', 'INSTALLDIR', run + ' --remove')])
            order = {row[0]: (row[1], int(row[2])) for row in
                     self.rows(db, 'SELECT `Action`, `Condition`, `Sequence` FROM `InstallExecuteSequence`')}
            self.assertEqual(order['StopHelper'], ('REMOVE="ALL"', 1600))
            self.assertEqual(order['StartHelper'], ('NOT REMOVE', 6650))
            # an older kit is removed first (its helper stops, its menu entry leaves X4), then the new one is installed
            self.assertLess(order['InstallValidate'][1], order['RemoveExistingProducts'][1])
            self.assertLess(order['RemoveExistingProducts'][1], order['InstallInitialize'][1])
            self.assertLess(order['StopHelper'][1], order['RemoveFiles'][1])
            self.assertLess(order['InstallFinalize'][1], order['StartHelper'][1])
            shortcuts = self.rows(db, 'SELECT `Directory_`, `Target`, `Arguments` FROM `Shortcut`')
            self.assertEqual(shortcuts, [('ProgramMenuFolder', '[INSTALLDIR]python\\pythonw.exe', '"[INSTALLDIR]helper.pyw"')])
            del db

    def test_versions_rise_with_time_and_a_folder_without_a_kit_is_refused(self):
        import time
        a = msi.version_now(time.gmtime(1794261900))
        b = msi.version_now(time.gmtime(1794261900 + 3600))
        self.assertLess([int(x) for x in a.split('.')], [int(x) for x in b.split('.')])
        self.assertLessEqual(int(a.split('.')[2]), 65535)
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                msi.build(tmp, Path(tmp) / 'x.msi')


if __name__ == '__main__':
    unittest.main()
