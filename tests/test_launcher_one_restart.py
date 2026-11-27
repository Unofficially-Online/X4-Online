"""From X4's Multiplayer button into the game: one restart from an X4 that Steam started (its profile is the player's
own), none from an X4 already in the multiplayer profile; restarts counted from the log rows (netkit.count_restarts:
netkit.RESTART_MARK, netkit.SESSION_ALONE_MARK). The Steam helper's hand-over jobs (the host's session found in the kit,
made beside the Steam-started X4 or, without room for a second X4, made with it closed behind a notice; a joiner's
session downloaded and its overlay built before X4 closes), the lobby's first start straight into the session, an X4
that takes another session in place, and the helper's stamps. X4, new-session, the master and the overlay build are
fakes here; no X4 runs."""
import itertools
import shutil
import sys
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import lobby, netkit, netlobby, netplay, steamplay  # noqa: E402
import netplay_fakes as fakes  # noqa: E402

MASTER_URL = 'http://master.example.org:47100'


class FakeProc:
    def __init__(self, pid):
        self.pid = pid
        self.ended = False

    def poll(self):
        return 0 if self.ended else None

    def wait(self, timeout=None):
        return 0


class QuietLobbyGame(netlobby.LobbyGame):
    """netlobby.LobbyGame without X4: launch() installs the overlay into a folder and gives a stand-in process; the rest
    (the restart rows, taking a session in place, the forms) is LobbyGame's own."""
    pids = itertools.count(9000)

    def __init__(self, folder, log, o=None):
        folder = Path(folder)
        super().__init__(o or lobby.LobbyOptions(lane=1), folder / 'X4', folder / 'view', 1, folder / 'run',
                         {'views': [], 'pids': []}, lambda: None, log)
        self.launched = []
        self.staged = []
        self.seeds = []           # the seeds told (force_seed: x4mem in a real X4)
        self.seed_tellable = True

    def can_force_seed(self):
        return self.seed_tellable

    def force_seed(self, seed):
        self.seeds.append(seed)

    def launch(self, form):
        self.launched.append(form)
        target = Path(self.view) / f'start{len(self.launched)}' / 'x4_lan_lab'
        target.mkdir(parents=True)
        if form.overlay:
            shutil.copytree(form.overlay, target, dirs_exist_ok=True)
        (target / 'lobby').mkdir(exist_ok=True)
        (target / 'ui').mkdir(exist_ok=True)
        self.target = target
        listed = form.overlay and (Path(form.overlay) / 'ui.xml.files').is_file()   # fake_build's overlays have none
        self.files = lobby.overlay_files(form.overlay) if listed else []
        return FakeProc(next(self.pids))

    def wait_start_menu(self):
        pass

    def stage(self, snapshot):
        self.staged.append(Path(snapshot))

    def end_process(self):
        self.proc.ended = True
        self.proc = None


