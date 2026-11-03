"""Protocol 5 adaptive window: relay rules, schedule exactness, simulated clients over latency/jitter/spikes, CLI (no game).

python tests/test_native_gate_adaptive.py --table prints the speed-versus-round-trip tables of
reports/LOCKSTEP-ADAPTIVE-DELAY-20261101.txt (same seeds as the tests).
"""
import contextlib
import heapq
import importlib.util
import io
import json
from pathlib import Path
import random
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
import native_gate_relay as relay  # noqa: E402
import native_gate_adaptive as adaptive  # noqa: E402
spec = importlib.util.spec_from_file_location('native_gate_lookahead_tests', ROOT/'tests/test_native_gate_lookahead.py')
lookahead_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lookahead_tests)
TOKEN = 'a'*32
FRAME = 1/30


def state(slot, tick, blob='', clients=2, epoch=0, version=5, digest='b'*64, hz=30, token=TOKEN, delta=0):
    game = 20+tick/hz+delta
    fields = ['NGSTATE', str(version), token, str(slot), str(tick), struct.pack('>d', game).hex(), struct.pack('>d', game).hex(), digest]
    if version == 3: fields += [str(clients), blob]
    if version == 4: fields += [str(clients), '2', blob]
    if version == 5: fields += [str(clients), str(epoch), blob]
    return '|'.join(fields).encode()


def items(blob):
    """The simulated inputs of a blob: ('01', slot, made), ('02', slot, apply), ('03', slot, capture, apply)."""
    i = 0
    while i < len(blob):
        size = 16 if blob[i:i+2] == '03' else 10
        part = blob[i:i+size]
        yield (part[:2], int(part[2:4], 16))+tuple(int(part[j:j+6], 16) for j in range(4, size, 6))
        i += size


def address(slot):
    return ('127.0.0.1', 10000+slot)


def kinds(out, kind):
    return [(a[1]-10000, p.decode()) for a, p in out if p.startswith(kind.encode())]


def pong_all(b, out, now):
    """Answer every ping in out at time now (the client is waiting, so at once)."""
    for slot, packet in kinds(out, 'NGPING'):
        nonce = packet.split('|')[4]
        b.receive(f'NGPONG|5|{TOKEN}|{slot}|{nonce}|0'.encode(), address(slot), now)


def measured_barrier(clients=2, rtt=0.0, polls=10, **kw):
    """A barrier whose tick 0 reports are in and whose plan is made after polls pings per slot of round trip rtt (s)."""
    kw.setdefault('hold_tick', 10**6)
    kw.setdefault('tick_hz', 30)
    b = adaptive.AdaptiveBarrier(TOKEN, clients=clients, **kw)
    now = 0.0
    for s in range(1, clients+1):
        b.receive(state(s, 0, clients=clients), address(s), now)
    out = []
    for _ in range(polls):
        pings = b.poll(now)
        out += [x for x in pings if not x[1].startswith(b'NGPING')]
        now += rtt
        pong_all(b, pings, now)
        now += .021
    out += b.poll(now)
    return b, out, now


class ScheduleTests(unittest.TestCase):
    def test_apply_ticks_rise_by_one_except_gaps_and_quiet_ticks_carry_nothing(self):
        s = adaptive.Schedule([(0, 2, 5), (20, 4, 5), (40, 1, 5), (60, 3, 5)])
        self.assertEqual([s.lag(c) for c in (0, 19, 20, 36, 37, 39, 40, 60)], [2, 2, 4, 4, 4, 4, 1, 3])
        self.assertEqual([c for c in range(80) if s.quiet(c)], [37, 38, 39])        # lag 4 -> 1: three quiet ticks before 40
        applied = [s.apply(c) for c in range(80) if not s.quiet(c)]
        self.assertEqual(applied, sorted(set(applied)))                               # strictly increasing: nothing twice
        gaps = sorted(set(range(2, applied[-1]+1))-set(applied))
        self.assertEqual(gaps, [22, 23, 61, 62])                                      # lag 2 -> 4 at 20, 1 -> 3 at 60
        self.assertEqual([s.first_affected(i) for i in range(4)], [0, 20, 37, 60])
        self.assertEqual(s.next_source(36), 40)
        self.assertEqual(s.text(2), '40:1:5')

    def test_entries_are_refused_unless_they_follow_the_rules(self):
        s = adaptive.Schedule()
        for bad in ((1, 2, 4), (0, 0, 2), (0, 10, 30), (0, 2, 2), (0, 2, 31), (0, 2.0, 4)):
            self.assertFalse(s.valid(*bad), bad)
        s.add(0, 2, 4)
        for bad in ((10, 2, 4),      # no change
                    (10, 4, 5),      # the pilot delay is the plan's
                    (10, 4, 4),      # lag above pilot-1
                    (0, 3, 4),       # must start after the previous entry
                    (1, 1, 4)):      # its quiet tick (0) is not after the previous start
            self.assertFalse(s.valid(*bad), bad)
        self.assertTrue(s.valid(2, 1, 4))
        with self.assertRaises(ValueError):
            s.add(10, 9, 4, kmax=8)
        self.assertFalse(adaptive.Schedule([(0, 2, 4)]).valid(5, 4, 4, kmax=2))       # above the window maximum
        full = adaptive.Schedule([(0, 2, 30)]+[(10*i, 2+i % 2, 30) for i in range(1, adaptive.ENTRIES_MAX)])
        self.assertFalse(full.valid(10*adaptive.ENTRIES_MAX, 3, 30))


