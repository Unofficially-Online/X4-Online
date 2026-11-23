"""LAN discovery (netplay package 2): the X4MPLAN announcement, the per-interface fan-out and the listener.

Interfaces are injected. The fan-out is checked with a recording socket (which source, which destinations, which
socket options); the end-to-end run uses real loopback sockets whose sendto is redirected to the test's listener on
127.0.0.1, so no broadcast or multicast datagram ever leaves this PC. Addresses are private or documentation ones."""
import socket
import sys
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import landisco  # noqa: E402

RELEASE = '0123456789ab-ba9876543210'
CABLE = {'ip': '192.168.1.20', 'netmask': '255.255.255.0', 'broadcast': '192.168.1.255', 'name': 'cable', 'prefix': 24}
TAILSCALE = {'ip': '100.101.102.103', 'netmask': '255.255.255.255', 'broadcast': None, 'name': 'ts', 'prefix': 32}


def loopback(ip):
    return {'ip': ip, 'netmask': '255.0.0.0', 'broadcast': None, 'name': f'lo {ip}', 'prefix': 8}


class Datagram(unittest.TestCase):
    def test_round_trip(self):
        data = landisco.encode(RELEASE, 'k7qx3fna', 'Bob|s, game: Teladi Outpost', 47001, 1, 4,
                               ['192.168.1.20', '10.147.17.5'])
        self.assertTrue(data.startswith(b'X4MPLAN|1|' + RELEASE.encode() + b'|K7QX-3FNA|'))
        self.assertEqual(data.count(b'|'), 7)
        game = landisco.decode(data, '192.168.1.20')
        self.assertEqual(game, {'ip': '192.168.1.20', 'candidates': ['192.168.1.20', '10.147.17.5'],
                                'release': RELEASE, 'code': 'K7QX-3FNA', 'name': 'Bob|s, game: Teladi Outpost'[:40],
                                'tcp_port': 47001, 'players': 1, 'players_max': 4})
        private = landisco.decode(landisco.encode(RELEASE, None, 'LAN party', 47001, 2, 2, []), '10.0.0.5')
        self.assertIsNone(private['code'])
        self.assertEqual(private['candidates'], [])

    def test_names_are_cut_by_bytes(self):
        name = 'Ünïcödé ' * 10
        cut = landisco.cut_name(name)
        self.assertLessEqual(len(cut.encode('utf-8')), landisco.NAME_BYTES_MAX)
        self.assertTrue(name.startswith(cut))
        self.assertEqual(landisco.cut_name('a\nb\x00c'), 'a b c')
        sixteen = ['192.168.1.%d' % i for i in range(1, 17)]
        data = landisco.encode(RELEASE, '-', '€' * 30, 47001, 1, 8, sixteen)
        self.assertLessEqual(len(data), landisco.DATAGRAM_MAX)
        self.assertEqual(landisco.decode(data, '192.168.1.1')['name'], '€' * 13)   # 13 x 3 bytes = 39

    def test_malformed_datagrams_are_ignored(self):
        good = landisco.encode(RELEASE, 'K7QX-3FNA', 'game', 47001, 1, 4, ['192.168.1.20']).decode()
        fields = good.split('|')

        def variant(index, value):
            changed = list(fields)
            changed[index] = value
            return '|'.join(changed).encode()
        bad = [good.encode() + b'x' * 600,                      # oversized
               b'X4MPLAN|2' + good.encode()[9:],                 # another version
               b'X4MPLAM' + good.encode()[7:],
               '|'.join(fields[:-1]).encode(),                   # a field missing
               good.encode() + b'|extra',
               variant(2, 'rel|ease'), variant(2, ''), variant(2, 'r' * 65), variant(2, 'ré'),
               variant(3, 'k7qx-3fna'), variant(3, 'K7QX3FNA'), variant(3, 'ABCD-EFG0'),
               variant(4, 'x' * 121), variant(4, '%ff%fe'),
               variant(5, '0'), variant(5, '65536'), variant(5, '047001'), variant(5, 'port'),
               variant(6, '3/2'), variant(6, '1/9'), variant(6, '1/1'), variant(6, '1'), variant(6, '01/4'),
               variant(7, ','.join(['192.168.1.%d' % i for i in range(1, 18)])),
               variant(7, '192.168.1.256'), variant(7, '224.0.0.1'), variant(7, '0.0.0.0'),
               variant(7, '255.255.255.255'), variant(7, '127.0.0.1'), variant(7, 'host.example'),
               b'\xff\xfe' + good.encode()]
        for data in bad:
            with self.subTest(data=data[:80]):
                self.assertIsNone(landisco.decode(data, '192.168.1.20'))
        self.assertIsNotNone(landisco.decode(variant(7, '127.0.0.1'), '127.0.0.1'))   # a loopback sender's own

    def test_announcer_refuses_bad_fields(self):
        for arguments in ((RELEASE, 'nonsense', 'x', 47001, 1, 4), (RELEASE, None, 'x', 0, 1, 4),
                          (RELEASE, None, 'x', 47001, 1, 9), ('bad|release', None, 'x', 47001, 1, 4)):
            with self.subTest(arguments=arguments):
                with self.assertRaises(ValueError):
                    landisco.Announcer(*arguments, interfaces=[])


