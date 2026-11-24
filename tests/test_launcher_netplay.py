"""lockstep.launcher.netplay (with nethost, netjoin, netlan): the page's requests and state table, and the host, join and
browse flows against fakes of the master server and of package 2's modules (tests/netplay_fakes.py). Real sockets on
this PC only (a host's lobby and file service on loopback); no X4 runs here, and no master, router or remote host is
contacted."""
import socket
import sys
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import lobby, netjoin, netkit, netlan, netplay  # noqa: E402
import netplay_fakes as fakes  # noqa: E402


class Requests(unittest.TestCase):
    def test_every_request_of_the_contract(self):
        parse = netplay.parse_request
        self.assertEqual(parse('HOST|1|4|Friday night'), netplay.Request('HOST', public=True, players=4, name='Friday night'))
        self.assertEqual(parse('HOST|0|2|'), netplay.Request('HOST', public=False, players=2, name=''))
        self.assertEqual(parse('HOST'), netplay.Request('HOST'))                     # the older page: the kit's session
        self.assertEqual(parse('JOINCODE| k7qx3fna '), netplay.Request('JOINCODE', text='k7qx3fna'))
        self.assertEqual(parse('JOINGAME|g1'), netplay.Request('JOINGAME', game_id='g1'))
        self.assertEqual(parse('JOINLAN|2'), netplay.Request('JOINLAN', index=2))
        self.assertEqual(parse('JOIN3'), netplay.Request('JOINSLOT', slot=3))        # a kit server's start picker
        for simple in ('JOIN', 'BROWSE', 'START', 'READY', 'CANCEL', 'COPY', 'LEAVE'):
            self.assertEqual(parse(simple), netplay.Request(simple))
        for bad in ('', 'HOST|2|4|x', 'HOST|1|9|x', 'HOST|1|1|x', 'HOST|1|4', 'JOINGAME|../x', 'JOINLAN|0', 'JOINLAN|x',
                    'JOINCODE|', 'START|now', 'JOIN9', 'host', None, 'X' * 600):
            self.assertIsNone(parse(bad), bad)

    def test_a_game_name_is_cleaned(self):
        request = netplay.parse_request('HOST|1|2|' + 'Zoë|s game\x07 ' + 'x' * 60)
        self.assertNotIn('|', request.name)
        self.assertNotIn('\x07', request.name)
        self.assertLessEqual(len(request.name.encode('utf-8')), netplay.GAME_NAME_MAX_BYTES)
        self.assertTrue(request.name.startswith('Zoës game'))

    def test_request_text_is_the_inverse(self):
        for text in ('HOST|1|4|Friday night', 'HOST|0|2|', 'JOINCODE|K7QX-3FNA', 'JOINGAME|g1', 'JOINLAN|3', 'JOIN5',
                     'BROWSE', 'JOIN', 'HOST'):
            self.assertEqual(netplay.request_text(netplay.parse_request(text)), text)

    def test_a_spelled_request_is_the_hex_of_its_utf8(self):
        digits = netplay.request_hex('HOST|1|3|Zoë')
        self.assertEqual(digits, 'HOST|1|3|Zoë'.encode('utf-8').hex())
        self.assertEqual(digits, digits.lower())
        self.assertEqual(netplay.request_from_hex(digits), 'HOST|1|3|Zoë')
        self.assertEqual(netplay.request_from_hex('4a4f494e'), 'JOIN')
        for bad in ('', '4a4', 'ZZ', '4A4F494E', 'ff'):
            self.assertIsNone(netplay.request_from_hex(bad), bad)


class JoinTargets(unittest.TestCase):
    def target(self, text):
        return netplay.join_target(text, fakes.normalize_code)

    def test_codes_invites_and_addresses(self):
        self.assertEqual(self.target('k7qx3fna'), netplay.JoinTarget('code', code='K7QX-3FNA'))
        token = 'ab' * 16
        self.assertEqual(self.target(f'here: X4MP-192.168.1.20-47000-{token} !'),
                         netplay.JoinTarget('invite', addresses=(('192.168.1.20', 47001),), token=token))
        self.assertEqual(self.target('10.20.0.5'), netplay.JoinTarget('address', addresses=(('10.20.0.5', 47001),)))
        self.assertEqual(self.target('10.20.0.5:47011, host.example.org;100.64.1.2'),
                         netplay.JoinTarget('address', addresses=(('10.20.0.5', 47011), ('host.example.org', 47001),
                                                                  ('100.64.1.2', 47001))))
        self.assertEqual(self.target('gamebox'), netplay.JoinTarget('address', addresses=(('gamebox', 47001),)))   # MagicDNS
        self.assertEqual(self.target('just words').kind, 'invalid')       # spaces: no address list

    def test_what_is_not_joinable_says_why(self):
        self.assertIn('not a join code', self.target('K7QX-3FN1').why)            # code-shaped, not of the alphabet
        self.assertIn('neither', self.target('10.20.0.5:99999').why)
        self.assertIn('neither', self.target('not an address!').why)
        self.assertEqual(self.target('   ').kind, 'invalid')
        self.assertIn('not a join code', netplay.join_target('K7QX-3FNA', None).why)   # no nat module: codes unknown


