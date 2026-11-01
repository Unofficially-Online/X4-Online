"""Protocol 5 (opt-in): the relay measures round trips and chooses the input lag (docs/LOCKSTEP-NSLOT-WIRE.txt, ADAPTIVE).

Protocol 4 fixes the permit window k for a session (--lookahead k, native_gate_lookahead = k) and the
input delays that must cover it (pilot >= k+2, orders >= k+1). Protocol 5 keeps that input wire and
lets the relay choose: it pings every client while they wait at tick 0, picks k from the slowest
measured round trip and the target step rate, and announces the plan (k and the pilot delay) in the
first permits, before the first gated step. During a session it can announce agreed changes
('from proposal tick c the input lag is L'), each ahead of every tick it affects, so the input
pipeline stays exact: every sealed proposal is carried by exactly one permit, in order, after it was
sealed and before the tick its input delay names. A client stalled past a bound is reported and,
opt-in, dropped (its later proposals are empty for everyone), instead of halting every client.

Everything here is used only with native_gate_relay.py --adaptive. Protocols 1-4 are unchanged.
"""
import collections
import hmac
import math
import struct

from native_gate_relay import Barrier, blob_valid, canonical, LOOKAHEAD_MAX, STATE_MAX

VERSION = 5
PILOT_DELAY_MAX = 30     # PILOT_DELAY_MAX in src/native/engine/lockstep_pilot_inputs.h
ENTRIES_MAX = 32         # ADAPT_ENTRIES in src/native/engine/lockstep_adaptive.h
NONCE_MAX = 10**12
PING_EXPIRY = 5.0        # seconds an unanswered ping is remembered
HELD_MAX_US = 10**9      # largest hold a pong may name
HEX = frozenset('0123456789abcdef')


def percentile(values, fraction):
    """Nearest-rank percentile of a non-empty list (p90 of 8 samples is the 8th)."""
    ordered = sorted(values)
    return ordered[max(0, min(len(ordered)-1, math.ceil(fraction*len(ordered))-1))]


class Schedule:
    """The agreed input lag per proposal tick: entries (start, lag, pilot); entry 1 is the plan (start 0).

    A proposal sealed with tick c is applied at tick c+lag(c) on every client. An entry with a larger
    lag leaves the permits between the previous apply tick and start+lag without inputs (gap permits).
    One with a smaller lag by d makes the d ticks before start quiet: every client seals the empty
    proposal there and no permit carries them. Over the other ticks the apply tick rises by exactly one
    per tick, so nothing is lost, duplicated or applied before it was sealed; a gap only adds ticks.
    pilot is the pilot record delay (record captured at c applies at c+pilot, staged one tick before),
    so lag <= pilot-1 always.
    """
    def __init__(self, entries=()):
        self.entries = []
        for entry in entries:
            self.add(*entry)

    def __len__(self):
        return len(self.entries)

    def valid(self, start, lag, pilot, kmax=LOOKAHEAD_MAX):
        """Whether (start, lag, pilot) may follow the current entries."""
        if not all(type(v) is int for v in (start, lag, pilot)) or not 1 <= lag <= kmax+1 or not lag+1 <= pilot <= PILOT_DELAY_MAX:
            return False
        if not self.entries:
            return start == 0
        if len(self.entries) >= ENTRIES_MAX or pilot != self.entries[0][2]:
            return False                      # the pilot delay is the plan's (lockstep_adaptive.h: ADAPT_STOP_PILOT)
        previous = self.entries[-1]
        return lag != previous[1] and start-max(0, previous[1]-lag) > previous[0]

    def add(self, start, lag, pilot, kmax=LOOKAHEAD_MAX):
        if not self.valid(start, lag, pilot, kmax):
            raise ValueError(f'schedule entry refused: {(start, lag, pilot)} after {self.entries}')
        self.entries.append((start, lag, pilot))

    def first_affected(self, index):
        """Earliest proposal tick entry index (0-based) changes: its start, or its first quiet tick."""
        start, lag, _ = self.entries[index]
        return start-max(0, self.entries[index-1][1]-lag) if index else 0

    def entry_at(self, c):
        """Index of the entry governing proposal tick c (the last one starting at or before c)."""
        index = 0
        for i, (start, _, _) in enumerate(self.entries):
            if start <= c:
                index = i
        return index

    def lag(self, c):
        return self.entries[self.entry_at(c)][1]

    def quiet(self, c):
        for i in range(1, len(self.entries)):
            start, lag, _ = self.entries[i]
            if start-max(0, self.entries[i-1][1]-lag) <= c < start:
                return True
        return False

    def apply(self, c):
        return c+self.lag(c)

    def next_source(self, c):
        """The first non-quiet proposal tick after c."""
        c += 1
        while self.quiet(c):
            c += 1
        return c

    def text(self, index):
        start, lag, pilot = self.entries[index]
        return f'{start}:{lag}:{pilot}'