class RecordingSocket:
    """What the announcer does with a socket, without a network."""
    made = []

    def __init__(self, family, kind):
        self.options = {}
        self.bound = None
        self.sent = []
        self.closed = False
        RecordingSocket.made.append(self)

    def setsockopt(self, level, option, value):
        self.options[(level, option)] = value

    def bind(self, address):
        if address[0] == '198.51.100.99':
            raise OSError('cannot assign requested address')
        self.bound = address

    def sendto(self, data, destination):
        if destination[0] == '255.255.255.255' and self.bound[0] == '100.101.102.103':
            raise OSError('network unreachable')
        self.sent.append((data, destination))

    def close(self):
        self.closed = True

    def __enter__(self):
        return self

    def __exit__(self, *exception):
        self.close()


class FanOut(unittest.TestCase):
    def test_every_interface_every_destination(self):
        RecordingSocket.made = []
        lines = []
        gone = dict(CABLE, ip='198.51.100.99', name='gone')
        announcer = landisco.Announcer(RELEASE, 'K7QX-3FNA', 'game', 47001, 1, 4, port=47102,
                                       interfaces=[CABLE, TAILSCALE, gone], socket_factory=RecordingSocket,
                                       log=lines.append)
        sent = announcer.announce()
        cable, tailscale, failed = RecordingSocket.made
        self.assertEqual(cable.bound, ('192.168.1.20', 0))
        self.assertEqual([destination for _, destination in cable.sent],
                         [('192.168.1.255', 47102), ('255.255.255.255', 47102), ('239.255.71.2', 47102)])
        self.assertEqual([destination for _, destination in tailscale.sent], [('239.255.71.2', 47102)])
        self.assertEqual(cable.options[(socket.IPPROTO_IP, socket.IP_MULTICAST_IF)], socket.inet_aton('192.168.1.20'))
        self.assertEqual(cable.options[(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL)], 8)
        self.assertEqual(cable.options[(socket.SOL_SOCKET, socket.SO_BROADCAST)], 1)
        self.assertTrue(all(made.closed for made in RecordingSocket.made))
        self.assertEqual(sent, 4)
        self.assertEqual(announcer.errors, 2)
        game = landisco.decode(cable.sent[0][0], '192.168.1.20')
        self.assertEqual(game['candidates'], ['192.168.1.20', '100.101.102.103', '198.51.100.99'])
        announcer.announce()
        self.assertEqual(len(lines), 2)                 # each failure is logged once, not every 2 s

    def test_the_interface_list_is_asked_every_round(self):
        RecordingSocket.made = []
        networks = [[CABLE], [CABLE, TAILSCALE]]
        announcer = landisco.Announcer(RELEASE, None, 'game', 47001, 1, 4, interfaces=lambda: networks.pop(0),
                                       socket_factory=RecordingSocket)
        self.assertEqual(announcer.announce(), 3)
        self.assertEqual(announcer.announce(), 4)       # a VPN connected meanwhile
        self.assertEqual(landisco.decode(RecordingSocket.made[-1].sent[0][0], '100.101.102.103')['candidates'],
                         ['192.168.1.20', '100.101.102.103'])