class AdaptiveBarrierTests(unittest.TestCase):
    def test_tick0_is_held_until_measured_then_the_plan_rides_the_first_permits(self):
        b = adaptive.AdaptiveBarrier(TOKEN, clients=2, hold_tick=99, kmax=4, tick_hz=30, samples=4)
        self.assertEqual(b.receive(state(1, 1), address(1), 0), [])                  # nothing permitted before tick 0
        self.assertEqual(b.receive(state(1, 0), address(1), 0), [])
        pings = b.poll(0)
        self.assertEqual(kinds(pings, 'NGPING'), [(1, f'NGPING|5|{TOKEN}|1|1')])      # only known peers are pinged
        pong_all(b, pings, .1)                                                        # 100 ms round trip
        self.assertEqual(b.receive(state(2, 0), address(2), 0.001), [])               # complete, not measured: held
        now = .021
        for _ in range(3):
            pings = b.poll(now)
            pong_all(b, pings, now+.1)
            now += .021
        pings = b.poll(now)
        self.assertEqual(kinds(pings, 'NGGO'), [])                                    # slot 2 has 3 samples so far
        pong_all(b, pings, now+.1)
        out = b.poll(now+.1)
        got = dict(((s, int(p.split('|')[4])), p) for s, p in kinds(out, 'NGGO'))
        # 100 ms + 10 ms margin at 30 Hz: k = ceil(110/33.3) = 4, lag 5, pilot delay 6; permits 1..4 are gaps.
        self.assertEqual(sorted(got), sorted((s, t) for t in range(1, 6) for s in (1, 2)))
        self.assertEqual(got[(1, 1)], f'NGGO|5|{TOKEN}|1|1|-|1|0:5:6|00||')
        self.assertEqual(got[(2, 5)], f'NGGO|5|{TOKEN}|2|5|0|1|0:5:6|00||')
        self.assertEqual(b.schedule.entries, [(0, 5, 6)])
        self.assertTrue(any(n.startswith('NATIVE_GATE PLAN epoch=1 k=4 lag=5 pilot_delay=6 rtt_p90_ms=100.') for n in b.take_notices()))
        summary = b.summary()['adaptive']
        self.assertEqual((summary['rtt_ms'][1]['n'], round(summary['rtt_ms'][2]['p90'])), (5, 100))
        self.assertEqual(summary['schedule'], [{'start': 0, 'lag': 5, 'k': 4, 'pilot': 6}])

    def test_round_trip_and_target_choose_the_window_within_its_bounds(self):
        for rtt, kw, k in ((0, {}, 1), (.02, {}, 1), (.05, {}, 2), (.1, {}, 4), (.3, {}, 4), (.1, {'margin_ms': 0}, 3),
                           (.1, {'target_hz': 24}, 3), (0, {'kmin': 2}, 2), (.3, {'kmax': 8}, 8), (.1, {'headroom': 2}, 4)):
            kw.setdefault('kmax', 4)
            b, out, _ = measured_barrier(rtt=rtt, **kw)
            self.assertEqual(b.schedule.entries[0][1]-1, k, (rtt, kw))
            self.assertEqual(b.schedule.entries[0][2], k+2+kw.get('headroom', 0))
            self.assertEqual(max(int(p.split('|')[4]) for _, p in kinds(out, 'NGGO')), k+1)

    def test_steady_releases_follow_the_lag_and_carry_the_sources(self):
        b, out, now = measured_barrier(rtt=.05, kmax=3)                                 # k = 2, lag 3
        self.assertEqual(b.schedule.entries, [(0, 3, 4)])
        for tick in range(1, 4):
            for s in (1, 2):
                out = b.receive(state(s, tick, blob=f'{s:02x}{tick:02x}', epoch=1), address(s), now)
        self.assertEqual(kinds(out, 'NGGO'), [(1, f'NGGO|5|{TOKEN}|1|6|3|1|0:3:4|00|0103|0203'),
                                              (2, f'NGGO|5|{TOKEN}|2|6|3|1|0:3:4|00|0103|0203')])
        # A resent report of an already released tick gets that tick's permits again, byte-identical.
        again = b.receive(state(1, 2, blob='0102', epoch=1), address(1), now)
        self.assertEqual(again, [(address(1), f'NGGO|5|{TOKEN}|1|5|2|1|0:3:4|00|0102|0202'.encode())])
        self.assertTrue(relay.permit_carries_inputs(again[0][1]))
        self.assertFalse(relay.permit_carries_inputs(f'NGGO|5|{TOKEN}|1|1|-|1|0:3:4|00||'.encode()))

    def test_agreement_is_enforced_fail_closed(self):
        cases = {'epoch ahead of the schedule': (state(1, 1, epoch=2), 'adaptive_epoch_slot_1_tick_1'),
                 'old transport': (state(2, 0, version=4), 'lookahead_mismatch_slot_2'),
                 'protocol 3': (state(1, 1, version=3), 'lookahead_mismatch_slot_1')}
        for name, (packet, reason) in cases.items():
            b, _, now = measured_barrier()
            b.receive(packet, address(int(packet.split(b'|')[3])), now)
            self.assertEqual(b.failed, reason, name)
        # A report of a tick an announced entry changes, sealed without knowing that entry.
        b, _, now = measured_barrier(changes=True, kmax=6, headroom=2, rtt=.1)       # k 4, lag 5, pilot 8: permits to 5
        b.schedule.add(4, 6, 8)
        b.receive(state(1, 3, epoch=1), address(1), now)
        self.assertIsNone(b.failed)
        b.receive(state(1, 4, epoch=1), address(1), now)
        self.assertEqual(b.failed, 'adaptive_unannounced_slot_1_tick_4')
        # Input sealed at a quiet tick.
        b, _, now = measured_barrier(changes=True, decrease=True, kmax=4, rtt=.1)   # lag 5
        b.schedule.add(6, 2, 6)                                                       # quiet 3, 4, 5
        b.receive(state(2, 2, epoch=2, blob='01'), address(2), now)
        self.assertIsNone(b.failed)
        out = b.receive(state(2, 3, epoch=2, blob='01'), address(2), now)
        self.assertEqual(b.failed, 'adaptive_quiet_input_slot_2_tick_3')
        self.assertEqual(out, [(address(2), f'NGSTOP|5|{TOKEN}|2|3'.encode())])
        # Malformed or unauthenticated reports and pongs never fail the session.
        for packet in (state(1, 1, token='0'*32), state(1, 1, epoch='01'), state(1, 1, clients=3), state(9, 1), state(1, 1)+b'|',
                       b'NGSTATE|5|'+b'0'*1020, f'NGPONG|5|{TOKEN}|1|999|0'.encode(), f'NGPONG|5|{TOKEN}|1|01|0'.encode(),
                       f'NGPONG|4|{TOKEN}|1|1|0'.encode(), f'NGPONG|5|{TOKEN}|1|1'.encode(), f'NGPONG|5|{TOKEN}|1|1|01'.encode(),
                       f'NGPONG|5|{TOKEN}|1|1|1000000001'.encode(), state(1, 1, blob='0A'), state(1, 1, epoch=99)):
            b, _, now = measured_barrier()
            self.assertEqual(b.receive(packet, address(1), now), [], packet[:60])
            self.assertIsNone(b.failed, packet[:60])
        b, _, now = measured_barrier()
        self.assertEqual(b.receive(state(1, 1, epoch=1, version=4), ('127.0.0.1', 20001), now), [])   # spoofed address
        self.assertIsNone(b.failed)

    def test_unmeasured_clients_stop_the_session_after_the_measure_time(self):
        b = adaptive.AdaptiveBarrier(TOKEN, clients=2, hold_tick=99, kmax=3, tick_hz=30, measure_seconds=1)
        b.receive(state(1, 0), address(1), 0);b.receive(state(2, 0), address(2), 0)
        for now in (0, .5):
            pong_all(b, [x for x in b.poll(now) if x[0] == address(1)], now+.01)          # slot 2 never answers
        self.assertIsNone(b.failed)
        out = b.poll(1.01)
        self.assertEqual(b.failed, 'adaptive_unmeasured_slot_2')
        self.assertEqual(sorted(kinds(out, 'NGSTOP')), [(1, f'NGSTOP|5|{TOKEN}|1|0'), (2, f'NGSTOP|5|{TOKEN}|2|0')])
        self.assertEqual(b.poll(1.2), [])                                                 # stopped once

    def test_constructor_refuses_invalid_settings(self):
        for kw in ({'kmax': 0}, {'kmax': 9}, {'kmin': 0}, {'kmin': 5, 'kmax': 4}, {'headroom': -1}, {'kmax': 8, 'headroom': 21},
                   {'samples': 1}, {'decrease': True}, {'margin_ms': -1}, {'measure_seconds': 0}, {'target_hz': 61},
                   {'stall_report_ms': 1000, 'stall_drop_ms': 500}, {'session_ping_ms': 0}, {'margin_ms': float('nan')}):
            with self.assertRaises(ValueError, msg=kw):
                adaptive.AdaptiveBarrier(TOKEN, **kw)


