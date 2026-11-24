"""The master's relay pool: the relay's settings from an uploaded play-recipe.json, its command line, and full games in
relay mode that really start tools/dev/native_gate_relay.py on 127.0.0.1 (protocol 4 datagrams are answered, a drop-in
game's join files go from the saver through the master to the joiner, a relay that ends or idles ends its game).
The join package reader is a stand-in (as in test_launcher_dropin.py); no X4 runs."""
import hashlib
import struct
import tempfile
import time
import unittest
from pathlib import Path

from master_support import (HOST_IP, OTHER_IP, PLAYER_IP, MasterCase, MasterLimits, recipe, sha256, udp_socket)
from lockstep.launcher import hotjoin, joinfiles, serve, session
from lockstep.master import relays


class FakeJoin:
    """Stands in for tools/dev/native_gate_join.py: a package is its own text, 'save=<sha of the save>=<tick>', and may
    end in zero bytes (as a real one is mostly zeros: the compressed copy's case)."""
    class JoinError(Exception):
        pass

    @staticmethod
    def read_package(data):
        _, save_sha, tick = data.rstrip(b'\0').decode().split('=')
        return {'sha': hashlib.sha256(data).hexdigest(), 'header': {'save_sha': save_sha},
                'sections': {'gate': {'tick': int(tick)}}}


def state_report(token, slot, tick, clients, lookahead=1):
    """A protocol 4 state report as a client's gate sends it (tests/test_native_gate_drop.py's layout)."""
    game = 20 + tick / 30
    real = 5 + slot * 0.25 + tick / 30
    fields = ['NGSTATE', '4', token, str(slot), str(tick), struct.pack('>d', game).hex(), struct.pack('>d', real).hex(),
              'b' * 64, str(clients), str(lookahead), '']
    return '|'.join(fields).encode('ascii')


class RecipeTests(unittest.TestCase):
    def test_the_settings_come_from_the_window_the_sets_and_the_defaults(self):
        settings = relays.recipe_settings(recipe(players=3, open_slots=[3], window=(40, 72040)), 3, (3,))
        self.assertEqual((settings.players, settings.open_slots, settings.present), (3, (3,), [1, 2]))
        self.assertEqual(settings.ticks, 72040 + session.PLAY_TAIL_TICKS)
        self.assertEqual((settings.tick_hz, settings.lookahead), (30, 1))   # tools/dev/native_gate_defaults.lua
        chosen = relays.recipe_settings(recipe(sets=['native_gate_tick_hz = 60', 'native_gate_lookahead=2']), 2, ())
        self.assertEqual((chosen.tick_hz, chosen.lookahead), (60, 2))

    def test_recipes_the_relay_cannot_serve(self):
        bad = [(b'not json', 2, ()), (b'[]', 2, ()), (recipe(players=3), 2, ()), (recipe(open_slots=[2]), 2, ()),
               (recipe(window=(40, 1 << 24)), 2, ()), (recipe(window=(50, 50)), 2, ()),
               (recipe(sets=['native_gate_tick_hz=45']), 2, ()), (recipe(sets=['native_gate_lookahead=adaptive']), 2, ()),
               (recipe(sets=['native_gate_lookahead=0']), 2, ()), (recipe(sets=['native_gate_wire_crypto=true']), 2, ()),
               (recipe(sets=['native_gate_peer_window=2']), 2, ()), (recipe(sets=[7]), 2, ()),
               (recipe().replace(b'x4ls-play-recipe', b'something-else'), 2, ())]
        for data, players, opened in bad:
            with self.assertRaises(relays.RecipeError, msg=data[:80]):
                relays.recipe_settings(data, players, opened)

    def test_the_command_is_serves(self):
        with tempfile.TemporaryDirectory() as folder:
            run = Path(folder)
            settings = relays.recipe_settings(recipe(players=3, open_slots=[3]), 3, (3,))
            command = relays.relay_command(settings, 47210, 'a' * 32, run, '0.0.0.0', ['203.0.113.10', '198.51.100.20'],
                                           run / 'allow.txt')
        text = ' '.join(command)
        self.assertEqual(command[1], str(session.RELAY))
        for part in ('--port 47210', '--clients 3', '--ticks 72041', '--relative-real', '--observe', '--keep-going',
                     '--lookahead 1', '--stall-drop-ms 15000', '--bind 0.0.0.0', '--allow-from 203.0.113.10,198.51.100.20',
                     '--allow-file', '--join --absent 3 --join-savers 1,2', '--join-observe',
                     f'--join-max-pause {serve.join_pause_limit(0)}', '--join-catchup-limit 120'):
            self.assertIn(part, text)
        self.assertIn('--tick-hz 30', text)   # relay_command names every rate but 60


