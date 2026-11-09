"""Loopback coordinator for opt-in native FrameTick gates; no engine memory access."""
import argparse
import hmac
import importlib.util
import heapq
import json
import math
from pathlib import Path
import random
import socket
import struct
import sys
import time

# Protocol 3 (docs/LOCKSTEP-NSLOT-WIRE.txt): N=2..8 clients, one opaque input
# blob of 0..480 lowercase hex characters (whole bytes) per client and tick.
CLIENTS_MAX = 8
BLOB_HEX_MAX = 480
STATE_MAX = {1: 512, 2: 512, 3: 1024}   # matches GATE_STATE_MAX in lockstep_wire.h (protocol 4 as 3)
# Protocol 4 = the protocol 3 input wire with a permit window of k=1..8 ticks
# (--lookahead; GATE_LOOKAHEAD_MAX in lockstep_wire.h). The permit for tick T
# needs every client's report of tick T-1-k (of tick 0 for T <= k+1) and
# carries the blobs sealed with tick T-1-k (empty for T <= k).
LOOKAHEAD_MAX = 8
# Peer links, extension 1 over protocol 4 (--peer-window M; tools/dev/native_gate_peer.py,
# src/native/engine/lockstep_peer.h): clients also send each sealed proposal straight to the
# other players and may run up to M ticks past the newest permit they hold on those copies.
# The relay then accepts reports up to M ticks past its newest permit and hands out the
# peer sockets' addresses once every client opted in (NGPHELLO). k + M <= LOOKAHEAD_MAX.
PEER_VERSION = 1
# native_gate_tick_hz (src/native/engine/lockstep_tick.h).
TICK_RATES = (60, 40, 30, 20)
HEX = frozenset('0123456789abcdef')
# Observe mode: while digests keep differing, repeat the DESYNC notice at most
# once per this many ticks (a real desync usually differs on every later tick).
DESYNC_REPEAT = 600


def blob_valid(value):
    return len(value) <= BLOB_HEX_MAX and len(value) % 2 == 0 and all(c in HEX for c in value)


def canonical(value):
    return value.isdecimal() and value.isascii() and str(int(value)) == value


def permit_carries_inputs(packet):
    """A protocol 4 or 5 permit with any non-empty blob (NGGO|4|token|slot|T|k|blobs..., NGGO|5|token|slot|T|source|
    epoch|entry|mask|blobs...)."""
    if packet.startswith(b'NGGO|5|'):
        return any(packet.split(b'|')[9:])
    return packet.startswith(b'NGGO|4|') and any(packet.split(b'|')[6:])


class LiveInputs:
    """Opt-in loopback command inboxes; only each game proposes its inputs."""
    def __init__(self, token, clients=2):
        self.token = token
        self.rows = {slot: {} for slot in range(1, clients+1)}
        self.captured = {slot: 0 for slot in range(1, clients+1)}
        self.peers = {}

    def receive(self, data, address):
        if address[0] != '127.0.0.1' or len(data) > 256:
            return []
        try:
            f = data.decode('ascii').split('|')
            if len(f) != 6 or f[0] not in ('NGCMD', 'NGPOLL') or f[1] != '1' or not hmac.compare_digest(f[2], self.token):
                return []
            if any(not value.isdecimal() or str(int(value)) != value for value in f[3:5]):
                return []
            slot, sequence = map(int, f[3:5])
            if slot not in self.rows or not 0 <= sequence <= 4096:
                return []
            rows = self.rows[slot]
            if f[0] == 'NGCMD':
                command = f[5]
                if not sequence or command == '00000000' or len(command) != 8 or any(c not in '0123456789abcdef' for c in command):
                    return []
                if sequence in rows:
                    if rows[sequence]['command'] != command:
                        return []  # Never replace a previously queued command.
                elif sequence == len(rows)+1 and len(rows)-self.captured[slot] < 64:
                    rows[sequence] = {'command': command}
                else:
                    return []
                row = rows[sequence]
                reply = (f'NGCAPTURED|1|{self.token}|{slot}|{sequence}|{row["tick"]}'
                         if 'tick' in row else f'NGQUEUED|1|{self.token}|{slot}|{sequence}')
                return [(address, reply.encode())]
            if slot in self.peers and self.peers[slot] != address:
                return []
            if not f[5].isdecimal() or str(int(f[5])) != f[5]:
                return []
            tick = int(f[5])
            if not 0 <= tick <= 18000 or (sequence == 0) != (tick == 0):
                return []
            if sequence > self.captured[slot]+1 or (sequence and sequence not in rows):
                return []
            if sequence:
                row = rows[sequence]
                if 'tick' in row and row['tick'] != tick:
                    return []
                if sequence == self.captured[slot]+1:
                    if sequence > 1 and tick <= rows[sequence-1]['tick']:
                        return []
                    row['tick'] = tick
                    self.captured[slot] = sequence
            self.peers[slot] = address
            following = self.captured[slot]+1
            if following not in rows:
                return []
            reply = f'NGNEXT|1|{self.token}|{slot}|{following}|{rows[following]["command"]}'
            return [(address, reply.encode())]
        except (ValueError, UnicodeError):
            return []