class StateTable(unittest.TestCase):
    def test_the_lua_table_of_the_contract(self):
        state = netplay.State()
        state.set('hosting', ['a', 'b', None, 'c', 'd', 'e', 'f', 'g'], code='K7QX-3FNA', public=True, host=True,
                  can_start=False, network={'mode': 'direct', 'mapping': 'upnp', 'public_ip_known': True, 'ping_ms': 38},
                  players=[{'slot': 1, 'ready': True, 'host': True}], progress={'what': 'download', 'done': 0.4},
                  games=None)
        text = state.text()
        self.assertTrue(text.startswith("return {stamp = 1, phase = 'hosting', "), text)
        for part in ("code = 'K7QX-3FNA'", 'public = true', 'can_start = false', "network = {mode = 'direct', "
                     "mapping = 'upnp', public_ip_known = true, ping_ms = 38}", 'players = {{slot = 1, ready = true, host = true}}',
                     "progress = {what = 'download', done = 0.4}", "lines = {'a', 'b', 'c', 'd', 'e', 'f'}"):
            self.assertIn(part, text)
        self.assertNotIn('games', text)                                    # None is no field
        state.set('hosting', ['a', 'b', 'c', 'd', 'e', 'f'], code='K7QX-3FNA', public=True, host=True, can_start=False,
                  network={'mode': 'direct', 'mapping': 'upnp', 'public_ip_known': True, 'ping_ms': 38},
                  players=[{'slot': 1, 'ready': True, 'host': True}], progress={'what': 'download', 'done': 0.4})
        self.assertEqual(state.view()[0], 1)                               # the same table: no new stamp
        state.update(progress={'what': 'download', 'done': 0.5})
        state.update(code=None)
        stamp, phase, lines, fields = state.view()
        self.assertEqual((stamp, phase, fields['progress']['done'], 'code' in fields), (3, 'hosting', 0.5, False))

    def test_floats_reach_lua_as_numbers(self):
        self.assertEqual(lobby.lua_value(0.25), '0.25')
        self.assertEqual(lobby.lua_value(float('nan')), '0')
        self.assertEqual(lobby.lua_value(3), '3')
        self.assertEqual(lobby.lua_value(True), 'true')


class Beats(unittest.TestCase):
    def test_the_heartbeat_runs_on_its_own_through_long_steps(self):
        master = fakes.FakeMaster()
        game = master.api_create_game('x', False, 'rel-1', 2, 'a' * 64)
        client = master.module().MasterClient('http://master.example.org:47100')
        beat = netplay.Heartbeat(client, game['game_id'], game['secret'], (fakes.MasterError,), log=lambda text: None,
                                 state=lambda: ('session', 12), interval=0.05)
        self.assertTrue(fakes.wait_for(lambda: beat.beats >= 2, 5))
        beat.stop()
        name, args, kwargs = master.called('heartbeat')[0]
        self.assertEqual((args, kwargs), ((game['game_id'], game['secret']), {'tick': 12, 'state': 'session'}))
        master.reachable = False                                  # a failing beat is logged; the thread goes on
        logs = []
        beat = netplay.Heartbeat(client, game['game_id'], game['secret'], (fakes.MasterError,), log=logs.append, interval=0.05)
        self.assertTrue(fakes.wait_for(lambda: len(logs) >= 2, 5))
        beat.stop()


class FlowCase(unittest.TestCase):
    """A host and joiners as flows in this process, each with its own kit, backend and work folder."""
    RELEASE = '0123456789ab-ba9876543210'

    def setUp(self):
        self.folder = fakes.Folder()
        self.root = self.folder.path
        self.port = fakes.free_port_pair()
        self.flows = []
        fakes.FakeAnnouncer.made = []
        self.builds = []
        patcher = mock.patch.object(netkit.subprocess, 'run', fakes.fake_build(self.builds))
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        for flow in self.flows:
            flow.end_session()
            flow.cancel()
        for flow in self.flows:
            job = flow.job
            if job is not None and job.thread is not None:
                job.thread.join(5)
        self.folder.cleanup()

    def flow(self, name, net, master_url='http://master.example.org:47100', host_files=None, own=None, release=None):
        kit = netkit.Kit(fakes.release_kit(self.root / name / 'kit', release=release or self.RELEASE))
        backend = fakes.FakeBackend(self.root / name / 'work', host_files=host_files, own=own)
        config = netplay.FlowConfig(kit=kit, release=kit.release, master_url=master_url, port=self.port, load_wait=20)
        logs = []
        flow = netplay.Flow(config, backend, net=net, log=logs.append)
        flow.logs = logs
        self.flows.append(flow)
        return flow

    def session_files(self, players=2):
        return fakes.make_session(self.root / 'host-session', players)

    def wait_phase(self, flow, phase, seconds=10):
        reached = fakes.wait_for(lambda: flow.state.phase == phase, seconds)
        self.assertTrue(reached, f'phase {flow.state.phase} {flow.state.lines}, not {phase}; log: {flow.logs[-5:]}')

    def fields(self, flow):
        return flow.state.view()[3]