class Case(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.root = self.folder.path
        self.builds = []
        patcher = mock.patch.object(netkit.subprocess, 'run', fakes.fake_build(self.builds))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.flows = []

    def tearDown(self):
        for flow in self.flows:
            flow.end_session()
            flow.cancel()
        for flow in self.flows:
            job = flow.job
            if job is not None and job.thread is not None:
                job.thread.join(5)
        self.folder.cleanup()

    # the Steam side
    def helper(self, kit, net=None, master_url='', fits=True, make=None, starts=None):
        self.helper_logs = []
        self.made = []
        self.notices = []
        self.x4_open = [True]   # the Steam-started X4

        def made(kit, players, mode, base_start, progress, log, cancelled, steam_fix=False, open_slots=(), game=None,
                 lanes=None, beside=False, story=None):
            self.made.append({'players': players, 'beside': beside, 'steam_x4_open': self.x4_open[0],
                              'open': tuple(open_slots), 'start': base_start, 'story': story})
            log(f'{netkit.SESSION_BESIDE_MARK if beside else netkit.SESSION_ALONE_MARK}: {players} players')
            if make is not None:
                make(beside)
            if progress:
                progress(0.5)
            shutil.rmtree(kit.sessions / netkit.host_session_key(kit, players, mode, base_start, open_slots),
                          ignore_errors=True)   # as make_session: a session made again replaces the one there
            folder = fakes.host_session(kit.root, players, open_slots, base_start=base_start, story=bool(story))
            for left in ('capture-overlay', 'overlay.base'):
                shutil.rmtree(folder / left)
            return netkit.cached_session(kit, players, mode, base_start, open_slots)
        notices = self.notices

        class Notice:
            def __init__(self, title, text):
                self.text = text
                notices.append(self)
                self.shown = self.closed = False

            def show(self):
                self.shown = True

            def close(self):
                self.closed = True
        helper = steamplay.NetHelper(kit.root, prefix='x4mp-onerestart-test', log=self.helper_logs.append,
                                     start=self.lobby_start, running=lambda: self.x4_open[0], net=net or fakes.network(None),
                                     master_url=master_url, make=made, fits=lambda: fits,
                                     release_mutex=lambda log: ([], 0), notice=Notice,
                                     starts=starts or (lambda mode: None))   # never this PC's own game data
        helper.flow.backend.installed_dlcs = lambda: None
        self.flows.append(helper.flow)
        return helper

    def lobby_start(self, request):
        """The helper's start of x4ls lobby --request: recorded; the lobby side runs in run_lobby."""
        self.started_requests.append(request)
        return 0, ''

    def hand_over(self, helper, text, phase_seconds=10):
        """The page's request through the helper; the restart it asks for; the Steam-started X4 closing; the lobby
        started with the request. Returns that request."""
        self.started_requests = []
        helper.flow.request(text)
        reached = fakes.wait_for(lambda: helper.flow.state.phase in ('restart', 'error'), phase_seconds)
        self.assertTrue(reached, (helper.flow.state.phase, helper.flow.state.lines, self.helper_logs[-5:]))
        self.assertEqual(helper.flow.state.phase, 'restart', (helper.flow.state.lines, self.helper_logs[-5:]))
        request, before = helper.flow.take_handover_with()
        self.x4_open[0] = False   # the page quit X4
        helper.session(request, before)
        self.assertEqual(self.started_requests, [request])
        return request

    # the lobby side
    def run_lobby(self, kit, request, net=None, master_url=None, timeout=3, game=None, page_first=False):
        """x4ls lobby --request: its flow and wait_for_start with a QuietLobbyGame; (game, logs, start or None)."""
        logs = []
        o = lobby.LobbyOptions(lane=1, page_first=page_first)
        o.request = request
        o.lobby_timeout = timeout
        o.master_url = master_url
        game = game or QuietLobbyGame(self.root / f'lobby{len(self.flows)}', logs.append, o)
        game.log = logs.append
        backend = netlobby.LobbyBackend(o, kit, game, self.root / f'run{len(self.flows)}', logs.append)
        backend.installed_dlcs = lambda: None
        config = netplay.FlowConfig(kit=kit, release=kit.release, master_url=master_url, port=fakes.free_port_pair(),
                                    load_wait=20)
        flow = netplay.Flow(config, backend, net=net or fakes.network(None), log=logs.append)
        self.flows.append(flow)
        with mock.patch.object(netlobby.extension, 'verify_overlay', lambda overlay: None):   # fake_build's overlays
            _result, start = netlobby.wait_for_start(o, flow, game, backend, logs.append)
        logs.append(f'(the test: the lobby ended in {flow.state.phase}: {" ".join(flow.state.lines)})')
        return game, logs, start, flow

    def restarts(self, *logs):
        return netkit.count_restarts('\n'.join(line for rows in logs for line in rows))


class SteamHost(Case):
    def test_a_ready_session_one_restart_straight_into_the_game(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        fakes.host_session(kit.root, 2)
        helper = self.helper(kit)
        request = self.hand_over(helper, 'HOST|0|2|Friday')
        self.assertEqual(request, 'HOST|0|2|Friday')
        self.assertEqual(self.made, [])                                      # nothing was made
        game, logs, _start, _flow = self.run_lobby(kit, request)
        self.assertEqual([form.overlay for form in game.launched],           # one X4: the session's, not the page first
                         [kit.sessions / netkit.host_session_key(kit, 2, 'coop', netkit.HOST_SESSION_START) / 'overlay'])
        self.assertEqual(self.restarts(self.helper_logs, logs), 1)

    def test_the_pages_start_is_the_sessions_and_a_story_start_gives_every_player_its_story(self):
        cadet = 'x4ep1_gamestart_terran1'
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        fakes.host_session(kit.root, 2, base_start=cadet, story=False)       # made before 26 Nov: the story for player 1 only
        helper = self.helper(kit, starts=lambda mode: fakes.STARTS)
        request = self.hand_over(helper, f'HOST|0|2|Cadets|{cadet}')
        self.assertEqual(request, f'HOST|0|2|Cadets|{cadet}')               # the lobby hosts the same start
        self.assertEqual([(entry['start'], entry['story'], entry['beside']) for entry in self.made], [(cadet, True, True)])
        self.assertTrue(any('preparing a new game' in row.lower() or 'SESSION MADE beside' in row for row in self.helper_logs))
        game, logs, _start, _flow = self.run_lobby(kit, request)
        key = netkit.host_session_key(kit, 2, 'coop', cadet)
        self.assertEqual([form.overlay for form in game.launched], [kit.sessions / key / 'overlay'])   # that session, one X4
        self.assertEqual(self.restarts(self.helper_logs, logs), 1)
        self.assertTrue(netkit.session_stories(netkit.read_json(kit.sessions / key / 'session.json'), 2))
        # the next game of that start finds it: nothing is made, still one restart
        self.made.clear()
        helper.flow.cancel()
        self.helper_logs.clear()
        self.assertEqual(self.hand_over(helper, f'HOST|1|2|Again|{cadet}'), f'HOST|1|2|Again|{cadet}')
        self.assertEqual(self.made, [])
        # a start this PC cannot host is said on the Steam page: X4 does not close, nothing is made
        helper.flow.cancel()
        helper.flow.request('HOST|0|2|x|x4ep1_gamestart_trade')
        self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'error'), helper.flow.state.lines)
        self.assertTrue(any('on_foot' in line for line in helper.flow.state.lines), helper.flow.state.lines)
        self.assertIsNone(helper.flow.take_handover())
        self.assertEqual(self.made, [])

    def test_the_steam_x4_says_once_that_x4_online_skips_the_restart(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        fakes.host_session(kit.root, 2)
        helper = self.helper(kit)
        notices = []
        for _ in range(2):
            helper.flow.request('HOST|0|2|Friday')
            self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'restart'))
            notices.append(' '.join(helper.flow.state.lines))
            helper.flow.take_handover_with()
            helper.flow.cancel()
        self.assertIn('Next time start "X4 Online"', notices[0])
        self.assertNotIn('Next time', notices[1])

    def test_a_new_session_is_made_beside_the_steam_x4_then_one_restart(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        helper = self.helper(kit)
        seen = []

        def watch():   # what the page shows while the session is made
            while not seen or seen[-1][0] != 'restart':
                _stamp, phase, lines, fields = helper.flow.state.view()
                if not seen or seen[-1][:2] != (phase, lines):
                    seen.append((phase, lines, fields))
                time.sleep(0.01)
        watcher = threading.Thread(target=watch, daemon=True)
        watcher.start()
        request = self.hand_over(helper, 'HOST|1|3|Friday')
        watcher.join(5)
        self.assertEqual([(entry['players'], entry['beside'], entry['steam_x4_open']) for entry in self.made],
                         [(3, True, True)])                                  # beside the Steam-started X4, which was open
        preparing = [lines for phase, lines, _fields in seen if phase == 'preparing']
        self.assertTrue(any('about 2 minutes' in ' '.join(lines) and 'X4 stays here' in ' '.join(lines)
                            for lines in preparing), seen)
        self.assertEqual(self.notices, [])                                   # no notice: X4 itself shows the progress
        game, logs, _start, _flow = self.run_lobby(kit, request)
        self.assertEqual(len(game.launched), 1)                              # the lobby found the session: one X4
        self.assertEqual(self.restarts(self.helper_logs, logs), 1)

    def test_without_memory_for_a_second_x4_the_page_says_so_and_a_notice_stands_in(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        helper = self.helper(kit, fits=False)
        self.started_requests = []
        helper.flow.request('HOST|0|4|Friday')
        self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'restart'))
        _stamp, _phase, lines, fields = helper.flow.state.view()
        self.assertEqual(fields['quit_after'], steamplay.CLOSE_NOTICE_S)    # read before X4 closes
        self.assertIn('about 2 minutes, with X4 closed', lines[0])
        self.assertEqual(self.made, [])                                      # nothing while the Steam X4 runs
        request, before = helper.flow.take_handover_with()
        self.x4_open[0] = False
        helper.session(request, before)
        self.assertEqual([(entry['players'], entry['beside'], entry['steam_x4_open']) for entry in self.made],
                         [(4, False, False)])
        self.assertEqual([(notice.shown, notice.closed) for notice in self.notices], [(True, True)])
        self.assertIn('preparing a new game for 4 players', self.notices[0].text)
        self.assertEqual(self.started_requests, ['HOST|0|4|Friday'])
        # the player's X4 closed once, and an X4 of the capture came and went meanwhile, announced
        self.assertEqual(self.restarts(self.helper_logs), 2)

    def test_a_capture_beside_that_fails_is_made_once_more_with_x4_closed(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))

        def fails(beside):
            if beside:
                raise netkit.KitError('The session could not be made: the capture of x4ep1_gamestart_discover ended failed')
        helper = self.helper(kit, make=fails)
        helper.flow.request('HOST|0|2|Friday')
        self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'restart'))
        self.assertIn('preparing it beside X4 did not work', helper.flow.state.lines[0])
        request, before = helper.flow.take_handover_with()
        self.assertIsNotNone(before)
        self.x4_open[0] = False
        self.started_requests = []
        helper.session(request, before)
        self.assertEqual([entry['beside'] for entry in self.made], [True, False])
        self.assertEqual(self.started_requests, ['HOST|0|2|Friday'])

    def test_cancel_while_the_session_is_made_beside(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        release = threading.Event()

        def slow(beside):
            release.wait(10)
            raise netkit.KitError('cancelled')
        helper = self.helper(kit, make=slow)
        helper.flow.request('HOST|0|2|Friday')
        self.assertTrue(fakes.wait_for(lambda: self.made))
        helper.flow.request('CANCEL')
        release.set()
        self.assertTrue(fakes.wait_for(lambda: helper.flow.job is None or not helper.flow.job.alive()))
        self.assertEqual(helper.flow.state.phase, 'idle')
        self.assertEqual(helper.flow.take_handover_with(), (None, None))   # X4 does not close
        self.assertEqual(self.restarts(self.helper_logs), 0)

    def test_drop_in_slots_without_a_master_are_refused_on_this_page(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        helper = self.helper(kit)
        helper.flow.request('HOST|0|1+1|Friday')
        self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'error'))
        self.assertIn('Drop-in slots need the master server', helper.flow.state.lines[0])
        self.assertEqual((self.made, helper.flow.take_handover()), ([], None))


