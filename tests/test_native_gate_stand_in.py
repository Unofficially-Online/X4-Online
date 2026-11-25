"""Stand-ins from tick 0 (native_gate_relay.py --stand-in, DropBarrier stand_in; x4ls play --solo K): the relay plays every
other slot as a drop at proposal tick 0, so one client plays a session of N slots alone.

The permit rules checked here mirror the client side of lockstep_lookahead.h (lookahead_permit_receive and lookahead_take:
no mask before proposal tick 0, a mask never naming this client, never shrinking, stand-in blobs empty). They are a
Python copy of the C rules, not the gate itself: the gate's own tests are test_lockstep_lookahead.h."""
import importlib.util
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_relay_stand_in', ROOT/'tools/dev/native_gate_relay.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
TOKEN = 'c'*32


def state(slot, tick, blob='', clients=3, lookahead=1, digest='d'*64):
    game = 20+tick/30
    real = 5+slot*0.25+tick/30          # every process has its own real clock base (relative_real compares elapsed time)
    fields = ['NGSTATE', '4', TOKEN, str(slot), str(tick), struct.pack('>d', game).hex(), struct.pack('>d', real).hex(), digest,
              str(clients), str(lookahead), blob]
    return '|'.join(fields).encode()


def addr(slot):
    return ('127.0.0.1', 20000+slot)


def make(stand_in, clients=3, lookahead=1, ticks=40, **kw):
    args = dict(ticks=ticks, hold_tick=99, hold_seconds=0, relative_real=True, clients=clients, lookahead=lookahead, tick_hz=30,
                stall_report_ms=1000, stall_drop_ms=0, stand_in=stand_in)
    args.update(kw)
    return relay.DropBarrier(TOKEN, **args)


class GateModel:
    """The client side of a protocol 4 permit with a drop mask, as lockstep_lookahead.h checks it (a Python mirror)."""
    def __init__(self, slot, clients, lookahead):
        self.slot, self.clients, self.k, self.dropped, self.drop_from = slot, clients, lookahead, 0, {}

    def take(self, packet):
        f = packet.decode().split('|')
        assert f[:4] == ['NGGO', '4', TOKEN, str(self.slot)], f
        tick = int(f[4])
        window, _, mask_text = f[5].partition('/')
        assert window == str(self.k), f                                 # the window field: k, or k/<mask>
        mask = 0
        if mask_text:
            assert len(mask_text) == 2 and all(c in '0123456789abcdef' for c in mask_text), f
            mask = int(mask_text, 16)
            assert mask and not mask >> self.clients, f                 # -2: an empty mask or one past the slots
            assert not mask >> (self.slot-1) & 1, f                    # -5: a permit naming this client stops it (193)
        blobs = f[6:]
        assert len(blobs) == self.clients, f
        if tick <= self.k:
            assert not mask and not any(blobs), f                       # -2: before proposal tick 0 nothing, no mask
        assert not any(blobs[i] for i in range(self.clients) if mask >> i & 1), f   # -2: a stand-in carries nothing
        assert mask & self.dropped == self.dropped, f                   # -3: a drop is never undone
        self.dropped |= mask
        if mask and tick > self.k:                                      # pilot_drop_note(mask, tick-1-k)
            for i in range(self.clients):
                if mask >> i & 1:
                    self.drop_from.setdefault(i+1, tick-1-self.k)
        return tick, blobs


