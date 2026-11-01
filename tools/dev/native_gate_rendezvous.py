"""NGNAT protocol 1: opt-in rendezvous for direct player links (roadmap B7), a relay extension.

The relay introduces two players so they can punch UDP holes towards each other; it
never forwards their traffic and never touches the lockstep barrier. Started by
native_gate_relay.py --rendezvous-port P (a thread with its own two sockets, P and P+1)
or standalone (python native_gate_rendezvous.py --port P --token ...).

Wire (ASCII, '|'-separated, last field = MAC = first 16 bytes of HMAC-SHA256 as 32 hex):
  rendezvous key  Kr = HMAC-SHA256(key=session token as 32 ASCII bytes, msg="NGNAT|1|rendezvous")
  client -> relay (Kr; padded with a '.' field to REQUEST_MIN bytes):
    NGNAT|1|HELLO|slot|inst|seq|lan|map|mapping|filter|pad|mac   to P and to P+1 (mapping test)
    NGNAT|1|FTEST|slot|inst|seq|pad|mac                  to P (filtering test)
    NGNAT|1|ASK|slot|inst|seq|peer|pad|mac               to P
    NGNAT|1|BYE|slot|inst|seq|pad|mac                    to P
  relay -> client (Kr; only ever to the source of the request, never larger than it):
    NGNAT|1|SEEN|slot|inst|seq|port_index|observed|mapping|mac   from the port it arrived on
    NGNAT|1|FSEEN|slot|inst|seq|mac                              from P+1 (the client never sent there)
    NGNAT|1|PEER|slot|inst|seq|peer|peer_inst|mapping|filter|public|lan|map|pairkey|mac
    NGNAT|1|WAIT|slot|inst|seq|peer|mac                          peer not registered (yet)
    NGNAT|1|GONE|slot|inst|seq|mac
  client <-> client (MAC key = the 16-byte pair key from PEER, same for both sides):
    NGNAT|1|PROBE|from|to|from_inst|to_inst|nonce|mac   ACK echoes the nonce;  CLOSE ends the path
inst = 16 hex (random per client start), seq = canonical decimal, increasing per request;
addresses a.b.c.d:port or '-'; mapping unknown|open|eim|edm; filter unknown|open|port.
HELLO carries the client's own classification (its filtering test, FTEST to P before any
datagram to P+1, then its mapping test); PEER passes it on. The relay's own mapping view
(SEEN) is a diagnostic and the fallback for 'unknown'.

Reflector rules (the relay must not become an open reflector or amplifier):
  * no reply at all to anything that is not a well-formed request with a valid Kr MAC
    (a spoofed source without the session token gets nothing);
  * requests shorter than REQUEST_MIN are ignored and no reply is longer than its request;
  * replies go only to the request's source address (never to an address in a payload);
  * per-source-address and global token buckets; a replayed (slot, inst, seq) is ignored;
  * only registered players learn addresses, and only of other registered players of the
    same session; a LAN address is shown only to peers with the same public IP.
"""
import argparse
import hashlib
import hmac
import ipaddress
import secrets
import select
import socket
import threading
import time

VERSION = '1'
REQUEST_MIN = 256
DATAGRAM_MAX = 512
CLIENTS_MAX = 8
HEX = frozenset('0123456789abcdef')
MAPPINGS = ('unknown', 'open', 'eim', 'edm')
FILTERS = ('unknown', 'open', 'port')
REPLAY_WINDOW = 64
SEQ_MAX = 999999999999999


def rendezvous_key(token):
    return hmac.new(token.encode('ascii'), b'NGNAT|1|rendezvous', hashlib.sha256).digest()


def seal(key, body):
    """body + '|' + mac."""
    return body + b'|' + hmac.new(key, body, hashlib.sha256).digest()[:16].hex().encode('ascii')


def verify(key, data):
    if len(data) < 34 or data[-33:-32] != b'|':
        return False
    expected = hmac.new(key, data[:-33], hashlib.sha256).digest()[:16].hex().encode('ascii')
    return hmac.compare_digest(expected, data[-32:])


