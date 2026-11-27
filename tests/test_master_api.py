"""The master's HTTP API v1 (docs/NETPLAY.md): every endpoint, its answers and its errors, against a real server on
127.0.0.1 (tests/master_support.py). The public IPs of the players are simulated with the X-Test-Client-IP hook."""
import contextlib
import io
import socket
import threading
import time
import unittest

from master_support import (HOST_IP, OTHER_IP, PLAYER_IP, RELEASE, MasterCase, MasterLimits)


class HealthAndListTests(MasterCase):
    def test_health(self):
        answer = self.call('GET', '/api/v1/health')
        self.assertEqual(answer.status, 200)
        payload = answer.json
        self.assertTrue(payload['ok'])
        self.assertEqual((payload['version'], payload['games'], payload['relays']), (1, 0, 0))
        self.assertIsInstance(payload['uptime_s'], int)
        self.assertEqual(payload['echo_port'], self.master.echo_port)
        self.assertEqual(answer.headers['Content-Type'], 'application/json; charset=utf-8')
        self.assertEqual(answer.headers['Cache-Control'], 'no-store')

    def test_the_list_has_public_games_only_newest_first_with_compatibility(self):
        first = self.host_game(name='First', ip='203.0.113.1')
        self.host_game(name='Hidden', public=False, ip='203.0.113.2')
        time.sleep(0.05)   # the list is ordered by the registration's wall time
        second = self.host_game(name='Second', release='ffffffffffff-111111111111', ip='203.0.113.3')
        listed = self.call('GET', f'/api/v1/games?release={RELEASE}').json['games']
        self.assertEqual([game['game_id'] for game in listed], [second['game_id'], first['game_id']])
        self.assertEqual([game['compatible'] for game in listed], [False, True])
        row = listed[1]
        self.assertEqual(set(row), {'game_id', 'name', 'release', 'compatible', 'players', 'players_max', 'state', 'mode',
                                    'open_slots_free', 'age_s'})
        self.assertEqual((row['players'], row['players_max'], row['state'], row['mode'], row['open_slots_free']),
                         (1, 2, 'lobby', None, 1))
        self.assertNotIn('secret', str(listed))
        self.assertNotIn(first['code'], str(listed))

    def test_a_games_start_is_in_the_list(self):
        self.host_game(name='Cadets', gamestart='x4ep1_gamestart_terran1', ip='203.0.113.1')
        listed = self.call('GET', f'/api/v1/games?release={RELEASE}').json['games']
        self.assertEqual((listed[0]['name'], listed[0]['gamestart']), ('Cadets', 'x4ep1_gamestart_terran1'))
        self.assertTrue(any('start x4ep1_gamestart_terran1' in line for line in self.logs))
        for number, bad in enumerate(('Terran Cadet', 'x' * 65, 7, '../x')):
            body = {'name': 'Game', 'public': True, 'release': RELEASE, 'players_max': 2, 'session_sha256': 'c' * 64,
                    'gamestart': bad}
            self.assertError(self.call('POST', '/api/v1/games', body, ip=f'192.0.2.{100 + number}'), 400, 'bad_request')

    def test_a_bad_release_in_the_list_query(self):
        self.assertError(self.call('GET', '/api/v1/games?release=%3Cscript%3E'), 400, 'bad_request')

    def test_unknown_paths_and_wrong_methods(self):
        self.assertError(self.call('GET', '/api/v2/health'), 404, 'not_found')
        self.assertError(self.call('POST', '/api/v1/health', {}), 400, 'bad_method')
        self.assertError(self.call('GET', '/api/v1/games/0123456789abcdef/join'), 400, 'bad_method')


