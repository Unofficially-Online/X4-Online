"""Hot join test harness for the live tests J1 and J2 (reports/LOCKSTEP-HOTJOIN-20261101.txt section 9).

session  The lane session's --server-cmd wrapper. Everything after '--' is the relay: its script and its arguments
         (native_gate_relay.py with --join and --absent naming --slot; or a rejoin, roadmap 15: --rejoin naming --slot
         and --stall-drop-ms, the slot is joined again after the relay dropped it, so --join-at must follow the drop). The driver starts it, passes its output through
         unchanged (the lane tool's --server-ready/--server-until/--server-fail-on read it), follows the released tick
         (the relay's 'NATIVE_GATE tick=' lines), sends NGJOIN for --slot once the session released each --join-at tick
         (one join at a time), polls NGJOINSTATE and prints JOIN_DRIVER lines. When a join is saved it finds
         j<T>.xml.gz and j<T>.x4join in every saver's lane profile (<profiles>/<profile>/save/mpjoin; present slot i is
         the i-th lane of --saver-lanes, as the lane tool hands out p1, p2 in --lanes order), verifies them
         (native_gate_join.read_package: every hash and record shape; the SHA-256 each saver announced in its SAVED
         record; the save the package names; the session's wire shape; the same portable content on every saver),
         copies the saver's save and every saver's package to --copy-to/j<T>/ and writes the joiner's handoff
         (--copy-to/handoff.json and --copy-to/j<T>/handoff.json): the session's port, token, slot, wire shape and the
         copied files, read by native_gate_join_overlays.py joiner and native_gate_lane.py --join-session.
replay   J1's relay for a joiner without the session (the session ended; its --input-journal holds every row it
         released). It serves the joiner from the journal as the session relay's catch-up would: the joiner's first
         report must be tick T, every report is checked against the session's reference row (game and real time
         exact, the declared digest from T+1; --observe records every differing tick and goes on, else the first
         difference stops the joiner) and answered with the permits the session itself sent; the joiner is stopped at
         the journal's last tick. It writes a handoff of its own (--handoff-out: its port and token, the session's
         files) and, with a command after '--', runs that command (the joiner's lane run) while it serves.

Lines of 'session' (add 'JOIN_DRIVER FAIL' to the session's --server-fail-on: every failure is loud):
  JOIN_DRIVER READY slot=K join_at=.. expect=.. savers=1:b3,2:b4 port=P copy_to=DIR
  JOIN_DRIVER REQUEST slot=K nonce=N released=R
  JOIN_DRIVER STATE slot=K nonce=N state=S T=T saver=S bytes=B package=SHA detail=D released=R   (each change)
  JOIN_DRIVER FILES slot=K T=T saver=S verified=1 save=.. save_sha=.. package=.. package_sha=.. portable=..
                    diagnostic=0|1 unsupported=N ambiguous=N modules=CLASS:COUNT,..
  JOIN_DRIVER COMPARE slot=K T=T slots=1,2 equal=1|0 lines=N
  JOIN_DRIVER HANDOFF path=..
  JOIN_DRIVER PAUSE kind=save|handover tick=.. pause_s=..     (the longest gap between released ticks around T / H)
  JOIN_DRIVER PASS slot=K T=T state=S expect=E                (the expected state reached and the files verified)
  JOIN_DRIVER FAIL slot=K reason=..                           (refused join, bad files, the session ended first)
Lines of 'replay': NATIVE_GATE JOIN_REPLAY READY|FIRST|PROGRESS|DESYNC|STOP|DONE ...
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_join as join  # noqa: E402

HANDOFF_KIND, HANDOFF_VERSION = 'x4mp-join-handoff', 1
# The released lane tool's lanes and profiles (dev/lane/lanecore.py LANE_PROFILES, read-only copy).
LANE_PROFILES = {'b1': '45850', 'b2': '45851', 'b3': '45853', 'b4': '45854',
                 'b5': '45855', 'b6': '45856', 'b7': '45857', 'b8': '45858'}
STATES = ('requested', 'queued', 'saved', 'catching_up', 'handover', 'joined')
RANK = {name: i for i, name in enumerate(STATES)}
TICK_LINE = re.compile(r'^NATIVE_GATE tick=(-?\d+) failure=(\S+) done=(True|False)')
SAVED_NOTICE = re.compile(r'^NATIVE_GATE JOIN slot=(\d+) saved slot=(\d+) tick=(\d+) bytes=(\d+) package=([0-9a-f]{64}) '
                          r'portable=([0-9a-f]{32})')
HEX64 = re.compile(r'[0-9a-f]{64}')
TICK_RATES = (60, 40, 30, 20)


class HandoffError(ValueError):
    pass


def profiles_root(explicit=None):
    """Where the lane profiles live: --profiles-root, else the lane tool's X4LANE_PROFILES, else Documents/Egosoft/X4."""
    if explicit:
        return Path(explicit)
    env = os.environ.get('X4LANE_PROFILES')
    return Path(env) if env else Path.home()/'Documents'/'Egosoft'/'X4'


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def atomic_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name+f'.tmp{os.getpid()}')
    temporary.write_text(json.dumps(data, indent=1, sort_keys=True)+'\n', encoding='utf-8')
    os.replace(temporary, path)