class Client:
    """One native client of lockstep_adaptive.h: window, schedule, quiet ticks, pongs, inputs."""

    def __init__(self, sim, slot, kmax):
        self.sim, self.slot, self.kmax = sim, slot, kmax
        self.completed, self.cursor, self.mask = 0, 0, 0
        self.schedule = adaptive.Schedule()
        self.held, self.sent, self.sealed = {}, {}, {}
        self.waiting, self.stopped, self.running = None, None, False
        self.inbox = []                                       # pings read at the next drain
        self.empty = 0.0                                      # when the socket was last found empty
        self.pending = []                                     # generic inputs not yet proposed
        self.starts, self.applied, self.sources = {}, {}, {}
        self.staged_orders, self.staged_pilot = {}, {}

    def fail(self, reason):
        self.stopped = self.stopped or reason

    # inputs: kind 1 generic (slot, made), kind 2 order (slot, apply tick), kind 3 pilot record (slot, capture, apply)
    def proposal(self, c):
        if self.schedule.entries and self.schedule.quiet(c):
            return ''
        items = [f'01{self.slot:02x}{made:06x}' for made in self.pending[:20]]
        self.pending = self.pending[20:]
        if not self.schedule.entries:
            return ''.join(items)                                              # tick 0: sealed before the plan
        lag = self.schedule.lag(c)
        if self.sim.orders(self.slot, c):
            items.append(f'02{self.slot:02x}{c+lag:06x}')                      # orders: delay = lag at the proposal
        if self.sim.pilot:
            pilot = lag+1 if self.sim.pilot == 'schedule' else self.schedule.entries[0][2]
            items.append(f'03{self.slot:02x}{c:06x}{c+pilot:06x}')
        return ''.join(items)

    def report(self, now):
        c = self.completed
        if self.stopped or c in self.sim.crash.get(self.slot, ()):
            self.stopped = self.stopped or 'crashed'
            return
        if self.sim.makes(self.slot, c):
            self.pending.append(c)
        blob = self.proposal(c)
        if blob and self.schedule.entries and self.schedule.quiet(c):
            return self.fail('quiet')
        self.sealed[c] = blob
        packet = state(self.slot, c, blob=blob, clients=self.sim.n, epoch=len(self.schedule), digest=self.sim.digest(self.slot, c))
        self.sent[c] = [packet, now]
        for old in [t for t in self.sent if t < c-self.kmax]:
            del self.sent[old]
        self.sim.to_relay(self.slot, packet, now)
        self.drain(now)
        self.try_run(now)

    def drain(self, now):
        for nonce in self.inbox:
            held = round(1e6*(now-self.empty))
            self.sim.to_relay(self.slot, f'NGPONG|5|{TOKEN}|{self.slot}|{nonce}|{held}'.encode(), now)
        self.inbox = []
        self.empty = now

    def deliver(self, packet, now):
        f = packet.decode().split('|')
        if self.stopped:
            return
        if f[0] == 'NGPING':
            # Read at once while waiting (blocked in a receive: held 0); else at the next drain after the frame,
            # naming the time since the socket was last found empty (the start of the frame).
            if self.running: self.inbox.append(f[4])
            else: self.sim.to_relay(self.slot, f'NGPONG|5|{TOKEN}|{self.slot}|{f[4]}|0'.encode(), now)
            return
        if f[0] in ('NGSTOP', 'NGDROP'):
            return self.fail(f[0])
        tick = int(f[4])
        if tick <= self.completed:
            return
        if tick > self.completed+1+self.kmax:
            return self.fail('ahead')
        source = None if f[5] == '-' else int(f[5])
        epoch, entry, mask, blobs = int(f[6]), tuple(map(int, f[7].split(':'))), int(f[8], 16), f[9:]
        if len(blobs) != self.sim.n:
            return self.fail('malformed')
        if source is None:
            if any(blobs): return self.fail('gap_inputs')
        elif not (source <= self.completed and tick-source <= self.kmax+1 and blobs[self.slot-1] == self.sealed[source]):
            return self.fail('own_proposal')
        if epoch == len(self.schedule)+1:
            if not self.schedule.valid(*entry, self.kmax):
                return self.fail('entry')
            self.schedule.add(*entry, self.kmax)
            if len(self.schedule) > 1 and self.schedule.first_affected(len(self.schedule)-1) <= self.completed:
                return self.fail('late_entry')
        elif not 1 <= epoch <= len(self.schedule) or tuple(self.schedule.entries[epoch-1]) != entry:
            return self.fail('entry_conflict')
        permit = (source, epoch, mask, blobs)
        if tick in self.held and self.held[tick] != permit:
            return self.fail('conflict')
        self.held[tick] = permit
        if self.waiting is not None:
            self.try_run(now)

    def try_run(self, now):
        if self.stopped or self.running:
            return
        want = self.completed+1
        if want not in self.held:
            if self.waiting is None:
                self.waiting = now
                self.sim.push(now, 'poll', self.slot)
            return
        source, epoch, mask, blobs = self.held.pop(want)
        expected = self.cursor if self.schedule.apply(self.cursor) == want else None
        if source != expected:
            return self.fail('source')
        if expected is not None:
            self.cursor = self.schedule.next_source(self.cursor)
        if mask & self.mask != self.mask:
            return self.fail('mask')
        self.mask = mask
        if mask >> (self.slot-1) & 1:
            return self.fail('dropped')
        self.waiting = None
        self.starts[want], self.applied[want], self.sources[want] = now, tuple(blobs), source
        for blob in blobs:
            for item in items(blob):
                if item[0] == '02':
                    if item[2] < want: self.sim.late.append(('order', self.slot, want, item))
                    self.staged_orders.setdefault(item[2], []).append(item)
                elif item[0] == '03':
                    if item[3] <= want: self.sim.late.append(('pilot', self.slot, want, item))
                    staged = self.staged_pilot.setdefault(item[1], {})
                    if item[3] in staged: self.sim.late.append(('pilot_twice', self.slot, item))
                    staged[item[3]] = item[2]
        self.running = True
        self.empty = now
        self.sim.push(now+self.sim.frame(self.slot, want), 'done', self.slot, want)

    def poll(self, now):
        if self.waiting is None or self.stopped:
            return
        if now-self.waiting > self.sim.timeout:
            return self.fail('timeout')
        for entry in self.sent.values():
            if now-entry[1] >= .1-1e-9:
                entry[1] = now
                self.sim.to_relay(self.slot, entry[0], now)
        self.sim.push(now+.025, 'poll', self.slot)

    def done(self, tick, now):
        self.running = False
        self.completed = tick
        self.report(now)


