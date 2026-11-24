"""lockstep.launcher.steamplay for a release kit: its own menu identity, the netplay helper behind real named pipes with
the spelled requests (tbegin, t0..tf, tend), the hand-over to x4ls lobby --request, and the state of the running game answered
from the lobby's mirror. A fake kit, fake network modules (tests/netplay_fakes.py), a fake start; no X4 runs here."""
import itertools
import os
import sys
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import lobby, netplay, steamplay  # noqa: E402
import netplay_fakes as fakes  # noqa: E402

NUMBER = itertools.count()


class Identities(unittest.TestCase):
    def test_a_release_has_its_own_menu_beside_a_playtest_kits(self):
        folder = fakes.Folder()
        try:
            release = steamplay.write_menu(folder.path / 'release-menu', identity=steamplay.RELEASE)
            playtest = steamplay.write_menu(folder.path / 'playtest-menu')
            spelled = steamplay.write_menu(folder.path / 'spelled-menu', lobby.LOBBY_LUA, release=True)
            self.assertEqual(steamplay.tree(spelled), steamplay.tree(release))   # make_release_kit.py's spelling
            self.assertEqual((release / 'ui/config.lua').read_text(),
                             "X4LAN_CONFIG = {enabled = false, lobby = {pipe = 'x4online', netplay = true}}\n")
            self.assertEqual((playtest / 'ui/config.lua').read_text(), steamplay.MENU_CONFIG)   # as it always was
            self.assertIn("lobby = {pipe = 'x4mp-playtest', server = true, title = 'Multiplayer (playtest)'}", steamplay.MENU_CONFIG)
            self.assertIn('id="x4online_menu" name="X4 Online (menu)"', (release / 'content.xml').read_text())
            self.assertIn('<addon name="x4online_menu">', (release / 'ui.xml').read_text())
            x4 = folder.path / 'X4'
            (x4 / 'extensions').mkdir(parents=True)
            self.assertTrue(steamplay.install_menu(release, x4, steamplay.RELEASE))
            self.assertTrue(steamplay.install_menu(playtest, x4))
            self.assertEqual(sorted(p.name for p in (x4 / 'extensions').iterdir()), ['x4mp_playtest_menu', 'x4online_menu'])
            self.assertTrue(steamplay.remove_menu(x4, steamplay.RELEASE))   # only its own
            self.assertEqual(sorted(p.name for p in (x4 / 'extensions').iterdir()), ['x4mp_playtest_menu'])
            self.assertFalse(steamplay.remove_menu(x4, steamplay.RELEASE))
            kit = fakes.release_kit(folder.path / 'kit')
            self.assertIs(steamplay.identity_of(kit), steamplay.RELEASE)
            self.assertIs(steamplay.identity_of(folder.path), steamplay.PLAYTEST)
        finally:
            folder.cleanup()

    def test_second_window_of_a_release_kit(self):
        folder = fakes.Folder()
        commands = []

        def run(command, **kwargs):
            commands.append(command)
            return mock.Mock(returncode=0, stdout='', stderr='')
        try:
            kit = fakes.release_kit(folder.path / 'kit')
            with mock.patch.object(steamplay.subprocess, 'run', run):
                self.assertEqual(steamplay.second(kit, None), 0)
            self.assertEqual(commands[0][1:], ['repo/tools/x4ls.py', 'doctor'])
            self.assertEqual(commands[1][1:], ['repo/tools/x4ls.py', 'lobby', '--kit', str(kit), '--show', '--close-when-over'])
        finally:
            folder.cleanup()


class SpelledText(unittest.TestCase):
    def test_begin_digits_end(self):
        logs = []
        text = steamplay.TextRequests(logs.append)

        def spell(request):
            text.open('tbegin')
            for digit in netplay.request_hex(request):
                self.assertIsNone(text.open('t' + digit))
            return text.open('tend')
        self.assertEqual(spell('JOINCODE|K7QX-3FNA'), 'JOINCODE|K7QX-3FNA')
        self.assertEqual(spell('HOST|1|3|Zoë'), 'HOST|1|3|Zoë')
        self.assertIsNone(text.open('tend'))                                  # an end without a begin
        self.assertEqual(len(logs), 1)
        text.open('t4')                                                     # digits without a begin are dropped
        text.open('tbegin')
        text.open('t4')
        self.assertIsNone(text.open('tend'))                                  # an odd number of digits: not hex
        self.assertIsNone(spell('JOINCODE|' + 'x' * 300))                   # over 400 digits
        self.assertEqual(len(logs), 3)