class HostWithMaster(FlowCase):
    def test_direct_mode_runs_the_relay_here_and_allows_the_check_ins(self):
        master = fakes.FakeMaster()
        nat = fakes.FakeNat(reachable=True)
        host = self.flow('host', fakes.network(master, nat), host_files=self.session_files(2))
        host.request('HOST|1|2|Friday night')
        self.wait_phase(host, 'hosting')
        fields = self.fields(host)
        self.assertEqual(fields['code'], 'K7QX-3FNA')
        self.assertEqual(host.backend.clipboard, 'K7QX-3FNA')
        self.assertEqual(fields['network']['mode'], 'direct')
        self.assertEqual(fields['network']['mapping'], 'upnp')
        self.assertEqual(fields['network']['ping_ms'], 38)
        self.assertFalse(fields['can_start'])
        game = next(iter(master.games.values()))
        self.assertEqual(game['direct'], {'udp_port': self.port, 'tcp_port': self.port + 1})
        self.assertEqual(game['lan'], {'candidates': ['192.168.10.5'], 'udp_port': self.port, 'tcp_port': self.port + 1})
        self.assertEqual((game['name'], game['public'], game['release'], game['players_max']),
                         ('Friday night', True, self.RELEASE, 2))
        self.assertEqual(sorted(game['files']), ['play-recipe.json', 'session-save.xml.gz', 'session.json'])
        for name in ('play-recipe.json', 'session.json'):          # nothing of the host's PC is published: no paths
            self.assertNotIn(str(self.root), game['files'][name].decode())
            self.assertNotIn(':\\\\', game['files'][name].decode())
        announcer = fakes.FakeAnnouncer.made[0]
        self.assertEqual((announcer.fields['code'], announcer.fields['tcp_port'], announcer.started), ('K7QX-3FNA', self.port + 1, True))
        slot = master.add_player(game['game_id'], ready=True)      # every slot ready: the host starts the game
        start = host.take_start(timeout=10)
        self.assertIsNotNone(start, host.logs[-5:])
        self.assertEqual((start.slot, start.clients, start.mode, start.relay_host), (1, 2, 'direct', '127.0.0.1'))
        self.assertEqual(start.relay.port, self.port)
        self.assertEqual(start.relay.token, game['start']['relay']['token'])
        self.assertEqual(start.overlay, host.backend.host_files.overlay)
        reached = netlan.check_in([('127.0.0.1', self.port + 1)], game['game_id'], slot)   # the joiner's check-in
        self.assertEqual(reached, '127.0.0.1')
        self.assertIsNone(netlan.check_in([('127.0.0.1', self.port + 1)], game['game_id'], 7))   # not a player: refused
        self.assertEqual(start.relay.allow_file.read_text().split(), ['127.0.0.1'])
        self.wait_phase(host, 'loading')                           # config.lua is not written yet: not 'session'
        host.request('CANCEL')                                     # too late to take the game away: a leave
        time.sleep(0.5)
        self.assertIn(game['game_id'], master.games)
        self.assertEqual(host.state.phase, 'loading')
        host.session_running(tick=lambda: 120)
        self.wait_phase(host, 'session')
        self.assertEqual(self.fields(host)['code'], 'K7QX-3FNA')   # the in-session page keeps the code
        host.end_session({'outcome': 'played'})
        self.assertTrue(fakes.wait_for(lambda: not master.games))  # the game is deleted at the end
        self.assertTrue(nat.mappings[0].released)                  # and the port mapping too
        self.assertTrue(nat.kept[0][1].is_set())
        self.assertTrue(announcer.stopped)

    def test_not_reachable_means_the_masters_relay(self):
        master = fakes.FakeMaster()
        host = self.flow('host', fakes.network(master, fakes.FakeNat(map_ok=False, reachable=False)),
                         host_files=self.session_files(2))
        host.request('HOST|0|2|')
        self.wait_phase(host, 'hosting')
        self.assertEqual(self.fields(host)['network']['mode'], 'relay')
        self.assertEqual(self.fields(host)['network']['mapping'], 'none')
        game = next(iter(master.games.values()))
        self.assertIsNone(game['direct'])
        self.assertEqual(game['name'], '2-player game')
        self.assertEqual(fakes.FakeAnnouncer.made, [])             # a private game is not announced
        master.add_player(game['game_id'])
        start = host.take_start(timeout=10)
        self.assertEqual((start.mode, start.relay, start.relay_host, start.port), ('relay', None, fakes.RELAY_HOST, 47200))

    def test_start_waits_for_every_slot(self):
        master = fakes.FakeMaster()
        host = self.flow('host', fakes.network(master), host_files=self.session_files(3))
        host.request('HOST|1|3|')
        self.wait_phase(host, 'hosting')
        game = next(iter(master.games.values()))
        master.add_player(game['game_id'], ready=True)
        host.request('START')
        self.assertTrue(fakes.wait_for(lambda: any('Start waits' in line for line in host.state.lines)), host.state.lines)
        self.assertEqual(master.called('start'), [])
        master.add_player(game['game_id'], ready=True)
        self.assertIsNotNone(host.take_start(timeout=10))
        self.assertEqual(len(master.called('start')), 1)

    def test_cancel_deletes_the_game_and_frees_the_ports(self):
        master = fakes.FakeMaster()
        nat = fakes.FakeNat()
        host = self.flow('host', fakes.network(master, nat), host_files=self.session_files(2))
        host.request('HOST|1|2|x')
        self.wait_phase(host, 'hosting')
        job = host.job
        host.request('CANCEL')
        self.assertEqual(host.state.phase, 'idle')
        job.thread.join(5)
        self.assertEqual(master.games, {})
        self.assertTrue(nat.mappings[0].released)
        with socket.socket() as again:                              # the lobby's port is free again
            again.bind(('0.0.0.0', self.port + 1))

    def test_a_kit_session_for_other_players_is_refused(self):
        own = self.session_files(2)
        host = self.flow('host', fakes.network(fakes.FakeMaster()), own=own)
        host.request('HOST|1|4|x')
        self.wait_phase(host, 'error')
        self.assertIn('made for 2 players', host.state.lines[0])