class CreateTests(MasterCase):
    def test_a_new_game(self):
        game = self.host_game(players_max=4, open_slots=[3, 4])
        self.assertRegex(game['game_id'], r'^[0-9a-f]{16}$')
        self.assertRegex(game['code'], r'^[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}$')
        self.assertRegex(game['secret'], r'^[0-9a-f]{32}$')
        self.assertRegex(game['host_ticket'], r'^[0-9a-f]{32}$')
        self.assertNotEqual(game['secret'], game['host_ticket'])
        self.assertEqual(game['slot'], 1)
        joined = '\n'.join(self.logs)
        self.assertNotIn(game['secret'], joined)
        self.assertNotIn(game['host_ticket'], joined)
        self.assertNotIn(game['code'], joined)

    def test_every_field_is_checked(self):
        good = {'name': 'Game', 'public': True, 'release': RELEASE, 'players_max': 2, 'session_sha256': 'c' * 64,
                'open_slots': [], 'want': 'auto', 'direct': None, 'lan': None}
        bad = [('name', ''), ('name', 'x' * 41), ('name', 'tab\there'), ('name', 7), ('public', 'yes'), ('release', ''),
               ('release', 'Has Spaces'), ('players_max', 1), ('players_max', 9), ('players_max', True),
               ('session_sha256', 'C' * 64), ('open_slots', [1]), ('open_slots', [3]), ('open_slots', [2, 2]),
               ('want', 'peer'), ('direct', {'udp_port': 80, 'tcp_port': 47001}), ('direct', {'udp_port': 47000}),
               ('lan', {'candidates': ['127.0.0.1'], 'udp_port': 47000, 'tcp_port': 47001}),
               ('lan', {'candidates': [], 'udp_port': 47000, 'tcp_port': 47001}),
               ('lan', {'candidates': ['10.0.0.300'], 'udp_port': 47000, 'tcp_port': 47001})]
        bad.append(('want', 'direct'))   # without direct ports
        for number, (key, value) in enumerate(bad):
            body = dict(good, **{key: value})
            ip = f'192.0.2.{number + 1}'   # new games are rate limited per IP, failed ones too
            self.assertError(self.call('POST', '/api/v1/games', body, ip=ip), 400, 'bad_request')

    def test_bodies_that_are_not_json_objects(self):
        headers = {'Content-Type': 'application/json'}
        self.assertError(self.call('POST', '/api/v1/games', raw=b'{nope', headers=headers), 400, 'bad_json')
        self.assertError(self.call('POST', '/api/v1/games', raw=b'[1, 2]', headers=headers), 400, 'bad_json')
        self.assertError(self.call('POST', '/api/v1/games', raw=b'[' * 30000 + b']' * 30000, headers=headers), 400,
                         'bad_json')

    def test_a_request_over_64_kib(self):
        body = {'name': 'x', 'padding': 'p' * (65 << 10)}
        self.assertError(self.call('POST', '/api/v1/games', body), 413, 'too_large')


class CapTests(MasterCase):
    limits = MasterLimits(games_max=3, games_per_ip=2)

    def test_games_per_ip_and_games_in_all(self):
        self.host_game(ip='203.0.113.1')
        self.host_game(ip='203.0.113.1')
        body = {'name': 'Third', 'public': True, 'release': RELEASE, 'players_max': 2, 'session_sha256': 'c' * 64}
        self.assertError(self.call('POST', '/api/v1/games', body, ip='203.0.113.1'), 429, 'too_many_games')
        self.host_game(ip='203.0.113.2')
        answer = self.call('POST', '/api/v1/games', body, ip='203.0.113.3')
        self.assertError(answer, 503, 'server_full')
        self.assertIn('Retry-After', answer.headers)


class RateTests(MasterCase):
    limits = MasterLimits(requests_per_s=0.001, requests_burst=3)

    def test_requests_per_ip_are_rate_limited(self):
        for _ in range(3):
            self.assertEqual(self.call('GET', '/api/v1/health', ip=OTHER_IP).status, 200)
        self.assertError(self.call('GET', '/api/v1/health', ip=OTHER_IP), 429, 'rate_limited')
        self.assertEqual(self.call('GET', '/api/v1/health', ip=PLAYER_IP).status, 200)   # another address


