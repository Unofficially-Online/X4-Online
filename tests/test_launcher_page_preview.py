"""x4ls lobby --page-preview, --shots and --display (netlobby.run_preview, preview_states, place_window): the page
alone shows a file's states in turn for pictures of every phase (docs/MULTIPLAYER-PAGE.md, "Phase 2 plan"). X4 and
the picture are fakes here; the shipped states are also drawn by the real page under LuaJIT."""
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import __main__ as x4ls, lobby, netlobby  # noqa: E402

STATES = ROOT / 'tools/dev/page_preview_states.json'

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:   # pragma: no cover
    LuaRuntime = None


class FakeProc:
    pid = 4242

    def poll(self):
        return None


class FakeGame:
    """LobbyGame's surface for the preview: start() makes a fresh extension folder with lobby/."""

    def __init__(self, folder):
        self.folder = Path(folder)
        self.started = []
        self.proc = None
        self.target = None
        self.closed = False   # exited(): the player closed X4

    def start(self, form):
        self.started.append(form)
        self.proc = FakeProc()
        self.target = self.folder / f'start{len(self.started)}' / 'x4_lan_lab'
        (self.target / 'lobby').mkdir(parents=True)

    def exited(self):
        return self.closed


class Options:
    def __init__(self, preview, shots=None):
        self.page_preview = preview
        self.shots = shots
        self.show = False


class States(unittest.TestCase):
    def write(self, folder, rows):
        path = Path(folder) / 'states.json'
        path.write_text(json.dumps(rows), encoding='utf-8')
        return path

    def test_the_shipped_states_cover_every_phase_the_page_can_show_in_the_launchers_x4(self):
        states = netlobby.preview_states(STATES)
        phases = {phase for _, phase, _, _, _ in states}
        self.assertEqual(phases, {'idle', 'browsing', 'preparing', 'hosting', 'joining', 'waiting', 'loading', 'error',
                                  'nohelper', 'choosing', 'starting', 'restart'})
        names = [name for name, *_ in states]
        self.assertEqual(len(names), len(set(names)))   # one picture per name
        self.assertTrue(all(name.replace('-', '').isalnum() for name in names), names)

    def test_the_steam_path_states(self):
        states = netlobby.preview_states(ROOT / 'tools/dev/page_preview_steam.json')
        self.assertEqual([name for name, *_ in states], ['steam-pending', 'steam-on', 'hosting', 'session-page',
                                                         'session-page-leave', 'restart'])
        self.assertEqual(states[-1][1], 'restart')   # last: a pipe page quits X4 on it

    def test_a_bad_file_is_refused_before_x4_starts(self):
        with tempfile.TemporaryDirectory() as folder:
            for rows, why in (([], 'a JSON list'), ({'phase': 'idle'}, 'a JSON list'), ([{'lines': []}], 'no phase'),
                              ([{'phase': 'idle'}, {'phase': 'session'}], 'runs the session'),
                              ([{'phase': 'starting'}], 'opens the page'), ([{'phase': 'idle', 'lines': 'x'}], 'a list')):
                with self.subTest(rows=rows), self.assertRaisesRegex(SystemExit, why):
                    netlobby.preview_states(self.write(folder, rows))
            with self.assertRaisesRegex(SystemExit, 'states.json'):
                netlobby.preview_states(Path(folder) / 'missing' / 'states.json')
            name, phase, lines, fields, hold = netlobby.preview_states(self.write(folder, [
                {'name': 'a b/c', 'phase': 'hosting', 'lines': ['x', 2], 'hold': 1.5, 'code': 'K7QX-3FNA'}]))[0]
            self.assertEqual((name, phase, lines, fields, hold), ('a-b-c', 'hosting', ['x', '2'], {'code': 'K7QX-3FNA'}, 1.5))