def load_handoff(path, verify=False):
    """A joiner handoff (written by 'session' or 'replay'), checked field by field; verify=True also hashes the files."""
    try:
        data = json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise HandoffError(f'handoff {path}: {error}') from None
    def need(ok, what):
        if not ok:
            raise HandoffError(f'handoff {path}: {what}')
    need(isinstance(data, dict) and data.get('kind') == HANDOFF_KIND and data.get('version') == HANDOFF_VERSION, 'not a join handoff')
    ints = ('port', 'slot', 'clients', 'lookahead', 'tick_hz', 'save_tick', 'saver')
    need(all(type(data.get(k)) is int for k in ints), 'integer fields')
    need(1 <= data['port'] <= 65535 and 2 <= data['clients'] <= 8 and 1 <= data['slot'] <= data['clients'], 'port, clients or slot')
    need(0 <= data['lookahead'] <= 8 and data['tick_hz'] in TICK_RATES and data['save_tick'] > 0, 'wire shape')
    need(1 <= data['saver'] <= data['clients'] and data['saver'] != data['slot'], 'saver')
    # The session's absent slots (the relay's --absent; the joiner's overlay names them: native_gate_join.absent) and its
    # rejoinable ones (--rejoin, native_gate_join.rejoin; roadmap 15): the joining slot is one of them.
    def slots(key):
        return key not in data or (isinstance(data[key], list) and all(type(k) is int and 2 <= k <= data['clients'] for k in data[key]))
    need(slots('absent') and slots('rejoin') and not set(data.get('absent', [])) & set(data.get('rejoin', [])), 'absent or rejoin slots')
    need('absent' not in data or data['slot'] in data['absent']+data.get('rejoin', []), 'absent slots')
    need(isinstance(data.get('token'), str) and len(data['token']) == 32 and not set(data['token'])-set('0123456789abcdef'), 'token')
    for key in ('save_sha', 'package_sha'):
        need(isinstance(data.get(key), str) and HEX64.fullmatch(data[key]), key)
    for key in ('save', 'package'):
        need(isinstance(data.get(key), str) and Path(data[key]).is_file(), f'{key} file missing')
    need(data['save'].endswith('.xml.gz'), 'the save must be a .xml.gz (the snapshot slot takes no other)')
    if verify:
        need(sha256_file(data['save']) == data['save_sha'], 'save SHA-256 differs')
        need(sha256_file(data['package']) == data['package_sha'], 'package SHA-256 differs')
    return data


def joiner_tokens(handoff):
    """The lane tokens a joiner's overlay needs (@X4LANE_PORT@, _TOKEN@, _SLOT@, _CLIENT@, _CLIENTS@)."""
    return {'@X4LANE_PORT@': str(handoff['port']), '@X4LANE_TOKEN@': handoff['token'], '@X4LANE_SLOT@': f"p{handoff['slot']}",
            '@X4LANE_CLIENT@': str(handoff['slot']), '@X4LANE_CLIENTS@': str(handoff['clients'])}


# ---- session: the relay wrapper ----
def relay_options(argv):
    """The relay arguments the driver needs (the relay itself checks all of them again)."""
    p = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    p.add_argument('--port', type=int)
    p.add_argument('--token')
    p.add_argument('--clients', type=int, default=2)
    p.add_argument('--lookahead', type=int, default=0)
    p.add_argument('--tick-hz', type=int, default=60)
    p.add_argument('--join', action='store_true')
    p.add_argument('--absent', default='')
    p.add_argument('--rejoin', default='')
    p.add_argument('--join-savers', default='')
    p.add_argument('--join-observe', action='store_true')
    p.add_argument('--bind', default='127.0.0.1')
    known, _ = p.parse_known_args(argv)
    if known.port is None or not 1 <= known.port <= 65535:
        raise ValueError('the relay needs --port')
    if not known.token or len(known.token) != 32 or set(known.token)-set('0123456789abcdef'):
        raise ValueError('the relay needs --token (32 hex)')
    if not known.join:
        raise ValueError('the relay needs --join (hot join)')
    if known.bind != '127.0.0.1':
        raise ValueError('the driver talks to a loopback relay only (--bind 127.0.0.1)')
    try:
        absent = sorted({int(v) for v in known.absent.split(',') if v.strip()})
        rejoin = sorted({int(v) for v in known.rejoin.split(',') if v.strip()})
        savers = sorted({int(v) for v in known.join_savers.split(',') if v.strip()})
    except ValueError:
        raise ValueError('--absent, --rejoin and --join-savers are slot lists') from None
    present = [s for s in range(1, known.clients+1) if s not in absent]
    if not (absent or rejoin) or not present:
        raise ValueError('the relay needs --absent or --rejoin with at least one present slot left')
    # A rejoinable slot is dropped before its join, so it never saves for it: savers default to the other present slots.
    return {'port': known.port, 'token': known.token, 'clients': known.clients, 'lookahead': known.lookahead,
            'tick_hz': known.tick_hz, 'absent': absent, 'rejoin': rejoin, 'present': present,
            'savers': savers or [s for s in present if s not in rejoin], 'observe': known.join_observe}


