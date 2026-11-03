"""NGNAT/1 rendezvous (relay extension for direct player links, roadmap B7): server rules
and the relay's opt-in flag. Pure Python; the C client side is tests/test_nat_traversal.py."""
import importlib.util
from pathlib import Path
import random
import socket
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_rendezvous_tests', ROOT/'tools/dev/native_gate_rendezvous.py')
rv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rv)
TOKEN = '0123456789abcdef0123456789abcdef'
KEY = rv.rendezvous_key(TOKEN)
A = ('203.0.113.1', 40001)
B = ('203.0.113.2', 40002)
INST = {1: '1111111111111111', 2: '2222222222222222', 3: '3333333333333333'}


def fields(packet):
    assert rv.verify(KEY, packet), packet
    return packet.decode().split('|')


class Client:
    def __init__(self, server, slot, address, lan='-', mapped='-', filt='open', inst=None, mapping='unknown'):
        self.server, self.slot, self.address, self.lan, self.mapped, self.filter = server, slot, address, lan, mapped, filt
        self.mapping = mapping
        self.inst, self.seq, self.now = inst or INST[slot], 0, 0.0

    def send(self, kind, *extra, index=0, address=None, key=KEY, pad_to=rv.REQUEST_MIN):
        self.seq += 1
        self.now += 0.01
        data = rv.request(key, kind, self.slot, self.inst, self.seq, *extra, pad_to=pad_to)
        return self.server.receive(data, address or self.address, index, self.now), data

    def hello(self, index=0, address=None):
        return self.send('HELLO', self.lan, self.mapped, self.mapping, self.filter, index=index, address=address)

    def ask(self, peer):
        return self.send('ASK', peer)