class Preview(unittest.TestCase):
    def test_each_state_is_written_in_turn_held_and_pictured(self):
        with tempfile.TemporaryDirectory() as folder:
            states = Path(folder) / 'states.json'
            states.write_text(json.dumps([
                {'name': 'idle', 'phase': 'idle', 'lines': ['Hello']},
                {'name': 'hosting', 'phase': 'hosting', 'lines': [], 'code': 'K7QX-3FNA', 'hold': 1,
                 'players': [{'slot': 1, 'ready': True, 'host': True}]}]), encoding='utf-8')
            game = FakeGame(Path(folder) / 'game')
            written, waits, shots, logs = [], [], [], []

            def wait(seconds):
                waits.append(seconds)
                written.append((game.target / 'lobby' / 'state.lua').read_text(encoding='utf-8'))

            def shoot(pid, path):
                shots.append((pid, Path(path).name))
                return True
            o = Options(states, Path(folder) / 'shots')
            with mock.patch('lockstep.launcher.hotjoin.set_priority', return_value=True) as priority:
                result = netlobby.run_preview(o, game, logs.append, shoot=shoot, wait=wait)
            self.assertTrue(o.show)                                  # the page opens by itself
            self.assertEqual(game.start_wait, netlobby.PREVIEW_START_WAIT_S)   # a slow start beside busy clients is waited for
            self.assertEqual([form.overlay for form in game.started], [None])   # the page alone
            priority.assert_called_once_with(4242, netlobby.BELOW_NORMAL_PRIORITY)
            self.assertEqual(written[0], lobby.state_text(1, 'idle', ['Hello']))
            self.assertEqual(written[1], lobby.state_text(2, 'hosting', [], code='K7QX-3FNA',
                                                          players=[{'slot': 1, 'ready': True, 'host': True}]))
            self.assertEqual(waits, [netlobby.PREVIEW_HOLD_S + netlobby.PREVIEW_OPEN_S, 1.0])
            self.assertEqual(shots, [(4242, '01-idle.png'), (4242, '02-hosting.png')])
            self.assertEqual(result['outcome'], 'preview')
            self.assertEqual([Path(p).name for p in result['shots']], ['01-idle.png', '02-hosting.png'])

    def test_without_shots_the_states_are_only_shown(self):
        with tempfile.TemporaryDirectory() as folder:
            game = FakeGame(Path(folder) / 'game')
            logs = []
            with mock.patch('lockstep.launcher.hotjoin.set_priority', return_value=True):
                result = netlobby.run_preview(Options(STATES), game, logs.append, wait=lambda s: None)
            self.assertEqual(result['outcome'], 'preview')
            self.assertEqual(result['shots'], [])
            self.assertEqual(sum('lobby: preview ' in row and '/' in row for row in logs), result['states'])


class PipePreview(unittest.TestCase):
    """--preview-pipe: the states answered through the helper's pipe names, the page's requests logged."""

    def test_the_states_go_through_the_pipes_and_the_requests_come_back(self):
        from lockstep.launcher import netplay, steamplay
        served = {}

        def pipes(answer, names):
            served['answer'], served['names'] = answer, names
            return types.SimpleNamespace(closed=False, close=lambda: served.update(closed=True))

        def wait(seconds):   # while a state is shown, the page asks for it and sends a spelled request and an old one
            served.setdefault('states', []).append(served['answer']('state'))
            if len(served['states']) == 1:
                for name in ['tbegin'] + ['t' + d for d in netplay.request_hex('STEAMRESTART')] + ['tend']:
                    served['answer'](name)
                served['answer']('join')

        with tempfile.TemporaryDirectory() as folder:
            states = Path(folder) / 'states.json'
            states.write_text(json.dumps([{'name': 'pending', 'phase': 'idle', 'steam_play': {'on': False, 'pending': True}},
                                          {'name': 'hosting', 'phase': 'hosting', 'code': 'K7QX-3FNA'}]), encoding='utf-8')
            o = Options(states)
            o.preview_pipe = True
            logs = []
            with mock.patch('lockstep.launcher.hotjoin.set_priority', return_value=True):
                result = netlobby.run_preview(o, FakeGame(Path(folder) / 'game'), logs.append, wait=wait, pipes=pipes)
        self.assertEqual(set(served['names']), {'state', *steamplay.TEXT_NAMES, *steamplay.LEGACY_NAMES})
        self.assertEqual(served['states'], [lobby.state_text(1, 'idle', [], steam_play={'on': False, 'pending': True}),
                                            lobby.state_text(2, 'hosting', [], code='K7QX-3FNA')])
        self.assertEqual(result['requests'], ['STEAMRESTART', 'JOIN'])
        self.assertTrue(served['closed'])
        self.assertIn('lobby: preview request STEAMRESTART', logs)

    def test_the_page_of_a_pipe_preview_talks_to_its_own_prefix(self):
        text = lobby.lobby_config([], True, netplay=True, pipe=netlobby.PREVIEW_PIPE)
        self.assertIn("pipe = 'x4mp-preview', preview = true", text)
        self.assertNotIn('x4online', text)   # never the release's prefix: no real helper answers it
        self.assertNotIn('pipe', lobby.lobby_config([], True, netplay=True))
        with self.assertRaisesRegex(SystemExit, '--preview-pipe needs --page-preview'):
            netlobby.run(lobby.LobbyOptions(preview_pipe=True))


class CommandLine(unittest.TestCase):
    def run_lobby(self, argv):
        chosen = []
        with mock.patch.object(lobby, 'run', lambda options: chosen.append(options) or {'outcome': 'preview'}), \
                mock.patch('builtins.print'):
            code = x4ls.main(['lobby', *argv])
        return code, chosen[0]

    def test_the_switches_reach_the_lobby(self):
        code, o = self.run_lobby(['--page-preview', str(STATES), '--shots', 'shots', '--display', '3', '--preview-pipe'])
        self.assertEqual(code, 0)   # a preview is a success
        self.assertEqual((o.page_preview, o.shots, o.display, o.preview_pipe), (STATES, Path('shots'), 3, True))
        _, o = self.run_lobby([])
        self.assertEqual((o.page_preview, o.shots, o.display, o.preview_pipe), (None, None, None, False))

    def test_shots_need_a_preview_and_the_preview_is_netplays(self):
        with self.assertRaisesRegex(SystemExit, '--shots needs --page-preview'):
            netlobby.run(lobby.LobbyOptions(shots=Path('shots')))
        with self.assertRaisesRegex(SystemExit, 'leave out --server-file'):
            lobby.run(lobby.LobbyOptions(server_file=Path('server.json'), page_preview=STATES))