class AdaptiveSession:
    """Discrete-event model: N clients (Client), the real AdaptiveBarrier, per-datagram delays and losses.

    up/down(slot, now) give one-way delays (jitter and spikes come from the functions); the relay's poll
    runs every 5 ms. Inputs: generic items (queued, proposed at the next non-quiet tick), orders naming
    their apply tick (proposal + lag) and, with pilot, one record per tick ('schedule': delay lag+1;
    'fixed': the plan's pilot delay, as the native pilot modules do today).
    """

    def __init__(self, clients=2, ticks=400, kmax=4, frame=lambda s, t: FRAME, up=None, down=None, loss=0.0, seed=1,
                 pilot=None, makes=None, orders=None, crash=None, digest=None, timeout=60.0, **barrier):
        self.n, self.ticks, self.frame, self.loss, self.pilot = clients, ticks, frame, loss, pilot
        self.up = up or (lambda s, now: .0005)
        self.down = down or (lambda s, now: .0005)
        self.rng = random.Random(seed)
        self.makes = makes or (lambda s, c: (c*7+s) % 5 == 0)
        self.orders = orders or (lambda s, c: (c+s) % 11 == 0)
        self.crash, self.digest, self.timeout = crash or {}, digest or (lambda s, t: 'b'*64), timeout
        barrier.setdefault('hold_tick', 10**9)
        barrier.setdefault('tick_hz', 30)
        self.barrier = adaptive.AdaptiveBarrier(TOKEN, ticks=ticks, clients=clients, kmax=kmax, **barrier)
        self.clients = {s: Client(self, s, kmax) for s in range(1, clients+1)}
        self.events, self.sequence, self.late = [], 0, []
        self.notices = []

    def push(self, at, kind, *data):
        self.sequence += 1
        heapq.heappush(self.events, (at, self.sequence, kind, data))

    def to_relay(self, slot, packet, now):
        if not (self.loss and self.rng.random() < self.loss):
            self.push(now+self.up(slot, now), 'relay', slot, packet)

    def send(self, out, now):
        for address_, packet in out:
            slot = address_[1]-10000
            if not (self.loss and self.rng.random() < self.loss):
                self.push(now+self.down(slot, now), 'client', slot, packet)

    def run(self, horizon=600.0):
        for client in self.clients.values():
            client.report(0.0)
        self.push(0.0, 'relay_poll')
        while self.events:
            now, _, kind, data = heapq.heappop(self.events)
            if now > horizon:
                break
            if kind == 'relay':
                slot, packet = data
                self.send(self.barrier.receive(packet, address(slot), now), now)
            elif kind == 'relay_poll':
                self.send(self.barrier.poll(now), now)
                self.notices += self.barrier.take_notices()
                if not (self.barrier.done or self.barrier.failed) or not all(c.stopped for c in self.clients.values()):
                    self.push(now+.005, 'relay_poll')
            elif kind == 'client':
                self.clients[data[0]].deliver(data[1], now)
            elif kind == 'poll':
                self.clients[data[0]].poll(now)
            elif kind == 'done':
                self.clients[data[0]].done(data[1], now)
            if all(c.stopped for c in self.clients.values()):
                break
        return self

    def rate(self, slot=1, first=None, last=None):
        starts = self.clients[slot].starts
        first, last = first or min(starts)+60, last or max(starts)
        return (last-first)/(starts[last]-starts[first])