class HostOnTheLan(FlowCase):
    def test_no_master_means_the_lan_lobby_and_a_joiner_downloads_the_session(self):
        master = fakes.FakeMaster(reachable=False)
        host = self.flow('host', fakes.network(master), host_files=self.session_files(2))
        host.request('HOST|0|2|')
        self.wait_phase(host, 'hosting')
        invite = self.fields(host)['code']
        self.assertTrue(invite.startswith('X4MP-'), invite)
        self.assertEqual(host.backend.clipboard, invite)
        self.assertTrue(any('does not answer' in line for line in host.state.lines), host.state.lines)
        self.assertEqual(self.fields(host)['network']['mode'], 'lan')
        address, port, token = lobby.parse_invite(invite)
        joiner = self.flow('joiner', fakes.network(fakes.FakeMaster(reachable=False)))
        joiner.backend.clipboard = invite.replace(address, '127.0.0.1')   # the test's host is this PC
        joiner.request('JOIN')
        start = joiner.take_start(timeout=20)
        self.assertIsNotNone(start, (joiner.state.lines, joiner.logs[-6:]))
        host_start = host.take_start(timeout=10)
        self.assertEqual((start.slot, start.clients, start.mode, start.relay_host, start.port, start.token),
                         (2, 2, 'lan', '127.0.0.1', self.port, token))
        self.assertEqual((host_start.slot, host_start.relay_host, host_start.token), (1, '127.0.0.1', token))
        self.assertEqual(host_start.relay.allow_from, ('127.0.0.1',))
        self.assertEqual(len(self.builds), 1)                       # the joiner built its overlay here
        command = self.builds[0]
        self.assertEqual(Path(command[command.index('--gate-dll') + 1]), joiner.config.kit.gate_dll.resolve())
        self.assertIn('--session', command)
        self.assertEqual(joiner.backend.installed[0].overlay, start.overlay)
        save = start.snapshot.read_bytes()
        self.assertEqual(save, host.backend.host_files.snapshot.read_bytes())

    def test_a_private_lan_game_needs_the_invite_codes_token(self):
        host = self.flow('host', fakes.network(None), master_url=None, host_files=self.session_files(2))
        host.request('HOST|0|2|')
        self.wait_phase(host, 'hosting')
        joiner = self.flow('joiner', fakes.network(None), master_url=None)
        joiner.request(f'JOINCODE|127.0.0.1:{self.port + 1}')       # an address: no token
        self.wait_phase(joiner, 'error')
        self.assertIn('The host refused', joiner.state.lines[0])
        self.assertIn('invite code', joiner.state.lines[1])

    def test_a_public_lan_game_takes_a_joiner_by_address_and_another_release_is_refused(self):
        host = self.flow('host', fakes.network(None), master_url=None, host_files=self.session_files(3))
        host.request('HOST|1|3|LAN party')
        self.wait_phase(host, 'hosting')
        self.assertEqual(fakes.FakeAnnouncer.made[0].fields['code'], '-')
        other = self.flow('other', fakes.network(None), master_url=None, release='fedcba987654-000000000000')
        other.request(f'JOINCODE|127.0.0.1:{self.port + 1}')
        self.wait_phase(other, 'error')
        self.assertIn('release', other.state.lines[1])
        joiner = self.flow('joiner', fakes.network(None), master_url=None)
        joiner.request(f'JOINCODE|127.0.0.1:{self.port + 1}')
        self.wait_phase(joiner, 'waiting', 20)
        self.assertTrue(fakes.wait_for(lambda: '2 of 3' in ' '.join(host.state.lines)), host.state.lines)
        self.assertIsNone(host.take_start(timeout=0.5))             # three slots: the third is not there


