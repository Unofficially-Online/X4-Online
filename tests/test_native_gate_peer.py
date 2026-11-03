"""Peer links (roadmap B6): wire, client rules, relay extension 1, and the simulated sessions (no game)."""
import importlib.util
import io
from pathlib import Path
import contextlib
import json
import random
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
import native_gate_peer as peer  # noqa: E402
import native_gate_peer_sim as sim  # noqa: E402

relay = sim.relay
_spec = importlib.util.spec_from_file_location('native_gate_lookahead_tests_for_peer', ROOT/'tests/test_native_gate_lookahead.py')
lookahead_tests = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lookahead_tests)
TOKEN = 'a'*32
RELAY = ('10.0.0.100', 9000)


def window(slot=2, clients=3, k=2, m=3, **kw):
    return peer.PeerWindow(slot, clients, k, m, TOKEN, RELAY, **kw)


def addresses(clients=3, m=3, slot=2, last=120):
    book = '|'.join(f'10.0.0.{s}:{20000+s}' for s in range(1, clients+1))
    return f'NGPADDR|1|{TOKEN}|{slot}|{m}|{last}|{book}'.encode()


def record(slot, tick, blob, clients=3, k=2, signature='', token=TOKEN):
    return f'{peer.record_text(token, slot, tick, clients, k, blob)}|{signature}'.encode()


def active(last=120, **kw):
    w = window(**kw)
    assert w.relay_answer(addresses(kw.get('clients', 3), kw.get('m', 3), kw.get('slot', 2), last)) == 0 and w.state == 'active'
    return w


def bundle(T, k=2, clients=3, blob=lambda s, t: f'{s:02x}{t:02x}'):
    return [blob(s, T-1-k) if T-1-k >= 0 else '' for s in range(1, clients+1)]


class PeerWireTests(unittest.TestCase):
    def test_messages_round_trip_and_refuse_noncanonical_fields(self):
        self.assertEqual(peer.hello(TOKEN, 2, 3, 2, 3), f'NGPHELLO|1|{TOKEN}|2|3|2|3'.encode())
        self.assertEqual(peer.parse_relay_answer(addresses(), TOKEN, 2, 3),
                         ('addresses', 3, 120, [('10.0.0.1', 20001), ('10.0.0.2', 20002), ('10.0.0.3', 20003)]))
        self.assertEqual(peer.parse_relay_answer(f'NGPREFUSE|1|{TOKEN}|2|off'.encode(), TOKEN, 2, 3), ('refused', 'off'))
        bad = [addresses(slot=1), addresses().replace(b'|3|120|', b'|03|120|'), addresses().replace(b'|120|', b'|0120|'),
               addresses().replace(b'|120|', b'|'), addresses().replace(b':20003', b':0'),
               addresses().replace(b'10.0.0.3', b'10.0.0.256'), addresses().replace(b'10.0.0.3', b'10.0.03'),
               addresses().replace(b'NGPADDR|1', b'NGPADDR|2'), addresses().replace(TOKEN.encode(), b'b'*32),
               addresses()+b'|10.0.0.4:1', f'NGPREFUSE|1|{TOKEN}|2|other'.encode(), b'\xff']
        for data in bad:
            self.assertIsNone(peer.parse_relay_answer(data, TOKEN, 2, 3), data)
        crypto = peer.StubCrypto()
        self.assertEqual(peer.parse_record(record(1, 7, '0a0b'), TOKEN, 3, 2, crypto), (1, 7, '0a0b'))
        self.assertEqual(peer.parse_record(record(3, 0, ''), TOKEN, 3, 2, crypto), (3, 0, ''))
        for data in (record(1, 7, '0A'), record(1, 7, 'abc'), record(1, 7, '00', clients=4), record(1, 7, '00', k=3),
                     record(4, 7, '00'), record(0, 7, '00'), record(1, 7, '00', signature='00'), record(1, 7, '00', token='b'*32),
                     record(1, 7, '00').replace(b'|7|', b'|07|'), record(1, 7, 'e'*482), b'NGPREC|1|x', b'\xff|'):
            self.assertIsNone(peer.parse_record(data, TOKEN, 3, 2, crypto), data)
        self.assertEqual(peer.fail_notice(TOKEN, 2, 9, 104), f'NGPFAIL|1|{TOKEN}|2|9|104'.encode())
        # The largest record (eight clients, 480 hex characters, Ed25519 hex) fits a 1 KiB datagram.
        self.assertLess(len(record(8, 2**64-1, 'e'*480, clients=8, k=8, signature='f'*128)), 1024)

    def test_window_bounds(self):
        for k, m in ((0, 1), (1, 8), (9, 0), (2, -1), (4, 5), (1.0, 1)):
            with self.assertRaises(ValueError):
                peer.PeerWindow(1, 2, k, m, TOKEN, RELAY)
        for k, m in ((1, 0), (1, 7), (8, 0), (4, 4)):
            peer.PeerWindow(1, 2, k, m, TOKEN, RELAY)