class AdaptiveSimulationTests(unittest.TestCase):
    def assert_exact(self, sim, slots=None):
        """Every client applied the same permits; every generic input exactly once, in order, after it was made;
        no order or pilot record late; pilot apply ticks once each."""
        slots = slots or list(sim.clients)
        reference = sim.clients[slots[0]]
        for s in slots:
            c = sim.clients[s]
            common = set(c.applied) & set(reference.applied)
            self.assertTrue(all(c.applied[t] == reference.applied[t] for t in common))
            self.assertTrue(all(c.sources[t] == reference.sources[t] for t in common))
        self.assertEqual(sim.late, [])
        seen = {}
        for tick in sorted(reference.applied):
            for blob in reference.applied[tick]:
                for item in items(blob):
                    if item[0] == '01':
                        key = item[1:]
                        self.assertNotIn(key, seen, 'an input applied twice')
                        self.assertLess(key[1], tick)
                        seen[key] = tick
        for s in slots:
            made = [c for c in range(max(reference.applied)) if sim.makes(s, c) and c+20 < max(reference.applied)-sim.barrier.kmax-8]
            for c in made:
                if s in sim.barrier.dropped and c >= sim.barrier.dropped[s]:
                    continue
                self.assertIn((s, c), seen, f'input of slot {s} made at {c} never applied')
            ordered = [seen[(s, c)] for c in sorted(c for (t, c) in seen if t == s)]
            self.assertEqual(ordered, sorted(ordered))
        # Sources: strictly increasing, never a proposal tick twice, quiet ticks never carried.
        sources = [reference.sources[t] for t in sorted(reference.sources) if reference.sources[t] is not None]
        self.assertEqual(sources, sorted(set(sources)))
        self.assertFalse(any(sim.barrier.schedule.quiet(c) for c in sources))
        missing = set(range(sources[0], sources[-1]+1))-set(sources)
        self.assertTrue(all(sim.barrier.schedule.quiet(c) for c in missing), sorted(missing)[:10])

    def test_plan_at_tick0_reaches_the_frame_rate_for_every_round_trip(self):
        for rtt_ms, k in ((0, 1), (40, 2), (100, 4), (150, 5), (200, 7)):
            half = rtt_ms/2000
            sim = AdaptiveSession(ticks=300, kmax=8, up=lambda s, now, h=half: h, down=lambda s, now, h=half: h).run()
            self.assertEqual(sim.barrier.summary()['verdict'], 'pass', rtt_ms)
            self.assertEqual(sim.barrier.schedule.entries[0][1]-1, k, rtt_ms)
            self.assertGreater(sim.rate(), 29.5, (rtt_ms, sim.rate()))
            self.assert_exact(sim)
            self.assertEqual({c.stopped for c in sim.clients.values()}, {'NGSTOP'})

    def test_fixed_window_falls_behind_where_the_plan_does_not(self):
        # Protocol 4 with today's people window (k=1, delay 3) against the plan, 100 ms round trip.
        fixed = lookahead_tests.SimulatedSession(1, clients=2, ticks=200, frame=lambda s, t: FRAME, up=lambda s: .05, down=lambda s: .05).run()
        self.assertAlmostEqual(1/fixed.period(first=100, last=190), 15.0, delta=.2)       # (100+33.3)/2 ms a step
        planned = AdaptiveSession(ticks=200, up=lambda s, now: .05, down=lambda s, now: .05).run()
        self.assertGreater(planned.rate(), 29.5)

    def test_jitter_is_absorbed_by_the_p90_plan_and_inputs_stay_exact(self):
        rng = random.Random(7)
        one_way = lambda s, now: .04+rng.uniform(0, .03)                                   # round trip 80..140 ms
        sim = AdaptiveSession(ticks=500, kmax=8, up=one_way, down=one_way, pilot='fixed').run()
        k = sim.barrier.schedule.entries[0][1]-1
        self.assertIn(k, (4, 5))
        self.assertEqual(sim.barrier.summary()['verdict'], 'pass')
        self.assertGreater(sim.rate(), 29)
        self.assert_exact(sim)

    def test_session_changes_follow_a_spike_up_and_down_and_lose_nothing(self):
        # 40 ms round trip; from 6 s to 12 s it is 240 ms, then 40 ms again.
        def one_way(s, now):
            return .12 if 6 <= now < 12 else .02
        runs = {}
        for changes in (False, True):
            sim = AdaptiveSession(ticks=900, kmax=8, up=one_way, down=one_way, pilot='schedule', changes=changes, decrease=changes,
                                  headroom=6, interval_seconds=1.0, settle=2).run()
            self.assertEqual(sim.barrier.summary()['verdict'], 'pass', changes)
            self.assert_exact(sim)
            runs[changes] = sim
        lags = [lag for _, lag, _ in runs[True].barrier.schedule.entries]
        self.assertEqual(lags[0], 3)                                                       # k=2 for 40 ms
        self.assertGreater(max(lags), 7)                                                   # raised during the spike
        self.assertEqual(lags[-1], 3)                                                      # lowered again after it
        self.assertTrue(any('quiet=' in n and 'gaps=0' in n for n in runs[True].notices))
        # Time for the 900 ticks: the schedule wins back most of what the spike costs a fixed plan.
        end = {c: max(sim.clients[1].starts.values()) for c, sim in runs.items()}
        self.assertLess(end[True], end[False]-2.0, end)

    def test_changes_keep_inputs_exact_under_jitter_spikes_loss_and_mixed_clients(self):
        for seed in range(4):
            rng = random.Random(seed)
            base = {s: rng.uniform(.002, .06) for s in range(1, 5)}
            frames = {s: rng.uniform(.02, .04) for s in range(1, 5)}

            def one_way(s, now, rng=rng, base=base):
                spike = .1 if int(now) % 7 == 3 else 0
                return base[s]+spike+rng.uniform(0, .02)
            sim = AdaptiveSession(clients=4, ticks=600, kmax=8, seed=seed, up=one_way, down=one_way, loss=.02, pilot='schedule',
                                  frame=lambda s, t: max(FRAME, frames[s]*(1.6 if t % 13 == 0 else 1)),
                                  changes=True, decrease=True, headroom=4, interval_seconds=.7, settle=1).run()
            self.assertEqual(sim.barrier.summary()['verdict'], 'pass', seed)
            self.assertGreaterEqual(len(sim.barrier.schedule), 2, seed)
            self.assert_exact(sim)

    def test_fixed_pilot_delay_bounds_session_raises_and_refuses_lowering(self):
        # Native pilot records keep the plan's delay: raises stay within pilot-1 (headroom), lowering would
        # make quiet ticks without records, so a client with pilot records must not get one.
        def one_way(s, now):
            return .12 if 4 <= now < 9 else .005
        sim = AdaptiveSession(ticks=600, kmax=8, up=one_way, down=one_way, pilot='fixed', changes=True, headroom=2,
                              interval_seconds=1.0).run()
        self.assertEqual(sim.barrier.summary()['verdict'], 'pass')
        entries = sim.barrier.schedule.entries
        self.assertEqual((entries[0][1], entries[0][2]), (2, 5))                           # k=1, pilot 1+2+2
        self.assertEqual(max(lag for _, lag, _ in entries), 4)                             # capped at pilot-1
        self.assert_exact(sim)

    def test_a_stalled_client_is_reported_then_dropped_and_the_others_go_on(self):
        sim = AdaptiveSession(clients=3, ticks=400, kmax=4, crash={2: range(150, 10**6)}, stall_report_ms=500, stall_drop_ms=2000).run()
        b = sim.barrier
        self.assertEqual(b.summary()['verdict'], 'pass')
        self.assertEqual(b.dropped, {2: 150})
        self.assertEqual([(s['slot'], s['tick']) for s in b.stalls], [(2, 150)])
        self.assertEqual(sim.clients[2].stopped, 'crashed')
        self.assertEqual({sim.clients[s].stopped for s in (1, 3)}, {'NGSTOP'})
        self.assertEqual(max(sim.clients[1].starts), 400)
        # From the drop on, slot 2's blob is empty and the mask names it, identically on both clients.
        c1 = sim.clients[1]
        for tick, source in c1.sources.items():
            if source is not None and source >= 150:
                self.assertEqual(c1.applied[tick][1], '')
        self.assertTrue(any(n.startswith('NATIVE_GATE STALL slot=2 tick=150') for n in sim.notices))
        self.assertTrue(any(n.startswith('NATIVE_GATE DROP slot=2 tick=150') for n in sim.notices))
        self.assert_exact(sim, slots=[1, 3])
        # Without a drop bound the stall is reported and everyone waits (the clients time out, nothing is guessed).
        sim = AdaptiveSession(clients=3, ticks=400, kmax=4, crash={2: range(150, 10**6)}, stall_report_ms=500, timeout=5).run(horizon=60)
        self.assertEqual(sim.barrier.dropped, {})
        self.assertEqual({sim.clients[s].stopped for s in (1, 3)}, {'timeout'})
        self.assertEqual(len(sim.barrier.stalls), 1)

    def test_a_late_client_rejoining_after_its_drop_is_told_so(self):
        b, _, now = measured_barrier(clients=3, kmax=2, stall_report_ms=100, stall_drop_ms=300)
        for tick in range(1, 4):
            for s in (1, 2, 3):
                b.receive(state(s, tick, clients=3, epoch=1), address(s), now)
        b.receive(state(1, 4, clients=3, epoch=1), address(1), now)
        b.receive(state(3, 4, clients=3, epoch=1), address(3), now)
        out = b.poll(now+.35)
        self.assertEqual(b.dropped, {2: 4})
        got = dict(((s, int(p.split('|')[4])), p) for s, p in kinds(out, 'NGGO'))
        self.assertEqual(sorted(got), [(1, 6), (3, 6)])
        self.assertEqual(got[(1, 6)], f'NGGO|5|{TOKEN}|1|6|4|1|0:2:3|02|||')
        self.assertEqual(b.receive(state(2, 4, clients=3, epoch=1), address(2), now+.4), [(address(2), f'NGDROP|5|{TOKEN}|2|4'.encode())])
        # The digest of a dropped slot is no longer compared.
        b.receive(state(1, 5, clients=3, epoch=1), address(1), now+.5)
        b.receive(state(3, 5, clients=3, epoch=1), address(3), now+.5)
        self.assertEqual((b.released, b.failed), (5, None))


