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
     5 ABORT     + reason u8                     relay: the join of slot k ended without a handover; reason 16 dropped:
                                                 the relay dropped slot k from this row (--stall-drop-ms, roadmap 15)
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
   Drop and rejoin (roadmap 15; --stall-drop-ms, --rejoin): a client the others waited for that long is dropped if its
   slot may be (a --rejoin slot, present from the start, or an absent slot after its handover): from its first missing
   row the relay stands in for it, that row carries ABORT(k, dropped), so every client makes the slot neutral from the same
   row (gate.pilot_abort), the slot is absent again and a later NGJOIN takes it like any absent slot. Its old address is
   answered NGDROP; its real clock base becomes the reference slot's (a rejoiner restores the saver's clocks).

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
           13: 'pause_too_long', 14: 'refused_by_relay', 15: 'platform', 16: 'dropped'}
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
PACKAGE_VERSION = 13          # 1: GATE..LUA; 2: and HEALTH, POSE; 3: and SHIELD, NAV, ATTN, KEYS; 4: and PATH, REGION; 5: and FLIGHT,
PACKAGE_VERSIONS = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13)   # SPLINE; 6: and AGENT; 7: and ROUTE; 8: and PARAM, DOCK; 9: and STEER,
#                                                        STEERPT; 10: and SOLVER, BULLET; 11: and GRAV, GRAVC, GRAVS; 12: and SPLM, SPLME, SPLMSEG, GRAVN, GRAVP, GRAVQ; 13: and JOBE, EVQT, GRAVD, VIEWK, VIEWKC, VIEWF, VIEWP, SHOOT, ATTK, LIVEF, SPEC, NAVC, JOBS, GONE, BODY, BUPD, LDC, NAVT, NMESH, NOCT, AGTT, AGCB, PBNK, PEND, GRAVB, VIEWL, IKV, RCNT, ECOS, SHPW, TRDN, NPCA, JOBG, CODE, ENGR, SHGR, BNDS
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
SEC_SPLM, SEC_SPLME, SEC_SPLMSEG, SEC_GRAVN, SEC_GRAVP, SEC_GRAVQ, SEC_JOBE, SEC_EVQT, SEC_GRAVD = 30, 31, 32, 33, 34, 35, 36, 37, 38
SEC_VIEWK, SEC_VIEWKC, SEC_VIEWF, SEC_VIEWP, SEC_SHOOT, SEC_ATTK, SEC_LIVEF, SEC_SPEC, SEC_NAVC, SEC_JOBS, SEC_GONE, SEC_BODY, SEC_BUPD, SEC_LDC, SEC_NAVT, SEC_NMESH, SEC_NOCT, SEC_AGTT, SEC_AGCB, SEC_PBNK, SEC_PEND, SEC_GRAVB, SEC_VIEWL, SEC_IKV, SEC_RCNT, SEC_ECOS, SEC_SHPW, SEC_TRDN, SEC_NPCA, SEC_JOBG, SEC_CODE, SEC_ENGR, SEC_SHGR, SEC_BNDS, SEC_OMGS, SEC_RESV, SEC_PCSV, SEC_CONN = 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76
SECTION_NAMES = {SEC_GATE: 'gate', SEC_IDS: 'ids', SEC_WAKE: 'wake', SEC_WAKEVEC: 'wakevec', SEC_MDHEAP: 'mdheap',
                 SEC_MODULES: 'modules', SEC_CTX: 'ctx', SEC_LUA: 'lua', SEC_HEALTH: 'health', SEC_POSE: 'pose',
                 SEC_SHIELD: 'shield', SEC_NAV: 'nav', SEC_ATTN: 'attn', SEC_KEYS: 'keys', SEC_PATH: 'path', SEC_REGION: 'region',
                 SEC_FLIGHT: 'flight', SEC_SPLINE: 'spline', SEC_AGENT: 'agent', SEC_ROUTE: 'route', SEC_PARAM: 'param', SEC_DOCK: 'dock',
                 SEC_STEER: 'steer', SEC_STEERPT: 'steerpt', SEC_SOLVER: 'solver', SEC_BULLET: 'bullet',
                 SEC_GRAV: 'grav', SEC_GRAVC: 'gravc', SEC_GRAVS: 'gravs', SEC_SPLM: 'splm', SEC_SPLME: 'splme',
                 SEC_SPLMSEG: 'splmseg', SEC_GRAVN: 'gravn', SEC_GRAVP: 'gravp',
                 SEC_GRAVQ: 'gravq', SEC_JOBE: 'jobe',
                 SEC_EVQT: 'evqt', SEC_GRAVD: 'gravd', SEC_VIEWK: 'viewk', SEC_VIEWKC: 'viewkc', SEC_VIEWF: 'viewf', SEC_VIEWP: 'viewp', SEC_SHOOT: 'shoot', SEC_ATTK: 'attk', SEC_LIVEF: 'livef', SEC_SPEC: 'spec', SEC_NAVC: 'navc', SEC_JOBS: 'jobs', SEC_GONE: 'gone', SEC_BODY: 'body', SEC_BUPD: 'bupd', SEC_LDC: 'ldc', SEC_NAVT: 'navt', SEC_NMESH: 'nmesh', SEC_NOCT: 'noct', SEC_AGTT: 'agtt', SEC_AGCB: 'agcb', SEC_PBNK: 'pbnk', SEC_PEND: 'pend', SEC_GRAVB: 'gravb', SEC_VIEWL: 'viewl', SEC_IKV: 'ikv', SEC_RCNT: 'rcnt', SEC_ECOS: 'ecos', SEC_SHPW: 'shpw', SEC_TRDN: 'trdn', SEC_NPCA: 'npca', SEC_JOBG: 'jobg', SEC_CODE: 'code', SEC_ENGR: 'engr', SEC_SHGR: 'shgr', SEC_BNDS: 'bnds', SEC_OMGS: 'omgs', SEC_RESV: 'resv', SEC_PCSV: 'pcsv', SEC_CONN: 'conn'}
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
WORLD12_SECTIONS = (SEC_SPLM, SEC_SPLME, SEC_SPLMSEG, SEC_GRAVN, SEC_GRAVP, SEC_GRAVQ)   # version 12, always all six
WORLD13_SECTIONS = (SEC_JOBE, SEC_EVQT, SEC_GRAVD, SEC_VIEWK, SEC_VIEWKC, SEC_VIEWF, SEC_VIEWP, SEC_SHOOT, SEC_ATTK,
                    SEC_LIVEF, SEC_SPEC, SEC_NAVC, SEC_JOBS, SEC_GONE, SEC_BODY, SEC_BUPD, SEC_LDC, SEC_NAVT, SEC_NMESH, SEC_NOCT, SEC_AGTT, SEC_AGCB, SEC_PBNK, SEC_PEND, SEC_GRAVB, SEC_VIEWL, SEC_IKV, SEC_RCNT, SEC_ECOS, SEC_SHPW, SEC_TRDN, SEC_NPCA, SEC_JOBG, SEC_CODE, SEC_ENGR, SEC_SHGR, SEC_BNDS, SEC_OMGS, SEC_RESV, SEC_PCSV, SEC_CONN)   # version 13, all forty-one
