"""Protocol 4 drops (native_gate_relay.py DropBarrier, roadmap D15): a client the others waited for past --stall-drop-ms is
stood in for, named in every later permit (k/<mask>) and answered NGDROP; without a drop the wire is unchanged."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_relay_drop', ROOT/'tools/dev/native_gate_relay.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
TOKEN = 'a'*32


def state(slot, tick, blob='', clients=3, lookahead=1, digest='b'*64):
    game = 20+tick/30
    real = 5+slot*0.25+tick/30          # every process has its own real clock base (relative_real compares elapsed time)
    fields = ['NGSTATE', '4', TOKEN, str(slot), str(tick), struct.pack('>d', game).hex(), struct.pack('>d', real).hex(), digest,
              str(clients), str(lookahead), blob]
    return '|'.join(fields).encode()


def addr(slot):
    return ('127.0.0.1', 10000+slot)


def send(b, slot, tick, now, blob=''):
    return b.receive(state(slot, tick, blob), addr(slot), now)


def decoded(out):
    return [(a[1]-10000, p.decode()) for a, p in out]


def make(cls=None, **kw):
    cls = cls or relay.DropBarrier
    args = dict(ticks=40, hold_tick=99, hold_seconds=0, relative_real=True, clients=3, lookahead=1, tick_hz=30)
    if cls is relay.DropBarrier:
        args.update(stall_report_ms=1000, stall_drop_ms=3000)
    args.update(kw)
    return cls(TOKEN, **args)


def run_ticks(b, ticks, now, slots=(1, 2, 3), blob=lambda s, t: f'{s:02x}{t:02x}'):
    out = []
    for t in ticks:
        for s in slots:
            out += send(b, s, t, now, blob(s, t))
    return out


class DropBarrierTests(unittest.TestCase):
    def test_without_a_drop_the_wire_is_the_protocol_4_wire(self):
        plain, drop = make(relay.Barrier), make()
        a = run_ticks(plain, range(0, 41), 0.0)
        b = run_ticks(drop, range(0, 41), 0.0)+drop.poll(10.0)
        self.assertEqual(a, b)
        self.assertTrue(drop.done and not drop.failed and drop.summary()['drop']['drops'] == [])

    def test_a_stalled_client_is_reported_then_dropped_and_named_in_later_permits(self):
        b = make()
        run_ticks(b, range(0, 5), 0.0)
        out = send(b, 1, 5, 1.0, '0105')+send(b, 2, 5, 1.0, '0205')
        self.assertEqual(out, [])                                  # tick 5 waits for slot 3
        self.assertEqual(b.poll(1.5), [])
        self.assertEqual(b.poll(2.1), [])
        self.assertEqual(b.take_notices(), ['NATIVE_GATE STALL slot=3 tick=5 waited_ms=1100'])
        got = decoded(b.poll(4.1))
        self.assertEqual(got[0], (3, f'NGDROP|4|{TOKEN}|3|5'))
        self.assertEqual(b.take_notices(), ['NATIVE_GATE DROP slot=3 tick=5 waited_ms=3100 remaining=1,2'])
        # Tick 5 released: the permit for 7 carries tick 5's proposals with slot 3's stand-in (empty) and the mask.
        self.assertEqual(got[1:], [(s, f'NGGO|4|{TOKEN}|{s}|7|1/04|0105|0205|') for s in (1, 2)])
        self.assertTrue(b.states[5][3]['standin'] and b.states[5][3]['blob'] == '')
        # Slot 3's late report is answered with NGDROP and never becomes a row.
        self.assertEqual(decoded(send(b, 3, 5, 4.2, '0305')), [(3, f'NGDROP|4|{TOKEN}|3|5')])
        self.assertTrue(b.states[5][3]['standin'])
        # The others go on to the end: the mask stays in every permit, the clock and digest checks pass on the stand-ins.
        out = run_ticks(b, range(6, 41), 5.0, slots=(1, 2))
        got = decoded(out)
        goes = [p for s, p in got if p.startswith('NGGO')]
        self.assertTrue(goes and all('|1/04|' in p for p in goes) and all(p.endswith('|') for p in goes))
        self.assertEqual(sorted(s for s, p in got if p.startswith('NGSTOP')), [1, 2])
        self.assertTrue(b.done and not b.failed and not b.mismatches)
        summary = b.summary()['drop']
        self.assertEqual([d['slot'] for d in summary['drops']], [3])
        self.assertEqual(summary['stalls'][0]['slot'], 3)

    def test_slot_1_can_be_dropped_and_the_reference_is_the_lowest_live_slot(self):
        b = make()
        run_ticks(b, range(0, 3), 0.0)
        send(b, 2, 3, 1.0, '0203');send(b, 3, 3, 1.0, '0303')
        got = decoded(b.poll(4.5))
        self.assertEqual(got[0], (1, f'NGDROP|4|{TOKEN}|1|3'))
        self.assertEqual(got[1:], [(s, f'NGGO|4|{TOKEN}|{s}|5|1/01||0203|0303') for s in (2, 3)])
        run_ticks(b, range(4, 41), 5.0, slots=(2, 3))
        self.assertTrue(b.done and not b.failed and not b.mismatches)

    def test_nobody_is_dropped_while_every_live_client_is_missing(self):
        b = make()
        run_ticks(b, range(0, 3), 0.0)
        self.assertEqual(b.poll(60.0), [])
        self.assertEqual(b.dropped, {})

    def test_a_differing_digest_of_a_live_client_still_counts(self):
        b = make(observe=True)
        run_ticks(b, range(0, 3), 0.0)
        send(b, 1, 3, 1.0);send(b, 2, 3, 1.0)
        b.poll(4.5)
        b.receive(state(1, 4), addr(1), 5.0);b.receive(state(2, 4, digest='c'*64), addr(2), 5.0)
        self.assertEqual(b.mismatch_slots.get(4), [2])

    def test_refusals(self):
        with self.assertRaises(ValueError):
            make(lookahead=0)
        with self.assertRaises(ValueError):
            make(stall_drop_ms=500)
        with self.assertRaises(ValueError):
            make(peer_window=2)

    def test_cli_refuses_protocol_4_drops_with_crypto_join_or_without_a_window(self):
        base = [sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', '0', '--token', TOKEN, '--ticks', '10',
                '--clients', '3', '--stall-drop-ms', '3000', '--report', str(ROOT/'.local'/'drop-cli-unused.json')]
        for extra in ([], ['--lookahead', '1', '--join']):
            r = subprocess.run(base+extra, capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 2, r.stderr)
            self.assertIn('--stall-drop-ms without --adaptive needs --lookahead K', r.stderr)


if __name__ == '__main__':
    unittest.main()
