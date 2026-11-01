"""NAT traversal for the optional direct player links (roadmap B7), C client side.

Builds src/native/engine/test_lockstep_nat.c and test_lockstep_nat_tool.c (Zig 0.15.2 from
X4LAN_ZIG or PATH, else cc on Linux) and runs:
  * the C unit tests;
  * the C core against simulated home routers in virtual time (tools/dev/nat_sim.py):
    every pair of NAT types, port mappings, players behind one router, carrier-grade NAT,
    loss, keep-alives, lost paths, closing, forged and replayed probes;
  * two real links on loopback through native_gate_relay.py --rendezvous-port;
  * PCP / NAT-PMP / UPnP against the loopback fakes of tools/dev/nat_fake_gateway.py.
No real router or remote host is contacted.
"""
import itertools
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT/'src/native/engine'
sys.path.insert(0, str(ROOT/'tools/dev'))
import nat_sim  # noqa: E402
import nat_fake_gateway  # noqa: E402

FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']
BUILD = {}


def toolchain():
    zig = os.environ.get('X4LAN_ZIG') or shutil.which('zig')
    if zig:
        try:
            if subprocess.check_output([zig, 'version'], text=True).strip() == '0.15.2':
                target = 'x86_64-windows-gnu' if os.name == 'nt' else 'x86_64-linux-gnu'
                return zig, [zig, 'cc', '-target', target], ('.exe' if os.name == 'nt' else '')
        except OSError:
            pass
    if os.name != 'nt':
        cc = shutil.which('cc') or shutil.which('gcc')
        if cc:
            return None, [cc], ''
    return None, None, None


def built():
    """(tool, unit test) executables, compiled once per test run."""
    if not BUILD:
        zig, cc, ext = toolchain()
        if not cc:
            raise unittest.SkipTest('needs Zig 0.15.2 (X4LAN_ZIG) or a C compiler')
        out = Path(tempfile.mkdtemp(prefix='x4nat-'))
        libs = ['-lbcrypt', '-lws2_32'] if os.name == 'nt' else []
        extra = ['-fms-extensions', '-mcx16'] if os.name == 'nt' else []
        for name, source in (('tool', 'test_lockstep_nat_tool.c'), ('unit', 'test_lockstep_nat.c')):
            exe = out/f'nat_{name}{ext}'
            subprocess.run(cc + FLAGS + extra + [str(ENGINE/source)] + libs + ['-o', str(exe)], check=True)
            BUILD[name] = exe
        BUILD['zig'] = zig
    return BUILD


def expected(a, b):
    """Hole punching without port prediction (the report's table): an endpoint-dependent
    mapping meets another one or port-dependent filtering."""
    unpunchable = {('symmetric', 'symmetric'), ('symmetric', 'port_restricted'), ('port_restricted', 'symmetric')}
    return 'relay' if (a, b) in unpunchable else 'direct'


class Unit(unittest.TestCase):
    def test_c_unit_tests(self):
        result = subprocess.run([str(built()['unit'])], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('test_lockstep_nat: ok', result.stdout)

    def test_linux_build(self):
        zig = built()['zig']
        if not zig or os.name != 'nt':
            self.skipTest('cross-check from Windows with Zig')
        with tempfile.TemporaryDirectory() as tmp:
            for source in ('test_lockstep_nat_tool.c', 'test_lockstep_nat.c'):
                subprocess.run([zig, 'cc', '-target', 'x86_64-linux-gnu.2.17', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ENGINE/source), '-o', str(Path(tmp)/'out')], check=True)

    def test_the_module_compiles_inside_both_gates(self):
        """Integration preview for the direct links: the headers next to the full Windows gate
        (lockstep_gate.c) and Linux gate (gate_linux.c) translation units, with their flags."""
        zig = built()['zig']
        if not zig:
            self.skipTest('needs Zig')
        probe = ('int x4gate_nat_probe(void) {nat_link l;nat_config c;nat_config_defaults(&c);'
                 'int r=nat_link_open(&l,&c,0,0)!=0;nat_link_close(&l);return r;}\n')
        with tempfile.TemporaryDirectory() as tmp:
            for name, gate, flags in (
                    ('win', 'lockstep_gate.c', ['-target', 'x86_64-windows-gnu', '-mcx16', '-std=c11', '-fms-extensions']),
                    ('linux', 'linux/gate_linux.c', ['-target', 'x86_64-linux-gnu.2.17', '-std=gnu11', '-Wno-misleading-indentation',
                                                     '-mcx16', '-fPIC', '-fvisibility=hidden', '-I', str(ENGINE/'linux')])):
                unit = Path(tmp)/f'gate_nat_{name}.c'
                unit.write_text(f'#include "{gate}"\n#include "lockstep_nat_net.h"\n{probe}')
                # Both gates include the wire crypto (lockstep_wire_crypto_gate.h: Monocypher) and the NAT core.
                monocypher = ROOT/'third_party/monocypher-4.0.2/src'
                subprocess.run([zig, 'cc', *flags, '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-I', str(ENGINE),
                                '-I', str(monocypher), '-I', str(monocypher/'optional'),
                                '-c', str(unit), '-o', str(Path(tmp)/f'{name}.o')], check=True)

    def test_expected_table(self):
        self.assertEqual(expected('symmetric', 'symmetric'), 'relay')
        self.assertEqual(expected('symmetric', 'port_restricted'), 'relay')
        self.assertEqual(expected('port_restricted', 'symmetric'), 'relay')
        self.assertEqual(expected('symmetric', 'restricted'), 'direct')
        self.assertEqual(expected('open', 'symmetric'), 'direct')
        self.assertEqual(expected('port_restricted', 'port_restricted'), 'direct')