LAST_SECTION = {1: SEC_LUA, 2: SEC_POSE, 3: SEC_KEYS, 4: SEC_REGION, 5: SEC_SPLINE, 6: SEC_AGENT, 7: SEC_ROUTE, 8: SEC_DOCK, 9: SEC_STEERPT,
                10: SEC_BULLET, 11: SEC_GRAVS, 12: SEC_GRAVQ, 13: SEC_CONN}
TEXT_SECTIONS = (SEC_MODULES, SEC_LUA)          # newline-terminated printable lines; count = lines
# Fixed record shapes (lockstep_join.h join_*_rec). GATE is one 256-byte record; MODULES and LUA (the probe's own state
# providers, native_gate_join_core.lua) are text (count = lines).
GATE = struct.Struct('<QQQIIIIIIQQIIIIIIIIIIII64s16sQQII12s12sQQ')
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
                 0x2c4d288: ((0x10, 2, 0), (0x20, 0x40, 1), (0x60, 10, 0)),                        # EvasiveSimpleSteeringBehaviour
                 0x2c4e3c0: ((0x10, 2, 0), (0x18, 4, 0), (0x20, 8, 0), (0x28, 1, 0)),              # SpiralBehaviour
                 0x2c4db70: ((0x10, 2, 0), (0x20, 0x40, 1), (0x60, 0x18, 0), (0x80, 4, 0), (0x88, 8, 0)),   # DogfightBehaviour2
                 0x2c4dcf0: ((0x10, 2, 0), (0x18, 4, 0), (0x20, 0x18, 0), (0x38, 1, 0)),           # CrazyIvanBehaviour
                 0x2c4d1c8: ((0x10, 2, 0), (0x20, 0x40, 1), (0x60, 10, 0)),                        # EvasiveRandomSteeringBehaviour
                 0x2c4f0c0: ((0x10, 2, 0), (0x20, 0x10, 1), (0x30, 4, 0)),                         # ParallelPathSteeringBehaviour
                 0x2c4ed00: ((0x10, 2, 0), (0x18, 4, 0), (0x20, 0x10, 0), (0x30, 0x10, 1), (0x40, 1, 0)),   # FrontalCapitalCombat...
                 0x2c4e6c0: ((0x10, 2, 0), (0x18, 8, 0), (0x20, 0x40, 1))}                         # MiningBehaviour


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
SOLVER_HEAP = 0x8000000000000007              # version 12: a qword that was an address of the saver's memory (the joiner keeps its own)
PCSV_VT = 0x2c39488                           # PCSV's kind: U::PlayerController (lockstep_pilot_slots.h PSLOT_VTABLE)
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
# Version 12 (lockstep_join.h join_splm_rec, join_splme_rec, join_gravn_rec; report 1.16): the spline models and the Gravidar zone groups'
# neighbours. SPLM: portable, flags, entries[2], requests, segments[2], pairs[2], reserved[2], the model's 0x4c0 bytes. SPLME: portable,
# list, index, flags, segments, pairs, 0, the entry's 0x50 bytes, its spline's 0x50 bytes. SPLMSEG: SPLINE's records (part list*2+kind,
# index element << 16 | k). GRAVN: sector, zone, neighbour, flags, 0. GRAVP: a radar owner's (or its ancestor's) published view pose
# (object, rel view 0 current / 1 the other, flags PRESENT, the 0x40-byte transform; zero without PRESENT). GRAVQ: the radar worker's
# pending command list at T in order (source portable ID, index, kind, flags UNMAPPED/OTHER, event vtable RVA, time bits, event qwords).
SPLM_BYTES, SPLM_ENTRIES = 0x4c0, 64
SPLM_REQUESTS, SPLM_UNCARRIED = 1, 2
SPLME_SPLINE = 1
GRAVN_NONE = 4
GRAVP_PRESENT = 1
GRAVQ_UNMAPPED, GRAVQ_OTHER, GRAVQ_EVENT_VT = 1, 2, 0x2baf7d8
JOBE_ENABLED, JOBE_RUNNING, JOBE_GATE, JOBE_CURSOR = 1, 2, 4, 112   # JOBE: the job engine's last run (bits), flags, the cursor job ID
SPLM = struct.Struct('<QI2II2I2I2I%ds' % SPLM_BYTES)
SPLME = struct.Struct('<QIIIIII80s80s')
GRAVN = struct.Struct('<QQQII')
GRAVP = struct.Struct('<QII64s')
GRAVQ = struct.Struct('<QIIIIQQQ')
JOBE = struct.Struct('<QII%ds' % JOBE_CURSOR)
EVQT = struct.Struct('<QIIIIQ')   # EVQT: source portable, class RVA, type ID, place, flags, time bits
EVQT_UNMAPPED = 1
GRAVD = struct.Struct('<QQII')   # GRAVD: ship, cached parent (portable IDs), view (relative), flags NONE/UNMAPPED
GRAVD_NONE, GRAVD_UNMAPPED = 1, 2
VIEWK = struct.Struct('<QIIII')   # VIEWK: component, view (relative), flags UNMAPPED/UNREAD, the view flags, the children count
VIEWKC = struct.Struct('<QQII')   # VIEWKC: component, child (portable IDs), view, place
VIEWK_UNMAPPED, VIEWK_UNREAD, VIEWK_CHILDREN = 1, 2, 65536
VIEWF = struct.Struct('<QIIII')   # VIEWF: component, its view flags and published attention (current view, the other)
VIEWP = struct.Struct('<Q64s')   # VIEWP: a zone-held component, its settled published pose
# SHOOT: a weapon, its last shot time, its controller's +0x28 +0x40 +0x48 +0x50 +0x58 (double bits), aim, controller vtable RVA,
# the weapon's +0x30c..+0x30e, the controller's +0x84 +0x85, has a controller, padding
SHOOT = struct.Struct('<QQ5Q32sI3s2sB2sI')   # ... has controller, padding, the controller's aim cone (+0x80, float bits)
ATTK = struct.Struct('<QQ3QII')   # ATTK: a destructible, its attacker (portable), attack times (bits), method, padding
LIVEF = struct.Struct('<QII')   # LIVEF: a component, the live flag bits to set and to clear (0x20 only)
SPEC = struct.Struct('<QIIIIQQ')   # SPEC: a component, a view cache specification entry: kind, flags, class, truncated, interval, last
NAVC = struct.Struct('<Q96s')   # NAVC: a ship, its navigation controller's cached transform and velocity (+0x60..+0xbf)
JOBS = struct.Struct('<QQIHH')  # JOBS: a job's ID hash (FNV-1a), its state's +0x48, its place in the job DB, active and waiting ships
GONE = struct.Struct('<Q')      # GONE: a census row of the session whose object is gone at T (portable ID, sorted)
BODY = struct.Struct('<QII8I23II8I8I')   # BODY: portable, which, record flags, pose, motion (23 words), moving, pre, delta
BUPD = struct.Struct('<QQ')     # BUPD: a class-0x6f object's last build update (+0xa58, the double's bits)
LDC = struct.Struct('<Q113s7x')   # LDC: a LinearDecelerationController's fields (transform, start, velocities, duration, flag)
NAVT = struct.Struct('<Q30I')     # NAVT: a navigation controller's bank vectors +0x60..+0xaf, x, y, z lanes, two banks by role
NMESH = struct.Struct('<Q')       # NMESH: the nav mesh receipt, its build count, then the folded terms (sorted, each once)
NOCT = struct.Struct('<QQQQIBBBB')   # NOCT: the octree index table: a, b, c, d, class, flag, kind (0 head, 1 entry, 2 hidden), agreed, shown
AGTT = struct.Struct('<QQQQ')        # AGTT: per AGENT record, the Agent's times +0x658, +0xf40, +0xf38 (the doubles' bits)
AGCB = struct.Struct('<Q1024s')      # AGCB: a pilot slot ship's Agent's command and probe block (+0x6e0, raw)
PBNK = struct.Struct('<QII'+NAV_BANK*2+'30I')   # PBNK: a pilot slot ship's Agent's banks by role (NAV's fields, NAVT's vectors)
PEND = struct.Struct('<QII')         # PEND: a component's nonzero pending view cache flags (+0x1e0), padding
GRAVB = struct.Struct('<QI3I')       # GRAVB: the Gravidar's mapped bounds by role (0 read, 1 write, 2 base): sector, x, y, r bits
VIEWL = struct.Struct('<QBB6s')      # VIEWL: an object's live view and player view flags (+0x3c8, +0x3c9), either set
IKV = struct.Struct('<QHHI24s')      # IKV: a skeleton IK joint's group, index and bytes +0x08..+0x1f (value, time, rate, target)
SHPW = struct.Struct('<Q6Q')        # SHPW: a ship's object words a load leaves at defaults
TRDN = struct.Struct('<Qd')         # TRDN: a container's next trade offer update (+0x928)
NPCA = struct.Struct('<QII48s')     # NPCA: an active NPC's flags, animation index and bytes +0x5b0 .. +0x5e0
JOBG = struct.Struct('<QQ')         # JOBG: a job's own generator word *(job+0x528), by its ID's FNV-1a hash
CODE = struct.Struct('<II')         # CODE: the object code counter and the game's code seed (one record)
ENGR = struct.Struct('<QdQ')        # ENGR: an engine's boost ramp time (+0x2f0) and factor word (+0x2f8)
RESV = struct.Struct('<QQQ112sd')   # RESV: a zone's space reservation (owner, index): box node+0x30 .. +0x9f raw, expiry +0xa0
CONN = struct.Struct('<QQQQ')       # CONN: an NPC's (or its partner's) connection (owner, ID) -> its partner (owner, ID), portable owners
OMGS = struct.Struct('<QII8760s')   # OMGS: a steering ship's OmegaSwitched controller (which 0/1), +0x10 .. +0x2248 raw
BNDS = struct.Struct('<Q27II')      # BNDS: a station's three bounding structs (+0xc90, +0xcc0, +0xcf0: 9 words each)
SHGR = struct.Struct('<QQIIdd')     # SHGR: a container's shield recharge group for a ship: value (+0x68), changed time (+0x70)
ECOS = struct.Struct('<Qdd')         # ECOS: a station's, build storage's or ship's next and last economy run (+0xa38, +0xa40)
RCNT = struct.Struct('<QII')         # RCNT: a created ship's or bay's reference count (+0x40)
assert CONN.size == 32 and RESV.size == 144 and SPLM.size == 1264 and SPLME.size == 192 and GRAVN.size == 32 and GRAVP.size == 80 and GRAVQ.size == 48 and JOBE.size == 128 and EVQT.size == 32 and GRAVD.size == 24 and VIEWK.size == 24 and VIEWKC.size == 24 and VIEWF.size == 24 and VIEWP.size == 72 and SHOOT.size == 104 and ATTK.size == 48 and LIVEF.size == 16 and SPEC.size == 40 and NAVC.size == 104 and JOBS.size == 24 and GONE.size == 8 and BODY.size == 208 and BUPD.size == 16 and LDC.size == 128 and NAVT.size == 128 and NMESH.size == 8 and NOCT.size == 40 and AGTT.size == 32 and AGCB.size == 1032 and PBNK.size == 376 and PEND.size == 16 and GRAVB.size == 24 and VIEWL.size == 16 and IKV.size == 40 and RCNT.size == 16 and ECOS.size == 24 and SHPW.size == 56 and TRDN.size == 16 and NPCA.size == 64 and JOBG.size == 16 and CODE.size == 8 and ENGR.size == 24 and SHGR.size == 40 and BNDS.size == 120 and OMGS.size == 8776
RECORDS = {SEC_IDS: IDS, SEC_CTX: CTX, SEC_WAKE: WAKE, SEC_WAKEVEC: WAKEVEC, SEC_MDHEAP: MDHEAP, SEC_HEALTH: HEALTH, SEC_POSE: POSE,
           SEC_SHIELD: SHIELD, SEC_NAV: NAV, SEC_ATTN: ATTN, SEC_KEYS: KEYS, SEC_PATH: PATH, SEC_REGION: REGION,
           SEC_FLIGHT: FLIGHT, SEC_SPLINE: SPLINE, SEC_AGENT: AGENT, SEC_ROUTE: ROUTE, SEC_PARAM: PARAM, SEC_DOCK: DOCK,
           SEC_STEER: STEER, SEC_STEERPT: ROUTE, SEC_SOLVER: SOLVER, SEC_BULLET: BULLET, SEC_GRAV: GRAV, SEC_GRAVC: GRAVC,
           SEC_GRAVS: GRAVS, SEC_SPLM: SPLM, SEC_SPLME: SPLME, SEC_SPLMSEG: SPLINE, SEC_GRAVN: GRAVN,
           SEC_GRAVP: GRAVP, SEC_GRAVQ: GRAVQ, SEC_JOBE: JOBE, SEC_EVQT: EVQT, SEC_GRAVD: GRAVD, SEC_VIEWK: VIEWK, SEC_VIEWKC: VIEWKC, SEC_VIEWF: VIEWF, SEC_VIEWP: VIEWP, SEC_SHOOT: SHOOT, SEC_ATTK: ATTK, SEC_LIVEF: LIVEF, SEC_SPEC: SPEC, SEC_NAVC: NAVC, SEC_JOBS: JOBS, SEC_GONE: GONE, SEC_BODY: BODY, SEC_BUPD: BUPD, SEC_LDC: LDC, SEC_NAVT: NAVT, SEC_NMESH: NMESH, SEC_NOCT: NOCT, SEC_AGTT: AGTT, SEC_AGCB: AGCB, SEC_PBNK: PBNK, SEC_PEND: PEND, SEC_GRAVB: GRAVB, SEC_VIEWL: VIEWL, SEC_IKV: IKV, SEC_RCNT: RCNT, SEC_ECOS: ECOS, SEC_SHPW: SHPW, SEC_TRDN: TRDN, SEC_NPCA: NPCA, SEC_JOBG: JOBG, SEC_CODE: CODE, SEC_ENGR: ENGR, SEC_SHGR: SHGR, SEC_BNDS: BNDS, SEC_OMGS: OMGS, SEC_RESV: RESV, SEC_PCSV: SOLVER, SEC_CONN: CONN}