class CodeLookupTests(MasterCase):
    limits = MasterLimits(codes_per_minute=4)

    def test_a_code_finds_public_and_private_games_however_it_is_typed(self):
        game = self.host_game(public=False)
        for typed in (game['code'], game['code'].lower(), game['code'].replace('-', '')):
            answer = self.call('GET', f'/api/v1/codes/{typed}', ip=PLAYER_IP)
            self.assertEqual(answer.status, 200, answer.body)
            self.assertEqual(answer.json['game_id'], game['game_id'])
            self.assertFalse(answer.json['public'])
        self.assertError(self.call('GET', '/api/v1/codes/NOPE', ip=PLAYER_IP), 400, 'bad_code')
        self.assertError(self.call('GET', '/api/v1/codes/rate', ip=PLAYER_IP), 429, 'rate_limited')

    def test_an_unknown_code(self):
        self.assertError(self.call('GET', '/api/v1/codes/2222-2222', ip=PLAYER_IP), 404, 'not_found')


class LanTests(MasterCase):
    def test_lan_candidates_only_for_askers_with_the_hosts_public_ip(self):
        lan = {'candidates': ['10.20.0.5', '192.168.1.5'], 'udp_port': 47000, 'tcp_port': 47001}
        game = self.host_game(lan=lan)
        same_site = self.call('GET', f'/api/v1/codes/{game["code"]}', ip=HOST_IP).json
        self.assertEqual(same_site['lan'], lan)
        elsewhere = self.call('GET', f'/api/v1/codes/{game["code"]}', ip=PLAYER_IP).json
        self.assertNotIn('lan', elsewhere)
        listed = self.call('GET', '/api/v1/games', ip=HOST_IP).json['games']
        self.assertNotIn('10.20.0.5', str(listed))


class JoinTests(MasterCase):
    def test_a_join_gets_a_ticket_a_slot_and_the_files(self):
        game = self.host_game(players_max=3)
        self.assertEqual(self.upload(game, 'session.json', b'{"x": 1}').status, 200)
        answer = self.join(game)
        self.assertEqual(answer.status, 200, answer.body)
        payload = answer.json
        self.assertRegex(payload['ticket'], r'^[0-9a-f]{32}$')
        self.assertEqual(payload['slot'], 2)
        self.assertEqual(payload['files']['session.json']['size'], 8)
        self.assertEqual(self.join(game, ip=OTHER_IP).json['slot'], 3)
        self.assertError(self.join(game, ip='192.0.2.99'), 409, 'full')

    def test_another_release_cannot_join(self):
        game = self.host_game()
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/join', {'release': 'ffffffffffff-111111111111'})
        self.assertError(answer, 409, 'incompatible')
        self.assertError(self.call('POST', f'/api/v1/games/{game["game_id"]}/join', {}), 400, 'bad_request')

    def test_a_private_game_needs_its_code(self):
        game = self.host_game(public=False)
        self.assertError(self.join(game), 403, 'forbidden')
        self.assertError(self.join(game, code='2222-2222'), 403, 'forbidden')
        self.assertEqual(self.join(game, code=game['code'].lower()).status, 200)

    def test_an_unknown_game(self):
        self.assertError(self.join({'game_id': '0123456789abcdef'}), 404, 'not_found')
        self.assertError(self.join({'game_id': 'not-an-id'}), 404, 'not_found')


