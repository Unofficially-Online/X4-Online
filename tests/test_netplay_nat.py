"""nat (netplay package 2): port mapping with PCP, NAT-PMP and UPnP IGD, the echo, the probe and join codes.

Every gateway is a loopback fake from tools/dev/nat_fake_gateway.py and every master a loopback fake from
tests/netplay_fake_master.py; map_ports is always called with its targets named (gateway=, ssdp=), so no test sends
anything to a real router. The last class measures the lockstep relay's datagrams with the relay's own code
(tools/dev/native_gate_relay.py Barrier, no sockets) for the "Datagram sizes" note in docs/NETPLAY.md."""
import json
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tools' / 'dev'))
sys.path.insert(0, str(ROOT / 'tests'))
from lockstep.launcher import nat  # noqa: E402
from lockstep.launcher.masterclient import MasterClient, MasterError  # noqa: E402
import nat_fake_gateway  # noqa: E402
import native_gate_relay  # noqa: E402
from netplay_fake_master import FakeMaster  # noqa: E402

UDP_PORT = 47000
TCP_PORT = 47001
LOOPBACK = [{'ip': '127.0.0.1', 'netmask': '255.0.0.0', 'broadcast': None, 'name': 'lo', 'prefix': 8}]


def free_port(kind=socket.SOCK_DGRAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class Codes(unittest.TestCase):
    def test_normalize_code(self):
        self.assertEqual(nat.normalize_code('K7QX-3FNA'), 'K7QX-3FNA')
        self.assertEqual(nat.normalize_code(' k7qx3fna '), 'K7QX-3FNA')
        self.assertEqual(nat.normalize_code('k7qx 3fna'), 'K7QX-3FNA')
        self.assertEqual(nat.normalize_code('K7-QX-3F-NA'), 'K7QX-3FNA')
        for bad in ('K7QX-3FN', 'K7QX-3FNAA', 'K7QX-3FN0', 'K7QX-3FNI', 'X4MP-10.0.0.5-47000-' + 'a' * 32, '', None, 42):
            self.assertIsNone(nat.normalize_code(bad), bad)


class Messages(unittest.TestCase):
    def test_pcp_request_layout_and_the_fakes_answer(self):
        nonce = bytes(range(12))
        request = nat.pcp_map_request('127.0.0.1', nonce, 'tcp', TCP_PORT, TCP_PORT, 600)
        self.assertEqual(len(request), nat.PCP_MAP_SIZE)
        self.assertEqual(request[:2], b'\x02\x01')
        self.assertEqual(request[20:24], socket.inet_aton('127.0.0.1'))
        self.assertEqual(request[36], 6)
        gateway = nat_fake_gateway.FakePcp('pcp')
        self.addCleanup(gateway.close)
        reply = nat.pcp_map_parse(gateway.handle(request), nonce, 'tcp', TCP_PORT)
        self.assertEqual((reply['result'], reply['external_port'], reply['external_ip'], reply['lifetime']),
                         (0, TCP_PORT, '203.0.113.7', 600))
        self.assertIsNone(nat.pcp_map_parse(gateway.handle(request), bytes(12), 'tcp', TCP_PORT))   # other nonce
        self.assertIsNone(nat.pcp_map_parse(gateway.handle(request), nonce, 'udp', TCP_PORT))       # other protocol
        self.assertIsNone(nat.pcp_map_parse(b'\x02\x81' + bytes(10), nonce, 'tcp', TCP_PORT))      # too short

    def test_natpmp_messages(self):
        gateway = nat_fake_gateway.FakePcp('natpmp')
        self.addCleanup(gateway.close)
        address = nat.natpmp_address_parse(gateway.handle(nat.natpmp_address_request()))
        self.assertEqual(address['external_ip'], '203.0.113.7')
        reply = nat.natpmp_map_parse(gateway.handle(nat.natpmp_map_request('tcp', TCP_PORT, TCP_PORT, 600)), 'tcp',
                                     TCP_PORT)
        self.assertEqual((reply['result'], reply['external_port'], reply['lifetime']), (0, TCP_PORT, 600))
        self.assertIn(('tcp', TCP_PORT), gateway.mappings)
        version0 = nat.pcp_map_parse(gateway.handle(nat.pcp_map_request('127.0.0.1', bytes(12), 'udp', 1, 1, 1)),
                                     bytes(12), 'udp', 1)
        self.assertEqual(version0, {'version0': True, 'result': 1})

    def test_soap_envelope_and_fields(self):
        body = nat.soap_envelope(nat.WAN_SERVICES[1], 'AddPortMapping', [('NewExternalPort', 47000),
                                                                          ('NewPortMappingDescription', 'a<b&c')])
        self.assertIn('<NewExternalPort>47000</NewExternalPort>', body)
        self.assertIn('a&lt;b&amp;c', body)
        self.assertEqual(nat.xml_fields(body)['NewPortMappingDescription'], 'a<b&c')
        self.assertEqual(nat.xml_fields(b'<not xml'), {})

    def test_device_description(self):
        location = 'http://192.168.1.1:5000/rootDesc.xml'
        description = ('<root xmlns="urn:schemas-upnp-org:device-1-0"><URLBase>http://192.168.1.1:5000/base/</URLBase>'
                       '<device><serviceList>'
                       '<service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>'
                       '<controlURL>ppp</controlURL></service>'
                       '<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>'
                       '<controlURL>/ctl/IPConn</controlURL></service>'
                       '<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType>'
                       '<controlURL>http://198.51.100.7/elsewhere</controlURL></service>'
                       '</serviceList></device></root>')
        self.assertEqual(nat.igd_services(description, location),
                         [(nat.WAN_SERVICES[1], 'http://192.168.1.1:5000/ctl/IPConn'),
                          (nat.WAN_SERVICES[2], 'http://192.168.1.1:5000/base/ppp')])
        self.assertEqual(nat.igd_services('<broken', location), [])

    def test_ssdp_answers_and_locations(self):
        answer = (b'HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n'
                  b'location: http://192.168.1.1:5000/rootDesc.xml\r\n\r\n')
        self.assertEqual(nat.ssdp_headers(answer)['location'], 'http://192.168.1.1:5000/rootDesc.xml')
        self.assertIsNone(nat.ssdp_headers(b'NOTIFY * HTTP/1.1\r\n\r\n'))
        self.assertTrue(nat.location_valid('http://192.168.1.1:5000/rootDesc.xml', '192.168.1.1'))
        self.assertFalse(nat.location_valid('http://198.51.100.7/rootDesc.xml', '192.168.1.1'))
        self.assertFalse(nat.location_valid('http://router.example/rootDesc.xml', '192.168.1.1'))
        self.assertFalse(nat.location_valid('https://192.168.1.1/rootDesc.xml', '192.168.1.1'))

    def test_ssdp_goes_out_on_every_interface(self):
        interfaces = [{'ip': '192.168.1.20'}, {'ip': '10.20.0.5'}]
        self.assertEqual(nat._ssdp_sources(nat.SSDP_MULTICAST, interfaces), ['192.168.1.20', '10.20.0.5'])
        self.assertEqual(nat._ssdp_sources(nat.SSDP_MULTICAST, []), ['0.0.0.0'])
        self.assertEqual(nat._ssdp_sources(('127.0.0.1', 1900), interfaces), ['127.0.0.1'])   # tests stay on loopback
        sock = nat._open_ssdp_socket('127.0.0.1', nat.SSDP_MULTICAST)
        self.addCleanup(sock.close)
        chosen = sock.getsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, 4)
        self.assertEqual(chosen, socket.inet_aton('127.0.0.1'))
        self.assertEqual(sock.getsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL), nat.SSDP_TTL)

    def test_routing_tables(self):
        proc = ('Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n'
                'wlan0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n'
                'eth0\t00000000\t0100000A\t0003\t0\t0\t100\t00000000\t0\t0\t0\n'
                'eth0\t0000000A\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n')
        self.assertEqual(nat.parse_proc_route(proc), '10.0.0.1')
        self.assertIsNone(nat.parse_proc_route(proc.splitlines()[0]))

        def row(destination, mask, next_hop, metric):
            values = [destination, mask, bytes(4), next_hop] + [bytes(4)] * 5 + [struct.pack('<I', metric)] + \
                [bytes(4)] * 4
            return b''.join(values)
        table = struct.pack('<I', 3) + row(bytes(4), bytes(4), socket.inet_aton('192.168.1.1'), 35) + \
            row(socket.inet_aton('10.0.0.0'), socket.inet_aton('255.0.0.0'), bytes(4), 5) + \
            row(bytes(4), bytes(4), socket.inet_aton('192.168.2.1'), 50)
        self.assertEqual(nat.parse_forward_table(table), '192.168.1.1')
        self.assertIsNone(nat.parse_forward_table(b''))