GATE_FIELDS = ('tick', 'game', 'real', 'wire_version', 'clients', 'lookahead', 'tick_hz', 'slot', 'md_seq', 'rid_next',
               'created_generation', 'created_live', 'identities', 'ctx_count', 'wake_count', 'vec_index', 'vec0', 'vec1',
               'md_count', 'md_void', 'md_release', 'md_queued', 'unsupported', 'digest', 'portable', 'save_us',
               'capture_us', 'ambiguous', 'flags', 'fixture', 'flight', 'cid', 'unpaused')
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


def splm_shape(splm, splme, splmseg):
    """lockstep_join_world.h join_splm_shape: SPLM by portable ID, known flags, requests with their flag, nothing under an uncarried
    record; SPLME exactly entries[0] then entries[1] entries per model (entry pointer 0, no counts without a spline); SPLMSEG exactly each
    spline's segments (part list*2, BezierSpline vtable 0) and ceil(pairs/20) pair records (part list*2+1, unused tail 0) in (part, index)
    order."""
    ei = gi = 0
    last = 0
    for r in splm:
        portable, flags, e0, e1, requests, s0, s1, p0, p1, r0, r1, _ = r
        if not portable or portable <= last or flags & ~3 or r0 or r1 or (not flags & SPLM_REQUESTS) != (not requests) or \
                e0 > SPLM_ENTRIES or e1 > SPLM_ENTRIES:
            return False
        if flags & SPLM_UNCARRIED and (e0 or e1 or s0 or s1 or p0 or p1):
            return False
        last = portable
        seg = {0: [s0], 1: [s1], 2: [], 3: []}
        pair = {0: [p0], 1: [p1], 2: [], 3: []}
        for lst, count in ((0, e0), (1, e1)):
            for k in range(count):
                if ei >= len(splme):
                    return False
                x = splme[ei]
                if x[0] != portable or x[1] != lst or x[2] != k or x[3] & ~1 or x[6] or (not x[3] & SPLME_SPLINE and (x[4] or x[5])) or \
                        x[4] > FLIGHT_SEGMENTS or x[5] > FLIGHT_PAIRS or any(x[7][:8]):
                    return False
                seg[2+lst].append(x[4])
                pair[2+lst].append(x[5])
                ei += 1
        for lst in range(4):
            for kind in (0, 1):
                for element, (ns, np_) in enumerate(zip(seg[lst], pair[lst])):
                    want = (np_+FLIGHT_PAIRS_PER-1)//FLIGHT_PAIRS_PER if kind else ns
                    for k in range(want):
                        if gi >= len(splmseg):
                            return False
                        owner, part, index, data = splmseg[gi]
                        if owner != portable or part != lst*2+kind or index != (element << 16 | k):
                            return False
                        if not kind and any(data[FLIGHT_BEZIER_AT:FLIGHT_BEZIER_AT+8]):
                            return False
                        if kind:
                            used = min(np_-k*FLIGHT_PAIRS_PER, FLIGHT_PAIRS_PER)
                            if any(data[used*8:]):
                                return False
                        gi += 1
    return ei == len(splme) and gi == len(splmseg)


