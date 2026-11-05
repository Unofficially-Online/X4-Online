"""tools/dev/native_gate_join.py: JOIN records, the join package, and the relay's hot join (JoinBarrier) against
simulated clients that apply every permit like a gate (protocols 3 and 4). Synthetic peers only, no X4.

The simulated clients keep a rolling world state: state(t) = sha256(state(t-1) | blobs of permit t), digest = that
state. A joiner starts from a saver's state at T (the package) and must then report exactly the session's digests,
so the relay's replay is checked end to end: every permit the joiner gets equals the one the session got."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
spec = importlib.util.spec_from_file_location('native_gate_join', ROOT/'tools/dev/native_gate_join.py')
join = importlib.util.module_from_spec(spec)
spec.loader.exec_module(join)
import native_gate_inputs as mux  # noqa: E402
TOKEN = 'ab'*16
HZ = 30
BOOT = 'b'*64


def bits(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]


def game(tick):
    return 7.0+tick/HZ


def real(tick):
    return 100.0+tick/HZ


class Client:
    """One simulated gate client: reports, permits, the rolling state, JOIN records (save and handover)."""
    def __init__(self, slot, clients, k, address, tick=0, state=b'world', saver_packages=None, joined_from=None):
        self.slot, self.clients, self.k, self.address = slot, clients, k, address
        self.tick, self.state = tick, state
        self.permits, self.applied, self.reports = {}, [], {}
        self.save_at, self.saved, self.handover, self.stopped = None, None, None, None
        self.packages = saver_packages if saver_packages is not None else {}
        self.joined_from = joined_from        # the save tick a joiner loaded (its first report is the boot report)
        self.inputs = {}                      # tick -> own blob to propose (tests put pilot-like records here)
        self.fail_save, self.failed = None, None   # a FAILED reason code instead of the save
        self.portable_salt = b''               # a saver whose package content differs from the others'
        self.clock = 0.0                       # this process's own real clock base (--relative-real compares elapsed time)

    def digest(self):
        if self.joined_from is not None and self.tick == self.joined_from:
            return BOOT
        return hashlib.sha256(self.state).hexdigest() if self.tick else BOOT

    def proposal(self):
        if self.failed and not self.failed.get('announced') and self.tick > self.failed['tick']:
            self.failed['announced'] = True
            return join.record_blob(join.encode_record(join.OP_FAILED, self.failed['k'], self.failed['tick'], reason=self.fail_save))
        if self.saved and not self.saved.get('announced') and self.tick > self.saved['tick']:
            self.saved['announced'] = True
            return join.record_blob(join.encode_record(join.OP_SAVED, self.saved['k'], self.saved['tick'], size=1234,
                                                       package=self.saved['package'], portable=self.saved['portable']))
        if self.joined_from is not None and (self.handover is None or self.tick < self.handover):
            return ''                         # a joiner proposes nothing before its handover
        return self.inputs.get(self.tick, '')

    def report(self):
        if self.tick not in self.reports:
            version = 4 if self.k else 3
            extra = f'|{self.k}' if self.k else ''
            self.reports[self.tick] = (f'NGSTATE|{version}|{TOKEN}|{self.slot}|{self.tick}|{struct.pack(">d", game(self.tick)).hex()}|'
                                       f'{struct.pack(">d", real(self.tick)+self.clock).hex()}|{self.digest()}|{self.clients}{extra}|'
                                       f'{self.proposal()}').encode()
        return self.reports[self.tick]

    def receive(self, packet):
        f = packet.decode().split('|')
        if f[0] == 'NGSTOP':
            self.stopped = int(f[4])
            return
        assert f[0] == 'NGGO' and f[2] == TOKEN and int(f[3]) == self.slot, packet
        tick = int(f[4])
        blobs = f[6:] if self.k else f[5:]
        assert len(blobs) == self.clients
        held = self.permits.setdefault(tick, blobs)
        assert held == blobs, (tick, held, blobs)   # one permit per tick, whoever sent it

    def step(self):
        """Run the next tick if its permit is held; the save of a SAVE_AT tick happens after the step."""
        nxt = self.tick+1
        if nxt not in self.permits or self.stopped is not None:
            return False
        blobs = self.permits[nxt]
        self.state = hashlib.sha256(self.state+'|'.join(blobs).encode()).digest()
        for slot, blob in enumerate(blobs, 1):
            record = join.blob_record(blob) if blob else None
            if not record:
                continue
            if record['op'] == 'save_at' and record['mask'] >> (self.slot-1) & 1:
                self.save_at = (record['slot'], record['tick'])
            if record['op'] == 'handover':
                self.handover = record['tick']
        if self.handover == nxt:
            self.state = hashlib.sha256(self.state+b'identity').digest()   # slot k's identity steps at H on every client
        self.tick = nxt
        self.applied.append((nxt, tuple(blobs)))
        if self.save_at and self.save_at[1] == self.tick and self.fail_save and not self.failed:
            self.failed = {'k': self.save_at[0], 'tick': self.tick}
        elif self.save_at and self.save_at[1] == self.tick and not self.saved:
            k, t = self.save_at
            package = hashlib.sha256(f'{self.slot}|{t}'.encode()).digest()
            portable = hashlib.sha256(self.state+self.portable_salt).digest()[:16]
            self.saved = {'k': k, 'tick': t, 'package': package, 'portable': portable}
            self.packages[package.hex()] = {'tick': t, 'state': self.state}
        return True


class Network:
    """The relay (a JoinBarrier) and its clients, stepped in lock: every client reports, the relay answers."""
    def __init__(self, clients=3, absent=(3,), k=0, ticks=400, barrier=None, **options):
        self.k, self.now, self.clients = k, 0.0, clients
        self.relay = join.JoinBarrier(TOKEN, absent=absent, ticks=ticks, hold_seconds=0, relative_real=True, clients=clients,
                                      lookahead=k, tick_hz=HZ, join_options=options, **(barrier or {}))
        self.packages, self.notices = {}, []
        self.live = {s: Client(s, clients, k, ('127.0.0.1', 10000+s), saver_packages=self.packages)
                     for s in range(1, clients+1) if s not in absent}
        self.joiner, self.launcher = None, ('127.0.0.1', 20000)

    def deliver(self, out):
        for address, packet in out:
            for client in list(self.live.values())+([self.joiner] if self.joiner else []):
                if client.address == address and not packet.startswith(b'NGJOINSTATE'):
                    client.receive(packet)
            if address == self.launcher:
                self.answers.append(packet.decode())

    def send(self, client):
        # Like lockstep_lookahead.h: the reports of the window completed-k..completed are resent while waiting.
        client.report()
        for tick in range(max(client.tick-self.k, 0), client.tick+1):
            if tick in client.reports:
                self.deliver(self.relay.receive(client.reports[tick], client.address, self.now))

    def round(self, joiner_steps=1, live_steps=1):
        self.now += 1/HZ
        for client in self.live.values():
            self.send(client)
        if self.joiner:
            for _ in range(joiner_steps):
                self.send(self.joiner)
                if not self.joiner.step():
                    break
            self.send(self.joiner)
        self.deliver(self.relay.poll(self.now))
        self.notices += self.relay.take_notices()
        for _ in range(live_steps):
            for client in self.live.values():
                client.step()

    def request(self, slot=3, nonce='0123456789abcdef'):
        self.answers = getattr(self, 'answers', [])
        self.deliver(self.relay.receive(f'NGJOIN|1|{TOKEN}|{slot}|{nonce}'.encode(), self.launcher, self.now))
        return self.answers[-1].split('|') if self.answers else None

    def boot_joiner(self, slot=3):
        answer = self.request(slot)
        assert answer[5] == 'saved', answer
        package = self.packages[answer[9]]
        self.joiner = Client(slot, self.clients, self.k, ('127.0.0.1', 10000+slot), tick=package['tick'], state=package['state'],
                             joined_from=package['tick'])
        return answer


class Records(unittest.TestCase):
    def test_round_trip_and_blob(self):
        cases = [dict(op=join.OP_SAVE_AT, slot=3, tick=600, mask=3, nonce=0x0123456789abcdef),
                 dict(op=join.OP_SAVED, slot=3, tick=600, size=99, package=b'\x11'*32, portable=b'\x22'*16),
                 dict(op=join.OP_FAILED, slot=3, tick=600, reason=2), dict(op=join.OP_HANDOVER, slot=3, tick=600),
                 dict(op=join.OP_ABORT, slot=3, tick=601, reason=13)]
        sizes = []
        for case in cases:
            text = join.encode_record(**case)
            sizes.append(len(text)//2)
            record = join.decode_record(text)
            self.assertEqual((record['slot'], record['tick']), (3, case['tick']))
            blob = join.record_blob(text)
            self.assertEqual(blob, '4d01'+f'4a{len(text)//2:02x}'+text)
            self.assertEqual(join.blob_record(blob), record)
            self.assertEqual(mux.decode(blob), {'join': text})
        self.assertEqual(sizes, [16, 63, 8, 7, 8])
        self.assertEqual(join.encode_record(join.OP_HANDOVER, 3, 600), '01040358020000')   # tests/test_native_gate_inputs.py JOIN
        self.assertIsNone(join.blob_record(''))

    def test_refusals(self):
        good = join.encode_record(join.OP_FAILED, 3, 600, reason=2)
        for bad, reason in ((good[:-2], 'record_length'), (good+'00', 'record_length'), ('02'+good[2:], 'join_version'),
                            (good[:2]+'09'+good[4:], 'record_op'), (good[:4]+'00'+good[6:], 'record_field'),
                            (good[:-2]+'ee', 'record_field'), ('zz', 'encoding')):
            with self.subTest(bad=bad), self.assertRaisesRegex(join.JoinError, reason):
                join.decode_record(bad)
        for kwargs in (dict(op=join.OP_SAVE_AT, slot=3, tick=1, mask=0), dict(op=join.OP_SAVED, slot=3, tick=1, size=1, package=b'x'),
                       dict(op=9, slot=3, tick=1), dict(op=join.OP_HANDOVER, slot=9, tick=1)):
            with self.subTest(kwargs=kwargs), self.assertRaises(join.JoinError):
                join.encode_record(**kwargs)


def golden(version=join.PACKAGE_VERSION):
    """The package lockstep_join.h's test builds byte for byte (test_lockstep_join.h join_golden); version 1 is the
    package of a saver before the world side table (still read)."""
    gate = {name: 0 for name in join.GATE_FIELDS}
    gate.update(tick=600, game=bits(27.0), real=bits(120.0), wire_version=3, clients=3, lookahead=2, tick_hz=30, slot=1, md_seq=4242,
                rid_next=0x7f0001, created_generation=5, created_live=1, identities=2, ctx_count=2, wake_count=1, vec_index=1, vec0=1,
                vec1=0, md_count=2, md_void=1, md_release=0, md_queued=0, unsupported=0, digest='c'*64, save_us=1500000,
                capture_us=2500, ambiguous=0, flags=0, fixture='111111121111')
    sections = {
        join.SEC_IDS: [(0x1001, 0x2001, 0, 0), (0x1002, (1 << 63) | 0x55, 1, 0)],
        join.SEC_CTX: [(0x2001, 0xaaaa, 0, 3), ((1 << 63) | 0x55, 0xbbbb, 1, 1)],
        join.SEC_WAKE: [(0x2001, 0xaaaa, bits(27.5), 0, 3, 2, 0)],
        join.SEC_WAKEVEC: [((1 << 63) | 0x55, 0xbbbb, 1, 1, 0, 0, 4, 0)],
        join.SEC_MDHEAP: [(0xcccc, 0, bits(28.0), bits(20.0), 17, 2, 0, 2), (0xdddd, 0x1234, bits(27.75), bits(-1.0), 18, 1, 0, 1)],
        join.SEC_MODULES: ['ai_wait_rng=superseded', 'world_identities=serialized'],
        join.SEC_LUA: ['identity=1|ready|00112233'],
    }
    if version >= 2:
        # The world side table (lockstep_join_world.h; j1-11's shapes): a ship's recharging shield group (changed time
        # 26.41633, saved as 26.4163), a build storage cargo bay's empty group (changed = the loading clock 7.083), a
        # turret's pose lanes (getter 0x10ba20, controller+0x10).
        sections[join.SEC_HEALTH] = [(0x2001, bits(3100.0), bits(1150.0), bits(26.41633), bits(1955.0), join.HEALTH_SHIELD, 1),
                                     (0x2003, bits(2500.0), 0, bits(7.083), 0, join.HEALTH_SHIELD, 0)]
        sections[join.SEC_POSE] = [(0x2004, 0x2a35000, 1, *(0x3f800000+i for i in range(12)))]
    if version >= 3:
        sections.update(golden_v3())
    if version >= 4:
        sections.update(golden_v4())
    if version >= 5:
        sections.update(golden_v5())
    if version >= 6:
        sections.update(golden_v6())
    if version >= 7:
        sections.update(golden_v7())
    if version >= 8:
        sections.update(golden_v8())
    if version >= 9:
        sections.update(golden_v9())
    if version >= 10:
        sections.update(golden_v10())
    if version >= 11:
        sections.update(golden_v11())
    if version >= 12:
        sections.update(golden_v12())
    if version >= 13:
        sections.update(golden_v13())
    return join.write_package(gate, sections, save_sha=bytes(range(32)), save_bytes=123456, version=version)


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def golden_v3():
    """Version 3's records (j1-12's shapes): the ship's and the cargo bay's shield parameters, the ship's navigation controller
    (cursor on the write bank, a stale parameter due, both banks with their own states and a pending transition count, a
    reference to the cargo bay), both objects' attention (the ship an Object with its due) and two first-call keys."""
    one = 0x3f800000
    bank0 = (*(0x40000000+i for i in range(18)), 0x2003, 0, bits(-1.0), 0, 1, 1, 0, 0, 2, 0)
    bank1 = (*(0x40800000+i for i in range(18)), 0, 0, bits(-1.0), 0x2003, 2, 1, 0, 0, 2, 1)
    return {join.SEC_SHIELD: [(0x2001, bits(15.0), f32(1.0), f32(120.0), one, one, one, 0), (0x2003, 0, 0, 0, one, one, one, 0)],
            join.SEC_NAV: [(0x2001, bits(26.98), bits(9.075415789763133), bits(27.5), 1, 1, 0, 0, 0, *bank0, *bank1)],
            join.SEC_ATTN: [(0x2001, bits(28.1), 5, join.ATTN_DUE), (0x2003, 0, 3, 0)],
            join.SEC_KEYS: [(0x2001, 1, 0), (0x2001, 4, 1)]}