class StandInBarrierTests(unittest.TestCase):
    def solo(self, slot, stand_in, clients=3, lookahead=1, ticks=40):
        b = make(stand_in, clients=clients, lookahead=lookahead, ticks=ticks)
        gate, permits, stops = GateModel(slot, clients, lookahead), {}, []
        for t in range(0, ticks+1):
            for a, packet in b.receive(state(slot, t, f'{slot:02x}{t:02x}', clients, lookahead), addr(slot), float(t)):
                self.assertEqual(a, addr(slot))                         # nobody else is ever sent anything
                if packet.startswith(b'NGSTOP'):
                    stops.append(packet.decode())
                    continue
                tick, blobs = gate.take(packet)
                permits[tick] = blobs
            self.assertEqual(b.poll(float(t)+0.5), [])                   # the one played slot is never stalled or dropped
        return b, gate, permits, stops

    def test_one_client_plays_slot_2_of_3_to_the_end(self):
        b, gate, permits, stops = self.solo(2, (1, 3))
        self.assertTrue(b.done and not b.failed and not b.mismatches)
        self.assertEqual(stops, [f'NGSTOP|4|{TOKEN}|2|40'])
        self.assertEqual(sorted(permits), list(range(1, 41)))           # every permit of the session, once
        self.assertEqual(permits[1], ['', '', ''])                      # tick 1 <= k: no proposals yet
        self.assertEqual(permits[2], ['', '0200', ''])                  # tick 2 carries proposal tick 0, slots 1 and 3 empty
        self.assertEqual(permits[40], ['', '0226', ''])
        self.assertEqual(gate.dropped, 0b101)
        self.assertEqual(gate.drop_from, {1: 0, 3: 0})                  # dropped at proposal tick 0 on the client
        for t in range(0, 41):
            for s in (1, 3):
                row = b.states[t][s]
                self.assertTrue(row['standin'] and row['blob'] == '')
                self.assertEqual((row['game'], row['real'], row['digest']), (b.states[t][2]['game'], b.states[t][2]['real'],
                                                                             b.states[t][2]['digest']))
        summary = b.summary()
        self.assertEqual(summary['drop']['stand_in'], [1, 3])
        self.assertEqual((summary['drop']['drops'], summary['drop']['stalls'], summary['drop']['stall_drop_ms']), ([], [], 0))
        self.assertEqual(summary['verdict'], 'pass')
        self.assertEqual(b.status(1.0)['stand_in'], [1, 3])
        self.assertEqual(set(b.peers), {2})

    def test_slot_1_played_alone_and_wider_windows(self):
        b, gate, permits, _ = self.solo(1, (2, 3, 4), clients=4, lookahead=3, ticks=30)
        self.assertTrue(b.done and not b.failed)
        self.assertEqual([permits[t] for t in (1, 2, 3)], [['']*4]*3)  # ticks 1..k: nothing
        self.assertEqual(permits[4], ['0100', '', '', ''])
        self.assertEqual(gate.drop_from, {2: 0, 3: 0, 4: 0})
        self.assertEqual(b.packet('NGGO', 1, 4).decode().split('|')[5], '3/0e')

    def test_the_last_slot_alone(self):
        b, gate, _, _ = self.solo(8, tuple(range(1, 8)), clients=8)
        self.assertTrue(b.done and not b.failed)
        self.assertEqual(gate.dropped, 0x7f)

    def test_a_report_from_a_stand_in_slot_is_never_admitted(self):
        b = make((1, 3))
        for t in range(0, 3):
            b.receive(state(2, t, '0200'), addr(2), 0.0)
        before = dict(b.states[1])
        self.assertEqual(b.receive(state(1, 1, '0101'), addr(1), 1.0), [])
        self.assertEqual(b.receive(state(3, 3, '0303'), addr(3), 1.0), [])
        self.assertEqual(b.states[1], before)
        self.assertNotIn(1, b.peers)
        self.assertNotIn(3, b.peers)

    def test_two_played_slots_and_a_later_drop_keep_the_mask_growing(self):
        # Stand-in slot 1 from tick 0, slots 2 and 3 played; slot 3 stalls at tick 5 and is dropped: the mask grows from 01 to 05.
        b = make((1,), stall_drop_ms=3000)
        gate = GateModel(2, 3, 1)
        for t in range(0, 5):
            for s in (2, 3):
                for a, packet in b.receive(state(s, t, f'{s:02x}{t:02x}'), addr(s), 0.0):
                    if a == addr(2):                                 # slot 2's permits, whichever report released them
                        gate.take(packet)
        b.receive(state(2, 5, '0205'), addr(2), 1.0)
        self.assertEqual(b.poll(2.5), [])
        self.assertEqual(b.take_notices(), ['NATIVE_GATE STALL slot=3 tick=5 waited_ms=1500'])
        out = b.poll(4.5)
        self.assertEqual(out[0], (addr(3), f'NGDROP|4|{TOKEN}|3|5'.encode()))
        self.assertEqual(b.take_notices(), ['NATIVE_GATE DROP slot=3 tick=5 waited_ms=3500 remaining=2'])
        for t in range(6, 41):
            out += b.receive(state(2, t, f'02{t:02x}'), addr(2), 5.0)
        for a, packet in out:
            if a == addr(2) and packet.startswith(b'NGGO'):
                gate.take(packet)
        self.assertTrue(b.done and not b.failed)
        self.assertEqual(gate.dropped, 0b101)
        self.assertEqual(gate.drop_from, {1: 0, 3: 5})
        self.assertEqual(b.summary()['drop']['stand_in'], [1])

    def test_refusals(self):
        for bad in (dict(stand_in=(1, 2, 3)), dict(stand_in=(4,)), dict(stand_in=(0,)), dict(stand_in=(1,), lookahead=0),
                    dict(stand_in=(1,), stall_drop_ms=500), dict(stand_in=(), stall_drop_ms=0)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                make(**bad)
        self.assertEqual(make((2,)).stand_in, (2,))
        self.assertEqual(make((3, 1, 3)).stand_in, (1, 3))


class StandInCliTests(unittest.TestCase):
    BASE = [sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--token', TOKEN, '--ticks', '6', '--hold-tick', '99',
            '--relative-real', '--report']

    def relay(self, extra, report):
        return self.BASE+[str(report)]+extra

    def test_cli_refusals(self):
        report = ROOT/'.local'/'stand-in-cli-unused.json'
        cases = {('--clients', '2', '--stand-in', '1'): '--stand-in needs --lookahead K',
                 ('--clients', '2', '--lookahead', '1', '--stand-in', '1,2'): '--stand-in: slots 1..--clients',
                 ('--clients', '3', '--lookahead', '1', '--stand-in', '4'): '--stand-in: slots 1..--clients',
                 ('--clients', '3', '--lookahead', '1', '--stand-in', '1,1'): '--stand-in: slots 1..--clients',
                 ('--clients', '3', '--lookahead', '1', '--stand-in', 'x'): '--stand-in takes slot numbers',
                 ('--clients', '3', '--lookahead', '1', '--stand-in', '1', '--join', '--absent', '3'): '--stand-in needs --lookahead K',
                 ('--clients', '3', '--lookahead', '1', '--stand-in', '1', '--adaptive'): '--stand-in needs --lookahead K',
                 ('--clients', '3', '--lookahead', '1', '--stand-in', '1', '--peer-window', '1'): '--stand-in needs --lookahead K'}
        for extra, message in cases.items():
            with self.subTest(extra=extra):
                r = subprocess.run(self.relay(['--port', '0', *extra], report), capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertIn(message, r.stderr)

    def test_one_client_over_udp(self):
        """The relay process with --stand-in 1 serves slot 2 of 2 alone: its first proposal permit names slot 1."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
            proc = subprocess.Popen(self.relay(['--port', str(port), '--clients', '2', '--lookahead', '1', '--stand-in', '1',
                                                '--stall-drop-ms', '15000', '--observe', '--keep-going'], Path(tmp)/'report.json'),
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                lines = []
                while True:
                    line = proc.stdout.readline()
                    self.assertTrue(line, ''.join(lines))
                    lines.append(line)
                    if 'RELAY READY' in line:
                        break
                self.assertIn('NATIVE_GATE RELAY STAND_IN slots=1 from=0 played=2\n', lines)
                client.bind(('127.0.0.1', 0))
                client.settimeout(5)
                got = {}
                # What each report releases (k=1, last tick 6): tick 0 the permits 1 and 2, tick t the permit t+2 (none past 6),
                # tick 6 the stop.
                expected = {0: ['NGGO|1', 'NGGO|2'], 5: [], 6: ['NGSTOP|6'], **{t: [f'NGGO|{t+2}'] for t in range(1, 5)}}
                for t in range(0, 7):
                    client.sendto(state(2, t, f'02{t:02x}', clients=2), ('127.0.0.1', port))
                    deadline = time.monotonic()+10
                    while not all(key in got for key in expected[t]) and time.monotonic() < deadline:
                        packet = client.recv(4096).decode()
                        got[packet.split('|')[0]+'|'+packet.split('|')[4]] = packet
                    self.assertTrue(all(key in got for key in expected[t]), (t, got))
                self.assertEqual(got['NGGO|1'], f'NGGO|4|{TOKEN}|2|1|1||')
                self.assertEqual(got['NGGO|2'], f'NGGO|4|{TOKEN}|2|2|1/01||0200')
                self.assertEqual(got['NGGO|6'], f'NGGO|4|{TOKEN}|2|6|1/01||0204')
                self.assertIn('NGSTOP|6', got)
            finally:
                proc.kill()
                proc.wait(10)
                proc.stdout.close()


if __name__ == '__main__':
    unittest.main()
