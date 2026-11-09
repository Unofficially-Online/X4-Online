"""x4ls lobby: the launcher's side (invite codes, the host's admissions over real local sockets, the installed lobby form
and the session config) and the game's side (tools/dev/native_gate_lobby.lua under LuaJIT with a fake options menu, its
loadfile calls in a temporary folder, no io library and no ffi, as in X4). No X4 runs here."""
import hashlib
import json
import os
import socket
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import extension, lobby  # noqa: E402

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:   # pragma: no cover
    LuaRuntime = None


NL = chr(13) + chr(10)   # X4's log is written with CR LF


def fake_overlay(folder, players=2, config=''):
    (folder / 'ui').mkdir(parents=True)
    (folder / 'native').mkdir()
    (folder / 'native' / 'x4lan_gate_64.dll').write_bytes(b'MZ')
    (folder / 'ui' / 'native_gate_defaults.lua').write_text('DEFAULTS_RAN = true\n')
    (folder / 'ui' / 'native_gate_probe.lua').write_text('PROBE_RAN = X4LAN_CONFIG.slot\n')
    (folder / 'ui.xml.files').write_text('ui/native_gate_defaults.lua\nui/native_gate_probe.lua\n')
    (folder / 'ui' / 'config.lua.append').write_text(
        'X4LAN_CONFIG.test_profile = "45841"\nX4LAN_CONFIG.port = @X4LANE_PORT@\nX4LAN_CONFIG.token = "@X4LANE_TOKEN@"\n'
        'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\nX4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n' + config)
    (folder / 'play-recipe.json').write_text(json.dumps({'players': players, 'mode': 'coop', 'minutes': 40.0, 'open': [],
                                                         'snapshot_sha': 'ab' * 32}))
    manifest = {str(p.relative_to(folder)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in folder.rglob('*') if p.is_file()}
    (folder / 'x4lane-manifest.json').write_text(json.dumps(manifest))


class Launcher(unittest.TestCase):
    def test_invite_codes(self):
        token = 'ab' * 16
        code = lobby.invite('192.168.1.20', 47000, token)
        self.assertEqual(code, 'X4MP-192.168.1.20-47000-' + token)
        self.assertEqual(lobby.parse_invite('paste: ' + code + ' !'), ('192.168.1.20', 47000, token))
        for bad in ('', 'X4MP-999.1.1.1-47000-' + token, 'X4MP-1.2.3.4-0-' + token, 'X4MP-1.2.3.4-47000-xyz', None):
            self.assertIsNone(lobby.parse_invite(bad), bad)

    def test_state_text(self):
        text = lobby.state_text(3, 'hosting', ['Hosting', "the host's code"], invite='X4MP-1', slot=None, arm_at=17)
        self.assertEqual(text, "return {stamp = 3, phase = 'hosting', invite = 'X4MP-1', arm_at = 17, "
                               "lines = {'Hosting', 'the host" + chr(92) + "'s code'}}" + chr(10))

    def test_installed_lobby_form_and_session_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fake_overlay(tmp / 'ov')
            (tmp / 'ext').mkdir()
            target, files = lobby.install_lobby(tmp / 'ov', tmp / 'ext', 2, 45850)
            self.assertEqual(files, ['ui/native_gate_defaults.lua', 'ui/native_gate_probe.lua'])
            ui = (target / 'ui.xml').read_text()
            self.assertIn('ui/native_gate_lobby.lua', ui)
            self.assertNotIn('native_gate_probe.lua', ui)          # the overlay's files wait for the session
            self.assertTrue((target / 'ui/native_gate_probe.lua').is_file() and (target / 'lobby').is_dir())
            config = (target / 'ui/config.lua').read_text()
            self.assertIn("X4LAN_CONFIG.lobby = {files = {'ui/native_gate_defaults.lua','ui/native_gate_probe.lua'}}", config)
            self.assertNotIn('token', config)
            text = lobby.session_config(tmp / 'ov', files, 2, 47000, 'cd' * 16, 2, 45850, 1800000000, '192.168.1.20')
            for want in ('X4LAN_CONFIG.port = 47000', 'X4LAN_CONFIG.token = "' + 'cd' * 16 + '"', 'X4LAN_CONFIG.slot = "p2"',
                         'X4LAN_CONFIG.native_gate_clients = 2', 'X4LAN_CONFIG.test_profile = "45850"',
                         'X4LAN_CONFIG.native_gate_arm_at = 1800000000', "X4LAN_CONFIG.native_gate_relay_host = '192.168.1.20'",
                         'session = true}'):
                self.assertIn(want, text)
            # the same text x4ls join installs, plus the lobby's line
            self.assertTrue(text.startswith(extension.config_text(tmp / 'ov', 2, 47000, 'cd' * 16, 2, profile_id=45850)))
            self.assertNotEqual(lobby.kit_id(tmp / 'ov', 'ab' * 32), lobby.kit_id(tmp / 'ov', 'ac' * 32))

    def test_requests_in_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'x4ls-c1.log'
            r = lobby.Request(path)
            self.assertIsNone(r.poll())            # no log yet
            row = '[=ERROR=] 4.21 [X4LAN] LOBBY '
            with path.open('ab') as f:
                f.write((row + 'READY files=10' + NL + row + 'REQUEST HOST' + NL + row + 'REQUEST CAN').encode())
            self.assertEqual(r.poll(), 'HOST')
            self.assertIsNone(r.poll())            # the same request once; the half-written row waits
            with path.open('ab') as f:
                f.write(('CEL' + NL + row + 'REQUEST JOIN' + NL + row + 'REQUEST COPY' + NL).encode())
            self.assertEqual([r.poll(), r.poll(), r.poll(), r.poll()], ['CANCEL', 'JOIN', 'COPY', None])

    def test_host_admits_refuses_and_starts(self):
        logs = []
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1] - 1
        host = lobby.Host('127.0.0.1', port, 'ab' * 16, 3, 'kit-1', logs.append)

        def hello(token='ab' * 16, kit='kit-1'):
            s = socket.create_connection(('127.0.0.1', port + 1), timeout=5)
            lobby.send(s, {'token': token, 'kit': kit})
            return s

        def answer(s):
            for _ in range(100):
                host.poll()
                s.settimeout(0.05)
                try:
                    return json.loads(s.makefile('rb').readline())
                except (TimeoutError, socket.timeout):
                    continue
            self.fail('no answer')

        try:
            wrong = hello(token='00' * 16)
            self.assertEqual(answer(wrong)['ok'], False)
            other = hello(kit='kit-2')
            self.assertIn('differs', answer(other)['why'])
            a = hello()
            self.assertEqual(answer(a), {'ok': True, 'slot': 2, 'clients': 3})
            self.assertFalse(host.poll())                       # one of two joiners
            b = hello()
            self.assertEqual(answer(b), {'ok': True, 'slot': 3, 'clients': 3})
            self.assertTrue(host.poll())
            c = hello()
            self.assertEqual(answer(c), {'ok': False, 'why': 'the game is full'})
            b.close()                                           # a player leaves before the start: the slot is free again
            for _ in range(50):
                if not host.poll():
                    break
                time.sleep(0.02)
            self.assertEqual(sorted(host.players), [2])
            b = hello()
            self.assertEqual(answer(b)['slot'], 3)
            self.assertTrue(host.poll())
            self.assertEqual(host.start(1800000000), ('127.0.0.1',))
            a.settimeout(5)
            rows = a.makefile('rb')
            message = json.loads(rows.readline())
            while 'here' in message:                            # how many players are there, said on every change
                self.assertEqual(message['clients'], 3)
                message = json.loads(rows.readline())
            self.assertEqual(message, {'start': {'host': '127.0.0.1', 'port': port, 'arm_at': 1800000000, 'slot': 2}})
            for s in (wrong, other, a, b, c):
                s.close()
        finally:
            host.close()
        self.assertTrue(any('player 2 joined' in t for t in logs), logs)


