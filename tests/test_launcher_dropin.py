"""Drop-in games on a server (x4ls serve with an overlay that has open slots): the server's answers to a launcher, the
join files' way from a saver's launcher through the server to a joiner, and the page's start picker with the helper.
Real sockets, pipes and joinfiles.FileService; the relay's log and the join package reader are stand-ins. No X4 runs."""
import hashlib
import json
import os
import socket
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import hotjoin, joinfiles, lobby, serve, steamplay  # noqa: E402

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:   # pragma: no cover
    LuaRuntime = None

KEY = 'a1' * 32
SALT = 'ab12' * 8   # a game's id: the salt of its token (hex, as the server makes it)
STARTS = [{'slot': 2, 'start': 'x4ep1_gamestart_fight', 'sector': 'Argon Prime', 'taken': False},
          {'slot': 3, 'start': 'x4ep1_gamestart_terran1', 'sector': 'Mars', 'taken': True}]


class FakeJoin:
    """Stands in for tools/dev/native_gate_join.py: a package is its own text, 'save=<sha of the save>=<tick>'."""
    class JoinError(Exception):
        pass

    @staticmethod
    def read_package(data):
        _, save_sha, tick = data.decode().split('=')
        return {'sha': hashlib.sha256(data).hexdigest(), 'header': {'save_sha': save_sha}, 'sections': {'gate': {'tick': int(tick)}}}


def free_port():
    for _ in range(50):
        with socket.socket() as a, socket.socket() as b:
            a.bind(('127.0.0.1', 0))
            port = a.getsockname()[1]
            try:
                b.bind(('127.0.0.1', port + 1))
                return port
            except OSError:
                continue
    raise RuntimeError('no free port pair')