class SteamJoin(Case):
    def hosted(self, players=2):
        master = fakes.FakeMaster()
        files = fakes.make_session(self.root / 'host-session', players)
        kit = netkit.Kit(fakes.release_kit(self.root / 'host' / 'kit'))
        backend = fakes.FakeBackend(self.root / 'host' / 'work', host_files=files)
        config = netplay.FlowConfig(kit=kit, release=kit.release, master_url=MASTER_URL, port=fakes.free_port_pair(),
                                    load_wait=20)
        host = netplay.Flow(config, backend, net=fakes.network(master, fakes.FakeNat(reachable=False)), log=lambda text: None)
        self.flows.append(host)
        host.request(f'HOST|1|{players}|Friday')
        self.assertTrue(fakes.wait_for(lambda: host.state.phase == 'hosting'), host.state.lines)
        return master, host

    def test_the_session_is_ready_before_x4_closes_one_restart(self):
        master, _host = self.hosted()
        kit = netkit.Kit(fakes.release_kit(self.root / 'joiner' / 'kit'))
        helper = self.helper(kit, net=fakes.network(master), master_url=MASTER_URL)
        request = self.hand_over(helper, 'JOINCODE|k7qx3fna')
        self.assertEqual(request, 'JOINCODE|K7QX-3FNA')
        downloads = len(master.called('download'))
        self.assertEqual((downloads, len(self.builds)), (3, 1))             # fetched and built while the Steam X4 ran
        self.assertEqual(len(master.called('leave')), 1)                   # its slot given back before X4 closed
        game, logs, start, _flow = self.run_lobby(kit, request, net=fakes.network(master), master_url=MASTER_URL,
                                                  timeout=20)
        self.assertIsNotNone(start, logs[-6:])
        self.assertEqual(start.slot, 2)
        self.assertEqual((len(master.called('download')), len(self.builds)), (downloads, 1))   # nothing again
        self.assertEqual(len(game.launched), 1)                            # the session's X4: no page first
        self.assertEqual(Path(game.launched[0].overlay).parent.name[:7], 'joined-')
        self.assertEqual(self.restarts(self.helper_logs, logs), 1)

    def test_a_join_that_cannot_work_is_said_on_this_page_and_x4_stays(self):
        master, _host = self.hosted()
        kit = netkit.Kit(fakes.release_kit(self.root / 'joiner' / 'kit', release='fedcba987654-000000000000'))
        helper = self.helper(kit, net=fakes.network(master), master_url=MASTER_URL)
        helper.flow.request('JOINCODE|k7qx3fna')
        self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'error'))
        self.assertIn('another release', helper.flow.state.lines[0])
        helper.flow.request('CANCEL')
        helper.flow.request('JOINCODE|K7QX-0000')                          # not a code at all
        self.assertTrue(fakes.wait_for(lambda: helper.flow.state.phase == 'error'))
        self.assertEqual(helper.flow.take_handover(), None)
        self.assertEqual(self.restarts(self.helper_logs), 0)