class PeerWindowTests(unittest.TestCase):
    def test_hello_until_answered_then_active_or_refused(self):
        w = window()
        self.assertEqual(w.hello_due(0.0), peer.hello(TOKEN, 2, 3, 2, 3))
        self.assertIsNone(w.hello_due(0.05))
        self.assertIsNotNone(w.hello_due(0.1))
        self.assertEqual(w.receive(addresses(m=4), RELAY, 0.2), 0)          # another window: dropped
        self.assertEqual((w.state, w.stats['dropped']), ('hello', 1))
        self.assertEqual(w.receive(addresses(), ('10.0.0.1', 20001), 0.2), 0)  # not from the relay
        self.assertEqual(w.receive(addresses(), RELAY, 0.2), 0)
        self.assertEqual((w.state, sorted(w.routes)), ('active', [1, 3]))
        self.assertIsNone(w.hello_due(1.0))
        refused = window()
        refused.receive(f'NGPREFUSE|1|{TOKEN}|2|mismatch'.encode(), RELAY, 0)
        self.assertEqual(refused.state, 'refused')
        self.assertEqual(refused.sealed(5, '01', 0), [])                    # relay only: nothing goes to peers
        self.assertIsNone(refused.take(8, 0))

    def test_nat_and_crypto_interfaces(self):
        class Nat(peer.StubNat):
            name = 'test'
            answers = {1: peer.PENDING, 3: None}
            seen = []

            def route(self, slot, advertised):
                return self.answers[slot] if self.answers[slot] != 'later' else advertised

            def datagram(self, source, data):
                self.seen.append(data)
                return data == b'PUNCH'

        class Crypto(peer.StubCrypto):
            name = 'test'

            def seal(self, plain):
                return b'S'+plain

            def open(self, sealed):
                return sealed[1:] if sealed[:1] == b'S' else None

            def sign(self, record):
                return f'{sum(record.encode()) % 65536:04x}'

            def verify(self, slot, record, signature):
                return signature == self.sign(record)

        nat, crypto = Nat(), Crypto()
        w = window(nat=nat, crypto=crypto)
        w.receive(addresses(), RELAY, 0)
        self.assertEqual((w.routes, w.pending, w.stats['unreachable']), ({}, {1: ('10.0.0.1', 20001)}, 1))
        Nat.answers[1] = 'later'
        out = w.sealed(4, '0a', 0)                                           # routes are asked again every frame
        self.assertEqual(w.routes, {1: ('10.0.0.1', 20001)})
        text = peer.record_text(TOKEN, 2, 4, 3, 2, '0a')
        self.assertEqual(out, [(('10.0.0.1', 20001), b'S'+f'{text}|{crypto.sign(text)}'.encode())])
        self.assertIn('crypto=test nat=test', w.status())
        good = b'S'+f'{peer.record_text(TOKEN, 1, 4, 3, 2, "01")}|'.encode()
        signed = b'S'+f'{peer.record_text(TOKEN, 1, 4, 3, 2, "01")}|{crypto.sign(peer.record_text(TOKEN, 1, 4, 3, 2, "01"))}'.encode()
        for forged in (good, signed[1:], b'S'+signed[2:]):
            self.assertEqual(w.receive(forged, ('10.0.0.1', 20001), 0), 0)
        self.assertEqual(w.stats['received'], 0)
        self.assertEqual(w.receive(signed, ('10.0.0.1', 20001), 0), 0)
        self.assertEqual((w.stats['received'], w.entry(7).blobs), (1, {2: '0a', 1: '01'}))
        self.assertEqual(w.receive(signed, ('10.0.0.3', 20003), 0), 0)       # pair 3 unreachable: not a peer source
        self.assertEqual(w.receive(b'PUNCH', ('10.9.9.9', 1), 0), 0)
        self.assertEqual(nat.seen[-2:], [signed, b'PUNCH'])

    def test_peer_runs_need_every_record_tick0_admitted_and_the_window(self):
        w = active(k=2, m=2)
        blobs = bundle(3)                                                   # T=3 carries tick 0
        w.sealed(0, blobs[1], 0)
        self.assertEqual(w.receive(record(1, 0, blobs[0]), ('10.0.0.1', 20001), 0.001), 0)
        self.assertEqual(w.receive(record(3, 0, blobs[2]), ('10.0.0.3', 20003), 0.002), 0)
        self.assertIsNone(w.take(3, 0.003))                                 # F=0: tick 0 not admitted by the relay yet
        for T in (1, 2):
            self.assertEqual(w.permit(T, ['', '', ''], 0.01), 0)
        self.assertEqual(w.final, 2)
        self.assertEqual(w.take(3, 0.011), blobs)
        self.assertEqual(w.stats['peer_runs'], 1)
        for t in (1, 2):
            w.sealed(t, bundle(t+3)[1], 0.02)
            w.receive(record(1, t, bundle(t+3)[0]), ('10.0.0.1', 20001), 0.02)
        self.assertIsNone(w.take(4, 0.02))                                  # slot 3's record of tick 1 is missing
        w.receive(record(3, 1, bundle(4)[2]), ('10.0.0.3', 20003), 0.02)
        self.assertEqual(w.take(4, 0.02), bundle(4))
        w.receive(record(3, 2, bundle(5)[2]), ('10.0.0.3', 20003), 0.02)
        self.assertIsNone(w.take(5, 0.02))                                  # 5 > F+m = 4
        self.assertFalse(w.may_run(5))
        self.assertEqual(w.permit(3, bundle(3), 0.05), 0)                   # verifies the peer run of 3
        self.assertEqual(w.final, 3)
        self.assertEqual(w.take(5, 0.05), bundle(5))
        self.assertEqual(w.stats['behind_max'], 2)
        self.assertEqual((w.stats['peer_first'], w.stats['relay_first']), (1, 2))
        self.assertAlmostEqual(w.stats['lead'], 0.05-0.002)
        self.assertEqual(w.resend_from(5), 1)                               # report 1 releases the missing permit 4
        # Never past the relay's last tick: the session ends there, whatever the peers send.
        end = active(k=2, m=2, last=4)
        for T in (1, 2, 3, 4):
            end.permit(T, bundle(T), 0)
        for t in (2, 3):
            end.sealed(t, bundle(t+3)[1], 0)
            end.receive(record(1, t, bundle(t+3)[0]), ('10.0.0.1', 20001), 0)
            end.receive(record(3, t, bundle(t+3)[2]), ('10.0.0.3', 20003), 0)
        self.assertTrue(end.may_run(5) and end.entry(5).blobs and end.take(5, 0) is None)

    def test_a_peer_copy_that_differs_from_the_permit_stops_before_or_after_the_run(self):
        # The copy arrived first and the tick ran on it: the late permit stops the client.
        w = active(k=1, m=3, clients=2, slot=2)
        w.permit(1, ['', ''], 0)
        w.sealed(0, '02', 0)
        w.receive(record(1, 0, '01', clients=2, k=1), ('10.0.0.1', 20001), 0)
        self.assertEqual(w.take(2, 0), ['01', '02'])
        self.assertEqual(w.permit(2, ['ff', '02'], 0.1), peer.STOP_MISMATCH)
        # The permit arrived first: a different late copy stops too, an identical one is a duplicate.
        w = active(k=1, m=3, clients=2, slot=2)
        w.permit(1, ['', ''], 0)
        w.sealed(0, '02', 0)
        self.assertEqual(w.permit(2, ['01', '02'], 0), 0)
        self.assertEqual(w.receive(record(1, 0, '01', clients=2, k=1), ('10.0.0.1', 20001), 0), 0)
        self.assertEqual(w.stats['duplicates'], 1)
        self.assertEqual(w.receive(record(1, 0, '0f', clients=2, k=1), ('10.0.0.1', 20001), 0), peer.STOP_MISMATCH)
        # Two different peer copies of one record (before any permit): 108.
        w = active(k=1, m=3, clients=2, slot=2)
        w.receive(record(1, 5, '01', clients=2, k=1), ('10.0.0.1', 20001), 0)
        self.assertEqual(w.receive(record(1, 5, '02', clients=2, k=1), ('10.0.0.1', 20001), 0), peer.STOP_EQUIVOCATION)
        # Two different permits for a tick already final: 103; the same one again: fine.
        w = active(k=1, m=3, clients=2, slot=2)
        self.assertEqual(w.permit(1, ['', ''], 0), 0)
        self.assertEqual(w.permit(1, ['', ''], 0), 0)
        self.assertEqual(w.permit(1, ['00', ''], 0), peer.STOP_PERMIT_CONFLICT)

    def test_records_outside_the_ring_are_dropped_or_stale(self):
        w = active(k=1, m=3, clients=2, slot=2)
        self.assertEqual(w.receive(record(1, peer.PEER_RING, '01', clients=2, k=1), ('10.0.0.1', 20001), 0), 0)
        self.assertEqual(w.stats['dropped'], 1)                             # T = 33 >= F + 32
        for T in range(1, 41):
            w.permit(T, ['', ''], 0)
        self.assertEqual(w.final, 40)
        self.assertEqual(w.receive(record(1, 2, '01', clients=2, k=1), ('10.0.0.1', 20001), 0), 0)
        self.assertEqual(w.stats['stale'], 1)                               # T = 4: overwritten long ago
        self.assertEqual(w.receive(record(1, 30, '', clients=2, k=1), ('10.0.0.1', 20001), 0), 0)
        self.assertEqual(w.stats['duplicates'], 1)                          # T = 32: final and equal
        self.assertEqual(w.receive(record(1, 30, '01', clients=2, k=1), ('10.0.0.1', 20001), 0), peer.STOP_MISMATCH)