class LobbyTests(MasterCase):
    def test_the_lobby_answer_and_wrong_tickets(self):
        game = self.host_game()
        answer = self.lobby(game, game['host_ticket'])
        self.assertEqual(answer.status, 200)
        payload = answer.json
        self.assertEqual(payload['state'], 'lobby')
        self.assertEqual(payload['players'], [{'slot': 1, 'ready': False, 'host': True}])
        self.assertIsNone(payload['start'])
        self.assertIsInstance(payload['stamp'], int)
        self.assertError(self.lobby(game, 'f' * 32), 403, 'forbidden')
        self.assertError(self.lobby(game, 'short'), 403, 'forbidden')
        headers = {'X-X4MP-Auth': game['host_ticket']}
        by_header = self.call('GET', f'/api/v1/games/{game["game_id"]}/lobby?since=0&wait=0', headers=headers)
        self.assertEqual(by_header.status, 200)
        self.assertError(self.call('GET', f'/api/v1/games/{game["game_id"]}/lobby?ticket={game["host_ticket"]}&since=x'),
                         400, 'bad_request')

    def test_a_long_poll_wakes_up_when_a_player_joins(self):
        game = self.host_game()
        stamp = self.lobby(game, game['host_ticket']).json['stamp']
        result = {}

        def poll():
            started = time.monotonic()
            result['answer'] = self.lobby(game, game['host_ticket'], since=stamp, wait=20)
            result['seconds'] = time.monotonic() - started
        waiter = threading.Thread(target=poll)
        waiter.start()
        time.sleep(0.5)
        self.assertEqual(self.join(game).status, 200)
        waiter.join(10)
        self.assertLess(result['seconds'], 5)
        payload = result['answer'].json
        self.assertGreater(payload['stamp'], stamp)
        self.assertEqual([row['slot'] for row in payload['players']], [1, 2])

    def test_a_long_poll_without_news_returns_after_its_wait(self):
        game = self.host_game()
        stamp = self.lobby(game, game['host_ticket']).json['stamp']
        started = time.monotonic()
        payload = self.lobby(game, game['host_ticket'], since=stamp, wait=1).json
        self.assertGreaterEqual(time.monotonic() - started, 0.9)
        self.assertEqual(payload['stamp'], stamp)


class ReadyStartTests(MasterCase):
    def test_ready_then_start_in_relay_mode(self):
        # Slot 4 is open (joined while the game runs): the host starts once slots 1 to 3 are ready.
        game = self.host_game(players_max=4, open_slots=[4])
        from master_support import recipe
        self.assertEqual(self.upload(game, 'play-recipe.json', recipe(players=4, open_slots=[4])).status, 200)
        second = self.join(game).json
        third = self.join(game, ip=OTHER_IP).json
        start = f'/api/v1/games/{game["game_id"]}/start'
        ready = f'/api/v1/games/{game["game_id"]}/ready'
        self.assertError(self.call('POST', start, {'secret': 'f' * 32}), 403, 'forbidden')
        self.assertError(self.call('POST', start, {'secret': game['secret']}, ip=HOST_IP), 409, 'not_ready')
        self.assertError(self.call('POST', ready, {'ticket': 'f' * 32}), 403, 'forbidden')
        self.assertError(self.call('POST', ready, {'ticket': second['ticket'], 'ready': 'yes'}), 400, 'bad_request')
        self.assertEqual(self.call('POST', ready, {'ticket': second['ticket'], 'ready': True}, ip=PLAYER_IP).json, {'ok': True})
        self.assertError(self.call('POST', start, {'secret': game['secret']}, ip=HOST_IP), 409, 'not_ready')
        self.assertEqual(self.call('POST', ready, {'ticket': third['ticket']}, ip=OTHER_IP).status, 200)
        answer = self.call('POST', start, {'secret': game['secret']}, ip=HOST_IP)
        self.assertEqual(answer.status, 200, answer.body)
        self.assertEqual(answer.json['mode'], 'relay')
        self.assertError(self.call('POST', start, {'secret': game['secret']}, ip=HOST_IP), 409, 'state')
        payload = self.lobby(game, third['ticket'], ip=OTHER_IP).json
        self.assertEqual(payload['state'], 'started')
        self.assertEqual(payload['start']['mode'], 'relay')
        self.assertEqual(set(payload['start']['relay']), {'host', 'port', 'token'})
        self.assertRegex(payload['start']['relay']['token'], r'^[0-9a-f]{32}$')
        self.assertGreaterEqual(payload['start']['load_at'], time.time() + 25)   # 30 s after the last ready player
        self.assertNotIn('ip', str(payload['players']))
        self.assertEqual(self.join(game, ip='192.0.2.98').json['slot'], 4)   # the open slot, while the game runs
        self.assertError(self.join(game, ip='192.0.2.99'), 409, 'full')

    def test_a_started_game_without_open_slots_takes_nobody(self):
        game = self.host_game(want='relay')
        from master_support import recipe
        self.upload(game, 'play-recipe.json', recipe())
        second = self.join(game).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/leave', {'ticket': second['ticket']})
        third = self.join(game, ip=OTHER_IP).json
        self.assertEqual(third['slot'], 2)   # the slot a player left before the start is free again
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': third['ticket']}, ip=OTHER_IP)
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']}, ip=HOST_IP)
        self.assertEqual(answer.status, 200, answer.body)
        self.assertError(self.join(game, ip='192.0.2.99'), 409, 'started')

    def test_every_slot_ready_starts_the_game_without_the_click(self):
        game = self.host_game()
        from master_support import recipe
        self.upload(game, 'play-recipe.json', recipe())
        second = self.join(game).json
        ready = f'/api/v1/games/{game["game_id"]}/ready'
        self.call('POST', ready, {'ticket': game['host_ticket']}, ip=HOST_IP)
        self.call('POST', ready, {'ticket': second['ticket']}, ip=PLAYER_IP)
        deadline = time.monotonic() + 15
        state = None
        while time.monotonic() < deadline:
            state = self.lobby(game, second['ticket'], ip=PLAYER_IP).json['state']
            if state == 'started':
                break
            time.sleep(0.2)
        self.assertEqual(state, 'started')

    def test_relay_mode_without_a_recipe(self):
        game = self.host_game()
        second = self.join(game).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': second['ticket']}, ip=PLAYER_IP)
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']}, ip=HOST_IP)
        self.assertError(answer, 409, 'no_recipe')
        self.assertEqual(self.lobby(game, second['ticket']).json['state'], 'lobby')