class RelayGameTests(MasterCase):
    def start_game(self, players=2, open_slots=(), present_ips=(PLAYER_IP,)):
        game = self.host_game(players_max=players, open_slots=list(open_slots))
        self.assertEqual(self.upload(game, 'play-recipe.json', recipe(players=players, open_slots=open_slots)).status, 200)
        tickets = {}
        for ip in present_ips:
            joined = self.join(game, ip=ip).json
            tickets[joined['slot']] = joined['ticket']
            self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': joined['ticket']}, ip=ip)
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']}, ip=HOST_IP)
        self.assertEqual(answer.status, 200, answer.body)
        start = self.lobby(game, game['host_ticket'], ip=HOST_IP).json['start']
        return game, tickets, start

    def test_host_join_ready_start_and_the_relay_answers(self):
        game, tickets, start = self.start_game()
        self.assertEqual(start['mode'], 'relay')
        relay = start['relay']
        self.assertEqual(relay['host'], '127.0.0.1')
        self.assertTrue(self.relay_ports[0] <= relay['port'] <= self.relay_ports[1])
        self.assertEqual(self.call('GET', '/api/v1/health').json['relays'], 1)
        running = self.master.games[game['game_id']].relay
        self.assertEqual(running.allowed, {HOST_IP, PLAYER_IP})
        relay_output = (running.run_dir / 'relay.log').read_text()
        self.assertIn('NATIVE_GATE RELAY READY', relay_output)
        clients = [udp_socket(), udp_socket()]
        try:
            for slot, client in enumerate(clients, start=1):
                client.sendto(state_report(relay['token'], slot, 0, 2), ('127.0.0.1', relay['port']))
            for client in clients:
                answer, source = client.recvfrom(4096)
                self.assertEqual(source[1], relay['port'])
                self.assertTrue(answer.startswith(b'NG'), answer[:40])
                self.assertIn(relay['token'].encode(), answer)
        finally:
            for client in clients:
                client.close()

    def test_a_new_address_of_a_player_is_added_to_the_allow_file(self):
        game, tickets, start = self.start_game()
        self.lobby(game, tickets[2], ip='192.0.2.77')
        running = self.master.games[game['game_id']].relay
        self.assertIn('192.0.2.77', running.allow_file.read_text(encoding='ascii').split())

    def test_the_relay_ending_ends_the_game_and_frees_its_port(self):
        game, tickets, start = self.start_game()
        running = self.master.games[game['game_id']].relay
        running.process.kill()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and self.master.pool.running():
            time.sleep(0.2)
        self.assertEqual(self.master.pool.running(), 0)
        self.assertEqual(self.lobby(game, tickets[2], ip=PLAYER_IP).json['state'], 'ended')
        self.assertTrue(any('over (the relay ended' in line for line in self.logs), self.logs)
        self.assertFalse((self.data / 'games' / game['game_id']).exists())

    def test_drop_in_join_files_from_the_saver_to_the_joiner(self):
        original = hotjoin.join_tools
        hotjoin.join_tools = lambda: FakeJoin
        self.addCleanup(setattr, hotjoin, 'join_tools', original)
        game, tickets, start = self.start_game(players=2, open_slots=(2,), present_ips=())
        relay = start['relay']
        running = self.master.games[game['game_id']].relay
        self.assertIn('--absent 2', ' '.join(relays.relay_command(running.settings, 1, 'a' * 32, running.run_dir, '127.0.0.1',
                                                                  [HOST_IP], running.allow_file)))
        late = self.join(game, ip=OTHER_IP).json   # an open slot of the running game
        self.assertEqual(late['slot'], 2)
        self.assertIn(OTHER_IP, running.allowed)
        save = b'the join save'
        package = f'save={sha256(save)}=900'.encode()
        self.assertError(self.upload(game, 'j900.x4join', package, auth=game['host_ticket']), 409, 'not_asked')
        # The relay's log is a stand-in from here: the line the relay writes when the saver (slot 1) saved tick 900.
        stand_in = running.run_dir / 'stand-in-relay.log'
        running.dropin.relay_log = serve.RelayLog(stand_in)
        stand_in.write_text(f'NATIVE_GATE JOIN slot=2 saved slot=1 tick=900 bytes={len(save)} package={sha256(package)} '
                            f'portable={"f" * 32}\n')
        deadline = time.monotonic() + 10
        host_view = {}
        while time.monotonic() < deadline and 'send_join' not in host_view:
            host_view = self.lobby(game, game['host_ticket'], ip=HOST_IP).json
            time.sleep(0.2)
        self.assertEqual(host_view['send_join'], [{'tick': 900, 'slot': 2}])
        self.assertNotIn('send_join', self.lobby(game, late['ticket'], ip=OTHER_IP).json)
        self.assertError(self.upload(game, 'j900.x4join', package, auth=late['ticket'], ip=OTHER_IP), 403, 'forbidden')
        self.assertError(self.upload(game, 'j900.x4join', b'other', auth=game['host_ticket']), 400, 'bad_sha256')
        self.assertEqual(self.upload(game, 'j900.x4join', package, auth=game['host_ticket']).status, 200)
        self.assertEqual(self.upload(game, 'j900.xml.gz', save, auth=game['host_ticket']).status, 200)
        self.assertNotIn('send_join', self.lobby(game, game['host_ticket'], ip=HOST_IP).json)
        downloaded = self.call('GET', f'/api/v1/games/{game["game_id"]}/files/j900.xml.gz',
                               headers={'X-X4MP-Auth': late['ticket']}, ip=OTHER_IP)
        self.assertEqual((downloaded.status, downloaded.body), (200, save))
        with tempfile.TemporaryDirectory() as folder:   # the hot joiner's own way: the join file service on the relay port
            target = Path(folder) / 'j900.x4join'
            digest = joinfiles.fetch('127.0.0.1', relay['port'], relay['token'], 2, 'j900.x4join', target,
                                     expect_sha=sha256(package), retries=1, offer_wait_s=5)
            self.assertEqual(digest, sha256(package))
            self.assertEqual(target.read_bytes(), package)
        self.call('DELETE', f'/api/v1/games/{game["game_id"]}', {'secret': game['secret']}, ip=HOST_IP)
        self.assertFalse((self.data / 'games' / game['game_id']).exists())
        self.assertEqual(self.master.budget.used, 0)

    def saved_join(self, game, tick, package, save):
        """A drop-in game whose relay (a stand-in log) announced that slot 1 saved a join of slot 2 at `tick`."""
        running = self.master.games[game['game_id']].relay
        stand_in = running.run_dir / 'stand-in-relay.log'
        running.dropin.relay_log = serve.RelayLog(stand_in)
        stand_in.write_text(f'NATIVE_GATE JOIN slot=2 saved slot=1 tick={tick} bytes={len(save)} package={sha256(package)} '
                            f'portable={"f" * 32}\n')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if 'send_join' in self.lobby(game, game['host_ticket'], ip=HOST_IP).json:
                return running
            time.sleep(0.2)
        self.fail('the saver was not asked for the join files')

    def test_a_compressed_package_is_unpacked_checked_and_offered_first(self):
        import gzip
        original = hotjoin.join_tools
        hotjoin.join_tools = lambda: FakeJoin
        self.addCleanup(setattr, hotjoin, 'join_tools', original)
        game, tickets, start = self.start_game(players=2, open_slots=(2,), present_ips=())
        relay = start['relay']
        late = self.join(game, ip=OTHER_IP).json
        save = b'the join save'
        package = f'save={sha256(save)}=900'.encode() + bytes(4096)   # a package is mostly zero bytes
        self.saved_join(game, 900, package, save)
        auth = game['host_ticket']
        self.assertError(self.upload(game, 'j900.x4join.gz', b'not gzip at all', auth=auth), 400, 'bad_gzip')
        self.assertError(self.upload(game, 'j900.x4join.gz', gzip.compress(b'another package'), auth=auth), 400, 'bad_sha256')
        packed = gzip.compress(package)
        self.assertLess(len(packed), len(package))
        self.assertEqual(self.upload(game, 'j900.x4join.gz', packed, auth=auth).status, 200)
        self.assertIn('send_join', self.lobby(game, auth, ip=HOST_IP).json)   # the save is still to come
        self.assertEqual(self.upload(game, 'j900.xml.gz', save, auth=auth).status, 200)
        self.assertNotIn('send_join', self.lobby(game, auth, ip=HOST_IP).json)
        with tempfile.TemporaryDirectory() as folder:   # hotjoin.fetch_files' way: the compressed copy when offered
            target = Path(folder) / 'j900.x4join'
            how = joinfiles.fetch_package('127.0.0.1', relay['port'], relay['token'], late['slot'], target.name, target,
                                          sha256(package), offer_wait_s=5, log=lambda line: None)
            self.assertEqual(how, 'gz')
            self.assertEqual(target.read_bytes(), package)
        stored = self.master.games[game['game_id']].join_sizes
        self.assertEqual(stored['j900.x4join'].size, len(package))   # the unpacked package counts like an upload
        self.call('DELETE', f'/api/v1/games/{game["game_id"]}', {'secret': game['secret']}, ip=HOST_IP)
        self.assertEqual(self.master.budget.used, 0)

    def test_a_drop_in_slot_whose_join_never_ran_is_free_again(self):
        game, tickets, start = self.start_game(players=3, open_slots=(3,), present_ips=(PLAYER_IP,))
        first = self.join(game, ip=OTHER_IP).json
        self.assertEqual(first['slot'], 3)
        self.call('POST', f'/api/v1/games/{game["game_id"]}/leave', {'ticket': first['ticket']}, ip=OTHER_IP)
        second = self.join(game, ip='192.0.2.50').json   # nobody played slot 3: the next player gets it
        self.assertEqual(second.get('slot'), 3, second)
        running = self.master.games[game['game_id']].relay
        stand_in = running.run_dir / 'stand-in-relay.log'
        running.dropin.relay_log = serve.RelayLog(stand_in)
        stand_in.write_text('NATIVE_GATE JOIN slot=3 requested\n')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not running.dropin.played(3):
            time.sleep(0.2)
        self.assertTrue(running.dropin.played(3))
        self.call('POST', f'/api/v1/games/{game["game_id"]}/leave', {'ticket': second['ticket']}, ip='192.0.2.50')
        self.assertError(self.join(game, ip='192.0.2.51'), 409, 'full')   # the relay took slot 3: it is gone


