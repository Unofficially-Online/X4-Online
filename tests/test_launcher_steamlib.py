"""lockstep.launcher.steamlib: X4 Online in the player's Steam, on copies of Steam's files only. The binary shortcuts.vdf
(read, written back byte for byte, our library entry added once and taken out again), the text localconfig.vdf (read and
written back byte for byte in Steam's own layout, X4's launch options set and put back exactly, also when there were
none), and SteamSetup: nothing written while Steam runs or for a kit that is not installed, the player's own later change
of the launch options kept. No real Steam file is read or written here."""
import json
import shutil
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import steamlib  # noqa: E402

# A shortcuts.vdf as Steam writes one with a single non-Steam game (made here from its fields, as Steam lays them out).
OTHER = {'appid': steamlib.int32(-1234567), 'AppName': (steamlib.STRING, 'Some Emulator'),
         'Exe': (steamlib.STRING, '"D:\\Emu\\emu.exe"'), 'StartDir': (steamlib.STRING, '"D:\\Emu\\"'),
         'icon': (steamlib.STRING, ''), 'ShortcutPath': (steamlib.STRING, ''), 'LaunchOptions': (steamlib.STRING, '-fullscreen'),
         'IsHidden': steamlib.int32(0), 'AllowDesktopConfig': steamlib.int32(1), 'AllowOverlay': steamlib.int32(1),
         'OpenVR': steamlib.int32(0), 'Devkit': steamlib.int32(0), 'DevkitGameID': (steamlib.STRING, ''),
         'DevkitOverrideAppID': steamlib.int32(0), 'LastPlayTime': steamlib.int32(1795600000),
         'FlatpakAppID': (steamlib.STRING, ''),
         'tags': (steamlib.MAP, {'0': (steamlib.STRING, 'favorite')})}

LOCALCONFIG = (
    '"UserLocalConfigStore"\n{\n'
    '\t"Software"\n\t{\n\t\t"Valve"\n\t\t{\n\t\t\t"Steam"\n\t\t\t{\n'
    '\t\t\t\t"apps"\n\t\t\t\t{\n'
    '\t\t\t\t\t"392160"\n\t\t\t\t\t{\n\t\t\t\t\t\t"LastPlayed"\t\t"1795600000"\n\t\t\t\t\t\t"playtime"\t\t"9000"\n\t\t\t\t\t}\n'
    '\t\t\t\t\t"440"\n\t\t\t\t\t{\n\t\t\t\t\t\t"LaunchOptions"\t\t"-novid \\"quoted\\" C:\\\\path"\n\t\t\t\t\t}\n'
    '\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n'
    '\t"friends"\n\t{\n\t\t"PersonaName"\t\t"someone"\n\t}\n'
    '}\n')


def shortcuts_file(entries):
    root = {'shortcuts': (steamlib.MAP, {str(i): (steamlib.MAP, entry) for i, entry in enumerate(entries)})}
    return steamlib.write_binary(root)