class Server(unittest.TestCase):
    """x4ls serve: every slot is a joiner's, a joiner proves the kit's key, the relay is the server's."""
    KEY = 'a1' * 32

    def free_port(self):
        for _ in range(50):   # TCP port + 1 for the lobby and UDP port for the relay, both free
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1] - 1
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    udp.bind(('127.0.0.1', port))
                return port
            except OSError:
                continue
        self.fail('no free port pair')

    def join(self, port, kit='kit-1', key=None):
        s = socket.create_connection(('127.0.0.1', port + 1), timeout=5)
        s.settimeout(5)
        lines = lobby.Lines(s)
        return s, lines

    def answer(self, host, lines, kit='kit-1', key=None):
        def wait():
            for _ in range(1500):   # generous: the PC may be loading a game next to the tests
                host.poll()
                message = lines.wait(0.02)
                if message is not None:
                    return message
            self.fail('no message')
        nonce = wait()['nonce']
        lobby.send(lines.sock, {'kit': kit, 'proof': lobby.key_proof(key or self.KEY, nonce, kit)})
        return wait()

    def test_key_proof_slots_busy_and_token(self):
        logs, port = [], self.free_port()
        host = lobby.Host('127.0.0.1', port, None, 2, 'kit-1', logs.append, key=self.KEY)
        socks = []
        try:
            s, wrong = self.join(port)
            socks.append(s)
            self.assertIn('another key', self.answer(host, wrong, key='b2' * 32)['why'])
            s, other = self.join(port)
            socks.append(s)
            self.assertIn('differs', self.answer(host, other, kit='kit-2')['why'])
            s, a = self.join(port)
            socks.append(s)
            self.assertEqual(self.answer(host, a), {'ok': True, 'slot': 1, 'clients': 2})   # slot 1 is a joiner's too
            self.assertFalse(host.poll())
            self.assertEqual(a.wait(1), {'here': 1, 'clients': 2})
            s, b = self.join(port)
            socks.append(s)
            self.assertEqual(self.answer(host, b), {'ok': True, 'slot': 2, 'clients': 2})
            self.assertTrue(host.poll())
            self.assertEqual(a.wait(1), {'here': 2, 'clients': 2})
            self.assertEqual(host.start(1800000000, 'salt-1'), ('127.0.0.1',))
            start = a.wait(1)['start']
            self.assertEqual(start, {'host': '127.0.0.1', 'port': port, 'arm_at': 1800000000, 'slot': 1, 'salt': 'salt-1'})
            token = lobby.session_token(self.KEY, start['salt'])
            self.assertRegex(token, '^[0-9a-f]{32}$')
            self.assertNotEqual(token, lobby.session_token(self.KEY, 'salt-2'))
            self.assertNotEqual(token, lobby.session_token('b2' * 32, 'salt-1'))
            host.release()
            host.busy = 'a game is running'
            s, late = self.join(port)
            socks.append(s)
            self.assertEqual(self.answer(host, late), {'ok': False, 'why': 'a game is running'})
            host.busy = None
            s, again = self.join(port)
            socks.append(s)
            self.assertEqual(self.answer(host, again)['slot'], 1)     # the next game
        finally:
            for s in socks:
                s.close()
            host.close()

    def test_a_launchers_logs_go_to_the_server(self):
        import threading
        import zipfile
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            run = tmp / 'pc' / '20261109-170000-lobby1'
            (run / 'client1').mkdir(parents=True)
            (run / 'summary.json').write_text('{"outcome": "played"}')
            (run / 'client1' / 'engine-output.log').write_text('log row ' * 5000)
            (run / 'client1' / 'save_001.xml.gz').write_bytes(b'a save, not a log')
            logs, port = [], self.free_port()
            host = lobby.Host('127.0.0.1', port, None, 2, 'kit-1', logs.append, key=self.KEY)
            host.upload_dir = lambda game: tmp / 'server' / (game or 'uploads')
            done = []

            def launcher(key, game):
                try:
                    done.append(lobby.send_logs(('127.0.0.1', port, key), 'kit-9', run, game, 2, logs.append))
                except OSError as error:
                    done.append(error)

            try:
                for key, game in ((self.KEY, 'ab12'), ('b2' * 32, 'ab12'), (self.KEY, '../../x')):
                    thread = threading.Thread(target=launcher, args=(key, game), daemon=True)
                    thread.start()
                    for _ in range(400):
                        host.poll()
                        if not thread.is_alive():
                            break
                        time.sleep(0.01)
                    thread.join(5)
            finally:
                host.close()
            self.assertEqual(len(done), 3, logs)
            stored = tmp / 'server' / 'ab12' / done[0]            # another kit's logs are taken too: the key is what counts
            self.assertRegex(done[0], r'^[0-9]{8}-[0-9]{6}-player2-127_0_0_1-[0-9a-f]{4}[.]zip$')
            for _ in range(100):                                    # the server's thread closes the file
                if stored.is_file():
                    break
                time.sleep(0.02)
            with zipfile.ZipFile(stored) as z:
                self.assertEqual(sorted(z.namelist()), ['client1/engine-output.log', 'summary.json'])   # no save
                self.assertEqual(z.read('summary.json'), b'{"outcome": "played"}')
            self.assertIsInstance(done[1], OSError)                 # another key: refused
            self.assertIn('another key', str(done[1]))
            # a game id is hex only ('../../x' is no id): nothing leaves the server's folder
            self.assertEqual([p.parent.parent for p in (tmp / 'server').rglob('*.zip')], [tmp / 'server'] * 2)
            self.assertFalse((run.parent / (run.name + '.zip')).exists())   # the packed copy goes once it is sent

    def test_server_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'server.json'
            path.write_text(json.dumps({'host': '203.0.113.7', 'port': 47000, 'key': self.KEY}))
            self.assertEqual(lobby.read_server_file(path), ('203.0.113.7', 47000, self.KEY))
            for bad in ({'host': 'example.org', 'port': 47000, 'key': self.KEY}, {'host': '203.0.113.7', 'port': 47000, 'key': 'short'}):
                path.write_text(json.dumps(bad))
                with self.assertRaises(SystemExit):
                    lobby.read_server_file(path)

    def test_serve_runs_one_game_with_the_real_relay(self):
        """Two launchers' worth of sockets join; the server starts tools/dev/native_gate_relay.py for them. No game sends
        a step, so the game is over after --idle-end and the server would take the next players."""
        from lockstep.launcher import serve
        import threading
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fake_overlay(tmp / 'ov', config='X4LAN_CONFIG.native_gate_pilot_window = {40,640}\nX4LAN_CONFIG.native_gate_lookahead = 2\n')
            port = self.free_port()
            (tmp / 'server.json').write_text(json.dumps({'host': '127.0.0.1', 'port': port, 'key': self.KEY}))
            logs, kit = [], lobby.kit_id(tmp / 'ov', 'ab' * 32)
            options = serve.ServeOptions(overlay=tmp / 'ov', server_file=tmp / 'server.json', bind='127.0.0.1', load_wait=1,
                                         idle_end=3, runs=tmp / 'runs', games=1)
            thread = threading.Thread(target=serve.run, args=(options, logs.append), daemon=True)
            thread.start()
            starts, socks = [], []
            try:
                for _ in range(2):
                    for _ in range(100):
                        try:
                            s = socket.create_connection(('127.0.0.1', port + 1), timeout=5)
                            break
                        except OSError:
                            time.sleep(0.05)
                    socks.append(s)
                    lines = lobby.Lines(s)
                    lobby.send(s, {'kit': kit, 'proof': lobby.key_proof(self.KEY, lines.wait(5)['nonce'], kit)})
                    self.assertTrue(lines.wait(5)['ok'])
                    starts.append(lines)
                for slot, lines in enumerate(starts, 1):
                    for _ in range(10):
                        message = lines.wait(5)
                        if message and 'start' in message:
                            break
                    self.assertEqual((message['start']['slot'], message['start']['port']), (slot, port))
                thread.join(30)
                self.assertFalse(thread.is_alive(), logs)
            finally:
                for s in socks:
                    s.close()
            self.assertTrue(any('game 1 over (no step for 3 s)' in t for t in logs), logs)
            run = next((tmp / 'runs').iterdir())
            relay_log = (run / 'relay.log').read_text()
            self.assertNotIn('Traceback', relay_log)
            self.assertNotIn('error:', relay_log)