def saver_lanes(text, present):
    """'b3,b4' (present slots in order) or '1=b3,2=b4' -> {slot: lane}."""
    parts = [v.strip() for v in text.split(',') if v.strip()]
    if all('=' in v for v in parts):
        lanes = {}
        for v in parts:
            slot, lane = v.split('=', 1)
            if not slot.isdigit():
                raise ValueError(f'--saver-lanes {v!r}: SLOT=LANE')
            lanes[int(slot)] = lane
    elif any('=' in v for v in parts):
        raise ValueError('--saver-lanes: either lanes in slot order or SLOT=LANE pairs')
    else:
        if len(parts) != len(present):
            raise ValueError(f'--saver-lanes needs one lane per present slot {present} (the session --lanes order), got {parts}')
        lanes = dict(zip(present, parts))
    for slot, lane in lanes.items():
        if lane not in LANE_PROFILES:
            raise ValueError(f'--saver-lanes: unknown lane {lane!r} (b1..b8)')
        if slot not in present:
            raise ValueError(f'--saver-lanes: slot {slot} is not a present slot {present}')
    if len(set(lanes.values())) != len(lanes):
        raise ValueError('--saver-lanes: every slot has its own lane')
    return lanes


class Join:
    def __init__(self, slot, at):
        self.slot, self.at = slot, at
        self.nonce = secrets.token_hex(8)
        self.state, self.save_tick, self.saver, self.bytes, self.package, self.detail = None, 0, 0, 0, '-', '-'
        self.sent, self.ended, self.passed, self.failed = False, False, False, None
        self.last_poll, self.files, self.files_deadline, self.files_why, self.next_try = 0.0, None, None, None, 0.0
        self.pauses = set()

    def verdict(self):
        return 'pass' if self.passed else 'fail' if self.failed else 'open'

    def fields(self):
        return (f'slot={self.slot} nonce={self.nonce} state={self.state} T={self.save_tick} saver={self.saver} bytes={self.bytes} '
                f'package={self.package} detail={self.detail}')


