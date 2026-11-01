"""Joining a running lockstep session from a forked save (roadmap D14): the relay side, a prototype.

Not wired into native_gate_relay.py and no wire change for existing clients: tests/test_native_gate_join.py
drives it against the relay's own Barrier and its --input-journal lines. The design and its limits are in
reports/LOCKSTEP-NONBLOCKING-SAVE-20261101.txt (section JOIN PROTOCOL).

Sequence (every message ASCII, '|'-separated, authenticated by the session token like NGSTATE/NGGO):
  joiner -> relay   NGJOIN|1|token|slot|nonce                  join as an absent slot of the session
  relay  -> server  NGFORK|1|token|server|F|name               fork at session tick F (the server's probe calls
                                                               gate.fork_save(F, name, ...)); F = released + lead
  server -> relay   NGFORKACK|1|token|server|F|name            armed; until then the relay holds the permit of tick F,
                                                               so the arm always lands before frame F ends
  server -> relay   NGFORKED|1|token|server|F|state|bytes|sha  the child's result (FORK_SAVE status); failed: a new F
  relay  -> joiner  NGSAVE|1|token|slot|F|bytes|sha            fetch (out of band, sha-checked) and boot from it with
                                                               native_gate_snapshot_sha = sha
  catch-up          the joiner's own gate wire, unchanged: it counts ticks from its load (t = session tick - F) and
                    reports NGSTATE t; the relay answers NGGO t+1 with exactly the blobs the session's permit of
                    F+t+1 carried (journal rows of F+t, or of F+t-k with lookahead k). Its digest and game time at t
                    must equal the server's journaled ones at F+t, else NGSTOP: the loaded world is not the server's.
                    Nothing paces it: it runs as fast as its frames allow.
  relay  -> all     NGHANDOVER|1|token|slot|H                  once the joiner is within handover_gap ticks of the
                    live session, the relay holds the live permits at H until the joiner reported H-F; from H on the
                    slot is a full client (its blob instead of the empty one), its ticks still offset by F.
Before the handover the joining slot is absent: the relay stands in for it with the server's digest and clocks
and an empty blob, so the clients (configured with the session's maximum N from the start) never wait for it.
"""
import hmac
import json
import math
import struct

PROTOCOL = 1
HEX = frozenset('0123456789abcdef')


def message(kind, token, *fields):
    return '|'.join([kind, str(PROTOCOL), token, *map(str, fields)]).encode('ascii')


def parse(data, token, kind, count):
    """Fields after the token of an authenticated message of this kind with count fields, else None."""
    try:
        f = data.decode('ascii').split('|')
    except UnicodeError:
        return None
    if len(f) != 3+count or f[0] != kind or f[1] != str(PROTOCOL) or not hmac.compare_digest(f[2], token):
        return None
    return f[3:]


def load_journal(path):
    """The relay's --input-journal: tick -> {slot: row}; JSON object keys are strings, slots become ints again."""
    journal = {}
    with open(path, encoding='ascii') as lines:
        for line in lines:
            if line.strip():
                record = json.loads(line)
                journal[int(record['tick'])] = {int(slot): row for slot, row in record['rows'].items()}
    return journal


def absent_row(server_row):
    """The relay's stand-in report for a slot nobody plays yet: the server's state, no input."""
    return {'game': server_row['game'], 'real': server_row['real'], 'digest': server_row['digest'], 'blob': ''}


def catch_up_seconds(gap_ticks, live_hz, joiner_steps_per_second):
    """Seconds a joiner needs to close a gap while the session keeps running at live_hz; inf if never."""
    margin = joiner_steps_per_second-live_hz
    return math.inf if margin <= 0 else gap_ticks/margin


