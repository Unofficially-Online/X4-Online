"""Opt-in wire crypto for the native gate relay (docs/LOCKSTEP-NSLOT-WIRE.txt, WIRE CRYPTO).

Standard library only: the primitives and every byte layout come from the C library
x4mp_wire_crypto (src/native/engine/lockstep_wire_crypto_lib.c over Monocypher 4.0.2), loaded
through ctypes, so the relay and the gates share one implementation. Built by
tools/dev/build_native_gate.py (Windows DLL) and build_native_gate_linux.py (Linux .so).

  keygen --out FILE                      new relay key file (relay Ed25519 seed + psk master)
  credential --key FILE --token T --slot K
                                         the player's X4LAN_CONFIG.native_gate_wire_crypto string
  selftest                               RFC known answers through the library
  bench [--clients N] [--lookahead K]    relay-side cost per tick through ctypes

Key files and credentials are secrets: keep them under .local/ or outside the checkout.
"""
import argparse
import collections
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
KEY_FORMAT = 'x4mp-wire-crypto-relay-1'
OVERHEAD, SIG, HELLO, WELCOME = 32, 64, 88, 104
ZERO_SIG = bytes(SIG)


def default_library():
    configured = os.environ.get('X4MP_WIRE_CRYPTO_LIB')
    if configured:
        return Path(configured)
    if sys.platform == 'win32':
        return ROOT / '.local/native-lockstep/build/x4mp_wire_crypto.dll'
    return ROOT / '.local/native-lockstep/linux/libx4mp_wire_crypto.so'


