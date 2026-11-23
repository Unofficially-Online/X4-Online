"""The master's UDP side and the probe: the echo answers only its source with no more bytes than it got, the probe
reaches only the requesting IP, and a passed probe makes a game direct."""
import socket
import time
import unittest

from master_support import RELEASE, MasterCase, MasterLimits, udp_socket
from lockstep.master import udp

NONCE = '0123456789abcdef'


def echo_request(nonce=NONCE, size=64):
    head = f'X4MPECHO|1|{nonce}|'.encode('ascii')
    return head + b'-' * max(0, size - len(head))


class EchoReplyTests(unittest.TestCase):
    def test_the_answer_names_the_source_padded_to_the_requests_size(self):
        request = echo_request()
        reply = udp.echo_reply(request, ('203.0.113.10', 50123))
        base = f'X4MPSEEN|1|{NONCE}|203.0.113.10:50123'.encode('ascii')
        self.assertEqual(len(reply), len(request))
        self.assertEqual(reply, base + b'|' + b'.' * (len(request) - len(base) - 1))
        self.assertEqual(reply.split(b'|')[:4], [b'X4MPSEEN', b'1', NONCE.encode(), b'203.0.113.10:50123'])

    def test_no_padding_field_when_fewer_than_two_bytes_are_left(self):
        address = ('203.0.113.10', 50123)
        base = f'X4MPSEEN|1|{NONCE}|203.0.113.10:50123'.encode('ascii')
        for size in (len(base), len(base) + 1):
            request = echo_request(size=size)
            self.assertEqual(udp.echo_reply(request, address), base)
        self.assertIsNone(udp.echo_reply(echo_request(size=len(base) - 1), address))

    def test_short_or_wrong_requests_are_not_answered(self):
        address = ('203.0.113.10', 50123)
        self.assertIsNone(udp.echo_reply(f'X4MPECHO|1|{NONCE}|'.encode(), address))   # too short for the answer
        self.assertIsNone(udp.echo_reply(echo_request(nonce='0123'), address))
        self.assertIsNone(udp.echo_reply(echo_request(nonce='0123456789abcdeg'), address))
        self.assertIsNone(udp.echo_reply(echo_request().replace(b'|1|', b'|2|', 1), address))
        self.assertIsNone(udp.echo_reply(b'X4MPPROBE|1|' + NONCE.encode() + b'|' + b'-' * 60, address))
        self.assertIsNone(udp.echo_reply(echo_request(size=1500), address))
        self.assertIsNone(udp.echo_reply(b'', address))


class EchoServerTests(MasterCase):
    limits = MasterLimits(echo_per_s=0.001, echo_burst=3)

    def test_a_round_trip_and_the_rate_limit(self):
        client = udp_socket()
        try:
            client.sendto(echo_request(), ('127.0.0.1', self.master.echo_port))
            reply, source = client.recvfrom(2048)
            self.assertEqual(source[1], self.master.echo_port)
            port = client.getsockname()[1]
            self.assertEqual(reply.split(b'|')[:4], [b'X4MPSEEN', b'1', NONCE.encode(), f'127.0.0.1:{port}'.encode()])
            self.assertEqual(len(reply), 64)
            client.sendto(f'X4MPECHO|1|{NONCE}|'.encode(), ('127.0.0.1', self.master.echo_port))   # not answered
            for _ in range(2):
                client.sendto(echo_request(), ('127.0.0.1', self.master.echo_port))
            client.settimeout(1.0)
            answers = 0
            try:
                while True:
                    client.recvfrom(2048)
                    answers += 1
            except socket.timeout:
                pass
            self.assertEqual(answers, 1)   # the burst of 3 is spent: the fourth datagram is dropped
        finally:
            client.close()