class RelayPoolTests(MasterCase):
    limits = MasterLimits(relays_max=1)

    def test_the_pool_has_a_cap(self):
        first = self.host_game(ip=HOST_IP)
        second = self.host_game(ip=OTHER_IP)
        for game, ip in ((first, PLAYER_IP), (second, '192.0.2.40')):
            self.upload(game, 'play-recipe.json', recipe(), ip=ip)
            joined = self.join(game, ip=ip).json
            self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': joined['ticket']}, ip=ip)
        self.assertEqual(self.call('POST', f'/api/v1/games/{first["game_id"]}/start', {'secret': first['secret']}).status, 200)
        answer = self.call('POST', f'/api/v1/games/{second["game_id"]}/start', {'secret': second['secret']})
        self.assertError(answer, 503, 'relays_full')


class IdleRelayTests(MasterCase):
    limits = MasterLimits(idle_end=2)

    def test_a_relay_without_a_step_ends_its_game(self):
        game = self.host_game()
        self.upload(game, 'play-recipe.json', recipe())
        joined = self.join(game).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': joined['ticket']}, ip=PLAYER_IP)
        self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']})
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and self.master.pool.running():
            time.sleep(0.3)
        self.assertEqual(self.master.pool.running(), 0)
        self.assertTrue(any('over (no step for 2 s)' in line for line in self.logs), self.logs)


if __name__ == '__main__':
    unittest.main()