class Server(unittest.TestCase):
    def setUp(self):
        self.server = rv.Rendezvous(TOKEN, clients=3, random_bytes=lambda n: bytes(random.getrandbits(8) for _ in range(n)))

    def test_hello_reports_the_observed_address_and_classifies_the_mapping(self):
        a = Client(self.server, 1, A, lan='192.168.1.10:5000')
        (out,), request = a.hello()
        index, dest, packet = out
        self.assertEqual((index, dest), (0, A))
        self.assertLessEqual(len(packet), len(request))
        f = fields(packet)
        self.assertEqual(f[2:9], ['SEEN', '1', INST[1], '1', '0', '203.0.113.1:40001', 'unknown'])
        (out,), _ = a.hello(index=1)
        self.assertEqual(out[0], 1)
        self.assertEqual(fields(out[2])[8], 'eim')
        (out,), _ = a.hello(index=1, address=('203.0.113.1', 40999))   # another mapping towards P+1
        self.assertEqual(fields(out[2])[8], 'edm')
        open_host = Client(self.server, 2, B, lan='203.0.113.2:40002')
        (out,), _ = open_host.hello()
        self.assertEqual(fields(out[2])[8], 'open')        # a host without NAT names its own address

    def test_peers_get_the_clients_own_classification(self):
        """A refresh HELLO through a re-mapped NAT must not turn the peer 'edm' (stale P+1 view)."""
        a = Client(self.server, 1, A, mapping='eim', filt='port')
        b = Client(self.server, 2, B)
        a.hello(), a.hello(index=1), b.hello()
        a.hello(address=('203.0.113.1', 41234))                        # the NAT re-mapped: new port on P only
        self.assertEqual(self.server.registrations[1].mapping(), 'edm')
        f = fields(b.ask(1)[0][0][2])
        self.assertEqual(f[8:11], ['eim', 'port', '203.0.113.1:41234'])
        c = Client(self.server, 3, ('203.0.113.3', 1), mapping='unknown')
        c.hello(), c.hello(index=1, address=('203.0.113.3', 2))
        self.assertEqual(fields(b.ask(3)[0][0][2])[8], 'edm')          # unknown: the relay's own view

    def test_nothing_unauthenticated_short_or_malformed_is_answered(self):
        a = Client(self.server, 1, A)
        self.assertEqual(a.send('HELLO', '-', '-', 'eim', 'open', key=rv.rendezvous_key('f'*32))[0], [])
        self.assertEqual(a.send('HELLO', '-', '-', 'eim', 'open', pad_to=200)[0], [])     # below REQUEST_MIN
        self.assertEqual(self.server.receive(b'NGNAT|1|HELLO' + b'.'*600, A, 0, 1.0), [])
        self.assertEqual(a.send('HELLO', '-', '-', 'eim', 'sideways')[0], [])
        self.assertEqual(a.send('HELLO', '-', '-', 'cone', 'open')[0], [])
        self.assertEqual(a.send('HELLO', '010.0.0.1:5', '-', 'eim', 'open')[0], [])
        self.assertEqual(a.send('HELLO', '-', '-', 'open')[0], [])                  # field count
        self.assertEqual(a.send('PEER', '-', '-', 'eim', 'open')[0], [])            # not a request type
        for slot in ('0', '4', '01', '+1'):
            data = rv.seal(KEY, f'NGNAT|1|HELLO|{slot}|{INST[1]}|99|-|-|eim|open|{"."*190}'.encode())
            self.assertEqual(self.server.receive(data, A, 0, 1.0), [], slot)
        self.assertEqual(self.server.registrations, {})
        self.assertGreaterEqual(self.server.counters['refused'], 10)

    def test_no_reply_is_ever_larger_than_its_request(self):
        rng = random.Random(7)
        clients = [Client(self.server, s, (f'203.0.113.{s}', 40000+s), lan=f'192.168.{s}.10:5000', mapped=f'198.51.100.{s}:6000')
                   for s in (1, 2, 3)]
        for _ in range(300):
            c = rng.choice(clients)
            kind = rng.choice(('HELLO', 'HELLO1', 'FTEST', 'ASK', 'BYE'))
            if kind == 'HELLO':
                out, request = c.hello()
            elif kind == 'HELLO1':
                out, request = c.hello(index=1)
            elif kind == 'ASK':
                out, request = c.ask(rng.choice([s for s in (1, 2, 3) if s != c.slot]))
            else:
                out, request = c.send(kind)
            for index, dest, packet in out:
                self.assertEqual(dest, c.address)                 # only ever to the request's source
                self.assertLessEqual(len(packet), len(request))
            self.server.global_bucket.level = 1e9
            for bucket in self.server.buckets.values():
                bucket.level = 1e9

    def test_replayed_requests_are_ignored(self):
        a = Client(self.server, 1, A)
        out, request = a.hello()
        self.assertEqual(len(out), 1)
        self.assertEqual(self.server.receive(request, ('198.51.100.66', 1), 0, 5.0), [])   # same seq, any source
        self.assertEqual(self.server.registrations[1].observed[0], A)

    def test_only_registered_players_learn_addresses(self):
        a, b = Client(self.server, 1, A, lan='192.168.1.10:5000'), Client(self.server, 2, B, lan='192.168.2.20:5000')
        self.assertEqual(a.ask(2)[0], [])                              # a is not registered: silence
        a.hello()
        (out,), _ = a.ask(2)
        self.assertEqual(fields(out[2])[2:7], ['WAIT', '1', INST[1], str(a.seq), '2'])
        b.hello()
        (out,), _ = a.ask(2)
        f = fields(out[2])
        self.assertEqual(f[2], 'PEER')
        self.assertEqual(f[6:13], ['2', INST[2], 'unknown', 'open', '203.0.113.2:40002', '-', '-'])   # no LAN across sites
        (back,), _ = b.ask(1)
        self.assertEqual(fields(back[2])[13], f[13])                   # one pair key for both directions
        self.assertEqual(len(bytes.fromhex(f[13])), 16)
        self.assertEqual(a.ask(1)[0], [])                              # never yourself
        self.assertEqual(a.ask(4)[0], [])

    def test_lan_addresses_only_between_players_behind_the_same_public_address(self):
        a = Client(self.server, 1, ('203.0.113.1', 40001), lan='192.168.1.10:5000')
        b = Client(self.server, 2, ('203.0.113.1', 40777), lan='192.168.1.11:5000')
        c = Client(self.server, 3, ('203.0.113.3', 40003), lan='192.168.3.10:5000')
        for x in (a, b, c):
            x.hello()
        self.assertEqual(fields(a.ask(2)[0][0][2])[11], '192.168.1.11:5000')
        self.assertEqual(fields(c.ask(1)[0][0][2])[11], '-')
        loop = Client(self.server, 3, ('203.0.113.1', 1), lan='127.0.0.1:5000', inst='4444444444444444')
        loop.hello()                                                   # replaces slot 3 (new instance)
        self.assertEqual(fields(a.ask(3)[0][0][2])[11], '-')           # loopback only for loopback test relays

    def test_mapped_addresses_must_be_public(self):
        a = Client(self.server, 1, A, mapped='192.168.0.2:6000')
        b = Client(self.server, 2, B, mapped='198.51.100.2:6000')
        a.hello(), b.hello()
        self.assertEqual(fields(b.ask(1)[0][0][2])[12], '-')
        self.assertEqual(fields(a.ask(2)[0][0][2])[12], '198.51.100.2:6000')

    def test_filter_test_answers_from_the_second_port(self):
        a = Client(self.server, 1, A)
        self.assertEqual(a.send('FTEST')[0], [])                       # unregistered
        a.hello()
        (out,), _ = a.send('FTEST')
        self.assertEqual(out[:2], (1, A))
        self.assertEqual(fields(out[2])[2], 'FSEEN')
        self.assertEqual(a.send('FTEST', index=1)[0], [])              # only on P

    def test_instances_pair_keys_bye_and_expiry(self):
        a, b = Client(self.server, 1, A), Client(self.server, 2, B)
        a.hello(), b.hello()
        key1 = fields(a.ask(2)[0][0][2])[13]
        restarted = Client(self.server, 2, B, inst='5555555555555555')
        restarted.hello()
        f = fields(a.ask(2)[0][0][2])
        self.assertEqual(f[7], '5555555555555555')
        self.assertNotEqual(f[13], key1)                               # new instance, new pair key
        (out,), _ = restarted.send('BYE')
        self.assertEqual(fields(out[2])[2], 'GONE')
        self.assertEqual(fields(a.ask(2)[0][0][2])[2], 'WAIT')
        a.now += 1000                                                  # beyond expiry (300 s)
        self.assertEqual(a.ask(2)[0], [])                              # a itself expired
        self.assertNotIn(1, self.server.registrations)

    def test_rate_limits_per_source_and_allow_list(self):
        server = rv.Rendezvous(TOKEN, clients=2, rate=5, burst=5)
        a = Client(server, 1, A)
        answered = sum(bool(a.hello()[0]) for _ in range(20))
        self.assertEqual(answered, 5)
        self.assertEqual(server.counters['rate_limited'], 15)
        other = Client(server, 2, B)
        self.assertTrue(other.hello()[0])                              # another source has its own bucket
        allowed = rv.Rendezvous(TOKEN, clients=2, allow={'203.0.113.2'})
        self.assertEqual(Client(allowed, 1, A).hello()[0], [])
        self.assertTrue(Client(allowed, 2, B).hello()[0])

    def test_constructor_and_address_checks(self):
        for token in ('', 'A'*32, '0'*31):
            with self.assertRaises(ValueError):
                rv.Rendezvous(token)
        with self.assertRaises(ValueError):
            rv.Rendezvous(TOKEN, clients=9)
        self.assertEqual(rv.parse_addr('-'), None)
        self.assertEqual(rv.parse_addr('1.2.3.4:5'), ('1.2.3.4', 5))
        for bad in ('1.2.3.4', '1.2.3.4:0', '1.2.3.04:5', '0.0.0.0:5', '1.2.3.4:65536', '1.2.3:4'):
            with self.assertRaises(ValueError):
                rv.parse_addr(bad)