class Driver:
    """The session driver without processes or sockets: relay lines in (line), NGJOINSTATE datagrams in (answer),
    timers (step); out: emitted lines (emit) and NGJOIN datagrams (send)."""
    def __init__(self, relay, slot, join_at, lanes, copy_to, expect='saved', profiles=None, poll=1.0, file_wait=60.0,
                 emit=print, send=None):
        if slot not in relay['absent']+relay.get('rejoin', []):
            raise ValueError(f'--slot {slot} is not an absent slot of the relay (--absent {",".join(map(str, relay["absent"]))})'
                             f' nor a rejoinable one (--rejoin {",".join(map(str, relay.get("rejoin", [])))})')
        if slot in relay['savers']:
            raise ValueError(f'--slot {slot} is one of the savers (--join-savers)')
        if expect not in ('saved', 'joined'):
            raise ValueError('--expect saved|joined')
        if not join_at or any(t < 1 for t in join_at) or sorted(join_at) != list(join_at) or len(set(join_at)) != len(join_at):
            raise ValueError('--join-at needs increasing ticks >= 1')
        missing = [s for s in relay['savers'] if s not in lanes]
        if missing:
            raise ValueError(f'--saver-lanes has no lane for saver slots {missing}')
        self.relay, self.slot, self.lanes, self.copy_to = relay, slot, lanes, Path(copy_to)
        self.expect, self.profiles, self.poll, self.file_wait = expect, profiles_root(profiles), poll, file_wait
        self.emit, self.send = emit, send
        self.joins = [Join(slot, t) for t in join_at]
        self.released, self.done, self.last_release = -1, False, None
        self.gaps = []                     # (tick, seconds) where a release took longer than half a second
        self.saved_notices = {}            # (k, T) -> {saver slot: (bytes, package, portable)}

    def ready_line(self):
        savers = ','.join(f'{s}:{self.lanes[s]}' for s in self.relay['savers'])
        return (f'JOIN_DRIVER READY slot={self.slot} join_at={",".join(str(j.at) for j in self.joins)} expect={self.expect} '
                f'savers={savers} port={self.relay["port"]} clients={self.relay["clients"]} copy_to={self.copy_to}')

    # -- relay output --
    def line(self, text, now):
        """One relay line: returns the lines to print, the relay line itself last (a FAIL goes before 'done=True')."""
        out = []
        m = TICK_LINE.match(text)
        if m:
            tick = int(m[1])
            if tick > self.released:
                if self.last_release is not None and now-self.last_release[1] > 0.5:
                    self.gaps.append((tick, now-self.last_release[1]))
                    del self.gaps[:-4096]
                self.released, self.last_release = tick, (tick, now)
            if m[3] == 'True' and not self.done:
                self.done = True
                out += self.session_over('session_done')
        n = SAVED_NOTICE.match(text)
        if n:
            k, saver, tick = int(n[1]), int(n[2]), int(n[3])
            self.saved_notices.setdefault((k, tick), {})[saver] = (int(n[4]), n[5], n[6])
        return out+[text]

    def session_over(self, why):
        out = []
        for j in self.joins:
            if not j.passed and not j.failed:   # with the files' last refusal (j1-150/151: a reader shape error, retried silently)
                files = f':files_{j.files_why}' if j.files_why else ''
                out.append(self.fail(j, f'{why}:state_{j.state or "not_requested"}{files}'))
        return out

    def fail(self, j, reason):
        j.failed = reason
        return f'JOIN_DRIVER FAIL slot={j.slot} T={j.save_tick} nonce={j.nonce} reason={reason}'

    # -- launcher datagrams --
    def request(self, j):
        return join.message('NGJOIN', self.relay['token'], j.slot, j.nonce)

    def answer(self, data, now):
        f = join.parse(data, self.relay['token'], 'NGJOINSTATE', 8)
        if not f:
            return []
        slot, nonce, state, tick, saver, size, package, detail = f
        j = next((j for j in self.joins if j.nonce == nonce and str(j.slot) == slot), None)
        if j is None or not tick.isdigit() or not saver.isdigit() or not size.isdigit():
            return []
        out = []
        changed = (state, int(tick), int(saver), int(size), package, detail) != (j.state, j.save_tick, j.saver, j.bytes, j.package, j.detail)
        j.state, j.save_tick, j.saver, j.bytes, j.package, j.detail = state, int(tick), int(saver), int(size), package, detail
        if changed:
            out.append(f'JOIN_DRIVER STATE {j.fields()} released={self.released}')
        if state == 'failed':
            j.ended = True
            if not j.passed and not j.failed:
                out.append(self.fail(j, f'join_failed:{detail}'))
        elif state == 'joined':
            j.ended = True
        if state in RANK and RANK[state] >= RANK['saved'] and j.files is None and j.files_deadline is None:
            j.files_deadline = now+self.file_wait
        out += self.files(j, now)
        out += self.check_pass(j)
        return out

    def check_pass(self, j):
        if j.passed or j.failed or j.state not in RANK or RANK[j.state] < RANK[self.expect] or not j.files:
            return []
        j.passed = True
        # --join-observe: the joiner's digest was not held to the session's before H; never an acceptance.
        diagnostic = ' diagnostic=join_observe' if self.relay.get('observe') else ''
        return [f'JOIN_DRIVER PASS slot={j.slot} T={j.save_tick} state={j.state} expect={self.expect}{diagnostic}']

    # -- timers --
    def step(self, now):
        out = []
        previous_open = False
        for j in self.joins:
            if not j.sent:
                if previous_open or self.done or self.released < j.at:
                    break
                j.sent, j.state, j.last_poll = True, 'requested', now
                out.append(f'JOIN_DRIVER REQUEST slot={j.slot} nonce={j.nonce} released={self.released} at={j.at}')
                self.send(self.request(j))
            elif not j.ended and now-j.last_poll >= self.poll:
                j.last_poll = now
                self.send(self.request(j))
            if j.files is None and j.files_deadline is not None:
                out += self.files(j, now)
                out += self.check_pass(j)
            out += self.pause_lines(j)
            previous_open = not j.ended
        return out

    def pause_lines(self, j):
        """The session's pause at the save tick T (every client saves) and at the handover tick H (the final wait for the
        joiner): the longest gap between two released ticks near it, once the session is 30 ticks past it."""
        out = []
        marks = [('save', j.save_tick)] if j.save_tick else []
        if j.state in ('handover', 'joined') and j.detail.isdigit():
            marks.append(('handover', int(j.detail)))
        for kind, tick in marks:
            if kind in j.pauses or not tick or self.released < tick+self.relay['lookahead']+30:
                continue
            j.pauses.add(kind)
            near = [(t, g) for t, g in self.gaps if tick-2 <= t <= tick+self.relay['lookahead']+30]
            at, gap = max(near, key=lambda tg: tg[1]) if near else (tick, 0.0)
            out.append(f'JOIN_DRIVER PAUSE slot={j.slot} kind={kind} tick={tick} pause_s={gap:.2f} at_tick={at}')
        return out

    # -- the files of a saved join --
    def saver_dir(self, slot):
        return self.profiles/LANE_PROFILES[self.lanes[slot]]/'save'/'mpjoin'

    def files(self, j, now):
        """Find, verify and copy the join files; retried until the deadline, then FAIL with the last reason."""
        if j.files is not None or j.files_deadline is None or j.failed or now < j.next_try:
            return []
        j.next_try = now+1.0                                # a retry hashes the saves again: at most once a second
        try:
            result = self.collect(j)
        except (join.JoinError, OSError) as error:   # OSError: a file still open in X4, a full disk: retried, then loud
            error = error if isinstance(error, join.JoinError) else f'os_{type(error).__name__}:{error}'.replace(' ', '_')
            j.files_why = str(error)
            if now < j.files_deadline:
                return []
            j.files = False
            return [self.fail(j, f'files:{error}')]
        j.files = result
        return result['lines']

    def collect(self, j):
        T, notices = j.save_tick, self.saved_notices.get((j.slot, j.save_tick), {})
        announced = {s: notices[s][1] for s in notices}
        if j.saver and j.package != '-':
            if announced.get(j.saver, j.package) != j.package:
                raise join.JoinError(f'saver_{j.saver}_announced_two_packages')
            announced[j.saver] = j.package
        missing = [s for s in self.relay['savers'] if s not in announced]
        if missing:
            raise join.JoinError(f'no_saved_notice_slots_{"_".join(map(str, missing))}')
        if j.saver not in self.relay['savers']:
            raise join.JoinError(f'saver_{j.saver}_not_a_saver')
        packages = {}
        for s in self.relay['savers']:
            folder = self.saver_dir(s)
            package_path = folder/f'j{T}.x4join'
            save_path = next((p for p in (folder/f'j{T}.xml.gz', folder/f'j{T}.xml') if p.is_file()), None)
            if not package_path.is_file() or save_path is None:
                raise join.JoinError(f'missing_slot_{s}:{package_path.parent}')
            data = package_path.read_bytes()
            if hashlib.sha256(data).hexdigest() != announced[s]:
                raise join.JoinError(f'package_sha_slot_{s}')
            package = join.read_package(data)
            gate, header = package['sections']['gate'], package['header']
            if header['tick'] != T or header['slot'] != s:
                raise join.JoinError(f'package_tick_or_slot_slot_{s}')
            shape = (gate['wire_version'], gate['clients'], gate['lookahead'], gate['tick_hz'])
            if shape != (3, self.relay['clients'], self.relay['lookahead'], self.relay['tick_hz']):
                raise join.JoinError(f'wire_shape_slot_{s}_{"_".join(map(str, shape))}')
            if s in notices and gate['portable'] != notices[s][2]:
                raise join.JoinError(f'portable_slot_{s}')
            if sha256_file(save_path) != header['save_sha']:
                raise join.JoinError(f'save_sha_slot_{s}')
            packages[s] = {'path': package_path, 'save': save_path, 'package': package}
        slots = self.relay['savers']
        first = packages[slots[0]]['package']
        compared = [join.compare_packages(first, packages[s]['package']) for s in slots[1:]]
        equal = all(c['equal'] for c in compared)
        if not equal:
            raise join.JoinError('savers_portable_content_differs')
        # Copy: the saver's save and package for the joiner, every saver's package for the record (J1 b compare).
        target = self.copy_to/f'j{T}'
        target.mkdir(parents=True, exist_ok=True)
        chosen = packages[j.saver]
        save_copy = target/f'j{T}.xml.gz' if chosen['save'].name.endswith('.gz') else target/f'j{T}.xml'
        copies = [(chosen['save'], save_copy), (chosen['path'], target/f'j{T}.x4join')]
        copies += [(packages[s]['path'], target/f'slot{s}.x4join') for s in slots]
        for source, dest in copies:
            shutil.copyfile(source, dest)
        save_sha = chosen['package']['header']['save_sha']
        if sha256_file(save_copy) != save_sha or sha256_file(target/f'j{T}.x4join') != announced[j.saver]:
            raise join.JoinError('copy_differs')
        header, gate = chosen['package']['header'], chosen['package']['sections']['gate']
        classes = {}
        for line in chosen['package']['sections'].get('modules', []):
            kind = line.rsplit('=', 1)[-1] if '=' in line else line.split()[-1]
            classes[kind] = classes.get(kind, 0)+1
        modules = ','.join(f'{k}:{v}' for k, v in sorted(classes.items())) or '-'
        handoff = {'kind': HANDOFF_KIND, 'version': HANDOFF_VERSION, 'source': 'session', 'port': self.relay['port'],
                   'token': self.relay['token'], 'slot': j.slot, 'clients': self.relay['clients'], 'absent': self.relay['absent'],
                   'rejoin': self.relay.get('rejoin', []),
                   'lookahead': self.relay['lookahead'], 'tick_hz': self.relay['tick_hz'], 'save_tick': T, 'saver': j.saver,
                   'save': str(save_copy.resolve()), 'save_sha': save_sha, 'package': str((target/f'j{T}.x4join').resolve()),
                   'package_sha': announced[j.saver], 'portable': gate['portable'], 'diagnostic': header['flags'] & 1,
                   'packages': {str(s): str((target/f'slot{s}.x4join').resolve()) for s in slots}}
        for path in (target/'handoff.json', self.copy_to/'handoff.json'):
            atomic_json(path, handoff)
        lines = [f'JOIN_DRIVER FILES slot={j.slot} T={T} saver={j.saver} verified=1 save={save_copy} save_bytes={header["save_bytes"]} '
                 f'save_sha={save_sha} package={target/f"j{T}.x4join"} package_sha={announced[j.saver]} portable={gate["portable"]} '
                 f'diagnostic={header["flags"] & 1} unsupported={gate["unsupported"]} ambiguous={gate["ambiguous"]} '
                 f'save_us={gate["save_us"]} capture_us={gate["capture_us"]} modules={modules}',
                 f'JOIN_DRIVER COMPARE slot={j.slot} T={T} slots={",".join(map(str, slots))} equal={int(equal)} '
                 f'lines={compared[0]["left_count"] if compared else "-"}',
                 f'JOIN_DRIVER HANDOFF path={self.copy_to/"handoff.json"}']
        return {'lines': lines, 'handoff': handoff}

    def finish(self, code):
        """The relay exited: every join not passed yet is a failure."""
        return self.session_over(f'relay_exited_{code}') if not self.done else []