class Simulated(unittest.TestCase):
    def setUp(self):
        self.scenarios = []

    def tearDown(self):
        for s in self.scenarios:
            s.shutdown()

    def scenario(self, **kw):
        s = nat_sim.Scenario(built()['tool'], **kw)
        self.scenarios.append(s)
        return s

    def place(self, s, slot, kind, **config):
        """A client behind its own NAT of this kind ('open': a public host without NAT)."""
        if kind == 'open':
            return s.client(slot, f'198.51.100.{100+slot}', **config), None
        nat = s.nat(f'nat{slot}', f'203.0.113.{slot}', kind)
        return s.client(slot, f'192.168.{slot}.10', nat, **config), nat

    def test_every_pair_of_nat_types(self):
        kinds = ('open', 'full_cone', 'restricted', 'port_restricted', 'symmetric')
        for a, b in itertools.combinations_with_replacement(kinds, 2):
            with self.subTest(a=a, b=b):
                s = self.scenario()
                self.place(s, 1, a)
                self.place(s, 2, b)
                decided = s.run_until_decided(30000)
                want = expected(a, b)
                for x, y in ((1, 2), (2, 1)):
                    state, addr, rtt, reason = s.path(x, y)
                    self.assertEqual(state, want, (a, b, x, s.hosts[x].driver.describe()))
                    if want == 'direct':
                        self.assertTrue(5 <= rtt <= 80, rtt)
                        self.assertLess(decided[(x, y)], 4000)
                    else:
                        self.assertEqual(reason, 'unpunchable_nat_pair')   # known at once, no punching window
                        self.assertLess(decided[(x, y)], 4000)
                s.shutdown()

    def test_a_port_mapping_rescues_pairs_hole_punching_cannot(self):
        for a, b in (('symmetric', 'symmetric'), ('port_restricted', 'symmetric'), ('symmetric', 'port_restricted')):
            with self.subTest(a=a, b=b):
                s = self.scenario()
                nat = s.nat('nat1', '203.0.113.1', a)
                s.client(1, '192.168.1.10', nat, mapped_port=6001)
                self.place(s, 2, b)
                s.run_until_decided(30000)
                self.assertEqual(s.path(1, 2)[0], 'direct')
                self.assertEqual(s.path(2, 1)[:2], ('direct', ('203.0.113.1', 6001)))
                s.shutdown()

    def test_players_behind_one_router(self):
        for share, hairpin, want in ((1, False, ('direct', ('192.168.1.11', 5000))), (0, True, ('direct', ('203.0.113.5', None))),
                                     (0, False, ('relay', None))):
            with self.subTest(share_lan=share, hairpin=hairpin):
                s = self.scenario()
                home = s.nat('home', '203.0.113.5', 'port_restricted', hairpin=hairpin)
                s.client(1, '192.168.1.10', home, sharelan=share)
                s.client(2, '192.168.1.11', home, sharelan=share)
                s.run_until_decided(30000)
                state, addr, _, reason = s.path(1, 2)
                self.assertEqual(state, want[0], reason)
                if want[1]:
                    self.assertEqual(addr[0], want[1][0])
                    if want[1][1]:
                        self.assertEqual(addr[1], want[1][1])
                s.shutdown()

    def test_lan_addresses_never_reach_players_elsewhere(self):
        s = self.scenario(clients=3)
        home = s.nat('home', '203.0.113.5', 'full_cone')
        s.client(1, '192.168.1.10', home)
        s.client(2, '192.168.1.11', home)
        self.place(s, 3, 'port_restricted')
        s.run_until_decided(30000)
        for t, name, source, data in s.network.trace:
            if name == 'client3':
                self.assertNotIn(b'192.168.1.', data)
        peers = [d for _, name, _, d in s.network.trace if name == 'client1' and d.startswith(b'NGNAT|1|PEER|')]
        self.assertTrue(any(b'|192.168.1.11:5000|' in d for d in peers))
        self.assertEqual(s.path(1, 2)[1], ('192.168.1.11', 5000))
        self.assertEqual(s.path(3, 1)[0], 'direct')

    def test_carrier_grade_nat(self):
        s = self.scenario(clients=3)
        cgn = s.nat('cgn', '203.0.113.100', 'port_restricted', hairpin=True)
        home1 = s.nat('home1', '100.64.0.2', 'full_cone', realm=cgn)
        home2 = s.nat('home2', '100.64.0.3', 'restricted', realm=cgn)
        s.client(1, '192.168.1.10', home1)
        s.client(2, '192.168.2.10', home2)             # another customer of the same carrier NAT
        self.place(s, 3, 'full_cone')
        s.run_until_decided(30000)
        self.assertEqual(s.path(1, 3)[0], 'direct')
        self.assertEqual(s.path(3, 1)[1][0], '203.0.113.100')
        state, addr, _, reason = s.path(1, 2)
        self.assertEqual((state, addr[0]), ('direct', '203.0.113.100'), reason)   # through the carrier NAT's hairpin

    def test_address_dependent_mapping_is_seen_as_eim_and_falls_back_by_timeout(self):
        """One relay address cannot tell adm from eim (needs a second IP); punching still fails safe."""
        s = self.scenario()
        self.place(s, 1, 'address_symmetric')
        self.place(s, 2, 'port_restricted')
        s.run_until_decided(30000)
        self.assertIn('mapping=eim', s.hosts[1].driver.describe())
        self.assertEqual(s.path(1, 2)[:1], ('relay',))
        self.assertEqual(s.path(1, 2)[3], 'punch_timeout')
        s2 = self.scenario()
        self.place(s2, 1, 'address_symmetric')
        self.place(s2, 2, 'full_cone')
        s2.run_until_decided(30000)
        self.assertEqual(s2.path(1, 2)[0], 'direct')

    def test_packet_loss(self):
        for seed in (1, 2, 3):
            with self.subTest(seed=seed):
                s = self.scenario(seed=seed, loss=0.3)
                self.place(s, 1, 'full_cone')
                self.place(s, 2, 'port_restricted')
                s.run_until_decided(60000)
                self.assertEqual((s.path(1, 2)[0], s.path(2, 1)[0]), ('direct', 'direct'))
                s.shutdown()

    def test_eight_players_of_every_kind(self):
        kinds = ['open', 'full_cone', 'restricted', 'port_restricted', 'symmetric', 'symmetric', 'port_restricted', 'restricted']
        s = self.scenario(clients=8)
        for slot, kind in enumerate(kinds, 1):
            self.place(s, slot, kind)
        s.run_until_decided(60000)
        for a, b in itertools.permutations(range(1, 9), 2):
            self.assertEqual(s.path(a, b)[0], expected(kinds[a-1], kinds[b-1]), (a, b, kinds[a-1], kinds[b-1]))
        self.assertEqual(s.rendezvous.counters['rate_limited'], 0)

    def test_keepalives_hold_the_mapping(self):
        s = self.scenario()
        n1 = s.nat('nat1', '203.0.113.1', 'port_restricted', timeout_ms=30000)
        n2 = s.nat('nat2', '203.0.113.2', 'port_restricted', timeout_ms=30000)
        s.client(1, '192.168.1.10', n1)
        s.client(2, '192.168.2.10', n2)
        s.run_until_decided(30000)
        for t in range(5000, 300000, 5000):
            s.run(t)
            self.assertEqual((s.path(1, 2)[0], s.path(2, 1)[0]), ('direct', 'direct'), t)
        self.assertEqual((n1.created, n2.created), (1, 1))   # one mapping each, never expired

    def test_a_lost_path_is_detected_and_punched_again(self):
        for kind in ('restricted', 'port_restricted'):
            with self.subTest(kind=kind):
                s = self.scenario()
                # Mappings expire before the 15 s keep-alive and come back on other ports.
                n1 = s.nat('nat1', '203.0.113.1', kind, timeout_ms=10000, allocation='sequential')
                n2 = s.nat('nat2', '203.0.113.2', kind, timeout_ms=10000, allocation='sequential')
                s.client(1, '192.168.1.10', n1)
                s.client(2, '192.168.2.10', n2)
                s.run_until_decided(30000)
                seen, recovered = set(), False
                for t in range(1000, 200000, 500):
                    s.run(t)
                    state, _, _, reason = s.path(1, 2)
                    seen.add((state, reason))
                    if ('relay', 'path_lost') in seen and state == 'direct':
                        recovered = True
                        break
                self.assertIn(('relay', 'path_lost'), seen)
                self.assertTrue(recovered, seen)
                s.shutdown()

    def test_a_peer_that_does_not_opt_in_stays_on_the_relay(self):
        s = self.scenario(clients=3)
        self.place(s, 1, 'full_cone')
        self.place(s, 2, 'restricted')
        decided = s.run_until_decided(40000, pairs=[(1, 2), (2, 1), (1, 3), (2, 3)])
        self.assertEqual(s.path(1, 2)[0], 'direct')
        for x in (1, 2):
            self.assertEqual(s.path(x, 3)[0::3], ('relay', 'peer_not_registered'))
            self.assertLess(decided[(x, 3)], 20000)
        self.assertEqual(sorted(s.rendezvous.registrations), [1, 2])

    def test_closing_tells_the_peer_and_the_relay(self):
        s = self.scenario()
        self.place(s, 1, 'full_cone')
        self.place(s, 2, 'port_restricted')
        s.run_until_decided(30000)
        self.assertEqual(s.path(2, 1)[0], 'direct')
        s.close(1)
        s.run(s.network.now + 200)
        self.assertEqual(s.path(2, 1)[0::3], ('relay', 'peer_closed'))
        self.assertEqual(s.path(1, 2)[0::3], ('relay', 'closed'))
        self.assertNotIn(1, s.rendezvous.registrations)
        s.run(s.network.now + 60000)
        self.assertEqual(s.path(2, 1)[0], 'relay')          # no new attempt towards a closed peer

    def test_forged_and_replayed_probes_get_no_answer(self):
        s = self.scenario()
        self.place(s, 1, 'full_cone')
        self.place(s, 2, 'full_cone')
        s.run_until_decided(30000)
        host = s.hosts[2]
        probe = next(d for _, name, _, d in s.network.trace if name == 'client2' and d.startswith(b'NGNAT|1|PROBE|1|2|'))
        attacker = ('198.51.100.66', 4444)
        now = s.network.now + 1
        sends, _ = host.driver.recv(now, attacker, probe)                     # replay from elsewhere
        self.assertEqual(sends, [])
        fresh = probe[:-49] + b'0123456789abcdef' + probe[-33:]                # new nonce, old MAC
        self.assertEqual(host.driver.recv(now, attacker, fresh)[0], [])
        tampered = probe[:-1] + (b'0' if probe[-1:] != b'0' else b'1')
        self.assertEqual(host.driver.recv(now, attacker, tampered)[0], [])
        hello = nat_sim.rendezvous_module.request(nat_sim.rendezvous_module.rendezvous_key('f'*32), 'PEER', 2, '0'*16, 1)
        self.assertEqual(host.driver.recv(now, (s.server.ip, s.server.ports[0]), hello)[0], [])
        self.assertEqual(s.path(2, 1)[0], 'direct')
        self.assertNotEqual(s.path(2, 1)[1], attacker)

    def test_a_new_port_mapping_is_advertised_at_once(self):
        s = self.scenario()
        host, nat = self.place(s, 1, 'symmetric')
        self.place(s, 2, 'symmetric')
        s.run_until_decided(30000)
        self.assertEqual(s.path(1, 2)[0], 'relay')
        nat.add_static((host.ip, host.port), 6001)
        host.driver.command(f'MAPPED 203.0.113.1:6001 {s.network.now}')
        s.network.schedule(s.network.now, 'poll', host, s.network.now)
        host.due = s.network.now
        s.run(s.network.now + 1000)
        self.assertEqual(s.rendezvous.registrations[1].mapped, ('203.0.113.1', 6001))


