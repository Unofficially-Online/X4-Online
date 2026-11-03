"""Discrete-event model of peer links (roadmap B6) against relay-only protocol 4.

N clients follow lockstep_lookahead.h (protocol 4, window k) and, with a peer window m, the
peer rules of native_gate_peer.PeerWindow (lockstep_peer.h); the relay is the real
native_gate_relay.Barrier. Each client completes tick c at time E, sends its report (and its
record to every reachable peer) and starts c+1 on the relay permit or, inside the peer window,
on all N peer records of c-k. Datagrams take up(slot)/down(slot) seconds to/from the relay and
peer(i, j) seconds from i to j (None: the pair cannot connect); any may be lost. The same model
as SimulatedSession in tests/test_native_gate_lookahead.py, which peer=None reproduces.

python tools/dev/native_gate_peer_sim.py   prints the geometry table of
reports/LOCKSTEP-PEER-LINKS-20261101.txt (no game, no network).
"""
import heapq
import importlib.util
from pathlib import Path
import random
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import native_gate_peer as peerlib  # noqa: E402

_spec = importlib.util.spec_from_file_location('native_gate_relay_peer_sim', HERE/'native_gate_relay.py')
relay = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(relay)

TOKEN = 'a'*32
FRAME = 1/30                  # a paced 30 Hz step (native_gate_tick_hz=30); X4's own frame costs ~28-31 ms
RELAY = ('10.0.0.100', 9000)


def report_address(slot):
    return (f'10.0.0.{slot}', 10000+slot)


def peer_address(slot):
    return (f'10.0.0.{slot}', 20000+slot)


def state(slot, tick, blob, clients, k, digest):
    game = 20+tick/60
    fields = ['NGSTATE', '4', TOKEN, str(slot), str(tick), struct.pack('>d', game).hex(), struct.pack('>d', game).hex(),
              digest, str(clients), str(k), blob]
    return '|'.join(fields).encode()


class SimNat(peerlib.StubNat):
    """The simulated network decides reachability: a pair connects only if both directions do."""
    name = 'sim'

    def __init__(self, session, slot):
        self.session, self.slot = session, slot

    def route(self, slot, advertised):
        p = self.session.peer
        return advertised if p and p(self.slot, slot) is not None and p(slot, self.slot) is not None else None


