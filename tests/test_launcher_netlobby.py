"""lockstep.launcher.netlobby and netcli: x4ls lobby's netplay backend without X4 (the pump between the page's log rows
and the state file, sessions installed by starting the lane's X4 again, the relay on this PC, the session's config
written before the page hears 'session') and the x4ls host / join / games command lines. X4, the relay and new-session
are fakes here."""
import io
import json
import sys
import types
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import __main__ as x4ls, lobby, netcli, netkit, netlobby, netplay, session  # noqa: E402
import netplay_fakes as fakes  # noqa: E402
from test_launcher_lobby import fake_overlay  # noqa: E402

NL = chr(13) + chr(10)


class FakeGame:
    """LobbyGame's surface for the backend and the pump: a target folder, a log, a generation, starts recorded."""

    def __init__(self, folder):
        self.folder = Path(folder)
        self.generation = 0
        self.target = None
        self.log_path = None
        self.form = None
        self.files = ['ui/native_gate_probe.lua']
        self.started = []
        self.taken = []
        self.held = 0
        self.profile_id = 45850
        self.proc = None
        self.x4_open = False      # open(): an X4 runs (the page the player looks at)
        self.takes = False        # take(): this X4 takes another session in place
        self.new_target()

    def new_target(self):
        self.generation += 1
        self.target = self.folder / f'start{self.generation}' / 'x4_lan_lab'
        (self.target / 'lobby').mkdir(parents=True)
        (self.target / 'ui').mkdir()
        self.log_path = self.folder / f'start{self.generation}' / 'x4ls-c1.log'
        self.log_path.write_bytes(b'')

    def runs(self, form):
        return form.same(self.form)

    def open(self):
        return self.x4_open

    def take(self, form):
        if not self.takes:
            return False
        self.taken.append(form)
        self.form = form
        return True

    def start(self, form):
        self.started.append(form)
        self.form = form
        self.x4_open = True
        self.new_target()

    def hold(self):
        self.held += 1
        self.x4_open = False


class Pumping(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.game = FakeGame(self.folder.path / 'game')
        kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))
        config = netplay.FlowConfig(kit=kit, release=kit.release, master_url=None)
        self.flow = netplay.Flow(config, fakes.FakeBackend(self.folder.path / 'work'),
                                 net=fakes.network(None, landisco=fakes.FakeLandisco()), log=lambda text: None)
        self.mirror = self.folder.path / 'mirror.lua'
        self.pump = netlobby.Pump(self.game, self.flow, self.mirror, log=lambda text: None).start()

    def tearDown(self):
        self.pump.stop()
        self.flow.cancel()
        self.folder.cleanup()

    def row(self, text):
        with self.game.log_path.open('ab') as log:
            log.write(('[=ERROR=] 4.2 [X4LAN] LOBBY ' + text + NL).encode())

    def state(self, target=None):
        path = (target or self.game.target) / 'lobby' / 'state.lua'
        return path.read_text() if path.is_file() else ''

    def test_rows_become_requests_and_the_state_follows_x4_restarts(self):
        self.assertTrue(fakes.wait_for(lambda: "phase = 'idle'" in self.state()))
        self.assertEqual(self.mirror.read_text(), self.state())          # the Steam helper's copy
        self.row('READY files=0')
        self.row('REQUEST BROWSE')
        self.assertTrue(fakes.wait_for(lambda: "phase = 'browsing'" in self.state()), self.state())
        old = self.game.target
        self.game.start(netlobby.Form())                                 # X4 started again: a new view and log
        self.assertTrue(fakes.wait_for(lambda: "phase = 'browsing'" in self.state()), self.state())   # rewritten there
        self.row('REQUEST CANCEL')
        self.assertTrue(fakes.wait_for(lambda: "phase = 'idle'" in self.state()), self.state())
        self.assertIn("phase = 'browsing'", self.state(old))             # the old view is not written any more