class Barrier:
    def __init__(self, token, ticks=120, hold_tick=10, hold_seconds=3, compare=True, relative_real=False, live_inputs=False,
                 observe=False, clients=2, remote=False, lookahead=0, tick_hz=60, peer_window=None):
        # remote: the caller already restricts sources (--allow-from); state reports may then
        # come from non-loopback addresses. Peers are still pinned per slot and authenticated.
        self.remote = remote
        if len(token) != 32 or any(c not in '0123456789abcdef' for c in token):
            raise ValueError('invalid token')
        if type(clients) is not int or not 2 <= clients <= CLIENTS_MAX:
            raise ValueError('clients must be 2..8')
        if type(lookahead) is not int or not 0 <= lookahead <= LOOKAHEAD_MAX:
            raise ValueError('lookahead must be 0..8')
        if lookahead and live_inputs:
            raise ValueError('live inputs carry protocol 2 commands; lookahead needs protocol 4')
        if tick_hz not in TICK_RATES:
            raise ValueError('tick rate must be 60, 40, 30 or 20')
        if peer_window is not None and (type(peer_window) is not int or not lookahead or not 0 <= peer_window <= LOOKAHEAD_MAX-lookahead):
            raise ValueError('peer window needs lookahead k >= 1 and 0 <= window <= 8-k')
        # native_gate_tick_hz of every client: each tick advances game and real time by 1/hz.
        self.tick_hz = tick_hz
        # 0: protocols 1-3, unchanged. k>0: only protocol 4 with the same k; any
        # other authenticated report fails the session (lookahead_mismatch_slot_S).
        self.lookahead = lookahead
        # Peer links (None: off, the default). Reports may then run peer_window ticks past the
        # newest permit; peer socket addresses (slot -> (ip, port), never written to a report)
        # go out only once every client sent NGPHELLO: nobody's address unless all opted in.
        self.peer_window, self.peer_addresses, self.peer_exchanged = peer_window, {}, False
        # Protocol 2 carries exactly two 8-hex commands; protocols 1, 3 and 4 admit N.
        self.clients, self.slots = clients, tuple(range(1, clients+1))
        self.token, self.limit = token, ticks
        self.hold_tick, self.hold_seconds, self.compare = hold_tick, hold_seconds, compare
        self.peers, self.states, self.arrivals = {}, {}, {}
        self.released, self.failed, self.done = -1, None, False
        # Timestamps from time.perf_counter: time.monotonic is the 15.6 ms tick on Windows
        # before Python 3.13, which made every frame period a multiple of it.
        self.released_at = {}
        self.relative_real = relative_real
        self.version = None
        self.live_inputs = LiveInputs(token, clients) if live_inputs else None
        # Census mode records every differing declared-state tick and keeps
        # releasing frames, so one run shows how divergence spreads. Clock,
        # timestep and transport checks still stop the session.
        self.observe = observe
        self.mismatches = []
        self.mismatch_slots = {}   # tick -> slots whose digest differs from slot 1
        # Observe-mode notices for people watching a session (desync indicator):
        # NATIVE_GATE DESYNC on the first differing tick of a run (repeated every
        # DESYNC_REPEAT ticks while it lasts) and NATIVE_GATE RESYNC when the
        # digests agree again. No wire change; the main loop prints them.
        self.diverged = False
        self.notices = []
        self.last_notice = None
        # Play sessions (--keep-going, set after construction; implies observe): a timestep or clock split is a notice
        # (NATIVE_GATE CLOCK_SPLIT, the first of a run and every DESYNC_REPEAT ticks while it lasts) and a row of
        # clock_splits instead of the end of the session. Off: such a split stops everyone, as before.
        self.keep_going = False
        self.clock_splits = []     # [tick, failure name] per split tick (the first name of the tick)
        self.last_clock_notice = None

    def receive(self, data, address, now):
        if self.live_inputs and data.startswith((b'NGCMD|', b'NGPOLL|')):
            return self.live_inputs.receive(data, address) if not self.done and not self.failed else []
        if data.startswith(b'NGPHELLO|'):
            return self.peer_hello(data, address)
        if data.startswith(b'NGPFAIL|'):
            return self.peer_fail(data, address)
        if len(data) > STATE_MAX[3 if data.startswith((b'NGSTATE|3|', b'NGSTATE|4|')) else 1] or (address[0] != '127.0.0.1' and not self.remote):
            return []
        try:
            f = data.decode('ascii').split('|')
            if len(f) not in (8, 9, 10, 11) or f[0] != 'NGSTATE' or f[1] not in ('1', '2', '3', '4') or not hmac.compare_digest(f[2], self.token):
                return []
            version = int(f[1])
            # A relay serves protocol 4 (--lookahead k>0) or protocols 1-3, never both.
            mismatch = (version == 4) != (self.lookahead > 0)
            if len(f) != 7+version or (self.version not in (None, version) and not mismatch):
                return []
            if version == 2 and (self.clients != 2 or len(f[8]) != 8 or any(c not in '0123456789abcdef' for c in f[8])):
                return []
            # Protocols 3 and 4: the client declares its configured N; refuse any other.
            if version >= 3 and (f[8] != str(self.clients) or not blob_valid(f[-1]) or not canonical(f[3]) or not canonical(f[4])):
                return []
            if version == 4:
                if not canonical(f[9]) or not 1 <= int(f[9]) <= LOOKAHEAD_MAX:
                    return []
                mismatch = mismatch or int(f[9]) != self.lookahead
            slot, tick = int(f[3]), int(f[4])
            if slot not in self.slots or not 0 <= tick <= self.limit or tick > self.accepted():
                return []
            if any(len(value) != size or any(c not in '0123456789abcdef' for c in value)
                   for value, size in ((f[5], 16), (f[6], 16), (f[7], 64))):
                return []
            game, real = (struct.unpack('>d', bytes.fromhex(value))[0] for value in f[5:7])
            if not math.isfinite(game) or not math.isfinite(real):
                return []
            if slot in self.peers and self.peers[slot] != address:
                return []
            if mismatch:
                # Old and new transports never mix: a client with another window
                # (or none) would apply other ticks' inputs. Stop the session.
                self.failed = self.failed or f'lookahead_mismatch_slot_{slot}'
                return [(address, f'NGSTOP|{version}|{self.token}|{slot}|{tick}'.encode('ascii'))]
            self.peers[slot] = address
            self.version = version
            row = {'game': game, 'real': real, 'digest': f[7]}
            if version == 2:
                row['command'] = f[8]
            if version >= 3:
                row['blob'] = f[-1]
            rows = self.states.setdefault(tick, {})
            if slot in rows and rows[slot] != row:
                self.failed = self.failed or f'changed_state_while_waiting_tick_{tick}'
            else:
                rows[slot] = row
            self.arrivals.setdefault(tick, now)
            if self.failed or self.done:
                return [(address, self.packet('NGSTOP', slot, tick))]
            if tick <= self.released:
                # A retransmitted report: answer the permits its tick released.
                return [(address, self.packet('NGGO', slot, permit)) for permit in self.unlocked(tick)]
            return self.advance(now)
        except (ValueError, UnicodeError, OverflowError):
            return []

    def permitted(self):
        """Highest permit issued; reports of later ticks cannot exist yet."""
        return min(self.limit, self.released+1+self.lookahead) if self.released >= 0 else 0

    def accepted(self):
        """Highest tick a report may carry: the newest permit, with peer links up to
        peer_window ticks later (a client runs at most that far past its permits)."""
        extra = self.peer_window if self.peer_window and self.released >= 0 else 0
        return min(self.limit, self.permitted()+extra)

    def peer_hello(self, data, address):
        """NGPHELLO|1|<token>|<slot>|<clients>|<k>|<m> from a client's peer socket (its opt-in).

        Once every slot said hello, every hello is answered with NGPADDR|1|<token>|<slot>|<m>|
        <last>|<ip:port of slot 1>|...|<slot N> (the addresses this relay saw; last = --ticks, past
        which no client may run on peer records: the session ends there); a relay without
        --peer-window, or another N/k/m, answers NGPREFUSE|1|<token>|<slot>|off or mismatch.
        The hello must come from the host whose reports the slot is pinned to; the first
        hello address of a slot is kept. Malformed or unauthenticated hellos are dropped."""
        if len(data) > 128 or (address[0] != '127.0.0.1' and not self.remote) or self.failed or self.done:
            return []
        try:
            f = data.decode('ascii').split('|')
        except UnicodeError:
            return []
        if len(f) != 7 or f[0] != 'NGPHELLO' or f[1] != str(PEER_VERSION) or not hmac.compare_digest(f[2], self.token):
            return []
        if not all(canonical(value) for value in f[3:]):
            return []
        slot, clients, k, window = map(int, f[3:])
        if slot not in self.slots or slot not in self.peers or self.peers[slot][0] != address[0]:
            return []
        if self.peer_window is None or (clients, k, window) != (self.clients, self.lookahead, self.peer_window):
            reason = 'off' if self.peer_window is None else 'mismatch'
            return [(address, f'NGPREFUSE|{PEER_VERSION}|{self.token}|{slot}|{reason}'.encode('ascii'))]
        if self.peer_addresses.setdefault(slot, address) != address:
            return []
        if len(self.peer_addresses) < self.clients:
            return []
        book = '|'.join(f'{ip}:{port}' for ip, port in (self.peer_addresses[s] for s in self.slots))
        targets = self.slots if not self.peer_exchanged else (slot,)
        self.peer_exchanged = True
        return [(self.peer_addresses[s], f'NGPADDR|{PEER_VERSION}|{self.token}|{s}|{self.peer_window}|{self.limit}|{book}'.encode('ascii'))
                for s in targets]

    def peer_fail(self, data, address):
        """NGPFAIL|1|<token>|<slot>|<tick>|<reason>: a client stopped on a peer check (a peer copy
        that differs from a permit, 104; two different peer copies, 108; two different permits, 103).
        The session fails (peer_link_stop_<reason>_slot_<S>_tick_<T>) and every client gets NGSTOP
        for the newest tick it reported, instead of waiting for its timeout. Only with
        --peer-window, only from the slot's pinned host; anything else is dropped."""
        if self.peer_window is None or len(data) > 128 or (address[0] != '127.0.0.1' and not self.remote) or self.done:
            return []
        try:
            f = data.decode('ascii').split('|')
        except UnicodeError:
            return []
        if len(f) != 6 or f[0] != 'NGPFAIL' or f[1] != str(PEER_VERSION) or not hmac.compare_digest(f[2], self.token):
            return []
        if not all(canonical(value) for value in f[3:]) or f[5] not in ('103', '104', '108'):
            return []
        slot = int(f[3])
        if slot not in self.slots or slot not in self.peers or self.peers[slot][0] != address[0]:
            return []
        self.failed = self.failed or f'peer_link_stop_{f[5]}_slot_{slot}_tick_{f[4]}'
        return [(self.peers[s], self.packet('NGSTOP', s, max(t for t, rows in self.states.items() if s in rows)))
                for s in self.slots if s in self.peers]

    def unlocked(self, tick):
        """Permits released once every client's report of tick passed its checks."""
        permits = range(1, self.lookahead+2) if tick == 0 else (tick+1+self.lookahead,)
        return [permit for permit in permits if permit <= self.limit]

    def advance(self, now):
        """Check complete ticks in order; release their permits or stop everyone."""
        out = []
        while not self.failed and not self.done:
            tick = self.released+1
            if tick > self.limit or len(self.states.get(tick, {})) != self.clients:
                break
            self.check(tick)
            if self.failed or tick == self.limit:
                self.done = not self.failed
                return out+[(self.peers[s], self.packet('NGSTOP', s, tick)) for s in self.slots]
            if tick == self.hold_tick and now-self.arrivals[tick] < self.hold_seconds:
                break
            self.released = tick
            self.released_at[tick] = now
            out += [(self.peers[s], self.packet('NGGO', s, permit)) for permit in self.unlocked(tick) for s in self.slots]
        return out

    def check(self, tick):
        """Timestep, declared digest and clock checks of one complete tick."""
        rows = self.states[tick]
        split = None
        if tick > 0:
            for s in self.slots:
                delta = rows[s]['game']-self.states[tick-1][s]['game']
                if abs(delta-1/self.tick_hz) > 1e-8:
                    split = split or f'native_dt_mismatch_player_{s}_tick_{tick}'
                    if not self.keep_going:
                        self.failed = f'native_dt_mismatch_player_{s}_tick_{tick}'
        differing = [s for s in self.slots if rows[s]['digest'] != rows[1]['digest']]
        if self.compare and differing:
            self.mismatch_slots[tick] = differing
            if self.observe:
                if tick not in self.mismatches:
                    self.mismatches.append(tick)
                    if not self.diverged or tick-self.last_notice >= DESYNC_REPEAT:
                        self.notices.append(f'NATIVE_GATE DESYNC tick={tick} slots={",".join(map(str, differing))} '
                                            f'first={self.mismatches[0]} mismatches={len(self.mismatches)}')
                        self.last_notice = tick
                    self.diverged = True
            else:
                self.failed = f'battle_state_mismatch_tick_{tick}'
        elif self.compare and self.observe and self.diverged:
            self.diverged = False
            self.notices.append(f'NATIVE_GATE RESYNC tick={tick} mismatches={len(self.mismatches)}')
        real_equal = all(rows[s]['real'] == rows[1]['real'] for s in self.slots)
        if self.relative_real:
            # Loading duration is process-local. Keep raw clocks as evidence
            # and require the same elapsed real ticks from the admission point.
            elapsed = [rows[s]['real']-self.states[0][s]['real'] for s in self.slots]
            real_equal = all(abs(elapsed[0]-t) <= 1e-8 for t in elapsed) and all(abs(t-tick/self.tick_hz) <= 1e-8 for t in elapsed)
        if not self.failed and (any(rows[s]['game'] != rows[1]['game'] for s in self.slots) or not real_equal):
            split = split or f'native_clock_mismatch_tick_{tick}'
            if not self.keep_going:
                self.failed = f'native_clock_mismatch_tick_{tick}'
        if split and self.keep_going:
            self.clock_splits.append([tick, split])
            if self.last_clock_notice is None or tick-self.last_clock_notice >= DESYNC_REPEAT or \
                    (len(self.clock_splits) > 1 and self.clock_splits[-2][0] != tick-1):
                self.notices.append(f'NATIVE_GATE CLOCK_SPLIT tick={tick} why={split} splits={len(self.clock_splits)}')
                self.last_clock_notice = tick

    def packet(self, kind, slot, tick):
        data = f'{kind}|{self.version or 1}|{self.token}|{slot}|{tick}'
        if kind == 'NGGO' and self.version == 2:
            data += ''.join('|'+self.states[tick-1][s]['command'] for s in (1, 2))
        if kind == 'NGGO' and self.version == 3:
            # Every client's sealed proposal of tick-1 in slot order: the exact
            # blobs each client applies at this tick.
            data += ''.join('|'+self.states[tick-1][s]['blob'] for s in self.slots)
        if kind == 'NGGO' and self.version == 4:
            # The window k, then every client's proposal sealed with tick-1-k
            # (none before tick 0): the exact blobs each client applies at tick.
            source = tick-1-self.lookahead
            data += f'|{self.lookahead}'+''.join('|'+(self.states[source][s]['blob'] if source >= 0 else '') for s in self.slots)
        return data.encode('ascii')

    def status(self, now):
        """Small, fixed-size live status for --status-file (desync indicator)."""
        last = self.mismatches[-1] if self.mismatches else None
        return {'tick': self.released, 'desync': bool(self.mismatches), 'diverged_now': self.diverged,
                'first_mismatch': self.mismatches[0] if self.mismatches else None, 'last_mismatch': last,
                'mismatches': len(self.mismatches), 'slots': self.mismatch_slots.get(last, []) if last is not None else [],
                'failure': self.failed, 'done': self.done, 'observe': self.observe, 'updated': round(now, 3),
                **({'keep_going': True, 'clock_splits': len(self.clock_splits),
                    'first_clock_split': self.clock_splits[0][0] if self.clock_splits else None} if self.keep_going else {}),
                **({'lookahead': self.lookahead, 'permitted': self.permitted()} if self.lookahead else {}),
                **self.peer_summary()}

    def take_notices(self):
        notices, self.notices = self.notices, []
        return notices

    def summary(self):
        common = [tick for tick, rows in self.states.items() if len(rows) == self.clients]
        return {'scope': 'native FrameTick gate and declared state subset',
                'protocol_version': self.version, 'clients': self.clients, 'tick_hz': self.tick_hz,
                'real_time_domain': 'elapsed from snapshot admission' if self.relative_real else 'absolute',
                'verdict': 'fail' if self.failed else 'diverged' if self.mismatches else 'pass' if self.done else 'incomplete',
                'failure': self.failed, 'common_ticks': common,
                'observe': self.observe,
                **({'keep_going': True, 'clock_splits': self.clock_splits} if self.keep_going else {}),
                'first_digest_mismatch': self.mismatches[0] if self.mismatches else None,
                'digest_mismatch_ticks': self.mismatches,
                'digest_mismatch_slots': self.mismatch_slots,
                # Wall-clock monotonic seconds when each tick was released: frame cost.
                'released_at': {tick: round(at, 6) for tick, at in self.released_at.items()},
                'hold_seconds': self.released_at.get(self.hold_tick, 0)-self.arrivals.get(self.hold_tick, 0),
                'states': self.states,
                'live_inputs': self.live_inputs.rows if self.live_inputs else None,
                # Protocol 4 only: the permit window; released_at then names the
                # tick whose reports released the permit for tick+1+lookahead.
                **({'lookahead': self.lookahead} if self.lookahead else {}),
                **self.peer_summary()}

    def peer_summary(self):
        """Peer links only: the window and whether addresses went out (never the addresses)."""
        if self.peer_window is None:
            return {}
        return {'peer_window': self.peer_window, 'peer_links': 'exchanged' if self.peer_exchanged else 'waiting'}