def hello_at(barrier, slot, address, clients=3, k=2, m=3, now=0):
    return barrier.receive(peer.hello(TOKEN, slot, clients, k, m), address, now)


def report(barrier, slot, tick, clients=3, k=2, blob=''):
    return barrier.receive(sim.state(slot, tick, blob, clients, k, 'b'*64), ('10.0.0.%d' % slot, 10000+slot), 0)


class RelayPeerTests(unittest.TestCase):
    def test_addresses_only_once_every_client_opted_in_and_never_in_the_report(self):
        b = relay.Barrier(TOKEN, clients=3, hold_tick=99, lookahead=2, peer_window=3, remote=True)
        self.assertEqual(hello_at(b, 1, ('10.0.0.1', 20001)), [])            # no report from slot 1 yet: dropped
        for s in (1, 2, 3):
            report(b, s, 0)
        self.assertEqual(hello_at(b, 1, ('10.0.0.1', 20001)), [])
        self.assertEqual(hello_at(b, 2, ('10.0.0.9', 20002)), [])            # not slot 2's host
        self.assertEqual(hello_at(b, 2, ('10.0.0.2', 20002)), [])
        self.assertEqual(b.summary()['peer_links'], 'waiting')
        self.assertEqual(hello_at(b, 2, ('10.0.0.2', 20099)), [])            # a second socket for slot 2: dropped
        out = hello_at(b, 3, ('10.0.0.3', 20003))
        book = '10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003'
        self.assertEqual(out, [((f'10.0.0.{s}', 20000+s), f'NGPADDR|1|{TOKEN}|{s}|3|120|{book}'.encode()) for s in (1, 2, 3)])
        self.assertEqual(hello_at(b, 1, ('10.0.0.1', 20001)), [(('10.0.0.1', 20001), f'NGPADDR|1|{TOKEN}|1|3|120|{book}'.encode())])
        for text in (json.dumps(b.summary()), json.dumps(b.status(0))):
            self.assertNotIn('10.0.0.1:20001', text)
            self.assertNotIn('20001]', text)
        self.assertEqual((b.summary()['peer_window'], b.summary()['peer_links']), (3, 'exchanged'))

    def test_refusals_and_drops(self):
        plain = relay.Barrier(TOKEN, clients=3, hold_tick=99, lookahead=2, remote=True)
        report(plain, 1, 0)
        self.assertEqual(hello_at(plain, 1, ('10.0.0.1', 20001)), [(('10.0.0.1', 20001), f'NGPREFUSE|1|{TOKEN}|1|off'.encode())])
        self.assertNotIn('peer_window', plain.summary())
        b = relay.Barrier(TOKEN, clients=3, hold_tick=99, lookahead=2, peer_window=3, remote=True)
        report(b, 1, 0)
        for clients, k, m in ((2, 2, 3), (3, 1, 3), (3, 2, 2)):
            self.assertEqual(hello_at(b, 1, ('10.0.0.1', 20001), clients, k, m),
                             [(('10.0.0.1', 20001), f'NGPREFUSE|1|{TOKEN}|1|mismatch'.encode())])
        for data in (f'NGPHELLO|2|{TOKEN}|1|3|2|3', f'NGPHELLO|1|{"b"*32}|1|3|2|3', f'NGPHELLO|1|{TOKEN}|01|3|2|3',
                     f'NGPHELLO|1|{TOKEN}|1|3|2', f'NGPHELLO|1|{TOKEN}|9|3|2|3', f'NGPHELLO|1|{TOKEN}|1|3|2|3|'+'x'*200):
            self.assertEqual(b.receive(data.encode(), ('10.0.0.1', 20001), 0), [], data)
        local = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=1, peer_window=1)
        local.receive(sim.state(1, 0, '', 2, 1, 'b'*64), ('127.0.0.1', 10001), 0)
        self.assertEqual(local.receive(peer.hello(TOKEN, 1, 2, 1, 1), ('10.0.0.1', 20001), 0), [])   # not loopback, not remote
        self.assertEqual(local.receive(peer.hello(TOKEN, 1, 2, 1, 1), ('127.0.0.1', 20001), 0), [])  # waiting for slot 2
        for bad in (dict(peer_window=1), dict(lookahead=4, peer_window=5), dict(lookahead=1, peer_window=-1),
                    dict(lookahead=1, peer_window=True)):
            with self.assertRaises(ValueError):
                relay.Barrier(TOKEN, **bad)

    def test_reports_may_run_the_peer_window_past_the_newest_permit(self):
        b = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=1, peer_window=3, remote=True)
        self.assertEqual(b.accepted(), 0)                                   # before tick 0: nothing beyond it
        for s in (1, 2):
            report(b, s, 0, clients=2, k=1)
        self.assertEqual((b.permitted(), b.accepted()), (2, 5))
        self.assertTrue(report(b, 1, 5, clients=2, k=1) == [] and 5 in b.states)
        report(b, 1, 6, clients=2, k=1)
        self.assertNotIn(6, b.states)
        plain = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=1, remote=True)
        for s in (1, 2):
            report(plain, s, 0, clients=2, k=1)
        self.assertEqual(plain.accepted(), plain.permitted())

    def test_a_peer_check_stop_fails_the_session_and_stops_everyone(self):
        b = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=1, peer_window=3, remote=True)
        for s in (1, 2):
            report(b, s, 0, clients=2, k=1)
        report(b, 1, 1, clients=2, k=1)
        self.assertEqual(b.receive(peer.fail_notice(TOKEN, 2, 1, 99), ('10.0.0.2', 20002), 0), [])
        self.assertEqual(b.receive(peer.fail_notice(TOKEN, 2, 1, 104), ('10.0.0.9', 20002), 0), [])
        out = b.receive(peer.fail_notice(TOKEN, 2, 1, 104), ('10.0.0.2', 20002), 0)
        self.assertEqual(out, [(('10.0.0.1', 10001), f'NGSTOP|4|{TOKEN}|1|1'.encode()), (('10.0.0.2', 10002), f'NGSTOP|4|{TOKEN}|2|0'.encode())])
        self.assertEqual(b.summary()['failure'], 'peer_link_stop_104_slot_2_tick_1')
        plain = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=1, remote=True)
        report(plain, 2, 0, clients=2, k=1)
        self.assertEqual(plain.receive(peer.fail_notice(TOKEN, 2, 1, 104), ('10.0.0.2', 20002), 0), [])
        self.assertIsNone(plain.failed)

    def test_cli_refuses_invalid_windows(self):
        for args in (['--peer-window', '1'], ['--lookahead', '4', '--peer-window', '5'], ['--lookahead', '1', '--peer-window', '-1']):
            with mock.patch.object(sys, 'argv', ['relay', '--port', '1', '--token', TOKEN, '--report', 'x']+args), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                relay.main()