class RoundTrips:
    """Ping/pong round trips per slot (ms), time-stamped, for the plan and the session policy.

    A client answers a ping when it next reads its socket: at once while it waits for a permit, else
    after the frame it is running. Its pong names how long the ping can have been waiting (held: the
    time since it last found the socket empty, 0 when it was blocked in a receive), so the network round
    trip lies in [measured-held, measured]. Samples keep both ends; 'mid' is the middle of the range.
    """
    WHICH = {'lower': 1, 'upper': 2}

    def __init__(self, slots):
        self.samples = {s: collections.deque(maxlen=512) for s in slots}

    def add(self, slot, at, measured, held=0.0):
        held = min(held, measured)
        self.samples[slot].append((at, measured-held, measured))

    def count(self, slot, since=None):
        return sum(1 for sample in self.samples[slot] if since is None or sample[0] >= since)

    def values(self, slot, since=None, which='mid'):
        rows = [sample for sample in self.samples[slot] if since is None or sample[0] >= since]
        if which == 'mid':
            return [(low+high)/2 for _, low, high in rows]
        return [row[self.WHICH[which]] for row in rows]

    def summary(self, slot, since=None):
        values = self.values(slot, since)
        if not values:
            return {'n': 0}
        return {'n': len(values), 'min': round(min(values), 3), 'p50': round(percentile(values, .5), 3),
                'p90': round(percentile(values, .9), 3), 'max': round(max(values), 3),
                'p90_lower': round(percentile(self.values(slot, since, 'lower'), .9), 3),
                'p90_upper': round(percentile(self.values(slot, since, 'upper'), .9), 3)}