def gravn_shape(gravn):
    """lockstep_join_world.h join_gravn_shape: strictly by (sector, zone, neighbour), a sector and a zone, NONE alone for its zone with
    neighbour 0, else a neighbour."""
    for i, (sector, zone, neighbour, flags, reserved) in enumerate(gravn):
        if not sector or not zone or reserved or flags & ~GRAVN_NONE or (not neighbour) != bool(flags & GRAVN_NONE):
            return False
        if i and gravn[i-1][:3] >= (sector, zone, neighbour):
            return False
        if flags & GRAVN_NONE and ((i and gravn[i-1][:2] == (sector, zone)) or (i+1 < len(gravn) and gravn[i+1][:2] == (sector, zone))):
            return False
    return True


def gravp_shape(gravp):
    """lockstep_join_world.h join_gravp_shape: strictly by (object, view), a portable ID, view 0 or 1, only PRESENT, no pose without it."""
    for i, (portable, rel, flags, pose) in enumerate(gravp):
        if not portable or rel > 1 or flags & ~GRAVP_PRESENT or (i and gravp[i-1][:2] >= (portable, rel)):
            return False
        if not flags & GRAVP_PRESENT and any(pose):
            return False
    return True


def gravq_shape(gravq):
    """lockstep_join_world.h join_gravq_shape: by index from 0, flags UNMAPPED/OTHER only, no portable ID exactly with UNMAPPED."""
    for i, (portable, index, kind, flags, event, time, q0, q1) in enumerate(gravq):
        if index != i or flags & ~3 or (not portable) != bool(flags & GRAVQ_UNMAPPED):
            return False
    return True


def jobe_shape(jobe):
    """lockstep_join_world.h join_jobe_shape: at most one, flags ENABLED/RUNNING/GATE only, a printable cursor shorter than the field."""
    if len(jobe) > 1:
        return False
    for last, flags, length, cursor in jobe:
        if flags & ~7 or length >= JOBE_CURSOR or any(cursor[length:]) or any(not 33 <= c <= 126 for c in cursor[:length]):
            return False
    return True


def _evqt_key(r):
    return (r[0], r[1], struct.unpack('<d', struct.pack('<Q', r[5]))[0], r[3])


def evqt_shape(evqt):
    """lockstep_join_world.h join_evqt_shape: sorted by (portable, class, time, place), flags UNMAPPED only, exactly without a source."""
    for i, r in enumerate(evqt):
        if r[4] & ~EVQT_UNMAPPED or (not r[0]) != bool(r[4] & EVQT_UNMAPPED) or (i and _evqt_key(evqt[i-1]) > _evqt_key(r)):
            return False
    return True


