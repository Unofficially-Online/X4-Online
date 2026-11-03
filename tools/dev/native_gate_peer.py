"""Opt-in direct player-to-player input links (roadmap B6): peer extension 1 over protocol 4.

Python reference of the client rules in src/native/engine/lockstep_peer.h, the peer wire and
the interfaces expected from the NAT traversal and crypto modules (stubs here). No engine
access. The relay side (address exchange, reports up to m ticks past the newest permit) is
native_gate_relay.py --peer-window M. Specification: docs/LOCKSTEP-NSLOT-WIRE.txt, PEER LINKS;
measurements: tools/dev/native_gate_peer_sim.py, reports/LOCKSTEP-PEER-LINKS-20261101.txt.

Rules (k = protocol 4 window >= 1, m = peer window, k + m <= 8):
- a client sends the proposal it seals with tick t (its NGSTATE blob) once to every peer it can
  reach; that record is applied at T = t+1+k on every client, exactly as with the relay permit;
- F is the highest tick whose relay permit, and every earlier one, has arrived. A client runs T
  only if T <= F + m, either on the relay permit for T or, without it, on all N records of
  T-1-k (F >= 1: the relay admitted tick 0 for everyone first; never past the relay's last
  tick, which NGPADDR names). m = 0 exchanges records only;
- every relay permit is compared with the peer copies of its records, whether the tick ran on
  them, will run on the permit or ran already: any difference stops the client (104), two
  different peer copies of one record stop it (108); a malformed, unauthenticated or unexpected
  peer datagram is dropped. The relay stays the referee: digests, clocks, timeouts, stops.
"""
import hmac

PEER_VERSION = 1
LOOKAHEAD_MAX = 8          # GATE_LOOKAHEAD_MAX; k + m <= 8 keeps every unconfirmed report in the native ring
CLIENTS_MAX = 8
BLOB_HEX_MAX = 480
SIGNATURE_HEX_MAX = 256    # lowercase hex; Ed25519 needs 128
PEER_RING = 32             # apply ticks tracked; a live record is never more than 2k+m+3 <= 19 ticks past F
HELLO_RESEND = 0.1         # seconds between NGPHELLO copies until the relay answers
STOP_PERMIT_CONFLICT = 103  # LOOKAHEAD_STOP_CONFLICT: two different relay permits for one tick
STOP_MISMATCH = 104        # a peer copy differs from the relay's bundle
STOP_EQUIVOCATION = 108    # two different peer copies of one record (105: the Linux gate's)
PENDING = 'pending'        # StubNat.route: still punching, ask again
HEX = frozenset('0123456789abcdef')


def canonical(value):
    return value.isascii() and value.isdecimal() and str(int(value)) == value


def blob_valid(value, limit=BLOB_HEX_MAX):
    return len(value) <= limit and len(value) % 2 == 0 and all(c in HEX for c in value)


def window_valid(k, m):
    return type(k) is int and type(m) is int and k >= 1 and m >= 0 and k+m <= LOOKAHEAD_MAX


def hello(token, slot, clients, k, m):
    """The opt-in, sent from the peer socket to the relay until answered."""
    return f'NGPHELLO|{PEER_VERSION}|{token}|{slot}|{clients}|{k}|{m}'.encode('ascii')


def parse_address(text):
    """'a.b.c.d:port' (canonical decimals) -> (ip, port) or None."""
    ip, sep, port = text.rpartition(':')
    parts = ip.split('.')
    if not sep or len(parts) != 4 or not all(canonical(p) and int(p) <= 255 for p in parts):
        return None
    if not canonical(port) or not 1 <= int(port) <= 65535:
        return None
    return ip, int(port)


def parse_relay_answer(data, token, slot, clients):
    """NGPADDR -> ('addresses', m, last, [(ip, port)] in slot order); NGPREFUSE -> ('refused', reason); else None."""
    try:
        f = data.decode('ascii').split('|')
    except UnicodeError:
        return None
    if len(f) < 5 or f[1] != str(PEER_VERSION) or not hmac.compare_digest(f[2], token) or f[3] != str(slot):
        return None
    if f[0] == 'NGPREFUSE' and len(f) == 5 and f[4] in ('off', 'mismatch'):
        return 'refused', f[4]
    if f[0] != 'NGPADDR' or len(f) != 6+clients or not canonical(f[4]) or not canonical(f[5]):
        return None
    book = [parse_address(entry) for entry in f[6:]]
    return None if None in book else ('addresses', int(f[4]), int(f[5]), book)


def fail_notice(token, slot, tick, reason):
    """Sent once to the relay when a peer check stops this client: the relay stops everyone."""
    return f'NGPFAIL|{PEER_VERSION}|{token}|{slot}|{tick}|{reason}'.encode('ascii')


def record_text(token, slot, tick, clients, k, blob):
    """The signed part of a peer record: the proposal slot sealed with tick."""
    return f'NGPREC|{PEER_VERSION}|{token}|{slot}|{tick}|{clients}|{k}|{blob}'