def free_port_pair():
    for _ in range(50):
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(('127.0.0.1', 0))
        base = probe.getsockname()[1]
        probe.close()
        held = []
        try:
            for port in (base, base+1, base+2):
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.bind(('127.0.0.1', port))
                held.append(s)
            return base
        except OSError:
            continue
        finally:
            for s in held:
                s.close()
    raise RuntimeError('no free ports')


class Loopback(unittest.TestCase):
    TOKEN = 'fedcba9876543210fedcba9876543210'

    def test_two_links_find_each_other_through_the_relay(self):
        tool = built()['tool']
        base = free_port_pair()
        with tempfile.TemporaryDirectory() as tmp:
            relay = subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', str(base), '--token', self.TOKEN,
                                      '--report', str(Path(tmp)/'r.json'), '--rendezvous-port', str(base+1)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertTrue(relay.stdout.readline().startswith('NATIVE_GATE RENDEZVOUS'))
                self.assertEqual(relay.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
                # Slot 1 holds its link for 6 s (--hold prints each path change); slot 2 stops once
                # both paths are direct and data went both ways, and closes.
                links = [subprocess.Popen([str(tool), 'link', '--server', f'127.0.0.1:{base+1}', '--token', self.TOKEN, '--slot', str(slot),
                                           '--clients', '2', '--seconds', seconds, '--bind', '127.0.0.1:0', '--loopback', '--fast', '--data', *hold],
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                         for slot, seconds, hold in ((1, '6', ['--hold']), (2, '15', []))]
                outputs = [link.communicate(timeout=60)[0] for link in links]
            finally:
                relay.kill()
                relay.communicate()
        ports = [int(next(l for l in out.splitlines() if l.startswith('LINK OPEN')).rsplit(':', 1)[1]) for out in outputs]
        path = next(l for l in outputs[1].splitlines() if l.startswith('PATH 1 '))
        self.assertEqual(path.split(' ')[2:4], ['direct', f'127.0.0.1:{ports[0]}'], outputs[1])
        changes = [l.split(' ', 2)[2] for l in outputs[0].splitlines() if l.startswith('CHANGE ')]
        self.assertEqual(changes, ['PATH 2 pending - 0 pending', f'PATH 2 direct 127.0.0.1:{ports[1]} ' + changes[1].split(' ')[4] + ' direct',
                                   'PATH 2 relay - 0 peer_closed'], outputs[0])
        for slot, out in ((1, outputs[0]), (2, outputs[1])):
            peer = 3 - slot
            self.assertIn(f'DATA {peer} hello from slot {peer}', out)
            self.assertIn('LINK CLOSED', out)


class PortMapping(unittest.TestCase):
    def run_tool(self, *args):
        result = subprocess.run([str(built()['tool']), 'portmap', *args], capture_output=True, text=True, timeout=60)
        return result.returncode, result.stdout.split('\n')[:-1]

    def gateway(self, mode='pcp', **kw):
        g = nat_fake_gateway.FakePcp(mode, **kw)
        self.addCleanup(g.close)
        return f'127.0.0.1:{g.address[1]}', g

    def igd(self, mode='grant', **kw):
        g = nat_fake_gateway.FakeIgd(mode, **kw)
        self.addCleanup(g.close)
        return f'127.0.0.1:{g.ssdp_address[1]}', g

    def common(self, methods, *extra):
        return ('--methods', methods, '--internal-port', '5000', '--lease', '600', '--timeout', '400', '--renew', '--release', *extra)

    def test_pcp_lease_renewal_and_release(self):
        address, g = self.gateway('pcp')
        code, out = self.run_tool('--gateway', address, *self.common('pcp,natpmp,upnp'))
        self.assertEqual((code, out), (0, ['MAPPED pcp 203.0.113.7:5000 600', 'RENEWED 203.0.113.7:5000 600', 'RELEASED']))
        self.assertEqual([r['lifetime'] for r in g.requests], [600, 600, 0])
        self.assertEqual(len({r['nonce'] for r in g.requests}), 1)           # one mapping nonce throughout
        self.assertEqual(g.requests[0]['client'], '127.0.0.1')
        self.assertEqual(g.mappings, {})

    def test_natpmp_when_the_gateway_does_not_speak_pcp(self):
        address, g = self.gateway('natpmp')
        code, out = self.run_tool('--gateway', address, *self.common('pcp,natpmp'))
        self.assertEqual(out, ['MAPPED natpmp 203.0.113.7:5000 600', 'RENEWED 203.0.113.7:5000 600', 'RELEASED'])
        self.assertEqual([(r['protocol'], r.get('op'), r.get('lifetime')) for r in g.requests],
                         [('pcp', None, 600), ('natpmp', 'address', None), ('natpmp', 'map', 600), ('natpmp', 'map', 600), ('natpmp', 'map', 0)])

    def test_refusal_and_silence(self):
        address, _ = self.gateway('refuse')
        self.assertEqual(self.run_tool('--gateway', address, *self.common('pcp,natpmp'))[1], ['REFUSED pcp:not_authorized'])
        address, _ = self.gateway('silent')
        started = time.monotonic()
        self.assertEqual(self.run_tool('--gateway', address, *self.common('pcp,natpmp'))[1], ['REFUSED pcp:no_reply'])
        self.assertLess(time.monotonic() - started, 5)
        ssdp, igd = self.igd('grant')
        code, out = self.run_tool('--gateway', address, '--ssdp', ssdp, *self.common('pcp,natpmp,upnp'))
        self.assertEqual(out[0], 'MAPPED upnp 203.0.113.9:5000 600')     # no PCP answer: UPnP

    def test_upnp_lease_renewal_release_and_error_handling(self):
        for mode, chunked, want in (('grant', False, 'MAPPED upnp 203.0.113.9:5000 600'), ('grant', True, 'MAPPED upnp 203.0.113.9:5000 600'),
                                    ('conflict_first', False, 'MAPPED upnp 203.0.113.9:5001 600'),
                                    ('permanent_only', False, 'MAPPED upnp 203.0.113.9:5000 0'),
                                    ('refuse', False, 'REFUSED upnp:error_606')):
            with self.subTest(mode=mode, chunked=chunked):
                ssdp, igd = self.igd(mode, chunked=chunked)
                code, out = self.run_tool('--ssdp', ssdp, *self.common('upnp'))
                self.assertEqual(out[0], want)
                adds = [a for a in igd.actions if a['action'] == 'AddPortMapping']
                if mode != 'refuse':
                    self.assertEqual(out[1:], [want.replace('MAPPED upnp', 'RENEWED'), 'RELEASED'])
                    self.assertEqual(adds[-1]['NewInternalClient'], '127.0.0.1')
                    self.assertEqual(adds[-1]['NewProtocol'], 'UDP')
                    self.assertEqual(igd.actions[-1]['action'], 'DeletePortMapping')
                    self.assertEqual(igd.mappings, {})
                self.assertTrue(any('InternetGatewayDevice' in search for search in igd.searches))

    def test_a_private_external_address_is_released_and_refused(self):
        address, g = self.gateway('pcp', external_ip='192.168.0.2')
        code, out = self.run_tool('--gateway', address, *self.common('pcp'))
        self.assertEqual(out, ['REFUSED pcp:double_nat'])
        self.assertEqual([r['lifetime'] for r in g.requests], [600, 0])
        ssdp, igd = self.igd('grant', external_ip='10.1.2.3')
        self.assertEqual(self.run_tool('--ssdp', ssdp, *self.common('upnp'))[1], ['REFUSED upnp:double_nat'])
        self.assertEqual([a['action'] for a in igd.actions][-2:], ['AddPortMapping', 'DeletePortMapping'])
        self.assertEqual(igd.mappings, {})

    def test_the_tool_only_talks_to_loopback(self):
        for args in (('--gateway', '192.168.1.1:5351'), ('--ssdp', '239.255.255.250:1900'), ()):
            code, out = self.run_tool(*args, '--methods', 'pcp,natpmp,upnp', '--internal-port', '5000')
            self.assertEqual((code, out), (2, []))


if __name__ == '__main__':
    unittest.main()
