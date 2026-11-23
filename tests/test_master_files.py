"""A game's files on the master (PUT and GET /api/v1/games/{id}/files/{name}): the names, the caps, the SHA-256 check,
who may upload and download, Range, the data budget, and the deletion with the game."""
import http.client
import os
import time
import unittest

from master_support import HOST_IP, PLAYER_IP, MasterCase, MasterLimits, sha256

SAVE = os.urandom(300 * 1024)


class FileTests(MasterCase):
    def test_round_trip_with_range(self):
        game = self.host_game()
        stored = self.upload(game, 'session-save.xml.gz', SAVE)
        self.assertEqual(stored.status, 200, stored.body)
        self.assertEqual(stored.json, {'ok': True, 'name': 'session-save.xml.gz', 'size': len(SAVE), 'sha256': sha256(SAVE)})
        joined = self.join(game).json
        self.assertEqual(joined['files'], {'session-save.xml.gz': {'size': len(SAVE), 'sha256': sha256(SAVE)}})
        path = f'/api/v1/games/{game["game_id"]}/files/session-save.xml.gz'
        auth = {'X-X4MP-Auth': joined['ticket']}
        whole = self.call('GET', path, headers=auth, ip=PLAYER_IP)
        self.assertEqual(whole.status, 200)
        self.assertEqual(whole.body, SAVE)
        self.assertEqual(whole.headers['Accept-Ranges'], 'bytes')
        self.assertEqual(whole.headers['X-X4MP-Sha256'], sha256(SAVE))
        resumed = self.call('GET', path, headers={**auth, 'Range': 'bytes=1000-'}, ip=PLAYER_IP)
        self.assertEqual(resumed.status, 206)
        self.assertEqual(resumed.body, SAVE[1000:])
        self.assertEqual(resumed.headers['Content-Range'], f'bytes 1000-{len(SAVE) - 1}/{len(SAVE)}')
        middle = self.call('GET', path, headers={**auth, 'Range': 'bytes=10-19'})
        self.assertEqual((middle.status, middle.body), (206, SAVE[10:20]))
        tail = self.call('GET', path, headers={**auth, 'Range': 'bytes=-5'})
        self.assertEqual((tail.status, tail.body), (206, SAVE[-5:]))
        self.assertError(self.call('GET', path, headers={**auth, 'Range': f'bytes={len(SAVE)}-'}), 416, 'bad_range')
        by_host = self.call('GET', path, headers={'X-X4MP-Auth': game['host_ticket']})
        self.assertEqual(by_host.body, SAVE)

    def test_downloads_need_a_ticket_of_the_game(self):
        game = self.host_game()
        other = self.host_game(ip='203.0.113.99')
        self.upload(game, 'session.json', b'{}')
        path = f'/api/v1/games/{game["game_id"]}/files/session.json'
        self.assertError(self.call('GET', path), 403, 'forbidden')
        self.assertError(self.call('GET', path, headers={'X-X4MP-Auth': other['host_ticket']}), 403, 'forbidden')
        missing = f'/api/v1/games/{game["game_id"]}/files/play-recipe.json'
        self.assertError(self.call('GET', missing, headers={'X-X4MP-Auth': game['host_ticket']}), 404, 'not_found')

    def test_uploads_are_the_hosts_and_checked(self):
        game = self.host_game()
        joined = self.join(game).json
        self.assertError(self.upload(game, 'session.json', b'{}', auth=joined['ticket']), 403, 'forbidden')
        self.assertError(self.upload(game, 'session.json', b'{}', sha='0' * 64), 400, 'bad_sha256')
        self.assertError(self.upload(game, 'session.json', b'{}', sha='nope'), 400, 'bad_sha256')
        self.assertError(self.upload(game, 'overlay.dll', b'MZ'), 400, 'bad_name')
        self.assertError(self.upload(game, '..%2Fsession.json', b'{}'), 400, 'bad_name')
        self.assertError(self.upload(game, 'j100.x4join', b'x'), 409, 'state')   # no drop-in relay runs
        self.assertError(self.upload({'game_id': '0123456789abcdef', 'secret': game['secret']}, 'session.json', b'{}'),
                         404, 'not_found')
        files = list((self.data / 'games' / game['game_id']).rglob('*'))
        self.assertFalse([path for path in files if path.is_file()])

    def test_caps_per_name_are_checked_before_the_body_is_read(self):
        game = self.host_game()
        for name, size in (('session.json', (1 << 20) + 1), ('play-recipe.json', (1 << 20) + 1),
                           ('session-save.xml.gz', (64 << 20) + 1)):
            connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=20)
            try:
                connection.putrequest('PUT', f'/api/v1/games/{game["game_id"]}/files/{name}')
                connection.putheader('Content-Length', str(size))
                connection.putheader('X-X4MP-Auth', game['secret'])
                connection.putheader('X-X4MP-Sha256', 'a' * 64)
                connection.endheaders()
                connection.send(b'x' * 1024)   # a client that would send it all; the answer comes first
                response = connection.getresponse()
                self.assertEqual(response.status, 413, name)
                self.assertIn(b'too_large', response.read())
            finally:
                connection.close()

    def test_a_short_body_is_not_kept(self):
        game = self.host_game()
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=40)
        try:
            connection.putrequest('PUT', f'/api/v1/games/{game["game_id"]}/files/session.json')
            connection.putheader('Content-Length', '100')
            connection.putheader('X-X4MP-Auth', game['secret'])
            connection.putheader('X-X4MP-Sha256', 'a' * 64)
            connection.endheaders()
            connection.send(b'{}')
            connection.sock.shutdown(1)   # the client stops sending
            response = connection.getresponse()
            self.assertEqual(response.status, 400)
            self.assertIn(b'short_body', response.read())
        finally:
            connection.close()
        joined = self.join(game).json
        self.assertEqual(joined['files'], {})

    def test_session_files_are_fixed_once_the_game_started(self):
        from master_support import recipe
        game = self.host_game()
        self.upload(game, 'play-recipe.json', recipe())
        joined = self.join(game).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': joined['ticket']}, ip=PLAYER_IP)
        self.assertEqual(self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']},
                                   ip=HOST_IP).status, 200)
        self.assertError(self.upload(game, 'session.json', b'{}'), 409, 'state')

    def test_a_replaced_file_and_the_deletion_with_the_game(self):
        game = self.host_game()
        self.upload(game, 'session.json', b'{"v": 1}')
        self.upload(game, 'session.json', b'{"v": 2}')
        joined = self.join(game).json
        self.assertEqual(joined['files']['session.json']['sha256'], sha256(b'{"v": 2}'))
        stored = [path for path in (self.data / 'games' / game['game_id']).rglob('*') if path.is_file()]
        self.assertEqual(len(stored), 1)
        self.call('DELETE', f'/api/v1/games/{game["game_id"]}', {'secret': game['secret']})
        self.assertFalse((self.data / 'games' / game['game_id']).exists())
        self.assertEqual(self.master.budget.used, 0)


class BudgetTests(MasterCase):
    limits = MasterLimits(data_bytes_max=400 * 1024)

    def test_every_file_together_has_a_budget(self):
        game = self.host_game()
        self.assertEqual(self.upload(game, 'session-save.xml.gz', SAVE).status, 200)
        answer = self.upload(game, 'session.json', b'{' + b' ' * (200 * 1024) + b'}')
        self.assertError(answer, 503, 'server_full')
        self.call('DELETE', f'/api/v1/games/{game["game_id"]}', {'secret': game['secret']})
        deadline = time.monotonic() + 3
        while self.master.budget.used and time.monotonic() < deadline:
            time.sleep(0.1)
        again = self.host_game()
        self.assertEqual(self.upload(again, 'session-save.xml.gz', SAVE).status, 200)


if __name__ == '__main__':
    unittest.main()