class PeerSession:
    """k: protocol 4 window; m: peer window (None: relay only, today's protocol 4)."""

    def __init__(self, k, m=None, clients=2, ticks=150, frame=lambda s, t: FRAME, up=lambda s: .0055, down=lambda s: .0055,
                 peer=None, loss=0.0, peer_loss=None, seed=1, digest=lambda s, t: 'b'*64, crash=None, equivocate=None,
                 timeout=2.0, opt_out=()):
        self.k, self.m, self.n, self.ticks, self.frame, self.up, self.down = k, m, clients, ticks, frame, up, down
        self.peer = peer if m is not None else None
        self.rng, self.loss = random.Random(seed), loss
        self.peer_loss = loss if peer_loss is None else peer_loss
        self.digest, self.crash, self.equivocate, self.timeout = digest, crash or {}, equivocate or {}, timeout
        self.barrier = relay.Barrier(TOKEN, ticks=ticks, hold_tick=10**9, clients=clients, lookahead=k, remote=True,
                                     peer_window=m)
        slots = range(1, clients+1)
        # opt_out: clients that never send NGPHELLO (peer links not enabled on that PC).
        self.links = {s: peerlib.PeerWindow(s, clients, k, m, TOKEN, RELAY, nat=SimNat(self, s)) for s in slots
                      if s not in opt_out} if m is not None else {}
        self.events, self.sequence = [], 0
        self.completed = {s: 0 for s in slots}
        self.held = {s: {} for s in slots}
        self.sent = {s: {} for s in slots}
        self.waiting = {s: None for s in slots}
        self.stopped = {s: None for s in slots}
        self.starts = {s: {} for s in slots}
        self.applied = {s: {} for s in slots}
        self.via = {s: {} for s in slots}
        self.issued = {}

    def proposal(self, slot, tick):
        return f'{slot:02x}{tick & 0xff:02x}' if (tick+slot) % 3 else ''

    def push(self, at, kind, *data):
        self.sequence += 1
        heapq.heappush(self.events, (at, self.sequence, kind, data))

    def lost(self, rate):
        return rate and self.rng.random() < rate

    def to_relay(self, slot, packet, source, now):
        if not self.lost(self.loss):
            self.push(now+self.up(slot), 'relay', source, packet)

    def stop(self, slot, reason, now=None):
        if self.stopped[slot]:
            return
        self.stopped[slot] = reason
        if reason in (peerlib.STOP_PERMIT_CONFLICT, peerlib.STOP_MISMATCH, peerlib.STOP_EQUIVOCATION) and slot in self.links:
            # lockstep_peer.h tells the relay before the gate stops, so the others stop at once.
            self.to_relay(slot, peerlib.fail_notice(TOKEN, slot, self.completed[slot], reason), peer_address(slot), now)

    def hello(self, slot, now):
        link = self.links.get(slot)
        data = link.hello_due(now) if link else None
        if data:
            self.to_relay(slot, data, peer_address(slot), now)

    def report(self, slot, now):
        c = self.completed[slot]
        if self.stopped[slot]:
            return
        if c in self.crash.get(slot, ()):
            self.stopped[slot] = 'crashed'
            return
        blob = self.proposal(slot, c)
        packet = state(slot, c, blob, self.n, self.k, self.digest(slot, c))
        self.sent[slot][c] = [packet, now]
        for old in [t for t in self.sent[slot] if t < c-peerlib.LOOKAHEAD_MAX]:
            del self.sent[slot][old]
        self.to_relay(slot, packet, report_address(slot), now)
        link = self.links.get(slot)
        if link:
            for route, data in link.sealed(c, blob, now):
                target = int(route[0].rsplit('.', 1)[1])
                if (slot, c) in self.equivocate:   # a cheating client: another blob to its peers than to the relay
                    text = peerlib.record_text(TOKEN, slot, c, self.n, self.k, self.equivocate[(slot, c)])
                    data = f'{text}|'.encode()
                if not self.lost(self.peer_loss):
                    self.push(now+self.peer(slot, target), 'peer', target, peer_address(slot), data)
            self.hello(slot, now)
        self.try_run(slot, now)

    def try_run(self, slot, now):
        if self.stopped[slot]:
            return
        want, link = self.completed[slot]+1, self.links.get(slot)
        blobs = via = None
        if want in self.held[slot] and (link is None or link.may_run(want)):
            blobs, via = self.held[slot].pop(want), 'relay'
        elif link:
            blobs, via = link.take(want, now), 'peer'
        if blobs is not None:
            self.waiting[slot] = None
            self.starts[slot][want], self.applied[slot][want], self.via[slot][want] = now, list(blobs), via
            self.push(now+self.frame(slot, want), 'done', slot, want)
        elif self.waiting[slot] is None:
            self.waiting[slot] = now
            self.push(now, 'poll', slot)

    def poll(self, slot, now):
        if self.waiting[slot] is None or self.stopped[slot]:
            return
        if now-self.waiting[slot] > self.timeout:
            self.stopped[slot] = 7
            return
        c, link = self.completed[slot], self.links.get(slot)
        first = link.resend_from(c) if link else max(0, c-self.k)
        for tick, entry in sorted(self.sent[slot].items()):
            if tick >= first and now-entry[1] >= .1-1e-9:
                entry[1] = now
                self.to_relay(slot, entry[0], report_address(slot), now)
        self.hello(slot, now)
        self.push(now+.025, 'poll', slot)

    def deliver(self, slot, packet, now):
        """A relay datagram at the client's report socket."""
        f = packet.decode().split('|')
        if self.stopped[slot]:
            return
        if f[0] == 'NGSTOP':
            self.stop(slot, 1)
            return
        tick, c, link = int(f[4]), self.completed[slot], self.links.get(slot)
        assert f[5] == str(self.k) and len(f) == 6+self.n and tick <= c+1+self.k, ('permit beyond the window', slot, tick, c)
        blobs, source = f[6:], tick-1-self.k
        if blobs[slot-1] != (self.proposal(slot, source) if source >= 0 else ''):
            self.stop(slot, 56)
            return
        if tick > c:
            if tick in self.held[slot] and self.held[slot][tick] != blobs:
                self.stop(slot, 103, now)
                return
            self.held[slot][tick] = blobs
        reason = link.permit(tick, blobs, now) if link else 0
        if reason:
            self.stop(slot, reason, now)
        elif self.waiting[slot] is not None:
            self.try_run(slot, now)

    def at_peer_socket(self, slot, source, data, now):
        if self.stopped[slot]:
            return
        reason = self.links[slot].receive(data, source, now)
        if reason:
            self.stop(slot, reason, now)
        elif self.waiting[slot] is not None:
            self.try_run(slot, now)

    def at_relay(self, source, packet, now):
        for address, reply in self.barrier.receive(packet, source, now):
            f = reply.decode().split('|')
            if f[0] == 'NGGO':
                tick, origin = int(f[4]), int(f[4])-1-self.k
                needed = max(origin, 0)
                assert self.barrier.released >= needed and len(self.barrier.states[needed]) == self.n
                self.issued.setdefault(tick, now)
            target = int(address[0].rsplit('.', 1)[1])
            if self.lost(self.loss):
                continue
            if address[1] >= 20000:
                self.push(now+self.down(target), 'peer', target, RELAY, reply)
            else:
                self.push(now+self.down(target), 'client', target, reply)

    def run(self, horizon=120.0):
        for slot in self.completed:
            self.report(slot, 0.0)
        while self.events:
            now, _, kind, data = heapq.heappop(self.events)
            if now > horizon:
                break
            if kind == 'relay':
                self.at_relay(*data, now)
            elif kind == 'client':
                self.deliver(*data, now)
            elif kind == 'peer':
                self.at_peer_socket(*data, now)
            elif kind == 'poll':
                self.poll(*data, now)
            elif kind == 'done':
                slot, tick = data
                if not self.stopped[slot]:
                    self.completed[slot] = tick
                    self.report(slot, now)
            if all(self.stopped.values()):
                break
        return self

    def period(self, slot=1, first=None, last=None):
        first = 40 if first is None else first
        last = self.ticks-20 if last is None else last
        return (self.starts[slot][last]-self.starts[slot][first])/(last-first)

    def rate(self, **kw):
        """Steps per second, slowest client."""
        return min(1/self.period(slot, **kw) for slot in self.starts)

    def peer_share(self):
        runs = [via for slot in self.via for via in self.via[slot].values()]
        return sum(via == 'peer' for via in runs)/len(runs) if runs else 0.0

    def lead(self):
        """Mean time the peer copies completed before the relay permit (ticks where they did), seconds."""
        first = sum(link.stats['peer_first'] for link in self.links.values())
        return sum(link.stats['lead'] for link in self.links.values())/first if first else 0.0

    def peer_first_share(self):
        first = sum(link.stats['peer_first'] for link in self.links.values())
        total = first+sum(link.stats['relay_first'] for link in self.links.values())
        return first/total if total else 0.0