class ProbeTests(MasterCase):
    hook = False   # the probe must reach this PC: the real peer address 127.0.0.1

    def listeners(self):
        tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp.bind(('127.0.0.1', 0))
        tcp.listen(16)
        self.addCleanup(tcp.close)
        datagrams = udp_socket()
        self.addCleanup(datagrams.close)
        return tcp, datagrams

    def test_the_probe_reaches_the_requesting_ip_by_tcp_and_udp(self):
        tcp, datagrams = self.listeners()
        body = {'udp_port': datagrams.getsockname()[1], 'tcp_port': tcp.getsockname()[1], 'nonce': NONCE}
        answer = self.call('POST', '/api/v1/probe', body)
        self.assertEqual(answer.status, 200, answer.body)
        self.assertEqual(answer.json, {'ok': True, 'tcp': True, 'ip': '127.0.0.1'})
        data, _ = datagrams.recvfrom(2048)
        self.assertEqual(data, f'X4MPPROBE|1|{NONCE}'.encode('ascii'))

    def test_a_closed_port_is_not_reached(self):
        tcp, datagrams = self.listeners()
        closed = tcp.getsockname()[1]
        tcp.close()
        body = {'udp_port': datagrams.getsockname()[1], 'tcp_port': closed, 'nonce': NONCE}
        self.assertFalse(self.call('POST', '/api/v1/probe', body).json['tcp'])

    def test_the_probe_takes_no_target(self):
        for extra in ({'ip': '192.0.2.1'}, {'host': 'master.example.org'}, {'address': '192.0.2.1'},
                      {'target': '192.0.2.1'}):
            body = {'udp_port': 47000, 'tcp_port': 47001, 'nonce': NONCE, **extra}
            self.assertError(self.call('POST', '/api/v1/probe', body), 400, 'bad_request')

    def test_the_probe_checks_its_fields(self):
        for body in ({'udp_port': 22, 'tcp_port': 47001, 'nonce': NONCE}, {'udp_port': 47000, 'tcp_port': 70000, 'nonce': NONCE},
                     {'udp_port': 47000, 'tcp_port': 47001, 'nonce': 'x'}):
            self.assertError(self.call('POST', '/api/v1/probe', body), 400, 'bad_request')

    def test_six_probes_a_minute_per_ip(self):
        tcp, datagrams = self.listeners()
        body = {'udp_port': datagrams.getsockname()[1], 'tcp_port': tcp.getsockname()[1], 'nonce': NONCE}
        statuses = [self.call('POST', '/api/v1/probe', body).status for _ in range(7)]
        self.assertEqual(statuses, [200] * 6 + [429])

    def test_a_passed_probe_makes_the_game_direct(self):
        tcp, datagrams = self.listeners()
        ports = {'udp_port': datagrams.getsockname()[1], 'tcp_port': tcp.getsockname()[1]}
        self.assertTrue(self.call('POST', '/api/v1/probe', {**ports, 'nonce': NONCE}).json['tcp'])
        game = self.host_game(ip=None, want='auto', direct=ports)
        second = self.join(game, ip=None).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': second['ticket']})
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']})
        self.assertEqual(answer.status, 200, answer.body)
        self.assertEqual(answer.json['mode'], 'direct')
        host_view = self.lobby(game, game['host_ticket']).json
        self.assertEqual(host_view['start']['relay']['host'], '127.0.0.1')
        self.assertEqual(host_view['start']['relay']['port'], ports['udp_port'])
        self.assertEqual([row['ip'] for row in host_view['players']], ['127.0.0.1', '127.0.0.1'])   # its --allow-file
        player_view = self.lobby(game, second['ticket']).json
        self.assertNotIn('ip', player_view['players'][0])
        self.assertEqual(self.master.pool.running(), 0)   # no relay of the master's

    def test_without_a_probe_auto_is_relay_and_direct_is_refused(self):
        ports = {'udp_port': 47000, 'tcp_port': 47001}
        game = self.host_game(ip=None, want='direct', direct=ports)
        second = self.join(game, ip=None).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': second['ticket']})
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']})
        self.assertError(answer, 409, 'unreachable')


class ProbeIpv6Tests(MasterCase):
    def test_an_ipv6_client_cannot_be_probed(self):
        body = {'udp_port': 47000, 'tcp_port': 47001, 'nonce': NONCE}
        self.assertError(self.call('POST', '/api/v1/probe', body, ip='2001:db8::1'), 400, 'ipv4_only')


class RelayReleaseTests(MasterCase):
    options = {'relay_releases': ('ffffffffffff-111111111111',)}

    def test_a_game_of_another_release_cannot_use_the_masters_relay(self):
        game = self.host_game(want='auto')
        second = self.join(game).json
        self.call('POST', f'/api/v1/games/{game["game_id"]}/ready', {'ticket': second['ticket']})
        answer = self.call('POST', f'/api/v1/games/{game["game_id"]}/start', {'secret': game['secret']})
        self.assertError(answer, 409, 'incompatible')
        self.assertIn('ffffffffffff-111111111111', answer.json['why'])
        self.assertIn(RELEASE, answer.json['why'])


if __name__ == '__main__':
    unittest.main()