class Library:
    """The flat C ABI of x4mp_wire_crypto. Refuses a library whose known answers fail."""

    def __init__(self, path=None):
        path = Path(path) if path else default_library()
        if not path.is_file():
            raise FileNotFoundError(f'wire crypto library missing: {path} (build it with tools/dev/build_native_gate.py)')
        self.path = path
        lib = self.lib = ctypes.CDLL(str(path))
        p, u, z, u64 = ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_uint64
        sig = {
            'x4c_abi': ([], ctypes.c_int), 'x4c_selftest': ([], ctypes.c_int), 'x4c_sizeof': ([ctypes.c_int], z),
            'x4c_ed25519_key_pair': ([p, p, p], None), 'x4c_slot_psk': ([p, p, p, u], None),
            'x4c_relay_welcome': ([p, p, p, u, u, p, z, p, p, ctypes.POINTER(u), p, p, p], ctypes.c_int),
            'x4c_client_hello': ([p, p, p, p, u, u, u, p, p, p], ctypes.c_int),
            'x4c_client_welcome': ([p, p, z, p, p], ctypes.c_int), 'x4c_client_keys': ([p, p, p], None),
            'x4c_tx_init': ([p, p, u, u, u, u, u, u64], None), 'x4c_rx_init': ([p, p, u, u, u, u, u], None),
            'x4c_rx_counts': ([p, p], None),
            'x4c_seal': ([p, p, p, z, p, z], z), 'x4c_open': ([p, p, p, z, p, z, ctypes.POINTER(z)], ctypes.c_int),
            'x4c_record_sign': ([p, p, p, u, u, u, u64, p, z], ctypes.c_int),
            'x4c_record_verify': ([p, p, p, u, u, u, u64, p, z], ctypes.c_int),
            'x4c_records_hash': ([p, p, z], None),
            'x4c_permit_sign': ([p, p, p, u64, u, u, p], ctypes.c_int), 'x4c_permit_verify': ([p, p, p, u64, u, u, p], ctypes.c_int),
            'x4c_roster_check': ([p, z, u, u, p], ctypes.c_int),
            'x4c_peer_key': ([p, p, p, u, u], None), 'x4c_peer_tx': ([p, p, p, u, u], None),
            'x4c_peer_seal': ([p, p, p, u, u, u64, p, z, p, z], z),
            'x4c_peer_open': ([p, p, p, u, u, p, z, ctypes.POINTER(u), ctypes.POINTER(u64), p, ctypes.POINTER(z), p], ctypes.c_int),
        }
        for name, (args, result) in sig.items():
            function = getattr(lib, name)
            function.argtypes, function.restype = args, result
        if lib.x4c_abi() != 1:
            raise RuntimeError('wire crypto library ABI mismatch')
        failure = lib.x4c_selftest()
        if failure:
            raise RuntimeError(f'wire crypto known answers failed (case {failure}); refusing the library')
        self.sizes = {kind: lib.x4c_sizeof(code) for kind, code in (('tx', 1), ('rx', 2), ('client', 3), ('key', 4))}

    def state(self, kind):
        return ctypes.create_string_buffer(self.sizes[kind])

    def key_pair(self, seed):
        secret, public = ctypes.create_string_buffer(64), ctypes.create_string_buffer(32)
        self.lib.x4c_ed25519_key_pair(secret, public, ctypes.create_string_buffer(bytes(seed), 32))
        return secret.raw, public.raw

    def slot_psk(self, master, sid, slot):
        out = ctypes.create_string_buffer(32)
        self.lib.x4c_slot_psk(out, bytes(master), bytes(sid), slot)
        return out.raw

    def relay_welcome(self, sid, master, relay_secret, clients, lookahead, hello, x_seed):
        """(welcome, slot, player public key, rx from player, tx to player) or None."""
        welcome, pk, slot = ctypes.create_string_buffer(WELCOME), ctypes.create_string_buffer(32), ctypes.c_uint(0)
        rx, tx = self.state('rx'), self.state('tx')
        ok = self.lib.x4c_relay_welcome(bytes(sid), bytes(master), bytes(relay_secret), clients, lookahead, bytes(hello), len(hello),
                                        ctypes.create_string_buffer(bytes(x_seed), 32), welcome, ctypes.byref(slot), pk, rx, tx)
        return (welcome.raw, slot.value, pk.raw, rx, tx) if ok else None

    def client_hello(self, sid, psk, relay_pk, slot, clients, lookahead, x_seed, sign_seed):
        state, hello = self.state('client'), ctypes.create_string_buffer(HELLO)
        if not self.lib.x4c_client_hello(state, bytes(sid), bytes(psk), bytes(relay_pk), slot, clients, lookahead,
                                         ctypes.create_string_buffer(bytes(x_seed), 32), ctypes.create_string_buffer(bytes(sign_seed), 32), hello):
            raise ValueError('client hello refused')
        return state, hello.raw

    def client_welcome(self, state, welcome):
        """(tx to relay, rx from relay) or None."""
        tx, rx = self.state('tx'), self.state('rx')
        return (tx, rx) if self.lib.x4c_client_welcome(state, bytes(welcome), len(welcome), tx, rx) else None

    def client_keys(self, state):
        secret, public = ctypes.create_string_buffer(64), ctypes.create_string_buffer(32)
        self.lib.x4c_client_keys(state, secret, public)
        return secret.raw, public.raw

    def tx(self, base, channel, sender, receiver, generation=0, epoch_shift=16, next_counter=0):
        state = self.state('tx')
        self.lib.x4c_tx_init(state, bytes(base), channel, sender, receiver, generation, epoch_shift, next_counter)
        return state

    def rx(self, base, channel, sender, receiver, generation=0, epoch_shift=16):
        state = self.state('rx')
        self.lib.x4c_rx_init(state, bytes(base), channel, sender, receiver, generation, epoch_shift)
        return state

    def rx_counts(self, state):
        out = (ctypes.c_uint64 * 3)()
        self.lib.x4c_rx_counts(state, out)
        return {'opened': out[0], 'forged': out[1], 'replayed': out[2]}

    def seal(self, tx, sid, plain):
        out = ctypes.create_string_buffer(len(plain) + OVERHEAD)
        size = self.lib.x4c_seal(tx, bytes(sid), bytes(plain), len(plain), out, len(out))
        if not size:
            raise ValueError('seal refused')
        return out.raw[:size]

    def open(self, rx, sid, data):
        """(1, plaintext) opened; (0, None) not authentic; (-1, None) replay or too old."""
        plain, size = ctypes.create_string_buffer(max(1, len(data))), ctypes.c_size_t(0)
        result = self.lib.x4c_open(rx, bytes(sid), bytes(data), len(data), plain, len(plain), ctypes.byref(size))
        return (1, plain.raw[:size.value]) if result == 1 else (result, None)

    def record_sign(self, secret, sid, slot, clients, lookahead, tick, blob):
        out = ctypes.create_string_buffer(SIG)
        if not self.lib.x4c_record_sign(out, bytes(secret), bytes(sid), slot, clients, lookahead, tick, bytes(blob), len(blob)):
            raise ValueError('record refused')
        return out.raw

    def record_verify(self, sig, public, sid, slot, clients, lookahead, tick, blob):
        return bool(self.lib.x4c_record_verify(bytes(sig), bytes(public), bytes(sid), slot, clients, lookahead, tick, bytes(blob), len(blob)))

    def records_hash(self, blobs, sigs):
        packed = b''.join(len(b).to_bytes(2, 'little') + b + s for b, s in zip(blobs, sigs))
        out = ctypes.create_string_buffer(32)
        self.lib.x4c_records_hash(out, packed, len(packed))
        return out.raw

    def permit_sign(self, relay_secret, sid, tick, clients, lookahead, records):
        out = ctypes.create_string_buffer(SIG)
        if not self.lib.x4c_permit_sign(out, bytes(relay_secret), bytes(sid), tick, clients, lookahead, bytes(records)):
            raise ValueError('permit refused')
        return out.raw

    def permit_verify(self, sig, relay_public, sid, tick, clients, lookahead, records):
        return bool(self.lib.x4c_permit_verify(bytes(sig), bytes(relay_public), bytes(sid), tick, clients, lookahead, bytes(records)))

    def roster_check(self, trailer, clients, slot, own_pk):
        return bool(self.lib.x4c_roster_check(bytes(trailer), len(trailer), clients, slot, bytes(own_pk)))

    def peer_keys(self, group, sid, clients, generation=1):
        keys = ctypes.create_string_buffer(self.sizes['key'] * clients)
        for s in range(1, clients + 1):
            self.lib.x4c_peer_key(ctypes.byref(keys, self.sizes['key'] * (s - 1)), bytes(group), bytes(sid), generation, s)
        return keys

    def peer_tx(self, group, sid, slot, generation=1):
        state = self.state('tx')
        self.lib.x4c_peer_tx(state, bytes(group), bytes(sid), generation, slot)
        return state

    def peer_seal(self, tx, sid, secret, clients, lookahead, tick, blob):
        out = ctypes.create_string_buffer(OVERHEAD + 14 + len(blob) + SIG)
        size = self.lib.x4c_peer_seal(tx, bytes(sid), bytes(secret), clients, lookahead, tick, bytes(blob), len(blob), out, len(out))
        if not size:
            raise ValueError('peer record refused')
        return out.raw[:size]

    def peer_open(self, keys, roster, sid, clients, lookahead, data):
        """(1, slot, tick, blob, sig); (0, ...) not authentic; (-2, ...) authentic under the group key, forged record."""
        slot, tick, size = ctypes.c_uint(0), ctypes.c_uint64(0), ctypes.c_size_t(0)
        blob, sig = ctypes.create_string_buffer(240), ctypes.create_string_buffer(SIG)
        result = self.lib.x4c_peer_open(keys, b''.join(roster), bytes(sid), clients, lookahead, bytes(data), len(data),
                                        ctypes.byref(slot), ctypes.byref(tick), blob, ctypes.byref(size), sig)
        if result != 1:
            return result, None, None, None, None
        return 1, slot.value, tick.value, blob.raw[:size.value], sig.raw