class Backend(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.game = FakeGame(self.folder.path / 'game')
        self.kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))
        self.options = lobby.LobbyOptions()
        self.backend = netlobby.LobbyBackend(self.options, self.kit, self.game, self.folder.path / 'run', log=lambda text: None)

    def tearDown(self):
        self.folder.cleanup()

    def test_install_starts_x4_again_only_for_another_session(self):
        files = fakes.make_session(self.folder.path / 'session')
        with mock.patch.object(netlobby.extension, 'verify_overlay', lambda overlay: None):
            seen = []
            self.backend.install_session(files, seen.append)
            self.assertEqual(self.game.started, [netlobby.Form(files.overlay, files.snapshot, 2)])
            self.backend.install_session(files, seen.append)                  # it runs that one already
            other = fakes.make_session(self.folder.path / 'other', players=3, save=b'SAVE-3')
            self.game.takes = True                                            # the same start-time files: in place
            self.backend.install_session(other, seen.append)
        self.assertEqual(len(self.game.started), 1)
        self.assertEqual(self.game.taken, [netlobby.Form(other.overlay, other.snapshot, 3)])
        self.assertEqual(seen, [0.1, 1.0, 0.1, 1.0])
        self.assertEqual(self.backend.default_form(), netlobby.Form())        # a release kit without sessions: the page

    def test_host_session_from_the_cache_or_made_beside_the_page(self):
        made = []
        beside_fails = []

        def make(kit, players, mode, base_start, progress, log, cancelled, steam_fix=False, open_slots=(), game=None,
                 lanes=None, beside=False):
            made.append((players, mode, base_start, beside) + ((tuple(open_slots),) if open_slots else ()))
            if beside and beside_fails:
                raise netkit.KitError('The session could not be made: X4 is running; close it first')
            return 'made'
        with mock.patch.object(netlobby.netkit, 'cached_session', lambda *args: 'cached'):
            self.assertEqual(self.backend.host_session(3, None, None), 'cached')
        self.assertEqual(self.game.held, 0)
        start = netkit.HOST_SESSION_START
        with mock.patch.object(netlobby.netkit, 'cached_session', lambda *args: None), \
                mock.patch.object(netlobby.netkit, 'make_session', make), \
                mock.patch.object(netlobby.netkit, 'beside_fits', lambda: True):
            # no X4 runs yet (x4ls host, a hand-over): made before X4 starts, nothing to hold
            self.assertEqual(self.backend.host_session(3, None, None), 'made')
            self.assertEqual((self.game.held, made[-1]), (0, (3, 'coop', start, False)))
            # the page runs: made beside it, which stays (no hold: no restart for the capture)
            self.game.x4_open = True
            self.assertEqual(self.backend.host_session(3, None, None), 'made')
            self.assertEqual((self.game.held, made[-1]), (0, (3, 'coop', start, True)))
            self.assertEqual(self.backend.host_session(3, None, None, open_slots=(3,)), 'made')   # drop-in slots too
            self.assertEqual(made[-1], (3, 'coop', start, True, (3,)))
            # the capture beside failed: X4 is held, and the session made once more alone
            beside_fails.append(True)
            self.assertEqual(self.backend.host_session(2, None, None), 'made')
            self.assertEqual(([entry[3] for entry in made[-2:]], self.game.held), ([True, False], 1))
        beside_fails.clear()
        with mock.patch.object(netlobby.netkit, 'cached_session', lambda *args: None), \
                mock.patch.object(netlobby.netkit, 'make_session', make), \
                mock.patch.object(netlobby.netkit, 'beside_fits', lambda: False):
            self.game.x4_open = True   # too little memory free for a second X4: held at once
            self.assertEqual(self.backend.host_session(2, None, None), 'made')
            self.assertEqual((self.game.held, made[-1][3]), (2, False))
        self.assertEqual(self.backend.save_folder().name, 'save')

        def fails(*args, **kwargs):
            raise netkit.KitError('X4 is running; close it first')
        with mock.patch.object(netlobby.netkit, 'cached_session', lambda *args: None), \
                mock.patch.object(netlobby.netkit, 'make_session', fails):
            with self.assertRaises(netkit.KitError):
                self.backend.host_session(2, None, None)
        self.assertEqual(self.game.started[-1], netlobby.Form())              # back to the page, which says why

    def test_the_page_alone(self):
        target, files = netlobby.install_page_only(self.folder.path / 'extensions', show=True)
        self.assertEqual(files, [])
        self.assertIn("X4LAN_CONFIG.lobby = {files = {}, show = true, netplay = true}", (target / 'ui/config.lua').read_text())
        self.assertIn('ui/native_gate_lobby.lua', (target / 'ui.xml').read_text())
        self.assertTrue((target / 'ui/sha256.lua').is_file() and (target / 'lobby').is_dir())

    def test_auto_requests(self):
        self.assertEqual([netlobby.auto_request(text) for text in ('', 'host', 'join', 'X4MP-1')],
                         ['', 'HOST', 'JOIN', 'JOINCODE|X4MP-1'])