class DropBarrier(Barrier):
    """Protocol 4 with --stall-drop-ms (roadmap D15): a client the others waited for that long is dropped. From the first tick
    it did not report, the relay stands in for it (the reference row's digest and game time, its own real clock offset, an
    empty blob), sends it NGDROP|4 instead of permits and ignores its later reports. Every permit whose proposal tick is at or
    after a slot's first stand-in names that slot in its window field, k/<mask> (two hex digits, bit s-1), so every client
    sees the drop with the same permit and its records stop with the same proposal tick (lockstep_lookahead.h). Without a
    drop the wire is the protocol 4 wire byte for byte. With --wire-crypto a stand-in record carries a zero signature
    (native_gate_crypto.py). Not with --join, --live-inputs or --peer-window."""
    def __init__(self, *args, stall_report_ms=1000.0, stall_drop_ms=0.0, **kw):
        super().__init__(*args, **kw)
        if not self.lookahead or self.peer_window is not None or self.live_inputs:
            raise ValueError('--stall-drop-ms without --adaptive covers protocol 4 (--lookahead K) without peer links or live inputs')
        if not stall_drop_ms > 0 or stall_drop_ms < stall_report_ms:
            raise ValueError('the stall drop bound must be positive and not below the stall report bound')
        self.stall_report, self.stall_drop = stall_report_ms/1000.0, stall_drop_ms/1000.0
        self.dropped = {}          # slot -> its first stand-in tick
        self.stall_seen = set()
        self.stalls, self.drops = [], []

    def live(self):
        return [s for s in self.slots if s not in self.dropped]

    def mask(self, source):
        return sum(1 << (s-1) for s, first in self.dropped.items() if source >= first)

    def packet(self, kind, slot, tick):
        if kind == 'NGDROP':
            return f'NGDROP|4|{self.token}|{slot}|{tick}'.encode('ascii')
        data = super().packet(kind, slot, tick)
        mask = self.mask(tick-1-self.lookahead) if kind == 'NGGO' else 0
        if not mask:
            return data
        head = f'NGGO|4|{self.token}|{slot}|{tick}|{self.lookahead}'.encode('ascii')
        assert data.startswith(head+b'|')
        return head+f'/{mask:02x}'.encode('ascii')+data[len(head):]

    def receive(self, data, address, now):
        if data.startswith(b'NGSTATE|4|'):
            f = data.split(b'|')
            if len(f) > 4 and f[3].isdigit() and int(f[3]) in self.dropped and hmac.compare_digest(f[2], self.token.encode('ascii')):
                slot = int(f[3])
                return [(address, self.packet('NGDROP', slot, self.dropped[slot]))] if self.peers.get(slot) == address else []
        return super().receive(data, address, now)

    def fill(self, tick):
        """Stand-in rows of the dropped slots at tick, once every live slot reported it."""
        rows = self.states.get(tick, {})
        live = self.live()
        if not live or not all(s in rows for s in live):
            return
        ref = min(live)
        for s, first in self.dropped.items():
            if tick >= first and s not in rows:
                # The real clock check compares each slot's elapsed real time since tick 0: keep the slot's own offset.
                offset = self.states[0][s]['real']-self.states[0][ref]['real'] if first > 0 else 0.0
                rows[s] = {'game': rows[ref]['game'], 'real': rows[ref]['real']+offset, 'digest': rows[ref]['digest'], 'blob': '',
                           'standin': True}

    def advance(self, now):
        out = []
        while not self.failed and not self.done:
            tick = self.released+1
            if tick > self.limit:
                break
            self.fill(tick)
            if len(self.states.get(tick, {})) != self.clients:
                break
            self.check(tick)
            live = [s for s in self.live() if s in self.peers]
            if self.failed or tick == self.limit:
                self.done = not self.failed
                return out+[(self.peers[s], self.packet('NGSTOP', s, tick)) for s in live]
            if tick == self.hold_tick and now-self.arrivals[tick] < self.hold_seconds:
                break
            self.released = tick
            self.released_at[tick] = now
            out += [(self.peers[s], self.packet('NGGO', s, permit)) for permit in self.unlocked(tick) for s in live]
        return out

    def poll(self, now):
        """Between datagrams: the clients the others wait for at the first unreleased tick are reported, then dropped."""
        if self.failed or self.done or self.released < 0:
            return []
        tick = self.released+1
        if tick > self.limit:
            return []
        rows = self.states.get(tick, {})
        live = self.live()
        missing = [s for s in live if s not in rows]
        if not missing or len(missing) == len(live):
            return []
        since = max(self.arrivals.get(tick, now), self.released_at.get(self.released, now))
        waited = now-since
        for s in missing:
            if waited >= self.stall_report and (s, tick) not in self.stall_seen:
                self.stall_seen.add((s, tick))
                self.stalls.append({'slot': s, 'tick': tick, 'reported_ms': round(1000*waited, 1)})
                self.notices.append(f'NATIVE_GATE STALL slot={s} tick={tick} waited_ms={1000*waited:.0f}')
        if waited < self.stall_drop:
            return []
        out = []
        for s in missing:
            self.dropped[s] = tick
            self.drops.append({'slot': s, 'tick': tick, 'waited_ms': round(1000*waited, 1)})
            self.notices.append(f'NATIVE_GATE DROP slot={s} tick={tick} waited_ms={1000*waited:.0f} '
                                f'remaining={",".join(map(str, self.live()))}')
            if s in self.peers:
                out.append((self.peers[s], self.packet('NGDROP', s, tick)))
        return out+self.advance(now)

    def summary(self):
        out = super().summary()
        out['drop'] = {'stall_report_ms': 1000*self.stall_report, 'stall_drop_ms': 1000*self.stall_drop,
                       'stalls': self.stalls, 'drops': self.drops}
        return out

