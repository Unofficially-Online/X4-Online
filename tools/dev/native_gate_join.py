"""Hot join: a player joins a running lockstep session (reports/LOCKSTEP-HOTJOIN-20261101.txt).

Three parts, each testable without X4 (tests/test_native_gate_join.py):

1. JOIN records: the input section 'join' (kind 0x4a, version byte 1; mirrors native_gate_join_core.lua and
   lockstep_join.h). They ride the deterministic input stream, so every client sees each record in the same
   permit, and a joiner sees the same records again in its replay. Little endian; common head version u8, op u8,
   slot u8 (the joining slot k), tick u32:
     1 SAVE_AT   + saver mask u8, nonce u64      relay, in absent slot k's stand-in blob: every client in the mask
                                                 saves the world after step T and writes the join package
     2 SAVED     + bytes u64, package sha256,    a saving client, in its own blob after its save: the package it
                   portable digest 16 bytes      wrote (it names the save's SHA-256) and the digest of its
                                                 client-independent content (equal on every saver, else a desync)
     3 FAILED    + reason u8                     a saving client whose save or capture failed (REASONS)
     4 HANDOVER  (tick = H)                      relay, in slot k's stand-in blob: from tick H on slot k is a full
                                                 client; every client applies slot k's identity steps at H
     5 ABORT     + reason u8                     relay: the join of slot k ended without a handover
2. The join package (<save folder>/mpjoin/j<T>.x4join, written by lockstep_join.h right after the join save
   j<T>.xml.gz): read_package verifies every hash and the record shapes; write_package builds one (tests,
   golden vectors shared with the C test); portable_digest recomputes the digest the SAVED record carries.
   Version 2 adds the world side table (lockstep_join_world.h): HEALTH (hull and shield group value and changed time
   of every destructible object) and POSE (the pure-transform controller pose of every positional one), what X4's
   save rounds or omits in the census health and motion domains (reports/LOCKSTEP-HOTJOIN-20261101.txt 1.6).
   Version 3 adds SHIELD (the shield groups' delay, rate, factor and modifiers), NAV (every navigation controller's
   census state by bank role: cursor relation, refresh and parameter/probe dues, both pathing banks, pending entries),
   ATTN (every object's attention level and due) and KEYS (the gate's first-call tables): report 1.7.
   Version 4 adds PATH (the pathing manager's agent batches, their cursors and round-robin indices and the new-agent list, by
   the agents' owners' portable IDs) and REGION (every hazard region's damage field last and next pass time): report 1.8.
   Version 5 adds FLIGHT (every navigation Agent's flight control model: the linear model's target, leg, step clock and
   spline scalars; another model's kind) and SPLINE (the linear models' Bezier segments and parameter pairs): report 1.9.
   Version 6 adds AGENT (one record per FLIGHT record: the model's Agent's step inputs, speed and turn limits, modes, the
   re-target flag, the waypoint, pending, group and formation lengths): report 1.10.
   Version 7 adds ROUTE (per AGENT record a head and the Agent's waypoint and pending waypoint entries, every reference as a
   portable ID, the Agent's IDs and bytes around them and the linear model's SetState word): report 1.11.
   Version 8 adds PARAM (per AGENT record the Agent's three flight parameter slots by role, read, write and its own copy: the
   turn rates the linear step and the Agent update read among them) and DOCK (every dock and undock controller's census
   fields: poses, velocities, start time, animation, options): report 1.12.
   Version 9 adds STEER (every steering flight control model: its scalars and the x, y, z lanes of its vectors, the target IDs
   as portable IDs, its two behaviours by class with the fields their constructors write, the lengths of its path and
   avoidance cache, whether a path job was pending) and STEERPT (those models' path points, as ROUTE's points): report 1.13.
   Version 10 adds SOLVER (every navigation Agent's motion solver, the whole object with its pointers made relative) and BULLET (a
   census of the projectiles in flight at T, the joiner writes nothing): report 1.14. Only SOLVER's (portable, kind) enter the digest.
3. The relay: JoinBarrier (native_gate_relay.py --join) keeps the session's N slots, stands in for absent ones
   (the reference slot's clocks and digest, an empty blob or a relay record) and runs one JoinCoordinator at a
   time: NGJOIN from the joiner's launcher, SAVE_AT, the savers' SAVED records, NGJOINSTATE answers, the replay
   of the session's own rows to the joiner (its gate counts session ticks from T: lockstep_join.h tick base),
   the per-tick check of the joiner against the session (game and real time exact, digest from T+1 on),
   HANDOVER at a tick H, then slot k is a normal client. Every refusal is reported (NGJOINSTATE ... failed
   <reason>, a JOIN line) and never desyncs the session: until H slot k stays a stand-in.

Launcher messages (UDP to the relay port, authenticated by the session token like NGSTATE):
  joiner -> relay  NGJOIN|1|token|slot|nonce                    ask to join as absent slot k; resend to poll
  relay -> joiner  NGJOINSTATE|1|token|slot|nonce|state|T|saver|bytes|package_sha|detail
                   state queued (save tick T), saved (saver slot's package ready: fetch, verify, boot),
                   catching_up, joined, failed (detail = reason)
"""
import argparse
import collections
import hashlib
import hmac
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_inputs as mux  # noqa: E402

PROTOCOL = 1
HEX = frozenset('0123456789abcdef')
RECORD_VERSION = 1
OP_SAVE_AT, OP_SAVED, OP_FAILED, OP_HANDOVER, OP_ABORT = 1, 2, 3, 4, 5
OPS = {OP_SAVE_AT: 'save_at', OP_SAVED: 'saved', OP_FAILED: 'failed', OP_HANDOVER: 'handover', OP_ABORT: 'abort'}
_HEAD = struct.Struct('<BBBI')
_BODY = {OP_SAVE_AT: struct.Struct('<BQ'), OP_SAVED: struct.Struct('<Q32s16s'), OP_FAILED: struct.Struct('<B'),
         OP_HANDOVER: struct.Struct(''), OP_ABORT: struct.Struct('<B')}
# FAILED/ABORT reasons (lockstep_join.h JOIN_WHY_*; the same numbers in native_gate_join_core.lua).
REASONS = {1: 'not_armed', 2: 'not_possible', 3: 'save_failed', 4: 'file_missing', 5: 'capture', 6: 'unsupported_module',
           7: 'package_write', 8: 'busy', 9: 'timeout', 10: 'mismatch', 11: 'saver_disagree', 12: 'joiner_lost',
           13: 'pause_too_long', 14: 'refused_by_relay', 15: 'platform'}
REASON_CODES = {name: code for code, name in REASONS.items()}


class JoinError(ValueError):
    """A strict refusal; str(error) is the reason token."""


def encode_record(op, slot, tick, mask=0, nonce=0, size=0, package=b'', portable=b'', reason=0):
    if op not in OPS or not 1 <= slot <= 8 or not 0 <= tick <= 0xffffffff:
        raise JoinError('record_field')
    head = _HEAD.pack(RECORD_VERSION, op, slot, tick)
    if op == OP_SAVE_AT:
        if not 1 <= mask <= 0xff or not 0 <= nonce < 1 << 64:
            raise JoinError('record_field')
        body = _BODY[op].pack(mask, nonce)
    elif op == OP_SAVED:
        if len(package) != 32 or len(portable) != 16 or not 0 < size < 1 << 64:
            raise JoinError('record_field')
        body = _BODY[op].pack(size, package, portable)
    elif op in (OP_FAILED, OP_ABORT):
        if reason not in REASONS:
            raise JoinError('record_field')
        body = _BODY[op].pack(reason)
    else:
        body = b''
    return (head+body).hex()


def decode_record(text):
    """Payload hex -> dict; JoinError on any other shape."""
    if not isinstance(text, str) or len(text) % 2 or set(text) - HEX:
        raise JoinError('encoding')
    b = bytes.fromhex(text)
    if len(b) < _HEAD.size:
        raise JoinError('record_length')
    version, op, slot, tick = _HEAD.unpack_from(b)
    if version != RECORD_VERSION:
        raise JoinError('join_version')
    if op not in OPS:
        raise JoinError('record_op')
    if len(b) != _HEAD.size+_BODY[op].size:
        raise JoinError('record_length')
    if not 1 <= slot <= 8:
        raise JoinError('record_field')
    out = {'op': OPS[op], 'slot': slot, 'tick': tick}
    fields = _BODY[op].unpack_from(b, _HEAD.size)
    if op == OP_SAVE_AT:
        out['mask'], out['nonce'] = fields
        if not out['mask']:
            raise JoinError('record_field')
    elif op == OP_SAVED:
        out['bytes'], package, portable = fields
        out['package'], out['portable'] = package.hex(), portable.hex()
    elif op in (OP_FAILED, OP_ABORT):
        if fields[0] not in REASONS:
            raise JoinError('record_field')
        out['reason'] = REASONS[fields[0]]
    return out


def record_blob(record_hex):
    """A blob that carries only this JOIN record (the relay's stand-in blob)."""
    return mux.encode({'join': record_hex})


def blob_record(blob):
    """The JOIN record of an admitted blob, or None; JoinError for a malformed blob or record."""
    try:
        sections = mux.decode(blob)
    except mux.Refused as error:
        raise JoinError(f'blob_{error}') from None
    return decode_record(sections['join']) if 'join' in sections else None