def parse_record(plain, token, clients, k, crypto):
    """An opened peer record -> (slot, tick, blob), or None (dropped)."""
    try:
        text = plain.decode('ascii')
    except UnicodeError:
        return None
    head, sep, signature = text.rpartition('|')
    f = head.split('|')
    if not sep or len(f) != 8 or f[0] != 'NGPREC' or f[1] != str(PEER_VERSION) or not hmac.compare_digest(f[2], token):
        return None
    if not all(canonical(value) for value in f[3:7]) or not blob_valid(f[7]) or not blob_valid(signature, SIGNATURE_HEX_MAX):
        return None
    slot, tick, n, window = map(int, f[3:7])
    if n != clients or window != k or not 1 <= slot <= clients or not crypto.verify(slot, head, signature):
        return None
    return slot, tick, f[7]


class StubCrypto:
    """What the peer links expect from the crypto module (X4-Multiplayer-crypto).

    seal(plain: bytes) -> bytes           AEAD (e.g. ChaCha20-Poly1305) under the session key the relay
                                          hands out; nonce and framing are the module's.
    open(sealed: bytes) -> bytes | None   None: not sealed with the session key -> dropped, never fatal.
    sign(record: str) -> str              this player's signature over the record text, lowercase hex.
    verify(slot, record, signature) -> bool  against the public key the relay gave out for slot.
    The stub passes datagrams through and signs with the empty string. A forged record can then
    still never be applied silently: the relay's bundle check stops the session (104)."""
    name = 'stub'

    def seal(self, plain):
        return plain

    def open(self, sealed):
        return sealed

    def sign(self, record):
        return ''

    def verify(self, slot, record, signature):
        return signature == ''


class StubNat:
    """What the peer links expect from the NAT module (X4-Multiplayer-nat).

    local_port() -> int                   port for the peer socket (0: any); UPnP maps it before bind.
    route(slot, advertised) -> (ip, port) | None | PENDING
        where to send slot's records from the peer socket. advertised = the address the relay saw
        for that player's peer socket (its NAT mapping). None: unreachable, that slot's records
        come only with the relay's permits. PENDING: still punching; asked again every frame.
    datagram(source, data) -> bool        a datagram on the peer socket that is neither a relay answer
        nor a record from a routed peer (the module's own probes); True: consumed.
    The stub reaches every advertised address (loopback, LAN, open internet)."""
    name = 'stub'

    def local_port(self):
        return 0

    def route(self, slot, advertised):
        return advertised

    def datagram(self, source, data):
        return False


class Entry:
    """Apply tick T: the records sealed with T-1-k, by slot (own, peer copies, or the permit's)."""
    __slots__ = ('tick', 'blobs', 'final', 'ran', 'complete_at', 'permit_at')

    def __init__(self, tick):
        self.tick, self.blobs, self.final, self.ran = tick, {}, False, False
        self.complete_at = self.permit_at = None