class JoinWithMaster(FlowCase):
    def hosted(self, players=2, reachable=False, public=True):
        master = fakes.FakeMaster()
        host = self.flow('host', fakes.network(master, fakes.FakeNat(reachable=reachable)), host_files=self.session_files(players))
        host.request(f'HOST|{1 if public else 0}|{players}|Friday')
        self.wait_phase(host, 'hosting')
        return master, host, next(iter(master.games.values()))

    def test_by_code_download_build_ready_start(self):
        master, host, game = self.hosted()
        joiner = self.flow('joiner', fakes.network(master))
        joiner.request('JOINCODE|k7qx3fna')
        start = joiner.take_start(timeout=20)
        self.assertIsNotNone(start, (joiner.state.lines, joiner.logs[-6:]))
        self.assertEqual((start.slot, start.mode, start.relay_host, start.port), (2, 'relay', fakes.RELAY_HOST, 47200))
        self.assertEqual(start.token, game['start']['relay']['token'])
        self.assertEqual(len(master.called('ready')), 1)
        self.assertEqual(len(self.builds), 1)
        command = self.builds[0]
        self.assertIn("native_gate_render_motion='interpolate'", command)
        self.assertEqual(Path(command[command.index('--gate-dll') + 1]), joiner.config.kit.gate_dll.resolve())
        self.assertEqual(joiner.backend.installed[0].snapshot.read_bytes(), host.backend.host_files.snapshot.read_bytes())
        host_start = host.take_start(timeout=10)
        self.assertEqual(host_start.token, start.token)

    def test_another_release_is_refused_with_a_clear_line(self):
        master, host, game = self.hosted()
        joiner = self.flow('joiner', fakes.network(master), release='fedcba987654-000000000000')
        joiner.request('JOINCODE|K7QX-3FNA')
        self.wait_phase(joiner, 'error')
        self.assertIn('another release', joiner.state.lines[0])
        self.assertIn('same release', joiner.state.lines[1])
        self.assertEqual(master.called('join'), [])                 # refused before it took a slot

    def test_a_host_built_with_another_gate_is_refused_after_the_download(self):
        master, host, game = self.hosted()
        kit_dll = self.root / 'joiner2' / 'kit'
        joiner = self.flow('joiner2', fakes.network(master))
        (kit_dll / 'native' / 'x4lan_gate_64.dll').write_bytes(b'MZ-another-gate')
        joiner.request('JOINCODE|K7QX-3FNA')
        self.wait_phase(joiner, 'error')
        self.assertIn('another release', ' '.join(joiner.state.lines))
        self.assertEqual(self.builds, [])                           # nothing was built from it
        self.assertTrue(fakes.wait_for(lambda: len(master.called('leave')) == 1))

    def test_direct_mode_checks_in_on_this_subnet_first(self):
        master, host, game = self.hosted(reachable=True)
        game['lan'] = {'candidates': ['10.1.2.3', '192.168.10.7'], 'udp_port': self.port, 'tcp_port': self.port + 1}
        joiner = self.flow('joiner', fakes.network(master))
        tried = []

        def check_in(addresses, game_id, slot, timeout=1.5, log=print):
            tried.extend(addresses)
            return '192.168.10.7'
        with mock.patch.object(netjoin.netlan, 'check_in', check_in), \
                mock.patch.object(netplay.lobby, 'local_address', lambda: '192.168.10.5'):
            joiner.request('JOINCODE|K7QX-3FNA')
            start = joiner.take_start(timeout=20)
        self.assertIsNotNone(start, joiner.state.lines)
        self.assertEqual(tried, [('192.168.10.7', self.port + 1), ('10.1.2.3', self.port + 1), (fakes.HOST_PUBLIC, self.port + 1)])
        self.assertEqual((start.mode, start.relay_host), ('direct', '192.168.10.7'))

    def test_direct_mode_without_any_answering_address_fails_clearly(self):
        master, host, game = self.hosted(reachable=True)
        joiner = self.flow('joiner', fakes.network(master))
        with mock.patch.object(netjoin.netlan, 'check_in', lambda *args, **kwargs: None):
            joiner.request('JOINCODE|K7QX-3FNA')
            self.wait_phase(joiner, 'error', 20)
        self.assertIn('cannot reach the host directly', joiner.state.lines[0])

    def test_from_the_public_list_and_from_the_lan_list(self):
        master, host, game = self.hosted()
        lan = fakes.FakeLandisco([{'ip': '192.168.10.9', 'candidates': ['192.168.10.9'], 'release': self.RELEASE,
                                   'code': 'K7QX-3FNA', 'name': 'Friday', 'tcp_port': self.port + 1, 'players': 1,
                                   'players_max': 2}])
        browser = self.flow('browser', fakes.network(master, landisco=lan))
        browser.request('BROWSE')
        self.assertTrue(fakes.wait_for(lambda: self.fields(browser).get('games')), browser.state.lines)
        fields = self.fields(browser)
        self.assertEqual(fields['games'][0], {'id': game['game_id'], 'name': 'Friday', 'players': 1, 'max': 2,
                                              'state': 'lobby', 'compatible': True, 'mode': 'relay'})
        self.assertEqual(fields['lan'][0], {'index': 1, 'name': 'Friday', 'players': 1, 'max': 2, 'compatible': True})
        plan = browser.join_plan(netplay.parse_request('JOINLAN|1'))
        self.assertEqual(plan[1], netplay.JoinTarget('code', code='K7QX-3FNA', addresses=(('192.168.10.9', self.port + 1),)))
        browser.request(f'JOINGAME|{game["game_id"]}')             # a browse gives way to a join
        start = browser.take_start(timeout=20)
        self.assertIsNotNone(start, browser.state.lines)
        self.assertEqual(start.slot, 2)

    def test_cancel_while_waiting_leaves_the_game(self):
        master, host, game = self.hosted(players=3)
        joiner = self.flow('joiner', fakes.network(master))
        joiner.request('JOINCODE|K7QX-3FNA')
        self.wait_phase(joiner, 'waiting', 20)
        self.assertTrue(fakes.wait_for(lambda: len(self.fields(joiner).get('players') or []) == 2), self.fields(joiner))
        self.assertEqual(self.fields(joiner)['players'][1], {'slot': 2, 'ready': True, 'host': False})
        joiner.request('CANCEL')
        self.assertEqual(joiner.state.phase, 'idle')
        self.assertTrue(fakes.wait_for(lambda: len(master.called('leave')) == 1))
        self.assertEqual(len(game['players']), 1)

    def test_a_join_code_without_a_master(self):
        joiner = self.flow('joiner', fakes.network(None), master_url=None)
        joiner.request('JOINCODE|K7QX-3FNA')
        self.wait_phase(joiner, 'error')
        self.assertIn('need the master server', joiner.state.lines[0])