class HeartbeatLeaveDeleteTests(MasterCase):
    def test_heartbeat_with_secret_or_ticket(self):
        game = self.host_game()
        path = f'/api/v1/games/{game["game_id"]}/heartbeat'
        self.assertEqual(self.call('POST', path, {'secret': game['secret'], 'tick': 0, 'state': 'lobby'}).json, {'ok': True})
        self.assertEqual(self.call('POST', path, {'ticket': game['host_ticket'], 'tick': 5, 'state': 'session'}).status, 200)
        self.assertError(self.call('POST', path, {'ticket': 'f' * 32, 'tick': 0, 'state': 'lobby'}), 403, 'forbidden')
        self.assertError(self.call('POST', path, {'secret': game['secret'], 'tick': -1}), 400, 'bad_request')
        self.assertError(self.call('POST', path, {'secret': game['secret'], 'state': 'Loading!'}), 400, 'bad_request')

    def test_leave_frees_the_slot_before_the_start(self):
        game = self.host_game()
        second = self.join(game).json
        path = f'/api/v1/games/{game["game_id"]}/leave'
        self.assertError(self.call('POST', path, {'ticket': 'f' * 32}), 403, 'forbidden')
        self.assertEqual(self.call('POST', path, {'ticket': second['ticket']}).json, {'ok': True})
        self.assertError(self.lobby(game, second['ticket']), 403, 'forbidden')
        self.assertEqual(self.join(game, ip=OTHER_IP).json['slot'], 2)

    def test_the_host_leaving_the_lobby_ends_the_game(self):
        game = self.host_game()
        self.call('POST', f'/api/v1/games/{game["game_id"]}/leave', {'ticket': game['host_ticket']})
        self.assertError(self.call('GET', f'/api/v1/codes/{game["code"]}'), 404, 'not_found')

    def test_delete_ends_the_game_and_frees_its_code(self):
        game = self.host_game()
        self.upload(game, 'session.json', b'{}')
        path = f'/api/v1/games/{game["game_id"]}'
        self.assertError(self.call('DELETE', path, {'secret': game['host_ticket']}), 403, 'forbidden')
        self.assertEqual(self.call('DELETE', path, {'secret': game['secret']}).json, {'ok': True})
        self.assertError(self.call('GET', f'/api/v1/codes/{game["code"]}'), 404, 'not_found')
        self.assertError(self.join(game), 404, 'not_found')
        self.assertEqual(self.lobby(game, game['host_ticket']).json['state'], 'ended')   # its polls learn why
        self.assertFalse((self.data / 'games' / game['game_id']).exists())
        self.assertEqual(self.call('GET', '/api/v1/health').json['games'], 0)