def request(key, kind, slot, inst, seq, *fields, pad_to=REQUEST_MIN):
    """A client request as the C core builds it (tests and tools)."""
    body = '|'.join(['NGNAT', VERSION, kind, str(slot), inst, str(seq), *map(str, fields)]).encode('ascii')
    pad = pad_to - len(body) - 34
    return seal(key, body + b'|' + b'.' * max(pad, 0))


def canonical(text, top):
    return text.isdecimal() and text.isascii() and str(int(text)) == text and int(text) <= top


def is_hex(text, length):
    return len(text) == length and all(c in HEX for c in text)


def parse_addr(text):
    """'a.b.c.d:port' (canonical numbers) or '-' -> (ip, port) or None; ValueError if malformed."""
    if text == '-':
        return None
    host, _, port = text.rpartition(':')
    parts = host.split('.')
    if len(parts) != 4 or not all(canonical(p, 255) for p in parts) or not canonical(port, 65535) or not int(port):
        raise ValueError(text)
    if host == '0.0.0.0':
        raise ValueError(text)
    return host, int(port)


def fmt_addr(addr):
    return '-' if addr is None else f'{addr[0]}:{addr[1]}'


# The same set as nat_ip_private in lockstep_nat.h: RFC 1918, shared CGN space, link-local.
PRIVATE = tuple(ipaddress.IPv4Network(n) for n in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '100.64.0.0/10', '169.254.0.0/16'))


def private(ip):
    a = ipaddress.IPv4Address(ip)
    return any(a in network for network in PRIVATE)


class Bucket:
    def __init__(self, rate, burst):
        self.rate, self.burst, self.level, self.at = rate, burst, burst, None

    def take(self, now):
        if self.at is not None:
            self.level = min(self.burst, self.level + (now - self.at) * self.rate)
        self.at = now
        if self.level < 1:
            return False
        self.level -= 1
        return True


class Registration:
    def __init__(self, inst):
        self.inst = inst
        self.observed = [None, None]
        self.lan = self.mapped = None
        self.filter = self.reported = 'unknown'
        self.seen_at = 0.0
        self.top = 0
        self.window = set()

    def fresh(self, seq):
        """Replay guard: each (inst, seq) is accepted once, within a window below the highest."""
        if seq in self.window or seq <= self.top - REPLAY_WINDOW:
            return False
        self.window.add(seq)
        if seq > self.top:
            self.top = seq
            self.window = {s for s in self.window if s > self.top - REPLAY_WINDOW}
        return True

    def peer_mapping(self):
        """What peers are told: the client's own measurement, else the relay's view."""
        return self.reported if self.reported != 'unknown' else self.mapping()

    def mapping(self):
        """The relay's view from the latest HELLO on each port (stale after the NAT re-maps)."""
        if self.observed[0] is None:
            return 'unknown'
        if self.lan is not None and self.lan == self.observed[0]:
            return 'open'
        if self.observed[1] is None:
            return 'unknown'
        return 'eim' if self.observed[0] == self.observed[1] else 'edm'