class HandOver(FlowCase):
    """The Steam helper's flow: hosting and joining are handed to x4ls lobby (restart), resolved first."""

    class Restarting(fakes.FakeBackend):
        def restart_for(self, request):
            return request.kind == 'HOST' or request.joins

    def test_requests_become_the_lobbys_first_request(self):
        helper = self.flow('helper', fakes.network(fakes.FakeMaster()))
        helper.backend.__class__ = self.Restarting
        helper.request('HOST|1|4|Zoë')
        self.assertEqual(helper.state.phase, 'restart')
        self.assertEqual(helper.take_handover(), 'HOST|1|4|Zoë')
        helper.backend.clipboard = 'k7qx3fna'
        helper.request('JOIN')
        self.assertEqual(helper.take_handover(), 'JOINCODE|K7QX-3FNA')   # the clipboard is read here, not later
        helper.backend.clipboard = 'just words'
        helper.request('JOIN')
        self.assertEqual(helper.state.phase, 'error')
        self.assertIsNone(helper.take_handover())
        helper.listing['lan'] = [{'ip': '192.168.10.9', 'candidates': ['192.168.10.9', '10.1.2.3'], 'code': '-',
                                  'tcp_port': 47001, 'release': self.RELEASE}]
        helper.request('JOINLAN|1')
        self.assertEqual(helper.take_handover(), 'JOINCODE|192.168.10.9:47001,10.1.2.3:47001')
        helper.request('JOINGAME|g7')
        self.assertEqual(helper.take_handover(), 'JOINGAME|g7')