def load_key(path):
    data = json.loads(Path(path).read_text())
    if data.get('format') != KEY_FORMAT:
        raise ValueError(f'not a {KEY_FORMAT} key file')
    key = {name: bytes.fromhex(data[name]) for name in ('relay_seed', 'master')}
    if any(len(value) != 32 or not any(value) for value in key.values()):
        raise ValueError('relay key file: relay_seed and master must be 32 non-zero bytes')
    return key


def new_key():
    return {'format': KEY_FORMAT, 'relay_seed': os.urandom(32).hex(), 'master': os.urandom(32).hex()}


def credential(lib, key, token, slot):
    """The player's X4LAN_CONFIG.native_gate_wire_crypto: x4c1:<relay public key>:<slot psk>."""
    sid = bytes.fromhex(token)
    if len(sid) != 16:
        raise ValueError('token must be 32 hex characters')
    _, relay_pk = lib.key_pair(key['relay_seed'])
    return f'x4c1:{relay_pk.hex()}:{lib.slot_psk(key["master"], sid, slot).hex()}'


def canonical(value):
    return value.isdigit() and value.isascii() and str(int(value)) == value


class Player:
    __slots__ = ('hello', 'welcome', 'address', 'public', 'rx', 'tx')


class RelayCrypto:
    """Relay side: answers handshakes, opens reports (checking each input record's signature),
    seals every reply and adds the record signatures and the relay's permit signature to permits.

    inbound(data, address) -> (ASCII datagram for the barrier or None, [(address, bytes)] to send now)
    outbound(address, packet) -> sealed bytes (or None: no handshake with that address)
    A report whose signature fails sets .failure (the session stops: fail closed)."""

    def __init__(self, lib, key, token, clients, lookahead, random=os.urandom):
        self.lib, self.token, self.clients, self.lookahead, self.random = lib, token, clients, lookahead, random
        self.sid = bytes.fromhex(token)
        self.relay_secret, self.relay_public = lib.key_pair(key['relay_seed'])
        self.master = key['master']
        self.group = random(32)
        self.generation = 1
        self.players, self.by_address = {}, {}
        self.sigs, self.permit_sigs = {}, {}
        self.failure = None
        self.counts = collections.Counter()
        self.seconds = collections.Counter()

    def complete(self):
        return len(self.players) == self.clients

    def roster(self):
        return [self.players[s].public for s in range(1, self.clients + 1)]

    def inbound(self, data, address):
        started = time.perf_counter()
        try:
            if data[:4] == b'X4H1':
                return None, self._hello(data, address)
            if data[:4] != b'X4C1' or len(data) < OVERHEAD:
                self.counts['dropped_plain'] += 1
                return None, []
            player = self.players.get(data[5])
            if data[4] != 1 or player is None or player.address != address:
                self.counts['dropped_unknown'] += 1
                return None, []
            slot = data[5]
            result, plain = self.lib.open(player.rx, self.sid, data)
            if result != 1:
                self.counts['dropped_replay' if result == -1 else 'dropped_forged'] += 1
                return None, []
            ascii_part, separator, trailer = plain.partition(b'\0')
            if ascii_part.startswith(b'NGKEYREQ|'):
                return None, self._keys(slot, ascii_part, separator, address)
            if ascii_part.startswith((b'NGSTATE|3|', b'NGSTATE|4|')):
                return self._report(slot, ascii_part, separator, trailer), []
            self.counts['dropped_other'] += 1
            return None, []
        finally:
            self.seconds['inbound'] += time.perf_counter() - started

    def _hello(self, data, address):
        if len(data) != HELLO:
            self.counts['hello_refused'] += 1
            return []
        player = self.players.get(data[5])
        if player is not None:
            # A repeated HELLO (its WELCOME was lost) gets the same WELCOME; anything else for a taken slot is refused.
            if player.hello == data and player.address == address:
                self.counts['hello_repeated'] += 1
                return [(address, player.welcome)]
            self.counts['hello_refused'] += 1
            return []
        if address in self.by_address:
            self.counts['hello_refused'] += 1
            return []
        answer = self.lib.relay_welcome(self.sid, self.master, self.relay_secret, self.clients, self.lookahead, data, self.random(32))
        if answer is None or any(p.public == answer[2] for p in self.players.values()):
            self.counts['hello_refused'] += 1
            return []
        player = Player()
        player.welcome, slot, player.public, player.rx, player.tx = answer
        player.hello, player.address = bytes(data), address
        self.players[slot], self.by_address[address] = player, slot
        self.counts['handshakes'] += 1
        return [(address, player.welcome)]

    def _keys(self, slot, ascii_part, separator, address):
        if separator or ascii_part != f'NGKEYREQ|1|{self.token}|{slot}'.encode() or not self.complete():
            self.counts['keyreq_unanswered'] += 1
            return []
        self.counts['keys_sent'] += 1
        plain = f'NGKEYS|1|{self.token}|{slot}|{self.generation}'.encode() + b'\0' + self.group + b''.join(self.roster())
        return [(address, self.lib.seal(self.players[slot].tx, self.sid, plain))]

    def _report(self, slot, ascii_part, separator, trailer):
        """Check a report's input record signature; the ASCII report goes on to the barrier."""
        try:
            f = ascii_part.decode('ascii').split('|')
        except UnicodeError:
            self.counts['dropped_other'] += 1
            return None
        version = int(f[1])
        if (len(f) != 7 + version or f[2] != self.token or not canonical(f[3]) or int(f[3]) != slot or not canonical(f[4])
                or len(f[-1]) > 480 or len(f[-1]) % 2 or any(c not in '0123456789abcdef' for c in f[-1])):
            # Malformed, or a report naming another slot inside this player's channel: never reaches the barrier.
            self.counts['dropped_report'] += 1
            return None
        tick, blob = int(f[4]), bytes.fromhex(f[-1])
        if not separator or len(trailer) != SIG or not self.lib.record_verify(trailer, self.players[slot].public, self.sid, slot,
                                                                            self.clients, self.lookahead, tick, blob):
            self.failure = self.failure or f'wire_crypto_signature_slot_{slot}_tick_{tick}'
            self.counts['bad_signature'] += 1
        else:
            self.sigs.setdefault((tick, slot), trailer)
            self.counts['reports'] += 1
        return ascii_part

    def outbound(self, address, packet):
        started = time.perf_counter()
        try:
            slot = self.by_address.get(address)
            if slot is None:
                return None
            plain = packet
            if packet.startswith((b'NGGO|3|', b'NGGO|4|')):
                trailer = self._permit_trailer(packet)
                if trailer is None:
                    return None
                plain = packet + b'\0' + trailer
            self.counts['sealed'] += 1
            return self.lib.seal(self.players[slot].tx, self.sid, plain)
        finally:
            self.seconds['outbound'] += time.perf_counter() - started

    def _permit_trailer(self, packet):
        f = packet.split(b'|')
        tick = int(f[4])
        blobs = [bytes.fromhex(b.decode()) for b in f[6 if f[1] == b'4' else 5:]]
        source = tick - 1 - self.lookahead if tick > self.lookahead else None
        try:
            sigs = [ZERO_SIG if source is None else self.sigs[(source, s)] for s in range(1, self.clients + 1)]
        except KeyError:
            self.failure = self.failure or f'wire_crypto_missing_signature_tick_{source}'
            return None
        records = self.lib.records_hash(blobs, sigs)
        cached = self.permit_sigs.get(tick)
        if cached is None or cached[0] != records:
            cached = self.permit_sigs[tick] = (records, self.lib.permit_sign(self.relay_secret, self.sid, tick, self.clients,
                                                                              self.lookahead, records))
            self.counts['permits_signed'] += 1
        return b''.join(sigs) + cached[1]

    def journal_sigs(self, tick):
        return {s: self.sigs[(tick, s)].hex() for s in range(1, self.clients + 1) if (tick, s) in self.sigs}

    def prune(self, released):
        """Forget signatures no permit can still need (retransmitted reports reach back at most k+1 ticks)."""
        floor = released - 8 * (self.lookahead + 1) - 16
        if floor > 0 and len(self.sigs) > 4096:
            self.sigs = {key: value for key, value in self.sigs.items() if key[0] >= floor}
            self.permit_sigs = {tick: value for tick, value in self.permit_sigs.items() if tick >= floor}

    def summary(self):
        roster = b''.join(self.players[s].public for s in sorted(self.players))
        counts = dict(self.counts)
        counts['replayed'] = sum(self.lib.rx_counts(p.rx)['replayed'] for p in self.players.values())
        return {'enabled': True, 'relay_public_key': self.relay_public.hex(), 'generation': self.generation,
                'players': {s: self.players[s].public.hex() for s in sorted(self.players)},
                'roster_fingerprint': hashlib.blake2b(roster, digest_size=16).hexdigest(),
                'counts': counts, 'seconds': {k: round(v, 6) for k, v in self.seconds.items()}, 'failure': self.failure}