class Binary(unittest.TestCase):
    def test_read_and_write_back_byte_for_byte(self):
        data = shortcuts_file([OTHER])
        self.assertTrue(data.startswith(b'\x00shortcuts\x00\x000\x00\x02appid\x00'))
        self.assertTrue(data.endswith(b'\x08\x08\x08\x08'))          # the tags, the entry, the shortcuts, the root
        root, tail = steamlib.read_binary_tail(data)
        self.assertEqual((steamlib.write_binary(root, tail), tail), (data, b''))
        self.assertEqual(steamlib.read_binary_tail(data + b'\x08')[1], b'\x08')   # a writer's extra end marker kept
        for broken in (data[:-3], data + b'xx', b'\x09key\x00'):
            with self.assertRaises(steamlib.VdfError):
                steamlib.read_binary(broken)

    def test_the_shortcut_id_is_steams(self):
        exe, name = '"C:\\X4 Online\\python\\pythonw.exe"', 'X4 Online'
        expected = (zlib.crc32((exe + name).encode()) | 0x80000000) - (1 << 32)
        self.assertEqual(steamlib.shortcut_appid(exe, name), expected)
        self.assertLess(expected, 0)                                   # the top bit set: an int32 below zero

    def test_added_once_and_taken_out_again(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp)
            original = shortcuts_file([OTHER])
            (config / 'shortcuts.vdf').write_bytes(original)
            exe, kit = 'C:\\X4 Online\\python\\pythonw.exe', 'C:\\X4 Online'
            args = ('X4 Online', exe, kit, '"C:\\X4 Online\\helper.pyw" --play')
            self.assertTrue(steamlib.add_shortcut(config, *args, icon='D:\\X4\\X4.exe'))
            self.assertFalse(steamlib.add_shortcut(config, *args, icon='D:\\X4\\X4.exe'))   # once
            root = steamlib.read_binary((config / 'shortcuts.vdf').read_bytes())
            entries = root['shortcuts'][1]
            self.assertEqual(list(entries), ['0', '1'])
            ours = entries['1'][1]
            self.assertEqual((ours['AppName'][1], ours['Exe'][1], ours['StartDir'][1], ours['LaunchOptions'][1], ours['icon'][1]),
                             ('X4 Online', f'"{exe}"', f'"{kit}"', '"C:\\X4 Online\\helper.pyw" --play', 'D:\\X4\\X4.exe'))
            self.assertEqual(steamlib.int32_of(ours['AllowOverlay']), 1)
            self.assertEqual(steamlib.int32_of(ours['appid']), steamlib.shortcut_appid(f'"{exe}"', 'X4 Online'))
            self.assertEqual(entries['0'], (steamlib.MAP, OTHER))      # the player's own entry as it was
            self.assertEqual((config / 'shortcuts.vdf.x4online-backup').read_bytes(), original)
            self.assertTrue(steamlib.add_shortcut(config, 'X4 Online', exe, kit, '"C:\\X4 Online\\helper.pyw" --play --x'))
            self.assertEqual(len(steamlib.read_binary((config / 'shortcuts.vdf').read_bytes())['shortcuts'][1]), 2)
            self.assertTrue(steamlib.remove_shortcut(config, 'X4 Online', exe))
            self.assertEqual((config / 'shortcuts.vdf').read_bytes(), original)   # exactly as before
            self.assertFalse(steamlib.remove_shortcut(config, 'X4 Online', exe))
            # a user without a shortcuts.vdf yet: one is made
            other = config / 'other'
            other.mkdir()
            self.assertTrue(steamlib.add_shortcut(other, *args))
            self.assertEqual(len(steamlib.read_binary((other / 'shortcuts.vdf').read_bytes())['shortcuts'][1]), 1)

    def test_others_entries_are_renumbered_as_steam_numbers_them(self):
        exe = 'C:\\X4 Online\\python\\pythonw.exe'
        root = {'shortcuts': (steamlib.MAP, {})}
        root, _ = steamlib.with_shortcut(root, 'X4 Online', exe, 'C:\\X4 Online', '--play')
        root = {'shortcuts': (steamlib.MAP, {'0': (steamlib.MAP, OTHER), **{'1': root['shortcuts'][1]['0']},
                                             '2': (steamlib.MAP, dict(OTHER, AppName=(steamlib.STRING, 'Second')))})}
        root, changed = steamlib.without_shortcut(root, 'X4 Online', exe)
        self.assertTrue(changed)
        self.assertEqual([entry[1]['AppName'][1] for entry in root['shortcuts'][1].values()], ['Some Emulator', 'Second'])
        self.assertEqual(list(root['shortcuts'][1]), ['0', '1'])


class Text(unittest.TestCase):
    def test_read_and_write_back_byte_for_byte(self):
        entries = steamlib.read_text(LOCALCONFIG)
        self.assertEqual(steamlib.write_text(entries), LOCALCONFIG)
        self.assertIsNone(steamlib.launch_options(entries))           # X4 has none
        other = steamlib.find_block(entries, ('UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', '440'))
        self.assertEqual(other[0], ('LaunchOptions', '-novid "quoted" C:\\path'))
        for broken in ('"a"\n{\n', '"a"\n}\n', '"a"\n', '{\n}\n'):
            with self.assertRaises(steamlib.VdfError):
                steamlib.read_text(broken)

    def test_launch_options_set_and_put_back_exactly(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp)
            (config / 'localconfig.vdf').write_text(LOCALCONFIG, encoding='utf-8')
            ours = '"C:\\X4 Online\\python\\pythonw.exe" "C:\\X4 Online\\helper.pyw" --steam-play %command%'
            before, changed = steamlib.set_launch_options(config, ours)
            self.assertEqual((before, changed), (None, True))
            text = (config / 'localconfig.vdf').read_text(encoding='utf-8')
            self.assertIn('"LaunchOptions"\t\t"\\"C:\\\\X4 Online\\\\python\\\\pythonw.exe\\" ', text)   # Steam's escapes
            self.assertEqual(steamlib.launch_options(steamlib.read_text(text)), ours)
            self.assertIn('"PersonaName"\t\t"someone"', text)            # the rest as it was
            self.assertEqual(steamlib.set_launch_options(config, ours), (ours, False))
            steamlib.set_launch_options(config, None)                   # none before: none again
            self.assertEqual((config / 'localconfig.vdf').read_text(encoding='utf-8'), LOCALCONFIG)
            # options of the player's own before: those come back
            steamlib.set_launch_options(config, '-skipintro')
            with_own = (config / 'localconfig.vdf').read_text(encoding='utf-8')
            steamlib.set_launch_options(config, ours)
            steamlib.set_launch_options(config, '-skipintro')
            self.assertEqual((config / 'localconfig.vdf').read_text(encoding='utf-8'), with_own)
            self.assertEqual((config / 'localconfig.vdf.x4online-backup').read_text(encoding='utf-8'), LOCALCONFIG)