def symmetric(relay_one_way, peer_one_way, clients=2, unreachable=()):
    """up/down/peer functions: every client relay_one_way from the relay, every pair peer_one_way apart."""
    def peer(i, j):
        return None if (min(i, j), max(i, j)) in unreachable else peer_one_way
    return dict(clients=clients, up=lambda s: relay_one_way, down=lambda s: relay_one_way, peer=peer)


def geometry(relay_ms, peer_ms):
    """relay_ms: {slot: one-way ms to the relay}; peer_ms: {(i, j): one-way ms} (symmetric, missing = unreachable)."""
    def peer(i, j):
        value = peer_ms.get((min(i, j), max(i, j)))
        return None if value is None else value/1000
    return dict(clients=len(relay_ms), up=lambda s: relay_ms[s]/1000, down=lambda s: relay_ms[s]/1000, peer=peer)


def jittered(relay_ms, relay_jitter, peer_ms, peer_jitter, clients=2, seed=7):
    rng = random.Random(seed)
    return dict(clients=clients, up=lambda s: (relay_ms+rng.uniform(0, relay_jitter))/1000,
                down=lambda s: (relay_ms+rng.uniform(0, relay_jitter))/1000,
                peer=lambda i, j: (peer_ms+rng.uniform(0, peer_jitter))/1000)