class LinkDelay:
    """Test only (--latency-ms, --latency-jitter-ms, --latency-spike): the extra one-way delay of each datagram.

    Half the round trip each way, plus a uniform 0..jitter/2 per datagram and direction, plus half
    the spike's extra round trip while a spike lasts (every period seconds from the start, for its
    duration). With jitter datagrams may overtake each other, as on the internet.
    """
    def __init__(self, round_trip_ms=0.0, jitter_ms=0.0, spike=None, seed=None, start=0.0):
        self.half, self.jitter = round_trip_ms/2000.0, jitter_ms/2000.0
        self.spike, self.start = spike, start
        self.random = random.Random(seed)

    def __bool__(self):
        return bool(self.half or self.jitter or self.spike)

    def __call__(self, now):
        delay = self.half+(self.random.uniform(0, self.jitter) if self.jitter else 0.0)
        if self.spike:
            period, duration, extra = self.spike
            if (now-self.start) % period < duration/1000.0:
                delay += extra/2000.0
        return delay


def parse_spike(text):
    """PERIOD_S,DURATION_MS,EXTRA_MS (e.g. 20,2000,300: every 20 s, for 2 s, 300 ms more round trip)."""
    period, duration, extra = (float(v) for v in text.split(','))
    if not (1 <= period <= 3600 and 0 < duration < period*1000 and 0 < extra <= 5000):
        raise ValueError('spike outside 1..3600 s, 0..period, 0..5000 ms')
    return period, duration, extra


