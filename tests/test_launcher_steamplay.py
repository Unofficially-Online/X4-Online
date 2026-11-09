"""lockstep.launcher.steamplay: the helper behind "Play in Steam, then Multiplayer". Real named pipes, a fake kit, a fake
session start; the page's Lua under LuaJIT talks to the helper through the pipes as X4 would. No X4 runs here."""
import itertools
import json
import os
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import steamplay  # noqa: E402

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:   # pragma: no cover
    LuaRuntime = None

NUMBER = itertools.count()


def fake_kit(folder):
    (folder / 'overlay').mkdir(parents=True)
    (folder / 'overlay' / 'play-recipe.json').write_text(json.dumps({'players': 2, 'mode': 'coop', 'minutes': 40.0}))
    return folder


class Menu(unittest.TestCase):
    def test_the_menu_extension_goes_into_x4_and_out_again(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = steamplay.write_menu(tmp / 'steam-menu')
            self.assertEqual(sorted(steamplay.tree(source)), ['content.xml', 'ui.xml', 'ui/config.lua', 'ui/native_gate_lobby.lua'])
            self.assertIn("lobby = {pipe = 'x4mp-playtest', server = true}", (source / 'ui/config.lua').read_text())
            self.assertIn('save="false"', (source / 'content.xml').read_text())     # no save depends on the menu
            x4 = tmp / 'X4'
            (x4 / 'extensions' / 'ego_dlc_boron').mkdir(parents=True)
            (x4 / 'extensions' / 'ego_dlc_boron' / 'content.xml').write_text('<content id="ego_dlc_boron" />')
            self.assertTrue(steamplay.install_menu(source, x4))
            self.assertFalse(steamplay.install_menu(source, x4))                    # up to date: nothing written
            (source / 'ui/config.lua').write_text(steamplay.MENU_CONFIG + '-- newer\n')
            self.assertTrue(steamplay.install_menu(source, x4))
            self.assertTrue(steamplay.remove_menu(x4))
            self.assertEqual(sorted(p.name for p in (x4 / 'extensions').iterdir()), ['ego_dlc_boron'])   # nothing else is touched
            self.assertFalse(steamplay.remove_menu(x4))
            other = x4 / 'extensions' / steamplay.MENU_ID
            other.mkdir()
            (other / 'content.xml').write_text('<content id="somebody_elses" />')
            self.assertFalse(steamplay.remove_menu(x4))                             # a folder of that name that is not ours stays
            self.assertTrue(other.is_dir())


@unittest.skipUnless(os.name == 'nt', 'named pipes: Windows')
class Helper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.kit = fake_kit(Path(self.tmp.name) / 'kit')
        self.prefix = f'x4mp-test-{os.getpid()}-{next(NUMBER)}'
        self.logs, self.starts, self.said = [], [], []
        self.x4_open, self.result = [True], [(0, '')]
        self.say, steamplay.say = steamplay.say, lambda text, error=False: self.said.append((text, error))
        self.helper = steamplay.Helper(self.kit, 'snapshot/save_009.xml.gz', prefix=self.prefix, log=self.logs.append,
                                       start=self.start, running=lambda: self.x4_open[0])
        self.thread = threading.Thread(target=self.helper.run, daemon=True)
        self.thread.start()

    def tearDown(self):
        steamplay.ask(self.prefix, 'stop', timeout=1)
        self.thread.join(5)
        steamplay.say = self.say
        self.tmp.cleanup()

    def start(self):
        self.starts.append(self.helper.phase)
        return self.result[0]

    def state(self, name='state'):
        text = steamplay.ask(self.prefix, name)
        self.assertIsNotNone(text, 'no helper answered')
        return text

    def wait(self, what, seconds=5):
        end = time.time() + seconds
        while time.time() < end:
            if what():
                return
            time.sleep(0.02)
        self.fail('waited in vain')

    def test_join_restarts_x4_for_the_session(self):
        self.assertIn("phase = 'idle'", self.state())
        self.assertIn('2 players, coop, 40 minutes', self.state())
        self.assertIn("phase = 'restart'", self.state('join'))     # the page quits X4 on this
        self.assertIn("phase = 'restart'", self.state())
        time.sleep(0.5)
        self.assertEqual(self.starts, [])                          # not while the Steam-started X4 is still open
        self.x4_open[0] = False
        self.wait(lambda: self.starts)
        self.assertEqual(self.starts, ['playing'])
        self.wait(lambda: "phase = 'idle'" in self.state())
        self.assertEqual(self.said, [])
        self.assertIn("phase = 'restart'", self.state('join'))     # and again, for the next game
        self.wait(lambda: len(self.starts) == 2)

    def test_a_failed_start_is_shown_and_the_helper_goes_on(self):
        self.x4_open[0], self.result[0] = False, (1, 'X4.exe is not the pinned 9.00 build')
        self.state('join')
        self.wait(lambda: self.said)
        self.assertTrue(self.said[0][1])
        self.assertIn('not the pinned 9.00 build', self.said[0][0])
        self.wait(lambda: "phase = 'idle'" in self.state())

    def test_this_process_is_not_guarded(self):
        self.assertFalse(steamplay.redirection_guarded())          # tests are not started by Windows Installer

    def test_one_helper_at_a_time_and_stop(self):
        import _winapi
        owner = steamplay.first_instance(self.prefix)
        self.assertIsNotNone(owner)
        self.assertIsNone(steamplay.first_instance(self.prefix))   # a second helper sees the first
        _winapi.CloseHandle(owner)
        self.assertTrue(steamplay.stop(self.prefix, wait=3))
        self.thread.join(5)
        self.assertFalse(self.thread.is_alive())
        self.assertIsNone(steamplay.ask(self.prefix, 'state', timeout=0.3))   # nobody answers any more

    @unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
    def test_the_page_in_a_steam_started_x4(self):
        """native_gate_lobby.lua with lobby.pipe, as the menu extension runs it: no io, no package (protected UI mode)."""
        from test_launcher_lobby import LOBBY, SETUP
        lua = LuaRuntime()
        lua.execute(SETUP + f"\npackage = nil\nquits = 0\nfunction QuitGame() quits = quits + 1 end\n"
                            f"X4LAN_CONFIG = {{enabled = false, lobby = {{pipe = '{self.prefix}', server = true}}}}\n")
        lua.execute(LOBBY)
        g = lua.globals()

        def frames(n):
            for _ in range(n):
                g.scripts.onUpdate()

        frames(1)
        g.menu.submenuHandler('x4lan_lobby')
        rows = [g.drawn[i] for i in range(1, len(g.drawn) + 1)]
        self.assertEqual([r.id for r in rows][6:], ['line', 'x4lan_lobby_join', 'x4lan_lobby_cancel'])
        frames(40)
        self.assertEqual(rows[0].name(), '2 players, coop, 40 minutes')
        self.assertTrue(rows[7].selectable())
        rows[7].callback()                                         # Join: the helper sees the open of <pipe>-join
        self.assertEqual(self.helper.phase, 'restart')
        frames(40)
        self.assertIn('X4 closes now', rows[0].name())
        self.assertEqual(g.quits, 0)                               # the page shows why for a moment
        frames(90)
        self.assertEqual(g.quits, 1)
        frames(200)
        self.assertEqual(g.quits, 1)                               # once

    @unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
    def test_the_page_says_when_no_helper_runs(self):
        from test_launcher_lobby import LOBBY, SETUP
        lua = LuaRuntime()
        lua.execute(SETUP + "\npackage = nil\nX4LAN_CONFIG = {enabled = false, lobby = {pipe = 'x4mp-test-nobody', server = true}}\n")
        lua.execute(LOBBY)
        g = lua.globals()
        g.scripts.onUpdate()
        g.menu.submenuHandler('x4lan_lobby')
        rows = [g.drawn[i] for i in range(1, len(g.drawn) + 1)]
        for _ in range(40 * 3):
            g.scripts.onUpdate()
        self.assertEqual(rows[0].name(), 'The multiplayer helper is not running.')
        self.assertFalse(rows[7].selectable())


if __name__ == '__main__':
    unittest.main()
