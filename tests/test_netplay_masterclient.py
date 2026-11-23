"""MasterClient (netplay package 2) against a fake master built from the contract's API table, on 127.0.0.1.

Covers the host and joiner flows, error mapping (JSON errors, a proxy's HTML page, no server at all), uploads with
their sha-256, downloads that resume with Range or restart and are checked before the rename, a path prefix as
behind a reverse proxy, HTTPS with verification, echo_address() and master_url(). Nothing leaves this PC."""
import hashlib
import json
import os
import socket
import ssl
import sys
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tests'))
from lockstep.launcher import masterclient  # noqa: E402
from lockstep.launcher.masterclient import MasterClient, MasterError  # noqa: E402
from netplay_fake_master import FakeMaster  # noqa: E402

RELEASE = '0123456789ab-ba9876543210'
OTHER_RELEASE = 'ffffffffffff-eeeeeeeeeeee'
SESSION_SHA = 'ab' * 32


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class Fixture(unittest.TestCase):
    def setUp(self):
        self.master = FakeMaster()
        self.addCleanup(self.master.close)
        self.client = MasterClient(self.master.base_url, timeout=5)
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.folder = Path(folder.name)

    def host_game(self, public=True, data=None):
        game = self.client.create_game('Test game', public, RELEASE, 4, SESSION_SHA, open_slots=(2, 3, 4),
                                       want='auto', direct={'udp_port': 47000, 'tcp_port': 47001}, lan=None)
        if data is not None:
            path = self.folder / 'session-save.xml.gz'
            path.write_bytes(data)
            self.client.upload(game['game_id'], game['secret'], 'session-save.xml.gz', path)
        return game