# ---- the join package ----
MAGIC = b'X4MPJN01'
PACKAGE_VERSION = 11          # 1: GATE..LUA; 2: and HEALTH, POSE; 3: and SHIELD, NAV, ATTN, KEYS; 4: and PATH, REGION; 5: and FLIGHT,
PACKAGE_VERSIONS = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)   # SPLINE; 6: and AGENT; 7: and ROUTE; 8: and PARAM, DOCK; 9: and STEER, STEERPT;
#                                                        10: and SOLVER, BULLET; 11: and GRAV, GRAVC, GRAVS
HEADER = struct.Struct('<8sIIQQQ32sQIIQ')      # 96 bytes
SECTION = struct.Struct('<IIQ32s')            # 48 bytes before each payload
SEC_GATE, SEC_IDS, SEC_WAKE, SEC_WAKEVEC, SEC_MDHEAP, SEC_MODULES, SEC_CTX, SEC_LUA, SEC_HEALTH, SEC_POSE = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
SEC_SHIELD, SEC_NAV, SEC_ATTN, SEC_KEYS = 11, 12, 13, 14
SEC_PATH, SEC_REGION = 15, 16
SEC_FLIGHT, SEC_SPLINE = 17, 18
SEC_AGENT = 19
SEC_ROUTE = 20
SEC_PARAM, SEC_DOCK = 21, 22
SEC_STEER, SEC_STEERPT = 23, 24
SEC_SOLVER, SEC_BULLET = 25, 26
SEC_GRAV, SEC_GRAVC, SEC_GRAVS = 27, 28, 29
SECTION_NAMES = {SEC_GATE: 'gate', SEC_IDS: 'ids', SEC_WAKE: 'wake', SEC_WAKEVEC: 'wakevec', SEC_MDHEAP: 'mdheap',
                 SEC_MODULES: 'modules', SEC_CTX: 'ctx', SEC_LUA: 'lua', SEC_HEALTH: 'health', SEC_POSE: 'pose',
                 SEC_SHIELD: 'shield', SEC_NAV: 'nav', SEC_ATTN: 'attn', SEC_KEYS: 'keys', SEC_PATH: 'path', SEC_REGION: 'region',
                 SEC_FLIGHT: 'flight', SEC_SPLINE: 'spline', SEC_AGENT: 'agent', SEC_ROUTE: 'route', SEC_PARAM: 'param', SEC_DOCK: 'dock',
                 SEC_STEER: 'steer', SEC_STEERPT: 'steerpt', SEC_SOLVER: 'solver', SEC_BULLET: 'bullet',
                 SEC_GRAV: 'grav', SEC_GRAVC: 'gravc', SEC_GRAVS: 'gravs'}
WORLD_SECTIONS = (SEC_HEALTH, SEC_POSE)         # version 2 and later, always both
WORLD3_SECTIONS = (SEC_SHIELD, SEC_NAV, SEC_ATTN, SEC_KEYS)   # version 3, always all four
WORLD4_SECTIONS = (SEC_PATH, SEC_REGION)                      # version 4, always both
WORLD5_SECTIONS = (SEC_FLIGHT, SEC_SPLINE)                    # version 5, always both
WORLD6_SECTIONS = (SEC_AGENT,)                                # version 6
WORLD7_SECTIONS = (SEC_ROUTE,)                                # version 7
WORLD8_SECTIONS = (SEC_PARAM, SEC_DOCK)                       # version 8, always both
WORLD9_SECTIONS = (SEC_STEER, SEC_STEERPT)                    # version 9, always both
WORLD10_SECTIONS = (SEC_SOLVER, SEC_BULLET)                   # version 10, always both
WORLD11_SECTIONS = (SEC_GRAV, SEC_GRAVC, SEC_GRAVS)           # version 11, always all three
LAST_SECTION = {1: SEC_LUA, 2: SEC_POSE, 3: SEC_KEYS, 4: SEC_REGION, 5: SEC_SPLINE, 6: SEC_AGENT, 7: SEC_ROUTE, 8: SEC_DOCK, 9: SEC_STEERPT,
                10: SEC_BULLET, 11: SEC_GRAVS}
TEXT_SECTIONS = (SEC_MODULES, SEC_LUA)          # newline-terminated printable lines; count = lines
# Fixed record shapes (lockstep_join.h join_*_rec). GATE is one 256-byte record; MODULES and LUA (the probe's own state
# providers, native_gate_join_core.lua) are text (count = lines).
GATE = struct.Struct('<QQQIIIIIIQQIIIIIIIIIIII64s16sQQII12s28x')
IDS = struct.Struct('<QQII')                  # runtime (the save's ID), portable ID, class (0 saved, 1 created), 0
CTX = struct.Struct('<QQII')                  # owner ID, script name hash, owner class, script version
WAKE = struct.Struct('<QQQIIII')              # owner, script, due bits, class, version, reason, flags
WAKEVEC = struct.Struct('<QQIIIIII')          # owner, script, class, version, vector, position, wake mask, 0
MDHEAP = struct.Struct('<QQQQIIII')           # name, root, time bits, activation bits, seq, kind, ordinal, state
# The world side table (lockstep_join_world.h), sorted by portable ID:
HEALTH = struct.Struct('<QQQQQII')            # portable, hull bits, shield value bits, changed bits, maximum bits, flags, count
POSE = struct.Struct('<QII12I')               # portable, controller vtable RVA, form (1: +0x10, 2: +0x20), x/y/z of 4 vectors
HEALTH_SHIELD, HEALTH_FULL = 1, 2             # flags: a shield group; its value and changed time are the census's full form
# Version 3 (lockstep_join.h join_shield_rec, join_nav_rec, join_attn_rec, join_key_rec):
SHIELD = struct.Struct('<QQIIIIII')           # portable, delay bits, rate bits, factor bits, the modifiers' 3 factors, flags (1: modifiers)
SHIELD_MODS_PRESENT = 1
NAV_BANK = '18IQQQQQIBBBB'                    # x/y/z of 6 vectors, 2 references (portable), eta bits, pending reference (portable),
                                              # transition count, mode, arrived, notified, layer, pending state (0 none, 1 a reference)
NAV = struct.Struct('<QQQQIBBBB'+NAV_BANK*2)  # portable, refresh, parameter due, probe due (bits), relation (0 read, 1 write, 2 none),
                                              # parameters on, probe on, flags (unmapped references), 0, the read-role then write-role bank
ATTN = struct.Struct('<QQII')                 # portable, due bits, level, flags (1: an Object's due)
ATTN_DUE = 1
KEYS = struct.Struct('<QII')                  # key (portable), table, actor (fixture index + 1 in tables 4 and 5, else 0)
KEY_TABLES = {1: 'attention_all', 2: 'nav_probe', 3: 'nav_params', 4: 'attention_actor', 5: 'nav_actor'}
# Version 4 (lockstep_join.h join_path_rec, join_region_rec):
PATH = struct.Struct('<QIIIIII')              # portable, kind, vector, batch, position, value, flags (lockstep_join_world.h)
PATH_MANAGER, PATH_BATCH, PATH_MEMBER, PATH_PENDING = 0, 1, 2, 3   # kinds: a batch vector (batch = its batches, value = the
                                              # round-robin index), a batch (value = members, position = the cursor's index,
                                              # flags = its flag byte), a member and a new agent (portable = the owner)
PATH_LOCAL = 1                                # a member or new agent whose owner has no portable ID (portable 0)
PATH_BATCHES_MAX, PATH_LIST_MAX = 4096, 1000000
REGION = struct.Struct('<QQQII')              # region portable ID, last pass bits, next pass bits, field index, flags (0)
# Version 5 (lockstep_join.h join_flight_rec, join_spline_rec):
FLIGHT = struct.Struct('<QIIII204sI')         # owner portable, model vtable RVA, flags, segments, pairs, state (JOIN_FLIGHT_RANGES), 0
SPLINE = struct.Struct('<QII160s')            # owner portable, part (0 a Bezier segment, 1 parameter pairs), index, data
FLIGHT_LINEAR = 0x2c4f610                     # U::Path::LinearFlightControlModel (model type 3)
FLIGHT_OTHER, FLIGHT_WORD, FLIGHT_UNCARRIED = 1, 2, 4
FLIGHT_STATE, FLIGHT_SEGMENTS, FLIGHT_PAIRS, FLIGHT_PAIRS_PER, FLIGHT_BEZIER_AT = 204, 64, 128, 20, 0x10
# The state bytes: the model's ranges in this order (target, leg, spline scale, spline length/min/max, spline flags).
FLIGHT_RANGES = ((0x20, 0x40), (0x68, 0x78), (0xe8, 4), (0x120, 0xc), (0x130, 4))
# Version 6 (lockstep_join.h join_agent_rec): one per FLIGHT record, same portable IDs and order. The 18 words at AGENT_VALUE_AT,
# the 10 bytes at AGENT_FLAG_AT (+ 2 zero), the waypoint, pending, group member and formation lengths, 0.
AGENT = struct.Struct('<Q18I12sHHHHI')
AGENT_VALUE_AT = (0x340, 0x344, 0x348, 0x34c, 0x358, 0x35c, 0x360, 0x364, 0x36c, 0x38c, 0x390, 0x4c8, 0x4d8, 0x504, 0x518, 0x51c, 0x520,
                  0xf34)