def gravd_shape(gravd):
    """lockstep_join_world.h join_gravd_shape: by (ship, view) each once, view 0/1, flags NONE or UNMAPPED, a parent exactly without."""
    for i, (portable, parent, rel, flags) in enumerate(gravd):
        if not portable or rel > 1 or flags & ~3 or flags == 3 or (not parent) != bool(flags) or (i and gravd[i-1][0:3:2] >= (portable, rel)):
            return False
    return True


def viewk_shape(viewk, viewkc):
    """lockstep_join_world.h join_viewk_shape: by (component, view) each once, view 0/1, flags UNMAPPED/UNREAD, UNREAD without children;
    VIEWKC exactly each record's children in order, a child 0 exactly under UNMAPPED."""
    j = 0
    for i, (portable, rel, flags, vflags, count) in enumerate(viewk):
        if not portable or rel > 1 or flags & ~3 or count > VIEWK_CHILDREN or (flags & VIEWK_UNREAD and count) or \
                (i and viewk[i-1][0:2] >= (portable, rel)):
            return False
        zero = False
        for x in range(count):
            if j >= len(viewkc) or (viewkc[j][0], viewkc[j][2], viewkc[j][3]) != (portable, rel, x):
                return False
            zero = zero or not viewkc[j][1]
            j += 1
        if zero != bool(flags & VIEWK_UNMAPPED):
            return False
    return j == len(viewkc)


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
        gate['flight'] = list(gate['flight'])   # each fixture actor's flight sample init generation at T
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
    if magic != MAGIC or version not in PACKAGE_VERSIONS or reserved or count > 76:
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
            (version >= 11 and not all(SECTION_NAMES[k] in sections for k in WORLD11_SECTIONS)) or \
            (version >= 12 and not all(SECTION_NAMES[k] in sections for k in WORLD12_SECTIONS)) or \
            (version >= 13 and not all(SECTION_NAMES[k] in sections for k in WORLD13_SECTIONS)):
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
    if version >= 12 and not splm_shape(sections['splm'], sections['splme'], sections['splmseg']):
        raise JoinError('splm_shape')
    if version >= 12 and not gravn_shape(sections['gravn']):
        raise JoinError('gravn_shape')
    if version >= 12 and not gravp_shape(sections['gravp']):
        raise JoinError('gravp_shape')
    if version >= 12 and not gravq_shape(sections['gravq']):
        raise JoinError('gravq_shape')
    if version >= 13 and not jobe_shape(sections['jobe']):
        raise JoinError('jobe_shape')
    if version >= 13 and not evqt_shape(sections['evqt']):
        raise JoinError('evqt_shape')
    if version >= 13 and not gravd_shape(sections['gravd']):
        raise JoinError('gravd_shape')
    if version >= 13 and not viewk_shape(sections['viewk'], sections['viewkc']):
        raise JoinError('viewk_shape')
    if version >= 13 and any(not r[0] or (i and sections['viewf'][i-1][0] >= r[0]) for i, r in enumerate(sections['viewf'])):
        raise JoinError('viewf_shape')
    if version >= 13 and any(not r[0] or (i and sections['viewp'][i-1][0] >= r[0]) for i, r in enumerate(sections['viewp'])):
        raise JoinError('viewp_shape')
    if version >= 13 and any(not r[0] or (i and sections['shoot'][i-1][0] >= r[0]) or r[11] > 1 or (not r[8]) != (not r[11])
                             or max(r[9]) > 1 or max(r[10]) > 1 or any(r[12]) or (not r[11] and r[13]) for i, r in enumerate(sections['shoot'])):
        raise JoinError('shoot_shape')
    if version >= 13 and any(not r[0] or (i and sections['attk'][i-1][0] >= r[0]) or r[6] or not (r[2] | r[3] | r[4])
                             for i, r in enumerate(sections['attk'])):
        raise JoinError('attk_shape')
    if version >= 13 and any(not r[0] or (i and sections['livef'][i-1][0] >= r[0]) or r[1] & r[2] or (r[1] | r[2]) & ~0x20 or not (r[1] | r[2])
                             for i, r in enumerate(sections['livef'])):
        raise JoinError('livef_shape')
    if version >= 13 and any(not r[0] or (i and sections['spec'][i-1][:2] >= r[:2]) for i, r in enumerate(sections['spec'])):
        raise JoinError('spec_shape')
    if version >= 13 and any(not r[0] or (i and sections['navc'][i-1][0] >= r[0]) for i, r in enumerate(sections['navc'])):
        raise JoinError('navc_shape')
    if version >= 13 and any(not r[0] or (i and sections['jobs'][i-1][2] >= r[2]) for i, r in enumerate(sections['jobs'])):
        raise JoinError('jobs_shape')
    if version >= 13 and any(not r[0] or (i and sections['gone'][i-1][0] >= r[0]) for i, r in enumerate(sections['gone'])):
        raise JoinError('gone_shape')
    if version >= 13 and any(not r[0] or r[1] > 1 or r[34] > 1 or (i and (sections['body'][i-1][0], sections['body'][i-1][1]) >= (r[0], r[1]))
                             for i, r in enumerate(sections['body'])):
        raise JoinError('body_shape')
    if version >= 13 and any(not r[0] or (i and sections['bupd'][i-1][0] >= r[0]) for i, r in enumerate(sections['bupd'])):
        raise JoinError('bupd_shape')
    if version >= 13 and any(not r[0] or (i and sections['ldc'][i-1][0] >= r[0]) for i, r in enumerate(sections['ldc'])):
        raise JoinError('ldc_shape')
    if version >= 13 and any(not r[0] or (i and sections['navt'][i-1][0] >= r[0]) for i, r in enumerate(sections['navt'])):
        raise JoinError('navt_shape')
    nmesh = sections.get('nmesh', [])
    if version >= 13 and (len(nmesh) == 1 or any(nmesh[i-1][0] >= nmesh[i][0] for i in range(3, len(nmesh)))):
        raise JoinError('nmesh_shape')
    if version >= 13 and not noct_shape(sections.get('noct', [])):
        raise JoinError('noct_shape')
    agtt = sections.get('agtt', [])
    if version >= 13 and agtt and (len(agtt) != len(sections.get('agent', [])) or
                                   any(r[0] != a[0] for r, a in zip(agtt, sections.get('agent', [])))):
        raise JoinError('agtt_shape')
    if version >= 13 and any(not r[0] or (i and sections['agcb'][i-1][0] >= r[0]) for i, r in enumerate(sections.get('agcb', []))):
        raise JoinError('agcb_shape')
    if version >= 13 and any(not r[0] or r[2] or r[1] & ~63 or (i and sections['pbnk'][i-1][0] >= r[0])
                             for i, r in enumerate(sections.get('pbnk', []))):
        raise JoinError('pbnk_shape')
    if version >= 13 and any(not r[0] or not r[1] or r[2] or (i and sections['pend'][i-1][0] >= r[0])
                             for i, r in enumerate(sections.get('pend', []))):
        raise JoinError('pend_shape')
    if version >= 13 and any(not r[0] or r[1] > 2 or (i and sections['gravb'][i-1][:2] >= r[:2])
                             for i, r in enumerate(sections.get('gravb', []))):
        raise JoinError('gravb_shape')
    if version >= 13 and any(not r[0] or r[1] > 1 or r[2] > 1 or not (r[1] | r[2]) or any(r[3]) or (i and sections['viewl'][i-1][0] >= r[0])
                             for i, r in enumerate(sections.get('viewl', []))):
        raise JoinError('viewl_shape')
    if version >= 13 and any(not r[0] or r[3] or r[1] >= 16 or r[2] >= 64 or (i and sections['ikv'][i-1][:3] >= r[:3])
                             for i, r in enumerate(sections.get('ikv', []))):
        raise JoinError('ikv_shape')
    if version >= 13 and any(not r[0] >> 63 or r[2] or not r[1] or (i and sections['rcnt'][i-1][0] >= r[0])
                             for i, r in enumerate(sections.get('rcnt', []))):
        raise JoinError('rcnt_shape')
    if version >= 13 and any(not r[0] or (i and sections['ecos'][i-1][0] >= r[0]) for i, r in enumerate(sections.get('ecos', []))):
        raise JoinError('ecos_shape')
    if version >= 13 and any(not r[0] or (i and sections['shpw'][i-1][0] >= r[0]) for i, r in enumerate(sections.get('shpw', []))):
        raise JoinError('shpw_shape')
    if version >= 13 and any(not r[0] or (i and sections['trdn'][i-1][0] >= r[0]) for i, r in enumerate(sections.get('trdn', []))):
        raise JoinError('trdn_shape')
    if version >= 13 and any(not r[0] or not r[1] or r[1] > 3 or (i and sections['npca'][i-1][0] >= r[0])
                             for i, r in enumerate(sections.get('npca', []))):
        raise JoinError('npca_shape')
    if version >= 13 and any(not r[0] or (i and sections['jobg'][i-1][0] >= r[0]) for i, r in enumerate(sections.get('jobg', []))):
        raise JoinError('jobg_shape')
    code = sections.get('code', [])
    if version >= 13 and (len(code) > 1 or any(r[0] > 0x1000000 or r[1] > 0xffffff for r in code)):
        raise JoinError('code_shape')
    if version >= 13 and any(not r[0] or (i and sections['engr'][i-1][0] >= r[0]) for i, r in enumerate(sections.get('engr', []))):
        raise JoinError('engr_shape')
    shgr = sections.get('shgr', [])
    if version >= 13 and any(not r[0] or not r[1] or r[3] or (i and shgr[i-1][:3] >= r[:3]) for i, r in enumerate(shgr)):
        raise JoinError('shgr_shape')
    bnds = sections.get('bnds', [])
    if version >= 13 and any(not r[0] or r[-1] or (i and bnds[i-1][0] >= r[0]) for i, r in enumerate(bnds)):
        raise JoinError('bnds_shape')
    resv = sections.get('resv', [])
    if version >= 13 and any(not r[0] or not r[1] or (i and resv[i-1][:3] >= r[:3]) for i, r in enumerate(resv)):
        raise JoinError('resv_shape')
    if version >= 13 and not solver_shape(sections.get('pcsv', [])):   # PCSV: SOLVER's record, by the ship's portable ID
        raise JoinError('pcsv_shape')
    conn = sections.get('conn', [])   # CONN: keyed ends, sorted by (owner, ID), each once
    if version >= 13 and any(not all(r) or (i and conn[i-1][:2] >= r[:2]) for i, r in enumerate(conn)):
        raise JoinError('conn_shape')
    omgs = sections.get('omgs', [])
    if version >= 13 and any(not r[0] or r[1] > 1 or r[2] or (i and (omgs[i-1][0], omgs[i-1][1]) >= (r[0], r[1])) for i, r in enumerate(omgs)):
        raise JoinError('omgs_shape')
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
    if 'splm' in sections:                     # version 12: which spline models with how many entries, requests, segments, pairs;
        records = sections['splm']             # and every GRAVN record (keys only)
        digest = hashlib.sha256(b''.join(struct.pack('<Q2II2I2I', r[0], r[2], r[3], r[4], r[5], r[6], r[7], r[8]) for r in records)).hexdigest()
        lines.append(f"SPLM|{len(records)}|{len(sections['splme'])}|{len(sections['splmseg'])}|{digest}")
        gravn = sections['gravn']
        lines.append(f"GRAVN|{len(gravn)}|{hashlib.sha256(b''.join(GRAVN.pack(*r) for r in gravn)).hexdigest()}")
        gravp = sections['gravp']              # which objects and views hold a pose; not its bytes
        lines.append(f"GRAVP|{len(gravp)}|{hashlib.sha256(b''.join(struct.pack('<QII', *r[:3]) for r in gravp)).hexdigest()}")
        gravq = sections['gravq']              # which sources, kinds, flags and events in which order; not the times
        lines.append(f"GRAVQ|{len(gravq)}|{hashlib.sha256(b''.join(struct.pack('<QIIII', *r[:5]) for r in gravq)).hexdigest()}")
    if 'jobe' in sections:                     # version 13: the job engine's schedule, whole
        jobe = sections['jobe']
        lines.append(f"JOBE|{len(jobe)}|{hashlib.sha256(b''.join(JOBE.pack(*r) for r in jobe)).hexdigest()}")
    return ''.join(line+'\n' for line in lines)


