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

    def test_a_drop_in_join_from_the_saver_through_the_master_to_the_joiner(self):
        """The host plays alone with one drop-in slot; a joiner takes it while the game runs; the relay (a stand-in log)
        has the host save the join; netdropin.JoinSender sends the files with the real client (the package compressed),
        the master unpacks and checks it, and the joiner fetches it from the relay's file service as hot_join does."""
        from lockstep.launcher import hotjoin, joinfiles, netdropin, netplay, serve
        from test_master_relay import FakeJoin
        original = hotjoin.join_tools
        hotjoin.join_tools = lambda: FakeJoin
        self.addCleanup(setattr, hotjoin, 'join_tools', original)
        client = masterclient.MasterClient(f'http://127.0.0.1:{self.port}', timeout=10)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            recipe_json = tmp / 'play-recipe.json'
            recipe_json.write_bytes(recipe(players=2, open_slots=(2,)))
            game = client.create_game('Drop-in', True, RELEASE, 2, 'a' * 64, open_slots=(2,), want='relay')
            client.upload(game['game_id'], game['secret'], recipe_json.name, recipe_json)
            client.start(game['game_id'], game['secret'])       # the host is the whole common load
            listed = [g for g in client.games(RELEASE) if g['game_id'] == game['game_id']][0]
            self.assertEqual((listed['state'], listed['open_slots_free']), ('started', 1))
            joined = client.join(game['game_id'], RELEASE, code=game['code'])
            self.assertEqual(joined['slot'], 2)
            start = client.lobby(game['game_id'], joined['ticket'], wait=0)['start']
            save = tmp / 'profile' / 'mpjoin' / 'j900.xml.gz'
            package = save.with_name('j900.x4join')
            save.parent.mkdir(parents=True)
            save.write_bytes(b'the join save')
            package.write_bytes(f'save={hashlib.sha256(save.read_bytes()).hexdigest()}=900'.encode() + bytes(200_000))
            running = self.master.games[game['game_id']].relay
            stand_in = running.run_dir / 'stand-in-relay.log'
            running.dropin.relay_log = serve.RelayLog(stand_in)
            stand_in.write_text(f'NATIVE_GATE JOIN slot=2 saved slot=1 tick=900 bytes=13 '
                                f'package={hashlib.sha256(package.read_bytes()).hexdigest()} portable={"f" * 32}\n')
            watch = netplay.LobbyWatch(client, game['game_id'], game['host_ticket'], (masterclient.MasterError, OSError), wait=2)
            logs = []
            sender = netdropin.JoinSender(client, game['game_id'], game['secret'], tmp / 'profile', watch,
                                          (masterclient.MasterError, OSError), log=logs.append)
            sender.start()
            try:
                target = tmp / 'joiner' / 'j900.x4join'
                target.parent.mkdir()
                how = joinfiles.fetch_package('127.0.0.1', start['relay']['port'], start['relay']['token'], 2, target.name,
                                              target, hashlib.sha256(package.read_bytes()).hexdigest(), offer_wait_s=30,
                                              log=logs.append)
                self.assertEqual(how, 'gz')
                self.assertEqual(target.read_bytes(), package.read_bytes())
                got = joinfiles.fetch('127.0.0.1', start['relay']['port'], start['relay']['token'], 2, save.name,
                                      target.with_name(save.name), retries=1, offer_wait_s=30)
                self.assertEqual(got, hashlib.sha256(save.read_bytes()).hexdigest())
                self.assertTrue(fakes_wait(lambda: 900 in sender.sent, 10), logs)
            finally:
                sender.stop()
                watch.stop()
            self.assertTrue(any('compressed to' in line for line in logs), logs)
            client.delete(game['game_id'], game['secret'])


def fakes_wait(predicate, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if predicate():
            return True
        time.sleep(0.05)
    return predicate()


if __name__ == '__main__':
    unittest.main()