def atomic_write(path, text):
    # On Windows a reader of the old file (an indexer, a scanner, a tool) makes the replace
    # fail for a moment, and p4-L lost its relay to that at tick 1164. These files are
    # checkpoints, so a session must not end for one: the next checkpoint tries again.
    temporary = path.with_suffix('.tmp')
    try:
        temporary.write_text(text)
        temporary.replace(path)
    except OSError as error:
        print(f'NATIVE_GATE checkpoint not written: {error}', flush=True)
        return False
    return True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bind', default='127.0.0.1')
    p.add_argument('--allow-from', default='', metavar='IP[,IP...]',
                   help='required for a non-loopback --bind: only datagrams from these IPv4 source addresses are read')
    p.add_argument('--port', type=int, required=True)
    p.add_argument('--token', required=True)
    p.add_argument('--ticks', type=int, default=120)
    p.add_argument('--hold-tick', type=int, default=10)
    p.add_argument('--hold-seconds', type=float, default=3)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--relative-real', action='store_true')
    p.add_argument('--live-inputs', action='store_true', help='accept local per-client tactical input queues')
    p.add_argument('--observe', action='store_true', help='census: record state mismatches and keep running to --ticks')
    p.add_argument('--keep-going', action='store_true',
                   help='play sessions (x4ls play): --observe, and a timestep or clock split is a CLOCK_SPLIT notice and a '
                        'clock_splits row instead of the end of the session')
    p.add_argument('--clients', type=int, default=2, help='full-simulation clients 2..8 (more than 2 needs protocol 1 or 3)')
    p.add_argument('--lookahead', type=int, default=0, metavar='K',
                   help='protocol 4: release the permit for tick T once every client reported tick T-1-K (0..8; '
                        '0 keeps protocols 1-3). Input delays must cover it: pilot >= K+2, orders >= K+1')
    p.add_argument('--peer-window', type=int, default=None, metavar='M',
                   help='peer links extension 1 (default off): accept reports up to M ticks past the newest permit and, once '
                        'every client opted in, give each client the peer socket addresses of all (players then learn each '
                        "other's IP addresses). Needs --lookahead K >= 1 and K+M <= 8")
    p.add_argument('--tick-hz', type=int, default=60, choices=TICK_RATES,
                   help="the clients' native_gate_tick_hz; the timestep and relative-real checks use 1/hz")
    p.add_argument('--input-journal', type=Path, metavar='FILE',
                   help='append every released tick (its reports: digests, clocks, inputs) as one JSON line, flushed before '
                        'its permits leave. The full --report is then written every 60 s and at the end instead of before each '
                        'release that carries inputs: that rewrite grows with the run (7 ms at 600 ticks, about 1 ms more per '
                        '100 ticks) and every pilot record is an input')
    p.add_argument('--latency-ms', type=float, default=0, metavar='MS',
                   help='test only: an added round trip for every client (half before a datagram is read, half before a '
                        'reply leaves), to try internet latency with all clients on one PC; 0 (default) adds nothing')
    p.add_argument('--latency-jitter-ms', type=float, default=0, metavar='MS',
                   help='test only, with or without --latency-ms: every datagram waits a further uniform 0..MS/2 in each '
                        'direction (datagrams may then overtake each other)')
    p.add_argument('--latency-spike', type=str, default='', metavar='PERIOD_S,DURATION_MS,EXTRA_MS',
                   help='test only: every PERIOD_S seconds from the start, for DURATION_MS, the round trip grows by EXTRA_MS')
    p.add_argument('--latency-seed', type=int, default=None, help='test only: seed of the jitter draws')
    adaptive = p.add_argument_group('protocol 5 (adaptive window, tools/dev/native_gate_adaptive.py)')
    adaptive.add_argument('--adaptive', action='store_true',
                          help='measure every client round trip at tick 0 and choose the window k (1..--lookahead) and the pilot '
                               'delay (k+2+headroom) from it; clients need native_gate_lookahead = "adaptive"')
    adaptive.add_argument('--adaptive-min', type=int, default=1, metavar='K', help='smallest window the relay chooses (default 1)')
    adaptive.add_argument('--adaptive-target-hz', type=float, default=None, metavar='HZ',
                          help='step rate the window should reach (default --tick-hz)')
    adaptive.add_argument('--adaptive-margin-ms', type=float, default=10, metavar='MS',
                          help='added to the slowest p90 round trip before the window is chosen (default 10)')
    adaptive.add_argument('--adaptive-samples', type=int, default=8, metavar='N', help='round trips per client before the plan (default 8)')
    adaptive.add_argument('--adaptive-measure-seconds', type=float, default=5, metavar='S',
                          help='longest wait for the samples after the last tick 0 report; fewer than 2 then stop (default 5)')
    adaptive.add_argument('--adaptive-headroom', type=int, default=0, metavar='H',
                          help='pilot delay = k+2+H: lets session changes raise k by up to H with pilot records (default 0)')
    adaptive.add_argument('--adaptive-changes', action='store_true',
                          help='also change the window during the session (raise it when round trips grow)')
    adaptive.add_argument('--adaptive-decrease', action='store_true',
                          help='with --adaptive-changes: also lower it again (quiet ticks; refused by clients with pilot records)')
    adaptive.add_argument('--adaptive-interval', type=float, default=2, metavar='S', help='session policy interval (default 2)')
    adaptive.add_argument('--stall-report-ms', type=float, default=1000, metavar='MS',
                          help='report a client the others have waited for this long (default 1000)')
    adaptive.add_argument('--stall-drop-ms', type=float, default=0, metavar='MS',
                          help='drop a client the others have waited for this long: its later proposals are empty for everyone and '
                               'it is stopped (0, the default: never); protocol 5, and protocol 4 (--lookahead K) without '
                               '--peer-window (DropBarrier); its ship goes neutral (roadmap D15). With --join: --rejoin slots and '
                               'joined slots, a later join takes the slot again (roadmap 15)')
    p.add_argument('--status-file', type=Path, help='observe mode: small live JSON status (desync flag, first/last mismatch), '
                   'rewritten atomically on every notice and at most once a second otherwise')
    p.add_argument('--rendezvous-port', type=int, default=0, metavar='P',
                   help='opt-in NGNAT/1 rendezvous for direct player links (roadmap B7, native_gate_rendezvous.py) on UDP P and '
                        'P+1 of --bind, same token and --allow-from; a separate thread and sockets, the barrier is unchanged. '
                        '0 (default): off')
    p.add_argument('--wire-crypto', type=Path, metavar='KEYFILE',
                   help='opt-in wire crypto (protocols 3 and 4): handshake, sealed datagrams, signed input records and permits '
                        '(tools/dev/native_gate_crypto.py keygen/credential); every unsealed datagram is then dropped')
    p.add_argument('--wire-crypto-lib', type=Path, metavar='PATH', help='the x4mp_wire_crypto library (default: the build output)')
    join = p.add_argument_group('hot join (tools/dev/native_gate_join.py, reports/LOCKSTEP-HOTJOIN-20261101.txt)')
    join.add_argument('--join', action='store_true',
                      help='a player may join the running session into an absent slot (protocols 3 and 4): the relay stands in '
                           'for absent slots, asks every saver to save at an agreed tick, replays the session to the joiner and '
                           'hands the slot over at an announced tick')
    join.add_argument('--absent', default='', metavar='K[,K...]', help='with --join: slots nobody plays at the start')
    join.add_argument('--rejoin', default='', metavar='K[,K...]',
                      help='with --join and --stall-drop-ms: slots played from the start that may be dropped and joined again '
                           '(the clients\' native_gate_join.rejoin; never slot 1; roadmap 15)')
    join.add_argument('--join-savers', default='', metavar='S[,S...]', help='slots that save (default: every present slot)')
    join.add_argument('--join-lead', type=int, default=30, metavar='TICKS',
                      help='ticks between the save request reaching the clients and the save tick (default 30)')
    join.add_argument('--join-save-at', type=int, default=0, metavar='TICK',
                      help='DIAGNOSTIC (default off): the save tick no earlier than TICK, so a test join lands on a chosen T')
    join.add_argument('--join-handover-gap', type=int, default=60, metavar='TICKS',
                      help='announce the handover once the joiner is this close to the live tick (default 60)')
    join.add_argument('--join-handover-lead', type=int, default=8, metavar='TICKS',
                      help='ticks between the handover record reaching the clients and the handover tick (default 8)')
    join.add_argument('--join-max-pause', type=float, default=60, metavar='S',
                      help='longest expected wait of the session at the handover tick before the join is refused (default 60)')
    join.add_argument('--join-catchup-limit', type=float, default=300, metavar='S',
                      help='after this many seconds of catch-up the handover is announced even with a larger gap, if the '
                           'expected pause stays within --join-max-pause (default 300)')
    join.add_argument('--join-observe', action='store_true',
                      help='DIAGNOSTIC (default off): a joiner whose declared digest differs before its handover is counted and '
                           'noticed instead of stopped (game and real time stay exact), so the handover runs while the join is not '
                           'yet equal; its lines say diagnostic=join_observe and such a run is never a J2 acceptance')
    join.add_argument('--join-joiner-timeout', type=float, default=900, metavar='S',
                      help='a joiner that stays silent this long is given up (default 900: load time included)')
    args = p.parse_args()
    args.observe = args.observe or args.keep_going   # a play session never stops on a digest split either
    allowed = set(filter(None, (part.strip() for part in args.allow_from.split(','))))
    if args.bind != '127.0.0.1':
        # A remote relay (e.g. the dedicated server host) serves only the listed client
        # addresses; everything else is dropped before parsing. The token still authenticates.
        try:
            for address in allowed | {args.bind}: socket.inet_aton(address)
        except OSError: p.error('--bind and --allow-from need dotted IPv4 addresses')
        if not allowed: p.error('a non-loopback --bind needs --allow-from')
    if not 2 <= args.clients <= CLIENTS_MAX: p.error('--clients must be 2..8')
    if not 0 <= args.lookahead <= LOOKAHEAD_MAX: p.error('--lookahead must be 0..8')
    if args.lookahead and args.live_inputs: p.error('--live-inputs needs protocol 2; --lookahead needs protocol 4')
    if args.peer_window is not None and (not args.lookahead or not 0 <= args.peer_window <= LOOKAHEAD_MAX-args.lookahead):
        p.error('--peer-window needs --lookahead K >= 1 and 0 <= M <= 8-K')
    if not 0 <= args.latency_ms <= 2000: p.error('--latency-ms must be 0..2000')
    if args.rendezvous_port and (not 1024 <= args.rendezvous_port <= 65534 or args.port in (args.rendezvous_port, args.rendezvous_port+1)):
        p.error('--rendezvous-port P needs 1024..65534 and P, P+1 apart from --port')
    if not 0 <= args.latency_jitter_ms <= 2000: p.error('--latency-jitter-ms must be 0..2000')
    try: spike = parse_spike(args.latency_spike) if args.latency_spike else None
    except ValueError as error: p.error(f'--latency-spike: {error}')
    if args.wire_crypto and args.live_inputs: p.error('--wire-crypto covers protocols 3 and 4; --live-inputs carries protocol 2 commands')
    if args.wire_crypto and args.adaptive: p.error('--wire-crypto covers protocols 3 and 4; --adaptive is protocol 5')
    if args.peer_window is not None and args.adaptive: p.error('--peer-window extends protocol 4; --adaptive is protocol 5')
    # The crypto relay drops every unsealed datagram, NGPHELLO too; a sealed address exchange is not built yet.
    if args.peer_window is not None and args.wire_crypto: p.error('--peer-window with --wire-crypto: the sealed peer address exchange is not built')
    if args.join and (args.adaptive or args.wire_crypto or args.live_inputs or args.peer_window is not None):
        p.error('--join covers protocols 3 and 4 without --adaptive, --wire-crypto, --live-inputs or --peer-window')
    if (args.absent or args.join_savers or args.rejoin or args.join_observe) and not args.join:
        p.error('--absent, --join-savers, --rejoin and --join-observe need --join')
    if args.rejoin and not args.stall_drop_ms: p.error('--rejoin needs --stall-drop-ms')
    # Protocol 4 drops (DropBarrier; with --wire-crypto the stand-in record carries a zero signature); the join path has its own
    # stand-ins: a drop there is an ABORT record in the slot's stand-in row (JoinBarrier, protocols 3 and 4).
    if args.stall_drop_ms and not args.adaptive and not args.join and (args.live_inputs or args.peer_window is not None or not args.lookahead):
        p.error('--stall-drop-ms without --adaptive or --join needs --lookahead K and no --live-inputs or --peer-window')
    # Every client builds the absent slot's player at H-2 (lockstep_pilot_inputs.h PILOT_HANDOVER_LEAD): the record must be
    # applied at least 3 ticks before H, and it is applied handover-lead ticks before it.
    if args.join and args.join_handover_lead < 3: p.error('--join-handover-lead must be at least 3 (the slot is built at H-2)')
    wire = None
    if args.wire_crypto:
        # Imported only when asked for: without --wire-crypto the relay is unchanged and needs no library.
        import native_gate_crypto
        try:
            wire = native_gate_crypto.RelayCrypto(native_gate_crypto.Library(args.wire_crypto_lib),
                                                  native_gate_crypto.load_key(args.wire_crypto), args.token, args.clients, args.lookahead)
        except (OSError, ValueError, RuntimeError, KeyError) as error:
            p.error(f'--wire-crypto: {error}')
    if args.adaptive:
        if not args.lookahead: p.error('--adaptive needs --lookahead K, the largest window the relay may choose')
        if args.live_inputs: p.error('--adaptive needs protocol 5; --live-inputs needs protocol 2')
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from native_gate_adaptive import AdaptiveBarrier
        try:
            barrier = AdaptiveBarrier(args.token, args.ticks, args.hold_tick, args.hold_seconds, relative_real=args.relative_real,
                                      observe=args.observe, clients=args.clients, remote=bool(allowed), kmax=args.lookahead,
                                      kmin=args.adaptive_min, tick_hz=args.tick_hz, target_hz=args.adaptive_target_hz,
                                      samples=args.adaptive_samples, margin_ms=args.adaptive_margin_ms,
                                      measure_seconds=args.adaptive_measure_seconds, changes=args.adaptive_changes,
                                      decrease=args.adaptive_decrease, headroom=args.adaptive_headroom,
                                      interval_seconds=args.adaptive_interval, stall_report_ms=args.stall_report_ms,
                                      stall_drop_ms=args.stall_drop_ms)
        except ValueError as error: p.error(f'--adaptive: {error}')
    elif args.join:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from native_gate_join import JoinBarrier
        try:
            absent = [int(v) for v in args.absent.split(',') if v.strip()]
            savers = [int(v) for v in args.join_savers.split(',') if v.strip()]
            rejoin = [int(v) for v in args.rejoin.split(',') if v.strip()]
            barrier = JoinBarrier(args.token, absent=absent, rejoin=rejoin, stall_report_ms=args.stall_report_ms,
                                  stall_drop_ms=args.stall_drop_ms, ticks=args.ticks, hold_tick=args.hold_tick, hold_seconds=args.hold_seconds,
                                  relative_real=args.relative_real, observe=args.observe, clients=args.clients, remote=bool(allowed),
                                  lookahead=args.lookahead, tick_hz=args.tick_hz,
                                  join_options={'lead': args.join_lead, 'handover_gap': args.join_handover_gap,
                                                'handover_lead': args.join_handover_lead, 'max_pause': args.join_max_pause,
                                                'catchup_limit': args.join_catchup_limit, 'joiner_timeout': args.join_joiner_timeout,
                                                'savers': savers or None, 'observe': args.join_observe,
                                                'save_at': args.join_save_at or None})
        except ValueError as error: p.error(f'--join: {error}')
        print(f'NATIVE_GATE RELAY JOIN absent={",".join(map(str, sorted(barrier.absent))) or "none"}'
              + (f' rejoin={",".join(map(str, sorted(barrier.rejoin))) or "none"} stall_drop_ms={args.stall_drop_ms:g}'
                 if args.stall_drop_ms else '')+(' join_observe=1 diagnostic=join_observe' if args.join_observe else ''), flush=True)
    elif args.stall_drop_ms:
        try:
            barrier = DropBarrier(args.token, args.ticks, args.hold_tick, args.hold_seconds, relative_real=args.relative_real,
                                  observe=args.observe, clients=args.clients, remote=bool(allowed), lookahead=args.lookahead,
                                  tick_hz=args.tick_hz, stall_report_ms=args.stall_report_ms, stall_drop_ms=args.stall_drop_ms)
        except ValueError as error: p.error(f'--stall-drop-ms: {error}')
        print(f'NATIVE_GATE RELAY DROP stall_drop_ms={args.stall_drop_ms:g}', flush=True)
    else:
        barrier = Barrier(args.token, args.ticks, args.hold_tick, args.hold_seconds, relative_real=args.relative_real, live_inputs=args.live_inputs,
                          observe=args.observe, clients=args.clients, remote=bool(allowed), lookahead=args.lookahead, tick_hz=args.tick_hz,
                          peer_window=args.peer_window)
    if args.keep_going:
        barrier.keep_going = True
        print('NATIVE_GATE RELAY KEEP_GOING observe=1 clock_splits=notice', flush=True)
    poll = getattr(barrier, 'poll', None)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((args.bind, args.port));s.settimeout(.1)
    rendezvous = None
    if args.rendezvous_port:
        spec = importlib.util.spec_from_file_location('native_gate_rendezvous', Path(__file__).with_name('native_gate_rendezvous.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        rendezvous = module.start_thread(args.token, args.bind, args.rendezvous_port, args.clients, allow=allowed or None,
                                         allow_loopback=args.bind == '127.0.0.1')
        print(f'NATIVE_GATE RENDEZVOUS ports={args.rendezvous_port},{args.rendezvous_port+1}', flush=True)
    if wire: print(f'NATIVE_GATE RELAY WIRE_CRYPTO relay_key={wire.relay_public.hex()} clients={args.clients}', flush=True)
    print('NATIVE_GATE RELAY READY', flush=True)
    def report_text():
        summary = barrier.summary()
        if wire: summary['wire_crypto'] = wire.summary()
        return json.dumps(summary, indent=2)+'\n'
    last = None
    last_report = float('-inf')  # perf_counter's origin is arbitrary (Linux: near boot); first report always writes
    last_status, final_written = 0, False
    def write_status(now):
        if not args.status_file:
            return
        atomic_write(args.status_file, json.dumps(barrier.status(now))+'\n')
    write_status(time.perf_counter())
    # --input-journal: the proposals a permit releases are durable before it is sent, one
    # line per tick, without rewriting the history; the full report becomes a checkpoint.
    journal = args.input_journal.open('w', encoding='ascii') if args.input_journal else None
    journaled = -1
    checkpoint = 60 if journal else 1
    # --latency-ms (and jitter, spikes): datagrams wait in each direction, ordered by their due time.
    delay = LinkDelay(args.latency_ms, args.latency_jitter_ms, spike, args.latency_seed, time.perf_counter())
    inbound, outbound, order = [], [], 0
    if args.latency_ms: print(f'NATIVE_GATE RELAY LATENCY round_trip_ms={args.latency_ms:g}', flush=True)
    if args.latency_jitter_ms or spike:
        print(f'NATIVE_GATE RELAY JITTER jitter_ms={args.latency_jitter_ms:g} spike={args.latency_spike or "none"}', flush=True)

    def deliver(packet, dest):
        nonlocal order
        if not delay:
            try: s.sendto(packet, dest)
            except ConnectionResetError: pass
            return
        order += 1
        heapq.heappush(outbound, (time.perf_counter()+delay(time.perf_counter()), order, packet, dest))
    try:
        while True:
            if delay or poll:
                now = time.perf_counter()
                while outbound and outbound[0][0] <= now:
                    _, _, packet, dest = heapq.heappop(outbound)
                    try: s.sendto(packet, dest)
                    except ConnectionResetError: pass
                # Protocol 5: pings, the plan once measured, stalls and the session policy run between datagrams.
                wait = 0.005 if poll else 0.1
                for queue in (inbound, outbound):
                    if queue: wait = min(wait, max(0.0, queue[0][0]-now))
                # Windows rounds a socket timeout up to its 15.6 ms timer tick, which made --latency-ms 100 a
                # 125 ms round trip (measured 1 Nov). With an emulated link the socket is polled and the loop
                # sleeps in high-resolution steps of at most 1 ms (time.sleep, Python 3.11+) instead.
                s.settimeout(0.0 if delay else max(0.0005, wait))
            # Larger than any UDP payload: an oversize datagram is read whole
            # and refused by the barrier instead of raising WSAEMSGSIZE here.
            try: data, address = s.recvfrom(65536)
            except (socket.timeout, BlockingIOError, ConnectionResetError):
                # Windows reports a previous UDP send to a closed peer as an
                # ICMP port-unreachable error here. It supplies no state report;
                # keep serving the other peer and preserve the admission gate.
                data = None
                if delay and wait > 0: time.sleep(min(wait, 0.001))
            if data is not None and allowed and address[0] not in allowed:
                data = None
            replies = poll(time.perf_counter()) if poll else []
            if delay:
                if data is not None:
                    order += 1
                    heapq.heappush(inbound, (time.perf_counter()+delay(time.perf_counter()), order, data, address))
                data = None
                if inbound and inbound[0][0] <= time.perf_counter():
                    _, _, data, address = heapq.heappop(inbound)
            direct = []
            if wire and data is not None:
                # Handshakes and key requests are answered here; reports reach the barrier opened and
                # signature-checked; everything else (unsealed, forged, replayed) is dropped.
                data, direct = wire.inbound(data, address)
                if wire.failure and not barrier.failed:
                    barrier.failed = wire.failure
                    print(f'NATIVE_GATE WIRE_CRYPTO failure={wire.failure}', flush=True)
            if data is not None:
                replies += barrier.receive(data, address, time.perf_counter())
            elif not replies and not direct and not barrier.notices and (barrier.released, barrier.failed, barrier.done) == last:
                continue
            notices = barrier.take_notices()
            for notice in notices:
                print(notice, flush=True)
            # An action can fail on the first admitted UI callback. Preserve its
            # proposals before releasing the clients, even inside the one-second
            # periodic checkpoint interval. Never infer missing commands later.
            command_release = any(packet.startswith((b'NGGO|2|', b'NGGO|3|')) for _,packet in replies) and any(
                row.get('command', '00000000') != '00000000' or row.get('blob', '') != ''
                for row in barrier.states.get(barrier.released, {}).values()) or any(
                permit_carries_inputs(packet) for _,packet in replies)
            if journal and barrier.released > journaled:
                for tick in range(journaled+1, barrier.released+1):
                    line = {'tick': tick, 'released_at': round(barrier.released_at.get(tick, 0), 6), 'rows': barrier.states.get(tick, {})}
                    if wire: line['sigs'] = wire.journal_sigs(tick)  # every released record stays verifiable
                    journal.write(json.dumps(line, separators=(',', ':'))+'\n')
                journal.flush()
                journaled = barrier.released
            elif command_release and not journal:
                if atomic_write(args.report, report_text()):
                    last_report = time.perf_counter()
            if wire:
                # Sealed per destination; permits carry every record signature and the relay's own.
                replies = [(dest, sealed) for dest, sealed in ((dest, wire.outbound(dest, packet)) for dest, packet in replies)
                           if sealed is not None]+direct
                wire.prune(barrier.released)
            for dest, packet in replies:
                # A departed peer must not prevent the other peer's STOP or retry (deliver
                # ignores its reset). Missing reports still cannot advance the gate.
                deliver(packet, dest)
            status = (barrier.released, barrier.failed, barrier.done)
            if status != last:
                last = status
                # Rewriting the complete growing history every frame makes long
                # tests quadratic in disk/serialization work. Checkpoint once a
                # second (every 60 s with --input-journal, which holds every
                # released tick), immediately on stop/failure, atomic replace.
                now = time.perf_counter()
                if now-last_report >= checkpoint or barrier.failed or barrier.done:
                    written = atomic_write(args.report, report_text())
                    # Nothing follows the last report, so it waits out a reader.
                    for _ in range(20):
                        if written or not (barrier.failed or barrier.done):
                            break
                        time.sleep(0.1)
                        written = atomic_write(args.report, report_text())
                    if written:
                        last_report = now
                diverged = f' first_mismatch={barrier.mismatches[0]} mismatches={len(barrier.mismatches)}' if barrier.mismatches else ''
                print(f'NATIVE_GATE tick={barrier.released} failure={barrier.failed or "none"} done={barrier.done}{diverged}', flush=True)
            # Live status for desync watchers: at once on a notice or the end,
            # otherwise at most once a second.
            now = time.perf_counter()
            final = bool(barrier.failed or barrier.done)
            if args.status_file and (notices or (final and not final_written) or now-last_status >= 1):
                write_status(now)
                last_status, final_written = now, final
    finally:
        s.close()
        if journal: journal.close()
        if rendezvous:
            rendezvous[1].set()
            rendezvous[0].join(1)
            for r in rendezvous[3]: r.close()


if __name__ == '__main__': main()