class SessionStart(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.overlay = self.folder.path / 'overlay'
        fake_overlay(self.overlay, config='X4LAN_CONFIG.native_gate_pilot_window = {40,640}\nX4LAN_CONFIG.native_gate_lookahead = 2\n')
        self.snapshot = self.folder.path / 'save.xml.gz'
        self.snapshot.write_bytes(b'SAVE')
        self.options = lobby.LobbyOptions(lane=1)
        self.run_dir = self.folder.path / 'run'
        self.run_dir.mkdir()

    def tearDown(self):
        self.folder.cleanup()

    def test_the_relay_here_binds_every_interface_and_allows_what_the_lobby_saw(self):
        allow = self.folder.path / 'allow.txt'
        plan = netplay.RelayPlan(port=47000, token='ab' * 16, allow_file=allow, allow_from=('192.0.2.7',))
        start = netplay.SessionStart(slot=1, clients=2, relay_host='127.0.0.1', port=47000, token='ab' * 16, arm_at=1800000000,
                                     overlay=self.overlay, snapshot=self.snapshot, mode='direct', relay=plan)
        commands = []

        class Proc:
            pid = 4242

            def __init__(self, command, **kwargs):
                commands.append(command)
        record = {'relay_pid': None}
        with mock.patch.object(netlobby.subprocess, 'Popen', Proc):
            relay, relay_log = netlobby.start_relay(self.options, start, self.run_dir, record, lambda: None, lambda text: None)
        relay_log.close()
        command = commands[0]
        self.assertEqual(command[command.index('--bind') + 1], '0.0.0.0')
        self.assertEqual(command[command.index('--allow-from') + 1].split(','), ['127.0.0.1', '192.0.2.7', '0.0.0.0'])
        self.assertEqual(command[command.index('--allow-file') + 1], str(allow))
        self.assertEqual((command[command.index('--port') + 1], command[command.index('--token') + 1]), ('47000', 'ab' * 16))
        self.assertIn('--keep-going', command)
        self.assertEqual(record['relay_pid'], 4242)
        no_relay = netplay.SessionStart(slot=2, clients=2, relay_host='203.0.113.50', port=47200, token='ab' * 16,
                                        arm_at=1800000000, overlay=self.overlay, snapshot=self.snapshot, mode='relay')
        self.assertEqual(netlobby.start_relay(self.options, no_relay, self.run_dir, record, lambda: None, print), (None, None))

    def test_the_config_is_written_before_the_page_hears_session(self):
        game = FakeGame(self.folder.path / 'game')
        game.form = netlobby.Form(self.overlay, self.snapshot, 2)
        kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))
        flow = netplay.Flow(netplay.FlowConfig(kit=kit, release=kit.release), fakes.FakeBackend(self.folder.path / 'work'),
                            net=fakes.network(None), log=lambda text: None)
        start = netplay.SessionStart(slot=2, clients=2, relay_host='192.168.10.7', port=47000, token='cd' * 16,
                                     arm_at=1800000000, overlay=self.overlay, snapshot=self.snapshot, mode='lan')
        seen = {}

        def monitor(play, run_dir, relay, clients, log):
            seen['config'] = (game.target / 'ui/config.lua').read_text()
            seen['running'] = flow.session_started.is_set()
            seen['ticks'] = play.ticks
            return {'outcome': 'played'}
        with mock.patch.object(netlobby.session, 'monitor_play', monitor):
            result = netlobby.play_session(self.options, flow, game, start, None, self.run_dir, lambda text: None)
        self.assertTrue(seen['running'])
        for part in ('X4LAN_CONFIG.slot = "p2"', 'X4LAN_CONFIG.token = "' + 'cd' * 16 + '"', 'X4LAN_CONFIG.port = 47000',
                     "native_gate_relay_host = '192.168.10.7'", 'native_gate_arm_at = 1800000000', 'session = true'):
            self.assertIn(part, seen['config'])
        self.assertEqual(seen['ticks'], 640 + session.PLAY_TAIL_TICKS)
        self.assertEqual((result['slot'], result['mode']), (2, 'lan'))
        other = netplay.SessionStart(slot=2, clients=2, relay_host='192.168.10.7', port=47000, token='cd' * 16,
                                     arm_at=1800000000, overlay=self.folder.path / 'elsewhere', snapshot=self.snapshot, mode='lan')
        with self.assertRaisesRegex(SystemExit, 'not the one this X4 runs'):
            netlobby.play_session(self.options, flow, game, other, None, self.run_dir, lambda text: None)

    def test_a_drop_in_slot_closes_this_x4_and_hot_joins_the_running_game(self):
        game = FakeGame(self.folder.path / 'game')
        kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))
        flow = netplay.Flow(netplay.FlowConfig(kit=kit, release=kit.release), fakes.FakeBackend(self.folder.path / 'work'),
                            net=fakes.network(None), log=lambda text: None)
        start = netplay.SessionStart(slot=2, clients=2, relay_host='203.0.113.50', port=47205, token='ef' * 16,
                                     arm_at=1700000000, overlay=self.overlay, snapshot=self.snapshot, mode='relay', late=True)
        joins = []

        def hot_join(options, log):
            joins.append(options)
            return {'outcome': 'played'}
        for others_running in (False, True):   # True: another player's X4 on this PC (a second window)
            with mock.patch('lockstep.launcher.hotjoin.hot_join', hot_join), \
                    mock.patch.object(netlobby.session, 'x4_running', lambda: others_running):
                result = netlobby.late_join(self.options, flow, game, start, lambda text: None)
            self.assertEqual((result['outcome'], result['late'], result['slot']), ('played', True, 2))
        alone, beside = joins
        self.assertEqual((alone.relay, alone.token, alone.slot, alone.offer_wait), ('203.0.113.50:47205', 'ef' * 16, 2, 780))
        self.assertEqual(alone.overlay, self.overlay.resolve())
        self.assertEqual((alone.same_pc, alone.state), (False, self.options.state))
        self.assertTrue(beside.same_pc)
        self.assertNotEqual(beside.state, self.options.state)   # its own state file beside the other X4's session
        self.assertEqual(game.held, 2)                            # this lane's X4 closed each time
        self.assertTrue(flow.session_started.is_set())

        def refused(options, log):
            raise SystemExit('the relay refused the join: slot_not_absent')
        with mock.patch('lockstep.launcher.hotjoin.hot_join', refused), \
                mock.patch.object(netlobby.session, 'x4_running', lambda: False):
            result = netlobby.late_join(self.options, flow, game, start, lambda text: None)
        self.assertEqual(result['outcome'], 'failed')
        self.assertIn('slot_not_absent', result['ended'])