class Setup(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.kit = base / 'X4 Online'
        (self.kit / 'python').mkdir(parents=True)
        (self.kit / 'python' / 'pythonw.exe').write_bytes(b'MZ')
        (self.kit / 'helper.pyw').write_text('')
        (self.kit / 'kit-manifest.json').write_text('{}')
        self.configs = []
        for user in ('111', '222'):
            config = base / 'Steam' / 'userdata' / user / 'config'
            config.mkdir(parents=True)
            (config / 'localconfig.vdf').write_text(LOCALCONFIG, encoding='utf-8')
            self.configs.append(config)
        self.steam = [False]
        self.logs = []
        self.setup = steamlib.SteamSetup(self.kit, configs=lambda: self.configs, running=lambda: self.steam[0],
                                         log=self.logs.append)

    def tearDown(self):
        self.tmp.cleanup()

    def entries(self, config):
        path = config / 'shortcuts.vdf'
        return steamlib.read_binary(path.read_bytes())['shortcuts'][1] if path.is_file() else {}

    def options(self, config):
        return steamlib.launch_options(steamlib.read_text((config / 'localconfig.vdf').read_text(encoding='utf-8')))

    def test_the_library_entry_once_steam_is_closed_and_the_switch_off_by_default(self):
        self.steam[0] = True
        self.assertFalse(self.setup.apply())                             # Steam runs: nothing written
        self.assertEqual([self.entries(c) for c in self.configs], [{}, {}])
        self.assertEqual(self.setup.view(), {'on': False, 'pending': False, 'steam_running': True})
        self.steam[0] = False
        self.assertTrue(self.setup.apply())
        for config in self.configs:
            self.assertEqual([e[1]['AppName'][1] for e in self.entries(config).values()], ['X4 Online'])
            self.assertEqual(self.entries(config)['0'][1]['LaunchOptions'][1], f'"{self.kit / "helper.pyw"}" --play')
            self.assertIsNone(self.options(config))                     # X4's own Play untouched
        self.assertTrue(self.setup.apply())
        self.assertEqual([len(self.entries(c)) for c in self.configs], [1, 1])

    def test_the_switch_applies_while_steam_is_closed_and_goes_back_exactly(self):
        own = self.configs[1]
        steamlib.set_launch_options(own, '-skipintro')                  # this user's own options
        before = [(c / 'localconfig.vdf').read_text(encoding='utf-8') for c in self.configs]
        self.steam[0] = True
        self.setup.want_play(True)
        self.assertEqual(self.setup.view(), {'on': True, 'pending': True, 'steam_running': True})
        self.assertFalse(self.setup.apply())
        self.assertEqual(self.options(self.configs[0]), None)
        self.steam[0] = False
        self.assertTrue(self.setup.apply())
        self.assertEqual([self.options(c) for c in self.configs], [self.setup.play_options()] * 2)
        self.assertIn('--steam-play %command%', self.setup.play_options())
        self.assertEqual(self.setup.view(), {'on': True, 'pending': False, 'steam_running': False})
        self.setup.want_play(False)
        self.assertTrue(self.setup.apply())
        self.assertEqual([(c / 'localconfig.vdf').read_text(encoding='utf-8') for c in self.configs], before)
        # on again, then the player edits X4's options in Steam: their own text is never taken away
        self.setup.want_play(True)
        self.setup.apply()
        steamlib.set_launch_options(self.configs[0], '-novid')
        self.assertTrue(self.setup.undo())
        self.assertEqual(self.options(self.configs[0]), '-novid')
        self.assertEqual(self.options(self.configs[1]), '-skipintro')
        self.assertTrue(any('were changed since' in line for line in self.logs))
        self.assertEqual([self.entries(c) for c in self.configs], [{}, {}])   # the library entry gone too
        self.assertFalse(self.setup.play_wanted())

    def test_a_kit_that_is_not_installed_never_touches_steam(self):
        (self.kit / 'kit-manifest.json').unlink()
        self.setup.want_play(True)
        self.assertFalse(self.setup.apply())
        self.assertIsNone(self.setup.view())
        self.assertTrue(self.setup.undo())
        self.assertEqual([self.entries(c) for c in self.configs], [{}, {}])
        self.assertEqual([self.options(c) for c in self.configs], [None, None])

    def test_steam_folders(self):
        root = Path(self.tmp.name) / 'Steam'
        (root / 'userdata' / '0' / 'config').mkdir(parents=True)        # not a user
        (root / 'userdata' / 'anonymous').mkdir()
        self.assertEqual(steamlib.user_configs(root), self.configs)
        self.assertTrue(steamlib.steam_running(tasklist='"steam.exe","1234","Console"'))
        self.assertFalse(steamlib.steam_running(tasklist='INFO: No tasks are running'))
        self.assertEqual(steamlib.user_configs(Path(self.tmp.name) / 'nowhere'), [])


if __name__ == '__main__':
    unittest.main()