class HeaderAuthTests(MasterCase):
    """The package 2 client sends the secret or ticket in X-X4MP-Auth only; it wins over a body's."""
    def test_every_authenticated_call_takes_the_header(self):
        from master_support import recipe
        game = self.host_game()
        base = f'/api/v1/games/{game["game_id"]}'
        host = {'X-X4MP-Auth': game['secret']}
        self.assertEqual(self.call('POST', base + '/heartbeat', {'tick': 3, 'state': 'lobby'}, headers=host).status, 200)
        self.upload(game, 'play-recipe.json', recipe())
        second = self.join(game).json
        player = {'X-X4MP-Auth': second['ticket']}
        self.assertEqual(self.call('POST', base + '/heartbeat', {'tick': 0, 'state': 'lobby'}, headers=player).status, 200)
        self.assertEqual(self.call('POST', base + '/ready', {'ready': True}, headers=player, ip=PLAYER_IP).status, 200)
        lobby = self.call('GET', base + '/lobby?since=0&wait=0', headers=player)
        self.assertEqual(lobby.json['slot'], 2)
        self.assertEqual(self.call('POST', base + '/start', {}, headers=host, ip=HOST_IP).status, 200)
        self.assertEqual(self.call('POST', base + '/leave', {}, headers=player).status, 200)
        self.assertEqual(self.call('DELETE', base, None, headers=host).status, 200)

    def test_the_header_wins_over_the_body(self):
        game = self.host_game()
        second = self.join(game).json
        base = f'/api/v1/games/{game["game_id"]}'
        wrong = {'X-X4MP-Auth': 'f' * 32}
        self.assertError(self.call('POST', base + '/heartbeat', {'secret': game['secret']}, headers=wrong), 403, 'forbidden')
        answer = self.call('GET', base + f'/lobby?ticket={game["host_ticket"]}&since=0&wait=0',
                           headers={'X-X4MP-Auth': second['ticket']})
        self.assertEqual(answer.json['slot'], 2)


class ExpiryTests(MasterCase):
    limits = MasterLimits(heartbeat_s=1.5, player_idle_s=60, ended_keep_s=0.5)

    def test_a_game_without_heartbeats_is_gone(self):
        game = self.host_game()
        kept = self.host_game(ip=OTHER_IP)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            self.call('POST', f'/api/v1/games/{kept["game_id"]}/heartbeat', {'secret': kept['secret']}, ip=OTHER_IP)
            if self.call('GET', f'/api/v1/codes/{game["code"]}').status == 404:
                break
            time.sleep(0.3)
        self.assertError(self.call('GET', f'/api/v1/codes/{game["code"]}'), 404, 'not_found')
        self.assertEqual(self.call('GET', f'/api/v1/codes/{kept["code"]}').status, 200)
        self.assertTrue(any('no heartbeat' in line for line in self.logs))


class IdlePlayerTests(MasterCase):
    limits = MasterLimits(player_idle_s=1.5)

    def test_a_joined_player_who_sends_nothing_leaves_the_lobby(self):
        game = self.host_game()
        self.join(game)
        deadline = time.monotonic() + 8
        players = None
        while time.monotonic() < deadline:
            players = self.lobby(game, game['host_ticket'], ip=HOST_IP).json['players']
            if len(players) == 1:
                break
            time.sleep(0.3)
        self.assertEqual([row['slot'] for row in players], [1])


class ConnectionCapTests(MasterCase):
    limits = MasterLimits(connections_per_ip=1)

    def test_connections_over_the_cap_are_answered_busy(self):
        idle = socket.create_connection(('127.0.0.1', self.port))
        try:
            time.sleep(0.3)
            self.assertError(self.call('GET', '/api/v1/health'), 503, 'server_full')
        finally:
            idle.close()
        deadline = time.monotonic() + 5
        status = None
        while time.monotonic() < deadline:
            status = self.call('GET', '/api/v1/health').status
            if status == 200:
                break
            time.sleep(0.2)
        self.assertEqual(status, 200)


