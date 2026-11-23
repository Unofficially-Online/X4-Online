"""netifs (netplay package 2): this PC's IPv4 networks and the order a host's candidate addresses are tried in.

The filter, the `ip -o -4 addr` and SIOCGIFCONF parsers and the GetAdaptersAddresses walk run on made-up inputs (the
Windows walk over a linked list built with ctypes, so it runs on every system). One live smoke reads this PC's own
interfaces; nothing is sent anywhere. All addresses below are private or documentation ranges."""
import ctypes
import socket
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import netifs  # noqa: E402


def entry(ip, prefix, name='test'):
    return {'ip': ip, 'netmask': netifs.prefix_netmask(prefix), 'prefix': prefix, 'name': name,
            'broadcast': None}


class UsableRows(unittest.TestCase):
    def test_loopback_and_link_local_are_left_out_and_virtual_lans_stay(self):
        rows = [('lo', '127.0.0.1', 8, False),
                ('Ethernet', '192.168.1.20', 24, True),
                ('no DHCP', '169.254.10.20', 16, True),
                ('Hamachi', '25.1.2.3', 8, True),
                ('Radmin VPN', '26.4.5.6', 8, True),
                ('Tailscale', '100.101.102.103', 32, False),
                ('broken', '0.0.0.0', 8, True),
                ('broken', '224.0.0.1', 4, True),
                ('broken', 'not an address', 24, True)]
        found = netifs.usable_rows(rows)
        self.assertEqual([item['ip'] for item in found], ['192.168.1.20', '25.1.2.3', '26.4.5.6', '100.101.102.103'])
        self.assertEqual(set(found[0]), {'ip', 'netmask', 'broadcast', 'name', 'prefix'})

    def test_broadcast_only_on_networks_that_have_one(self):
        rows = [('cable', '192.168.1.20', 24, True), ('vlan', '10.20.0.5', 22, True), ('wg', '10.8.0.2', 32, True),
                ('p2p', '10.9.0.1', 31, True), ('tun', '10.30.0.1', 24, False)]
        by_name = {item['name']: item for item in netifs.usable_rows(rows)}
        self.assertEqual(by_name['cable']['broadcast'], '192.168.1.255')
        self.assertEqual(by_name['cable']['netmask'], '255.255.255.0')
        self.assertEqual(by_name['vlan']['broadcast'], '10.20.3.255')
        self.assertIsNone(by_name['wg']['broadcast'])
        self.assertIsNone(by_name['p2p']['broadcast'])
        self.assertIsNone(by_name['tun']['broadcast'])

    def test_repeats_and_bad_prefixes(self):
        rows = [('a', '192.168.1.20', 24, True), ('b', '192.168.1.20', 24, True), ('c', '10.0.0.5', 0, True),
                ('d', '10.0.0.6', None, True)]
        found = netifs.usable_rows(rows)
        self.assertEqual([item['name'] for item in found], ['a', 'c', 'd'])
        self.assertEqual(found[1]['prefix'], 32)      # a /0 would make every address "local"
        self.assertEqual(found[2]['prefix'], 32)


class Parsers(unittest.TestCase):
    IP_OUTPUT = (
        '1: lo    inet 127.0.0.1/8 scope host lo\\       valid_lft forever preferred_lft forever\n'
        '2: eth0    inet 192.168.1.20/24 brd 192.168.1.255 scope global dynamic noprefixroute eth0\\       '
        'valid_lft 85000sec preferred_lft 85000sec\n'
        '2: eth0    inet 192.168.1.21/24 brd 192.168.1.255 scope global secondary eth0\\       valid_lft forever\n'
        '4: eth0.20@eth0    inet 10.20.0.5/24 brd 10.20.0.255 scope global eth0.20\\       valid_lft forever\n'
        '6: tailscale0    inet 100.101.102.103/32 scope global tailscale0\\       valid_lft forever\n'
        '9: ppp0    inet 10.64.0.9 peer 10.64.0.1/32 scope global ppp0\\       valid_lft forever\n'
        'garbage line\n')

    def test_ip_addr_output(self):
        rows = netifs.parse_ip_addr(self.IP_OUTPUT)
        self.assertEqual(rows, [('lo', '127.0.0.1', 8, False),
                                ('eth0', '192.168.1.20', 24, True),
                                ('eth0', '192.168.1.21', 24, True),
                                ('eth0.20', '10.20.0.5', 24, True),
                                ('tailscale0', '100.101.102.103', 32, False),
                                ('ppp0', '10.64.0.9', 32, False)])

    def test_ifconf_buffer(self):
        size = netifs.ifreq_size()

        def ifreq(name, family, ip):
            sockaddr = struct.pack('=HH4s', family, 0, socket.inet_aton(ip))
            return name.encode().ljust(netifs.IFNAMSIZ, b'\0') + sockaddr.ljust(size - netifs.IFNAMSIZ, b'\0')
        data = ifreq('lo', socket.AF_INET, '127.0.0.1') + ifreq('eth0', socket.AF_INET, '192.168.1.20') + \
            ifreq('odd', 99, '10.0.0.1') + ifreq('eth0:1', socket.AF_INET, '192.168.1.21')
        self.assertEqual(netifs.parse_ifconf(data),
                         [('lo', '127.0.0.1'), ('eth0', '192.168.1.20'), ('eth0:1', '192.168.1.21')])

    def test_netmask_and_dotted_quad_helpers(self):
        self.assertEqual(netifs.netmask_prefix('255.255.252.0'), 22)
        self.assertIsNone(netifs.netmask_prefix('255.0.255.0'))
        self.assertIsNone(netifs.parse_ipv4('010.0.0.1'))
        self.assertIsNone(netifs.parse_ipv4('host.example'))
        self.assertIsNone(netifs.parse_ipv4('1.2.3.256'))
        self.assertEqual(str(netifs.parse_ipv4('10.0.0.1')), '10.0.0.1')