class LanLobby(unittest.TestCase):
    """netlan.GameHost directly: admissions, an older launcher, ready and the start."""

    def setUp(self):
        self.folder = fakes.Folder()
        self.port = fakes.free_port_pair()
        self.allow = netlan.AllowList(self.folder.path / 'allow.txt')
        self.host = netlan.GameHost(self.port, 3, 'rel-1', 'kit-1', self.allow, log=lambda text: None, token='ab' * 16)

    def tearDown(self):
        self.host.close()
        self.folder.cleanup()

    def hello(self, message):
        sock = socket.create_connection(('127.0.0.1', self.port + 1), timeout=5)
        lines = lobby.Lines(sock)
        lobby.send(sock, message)
        for _ in range(200):
            self.host.poll()
            answer = lines.wait(0.02)
            if answer is not None:
                return sock, lines, answer
        self.fail('no answer')

    def test_private_public_releases_and_an_older_launcher(self):
        sock, _, answer = self.hello({'join': 1, 'release': 'rel-1', 'kit': None, 'token': None})
        self.assertEqual(answer, {'ok': False, 'why': "the invite code is not this game's"})
        sock.close()
        sock, _, answer = self.hello({'join': 1, 'release': 'rel-2', 'kit': None, 'token': 'ab' * 16})
        self.assertIn('release rel-1', answer['why'])
        sock.close()
        first, first_lines, answer = self.hello({'join': 1, 'release': 'rel-1', 'kit': None, 'token': 'ab' * 16})
        self.assertEqual((answer['ok'], answer['slot'], answer['clients'], answer['token']), (True, 2, 3, 'ab' * 16))
        self.assertEqual(self.host.ready_slots(), [])               # ready once its overlay is built
        old, old_lines, answer = self.hello({'token': 'ab' * 16, 'kit': 'kit-1'})   # x4ls lobby before netplay
        self.assertEqual(answer['slot'], 3)
        self.assertEqual(self.host.ready_slots(), [3])
        self.assertFalse(self.host.all_ready())
        lobby.send(first, {'ready': True})
        self.assertTrue(fakes.wait_for(lambda: self.host.poll() or self.host.all_ready(), 5))
        late, _, answer = self.hello({'join': 1, 'release': 'rel-1', 'token': 'ab' * 16})
        self.assertEqual(answer['why'], 'the game is full')
        late.close()
        self.assertEqual(self.host.start(1800000000, 'ab' * 16), ('127.0.0.1',))
        message = None
        for _ in range(10):
            message = first_lines.wait(2)
            if message and 'start' in message:
                break
        self.assertEqual(message, {'start': {'host': '127.0.0.1', 'port': self.port, 'arm_at': 1800000000, 'slot': 2,
                                             'token': 'ab' * 16}})
        self.assertEqual(self.allow.path.read_text().split(), ['127.0.0.1'])
        for sock in (first, old):
            sock.close()

    def test_a_master_game_sends_lan_joiners_to_its_code(self):
        self.host.game_id = 'g1'
        self.host.code = 'K7QX-3FNA'
        sock, _, answer = self.hello({'join': 1, 'release': 'rel-1', 'token': 'ab' * 16})
        self.assertEqual(answer, {'ok': False, 'why': 'this game is joined with its code', 'code': 'K7QX-3FNA'})
        sock.close()

    def test_the_allow_list_learns_addresses_once(self):
        self.assertFalse(self.allow.add('127.0.0.1'))
        self.assertTrue(self.allow.add('192.0.2.7'))
        self.assertFalse(self.allow.add('192.0.2.7'))
        self.assertFalse(self.allow.add('not-an-ip'))
        self.assertEqual(self.allow.path.read_text().split(), ['127.0.0.1', '192.0.2.7'])


class LanFiles(unittest.TestCase):
    def test_fetch_verifies_resumes_and_refuses(self):
        from lockstep.launcher import netfiles
        folder = fakes.Folder()
        try:
            data = bytes(range(256)) * 5000
            source = folder.path / 'session-save.xml.gz'
            source.write_bytes(data)
            sha = netfiles.sha256_file(source)
            allow = {'127.0.0.1'}
            token = 'cd' * 16
            files = netfiles.FileHost(token, 0, allow, (2,), bind='127.0.0.1').start()
            try:
                files.offer('session-save.xml.gz', source)
                with self.assertRaises(ValueError):
                    files.offer('j600.x4join', source)              # only the three session names
                dest = folder.path / 'got' / 'session-save.xml.gz'
                seen = []
                netfiles.fetch('127.0.0.1', files.port, token, 2, 'session-save.xml.gz', dest, sha,
                               progress=lambda done, total: seen.append(done))
                self.assertEqual(dest.read_bytes(), data)
                self.assertEqual(seen[-1], len(data))
                part = folder.path / 'again' / 'session-save.xml.gz.part'
                part.parent.mkdir()
                part.write_bytes(data[:100000])
                netfiles.fetch('127.0.0.1', files.port, token, 2, 'session-save.xml.gz', part.parent / 'session-save.xml.gz', sha)
                self.assertEqual(files.served[-1][2:], (100000, len(data)))
                with self.assertRaisesRegex(netfiles.FetchError, r'refused by the host \(mac\)'):
                    netfiles.fetch('127.0.0.1', files.port, 'ef' * 16, 2, 'session-save.xml.gz', folder.path / 'a', sha, retries=0)
                with self.assertRaisesRegex(netfiles.FetchError, r'\(slot\)'):
                    netfiles.fetch('127.0.0.1', files.port, token, 3, 'session-save.xml.gz', folder.path / 'b', sha, retries=0)
                with self.assertRaisesRegex(netfiles.FetchError, 'another file'):
                    netfiles.fetch('127.0.0.1', files.port, token, 2, 'session-save.xml.gz', folder.path / 'c', '0' * 64, retries=0)
                allow.discard('127.0.0.1')
                with self.assertRaisesRegex(netfiles.FetchError, r'\(address\)'):
                    netfiles.fetch('127.0.0.1', files.port, token, 2, 'session-save.xml.gz', folder.path / 'd', sha, retries=0)
            finally:
                files.close()
        finally:
            folder.cleanup()