def run_session(argv):
    p = argparse.ArgumentParser(prog='native_gate_join_driver.py session', description='the lane session --server-cmd wrapper')
    p.add_argument('--slot', type=int, default=3, help='the joining (absent) slot (default 3)')
    p.add_argument('--join-at', required=True, help='released tick(s) at which NGJOIN is sent, comma list; one join at a time')
    p.add_argument('--saver-lanes', required=True, help="the savers' lanes: 'b3,b4' in present slot order or '1=b3,2=b4'")
    p.add_argument('--copy-to', required=True, type=Path, help='where the join files and the handoff go (e.g. {outdir}/join)')
    p.add_argument('--expect', choices=('saved', 'joined'), default='saved',
                   help='saved (J1): pass once the files are verified; joined (J2): once the joiner took over its slot')
    p.add_argument('--profiles-root', help='lane profiles (default: X4LANE_PROFILES or Documents/Egosoft/X4)')
    p.add_argument('--poll', type=float, default=1.0, help='seconds between NGJOIN polls (default 1)')
    p.add_argument('--file-wait', type=float, default=60.0, help='seconds the files may take to appear and verify (default 60)')
    p.add_argument('--exit-after-done', type=float, metavar='S',
                   help='stop the relay and exit S seconds after it printed done=True (runs outside the lane tool; the lane tool '
                        'stops its server itself)')
    p.add_argument('relay', nargs=argparse.REMAINDER, help="'--' then the relay script and its arguments")
    args = p.parse_args(argv)
    relay_argv = args.relay[1:] if args.relay[:1] == ['--'] else args.relay
    if not relay_argv or not relay_argv[0].endswith('.py'):
        p.error("give the relay after '--': -- tools/dev/native_gate_relay.py --port {port} --token {token} ... --join --absent 3")
    script = Path(relay_argv[0])
    if not script.is_file():
        p.error(f'relay script {script} not found (run from the repository: --server-cwd)')
    try:
        relay = relay_options(relay_argv[1:])
        lanes = saver_lanes(args.saver_lanes, relay['present'])
        join_at = [int(v) for v in args.join_at.split(',') if v.strip()]
        driver = Driver(relay, args.slot, join_at, lanes, args.copy_to, args.expect, args.profiles_root, args.poll, args.file_wait)
    except ValueError as error:
        p.error(str(error))
    lock = threading.Lock()
    def emit(text):
        with lock:
            print(text, flush=True)
    driver.emit = emit
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 0))                  # the lane tool allows loopback endpoints only
    sock.setblocking(False)
    driver.send = lambda packet: sock.sendto(packet, ('127.0.0.1', relay['port']))
    proc = subprocess.Popen([sys.executable, '-u', str(script), *relay_argv[1:]], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=1)
    lines = queue.Queue()
    def reader():
        for text in proc.stdout:
            lines.put(text.rstrip('\r\n'))
        lines.put(None)
    reading = threading.Thread(target=reader, daemon=True)
    reading.start()
    emit(driver.ready_line())
    eof, code, done_at, stopped = False, None, None, False
    try:
        while True:
            if driver.done and done_at is None:
                done_at = time.monotonic()
            if args.exit_after_done is not None and done_at is not None and time.monotonic()-done_at >= args.exit_after_done:
                stopped = proc.poll() is None
                break
            try:
                item = lines.get(timeout=0.05)
                while True:
                    if item is None:
                        eof = True
                    else:
                        for text in driver.line(item, time.monotonic()):
                            emit(text)
                    item = lines.get_nowait()
            except queue.Empty:
                pass
            while True:
                try:
                    data, _ = sock.recvfrom(4096)
                except (BlockingIOError, ConnectionResetError, OSError):
                    break
                for text in driver.answer(data, time.monotonic()):
                    emit(text)
            for text in driver.step(time.monotonic()):
                emit(text)
            if eof and proc.poll() is not None:
                break
    except KeyboardInterrupt:
        pass
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        code = proc.wait()
        sock.close()
    reading.join(5)
    while not lines.empty():                          # the relay's last lines (the reader ends with the pipe)
        item = lines.get_nowait()
        if item is not None:
            for text in driver.line(item, time.monotonic()):
                emit(text)
    if stopped:
        code = 0                                      # the driver ended the relay after the session was done
    for text in driver.finish(code):
        emit(text)
    summary = ','.join(f'{j.state}/{j.verdict()}' for j in driver.joins)
    emit(f'JOIN_DRIVER EXIT relay_code={code} joins={summary}')
    return code