class Server(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.join_tools, hotjoin.join_tools = hotjoin.join_tools, lambda: FakeJoin
        self.port, self.logs = free_port(), []
        self.token = lobby.session_token(KEY, SALT)
        self.host = lobby.Host('127.0.0.1', self.port, None, 3, 'kit-1', self.logs.append, key=KEY)
        self.host.slots = [1]                      # slots 2 and 3 are open
        self.server = ('127.0.0.1', self.port, KEY)
        self.stop = False
        self.joins = None
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def loop(self):
        while not self.stop:
            self.host.poll()
            if self.joins:
                self.joins.poll()
            time.sleep(0.01)

    def tearDown(self):
        self.stop = True
        self.thread.join(5)
        if self.joins:
            self.joins.close()
        self.host.close()
        hotjoin.join_tools = self.join_tools
        self.tmp.cleanup()

    def wait(self, what, seconds=10):
        end = time.time() + seconds
        while time.time() < end:
            if what():
                return
            time.sleep(0.02)
        self.fail(f'waited in vain; {self.logs}')

    def test_status_late_joiners_and_the_join_files(self):
        self.host.status = lambda: {'running': self.joins is not None, 'drop_in': True, 'starts': STARTS}
        self.assertEqual(lobby.ask_server(self.server, 'kit-1', 'status'),
                         {'ok': True, 'status': {'running': False, 'drop_in': True, 'starts': STARTS}})
        self.assertIn('another key', lobby.ask_server(('127.0.0.1', self.port, 'b2' * 32), 'kit-1', 'status')['why'])
        self.assertIn('differs', lobby.ask_server(self.server, 'kit-2', 'status')['why'])
        # nobody can join a game that does not run
        self.assertEqual(lobby.ask_server(self.server, 'kit-1', 'late'),
                         {'ok': False, 'why': 'no game is running on the server that can be joined'})
        # player 1: the common load; its launcher stays connected as the game's saver
        sock = socket.create_connection(('127.0.0.1', self.port + 1), timeout=5)
        sock.settimeout(5)
        saver = lobby.Lines(sock)
        lobby.send(sock, {'kit': 'kit-1', 'proof': lobby.key_proof(KEY, saver.wait(5)['nonce'], 'kit-1')})
        self.assertEqual(saver.wait(5), {'ok': True, 'slot': 1, 'clients': 3})
        self.wait(lambda: 1 in self.host.players)
        self.host.start(1800000000, SALT, drop_in=True)
        for _ in range(5):
            message = saver.wait(5)
            if 'start' in message:
                break
        self.assertEqual(message['start']['drop_in'], True)
        run_dir = self.base / 'run'
        run_dir.mkdir()
        self.joins = serve.DropIn(self.host, run_dir, self.token, '127.0.0.1', self.port, [2, 3], ('127.0.0.1',), self.logs.append)
        self.host.join_dir = lambda game: (run_dir / 'join') if game == SALT else None
        self.host.join_done = self.joins.stored
        self.host.running = lambda address: (self.joins.admit(address), {'salt': SALT, 'port': self.port, 'starts': STARTS})[1]
        # a late joiner is admitted: it learns the salt, and its address may send to the relay
        answer = lobby.ask_server(self.server, 'kit-1', 'late')
        self.assertEqual(answer['running']['salt'], SALT)
        # a second "Start the game" while the game runs is told that it runs (the launcher's page then says so)
        again = socket.create_connection(('127.0.0.1', self.port + 1), timeout=5)
        again.settimeout(5)
        lines = lobby.Lines(again)
        lobby.send(again, {'kit': 'kit-1', 'proof': lobby.key_proof(KEY, lines.wait(5)['nonce'], 'kit-1')})
        self.assertEqual(lines.wait(5)['running']['salt'], SALT)
        again.close()
        self.joins.admit('203.0.113.9')
        self.assertEqual((run_dir / 'allow.txt').read_text().split(), ['203.0.113.9'])   # 127.0.0.1 was allowed from the start
        self.assertIn('203.0.113.9', self.joins.service.allow)
        # the saver's X4 wrote the join files; the relay announces the save; the launcher hands them to the server
        saves = self.base / 'profile' / 'save'
        (saves / 'mpjoin').mkdir(parents=True)
        save = os.urandom(300000)
        package = ('save=' + hashlib.sha256(save).hexdigest() + '=100').encode()
        (saves / 'mpjoin' / 'j100.xml.gz').write_bytes(save)
        (saves / 'mpjoin' / 'j100.x4join').write_bytes(package)
        threading.Thread(target=lobby.hand_over_joins, daemon=True,
                         args=(saver, self.server, 'kit-1', saves, SALT, 1, self.logs.append)).start()
        sha = hashlib.sha256(package).hexdigest()
        with (run_dir / 'relay.log').open('a') as f:
            f.write('NATIVE_GATE tick=99 failure=none done=False\n'
                    'NATIVE_GATE JOIN slot=2 queued tick=100\n'
                    f'NATIVE_GATE JOIN slot=2 saved slot=1 tick=100 bytes=300000 package={sha} portable={"0" * 32}\n')
        self.wait(lambda: any('serving j100 from saver 1' in t for t in self.logs))
        self.assertEqual(self.joins.taken, {2: 'saved'})
        # the joiner fetches them from the server as it would from a host's PC
        state = {'tick': 100, 'package_sha': sha}
        got_save, got_package, loaded = hotjoin.fetch_files('127.0.0.1', self.port, self.token, 2, state, self.base / 'joiner')
        self.assertEqual(got_save.read_bytes(), save)
        self.assertEqual(got_package.read_bytes(), package)
        # a join that failed frees its start again
        with (run_dir / 'relay.log').open('a') as f:
            f.write('NATIVE_GATE JOIN slot=2 failed reason=joiner_timeout state=saved\n')
        self.wait(lambda: self.joins.taken == {})
        # a file nobody asked for, or outside a drop-in game, is refused
        stray = self.base / 'j7.xml.gz'
        stray.write_bytes(b'x' * 10)
        with self.assertRaises(OSError):
            lobby.send_file(self.server, 'kit-1', stray, 'cd34', 1, name='j7.xml.gz')
        with self.assertRaises(OSError):
            lobby.send_file(self.server, 'kit-1', stray, SALT, 1, name='../evil.xml.gz')
        sock.close()

    def test_the_relay_takes_addresses_added_while_it_runs(self):
        relay = (ROOT / 'tools/dev/native_gate_relay.py').read_text(encoding='utf-8')
        self.assertIn("p.add_argument('--allow-file'", relay)
        self.assertIn('allow_more(time.perf_counter())', relay)


@unittest.skipUnless(os.name == 'nt', 'named pipes: Windows')
class Picker(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        kit = Path(self.tmp.name) / 'kit'
        (kit / 'overlay').mkdir(parents=True)
        (kit / 'overlay' / 'play-recipe.json').write_text(json.dumps({'players': 3, 'mode': 'coop', 'minutes': 40.0, 'open': [2, 3]}))
        self.prefix = f'x4mp-test-pick-{os.getpid()}-{time.time_ns() % 100000}'
        self.server_status = {'running': False, 'drop_in': True, 'clients': 3, 'here': 0, 'joinable': False,
                              'base': 'x4ep1_gamestart_discover', 'starts': STARTS}
        self.started, self.x4_open = [], [False]
        self.helper = steamplay.Helper(kit, 'snapshot/save_009.xml.gz', prefix=self.prefix, log=lambda text: None,
                                       start=lambda slot=None: (self.started.append(slot), (0, ''))[1],
                                       running=lambda: self.x4_open[0], status=lambda: self.server_status)
        self.thread = threading.Thread(target=self.helper.run, daemon=True)
        self.thread.start()

    def tearDown(self):
        steamplay.ask(self.prefix, 'stop', timeout=1)
        self.thread.join(5)
        self.tmp.cleanup()

    def state(self, name='state'):
        return steamplay.ask(self.prefix, name)

    def wait(self, what, seconds=8):
        end = time.time() + seconds
        while time.time() < end:
            if what():
                return
            time.sleep(0.05)
        self.fail('waited in vain: ' + str(self.state()))

    def test_no_game_then_a_running_game_with_starts(self):
        self.wait(lambda: "game = 'none'" in self.state())
        self.assertIn("join_label = 'Start the game'", self.state())
        self.assertIn('you play discover', self.state())
        self.server_status = dict(self.server_status, running=True, here=1, joinable=True)
        self.wait(lambda: "game = 'running'" in self.state())
        text = self.state()
        self.assertIn("{slot = 2, taken = false, label = 'fight (Argon Prime)'}", text)
        self.assertIn("{slot = 3, taken = true, label = 'terran1 (Mars)'}", text)
        self.assertIn('A game is running with 1 of 3 players', text)
        self.assertIn("phase = 'restart'", self.state('join2'))     # the start of slot 2
        self.wait(lambda: self.started == [2])                      # x4ls join --server-file --slot 2
        self.wait(lambda: "phase = 'idle'" in self.state())
        self.server_status = dict(self.server_status, joinable=False)
        self.wait(lambda: 'nobody can join it any more' in self.state())

    @unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
    def test_the_page_lists_the_starts_and_a_click_joins(self):
        from test_launcher_lobby import LOBBY, SETUP
        self.server_status = dict(self.server_status, running=True, here=1, joinable=True)
        self.wait(lambda: "game = 'running'" in self.state())
        lua = LuaRuntime()
        lua.execute(SETUP + f"\npackage = nil\nfunction QuitGame() end\n"
                            f"X4LAN_CONFIG = {{enabled = false, lobby = {{pipe = '{self.prefix}', server = true}}}}\n")
        lua.execute(LOBBY)
        g = lua.globals()
        for _ in range(41):
            g.scripts.onUpdate()
        g.menu.submenuHandler('x4lan_lobby')
        rows = [g.drawn[i] for i in range(1, len(g.drawn) + 1)]
        self.assertEqual([r.id for r in rows][6:], ['line', 'x4lan_lobby_start2', 'x4lan_lobby_start3', 'x4lan_lobby_cancel'])
        self.assertEqual((rows[7].name, rows[7].value, rows[8].name, rows[8].value),
                         ('Join as fight (Argon Prime)', 'Join', 'Join as terran1 (Mars)', 'Taken'))
        self.assertTrue(rows[7].selectable() and not rows[8].selectable())
        rows[7].callback()
        self.wait(lambda: self.started == [2])


if __name__ == '__main__':
    unittest.main()