class Fixture(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.state = Path(folder.name) / 'nat-state.json'

    def pcp(self, mode='pcp', **options):
        gateway = nat_fake_gateway.FakePcp(mode, **options)
        self.addCleanup(gateway.close)
        return gateway

    def igd(self, mode='grant', **options):
        device = nat_fake_gateway.FakeIgd(mode, **options)
        self.addCleanup(device.close)
        return device

    def map(self, gateway=None, device=None, methods=nat.METHODS, state=True, timeout=1.0):
        reasons = []
        lines = []
        mapping = nat.map_ports(UDP_PORT, TCP_PORT, lease=600, timeout=timeout, log=lines.append,
                                gateway=gateway.address if gateway else ('127.0.0.1', free_port()),
                                ssdp=device.ssdp_address if device else ('127.0.0.1', free_port()),
                                interfaces=LOOPBACK, methods=methods, state_file=self.state if state else None,
                                reasons=reasons)
        self.assertEqual(len(lines), 1)
        return mapping, reasons


class Pcp(Fixture):
    def test_grant_renew_release(self):
        gateway = self.pcp()
        mapping, _ = self.map(gateway)
        self.assertEqual((mapping.method, mapping.external_ip, mapping.udp_port, mapping.tcp_port, mapping.lease),
                         ('pcp', '203.0.113.7', UDP_PORT, TCP_PORT, 600))
        self.assertEqual(gateway.mappings, {('udp', UDP_PORT): UDP_PORT, ('tcp', TCP_PORT): TCP_PORT})
        self.assertEqual(len(json.loads(self.state.read_text())['pcp']), 2)
        self.assertTrue(mapping.renew())
        mapping.release()
        mapping.release()
        self.assertEqual(gateway.mappings, {})
        self.assertEqual(json.loads(self.state.read_text())['pcp'], {})
        rows = [(r['transport'], r['lifetime']) for r in gateway.requests]
        self.assertEqual(rows, [('udp', 600), ('tcp', 600), ('udp', 600), ('tcp', 600), ('udp', 0), ('tcp', 0)])
        udp_nonces = {r['nonce'] for r in gateway.requests if r['transport'] == 'udp'}
        self.assertEqual(len(udp_nonces), 1)                 # one nonce per mapping, for renewal and release
        self.assertEqual({r['client'] for r in gateway.requests}, {'127.0.0.1'})
        self.assertFalse(mapping.renew())

    def test_a_crashed_hosts_mapping_is_deleted_first(self):
        gateway = self.pcp(nonce_check=True)
        crashed, _ = self.map(gateway)
        old_nonces = {r['transport']: r['nonce'] for r in gateway.requests}
        del crashed                                          # no release: the launcher was killed
        mapping, _ = self.map(gateway)
        self.assertIsNotNone(mapping)
        deletes = [(r['transport'], r['nonce']) for r in gateway.requests[2:] if r['lifetime'] == 0]
        self.assertEqual(deletes, [('udp', old_nonces['udp']), ('tcp', old_nonces['tcp'])])
        mapping.release()
        self.assertEqual(gateway.mappings, {})

    def test_without_the_old_nonce_a_strict_server_refuses(self):
        gateway = self.pcp(nonce_check=True)
        self.map(gateway, state=False)
        mapping, reasons = self.map(gateway, state=False, methods=('pcp', 'natpmp'))
        self.assertIsNone(mapping)
        self.assertEqual(reasons[0], 'pcp: not_authorized')
        self.assertTrue(reasons[1].startswith('natpmp: skipped'))

    def test_refusal_silence_and_no_service(self):
        mapping, reasons = self.map(self.pcp('refuse'), methods=('pcp', 'natpmp'))
        self.assertIsNone(mapping)
        self.assertEqual(reasons[0], 'pcp: not_authorized')
        started = time.perf_counter()
        mapping, reasons = self.map(self.pcp('silent'), methods=('pcp', 'natpmp'), timeout=0.6)
        self.assertEqual(reasons[0], 'pcp: no_reply')
        self.assertLess(time.perf_counter() - started, 2.0)
        started = time.perf_counter()
        mapping, reasons = self.map(None, methods=('pcp',), timeout=2.0)    # a closed port: ICMP unreachable
        self.assertEqual(reasons, ['pcp: unreachable'])
        self.assertLess(time.perf_counter() - started, 1.5)

    def test_double_nat_is_refused_and_cleaned_up(self):
        gateway = self.pcp(external_ip='192.168.0.2')
        mapping, reasons = self.map(gateway, methods=('pcp',))
        self.assertIsNone(mapping)
        self.assertEqual(reasons, ['pcp: double_nat'])
        self.assertEqual(gateway.mappings, {})
        self.assertEqual([r['lifetime'] for r in gateway.requests], [600, 0])


class NatPmp(Fixture):
    def test_fallback_from_pcp_renew_release(self):
        gateway = self.pcp('natpmp')
        mapping, reasons = self.map(gateway)
        self.assertEqual(reasons, ['pcp: unsupported_version'])
        self.assertEqual((mapping.method, mapping.external_ip, mapping.udp_port, mapping.tcp_port),
                         ('natpmp', '203.0.113.7', UDP_PORT, TCP_PORT))
        self.assertTrue(mapping.renew())
        mapping.release()
        self.assertEqual(gateway.mappings, {})
        rows = [(r['protocol'], r.get('op'), r.get('transport'), r.get('lifetime')) for r in gateway.requests]
        self.assertEqual(rows, [('pcp', None, 'udp', 600),
                                ('natpmp', 'address', None, None),
                                ('natpmp', 'map', 'udp', 0), ('natpmp', 'map', 'udp', 600),     # stale delete, map
                                ('natpmp', 'map', 'tcp', 0), ('natpmp', 'map', 'tcp', 600),
                                ('natpmp', 'map', 'udp', 600), ('natpmp', 'map', 'tcp', 600),   # renew
                                ('natpmp', 'map', 'udp', 0), ('natpmp', 'map', 'tcp', 0)])      # release


class Upnp(Fixture):
    def test_grant_renew_release_plain_and_chunked(self):
        for chunked in (False, True):
            with self.subTest(chunked=chunked):
                device = self.igd(chunked=chunked)
                mapping, reasons = self.map(self.pcp('silent'), device, timeout=0.6)
                self.assertEqual(reasons, ['pcp: no_reply', 'natpmp: skipped (the gateway did not answer PCP as '
                                                            'a NAT-PMP router)'])
                self.assertEqual((mapping.method, mapping.external_ip, mapping.udp_port, mapping.tcp_port,
                                  mapping.lease), ('upnp', '203.0.113.9', UDP_PORT, TCP_PORT, 600))
                adds = [a for a in device.actions if a['action'] == 'AddPortMapping']
                self.assertEqual([(a['NewProtocol'], a['NewExternalPort'], a['NewInternalClient']) for a in adds],
                                 [('UDP', '47000', '127.0.0.1'), ('TCP', '47001', '127.0.0.1')])
                self.assertEqual(adds[0]['NewPortMappingDescription'], nat.UPNP_DESCRIPTION)
                self.assertTrue(mapping.renew())
                mapping.release()
                self.assertEqual(device.mappings, {})
                self.assertEqual([a['action'] for a in device.actions][-2:], ['DeletePortMapping'] * 2)
                self.assertTrue(any('InternetGatewayDevice' in search for search in device.searches))

    def test_conflict_permanent_only_and_refusal(self):
        mapping, _ = self.map(device=self.igd('conflict_first'), methods=('upnp',))
        self.assertEqual((mapping.udp_port, mapping.tcp_port), (UDP_PORT + 1, TCP_PORT))
        mapping.release()
        mapping, _ = self.map(device=self.igd('permanent_only'), methods=('upnp',))
        self.assertEqual(mapping.lease, 0)
        self.assertEqual(nat.renew_interval(mapping.lease), nat.PERMANENT_RECHECK)
        self.assertTrue(mapping.renew())
        mapping.release()
        mapping, reasons = self.map(device=self.igd('refuse'), methods=('upnp',))
        self.assertIsNone(mapping)
        self.assertEqual(reasons, ['upnp: error_606'])

    def test_a_stale_mapping_of_ours_is_deleted_and_a_foreign_one_kept(self):
        device = self.igd()
        device.mappings[('UDP', '47000')] = {'NewInternalClient': '127.0.0.1', 'NewInternalPort': '47000',
                                             'NewPortMappingDescription': nat.UPNP_DESCRIPTION}
        foreign = {'NewInternalClient': '192.168.1.50', 'NewInternalPort': '47001', 'NewPortMappingDescription': 'NAS'}
        device.mappings[('TCP', '47001')] = foreign
        mapping, _ = self.map(device=device, methods=('upnp',))
        self.assertEqual((mapping.udp_port, mapping.tcp_port), (UDP_PORT, TCP_PORT + 1))
        actions = [(a['action'], a.get('NewProtocol'), a.get('NewExternalPort')) for a in device.actions
                   if a['action'] != 'GET']
        delete_at = actions.index(('DeletePortMapping', 'UDP', '47000'))
        self.assertLess(delete_at, actions.index(('AddPortMapping', 'UDP', '47000')))
        self.assertNotIn(('DeletePortMapping', 'TCP', '47001'), actions)
        mapping.release()
        self.assertEqual(device.mappings, {('TCP', '47001'): foreign})

    def test_double_nat_and_other_services(self):
        device = self.igd(external_ip='10.1.2.3')
        mapping, reasons = self.map(device=device, methods=('upnp',))
        self.assertIsNone(mapping)
        self.assertEqual(reasons, ['upnp: double_nat'])
        self.assertFalse([a for a in device.actions if a['action'] == 'AddPortMapping'])
        for service, version in (('urn:schemas-upnp-org:service:WANPPPConnection:1', 1),
                                 ('urn:schemas-upnp-org:service:WANIPConnection:2', 2)):
            with self.subTest(service=service):
                device = self.igd(service=service, version=version)
                mapping, _ = self.map(device=device, methods=('upnp',))
                self.assertEqual(mapping.method, 'upnp')
                mapping.release()
                self.assertEqual(device.mappings, {})

    def test_an_answer_naming_another_host_is_ignored(self):
        liar = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        liar.bind(('127.0.0.1', 0))
        self.addCleanup(liar.close)

        def answer():
            data, source = liar.recvfrom(2048)
            liar.sendto(b'HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n'
                        b'LOCATION: http://198.51.100.7:80/rootDesc.xml\r\n\r\n', source)
        threading.Thread(target=answer, daemon=True).start()
        self.assertEqual(nat.discover_igds(0.6, liar.getsockname(), LOOPBACK), [])


class Renewal(unittest.TestCase):
    def test_keep_mapped_renews_until_stopped(self):
        class Counting:
            lease = 0.2
            released = False
            renewals = 0

            def renew(self):
                self.renewals += 1
                return True
        mapping = Counting()
        stop = threading.Event()
        with mock.patch.object(nat, 'MIN_RENEW', 0.05):
            thread = nat.keep_mapped(mapping, stop)
            time.sleep(0.45)
            stop.set()
            thread.join(2)
        self.assertFalse(thread.is_alive())
        self.assertGreaterEqual(mapping.renewals, 2)
        self.assertEqual(nat.renew_interval(3600), 1800)
        self.assertEqual(nat.renew_interval(20), nat.MIN_RENEW)


class EchoAndProbe(unittest.TestCase):
    def setUp(self):
        self.master = FakeMaster()
        self.addCleanup(self.master.close)
        self.client = MasterClient(self.master.base_url, timeout=5)

    def test_public_endpoint(self):
        echo = self.client.echo_address()
        port = free_port()
        ip, seen_port, rtt_ms = nat.public_endpoint(echo, local_port=port, bind='127.0.0.1')
        self.assertEqual((ip, seen_port), ('127.0.0.1', port))
        self.assertGreaterEqual(rtt_ms, 0)
        self.assertLess(rtt_ms, 1000)
        answer = nat.public_endpoint(self.master.base_url, echo_port=echo[1], bind='127.0.0.1')
        self.assertEqual(answer[0], '127.0.0.1')
        request = self.master.echo.seen[0][0]
        self.assertEqual(len(request), nat.ECHO_SIZE)
        self.master.echo.silent = True
        started = time.perf_counter()
        self.assertIsNone(nat.public_endpoint(echo, timeout=0.6, bind='127.0.0.1'))
        self.assertLess(time.perf_counter() - started, 1.5)

    def test_echo_reply_parsing(self):
        nonce = '0123456789abcdef'
        self.assertEqual(nat.parse_echo_reply(f'X4MPSEEN|1|{nonce}|198.51.100.7:40001|....'.encode()),
                         (nonce, '198.51.100.7', 40001))
        self.assertEqual(nat.parse_echo_reply(f'X4MPSEEN|1|{nonce}|198.51.100.7:40001...'.encode()),
                         (nonce, '198.51.100.7', 40001))
        for bad in (f'X4MPSEEN|2|{nonce}|198.51.100.7:40001', f'X4MPSEEN|1|{nonce[:-1]}|198.51.100.7:40001',
                    f'X4MPSEEN|1|{nonce}|198.51.100.7:0', f'X4MPSEEN|1|{nonce}|host:40001', 'X4MPSEEN|1'):
            self.assertIsNone(nat.parse_echo_reply(bad.encode()), bad)

    def test_reachable(self):
        udp_port = free_port()
        tcp_port = free_port(socket.SOCK_STREAM)
        self.assertEqual(nat.reachable(self.client, udp_port, tcp_port, timeout=2, bind='127.0.0.1'),
                         {'tcp': True, 'udp': True, 'ip': '127.0.0.1'})
        # The lobby already listens on the TCP port: it answers the connect, nothing is taken over.
        lobby = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        lobby.bind(('127.0.0.1', tcp_port))
        lobby.listen(1)
        self.addCleanup(lobby.close)
        self.assertTrue(nat.reachable(self.client, udp_port, tcp_port, timeout=2, bind='127.0.0.1')['tcp'])
        # Another program holds the UDP port: the probe cannot be seen.
        holder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        holder.bind(('127.0.0.1', udp_port))
        self.addCleanup(holder.close)
        self.assertFalse(nat.reachable(self.client, udp_port, tcp_port, timeout=1, bind='127.0.0.1')['udp'])

    def test_the_probe_rate_limit_reaches_the_caller(self):
        tcp_port = free_port(socket.SOCK_STREAM)
        for _ in range(6):
            nat.reachable(self.client, free_port(), tcp_port, timeout=0.2, bind='127.0.0.1')
        with self.assertRaises(MasterError) as caught:
            nat.reachable(self.client, free_port(), tcp_port, timeout=0.2, bind='127.0.0.1')
        self.assertEqual((caught.exception.code, caught.exception.status), ('rate_limited', 429))


class DatagramSizes(unittest.TestCase):
    """The lockstep relay's largest datagrams, from its own Barrier code with maximal blobs (docs/NETPLAY.md,
    "Datagram sizes"). Payload bytes, without the IP and UDP headers."""
    TOKEN = '0123456789abcdef' * 2
    PAYLOAD_LIMIT = 1200

    def report(self, protocol, slot, tick, clients, lookahead, blob):
        clock = struct.pack('>d', tick / 60).hex()
        fields = ['NGSTATE', str(protocol), self.TOKEN, str(slot), str(tick), clock, clock, 'd' * 64, str(clients)]
        if protocol == 4:
            fields.append(str(lookahead))
        fields.append(blob)
        return '|'.join(fields).encode('ascii')

    def largest_permit(self, protocol, clients, lookahead):
        barrier = native_gate_relay.Barrier(self.TOKEN, ticks=1000, clients=clients, lookahead=lookahead)
        blob = 'e' * native_gate_relay.BLOB_HEX_MAX
        packets = []
        for slot in range(1, clients + 1):
            report = self.report(protocol, slot, 0, clients, lookahead, blob)
            packets += barrier.receive(report, ('127.0.0.1', 50000 + slot), 0.0)
        self.assertTrue(packets)
        return max(len(packet) for _, packet in packets)

    def test_reports_stay_small_and_permits_grow_with_the_players(self):
        blob = 'e' * native_gate_relay.BLOB_HEX_MAX
        # A 20-digit tick (the uint64 maximum) gives the largest NGSTATE|4 lockstep_wire.h names; 4 h at 60 Hz is 6.
        self.assertEqual(len(self.report(4, 8, 2 ** 64 - 1, 8, 8, blob)), 649)
        self.assertEqual(len(self.report(4, 8, 864000, 8, 8, blob)), 635)
        self.assertLessEqual(len(self.report(4, 8, 2 ** 64 - 1, 8, 8, blob)), self.PAYLOAD_LIMIT)
        sizes = {}
        for clients in range(2, 9):
            sizes[clients] = (self.largest_permit(3, clients, 0), self.largest_permit(4, clients, 1))
            head = len(f'NGGO|4|{self.TOKEN}|{clients}|2|1')
            self.assertEqual(sizes[clients][1], head + clients * (1 + native_gate_relay.BLOB_HEX_MAX))
        self.assertLessEqual(max(sizes[2]), self.PAYLOAD_LIMIT)
        self.assertGreater(min(sizes[3]), self.PAYLOAD_LIMIT)    # three full blobs no longer fit
        self.assertEqual(sizes[2], (1005, 1007))                 # permits of ticks 1 and 2: +1 byte per tick digit
        self.assertEqual(sizes[8], (3891, 3893))
        # Typical play: a 28-byte pilot record per player (56 hex) keeps even eight players far below the limit.
        barrier = native_gate_relay.Barrier(self.TOKEN, ticks=1000, clients=8, lookahead=1)
        packets = []
        for slot in range(1, 9):
            packets += barrier.receive(self.report(4, slot, 0, 8, 1, 'a' * 56), ('127.0.0.1', 50000 + slot), 0.0)
        self.assertLess(max(len(packet) for _, packet in packets), 600)


if __name__ == '__main__':
    unittest.main()