class SimulatedPeerTests(unittest.TestCase):
    def assert_identical_inputs(self, s):
        """Every client applied, at every tick it ran, the proposals the relay released for it."""
        for slot in s.applied:
            for tick, blobs in s.applied[slot].items():
                source = tick-1-s.k
                self.assertEqual(blobs, [s.proposal(i, source) if source >= 0 else '' for i in range(1, s.n+1)], (slot, tick))

    def test_relay_only_reproduces_the_lookahead_session_model(self):
        for r, f in ((.011, .031), (.06, .031), (.12, 1/30)):
            for k in (1, 2, 4):
                old = lookahead_tests.SimulatedSession(k, clients=2, ticks=130, frame=lambda s, t, f=f: f,
                                                      up=lambda s, r=r: r/2, down=lambda s, r=r: r/2).run()
                new = sim.PeerSession(k, None, clients=2, ticks=130, frame=lambda s, t, f=f: f,
                                      up=lambda s, r=r: r/2, down=lambda s, r=r: r/2).run()
                self.assertEqual(old.starts, new.starts, (r, f, k))

    def test_steady_state_follows_the_peer_and_relay_cycles(self):
        # Relay only: every client's own report makes the relay round trip r, so P = max(f, (r+f)/(k+1)).
        # Peer links: the cycles run through the pairs (one-way p each) and the relay may lag m more
        # ticks: P = max(f, (p+f)/(k+1), (r+f)/(k+m+1)).
        f = 1/30
        for r, p in ((.12, .01), (.09, .03), (.2, .02)):
            for k, m in ((1, None), (2, None), (1, 0), (1, 2), (1, 7), (2, 3)):
                s = sim.PeerSession(k, m, ticks=250, up=lambda x, r=r: r/2, down=lambda x, r=r: r/2,
                                    peer=lambda i, j, p=p: p).run()
                expected = max(f, (r+f)/(k+1)) if m is None else max(f, (p+f)/(k+1), (r+f)/(k+m+1))
                self.assertAlmostEqual(s.period(first=40, last=240), expected, delta=expected*.03, msg=(r, p, k, m))
                self.assertEqual(s.barrier.summary()['verdict'], 'pass')
                self.assert_identical_inputs(s)

    def test_geometries_of_the_report(self):
        g = {name.split()[0]: kwargs for name, kwargs, _ in sim.GEOMETRIES}
        rate = lambda geometry, k, m: sim.run_case(g[geometry], k, m).rate()   # noqa: E731
        # G1: players 5 ms apart, relay 120 ms round trip: full rate at k=1 with peers, k=4 without.
        self.assertAlmostEqual(rate('G1', 1, None), 13.04, delta=.05)
        self.assertAlmostEqual(rate('G1', 1, 7), 30.0, delta=.05)
        self.assertAlmostEqual(rate('G1', 4, None), 30.0, delta=.05)
        self.assertLess(rate('G1', 1, 2), 29.5)                           # the relay still limits: m >= 3 needed
        self.assertAlmostEqual(rate('G1', 1, 3), 30.0, delta=.05)
        # G3/G4: the relay path is as short or shorter than the direct one: no gain, no loss.
        self.assertAlmostEqual(rate('G3', 1, 7), rate('G3', 1, None), delta=.05)
        self.assertAlmostEqual(rate('G4', 1, 7), rate('G4', 1, None), delta=.05)
        # G5: even next to the relay, player 2's own report pays its round trip without peers.
        self.assertGreater(rate('G5', 1, 7), rate('G5', 1, None)+9)
        # G8: one pair that cannot connect puts everyone back on relay speed.
        self.assertAlmostEqual(rate('G8', 1, 7), rate('G8', 1, None), delta=.05)
        # Records only (m=0) never changes the rate; the peer copies arrive 115 ms before the permit in G1.
        records = sim.run_case(g['G1'], 1, 0)
        self.assertEqual(records.starts, sim.run_case(g['G1'], 1, None).starts)
        self.assertAlmostEqual(records.lead(), .115, delta=.001)

    def test_mixed_speeds_latencies_and_loss_keep_inputs_identical(self):
        for seed in range(6):
            rng = random.Random(seed)
            n = rng.choice((2, 3, 4))
            k = rng.randint(1, 4)
            m = rng.randint(0, 8-k)
            frames = {s: rng.uniform(.008, .05) for s in range(1, n+1)}
            ups = {s: rng.uniform(.001, .08) for s in range(1, n+1)}
            pair = {(i, j): rng.choice((None, rng.uniform(.001, .06))) for i in range(1, n+1) for j in range(i+1, n+1)}
            s = sim.PeerSession(k, m, clients=n, ticks=160, seed=seed, loss=.03,
                                frame=lambda x, t: frames[x]*(1.5 if t % 7 == 0 else 1),
                                up=lambda x: ups[x], down=lambda x: ups[x],
                                peer=lambda i, j: pair[(min(i, j), max(i, j))]).run()
            self.assertEqual(s.barrier.summary()['verdict'], 'pass', (seed, s.barrier.summary()['failure']))
            self.assertEqual(set(s.stopped.values()), {1}, seed)
            self.assertEqual(max(max(starts) for starts in s.starts.values()), 160, seed)   # never past --ticks
            self.assert_identical_inputs(s)
            for slot, link in s.links.items():
                self.assertLessEqual(link.stats['behind_max'], m)
                # Every tick that ran on peer records was later confirmed by its permit, except the last
                # few when the relay's NGSTOP at --ticks overtook a lost permit (their inputs are in the
                # digests the relay compared up to --ticks, natively through the INPUTS_1 receipts).
                self.assertTrue(all(link.entry(T, create=False) is None or link.entry(T, create=False).final
                                    for T, via in s.via[slot].items() if via == 'peer' and T < s.ticks-10), (seed, slot))

    def test_a_client_that_sends_its_peers_another_record_stops_the_session(self):
        k, m = 1, 4
        s = sim.PeerSession(k, m, ticks=120, equivocate={(2, 30): 'ffff'},
                            **sim.symmetric(.06, .005, clients=3)).run()
        self.assertRegex(s.barrier.summary()['failure'], r'^peer_link_stop_104_slot_[13]_tick_\d+$')
        for slot in (1, 3):
            self.assertEqual(s.stopped[slot], peer.STOP_MISMATCH)
            self.assertEqual(s.applied[slot][30+1+k][1], 'ffff')         # it ran tick 32 on the forged copy ...
            self.assertLessEqual(max(s.starts[slot]), 31+m)              # ... and never past F+m before the permit
        self.assertEqual(s.stopped[2], 1)                                  # the relay stopped the cheater too, at once
        self.assertLess(max(max(starts.values()) for starts in s.starts.values()), 2.0)

    def test_a_missing_client_or_relay_verdict_bounds_the_run(self):
        k, m = 2, 3
        s = sim.PeerSession(k, m, ticks=100, crash={2: range(20, 10**6)}, **sim.symmetric(.03, .005, clients=3)).run()
        self.assertEqual(s.stopped, {1: 7, 2: 'crashed', 3: 7})
        self.assertEqual(max(max(starts) for starts in s.starts.values()), 19+1+k)   # as relay only: no record, no run
        s = sim.PeerSession(k, m, ticks=100, digest=lambda x, t: 'c'*64 if (x, t) == (2, 40) else 'b'*64,
                            **sim.symmetric(.06, .005)).run()
        self.assertEqual(s.barrier.failed, 'battle_state_mismatch_tick_40')
        self.assertLessEqual(max(max(starts) for starts in s.starts.values()), 40+k+m)
        self.assertEqual(set(s.stopped.values()), {1})

    def test_no_addresses_unless_every_client_opts_in(self):
        s = sim.PeerSession(1, 7, ticks=80, opt_out={3}, **sim.symmetric(.06, .005, clients=3)).run()
        self.assertEqual(s.barrier.summary()['peer_links'], 'waiting')
        self.assertEqual({link.state for link in s.links.values()}, {'hello'})
        self.assertEqual(s.peer_share(), 0.0)
        self.assertEqual(s.barrier.summary()['verdict'], 'pass')
        self.assertAlmostEqual(s.rate(), 13.04, delta=.05)                # relay only