def golden_v4():
    """Version 4's records (j1-14's shapes, test_lockstep_join.h join_golden): the pathing manager's two batch vectors (two
    batches round-robin at 1, one empty batch), a flagged batch of the ship and the cargo bay with its cursor on the cargo bay,
    a batch whose one member's owner has no portable ID, the turret's Agent on the new-agent list; a hazard region's two damage
    fields (the last pass of cluster_32's at tick 607, 27.31633 s)."""
    return {join.SEC_PATH: [(0, join.PATH_MANAGER, 0, 2, 0, 1, 0), (0, join.PATH_MANAGER, 1, 1, 0, 0, 0),
                            (0, join.PATH_BATCH, 0, 0, 1, 2, 1), (0, join.PATH_BATCH, 0, 1, 0, 1, 0), (0, join.PATH_BATCH, 1, 0, 0, 0, 0),
                            (0x2001, join.PATH_MEMBER, 0, 0, 0, 0, 0), (0x2003, join.PATH_MEMBER, 0, 0, 1, 0, 0),
                            (0, join.PATH_MEMBER, 0, 1, 0, 0, join.PATH_LOCAL), (0x2004, join.PATH_PENDING, 0, 0, 0, 0, 0)],
            join.SEC_REGION: [(0x2005, bits(27.316333333333866), bits(28.4), 0, 0), (0x2005, bits(20.383), bits(21.5), 2, 0)]}


def golden_v5():
    """Version 5's records (j1-15's shapes, test_lockstep_join.h join_golden): the ship's linear flight control model with one
    Bezier segment (its vtable bytes 0) and two parameter pairs, the turret's steering model (kind only)."""
    state = bytes((i*7+1) & 255 for i in range(join.FLIGHT_STATE))
    segment = bytes(0 if 0x10 <= i < 0x18 else i+3 for i in range(160))
    return {join.SEC_FLIGHT: [(0x2001, join.FLIGHT_LINEAR, 0, 1, 2, state, 0), (0x2004, 0x2c4e9c8, join.FLIGHT_OTHER, 0, 0, bytes(204), 0)],
            join.SEC_SPLINE: [(0x2001, 0, 0, segment), (0x2001, 1, 0, bytes(0x80+i for i in range(16))+bytes(144))]}


def golden_v6():
    """Version 6's records (j1-20's shapes, test_lockstep_join.h join_golden): the ship's Agent (18 values 1.0 .. 18.0, the
    formation speed mode +0x4d2 and the re-target flag +0xf33 set, one waypoint, a group of three it leads), the turret's
    (following a formation)."""
    flags = bytes([0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0])
    return {join.SEC_AGENT: [(0x2001, *(f32(1.0+i) for i in range(18)), flags, 1, 0, 3, 0, 0),
                             (0x2004, *(0 for _ in range(18)), bytes(12), 0, 0, 0, 1, 0)]}


def golden_v7():
    """Version 7's records (j1-21's shapes, test_lockstep_join.h join_golden): the ship's ROUTE head (its linear model's word the
    cargo bay, +0xef8 the hazard region, the bytes +0xf5c, +0xf74, +0xf75 set, +0xf70 7) and its one waypoint (the cargo bay, a
    sub-point on the hazard region), the turret's head (+0xf48 without a portable ID)."""
    head = struct.pack('<HH6BxxI', 1, 0, 1, 0, 1, 1, 0, 0, 7)+bytes(152)
    point = bytearray((i*5+2) & 255 for i in range(0x68))
    point[join.ROUTE_PAD] = 0
    point = bytes(point)+bytes(0x40+i for i in range(0x40))
    turret = struct.pack('<HH6BxxI', 0, 0, 0, 0, 0, 0, 0, 0, 0)+bytes(152)
    return {join.SEC_ROUTE: [(0x2001, join.ROUTE_HEAD, 0, 0, join.ROUTE_LINEAR, 0, 0x2003, 0x2005, 0, 0, 0, head),
                             (0x2001, join.ROUTE_POINT, 0, 0, join.ROUTE_SUB, 0, 0x2003, 0x2005, 0, 0, 0, point),
                             (0x2004, join.ROUTE_HEAD, 0, 0, 1 << 3, 0, 0, 0, 0, 0, 0, turret)]}


def golden_v8():
    """Version 8's records (j1-22's shapes, test_lockstep_join.h join_golden): the ship's PARAM (its read slot's object bytes i*3+1,
    refresh time 27.9, an ID without a portable ID; its own copy's turn rates 0.5, 0.25, 0.125, the ID the cargo bay, time 28.05, the
    mode byte +0x190), the turret's (all 0); the weapon's dock controller (start 13.950000000001, animation 3, the first option,
    lanes 1.0 + k ulp)."""
    read = bytearray((i*3+1) & 255 for i in range(join.PARAM_OBJECT))+bytearray(join.PARAM_SLOT-join.PARAM_OBJECT)
    read[join.PARAMB_TIME:join.PARAMB_TIME+8] = struct.pack('<Q', bits(27.9))
    own = bytearray(join.PARAM_SLOT)
    own[join.PARAM_TURN:join.PARAM_TURN+12] = struct.pack('<3f', 0.5, 0.25, 0.125)
    own[join.PARAMB_ID:join.PARAMB_ID+8] = struct.pack('<Q', 0x2003)
    own[join.PARAMB_TIME:join.PARAMB_TIME+8] = struct.pack('<Q', bits(28.05))
    own[join.PARAMB_TAIL+0x10] = 1
    empty = bytes(join.PARAM_SLOT)
    return {join.SEC_PARAM: [(0x2001, 1, 0, bytes(read), empty, bytes(own)), (0x2004, 0, 0, empty, empty, empty)],
            join.SEC_DOCK: [(0x2002, join.DOCK_VT, 0, bits(13.950000000001), 3, b'\1\0', 0, *(0x3f800000+k for k in range(42)))]}


def golden_v9():
    """Version 9's records (j1-25's shapes, test_lockstep_join.h join_golden): the turret's steering model (its carried bytes
    i*11+3, the target the ship, the second ID without a portable ID, a path job pending, three contacts), a chase behaviour (its
    carried bytes i*5+1) and the generic default (7, 8, 9), one path point (ROUTE's golden point, list 2)."""
    model = bytes((i*11+3) & 255 for i in range(569))+bytes(join.STEER_MODEL-569)
    chase = bytes((i*5+1) & 255 for i in range(94))+bytes(join.STEER_STATE-94)
    generic = bytes([7, 8, 9])+bytes(join.STEER_STATE-3)
    point = golden_v7()[join.SEC_ROUTE][1]
    return {join.SEC_STEER: [(0x2004, 2 | join.STEER_JOB, join.STEER_VT, 0x2001, 0, 0x2c4e840, 0x2c4edc0, 1, 3, 0, model, chase, generic)],
            join.SEC_STEERPT: [(0x2004, join.ROUTE_POINT, join.STEER_LIST)+point[3:]]}