class HostLines(unittest.TestCase):
    """The live run of 23 Nov: the router opened the ports (NAT-PMP) but Windows Firewall blocked the release's own
    python.exe, and the host's local relay said tick -1 before its first step, which the master refuses."""
    def test_mapped_but_unreachable_names_the_firewall(self):
        from lockstep.launcher import nethost
        line = nethost.network_line({'mode': 'relay', 'mapping': 'natpmp', 'ping_ms': 11})
        self.assertIn('firewall on this PC', line)
        self.assertIn('Ping to the server 11 ms', line)
        self.assertNotIn('firewall', nethost.network_line({'mode': 'relay', 'mapping': 'none'}))
        self.assertIn('NAT-PMP', nethost.network_line({'mode': 'direct', 'mapping': 'natpmp'}))

    def test_the_heartbeat_tick_is_never_negative(self):
        from lockstep.launcher import nethost
        host = nethost.HostJob.__new__(nethost.HostJob)
        host.flow = mock.Mock(session_info={'tick': lambda: -1})
        self.assertEqual(host.session_tick(), 0)
        host.flow.session_info = {'tick': lambda: 1234}
        self.assertEqual(host.session_tick(), 1234)


class Firewall(unittest.TestCase):
    """FIREWALL: an inbound rule for the launcher's own Python, asked of Windows through its elevation prompt."""
    def test_the_rule_names_python_exe_beside_pythonw(self):
        rule = netplay.firewall_rule(r'C:\Games\X4 Online\python\pythonw.exe')
        if rule is None:
            self.skipTest('the known folder is asked on Windows only')
        self.assertIn(r'program="C:\Games\X4 Online\python\python.exe"', rule)
        self.assertIn('dir=in action=allow', rule)
        self.assertTrue(rule.startswith('advfirewall firewall add rule name="X4 Online (X4 Online)"'))

    def test_the_request_asks_windows_and_notes_it(self):
        self.assertEqual(netplay.parse_request('FIREWALL').kind, 'FIREWALL')
        if netplay.firewall_rule() is None:
            self.skipTest('the known folder is asked on Windows only')
        flow = netplay.Flow.__new__(netplay.Flow)
        flow.state = netplay.State()
        flow.state.set('hosting', ('Join code: K7QX-3FNA',), code='K7QX-3FNA')
        flow.log = lambda *_: None
        import ctypes
        with mock.patch.object(ctypes.windll.shell32, 'ShellExecuteW', return_value=42) as shell:
            flow.allow_firewall()
        verb, program, arguments = shell.call_args[0][1:4]
        self.assertEqual((verb, program), ('runas', 'netsh.exe'))
        self.assertIn('advfirewall firewall add rule', arguments)
        _stamp, phase, lines, fields = flow.state.view()
        self.assertEqual(phase, 'hosting')
        self.assertEqual(fields.get('code'), 'K7QX-3FNA')
        self.assertTrue(lines[-1].startswith('Windows Firewall: once allowed'))


RULES = r"""Rule Name:                            python.exe
----------------------------------------------------------------------
Enabled:                              Yes
Direction:                            In
Profiles:                             Private,Public
Protocol:                             TCP
Action:                               Allow
Program:                              C:\users\player\appdata\local\x4 online\python\python.exe

Rule Name:                            other.exe
----------------------------------------------------------------------
Enabled:                              Yes
Direction:                            In
Action:                               Block
Program:                              C:\games\other\python.exe

Rule Name:                            off.exe
----------------------------------------------------------------------
Enabled:                              No
Direction:                            In
Action:                               Allow
Program:                              C:\games\off\python.exe
"""


class FirewallRules(unittest.TestCase):
    """firewall_allowed: Windows' own rules (its security alert's 'python.exe' rules) for the launcher's Python."""
    def test_an_enabled_allow_rule_for_this_python(self):
        allowed = netplay.firewall_allowed(r'C:\Users\Player\AppData\Local\X4 Online\python\pythonw.exe', rules_text=RULES)
        if sys.platform != 'win32':
            self.skipTest('the known folder is asked on Windows only')   # paths compare as Windows compares them
        self.assertTrue(allowed)                        # pythonw -> python.exe, case and separators as Windows
        self.assertFalse(netplay.firewall_allowed(r'C:\games\other\python.exe', rules_text=RULES))   # a Block rule
        self.assertFalse(netplay.firewall_allowed(r'C:\games\off\python.exe', rules_text=RULES))     # disabled
        self.assertFalse(netplay.firewall_allowed(r'C:\games\none\python.exe', rules_text=RULES))    # no rule


if __name__ == '__main__':
    unittest.main()