AGENT_FLAG_AT = (0x4d0, 0x4d1, 0x4d2, 0x4d3, 0xf33, 0xf5e, 0xf5f, 0xf77, 0xf79, 0xf7a)
# Version 7 (lockstep_join.h join_route_rec): per AGENT record (same order) a head, then the Agent's waypoint entries (list 0) and
# pending waypoint entries (list 1) in vector order. portable, kind (0 head, 1 point), list, index, flags, 0, five references
# (portable IDs: head the linear model's word and the Agent's +0xef8, +0xf08, +0xf48, +0xf60; point its +0x10 and its sub-point's),
# 168 data bytes (head: the two lengths u16, the bytes +0xf5c, +0xf5d, +0xf74, +0xf75, +0xf76, +0xf7b, two 0, the word +0xf70;
# point: +0x20..+0x67, +0x70..+0x8f with +0x7f 0, the sub-point's +0x20..+0x5f).
ROUTE = struct.Struct('<QIHHII5Q168s')
ROUTE_HEAD, ROUTE_POINT = 0, 1
ROUTE_UNMAPPED, ROUTE_SUB, ROUTE_CONN, ROUTE_SUB_CONN, ROUTE_OTHER, ROUTE_LINEAR = 0x1f, 0x20, 0x40, 0x80, 0x100, 0x200
ROUTE_POINTS, ROUTE_PAD = 64, 0x48+0xf
# Version 8 (lockstep_join.h join_param_rec, join_dock_rec). PARAM: one per AGENT record (same order): portable, flags (bit role: that
# slot's ID +0x170 had no portable ID), 0, three slot blocks by role (0 the read slot Agent + 0x1a0 x g_PathingStateBufferIndex, 1 the
# write slot, 2 the Agent's own copy +0x340), each PARAM_SLOT bytes: +0x000..+0x13f, +0x160..+0x16f, the ID as a portable ID, the
# refresh time +0x178, +0x180..+0x19b, four 0. DOCK: portable, kind (the controller's vtable RVA), 0, start time bits, animation,
# two option bytes, 0, the x, y, z lanes of 14 vectors (an undock controller's 8, the rest 0); sorted by portable ID.
PARAM_ROLES, PARAM_SLOT = 3, 0x180
PARAM = struct.Struct('<QII%ds%ds%ds' % (PARAM_SLOT, PARAM_SLOT, PARAM_SLOT))
PARAM_OBJECT, PARAM_TURN, PARAMB_ID, PARAMB_TIME, PARAMB_TAIL, PARAMB_PAD = 0x140, 0x38, 0x150, 0x158, 0x160, 0x17c
DOCK = struct.Struct('<QIIQI2sH42I')
DOCK_VT, UNDOCK_VT, DOCK_VECTORS = 0x2c3fdc0, 0x2c3e568, 14
# Version 9 (lockstep_join.h join_steer_rec; lockstep_join_world.h join_steer_ranges, join_steer_classes). STEER: one per FLIGHT record
# of a steering model (same order): portable, flags (bits 0, 1: ref 0/1 had no portable ID; STEER_UNCARRIED0 << b: behaviour b of a
# class this build does not carry; STEER_POINTS: a path the record cannot carry; STEER_JOB: an async path job pending), kind (the
# model's vtable RVA), ref[2] (+0x60, +0x80 as portable IDs), the two behaviours' vtable RVAs (+0x70 active, +0x78 default; 0 none),
# points (the path vector +0x360's length), contacts (the avoidance cache +0x90's length), 0, the model's carried bytes (STEER_RANGES:
# a vector range carries the x, y, z lanes of each 16 bytes), each behaviour's carried bytes (STEER_CLASSES). STEERPT: ROUTE records,
# kind ROUTE_POINT, list STEER_LIST, sorted by (portable, index).
STEER = struct.Struct('<QII2Q2IHHI592s96s96s')
STEER_VT, STEER_MODEL, STEER_STATE, STEER_LIST = 0x2c4e9c8, 592, 96, 2
STEER_UNMAPPED, STEER_UNCARRIED0, STEER_UNCARRIED1, STEER_POINTS, STEER_JOB = 3, 4, 8, 16, 32
STEER_RANGES = ((0x20, 0x40, 1), (0x68, 8, 0), (0xa8, 12, 0), (0xb8, 0x60, 0), (0x120, 0xa0, 1), (0x1c0, 8, 0), (0x1d0, 0x20, 0),
                (0x1f0, 0x80, 1), (0x270, 4, 0), (0x280, 0x80, 1), (0x330, 0x14, 0), (0x350, 0x10, 1), (0x378, 9, 0),
                (0x390, 8, 0))
STEER_CLASSES = {0x2c4edc0: ((0x10, 2, 0), (0x18, 1, 0)),                                          # GenericSteeringBehaviour
                 0x2c4dc30: ((0x10, 2, 0), (0x20, 0x40, 1), (0x60, 0x18, 0)),                      # DogfightBehaviour1
                 0x2c4e840: ((0x10, 2, 0), (0x20, 0x40, 1), (0x60, 0x18, 0), (0x80, 0xc, 0), (0x90, 8, 0)),   # ChaseBehaviour
                 0x2c4dab0: ((0x10, 2, 0), (0x18, 5, 0), (0x20, 8, 0)),                            # GainDistanceBehaviour
                 0x2c4d288: ((0x10, 2, 0), (0x20, 0x40, 1), (0x60, 10, 0))}                        # EvasiveSimpleSteeringBehaviour