class Client:
    """A synthetic gate for tests and the benchmark: the same handshake and records as the native gate."""

    def __init__(self, lib, cred, token, slot, clients, lookahead, random=os.urandom):
        parts = cred.split(':')
        self.lib, self.token, self.slot, self.clients, self.lookahead = lib, token, slot, clients, lookahead
        self.sid = bytes.fromhex(token)
        self.relay_public, psk = bytes.fromhex(parts[1]), bytes.fromhex(parts[2])
        self.state, self.hello = lib.client_hello(self.sid, psk, self.relay_public, slot, clients, lookahead, random(32), random(32))
        self.secret, self.public = lib.client_keys(self.state)
        self.tx = self.rx = None
        self.group, self.roster = None, None

    def welcome(self, data):
        pair = self.lib.client_welcome(self.state, data)
        if pair:
            self.tx, self.rx = pair
        return pair is not None

    def keyreq(self):
        return self.lib.seal(self.tx, self.sid, f'NGKEYREQ|1|{self.token}|{self.slot}'.encode())

    def open(self, data):
        result, plain = self.lib.open(self.rx, self.sid, data)
        if result != 1:
            return None
        ascii_part, _, trailer = plain.partition(b'\0')
        if ascii_part.startswith(b'NGKEYS|'):
            if ascii_part != f'NGKEYS|1|{self.token}|{self.slot}|1'.encode() or not self.lib.roster_check(trailer, self.clients, self.slot, self.public):
                raise AssertionError('invalid NGKEYS')
            self.group, self.roster = trailer[:32], [trailer[32 * s:32 * s + 32] for s in range(1, self.clients + 1)]
        return ascii_part, trailer

    def report(self, tick, blob_hex, digest='0' * 64, game=0.0, real=0.0):
        game_hex, real_hex = (struct.pack('>d', value).hex() for value in (game, real))
        if self.lookahead:
            text = f'NGSTATE|4|{self.token}|{self.slot}|{tick}|{game_hex}|{real_hex}|{digest}|{self.clients}|{self.lookahead}|{blob_hex}'
        else:
            text = f'NGSTATE|3|{self.token}|{self.slot}|{tick}|{game_hex}|{real_hex}|{digest}|{self.clients}|{blob_hex}'
        sig = self.lib.record_sign(self.secret, self.sid, self.slot, self.clients, self.lookahead, tick, bytes.fromhex(blob_hex))
        return self.lib.seal(self.tx, self.sid, text.encode() + b'\0' + sig)

    def verify_permit(self, ascii_part, trailer):
        """What the native gate checks in an NGGO: every other slot's record and the relay's signature."""
        f = ascii_part.split(b'|')
        tick = int(f[4])
        blobs = [bytes.fromhex(b.decode()) for b in f[6 if self.lookahead else 5:]]
        sigs = [trailer[SIG * i:SIG * i + SIG] for i in range(self.clients)]
        if len(trailer) != SIG * (self.clients + 1) or len(blobs) != self.clients:
            return False
        source = tick - 1 - self.lookahead if tick > self.lookahead else None
        for s in range(1, self.clients + 1):
            if source is None:
                if blobs[s - 1] or sigs[s - 1] != ZERO_SIG:
                    return False
            elif s != self.slot and not self.lib.record_verify(sigs[s - 1], self.roster[s - 1], self.sid, s, self.clients,
                                                               self.lookahead, source, blobs[s - 1]):
                return False
        return self.lib.permit_verify(trailer[SIG * self.clients:], self.relay_public, self.sid, tick, self.clients, self.lookahead,
                                      self.lib.records_hash(blobs, sigs))