class Flows(Fixture):
    def test_health_and_the_echo_address(self):
        answer = self.client.health()
        self.assertTrue(answer['ok'])
        self.assertEqual(answer['echo_port'], self.master.echo.address[1])
        self.assertEqual(self.client.echo_address(), ('127.0.0.1', self.master.echo.address[1]))
        asked = len(self.master.requests)
        self.assertEqual(self.client.echo_address(), ('127.0.0.1', self.master.echo.address[1]))
        self.assertEqual(len(self.master.requests), asked)       # cached

    def test_host_and_join(self):
        data = os.urandom(300_000)
        game = self.host_game(public=False, data=data)
        self.assertEqual(set(game), {'ok', 'game_id', 'code', 'secret', 'host_ticket', 'slot'})
        self.assertEqual(self.client.games(RELEASE), [])          # private: not listed
        typed = game['code'].lower().replace('-', ' ')
        found = self.client.code(typed)
        self.assertEqual(found['game_id'], game['game_id'])
        joined = self.client.join(game['game_id'], RELEASE, code=typed)
        self.assertEqual(joined['slot'], 2)
        announced = joined['files']['session-save.xml.gz']
        self.assertEqual(announced, {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        calls = []
        dest = self.folder / 'joiner' / 'session-save.xml.gz'
        path = self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest,
                                    announced['sha256'], announced['size'], progress=lambda done, total: calls.append(
                                        (done, total)))
        self.assertEqual(path, dest)
        self.assertEqual(dest.read_bytes(), data)
        self.assertEqual(calls[-1], (len(data), len(data)))
        self.assertFalse(dest.with_name(dest.name + '.part').exists())

    def test_lobby_ready_start_heartbeat_leave_delete(self):
        game = self.host_game()
        listed = self.client.games(RELEASE)
        self.assertEqual([(item['game_id'], item['compatible']) for item in listed], [(game['game_id'], True)])
        self.assertFalse(self.client.games(OTHER_RELEASE)[0]['compatible'])
        joined = self.client.join(game['game_id'], RELEASE)
        first = self.client.lobby(game['game_id'], joined['ticket'], since=0, wait=2)
        result = {}

        def wait_for_change():
            result['answer'] = self.client.lobby(game['game_id'], joined['ticket'], since=first['stamp'], wait=2)
        poller = threading.Thread(target=wait_for_change)
        poller.start()
        self.client.ready(game['game_id'], joined['ticket'])
        poller.join(5)
        self.assertGreater(result['answer']['stamp'], first['stamp'])
        self.assertIn({'slot': 2, 'ready': True, 'host': False}, result['answer']['players'])
        self.assertEqual(self.client.start(game['game_id'], game['secret']), {'ok': True})
        self.assertEqual(self.client.heartbeat(game['game_id'], game['secret'], tick=5, state='session'), {'ok': True})
        self.assertEqual(self.client.heartbeat(game['game_id'], joined['ticket']), {'ok': True})
        self.assertEqual(self.master.requests[-1]['auth'], joined['ticket'])
        self.client.leave(game['game_id'], joined['ticket'])
        self.client.delete(game['game_id'], game['secret'])
        with self.assertRaises(MasterError) as caught:
            self.client.code(game['code'])
        self.assertEqual((caught.exception.code, caught.exception.status), ('unknown_code', 404))


class Errors(Fixture):
    def test_the_servers_error_reaches_the_page(self):
        game = self.host_game()
        with self.assertRaises(MasterError) as caught:
            self.client.join(game['game_id'], OTHER_RELEASE)
        self.assertEqual((caught.exception.code, caught.exception.status), ('incompatible', 409))
        self.assertEqual(caught.exception.why, 'This game runs another release.')
        path = self.folder / 'session.json'
        path.write_text('{}')
        with self.assertRaises(MasterError) as caught:
            self.client.upload(game['game_id'], 'f' * 32, 'session.json', path)
        self.assertEqual((caught.exception.code, caught.exception.status), ('forbidden', 403))

    def test_a_text_that_is_no_join_code_is_not_sent(self):
        asked = len(self.master.requests)
        with self.assertRaises(MasterError) as caught:
            self.client.code('X4MP-192.168.1.20-47000-' + 'a' * 32)
        self.assertEqual((caught.exception.code, caught.exception.status), ('bad_code', 0))
        self.assertEqual(len(self.master.requests), asked)

    def test_a_proxy_page_and_no_server(self):
        proxy = FakeMaster(prefix='/x4mp')
        self.addCleanup(proxy.close)
        wrong_prefix = MasterClient(f'http://127.0.0.1:{proxy.port}/elsewhere', timeout=5)
        with self.assertRaises(MasterError) as caught:
            wrong_prefix.health()
        self.assertEqual((caught.exception.code, caught.exception.status), ('http_404', 404))
        self.assertIn('not here', caught.exception.why)
        nobody = MasterClient(f'http://127.0.0.1:{free_port()}', timeout=2)
        with self.assertRaises(MasterError) as caught:
            nobody.health()
        self.assertEqual((caught.exception.code, caught.exception.status), ('unreachable', 0))
        self.assertIsNone(nobody.echo_address())


class Downloads(Fixture):
    def joined(self, data):
        game = self.host_game(data=data)
        joined = self.client.join(game['game_id'], RELEASE)
        return game, joined, joined['files']['session-save.xml.gz']

    def test_a_broken_download_resumes_with_range(self):
        data = os.urandom(200_000)
        game, joined, announced = self.joined(data)
        dest = self.folder / 'save.xml.gz'
        part = dest.with_name(dest.name + '.part')
        self.master.cut_download_after = 70_000
        with self.assertRaises(MasterError) as caught:
            self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, announced['sha256'],
                                 announced['size'])
        self.assertEqual(caught.exception.code, 'unreachable')
        self.assertEqual(part.stat().st_size, 70_000)
        self.assertFalse(dest.exists())
        calls = []
        self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, announced['sha256'],
                             announced['size'], progress=lambda done, total: calls.append(done))
        self.assertEqual(self.master.requests[-1]['range'], 'bytes=70000-')
        self.assertEqual(dest.read_bytes(), data)
        self.assertGreater(calls[0], 70_000)
        self.assertFalse(part.exists())
        asked = len(self.master.requests)
        self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, announced['sha256'],
                             announced['size'])
        self.assertEqual(len(self.master.requests), asked)        # already there: nothing fetched

    def test_a_server_that_ignores_range_and_a_stale_part(self):
        data = os.urandom(50_000)
        game, joined, announced = self.joined(data)
        dest = self.folder / 'save.xml.gz'
        dest.with_name(dest.name + '.part').write_bytes(b'x' * 1000)      # bytes of some other file
        self.master.ignore_range = True
        self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, announced['sha256'],
                             announced['size'])
        self.assertEqual(dest.read_bytes(), data)
        self.master.ignore_range = False
        dest.unlink()
        dest.with_name(dest.name + '.part').write_bytes(b'y' * 1000)      # wrong bytes, honoured range
        self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, announced['sha256'],
                             announced['size'])
        self.assertEqual(dest.read_bytes(), data)                       # checked, failed, fetched again whole

    def test_bytes_that_do_not_match_are_never_renamed(self):
        data = os.urandom(10_000)
        game, joined, announced = self.joined(data)
        dest = self.folder / 'save.xml.gz'
        with self.assertRaises(MasterError) as caught:
            self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, 'cd' * 32,
                                 announced['size'])
        self.assertEqual(caught.exception.code, 'download_mismatch')
        self.assertFalse(dest.exists())
        self.assertFalse(dest.with_name(dest.name + '.part').exists())
        with self.assertRaises(MasterError) as caught:
            self.client.download(game['game_id'], joined['ticket'], 'session-save.xml.gz', dest, announced['sha256'],
                                 announced['size'] - 1)
        self.assertEqual(caught.exception.code, 'download_mismatch')
        self.assertFalse(dest.with_name(dest.name + '.part').exists())