# ---- replay: J1's relay from the session journal ----
class JournalReplay:
    """Serves one joiner (slot k, loaded at T) from the session's journal: tick -> {slot: row} (native_gate_join.load_journal)."""
    def __init__(self, token, journal, slot, save_tick, clients, lookahead, tick_hz, last=None, observe=False):
        if len(token) != 32 or set(token)-set('0123456789abcdef'):
            raise ValueError('invalid token')
        if not 1 <= slot <= clients <= 8 or not 0 <= lookahead <= 8 or tick_hz not in TICK_RATES:
            raise ValueError('slot, clients, lookahead or tick rate out of range')
        self.token, self.journal, self.slot, self.save_tick = token, journal, slot, save_tick
        self.clients, self.lookahead, self.tick_hz, self.observe = clients, lookahead, tick_hz, observe
        self.version = 4 if lookahead else 3
        last_known = max(journal) if journal else -1
        self.last = last_known if last is None else min(last, last_known)
        if self.last <= save_tick:
            raise ValueError(f'the journal ends at tick {last_known}, not after the save tick {save_tick}')
        for tick in range(max(save_tick-lookahead-1, 0), self.last+1):
            rows = journal.get(tick)
            if rows is None or sorted(rows) != list(range(1, clients+1)):
                raise ValueError(f'the journal has no complete row set at tick {tick} (clients {clients})')
            if self.reference(tick) is None:
                raise ValueError(f'the journal has no present slot at tick {tick}')
        self.address, self.joiner_tick, self.state, self.failure = None, None, 'waiting', None
        self.mismatches, self.notices, self.first_at, self.last_at, self.diverged = [], [], None, None, False

    def reference(self, tick):
        rows = self.journal.get(tick, {})
        present = [s for s in sorted(rows) if s != self.slot and not rows[s].get('standin')]
        return rows[present[0]] if present else None

    def packet(self, kind, tick):
        data = f'{kind}|{self.version}|{self.token}|{self.slot}|{tick}'
        if kind == 'NGGO' and self.version == 3:
            data += ''.join('|'+self.journal[tick-1][s].get('blob', '') for s in range(1, self.clients+1))
        elif kind == 'NGGO':
            source = tick-1-self.lookahead
            data += f'|{self.lookahead}'+''.join('|'+(self.journal[source][s].get('blob', '') if source >= 0 else '')
                                                   for s in range(1, self.clients+1))
        return data.encode('ascii')

    def stop(self, reason, tick):
        self.state, self.failure = ('done', None) if reason is None else ('failed', reason)
        self.notices.append(f'NATIVE_GATE JOIN_REPLAY STOP tick={tick} reason={reason or "end"}')
        return [(self.address, self.packet('NGSTOP', tick))]

    def receive(self, data, address, now):
        if self.state in ('done', 'failed') or not data.startswith(b'NGSTATE|'):
            return []
        fields = join._state_fields(data, self.token)
        if not fields:
            return []
        slot, tick, game, real, digest, _blob, version = fields
        if slot != self.slot or data.split(b'|')[8] != str(self.clients).encode():
            return []
        if self.address is not None and address != self.address:
            return []
        if version != self.version:
            self.address = self.address or address
            return self.stop(f'mismatch:protocol_{version}', tick)
        if self.joiner_tick is None and tick != self.save_tick:
            return []                                       # its first report is the loaded world at T
        if self.joiner_tick is not None and (tick < self.joiner_tick or tick > self.joiner_tick+1):
            return []
        ref = self.reference(tick)
        if ref is None:
            return []
        first = self.joiner_tick is None
        if first:
            self.address, self.state, self.first_at = address, 'replaying', now
            self.notices.append(f'NATIVE_GATE JOIN_REPLAY FIRST tick={tick} slot={slot}')
        if game != struct.pack('>d', ref['game']).hex():
            return self.stop(f'mismatch:game_tick_{tick}', tick)
        if struct.unpack('>d', bytes.fromhex(real))[0] != ref['real']:
            return self.stop(f'mismatch:real_tick_{tick}', tick)
        new = self.joiner_tick is None or tick > self.joiner_tick
        if new and tick != self.save_tick and digest != ref['digest']:
            if not self.observe:
                return self.stop(f'mismatch:digest_tick_{tick}', tick)
            self.mismatches.append(tick)
            if not self.diverged:                           # the first tick of each differing run
                self.notices.append(f'NATIVE_GATE JOIN_REPLAY DESYNC tick={tick} first={self.mismatches[0]} mismatches={len(self.mismatches)}')
            self.diverged = True
        elif new and tick != self.save_tick and self.diverged:
            self.diverged = False
            self.notices.append(f'NATIVE_GATE JOIN_REPLAY RESYNC tick={tick} mismatches={len(self.mismatches)}')
        if new:
            self.joiner_tick, self.last_at = tick, now
            if (tick-self.save_tick) % 300 == 0 and tick != self.save_tick:
                self.notices.append(f'NATIVE_GATE JOIN_REPLAY PROGRESS tick={tick} mismatches={len(self.mismatches)} rate={self.rate():.1f}')
        if tick >= self.last:
            return self.stop(None, tick)
        permits = range(tick+1, tick+2+self.lookahead) if first else (tick+1+self.lookahead,)
        return [(address, self.packet('NGGO', permit)) for permit in permits if permit <= self.last]

    def rate(self):
        if self.first_at is None or self.last_at is None or self.last_at <= self.first_at:
            return 0.0
        return (self.joiner_tick-self.save_tick)/(self.last_at-self.first_at)

    def summary(self):
        verdict = ('failed' if self.state == 'failed' else 'diverged' if self.mismatches else 'equal' if self.state == 'done'
                   else 'incomplete')
        return {'scope': 'hot join J1: a joiner replayed from the session journal', 'verdict': verdict, 'failure': self.failure,
                'slot': self.slot, 'save_tick': self.save_tick, 'last': self.last, 'joiner_tick': self.joiner_tick,
                'first_mismatch': self.mismatches[0] if self.mismatches else None, 'mismatches': len(self.mismatches),
                'mismatch_ticks': self.mismatches[:2000], 'steps_per_s': round(self.rate(), 2), 'observe': self.observe}

    def done_line(self):
        s = self.summary()
        return (f'NATIVE_GATE JOIN_REPLAY DONE verdict={s["verdict"]} reason={s["failure"] or "-"} save_tick={self.save_tick} '
                f'joiner_tick={self.joiner_tick} last={self.last} first_mismatch={s["first_mismatch"]} mismatches={s["mismatches"]} '
                f'steps_per_s={s["steps_per_s"]}')