class Rendezvous:
    """Sans-IO NGNAT/1 server: receive(data, address, port_index, now) -> [(port_index, address, packet)]."""

    def __init__(self, token, clients=CLIENTS_MAX, allow=None, allow_loopback=False, rate=30.0, burst=60,
                 global_rate=400.0, expiry=300.0, random_bytes=secrets.token_bytes):
        if len(token) != 32 or any(c not in HEX for c in token):
            raise ValueError('invalid token')
        if type(clients) is not int or not 2 <= clients <= CLIENTS_MAX:
            raise ValueError('clients must be 2..8')
        self.key = rendezvous_key(token)
        self.clients = clients
        self.allow = set(allow) if allow else None
        self.allow_loopback = allow_loopback
        self.rate, self.burst = rate, burst
        self.global_bucket = Bucket(global_rate, global_rate)
        self.buckets = {}
        self.expiry = expiry
        self.random_bytes = random_bytes
        self.registrations = {}     # slot -> Registration
        self.pair_keys = {}         # frozenset({(slot, inst), (peer, inst)}) -> 16 bytes
        self.counters = {'accepted': 0, 'refused': 0, 'rate_limited': 0, 'replies': 0}

    def _refuse(self):
        self.counters['refused'] += 1
        return []

    def _lan_ok(self, addr):
        if addr is None:
            return True
        if ipaddress.IPv4Address(addr[0]).is_loopback:
            return self.allow_loopback
        return private(addr[0])

    def _map_ok(self, addr):
        if addr is None:
            return True
        ip = ipaddress.IPv4Address(addr[0])
        if ip.is_loopback:
            return self.allow_loopback
        return not private(addr[0]) and not ip.is_multicast and not ip.is_reserved

    def _expire(self, now):
        for slot in [s for s, r in self.registrations.items() if now - r.seen_at > self.expiry]:
            self._forget(slot)

    def _forget(self, slot):
        reg = self.registrations.pop(slot, None)
        if reg:
            self.pair_keys = {k: v for k, v in self.pair_keys.items() if (slot, reg.inst) not in k}

    def receive(self, data, address, index, now):
        if not REQUEST_MIN <= len(data) <= DATAGRAM_MAX or not data.startswith(b'NGNAT|1|'):
            return self._refuse()
        if self.allow is not None and address[0] not in self.allow:
            return self._refuse()
        if not verify(self.key, data):
            return self._refuse()
        try:
            f = data.decode('ascii').split('|')
        except UnicodeError:
            return self._refuse()
        if len(f) < 8 or f[2] not in ('HELLO', 'FTEST', 'ASK', 'BYE') or not canonical(f[3], self.clients) \
                or not int(f[3]) or not is_hex(f[4], 16) or not canonical(f[5], SEQ_MAX) or not int(f[5]):
            return self._refuse()
        kind, slot, inst, seq = f[2], int(f[3]), f[4], int(f[5])
        if any(c != '.' for c in f[-2]):
            return self._refuse()
        expected = {'HELLO': 12, 'FTEST': 8, 'ASK': 9, 'BYE': 8}[kind]
        if len(f) != expected or (kind != 'HELLO' and index != 0):
            return self._refuse()
        bucket = self.buckets.setdefault(address[0], Bucket(self.rate, self.burst))
        if len(self.buckets) > 1024:
            self.buckets = {address[0]: bucket}
        if not bucket.take(now) or not self.global_bucket.take(now):
            self.counters['rate_limited'] += 1
            return []
        self._expire(now)
        reg = self.registrations.get(slot)
        if kind == 'HELLO':
            try:
                lan, mapped = parse_addr(f[6]), parse_addr(f[7])
            except ValueError:
                return self._refuse()
            if f[8] not in MAPPINGS or f[9] not in FILTERS:
                return self._refuse()
            if reg is None or reg.inst != inst:
                if reg is not None:
                    self._forget(slot)     # the player restarted: new instance, new pair keys
                reg = self.registrations[slot] = Registration(inst)
            if not reg.fresh(seq):
                return self._refuse()
            reg.observed[index] = (address[0], address[1])
            # lan stays for the 'open' check (a host without NAT names its public address);
            # it is shown to peers only when private. An unusable mapped address is dropped.
            reg.lan, reg.reported, reg.filter, reg.seen_at = lan, f[8], f[9], now
            reg.mapped = mapped if self._map_ok(mapped) else None
            reply = [index, 'SEEN', str(index), fmt_addr(reg.observed[index]), reg.mapping()]
        else:
            if reg is None or reg.inst != inst or not reg.fresh(seq):
                return self._refuse()   # only registered players are answered
            if kind == 'FTEST':
                reply = [1, 'FSEEN']
            elif kind == 'BYE':
                self._forget(slot)
                reply = [0, 'GONE']
            else:
                if not canonical(f[6], self.clients) or int(f[6]) in (0, slot):
                    return self._refuse()
                peer = int(f[6])
                other = self.registrations.get(peer)
                if other is None or other.observed[0] is None:
                    reply = [0, 'WAIT', str(peer)]
                else:
                    pair = frozenset({(slot, inst), (peer, other.inst)})
                    if pair not in self.pair_keys:
                        self.pair_keys[pair] = self.random_bytes(16)
                    key = self.pair_keys[pair]
                    same_site = other.observed[0][0] == address[0] and other.lan is not None and self._lan_ok(other.lan)
                    reply = [0, 'PEER', str(peer), other.inst, other.peer_mapping(), other.filter, fmt_addr(other.observed[0]),
                             fmt_addr(other.lan if same_site else None), fmt_addr(other.mapped), key.hex()]
        port_index, kind_out, *rest = reply
        packet = seal(self.key, '|'.join(['NGNAT', VERSION, kind_out, str(slot), inst, str(seq), *rest]).encode('ascii'))
        if len(packet) > len(data):
            return self._refuse()      # never amplify
        self.counters['accepted'] += 1
        self.counters['replies'] += 1
        return [(port_index, address, packet)]

    def summary(self):
        return {'registered': {slot: {'mapping': r.peer_mapping(), 'filter': r.filter} for slot, r in sorted(self.registrations.items())},
                **self.counters}