class PeerWindow:
    """One client's peer links (lockstep_peer.h). Times are seconds, any monotonic origin."""

    def __init__(self, slot, clients, k, m, token, relay, crypto=None, nat=None):
        if not window_valid(k, m) or not 2 <= clients <= CLIENTS_MAX or not 1 <= slot <= clients:
            raise ValueError('peer links need 2..8 clients, k >= 1, m >= 0, k + m <= 8')
        self.slot, self.clients, self.k, self.m, self.token, self.relay = slot, clients, k, m, token, relay
        self.crypto, self.nat = crypto or StubCrypto(), nat or StubNat()
        self.state = 'hello'          # -> 'active' (NGPADDR) or 'refused' (NGPREFUSE); relay-only until active
        self.final = 0                # F
        self.last = 0                 # the relay's last tick (--ticks): never a peer run past it
        self.entries = {}
        self.routes, self.sources, self.pending = {}, {}, {}
        self.hello_at = None
        self.stats = dict(sent=0, received=0, dropped=0, duplicates=0, stale=0, peer_runs=0, peer_first=0, relay_first=0,
                          lead=0.0, behind_max=0, unreachable=0)

    def entry(self, tick, create=True):
        e = self.entries.get(tick % PEER_RING)
        if e is not None and e.tick == tick:
            return e
        if not create:
            return None
        e = self.entries[tick % PEER_RING] = Entry(tick)
        return e

    def hello_due(self, now):
        """The NGPHELLO datagram for the relay when one is due (every HELLO_RESEND until answered)."""
        if self.state != 'hello' or (self.hello_at is not None and now-self.hello_at < HELLO_RESEND-1e-9):
            return None
        self.hello_at = now
        return hello(self.token, self.slot, self.clients, self.k, self.m)

    def relay_answer(self, data):
        if self.state != 'hello':
            return 0
        answer = parse_relay_answer(data, self.token, self.slot, self.clients)
        if answer is None or (answer[0] == 'addresses' and answer[1] != self.m):
            self.stats['dropped'] += 1
            return 0
        if answer[0] == 'refused':
            self.state = 'refused'
            return 0
        self.last = answer[2]
        self.pending = {s: address for s, address in enumerate(answer[3], 1) if s != self.slot}
        self.state = 'active'
        self.poll_routes()
        return 0

    def poll_routes(self):
        """Ask the NAT module for every peer it has not decided yet."""
        for s, address in list(self.pending.items()):
            route = self.nat.route(s, address)
            if route == PENDING:
                continue
            del self.pending[s]
            if route is None:
                self.stats['unreachable'] += 1
            else:
                self.routes[s], self.sources[route] = route, s

    def complete(self, e, now):
        if e.complete_at is None and not e.final and len(e.blobs) == self.clients:
            e.complete_at = now

    def sealed(self, tick, blob, now):
        """Own proposal sealed with tick: kept for T = tick+1+k; [(route, datagram)] for the peers."""
        e = self.entry(tick+1+self.k)
        e.blobs[self.slot] = blob
        self.complete(e, now)
        if self.state != 'active':
            return []
        self.poll_routes()
        text = record_text(self.token, self.slot, tick, self.clients, self.k, blob)
        data = self.crypto.seal(f'{text}|{self.crypto.sign(text)}'.encode('ascii'))
        self.stats['sent'] += len(self.routes)
        return [(route, data) for route in self.routes.values()]

    def drop(self):
        self.stats['dropped'] += 1
        return 0

    def receive(self, data, source, now):
        """A datagram on the peer socket: 0, or the stop reason."""
        if source == self.relay:
            return self.relay_answer(data)
        if self.state != 'active' or source not in self.sources:
            return 0 if self.nat.datagram(source, data) else self.drop()
        plain = self.crypto.open(data)
        parsed = parse_record(plain, self.token, self.clients, self.k, self.crypto) if plain is not None else None
        if parsed is None or parsed[0] != self.sources[source]:
            return self.drop()
        slot, tick, blob = parsed
        T = tick+1+self.k
        if T <= self.final:
            e = self.entry(T, create=False)
            if e is None:
                self.stats['stale'] += 1
                return 0
        elif T >= self.final+PEER_RING:
            return self.drop()
        else:
            e = self.entry(T)
        held = e.blobs.get(slot)
        if held is not None:
            if held != blob:
                return STOP_MISMATCH if e.final else STOP_EQUIVOCATION
            self.stats['duplicates'] += 1
            return 0
        e.blobs[slot] = blob
        self.stats['received'] += 1
        self.complete(e, now)
        return 0

    def permit(self, T, blobs, now):
        """The relay's bundle for T (blobs in slot order), before or after T ran: 0 or the stop reason."""
        e = self.entry(T, create=T > self.final)
        if e is None:
            return 0
        if e.final:
            return 0 if [e.blobs[s] for s in range(1, self.clients+1)] == list(blobs) else STOP_PERMIT_CONFLICT
        if any(blobs[s-1] != blob for s, blob in e.blobs.items()):
            return STOP_MISMATCH
        e.blobs = {s: blob for s, blob in enumerate(blobs, 1)}
        e.final, e.permit_at = True, now
        if e.complete_at is not None:
            self.stats['peer_first'] += 1
            self.stats['lead'] += now-e.complete_at
        else:
            self.stats['relay_first'] += 1
        while True:
            following = self.entry(self.final+1, create=False)
            if following is None or not following.final:
                break
            self.final += 1
        return 0

    def may_run(self, T):
        """Any tick (on a permit or on peer records) at most m ticks past F."""
        return T <= self.final+self.m

    def take(self, T, now):
        """All N records of T-1-k without the relay's permit, or None."""
        if self.state != 'active' or self.final < 1 or not self.may_run(T) or T > self.last:
            return None
        e = self.entry(T, create=False)
        if e is None or e.final or len(e.blobs) != self.clients:
            return None
        e.ran = True
        self.stats['peer_runs'] += 1
        self.stats['behind_max'] = max(self.stats['behind_max'], T-self.final)
        return [e.blobs[s] for s in range(1, self.clients+1)]

    def resend_from(self, completed):
        """Lowest report the relay may still lack: from F-k (its permit F+1 is missing) or the window."""
        return max(0, min(completed-self.k, self.final-self.k))

    def status(self):
        s = self.stats
        return (f'peer={self.state} window={self.m} sent={s["sent"]} received={s["received"]} peer_runs={s["peer_runs"]} '
                f'peer_first={s["peer_first"]} relay_first={s["relay_first"]} lead_us={round(s["lead"]*1e6)} '
                f'behind_max={s["behind_max"]} dropped={s["dropped"]} unreachable={s["unreachable"]} crypto={self.crypto.name} '
                f'nat={self.nat.name}')