class MultiplayerProfile(Case):
    """X4 already runs in the multiplayer profile (x4ls lobby, the Start menu's "X4 Online (play)"): it waits with a kept
    session's start-time files, and takes the hosted or joined session in place."""

    def page(self, kit):
        logs = []
        o = lobby.LobbyOptions(lane=1)
        game = QuietLobbyGame(self.root / 'page', logs.append, o)
        backend = netlobby.LobbyBackend(o, kit, game, self.root / 'run', logs.append)
        backend.installed_dlcs = lambda: None
        game.start(backend.default_form())
        return game, backend, logs

    def test_hosting_another_player_count_takes_it_in_place(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        two = fakes.host_session(kit.root, 2)
        fakes.host_session(kit.root, 3)
        game, backend, logs = self.page(kit)
        self.assertEqual(Path(game.form.overlay), two / 'overlay')          # it waits with the 2-player session
        files = netkit.cached_session(kit, 3)
        backend.install_session(files, lambda done: None)
        self.assertEqual(len(game.launched), 1)                            # no second start
        self.assertEqual((game.form.overlay, game.staged), (files.overlay, [files.snapshot]))
        self.assertIn('"players": 3', (game.target / 'play-recipe.json').read_text())   # the new session's files
        self.assertIn("files = {'ui/native_gate_probe.lua'}", (game.target / 'ui/config.lua').read_text())
        self.assertTrue(any('in place' in line for line in logs))
        self.assertEqual(self.restarts(logs), 0)

    def test_other_scripts_and_another_seed_are_taken_in_place(self):
        """MD, AI scripts and libraries are read at every load, the seed is told: only patch paths X4 did not have at its
        start (and a story's load order) still start it again (reports/INPLACE-STUDY-20261126.txt 2.1, 3)."""
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        patch = b'<?xml version="1.0"?>\n<!-- a patch -->\n<diff><add sel="/x"/></diff>\n'
        fakes.host_session(kit.root, 2, extra={'aiscripts/order.fight.xml': patch, 'md/x4lan_old.xml': b'<mdscript/>\n',
                                                'libraries/factions.xml': patch, 'libraries/x4lan_extra.xml': patch})
        fakes.host_session(kit.root, 3, md=b'<mdscript name="Other"/>\n', extra={
            'aiscripts/order.fight.xml': patch, 'md/x4lan_new.xml': b'<mdscript name="New"/>\n',
            'md/notifications.xml': patch, 'aiscripts/x4lan_new.xml': b'<aiscript name="New"/>\n'})
        fakes.host_session(kit.root, 4, seed=12345, extra={'aiscripts/order.fight.xml': patch})
        game, backend, logs = self.page(kit)
        start_target = game.target
        backend.install_session(netkit.cached_session(kit, 3), lambda done: None)   # other MD, new MD and AI scripts
        self.assertEqual(len(game.launched), 1)
        self.assertEqual((start_target / 'md' / 'setup.xml').read_bytes(), b'<mdscript name="Other"/>\n')
        self.assertTrue((start_target / 'md' / 'notifications.xml').is_file())     # an MD patch path new to X4: fine
        self.assertTrue((start_target / 'aiscripts' / 'x4lan_new.xml').is_file())
        self.assertFalse((start_target / 'md' / 'x4lan_old.xml').exists())         # the previous form's script is out
        # a patch path X4 had at its start and the new form lacks stays as a placeholder (X4 knows it exists); a base
        # file the previous overlay had replaced is the base's again
        self.assertEqual((start_target / 'libraries' / 'x4lan_extra.xml').read_text(encoding='utf-8'), netlobby.PLACEHOLDER)
        self.assertEqual((start_target / 'libraries' / 'factions.xml').read_bytes(),
                         (netlobby.extension.BASE / 'libraries' / 'factions.xml').read_bytes())
        backend.install_session(netkit.cached_session(kit, 4), lambda done: None)   # another seed: told
        self.assertEqual((len(game.launched), game.seeds), (1, [12345]))
        self.assertEqual(self.restarts(logs), 0)

    def test_what_still_starts_x4_again(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        patch = b'<diff><add sel="/x"/></diff>\n'
        fakes.host_session(kit.root, 2)
        fakes.host_session(kit.root, 3, extra={'aiscripts/order.fight.xml': patch})       # an AI patch new to X4
        fakes.host_session(kit.root, 4, extra={'md/x4lan_story.xml': b'<mdscript/>\n'})    # a story's load order
        fakes.host_session(kit.root, 5, extra={'extensions/ego_dlc_split/md/gs_split.xml': patch})
        fakes.host_session(kit.root, 6, seed=12345)
        game, backend, logs = self.page(kit)
        game.seed_tellable = False
        for players in (3, 4, 5, 6):
            game.start(backend.default_form())   # back to the page with the 2-player session's files
            backend.install_session(netkit.cached_session(kit, players), lambda done: None)
        reasons = [line for line in logs if 'X4 starts again for the session' in line]
        self.assertEqual(len(reasons), 4)
        self.assertIn('aiscripts/order.fight.xml is a patch path this X4 did not have', reasons[0])
        self.assertIn('a story session needs X4 to load this extension after the DLCs', reasons[1])
        self.assertIn('extensions/ego_dlc_split/md/gs_split.xml is a patch path', reasons[2])
        self.assertIn('another seed, and this X4 cannot be told it', reasons[3])

    def test_the_page_alone_cannot_take_a_session_with_ai_patches(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        game, backend, logs = self.page(kit)                                 # no kept session: the page alone
        self.assertIsNone(game.form.overlay)
        fakes.host_session(kit.root, 2, extra={'aiscripts/order.fight.xml': b'<diff><add sel="/x"/></diff>\n'})
        backend.install_session(netkit.cached_session(kit, 2), lambda done: None)   # a session's AI patches: not there
        self.assertEqual(len(game.launched), 2)
        self.assertEqual(self.restarts(logs), 1)

    def test_x4ls_lobby_page_first_hosts_in_place(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        fakes.host_session(kit.root, 2)
        fakes.host_session(kit.root, 3)
        game, logs, _start, _flow = self.run_lobby(kit, 'HOST|0|3|Friday', timeout=3, page_first=True)
        self.assertEqual(len(game.launched), 1)                            # the page (with the 2-player session's files)
        self.assertEqual(Path(game.form.overlay).parent.name,
                         netkit.host_session_key(kit, 3, 'coop', netkit.HOST_SESSION_START))
        self.assertEqual(self.restarts(logs), 0)

    def test_a_first_host_or_join_starts_no_page(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        game, logs, _start, _flow = self.run_lobby(kit, 'JOINCODE|K7QX-3FNA', timeout=2)   # no master: it fails
        self.assertEqual([form.overlay for form in game.launched], [None])  # only to say why, after the failure
        game, logs, _start, _flow = self.run_lobby(kit, '', timeout=1)
        self.assertEqual(len(game.launched), 1)                            # no request: the page at once


class Counting(unittest.TestCase):
    def test_rows_that_count(self):
        rows = '\n'.join([
            '17:55:59 HOST|1|2|X4 game asked for: waiting for X4 to close',
            f'17:56:01 {netkit.RESTART_MARK}: the Steam-started X4 closed for HOST',
            f'17:56:03 {netkit.SESSION_ALONE_MARK}: 2 players (no X4 of the player is open meanwhile)',
            f'17:56:03 {netkit.SESSION_BESIDE_MARK}: 3 players (the X4 the player looks at stays open)',
            f'17:58:00 {netkit.RESTART_MARK}: lobby X4 pid 4 closes: it starts again with the session host-x'])
        self.assertEqual(netkit.count_restarts(rows), 3)
        self.assertEqual(netkit.count_restarts(''), 0)


class SteamPieces(unittest.TestCase):
    def test_only_the_steam_started_x4_has_its_mutex_freed(self):
        asked, logs = [], []

        def release(pid):
            asked.append(pid)
            return 1
        found, freed = steamplay.release_player_mutex(logs.append, clients=lambda: [(10, True), (11, False)],
                                                      release=release)
        self.assertEqual((found, freed, asked), ([11], 1, [11]))       # never a launcher client of another session
        self.assertIn('X4 pid 11 (started from Steam): released 1', logs[0])

    def test_play_opens_x4_in_the_multiplayer_profile_and_starts_a_missing_helper(self):
        folder = fakes.Folder()
        try:
            kit = fakes.release_kit(folder.path / 'kit')
            started, seconds = [], []
            code = steamplay.play(kit, None, owner=lambda: 'handle', popen=lambda command, **kw: started.append(command),
                                  start_second=lambda kit_dir, snapshot: seconds.append(kit_dir) or 0, clients=lambda: [])
            self.assertEqual(code, 0)
            self.assertEqual(started[0][1:], [str(kit / 'helper.pyw'), '--quiet'])
            self.assertEqual(seconds, [kit])
            steamplay.play(kit, None, owner=lambda: None, popen=lambda command, **kw: started.append(command),
                           start_second=lambda kit_dir, snapshot: 0, clients=lambda: [])
            self.assertEqual(len(started), 1)                           # a helper runs already: X4 only
        finally:
            folder.cleanup()

    def test_the_notice_closes_its_own_window(self):
        class User32:
            def __init__(self):
                self.closed = threading.Event()
                self.posted = []

            def MessageBoxW(self, hwnd, text, title, flags):
                self.title = title
                self.closed.wait(5)

            def FindWindowW(self, cls, title):
                return 77 if title == self.title else 0

            def PostMessageW(self, window, message, wparam, lparam):
                self.posted.append((window, message))
                self.closed.set()
        user32 = User32()
        notice = steamplay.Notice('X4 Online', 'Preparing', user32=user32).show()
        fakes.wait_for(lambda: hasattr(user32, 'title'))
        notice.close()
        self.assertEqual(user32.posted, [(77, steamplay.Notice.WM_CLOSE)])
        self.assertFalse(notice.thread.is_alive())


class DropInInPlace(Case):
    """A drop-in slot played by the X4 that waits on the Multiplayer page (netlobby.late_join, session.join_running):
    the early joiner's overlay has the page's start-time files and seed, so no X4 starts. The relay, the files and the
    monitor are fakes (hotjoin.EarlyJoin.__call__, session.monitor_play)."""

    def fresh(self, name):
        """A folder of its own for each use in one test."""
        self.uses = getattr(self, 'uses', 0) + 1
        return self.root / f'{name}{self.uses}'

    def session_overlay(self):
        kit = fakes.release_kit(self.fresh('src-kit'))
        fakes.host_session(kit, 2)
        return netkit.cached_session(netkit.Kit(kit), 2).overlay

    def joiner_overlay(self, source, md=None, extra=None):
        overlay = self.fresh('joiner') / 'overlay'
        shutil.copytree(source, overlay)
        for name, data in (extra or {}).items():
            (overlay / name).parent.mkdir(parents=True, exist_ok=True)
            (overlay / name).write_bytes(data)
        (overlay / 'ui' / 'config.lua.append').write_text('X4LAN_CONFIG.native_gate_join = "early"\n'
                                                          'X4LAN_CONFIG.port = @X4LANE_PORT@\n'
                                                          'X4LAN_CONFIG.token = "@X4LANE_TOKEN@"\n'
                                                          'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\n'
                                                          'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n')
        if md:
            (overlay / 'md' / 'setup.xml').write_bytes(md)
        fakes.overlay_manifest(overlay)
        return overlay

    def run_late(self, joiner):
        base = self.fresh('late')
        kit = netkit.Kit(fakes.release_kit(base / 'kit'))
        fakes.host_session(kit.root, 2)
        dropin = fakes.host_session(kit.root, 2, open_slots=(2,))   # the host's session as this PC built it
        logs = []
        o = lobby.LobbyOptions(lane=1)
        o.state = base / 'state'
        o.runs = base / 'runs'
        game = QuietLobbyGame(base / 'page', logs.append, o)
        backend = netlobby.LobbyBackend(o, kit, game, base / 'run', logs.append)
        game.start(backend.default_form())
        flow = netplay.Flow(netplay.FlowConfig(kit=kit, release=kit.release), fakes.FakeBackend(base / 'work'),
                            net=fakes.network(None), log=logs.append)
        self.flows.append(flow)
        start = netplay.SessionStart(slot=2, clients=2, relay_host='203.0.113.50', port=47205, token='ef' * 16,
                                     arm_at=1700000000, overlay=dropin / 'overlay', snapshot=dropin / 'session-save.xml.gz',
                                     mode='relay', late=True)
        recipe = {'players': 2, 'open': [2], 'join_mode': 'report', 'session': {'seed': '276944566'}}
        early, others = [], []
        real_join = netlobby.session.join

        def join(options, log=print):
            if options.running is None:     # an X4 of the hot join's own: not started here
                others.append(options)
                return {'outcome': 'played', 'own_x4': True}
            return real_join(options, log)
        with mock.patch('lockstep.launcher.hotjoin.build_joiner_overlay', lambda *args: (joiner, recipe)), \
                mock.patch('lockstep.launcher.hotjoin.EarlyJoin.__call__',
                           lambda job, view, profile_id, proc, log: early.append((Path(view), profile_id, job.log_name))), \
                mock.patch.object(netlobby.session, 'join', join), \
                mock.patch.object(netlobby.session, 'monitor_play', lambda *args: {'outcome': 'played'}), \
                mock.patch.object(netlobby.session, 'other_clients', lambda: []), \
                mock.patch.object(netlobby.session, 'x4_running', lambda: False):
            result = netlobby.late_join(o, flow, game, start, logs.append)
        return game, logs, result, early, others, flow

    def test_the_page_x4_plays_the_drop_in_slot(self):
        joiner = self.joiner_overlay(self.session_overlay())   # the same release: the same start-time files
        game, logs, result, early, others, flow = self.run_late(joiner)
        self.assertEqual((result['outcome'], result.get('in_place'), result['late']), ('played', True, True))
        self.assertEqual(others, [])                                        # no X4 of the hot join's own
        self.assertEqual(len(game.launched), 1)                             # only the page's first start
        self.assertEqual(early, [(Path(game.view), game.profile_id, 'x4ls-c1.log')])   # the page X4's log
        config = (game.target / 'ui' / 'config.lua').read_text()
        for part in ('X4LAN_CONFIG.slot = "p2"', 'X4LAN_CONFIG.token = "' + 'ef' * 16 + '"', 'session = true',
                     'native_gate_join = "early"'):
            self.assertIn(part, config)
        self.assertTrue(flow.session_started.is_set())
        self.assertTrue(any('plays the drop-in slot 2 in place' in line for line in logs))
        self.assertEqual(self.restarts(logs), 0)

    def test_other_scripts_are_taken_and_a_new_patch_path_starts_an_x4_of_its_own(self):
        joiner = self.joiner_overlay(self.session_overlay(), md=b'<mdscript name="Other"/>\n')
        game, logs, result, early, others, _flow = self.run_late(joiner)
        self.assertEqual((result.get('in_place'), others), (True, []))   # other MD content: read at the join's load
        joiner = self.joiner_overlay(self.session_overlay(), extra={'aiscripts/order.fight.xml': b'<diff/>\n'})
        game, logs, result, early, others, _flow = self.run_late(joiner)
        self.assertEqual(result.get('own_x4'), True)
        self.assertEqual(len(others), 1)
        self.assertEqual(early, [])
        self.assertTrue(any('aiscripts/order.fight.xml is a patch path this X4 did not have' in line for line in logs))
        self.assertEqual(self.restarts(logs), 1)                            # the page X4 closed for the hot join's own


class Play(unittest.TestCase):
    """helper.pyw --play (desktop, Start menu, Steam library) and --steam-play (X4's launch options)."""

    def setUp(self):
        self.folder = fakes.Folder()
        self.kit = fakes.release_kit(self.folder.path / 'kit')
        self.started, self.seconds, self.told, self.runs = [], [], [], []

    def tearDown(self):
        self.folder.cleanup()

    def play(self, clients, first=False):
        return steamplay.play(self.kit, None, owner=lambda: None, popen=lambda command, **kw: self.started.append(command),
                              start_second=lambda kit, snapshot: self.seconds.append(kit) or 0, first=first,
                              clients=lambda: clients, tell=lambda text, **kw: self.told.append(text))

    def test_what_runs_already(self):
        self.assertEqual(self.play([]), 0)
        self.assertEqual(len(self.seconds), 1)                              # X4 opens in the multiplayer profile
        self.play([(10, True)])                                             # X4 Online's X4 is open: nothing more
        self.assertEqual((len(self.seconds), self.told), (1, []))
        self.play([(11, False)])                                            # the player's own X4 holds the mutex
        self.assertEqual(len(self.seconds), 1)
        self.assertIn('Close it, then start X4 Online again', self.told[0])
        self.play([(11, False)], first=True)                                # the installer's last step: silent
        self.play([(10, True)], first=True)
        self.assertEqual((len(self.seconds), len(self.told)), (1, 1))
        self.play([], first=True)
        self.assertEqual(len(self.seconds), 2)

    def test_steams_play_through_x4_online_and_single_player(self):
        command = ['D:\\Steam\\steamapps\\common\\X4 Foundations\\X4.exe', '-skipintro']

        def second(kit, snapshot, extra=()):
            mark = Path(extra[extra.index('--single-player-mark') + 1])
            self.seconds.append(extra)
            if self.single:
                mark.write_text('single-player\n')
            return 0
        run = lambda cmd, cwd=None: self.runs.append((cmd, cwd)) or mock.Mock(returncode=0)   # noqa: E731
        self.single = False
        self.assertEqual(steamplay.steam_play(self.kit, command, owner=lambda: None, start_second=second, run=run), 0)
        self.assertEqual(self.runs, [])                                     # the multiplayer page only
        self.single = True
        steamplay.steam_play(self.kit, command, owner=lambda: None, start_second=second, run=run)
        self.assertEqual(self.runs, [(command, 'D:\\Steam\\steamapps\\common\\X4 Foundations')])   # Steam's own command
        self.assertFalse((self.kit / 'repo' / '.local' / 'single-player.mark').exists())

    def test_uninstall_takes_out_steam_and_the_views(self):
        from lockstep.launcher import game
        lanes = self.folder.path / 'x4ls-lanes'
        install = self.folder.path / 'X4'
        (install / 'extensions').mkdir(parents=True)
        (install / 'X4.exe').write_bytes(b'MZ')
        (install / 'data').mkdir()
        (install / 'data' / 'keep.txt').write_text('a game file')
        game.create_view(install, lanes / 'c1')
        game.create_view(install, lanes / 'c2')
        (lanes / 'notes').mkdir()                                           # not a view: stays
        removed = steamplay.remove_lanes(lanes, clients=lambda: [str(lanes / 'c2' / 'X4.exe') + ' -personalfolderid 45851'])
        self.assertEqual(removed, [lanes / 'c1'])                           # c2's X4 runs: it stays
        self.assertEqual((install / 'data' / 'keep.txt').read_text(), 'a game file')   # links unlinked, never entered
        self.assertTrue((install / 'X4.exe').is_file())
        self.assertTrue((lanes / 'c2').is_dir() and (lanes / 'notes').is_dir())
        calls = []

        class Setup:
            def __init__(self):
                self.tries = 0

            def undo(self):
                self.tries += 1
                calls.append('undo')
                return self.tries > 1   # Steam ran at first

            def read(self):
                return {'play': {'x': {}}}
        steamplay.uninstall(self.kit, setup=Setup(), lanes=lanes, shutdown=lambda log: calls.append('shutdown'))
        self.assertEqual(calls, ['undo', 'shutdown', 'undo'])               # Steam closed so its old options stay out


class SteamSwitch(Case):
    """The page's "Steam's Play starts X4 Online" and Single-player through the flow."""

    def test_the_switch_and_single_player_in_the_lobby(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit'))
        (kit.root / 'kit-manifest.json').write_text('{}')
        (kit.root / 'helper.pyw').write_text('')
        logs = []
        o = lobby.LobbyOptions(lane=1)
        game = QuietLobbyGame(self.root / 'page', logs.append, o)
        backend = netlobby.LobbyBackend(o, kit, game, self.root / 'run', logs.append)
        with mock.patch.object(netlobby.netkit, 'beside_fits', lambda: True), \
                mock.patch('lockstep.launcher.steamlib.steam_running', lambda tasklist=None: True):
            flow = netplay.Flow(netplay.FlowConfig(kit=kit, release=kit.release), backend, net=fakes.network(None),
                                log=logs.append)
            self.flows.append(flow)
            self.assertEqual(flow.state.view()[3]['steam_play'], {'on': False, 'pending': False, 'steam_running': True})
            flow.request('STEAMPLAY|1')
            fields = flow.state.view()[3]
            self.assertEqual(fields['steam_play'], {'on': True, 'pending': True, 'steam_running': True})
            self.assertIn('once Steam is closed', ' '.join(flow.state.lines))
            flow.request('BROWSE')
            self.assertEqual(flow.state.view()[3]['steam_play']['on'], True)   # every state carries it
            flow.request('CANCEL')
            flow.request('SINGLEPLAYER')                                  # not started by Steam's Play: said so
            self.assertIn('Single-player is for an X4', ' '.join(flow.state.lines))
            mark = self.root / 'single.mark'
            o.single_player_mark = mark
            flow.request('SINGLEPLAYER')
            self.assertTrue(mark.is_file())
            self.assertTrue(backend.single)
        self.assertEqual(netplay.parse_request('STEAMPLAY|2'), None)
        self.assertEqual(netplay.request_text(netplay.parse_request('STEAMPLAY|0')), 'STEAMPLAY|0')


class HelperStamps(unittest.TestCase):
    def test_every_new_state_has_a_new_stamp_after_a_game(self):
        folder = fakes.Folder()
        try:
            kit = fakes.release_kit(folder.path / 'kit')
            helper = steamplay.NetHelper(kit, prefix='x4mp-stamps-test', log=lambda text: None, running=lambda: False,
                                         net=fakes.network(None), master_url='', make=lambda *a, **k: None)
            first = helper.state_text()
            helper.mirror.parent.mkdir(parents=True, exist_ok=True)
            with helper.lock:   # a game ran: its mirrored states counted on
                helper.stamp += 40
                helper.mirrored = ("return {stamp = 7, phase = 'session'}\n", helper.stamp)
            mirrored = helper.state_text()
            with helper.lock:
                helper.mirrored = None
                helper.flow_stamp = None
            stamps = [int(text.split('stamp = ')[1].split(',')[0]) for text in (first, mirrored, helper.state_text())]
            helper.flow.request('BROWSE')
            fakes.wait_for(lambda: helper.flow.state.phase == 'browsing')
            stamps.append(int(helper.state_text().split('stamp = ')[1].split(',')[0]))
            self.assertEqual(stamps, sorted(set(stamps)), stamps)           # strictly rising: the page takes each one
            helper.flow.cancel()
        finally:
            folder.cleanup()


if __name__ == '__main__':
    unittest.main()