class RelayProcessAdaptiveTests(unittest.TestCase):
    def test_cli_refuses_inconsistent_options(self):
        for extra in (['--adaptive'], ['--adaptive', '--lookahead', '2', '--live-inputs'], ['--adaptive', '--lookahead', '2', '--adaptive-min', '3'],
                      ['--latency-jitter-ms', '-1'], ['--latency-spike', '0,10,10'], ['--latency-spike', 'x'],
                      ['--adaptive', '--lookahead', '2', '--adaptive-decrease']):
            with tempfile.TemporaryDirectory() as d:
                argv = ['relay', '--port', '12345', '--token', TOKEN, '--report', str(Path(d)/'r.json'), *extra]
                with mock.patch('sys.argv', argv), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit, msg=extra):
                        relay.main()

    def test_link_delay_jitter_and_spikes(self):
        d = relay.LinkDelay(100, 40, (10, 2000, 300), seed=3, start=100.0)
        values = [d(100.0+5+i/100) for i in range(100)]                                   # outside the spike
        self.assertTrue(all(.05 <= v <= .07 for v in values) and max(values)-min(values) > .01)
        self.assertTrue(all(.2 <= d(100.0+i/100) <= .22 for i in range(150)))               # inside: +150 ms each way
        self.assertFalse(relay.LinkDelay())
        self.assertEqual(relay.parse_spike('20,2000,300'), (20.0, 2000.0, 300.0))

    def test_real_udp_plan_with_latency_and_jitter(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
            reservation.close()
            report = Path(directory)/'report.json'
            process = subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', str(port), '--token', TOKEN,
                                        '--hold-tick', '99', '--clients', '2', '--ticks', '4', '--lookahead', '4', '--tick-hz', '30',
                                        '--report', str(report), '--adaptive', '--adaptive-samples', '4', '--latency-ms', '60',
                                        '--latency-jitter-ms', '10', '--latency-seed', '1'],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            peers = []
            try:
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY LATENCY round_trip_ms=60')
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY JITTER jitter_ms=10 spike=none')
                for _ in range(2):
                    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    peer.bind(('127.0.0.1', 0));peers.append(peer)
                for slot, peer in enumerate(peers, 1):
                    peer.sendto(state(slot, 0, blob='0a' if slot == 1 else ''), ('127.0.0.1', port))
                permits = {1: {}, 2: {}}
                deadline = time.perf_counter()+10
                # Waiting clients: answer pings at once, collect permits (the plan rides permit 1).
                while time.perf_counter() < deadline and not all(len(p) >= 4 for p in permits.values()):
                    for peer in select.select(peers, [], [], .005)[0]:
                        slot = peers.index(peer)+1
                        data = peer.recv(8192).decode()
                        f = data.split('|')
                        if f[0] == 'NGPING': peer.sendto(f'NGPONG|5|{TOKEN}|{slot}|{f[4]}|0'.encode(), ('127.0.0.1', port))
                        elif f[0] == 'NGGO': permits[slot][int(f[4])] = data
                # 60..70 ms round trip + 10 ms margin at 30 Hz: k = 3 (lag 4, pilot 5); a coarse Windows timer
                # (select timeouts in 15.6 ms steps) can add a hop's worth: then k = 4 and only permits 1..4 count.
                plan = permits[1][1].split('|')[7]
                self.assertIn(plan, ('0:4:5', '0:5:6'))
                self.assertEqual(permits[1][1], f'NGGO|5|{TOKEN}|1|1|-|1|{plan}|00||')
                if plan == '0:4:5':
                    self.assertEqual(permits[2][4], f'NGGO|5|{TOKEN}|2|4|0|1|0:4:5|00|0a|')
                for tick in range(1, 5):
                    for slot, peer in enumerate(peers, 1):
                        peer.sendto(state(slot, tick, epoch=1), ('127.0.0.1', port))
                for line in iter(process.stdout.readline, ''):
                    if 'done=True' in line: break
                saved = json.loads(report.read_text())
                self.assertEqual((saved['verdict'], saved['protocol_version']), ('pass', 5))
                lag = int(plan.split(':')[1])
                self.assertEqual(saved['adaptive']['schedule'], [{'start': 0, 'lag': lag, 'k': lag-1, 'pilot': lag+1}])
                self.assertGreaterEqual(saved['adaptive']['rtt_ms']['1']['min'], 59)
            finally:
                for peer in peers: peer.close()
                process.terminate();process.communicate(timeout=5)