def golden_v10():
    """Version 10's records (j1-27's shapes, test_lockstep_join.h join_golden): the turret's motion solver (bytes i*7+1, a pointer into itself
    at +0x16a0 and a vtable at +0x6a00), two bullets in the cargo bay's zone: a linear one with its shooter, macro and bytes, and one with
    nothing readable."""
    data = bytearray((i*7+1) & 255 for i in range(join.SOLVER_BYTES))
    data[0x16a0:0x16a8] = struct.pack('<Q', 0x3e8)
    data[0x6a00:0x6a08] = struct.pack('<Q', 0x2c1f0f8)
    listed = struct.pack('<2I', (0x16a0//8) << 1, (0x6a00//8) << 1 | 1)+bytes(4*join.SOLVER_POINTERS-8)
    macro = b'bullet_ter_s_gatling_01_mk1_macro'
    return {join.SEC_SOLVER: [(0x2004, join.SOLVER_ACTIVE, join.STEER_VT, 2, 0, listed, bytes(data))],
            join.SEC_BULLET: [(0x2003, 0x2004, 3, 0, 0, 0x2c3f7c0, macro+bytes(join.BULLET_MACRO-len(macro)),
                               bytes((i*3+5) & 255 for i in range(join.BULLET_BYTES)), bytes((i*9+2) & 255 for i in range(join.BULLET_MOTION))),
                              (0x2003, 0, 7, 1, join.BULLET_NO_SHOOTER | join.BULLET_NO_MACRO | join.BULLET_NO_MOTION, 0, bytes(join.BULLET_MACRO),
                               bytes(join.BULLET_BYTES), bytes(join.BULLET_MOTION))]}


def golden_v11():
    """Version 11's records (j1-32's shapes, test_lockstep_join.h join_golden): an active unit owned by the turret with two primary contacts
    (the ship, one without a portable ID) and one second contact (the cargo bay), a passive unit of the module, the active sources (the
    cargo bay pending, an unmapped current one) and a field sector seen by the deadlines module."""
    active = bytes(((i*5+3) & 255) if 0x40 <= i and (i < 0x68 or i >= 0xa8) else 0 for i in range(join.GRAV_BYTES))
    passive = bytes(((i*11+1) & 255) if 0x40 <= i < 0x68 else 0 for i in range(join.GRAV_BYTES))
    return {join.SEC_GRAV: [(0x2004, join.GRAV_ACTIVE_VT, join.GRAV_ACTIVE, 2, 1, 0, 0, active),
                            (0x2005, join.GRAV_PASSIVE_VT, 0, 0, 0, 0, 0, passive)],
            join.SEC_GRAVC: [(0x2004, 0, 0, 0x2001, 0, 0), (0x2004, 0, 1, 0, join.GRAVC_UNMAPPED, 0), (0x2004, 1, 0, 0x2003, 0, 0)],
            join.SEC_GRAVS: [(0x2003, 0, join.GRAVS_PENDING, 0, 0, 0), (0, 0, join.GRAVS_CURRENT, 0, join.GRAVS_UNMAPPED, 0),
                             (0x2006, 0x4029000000000000, join.GRAVS_SECTOR, 7, join.GRAVS_SEEN, 0), (0, 3, join.GRAVS_CURSOR, 40, 0, 0)]}


def golden_v12():
    """Version 12's records (test_lockstep_join.h join_golden): the turret's spline model (bytes i*13+7) with one entry in its route
    (an entry owning a spline of one segment and one pair) and one segment and two pairs in its first embedded spline; the zone groups'
    neighbours of one sector: the cargo bay's and the module's zones neighbours, a third zone with none; the published view poses of two
    radar objects (the first in both views, its other view without a pose); the radar worker's pending posts (a has_scanned post, one
    whose source has no portable ID, another kind)."""
    model = bytes((i*13+7) & 255 for i in range(join.SPLM_BYTES))
    entry = bytes(8)+bytes((i*3+1) & 255 for i in range(8, 0x50))
    spline = bytes(8)+bytes((i*5+2) & 255 for i in range(8, 0x10))+bytes(0x30)+bytes((i*5+2) & 255 for i in range(0x40, 0x50))
    seg0 = bytes((i*7+1) & 255 if not 0x10 <= i < 0x18 else 0 for i in range(0xa0))
    seg1 = bytes((i*9+4) & 255 if not 0x10 <= i < 0x18 else 0 for i in range(0xa0))
    return {join.SEC_SPLM: [(0x2004, 0, 1, 0, 0, 1, 0, 2, 0, 0, 0, model)],
            join.SEC_SPLME: [(0x2004, 0, 0, join.SPLME_SPLINE, 1, 1, 0, entry, spline)],
            join.SEC_SPLMSEG: [(0x2004, 0, 0, seg0), (0x2004, 1, 0, bytes(range(1, 17))+bytes(0xa0-16)), (0x2004, 4, 0, seg1),
                               (0x2004, 5, 0, bytes([0x11]*8)+bytes(0xa0-8))],
            join.SEC_GRAVN: [(0x2006, 0x2003, 0x2005, 0, 0), (0x2006, 0x2005, 0x2003, 0, 0), (0x2006, 0x2007, 0, join.GRAVN_NONE, 0)],
            join.SEC_GRAVP: [(0x2003, 0, join.GRAVP_PRESENT, bytes((i*17+3) & 255 for i in range(0x40))), (0x2003, 1, 0, bytes(0x40)),
                             (0x2006, 0, join.GRAVP_PRESENT, bytes((i*19+5) & 255 for i in range(0x40)))],
            join.SEC_GRAVQ: [(0x2003, 0, 3, 0, join.GRAVQ_EVENT_VT, 0x403c400000000000, 0, 0),
                             (0, 1, 3, join.GRAVQ_UNMAPPED, join.GRAVQ_EVENT_VT, 0x403c400000000000, 0, 0),
                             (0x2006, 2, 1, join.GRAVQ_OTHER, 0, 0, 0x55, 0)]}


def golden_v13():
    """Version 13's records (test_lockstep_join.h join_golden): the job engine's schedule (its last run 23.25, its cursor); three
    pending events (two of one source, one of a source without a portable ID)."""
    return {join.SEC_JOBE: [(0x4037400000000000, join.JOBE_ENABLED | join.JOBE_RUNNING, 13, b'xenon_miner_1'+bytes(join.JOBE_CURSOR-13))],
            join.SEC_EVQT: [(0, 0x2ac1710, 0x20d, 0, join.EVQT_UNMAPPED, bits(40.0)), (0x2001, 0x2ac17b8, 0x215, 1, 0, bits(31.5)),
                            (0x2001, 0x2baf650, 0x20f, 0, 0, bits(30.25))],
            join.SEC_GRAVD: [(0x2001, 0x2003, 0, 0), (0x2001, 0, 1, join.GRAVD_NONE)],
            join.SEC_VIEWK: [(0x2002, 0, 0, 0x10042, 1), (0x2002, 1, join.VIEWK_UNMAPPED, 0x10042, 1)],
            join.SEC_VIEWKC: [(0x2002, 0x2003, 0, 0), (0x2002, 0, 1, 0)],
            join.SEC_VIEWF: [(0x2001, 0x10058, 0x10058, 1, 1), (0x2002, 0x10042, 0x10042, 6, 6)],
            join.SEC_VIEWP: [(0x2001, bytes(range(64)))],
            join.SEC_SHOOT: [(0x2001, 0x4039d983c465091e, 0x403c2ed916872ba4, 0x403c26508dfea31b, 0x3f9eb851e0000000, 0x403c2ed916872ba4,
                              0x403bf31d5acb6fe5, bytes(range(32)), 0x2b7fcc0, b'\x01\x01\x01', b'\x01\x00', 1, bytes(2), 0x3db2b8c2)],
            join.SEC_ATTK: [(0x2001, 0x2002, 0x403b5987f5df8f7b, 0x403b5987f5df8f7b, 0, 7, 0)],
            join.SEC_LIVEF: [(0x2001, 0, 0x20)],
            join.SEC_SPEC: [(0x2001, 3, 0x4f, 2, 0, 0x3fc5555555555555, 0x403c26508dfea31b), (0x2001, 7, 0x7f, 0, 0, 0, 0x403c2ed916872b02)],
            join.SEC_NAVC: [(0x2001, bytes(range(96)))],
            join.SEC_JOBS: [(0xcbf29ce484222325, 3, 524, 0, 0)],
            join.SEC_GONE: [(0x6a0ad,), (0x8000000000001234,)],
            join.SEC_BODY: [(0x2001, 0, 3, *[0x3f800000+i for i in range(8)], *[0x40000000+i for i in range(23)], 1,
                             *[0x1000+i for i in range(8)], *[0x2000+i for i in range(8)])],
            join.SEC_BUPD: [(0x2001, 0x401c54fdf3b645a2), (0x2003, 0x403c2ed916872ba4)],
            join.SEC_LDC: [(0x2001, bytes(range(113)))],
            join.SEC_NAVT: [(0x2001, *[0x3f800000+i for i in range(30)])],
            join.SEC_NMESH: [(0xa5ec412f2588ab8d,), (16,), (0x1234,), (0x5678,)],
            join.SEC_NOCT: [(3, 1, 0, 0x67a818fdb1644740, 0, 0, 0, 0, 0), (1, 0x2001, 12, 1, 2, 1, 1, 0, 0),
                            (2, 0x2003, 12, 0, 2, 0, 1, 1, 0), (0xffffffffffffffff, 0x2001, 0, 0, 2, 1, 2, 0, 0)],
            join.SEC_AGTT: [(0x2001, 0x401c54fdf3b645a2, 0, 0x4028a1f671529a3e), (0x2004, 0xbff0000000000000, 0, 0)],
            join.SEC_AGCB: [(0x2001, bytes(range(256))*4)],
            join.SEC_PBNK: [(0x2001, 0, 0, *[0x3f800000+i for i in range(18)], 0x2003, 0, 0x4000000000000000, 0, 1, 3, 1, 0, 2, 1, *[0x3f800000+i for i in range(18)], 0x2003, 0, 0x4000000000000000, 0, 1, 3, 1, 0, 2, 1, *[0x40000000+i for i in range(30)])],
            join.SEC_PEND: [(0x2001, 0x19, 0), (0x2004, 0x8, 0)],
            join.SEC_GRAVB: [(0x16, 0, 0x454b1b00, 0x4638f638, 0x48a34182), (0x16, 2, 0x454b1b00, 0x4638f638, 0x48970c82)],
            join.SEC_VIEWL: [(0x2001, 1, 1, bytes(6)), (0x2004, 1, 0, bytes(6))],
            join.SEC_IKV: [(0x2001, 0, 0, 0, bytes(range(1, 25))), (0x2001, 0, 1, 0, bytes(24))],
            join.SEC_RCNT: [(0x8000000000002001, 31, 0)],
            join.SEC_ECOS: [(0x2001, 612.5, 70.25)],
            join.SEC_SHPW: [(0x2001, 1, 2, 3, 4, 5, 6)],
            join.SEC_TRDN: [(0x2001, 88.5)]}


GOLDEN_SHA = '647b74de77bd1f094d5df17bc47a3bd5ec5628482ef58cfd0a827d096f8d4bde'
GOLDEN_SHA_V12 = '67459b50e3057be54939b69aae43c7fc8ba426523b78274f70eb2702beaff7ef'   # a version 12 package (before JOBE)
GOLDEN_SHA_V11 = 'f0874152607c013ff72febfbf675b20f64b391fb5a9d431795bd302fc0964e7f'   # a version 11 package (before SPLM..GRAVP)
GOLDEN_SHA_V10 = '1a324e00b49dbc814f757c26ad28741675b7d8e0b9df120c63d52594724e27bd'   # a version 10 package (before GRAV, GRAVC, GRAVS)
GOLDEN_SHA_V9 = 'dc43e62589f878b9f7dcbd36907ec4bbdbea4b715287dcde799cd239248e2983'   # a version 9 package (before SOLVER, BULLET)
GOLDEN_SHA_V8 = '57d2e0fd08fd8171111e0156d00c6a80a7603148d8e552fa56c598e58a7f58e8'   # a version 8 package (before STEER, STEERPT)
GOLDEN_SHA_V7 = '49158c8e6a32c476927667f1ed6820361ee5231862b2d5df058138abb491f76b'   # a version 7 package (before PARAM, DOCK)
GOLDEN_SHA_V6 = 'abea9110181f0f3412b47c021bb77dae4207ef7d5e56686d927cdf6a95d995e5'   # a version 6 package (before ROUTE)
GOLDEN_SHA_V5 = '7f1c09b678e4e021c3e76459d2a859cd9f8c46719ec8c6764ef4f45bb38ffc97'   # a version 5 package (before AGENT)
GOLDEN_SHA_V4 = '0e53a9faa55a29f183753bfe1a6653ae8063428f53da4d4ecf61c72d0646c216'   # a version 4 package (before FLIGHT, SPLINE)
GOLDEN_SHA_V3 = '8b34a0256082303b4c4472a727a247a1d25bfbad72857914c890029cbc7c6285'   # a version 3 package (before PATH, REGION)
GOLDEN_SHA_V2 = '1f358ed77f778b076d6eef9c69a5b9c99b435f227388915d107dabcf5c4c74d7'   # a version 2 package (before SHIELD..KEYS)
GOLDEN_SHA_V1 = '667d443761fc4ae49793cc6d5d74b2cc80f8bd1a067b20790a204e76f359aae8'   # a version 1 package (before the world table)


class Package(unittest.TestCase):
    def test_round_trip_summary_and_golden(self):
        data = golden()
        package = join.read_package(data)
        s = join.summary(package)
        self.assertEqual((s['tick'], s['slot'], s['save_bytes'], s['md_seq']), (600, 1, 123456, 4242))
        self.assertEqual(s['counts'], {'ids': 2, 'ctx': 2, 'wake': 1, 'wakevec': 1, 'mdheap': 2, 'modules': 2, 'lua': 1, 'health': 2,
                                       'pose': 1, 'shield': 2, 'nav': 1, 'attn': 2, 'keys': 2, 'path': 9, 'region': 2, 'flight': 2,
                                       'spline': 2, 'agent': 2, 'route': 3, 'param': 2, 'dock': 1, 'steer': 1, 'steerpt': 1, 'solver': 1,
                                       'bullet': 2, 'grav': 2, 'gravc': 3, 'gravs': 4, 'splm': 1, 'splme': 1,
                                       'splmseg': 4, 'gravn': 3, 'gravp': 3, 'gravq': 3, 'jobe': 1, 'evqt': 3, 'gravd': 2,
                                       'viewk': 2, 'viewkc': 2, 'viewf': 2, 'viewp': 1, 'shoot': 1, 'attk': 1, 'livef': 1, 'spec': 2, 'navc': 1, 'jobs': 1, 'gone': 2, 'body': 1, 'bupd': 2, 'ldc': 1, 'navt': 1, 'nmesh': 4, 'noct': 4, 'agtt': 2, 'agcb': 1, 'pbnk': 1, 'pend': 2, 'gravb': 2, 'viewl': 2, 'ikv': 2, 'rcnt': 1, 'ecos': 1, 'shpw': 1, 'trdn': 1})
        self.assertEqual(s['version'], 13)
        self.assertEqual(s['save_sha'], bytes(range(32)).hex())
        # The MD heap enters the portable digest in pop order (time, then seq) and without its seq.
        text = join.portable_text(package['sections'])
        self.assertLess(text.index('MD|000000000000dddd'), text.index('MD|000000000000cccc'))
        self.assertNotIn('|17|', text.split('MD|')[1])
        self.assertEqual(hashlib.sha256(data).hexdigest(), package['sha'])
        print('GOLDEN', package['sha'], s['portable'])
        self.assertEqual(package['sha'], GOLDEN_SHA)

    def test_version_1_and_the_world_side_table(self):
        # A saver before the world table wrote version 1 (8 sections): still read, its digest unchanged, no world lines.
        v1 = join.read_package(golden(1))
        self.assertEqual((v1['sha'], v1['header']['version']), (GOLDEN_SHA_V1, 1))
        self.assertNotIn('health', v1['sections'])
        self.assertNotIn('HEALTH|', join.portable_text(v1['sections']))
        # Version 2: one portable line per world section (count and SHA-256 of the records), after the fixture's.
        v2 = join.read_package(golden(2))
        self.assertEqual((v2['sha'], v2['header']['version']), (GOLDEN_SHA_V2, 2))
        self.assertNotIn('nav', v2['sections'])
        lines = join.portable_text(v2['sections']).splitlines()
        self.assertTrue(lines[-2].startswith('HEALTH|2|') and lines[-1].startswith('POSE|1|'))
        self.assertEqual(v2['sections']['health'][1], (0x2003, bits(2500.0), 0, bits(7.083), 0, join.HEALTH_SHIELD, 0))
        self.assertEqual(v2['sections']['pose'][0][:3], (0x2004, 0x2a35000, 1))
        # A saver whose world differs (one shield's changed time, one pose lane) announces another portable digest; the
        # comparison names the objects.
        other = {k: list(v) if isinstance(v, list) else v for k, v in v2['sections'].items()}
        other['health'] = [other['health'][0], (0x2003, bits(2500.0), 0, bits(28.183), 0, join.HEALTH_SHIELD, 0)]
        other['pose'] = [(0x2004, 0x2a35000, 1, *(0x3f800000+i+(i == 5) for i in range(12)))]
        self.assertNotEqual(join.portable_digest(other), v2['sections']['gate']['portable'])
        result = join.compare_packages(v2, {'sections': other})
        self.assertFalse(result['equal'])
        self.assertEqual(result['world']['health'], {'left_only': 0, 'right_only': 0, 'differ': 1, 'samples': ['0000000000002003']})
        self.assertEqual(result['world']['pose']['samples'], ['0000000000002004'])
        # Version 1 with world records, unknown versions: refused by the writer.
        gate = dict(v2['sections']['gate'])
        for kwargs in (dict(version=1, sections={join.SEC_HEALTH: []}), dict(version=2, sections={join.SEC_NAV: []}),
                       dict(version=3, sections={join.SEC_PATH: []}), dict(version=4, sections={join.SEC_FLIGHT: []}),
                       dict(version=5, sections={join.SEC_AGENT: []}), dict(version=6, sections={join.SEC_ROUTE: []}),
                       dict(version=7, sections={join.SEC_PARAM: []}), dict(version=7, sections={join.SEC_DOCK: []}),
                       dict(version=8, sections={join.SEC_STEER: []}), dict(version=8, sections={join.SEC_STEERPT: []}),
                       dict(version=9, sections={join.SEC_SOLVER: []}), dict(version=9, sections={join.SEC_BULLET: []}),
                       dict(version=10, sections={join.SEC_GRAV: []}), dict(version=11, sections={join.SEC_SPLM: []}),
                       dict(version=11, sections={join.SEC_GRAVN: []}), dict(version=12, sections={join.SEC_JOBE: []}),
                       dict(version=14, sections={})):
            with self.subTest(kwargs=kwargs), self.assertRaisesRegex(join.JoinError, 'package_version'):
                join.write_package(gate, kwargs['sections'], version=kwargs['version'])
        # The reader: a world section in a version 1 header (resealed), a version 2 package without them, unsorted records.
        def resealed(data, mutate):
            b = bytearray(data[:-32])
            mutate(b)
            return bytes(b)+hashlib.sha256(bytes(b)).digest()
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(resealed(golden(2), lambda b: b.__setitem__(slice(8, 12), struct.pack('<I', 1))))
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(resealed(golden(), lambda b: b.__setitem__(slice(8, 12), struct.pack('<I', 2))))
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(resealed(golden(1), lambda b: b.__setitem__(slice(8, 12), struct.pack('<I', 2))))
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(resealed(golden(2), lambda b: b.__setitem__(slice(8, 12), struct.pack('<I', 3))))
        unsorted = dict(v2['sections'])
        raw = {join.SEC_IDS: unsorted['ids'], join.SEC_HEALTH: list(reversed(unsorted['health'])), join.SEC_POSE: unsorted['pose']}
        with self.assertRaisesRegex(join.JoinError, 'world_order'):
            join.read_package(join.write_package(gate, raw))

    def test_version_3_navigation_attention_keys_shield(self):
        # Version 3 (report 1.7): four more lines after HEALTH and POSE, each record shape round trips, every record enters
        # the portable digest, the savers' comparison names the differing objects (KEYS by table and key).
        v3 = join.read_package(golden(3))
        self.assertEqual((v3['sha'], v3['header']['version'], v3['header']['sections']), (GOLDEN_SHA_V3, 3, 14))
        lines = join.portable_text(v3['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-6:]], ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS'])
        self.assertEqual([line.split('|')[1] for line in lines[-6:]], ['2', '1', '2', '1', '2', '2'])
        expected = golden_v3()
        for kind, name in ((join.SEC_SHIELD, 'shield'), (join.SEC_NAV, 'nav'), (join.SEC_ATTN, 'attn'), (join.SEC_KEYS, 'keys')):
            self.assertEqual(v3['sections'][name], expected[kind])
        nav = v3['sections']['nav'][0]
        self.assertEqual(nav[4], 1)                        # the cursor's relation: the write bank
        self.assertEqual(nav[9+18], 0x2003)                # the read-role bank's first reference, a portable ID
        self.assertEqual(nav[9+28+18+4], 2)                # the write-role bank's transition count
        other = {k: list(v) if isinstance(v, list) else v for k, v in v3['sections'].items()}
        other['nav'] = [nav[:4]+(0,)+nav[5:]]              # the session's cursor on the read bank
        other['attn'] = [(0x2001, bits(28.1), 2, join.ATTN_DUE), v3['sections']['attn'][1]]
        other['keys'] = [(0x2001, 1, 0)]
        self.assertNotEqual(join.portable_digest(other), v3['sections']['gate']['portable'])
        world = join.compare_packages(v3, {'sections': other})['world']
        self.assertEqual((world['nav']['differ'], world['nav']['samples']), (1, ['0000000000002001']))
        self.assertEqual(world['attn']['samples'], ['0000000000002001'])
        self.assertEqual((world['keys']['left_only'], world['keys']['differ'], world['shield']['differ']), (1, 0, 0))
        # The reader: records out of order, a key of an unknown table, an actor key without its actor.
        gate = dict(v3['sections']['gate'])
        base = {join.SEC_IDS: v3['sections']['ids']}
        for kind, records, reason in ((join.SEC_ATTN, list(reversed(expected[join.SEC_ATTN])), 'world_order'),
                                      (join.SEC_KEYS, list(reversed(expected[join.SEC_KEYS])), 'world_order'),
                                      (join.SEC_KEYS, [(0x2001, 6, 0)], 'keys_shape'),
                                      (join.SEC_KEYS, [(0x2001, 4, 0)], 'keys_shape'),
                                      (join.SEC_KEYS, [(0x2001, 1, 3)], 'keys_shape')):
            with self.subTest(kind=kind, records=records), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(join.write_package(gate, {**base, kind: records}))
        # A version 3 header without one of the four (resealed count and layout): refused.
        data = golden(3)
        nav_at = data.index(struct.pack('<II', join.SEC_NAV, 1))
        head = join.SECTION.unpack_from(data, nav_at)
        cut = bytearray(data[:nav_at]+data[nav_at+join.SECTION.size+head[2]:-32])
        cut[12:16] = struct.pack('<I', 13)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())

    def test_version_4_path_and_region(self):
        # Version 4 (report 1.8): PATH and REGION after the six world lines, each record round trips and enters the portable
        # digest, the savers' comparison names the differing batch positions and damage fields.
        v4 = join.read_package(golden(4))
        self.assertEqual((v4['sha'], v4['header']['version'], v4['header']['sections']), (GOLDEN_SHA_V4, 4, 16))
        lines = join.portable_text(v4['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-8:]], ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS', 'PATH', 'REGION'])
        self.assertEqual([line.split('|')[1] for line in lines[-2:]], ['9', '2'])
        expected = golden_v4()
        self.assertEqual(v4['sections']['path'], expected[join.SEC_PATH])
        self.assertEqual(v4['sections']['region'], expected[join.SEC_REGION])
        self.assertTrue(join.path_shape(expected[join.SEC_PATH]))
        other = {k: list(v) if isinstance(v, list) else v for k, v in v4['sections'].items()}
        path = list(expected[join.SEC_PATH])
        path[2] = (0, join.PATH_BATCH, 0, 0, 0, 2, 1)       # the session's other saver: the cursor on the ship
        other['path'] = path
        other['region'] = [expected[join.SEC_REGION][0][:2]+(bits(28.5),)+expected[join.SEC_REGION][0][3:], expected[join.SEC_REGION][1]]
        self.assertNotEqual(join.portable_digest(other), v4['sections']['gate']['portable'])
        world = join.compare_packages(v4, {'sections': other})['world']
        self.assertEqual((world['path']['differ'], world['path']['samples']), (1, ['1/0/0/0']))
        self.assertEqual((world['region']['differ'], world['region']['samples']), (1, ['0000000000002005']))
        # The reader: every PATH structure fault, REGION out of order or flagged.
        gate = dict(v4['sections']['gate'])
        base = {join.SEC_IDS: v4['sections']['ids']}
        good = expected[join.SEC_PATH]
        cases = (([good[1], good[0]]+good[2:], 'path_shape'),                                  # vectors out of order
                 (good[:2]+good[3:], 'path_shape'),                                            # a batch record missing
                 (good[:5]+[good[6], good[5]]+good[7:], 'path_shape'),                         # members out of order
                 (good[:5]+[(0x2001, join.PATH_MEMBER, 0, 0, 0, 0, join.PATH_LOCAL)]+good[6:], 'path_shape'),   # local with an ID
                 (good[:7]+[(0, join.PATH_MEMBER, 0, 1, 0, 0, 0)]+good[8:], 'path_shape'),      # no ID without the local flag
                 (good[:2]+[(0, join.PATH_BATCH, 0, 0, 2, 2, 1)]+good[3:], 'path_shape'),       # the cursor on the sentinel of a full batch
                 ([(0, join.PATH_MANAGER, 0, 2, 2, 0, 0)]+good[1:], 'path_shape'),             # a round-robin index past the batches
                 (good+[(0x2006, join.PATH_PENDING, 0, 0, 2, 0, 0)], 'path_shape'),            # a gap in the new agents
                 (good[:4]+[(0, join.PATH_BATCH, 1, 0, 0, 1, 0)]+good[5:8]+[(0x2007, join.PATH_MEMBER, 1, 0, 0, 0, 0)]+good[8:], None))
        for records, reason in cases:
            data = join.write_package(gate, {**base, join.SEC_PATH: records})
            if reason is None:
                self.assertEqual(join.read_package(data)['sections']['path'], records)
                continue
            with self.subTest(records=records), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(data)
        for regions, reason in (([expected[join.SEC_REGION][1], expected[join.SEC_REGION][0]], 'world_order'),
                                ([(0x2005, 1, 2, 0, 1)], 'region_shape'), ([(0, 1, 2, 0, 0)], 'region_shape')):
            with self.subTest(regions=regions), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(join.write_package(gate, {**base, join.SEC_REGION: regions}))
        # A version 4 header without REGION (resealed count and layout): refused; a version 3 header with them: refused.
        data = golden(4)
        at = data.index(struct.pack('<II', join.SEC_REGION, 2))
        cut = bytearray(data[:at]+data[at+join.SECTION.size+2*join.REGION.size:-32])
        cut[12:16] = struct.pack('<I', 15)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())
        three = bytearray(data[:-32])
        three[8:12] = struct.pack('<I', 3)
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(bytes(three)+hashlib.sha256(bytes(three)).digest())

    def test_version_5_flight_and_spline(self):
        # Version 5 (report 1.9): FLIGHT and SPLINE after the eight world lines, each record round trips and enters the portable
        # digest, the savers' comparison names the differing model and spline record, the reader refuses every shape fault.
        v5 = join.read_package(golden(5))
        self.assertEqual((v5['sha'], v5['header']['version'], v5['header']['sections']), (GOLDEN_SHA_V5, 5, 18))
        lines = join.portable_text(v5['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-10:]],
                         ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS', 'PATH', 'REGION', 'FLIGHT', 'SPLINE'])
        expected = golden_v5()
        self.assertEqual(v5['sections']['flight'], expected[join.SEC_FLIGHT])
        self.assertEqual(v5['sections']['spline'], expected[join.SEC_SPLINE])
        self.assertTrue(join.flight_shape(expected[join.SEC_FLIGHT], expected[join.SEC_SPLINE]))
        # The other saver: the ship's last step one frame later (the state's +0x88 at offset 0x60), a segment byte apart.
        other = dict(v5['sections'])
        flight = list(expected[join.SEC_FLIGHT])
        state = bytearray(flight[0][5])
        state[0x60] ^= 1
        flight[0] = flight[0][:5]+(bytes(state), 0)
        spline = list(expected[join.SEC_SPLINE])
        spline[0] = spline[0][:3]+(bytes([spline[0][3][0] ^ 1])+spline[0][3][1:],)
        other['flight'], other['spline'] = flight, spline
        self.assertNotEqual(join.portable_digest(other), v5['sections']['gate']['portable'])
        world = join.compare_packages(v5, {'sections': other})['world']
        self.assertEqual((world['flight']['differ'], world['flight']['samples']), (1, ['0000000000002001']))
        self.assertEqual((world['spline']['differ'], world['spline']['samples']), (1, ['0000000000002001/0/0']))
        gate = dict(v5['sections']['gate'])
        base = {join.SEC_IDS: v5['sections']['ids']}
        good_f, good_s = expected[join.SEC_FLIGHT], expected[join.SEC_SPLINE]
        linear = good_f[0]
        cases = (([good_f[1], good_f[0]], good_s, 'world_order'),
                 (good_f, good_s[:1], 'flight_shape'),                                          # a pairs record missing
                 (good_f, good_s+[(0x2004, 0, 0, bytes(160))], 'flight_shape'),                 # a record no model names
                 (good_f, [good_s[1], good_s[0]], 'flight_shape'),                              # out of (part, index) order
                 (good_f, [good_s[0][:3]+(bytes(0x10)+b'\1'+bytes(143),), good_s[1]], 'flight_shape'),   # the vtable bytes set
                 (good_f, [good_s[0], good_s[1][:3]+(bytes(16)+b'\1'+bytes(143),)], 'flight_shape'),     # bytes past the pairs
                 ([linear, good_f[1][:5]+(b'\1'+bytes(203), 0)], good_s, 'flight_shape'),        # another model with state
                 ([linear, (0x2004, join.FLIGHT_LINEAR, join.FLIGHT_OTHER, 0, 0, bytes(204), 0)], good_s, 'flight_shape'),
                 ([linear[:2]+(join.FLIGHT_UNCARRIED,)+linear[3:], good_f[1]], good_s, 'flight_shape'),   # uncarried with a spline
                 ([linear[:6]+(1,), good_f[1]], good_s, 'flight_shape'),                         # the reserved word
                 ([(0x2001, 0x2c4e9c8, 0)+linear[3:], good_f[1]], good_s, 'flight_shape'),       # a linear record of another kind
                 ([linear[:3]+(65,)+linear[4:], good_f[1]], good_s, 'flight_shape'),             # past JOIN_FLIGHT_SEGMENTS
                 ([linear[:2]+(join.FLIGHT_UNCARRIED | join.FLIGHT_WORD, 0, 0)+linear[5:], good_f[1]], [], None))
        for flights, splines, reason in cases:
            data = join.write_package(gate, {**base, join.SEC_FLIGHT: flights, join.SEC_SPLINE: splines}, version=5)
            if reason is None:
                self.assertEqual(join.read_package(data)['sections']['flight'], flights)
                continue
            with self.subTest(flights=flights, splines=splines), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(data)
        # A version 5 header without SPLINE (resealed count and layout): refused; a version 4 header with them: refused.
        data = golden(5)
        at = data.index(struct.pack('<II', join.SEC_SPLINE, 2))
        cut = bytearray(data[:at]+data[at+join.SECTION.size+2*join.SPLINE.size:-32])
        cut[12:16] = struct.pack('<I', 17)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())
        four = bytearray(data[:-32])
        four[8:12] = struct.pack('<I', 4)
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(bytes(four)+hashlib.sha256(bytes(four)).digest())
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package(gate, {join.SEC_FLIGHT: good_f}, version=4)

    def test_version_6_agent(self):
        # Version 6 (report 1.10): AGENT after the ten world lines, one record per FLIGHT record in its order; each record round
        # trips and enters the portable digest, the savers' comparison names the differing Agent, the reader refuses every shape
        # fault and a version 5 header with it.
        v6 = join.read_package(golden(6))
        self.assertEqual((v6['sha'], v6['header']['version'], v6['header']['sections']), (GOLDEN_SHA_V6, 6, 19))
        lines = join.portable_text(v6['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-11:]],
                         ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS', 'PATH', 'REGION', 'FLIGHT', 'SPLINE', 'AGENT'])
        expected = golden_v6()[join.SEC_AGENT]
        self.assertEqual(v6['sections']['agent'], expected)
        self.assertEqual(len(join.AGENT_VALUE_AT), 18)
        self.assertEqual(join.AGENT_VALUE_AT[7], 0x364)
        self.assertEqual(join.AGENT_FLAG_AT[4], 0xf33)
        self.assertTrue(join.agent_shape(v6['sections']['flight'], expected))
        # The other saver: the ship's speed limit apart.
        other = dict(v6['sections'])
        agent = list(expected)
        agent[0] = agent[0][:8]+(f32(2706.0),)+agent[0][9:]
        other['agent'] = agent
        self.assertNotEqual(join.portable_digest(other), v6['sections']['gate']['portable'])
        world = join.compare_packages(v6, {'sections': other})['world']
        self.assertEqual((world['agent']['differ'], world['agent']['samples']), (1, ['0000000000002001']))
        gate = dict(v6['sections']['gate'])
        base = {join.SEC_IDS: v6['sections']['ids'], join.SEC_FLIGHT: v6['sections']['flight'], join.SEC_SPLINE: v6['sections']['spline']}
        ship, turret = expected
        cases = (([turret, ship], 'agent_shape'),                                     # out of FLIGHT's order
                 ([ship], 'agent_shape'),                                             # one missing
                 ([ship, turret, (0x2005,)+turret[1:]], 'agent_shape'),               # one no FLIGHT record names
                 ([ship[:24]+(1,), turret], 'agent_shape'),                           # reserved
                 ([ship, turret[:23]+(2, 0)], 'agent_shape'),                         # formation past 1
                 ([ship[:19]+(bytes(11)+b'\1',)+ship[20:], turret], 'agent_shape'),   # an unused flag byte
                 ([ship, turret], None))
        for agents, reason in cases:
            data = join.write_package(gate, {**base, join.SEC_AGENT: agents}, version=6)
            if reason is None:
                self.assertEqual(join.read_package(data)['sections']['agent'], agents)
                continue
            with self.subTest(agents=agents), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(data)
        # A version 6 header without AGENT (resealed count and layout): refused; a version 5 header with it: refused.
        data = golden(6)
        at = data.index(struct.pack('<II', join.SEC_AGENT, 2))
        cut = bytearray(data[:at]+data[at+join.SECTION.size+2*join.AGENT.size:-32])
        cut[12:16] = struct.pack('<I', 18)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())
        five = bytearray(data[:-32])
        five[8:12] = struct.pack('<I', 5)
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(bytes(five)+hashlib.sha256(bytes(five)).digest())

    def test_version_7_route(self):
        # Version 7 (report 1.11): ROUTE after the eleven world lines, per AGENT record a head and its points; each record round
        # trips and enters the portable digest, the savers' comparison names the differing point, the reader refuses every shape
        # fault and a version 6 header with it.
        v7 = join.read_package(golden(7))
        self.assertEqual((v7['sha'], v7['header']['version'], v7['header']['sections']), (GOLDEN_SHA_V7, 7, 20))
        lines = join.portable_text(v7['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-12:]],
                         ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS', 'PATH', 'REGION', 'FLIGHT', 'SPLINE', 'AGENT', 'ROUTE'])
        self.assertEqual(lines[-1].split('|')[1], '3')
        expected = golden_v7()[join.SEC_ROUTE]
        self.assertEqual(v7['sections']['route'], expected)
        self.assertTrue(join.route_shape(v7['sections']['agent'], expected))
        # The other saver: the waypoint's removal time apart (+0x80, data 0x58).
        other = dict(v7['sections'])
        point = bytearray(expected[1][11])
        point[0x58] ^= 1
        other['route'] = [expected[0], expected[1][:11]+(bytes(point),), expected[2]]
        self.assertNotEqual(join.portable_digest(other), v7['sections']['gate']['portable'])
        world = join.compare_packages(v7, {'sections': other})['world']
        self.assertEqual((world['route']['differ'], world['route']['samples']), (1, ['0000000000002001/1/0/0']))
        gate = dict(v7['sections']['gate'])
        base = {join.SEC_IDS: v7['sections']['ids'], join.SEC_FLIGHT: v7['sections']['flight'], join.SEC_SPLINE: v7['sections']['spline'],
                join.SEC_AGENT: v7['sections']['agent']}
        head, waypoint, turret = expected
        def with_data(record, at, value):
            data = bytearray(record[11])
            data[at] = value
            return record[:11]+(bytes(data),)
        cases = (([turret, head, waypoint], 'route_shape'),                              # out of AGENT's order
                 ([head, waypoint], 'route_shape'),                                      # a head missing
                 ([head, turret], 'route_shape'),                                        # the ship's waypoint missing
                 ([head, waypoint, waypoint, turret], 'route_shape'),                    # one point too many
                 ([head[:5]+(1,)+head[6:], waypoint, turret], 'route_shape'),            # reserved
                 ([head, waypoint[:3]+(1,)+waypoint[4:], turret], 'route_shape'),        # the index
                 ([head, waypoint[:2]+(1,)+waypoint[3:], turret], 'route_shape'),        # the list (a pending one: AGENT says none)
                 ([head, with_data(waypoint, join.ROUTE_PAD, 1), turret], 'route_shape'),   # the padding +0x7f
                 ([head, waypoint[:4]+(0,)+waypoint[5:], turret], 'route_shape'),        # a sub-point's bytes without the flag
                 ([head, waypoint[:4]+(join.ROUTE_SUB | 1,)+waypoint[5:], turret], 'route_shape'),   # unmapped with a reference
                 ([head[:4]+(0,)+head[5:], waypoint, turret], 'route_shape'),            # a word without the linear flag
                 ([head, waypoint, turret[:9]+(5,)+turret[10:]], 'route_shape'),         # unmapped slot 3 with a reference
                 ([with_data(head, 0, 2), waypoint, turret], 'route_shape'),             # lengths not AGENT's
                 ([with_data(head, 20, 1), waypoint, turret], 'route_shape'),            # an unused head byte
                 ([head[:4]+(join.ROUTE_LINEAR | join.ROUTE_OTHER,)+head[5:], turret], None),   # other: no points
                 ([head, waypoint[:4]+(join.ROUTE_SUB | join.ROUTE_CONN,)+waypoint[5:], turret], None),
                 ([head, waypoint, turret], None))
        for routes, reason in cases:
            data = join.write_package(gate, {**base, join.SEC_ROUTE: routes}, version=7)
            if reason is None:
                self.assertEqual(join.read_package(data)['sections']['route'], routes)
                continue
            with self.subTest(routes=routes), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(data)
        # A version 7 header without ROUTE (resealed count and layout): refused; a version 6 header with it: refused.
        data = golden(7)
        at = data.index(struct.pack('<II', join.SEC_ROUTE, 3))
        cut = bytearray(data[:at]+data[at+join.SECTION.size+3*join.ROUTE.size:-32])
        cut[12:16] = struct.pack('<I', 19)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())
        six = bytearray(data[:-32])
        six[8:12] = struct.pack('<I', 6)
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(bytes(six)+hashlib.sha256(bytes(six)).digest())
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package(gate, {join.SEC_ROUTE: expected}, version=6)

    def test_version_8_param_and_dock(self):
        # Version 8 (report 1.12): PARAM and DOCK after the twelve world lines, one PARAM record per AGENT record in its order with
        # three slot blocks, DOCK by portable ID; each record round trips and enters the portable digest, the savers' comparison
        # names the differing Agent and ship, the reader refuses every shape fault and a version 7 header with them.
        v8 = join.read_package(golden(8))
        self.assertEqual((v8['sha'], v8['header']['version'], v8['header']['sections']), (GOLDEN_SHA_V8, 8, 22))
        lines = join.portable_text(v8['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-14:]],
                         ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS', 'PATH', 'REGION', 'FLIGHT', 'SPLINE', 'AGENT', 'ROUTE', 'PARAM',
                          'DOCK'])
        self.assertEqual([line.split('|')[1] for line in lines[-2:]], ['2', '1'])
        expected = golden_v8()
        self.assertEqual(v8['sections']['param'], expected[join.SEC_PARAM])
        self.assertEqual(v8['sections']['dock'], expected[join.SEC_DOCK])
        self.assertTrue(join.param_shape(v8['sections']['agent'], expected[join.SEC_PARAM]))
        self.assertTrue(join.dock_shape(expected[join.SEC_DOCK]))
        ship, turret = expected[join.SEC_PARAM]
        dock, = expected[join.SEC_DOCK]
        self.assertEqual(struct.unpack_from('<3f', ship[5], join.PARAM_TURN), (0.5, 0.25, 0.125))   # the own copy's turn rates
        # The other saver: the own copy's yaw rate apart, the dock controller's start time apart.
        other = dict(v8['sections'])
        own = bytearray(ship[5])
        own[join.PARAM_TURN] ^= 1
        other['param'] = [ship[:5]+(bytes(own),), turret]
        other['dock'] = [dock[:3]+(bits(13.95),)+dock[4:]]
        self.assertNotEqual(join.portable_digest(other), v8['sections']['gate']['portable'])
        world = join.compare_packages(v8, {'sections': other})['world']
        self.assertEqual((world['param']['differ'], world['param']['samples']), (1, ['0000000000002001']))
        self.assertEqual((world['dock']['differ'], world['dock']['samples']), (1, ['0000000000002002']))
        gate = dict(v8['sections']['gate'])
        base = {join.SEC_IDS: v8['sections']['ids'], join.SEC_FLIGHT: v8['sections']['flight'], join.SEC_SPLINE: v8['sections']['spline'],
                join.SEC_AGENT: v8['sections']['agent'], join.SEC_ROUTE: v8['sections']['route']}
        def with_block(record, role, at, value):
            block = bytearray(record[3+role])
            block[at] = value
            return record[:3+role]+(bytes(block),)+record[4+role:]
        undock = (dock[0], join.UNDOCK_VT)+dock[2:]
        undock_ok = (dock[0], join.UNDOCK_VT)+dock[2:5]+(b'\1\0', 0)+tuple(dock[7:7+24])+(0,)*18
        cases = (([turret, ship], [dock], 'param_shape'),                                  # out of AGENT's order
                 ([ship], [dock], 'param_shape'),                                          # one missing
                 ([ship[:2]+(1,)+ship[3:], turret], [dock], 'param_shape'),                # reserved
                 ([ship[:1]+(9,)+ship[2:], turret], [dock], 'param_shape'),                # an unused flag
                 ([with_block(ship, 1, join.PARAMB_PAD, 1), turret], [dock], 'param_shape'),   # a block's pad
                 ([with_block(ship, 0, join.PARAMB_ID, 5), turret], [dock], 'param_shape'),    # an ID beside its unmapped bit
                 ([ship, turret], [dock, dock], 'world_order'),                            # one ship twice
                 ([ship, turret], [dock[:1]+(0x2c51158,)+dock[2:]], 'dock_shape'),         # another controller
                 ([ship, turret], [dock[:2]+(1,)+dock[3:]], 'dock_shape'),                 # flags
                 ([ship, turret], [dock[:5]+(b'\2\0',)+dock[6:]], 'dock_shape'),          # an option past 1
                 ([ship, turret], [undock], 'dock_shape'),                                 # an undock record with a dock's vectors
                 ([ship, turret], [undock_ok], None),
                 ([ship, turret], [], None),
                 ([ship, turret], [dock], None))
        for params, docks, reason in cases:
            data = join.write_package(gate, {**base, join.SEC_PARAM: params, join.SEC_DOCK: docks}, version=8)
            if reason is None:
                package = join.read_package(data)
                self.assertEqual((package['sections']['param'], package['sections']['dock']), (params, docks))
                continue
            with self.subTest(params=len(params), docks=docks[:1], reason=reason), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(data)
        # A version 8 header without DOCK (resealed count and layout): refused; a version 7 header with them: refused.
        data = golden(8)
        at = data.index(struct.pack('<II', join.SEC_DOCK, 1))
        cut = bytearray(data[:at]+data[at+join.SECTION.size+join.DOCK.size:-32])
        cut[12:16] = struct.pack('<I', 21)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())
        seven = bytearray(data[:-32])
        seven[8:12] = struct.pack('<I', 7)
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(bytes(seven)+hashlib.sha256(bytes(seven)).digest())
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package(gate, {join.SEC_PARAM: expected[join.SEC_PARAM]}, version=7)

    def test_version_10_solver_and_bullet(self):
        # Version 10 (report 1.14): SOLVER and BULLET after STEERPT; the digest takes one line for SOLVER, over (portable, kind) only, and none
        # for BULLET; each record round trips, the savers' comparison names the qwords and object offsets apart without refusing the join,
        # the reader refuses every shape fault and a version 9 header with them.
        v10 = join.read_package(golden(10))
        self.assertEqual((v10['sha'], v10['header']['version'], v10['header']['sections']), (GOLDEN_SHA_V10, 10, 26))
        lines = join.portable_text(v10['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-3:]], ['STEER', 'STEERPT', 'SOLVER'])
        self.assertEqual(lines[-1].split('|')[1], '1')
        self.assertEqual(lines[-1].split('|')[2], hashlib.sha256(struct.pack('<QI', 0x2004, join.STEER_VT)).hexdigest())
        expected = golden_v10()
        self.assertEqual(v10['sections']['solver'], expected[join.SEC_SOLVER])
        self.assertEqual(v10['sections']['bullet'], expected[join.SEC_BULLET])
        solver, = expected[join.SEC_SOLVER]
        first, second = expected[join.SEC_BULLET]
        # The other saver: bytes of the object apart: the same digest, the comparison names them.
        data = bytearray(solver[6])
        data[0x18] ^= 1
        data[0x100] ^= 1
        other = dict(v10['sections'])
        other['solver'] = [solver[:6]+(bytes(data),)]
        moved = bytearray(first[7])
        moved[0x268] ^= 1
        other['bullet'] = [first[:7]+(bytes(moved), first[8]), second]
        self.assertEqual(join.portable_digest(other), join.portable_digest(v10['sections']))
        world = join.world_differences(v10['sections'], other)
        self.assertEqual((world['solver']['differ'], world['solver']['samples']), (1, ['0000000000002004']))
        self.assertEqual(sorted(world['solver']['offsets']), ['q+0x100:1', 'q+0x18:1'])
        self.assertEqual((world['bullet']['differ'], world['bullet']['samples'], world['bullet']['offsets']),
                         (1, ['0000000000002003/3'], ['bullet+0x268:1']))
        # A different model kind changes the digest.
        other['solver'] = [solver[:2]+(join.STEER_VT+8,)+solver[3:]]
        self.assertNotEqual(join.portable_digest(other), join.portable_digest(v10['sections']))
        # Shape faults.
        listed = struct.pack('<3I', (0x16a0//8) << 1, (0x6a00//8) << 1 | 1, 1)+bytes(4*join.SOLVER_POINTERS-12)
        swapped = struct.pack('<2I', (0x6a00//8) << 1 | 1, (0x16a0//8) << 1)+bytes(4*join.SOLVER_POINTERS-8)
        far = bytearray(solver[6])
        far[0x16a0:0x16a8] = struct.pack('<Q', join.SOLVER_BYTES)
        for bad in ([solver, solver], [solver[:4]+(1,)+solver[5:]], [solver[:1]+(4,)+solver[2:]], [solver[:1]+(3,)+solver[2:]],
                    [solver[:5]+(listed,)+solver[6:]], [solver[:5]+(swapped,)+solver[6:]], [solver[:6]+(bytes(far),)]):
            self.assertFalse(join.solver_shape(bad))
        self.assertTrue(join.solver_shape([solver[:1]+(3, solver[2], 0, 0, bytes(4*join.SOLVER_POINTERS), solver[6])]))
        for bad in ([first, first], [second, first], [first, second[:3]+(2,)+second[4:]], [first, second[:1]+(1,)+second[2:]],
                    [first[:6]+(first[6][:40]+b'\1'+first[6][41:],)+first[7:]], [first, second[:6]+(b'x'+bytes(join.BULLET_MACRO-1),)+second[7:]],
                    [first[:4]+(8,)+first[5:]], [first, second[:5]+(1,)+second[6:]]):
            self.assertFalse(join.bullet_shape(bad))
        self.assertTrue(join.bullet_shape([first, second]))
        # A version 9 header may not carry them.
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package({name: 0 for name in join.GATE_FIELDS}, {join.SEC_SOLVER: []}, version=9)

    def test_version_11_grav(self):
        # Version 11 (report 1.15): GRAV, GRAVC and GRAVS after BULLET; the digest takes one line, which units, their kind and their
        # list lengths; the bytes, contacts and GRAVS only in the savers' comparison; the reader refuses every shape fault and a
        # version 10 header with them.
        v11 = join.read_package(golden(11))
        self.assertEqual((v11['sha'], v11['header']['version'], v11['header']['sections']), (GOLDEN_SHA_V11, 11, 29))
        lines = join.portable_text(v11['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-2:]], ['SOLVER', 'GRAV'])
        expected = golden_v11()
        self.assertEqual(lines[-1].split('|')[1:4], ['2', '3', '4'])
        self.assertEqual(lines[-1].split('|')[4], hashlib.sha256(struct.pack('<QI4I', 0x2004, join.GRAV_ACTIVE_VT, 2, 1, 0, 0) +
                                                                  struct.pack('<QI4I', 0x2005, join.GRAV_PASSIVE_VT, 0, 0, 0, 0)).hexdigest())
        for kind, name in ((join.SEC_GRAV, 'grav'), (join.SEC_GRAVC, 'gravc'), (join.SEC_GRAVS, 'gravs')):
            self.assertEqual(v11['sections'][name], expected[kind])
        active, passive = expected[join.SEC_GRAV]
        # The other saver: unit bytes and a contact apart: the same digest, the comparison names them.
        unit = bytearray(active[7])
        unit[0x50] ^= 1
        other = dict(v11['sections'])
        other['grav'] = [active[:7]+(bytes(unit),), passive]
        other['gravc'] = [expected[join.SEC_GRAVC][0][:3]+(0x2002,)+expected[join.SEC_GRAVC][0][4:]]+expected[join.SEC_GRAVC][1:]
        self.assertEqual(join.portable_digest(other), join.portable_digest(v11['sections']))
        world = join.world_differences(v11['sections'], other)
        self.assertEqual((world['grav']['differ'], world['grav']['offsets']), (1, ['unit+0x50:1']))
        self.assertEqual((world['gravc']['differ'], world['gravc']['samples']), (1, ['0000000000002004/0/0']))
        # A list length changes the digest.
        other['grav'] = [active[:3]+(3,)+active[4:], passive]
        self.assertNotEqual(join.portable_digest(other), join.portable_digest(v11['sections']))
        # Shape faults.
        g, c, z = expected[join.SEC_GRAV], expected[join.SEC_GRAVC], expected[join.SEC_GRAVS]
        self.assertTrue(join.grav_shape(g, c, z))
        for bad in ((g, c[:2], z), (g, [c[0], c[1][:4]+(0, 0), c[2]], z), ([g[0], passive[:3]+(1,)+passive[4:]], c, z),
                    ([g[0], g[0]], c, z), (g, c, [z[0][:4]+(join.GRAVS_SEEN, 0), z[1], z[2]]),
                    (g, c, [z[0], z[1], z[2][:2]+(join.GRAVS_PENDING, 0)+z[2][4:]]), (g, c, [z[0], z[1][:1]+(5,)+z[1][2:], z[2]]),
                    ([active[:2]+(0,)+active[3:], passive], c, z), (g, c, z[:3]+[z[3][:1]+(40,)+z[3][2:]]), (g, c, z+[z[3]]),
                    (g, c, [z[3]]+z[:3])):
            self.assertFalse(join.grav_shape(*bad))
        # A version 10 header may not carry them.
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package({name: 0 for name in join.GATE_FIELDS}, {join.SEC_GRAV: []}, version=10)

    def test_version_12_splm_and_gravn(self):
        # Version 12 (report 1.16): SPLM, SPLME, SPLMSEG, GRAVN and GRAVP after GRAVS; the digest takes SPLM's counts, GRAVN whole and
        # GRAVP's keys; the reader refuses every shape fault and a version 11 header with them.
        v12 = join.read_package(golden(12))
        self.assertEqual((v12['sha'], v12['header']['version'], v12['header']['sections']), (GOLDEN_SHA_V12, 12, 35))
        lines = join.portable_text(v12['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-5:]], ['GRAV', 'SPLM', 'GRAVN', 'GRAVP', 'GRAVQ'])
        self.assertEqual(lines[-4].split('|')[1:4], ['1', '1', '4'])
        self.assertNotIn('jobe', v12['sections'])
        expected = golden_v12()
        for kind, name in ((join.SEC_SPLM, 'splm'), (join.SEC_SPLME, 'splme'), (join.SEC_SPLMSEG, 'splmseg'), (join.SEC_GRAVN, 'gravn'),
                           (join.SEC_GRAVP, 'gravp')):
            self.assertEqual(v12['sections'][name], expected[kind])
        p = expected[join.SEC_GRAVP]
        self.assertTrue(join.gravp_shape(p))
        for bad in ([p[1], p[0], p[2]], [p[0], p[1][:3]+(b'\x01'+bytes(0x3f),), p[2]], [p[0], (0x2003, 2, 0, bytes(0x40)), p[2]],
                    [p[0], p[1], p[2][:2]+(2,)+p[2][3:]], [(0,)+p[0][1:]]):
            self.assertFalse(join.gravp_shape(bad))
        other = dict(v12['sections'])          # a pose's bytes stay out of the digest; which views hold one is in it
        other['gravp'] = [p[0][:3]+(bytes(0x40),)]+p[1:]
        self.assertEqual(join.portable_digest(other), join.portable_digest(v12['sections']))
        other['gravp'] = p[:2]
        self.assertNotEqual(join.portable_digest(other), join.portable_digest(v12['sections']))
        q = expected[join.SEC_GRAVQ]
        self.assertEqual(v12['sections']['gravq'], q)
        self.assertTrue(join.gravq_shape(q))
        for bad in ([q[1], q[0], q[2]], [q[0], (0x2004,)+q[1][1:], q[2]], [q[0], q[1], q[2][:3]+(4,)+q[2][4:]], [(0,)+q[0][1:]]):
            self.assertFalse(join.gravq_shape(bad))
        other = dict(v12['sections'])          # the post times stay out of the digest; the order and the sources are in it
        other['gravq'] = [q[0][:5]+(0,)+q[0][6:]]+q[1:]
        self.assertEqual(join.portable_digest(other), join.portable_digest(v12['sections']))
        other['gravq'] = q[:2]
        self.assertNotEqual(join.portable_digest(other), join.portable_digest(v12['sections']))
        m, e, g, n = (expected[k] for k in (join.SEC_SPLM, join.SEC_SPLME, join.SEC_SPLMSEG, join.SEC_GRAVN))
        self.assertTrue(join.splm_shape(m, e, g) and join.gravn_shape(n))
        # The model's bytes stay out of the digest; a count or a GRAVN record is in it.
        other = dict(v12['sections'])
        other['splm'] = [m[0][:11]+(bytes(join.SPLM_BYTES),)]
        self.assertEqual(join.portable_digest(other), join.portable_digest(v12['sections']))
        other['gravn'] = n[:2]
        self.assertNotEqual(join.portable_digest(other), join.portable_digest(v12['sections']))
        for bad in ((m, [], g), (m, e, g[:3]), (m, e, [g[1], g[0]]+g[2:]), ([m[0][:2]+(2,)+m[0][3:]], e, g),
                    ([m[0][:1]+(1,)+m[0][2:]], e, g), (m, [e[0][:7]+(b'x'+e[0][7][1:],)+e[0][8:]], g),
                    (m, e, g[:1]+[(0x2004, 1, 0, bytes(range(1, 18))+bytes(0xa0-17))]+g[2:])):
            self.assertFalse(join.splm_shape(*bad))
        for bad in ([n[1], n[0], n[2]], [n[0], n[0]], [n[0], n[1], n[2][:2]+(5,)+n[2][3:]], [n[0], n[1], (0x2006, 0x2007, 0, 0, 0)],
                    [n[0], n[1], n[2], (0x2006, 0x2007, 0x2008, 0, 0)]):
            self.assertFalse(join.gravn_shape(bad))
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package({name: 0 for name in join.GATE_FIELDS}, {join.SEC_SPLM: []}, version=11)

    def test_version_13_jobe(self):
        # Version 13 (report 1.17): JOBE after GRAVQ, in the digest whole; the reader refuses its shape faults and a version 12 header
        # with it.
        v13 = join.read_package(golden())
        self.assertEqual((v13['sha'], v13['header']['version'], v13['header']['sections']), (GOLDEN_SHA, 13, 66))
        self.assertEqual(join.portable_text(v13['sections']).splitlines()[-1].split('|')[:2], ['JOBE', '1'])   # EVQT: not in the digest
        other = dict(v13['sections']);other['evqt'] = []
        self.assertEqual(join.portable_digest(other), join.portable_digest(v13['sections']))
        ev = golden_v13()[join.SEC_EVQT]
        self.assertEqual(v13['sections']['evqt'], ev)
        self.assertTrue(join.evqt_shape(ev) and join.evqt_shape([]))
        for bad in ([ev[1], ev[0], ev[2]], [ev[0], ev[2], ev[1]], [ev[0][:4]+(0,)+ev[0][5:]]+ev[1:], [ev[0], ev[1][:4]+(2,)+ev[1][5:], ev[2]]):
            self.assertFalse(join.evqt_shape(bad))
        d = golden_v13()[join.SEC_GRAVD]
        self.assertEqual(v13['sections']['gravd'], d)
        self.assertTrue(join.gravd_shape(d) and join.gravd_shape([]))
        for bad in ([d[1], d[0]], [d[0], d[0]], [d[0][:3]+(1,)], [d[0], d[1][:3]+(0,)], [d[0], (0x2001, 0x2003, 2, 0)]):
            self.assertFalse(join.gravd_shape(bad))
        k, kc = golden_v13()[join.SEC_VIEWK], golden_v13()[join.SEC_VIEWKC]
        self.assertEqual((v13['sections']['viewk'], v13['sections']['viewkc']), (k, kc))
        self.assertTrue(join.viewk_shape(k, kc) and join.viewk_shape([], []))
        for bad_k, bad_kc in (([k[1], k[0]], kc), (k, kc[:1]), (k, [kc[1], kc[0]]), ([k[0], k[1][:2]+(0,)+k[1][3:]], kc),
                              ([k[0][:2]+(join.VIEWK_UNREAD,)+k[0][3:], k[1]], kc), ([k[0], (0x2002, 2)+k[1][2:]], kc)):
            self.assertFalse(join.viewk_shape(bad_k, bad_kc))
        j = golden_v13()[join.SEC_JOBE]
        self.assertEqual(v13['sections']['jobe'], j)
        self.assertTrue(join.jobe_shape(j) and join.jobe_shape([]))
        for bad in (j+j, [j[0][:1]+(8,)+j[0][2:]], [j[0][:2]+(12,)+j[0][3:]], [j[0][:3]+(b'xenon miner_1'+bytes(join.JOBE_CURSOR-13),)]):
            self.assertFalse(join.jobe_shape(bad))
        other = dict(v13['sections'])          # the schedule is in the digest whole
        other['jobe'] = [(0x4037800000000000,)+j[0][1:]]
        self.assertNotEqual(join.portable_digest(other), join.portable_digest(v13['sections']))
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package({name: 0 for name in join.GATE_FIELDS}, {join.SEC_JOBE: []}, version=12)

    def test_version_9_steer(self):
        # Version 9 (report 1.13): STEER and STEERPT after the fourteen world lines, one STEER record per steering model of FLIGHT
        # in its order, its path points in STEERPT; each record round trips and enters the portable digest, the savers' comparison
        # names the differing model and the object offsets of the bytes apart, the reader refuses every shape fault and a version
        # 8 header with them.
        v9 = join.read_package(golden(9))
        self.assertEqual((v9['sha'], v9['header']['version'], v9['header']['sections']), (GOLDEN_SHA_V9, 9, 24))
        lines = join.portable_text(v9['sections']).splitlines()
        self.assertEqual([line.split('|')[0] for line in lines[-16:]],
                         ['HEALTH', 'POSE', 'SHIELD', 'NAV', 'ATTN', 'KEYS', 'PATH', 'REGION', 'FLIGHT', 'SPLINE', 'AGENT', 'ROUTE', 'PARAM',
                          'DOCK', 'STEER', 'STEERPT'])
        self.assertEqual([line.split('|')[1] for line in lines[-2:]], ['1', '1'])
        expected = golden_v9()
        self.assertEqual(v9['sections']['steer'], expected[join.SEC_STEER])
        self.assertEqual(v9['sections']['steerpt'], expected[join.SEC_STEERPT])
        self.assertTrue(join.steer_shape(v9['sections']['flight'], expected[join.SEC_STEER], expected[join.SEC_STEERPT]))
        # The carried bytes' object offsets: the target's x, y, z lanes first (no w lane +0x2c), the radius and the draws at their place.
        offsets = join.steer_offsets(join.STEER_RANGES)
        self.assertEqual((offsets[:13], len(offsets)), ([0x20+i for i in range(12)]+[0x30], 569))
        self.assertNotIn(0x2c, offsets)
        self.assertIn(0x33c, offsets)
        for gap in (0x18, 0x60, 0x70, 0x80, 0x88, 0x90, 0xb4, 0x118, 0x1c8, 0x274, 0x300, 0x310, 0x31c, 0x320, 0x344, 0x360, 0x381, 0x388, 0x398):
            self.assertNotIn(gap, offsets)
        # The other saver: one byte of the model's draw +0x33c and of the chase behaviour's +0x84 apart; the comparison names them.
        steer, = expected[join.SEC_STEER]
        model, chase = bytearray(steer[10]), bytearray(steer[11])
        model[offsets.index(0x33c)] ^= 1
        chase[join.steer_offsets(join.STEER_CLASSES[0x2c4e840]).index(0x84)] ^= 1
        other = dict(v9['sections'])
        other['steer'] = [steer[:10]+(bytes(model), bytes(chase), steer[12])]
        self.assertNotEqual(join.portable_digest(other), v9['sections']['gate']['portable'])
        world = join.compare_packages(v9, {'sections': other})['world']
        self.assertEqual((world['steer']['differ'], world['steer']['samples']), (1, ['0000000000002004']))
        self.assertEqual(world['steer']['offsets'], ['model+0x33c:1', 'b0+0x84:1'])
        self.assertEqual(world['steerpt']['differ'], 0)
        gate = dict(v9['sections']['gate'])
        base = {join.SEC_IDS: v9['sections']['ids'], join.SEC_FLIGHT: v9['sections']['flight'], join.SEC_SPLINE: v9['sections']['spline'],
                join.SEC_AGENT: v9['sections']['agent'], join.SEC_ROUTE: v9['sections']['route'], join.SEC_PARAM: v9['sections']['param'],
                join.SEC_DOCK: v9['sections']['dock']}
        point, = expected[join.SEC_STEERPT]
        def with_state(record, slot, at, value):
            state = bytearray(record[11+slot])
            state[at] = value
            return record[:11+slot]+(bytes(state),)+record[12+slot:]
        no_points = steer[:7]+(0,)+steer[8:]
        cases = (([], [point], 'steer_shape'),                                                     # the steering model's record missing
                 ([steer, steer], [point], 'world_order'),                                         # twice
                 ([steer[:9]+(1,)+steer[10:]], [point], 'steer_shape'),                            # reserved
                 ([steer[:1]+(64,)+steer[2:]], [point], 'steer_shape'),                            # an unused flag
                 ([steer[:4]+(0x2003,)+steer[5:]], [point], 'steer_shape'),                        # ref 1 beside its unmapped bit
                 ([steer[:10]+(bytes(569)+b'\1'+bytes(22),)+steer[11:]], [point], 'steer_shape'),   # past the model's carried bytes
                 ([with_state(steer, 1, 3, 1)], [point], 'steer_shape'),                           # past the generic class's bytes
                 ([steer[:6]+(0x2c4ee80,)+steer[7:]], [point], 'steer_shape'),                     # an unknown class, not flagged
                 ([steer[:1]+(2 | join.STEER_JOB | join.STEER_UNCARRIED1,)+steer[2:]], [point], 'steer_shape'),   # flagged, its class known
                 ([steer], [], 'steer_shape'),                                                     # its point missing
                 ([steer], [point[:2]+(1,)+point[3:]], 'steer_shape'),                             # a point of another list
                 ([steer], [point[:3]+(1,)+point[4:]], 'steer_shape'),                             # its index
                 ([no_points], [point], 'steer_shape'),                                            # a point the record does not name
                 ([steer[:1]+(2 | join.STEER_JOB | join.STEER_POINTS,)+steer[2:]], [point], 'steer_shape'),   # flagged: no points
                 ([steer[:1]+(2 | join.STEER_JOB | join.STEER_POINTS,)+steer[2:]], [], None),
                 ([steer[:1]+(2 | join.STEER_JOB | join.STEER_UNCARRIED1,)+steer[2:6]+(0x2c4ee80,)+steer[7:12]+(bytes(join.STEER_STATE),)],
                  [point], None),                                                                  # an unknown default class, flagged, state 0
                 ([no_points], [], None),
                 ([steer], [point], None))
        for steers, points, reason in cases:
            data = join.write_package(gate, {**base, join.SEC_STEER: steers, join.SEC_STEERPT: points})
            if reason is None:
                package = join.read_package(data)
                self.assertEqual((package['sections']['steer'], package['sections']['steerpt']), (steers, points))
                continue
            with self.subTest(reason=reason, steers=len(steers), points=len(points)), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(data)
        # A version 9 header without STEERPT (resealed count and layout): refused; a version 8 header with them: refused.
        data = golden()
        at = data.index(struct.pack('<II', join.SEC_STEERPT, 1))
        cut = bytearray(data[:at]+data[at+join.SECTION.size+join.ROUTE.size:-32])
        cut[12:16] = struct.pack('<I', 23)
        with self.assertRaisesRegex(join.JoinError, 'package_layout'):
            join.read_package(bytes(cut)+hashlib.sha256(bytes(cut)).digest())
        eight = bytearray(data[:-32])
        eight[8:12] = struct.pack('<I', 8)
        with self.assertRaisesRegex(join.JoinError, 'section_head'):
            join.read_package(bytes(eight)+hashlib.sha256(bytes(eight)).digest())
        with self.assertRaisesRegex(join.JoinError, 'package_version'):
            join.write_package(gate, {join.SEC_STEER: expected[join.SEC_STEER]}, version=8)

    def test_every_hash_and_shape_is_checked(self):
        data = bytearray(golden())
        with self.assertRaisesRegex(join.JoinError, 'package_sha'):
            join.read_package(bytes(data[:-1])+b'\0')
        def resealed(mutate):
            b = bytearray(data[:-32])
            mutate(b)
            return bytes(b)+hashlib.sha256(bytes(b)).digest()
        at = join.HEADER.size+join.SECTION.size+join.GATE.size     # the first section after GATE (ids) head
        payload = at+join.SECTION.size
        cases = [(lambda b: b.__setitem__(0, 0x41), 'package_magic'),
                 (lambda b: b.__setitem__(payload, b[payload] ^ 1), 'ids_sha'),
                 (lambda b: b.__setitem__(16, 0x59), 'gate_header'),
                 (lambda b: b.__setitem__(at+4, 3), 'ids_shape|ids_sha')]
        for mutate, reason in cases:
            with self.subTest(reason=reason), self.assertRaisesRegex(join.JoinError, reason):
                join.read_package(resealed(mutate))

    def test_flight_generations(self):
        # Each fixture actor's flight sample init generation at T rides the GATE record's former padding (j1-120: steering=1 on
        # the session's live fixture actors, 0 on the joiner); a package without them reads as zeros, as before.
        package = join.read_package(golden())
        self.assertEqual(package['sections']['gate']['flight'], [0]*12)
        gate = dict(package['sections']['gate'], flight=[1]*7+[0]+[1]*4)
        sections = {k: v for k, v in package['sections'].items() if k != 'gate'}
        raw = {next(kind for kind, name in join.SECTION_NAMES.items() if name == k): v for k, v in sections.items()}
        again = join.read_package(join.write_package(gate, raw, save_sha=bytes(range(32)), save_bytes=123456))
        self.assertEqual(again['sections']['gate']['flight'], [1]*7+[0]+[1]*4)
        with self.assertRaises(join.JoinError):
            join.write_package(dict(gate, flight=[256]+[0]*11), raw, save_sha=bytes(range(32)), save_bytes=123456)

    def test_fixture_views(self):
        # The session's view of each snapshot fixture actor at T rides the GATE record (j1-9: actor 8 a wreck with its
        # drift controller, view 2) and enters the portable digest; a package without a fixture keeps its old digest.
        package = join.read_package(golden())
        self.assertEqual(package['sections']['gate']['fixture'], '111111121111')
        self.assertEqual(join.summary(package)['fixture'], '111111121111')
        self.assertEqual(join.portable_text(package['sections']).splitlines()[-24], 'FIXTURE|111111121111')   # then the world lines
        sections = dict(package['sections'])
        sections['gate'] = dict(sections['gate'], fixture='111111111111')
        self.assertNotEqual(join.portable_digest(sections), package['sections']['gate']['portable'])
        self.assertFalse(join.compare_packages(package, {'sections': sections})['equal'])
        sections['gate'] = dict(sections['gate'], fixture=join.NO_FIXTURE)
        self.assertNotIn('FIXTURE', join.portable_text(sections))
        gate = dict(package['sections']['gate'])
        raw = {join.SEC_IDS: [(0x1001, 0x2001, 0, 0)]}
        for bad in ('11111112111', '111111161111', 'x11111121111'):
            with self.subTest(bad=bad), self.assertRaisesRegex(join.JoinError, 'gate_fixture'):
                join.write_package(dict(gate, fixture=bad), raw)
        # A view byte past the last view (resealed: GATE's hash and the trailer) is refused by the reader.
        data = bytearray(golden()[:-32])
        at = join.HEADER.size+join.SECTION.size     # the GATE payload
        offset = join.GATE.size-28-12+7             # actor 8's view byte (fixture[12] before the 28 pad bytes)
        data[at+offset] = 6
        data[join.HEADER.size+16:join.HEADER.size+48] = hashlib.sha256(bytes(data[at:at+join.GATE.size])).digest()
        with self.assertRaisesRegex(join.JoinError, 'gate_fixture'):
            join.read_package(bytes(data)+hashlib.sha256(bytes(data)).digest())

    def test_portable_digest_detects_a_different_saver(self):
        a = join.read_package(golden())
        sections = {k: list(v) if isinstance(v, list) else v for k, v in a['sections'].items()}
        sections['wake'] = [(0x2001, 0xaaaa, bits(27.6), 0, 3, 2, 0)]
        self.assertNotEqual(join.portable_digest(sections), a['sections']['gate']['portable'])
        other = {'sections': sections}
        result = join.compare_packages(a, other)
        self.assertFalse(result['equal'])
        self.assertTrue(any(line.startswith('WAKE') for line in result['left_only']))


class Relay(unittest.TestCase):
    def run_join(self, k=0, ticks=400, rounds=380, joiner_steps=3, at=40, boot_after=10, **options):
        net = Network(k=k, ticks=ticks, **options)
        for _ in range(at):
            net.round()
        self.assertTrue(net.relay.released >= at-2-k, net.relay.released)
        answer = net.request()
        self.assertEqual(answer[0], 'NGJOINSTATE')
        self.assertEqual(answer[5], 'queued')
        save_tick = int(answer[6])
        while net.relay.join.state == 'queued':
            net.round()
        self.assertEqual(net.relay.join.state, 'saved', net.notices)
        for _ in range(boot_after):                          # the joiner loads while the session runs on
            net.round()
        net.boot_joiner()
        for _ in range(rounds):
            net.round(joiner_steps=joiner_steps)
            if net.relay.join.state in ('joined', 'failed') and net.relay.released > net.relay.join.handover+40:
                break
        return net, save_tick

    def assert_joined(self, net, save_tick):
        j = net.relay.join
        self.assertEqual(j.state, 'joined', (j.failure, net.notices[-5:]))
        self.assertIsNone(net.relay.failed)
        self.assertNotIn(3, net.relay.absent)
        joiner, live = net.joiner, net.live[1]
        # The joiner applied exactly the permits of every tick after T that slot 1 applied.
        theirs = {t: b for t, b in live.applied if t > save_tick}
        mine = dict(joiner.applied)
        common = sorted(set(theirs) & set(mine))
        self.assertGreater(len(common), 30)
        self.assertEqual([mine[t] for t in common], [theirs[t] for t in common])
        self.assertEqual(joiner.handover, j.handover)
        self.assertEqual(live.handover, j.handover)
        self.assertEqual(hashlib.sha256(joiner.state).hexdigest(), hashlib.sha256(live.state).hexdigest()) if joiner.tick == live.tick else None
        # The relay journal holds slot 3's stand-ins before H and the joiner's own rows from H on.
        states = net.relay.states
        self.assertTrue(states[j.handover-1][3].get('standin'))
        self.assertFalse(states[j.handover][3].get('standin', False))
        self.assertIn(f'NATIVE_GATE JOIN slot=3 joined tick={j.handover}', ' '.join(net.notices))

    def test_protocol_3_hot_join(self):
        net, save_tick = self.run_join()
        self.assert_joined(net, save_tick)
        # Both live clients saved and announced the same portable digest.
        self.assertEqual(sorted(net.relay.join.saved), [1, 2])

    def test_protocol_4_hot_join(self):
        net, save_tick = self.run_join(k=2)
        self.assert_joined(net, save_tick)

    def test_inputs_flow_after_the_handover(self):
        net, save_tick = self.run_join(rounds=0)
        for client in list(net.live.values())+[net.joiner]:
            for t in range(0, 800):
                client.inputs[t] = join.mux.encode({'hits': '01'+'12'+'00'+f'{t % 256:02x}00'+f'{client.slot:016x}'+'d56a170000000000'+'f8ffff7f0800'})
        for _ in range(380):
            net.round(joiner_steps=3)
        self.assert_joined(net, save_tick)

    def test_a_diverged_joiner_is_stopped_and_the_session_goes_on(self):
        net = Network()
        for _ in range(40):
            net.round()
        net.request()
        while net.relay.join.state == 'queued':
            net.round()
        net.boot_joiner()
        net.joiner.state = b'not the saved world'
        for _ in range(60):
            net.round(joiner_steps=2)
        j = net.relay.join
        self.assertEqual(j.state, 'failed')
        self.assertTrue(j.failure.startswith('mismatch:digest_tick_'), j.failure)
        self.assertIsNotNone(net.joiner.stopped)
        self.assertIn(3, net.relay.absent)
        released = net.relay.released
        for _ in range(30):
            net.round()
        self.assertGreater(net.relay.released, released+20)        # the session never waited for the failed joiner
        self.assertIsNone(net.relay.failed)
        # Every live client saw the ABORT record in slot 3's stand-in blob.
        aborts = [t for t, blobs in net.live[1].applied if blobs[2] and join.blob_record(blobs[2])['op'] == 'abort']
        self.assertEqual(len(aborts), 1)

    def test_join_observe_counts_a_diverged_joiner_and_hands_over(self):
        # --join-observe (diagnostic): the digest is not held before H, the clocks are; the lines say so.
        net = Network(barrier={'observe': True}, observe=True)
        for _ in range(40):
            net.round()
        net.request()
        while net.relay.join.state == 'queued':
            net.round()
        net.boot_joiner()
        net.joiner.state = b'not the saved world'
        for _ in range(380):
            net.round(joiner_steps=3)
            if net.relay.join.state in ('joined', 'failed') and net.relay.released > net.relay.join.handover+20:
                break
        j = net.relay.join
        self.assertEqual(j.state, 'joined', (j.failure, net.notices[-4:]))
        self.assertEqual(j.first_digest_mismatch, j.save_tick+1)
        self.assertEqual(j.digest_mismatches, j.handover-j.save_tick-1)
        notices = ' '.join(net.notices)
        self.assertIn(f'digest_mismatch first={j.save_tick+1} diagnostic=join_observe', notices)
        self.assertIn(f'joined tick={j.handover} ', notices)
        self.assertIn('diagnostic=join_observe digest_mismatches=', notices)
        self.assertEqual(net.relay.summary()['join'][0]['diagnostic'], 'join_observe')
        self.assertIsNone(net.relay.failed)
        self.assertGreater(len(net.relay.mismatches), 0)     # after H the session's --observe records the divergence
        # Without it the same joiner is stopped at T+1 (test above); a clock mismatch stays fatal with it.
        net2 = Network(barrier={'observe': True}, observe=True)
        for _ in range(40):
            net2.round()
        net2.request()
        while net2.relay.join.state == 'queued':
            net2.round()
        net2.boot_joiner()
        net2.joiner.clock = 0.5
        for _ in range(20):
            net2.round(joiner_steps=2)
        self.assertEqual(net2.relay.join.state, 'failed')
        self.assertTrue(net2.relay.join.failure.startswith('mismatch:real_tick_'), net2.relay.join.failure)

    def test_a_slow_joiner_is_refused_when_the_pause_would_be_too_long(self):
        net = Network(catchup_limit=0.5, max_pause=1.0, handover_gap=5)
        for _ in range(40):
            net.round()
        net.request()
        while net.relay.join.state == 'queued':
            net.round()
        for _ in range(200):
            net.round()                                     # the joiner loads for a long time
        net.boot_joiner()
        for _ in range(80):
            net.round(joiner_steps=1)                       # no faster than the session: the gap never closes
        j = net.relay.join
        self.assertEqual(j.state, 'failed')
        self.assertTrue(j.failure.startswith('pause_too_long'), j.failure)
        self.assertIsNone(net.relay.failed)

    def test_a_joiner_no_faster_than_the_session_is_handed_over_after_the_catchup_limit(self):
        # The limit counts from the catch-up's first report: 5 s is longer than the coordinator's 64-report progress window
        # (about 2 s at 30 Hz), which it was once measured from, so such a joiner was never handed over (rj2, rj3).
        net = Network(ticks=3000, catchup_limit=5.0, max_pause=60.0, handover_gap=5)
        for _ in range(40):
            net.round()
        net.request()
        while net.relay.join.state == 'queued':
            net.round()
        for _ in range(100):
            net.round()                                     # the joiner loads while the session runs on
        net.boot_joiner()
        for _ in range(140):
            net.round(joiner_steps=1)                       # 4.7 s at the session's own rate: not yet
        j = net.relay.join
        self.assertEqual(j.state, 'catching_up')
        gap = net.relay.released-net.joiner.tick
        for _ in range(600):
            net.round(joiner_steps=1)                       # still no faster: only the limit can hand it over
            if j.state in ('joined', 'failed') and net.relay.released > j.handover+20:
                break
        self.assertGreater(gap, 50)
        self.assertEqual(j.state, 'joined', (j.failure, net.notices[-3:]))
        self.assertIn('joined tick=', ' '.join(net.notices))

    def test_launcher_answers_and_refusals(self):
        net = Network()
        for _ in range(20):
            net.round()
        self.assertEqual(net.request(slot=1)[5:7], ['failed', '0'])            # slot 1 plays
        self.assertEqual(net.answers[-1].split('|')[-1], 'slot_not_absent')
        first = net.request(slot=3, nonce='00000000000000aa')
        self.assertEqual(first[5], 'queued')
        self.assertEqual(net.request(slot=3, nonce='00000000000000aa'), first)  # a poll repeats the state
        self.assertEqual(net.request(slot=3, nonce='00000000000000bb')[-1], 'busy')
        self.assertEqual(net.relay.receive(f'NGJOIN|1|{"cd"*16}|3|00000000000000aa'.encode(), net.launcher, 0), [])

    def test_saver_disagreement_refuses_the_join(self):
        net = Network()
        for _ in range(40):
            net.round()
        net.request()
        net.live[2].portable_salt = b'a saver whose side table differs'
        while net.relay.join.state == 'queued':
            net.round()
        self.assertEqual((net.relay.join.state, net.relay.join.failure), ('failed', 'saver_disagree'))

    def test_failed_save_record_refuses_the_join(self):
        net = Network()
        for _ in range(40):
            net.round()
        answer = net.request()
        save_tick = int(answer[6])
        net.live[2].fail_save = 2                           # it reports not_possible instead of saving
        for _ in range(save_tick+5-net.relay.released):
            net.round()
        self.assertEqual((net.relay.join.state, net.relay.join.failure), ('failed', 'not_possible:slot_2'))

    def test_relay_cli_options(self):
        relay = (ROOT/'tools/dev/native_gate_relay.py').read_text(encoding='utf-8')
        for option in ('--join', '--absent', '--join-savers', '--join-lead', '--join-handover-gap', '--join-max-pause'):
            self.assertIn(f"'{option}'", relay)
        # Every client builds the absent slot's player at H-2: a handover lead under 3 is refused before anything binds.
        import subprocess
        import sys
        proc = subprocess.run([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', '40999', '--token', TOKEN, '--clients', '3',
                               '--report', 'unused.json', '--join', '--absent', '3', '--join-handover-lead', '2'], capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 2)
        self.assertIn('--join-handover-lead must be at least 3', proc.stderr)

    def test_overlay_options(self):
        import contextlib
        import io
        import tempfile
        from unittest import mock
        builder = __import__('build_native_gate_overlay')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            dll, save = root/'gate.dll', root/'j600.xml.gz'
            dll.write_bytes(b'dll')
            save.write_bytes(b'save')
            package = root/'j600.x4join'
            package.write_bytes(golden())
            sha = hashlib.sha256(package.read_bytes()).hexdigest()
            common = ['build', '--gate-dll', str(dll), '--mode', 'battle', '--snapshot', str(save), '--session-clients', '--world-identities']
            with mock.patch('sys.argv', common+['--out', str(root/'saver'), '--join', 'report']):
                builder.main()
            self.assertIn("X4LAN_CONFIG.native_gate_join = {save = 'report'}", (root/'saver/ui/config.lua.append').read_text())
            # The session's absent slots for every per-slot module (native_gate_join.absent), on both sides.
            with mock.patch('sys.argv', common+['--out', str(root/'saver3'), '--join', 'report', '--join-absent', '4,3']):
                builder.main()
            self.assertIn("X4LAN_CONFIG.native_gate_join = {save = 'report', absent = {3, 4}}", (root/'saver3/ui/config.lua.append').read_text())
            files = (root/'saver/ui.xml.files').read_text().split()
            self.assertLess(files.index('ui/native_gate_join_core.lua'), files.index('ui/native_gate_probe.lua'))
            with mock.patch('sys.argv', common+['--out', str(root/'joiner'), '--join-package', str(package), '--join-package-sha', sha]):
                builder.main()
            self.assertIn(f"joiner = {{package = './extensions/x4_lan_lab/native/join.x4join', sha = '{sha}', mode = 'strict'}}",
                          (root/'joiner/ui/config.lua.append').read_text())
            self.assertEqual((root/'joiner/native/join.x4join').read_bytes(), package.read_bytes())
            for argv in (common+['--out', str(root/'bad1'), '--join-package', str(package), '--join-package-sha', '0'*64],
                         [a for a in common if a != '--world-identities']+['--out', str(root/'bad2'), '--join'],
                         common+['--out', str(root/'bad3'), '--join', '--join-absent', '1'],
                         common+['--out', str(root/'bad4'), '--join-absent', '3']):
                with self.subTest(argv=argv[-2:]), mock.patch('sys.argv', argv), self.assertRaises(SystemExit),                         contextlib.redirect_stderr(io.StringIO()):
                    builder.main()

    def test_catch_up_arithmetic(self):
        self.assertEqual(join.catch_up_seconds(2550, 30, 40), 255)
        self.assertEqual(join.catch_up_seconds(100, 30, 30), float('inf'))
        self.assertAlmostEqual(join.handover_pause(60, 10, 30, 40), 70/40-10/30)
        self.assertEqual(join.handover_pause(0, 10, 30, 60), 0.0)


class Rejoin(unittest.TestCase):
    """Drop and rejoin (roadmap 15): slot 3 plays from the start (--rejoin 3), stalls, is dropped with ABORT(3, dropped) in
    the first row it did not report, and a later join takes the slot again; the session never stops."""
    DROP = {'rejoin': (3,), 'stall_report_ms': 500, 'stall_drop_ms': 1000}

    def stall(self, net, slot=3, at=30):
        for _ in range(at):
            net.round()
        stalled = net.live.pop(slot)
        first = stalled.tick                                # it stepped to this tick and never reported it
        for _ in range(75):                                 # 2.5 s at 30 Hz: reported at 0.5 s, dropped at 1 s
            net.round()
        return stalled, first

    def rejoin(self, k=1, clock=0.25):
        net = Network(absent=(), k=k, ticks=600, barrier=self.DROP)
        net.live[3].clock = clock
        stalled, first = self.stall(net)
        r = net.relay
        notices = ' '.join(net.notices)
        self.assertIn(f'NATIVE_GATE STALL slot=3 tick={first} ', notices)
        self.assertIn(f'NATIVE_GATE DROP slot=3 tick={first} ', notices)
        self.assertIsNone(r.failed)
        self.assertEqual((r.dropped, sorted(r.absent)), ({3: first}, [3]))
        self.assertGreater(r.released, first+20)            # the session went on without slot 3
        # Its first missing row carries ABORT(3, dropped) for every client; the rows after it are empty stand-ins.
        row = r.states[first][3]
        record = join.blob_record(row['blob'])
        self.assertTrue(row['standin'])
        self.assertEqual((record['op'], record['slot'], record['tick'], record['reason']), ('abort', 3, first, 'dropped'))
        self.assertEqual([r.states[t][3]['blob'] for t in range(first+1, first+6)], ['']*5)
        self.assertEqual(r.states[first-1][3].get('standin'), None)
        held = net.live[1].permits[first+1+k]
        self.assertEqual(held[2], row['blob'])
        self.assertEqual(r.summary()['drop']['drops'][0]['slot'], 3)
        # The dropped process's later reports are answered NGDROP (protocol 4), never counted.
        answer = r.receive(stalled.report(), stalled.address, net.now)   # the frozen process wakes up and reports
        self.assertEqual(answer, [(stalled.address, f'NGDROP|4|{TOKEN}|3|{first}'.encode())] if k else [])
        # A later join takes the slot like any absent one (here from the same address the dropped client had).
        for _ in range(20):
            net.round()
        self.assertEqual(net.request()[5], 'queued')
        while r.join.state == 'queued':
            net.round()
        self.assertEqual(r.join.state, 'saved', net.notices[-4:])
        self.assertEqual(sorted(r.join.saved), [1, 2])
        for _ in range(10):
            net.round()
        save_tick = r.join.save_tick
        net.boot_joiner()
        for _ in range(400):
            net.round(joiner_steps=3)
            if r.join.state in ('joined', 'failed') and r.released > r.join.handover+40:
                break
        j = r.join
        self.assertEqual(j.state, 'joined', (j.failure, net.notices[-5:]))
        self.assertIsNone(r.failed)
        self.assertNotIn(3, r.absent)
        self.assertEqual((r.dropped, r.dropped_peers), ({}, {}))
        theirs = {t: b for t, b in net.live[1].applied if t > save_tick}
        mine = dict(net.joiner.applied)
        common = sorted(set(theirs) & set(mine))
        self.assertGreater(len(common), 30)
        self.assertEqual([mine[t] for t in common], [theirs[t] for t in common])
        self.assertTrue(r.states[j.handover-1][3].get('standin'))
        self.assertFalse(r.states[j.handover][3].get('standin', False))
        return net

    def test_protocol_4_drop_then_rejoin(self):
        net = self.rejoin(k=1)
        # Its own real clock base (0.25 s off) became the reference slot's at the drop: stand-ins and the rejoiner agree.
        self.assertEqual(net.relay.summary()['drop']['drops'][0]['real_base_was'], real(0)+0.25)

    def test_protocol_3_drop_then_rejoin(self):
        self.rejoin(k=0, clock=0.0)

    def test_the_rejoined_slot_can_be_dropped_again(self):
        net = self.rejoin(k=1)
        net.live[3] = net.joiner
        net.joiner = None
        stalled, first = self.stall(net, at=10)
        self.assertEqual(net.relay.dropped, {3: first})
        self.assertEqual(len(net.relay.drops), 2)
        self.assertIsNone(net.relay.failed)

    def test_a_slot_that_may_not_be_dropped_is_waited_for(self):
        net = Network(absent=(), k=1, ticks=600, barrier=self.DROP)
        stalled, first = self.stall(net, slot=2)
        self.assertIn(f'NATIVE_GATE STALL slot=2 tick={first} ', ' '.join(net.notices))
        self.assertIn('droppable=0', ' '.join(net.notices))
        self.assertNotIn('NATIVE_GATE DROP', ' '.join(net.notices))
        self.assertEqual(net.relay.released, first-1)
        self.assertEqual(net.relay.dropped, {})

    def test_a_saver_dropped_before_its_save_refuses_the_join(self):
        net = Network(absent=(), k=1, ticks=600, barrier={'rejoin': (2, 3), 'stall_report_ms': 500, 'stall_drop_ms': 1000})
        stalled, first = self.stall(net, slot=3)
        net.request(slot=3)
        stalled2 = net.live.pop(2)                          # slot 2 stalls before its save tick
        for _ in range(60):
            net.round()
        self.assertEqual(net.relay.join.state, 'failed')
        self.assertEqual(net.relay.join.failure, 'refused_by_relay:saver_dropped')
        self.assertEqual(sorted(net.relay.absent), [2, 3])
        self.assertIsNone(net.relay.failed)
        self.assertGreater(net.relay.released, first+40)

    def test_refusals(self):
        for options in ({'rejoin': (1,), 'stall_drop_ms': 1000}, {'rejoin': (4,), 'stall_drop_ms': 1000},
                        {'rejoin': (3,)}, {'rejoin': (2,), 'stall_drop_ms': 500, 'stall_report_ms': 1000}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                join.JoinBarrier(TOKEN, absent=(), clients=3, lookahead=1, **options)
        with self.assertRaises(ValueError):
            join.JoinBarrier(TOKEN, absent=(3,), rejoin=(3,), stall_drop_ms=1000, clients=3, lookahead=1)

    def test_relay_cli(self):
        import subprocess
        base = [sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', '40998', '--token', TOKEN, '--clients', '3',
                '--report', 'unused.json']
        for extra, text in ((['--rejoin', '3'], '--rejoin and --join-observe need --join'), (['--join', '--rejoin', '3'], '--rejoin needs --stall-drop-ms'),
                            (['--join', '--rejoin', '1', '--stall-drop-ms', '3000'], 'rejoin slots must be 2..N')):
            with self.subTest(extra=extra):
                proc = subprocess.run(base+extra, capture_output=True, text=True, timeout=60)
                self.assertEqual(proc.returncode, 2, proc.stderr)
                self.assertIn(text, proc.stderr)


if __name__ == '__main__':
    unittest.main()