def _double(bits):
    return struct.unpack('<d', struct.pack('<Q', bits))[0]


def portable_digest(sections):
    return hashlib.sha256(portable_text(sections).encode('ascii')).digest()[:16].hex()


def noct_shape(records):
    """NOCT (lockstep_join.h join_noct_shape): none, or the head (next >= shown), one entry per index shown..next-1 in order
    (flag and live 0/1), then hidden keys (flag 0/1)."""
    if not records:
        return True
    head = records[0]
    if head[6] != 0 or head[1] > head[0]:
        return False
    i, want = 1, head[1]
    while i < len(records) and records[i][6] == 1:
        if records[i][0] != want or records[i][5] > 1 or records[i][3] > 1:
            return False
        want, i = want+1, i+1
    if want != head[0]:
        return False
    return all(r[6] == 2 and r[5] <= 1 for r in records[i:])


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
        (WORLD11_SECTIONS if version >= 11 else ())+(WORLD12_SECTIONS if version >= 12 else ())+(WORLD13_SECTIONS if version >= 13 else ())
    if any(kind in sections for kind in WORLD_SECTIONS+WORLD3_SECTIONS+WORLD4_SECTIONS+WORLD5_SECTIONS+WORLD6_SECTIONS+WORLD7_SECTIONS+
           WORLD8_SECTIONS+WORLD9_SECTIONS+WORLD10_SECTIONS+WORLD11_SECTIONS+WORLD12_SECTIONS+WORLD13_SECTIONS if kind not in allowed):
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
    gate['flight'] = list(gate.get('flight') or [0]*12)   # a 0 or a missing field: every fixture actor's generation 0
    gate['cid'] = int(gate.get('cid') or 0)   # X4's construction ID counter at T (0: not carried)
    gate['unpaused'] = int(gate.get('unpaused') or 0)   # the unpaused (speech) clock's bits at T (0: not carried)
    if len(gate['flight']) != 12 or any(not 0 <= g <= 255 for g in gate['flight']):
        raise JoinError('gate_flight')
    values = [gate[name] for name in GATE_FIELDS]
    values[GATE_FIELDS.index('fixture')] = bytes(int(c) for c in gate['fixture'])
    values[GATE_FIELDS.index('flight')] = bytes(gate['flight'])
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
                 'dock', 'steer', 'steerpt', 'solver', 'bullet', 'grav', 'gravc', 'gravs', 'splm', 'splme', 'splmseg', 'gravn',
                 'gravp', 'gravq', 'jobe', 'evqt', 'gravd', 'viewk', 'viewkc', 'viewf', 'viewp', 'shoot', 'attk', 'livef', 'spec', 'navc', 'jobs', 'gone', 'body', 'bupd', 'ldc', 'navt', 'nmesh', 'noct', 'agtt', 'agcb', 'pbnk', 'pend', 'gravb', 'viewl', 'ikv', 'rcnt', 'ecos', 'shpw', 'trdn', 'npca', 'jobg', 'code', 'engr', 'shgr', 'bnds', 'omgs', 'resv', 'pcsv', 'conn'):
        if name not in left and name not in right:
            continue
        key = (lambda r: (r[1] << 64) | r[0]) if name == 'keys' else (lambda r: r[0])   # KEYS by (table, key)
        if name in ('body', 'omgs'):          # BODY, OMGS by (portable, which): two records an object
            key = lambda r: (r[0] << 1) | r[1]
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
        elif name == 'splme':                  # SPLME by (portable, list, index)
            key = lambda r: (r[1] << 96) | (r[2] << 64) | r[0]
        elif name == 'splmseg':                # SPLMSEG by (portable, part, index)
            key = lambda r: (r[1] << 96) | (r[2] << 64) | r[0]
        elif name == 'gravn':                  # GRAVN by (sector, zone, neighbour)
            key = lambda r: (r[0] << 128) | (r[1] << 64) | r[2]
        elif name == 'gravp':                  # GRAVP by (object, view)
            key = lambda r: (r[1] << 64) | r[0]
        elif name == 'gravq':                  # GRAVQ by its place in the list
            key = lambda r: r[1]
        elif name == 'jobe':                   # JOBE: the one record
            key = lambda r: 0
        elif name == 'evqt':                   # EVQT by (source, class, place)
            key = lambda r: (r[0] << 64) | (r[1] << 32) | r[3]
        elif name == 'gravd':                  # GRAVD by (ship, view)
            key = lambda r: (r[2] << 64) | r[0]
        elif name == 'viewk':                  # VIEWK by (component, view)
            key = lambda r: (r[1] << 64) | r[0]
        elif name == 'viewkc':                 # VIEWKC by (component, view, place)
            key = lambda r: (r[2] << 96) | (r[3] << 64) | r[0]
        x = {key(r): r for r in left.get(name, [])}
        y = {key(r): r for r in right.get(name, [])}
        differ = sorted(i for i in x.keys() & y.keys() if x[i] != y[i])
        out[name] = {'left_only': len(x.keys()-y.keys()), 'right_only': len(y.keys()-x.keys()), 'differ': len(differ),
                     'samples': [_sample(name, i) for i in differ[:samples]]}
        if name == 'steer':                    # the offsets that differ, over every differing model (per process: carry them as 0)
            offsets = collections.Counter(o for i in differ for o in steer_differences(x[i], y[i]))
            out[name]['offsets'] = [f'{o}:{n}' for o, n in offsets.most_common(24)]
        elif name in ('solver', 'bullet', 'pcsv'):   # the same; report 1.14: not in the digest, so a difference here does not refuse the join
            differences = bullet_differences if name == 'bullet' else solver_differences
            offsets = collections.Counter(o for i in differ for o in differences(x[i], y[i]))
            out[name]['offsets'] = [f'{o}:{n}' for o, n in offsets.most_common(96 if name == 'bullet' else 4096)]
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
                 joiner_timeout=900.0, savers=None, observe=False):
        self.b, self.slot, self.nonce = barrier, slot, nonce
        # --join-observe (diagnostic, default off): a joiner's declared digest that differs from the session's before H is
        # counted and noticed, not fatal, so the handover and what follows it can run while the join is not yet equal.
        # Game and real time stay exact. Every line of such a join says diagnostic=join_observe: it is never an acceptance.
        self.observe, self.digest_mismatches, self.first_digest_mismatch = observe, 0, None
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
        self.catchup_started = None   # the first report's time (progress keeps only the last 64 reports)
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
            if self.observe:
                if self.joiner_tick is None or tick > self.joiner_tick:      # each tick once (resent reports repeat it)
                    self.digest_mismatches += 1
                    if self.first_digest_mismatch is None:
                        self.first_digest_mismatch = tick
                        self.notice(f'digest_mismatch first={tick} diagnostic=join_observe (counted, not fatal)')
            else:
                reason = f'mismatch:digest_tick_{tick}'
        if reason:
            self.fail(reason, now)
            return [(address, f'NGSTOP|{version}|{self.b.token}|{slot}|{tick}'.encode('ascii'))]
        first = self.joiner_tick is None
        if first:
            self.address, self.state, self.catchup_started = address, 'catching_up', now
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
        # Since the catch-up began (not progress[0]: that list keeps the last 64 reports, about 2-3 s, so a joiner no faster
        # than the session never reached --join-catchup-limit and was never handed over; rj2, rj3).
        waited = now-(self.catchup_started if self.catchup_started is not None else now)
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
        observed = (f' diagnostic=join_observe digest_mismatches={self.digest_mismatches} '
                    f'first_digest_mismatch={self.first_digest_mismatch if self.first_digest_mismatch is not None else "-"}'
                    if self.observe else '')
        self.notice((f'joined tick={self.handover} catch_up_s={now-self.catchup_started:.1f}' if self.catchup_started is not None
                     else 'joined')+observed)