class AdaptiveBarrier(Barrier):
    """Protocol 5 relay: the protocol 4 checks with a schedule-driven window, pings, plan and stall bound."""

    def __init__(self, token, ticks=120, hold_tick=10, hold_seconds=3, compare=True, relative_real=False, observe=False,
                 clients=2, remote=False, kmax=4, kmin=1, tick_hz=60, target_hz=None, samples=8, margin_ms=10.0,
                 measure_seconds=5.0, measure_ping_ms=20.0, session_ping_ms=200.0, changes=False, decrease=False,
                 headroom=0, interval_seconds=2.0, settle=3, stall_report_ms=1000.0, stall_drop_ms=0.0):
        if type(kmax) is not int or not 1 <= kmax <= LOOKAHEAD_MAX:
            raise ValueError('adaptive window maximum must be 1..8')
        if type(kmin) is not int or not 1 <= kmin <= kmax:
            raise ValueError('adaptive window minimum must be 1..maximum')
        if type(headroom) is not int or headroom < 0 or kmax+2+headroom > PILOT_DELAY_MAX:
            raise ValueError('adaptive headroom must keep the pilot delay within 30')
        if type(samples) is not int or not 2 <= samples <= 256:
            raise ValueError('adaptive samples must be 2..256')
        if decrease and not changes:
            raise ValueError('adaptive decreases need adaptive changes')
        for name, value in (('margin', margin_ms), ('measure', measure_seconds), ('ping', measure_ping_ms), ('ping', session_ping_ms),
                            ('interval', interval_seconds), ('stall report', stall_report_ms), ('stall drop', stall_drop_ms)):
            if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                raise ValueError(f'adaptive {name} must be a finite non-negative number')
        if min(measure_ping_ms, session_ping_ms) < 1 or interval_seconds <= 0 or measure_seconds <= 0:
            raise ValueError('adaptive ping intervals, measure time and policy interval must be positive')
        if stall_drop_ms and stall_drop_ms < stall_report_ms:
            raise ValueError('the stall drop bound must not be below the stall report bound')
        super().__init__(token, ticks, hold_tick, hold_seconds, compare=compare, relative_real=relative_real, observe=observe,
                         clients=clients, remote=remote, lookahead=kmax, tick_hz=tick_hz)
        self.kmin, self.kmax = kmin, kmax
        self.frame_ms = 1000.0/tick_hz
        self.target_hz = target_hz or tick_hz
        if not isinstance(self.target_hz, (int, float)) or not 0 < self.target_hz <= tick_hz:
            raise ValueError('adaptive target rate must be 0 < target <= tick rate')
        self.samples_needed, self.margin_ms, self.measure_seconds = samples, float(margin_ms), float(measure_seconds)
        self.measure_ping, self.session_ping = measure_ping_ms/1000.0, session_ping_ms/1000.0
        self.changes, self.decrease, self.headroom = changes, decrease, headroom
        self.interval, self.settle = float(interval_seconds), settle
        self.stall_report, self.stall_drop = stall_report_ms/1000.0, stall_drop_ms/1000.0
        self.schedule = Schedule()
        self.rtt = RoundTrips(self.slots)
        self.pings = {}                       # nonce -> (slot, sent)
        self.nonce = 0
        self.next_ping = {s: 0.0 for s in self.slots}
        self.epochs = {s: 0 for s in self.slots}
        self.dropped = {}                     # slot -> first proposal tick whose blob is empty for everyone
        self.next_permit, self.cursor = 1, 0  # next permit to issue; next non-quiet proposal tick to carry
        self.meta = {}                        # permit T -> (source or None, epoch, mask): identical on every resend
        self.released_by = {}                 # report tick -> permits it released
        self.measure_from = None              # when every tick 0 report was in
        self.plan_at = None
        self.announced = []                   # (epoch, at, released tick, highest permit, start, lag, pilot, rtt)
        self.next_policy, self.lower_runs = None, 0
        self.stalls, self.stall_seen = [], set()
        self.pongs = self.late_pongs = 0
        self.stop_sent = False

    # ---- wire -------------------------------------------------------------------------------------
    def active(self, tick=None):
        """Slots still in the session (for tick: those not dropped at or before it)."""
        return tuple(s for s in self.slots if s not in self.dropped or (tick is not None and tick < self.dropped[s]))

    def permitted(self):
        return min(self.limit, self.next_permit-1)

    def receive(self, data, address, now):
        if len(data) > STATE_MAX[3] or (address[0] != '127.0.0.1' and not self.remote):
            return []
        try:
            f = data.decode('ascii').split('|')
            if len(f) < 5 or not hmac.compare_digest(f[2], self.token):
                return []
            if f[0] == 'NGPONG':
                return self.pong(f, address, now)
            if f[0] != 'NGSTATE' or f[1] not in ('1', '2', '3', '4', '5'):
                return []
            version = int(f[1])
            if version != VERSION:
                # An authenticated, well-formed report of protocols 1-4 at an adaptive relay: old and new
                # transports never mix (as lookahead_mismatch in protocol 4). Stop the session.
                if len(f) != 7+version or not canonical(f[3]) or not canonical(f[4]) or int(f[3]) not in self.slots:
                    return []
                if version == 4 and not canonical(f[9]):
                    return []
                slot = int(f[3])
                if slot in self.peers and self.peers[slot] != address:
                    return []
                self.failed = self.failed or f'lookahead_mismatch_slot_{slot}'
                return [(address, f'NGSTOP|{version}|{self.token}|{slot}|{f[4]}'.encode('ascii'))]
            if len(f) != 11 or f[8] != str(self.clients) or not blob_valid(f[10]) or not all(canonical(v) for v in (f[3], f[4], f[9])):
                return []
            slot, tick, epoch = int(f[3]), int(f[4]), int(f[9])
            if slot not in self.slots or not 0 <= tick <= self.limit or tick > self.permitted() or epoch > ENTRIES_MAX:
                return []
            if any(len(value) != size or any(c not in HEX for c in value) for value, size in ((f[5], 16), (f[6], 16), (f[7], 64))):
                return []
            game, real = (struct.unpack('>d', bytes.fromhex(value))[0] for value in f[5:7])
            if not math.isfinite(game) or not math.isfinite(real):
                return []
            if slot in self.peers and self.peers[slot] != address:
                return []
            self.peers[slot] = address
            self.version = VERSION
            if slot in self.dropped:
                return [(address, f'NGDROP|5|{self.token}|{slot}|{self.dropped[slot]}'.encode('ascii'))]
            self.agree(slot, tick, epoch, f[10])
            row = {'game': game, 'real': real, 'digest': f[7], 'blob': f[10], 'epoch': epoch}
            rows = self.states.setdefault(tick, {})
            if slot in rows and rows[slot] != row:
                self.failed = self.failed or f'changed_state_while_waiting_tick_{tick}'
            elif slot not in rows:
                rows[slot] = row
                self.epochs[slot] = max(self.epochs[slot], epoch)
            self.arrivals.setdefault(tick, now)
            if self.failed or self.done:
                return [(address, self.packet('NGSTOP', slot, tick))]
            if tick == 0 and self.measure_from is None and len(rows) == len(self.active()):
                self.measure_from = now
            if tick <= self.released:
                return [(address, self.packet('NGGO', slot, permit)) for permit in self.released_by.get(tick, ())]
            return self.advance(now)
        except (ValueError, UnicodeError, OverflowError, struct.error):
            return []

    def agree(self, slot, tick, epoch, blob):
        """The lockstep contract: a client knows every entry that changes a tick it sealed; quiet ticks are empty."""
        if epoch > len(self.schedule):
            self.failed = self.failed or f'adaptive_epoch_slot_{slot}_tick_{tick}'
            return
        for index in range(epoch, len(self.schedule)):
            if tick >= max(1, self.schedule.first_affected(index)):   # tick 0 is sealed before the plan
                self.failed = self.failed or f'adaptive_unannounced_slot_{slot}_tick_{tick}'
                return
        if blob and self.schedule.entries and self.schedule.quiet(tick):
            self.failed = self.failed or f'adaptive_quiet_input_slot_{slot}_tick_{tick}'

    def pong(self, f, address, now):
        """NGPONG|5|token|slot|nonce|held_us: a ping's answer and how long it may have waited unread (us)."""
        if len(f) != 6 or f[1] != '5' or not all(canonical(v) for v in f[3:6]) or int(f[5]) > HELD_MAX_US:
            return []
        slot, nonce, held = int(f[3]), int(f[4]), int(f[5])/1000.0
        sent = self.pings.pop(nonce, None)
        if not sent or sent[0] != slot or self.peers.get(slot) != address:
            self.late_pongs += 1
            return []
        self.pongs += 1
        self.rtt.add(slot, now, (now-sent[1])*1000.0, held)
        return []

    def packet(self, kind, slot, tick):
        if kind != 'NGGO':
            return f'{kind}|5|{self.token}|{slot}|{tick}'.encode('ascii')
        source, epoch, mask = self.meta[tick]
        if source is None:
            blobs = ['']*self.clients
        else:
            rows = self.states[source]
            blobs = [rows[s]['blob'] if s in rows and not (mask >> (s-1)) & 1 else '' for s in self.slots]
        return (f'NGGO|5|{self.token}|{slot}|{tick}|{"-" if source is None else source}|{epoch}|{self.schedule.text(epoch-1)}|'
                f'{mask:02x}|'+'|'.join(blobs)).encode('ascii')

    # ---- releases ---------------------------------------------------------------------------------
    def complete(self, tick):
        rows = self.states.get(tick, {})
        return all(s in rows for s in self.active(tick))

    def issue(self, tick, now):
        """Permits released by the reports of tick: up to the apply tick of the next non-quiet source, less one."""
        if self.schedule.quiet(tick):
            return []
        last = min(self.limit, self.schedule.apply(self.schedule.next_source(tick))-1)
        issued = []
        mask = self.meta[self.next_permit-1][2] if self.next_permit > 1 else 0
        while self.next_permit <= last:
            permit = self.next_permit
            if self.schedule.apply(self.cursor) == permit:
                source = self.cursor
                mask = sum(1 << (s-1) for s, d in self.dropped.items() if d <= source)
                self.cursor = self.schedule.next_source(self.cursor)
            else:
                assert self.schedule.apply(self.cursor) > permit, 'a proposal tick would be skipped'
                source = None
            self.meta[permit] = (source, len(self.schedule), mask)
            issued.append(permit)
            self.next_permit += 1
        self.released_by[tick] = issued
        return issued

    def advance(self, now):
        out = []
        while not self.failed and not self.done:
            tick = self.released+1
            if tick > self.limit or not self.complete(tick):
                break
            if tick == 0 and not self.schedule.entries:
                if not self.measured(now):
                    break
                self.plan(now)
                if self.failed:
                    break
            self.check(tick)
            if self.failed or tick == self.limit:
                self.done = not self.failed
                return out+[(self.peers[s], self.packet('NGSTOP', s, tick)) for s in self.active() if s in self.peers]
            if tick == self.hold_tick and now-self.arrivals[tick] < self.hold_seconds:
                break
            self.released = tick
            self.released_at[tick] = now
            out += [(self.peers[s], self.packet('NGGO', s, permit)) for permit in self.issue(tick, now) for s in self.active()]
        return out

    def check(self, tick):
        """Timestep, digest and clock checks of one complete tick among the slots still in the session."""
        slots = self.active(tick)
        rows, first = self.states[tick], slots[0]
        if tick > 0:
            for s in slots:
                delta = rows[s]['game']-self.states[tick-1][s]['game']
                if abs(delta-1/self.tick_hz) > 1e-8:
                    self.failed = f'native_dt_mismatch_player_{s}_tick_{tick}'
        differing = [s for s in slots if rows[s]['digest'] != rows[first]['digest']]
        if self.compare and differing:
            self.mismatch_slots[tick] = differing
            if self.observe:
                if tick not in self.mismatches:
                    self.mismatches.append(tick)
                    if not self.diverged or tick-self.last_notice >= 600:
                        self.notices.append(f'NATIVE_GATE DESYNC tick={tick} slots={",".join(map(str, differing))} '
                                            f'first={self.mismatches[0]} mismatches={len(self.mismatches)}')
                        self.last_notice = tick
                    self.diverged = True
            else:
                self.failed = f'battle_state_mismatch_tick_{tick}'
        elif self.compare and self.observe and self.diverged:
            self.diverged = False
            self.notices.append(f'NATIVE_GATE RESYNC tick={tick} mismatches={len(self.mismatches)}')
        real_equal = all(rows[s]['real'] == rows[first]['real'] for s in slots)
        if self.relative_real:
            elapsed = [rows[s]['real']-self.states[0][s]['real'] for s in slots]
            real_equal = all(abs(elapsed[0]-t) <= 1e-8 for t in elapsed) and all(abs(t-tick/self.tick_hz) <= 1e-8 for t in elapsed)
        if not self.failed and (any(rows[s]['game'] != rows[first]['game'] for s in slots) or not real_equal):
            self.failed = f'native_clock_mismatch_tick_{tick}'

    # ---- measurement and plan ---------------------------------------------------------------------
    def measured(self, now):
        if self.measure_from is None:
            return False
        if all(self.rtt.count(s) >= self.samples_needed for s in self.active()):
            return True
        if now-self.measure_from < self.measure_seconds:
            return False
        for s in self.active():
            if self.rtt.count(s) < 2:
                self.failed = self.failed or f'adaptive_unmeasured_slot_{s}'
        return not self.failed

    def round_trip(self, since=None, which='mid'):
        """The slowest client's p90 round trip (ms) over the samples since then; None without samples."""
        values = [percentile(v, .9) for v in (self.rtt.values(s, since, which) for s in self.active()) if v]
        return max(values) if values else None

    def wanted_k(self, rtt):
        # A target below the tick rate accepts a longer step: (r+f)/(k+1) <= 1/target.
        target_ms = 1000.0/self.target_hz
        need = max(0.0, (rtt+self.margin_ms+self.frame_ms)/target_ms-1)
        return max(self.kmin, min(self.kmax, math.ceil(need-1e-9)))

    def plan(self, now):
        rtt = self.round_trip()
        k = self.wanted_k(rtt)
        pilot = k+2+self.headroom
        self.schedule.add(0, k+1, pilot, self.kmax)
        self.plan_at = now
        self.next_policy = now+self.interval
        self.announced.append({'epoch': 1, 'at': round(now, 6), 'released': -1, 'permitted': 0, 'start': 0, 'lag': k+1,
                               'k': k, 'pilot': pilot, 'rtt_ms': round(rtt, 3)})
        rows = ' '.join(f's{s}={self.rtt.summary(s)["p90"]}' for s in self.active())
        self.notices.append(f'NATIVE_GATE PLAN epoch=1 k={k} lag={k+1} pilot_delay={pilot} rtt_p90_ms={rtt:.3f} {rows} '
                            f'measure_ms={1000*(now-self.measure_from):.0f}')

    def policy(self, now):
        """Opt-in changes during the session: one entry in flight, acknowledged by every client first."""
        if not self.changes or self.failed or self.done or self.plan_at is None or now < self.next_policy:
            return
        since, self.next_policy = now-self.interval, now+self.interval
        epoch = len(self.schedule)
        if any(self.epochs[s] < epoch for s in self.active()):
            return
        low, high = self.round_trip(since, 'lower'), self.round_trip(since, 'upper')
        if low is None:
            return
        start, lag, pilot = self.schedule.entries[-1]
        current = lag-1
        # Raise on the low end of each sample's range, lower on the high end: a pong that waited for a frame
        # moves neither, and the two ends keep about a frame of hysteresis between raising and lowering.
        if min(self.wanted_k(low), pilot-2) > current:
            k, rtt = min(self.wanted_k(low), pilot-2), low
            self.lower_runs = 0
        elif min(self.wanted_k(high), pilot-2) < current and self.decrease:
            k, rtt = min(self.wanted_k(high), pilot-2), high
            self.lower_runs += 1
            if self.lower_runs < self.settle:
                return
        else:
            self.lower_runs = 0
            return
        self.lower_runs = 0
        # Ahead of every tick it changes: no client has sealed a tick after the highest permit issued.
        first = self.next_permit
        new_start = first+max(0, current-k)
        if not self.schedule.valid(new_start, k+1, pilot, self.kmax):
            return
        self.schedule.add(new_start, k+1, pilot, self.kmax)
        self.announced.append({'epoch': epoch+1, 'at': round(now, 6), 'released': self.released, 'permitted': self.next_permit-1,
                               'start': new_start, 'lag': k+1, 'k': k, 'pilot': pilot, 'rtt_ms': round(rtt, 3)})
        self.notices.append(f'NATIVE_GATE SCHEDULE epoch={epoch+1} from={new_start} k={current}->{k} lag={k+1} '
                            f'quiet={max(0, current-k)} gaps={max(0, k-current)} rtt_p90_ms={rtt:.3f} permitted={self.next_permit-1}')

    # ---- stalls -----------------------------------------------------------------------------------
    def stalled(self, now):
        """The slots the next tick waits for, with how long (s) the others have been waiting for them."""
        tick = self.released+1
        rows = self.states.get(tick, {})
        if tick > self.limit or not rows or self.complete(tick) or self.failed or self.done:
            return tick, 0.0, []
        since = max(self.arrivals[tick], self.released_at.get(tick-1, self.arrivals[tick]))
        return tick, now-since, [s for s in self.active(tick) if s not in rows]

    def stall(self, now):
        tick, waited, missing = self.stalled(now)
        if not missing:
            return []
        out = []
        for s in missing:
            if waited >= self.stall_report and (s, tick) not in self.stall_seen:
                self.stall_seen.add((s, tick))
                self.stalls.append({'slot': s, 'tick': tick, 'reported_ms': round(1000*waited, 1)})
                self.notices.append(f'NATIVE_GATE STALL slot={s} tick={tick} waited_ms={1000*waited:.0f}')
        if self.stall_drop and waited >= self.stall_drop and tick > 0 and self.schedule.entries:
            for s in missing:
                self.dropped[s] = tick
                self.notices.append(f'NATIVE_GATE DROP slot={s} tick={tick} waited_ms={1000*waited:.0f} '
                                    f'remaining={",".join(map(str, self.active())) or "none"}')
            if not self.active():
                self.failed = f'adaptive_all_dropped_tick_{tick}'
                return []
            out += self.advance(now)
        return out

    # ---- periodic ---------------------------------------------------------------------------------
    def poll(self, now):
        """Called by the relay loop between datagrams: pings, the plan once measured, policy and stalls."""
        if self.done:
            return []
        if self.failed:
            return self.stop_all()
        out = []
        for nonce in [n for n, (_, sent) in self.pings.items() if now-sent > PING_EXPIRY]:
            del self.pings[nonce]
        interval = self.measure_ping if self.plan_at is None else self.session_ping
        for s in self.active():
            if s in self.peers and now >= self.next_ping[s]:
                self.nonce = self.nonce % NONCE_MAX+1
                self.pings[self.nonce] = (s, now)
                self.next_ping[s] = now+interval
                out.append((self.peers[s], f'NGPING|5|{self.token}|{s}|{self.nonce}'.encode('ascii')))
        self.policy(now)
        out += self.stall(now)
        out += self.advance(now)
        return out+self.stop_all() if self.failed else out

    def stop_all(self):
        """Once after a failure: every client still in the session gets the stop for its newest report."""
        if self.stop_sent:
            return []
        self.stop_sent = True
        out = []
        for s in self.active():
            reported = [t for t, rows in self.states.items() if s in rows]
            if s in self.peers and reported:
                out.append((self.peers[s], self.packet('NGSTOP', s, max(reported))))
        return out

    # ---- reports ----------------------------------------------------------------------------------
    def adaptive_summary(self):
        return {'protocol': VERSION, 'kmin': self.kmin, 'kmax': self.kmax, 'target_hz': self.target_hz, 'margin_ms': self.margin_ms,
                'headroom': self.headroom, 'changes': self.changes, 'decrease': self.decrease,
                'schedule': [{'start': s, 'lag': l, 'k': l-1, 'pilot': p} for s, l, p in self.schedule.entries],
                'announced': self.announced, 'rtt_ms': {s: self.rtt.summary(s) for s in self.slots},
                'acknowledged': dict(self.epochs), 'stalls': self.stalls, 'dropped': dict(self.dropped),
                'pongs': self.pongs, 'late_pongs': self.late_pongs,
                'gap_permits': sum(1 for source, _, _ in self.meta.values() if source is None)}

    def status(self, now):
        status = super().status(now)
        entry = self.schedule.entries[-1] if self.schedule.entries else None
        status.update({'adaptive': True, 'epoch': len(self.schedule), 'k': entry[1]-1 if entry else None,
                       'pilot_delay': entry[2] if entry else None, 'permitted': self.permitted(),
                       'dropped': dict(self.dropped), 'stalls': len(self.stalls),
                       'rtt_p90_ms': {s: self.rtt.summary(s).get('p90') for s in self.slots}})
        return status

    def summary(self):
        summary = super().summary()
        summary['adaptive'] = self.adaptive_summary()
        summary['permits'] = {t: {'source': m[0], 'epoch': m[1], 'mask': m[2]} for t, m in self.meta.items()}
        return summary