class RedirectingSocket:
    """A real UDP socket whose every send goes to the test's listener instead (broadcast and multicast included)."""

    def __init__(self, target):
        self.target = target
        self.destinations = []

    def __call__(self, family, kind):
        owner = self
        real = socket.socket(family, kind)

        class Wrapper:
            def setsockopt(self, *arguments):
                if arguments[1] in (socket.IP_MULTICAST_IF, socket.IP_MULTICAST_TTL, socket.SO_BROADCAST):
                    return None
                return real.setsockopt(*arguments)

            def bind(self, address):
                real.bind(address)

            def sendto(self, data, destination):
                owner.destinations.append(destination)
                return real.sendto(data, owner.target)

            def close(self):
                real.close()

            def __enter__(self):
                return self

            def __exit__(self, *exception):
                real.close()
        return Wrapper()


class Loopback(unittest.TestCase):
    def test_scan_finds_each_game_once_with_its_newest_data(self):
        listener = landisco.open_listener(0, [loopback('127.0.0.1')], bind='127.0.0.1')
        self.addCleanup(listener.close)
        target = listener.getsockname()
        redirect = RedirectingSocket(target)
        interfaces = [loopback('127.0.0.1'), loopback('127.0.0.2')]
        announcer = landisco.Announcer(RELEASE, 'K7QX-3FNA', 'game', 47001, 1, 4, port=target[1],
                                       interfaces=interfaces, socket_factory=redirect, interval=0.2)
        junk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(junk.close)
        result = {}

        def browse():
            result['games'] = landisco.scan(1.5, sock=listener)
        browser = threading.Thread(target=browse)
        browser.start()
        junk.sendto(b'X4MPLAN|1|broken', target)
        junk.sendto(b'x' * 2000, target)
        announcer.start()
        time.sleep(0.5)
        announcer.update(players=3)
        time.sleep(0.4)
        browser.join(5)
        announcer.stop()
        games = result['games']
        senders = [game['ip'] for game in games]
        self.assertIn('127.0.0.1', senders)
        self.assertEqual(len(senders), len(set(senders)))              # many copies, one entry per sender
        self.assertTrue(set(senders) <= {'127.0.0.1', '127.0.0.2'})
        for game in games:
            self.assertEqual((game['code'], game['players'], game['players_max'], game['tcp_port']),
                             ('K7QX-3FNA', 3, 4, 47001))
            self.assertEqual(game['candidates'], ['127.0.0.1', '127.0.0.2'])
        self.assertIn(('239.255.71.2', target[1]), redirect.destinations)
        self.assertIn(('255.255.255.255', target[1]), redirect.destinations)

    def test_the_listener_shares_its_port(self):
        first = landisco.open_listener(0, [loopback('127.0.0.1')], bind='127.0.0.1')
        self.addCleanup(first.close)
        second = landisco.open_listener(first.getsockname()[1], [loopback('127.0.0.1')], bind='127.0.0.1')
        self.addCleanup(second.close)
        self.assertEqual(first.getsockname(), second.getsockname())    # SO_REUSEADDR: two browsers on one PC
        started = time.perf_counter()
        self.assertEqual(landisco.scan(0.3, sock=first), [])
        self.assertLess(time.perf_counter() - started, 1.0)


if __name__ == '__main__':
    unittest.main()