def table():
    """The report's tables: steps per second at 30 Hz, 2 clients, for fixed windows and the plan."""
    print('round trip ms | fixed k=1 d=3 | fixed k=3 d=5 | adaptive: k, pilot delay, steps/s')
    for rtt in (0, 20, 50, 100, 150, 200, 300):
        half = rtt/2000
        fixed = []
        for k in (1, 3):
            sim = lookahead_tests.SimulatedSession(k, clients=2, ticks=300, frame=lambda s, t: FRAME, up=lambda s, h=half: h,
                                                   down=lambda s, h=half: h).run()
            fixed.append(1/sim.period(first=100, last=280))
        sim = AdaptiveSession(ticks=300, kmax=8, up=lambda s, now, h=half: h, down=lambda s, now, h=half: h).run()
        lag, pilot = sim.barrier.schedule.entries[0][1:]
        print(f'{rtt:13} | {fixed[0]:13.1f} | {fixed[1]:13.1f} | k={lag-1} d={pilot} {sim.rate():.1f}')
    print('\njitter: one-way 40..70 ms uniform (round trip 80..140), 500 ticks')
    rng = random.Random(7)
    one_way = lambda s, now: .04+rng.uniform(0, .03)
    for k in (1, 3, 4, 5):
        rng.seed(7)
        sim = lookahead_tests.SimulatedSession(k, clients=2, ticks=500, frame=lambda s, t: FRAME, up=lambda s: one_way(s, 0),
                                               down=lambda s: one_way(s, 0)).run()
        print(f'fixed k={k}: {1/sim.period(first=100, last=480):.1f} steps/s')
    rng.seed(7)
    sim = AdaptiveSession(ticks=500, kmax=8, up=one_way, down=one_way).run()
    print(f'adaptive: k={sim.barrier.schedule.entries[0][1]-1} {sim.rate():.1f} steps/s')
    print('\nspike: 40 ms round trip, 240 ms from 6 s to 12 s, 900 ticks')

    def spike(s, now):
        return .12 if 6 <= now < 12 else .02
    for changes in (False, True):
        sim = AdaptiveSession(ticks=900, kmax=8, up=spike, down=spike, pilot='schedule', changes=changes, decrease=changes,
                              headroom=6, interval_seconds=1.0, settle=2).run()
        starts = sim.clients[1].starts
        print(f'changes={changes}: 900 ticks in {max(starts.values())-starts[1]:.2f} s, schedule '
              f'{[(s, l-1) for s, l, _ in sim.barrier.schedule.entries]}')
    print('\nlive comparison: frame 36.6 ms (27.3 steps/s measured without latency), round trip 100 and 125 ms')
    for rtt in (100, 125):
        half = rtt/2000
        row = []
        for k in (1, 3, 4, 5):
            sim = lookahead_tests.SimulatedSession(k, clients=2, ticks=300, frame=lambda s, t: .0366, up=lambda s, h=half: h,
                                                   down=lambda s, h=half: h).run()
            row.append(f'k={k} {1/sim.period(first=100, last=280):.1f}')
        print(f'{rtt} ms: '+', '.join(row))


if __name__ == '__main__':
    if '--table' in sys.argv:
        table()
    else:
        unittest.main()