# Name, geometry kwargs, description (one-way milliseconds).
GEOMETRIES = [
    ('G1 near players, far relay', symmetric(.060, .005), 'peers 5 ms apart (RTT 10), relay 60 ms from each (RTT 120)'),
    ('G2 one country, relay abroad', symmetric(.045, .015), 'peers 15 ms (RTT 30), relay 45 ms from each (RTT 90)'),
    ('G3 relay between players', symmetric(.020, .040), 'peers 40 ms (RTT 80), relay 20 ms from each (relay path = direct path)'),
    ('G4 relay on a better route', symmetric(.015, .050), 'peers 50 ms (RTT 100), relay 15 ms from each (relay path 30 ms)'),
    ('G5 relay at player 1', geometry({1: .5, 2: 40}, {(1, 2): 40}), 'player 1 next to the relay, player 2 40 ms from both'),
    ('G6 three players, one far', geometry({1: 60, 2: 60, 3: 60}, {(1, 2): 5, (1, 3): 60, (2, 3): 60}),
     'players 1-2 5 ms apart, player 3 60 ms from them and the relay (60 ms from all)'),
    ('G7 G1 with three players', symmetric(.060, .005, clients=3), 'as G1, three players'),
    ('G8 G7, pair 1-2 cannot connect', symmetric(.060, .005, clients=3, unreachable={(1, 2)}),
     'as G7, NAT module answers "use the relay" for pair 1-2'),
    ('G9 G1 with jitter', jittered(60, 30, 5, 10), 'relay 60..90 ms, peers 5..15 ms one-way, uniform per datagram'),
    ('G10 G1 with 2 % loss', dict(symmetric(.060, .005), loss=.02), 'as G1, every datagram (relay and peer) lost with 2 %'),
]


def run_case(kwargs, k, m, ticks=200, seed=1):
    session = PeerSession(k, m, ticks=ticks, seed=seed, **kwargs).run()
    assert session.barrier.summary()['verdict'] == 'pass', session.barrier.summary()['failure']
    return session


FULL = .99/FRAME               # "full rate": at least 99 % of 30 steps per second


def smallest(kwargs, peer, ticks):
    """(k, m, session) of the smallest input delay reaching the full rate: relay only, or peer links
    with the smallest m that still does; None when k + m <= 8 cannot."""
    for k in range(1, peerlib.LOOKAHEAD_MAX+1):
        if not peer:
            session = run_case(kwargs, k, None, ticks)
            if session.rate() >= FULL:
                return k, None, session
            continue
        if run_case(kwargs, k, peerlib.LOOKAHEAD_MAX-k, ticks).rate() < FULL:
            continue
        for m in range(0, peerlib.LOOKAHEAD_MAX-k+1):
            session = run_case(kwargs, k, m, ticks)
            if session.rate() >= FULL:
                return k, m, session
    return None


def table(ticks=200):
    """Per geometry: the same input delay (k=1) relay-only against peer links (m=7); the smallest
    delay reaching the full rate for each; the head start of peer records (m=0, records only)."""
    rows = ['f = 33.3 ms paced steps (30/s). A proposal sealed with tick t applies at t+1+k: order delay',
            '(k+1) steps, pilot delay (k+2) steps, in ms at the reached rate. Peer runs: steps started on peer',
            'records without the permit. Lead: how much earlier all peer records were there than the permit,',
            'mean over the ticks where they were first (records only, m=0).', '']
    for name, kwargs, description in GEOMETRIES:
        relay_k1, peer_k1, records = run_case(kwargs, 1, None, ticks), run_case(kwargs, 1, 7, ticks), run_case(kwargs, 1, 0, ticks)
        rows.append(f'{name}: {description}')
        rows.append(f'  k=1 (order delay 2 steps): relay only {relay_k1.rate():5.2f} steps/s; peer links m=7 '
                    f'{peer_k1.rate():5.2f} steps/s, {100*peer_k1.peer_share():.0f} % peer runs, at most '
                    f'{max(link.stats["behind_max"] for link in peer_k1.links.values())} ticks past the permits')
        for label, found in (('relay only', smallest(kwargs, False, ticks)), ('peer links', smallest(kwargs, True, ticks))):
            if found is None:
                best = [(k, m, run_case(kwargs, k, m, ticks).rate()) for k, m in
                        (((8, None), (4, None)) if label == 'relay only' else ((1, 7), (2, 6), (4, 4)))]
                rows.append(f'  full rate, {label}: not reached with k+m <= 8; ' + ', '.join(
                    f'k={k}{"" if m is None else f" m={m}"} {rate:5.2f} steps/s' for k, m, rate in best))
                continue
            k, m, session = found
            rate = session.rate()
            window = f'k={k}' if m is None else f'k={k} m={m}'
            rows.append(f'  full rate, {label}: {window:<7} {rate:5.2f} steps/s, order delay {1000*(k+1)/rate:3.0f} ms, '
                        f'pilot delay {1000*(k+2)/rate:3.0f} ms')
        rows.append(f'  records only (m=0): {records.rate():5.2f} steps/s (as relay only), peer records first on '
                    f'{100*records.peer_first_share():.0f} % of ticks, lead {1000*records.lead():.0f} ms')
        rows.append('')
    return '\n'.join(rows)


if __name__ == '__main__':
    print(table())