class JoinCoordinator:
    """One join at a time: fork request, the permit hold, save hand-out, catch-up replay, handover."""
    def __init__(self, token, clients, server_slot=1, lookahead=0, lead=60, handover_gap=30, retries=3):
        if len(token) != 32 or any(c not in HEX for c in token):
            raise ValueError('invalid token')
        if not 2 <= clients <= 8 or not 1 <= server_slot <= clients or not 0 <= lookahead <= 8:
            raise ValueError('clients 2..8, server slot among them, lookahead 0..8')
        if lead <= lookahead+1 or handover_gap < 1:
            raise ValueError('the fork lead must exceed the permit window; the handover gap must be positive')
        self.token, self.clients, self.server, self.lookahead = token, clients, server_slot, lookahead
        self.lead, self.handover_gap, self.retries = lead, handover_gap, retries
        self.state, self.slot, self.fork_tick, self.name, self.acked = 'idle', None, None, None, False
        self.save, self.joiner_tick, self.handover, self.failure, self.attempts = None, -1, None, None, 0

    # ---- fork ----
    def request(self, data, released):
        f = parse(data, self.token, 'NGJOIN', 2)
        if not f or self.state != 'idle' or not f[0].isdigit():
            return []
        slot = int(f[0])
        if not 1 <= slot <= self.clients or slot == self.server:
            return []
        self.slot = slot
        return self._fork(released)

    def _fork(self, released):
        self.attempts += 1
        self.fork_tick, self.acked, self.state = released+self.lead, False, 'forking'
        self.name = f'join_{self.fork_tick}'[:15]
        return [('server', message('NGFORK', self.token, self.server, self.fork_tick, self.name))]

    def may_release(self, permit_tick):
        """The live barrier asks before each permit: the permit of the fork tick waits for the server's arm."""
        return not (self.state == 'forking' and not self.acked and permit_tick >= self.fork_tick)

    def server_message(self, data, released):
        ack = parse(data, self.token, 'NGFORKACK', 3)
        if ack and self.state == 'forking' and ack == [str(self.server), str(self.fork_tick), self.name]:
            self.acked = True
            return []
        done = parse(data, self.token, 'NGFORKED', 5)
        if not done or self.state != 'forking' or done[:2] != [str(self.server), str(self.fork_tick)]:
            return []
        state, size, sha = done[2:]
        if state == 'done' and size.isdigit() and int(size) > 0 and len(sha) == 64 and set(sha) <= HEX:
            self.save, self.state = (int(size), sha), 'loading'
            return [('joiner', message('NGSAVE', self.token, self.slot, self.fork_tick, size, sha))]
        if self.attempts > self.retries:
            self.state, self.failure = 'failed', f'fork_{state}_after_{self.attempts}'
            return []
        return self._fork(released)

    # ---- catch-up ----
    def replay(self, data, journal, released):
        """The joiner's NGSTATE (its own tick t): checked against the server's row of F+t, answered from the journal."""
        if self.state not in ('loading', 'catching_up'):
            return []
        try:
            f = data.decode('ascii').split('|')
        except UnicodeError:
            return []
        version = 4 if self.lookahead else 3
        if len(f) != 7+version or f[0] != 'NGSTATE' or f[1] != str(version) or not hmac.compare_digest(f[2], self.token):
            return []
        if f[3] != str(self.slot) or not f[4].isdigit() or f[8] != str(self.clients):
            return []
        t = int(f[4])
        session = self.fork_tick+t
        server_row = journal.get(session, {}).get(self.server)
        if server_row is None or t > self.joiner_tick+1:
            return []  # not journaled yet, or a report from the future: the joiner retransmits
        # Tick 0 carries the boot digest, not a world digest: only the game time is compared there.
        if f[5] != struct.pack('>d', server_row['game']).hex() or (t and f[7] != server_row['digest']):
            self.state, self.failure = 'failed', f'join_mismatch_tick_{session}'
            return [('joiner', f'NGSTOP|{version}|{self.token}|{self.slot}|{t}'.encode('ascii'))]
        self.state, self.joiner_tick = 'catching_up', max(self.joiner_tick, t)
        if self.handover is None and released-session <= self.handover_gap:
            self.handover = released+1
            return [('all', message('NGHANDOVER', self.token, self.slot, self.handover))]+self._permit(journal, t, version)
        if self.handover is not None and session >= self.handover:
            self.state = 'joined'
            return []
        return self._permit(journal, t, version)

    def _permit(self, journal, t, version):
        """NGGO t+1 for the joiner: the blobs of the session's permit of F+t+1, in slot order."""
        source = self.fork_tick+t-self.lookahead
        if source >= 0 and source not in journal:
            return []
        rows = journal.get(source, {})
        blobs = [rows.get(s, {}).get('blob', '') for s in range(1, self.clients+1)] if source >= 0 else ['']*self.clients
        head = f'NGGO|{version}|{self.token}|{self.slot}|{t+1}' + (f'|{self.lookahead}' if version == 4 else '')
        return [('joiner', (head+''.join('|'+b for b in blobs)).encode('ascii'))]

    def live_hold(self, permit_tick):
        """After the handover tick the live session waits until the joiner reported it."""
        return self.handover is not None and self.state == 'catching_up' and permit_tick > self.handover