class WindowsAdapterWalk(unittest.TestCase):
    """The GetAdaptersAddresses list walk over a list built in ctypes memory, with the real structure layouts."""

    def build(self):
        socket_address, unicast_type, adapter_type = netifs._windows_structures()
        keep = []

        def sockaddr(ip):
            raw = ctypes.create_string_buffer(struct.pack('<HH4s8x', netifs.AF_INET, 0, socket.inet_aton(ip)), 16)
            keep.append(raw)
            return socket_address(ctypes.cast(raw, ctypes.c_void_p), 16)

        def unicasts(items):
            first = None
            for ip, prefix, dad in reversed(items):
                node = unicast_type()
                node.Address = sockaddr(ip)
                node.OnLinkPrefixLength = prefix
                node.DadState = dad
                if first is not None:
                    node.Next = ctypes.pointer(first)
                keep.append(node)
                first = node
            return ctypes.pointer(first) if first is not None else None

        adapters = [('Ethernet', 1, 6, [('192.168.1.20', 24, 4), ('192.168.1.99', 24, 2)]),
                    ('Loopback Pseudo-Interface 1', 1, netifs.IF_TYPE_SOFTWARE_LOOPBACK, [('127.0.0.1', 8, 4)]),
                    ('Wi-Fi', 2, 71, [('10.0.0.7', 24, 4)]),          # down: left out
                    ('ZeroTier One', 1, 6, [('10.147.17.5', 24, 3)])]
        first = None
        for name, status, kind, items in reversed(adapters):
            node = adapter_type()
            node.FriendlyName = name
            node.OperStatus = status
            node.IfType = kind
            pointer = unicasts(items)
            if pointer is not None:
                node.FirstUnicastAddress = pointer
            if first is not None:
                node.Next = ctypes.pointer(first)
            keep.append(node)
            first = node
        return ctypes.pointer(first), keep

    def test_walk(self):
        first, keep = self.build()
        rows = netifs.adapter_rows(first)
        self.assertEqual(rows, [('Ethernet', '192.168.1.20', 24, True), ('ZeroTier One', '10.147.17.5', 24, True)])
        self.assertTrue(keep)


class BestFor(unittest.TestCase):
    HOST = [entry('172.20.16.1', 20, 'vEthernet (WSL)'), entry('10.20.0.5', 24, 'cable VLAN 20'),
            entry('100.101.102.103', 32, 'Tailscale'), entry('25.1.2.3', 8, 'Hamachi')]

    def test_a_host_puts_its_address_on_the_joiners_subnet_first(self):
        candidates = [item['ip'] for item in self.HOST]
        self.assertEqual(netifs.best_for('10.20.0.77', candidates, self.HOST)[0], '10.20.0.5')
        self.assertEqual(netifs.best_for('25.9.9.9', candidates, self.HOST)[0], '25.1.2.3')

    def test_a_routed_vlan_prefers_the_closest_address(self):
        candidates = [item['ip'] for item in self.HOST]
        # 10.30.0.0/24 is not one of the host's subnets: the address sharing the longest prefix comes first.
        self.assertEqual(netifs.best_for('10.30.0.7', candidates, self.HOST)[0], '10.20.0.5')

    def test_a_joiner_tries_the_announcing_address_then_its_own_subnets(self):
        joiner = [entry('192.168.56.101', 24, 'host-only'), entry('10.30.0.7', 24, 'cable VLAN 30')]
        announced = ['172.20.16.1', '192.168.56.1', '10.20.0.5', 'junk', '10.20.0.5']
        order = netifs.best_for('10.20.0.5', announced, joiner)
        self.assertEqual(order, ['10.20.0.5', '192.168.56.1', '172.20.16.1'])

    def test_without_a_peer_only_local_subnets_count(self):
        joiner = [entry('10.30.0.7', 24)]
        self.assertEqual(netifs.best_for(None, ['203.0.113.5', '10.30.0.9', '198.51.100.7'], joiner),
                         ['10.30.0.9', '203.0.113.5', '198.51.100.7'])

    def test_same_lan(self):
        joiner = [entry('192.168.1.30', 24)]
        self.assertEqual(netifs.best_for('192.168.1.20', ['10.147.17.5', '192.168.1.20'], joiner),
                         ['192.168.1.20', '10.147.17.5'])


class LiveSmoke(unittest.TestCase):
    def test_this_pc_has_an_address(self):
        """Reads the local interface list only. A PC without any network (or a sandbox) has nothing to list."""
        found = netifs.addresses()
        if not found:
            self.skipTest('this PC reports no usable IPv4 address (no network up, or a sandbox without interfaces)')
        for item in found:
            self.assertIsNotNone(netifs.parse_ipv4(item['ip']))
            self.assertFalse(item['ip'].startswith('127.'))
            self.assertFalse(item['ip'].startswith('169.254.'))
            self.assertEqual(netifs.prefix_netmask(item['prefix']), item['netmask'])


if __name__ == '__main__':
    unittest.main()