class BaseUrls(unittest.TestCase):
    def test_a_path_prefix_as_behind_a_reverse_proxy(self):
        master = FakeMaster(prefix='/x4mp')
        self.addCleanup(master.close)
        client = MasterClient(master.base_url + '/', timeout=5)
        self.assertEqual(client.base_url, master.base_url)
        self.assertEqual(client.health()['version'], 'fake')
        self.assertEqual(master.requests[-1]['path'], '/x4mp/api/v1/health')
        self.assertEqual(client.url('/games/g1/join'), master.base_url + '/api/v1/games/g1/join')

    def test_https_is_verified_and_has_no_off_switch(self):
        master = FakeMaster(prefix='/x4mp')
        self.addCleanup(master.close)
        client = MasterClient(f'https://127.0.0.1:{master.port}/x4mp', timeout=3)
        self.assertEqual(client.ssl_context.verify_mode, ssl.CERT_REQUIRED)
        self.assertTrue(client.ssl_context.check_hostname)
        with self.assertRaises(MasterError) as caught:
            client.health()                     # a plain-HTTP server cannot complete a TLS handshake
        self.assertEqual((caught.exception.code, caught.exception.status), ('unreachable', 0))

    def test_base_url_forms(self):
        normalize = masterclient.normalize_base_url
        self.assertEqual(normalize('203.0.113.10'), 'http://203.0.113.10:47100')
        self.assertEqual(normalize('203.0.113.10:47999'), 'http://203.0.113.10:47999')
        self.assertEqual(normalize('master.example.org'), 'http://master.example.org:47100')
        self.assertEqual(normalize('https://master.example.org/x4mp/'), 'https://master.example.org/x4mp')
        self.assertEqual(normalize('http://203.0.113.10:47100'), 'http://203.0.113.10:47100')
        for bad in ('', 'ftp://master.example.org', 'http://', 'host:port', 'https://master.example.org/?a=1', None):
            self.assertIsNone(normalize(bad), bad)
        with self.assertRaises(ValueError):
            MasterClient('ftp://master.example.org')

    def test_master_url(self):
        with tempfile.TemporaryDirectory() as folder:
            kit = Path(folder)
            self.assertIsNone(masterclient.master_url(kit, env={}))
            (kit / 'master.json').write_text(json.dumps({'master': '203.0.113.10:47100'}))
            self.assertEqual(masterclient.master_url(kit, env={}), 'http://203.0.113.10:47100')
            (kit / 'master.json').write_text(json.dumps({'url': 'https://master.example.org/x4mp'}))
            self.assertEqual(masterclient.master_url(kit, env={}), 'https://master.example.org/x4mp')
            (kit / 'master.json').write_text(json.dumps({'host': '198.51.100.4', 'port': 47101}))
            self.assertEqual(masterclient.master_url(kit, env={}), 'http://198.51.100.4:47101')
            (kit / 'master.json').write_text('not json')
            self.assertIsNone(masterclient.master_url(kit, env={}))
            environment = {'X4MP_MASTER': 'master.example.org:47100'}
            self.assertEqual(masterclient.master_url(kit, env=environment), 'http://master.example.org:47100')
            self.assertIsNone(masterclient.master_url(kit, env={'X4MP_MASTER': '  '}))


if __name__ == '__main__':
    unittest.main()
