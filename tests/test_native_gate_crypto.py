"""Wire crypto (docs/LOCKSTEP-NSLOT-WIRE.txt, WIRE CRYPTO): the C library through ctypes, the
golden byte layouts recomputed independently (Python cryptography + hashlib), the relay side with
synthetic clients (in process and over real UDP) and every fail-closed case. No native game.

Needs the library from tools/dev/build_native_gate.py (or X4MP_WIRE_CRYPTO_LIB)."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
import native_gate_crypto as nc  # noqa: E402
spec = importlib.util.spec_from_file_location('native_gate_relay_crypto', ROOT/'tools/dev/native_gate_relay.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)

try:
    LIB = nc.Library()
except FileNotFoundError as missing:
    LIB, MISSING = None, str(missing)
else:
    MISSING = ''
try:
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
    from cryptography.hazmat.primitives.asymmetric import ed25519, x25519
    from cryptography.hazmat.primitives import serialization
    HAVE_REFERENCE = True
except ImportError:
    HAVE_REFERENCE = False

TOKEN = 'a0a1a2a3a4a5a6a7a8a9aaabacadaeaf'
SID = bytes.fromhex(TOKEN)
# Golden values of our layouts on fixed inputs; the same strings are pinned in
# src/native/engine/test_lockstep_wire_crypto.c.
GOLDEN = {
    'seal': '58344331010200000500000000000000483ec75c587d76b2f4de4678099fa13a6c55cc07a621a492',
    'subkey0': 'a93111c7fd91f1275f5e269aefed5a1aa351aa3e58c90748ab727a5dafce9a61',
    'record': '58344d5020696e707574207631000000a0a1a2a3a4a5a6a7a8a9aaabacadaeaf02030100d2040000000000000300010203',
    'record_pk': 'd04ab232742bb4ab3a1368bd4615e4e6d0224ab71a016baf8520a332c9778737',
    'record_sig': 'd4a91965b02bce3a190560c12cae13467a970e828578827da4bab8c9a6d089e7f9fec7d44858f3faa2cd50e367b149b64872190274fb1ac9211d34d6385db80d',
    'records': 'cbb951750f032a73878482cf5f486fcda2c59f13ced5410a71d43346d2b9f93a',
    'permit_sig': '3864a753a250be48d8ab8a92529defaf0c3d9e9f4f3f6b37d847fab0ef17a05b7d565af93169f79f9214a8ff8421e3613428024edf4a56a40f0f3d2e646df90d',
    'psk2': '7184b68463bd0b83a5b386f47bffcc183e170b47ddb7b6c3d794a5d1467fc4a9',
    'hello': '5834483101020301ff2ee45601ec1b67310c7790404585ae697331eee1c1f8cf2419731c1fff3e6bc6822637c7d310ec57627be00ba259d253749f4aaf644470cffbe53a35f7324222d281a13ad6125b8ee756153faef3f6',
    'welcome': '5834483102020301219e4d800da968d2a5fcb009c784f4746c7138edb9ee4844b739e830b05cf424970034402ad1179b6dbbb739989bd75a902ac2eb6dff3e11fb5ab1070e99e1d757fae9ff7093a32f933dfeff879de2c8a09cdfb2848b3adef298424b3f43de0a',
    'c2r': '58344331010200000000000000000000376fccc3ef0251d2135d2f012834b8a6f754cbfd6d536fbc',
    'r2c': '583443310200020000000000000000004006c742b67d0fe88329a654dc3690ee5861150dfb6d',
}


def domain(name):
    return name.encode().ljust(16, b'\0')


def reference_layouts():
    """Every golden value from the written layout, with an independent implementation of each primitive."""
    raw = lambda key: key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    subkey = lambda base, h, epoch: hashlib.blake2b(domain('X4MP subkey v1')+h[4:8]+struct.pack('<Q', epoch), key=base, digest_size=32).digest()

    def seal(base, channel, sender, receiver, counter, plain):
        header = b'X4C1'+bytes([channel, sender, receiver, 0])+struct.pack('<Q', counter)
        return header+ChaCha20Poly1305(subkey(base, header, counter >> 16)).encrypt(header[4:], plain, SID+header)
    out = {'seal': seal(bytes(range(32)), 1, 2, 0, 5, b'lockstep').hex(),
           'subkey0': subkey(bytes(range(32)), b'X4C1\x01\x02\x00\x00', 0).hex()}
    record = domain('X4MP input v1')+SID+bytes([2, 3, 1, 0])+struct.pack('<QH', 1234, 3)+b'\1\2\3'
    player = ed25519.Ed25519PrivateKey.from_private_bytes(b'\x11'*32)
    sig = player.sign(record)
    out.update(record=record.hex(), record_pk=raw(player.public_key()).hex(), record_sig=sig.hex())
    zero = bytes(64)
    records = hashlib.blake2b(domain('X4MP records v1')+struct.pack('<H', 0)+zero+struct.pack('<H', 3)+b'\1\2\3'+sig
                              + struct.pack('<H', 1)+b'\1'+zero, digest_size=32).digest()
    out['records'] = records.hex()
    relay_key = ed25519.Ed25519PrivateKey.from_private_bytes(b'\x22'*32)
    permit = domain('X4MP permit v1')+SID+struct.pack('<Q', 1236)+bytes([3, 1, 0, 0])+struct.pack('<Q', 1236-1-1)+records
    out['permit_sig'] = relay_key.sign(permit).hex()
    psk = hashlib.blake2b(domain('X4MP slot psk v1')+SID+bytes([2]), key=b'\x33'*32, digest_size=32).digest()
    out['psk2'] = psk.hex()
    client = x25519.X25519PrivateKey.from_private_bytes(b'\x44'*32)
    hello = b'X4H1'+bytes([1, 2, 3, 1])+raw(client.public_key())+raw(ed25519.Ed25519PrivateKey.from_private_bytes(b'\x55'*32).public_key())
    hello += hashlib.blake2b(domain('X4MP hello v1')+SID+hello, key=psk, digest_size=16).digest()
    ephemeral = x25519.X25519PrivateKey.from_private_bytes(b'\x66'*32)
    welcome = b'X4H1'+bytes([2, 2, 3, 1])+raw(ephemeral.public_key())
    welcome += relay_key.sign(domain('X4MP welcome v1')+SID+hello+welcome)
    keys = hashlib.blake2b(domain('X4MP keys v1')+SID+hello+welcome+client.exchange(ephemeral.public_key()), key=psk, digest_size=64).digest()
    out.update(hello=hello.hex(), welcome=welcome.hex(), c2r=seal(keys[:32], 1, 2, 0, 0, b'NGKEYREQ').hex(),
               r2c=seal(keys[32:], 2, 0, 2, 0, b'NGKEYS').hex())
    return out


def game_hex(tick):
    return struct.pack('>d', 20+tick/60).hex()


class Session:
    """A relay (barrier + RelayCrypto) and N synthetic clients, in process."""

    def __init__(self, clients=3, lookahead=0, ticks=40):
        self.key = {'relay_seed': b'\x22'*32, 'master': b'\x33'*32}
        self.wire = nc.RelayCrypto(LIB, self.key, TOKEN, clients, lookahead)
        self.barrier = relay.Barrier(TOKEN, ticks=ticks, hold_tick=-1, clients=clients, lookahead=lookahead)
        self.addresses = [('127.0.0.1', 41000+s) for s in range(1, clients+1)]
        self.clients = [nc.Client(LIB, nc.credential(LIB, self.key, TOKEN, s), TOKEN, s, clients, lookahead) for s in range(1, clients+1)]
        self.inboxes = {address: [] for address in self.addresses}

    def deliver(self, data, address):
        plain, direct = self.wire.inbound(data, address)
        if self.wire.failure and not self.barrier.failed:
            self.barrier.failed = self.wire.failure
        replies = self.barrier.receive(plain, address, 0.0) if plain is not None else []
        out = [(d, self.wire.outbound(d, p)) for d, p in replies]+direct
        for dest, packet in out:
            if packet is not None:
                self.inboxes[dest].append(packet)
        return out

    def handshake(self):
        for client, address in zip(self.clients, self.addresses):
            self.deliver(client.hello, address)
            assert client.welcome(self.inboxes[address].pop())
        for client, address in zip(self.clients, self.addresses):
            self.deliver(client.keyreq(), address)
            client.open(self.inboxes[address].pop())

    def report(self, slot, tick, blob=''):
        client = self.clients[slot-1]
        return self.deliver(client.report(tick, blob, game=20+tick/60, real=20+tick/60), self.addresses[slot-1])


@unittest.skipIf(LIB is None, MISSING)
class LibraryTests(unittest.TestCase):
    def test_known_answers_and_golden_layouts(self):
        self.assertEqual(LIB.lib.x4c_selftest(), 0)
        tx = LIB.tx(bytes(range(32)), 1, 2, 0, next_counter=5)
        self.assertEqual(LIB.seal(tx, SID, b'lockstep').hex(), GOLDEN['seal'])
        secret, public = LIB.key_pair(b'\x11'*32)
        self.assertEqual(public.hex(), GOLDEN['record_pk'])
        sig = LIB.record_sign(secret, SID, 2, 3, 1, 1234, b'\1\2\3')
        self.assertEqual(sig.hex(), GOLDEN['record_sig'])
        records = LIB.records_hash([b'', b'\1\2\3', b'\1'], [bytes(64), sig, bytes(64)])
        self.assertEqual(records.hex(), GOLDEN['records'])
        relay_secret, relay_public = LIB.key_pair(b'\x22'*32)
        self.assertEqual(LIB.permit_sign(relay_secret, SID, 1236, 3, 1, records).hex(), GOLDEN['permit_sig'])
        psk = LIB.slot_psk(b'\x33'*32, SID, 2)
        self.assertEqual(psk.hex(), GOLDEN['psk2'])
        state, hello = LIB.client_hello(SID, psk, relay_public, 2, 3, 1, b'\x44'*32, b'\x55'*32)
        self.assertEqual(hello.hex(), GOLDEN['hello'])
        welcome, slot, _, relay_rx, relay_tx = LIB.relay_welcome(SID, b'\x33'*32, relay_secret, 3, 1, hello, b'\x66'*32)
        self.assertEqual((welcome.hex(), slot), (GOLDEN['welcome'], 2))
        to_relay, from_relay = LIB.client_welcome(state, welcome)
        self.assertEqual(LIB.seal(to_relay, SID, b'NGKEYREQ').hex(), GOLDEN['c2r'])
        self.assertEqual(LIB.seal(relay_tx, SID, b'NGKEYS').hex(), GOLDEN['r2c'])

    @unittest.skipUnless(HAVE_REFERENCE, 'the cryptography package is not installed')
    def test_independent_reference_recomputes_every_golden_value(self):
        self.assertEqual(reference_layouts(), GOLDEN)

    @unittest.skipUnless(HAVE_REFERENCE, 'the cryptography package is not installed')
    def test_random_round_trips_against_the_reference(self):
        # Random keys, counters, sizes: the library's datagrams open with the reference AEAD and
        # its record signatures verify with the reference Ed25519, and the other way round.
        for size in (0, 1, 63, 64, 245, 714, 4489):
            base, counter, plain = os.urandom(32), int.from_bytes(os.urandom(6), 'little'), os.urandom(size)
            datagram = LIB.seal(LIB.tx(base, 2, 0, 5, next_counter=counter), SID, plain)
            header = datagram[:16]
            subkey = hashlib.blake2b(domain('X4MP subkey v1')+header[4:8]+struct.pack('<Q', counter >> 16), key=base, digest_size=32).digest()
            self.assertEqual(ChaCha20Poly1305(subkey).decrypt(header[4:], datagram[16:], SID+header), plain)
            forged = header+ChaCha20Poly1305(subkey).encrypt(header[4:], plain[::-1], SID+header)
            self.assertEqual(LIB.open(LIB.rx(base, 2, 0, 5), SID, forged), (1, plain[::-1]))
        seed, blob = os.urandom(32), os.urandom(240)
        secret, public = LIB.key_pair(seed)
        sig = LIB.record_sign(secret, SID, 4, 8, 3, 99, blob)
        record = domain('X4MP input v1')+SID+bytes([4, 8, 3, 0])+struct.pack('<QH', 99, 240)+blob
        ed25519.Ed25519PrivateKey.from_private_bytes(seed).public_key().verify(sig, record)
        self.assertTrue(LIB.record_verify(ed25519.Ed25519PrivateKey.from_private_bytes(seed).sign(record), public, SID, 4, 8, 3, 99, blob))

    def test_tamper_replay_and_window(self):
        base = os.urandom(32)
        tx, rx = LIB.tx(base, 1, 3, 0), LIB.rx(base, 1, 3, 0)
        datagram = LIB.seal(tx, SID, b'NGSTATE|4|...')
        for i in range(len(datagram)):
            tampered = bytearray(datagram);tampered[i] ^= 0x40
            self.assertNotEqual(LIB.open(rx, SID, bytes(tampered))[0], 1)
        self.assertEqual(LIB.open(rx, SID, datagram), (1, b'NGSTATE|4|...'))
        self.assertEqual(LIB.open(rx, SID, datagram), (-1, None))
        self.assertEqual(LIB.open(LIB.rx(base, 1, 3, 0), bytes(16), datagram)[0], 0)       # another session id
        self.assertEqual(LIB.open(LIB.rx(base, 2, 0, 3), SID, datagram)[0], 0)             # another direction
        late = [LIB.seal(tx, SID, b'%d' % i) for i in range(1100)]
        self.assertEqual(LIB.open(rx, SID, late[-1])[0], 1)
        self.assertEqual(LIB.open(rx, SID, late[1050])[0], 1)   # inside the 1,024 window, out of order
        self.assertEqual(LIB.open(rx, SID, late[10])[0], -1)    # older than the window
        self.assertEqual(LIB.rx_counts(rx), {'opened': 3, 'forged': len(datagram), 'replayed': 2})

    def test_peer_records(self):
        group, clients = os.urandom(32), 3
        keys = [LIB.key_pair(os.urandom(32)) for _ in range(clients)]
        roster = [public for _, public in keys]
        receive = LIB.peer_keys(group, SID, clients)
        datagram = LIB.peer_seal(LIB.peer_tx(group, SID, 2), SID, keys[1][0], clients, 1, 77, b'\x01pilot')
        self.assertEqual(LIB.peer_open(receive, roster, SID, clients, 1, datagram)[:4], (1, 2, 77, b'\x01pilot'))
        outsider = LIB.peer_seal(LIB.peer_tx(os.urandom(32), SID, 2), SID, keys[1][0], clients, 1, 78, b'x')
        self.assertEqual(LIB.peer_open(receive, roster, SID, clients, 1, outsider)[0], 0)
        # Player 1 holds the group key and seals a record as sender 2, signed with its own key.
        forged = LIB.peer_seal(LIB.peer_tx(group, SID, 2), SID, keys[0][0], clients, 1, 79, b'x')
        self.assertEqual(LIB.peer_open(receive, roster, SID, clients, 1, forged)[0], -2)


@unittest.skipIf(LIB is None, MISSING)
class RelayCryptoTests(unittest.TestCase):
    def run_session(self, clients, lookahead, ticks=12):
        session = Session(clients, lookahead, ticks=ticks)
        session.handshake()
        seen = {address: {} for address in session.addresses}
        for tick in range(ticks+1):
            for slot in range(1, clients+1):
                blob = os.urandom((tick*slot) % 30).hex()
                session.report(slot, tick, blob)
            for client, address in zip(session.clients, session.addresses):
                for data in session.inboxes[address]:
                    ascii_part, trailer = client.open(data)
                    if ascii_part.startswith(b'NGGO'):
                        self.assertTrue(client.verify_permit(ascii_part, trailer), ascii_part)
                        seen[address][int(ascii_part.split(b'|')[4])] = ascii_part.split(b'|')[6 if lookahead else 5:]
                session.inboxes[address].clear()
        self.assertFalse(session.barrier.failed or session.wire.failure)
        self.assertTrue(session.barrier.done)
        # Every client admitted the same blobs at every tick, all signed.
        first = seen[session.addresses[0]]
        self.assertEqual(sorted(first), list(range(1, ticks+1)))
        for address in session.addresses[1:]:
            self.assertEqual(seen[address], first)
        self.assertEqual(set(session.wire.journal_sigs(3)), set(range(1, clients+1)))
        return session

    def test_sessions_protocol_3_and_4(self):
        self.run_session(3, 0)
        session = self.run_session(3, 2)
        self.assertEqual(session.wire.summary()['counts']['handshakes'], 3)
        self.run_session(8, 1, ticks=6)

    def test_unsealed_forged_and_replayed_reports_are_dropped(self):
        session = Session(2, 1)
        session.handshake()
        plain = f'NGSTATE|4|{TOKEN}|1|0|{game_hex(0)}|{game_hex(0)}|{"b"*64}|2|1|'.encode()
        self.assertEqual(session.deliver(plain, session.addresses[0]), [])
        datagram = session.clients[0].report(0, '0a', game=20, real=20)
        tampered = bytearray(datagram);tampered[40] ^= 1
        self.assertEqual(session.deliver(bytes(tampered), session.addresses[0]), [])
        self.assertEqual(session.deliver(datagram, session.addresses[1]), [])     # from the other player's address
        session.deliver(datagram, session.addresses[0])
        self.assertEqual(session.deliver(datagram, session.addresses[0]), [])     # replay
        counts = session.wire.summary()['counts']
        self.assertEqual((counts['dropped_plain'], counts['dropped_forged'], counts['dropped_unknown'], counts['replayed']), (1, 1, 1, 1))
        self.assertEqual(set(session.barrier.states[0]), {1})
        self.assertFalse(session.barrier.failed)

    def test_wrong_slot_signature_fails_closed(self):
        session = Session(2, 0)
        session.handshake()
        session.report(1, 0, '01')
        # Player 2's channel carries a report whose record is signed with player 1's key.
        impostor = session.clients[1]
        text = f'NGSTATE|3|{TOKEN}|2|0|{game_hex(0)}|{game_hex(0)}|{"b"*64}|2|02'.encode()
        sig = LIB.record_sign(session.clients[0].secret, SID, 2, 2, 0, 0, b'\x02')
        replies = session.deliver(LIB.seal(impostor.tx, SID, text+b'\0'+sig), session.addresses[1])
        self.assertEqual(session.barrier.failed, 'wire_crypto_signature_slot_2_tick_0')
        stop = impostor.open(replies[0][1])[0]
        self.assertEqual(stop, f'NGSTOP|3|{TOKEN}|2|0'.encode())
        # A report without its signature fails the same way.
        session = Session(2, 0)
        session.handshake()
        session.deliver(LIB.seal(session.clients[0].tx, SID, text.replace(b'|2|0|', b'|1|0|', 1)), session.addresses[0])
        self.assertTrue(session.barrier.failed.startswith('wire_crypto_signature_slot_1'))

    def test_report_naming_another_slot_is_dropped(self):
        session = Session(2, 0)
        session.handshake()
        text = f'NGSTATE|3|{TOKEN}|2|0|{game_hex(0)}|{game_hex(0)}|{"b"*64}|2|'.encode()
        sig = LIB.record_sign(session.clients[0].secret, SID, 2, 2, 0, 0, b'')
        self.assertEqual(session.deliver(LIB.seal(session.clients[0].tx, SID, text+b'\0'+sig), session.addresses[0]), [])
        self.assertEqual(session.wire.counts['dropped_report'], 1)
        self.assertEqual(session.barrier.states, {})

    def test_key_mismatches_fail_closed(self):
        session = Session(2, 1)
        key = session.key
        # Slot 1's credential used for slot 2: the relay refuses the HELLO (no WELCOME, no keys).
        thief = nc.Client(LIB, nc.credential(LIB, key, TOKEN, 1), TOKEN, 2, 2, 1)
        self.assertEqual(session.deliver(thief.hello, session.addresses[1]), [])
        # Another window or player count than the relay's: refused at the HELLO.
        for clients, lookahead in ((2, 2), (3, 1)):
            other = nc.Client(LIB, nc.credential(LIB, key, TOKEN, 2), TOKEN, 2, clients, lookahead)
            self.assertEqual(session.deliver(other.hello, session.addresses[1]), [])
        # Another session's token.
        other = nc.Client(LIB, nc.credential(LIB, key, 'b'*32, 2), 'b'*32, 2, 2, 1)
        self.assertEqual(session.deliver(other.hello, session.addresses[1]), [])
        self.assertEqual(session.wire.counts['hello_refused'], 4)
        # A player pinned to another relay key ignores this relay's WELCOME.
        stranger = nc.Client(LIB, nc.credential(LIB, {'relay_seed': b'\x99'*32, 'master': key['master']}, TOKEN, 2), TOKEN, 2, 2, 1)
        stranger.relay_public = LIB.key_pair(b'\x99'*32)[1]
        welcome = session.deliver(stranger.hello, session.addresses[1])
        self.assertEqual(len(welcome), 1)
        self.assertFalse(stranger.welcome(welcome[0][1]))

    def test_hello_repeat_takeover_and_keys(self):
        session = Session(2, 0)
        first = session.clients[0]
        welcome = session.deliver(first.hello, session.addresses[0])[0][1]
        self.assertEqual(session.deliver(first.hello, session.addresses[0])[0][1], welcome)     # repeated: same answer
        self.assertEqual(session.deliver(first.hello, ('127.0.0.1', 1)), [])                     # another address
        rival = nc.Client(LIB, nc.credential(LIB, session.key, TOKEN, 1), TOKEN, 1, 2, 0)
        self.assertEqual(session.deliver(rival.hello, session.addresses[0]), [])                 # slot taken
        self.assertTrue(first.welcome(welcome))
        # No keys before every player finished its handshake.
        self.assertEqual(session.deliver(first.keyreq(), session.addresses[0]), [])
        second = session.clients[1]
        self.assertTrue(second.welcome(session.deliver(second.hello, session.addresses[1])[0][1]))
        first.open(session.deliver(first.keyreq(), session.addresses[0])[0][1])
        self.assertEqual(first.roster, [first.public, second.public])
        self.assertEqual(first.group, session.wire.group)

    def test_client_detects_a_changed_permit(self):
        session = Session(2, 0)
        session.handshake()
        session.report(1, 0, '01')
        replies = session.report(2, 0, '02')
        client = session.clients[0]
        ascii_part, trailer = client.open(replies[0][1])
        self.assertTrue(client.verify_permit(ascii_part, trailer))
        self.assertFalse(client.verify_permit(ascii_part.replace(b'|02', b'|03'), trailer))              # slot 2's input changed
        self.assertFalse(client.verify_permit(ascii_part, trailer[:64]+bytes(64)+trailer[128:]))         # slot 2's signature
        self.assertFalse(client.verify_permit(ascii_part, trailer[:128]+bytes(64)))                      # relay signature


@unittest.skipIf(LIB is None, MISSING)
class RelayProcessCryptoTests(unittest.TestCase):
    def test_real_udp_two_clients_window_1_with_journal(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
            reservation.close()
            keyfile, report, journal = Path(directory)/'relay.key', Path(directory)/'report.json', Path(directory)/'journal.jsonl'
            keyfile.write_text(json.dumps(nc.new_key()))
            key = nc.load_key(keyfile)
            process = subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', str(port), '--token', TOKEN,
                                        '--hold-tick', '99', '--clients', '2', '--ticks', '4', '--lookahead', '1', '--report', str(report),
                                        '--input-journal', str(journal), '--wire-crypto', str(keyfile), '--wire-crypto-lib', str(LIB.path)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            peers = []
            try:
                self.assertTrue(process.stdout.readline().startswith('NATIVE_GATE RELAY WIRE_CRYPTO relay_key='))
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
                clients = [nc.Client(LIB, nc.credential(LIB, key, TOKEN, s), TOKEN, s, 2, 1) for s in (1, 2)]
                for _ in clients:
                    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    peer.bind(('127.0.0.1', 0));peer.settimeout(5);peers.append(peer)
                # In the clear, nothing is answered.
                peers[0].sendto(f'NGSTATE|4|{TOKEN}|1|0|{game_hex(0)}|{game_hex(0)}|{"b"*64}|2|1|'.encode(), ('127.0.0.1', port))
                for client, peer in zip(clients, peers):
                    peer.sendto(client.hello, ('127.0.0.1', port))
                    self.assertTrue(client.welcome(peer.recv(8192)))
                for client, peer in zip(clients, peers):
                    peer.sendto(client.keyreq(), ('127.0.0.1', port))
                    client.open(peer.recv(8192))
                admitted = {1: [], 2: []}
                # Window 1, four ticks: tick 0 releases permits 1 and 2, ticks 1 and 2 release 3 and 4, tick 4 stops.
                for tick, expected in ((0, 2), (1, 1), (2, 1), (3, 0), (4, 1)):
                    for client, peer in zip(clients, peers):
                        peer.sendto(client.report(tick, f'{client.slot:02x}{tick:02x}', game=20+tick/60, real=20+tick/60), ('127.0.0.1', port))
                    for client, peer in zip(clients, peers):
                        for _ in range(expected):
                            ascii_part, trailer = client.open(peer.recv(8192))
                            if ascii_part.startswith(b'NGSTOP'):
                                self.assertEqual(ascii_part, f'NGSTOP|4|{TOKEN}|{client.slot}|4'.encode())
                                continue
                            self.assertTrue(client.verify_permit(ascii_part, trailer))
                            admitted[client.slot].append(ascii_part.split(b'|', 4)[4])
                self.assertEqual(admitted[1], admitted[2])
                self.assertEqual(admitted[1][-1], b'4|1|0102|0202')
                for line in iter(process.stdout.readline, ''):
                    if 'done=True' in line: break
                saved = json.loads(report.read_text())
                self.assertEqual((saved['verdict'], saved['common_ticks']), ('pass', [0, 1, 2, 3, 4]))
                summary = saved['wire_crypto']
                self.assertEqual((summary['failure'], summary['counts']['dropped_plain'], summary['counts']['handshakes']), (None, 1, 2))
                self.assertEqual(summary['players'], {'1': clients[0].public.hex(), '2': clients[1].public.hex()})
                lines = [json.loads(line) for line in journal.read_text().splitlines()]
                # Every journaled record still verifies: the input journal is auditable after the session.
                for line in lines:
                    for slot, row in line['rows'].items():
                        self.assertTrue(LIB.record_verify(bytes.fromhex(line['sigs'][slot]), clients[int(slot)-1].public, SID, int(slot), 2, 1,
                                                          line['tick'], bytes.fromhex(row['blob'])))
                self.assertEqual([line['tick'] for line in lines], [0, 1, 2, 3])
            finally:
                for peer in peers: peer.close()
                process.terminate();process.communicate(timeout=5)

    def test_cli(self):
        tool = [sys.executable, str(ROOT/'tools/dev/native_gate_crypto.py'), '--lib', str(LIB.path)]
        with tempfile.TemporaryDirectory() as directory:
            keyfile = Path(directory)/'relay.key'
            subprocess.run(tool+['keygen', '--out', str(keyfile)], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(tool+['keygen', '--out', str(keyfile)], capture_output=True).returncode, 0)  # never overwritten
            out = subprocess.run(tool+['credential', '--key', str(keyfile), '--token', TOKEN, '--slot', '2'], check=True, capture_output=True,
                                 text=True).stdout.strip()
            self.assertRegex(out, r'^x4c1:[0-9a-f]{64}:[0-9a-f]{64}$')
            self.assertEqual(out, nc.credential(LIB, nc.load_key(keyfile), TOKEN, 2))
            refused = subprocess.run([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', '40000', '--token', TOKEN,
                                      '--report', str(Path(directory)/'r.json'), '--wire-crypto', str(keyfile), '--live-inputs'],
                                     capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn('--wire-crypto covers protocols 3 and 4', refused.stderr)
            refused = subprocess.run([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', '40000', '--token', TOKEN,
                                      '--report', str(Path(directory)/'r.json'), '--wire-crypto', str(Path(directory)/'missing.key')],
                                     capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
        self.assertIn('SELFTEST PASS', subprocess.run(tool+['selftest'], check=True, capture_output=True, text=True).stdout)
        bench = json.loads(subprocess.run(tool+['bench', '--clients', '3', '--ticks', '20'], check=True, capture_output=True, text=True).stdout)
        self.assertEqual((bench['clients'], bench['permits']), (3, 3*(20+1)))  # window 1: tick 0 releases two permits


if __name__ == '__main__':
    unittest.main()
