"""Packages 1 and 2 together (docs/NETPLAY.md): the real master (src/lockstep/master) on 127.0.0.1 and the real client
(src/lockstep/launcher/masterclient.py, nat.py). The packages were written apart against the contract; each has its own
tests against a fake of the other, so this one checks that the two halves agree on the wire: a host makes a game,
uploads its session, a joiner finds it by code and in the list, downloads and checks the files, both are ready, the host
starts it, and the master hands out the relay it started."""
import hashlib
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from master_support import RELEASE, MasterCase, recipe  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from lockstep.launcher import masterclient, nat  # noqa: E402


class HostAndJoinThroughTheRealMaster(MasterCase):
    hook = False   # the client IP is the real one (127.0.0.1)

    def test_a_game_from_registration_to_the_relay(self):
        client = masterclient.MasterClient(f'http://127.0.0.1:{self.port}', timeout=10)
        self.assertTrue(client.health().get('version'))
        echo = client.echo_address()
        self.assertEqual(echo, ('127.0.0.1', self.master.echo_port))
        seen = nat.public_endpoint(*echo, timeout=2.0)
        self.assertIsNotNone(seen)
        self.assertEqual(seen[0], '127.0.0.1')

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            save = tmp / 'session-save.xml.gz'
            save.write_bytes(b'not really a save' * 4096)
            session_json = tmp / 'session.json'
            session_json.write_text('{"format": "X4LAN_SESSION_1"}')
            recipe_json = tmp / 'play-recipe.json'
            recipe_json.write_bytes(recipe(players=2))
            sha = hashlib.sha256(save.read_bytes()).hexdigest()

            game = client.create_game('Integration', True, RELEASE, 2, sha, open_slots=(), want='relay')
            for key in ('game_id', 'code', 'secret', 'host_ticket'):
                self.assertTrue(game.get(key), key)
            for path in (save, session_json, recipe_json):
                stored = client.upload(game['game_id'], game['secret'], path.name, path)
                self.assertEqual(stored['sha256'], hashlib.sha256(path.read_bytes()).hexdigest())

            listed = [g for g in client.games(RELEASE) if g['game_id'] == game['game_id']]
            self.assertEqual(len(listed), 1)
            self.assertTrue(listed[0]['compatible'])
            typed = game['code'].replace('-', '').lower()   # as a player might type it
            self.assertEqual(nat.normalize_code(typed), game['code'])
            self.assertEqual(client.code(nat.normalize_code(typed))['game_id'], game['game_id'])

            joined = client.join(game['game_id'], RELEASE, code=game['code'])
            self.assertEqual(joined['slot'], 2)
            for name, info in joined['files'].items():
                got = client.download(game['game_id'], joined['ticket'], name, tmp / 'dl' / name, info['sha256'], info['size'])
                self.assertEqual(got.read_bytes(), (tmp / name).read_bytes())

            client.heartbeat(game['game_id'], joined['ticket'], tick=0, state='lobby')
            client.ready(game['game_id'], joined['ticket'])
            client.start(game['game_id'], game['secret'])
            end = time.time() + 30
            start = None
            since = 0
            while time.time() < end and not start:
                lobby = client.lobby(game['game_id'], joined['ticket'], since=since, wait=5)
                since = lobby.get('stamp', since)
                start = lobby.get('start')
            self.assertIsNotNone(start, 'the start never reached the joiner')
            self.assertEqual(start['mode'], 'relay')
            relay = start['relay']
            self.assertTrue(relay['port'] and relay['token'])
            self.assertGreater(start['load_at'], time.time())

            client.leave(game['game_id'], joined['ticket'])
            client.delete(game['game_id'], game['secret'])
            with self.assertRaises(masterclient.MasterError) as gone:
                client.code(game['code'])
            self.assertEqual(gone.exception.status, 404)


if __name__ == '__main__':
    unittest.main()