def bench(lib, clients, lookahead, ticks, blob_bytes):
    """Relay and client cost per tick with every slot proposing blob_bytes (synthetic, in process)."""
    sys.path.insert(0, str(ROOT / 'tools/dev'))
    import native_gate_relay
    token = os.urandom(16).hex()
    key = {'relay_seed': os.urandom(32), 'master': os.urandom(32)}
    wire = RelayCrypto(lib, key, token, clients, lookahead)
    barrier = native_gate_relay.Barrier(token, ticks=ticks + lookahead + 2, hold_tick=-1, clients=clients, lookahead=lookahead)
    players = [Client(lib, credential(lib, key, token, s), token, s, clients, lookahead) for s in range(1, clients + 1)]
    addresses = [('127.0.0.1', 40000 + s) for s in range(1, clients + 1)]
    for client, address in zip(players, addresses):
        _, replies = wire.inbound(client.hello, address)
        assert client.welcome(replies[0][1])
    for client, address in zip(players, addresses):
        _, replies = wire.inbound(client.keyreq(), address)
        client.open(replies[0][1])
    relay_seconds = client_seconds = 0.0
    permits = 0
    for tick in range(ticks):
        for client, address in zip(players, addresses):
            started = time.perf_counter()
            datagram = client.report(tick, os.urandom(blob_bytes).hex(), game=tick / 60, real=tick / 60)
            client_seconds += time.perf_counter() - started
            started = time.perf_counter()
            plain, _ = wire.inbound(datagram, address)
            replies = barrier.receive(plain, address, 0.0)
            sealed = [(dest, wire.outbound(dest, packet)) for dest, packet in replies]
            relay_seconds += time.perf_counter() - started
            for dest, data in sealed:
                client = players[addresses.index(dest)]
                started = time.perf_counter()
                ascii_part, trailer = client.open(data)
                if ascii_part.startswith(b'NGGO'):
                    assert client.verify_permit(ascii_part, trailer)
                    permits += 1
                client_seconds += time.perf_counter() - started
    assert not barrier.failed and not wire.failure, (barrier.failed, wire.failure)
    return {'clients': clients, 'lookahead': lookahead, 'ticks': ticks, 'blob_bytes': blob_bytes,
            'relay_us_per_tick': round(relay_seconds / ticks * 1e6, 1),
            'client_us_per_tick': round(client_seconds / ticks / clients * 1e6, 1), 'permits': permits}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--lib', type=Path, help='x4mp_wire_crypto library (default: the build output)')
    sub = parser.add_subparsers(dest='command', required=True)
    keygen = sub.add_parser('keygen')
    keygen.add_argument('--out', type=Path, required=True)
    cred = sub.add_parser('credential')
    cred.add_argument('--key', type=Path, required=True)
    cred.add_argument('--token', required=True)
    cred.add_argument('--slot', type=int, required=True)
    sub.add_parser('selftest')
    b = sub.add_parser('bench')
    b.add_argument('--clients', type=int, default=2)
    b.add_argument('--lookahead', type=int, default=1)
    b.add_argument('--ticks', type=int, default=300)
    b.add_argument('--blob-bytes', type=int, default=28)
    args = parser.parse_args()
    if args.command == 'keygen':
        if args.out.exists():
            parser.error(f'{args.out} exists; a relay key is never overwritten')
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(new_key(), indent=2) + '\n')
        print(args.out)
        return
    lib = Library(args.lib)
    if args.command == 'credential':
        if not 1 <= args.slot <= 8:
            parser.error('--slot must be 1..8')
        print(credential(lib, load_key(args.key), args.token, args.slot))
    elif args.command == 'selftest':
        print(f'WIRE CRYPTO SELFTEST PASS {lib.path.name}')
    else:
        print(json.dumps(bench(lib, args.clients, args.lookahead, args.ticks, args.blob_bytes)))


if __name__ == '__main__':
    main()