class RelayProcessPeerTests(unittest.TestCase):
    def test_real_udp_address_exchange(self):
        with tempfile.TemporaryDirectory() as tmp:
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
            probe.close()
            report_path = Path(tmp)/'report.json'
            process = subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', str(port), '--token', TOKEN,
                                        '--report', str(report_path), '--clients', '2', '--lookahead', '1', '--peer-window', '2',
                                        '--ticks', '3', '--hold-tick', '99'], stdout=subprocess.PIPE, text=True)
            sockets = []
            try:
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
                for _ in range(4):
                    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    s.bind(('127.0.0.1', 0))
                    s.settimeout(2)
                    sockets.append(s)
                reports, peers = sockets[:2], sockets[2:]
                for slot in (1, 2):
                    reports[slot-1].sendto(sim.state(slot, 0, '', 2, 1, 'b'*64), ('127.0.0.1', port))
                for slot in (1, 2):
                    for _ in range(2):                                     # permits 1 and 2
                        self.assertTrue(reports[slot-1].recvfrom(4096)[0].startswith(f'NGGO|4|{TOKEN}|{slot}|'.encode()))
                peers[0].sendto(peer.hello(TOKEN, 1, 2, 1, 2), ('127.0.0.1', port))
                peers[1].sendto(peer.hello(TOKEN, 2, 2, 1, 2), ('127.0.0.1', port))
                book = '|'.join(f'127.0.0.1:{s.getsockname()[1]}' for s in peers)
                for slot in (1, 2):
                    self.assertEqual(peers[slot-1].recvfrom(4096)[0], f'NGPADDR|1|{TOKEN}|{slot}|2|3|{book}'.encode())
                # A report 2+2 ticks past the newest permit is accepted (kept), 5 would not be.
                reports[0].sendto(sim.state(1, 3, '', 2, 1, 'b'*64), ('127.0.0.1', port))
                peers[1].sendto(peer.fail_notice(TOKEN, 2, 0, 104), ('127.0.0.1', port))
                self.assertEqual(reports[0].recvfrom(4096)[0], f'NGSTOP|4|{TOKEN}|1|3'.encode())
                self.assertEqual(reports[1].recvfrom(4096)[0], f'NGSTOP|4|{TOKEN}|2|0'.encode())
                deadline = time.time()+5
                while time.time() < deadline:
                    if report_path.exists():
                        result = json.loads(report_path.read_text())
                        if result['failure']:
                            break
                    time.sleep(.05)
                self.assertEqual((result['failure'], result['peer_window'], result['peer_links']),
                                 ('peer_link_stop_104_slot_2_tick_0', 2, 'exchanged'))
                self.assertNotIn(f'127.0.0.1:{peers[0].getsockname()[1]}', report_path.read_text())
            finally:
                for s in sockets:
                    s.close()
                process.kill()
                process.wait()
                process.stdout.close()


if __name__ == '__main__':
    unittest.main()
