"""Shared pieces of the master's tests (tests/test_master_*.py): a master on 127.0.0.1 with ephemeral ports and a
temporary data folder, and a small HTTP client. Not a test module itself (unittest discovers test_*.py only).

Every player of a test runs on this PC, so the public IP a request comes from is simulated: the master's client_ip
hook takes the header X-Test-Client-IP (documentation addresses, RFC 5737) and falls back to the real peer."""
import hashlib
import http.client
import json
import random
import socket
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.master import relays, server  # noqa: E402
from lockstep.master.limits import MasterLimits  # noqa: E402

RELEASE = 'aaaaaaaaaaaa-bbbbbbbbbbbb'
SESSION_SHA = 'c' * 64
HOST_IP = '203.0.113.10'
PLAYER_IP = '198.51.100.20'
OTHER_IP = '192.0.2.30'
TEST_IP_HEADER = 'X-Test-Client-IP'


def test_client_ip(handler):
    return handler.headers.get(TEST_IP_HEADER) or handler.client_address[0]


def free_relay_ports(count=4):
    """A block of `count` ports free for UDP and TCP on 127.0.0.1."""
    for _ in range(200):
        first = random.randint(30000, 60000)
        if all(relays.port_free('127.0.0.1', port) for port in range(first, first + count)):
            return first, first + count - 1
    raise RuntimeError('no free block of ports')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class Answer:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body

    @property
    def json(self):
        return json.loads(self.body.decode('utf-8'))


class MasterCase(unittest.TestCase):
    """A running master per test (setUp/tearDown); self.call speaks HTTP to it."""
    limits = None          # a MasterLimits for the class, or the defaults
    options = {}           # more MasterOptions fields
    hook = True            # the X-Test-Client-IP hook

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.data = Path(self.tmp.name) / 'data'
        self.logs = []
        self.relay_ports = free_relay_ports()
        fields = dict(bind='127.0.0.1', http_port=0, echo_port=0, relay_ports=self.relay_ports, data=self.data,
                      limits=self.limits or MasterLimits())
        if self.hook:
            fields['client_ip'] = test_client_ip
        fields.update(self.options)
        self.master = server.Master(server.MasterOptions(**fields), log=self.logs.append).start()
        self.port = self.master.http_port

    def tearDown(self):
        self.master.close()
        self.tmp.cleanup()

    def call(self, method, path, body=None, ip=None, headers=None, raw=None, timeout=40):
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=timeout)
        sent = dict(headers or {})
        if ip:
            sent[TEST_IP_HEADER] = ip
        data = raw
        if body is not None:
            data = json.dumps(body).encode('utf-8')
            sent['Content-Type'] = 'application/json'
        try:
            connection.request(method, path, body=data, headers=sent)
            response = connection.getresponse()
            return Answer(response.status, dict(response.getheaders()), response.read())
        finally:
            connection.close()

    def host_game(self, ip=HOST_IP, **fields):
        body = {'name': 'Test game', 'public': True, 'release': RELEASE, 'players_max': 2, 'session_sha256': SESSION_SHA,
                'open_slots': [], 'want': 'relay', 'direct': None, 'lan': None}
        body.update(fields)
        answer = self.call('POST', '/api/v1/games', body, ip=ip)
        self.assertEqual(answer.status, 200, answer.body)
        return answer.json

    def join(self, game, ip=PLAYER_IP, code=None):
        body = {'release': RELEASE}
        if code:
            body['code'] = code
        return self.call('POST', f'/api/v1/games/{game["game_id"]}/join', body, ip=ip)

    def upload(self, game, name, data, auth=None, sha=None, ip=HOST_IP):
        headers = {'X-X4MP-Auth': auth or game['secret'], 'X-X4MP-Sha256': sha or sha256(data),
                   'Content-Type': 'application/octet-stream'}
        return self.call('PUT', f'/api/v1/games/{game["game_id"]}/files/{name}', ip=ip, headers=headers, raw=data)

    def lobby(self, game, ticket, since=0, wait=0, ip=None):
        return self.call('GET', f'/api/v1/games/{game["game_id"]}/lobby?ticket={ticket}&since={since}&wait={wait}', ip=ip)

    def assertError(self, answer, status, code):
        self.assertEqual(answer.status, status, answer.body)
        payload = answer.json
        self.assertFalse(payload['ok'])
        self.assertEqual(payload['error'], code)
        self.assertIsInstance(payload['why'], str)


def recipe(players=2, open_slots=(), window=(40, 72040), sets=()):
    """A play-recipe.json as tools/build_play_overlay.py writes one (the fields the master reads)."""
    data = {'kind': 'x4ls-play-recipe', 'version': 1, 'role': 'session', 'players': players, 'mode': 'coop', 'minutes': 40,
            'full_logs': False, 'lua_block': 'Pause', 'set': list(sets), 'open': list(open_slots), 'join_mode': 'report',
            'window': list(window), 'gate_dll_sha': 'd' * 64, 'snapshot_sha': 'e' * 64, 'session': None,
            'mixed_platform': False, 'save_every': 0}
    return json.dumps(data, indent=1, sort_keys=True).encode('utf-8')


def udp_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 0))
    sock.settimeout(5)
    return sock