SETUP = r"""
logs, scripts = {}, {}
io, os = nil, nil   -- as in X4's UI Lua
do local real=require
 function require(name) if name=='ffi' then error('the lobby must not need ffi: X4 refuses its Win32 calls') end return real(name) end
end
function DebugError(text) logs[#logs+1]=text end
function SetScript(name, fn) scripts[name]=fn end
local config = {optionDefinitions={main={{id='continue'},{id='multiplayer'},{id='line'},{id='settings'},{id='quit'}},
  multiplayer={name='Multiplayer', {id='multiplayer_server'}, {id='lobby'}}}}
local options = {name='OptionsMenu', isStartmenu=true, refreshes=0}
drawn = {}
function options.displayOption(ftable, option) drawn[#drawn+1]=option end
function options.displayOptions(p)
  options.currentOption=p; drawn={}
  for _,o in ipairs(config.optionDefinitions[p]) do options.displayOption({}, o) end
end
function options.submenuHandler(p) if config.optionDefinitions[p] then options.displayOptions(p) end end
function options.refresh() options.refreshes=options.refreshes+1; options.displayOptions(options.currentOption) end
Menus = {{name='MapMenu'}, options}
menu = options
"""
LOBBY = (ROOT / 'tools/dev/native_gate_lobby.lua').read_text(encoding='utf-8')


@unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
class Game(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old = os.getcwd()
        os.chdir(self.tmp.name)
        self.ext = Path('extensions/x4_lan_lab')
        (self.ext / 'lobby').mkdir(parents=True)
        (self.ext / 'ui').mkdir()
        (self.ext / 'ui/native_gate_defaults.lua').write_text('DEFAULTS_RAN = X4LAN_CONFIG.token\n')
        (self.ext / 'ui/native_gate_probe.lua').write_text('PROBE_RAN = X4LAN_CONFIG.slot\n')

    def tearDown(self):
        os.chdir(self.old)
        self.tmp.cleanup()

    FILES = "{'ui/native_gate_defaults.lua','ui/native_gate_probe.lua'}"

    def start(self, config):
        lua = LuaRuntime()
        lua.execute(SETUP + config)
        lua.execute(LOBBY)
        return lua, lua.globals()

    def state(self, stamp, phase, *lines, **fields):
        (self.ext / 'lobby/state.lua').write_text(lobby.state_text(stamp, phase, lines, **fields))

    def frames(self, g, n=20):
        for _ in range(n):
            g.scripts.onUpdate()

    def test_menu_request_state_and_session(self):
        lua, g = self.start('X4LAN_CONFIG = {lobby = {files = ' + self.FILES + '}}')
        self.frames(g, 1)
        g.menu.displayOptions('main')
        ids = [g.drawn[i].id for i in range(1, len(g.drawn) + 1)]
        self.assertEqual(ids, ['continue', 'multiplayer', 'line', 'x4lan_lobby', 'settings', 'quit'])
        self.assertEqual(g.drawn[4].name, 'Multiplayer')
        g.menu.submenuHandler('x4lan_lobby')
        rows = [g.drawn[i] for i in range(1, len(g.drawn) + 1)]
        self.assertEqual([r.id for r in rows], ['header'] * 6 + ['line', 'x4lan_lobby_host', 'x4lan_lobby_join',
                                                                 'x4lan_lobby_copy', 'x4lan_lobby_cancel'])
        host, join, copy, cancel = rows[7:]
        self.assertFalse(host.selectable())                    # the launcher has not said idle yet
        self.state(1, 'idle', '2 players, coop', 'Host a game, or join.')
        self.frames(g)
        self.assertEqual(rows[0].name(), '2 players, coop')
        self.assertTrue(host.selectable() and join.selectable() and not cancel.selectable() and not copy.selectable())
        self.assertEqual(g.menu.refreshes, 1)                  # the open page is drawn again
        host.callback()
        self.assertEqual(g.logs[len(g.logs)], '[X4LAN] LOBBY REQUEST HOST')
        self.state(2, 'hosting', 'Hosting on 10.0.0.2', "the host's code", invite='X4MP-1')
        self.frames(g)
        self.assertEqual((rows[0].name(), rows[1].name(), rows[2].name()), ('Hosting on 10.0.0.2', "the host's code", ''))
        self.assertTrue(cancel.selectable() and copy.selectable() and not host.selectable())
        refreshes = g.menu.refreshes
        self.frames(g, 60)
        self.assertEqual(g.menu.refreshes, refreshes)          # the same state is not drawn again
        (self.ext / 'lobby/state.lua').write_text('return {stamp = 3, pha')   # caught half-written: skipped, read again later
        self.frames(g)
        self.assertEqual(rows[0].name(), 'Hosting on 10.0.0.2')
        copy.callback()
        cancel.callback()
        self.assertEqual([g.logs[len(g.logs) - 1], g.logs[len(g.logs)]], ['[X4LAN] LOBBY REQUEST COPY', '[X4LAN] LOBBY REQUEST CANCEL'])
        join.callback()
        self.assertEqual(g.logs[len(g.logs)], '[X4LAN] LOBBY REQUEST JOIN')
        self.assertIsNone(g.PROBE_RAN)
        # the session: the launcher has rewritten config.lua; the overlay's files run in order with it
        (self.ext / 'ui/config.lua').write_text('X4LAN_CONFIG = {enabled = false}\nX4LAN_CONFIG.slot = "p2"\nX4LAN_CONFIG.token = "t1"\n'
                                                'X4LAN_CONFIG.lobby = {files = ' + self.FILES + ', session = true}\n')
        self.state(4, 'session', 'You are player 2 of 2.')
        self.frames(g)
        self.assertEqual((g.PROBE_RAN, g.DEFAULTS_RAN), ('p2', 't1'))
        logs = [g.logs[i] for i in range(1, len(g.logs) + 1)]
        self.assertTrue(any('MENU_INSTALLED rows=11' in t for t in logs) and any('SESSION_FILES run=2' in t for t in logs), logs)
        self.assertFalse(any('ERROR' in t for t in logs), logs)
        self.assertEqual(sum('STATE_UNREAD' in t for t in logs), 1, logs)   # the half-written state, said once

    def test_a_kit_with_a_server_has_join_only(self):
        lua, g = self.start('X4LAN_CONFIG = {lobby = {server = true, files = ' + self.FILES + '}}')
        self.frames(g, 1)
        g.menu.submenuHandler('x4lan_lobby')
        rows = [g.drawn[i] for i in range(1, len(g.drawn) + 1)]
        self.assertEqual([r.id for r in rows], ['header'] * 6 + ['line', 'x4lan_lobby_join', 'x4lan_lobby_cancel'])
        self.assertEqual(rows[7].name, 'Join the game')
        self.state(1, 'idle', '2 players, coop', 'Server 203.0.113.7. Choose Join.')
        self.frames(g)
        self.assertTrue(rows[7].selectable() and not rows[8].selectable())
        rows[7].callback()
        self.assertEqual(g.logs[len(g.logs)], '[X4LAN] LOBBY REQUEST JOIN')
        self.state(2, 'joined', 'Joined 203.0.113.7 as player 1 of 2.', '1 of 2 players are here.')
        self.frames(g)
        self.assertTrue(rows[8].selectable() and not rows[7].selectable())
        self.assertEqual(rows[1].name(), '1 of 2 players are here.')

    def test_after_the_load_the_files_run_at_once(self):
        lua, g = self.start('X4LAN_CONFIG = {slot = "p1", token = "t9", lobby = {files = ' + self.FILES + ', session = true}}')
        self.assertEqual((g.PROBE_RAN, g.DEFAULTS_RAN), ('p1', 't9'))
        self.assertIsNone(g.scripts.onUpdate)                  # no menu, no polling
        self.assertIsNone(g.X4LAN_LOBBY)

    def test_without_a_lobby_config_nothing_happens(self):
        lua, g = self.start('X4LAN_CONFIG = {enabled = false}')
        self.assertIsNone(g.scripts.onUpdate)
        self.assertEqual(len(g.logs), 0)

    def test_a_missing_overlay_file_is_an_error_row(self):
        (self.ext / 'ui/native_gate_probe.lua').unlink()
        lua, g = self.start('X4LAN_CONFIG = {lobby = {files = ' + self.FILES + ', session = true}}')
        logs = [g.logs[i] for i in range(1, len(g.logs) + 1)]
        self.assertTrue(any('ERROR missing ui/native_gate_probe.lua' in t for t in logs), logs)


if __name__ == '__main__':
    unittest.main()