def _barrier_base():
    from native_gate_relay import Barrier
    return Barrier


class JoinBarrier(_barrier_base()):
    """The relay's Barrier with absent slots (native_gate_relay.py --join). Absent slots are stand-ins: once every
    present slot reported tick t, each absent slot gets the reference slot's game, real and digest and the blob the
    relay scheduled for it (a JOIN record) or ''. Protocols 3 and 4 only; no live inputs, peer links or crypto.
    stall_drop_ms > 0: drops (module docstring, part 3); rejoin: the slots present from the start that may be dropped."""
    def __init__(self, token, absent=(), join_options=None, rejoin=(), stall_report_ms=1000.0, stall_drop_ms=0.0, **kw):
        super().__init__(token, **kw)
        if kw.get('live_inputs') or kw.get('peer_window') is not None:
            raise ValueError('--join covers protocols 3 and 4 without live inputs or peer links')
        self.absent = set(absent)
        if not self.absent <= set(self.slots) or len(self.absent) >= self.clients:
            raise ValueError('absent slots must leave at least one present slot')
        self.rejoin, self.absent_at_start = set(rejoin), frozenset(self.absent)
        if not self.rejoin <= set(self.slots)-{1} or self.rejoin & self.absent:
            raise ValueError('rejoin slots must be 2..N and not absent')
        if self.rejoin and not stall_drop_ms:
            raise ValueError('rejoin slots need a stall drop bound')
        if stall_drop_ms and (not stall_drop_ms > 0 or stall_drop_ms < stall_report_ms):
            raise ValueError('the stall drop bound must be positive and not below the stall report bound')
        self.stall_report, self.stall_drop = stall_report_ms/1000.0, stall_drop_ms/1000.0
        self.dropped = {}          # slot -> the first stand-in row of its current drop
        self.dropped_peers = {}    # slot -> the dropped client's address (answered NGDROP)
        self.stall_seen = set()
        self.stalls, self.drops = [], []
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

    def standing(self, tick):
        """The slots whose row at tick is a stand-in: before its handover tick H a slot is one, from H on a client;
        without a handover an absent slot is one."""
        return [s for s in self.slots if (tick < self.handovers[s] if s in self.handovers else s in self.absent)]

    def fill(self, tick):
        rows = self.states.get(tick, {})
        stand = self.standing(tick)
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
                if slot in self.dropped and address == self.dropped_peers.get(slot) and not joining:
                    return [(address, self.drop_packet(slot))] if self.version == 4 else []
                if h is None or tick < h:
                    # Before H: the catch-up replay (also a joined slot's resent older reports, never a session row).
                    return self.join.joiner_report(*fields, address, now) if joining else []
                if slot in self.absent:
                    if not (joining and self.join.state == 'handover'):
                        return []
                    self.absent.discard(slot)           # from H on slot k is a client like any other
                    self.dropped.pop(slot, None)        # a rejoin: its old address is no longer answered
                    self.dropped_peers.pop(slot, None)
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
        return self.poll_stall(now) if self.stall_drop else []

    # -- drops (roadmap 15) --
    def droppable(self, slot):
        """Every client has the slot in its pilot absent mask, so an ABORT makes it neutral: a --rejoin slot, or a slot
        absent at the start (after its handover)."""
        return slot != 1 and (slot in self.rejoin or slot in self.absent_at_start)

    def drop_packet(self, slot):
        return f'NGDROP|4|{self.token}|{slot}|{self.dropped[slot]}'.encode('ascii')

    def poll_stall(self, now):
        """The present clients the others wait for at the first unreleased tick are reported, then dropped."""
        if self.failed or self.done or self.released < 0:
            return []
        tick = self.released+1
        if tick > self.limit:
            return []
        rows = self.states.get(tick, {})
        stand = self.standing(tick)
        present = [s for s in self.slots if s not in stand]
        missing = [s for s in present if s not in rows]
        if not missing or len(missing) == len(present):
            return []
        since = max(self.arrivals.get(tick, now), self.released_at.get(self.released, now))
        waited = now-since
        for s in missing:
            if waited >= self.stall_report and (s, tick) not in self.stall_seen:
                self.stall_seen.add((s, tick))
                self.stalls.append({'slot': s, 'tick': tick, 'reported_ms': round(1000*waited, 1)})
                self.notices.append(f'NATIVE_GATE STALL slot={s} tick={tick} waited_ms={1000*waited:.0f}'
                                    f'{"" if self.droppable(s) else " droppable=0"}')
        if waited < self.stall_drop:
            return []
        out, dropped = [], False
        for s in missing:
            if self.droppable(s):
                out += self.drop(s, tick, now, waited)
                dropped = True
        return out+self.advance(now) if dropped else []

    def drop(self, slot, tick, now, waited):
        """Slot's rows are stand-ins from tick, the first it did not report; that row carries ABORT(slot, dropped)."""
        ref = min(s for s in self.slots if s not in self.standing(tick) and s != slot)
        self.dropped[slot] = tick
        record = {'slot': slot, 'tick': tick, 'waited_ms': round(1000*waited, 1)}
        base = self.states.get(0, {})
        if slot in base and ref in base and base[slot]['real'] != base[ref]['real']:
            # --relative-real compares each slot's real time since its tick 0: the stand-ins and a rejoiner carry the
            # reference slot's clock from here on.
            record['real_base_was'] = base[slot]['real']
            base[slot] = dict(base[slot], real=base[ref]['real'])
        address = self.peers.pop(slot, None)
        if address is not None:
            self.dropped_peers[slot] = address
        if self.join and self.join.state in ('queued', 'saved', 'catching_up', 'handover'):
            if self.join.slot == slot:
                self.join.fail('joiner_lost:dropped', now)      # its ABORT goes in the first row it did not report
            elif slot in self.join.savers and slot not in self.join.saved:
                self.join.fail('refused_by_relay:saver_dropped', now)
            self.notices += self.join.notices
            self.join.notices = []
        self.absent.add(slot)
        self.handovers.pop(slot, None)
        if (tick, slot) not in self.controls:               # (a failed join's ABORT may be there already)
            self.schedule(slot, encode_record(OP_ABORT, slot, tick, reason=REASON_CODES['dropped']), tick)
        self.drops.append(record)
        self.notices.append(f'NATIVE_GATE DROP slot={slot} tick={tick} waited_ms={1000*waited:.0f} '
                            f'remaining={",".join(str(s) for s in self.slots if s not in self.absent)}')
        return [(address, self.drop_packet(slot))] if address is not None and self.version == 4 else []

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
                        'saved': j.saved, 'handover': j.handover, 'joiner_tick': j.joiner_tick, 'joiner_rate': round(j.rate(), 2),
                        **({'diagnostic': 'join_observe', 'digest_mismatches': j.digest_mismatches,
                            'first_digest_mismatch': j.first_digest_mismatch} if j.observe else {})}
                       for j in self.joins]
        out['absent'] = sorted(self.absent)
        if self.stall_drop:
            out['drop'] = {'stall_report_ms': 1000*self.stall_report, 'stall_drop_ms': 1000*self.stall_drop,
                           'rejoin': sorted(self.rejoin), 'stalls': self.stalls, 'drops': self.drops}
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