def free_ports(count):
    """count consecutive free UDP ports on loopback."""
    for _ in range(50):
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(('127.0.0.1', 0))
        base = probe.getsockname()[1]
        probe.close()
        if base + count > 65535:
            continue
        sockets = []
        try:
            for port in range(base, base+count):
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.bind(('127.0.0.1', port))
                sockets.append(s)
            return base
        except OSError:
            continue
        finally:
            for s in sockets:
                s.close()
    raise RuntimeError('no free ports')


class RelayFlag(unittest.TestCase):
    def relay(self, *extra):
        return subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--token', TOKEN, '--ticks', '5',
                                 '--report', str(Path(self.tmp)/'report.json'), *extra],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def setUp(self):
        import tempfile
        self.tmp = tempfile.mkdtemp()

    def ask(self, port, timeout=1.0):
        held = []
        while True:     # never the target port itself (a just-freed port can come back as ephemeral)
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.bind(('127.0.0.1', 0))
            if s.getsockname()[1] not in (port-1, port, port+1):
                break
            held.append(s)
        for h in held:
            h.close()
        s.settimeout(timeout)
        try:
            lan = f'127.0.0.1:{s.getsockname()[1]}'
            s.sendto(rv.request(KEY, 'HELLO', 1, INST[1], 1, lan, '-', 'unknown', 'open'), ('127.0.0.1', port))
            return s.recvfrom(2048)
        except (socket.timeout, ConnectionResetError):
            return None
        finally:
            s.close()

    def test_rendezvous_runs_beside_the_barrier_only_when_asked(self):
        base = free_ports(3)
        process = self.relay('--port', str(base), '--rendezvous-port', str(base+1))
        try:
            self.assertEqual(process.stdout.readline().strip(), f'NATIVE_GATE RENDEZVOUS ports={base+1},{base+2}')
            self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
            reply = self.ask(base+1)
            self.assertIsNotNone(reply)
            f = fields(reply[0])
            self.assertEqual(f[2], 'SEEN')
            self.assertEqual(f[8], 'open')                             # loopback relay: 127/8 LAN accepted
            self.assertIsNone(self.ask(base, timeout=0.3))             # the barrier port does not speak NGNAT
        finally:
            process.kill()
            process.communicate()
        base = free_ports(3)
        process = self.relay('--port', str(base))
        try:
            self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
            self.assertIsNone(self.ask(base+1, timeout=0.3))           # default off: nothing listens on P
        finally:
            process.kill()
            process.communicate()

    def test_rendezvous_port_must_not_overlap_the_relay_port(self):
        for value in ('5000', '4999', '80'):
            process = self.relay('--port', '5000', '--rendezvous-port', value)
            _, err = process.communicate(timeout=30)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn('--rendezvous-port', err)


if __name__ == '__main__':
    unittest.main()