@unittest.skipUnless(os.name == 'nt', 'named pipes: Windows')
class NetHelperPipes(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.kit = fakes.release_kit(self.folder.path / 'kit')
        self.prefix = f'x4mp-nettest-{os.getpid()}-{next(NUMBER)}'
        self.logs = []
        self.started = []
        self.release_start = threading.Event()
        self.x4_open = [True]
        self.lan = fakes.FakeLandisco([{'ip': '192.168.10.9', 'candidates': ['192.168.10.9'], 'release': 'other-release',
                                        'code': '-', 'name': 'Basement', 'tcp_port': 47001, 'players': 1, 'players_max': 2}])
        self.say = steamplay.say
        steamplay.say = lambda *args, **kwargs: None
        self.helper = steamplay.NetHelper(self.kit, prefix=self.prefix, log=self.logs.append, start=self.start,
                                          running=lambda: self.x4_open[0], net=fakes.network(None, landisco=self.lan),
                                          master_url='')
        self.helper.flow.backend.clipboard_get = lambda: 'k7qx3fna'
        self.thread = threading.Thread(target=self.helper.run, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.release_start.set()
        steamplay.ask(self.prefix, 'stop', timeout=1)
        self.thread.join(5)
        steamplay.say = self.say
        self.folder.cleanup()

    def start(self, request):
        self.started.append(request)
        mirror = self.helper.mirror
        lobby.write_atomic(mirror, "return {stamp = 41, phase = 'hosting', code = 'K7QX-3FNA', lines = {'Join code: K7QX-3FNA'}}\n")
        self.release_start.wait(10)
        return 0, ''

    def ask(self, name):
        text = steamplay.ask(self.prefix, name)
        self.assertIsNotNone(text, 'no helper answered')
        return text

    def spell(self, request):
        self.ask('tbegin')
        for digit in netplay.request_hex(request):
            self.ask('t' + digit)
        return self.ask('tend')

    def test_host_spelled_restarts_into_the_lobby_with_the_request(self):
        self.assertIn("phase = 'idle'", self.ask('state'))
        self.assertIn('No master server for this release', self.ask('state'))
        answer = self.spell('HOST|1|3|Zoë 000 game')       # the same digit three times in a row
        self.assertIn("phase = 'restart'", answer)
        time.sleep(0.5)
        self.assertEqual(self.started, [])                 # not while the Steam-started X4 is open
        self.x4_open[0] = False
        self.assertTrue(fakes.wait_for(lambda: self.started))
        self.assertEqual(self.started, ['HOST|1|3|Zoë 000 game'])
        self.assertTrue(fakes.wait_for(lambda: "phase = 'hosting'" in self.ask('state')))   # the lobby's state, mirrored
        state = self.ask('state')
        self.assertIn("code = 'K7QX-3FNA'", state)
        self.assertNotIn('stamp = 41', state)              # restamped in the helper's own count
        self.assertIn("phase = 'hosting'", self.spell('BROWSE'))   # ignored while the game runs
        self.release_start.set()
        self.assertTrue(fakes.wait_for(lambda: "phase = 'idle'" in self.ask('state')))

    def test_one_digit_many_times_in_a_row(self):
        self.x4_open[0] = False
        self.release_start.set()
        name = 'D' * 60                                            # hex 44 44 ...: 120 opens of t4 in a row
        self.assertIn("phase = 'restart'", self.spell('JOINCODE|' + name))
        self.assertTrue(fakes.wait_for(lambda: self.started))
        self.assertEqual(self.started, [f'JOINCODE|{name}:47001'])   # a host name, at the default lobby port

    def test_browse_here_join_from_the_lan_and_the_clipboard(self):
        self.x4_open[0] = False
        self.release_start.set()                                   # each game ends at once
        self.assertIn("phase = 'browsing'", self.spell('BROWSE'))
        self.assertTrue(fakes.wait_for(lambda: "name = 'Basement'" in self.ask('state')), self.ask('state'))
        self.assertIn('compatible = false', self.ask('state'))     # another release, shown as such
        self.assertIn("phase = 'restart'", self.spell('JOINLAN|1'))
        self.assertTrue(fakes.wait_for(lambda: self.started))
        self.assertEqual(self.started, ['JOINCODE|192.168.10.9:47001'])   # resolved here: the lobby has no list
        self.assertTrue(fakes.wait_for(lambda: "phase = 'idle'" in self.ask('state')))
        self.assertIn("phase = 'restart'", self.ask('join'))       # the older pipe name: the code on the clipboard
        self.assertTrue(fakes.wait_for(lambda: len(self.started) == 2))
        self.assertEqual(self.started[1], 'JOINCODE|K7QX-3FNA')


if __name__ == '__main__':
    unittest.main()