def steer_bytes(ranges):
    """The carried byte count of ranges (a vector range: 12 of each 16)."""
    return sum(n//16*12 if vectors else n for _, n, vectors in ranges)


def steer_offsets(ranges):
    """For each carried byte, the object offset it came from (the inverse of lockstep_join_world.h join_steer_read)."""
    out = []
    for at, n, vectors in ranges:
        out += [at+16*v+i for v in range(n//16) for i in range(12)] if vectors else list(range(at, at+n))
    return out


assert steer_bytes(STEER_RANGES) == 569 and all(steer_bytes(r) <= STEER_STATE for r in STEER_CLASSES.values())
# Version 10 (lockstep_join.h join_solver_rec, join_bullet_rec; lockstep_join_world.h). SOLVER: portable, flags, kind (the model's vtable
# RVA), the number of pointers, 0, the pointer list ((offset/8) << 1 | 1 for the image, in offset order, the rest 0), the object's bytes
# with those qwords as offsets or RVAs. BULLET: zone, shooter (portable IDs), position in the zone's children, index among its bullets,
# flags, the movement controller's vtable RVA, the macro's name, the object's and the controller's bytes.
SOLVER_BYTES, SOLVER_POINTERS, SOLVER_ACTIVE, SOLVER_OVERFLOW = 0x18f60, 256, 1, 2
SOLVER_IMAGE = 0x6fa9000                       # lockstep_join_world.h JOIN_FLIGHT_IMAGE
SOLVER = struct.Struct('<QIIII%ds%ds' % (4*SOLVER_POINTERS, SOLVER_BYTES))
BULLET_BYTES, BULLET_MOTION, BULLET_MACRO = 0x330, 0xb0, 64
BULLET_NO_SHOOTER, BULLET_NO_MACRO, BULLET_NO_MOTION = 1, 2, 4
BULLET = struct.Struct('<QQIIII%ds%ds%ds' % (BULLET_MACRO, BULLET_BYTES, BULLET_MOTION))
assert SOLVER.size == 103288 and BULLET.size == 1088
# Version 11 (lockstep_join.h join_grav_rec, join_gravc_rec, join_gravs_rec; report 1.15): the radar. GRAV: owner, kind (vtable RVA),
# flags, the four contact lists' lengths, the unit's 0xc8 bytes (vtable, IDs, lock and list pointers 0). GRAVC: owner, list, index,
# contact (portable, 0 with UNMAPPED), flags, 0. GRAVS: portable, value, kind (0 pending, 1 current source, 2 sector), index, flags, 0.
GRAV_BYTES, GRAV_LISTS = 0xc8, 4
GRAV_ACTIVE_VT, GRAV_PASSIVE_VT = 0x2c67610, 0x2c67660
GRAV_ACTIVE, GRAV_UNCARRIED = 1, 2
GRAVC_UNMAPPED = 1
GRAVS_PENDING, GRAVS_CURRENT, GRAVS_SECTOR, GRAVS_CURSOR = 0, 1, 2, 3
GRAVS_UNMAPPED, GRAVS_SEEN = 1, 2
GRAV = struct.Struct('<QII4I%ds' % GRAV_BYTES)
GRAVC = struct.Struct('<QIIQII')
GRAVS = struct.Struct('<QQIIII')
assert GRAV.size == 232 and GRAVC.size == 32 and GRAVS.size == 32
RECORDS = {SEC_IDS: IDS, SEC_CTX: CTX, SEC_WAKE: WAKE, SEC_WAKEVEC: WAKEVEC, SEC_MDHEAP: MDHEAP, SEC_HEALTH: HEALTH, SEC_POSE: POSE,
           SEC_SHIELD: SHIELD, SEC_NAV: NAV, SEC_ATTN: ATTN, SEC_KEYS: KEYS, SEC_PATH: PATH, SEC_REGION: REGION,
           SEC_FLIGHT: FLIGHT, SEC_SPLINE: SPLINE, SEC_AGENT: AGENT, SEC_ROUTE: ROUTE, SEC_PARAM: PARAM, SEC_DOCK: DOCK,
           SEC_STEER: STEER, SEC_STEERPT: ROUTE, SEC_SOLVER: SOLVER, SEC_BULLET: BULLET, SEC_GRAV: GRAV, SEC_GRAVC: GRAVC,
           SEC_GRAVS: GRAVS}
GATE_FIELDS = ('tick', 'game', 'real', 'wire_version', 'clients', 'lookahead', 'tick_hz', 'slot', 'md_seq', 'rid_next',
               'created_generation', 'created_live', 'identities', 'ctx_count', 'wake_count', 'vec_index', 'vec0', 'vec1',
               'md_count', 'md_void', 'md_release', 'md_queued', 'unsupported', 'digest', 'portable', 'save_us',
               'capture_us', 'ambiguous', 'flags', 'fixture')
FLAG_DIAGNOSTIC, FLAG_NO_SAVE = 1, 2
ACTOR_CAPACITY = 30                           # the gate's fixture actor slots (lockstep_registry.h ACTOR_CAPACITY: 15 + 16 - 1)
# The session's view of each snapshot fixture actor at T (GATE fixture, one byte per actor; lockstep_snapshot_fixture.h
# SNAPSHOT_VIEW_*): what its gate binds. A joiner binds only nav (with the Agent), drift (a wreck's drift controller) and
# bare (no controller), each equal to its own loaded actor's view. Here as twelve digits; NO_FIXTURE when none was bound.
FIXTURE_VIEWS = ('unset', 'nav', 'drift', 'bare', 'other', 'lost')
NO_FIXTURE = '0'*12
assert HEADER.size == 96 and SECTION.size == 48 and GATE.size == 256
assert (IDS.size, CTX.size, WAKE.size, WAKEVEC.size, MDHEAP.size, HEALTH.size, POSE.size) == (24, 24, 40, 40, 48, 48, 64)
assert (SHIELD.size, NAV.size, ATTN.size, KEYS.size) == (40, 280, 24, 16)
assert (PATH.size, REGION.size) == (32, 32)
assert (FLIGHT.size, SPLINE.size) == (232, 176) and sum(n for _, n in FLIGHT_RANGES) == FLIGHT_STATE
assert AGENT.size == 104 and ROUTE.size == 232
assert PARAM.size == 1168 and DOCK.size == 200
assert STEER.size == 832


def flight_shape(flights, splines):
    """lockstep_join_world.h join_flight_shape: every FLIGHT record well formed (a linear model with its spline lengths, another
    model kind only), and SPLINE exactly the records they name, in (portable, part, index) order. False otherwise."""
    j = 0
    for portable, kind, flags, segments, pairs, state, reserved in flights:
        if not portable or not kind or reserved or flags & ~7 or segments > FLIGHT_SEGMENTS or pairs > FLIGHT_PAIRS:
            return False
        if flags & FLIGHT_OTHER:
            if flags != FLIGHT_OTHER or kind == FLIGHT_LINEAR or any(state):
                return False
        elif kind != FLIGHT_LINEAR:
            return False
        if flags & (FLIGHT_OTHER | FLIGHT_UNCARRIED) and (segments or pairs):
            return False
        for k in range(segments):
            if j >= len(splines) or splines[j][:3] != (portable, 0, k) or any(splines[j][3][FLIGHT_BEZIER_AT:FLIGHT_BEZIER_AT+8]):
                return False
            j += 1
        for k in range((pairs+FLIGHT_PAIRS_PER-1)//FLIGHT_PAIRS_PER):
            used = min(pairs-k*FLIGHT_PAIRS_PER, FLIGHT_PAIRS_PER)
            if j >= len(splines) or splines[j][:3] != (portable, 1, k) or any(splines[j][3][8*used:]):
                return False
            j += 1
    return j == len(splines)


def agent_shape(flights, agents):
    """lockstep_join_world.h join_agent_shape: one AGENT record per FLIGHT record, in its order; formation 0 or 1, the unused
    flag bytes and reserved 0. False otherwise."""
    if len(agents) != len(flights):
        return False
    for flight, agent in zip(flights, agents):
        portable, flags, formation, reserved = agent[0], agent[19], agent[23], agent[24]
        if portable != flight[0] or reserved or formation > 1 or any(flags[len(AGENT_FLAG_AT):]):
            return False
    return True


def route_shape(agents, routes):
    """lockstep_join_world.h join_route_shape: one ROUTE head per AGENT record, in its order (the lengths AGENT names), each
    followed by exactly its points (none for a head flagged other); unused fields 0, a reference without a portable ID 0. False
    otherwise."""
    j = 0
    for agent in agents:
        if j >= len(routes):
            return False
        portable, kind, lst, index, flags, reserved, refs, data = routes[j][0], routes[j][1], routes[j][2], routes[j][3], \
            routes[j][4], routes[j][5], routes[j][6:11], routes[j][11]
        j += 1
        n = struct.unpack_from('<HH', data)
        if portable != agent[0] or kind != ROUTE_HEAD or lst or index or reserved or flags & ~(ROUTE_UNMAPPED | ROUTE_OTHER | ROUTE_LINEAR) or \
                (not flags & ROUTE_LINEAR and (refs[0] or flags & 1)) or n != (agent[20], agent[21]) or data[10:12] != b'\0\0' or any(data[16:]):
            return False
        if any(ref and flags >> slot & 1 for slot, ref in enumerate(refs)):
            return False
        if flags & ROUTE_OTHER:
            continue
        if max(n) > ROUTE_POINTS:
            return False
        for want in range(2):
            for k in range(n[want]):
                if j >= len(routes):
                    return False
                p_portable, p_kind, p_list, p_index, p_flags, p_reserved = routes[j][:6]
                p_refs, p_data = routes[j][6:11], routes[j][11]
                j += 1
                if p_portable != portable or p_kind != ROUTE_POINT or p_list != want or p_index != k or p_reserved or \
                        p_flags & ~(3 | ROUTE_SUB | ROUTE_CONN | ROUTE_SUB_CONN) or any(p_refs[2:]) or p_data[ROUTE_PAD] or \
                        (p_refs[0] and p_flags & 1) or (p_refs[1] and p_flags & 2):
                    return False
                if not p_flags & ROUTE_SUB and (p_refs[1] or p_flags & (2 | ROUTE_SUB_CONN) or any(p_data[0x68:])):
                    return False
    return j == len(routes)


def param_shape(agents, params):
    """lockstep_join_world.h join_param_shape: one PARAM record per AGENT record, in its order; reserved, unused flags and each
    block's pad 0, an ID beside its role's unmapped bit refused. False otherwise."""
    if len(params) != len(agents):
        return False
    for agent, (portable, flags, reserved, *blocks) in zip(agents, params):
        if portable != agent[0] or reserved or flags & ~7:
            return False
        for role, block in enumerate(blocks):
            ident, = struct.unpack_from('<Q', block, PARAMB_ID)
            if any(block[PARAMB_PAD:]) or (ident and flags >> role & 1):
                return False
    return True


def dock_shape(docks):
    """lockstep_join_world.h join_dock_shape: a dock or undock controller's record, its unused fields 0 (an undock controller's
    second option and vectors 8..13), options 0 or 1. False otherwise."""
    for portable, kind, flags, time, animation, option, reserved, *xyz in docks:
        if not portable or kind not in (DOCK_VT, UNDOCK_VT) or flags or reserved or option[0] > 1 or option[1] > 1:
            return False
        if kind == UNDOCK_VT and (option[1] or any(xyz[8*3:])):
            return False
    return True


def steer_shape(flights, steers, points):
    """lockstep_join_world.h join_steer_shape: one STEER record per FLIGHT record of a steering model, in its order; unused model
    bytes 0, a behaviour's state only as its class carries it (an unknown class flagged uncarried, state 0), an ID beside its
    unmapped bit refused; STEERPT exactly the points each record names (none when flagged STEER_POINTS), each a ROUTE point of list
    STEER_LIST. False otherwise."""
    model = steer_bytes(STEER_RANGES)
    k = j = 0
    for flight in flights:
        if flight[1] != STEER_VT:
            continue
        if k >= len(steers):
            return False
        portable, flags, kind, ref0, ref1, b0, b1, count, contacts, reserved, blob, s0, s1 = steers[k]
        k += 1
        if portable != flight[0] or kind != STEER_VT or reserved or flags & ~63 or any(blob[model:]) or \
                (ref0 and flags & 1) or (ref1 and flags & 2):
            return False
        for slot, (behaviour, state) in enumerate(((b0, s0), (b1, s1))):
            uncarried = bool(flags & STEER_UNCARRIED0 << slot)
            ranges = STEER_CLASSES.get(behaviour) if behaviour else None
            if (not behaviour and uncarried) or (behaviour and (ranges is None) != uncarried):
                return False
            if any(state[steer_bytes(ranges) if ranges else 0:]):
                return False
        if flags & STEER_POINTS:
            continue
        if count > ROUTE_POINTS:
            return False
        for index in range(count):
            if j >= len(points):
                return False
            p_portable, p_kind, p_list, p_index, p_flags, p_reserved = points[j][:6]
            p_refs, p_data = points[j][6:11], points[j][11]
            j += 1
            if p_portable != portable or p_kind != ROUTE_POINT or p_list != STEER_LIST or p_index != index or p_reserved or \
                    p_flags & ~(3 | ROUTE_SUB | ROUTE_CONN | ROUTE_SUB_CONN) or any(p_refs[2:]) or p_data[ROUTE_PAD] or \
                    (p_refs[0] and p_flags & 1) or (p_refs[1] and p_flags & 2):
                return False
            if not p_flags & ROUTE_SUB and (p_refs[1] or p_flags & (2 | ROUTE_SUB_CONN) or any(p_data[0x68:])):
                return False
    return k == len(steers) and j == len(points)


def steer_differences(left, right):
    """The object offsets where two savers' STEER records of one model differ ('model+0x..', 'b0+0x..', 'ref0', 'flags', ...): the
    bytes a saver_disagree naming STEER says are per process (then carried as 0, like FLIGHT's padding)."""
    names = ('portable', 'flags', 'kind', 'ref0', 'ref1', 'b0', 'b1', 'points', 'contacts', 'reserved')
    out = [name for name, x, y in zip(names, left, right) if x != y]
    model = steer_offsets(STEER_RANGES)
    out += [f'model+{model[i]:#x}' for i, (x, y) in enumerate(zip(left[10], right[10])) if x != y and i < len(model)]
    for slot in range(2):
        ranges = STEER_CLASSES.get(left[5+slot]) or ()
        at = steer_offsets(ranges)
        out += [f'b{slot}+{at[i]:#x}' for i, (x, y) in enumerate(zip(left[11+slot], right[11+slot])) if x != y and i < len(at)]
    return out


def solver_shape(records):
    """lockstep_join_world.h join_solver_shape: by portable ID, each once; known flags, 0 reserved; the pointer list in offset order, each
    value in range (an offset into the object, an RVA into the image), the rest of the list 0; none on a record flagged OVERFLOW."""
    last = 0
    for portable, flags, _, pointers, reserved, listed, data in records:
        if not portable or portable <= last or flags & ~3 or reserved or pointers > SOLVER_POINTERS or (flags & SOLVER_OVERFLOW and pointers):
            return False
        last = portable
        entries = struct.unpack('<%dI' % SOLVER_POINTERS, listed)
        if any(entries[pointers:]):
            return False
        previous = -1
        for entry in entries[:pointers]:
            at = (entry >> 1)*8
            if at+8 > SOLVER_BYTES or entry >> 1 <= previous:
                return False
            previous = entry >> 1
            if struct.unpack_from('<Q', data, at)[0] >= (SOLVER_IMAGE if entry & 1 else SOLVER_BYTES):
                return False
    return True


def bullet_shape(records):
    """lockstep_join_world.h join_bullet_shape: by (zone, position), index counting each zone's bullets, known flags, a shooter only without
    its bit, a macro name of printable characters with 0 behind it, none with its bit."""
    previous = None
    for zone, shooter, position, index, flags, motion_kind, macro, _, _ in records:
        same = previous is not None and previous[0] == zone
        if not zone or (previous is not None and (zone, position) <= previous[:2]) or index != (previous[2]+1 if same else 0) or flags & ~7 or \
                (flags & BULLET_NO_SHOOTER and shooter) or (flags & BULLET_NO_MOTION and motion_kind):
            return False
        name = macro.split(b'\0', 1)[0]
        if len(name) == BULLET_MACRO or any(not 33 <= c <= 126 for c in name) or any(macro[len(name):]) or bool(flags & BULLET_NO_MACRO) == bool(name):
            return False
        previous = (zone, position, index)
    return True


def solver_differences(left, right):
    """The qword offsets where two savers' SOLVER records of one Agent differ ('q+0x..'; 'pointers': the pointer lists). The constructor's
    unset bytes are expected among them (lockstep_join.h)."""
    out = ['pointers'] if left[3] != right[3] or left[5] != right[5] else []
    a, b = left[6], right[6]
    if a != b:
        out += [f'q+{at:#x}' for at in range(0, SOLVER_BYTES, 8) if a[at:at+8] != b[at:at+8]]
    return out


def grav_shape(grav, gravc, gravs):
    """lockstep_join_world.h join_grav_shape: GRAV by owner, each once, an active or passive kind with its flag, no contacts on a passive
    or uncarried unit; GRAVC exactly each unit's counted contacts in (list, index) order, a contact 0 only with its flag; GRAVS by kind
    then index (sources counted from 0), SEEN and a value only on a mapped sector, portable 0 only with its flag."""
    at, last = 0, 0
    for portable, kind, flags, *rest in grav:
        counts = rest[:GRAV_LISTS]
        active = kind == GRAV_ACTIVE_VT
        if not portable or portable <= last or (not active and kind != GRAV_PASSIVE_VT) or flags & ~3 or bool(flags & GRAV_ACTIVE) != active:
            return False
        last = portable
        for lst, count in enumerate(counts):
            if (not active or flags & GRAV_UNCARRIED) and count:
                return False
            for k in range(count):
                if at >= len(gravc):
                    return False
                owner, li, index, contact, cflags, reserved = gravc[at]
                if owner != portable or li != lst or index != k or cflags & ~1 or reserved or (not contact) != bool(cflags & GRAVC_UNMAPPED):
                    return False
                at += 1
    if at != len(gravc):
        return False
    previous = None
    for at, (portable, value, kind, index, flags, reserved) in enumerate(gravs):
        if kind == GRAVS_CURSOR:               # once, last, portable 0, no flags, a cursor inside the list (lockstep_ai_schedule.h)
            if portable or flags or reserved or not index or value >= index or at+1 != len(gravs):
                return False
            continue
        if kind > GRAVS_SECTOR or flags & ~3 or reserved or (not portable) != bool(flags & GRAVS_UNMAPPED) or \
                (flags & GRAVS_SEEN and kind != GRAVS_SECTOR) or ((kind != GRAVS_SECTOR or not portable) and value):
            return False
        if previous is not None and (kind < previous[0] or (kind == previous[0] and index <= previous[1])):
            return False
        if kind != GRAVS_SECTOR and index != (previous[1]+1 if previous is not None and previous[0] == kind else 0):
            return False
        previous = (kind, index)
    return True


def bullet_differences(left, right):
    """The object offsets where two savers' BULLET records of one projectile differ ('fields' for its named ones, 'bullet+0x..', 'motion+0x..')."""
    out = ['fields'] if left[:7] != right[:7] else []
    for name, x, y in (('bullet', left[7], right[7]), ('motion', left[8], right[8])):
        out += [f'{name}+{at:#x}' for at in range(0, len(x), 8) if x[at:at+8] != y[at:at+8]]
    return out


def path_shape(records):
    """lockstep_join_world.h join_path_shape: the two vector records, every batch of each, every member of each batch in
    order, then the new agents; a cursor inside a non-empty batch, 0 in an empty one; portable present exactly without the
    local flag. False: not a PATH section."""
    r, i, batches = records, 0, []
    for v in (0, 1):
        if i >= len(r):
            return False
        portable, kind, vector, batch, position, value, flags = r[i]
        if kind != PATH_MANAGER or vector != v or position or portable or flags or batch > PATH_BATCHES_MAX or \
                (value >= batch if batch else value != 0):
            return False
        batches.append(batch)
        i += 1
    first = i
    for v in (0, 1):
        for b in range(batches[v]):
            if i >= len(r):
                return False
            portable, kind, vector, batch, position, value, flags = r[i]
            if kind != PATH_BATCH or vector != v or batch != b or portable or flags > 255 or value > PATH_LIST_MAX or \
                    (position >= value if value else position != 0):
                return False
            i += 1
    def member(record, kind_wanted, v, b, p):
        portable, kind, vector, batch, position, value, flags = record
        return (kind == kind_wanted and vector == v and batch == b and position == p and not value and not flags & ~PATH_LOCAL and
                (portable == 0) == bool(flags & PATH_LOCAL))
    at = first
    for v in (0, 1):
        for b in range(batches[v]):
            for p in range(r[at][5]):
                if i >= len(r) or not member(r[i], PATH_MEMBER, v, b, p):
                    return False
                i += 1
            at += 1
    for p, record in enumerate(r[i:]):
        if not member(record, PATH_PENDING, 0, 0, p):
            return False
    return True


def _records(kind, payload, count):
    if kind == SEC_GATE:
        if count != 1 or len(payload) != GATE.size:
            raise JoinError('gate_shape')
        values = GATE.unpack(payload)
        gate = dict(zip(GATE_FIELDS, values))
        gate['digest'] = gate['digest'].decode('ascii', 'replace')
        gate['portable'] = gate['portable'].hex()
        if any(view >= len(FIXTURE_VIEWS) for view in gate['fixture']):
            raise JoinError('gate_fixture')
        gate['fixture'] = ''.join(str(view) for view in gate['fixture'])
        return gate
    if kind in TEXT_SECTIONS:
        text = payload.decode('ascii')
        lines = [line for line in text.split('\n') if line]
        if len(lines) != count or (text and not text.endswith('\n')) or any(not 32 <= ord(c) <= 126 for c in text.replace('\n', '')):
            raise JoinError(f'{SECTION_NAMES[kind]}_shape')
        return lines
    shape = RECORDS[kind]
    if len(payload) != count*shape.size:
        raise JoinError(f'{SECTION_NAMES[kind]}_shape')
    return [shape.unpack_from(payload, i*shape.size) for i in range(count)]


def read_package(data):
    """bytes (or a path) -> {'header': {...}, 'sections': {name: records}}; JoinError(reason) on any mismatch."""
    if isinstance(data, (str, Path)):
        data = Path(data).read_bytes()
    if len(data) < HEADER.size+32:
        raise JoinError('package_length')
    if hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise JoinError('package_sha')
    magic, version, count, tick, game, real, save_sha, save_bytes, slot, flags, reserved = HEADER.unpack_from(data)
    if magic != MAGIC or version not in PACKAGE_VERSIONS or reserved or count > 29:
        raise JoinError('package_magic')
    header = {'tick': tick, 'game': game, 'real': real, 'save_sha': save_sha.hex(), 'save_bytes': save_bytes, 'slot': slot,
              'flags': flags, 'sections': count, 'version': version}
    last = LAST_SECTION[version]
    at, sections, end = HEADER.size, {}, len(data)-32
    for _ in range(count):
        if end-at < SECTION.size:
            raise JoinError('section_truncated')
        kind, records, size, digest = SECTION.unpack_from(data, at)
        at += SECTION.size
        if kind not in SECTION_NAMES or kind > last or SECTION_NAMES[kind] in sections or size > end-at:
            raise JoinError('section_head')
        payload = data[at:at+size]
        at += size
        if hashlib.sha256(payload).digest() != digest:
            raise JoinError(f'{SECTION_NAMES[kind]}_sha')
        sections[SECTION_NAMES[kind]] = _records(kind, payload, records)
    if at != end or 'gate' not in sections or (version >= 2 and not all(SECTION_NAMES[k] in sections for k in WORLD_SECTIONS)) or \
            (version >= 3 and not all(SECTION_NAMES[k] in sections for k in WORLD3_SECTIONS)) or \
            (version >= 4 and not all(SECTION_NAMES[k] in sections for k in WORLD4_SECTIONS)) or \
            (version >= 5 and not all(SECTION_NAMES[k] in sections for k in WORLD5_SECTIONS)) or \
            (version >= 6 and not all(SECTION_NAMES[k] in sections for k in WORLD6_SECTIONS)) or \
            (version >= 7 and not all(SECTION_NAMES[k] in sections for k in WORLD7_SECTIONS)) or \
            (version >= 8 and not all(SECTION_NAMES[k] in sections for k in WORLD8_SECTIONS)) or \
            (version >= 9 and not all(SECTION_NAMES[k] in sections for k in WORLD9_SECTIONS)) or \
            (version >= 10 and not all(SECTION_NAMES[k] in sections for k in WORLD10_SECTIONS)) or \
            (version >= 11 and not all(SECTION_NAMES[k] in sections for k in WORLD11_SECTIONS)):
        raise JoinError('package_layout')
    for name in ('health', 'pose', 'shield', 'nav', 'attn', 'flight', 'dock', 'steer'):   # sorted by portable ID, each once (world_order)
        keys = [r[0] for r in sections.get(name, [])]
        if any(b <= a for a, b in zip(keys, keys[1:])):
            raise JoinError('world_order')
    for key, table, actor in sections.get('keys', []):         # a known table; an actor only in the actor tables
        if not key or table not in KEY_TABLES or (not 1 <= actor <= ACTOR_CAPACITY if table >= 4 else actor):
            raise JoinError('keys_shape')
    keys = [(table, key) for key, table, _ in sections.get('keys', [])]
    if any(b <= a for a, b in zip(keys, keys[1:])):
        raise JoinError('world_order')
    regions = sections.get('region', [])                      # version 4: by (region, field), each once
    if any(not region or flags for region, _, _, _, flags in regions):
        raise JoinError('region_shape')
    fields = [(region, field) for region, _, _, field, _ in regions]
    if any(b <= a for a, b in zip(fields, fields[1:])):
        raise JoinError('world_order')
    if version >= 4 and not path_shape(sections['path']):
        raise JoinError('path_shape')
    if not flight_shape(sections.get('flight', []), sections.get('spline', [])):   # version 5
        raise JoinError('flight_shape')
    if version >= 6 and not agent_shape(sections['flight'], sections['agent']):
        raise JoinError('agent_shape')
    if version >= 7 and not route_shape(sections['agent'], sections['route']):
        raise JoinError('route_shape')
    if version >= 8 and not param_shape(sections['agent'], sections['param']):
        raise JoinError('param_shape')
    if not dock_shape(sections.get('dock', [])):
        raise JoinError('dock_shape')
    if version >= 9 and not steer_shape(sections['flight'], sections['steer'], sections['steerpt']):
        raise JoinError('steer_shape')
    if version >= 10 and not solver_shape(sections['solver']):
        raise JoinError('solver_shape')
    if version >= 10 and not bullet_shape(sections['bullet']):
        raise JoinError('bullet_shape')
    if version >= 11 and not grav_shape(sections['grav'], sections['gravc'], sections['gravs']):
        raise JoinError('grav_shape')
    gate = sections['gate']
    if (gate['tick'], gate['game'], gate['real'], gate['slot']) != (tick, game, real, slot):
        raise JoinError('gate_header')
    if portable_digest(sections) != gate['portable']:
        raise JoinError('portable_digest')
    return {'header': header, 'sections': sections, 'sha': hashlib.sha256(data).hexdigest()}


def portable_text(sections):
    """The client-independent content, as lockstep_join.h join_portable folds it (no runtime IDs, no MD seq)."""
    lines = []
    for owner, script, klass, version in sections.get('ctx', []):
        lines.append(f'CTX|{klass}|{owner:016x}|{script:016x}|{version}')
    for owner, script, due, klass, version, reason, flags in sections.get('wake', []):
        lines.append(f'WAKE|{klass}|{owner:016x}|{script:016x}|{version}|{due:016x}|{reason}')
    for owner, script, klass, version, vector, position, mask, _ in sections.get('wakevec', []):
        lines.append(f'VEC|{vector}|{position}|{klass}|{owner:016x}|{script:016x}|{version}|{mask}')
    heap = sorted(sections.get('mdheap', []), key=lambda r: (_double(r[2]), r[4]))
    for name, root, time, activation, seq, kind, ordinal, state in heap:
        lines.append(f'MD|{name:016x}|{root:016x}|{ordinal}|{kind}|{state}|{activation:016x}|{time:016x}')
    fixture = sections.get('gate', {}).get('fixture', NO_FIXTURE)
    if fixture != NO_FIXTURE:                  # only a session with a bound fixture: other packages keep their digest
        lines.append(f'FIXTURE|{fixture}')
    world = (('health', HEALTH), ('pose', POSE))+((('shield', SHIELD), ('nav', NAV), ('attn', ATTN), ('keys', KEYS)) if 'nav' in sections else ())
    world += (('path', PATH), ('region', REGION)) if 'path' in sections else ()
    world += (('flight', FLIGHT), ('spline', SPLINE)) if 'flight' in sections else ()
    world += (('agent', AGENT),) if 'agent' in sections else ()
    world += (('route', ROUTE),) if 'route' in sections else ()
    world += (('param', PARAM), ('dock', DOCK)) if 'param' in sections else ()
    world += (('steer', STEER), ('steerpt', ROUTE)) if 'steer' in sections else ()
    if 'health' in sections:                   # version 2 (and later): the world side table, one line per section
        for name, shape in world:
            records = sections.get(name, [])
            digest = hashlib.sha256(b''.join(shape.pack(*r) for r in records)).hexdigest()
            lines.append(f'{name.upper()}|{len(records)}|{digest}')
    if 'solver' in sections:                   # version 10: which Agents hold a solver and under which model; not its bytes, not BULLET
        records = sections['solver']
        digest = hashlib.sha256(b''.join(struct.pack('<QI', r[0], r[2]) for r in records)).hexdigest()
        lines.append(f'SOLVER|{len(records)}|{digest}')
    if 'grav' in sections:                     # version 11: which units, of which kind, how many contacts per list; not their bytes
        records = sections['grav']
        digest = hashlib.sha256(b''.join(struct.pack('<QI4I', r[0], r[1], *r[3:7]) for r in records)).hexdigest()
        lines.append(f"GRAV|{len(records)}|{len(sections['gravc'])}|{len(sections['gravs'])}|{digest}")
    return ''.join(line+'\n' for line in lines)


def _double(bits):
    return struct.unpack('<d', struct.pack('<Q', bits))[0]


def portable_digest(sections):
    return hashlib.sha256(portable_text(sections).encode('ascii')).digest()[:16].hex()


def write_package(gate, sections, save_sha=b'\0'*32, save_bytes=0, flags=0, version=PACKAGE_VERSION):
    """Build a package: gate is a dict of GATE_FIELDS (digest a 64-char str; portable computed here), sections maps
    kind -> list of record tuples (MODULES: list of text lines). Mirrors join_package_write. Version 10 adds SOLVER and BULLET, version 9
    STEER and STEERPT, version 8
    PARAM and DOCK, version 7
    ROUTE, version 6 AGENT, version 5
    FLIGHT and SPLINE (empty when not given) to version 4's sections. Version 4 always carries
    HEALTH, POSE, SHIELD, NAV, ATTN, KEYS, PATH and REGION (empty when not given; an empty PATH is the two vector records
    of no batches), version 3 all but PATH and REGION, version 2 HEALTH and POSE only, version 1 (a saver before the world
    side table) none."""
    if version not in PACKAGE_VERSIONS:
        raise JoinError('package_version')
    sections = dict(sections)
    allowed = (WORLD_SECTIONS if version >= 2 else ())+(WORLD3_SECTIONS if version >= 3 else ())+(WORLD4_SECTIONS if version >= 4 else ())+\
        (WORLD5_SECTIONS if version >= 5 else ())+(WORLD6_SECTIONS if version >= 6 else ())+(WORLD7_SECTIONS if version >= 7 else ())+\
        (WORLD8_SECTIONS if version >= 8 else ())+(WORLD9_SECTIONS if version >= 9 else ())+(WORLD10_SECTIONS if version >= 10 else ())+\
        (WORLD11_SECTIONS if version >= 11 else ())
    if any(kind in sections for kind in WORLD_SECTIONS+WORLD3_SECTIONS+WORLD4_SECTIONS+WORLD5_SECTIONS+WORLD6_SECTIONS+WORLD7_SECTIONS+
           WORLD8_SECTIONS+WORLD9_SECTIONS+WORLD10_SECTIONS+WORLD11_SECTIONS if kind not in allowed):
        raise JoinError('package_version')
    if version >= 4:
        sections.setdefault(SEC_PATH, [(0, PATH_MANAGER, 0, 0, 0, 0, 0), (0, PATH_MANAGER, 1, 0, 0, 0, 0)])
    for kind in allowed:
        sections.setdefault(kind, [])
    gate = dict(gate)
    gate['fixture'] = gate.get('fixture') or NO_FIXTURE
    if len(gate['fixture']) != 12 or any(c not in '012345' for c in gate['fixture']):
        raise JoinError('gate_fixture')
    named = {SECTION_NAMES[k]: v for k, v in sections.items()}
    named['gate'] = gate
    gate['portable'] = portable_digest(named)
    values = [gate[name] for name in GATE_FIELDS]
    values[GATE_FIELDS.index('fixture')] = bytes(int(c) for c in gate['fixture'])
    values[GATE_FIELDS.index('digest')] = gate['digest'].encode('ascii')
    values[GATE_FIELDS.index('portable')] = bytes.fromhex(gate['portable'])
    parts = [(SEC_GATE, 1, GATE.pack(*values))]
    for kind in sorted(sections):
        records = sections[kind]
        if kind in TEXT_SECTIONS:
            parts.append((kind, len(records), ''.join(line+'\n' for line in records).encode('ascii')))
        else:
            parts.append((kind, len(records), b''.join(RECORDS[kind].pack(*r) for r in records)))
    out = bytearray(HEADER.pack(MAGIC, version, len(parts), gate['tick'], gate['game'], gate['real'], save_sha,
                                save_bytes, gate['slot'], flags, 0))
    for kind, count, payload in parts:
        out += SECTION.pack(kind, count, len(payload), hashlib.sha256(payload).digest())+payload
    out += hashlib.sha256(out).digest()
    return bytes(out)


def summary(package):
    gate = package['sections']['gate']
    counts = {name: len(records) for name, records in package['sections'].items() if name != 'gate'}
    return {'tick': gate['tick'], 'slot': gate['slot'], 'save_sha': package['header']['save_sha'], 'version': package['header']['version'],
            'save_bytes': package['header']['save_bytes'], 'flags': package['header']['flags'], 'portable': gate['portable'],
            'md_seq': gate['md_seq'], 'unsupported': gate['unsupported'], 'ambiguous': gate['ambiguous'], 'counts': counts,
            'fixture': gate['fixture'],
            'modules': package['sections'].get('modules', []), 'lua': package['sections'].get('lua', []), 'sha': package['sha']}


def compare_packages(left, right):
    """Two savers' packages of one tick: their client-independent content must agree (portable digest); the
    differing portable lines are listed for the report, and the world side table's differing objects by portable ID."""
    a, b = portable_text(left['sections']).splitlines(), portable_text(right['sections']).splitlines()
    only_a, only_b = sorted(set(a)-set(b)), sorted(set(b)-set(a))
    return {'equal': a == b, 'left_only': only_a[:40], 'right_only': only_b[:40], 'left_count': len(a), 'right_count': len(b),
            'world': world_differences(left['sections'], right['sections'])}


def world_differences(left, right, samples=8):
    """Per world section (version 2): records only on one side and records that differ, by portable ID (samples)."""
    out = {}
    for name in ('health', 'pose', 'shield', 'nav', 'attn', 'keys', 'path', 'region', 'flight', 'spline', 'agent', 'route', 'param',
                 'dock', 'steer', 'steerpt', 'solver', 'bullet', 'grav', 'gravc', 'gravs'):
        if name not in left and name not in right:
            continue
        key = (lambda r: (r[1] << 64) | r[0]) if name == 'keys' else (lambda r: r[0])   # KEYS by (table, key)
        if name == 'path':                     # PATH by (kind, vector, batch, position): a batch's position is its cursor (data)
            key = lambda r: (r[1] << 96) | (r[2] << 64) | (r[3] << 32) | (r[4] if r[1] >= PATH_MEMBER else 0)
        elif name == 'region':                 # REGION by (region, field)
            key = lambda r: (r[3] << 64) | r[0]
        elif name == 'spline':                 # SPLINE by (portable, part, index)
            key = lambda r: (r[1] << 96) | (r[2] << 64) | r[0]
        elif name in ('route', 'steerpt'):    # ROUTE and STEERPT by (portable, kind, list, index)
            key = lambda r: (r[1] << 96) | (r[2] << 80) | (r[3] << 64) | r[0]
        elif name == 'bullet':                 # BULLET by (zone, position)
            key = lambda r: (r[2] << 64) | r[0]
        elif name == 'gravc':                  # GRAVC by (owner, list, index)
            key = lambda r: (r[1] << 96) | (r[2] << 64) | r[0]
        elif name == 'gravs':                  # GRAVS by (kind, index)
            key = lambda r: (r[2] << 32) | r[3]
        x = {key(r): r for r in left.get(name, [])}
        y = {key(r): r for r in right.get(name, [])}
        differ = sorted(i for i in x.keys() & y.keys() if x[i] != y[i])
        out[name] = {'left_only': len(x.keys()-y.keys()), 'right_only': len(y.keys()-x.keys()), 'differ': len(differ),
                     'samples': [_sample(name, i) for i in differ[:samples]]}
        if name == 'steer':                    # the offsets that differ, over every differing model (per process: carry them as 0)
            offsets = collections.Counter(o for i in differ for o in steer_differences(x[i], y[i]))
            out[name]['offsets'] = [f'{o}:{n}' for o, n in offsets.most_common(24)]
        elif name in ('solver', 'bullet'):     # the same; report 1.14: not in the digest, so a difference here does not refuse the join
            differences = solver_differences if name == 'solver' else bullet_differences
            offsets = collections.Counter(o for i in differ for o in differences(x[i], y[i]))
            out[name]['offsets'] = [f'{o}:{n}' for o, n in offsets.most_common(4096 if name == 'solver' else 96)]
        elif name == 'grav':                   # report 1.15: the unit bytes apart, by offset (a pointer or a per-process field)
            offsets = collections.Counter(f'unit+{at:#x}' for i in differ for at in range(0, GRAV_BYTES, 8)
                                          if x[i][7][at:at+8] != y[i][7][at:at+8])
            offsets.update('fields' for i in differ if x[i][:7] != y[i][:7])
            out[name]['offsets'] = [f'{o}:{n}' for o, n in offsets.most_common(64)]
    return out


def _sample(name, key):
    if name == 'path':
        return f'{key >> 96}/{key >> 64 & 0xffffffff}/{key >> 32 & 0xffffffff}/{key & 0xffffffff}'
    if name == 'spline':
        return f'{key & (1 << 64)-1:016x}/{key >> 96}/{key >> 64 & 0xffffffff}'
    if name in ('route', 'steerpt'):
        return f'{key & (1 << 64)-1:016x}/{key >> 96}/{key >> 80 & 0xffff}/{key >> 64 & 0xffff}'
    if name == 'bullet':
        return f'{key & (1 << 64)-1:016x}/{key >> 64}'
    if name == 'gravc':
        return f'{key & (1 << 64)-1:016x}/{key >> 96}/{key >> 64 & 0xffffffff}'
    if name == 'gravs':
        return f'{key >> 32}/{key & 0xffffffff}'
    return f'{key:016x}' if key < 1 << 64 else f'{key >> 64}:{key & (1 << 64)-1:016x}'


# ---- relay side ----
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


def catch_up_seconds(gap_ticks, live_hz, joiner_steps_per_second):
    """Seconds a joiner needs to close a gap while the session keeps running at live_hz; inf if never."""
    margin = joiner_steps_per_second-live_hz
    return math.inf if margin <= 0 else gap_ticks/margin


def handover_pause(gap_ticks, lead_ticks, live_hz, joiner_steps_per_second):
    """Seconds the live session waits at H when the handover is announced with gap_ticks between the live tick and
    the joiner and H lies lead_ticks after the live tick: the joiner must step gap+lead ticks, the session lead."""
    if joiner_steps_per_second <= 0:
        return math.inf
    return max(0.0, (gap_ticks+lead_ticks)/joiner_steps_per_second-lead_ticks/live_hz)


def _state_fields(data, token):
    """(slot, tick, game hex, real hex, digest, blob, version) of an authenticated NGSTATE 3/4 report, else None."""
    try:
        f = data.decode('ascii').split('|')
    except UnicodeError:
        return None
    if len(f) not in (10, 11) or f[0] != 'NGSTATE' or f[1] not in ('3', '4') or not hmac.compare_digest(f[2], token):
        return None
    version = int(f[1])
    if len(f) != 7+version or not f[3].isdigit() or not f[4].isdigit():
        return None
    if any(len(v) != n or set(v)-HEX for v, n in ((f[5], 16), (f[6], 16), (f[7], 64))) or not mux.HEX.fullmatch(f[-1]):
        return None
    return int(f[3]), int(f[4]), f[5], f[6], f[7], f[-1], version


class JoinCoordinator:
    """One join at a time, driven by the JoinBarrier (which owns the session rows). States: idle, queued (SAVE_AT
    placed, waiting for the save tick and the savers' SAVED records), saved (package known, the joiner boots),
    catching_up (the joiner replays), handover (H announced), joined, failed."""
    def __init__(self, barrier, slot, nonce, lead=30, handover_gap=60, handover_lead=8, max_pause=60.0, catchup_limit=300.0,
                 joiner_timeout=900.0, savers=None):
        self.b, self.slot, self.nonce = barrier, slot, nonce
        self.lead, self.handover_gap, self.handover_lead = lead, handover_gap, handover_lead
        self.max_pause, self.catchup_limit, self.joiner_timeout = max_pause, catchup_limit, joiner_timeout
        present = [s for s in barrier.slots if s not in barrier.absent]
        self.savers = sorted(savers) if savers else present
        if not self.savers or any(s not in present for s in self.savers):
            raise ValueError('savers must be present slots')
        self.state, self.failure, self.save_tick, self.handover = 'idle', None, None, None
        self.saved = {}            # saver slot -> SAVED record
        self.saver = None          # the slot whose files the joiner loads
        self.joiner_tick, self.first_real, self.started, self.progress = None, None, None, []
        self.address, self.last_seen, self.notices = None, None, []

    def notice(self, text):
        self.notices.append(f'NATIVE_GATE JOIN slot={self.slot} {text}')

    def fail(self, reason, now=None):
        if self.state in ('failed', 'joined'):
            return
        before, self.state, self.failure = self.state, 'failed', reason
        self.notice(f'failed reason={reason} state={before}')
        if before in ('queued', 'saved', 'catching_up', 'handover'):
            self.b.schedule(self.slot, encode_record(OP_ABORT, self.slot, max(self.b.released, 0),
                                                      reason=REASON_CODES.get(reason.split(':')[0], REASON_CODES['refused_by_relay'])))
        self.b.absent.add(self.slot)   # slot k stays (or becomes again) a stand-in: the session goes on without it
        self.b.handovers.pop(self.slot, None)

    # ---- the request and the save ----
    def start(self, now):
        """Place SAVE_AT in slot k's next stand-in blob; the save tick leaves room for the record to reach every
        client (permit P+1+k) and for its probe to arm the gate before step T ends."""
        placed = self.b.next_standin_tick()
        self.save_tick = placed+1+self.b.lookahead+self.lead
        mask = sum(1 << (s-1) for s in self.savers)
        self.b.schedule(self.slot, encode_record(OP_SAVE_AT, self.slot, self.save_tick, mask=mask, nonce=int(self.nonce, 16)), placed)
        self.state, self.started = 'queued', now
        self.notice(f'queued save_tick={self.save_tick} placed={placed} savers={",".join(map(str, self.savers))}')

    def saver_record(self, slot, record, tick, now):
        """A JOIN record in a present client's own blob (released rows only)."""
        if record['slot'] != self.slot or record['tick'] != self.save_tick or self.state != 'queued':
            return
        if slot not in self.savers or slot in self.saved:
            self.fail(f'refused_by_relay:unexpected_{record["op"]}_slot_{slot}', now)
            return
        if record['op'] == 'failed':
            self.fail(f'{record["reason"]}:slot_{slot}', now)
            return
        if record['op'] != 'saved':
            return
        self.saved[slot] = record
        self.notice(f'saved slot={slot} tick={self.save_tick} bytes={record["bytes"]} package={record["package"]} '
                    f'portable={record["portable"]} at={tick}')
        if len(self.saved) == len(self.savers):
            if len({r['portable'] for r in self.saved.values()}) != 1:
                self.fail('saver_disagree', now)
                return
            self.saver, self.state = self.savers[0], 'saved'
            self.notice(f'ready saver={self.saver} package={self.saved[self.saver]["package"]}')

    def poll(self, now):
        if self.state == 'queued' and self.b.released > self.save_tick+self.b.lookahead+4*self.lead and len(self.saved) < len(self.savers):
            self.fail('timeout:no_saved_record', now)
        if self.state in ('saved', 'catching_up', 'handover') and self.last_seen is not None and now-self.last_seen > self.joiner_timeout:
            self.fail('joiner_lost', now)
        if self.state == 'saved' and self.last_seen is None and now-self.started > self.joiner_timeout:
            self.fail('joiner_lost:never_reported', now)
        if self.state == 'catching_up':
            self.consider_handover(now)

    def answer(self):
        """NGJOINSTATE fields for the launcher."""
        record = self.saved.get(self.saver) if self.saver else None
        detail = self.failure or (str(self.handover) if self.handover else '-')
        return [self.slot, self.nonce, self.state, self.save_tick if self.save_tick is not None else 0, self.saver or 0,
                record['bytes'] if record else 0, record['package'] if record else '-', detail]

    # ---- the joiner's catch-up ----
    def joiner_report(self, slot, tick, game, real, digest, blob, version, address, now):
        """A report of the joining slot before its handover tick: checked against the session's rows, answered with
        the permits the session itself received (built from the same rows)."""
        if self.state not in ('saved', 'catching_up', 'handover'):
            return []
        if self.address is not None and address != self.address:
            return []
        if version != (4 if self.b.lookahead else 3):
            self.fail(f'mismatch:protocol_{version}', now)
            return [(address, f'NGSTOP|{version}|{self.b.token}|{slot}|{tick}'.encode('ascii'))]
        if self.joiner_tick is None and tick != self.save_tick:
            return []                                       # its first report is the loaded world at T
        if self.joiner_tick is not None and (tick < self.joiner_tick or tick > self.joiner_tick+1):
            return []
        ref = self.b.reference_row(tick)
        if ref is None:
            return []                                       # not released yet: the joiner retransmits
        real_value = struct.unpack('>d', bytes.fromhex(real))[0]
        reason = None
        if game != struct.pack('>d', ref['game']).hex():
            reason = f'mismatch:game_tick_{tick}'
        elif real_value != ref['real']:
            reason = f'mismatch:real_tick_{tick}'
        elif tick != self.save_tick and digest != ref['digest']:
            # Tick T carries the loaded world's boot report; from T+1 on the declared state must equal the session's.
            reason = f'mismatch:digest_tick_{tick}'
        if reason:
            self.fail(reason, now)
            return [(address, f'NGSTOP|{version}|{self.b.token}|{slot}|{tick}'.encode('ascii'))]
        first = self.joiner_tick is None
        if first:
            self.address, self.state = address, 'catching_up'
            self.notice(f'catching_up first_report={tick}')
        if self.joiner_tick is None or tick > self.joiner_tick:
            self.joiner_tick = tick
            self.progress.append((now, tick))
            del self.progress[:-64]
        self.last_seen = now
        permits = range(tick+1, tick+2+self.b.lookahead) if first else (tick+1+self.b.lookahead,)
        out = []
        for permit in permits:
            if self.handover is not None and permit-1-self.b.lookahead >= self.handover:
                break                                       # sealed by its own live report: the barrier sends those
            packet = self.b.replay_permit(slot, permit)
            if packet is None:
                break
            out.append((address, packet))
        return out

    def rate(self):
        """The joiner's steps per second over its recent reports (0 until measurable)."""
        if len(self.progress) < 2 or self.progress[-1][0] <= self.progress[0][0]:
            return 0.0
        (t0, k0), (t1, k1) = self.progress[0], self.progress[-1]
        return (k1-k0)/(t1-t0)

    def consider_handover(self, now):
        if self.handover is not None or self.joiner_tick is None:
            return
        gap = self.b.released-self.joiner_tick
        lead = self.b.lookahead+1+self.handover_lead
        rate = self.rate()
        waited = now-(self.progress[0][0] if self.progress else now)
        if gap > self.handover_gap and waited < self.catchup_limit:
            return
        pause = handover_pause(max(gap, 0), lead, self.b.tick_hz, rate) if rate else math.inf
        if gap > self.handover_gap and pause > self.max_pause:
            self.fail(f'pause_too_long:gap_{gap}_rate_{rate:.1f}', now)
            return
        placed = self.b.next_standin_tick()
        self.handover = placed+lead
        self.b.schedule(self.slot, encode_record(OP_HANDOVER, self.slot, self.handover), placed)
        self.b.handover_at(self.slot, self.handover)
        self.state = 'handover'
        self.notice(f'handover tick={self.handover} placed={placed} gap={gap} joiner_rate={rate:.1f} '
                    f'expected_pause_s={0.0 if gap <= self.handover_gap and rate > self.b.tick_hz else pause:.1f}')

    def joined(self, now):
        self.state = 'joined'
        self.notice(f'joined tick={self.handover} catch_up_s={now-self.progress[0][0]:.1f}' if self.progress else 'joined')


def _barrier_base():
    from native_gate_relay import Barrier
    return Barrier


class JoinBarrier(_barrier_base()):
    """The relay's Barrier with absent slots (native_gate_relay.py --join). Absent slots are stand-ins: once every
    present slot reported tick t, each absent slot gets the reference slot's game, real and digest and the blob the
    relay scheduled for it (a JOIN record) or ''. Protocols 3 and 4 only; no live inputs, peer links or crypto."""
    def __init__(self, token, absent=(), join_options=None, **kw):
        super().__init__(token, **kw)
        if kw.get('live_inputs') or kw.get('peer_window') is not None:
            raise ValueError('--join covers protocols 3 and 4 without live inputs or peer links')
        self.absent = set(absent)
        if not self.absent <= set(self.slots) or len(self.absent) >= self.clients:
            raise ValueError('absent slots must leave at least one present slot')
        self.controls = {}         # (tick, slot) -> blob for an absent slot's stand-in row
        self.handovers = {}        # slot -> H
        self.join, self.joins, self.join_options = None, [], dict(join_options or {})
        savers = self.join_options.get('savers')
        if savers and (not set(savers) <= set(self.slots)-self.absent):
            raise ValueError('savers must be present slots')
        self.filled = -1           # highest tick whose stand-ins are filled
        self._scanned = -1         # highest released tick scanned for JOIN records

    # -- stand-ins --
    def next_standin_tick(self):
        """The first tick whose stand-in rows are not filled yet (a record placed there reaches every client)."""
        return max(self.filled+1, 0)

    def schedule(self, slot, record_hex, tick=None):
        tick = self.next_standin_tick() if tick is None else tick
        while (tick, slot) in self.controls:
            tick += 1
        self.controls[(tick, slot)] = record_blob(record_hex)
        return tick

    def handover_at(self, slot, tick):
        self.handovers[slot] = tick

    def reference_row(self, tick):
        rows = self.states.get(tick, {})
        present = [s for s in self.slots if s in rows and s not in self.absent]
        if tick > self.released or not present:
            return None
        return rows[min(present)]

    def fill(self, tick):
        rows = self.states.get(tick, {})
        # A slot with a handover tick H is a stand-in before H and a client from H on; otherwise absent = stand-in.
        stand = [s for s in self.slots if (tick < self.handovers[s] if s in self.handovers else s in self.absent)]
        present = [s for s in self.slots if s not in stand]
        if not all(s in rows for s in present) or not present:
            return False
        ref = rows[min(present)]
        for s in stand:
            if s in rows:
                continue                                    # a joined slot's own report stays (after a late failure)
            rows[s] = {'game': ref['game'], 'real': ref['real'], 'digest': ref['digest'], 'blob': self.controls.pop((tick, s), ''),
                       'standin': True}
        self.filled = max(self.filled, tick)
        return True

    # -- the barrier --
    def receive(self, data, address, now):
        if data.startswith(b'NGJOIN|'):
            return self.join_request(data, address, now)
        if data.startswith(b'NGSTATE|'):
            fields = _state_fields(data, self.token)
            if fields and (fields[0] in self.absent or fields[0] in self.handovers):
                slot, tick = fields[0], fields[1]
                h = self.handovers.get(slot)
                joining = self.join and self.join.slot == slot and self.join.state in ('saved', 'catching_up', 'handover')
                if h is None or tick < h:
                    # Before H: the catch-up replay (also a joined slot's resent older reports, never a session row).
                    return self.join.joiner_report(*fields, address, now) if joining else []
                if slot in self.absent:
                    if not (joining and self.join.state == 'handover'):
                        return []
                    self.absent.discard(slot)           # from H on slot k is a client like any other
                    self.join.joined(now)
        out = super().receive(data, address, now)
        self.after_release(now)
        return out

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
            live = [s for s in self.slots if s in self.peers and s not in self.absent]
            if self.failed or tick == self.limit:
                self.done = not self.failed
                return out+[(self.peers[s], self.packet('NGSTOP', s, tick)) for s in live]
            if tick == self.hold_tick and now-self.arrivals.get(tick, now) < self.hold_seconds:
                break
            self.released = tick
            self.released_at[tick] = now
            out += [(self.peers[s], self.packet('NGGO', s, permit)) for permit in self.unlocked(tick) for s in live]
        return out

    def replay_permit(self, slot, permit):
        """The session's own permit of this tick for slot (its rows; None while not released)."""
        source = permit-1-self.lookahead
        if source > self.released:
            return None
        return self.packet('NGGO', slot, permit)

    def after_release(self, now):
        """JOIN records in present clients' released rows; the coordinator's timers."""
        if not self.join:
            return
        for tick in range(self._scanned+1, self.released+1):
            for s, row in sorted(self.states.get(tick, {}).items()):
                if row.get('standin') or not row.get('blob'):
                    continue
                try:
                    record = blob_record(row['blob'])
                except JoinError as error:
                    self.join.notice(f'unreadable_record slot={s} tick={tick} reason={error}')
                    continue
                if record:
                    self.join.saver_record(s, record, tick, now)
            self._scanned = tick
        self.join.poll(now)

    def poll(self, now):
        # The relay loop polls between datagrams: timers run, and JOIN notices print without waiting for a datagram.
        if self.join:
            self.join.poll(now)
            self.notices += self.join.notices
            self.join.notices = []
        return []

    def take_notices(self):
        notices = super().take_notices()
        if self.join:
            notices += self.join.notices
            self.join.notices = []
        return notices

    # -- the launcher --
    def join_request(self, data, address, now):
        f = parse(data, self.token, 'NGJOIN', 2)
        if not f or not f[0].isdigit() or len(f[1]) != 16 or set(f[1])-HEX:
            return []
        slot, nonce = int(f[0]), f[1]
        if self.join and self.join.slot == slot and self.join.nonce == nonce:
            pass                                       # a poll: answer with the current state
        elif self.join and self.join.state not in ('joined', 'failed'):
            return [(address, message('NGJOINSTATE', self.token, slot, nonce, 'failed', 0, 0, 0, '-', 'busy'))]
        elif slot not in self.absent or self.failed or self.done:
            return [(address, message('NGJOINSTATE', self.token, slot, nonce, 'failed', 0, 0, 0, '-', 'slot_not_absent'))]
        else:
            self.join = JoinCoordinator(self, slot, nonce, **self.join_options)
            self.joins.append(self.join)
            self.join.start(now)
        return [(address, message('NGJOINSTATE', self.token, *self.join.answer()))]

    def summary(self):
        out = super().summary()
        out['join'] = [{'slot': j.slot, 'state': j.state, 'failure': j.failure, 'save_tick': j.save_tick, 'saver': j.saver,
                        'saved': j.saved, 'handover': j.handover, 'joiner_tick': j.joiner_tick, 'joiner_rate': round(j.rate(), 2)}
                       for j in self.joins]
        out['absent'] = sorted(self.absent)
        return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    v = sub.add_parser('verify', help='verify a join package (every hash, record shape and the portable digest)')
    v.add_argument('package', type=Path)
    v.add_argument('--save', type=Path, help='also check the save file against the SHA-256 the package names')
    v.add_argument('--sha', help='the package SHA-256 the SAVED record announced')
    c = sub.add_parser('compare', help="two savers' packages of one tick: equal client-independent content?")
    c.add_argument('left', type=Path)
    c.add_argument('right', type=Path)
    r = sub.add_parser('record', help='decode a JOIN record payload or a blob carrying one')
    r.add_argument('hex')
    args = p.parse_args(argv)
    try:
        if args.command == 'verify':
            package = read_package(args.package)
            result = summary(package)
            if args.sha and args.sha != package['sha']:
                raise JoinError('package_sha_announced')
            if args.save:
                if hashlib.sha256(args.save.read_bytes()).hexdigest() != result['save_sha']:
                    raise JoinError('save_sha')
                result['save_verified'] = True
            print(json.dumps(result, indent=1))
        elif args.command == 'compare':
            result = compare_packages(read_package(args.left), read_package(args.right))
            print(json.dumps(result, indent=1))
            return 0 if result['equal'] else 1
        else:
            try:
                print(json.dumps(decode_record(args.hex)))
            except JoinError:
                print(json.dumps(blob_record(args.hex)))
    except (JoinError, OSError) as error:
        print(f'refused {error}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