class Window(unittest.TestCase):
    def test_the_window_goes_to_its_monitor_and_a_preview_gives_the_focus_back(self):
        class Proc:
            pid = 7
            polls = 0

            def poll(self):
                self.polls += 1
                return None if self.polls < 4 else 0

        class O:
            display, width, height = 3, 1280, 720
        with mock.patch.object(netlobby.session, 'tile') as tile, mock.patch.object(netlobby.session, 'yield_focus') as yields, \
                mock.patch.object(netlobby.time, 'sleep'):
            netlobby.place_window(Proc(), O(), keep_focus_away=True).join(5)
        tile.assert_called_with([7], 1280, 720, 1, 3)
        yields.assert_called_with({7})
        O.display = None
        with mock.patch.object(netlobby.session, 'tile') as tile, mock.patch.object(netlobby.session, 'yield_focus') as yields, \
                mock.patch.object(netlobby.time, 'sleep'):
            netlobby.place_window(Proc(), O()).join(5)
        tile.assert_not_called()
        yields.assert_not_called()


class StateForTheViews(unittest.TestCase):
    """What the launcher adds for the page's views (docs/MULTIPLAYER-PAGE.md, "State"): the hosted game's summary, and
    an unexpected failure in plain words with the exception in the log only."""

    def test_the_hosted_games_summary(self):
        from lockstep.launcher import nethost

        class Files:
            players = 4
            recipe = {'open': [4], 'session': {'base_start': 'x4ep1_gamestart_fight'}}
        self.assertEqual(nethost.game_summary('Friday|night', True, Files()),
                         {'name': 'Fridaynight', 'public': True, 'start': 'x4ep1_gamestart_fight', 'players_max': 4, 'open': [4]})
        Files.recipe = {}
        self.assertEqual(nethost.game_summary('X4 game', False, Files())['start'], None)

    def test_an_unexpected_failure_is_said_plainly(self):
        import netplay_fakes as fakes
        from lockstep.launcher import netkit, netplay
        folder = fakes.Folder()
        self.addCleanup(folder.cleanup)
        kit = netkit.Kit(fakes.release_kit(folder.path / 'kit'))
        logs = []
        flow = netplay.Flow(netplay.FlowConfig(kit=kit, release=kit.release, master_url=None), netplay.Backend(), log=logs.append)

        class Broken(netplay.Job):
            def run(self):
                raise KeyError('slot')
        job = Broken(flow, netplay.Request('BROWSE'))
        flow.job = job
        job.start()
        job.thread.join(5)
        _, phase, lines, _ = flow.state.view()
        self.assertEqual(phase, 'error')
        self.assertEqual(lines[:2], ('Something went wrong in X4 Online.', "The launcher's log has the details."))
        self.assertFalse([line for line in lines if 'KeyError' in line])
        self.assertTrue([row for row in logs if "Broken failed: KeyError: 'slot'" in row], logs)


@unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
class PageDrawsThem(unittest.TestCase):
    """Every shipped state through the real page (the page test's fake stock menu): drawn without an error row."""

    def test_every_state_is_drawn_without_an_error(self):
        import test_native_gate_lobby_page as page
        # the page as the launcher installs it, and with Single-player (x4ls lobby --single-player-mark: Steam's Play)
        for single in (False, True):
            lua = LuaRuntime()
            lua.execute(page.STOCK + '\nX4LAN_CONFIG = {enabled = false, lobby = {netplay = true, show = true'
                        + (', single = true' if single else '') + '}}\n')
            lua.execute(page.LOBBY)
            g = lua.globals()
            g.scripts.onUpdate()
            for index, (name, phase, lines, fields, _) in enumerate(netlobby.preview_states(STATES), 1):
                text = lobby.state_text(index, phase, lines, **fields)
                with self.subTest(state=name, single=single):
                    lua.execute('X4LAN_LOBBY.state = (function() ' + text + ' end)()')
                    g.menu.submenuHandler('x4lan_lobby')
                    logs = [g.logs[i] for i in range(1, len(g.logs) + 1)]
                    self.assertFalse([row for row in logs if 'ERROR' in row], logs)
                    self.assertGreaterEqual(len(g.drawn), 7)   # at least the lines and their separator
                    ids = {g.drawn[i].id for i in range(1, len(g.drawn) + 1)}
                    if 'steam_play' in fields:   # the Steam switch of the start rows (steamlib.py)
                        self.assertIn('x4lan_lobby_steamplay', ids)
                    if single and phase in ('idle', 'browsing'):
                        self.assertIn('x4lan_lobby_single', ids)


if __name__ == '__main__':
    unittest.main()