def serve(rendezvous, sockets, stop, log=None):
    """Two-socket loop (P, P+1) until stop is set; used by the relay thread and the CLI."""
    while not stop.is_set():
        readable, _, _ = select.select(sockets, [], [], 0.2)
        for s in readable:
            try:
                data, address = s.recvfrom(65536)
            except (ConnectionResetError, OSError):
                continue
            for index, dest, packet in rendezvous.receive(data, address, sockets.index(s), time.monotonic()):
                try:
                    sockets[index].sendto(packet, dest)
                except OSError:
                    pass


def open_sockets(bind, port):
    if not 1024 <= port <= 65534:
        raise ValueError('rendezvous port must be 1024..65534 (P and P+1)')
    sockets = []
    try:
        for p in (port, port+1):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.bind((bind, p))
            sockets.append(s)
    except OSError:
        for s in sockets:
            s.close()
        raise
    return sockets


def start_thread(token, bind, port, clients, allow=None, allow_loopback=False):
    """For native_gate_relay.py: returns (thread, stop event, rendezvous, sockets)."""
    rendezvous = Rendezvous(token, clients=clients, allow=allow, allow_loopback=allow_loopback)
    sockets = open_sockets(bind, port)
    stop = threading.Event()
    thread = threading.Thread(target=serve, args=(rendezvous, sockets, stop), name='ngnat-rendezvous', daemon=True)
    thread.start()
    return thread, stop, rendezvous, sockets


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--bind', default='127.0.0.1')
    p.add_argument('--allow-from', default='', metavar='IP[,IP...]', help='required for a non-loopback --bind')
    p.add_argument('--port', type=int, required=True, help='P; the filtering/mapping tests also use P+1')
    p.add_argument('--token', required=True)
    p.add_argument('--clients', type=int, default=CLIENTS_MAX)
    p.add_argument('--allow-loopback', action='store_true', help='tests on one PC: accept 127/8 LAN and mapped addresses')
    args = p.parse_args()
    allowed = set(filter(None, (part.strip() for part in args.allow_from.split(','))))
    if args.bind != '127.0.0.1' and not allowed:
        p.error('a non-loopback --bind needs --allow-from')
    rendezvous = Rendezvous(args.token, clients=args.clients, allow=allowed or None, allow_loopback=args.allow_loopback)
    sockets = open_sockets(args.bind, args.port)
    print(f'NGNAT RENDEZVOUS READY ports={args.port},{args.port+1}', flush=True)
    stop = threading.Event()
    try:
        serve(rendezvous, sockets, stop)
    except KeyboardInterrupt:
        pass
    finally:
        for s in sockets:
            s.close()


if __name__ == '__main__':
    main()