class ProxyTests(MasterCase):
    hook = False
    options = {'trust_proxy': True, 'public_host': '127.0.0.1', 'http_bind': '127.0.0.1'}

    def test_behind_a_trusted_proxy_the_last_forwarded_address_is_the_client(self):
        lan = {'candidates': ['10.20.0.5'], 'udp_port': 47000, 'tcp_port': 47001}
        body = {'name': 'Proxied', 'public': True, 'release': RELEASE, 'players_max': 2, 'session_sha256': 'c' * 64,
                'lan': lan}
        forwarded = {'X-Forwarded-For': '192.0.2.250, 203.0.113.77'}   # the first one is the client's own claim
        game = self.call('POST', '/api/v1/games', body, headers=forwarded).json
        self.assertTrue(any('created by 203.0.113.77' in line for line in self.logs), self.logs)
        same = self.call('GET', f'/api/v1/codes/{game["code"]}', headers={'X-Forwarded-For': '203.0.113.77'}).json
        self.assertIn('lan', same)
        claimed = self.call('GET', f'/api/v1/codes/{game["code"]}', headers={'X-Forwarded-For': '203.0.113.77, 192.0.2.1'})
        self.assertNotIn('lan', claimed.json)
        self.assertError(self.call('GET', '/api/v1/health', headers={'X-Forwarded-For': 'not-an-ip'}), 400, 'bad_proxy')


class NoProxyTests(MasterCase):
    hook = False

    def test_without_trust_proxy_a_forwarded_header_is_ignored(self):
        body = {'name': 'Direct', 'public': True, 'release': RELEASE, 'players_max': 2, 'session_sha256': 'c' * 64}
        self.call('POST', '/api/v1/games', body, headers={'X-Forwarded-For': '203.0.113.77'})
        self.assertTrue(any('created by 127.0.0.1' in line for line in self.logs), self.logs)


class OptionTests(unittest.TestCase):
    def test_a_proxy_setup_needs_the_public_udp_address(self):
        from master_support import server
        with self.assertRaises(SystemExit):
            server.check_options(server.MasterOptions(trust_proxy=True))
        with self.assertRaises(SystemExit):
            server.check_options(server.MasterOptions(http_bind='127.0.0.1', bind='0.0.0.0'))
        with self.assertRaises(SystemExit):
            server.check_options(server.MasterOptions(public_host='master.example.org'))
        with self.assertRaises(SystemExit):
            server.check_options(server.MasterOptions(public_prefix='/x4mp/"><script>'))
        with self.assertRaises(SystemExit):
            server.check_options(server.MasterOptions(relay_ports=(47263, 47200)))
        server.check_options(server.MasterOptions(trust_proxy=True, http_bind='127.0.0.1', public_host='203.0.113.10',
                                                  public_prefix='/x4mp'))

    def test_the_master_subcommand(self):
        from unittest import mock
        from lockstep.launcher import __main__ as cli
        from lockstep.master import server
        with mock.patch.object(server, 'run', return_value=0) as run:
            self.assertEqual(cli.main(['master', '--data', 'somewhere', '--relay-ports', '47200-47231', '--trust-proxy',
                                       '--http-bind', '127.0.0.1', '--public-host', '203.0.113.10',
                                       '--public-prefix', '/x4mp', '--relay-release', RELEASE]), 0)
        options = run.call_args[0][0]
        self.assertEqual((options.bind, options.http_bind, options.http_port, options.echo_port),
                         ('0.0.0.0', '127.0.0.1', 47100, 47101))
        self.assertEqual(options.relay_ports, (47200, 47231))
        self.assertTrue(options.trust_proxy)
        self.assertEqual(options.relay_releases, (RELEASE,))
        self.assertEqual(options.limits.games_max, 200)
        with self.assertRaises(SystemExit):
            cli.main(['master', '--data', 'somewhere', '--relay-ports', '47200'])
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            cli.main(['master'])   # --data is required


if __name__ == '__main__':
    unittest.main()