def run_replay(argv):
    p = argparse.ArgumentParser(prog='native_gate_join_driver.py replay', description="J1's relay from the session journal")
    p.add_argument('--journal', required=True, type=Path, help="the session relay's --input-journal (native-inputs.jsonl)")
    p.add_argument('--handoff', required=True, type=Path, help="the session driver's handoff (the join files and wire shape)")
    p.add_argument('--handoff-out', required=True, type=Path, help="this relay's handoff for the joiner (its port and token)")
    p.add_argument('--report', required=True, type=Path, help='summary JSON (verdict equal|diverged|failed|incomplete)')
    p.add_argument('--port', type=int, default=0, help='UDP port on 127.0.0.1 (default: a free one)')
    p.add_argument('--token', help='32 hex (default: a new random token, written to --handoff-out)')
    p.add_argument('--ticks', type=int, help='last tick to replay (default: the journal end)')
    p.add_argument('--observe', action='store_true', help='record every differing declared digest and go on (census)')
    p.add_argument('--boot-timeout', type=float, default=900, help='seconds to wait for the first report (default 900)')
    p.add_argument('--idle-timeout', type=float, default=180, help='seconds without a new report before giving up (default 180)')
    p.add_argument('--log', type=Path, help='write the relay lines here instead of stdout (with a command after --)')
    p.add_argument('command', nargs=argparse.REMAINDER, help="optional: '--' and the joiner's command, run while serving")
    args = p.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    try:
        session = load_handoff(args.handoff, verify=True)
        journal = join.load_journal(args.journal)
        token = args.token or secrets.token_hex(16)
        replay = JournalReplay(token, journal, session['slot'], session['save_tick'], session['clients'], session['lookahead'],
                               session['tick_hz'], args.ticks, args.observe)
    except (ValueError, OSError, KeyError) as error:
        p.error(str(error))
    out = open(args.log, 'a', encoding='utf-8') if args.log else sys.stdout
    def emit(text):
        print(text, file=out, flush=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', args.port))
    sock.settimeout(0.05)
    port = sock.getsockname()[1]
    atomic_json(args.handoff_out, dict(session, source='replay', port=port, token=token))
    emit(f'NATIVE_GATE JOIN_REPLAY READY port={port} slot={replay.slot} save_tick={replay.save_tick} last={replay.last} '
         f'clients={replay.clients} lookahead={replay.lookahead} observe={int(args.observe)} handoff={args.handoff_out}')
    child = subprocess.Popen(command) if command else None
    started = time.monotonic()
    try:
        while replay.state not in ('done', 'failed'):
            try:
                data, address = sock.recvfrom(65536)
            except (socket.timeout, ConnectionResetError):
                data = None
            now = time.monotonic()
            if data is not None:
                for dest, packet in replay.receive(data, address, now):
                    try:
                        sock.sendto(packet, dest)
                    except ConnectionResetError:
                        pass
            for text in replay.notices:
                emit(text)
            replay.notices = []
            if replay.first_at is None and now-started > args.boot_timeout:
                replay.state, replay.failure = 'failed', 'boot_timeout:no_first_report'
            elif replay.last_at is not None and now-replay.last_at > args.idle_timeout:
                replay.state, replay.failure = 'failed', f'joiner_silent_after_tick_{replay.joiner_tick}'
            if child is not None and child.poll() is not None and replay.state not in ('done', 'failed'):
                replay.state, replay.failure = 'failed', f'joiner_command_exited_{child.returncode}'
    except KeyboardInterrupt:
        replay.state, replay.failure = 'failed', 'interrupted'
    finally:
        sock.close()
    atomic_json(args.report, replay.summary())
    emit(replay.done_line())
    if child is not None:
        code = child.wait()
        emit(f'NATIVE_GATE JOIN_REPLAY COMMAND exit={code}')
    if out is not sys.stdout:
        out.close()
    return 0 if replay.summary()['verdict'] in ('equal', 'diverged') else 1


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] not in ('session', 'replay'):
        print(__doc__)
        print('usage: native_gate_join_driver.py session|replay ... (--help for each)')
        return 2
    return run_session(argv[1:]) if argv[0] == 'session' else run_replay(argv[1:])


if __name__ == '__main__':
    sys.exit(main())