class CommandLines(unittest.TestCase):
    def run_x4ls(self, argv):
        chosen = []
        with mock.patch.object(netcli, 'run_lobby', lambda options: chosen.append(options) or 0):
            code = x4ls.main(argv)
        return code, chosen[0] if chosen else None

    def test_host_and_join_are_the_lobby_with_its_first_request(self):
        code, options = self.run_x4ls(['host', '--players', '3', '--name', 'Friday|night', '--public', '--no-master',
                                       '--port', '47010', '--show'])
        self.assertEqual(code, 0)
        self.assertEqual((options.request, options.no_master, options.port, options.show), ('HOST|1|3|Fridaynight', True, 47010, True))
        _, options = self.run_x4ls(['join', '--code', 'k7qx-3fna', '--master', 'https://master.example.org/x4mp'])
        self.assertEqual((options.request, options.master), ('JOINCODE|k7qx-3fna', 'https://master.example.org/x4mp'))
        _, options = self.run_x4ls(['join', '--address', '10.20.0.5:47001,100.64.1.2'])
        self.assertEqual(options.request, 'JOINCODE|10.20.0.5:47001,100.64.1.2')
        _, options = self.run_x4ls(['join', '--game-id', 'g1'])
        self.assertEqual(options.request, 'JOINGAME|g1')
        with self.assertRaisesRegex(SystemExit, 'one of --code'):
            self.run_x4ls(['join', '--code', 'K7QX-3FNA', '--game-id', 'g1'])
        with self.assertRaisesRegex(SystemExit, 'needs --overlay'):
            self.run_x4ls(['join', '--relay', '192.0.2.1:47000', '--token', 'ab' * 16, '--slot', '2'])
        with self.assertRaisesRegex(SystemExit, '--players must be'):
            self.run_x4ls(['host', '--players', '9'])

    def test_games_lists_the_public_and_the_lan(self):
        master = fakes.FakeMaster()
        master.api_create_game('Friday', True, 'rel-1', 4, 'a' * 64)
        master.api_create_game('Secret', False, 'rel-1', 2, 'a' * 64)
        lan = fakes.FakeLandisco([{'ip': '192.168.10.9', 'candidates': ['192.168.10.9'], 'release': 'rel-2', 'code': '-',
                                   'name': 'Basement', 'tcp_port': 47001, 'players': 1, 'players_max': 2}])
        kit = types.SimpleNamespace(release='rel-1')
        rows = netcli.list_games(kit, 'http://master.example.org:47100', lan=True, net=fakes.network(master, landisco=lan))
        self.assertEqual([(row['where'], row['name'], row['compatible']) for row in rows],
                         [('public', 'Friday', True), ('lan', 'Basement', False)])   # a private game is not listed
        args = types.SimpleNamespace(kit=None, master='http://master.example.org:47100', no_master=False, lan=True, json=True)
        out = io.StringIO()
        with redirect_stdout(out):
            netcli.run_games(args, net=fakes.network(master, landisco=lan))
        self.assertEqual(len(json.loads(out.getvalue())), 2)
        self.assertEqual(netcli.list_games(kit, None, lan=False, net=fakes.network(master)), [])


if __name__ == '__main__':
    unittest.main()
