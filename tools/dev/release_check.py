"""Release check: what must pass before a build goes to the players. Runs the sessions it needs on this PC and prints
PASS/FAIL per check with the rows that decided it.

    python tools/dev/release_check.py perf --candidate DLL --baseline DLL[@COMMIT] [--baseline DLL@COMMIT ...]
        --save SAVE --session SESSION.json --recipe DIR [--solo K | --clients N] [--minutes 6] [--ablate KEY,...]
    python tools/dev/release_check.py missions --gate-dll DLL [--players 2|3] [--save ... | --fresh]
    python tools/dev/release_check.py satellite --gate-dll DLL --players 2|3
    python tools/dev/release_check.py quick --gate-dll DLL --players 2|3
    python tools/dev/release_check.py all --candidate DLL --baseline DLL@COMMIT --save ... --players 2|3
    python tools/dev/release_check.py perf-logs BASE.log CAND.log [--base-run DIR --cand-run DIR]   (no X4: two logs)
    python tools/dev/release_check.py report KIND RUN [RUN ...]                                      (no X4: the verdicts again)

What each check is (docs/PLAYTEST.md "Release check"):

  perf       the same session and scripted play (the plan PERF_PLAN: throttle, travel drive, turns, the guns on a target)
             with the candidate DLL and each baseline DLL, each from its own release's checkout (the baseline's commit is
             exported once into .local/release-check/repos/<commit>, so its launcher, probe and defaults are its own).
             Rows: FRAME_TIMING (every tick: the simulation thread's frame sim_us, its render part slot10_us, the gate's
             pre_us, the render-only frames rframe_us), PROBE_PROFILE (the probe's sections), RENDER_FRAMES (frames per
             step), the passes' joins, and the relay's released_at stamps for ticks per second at the session's fixed pace
             (30 Hz). FAIL when the candidate is more than 10% slower than a baseline per tick (sim_us + pre_us), per
             render-only frame (rframe_us) or in ticks per second. It names the largest differences: timing fields of every
             status row (FRAME_TIMING, PROBE_PROFILE sections, TRAVEL/PATH/MASS_TRAFFIC/REGION, RENDER_DISPLAY/SHOTS) and
             the per-tick rates of every counter of the status rows (e.g. PILOT_INPUTS eye= poses per tick), so a slower
             build points at the module that does more. --ablate runs the candidate again with each session key off
             (native_gate_KEY=false): the difference is that module's own cost (the presentation modules have no timers).
  missions   each player on its own client, through the UI replay script ops (rc_look, scoped like a person's menu):
             the mission manager opens (MapMenu in its mission mode), the client lists that player's missions and offers
             (and none of another player's: gate.mpp_owner of each listed mission is this slot or nobody), the active
             mission (what the HUD's mission panel shows) is one of that player's, the guidance target (the yellow
             objective) is that mission's objective target; a story offer is accepted (the Terran story's first offer
             from Story_Yaki_P<k>, after the cadet story; else the first generic offer) and becomes that player's mission.
  satellite  a fresh session with every player on the Terran cadet start and its own story copy; each player flies the
             cadet's first chapter by its own inputs (rc_drive: the mission made active as the mission manager does, Shift+A
             through the replayed input to the guidance, a satellite launched by the ship menu's call, the spacesuit, the
             repair laser on the broken satellite); PASS when every slot has `MILESTONE reached
             name=terran_cadet-M1-slot<k>`, its story's speech played on its own client only (MD_MEDIA), and the session
             stayed in sync (no DESYNC, every WORLD_HASH equal).
  quick      tonight's regressions, cheap: Shift+A resumes the saved destination after a continue (the autopilot flies
             at once); the HUD's set speed is steady at a constant simulated speed (OWN_THROTTLE rows); the lead marker
             and the aim dots hide beyond weapon range for players 2..N as for player 1 (PILOT_AIM_PRESENT); the weapon
             panel's state of player 2 equals player 1's (rc_weapons).

Every run: windowed clients with disposable profiles (x4ls play), --display/--never-focus/--close-when-over, the
clients below normal priority, lanes and profiles of the caller's lock (--lanes, --first-profile; C:/x4ls-locks rules:
--locks takes them and waits while the user plays). Exit code: 0 every check passed, 1 a check failed, 2 a run did not
work. A scripted run is not a person playing: what a person sees (colours, the cursor) is covered only where a row says
it.
"""
import argparse
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / 'tools/dev'
WORK = ROOT / '.local/release-check'

# ---------------------------------------------------------------------------------------------------------------------
# Reading client logs. X4 stamps a line with the seconds since its start ("[=ERROR=] 815.93 [X4LAN] ..."); a batch of
# the probe's rows follows its first stamped line unstamped, so a row's time is the last stamp before it.
STAMP = re.compile(r'^\[[^\]]*\]\s+(\d+(?:\.\d+)?)\s')
ROW = re.compile(r'\[X4LAN\] (?:NATIVE_GATE )?([A-Z][A-Z0-9_]+) (.*)$')
FIELD = re.compile(r'([A-Za-z_][\w.]*)=(\S*)')
NUMBER = re.compile(r'^-?\d+(?:\.\d+)?(?:e[-+]?\d+)?$')


def read_lines(path):
    try:
        with open(path, encoding='latin-1') as f:
            for line in f:
                yield line.rstrip('\r\n')
    except OSError:
        return


def rows(path, kinds=None):
    """(stamp seconds, kind, text) of every [X4LAN] row of a client log (kinds: a set to keep)."""
    stamp = None
    for line in read_lines(path):
        m = STAMP.match(line)
        if m:
            stamp = float(m.group(1))
        if '[X4LAN] ' not in line:
            continue
        r = ROW.search(line)
        if r and (kinds is None or r.group(1) in kinds):
            yield stamp, r.group(1), r.group(2)


def fields(text):
    """key=value pairs of a row; a value with slashes also gives key.1, key.2, ... for its numeric parts."""
    out = {}
    for key, value in FIELD.findall(text):
        if key in out:          # the first one counts (a row may repeat a key in a later part)
            continue
        out[key] = value
        if '/' in value:
            for i, part in enumerate(value.split('/')):
                if NUMBER.match(part):
                    out['%s.%d' % (key, i)] = part
    return out


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


# ---------------------------------------------------------------------------------------------------------------------
# Performance. Rows every tick (FRAME_TIMING with native_gate_frame_timing=true), every 60 ticks (PROBE_PROFILE with
# native_gate_probe_profile=true, a mean of 60 ticks) and the status rows every 60 ticks (counters since the start, or
# the last value). Timing fields (name ending in _us) are averaged; counters are turned into a rate per tick.
PERF_SKIP = 600            # ticks after the load that are not measured (boot frames, first loads)
TIMED = ('FRAME_TIMING', 'PROBE_PROFILE')
# Fields that are a value of the moment or a mean already, never a counter (averaged as they are).
LEVEL_FIELDS = re.compile(r'(_us|_ms|_x100|_m|_rad|^per_step|^eff_fps|^fps|^x4_limit|^viewpoints|^workers|^last_gap|^max_gap)$')
# A slower candidate fails the check when it is this much slower than a baseline.
TOLERANCE = 0.10
# The verdict's measures: (name, what it is, higher is slower).
MEASURES = (('tick_us', 'per tick: the simulation thread\'s frame plus the gate\'s pre-step work (FRAME_TIMING sim_us + pre_us)', True),
            ('frame_us', 'per render-only frame: its whole period (FRAME_TIMING rframe_us, RENDER_FRAMES mean period)', True),
            ('tps', 'ticks per second at the fixed relay pace (the released_at stamps of the relay)', False),
            ('cpu_us', 'processor time per tick, every thread of the X4 (independent of other programs on the PC)', True))


def perf_profile(log, run_dir=None, skip=PERF_SKIP, end=None):
    """One client's numbers over ticks skip..end: {'ticks', 'tps', 'tick_us', 'frame_us', 'timing': {name: mean},
    'rates': {name: per tick}}. A status field whose values never fall is a counter (its rate per tick; for a time in
    microseconds that is microseconds per tick); any other is a level (its mean). FRAME_TIMING and PROBE_PROFILE
    fields are levels (their sum_ fields counters). X4's own line stamps are game time (paced: 30 per tick second), so
    ticks per second come from the relay's record only."""
    series, ticks_seen = {}, set()
    sim, pre, rframe = [], [], []
    for _, kind, text in rows(log):
        if not text.startswith('tick='):
            continue
        f = fields(text)
        tick = number(f.get('tick'))
        if tick is None or tick < skip or (end is not None and tick > end):
            continue
        ticks_seen.add(tick)
        if kind == 'FRAME_TIMING':
            s, p, r = number(f.get('sim_us')), number(f.get('pre_us')), number(f.get('rframe_us'))
            if s and s > 0:
                sim.append(s)
                pre.append(p or 0)
            if r and r > 0 and number(f.get('rframes')):
                rframe.append(r)
        for key, value in f.items():
            v = number(value)
            if v is not None and key != 'tick':
                series.setdefault((kind, key), []).append((tick, v))
    timing, rates = {}, {}
    for (kind, key), values in series.items():
        name = '%s.%s' % (kind, key)
        level = (kind in TIMED and not key.startswith('sum_')) or LEVEL_FIELDS.search(key) and not key.startswith(('total_', 'sum_'))
        rising = len(values) >= 3 and all(b[1] >= a[1] for a, b in zip(values, values[1:])) and values[-1][1] > values[0][1]
        if rising and not (kind in TIMED and not key.startswith('sum_')):
            (t0, v0), (t1, v1) = values[0], values[-1]
            if t1 > t0:
                rates[name] = (v1 - v0) / (t1 - t0)
        elif level or not rising:
            timing[name] = statistics.fmean(v for _, v in values)
    out = {'log': str(log), 'timing': timing, 'rates': rates, 'samples': len(sim)}
    ticks = sorted(ticks_seen)
    out['ticks'] = (int(ticks[0]), int(ticks[-1])) if ticks else None
    out['tps'] = relay_tps(run_dir, skip, end) if run_dir else None
    out['cpu_us'] = cpu_per_tick(run_dir, skip, end) if run_dir else None
    out['tick_us'] = statistics.fmean(sim) + statistics.fmean(pre) if sim else None
    if sim:
        out['sim_p90_us'] = sorted(sim)[int(0.9 * (len(sim) - 1))]
    out['frame_us'] = statistics.fmean(rframe) if rframe else timing.get('RENDER_FRAMES.mean_period_us')
    return out


def relay_tps(run_dir, skip=PERF_SKIP, end=None):
    """Ticks per second from the relay's record of released permits (native-inputs.jsonl released_at)."""
    path = Path(run_dir) / 'native-inputs.jsonl'
    first = last = None
    try:
        with open(path, encoding='utf-8') as f:
            for line in f:
                m = re.match(r'\{"tick":(\d+),"released_at":([\d.]+)', line)
                if not m:
                    continue
                tick, at = int(m.group(1)), float(m.group(2))
                if tick < skip or (end is not None and tick > end):
                    continue
                if first is None:
                    first = (tick, at)
                last = (tick, at)
    except OSError:
        return None
    if not first or last[0] <= first[0] or last[1] <= first[1]:
        return None
    return (last[0] - first[0]) / (last[1] - first[1])


def slower(base, cand, higher_is_slower=True):
    """How much slower the candidate is (0.12 = 12% slower), None when a value is missing."""
    if not base or not cand:
        return None
    return (cand - base) / base if higher_is_slower else (base - cand) / cand


def differences(base, cand, top=12, floor_us=20.0):
    """The largest differences between two profiles, (times, activity):
    times: microseconds per tick or per frame, from the means of the timing fields (levels) and the rates of the
    cumulative microsecond counters (total_wait_us, load_us, ...), at least floor_us apart, largest first;
    activity: the per-tick rates of the other counters, at least 25% and 0.1 per tick apart, largest change first.
    Each entry: (name, unit, base, cand)."""
    times, activity = [], []
    for name in set(base['timing']) | set(cand['timing']):
        if not name.endswith('_us') or '.max' in name or 'max_' in name:
            continue
        b, c = base['timing'].get(name), cand['timing'].get(name)
        if b is not None and c is not None and abs(c - b) >= floor_us:
            times.append((abs(c - b), name, 'us', b, c))
    for name in set(base['rates']) | set(cand['rates']):
        b, c = base['rates'].get(name, 0.0), cand['rates'].get(name, 0.0)
        if name.endswith('_us') or re.search(r'_us\.\d+$', name):
            if abs(c - b) >= floor_us and 'max' not in name:
                times.append((abs(c - b), name, 'us/tick', b, c))
        elif max(b, c) >= 0.1 and abs(c - b) >= 0.25 * max(b, c):
            activity.append((abs(c - b) / max(b, c), name, '/tick', b, c))
    times.sort(key=lambda e: -e[0])
    activity.sort(key=lambda e: (-e[0], e[1]))
    return [e[1:] for e in times[:top]], [e[1:] for e in activity[:top]]


def perf_verdict(name, base, cand, tolerance=TOLERANCE):
    """Lines and failures of one candidate/baseline pair."""
    lines, failed = [], 0
    for key, what, higher in MEASURES:
        b, c = base.get(key), cand.get(key)
        d = slower(b, c, higher)
        if d is None:
            lines.append('  %-9s n/a   %s: baseline %s, candidate %s' % (key, what, fmt(b), fmt(c)))
            continue
        bad = d > tolerance
        failed += bad
        lines.append('  %-9s %-5s %s: baseline %s, candidate %s (%+.1f%% slower)' % (key, 'FAIL' if bad else 'ok', what, fmt(b), fmt(c), 100 * d))
    times, activity = differences(base, cand)
    lines.append('  where the time differs (baseline -> candidate):')
    for metric, unit, b, c in times:
        lines.append('    %-52s %10s -> %10s %s (%+.0f)' % (metric, fmt(b), fmt(c), unit, c - b))
    lines.append('  what is done more or less often (baseline -> candidate, per tick):')
    for metric, unit, b, c in activity:
        lines.append('    %-52s %10.2f -> %10.2f' % (metric, b, c))
    return lines, failed


def fmt(v):
    if v is None:
        return '-'
    return '%.1f' % v if abs(v) < 1000 else '%.0f' % v


def perf_logs(args):
    """perf-logs: two client logs (and their runs), no X4."""
    base = perf_profile(args.base, args.base_run, args.skip, args.end)
    cand = perf_profile(args.cand, args.cand_run, args.skip, args.end)
    print('baseline  %s: ticks %s, %d timed ticks' % (args.base, base['ticks'], base['samples']))
    print('candidate %s: ticks %s, %d timed ticks' % (args.cand, cand['ticks'], cand['samples']))
    lines, failed = perf_verdict('logs', base, cand, args.tolerance)
    print('\n'.join(lines))
    print('perf: %s' % ('FAIL' if failed else 'PASS'))
    return 1 if failed else 0


# ---------------------------------------------------------------------------------------------------------------------
# The slots of this PC (C:/x4ls-locks/PROFILES.txt, as of 25 Nov 17:10): four, one X4 each. x4-a (X4LS_FIRST_PROFILE 4:
# 45855/45856), x4-b (6: 45857/45858), x4-c (0: 45850/45851), x4-d (2: 45853/45854). A two-client run takes a whole pair,
# x4-a + x4-b (index 4) or x4-c + x4-d (index 0), whichever is free, and queues in want-both.txt only when neither is; a
# three-client run takes x4-a, x4-b and x4-c (index 4: 45855..45857). While USER-PLAYING.txt exists the user plays: x4-c and
# x4-d are closed (their profiles sit beside the user's), one --solo X4 at a time, no run of several clients.
LOCKS = Path(os.environ.get('X4LS_LOCKS', 'C:/x4ls-locks'))
# profile.ALL_PROFILES = 45850,45851,45853,45854,45855,45856,45857,45858; client K uses index FIRST+K-1 (mod 8).
PROFILE_IDS = (45850, 45851, 45853, 45854, 45855, 45856, 45857, 45858)
LOCK_INDEX = {'x4-a': 4, 'x4-b': 6, 'x4-c': 0, 'x4-d': 2}
PAIRS = ((('x4-a', 'x4-b'), 4), (('x4-c', 'x4-d'), 0))
THREE = (('x4-a', 'x4-b', 'x4-c'), 4)
CLIENTS_FIRST = 4          # two or three clients on 45855.. (x4-a + x4-b [+ x4-c])


def solo_first_profile(lock, slot):
    """X4LS_FIRST_PROFILE for `x4ls play --solo slot` so that client `slot` takes that lock's first profile."""
    return (LOCK_INDEX[lock] - (slot - 1)) % 8


def profile_of(first, slot):
    """The profile id client `slot` takes with X4LS_FIRST_PROFILE=first."""
    return PROFILE_IDS[(first + slot - 1) % 8]


def lock_profiles(locks):
    """The profiles the held locks own (each lock its index and the next)."""
    return {PROFILE_IDS[(LOCK_INDEX[l] + i) % 8] for l in locks for i in (0, 1)}


class Locks:
    """mkdir/owner.txt/rmdir of the slots this run needs; waits its turn. want: 'one', 'two' or 'three'.
    first_profile() is X4LS_FIRST_PROFILE of the clients of what it holds."""

    def __init__(self, name, what, root=None, log=print, poll=3, slots=None, beside_user=False, pause=None, skip_queue=False):
        self.name, self.what, self.root, self.log, self.poll = name, what, Path(root or LOCKS), log, poll
        self.pause = Path(pause) if pause else None      # the runner's: WORK/PAUSE
        self.skip_queue = skip_queue          # a short solo (a session capture) the coordinator let past the queued pairs
        self.beside_user = beside_user        # one --solo X4 beside the user's game (only when the user allows it)
        self.held, self.queued, self.index = [], False, None
        self.slots = set(slots) if slots else set(LOCK_INDEX)        # the slots this run may take

    def user_playing(self):
        return (self.root / 'USER-PLAYING.txt').exists()

    def queue(self):
        try:
            return [l.strip() for l in (self.root / 'want-both.txt').read_text(encoding='utf-8').splitlines() if l.strip()]
        except OSError:
            return []

    def take(self, lock):
        try:
            (self.root / lock).mkdir()
        except FileExistsError:
            return False
        (self.root / lock / 'owner.txt').write_text('%s (%s) %s%s' % (self.name, self.what, time.strftime('%Y-%m-%d %H:%M:%S'), chr(10)),
                                                     encoding='utf-8')
        self.held.append(lock)
        return True

    def take_all(self, locks):
        if any((self.root / l).exists() for l in locks):
            return False
        if all(self.take(l) for l in locks):
            return True
        self.release()
        return False

    def try_acquire(self, want):
        """One attempt: (locks or None, why not). A PAUSE file in this checkout's .local/release-check holds every new
        run of it (the one in progress goes on): the coordinator gives the slots to another agent's runs for a while."""
        if self.pause and self.pause.exists():
            self.unqueue()
            return None, 'paused (%s exists): no new run until it is removed' % self.pause
        playing = self.user_playing()
        if playing and (not self.beside_user or want == 'three'):
            self.unqueue()
            return None, 'the user is playing (USER-PLAYING.txt): no X4 starts until the file is gone'
        if want == 'one':
            queue = self.queue()
            if queue and queue[0] != self.name and not self.skip_queue:
                return None, 'a two-client run is queued (%s)' % queue[0]
            for lock in ('x4-b', 'x4-a') + (() if playing else ('x4-d', 'x4-c')):
                if lock in self.slots and self.take(lock):
                    self.index = LOCK_INDEX[lock]
                    return list(self.held), ''
            return None, 'every slot is taken'
        # beside the user's game (--beside-user, when the coordinator opens them): x4-a + x4-b only; x4-c and x4-d sit
        # beside the user's profiles
        options = [o for o in (PAIRS if want == 'two' else (THREE,)) if set(o[0]) <= self.slots and
                   not (playing and set(o[0]) & {'x4-c', 'x4-d'})]
        if not options:
            return None, 'no %s-client set within the allowed slots %s' % (want, ','.join(sorted(self.slots)))
        queue = self.queue()
        if not queue or queue[0] == self.name:
            for locks, index in options:
                if self.take_all(locks):
                    self.index = index
                    self.unqueue()            # the line is for waiting (PROFILES.txt): holding the slots, it goes
                    return list(self.held), ''
        if self.name not in queue:
            with open(self.root / 'want-both.txt', 'a', encoding='utf-8') as f:
                f.write(self.name + chr(10))
            self.queued = True
        return None, 'waiting for %s (queue: %s)' % (' or '.join('+'.join(l) for l, _ in options), ', '.join(self.queue()[:3]))

    def acquire(self, want, deadline=None):
        """The locks for `want`, waiting (polling) until they are free and it is this run's turn."""
        said = None
        while True:
            held, why = self.try_acquire(want)
            if held:
                return held
            if deadline and time.time() > deadline:
                raise TimeoutError('no slot in time: ' + why)
            if why != said:
                self.log('locks: ' + why)
                said = why
            time.sleep(self.poll)

    def release(self):
        for lock in self.held:
            try:
                (self.root / lock / 'owner.txt').unlink()
            except OSError:
                pass
            try:
                (self.root / lock).rmdir()
            except OSError:
                pass
        self.held = []

    def unqueue(self):
        """This run's line out of want-both.txt (the file removed when it is empty)."""
        if self.queued or self.name in self.queue():
            rest = [l for l in self.queue() if l != self.name]
            path = self.root / 'want-both.txt'
            if rest:
                path.write_text(chr(10).join(rest) + chr(10), encoding='utf-8')
            else:
                path.unlink(missing_ok=True)
        self.queued = False

    def done(self):
        """Every lock back and this run's line out of the queue."""
        self.release()
        self.unqueue()


# ---------------------------------------------------------------------------------------------------------------------
# Checkouts: the candidate runs from this checkout; a baseline DLL@COMMIT from that commit's own tree (git archive into
# .local/release-check/repos/<commit>), so its launcher, overlay builder, probe and defaults are its release's.
def parse_dll(spec):
    """'PATH' or 'PATH@COMMIT' -> (Path, commit or None)."""
    text = str(spec)
    path, sep, commit = text.rpartition('@')
    if sep and re.fullmatch(r'[0-9a-f]{7,40}', commit):
        return Path(path), commit
    return Path(text), None


CORE = 'tools/dev/native_gate_ui_replay_core.lua'
OPS_START = '-- Release check ops (tools/dev/release_check.py'
OPS_HOOK = ("  elseif X4LAN_RELEASE_CHECK and X4LAN_RELEASE_CHECK.SCRIPT_OPS[op] then call=X4LAN_RELEASE_CHECK.script_call(e,tools)"
            "   -- tools/dev/release_check.py's ops")
OLD_TAIL = ("  else error('unknown ui replay script op '..tostring(op)) end\n"
            "  by[at]=by[at] or {};by[at][#by[at]+1]={op=op,call=call,scoped=K.SCOPED[op] or nil,canonical=K.CANONICAL[op] or nil}")
NEW_TAIL = ("  else error('unknown ui replay script op '..tostring(op)) end\n"
            "  by[at]=by[at] or {};by[at][#by[at]+1]={op=op,call=call,scoped=K.SCOPED[op] or (X4LAN_RELEASE_CHECK and "
            "X4LAN_RELEASE_CHECK.SCOPED[op]) or nil,canonical=K.CANONICAL[op] or nil}")


def ops_block(text):
    """The release check ops of a replay core's text (from the dashed line before their comment to the end), or None."""
    lines = text.split('\n')
    for i, line in enumerate(lines):
        if line.startswith(OPS_START):
            return '\n'.join(lines[max(i - 1, 0):])
    return None


def graft_ops(tree, log=print):
    """This checkout's release check ops (rc_look, rc_drive, ...) in another release's replay core: its block replaced
    by this one's, or the dispatch hook and the block added to an older core without them. The rest of that core stays
    the release's own; the ops are the check's harness, so every release is driven by the same one. True when the
    tree's core now holds this checkout's ops."""
    core = Path(tree) / CORE
    if Path(tree).resolve() == ROOT.resolve():
        return True
    if not core.exists():
        return False
    mine = ops_block((ROOT / CORE).read_text(encoding='utf-8').replace('\r\n', '\n'))
    raw = core.read_bytes().decode('utf-8')
    nl = '\r\n' if '\r\n' in raw else '\n'
    text = raw.replace('\r\n', '\n')
    theirs = ops_block(text)
    if mine is None or theirs == mine:
        return mine is not None
    if theirs is not None:
        text = text[:len(text) - len(theirs)] + mine
    elif text.count(OLD_TAIL) == 1:
        text = text.replace(OLD_TAIL, OPS_HOOK + '\n' + NEW_TAIL).rstrip('\n') + '\n' + mine
    else:
        log('%s: no place for the release check ops (left as it is)' % core)
        return False
    part = core.with_name(core.name + '.part')       # whole or not at all (live: a full disk left the core empty)
    part.write_bytes(text.replace('\n', nl).encode('utf-8'))
    os.replace(part, core)
    log('%s: the release check ops of this checkout grafted in' % core)
    return True


def checkout(commit, work=None, log=print):
    """The tree of a commit (ROOT for None), with this checkout's release check ops (graft_ops)."""
    if not commit:
        return ROOT
    full = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', commit], capture_output=True, text=True, check=True).stdout.strip()
    out = Path(work or WORK) / 'repos' / full[:12]
    if (out / '.release-check-complete').exists():
        graft_ops(out, log)
        return out
    import io
    import tarfile
    data = subprocess.run(['git', '-C', str(ROOT), 'archive', '--format=tar', full], capture_output=True, check=True).stdout
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        if hasattr(tarfile, 'data_filter'):
            tar.extractall(out, filter='data')
        else:
            tar.extractall(out)
    (out / '.release-check-complete').write_text(full, encoding='utf-8')
    log('checkout %s: %s' % (full[:12], out))
    graft_ops(out, log)
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Sessions and runs, through the launcher of the checkout under test.
CADET = 'x4ep1_gamestart_terran1'


def launcher(repo, *args):
    return [sys.executable, str(Path(repo) / 'tools/x4ls.py'), *map(str, args)]


def run_logged(cmd, cwd, log_path, env=None, timeout=None):
    """A command with its output in log_path; its return code (124: over its time)."""
    Path(log_path).parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, 'w', encoding='utf-8') as out:
        out.write('$ ' + ' '.join(map(str, cmd)) + '\n')
        out.flush()
        try:
            return subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            return 124


def lua(value):
    """A Lua literal of a plan (lists, numbers, strings)."""
    if isinstance(value, (list, tuple)):
        return '{' + ','.join(lua(v) for v in value) + '}'
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, str):
        return "'" + value.replace('\\', '\\\\').replace("'", "\\'") + "'"
    return repr(value)


def continue_session(repo, dll, save, out, minutes, sets=(), session=None, recipe=None, env=None):
    """x4ls continue (no X4): a session of `save` with this DLL; (overlay, snapshot). A folder made before is used again."""
    out = Path(out)
    if (out / 'overlay').exists() and (out / 'session-save.xml.gz').exists():
        return out / 'overlay', out / 'session-save.xml.gz'
    if out.exists():
        shutil.rmtree(out)
    cmd = launcher(repo, 'continue', '--save', save, '--gate-dll', dll, '--out', out, '--minutes', minutes, '--save-every', 0)
    if session:
        cmd += ['--session', session]
    if recipe:
        cmd += ['--overlay', recipe]
    for item in sets:
        cmd += ['--set', item]
    code = run_logged(cmd, repo, str(out) + '.log', env)
    if code or not (out / 'overlay').exists():
        raise RuntimeError('x4ls continue failed (%d): %s.log' % (code, out))
    return out / 'overlay', out / 'session-save.xml.gz'


def fresh_session(repo, dll, players, out, minutes, display=None, lanes=None, env=None):
    """x4ls new-session (one capture X4 on the capture profile): every player on the Terran cadet start with its own
    story copy (players 2..N: --start K=terran1 --omit K=world --story K). Made once per checkout and player count."""
    out = Path(out)
    if (out / 'session.json').exists():
        return out
    if out.exists():
        shutil.rmtree(out)
    cmd = launcher(repo, 'new-session', '--base-start', CADET, '--players', players, '--mode', 'coop', '--gate-dll', dll, '--out', out,
                   '--minutes', minutes, '--no-focus')
    for k in range(2, players + 1):
        cmd += ['--start', '%d=%s' % (k, CADET), '--omit', '%d=world' % k]
    if players > 1:
        cmd += ['--story', ','.join(str(k) for k in range(2, players + 1))]
    if display:
        cmd += ['--display', display]
    if lanes:
        cmd += ['--lanes', lanes]
    # X4LS_BESIDE: other sessions' X4 run on this PC (their own profiles); the capture has its own profile and view. The
    # capture profile is one per PC: wait for any other capture (an X4 started on it), and hold capture-45841 meanwhile.
    gate = LOCKS / ('capture-%d' % CAPTURE_PROFILE)
    while True:
        busy = captures_running()
        if not busy:
            try:
                gate.mkdir()
                break
            except FileExistsError:
                pass
        time.sleep(10)
    try:
        (gate / 'owner.txt').write_text('%s (x4ls new-session capture) %s%s' % (ROOT.name, time.strftime('%Y-%m-%d %H:%M:%S'), chr(10)), encoding='utf-8')
        code = run_logged(cmd, repo, str(out) + '.log', env or dict(os.environ, X4LS_BESIDE='1'), timeout=1800)
    finally:
        shutil.rmtree(gate, ignore_errors=True)
    if code or not (out / 'session.json').exists():
        raise RuntimeError('x4ls new-session failed (%d): %s.log' % (code, out))
    return out


CAPTURE_PROFILE = 45841       # src/lockstep/launcher/profile.py: every capture of this PC


def hosted_session(folder, players):
    """satellite --hosted: a made session folder (session.json, session-save.xml.gz), e.g. the one a hosted game of the
    cadet start keeps (<kit>/sessions/host-x4ep1_gamestart_terran1-<N>p-coop-<dll>-n). It must be a session of the cadet
    start for these players in which every player 2..N plays the cadet story as its own copy (as fresh_session's)."""
    folder = Path(folder)
    try:
        data = json.loads((folder / 'session.json').read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise SystemExit('--hosted %s: no session.json (%s)' % (folder, error))
    if data.get('base_start') != CADET or data.get('players') != players:
        raise SystemExit('--hosted %s: a session of %s for %s players, not of %s for %d'
                         % (folder, data.get('base_start'), data.get('players'), CADET, players))
    slots = data.get('slots') or {}
    missing = [k for k in range(2, players + 1) if not (slots.get(str(k)) or {}).get('plots')]
    if missing:
        raise SystemExit('--hosted %s: players %s have no cadet story of their own (a session made before 26 Nov?)'
                         % (folder, ', '.join(map(str, missing))))
    if not (folder / 'session-save.xml.gz').is_file():
        raise SystemExit('--hosted %s: no session-save.xml.gz' % folder)
    return folder


def captures_running():
    """The X4 processes started on the capture profile (another session's capture)."""
    try:
        import psutil
    except ImportError:
        return []
    out = []
    for proc in psutil.process_iter(['name', 'cmdline']):
        try:
            if (proc.info['name'] or '').lower() == 'x4.exe' and str(CAPTURE_PROFILE) in ' '.join(proc.info['cmdline'] or []):
                out.append(proc.pid)
        except (psutil.Error, TypeError):
            pass
    return out


def session_overlay(repo, session_dir, dll, out, players, minutes, sets=(), env=None):
    """A play overlay of a made session with more settings (build_play_overlay.py --session; no X4). Made again each
    time (the builder freezes an overlay with its .base beside it)."""
    out = Path(out)
    for old in (out, Path(str(out) + '.base')):
        if old.exists():
            shutil.rmtree(old)
    cmd = [sys.executable, Path(repo) / 'tools/build_play_overlay.py', '--snapshot', Path(session_dir) / 'session-save.xml.gz',
           '--gate-dll', dll, '--out', out, '--players', players, '--mode', 'coop', '--minutes', minutes, '--save-every', 0,
           '--session', Path(session_dir) / 'session.json']
    for item in sets:
        cmd += ['--set', item]
    code = run_logged(cmd, repo, str(out) + '.log', env)
    if code or not out.exists():
        raise RuntimeError('build_play_overlay failed (%d): %s.log' % (code, out))
    return out


def below_normal(pids):
    """Windows: the test clients below normal priority (a person's work on this PC comes first)."""
    if os.name != 'nt':
        return
    import ctypes
    kernel = ctypes.WinDLL('kernel32')
    kernel.OpenProcess.restype = ctypes.c_void_p
    for pid in pids:
        handle = kernel.OpenProcess(0x0200, False, int(pid))
        if handle:
            kernel.SetPriorityClass(ctypes.c_void_p(handle), 0x4000)
            kernel.CloseHandle(ctypes.c_void_p(handle))


class Lane:
    """Where this run's X4 go: lanes folder, monitor, window size."""

    def __init__(self, lanes=None, display=3, size='960x540'):
        self.lanes, self.display, self.size = lanes, display, size


BUSY = 'X4 is running; close it first'   # x4ls play beside an X4 that is no launcher client (another session's capture)


def play(repo, overlay, snapshot, lane, name, first_profile, clients=None, solo=None, log=print, timeout=None, allowed=None,
         busy_wait=1800, until=None):
    """x4ls play until the session is over (--close-when-over); the run folder (the checkout's .local/runs/<time>). While
    another X4 that is no launcher client runs (a capture), x4ls refuses to start: it is asked again for up to busy_wait s."""
    waited = 0
    while True:
        try:
            return play_once(repo, overlay, snapshot, lane, name, first_profile, clients, solo, log, timeout, allowed, until)
        except RuntimeError as error:
            host_log = WORK / 'logs' / (name + '.log')
            text = host_log.read_text(encoding='utf-8', errors='replace') if host_log.exists() else ''
            if BUSY not in text or waited >= busy_wait or 'refused: profiles' in str(error):
                raise
            if not waited:
                log('%s: another X4 that is no launcher client runs (a capture); waiting' % name)
            time.sleep(30)
            waited += 30


def live_logs(first_profile, slots):
    """{slot: the client log X4 writes in its profile while it runs}."""
    sys.path.insert(0, str(ROOT / 'src'))
    from lockstep.launcher import profile as launcher_profile
    return {k: launcher_profile.documents() / str(profile_of(first_profile, k)) / ('x4ls-c%d.log' % k) for k in slots}


def clear_stale_state(repo, log=print):
    """A launcher state file a killed run left in that checkout (x4ls play then refuses: 'a previous session is still
    recorded'): removed with its views when none of its processes runs any more; True when the checkout is free."""
    state = Path(repo) / '.local/x4ls/session.json'
    if not state.exists():
        return True
    try:
        import psutil
        data = json.loads(state.read_text(encoding='utf-8'))
    except (ImportError, OSError, ValueError):
        return False
    # a relay or X4 of that run still going after its launcher was stopped: this checkout's own (its path or the run's
    # views in the command line), so it is stopped; any other process with a recorded pid keeps the state
    live = []
    for pid in list(data.get('pids') or []) + [data.get('relay_pid')]:
        if pid and psutil.pid_exists(int(pid)):
            try:
                proc = psutil.Process(int(pid))
                text = ' '.join(proc.cmdline()) + ' ' + (proc.exe() or '')
            except (psutil.Error, OSError):
                continue
            ours = [str(Path(repo))] + [str(Path(v)) for v in data.get('views') or []]
            if not any(o.lower() in text.replace('/', chr(92)).lower() for o in ours):
                return False
            live.append(proc)
    for proc in live:
        log('%s: a process of the stopped run still ran (pid %d): stopped' % (state, proc.pid))
        try:
            proc.kill()
            proc.wait(10)
        except (psutil.Error, OSError):
            return False
    for view in data.get('views') or []:
        view = Path(view)
        if not view.exists():
            continue
        if not any(view.iterdir()):
            view.rmdir()
            continue
        sys.path.insert(0, str(ROOT / 'src'))
        from lockstep.launcher import game as launcher_game      # removes a view without entering its junctions
        try:
            launcher_game.remove_view(view)
        except SystemExit as refused:
            log('%s: %s' % (view, refused))
            return False
    state.unlink()
    log('%s: the state of a stopped run was removed' % state)
    return True


def suites_in(repo):
    """The Python test processes that run from that tree (a unittest/pytest command whose working folder or command
    line is the tree): its launcher tests stop the session recorded in the tree's .local/x4ls/session.json, and a live
    session's X4 then all exit with code 15 at once (25 Nov)."""
    try:
        import psutil
    except ImportError:
        return []
    tree = str(Path(repo).resolve()).lower()
    out = []
    mine = {os.getpid()}                  # this process and its parents (a test of this tool runs as a suite itself)
    try:
        mine |= {p.pid for p in psutil.Process().parents()}
    except psutil.Error:
        pass
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        if proc.info['pid'] in mine:
            continue
        try:
            cmd = ' '.join(proc.info.get('cmdline') or [])
            if 'python' not in (proc.info.get('name') or '').lower() or not re.search(r'\bunittest\b|\bpytest\b', cmd):
                continue
            cwd = (proc.cwd() or '').lower()
        except (psutil.Error, OSError):
            continue
        if cwd == tree or cwd.startswith(tree + os.sep) or tree in cmd.replace('/', os.sep).lower():
            out.append(proc.info['pid'])
    return out


def wait_for_suites(repo, log=print, limit=1800, poll=15):
    """No run starts while a test suite runs from its tree; waits for it (up to `limit` seconds, then refuses)."""
    started = time.time()
    said = False
    while True:
        pids = suites_in(repo)
        if not pids:
            return
        if time.time() - started > limit:
            raise RuntimeError('refused: a test suite runs from %s (pid %s); its launcher tests would stop the session' %
                               (repo, ','.join(map(str, pids))))
        if not said:
            log('%s: a test suite runs from this tree (pid %s): the run waits for it' % (repo, ','.join(map(str, pids))))
            said = True
        time.sleep(poll)


def play_once(repo, overlay, snapshot, lane, name, first_profile, clients=None, solo=None, log=print, timeout=None, allowed=None,
              until=None):
    """One x4ls play (see play). until(logs) -> True ends the run early (the check has its rows): its X4 are closed, and the
    launcher collects the logs as at the session's end; so does the timeout."""
    repo = Path(repo)
    wait_for_suites(repo, log)            # a suite from this tree would stop the session (code 15)
    clear_stale_state(repo, log)
    runs = repo / '.local/runs'
    before = set(runs.glob('2*')) if runs.exists() else set()
    cmd = launcher(repo, 'play', '--overlay', overlay, '--snapshot', snapshot, '--close-when-over', '--never-focus', '--size', lane.size)
    cmd += ['--solo', solo] if solo else ['--clients', clients]
    if lane.display:
        cmd += ['--display', lane.display]
    if lane.lanes:
        cmd += ['--lanes', lane.lanes]
    host_log = WORK / 'logs' / (name + '.log')
    host_log.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, X4LS_FIRST_PROFILE=str(first_profile))
    if allowed:                       # its slots are held (C:/x4ls-locks): other X4 on other slots are no reason to refuse
        env['X4LS_BESIDE'] = '1'
    used = [profile_of(first_profile, k) for k in ([solo] if solo else range(1, clients + 1))]
    allowed = set(allowed) if allowed else {45855, 45856, 45857, 45858}
    if any(p not in allowed for p in used):
        raise RuntimeError('refused: profiles %s, outside the held slots %s' % (used, sorted(allowed)))
    log('%s: %s, profiles %s' % (name, 'solo %d' % solo if solo else '%d clients' % clients, ','.join(map(str, used))))
    with open(host_log, 'w', encoding='utf-8') as out:
        out.write('$ ' + ' '.join(map(str, cmd)) + '\n')
        out.flush()
        host = subprocess.Popen(list(map(str, cmd)), cwd=repo, env=env, stdout=out, stderr=subprocess.STDOUT)
        started, lowered, run_dir, cpu, closing = time.time(), set(), None, None, None
        slots = [solo] if solo else list(range(1, clients + 1))
        try:
            while host.poll() is None:
                time.sleep(5)
                pids = set(re.findall(r'client \d+: pid (\d+)', host_log.read_text(encoding='utf-8', errors='replace'))) - lowered
                if pids:
                    below_normal(pids)
                    lowered |= pids
                if run_dir is None and runs.exists():
                    fresh = sorted(set(runs.glob('2*')) - before)
                    run_dir = fresh[-1] if fresh else None
                if run_dir is not None and lowered:
                    cpu = cpu or open(Path(run_dir) / 'release-check-cpu.jsonl', 'a', encoding='utf-8')
                    sample_cpu(cpu, run_dir, lowered)
                if closing is None:
                    why = 'over its time (%d s)' % timeout if timeout and time.time() - started > timeout else None
                    if why is None and until and lowered:
                        try:
                            why = until(live_logs(first_profile, slots), host_log) or None
                        except OSError:
                            why = None
                    if why:
                        log('%s: %s: closing its X4' % (name, why if isinstance(why, str) else 'done'))
                        closing = time.time()
                        for pid in lowered:
                            subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
                elif time.time() - closing > 240:
                    log('%s: the launcher did not end after its X4 were closed: stopping it' % name)
                    break
        finally:
            if cpu:
                cpu.close()
            if host.poll() is None:          # its own clients first (their pids from its log), then the launcher
                for pid in lowered:
                    subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], capture_output=True)
                host.kill()
                host.wait(timeout=60)
                # the views and the state file the launcher had recorded for this checkout (its clients only)
                run_logged(launcher(repo, 'clean'), repo, WORK / 'logs' / (name + '-clean.log'))
    fresh = sorted(set(runs.glob('2*')) - before) if runs.exists() else []
    if not fresh:
        raise RuntimeError('%s: no run folder (see %s)' % (name, host_log))
    log('%s: run %s (%d s)' % (name, fresh[-1], time.time() - started))
    return fresh[-1]


def last_relay_tick(run_dir):
    """The relay's last released tick (the end of relay.log)."""
    try:
        with open(Path(run_dir) / 'relay.log', 'rb') as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4096))
            ticks = re.findall(rb'NATIVE_GATE tick=(\d+) ', f.read())
        return int(ticks[-1]) if ticks else None
    except OSError:
        return None


def sample_cpu(out, run_dir, pids):
    """One line of the clients' processor time (every thread of each X4: user + kernel seconds) at the relay's tick: what a
    tick costs independent of the other programs on the PC (the wall time per tick waits for the processor with them)."""
    try:
        import psutil
    except ImportError:
        return
    tick = last_relay_tick(run_dir)
    if tick is None:
        return
    times = {}
    for pid in pids:
        try:
            t = psutil.Process(int(pid)).cpu_times()
            times[str(pid)] = round(t.user + t.system, 3)
        except (psutil.Error, ValueError):
            pass
    out.write(json.dumps({'t': round(time.time(), 2), 'tick': tick, 'cpu': times}) + chr(10))
    out.flush()


def cpu_per_tick(run_dir, skip=PERF_SKIP, end=None):
    """Microseconds of processor time per tick, all threads of the run's X4 summed (release-check-cpu.jsonl); None
    without the samples."""
    first = last = None
    for line in read_lines(Path(run_dir) / 'release-check-cpu.jsonl'):
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if row['tick'] < skip or (end is not None and row['tick'] > end) or not row['cpu']:
            continue
        total = sum(row['cpu'].values())
        if first is None:
            first = (row['tick'], total, set(row['cpu']))
        if set(row['cpu']) == first[2]:
            last = (row['tick'], total)
    if not first or not last or last[0] <= first[0]:
        return None
    return (last[1] - first[1]) / (last[0] - first[0]) / max(1, len(first[2])) * 1e6


def client_logs(run_dir):
    """{slot: client log} of a run folder (x4ls collects client K's log as clientK/engine-output.log; a copied log may
    keep its profile name x4ls-cK.log)."""
    out = {}
    for path in sorted(Path(run_dir).glob('client*/*.log')):
        m = re.match(r'client(\d+)$', path.parent.name)
        if m and path.name in ('engine-output.log', 'x4ls-c%s.log' % m.group(1)):
            out.setdefault(int(m.group(1)), path)
    return out


def sync_state(run_dir):
    """(in sync, the first desync line or '') of a run: the relay's notices and its desync status."""
    lines = [l for l in read_lines(Path(run_dir) / 'desyncs.log') if 'DESYNC' in l or 'CLOCK_SPLIT' in l]
    try:
        status = json.loads((Path(run_dir) / 'desync-status.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        status = {}
    first = status.get('first_mismatch')
    return not lines and not first, (lines[0].strip() if lines else ('first mismatch at tick %s' % first if first else ''))


def world_hashes(log):
    """{tick: hash} of a client's WORLD_HASH rows (the census)."""
    out = {}
    for _, kind, text in rows(log, {'WORLD_HASH'}):
        f = fields(text)
        value = f.get('hash') or f.get('state') or f.get('root')
        if f.get('tick') and value:
            out[int(f['tick'])] = value
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Plans (native_gate_ui_replay_script: one list per slot, ticks after the pilot window's start).
YAW_AXIS, ACCELERATE, AUTOPILOT_KEY = 0xd, 13, 0xb3
SCRIPT = re.compile(r'UI_REPLAY_SCRIPT tick=(\d+) op=(\w+) ok=(\w+) result=(.*?)(?: scope=([01]))?$')


def perf_plan(end, start=300, cycle=1800):
    """The same scripted flight for every build (ops the older releases have too): every minute throttle up, the
    travel drive on and off, a yaw turn, the guns fired at a point ahead."""
    plan = []
    for c in range(start, end - cycle + 1, cycle):
        plan += [[c, 'input', 'state', ACCELERATE, 1], [c + 150, 'input', 'state', ACCELERATE, 0],
                 [c + 300, 'travel_key'], [c + 750, 'travel_key'],
                 [c + 900, 'every', 2, c + 1200, [['input', 'range', YAW_AXIS, 0.5]]], [c + 1202, 'input', 'range', YAW_AXIS, 0.0],
                 [c + 1300, 'aim_fire', 1, 0, 0, 1000], [c + 1500, 'aim_fire', 0]]
    return plan


def has_op(repo, name):
    """Whether that checkout's UI replay core knows a script op (an older release's plan must not name a newer op: an
    unknown op refuses the whole plan)."""
    try:
        return ("op=='%s'" % name) in (Path(repo) / 'tools/dev/native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
    except OSError:
        return False


def missions_plan(end, offer='Defenders', weapons=False):
    """A player's mission checks on its own client: the state after the continue, Shift+A (the saved destination), the
    mission manager opened and closed, a constant-speed travel (the HUD's speed number), then every 10 s the look and
    the story offer accepted once it is there, and made active."""
    return [[150, 'rc_look'], [160, 'rc_offers', offer], [200, 'input_local', 'action', AUTOPILOT_KEY, 0], [290, 'rc_look'], [330, 'ap_stop'],
            [400, 'rc_mm', 'open'], [460, 'rc_look'], [520, 'rc_mm', 'close'],
            [600, 'input', 'state', ACCELERATE, 1], [750, 'input', 'state', ACCELERATE, 0], [800, 'travel_key'], [1700, 'travel_key'],
            [1800, 'every', 300, end, [['rc_look'], ['rc_offers', offer]]],
            [1800, 'every', 150, end, [['rc_accept', offer]]],
            [1950, 'every', 300, end, [['rc_activate', offer]]]] +         ([[140, 'hud_weapons'], [1300, 'aim_fire', 1, 0, 0, 1000], [1330, 'hud_weapons'], [1360, 'aim_fire', 0]] if weapons else [])


def satellite_plan(end, ship_every=None):
    """A player flying the cadet's first chapter by its own inputs (rc_drive every second), and its look every 10 s;
    with ship_every, the ship it leaves for the spacesuit every that many ticks (rc_ship)."""
    plan = [[200, 'rc_look'], [210, 'rc_offers'], [300, 'every', 30, end, [['rc_drive']]], [330, 'every', 300, end, [['rc_look']]]]
    if ship_every:
        plan.append([305, 'every', ship_every, end, [['rc_ship']]])
    return plan


GET_UP_KEY = 0x115


def suit_plan(at=600):
    """Out in the spacesuit and floating beside the ship, nothing else, with ops every release since 12 Nov has (the
    get-up key through the replayed input, the transporter menu's Use Spacesuit): a split of the empty ship or the suit."""
    return [[at, 'input', 'action', GET_UP_KEY, 0], [at + 100, 'suit_out']]


CENSUS_SETS = ('native_gate_world_hash_spread=false', 'native_gate_world_record=16', 'native_gate_digest_parts={1,400000}')


def census_sets(every):
    """A census every `every` ticks read in one step, the last 16 records kept (the probe's limit), the digest's parts every tick: a split
    named to the capture and the object (tools/dev/census_record_diff.py), and its tick (DIGEST_PARTS)."""
    return ['native_gate_world_hash_stride=%d' % every] + list(CENSUS_SETS) if every else []


def census_diff(run, limit=20):
    """tools/dev/census_record_diff.py on clients 1 and 2 of a run: the lines that name the split's objects."""
    if not (Path(run) / 'client2').exists():
        return []
    x4 = None
    try:
        sys.path.insert(0, str(ROOT / 'src'))
        from lockstep.launcher import game as launcher_game
        x4 = launcher_game.find_install() / 'X4.exe'
    except Exception:
        x4 = None
    cmd = [sys.executable, str(DEV / 'census_record_diff.py'), str(Path(run) / 'client1'), str(Path(run) / 'client2'), '--limit', str(limit)]
    if x4 and Path(x4).exists():
        cmd += ['--x4', str(x4)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=600).stdout
    except subprocess.TimeoutExpired:
        return ['census_record_diff: over its time']
    return [l for l in out.splitlines() if l.strip()][:limit + 10]


PART = re.compile(r'(\w+)=(\w+)')


def first_part_split(logs):
    """The first tick whose DIGEST_PARTS rows differ between clients 1 and 2, and the parts that differ there."""
    if 1 not in logs or 2 not in logs:
        return None
    parts = []
    for k in (1, 2):
        found = {}
        for _, kind, text in rows(logs[k], {'DIGEST_PARTS'}):
            f = dict(PART.findall(text))
            if f.get('tick'):
                found[int(f['tick'])] = f
        parts.append(found)
    for t in sorted(set(parts[0]) & set(parts[1])):
        a, b = parts[0][t], parts[1][t]
        differ = sorted(k for k in set(a) | set(b) if k not in ('tick', 'hash') and a.get(k) != b.get(k))
        if differ:
            return t, differ
    return None


def plans_for(players, own_plan, slots=None):
    """Every slot's plan: `own_plan` (a list, or a function of the slot) for the slots played (all by default), nothing
    for the others."""
    return [(own_plan(k) if callable(own_plan) else own_plan) if (slots is None or k in slots) else [] for k in range(1, players + 1)]


def range_plan(slot, players, at=2300, spacing=300):
    """The lead mark's range, one player after the other: its ship put 1 km, then 10 km from the next player's ship (the
    test placement), that ship its target, and what its HUD reads (aim_look: the aim assist state, in weapon range)."""
    if players < 2:
        return []
    other = slot % players + 1
    t = at + (slot - 1) * spacing
    return [[t, 'goto_slot', other, 1000], [t + 30, 'target_slot', other], [t + 60, 'aim_look'],
            [t + 90, 'goto_slot', other, 10000], [t + 150, 'aim_look'], [t + 180, 'target_none']]


def script_rows(log, ops=None):
    """[(tick, op, ok, result)] of a client's script rows (ops: a set to keep)."""
    out = []
    for _, kind, text in rows(log, {'UI_REPLAY_SCRIPT'}):
        m = SCRIPT.match('UI_REPLAY_SCRIPT ' + text)
        if m and (ops is None or m.group(2) in ops):
            out.append((int(m.group(1)), m.group(2), m.group(3) == 'true', m.group(4)))
    return out


LIST = re.compile(r'(\w+)=(\d+)\[([^\]]*)\]')
OWN = re.compile(r'(?:^| )own=\[([^\]]*)\]')


def parse_look(result):
    """An rc_look answer as a dict: slot, active (id, owner), listed [(id, owner)], all [(id, owner)], own [names],
    guidance, target, distance, ap, step, objective, menus."""
    f = dict(FIELD.findall(result))
    out = {'slot': f.get('slot'), 'guidance': f.get('guidance'), 'target': f.get('target'), 'ap': f.get('ap'),
           'step': f.get('step'), 'objective': f.get('objective', ''), 'menus': f.get('menus', '-'), 'distance': number(f.get('distance'))}
    out['active'] = tuple(((f.get('active') or '?/?').split('/') + ['?'])[:2])
    for name, count, body in LIST.findall(result):
        out[name] = [tuple(x.split('/', 1)) for x in body.split(',') if x and x != '+']
        out[name + '_n'] = int(count)
    for name in ('listed', 'all', 'story'):
        out.setdefault(name, [])
    m = OWN.search(result)
    out['own'] = [n for n in m.group(1).split(',') if n] if m else []
    return out


def parse_offers(result):
    """An rc_offers answer: {'n', 'story': [(owner, name)], 'other'}."""
    f = dict(FIELD.findall(result))
    look = parse_look(result)
    return {'n': int(f.get('n') or 0), 'story': look['story'], 'other': int(f.get('other') or 0)}


# ---------------------------------------------------------------------------------------------------------------------
# Verdicts: each check prints 'PASS|FAIL|n/a  name  detail' lines and the rows that decided them.
class Report:
    def __init__(self, title):
        self.title, self.lines, self.failed, self.passed = title, [], 0, 0

    def check(self, name, ok, detail, evidence=()):
        verdict = 'n/a' if ok is None else 'PASS' if ok else 'FAIL'
        self.failed += ok is False
        self.passed += ok is True
        self.lines.append('%-4s  %-34s %s' % (verdict, name, detail))
        for row in list(evidence)[:6]:
            self.lines.append('        | %s' % str(row)[:300])

    def note(self, text):
        self.lines.append('      %s' % text)

    def text(self):
        head = '== %s: %s (%d passed, %d failed)' % (self.title, 'FAIL' if self.failed else 'PASS', self.passed, self.failed)
        return '\n'.join([head] + self.lines)


def own_owner(slot):
    """The owners a mission listed on slot's client may have: its own slot, or nobody (0)."""
    return {str(slot), '0'}


def missions_verdict(report, logs, offer=None, fresh=False):
    """The mission checks of every client's rc_look / rc_offers rows (logs: {slot: client log})."""
    for k, log in sorted(logs.items()):
        looks = [(t, parse_look(r)) for t, op, ok, r in script_rows(log, {'rc_look'}) if ok]
        offers = [(t, parse_offers(r)) for t, op, ok, r in script_rows(log, {'rc_offers'}) if ok]
        ops = script_rows(log, {'rc_mm', 'rc_accept', 'rc_activate'})
        if not looks:
            report.check('player %d: rows' % k, False, 'no rc_look row on client %d (the plan did not run)' % k)
            continue
        # the mission manager opens
        opened = [t for t, op, ok, r in ops if op == 'rc_mm' and ok and r.startswith('opened')]
        shown = [(t, l['menus']) for t, l in looks if 'MapMenu' in l['menus']]
        if opened or not fresh:
            report.check('player %d: mission manager opens' % k, bool(opened and shown),
                         'opened at tick %s, shown: %s' % (opened[:1] or '-', shown[0] if shown else 'never'), [r for r in ops if r[1] == 'rc_mm'][:2])
        # the list: this player's missions only, and every mission MD gave this player
        foreign = sorted({m for t, l in looks for m in l['listed'] if len(m) == 2 and m[1] not in own_owner(k)})
        report.check('player %d: lists only its own missions' % k, not foreign,
                     '%d look(s); %s' % (len(looks), 'another player\'s mission listed (id/owner): %s' % foreign[:3] if foreign else
                                         'every listed mission is this player\'s or nobody\'s'))
        missing = []
        for t, l in looks:
            if l.get('all_n', 0) > len(l['all']):
                continue                      # a cut list proves nothing missing
            listed = {m[0] for m in l['listed']}
            mine = {m[0] for m in l['all'] if len(m) == 2 and m[1] == str(k)}
            if mine - listed:
                missing.append((t, sorted(mine - listed)))
        last = looks[-1][1]
        report.check('player %d: lists every mission MD gave it' % k, not missing,
                     'last look: listed %d (own: %s), MD (mpp_owner=%d) %d of %d in the world' %
                     (len(last['listed']), ','.join(last['own']) or '-', k, sum(1 for m in last['all'] if len(m) == 2 and m[1] == str(k)),
                      len(last['all'])), ['tick %d: MD gave %s, not listed' % m for m in missing[:3]])
        # the world's mission count as the probe's UI_MISSIONS_STATE row says it (the manager's vector), against the look's
        states = [(int(f['tick']), int(f['missions'])) for _, kind, text in rows(log, {'UI_MISSIONS_STATE'})
                  for f in [fields(text)] if f.get('tick') and f.get('missions')]
        differ = []
        for t, l in looks:
            before = [n for at, n in states if at <= t]
            if before and 'all_n' in l and before[-1] != l['all_n']:
                differ.append((t, l['all_n'], before[-1]))
        if states:
            report.check('player %d: world count = the manager' % k, len(differ) <= len(looks) // 10,
                         '%d look(s), %d with another count than UI_MISSIONS_STATE' % (len(looks), len(differ)),
                         ['tick %d: look %d, manager %d' % d for d in differ[:3]])
        others = [(t, o['other']) for t, o in offers if o['other']]
        if others:
            report.note('player %d: offers of another player in its list (rc_offers other=%d at tick %d)' % (k, others[-1][1], others[-1][0]))
        # the active mission (the HUD's mission panel) is this player's, and the guidance points at its objective
        own_active = [(t, l) for t, l in looks if l['active'][0] not in ('0', '?')]
        wrong = [(t, l['active']) for t, l in own_active if l['active'][1] not in own_owner(k)]
        has_own = any(len(m) == 2 and m[1] == str(k) for t, l in looks for m in l['listed'] + l['all'])
        if own_active or has_own:
            report.check('player %d: active mission is its own' % k, bool(own_active) and not wrong,
                         ('active %s (owner %s) at tick %d' % (own_active[-1][1]['active'] + (own_active[-1][0],)) if own_active else
                          'no active mission while it has its own') + ('; another player\'s: %s' % wrong[:2] if wrong else ''))
        # The yellow objective is the active mission's objective target (the HUD draws the manager's active mission; X4's
        # guidance component is set by a guidance mission, Plot course, and is 'none' otherwise): it must be there for an
        # active mission with an objective, and a guidance that is set must point at it.
        aimed = [(t, l) for t, l in own_active if l['target'] not in ('0', '?', None)]
        off = [(t, l['guidance'], l['target']) for t, l in aimed if l['guidance'] not in ('none', None, l['target'])]
        if own_active:
            report.check('player %d: objective marker = its mission' % k, bool(aimed) and len(off) <= len(aimed) // 5,
                         '%d of %d looks with its active mission had an objective target%s' %
                         (len(aimed), len(own_active), '; %d with the guidance elsewhere' % len(off) if off else ''),
                         ['tick %d guidance=%s target=%s' % o for o in off[:3]])
        if offer:
            accepted = [(t, r) for t, op, ok, r in ops if op == 'rc_accept' and ok and r.startswith('accepted ')]
            took = [t for t, l in looks if any(offer in n for n in l['own'])]
            seen = [(t, s) for t, o in offers for s in o['story'] if len(s) == 2 and offer in s[1]]
            if accepted or seen or k > 1:
                report.check('player %d: story offer "%s" accepted' % (k, offer), bool(accepted and took),
                             ('accepted at tick %d, listed as its own mission from tick %s' % (accepted[0][0], took[0] if took else '- (never)'))
                             if accepted else ('offered (owner %s) but never accepted' % seen[0][1][0] if seen else 'never offered to this player'),
                             [r for r in ops if r[1] == 'rc_accept' and r[3] != 'nil'][:2] + [s for s in seen[:1]])


def shift_a_verdict(report, logs):
    """Shift+A after a continue: the look before and after the key (the plan's first two looks) of every player."""
    for k, log in sorted(logs.items()):
        looks = [parse_look(r) for t, op, ok, r in script_rows(log, {'rc_look'}) if ok]
        before, after = (looks[0], looks[1]) if len(looks) > 1 else (None, None)
        if not before or not after:
            report.check('player %d: Shift+A after the continue' % k, None, 'no look before and after the key')
            continue
        if before['active'][0] in ('0', '?') or before['target'] in ('0', '?', None):
            report.check('player %d: Shift+A after the continue' % k, None, 'no saved destination (no active mission with a target)')
        else:
            report.check('player %d: Shift+A after the continue' % k, after['ap'] == '1',
                         'active mission %s, objective target %s at %s m; after Shift+A the autopilot %s' %
                         (before['active'][0], before['target'], fmt(before['distance']), 'flies' if after['ap'] == '1' else 'is off'))


WEAPON = re.compile(r'\b([ps])(\d+)=(\d)/(\d+)/')


def weapons_verdict(report, logs):
    """The HUD weapon panel of every player (script op hud_weapons: GetWeaponDetails3's active flag per weapon, as the
    panel colours it): the weapons of the active group show active, for player 1 and every other player alike."""
    seen = {}
    for k, log in sorted(logs.items()):
        looks = [(t, r) for t, op, ok, r in script_rows(log, {'hud_weapons'}) if ok and 'no_ship' not in r]
        if looks:
            t, r = looks[-1]
            seen[k] = (t, [(kind, int(n), int(active)) for kind, n, active, mode in WEAPON.findall(r)], r)
    if not seen:
        return
    for k, (t, weapons, r) in sorted(seen.items()):
        primary = [w for w in weapons if w[0] == 'p']
        active = sum(w[2] for w in primary)
        if not primary:
            report.check('player %d: weapon panel' % k, None, 'no primary weapon on the ship')
            continue
        report.check('player %d: weapon panel' % k, active > 0,
                     '%d of %d primary weapons shown active at tick %d%s' % (active, len(primary), t,
                     '' if k == 1 or 1 not in seen else ' (player 1: %d of %d)' % (sum(w[2] for w in seen[1][1] if w[0] == 'p'),
                                                                                    sum(1 for w in seen[1][1] if w[0] == 'p'))),
                     [r[:200]] if not active else ())


AIM_LOOK = re.compile(r'assist=(-?\d+) mouse=(\w+) target=(\S+) in_range=(\w+)')


def range_verdict(report, logs):
    """The lead mark's range for every player (aim_look at 1 km and 10 km from the target): in weapon range near, out of
    it far, and the aim assist state as player 1's at the same distances."""
    seen = {}
    for k, log in sorted(logs.items()):
        looks = [AIM_LOOK.search(r) for t, op, ok, r in script_rows(log, {'aim_look'}) if ok]
        looks = [m.groups() for m in looks if m]
        if len(looks) >= 2:
            seen[k] = looks[-2:]
    for k, (near, far) in sorted(seen.items()):
        same = k == 1 or 1 not in seen or [near[0], far[0]] == [seen[1][0][0], seen[1][1][0]]
        report.check('player %d: lead mark range' % k, near[3] == 'true' and far[3] == 'false' and same,
                     'in weapon range at 1 km: %s, at 10 km: %s; aim assist %s/%s%s' %
                     (near[3], far[3], near[0], far[0], '' if k == 1 or 1 not in seen else
                      ' (player 1: %s/%s)' % (seen[1][0][0], seen[1][1][0])))


THROTTLE = re.compile(r'OWN_THROTTLE tick=(\d+) .*?key=(-?\d+).*? travel=(\d)/(\d).*? mps=([-\d.e+]+)/([-\d.e+]+)')


def throttle_verdict(report, logs, tolerance=0.02, window=10, overshoot=0.10):
    """The HUD's speed number (OWN_THROTTLE rows, native_gate_own_throttle_rows=1: written while the HUD shows what the
    simulation does not yet), with no key held:
    steady  the simulated speed of the rows within `window` ticks stays within 2%: the shown number must stay within 2%;
    settle  the simulated speed changes by less than 1% a tick: the shown number must not be more than 10% from it (the
            display's prediction runs past the speed the ship settles at, and the number jumps back).
    Three or more such rows in a row are a jump of the number, off rows in more than a tenth of the steady ones a blinking
    number (FAIL)."""
    for k, log in sorted(logs.items()):
        samples = {}
        for _, kind, text in rows(log, {'OWN_THROTTLE'}):
            m = THROTTLE.search('OWN_THROTTLE ' + text)
            if m:
                samples[int(m.group(1))] = (int(m.group(2)), float(m.group(5)), float(m.group(6)))
        if not samples:
            report.check('player %d: HUD speed steady' % k, None, 'no OWN_THROTTLE rows (the HUD showed the number of the simulation throughout)')
            continue
        steady, off, run, jumps = 0, [], [], []
        for t in sorted(samples):
            key, sim, shown = samples[t]
            near = [samples[u][1] for u in range(t - window, t + window + 1) if u in samples]
            before = samples.get(t - 1) or samples.get(t - 2)
            flat = len(near) >= 3 and (max(near) - min(near)) <= tolerance * statistics.fmean(near)
            slow = before is not None and abs(sim - before[1]) < 0.01 * sim
            if key or sim < 10 or not (flat or slow):
                run = []
                continue
            steady += flat
            bad = abs(shown - sim) > (tolerance if flat else overshoot) * sim
            if bad:
                off.append((t, sim, shown))
                run.append(t)
                if len(run) == 3:
                    jumps.append(t)
            else:
                run = []
        pulsing = len(off) >= 3 and len(off) > 0.1 * max(steady, 1)      # every other row off: the number blinks
        report.check('player %d: HUD speed steady' % k, not jumps and not pulsing,
                     '%d steady-speed rows; %d row(s) with the shown number off (2%% at a steady speed, 10%% while it settles), %d jump(s)' %
                     (steady, len(off), len(jumps)),
                     ['tick %d: simulated %.1f m/s, shown %.1f' % o for o in off[:4]])


MILESTONE = re.compile(r'MILESTONE reached name=(\S+)')
SPEECH = re.compile(r'SPEECH_START tick=(\d+) speaker=(\S+) owner=(\d+) scope=(\d+) muted=(\d)')


def satellite_verdict(report, logs, players, run_dir, solo=None):
    """M1 for every slot on every client, the story's speech on its owner's client only, the session in sync."""
    reached = {k: {m.group(1) for line in read_lines(log) for m in [MILESTONE.search(line)] if m} for k, log in logs.items()}
    # one X4 (--solo K): the other slots are stood in and sit idle, only slot K plays
    for slot in ([solo] if solo else range(1, players + 1)):
        name = 'terran_cadet-M1-slot%d' % slot
        on = [k for k in sorted(logs) if name in reached[k]]
        drive = [r for t, op, ok, r in script_rows(logs[slot], {'rc_drive'})][-3:] if slot in logs else []
        report.check('player %d: cadet chapter 1 (M1)' % slot, len(on) == len(logs),
                     '%s on client(s) %s of %s' % (name, on or '-', sorted(logs)), drive if len(on) < len(logs) else ())
    speech = {k: [m.groups() for line in read_lines(log) for m in [SPEECH.search(line)] if m] for k, log in logs.items()}
    if len(logs) > 1:
        for slot in range(1, players + 1):
            own = [s for s in speech.get(slot, []) if s[2] == str(slot)]
            heard = [s for s in own if s[4] == '0']
            leaked = [(k, s) for k in logs if k != slot for s in speech[k] if s[2] == str(slot) and s[4] == '0']
            report.check('player %d: its speech on its client only' % slot, bool(heard) and not leaked,
                         '%d line(s) of owner %d played on client %d, %d played on another client' % (len(heard), slot, slot, len(leaked)),
                         ['client %d: %s' % (k, ' '.join(s)) for k, s in leaked[:3]])
    sync_verdict(report, logs, run_dir, solo)


def sync_verdict(report, logs, run_dir, solo=None):
    if solo:
        report.check('in sync', None, 'one X4 (--solo %d): the relay compares that client with itself' % solo)
        return
    ok, first = sync_state(run_dir)
    hashes = {k: world_hashes(log) for k, log in logs.items()}
    common = sorted(set.intersection(*(set(h) for h in hashes.values()))) if hashes else []
    split = [t for t in common if len({hashes[k][t] for k in hashes}) > 1]
    report.check('in sync', ok and not split and bool(common),
                 '%s; census at %d common tick(s), %d differ' % ('no desync' if ok else first[:160], len(common), len(split)),
                 ['tick %d differs' % t for t in split[:3]])


# the session's planned end (launcher session.STOP_OK) and its notice; a mismatch that ended it is the in-sync check's
STOP_END = re.compile(r'NATIVE_GATE STOP .*reason=1 restored=1|NATIVE_GATE STOP_NOTICE reason=1 ')


def stops(logs):
    """Gate stops of a run's clients (a check ran on a stopped client is no check); the session's planned end is none."""
    out = []
    for k, log in logs.items():
        for line in read_lines(log):
            if 'NATIVE_GATE STOP' in line and not STOP_END.search(line):
                out.append('client %d: %s' % (k, line.split('[X4LAN] ')[-1][:200]))
                break
    return out


def crash_notes(logs):
    """The gate's crash notes beside each client log (x4lan-crash.txt: first-chance exceptions it caught, a CRASH row of
    an unhandled one): (client, count, first line)."""
    out = []
    for k, log in sorted(logs.items()):
        lines = [l for l in read_lines(Path(log).parent / 'x4lan-crash.txt') if 'CRASH' in l]
        if lines:
            out.append((k, len(lines), lines[0].split('[X4LAN] ')[-1][:160]))
    return out


def common_checks(report, logs, run_dir=None):
    """What every run reports first: gate stops and the relay's drops (failures), crash notes (named)."""
    for line in stops(logs):
        report.check('running', False, line)
    for line in read_lines(Path(run_dir) / 'desyncs.log') if run_dir else []:
        m = re.search(r'DROP slot=(\d+) tick=(\d+)', line)
        if m:
            report.check('running', False, 'the relay dropped client %s at tick %s (%s)' % (m.group(1), m.group(2), line.strip()[:120]))
    for k, n, first in crash_notes(logs):
        report.note('client %d: %d crash note(s) of the gate, first: %s' % (k, n, first))
    for k, line in config_refusals(logs):
        report.check('session settings taken', False, 'client %d: %s' % (k, line))


def config_refusals(logs):
    """(client, the probe's reason) for each client whose probe refused the session's settings (CONFIG_REFUSED: the
    gate runs without the settings, the run proves nothing)."""
    out = []
    for k, log in sorted(logs.items()):
        for line in read_lines(log):
            if 'CONFIG_REFUSED' in line:
                out.append((k, line.split('CONFIG_REFUSED', 1)[1].strip()[-200:]))
                break
            # an arming step of the probe that failed (live: a Linux-only key on Windows): what it arms after it may
            # be off for the rest of the run
            if 'NATIVE_GATE ERROR [string' in line and 'native_gate_probe.lua' in line:
                out.append((k, line.split('NATIVE_GATE ERROR', 1)[1].strip()[:200]))
                break
    return out


# ---------------------------------------------------------------------------------------------------------------------
# The checks as runs.
PERF_SETS = ('native_gate_probe_profile=true', 'native_gate_frame_timing=true')
# The session keys whose modules came between rk9 and rk15 (native_gate_defaults.lua): the default --ablate list.
NEW_MODULES = ('pilot_present_hud_zone', 'pilot_aim_present', 'own_throttle', 'pilot_present_transit', 'md_fullscreen',
               'pilot_travel', 'turret_assist', 'cover_slots', 'highway_players', 'hud_weapons', 'escort_present')


def default_keys(repo):
    """The session default keys (without native_gate_) of that checkout's native_gate_defaults.lua."""
    try:
        text = (Path(repo) / 'tools/dev/native_gate_defaults.lua').read_text(encoding='utf-8')
    except OSError:
        return set()
    return set(re.findall(r"\{'native_gate_(\w+)',", text))


MIN_FREE_GB = 8          # no run starts below this much free space on the drive (25 Nov: a full C: froze runs)
KEEP_IN_RUN = ('summary.json', 'desyncs.log', 'desync-status.json', 'errors.log', 'release-check-report.txt',
               'release-check-profile.json', 'release-check-cpu.jsonl')


def disk_free_gb(path):
    try:
        return shutil.disk_usage(str(Path(path).anchor or path)).free / 2 ** 30
    except OSError:
        return None


def wait_for_disk(need_gb, log=print, path=None, poll=60):
    """Wait until the drive of `path` (this checkout's) has need_gb free; a line every five minutes meanwhile."""
    path = path or ROOT
    said = 0.0
    while True:
        free = disk_free_gb(path)
        if free is None or free >= need_gb:
            return free
        if time.time() - said >= 300:
            log('disk: %.1f GB free on %s, a run needs %d GB: waiting' % (free, Path(path).anchor, need_gb))
            said = time.time()
        time.sleep(poll)


def save_report(run, report):
    """The verdict beside the run (release-check-report.txt), so it outlives the pruned logs."""
    try:
        (Path(run) / 'release-check-report.txt').write_text(report.text() + chr(10), encoding='utf-8')
    except OSError:
        pass


def prune_run(run, log=print):
    """After the verdict: the run's raw X4 logs, saves and relay journals go; the verdict, the census records
    (x4lan-census-*.txt), the crash notes and the relay's summaries stay. The bytes freed."""
    run = Path(run)
    freed = 0
    if not (run / 'release-check-report.txt').exists() and not (run / 'release-check-profile.json').exists():
        return 0                          # no verdict kept: nothing is deleted
    for path in sorted(run.rglob('*'), key=lambda p: -len(p.parts)):
        rel = path.relative_to(run)
        keep = (len(rel.parts) == 1 and rel.name in KEEP_IN_RUN) or rel.name.startswith('x4lan-census-') or \
            rel.name == 'x4lan-crash.txt' or path.is_dir()
        if keep:
            continue
        try:
            freed += path.stat().st_size
            path.unlink()
        except OSError:
            pass
    for path in sorted((p for p in run.rglob('*') if p.is_dir()), key=lambda p: -len(p.parts)):
        try:
            path.rmdir()                  # the emptied folders (saves/milestones, client installed copies)
        except OSError:
            pass
    log('%s: raw logs and saves removed (%.0f MB), the verdict and census records kept' % (run, freed / 2 ** 20))
    return freed


class Runner:
    """Locks, lanes and logging shared by every run of one command."""

    def __init__(self, args):
        self.args = args
        self.lane = Lane(args.lanes, args.display, args.size)
        slots = [x if x.startswith('x4-') else 'x4-' + x for x in args.lock_slots.split(',') if x] if args.lock_slots else None
        self.locks = Locks(args.lock_name, args.lock_what, slots=slots, beside_user=args.beside_user,
                           pause=WORK / 'PAUSE', skip_queue=getattr(args, 'capture_skips_queue', False) or getattr(args, 'skip_queue', False)) if args.locks else None
        self.log = lambda text: print(time.strftime('%H:%M:%S ') + text, flush=True)

    def held(self, want, body):
        """body(first lock or None) under the locks `want` asks for (none without --locks)."""
        if not self.locks:
            return body(None)
        held = self.locks.acquire(want)
        self.log('locks: %s' % ','.join(held))
        try:
            return body(held[0])
        finally:
            self.locks.done()

    def play(self, repo, overlay, snapshot, name, clients=None, solo=None, timeout=None, until=None):
        want = 'one' if solo else 'three' if clients >= 3 else 'two'
        wait_for_disk(getattr(self.args, 'min_free_gb', MIN_FREE_GB), self.log, self.lane.lanes or ROOT)

        def body(lock):
            allowed = None
            if lock:
                index = self.locks.index
                first = (index - (solo - 1)) % 8 if solo else index
                allowed = lock_profiles(self.locks.held)
            else:
                first = self.args.first_profile
            if first is None:
                raise SystemExit('without --locks give --first-profile (the index of the profiles your slots own)')
            return play(repo, overlay, snapshot, self.lane, name, first, clients=clients, solo=solo, log=self.log, timeout=timeout,
                        allowed=allowed, until=until)
        return self.held(want, body)


def build_list(args):
    """[(tag, dll, repo)] of the candidate and every baseline."""
    out = []
    dll, commit = parse_dll(args.candidate)
    out.append(('candidate', dll, checkout(commit)))
    for i, spec in enumerate(args.baseline or []):
        dll, commit = parse_dll(spec)
        out.append(('baseline%d' % (i + 1) if len(args.baseline) > 1 else 'baseline', dll, checkout(commit)))
    return out


def save_result(name, data):
    path = WORK / 'results' / (name + '.json')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1, default=str) + '\n', encoding='utf-8')
    return path


def cmd_perf(args):
    runner = Runner(args)
    builds = build_list(args)
    players = json.loads(Path(args.session).read_text(encoding='utf-8')).get('players', 2) if args.session else 2
    slots = [args.solo] if args.solo else list(range(1, players + 1))
    end = int(args.minutes * 60 * 30)
    plan = lua(plans_for(players, perf_plan(end), slots))
    sets = list(PERF_SETS) + (['native_gate_sample_profile=%d' % args.sample_ms] if args.sample_ms else []) + \
        ['native_gate_ui_replay_script=' + plan] + list(args.set)
    variants = [(tag, dll, repo, ()) for tag, dll, repo in builds]
    ablate = [k for k in (args.ablate.split(',') if args.ablate else []) if k]
    if args.ablate == 'new':
        ablate = list(NEW_MODULES)
    known = default_keys(builds[0][2])
    skipped = [k for k in ablate if known and k not in known]
    if skipped:
        runner.log('ablate: not session keys of the candidate tree, left out: %s' % ','.join(skipped))
    for key in [k for k in ablate if not known or k in known]:
        variants.append(('without-%s' % key, builds[0][1], builds[0][2], ('native_gate_%s=false' % key,)))
    stamp = time.strftime('%Y%m%d-%H%M%S')
    sessions = {}
    for tag, dll, repo, extra in variants:
        sha = file_sha(dll)[:8]
        sessions[tag] = continue_session(repo, dll, args.save, WORK / 'sessions' / ('perf-%s-%s-%s-%dm%s' % (tag, sha, Path(args.save).stem.split('.')[0],
                                         args.minutes, ('-s%d' % args.solo if args.solo else '') + args.tag)), args.minutes, sets + list(extra),
                                         session=args.session, recipe=args.recipe)
    profiles = {}
    for r in range(args.repeat):
        for tag, dll, repo, extra in variants:
            if tag.startswith('without-') and r >= args.ablate_repeat:
                continue                  # a module's cost: its runs in the first --ablate-repeat rounds only
            overlay, snapshot = sessions[tag]
            run = runner.play(repo, overlay, snapshot, 'perf-%s-%s-r%d' % (stamp, tag, r + 1), clients=None if args.solo else players,
                              solo=args.solo, timeout=int(args.minutes * 60 * 3 + 900))
            logs = client_logs(run)
            got = 0
            kept = {}
            for k in slots:
                if k in logs:
                    profile = perf_profile(logs[k], run)
                    if profile['samples']:
                        profiles.setdefault((tag, k), []).append(profile)
                        kept[k] = profile
                        got += 1
            if kept:
                (Path(run) / 'release-check-profile.json').write_text(json.dumps({str(k): v for k, v in kept.items()}), encoding='utf-8')
                if not args.keep_logs:
                    prune_run(run, runner.log)
            if not got:
                runner.log('%s: the run gave no timing rows (its X4 did not get to the session: %s); the series stops' % (tag, run))
                return perf_summary(profiles, variants, slots, args.tolerance) or 2
            save_result('perf-%s-%s-r%d' % (stamp, tag, r + 1), {'run': str(run), 'dll': str(dll), 'repo': str(repo), 'extra': extra})
    return perf_summary(profiles, variants, slots, args.tolerance)


def merge_profiles(items):
    """The mean of several runs' profiles of one build (the timing means and rates averaged, tps averaged)."""
    if len(items) == 1:
        return items[0]
    out = {'log': ' + '.join(p['log'] for p in items), 'samples': sum(p['samples'] for p in items), 'ticks': items[0]['ticks']}
    for key in ('tps', 'tick_us', 'frame_us', 'sim_p90_us', 'cpu_us'):
        values = [p.get(key) for p in items if p.get(key) is not None]
        out[key] = statistics.fmean(values) if values else None
    for part in ('timing', 'rates'):
        names = set().union(*(p[part] for p in items))
        out[part] = {n: statistics.fmean(p[part][n] for p in items if n in p[part]) for n in names}
    return out


def run_noise(profiles, k):
    """(runs, tick spread, frame spread) over the repeated runs of one build for client k: the largest max-min of
    tick_us and frame_us among the builds with two or more runs (None without repeats). On a PC other programs load,
    that is what a module's cost must exceed to mean anything."""
    best = None
    for (tag, slot), items in profiles.items():
        if slot != k or tag.startswith('without-') or len(items) < 2:
            continue
        ticks = [p['tick_us'] for p in items if p.get('tick_us') is not None]
        frames = [p['frame_us'] for p in items if p.get('frame_us') is not None]
        if len(ticks) < 2:
            continue
        spread = (len(items), max(ticks) - min(ticks), (max(frames) - min(frames)) if len(frames) > 1 else 0.0)
        best = spread if best is None else (max(best[0], spread[0]), max(best[1], spread[1]), max(best[2], spread[2]))
    return best


def noise_text(noise):
    if not noise:
        return 'run-to-run noise unknown: no build ran twice'
    return 'run-to-run spread of one build: %.0f us per tick, %.0f us per frame' % (noise[1], noise[2])


def perf_summary(profiles, variants, slots, tolerance=TOLERANCE):
    report = Report('performance')
    for k in slots:
        cand = profiles.get(('candidate', k))
        if not cand:
            report.check('client %d: candidate' % k, False, 'no candidate run')
            continue
        cand = merge_profiles(cand)
        for tag, dll, repo, extra in variants:
            if tag == 'candidate' or tag.startswith('without-') or (tag, k) not in profiles:
                continue
            base = merge_profiles(profiles[(tag, k)])
            lines, failed = perf_verdict(tag, base, cand, tolerance)
            report.check('client %d: candidate vs %s' % (k, tag), not failed,
                         'per tick %s -> %s us, per frame %s -> %s us, %s -> %s ticks/s (%s)' %
                         (fmt(base.get('tick_us')), fmt(cand.get('tick_us')), fmt(base.get('frame_us')), fmt(cand.get('frame_us')),
                          fmt(base.get('tps')), fmt(cand.get('tps')), Path(str(dll)).parent.name or dll))
            report.lines += ['      ' + l for l in lines]
        costs = []
        for tag, dll, repo, extra in variants:
            if tag.startswith('without-') and (tag, k) in profiles:
                off = merge_profiles(profiles[(tag, k)])
                costs.append((tag[len('without-'):], (cand.get('tick_us') or 0) - (off.get('tick_us') or 0),
                              (cand.get('frame_us') or 0) - (off.get('frame_us') or 0)))
        if costs:
            noise = run_noise(profiles, k)
            report.note('client %d: each module\'s own cost (candidate minus the candidate without it; %s):' % (k, noise_text(noise)))
            for key, tick, frame in sorted(costs, key=lambda c: -(c[1] + c[2])):
                inside = noise and abs(tick) <= noise[1] and abs(frame) <= noise[2]
                report.note('  %-28s per tick %+7.0f us, per frame %+7.0f us%s' % (key, tick, frame, ' (within the noise)' if inside else ''))
    print(report.text())
    return 1 if report.failed else 0


def file_sha(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def cmd_missions(args):
    """The mission manager per player on the continue of a save (the Terran story's offer by its ten-minute timer),
    with the quick checks Shift+A after the continue and the HUD's speed number."""
    runner = Runner(args)
    dll, commit = parse_dll(args.gate_dll)
    repo = checkout(commit)
    players = json.loads(Path(args.session).read_text(encoding='utf-8')).get('players', 2) if args.session else args.players
    end = int(args.minutes * 60 * 30)
    slots = [args.solo] if args.solo else None
    own = missions_plan(end, args.offer, weapons=has_op(repo, 'hud_weapons'))
    aim = has_op(repo, 'aim_look')
    plan = lua(plans_for(players, lambda k: own + (range_plan(k, players) if aim and not args.solo else []), slots))
    sets = ['native_gate_ui_replay_script=' + plan, 'native_gate_own_throttle_rows=1'] + list(args.set)
    overlay, snapshot = continue_session(repo, dll, args.save, WORK / 'sessions' / ('missions-%s-%s-%dm%s' % (
        file_sha(dll)[:8], Path(args.save).stem.split('.')[0], args.minutes, ('-s%d' % args.solo if args.solo else '') + args.tag)),
        args.minutes, sets, session=args.session, recipe=args.recipe)
    run = runner.play(repo, overlay, snapshot, 'missions-%s' % time.strftime('%Y%m%d-%H%M%S'), clients=None if args.solo else players,
                      solo=args.solo, timeout=int(args.minutes * 60 * 3 + 900))
    code = missions_report(run, args.offer, players, args.solo)
    if not args.keep_logs:
        prune_run(run, runner.log)
    return code


def missions_report(run, offer, players, solo=None):
    logs = client_logs(run)
    report = Report('missions (%s)' % run)
    common_checks(report, logs, run)
    missions_verdict(report, logs, offer)
    shift_a_verdict(report, logs)
    throttle_verdict(report, logs)
    weapons_verdict(report, logs)
    range_verdict(report, logs)
    sync_verdict(report, logs, run, solo)
    print(report.text())
    save_report(run, report)
    return 1 if report.failed else 0


def cmd_satellite(args):
    """Every player on the Terran cadet start flies its first chapter (M1), in one session."""
    runner = Runner(args)
    dll, commit = parse_dll(args.gate_dll)
    repo = checkout(commit)
    end = int(args.minutes * 60 * 30)
    if getattr(args, 'hosted', None):   # a session made elsewhere: a hosted game's (the Host form's Start, x4ls host)
        session = hosted_session(args.hosted, args.players)
    else:
        session = WORK / 'sessions' / ('cadet-%dp-%s' % (args.players, (commit or 'here')[:8]))
    if not (session / 'session.json').exists():
        runner.held('one', lambda lock: fresh_session(repo, dll, args.players, session, args.minutes, display=args.display,
                                                       lanes=args.lanes))
    slots = [args.solo] if args.solo else None
    if args.plan == 'suit':           # the spacesuit alone (any release's ops): slot --suit-slot gets out and floats
        plan = lua([suit_plan() if k == args.suit_slot else [] for k in range(1, args.players + 1)])
    else:
        plan = lua(plans_for(args.players, satellite_plan(end, 10 if args.census_every and has_op(repo, 'rc_ship') else None), slots))
    sets = ['native_gate_ui_replay_script=' + plan] + census_sets(args.census_every) + list(args.set)
    overlay = session_overlay(repo, session, dll, WORK / 'sessions' / ('%s-%s-%s-%dm%s' % (session.name, args.plan, file_sha(dll)[:8], args.minutes,
                              ('-s%d' % args.solo if args.solo else '') + args.tag)), args.players, args.minutes, sets)
    slots = [args.solo] if args.solo else list(range(1, args.players + 1))
    run = runner.play(repo, overlay, session / 'session-save.xml.gz', '%s-%dp-%s' % (args.plan, args.players, time.strftime('%Y%m%d-%H%M%S')),
                      clients=None if args.solo else args.players, solo=args.solo, timeout=int(args.minutes * 60 * 3 + 900),
                      until=milestones_done(slots, len(slots)) if args.plan == 'satellite' else None)
    if args.plan == 'suit':
        code = suit_report(run, args.solo)
    else:
        code = satellite_report(run, args.players, args.solo)
    if not args.keep_logs:
        prune_run(run, runner.log)
    return code


def suit_report(run, solo=None):
    logs = client_logs(run)
    report = Report('spacesuit beside the ship (%s)' % run)
    common_checks(report, logs, run)
    suit = [(k, t, r) for k, log in sorted(logs.items()) for t, op, ok, r in script_rows(log, {'suit_out'})]
    report.check('the suit-out ran', bool(suit) and all(r.startswith('zone=') for _, _, r in suit),
                 '; '.join('client %d tick %d: %s' % x for x in suit[:2]) or 'no suit_out row')
    sync_verdict(report, logs, run, solo)
    split = first_part_split(logs)
    if split:
        report.note('first tick whose digest parts differ: %d (%s)' % (split[0], ' '.join(split[1])))
    if report.failed:
        for line in census_diff(run):
            report.note('census: ' + line)
    print(report.text())
    save_report(run, report)
    return 1 if report.failed else 0


class Watch:
    """The rows a run waits for, read incrementally from the clients' growing logs: found(name, slot) when that client's
    log has a MILESTONE reached row of that name. A profile's log of an earlier run (written before `since`) is not
    read; a log that X4 started anew (another file, or shorter than what was read) is read from its start again (live:
    the previous run's longer log on that profile was read to its end first, so the new log's M1 rows were never read
    and two runs went on to their time)."""

    def __init__(self):
        self.offsets, self.found, self.rest, self.files = {}, {}, {}, {}

    def forget(self, k):
        self.offsets.pop(k, None)
        self.rest.pop(k, None)
        for seen in self.found.values():
            seen.pop(k, None)

    def read(self, logs, since=None):
        for k, path in logs.items():
            try:
                st = os.stat(path)
                if since and st.st_mtime < since:
                    continue                  # still the log of an earlier run on that profile
                if self.files.get(k) not in (None, st.st_ino) or st.st_size < self.offsets.get(k, 0):
                    self.forget(k)
                self.files[k] = st.st_ino
                with open(path, 'rb') as f:
                    f.seek(self.offsets.get(k, 0))
                    data = self.rest.get(k, b'') + f.read(1 << 24)
                    self.offsets[k] = f.tell()
            except OSError:
                continue
            *lines, self.rest[k] = data.split(bytes([10]))
            for line in lines:
                m = re.search(rb'MILESTONE reached name=(\S+)', line)
                if m:
                    self.found.setdefault(m.group(1).decode(), {})[k] = time.time()


RELAY_DESYNC = re.compile(rb'relay: NATIVE_GATE DESYNC ')


def milestones_done(slots, clients, settle=120):
    """until() for a satellite run: every slot's M1 on every client's log, then `settle` seconds more (a census after it).
    A session the relay found split ends as well once every slot's M1 is in on its own client and `settle` seconds
    passed (on another client it may never come: that world went its own way, and the in-sync check fails anyway)."""
    watch = Watch()
    state = {'split': False}

    def until(logs, host_log=None):
        since = None
        if host_log:                          # the launcher's log is made when the run starts
            try:
                since = os.stat(host_log).st_ctime - 2
            except OSError:
                since = None
        watch.read(logs, since)
        names = ['terran_cadet-M1-slot%d' % k for k in slots]
        if all(len(watch.found.get(n, {})) >= clients for n in names):
            last = max(t for n in names for t in watch.found[n].values())
            if time.time() - last >= settle:
                return 'every M1 on every client'
            return None
        if not state['split'] and host_log:
            try:
                state['split'] = bool(RELAY_DESYNC.search(Path(host_log).read_bytes()))
            except OSError:
                pass
        own = [watch.found.get(n, {}).get(k) for k, n in zip(slots, names)]
        if state['split'] and all(own) and time.time() - max(own) >= settle:
            return 'the session split; every M1 on its own client'
        return None
    return until


def satellite_report(run, players, solo=None):
    logs = client_logs(run)
    report = Report('first mission to the satellite (%s)' % run)
    common_checks(report, logs, run)
    satellite_verdict(report, logs, players, run, solo)
    missions_verdict(report, logs, None, fresh=True)
    split = first_part_split(logs)
    if split:
        report.note('first tick whose digest parts differ: %d (%s)' % (split[0], ' '.join(split[1])))
    if not solo and not sync_state(run)[0]:
        for line in census_diff(run):
            report.note('census: ' + line)
    print(report.text())
    save_report(run, report)
    return 1 if report.failed else 0


def cmd_report(args):
    if args.kind == 'perf':
        # NAME=RUN groups (the same name: repeats of one build, merged); without names the first run is the candidate
        profiles, variants, named = {}, [], []
        for i, spec in enumerate(map(str, args.runs)):
            name, sep, run = spec.partition('=')
            if not sep or len(name) > 40 or ':' in name:
                name, run = ('candidate' if i == 0 else 'baseline%d' % i), spec
            named.append((name, Path(run)))
        if not any(n == 'candidate' for n, _ in named) or len({n for n, _ in named}) < 2:
            raise SystemExit('report perf [candidate=]RUN [NAME=]RUN ...: a candidate and at least one other build')
        slots = sorted({k for _, run in named for k in client_logs(run)})
        for name, run in named:
            if name not in [v[0] for v in variants]:
                variants.append((name, run, None, ()))
            kept = Path(run) / 'release-check-profile.json'
            if not client_logs(run) and kept.exists():          # a pruned run: the profile its series kept
                for k, profile in json.loads(kept.read_text(encoding='utf-8')).items():
                    profiles.setdefault((name, int(k)), []).append(profile)
                    slots = sorted(set(slots) | {int(k)})
                continue
            for k, log in client_logs(run).items():
                profiles.setdefault((name, k), []).append(perf_profile(log, run, args.skip))
        return perf_summary(profiles, variants, slots, args.tolerance)
    code = 0
    for run in args.runs:
        players = args.players or len(client_logs(run)) or 2
        solo = None
        try:
            solo = json.loads((Path(run) / 'summary.json').read_text(encoding='utf-8')).get('solo')
        except (OSError, ValueError):
            pass
        if args.kind == 'missions':
            code = max(code, missions_report(run, args.offer, players, solo))
        else:
            code = max(code, satellite_report(run, players, solo))
    return code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)

    def common(p):
        p.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=VALUE', help='more session settings')
        p.add_argument('--tag', default='', help='a name part of the sessions this run makes (a variant with --set)')
        p.add_argument('--lanes', default='C:/x4ls-lanes-relcheck', help="the clients' game views (default C:/x4ls-lanes-relcheck)")
        p.add_argument('--display', type=int, default=3, help='the monitor of the windows (default 3)')
        p.add_argument('--size', default='960x540')
        p.add_argument('--locks', action='store_true', help='take this PC\'s slots by C:/x4ls-locks (and wait for them)')
        p.add_argument('--lock-name', default='X4-Multiplayer-%s' % ROOT.name.split('-')[-1], help='the name in owner.txt and want-both.txt')
        p.add_argument('--lock-what', default='release check')
        p.add_argument('--lock-slots', default='', help='the slots this run may take, e.g. a,b (default: all four)')
        p.add_argument('--beside-user', action='store_true',
                       help='x4-a and x4-b may be taken while USER-PLAYING.txt exists (only when the coordinator has opened them)')
        p.add_argument('--first-profile', type=int, help='without --locks: X4LS_FIRST_PROFILE of the profiles this run may use')
        p.add_argument('--tolerance', type=float, default=TOLERANCE, help='how much slower fails (default 0.10)')
        p.add_argument('--min-free-gb', type=float, default=MIN_FREE_GB, help='no run starts below this free space (default 8)')
        p.add_argument('--keep-logs', action='store_true', help="keep a run's raw X4 logs and saves after its verdict")
        p.add_argument('--skip-queue', action='store_true', help='a one-slot run does not wait behind queued pairs (when the coordinator says so)')

    def continued(p, required=True):
        p.add_argument('--save', type=Path, required=required, help='the save to continue (a periodic, a person\'s or a milestone save)')
        p.add_argument('--session', type=Path, help='its session.json (a save without a sidecar)')
        p.add_argument('--recipe', type=Path, help='a folder with its play-recipe.json (a save without a sidecar)')

    perf = sub.add_parser('perf', help='the candidate against baseline builds on the same session and play')
    for p in (perf,):
        p.add_argument('--candidate', required=True, help='the gate DLL (DLL@COMMIT: from that commit\'s tree; default this checkout)')
        p.add_argument('--baseline', action='append', default=[], help='DLL@COMMIT of a baseline release (repeat for more)')
        p.add_argument('--solo', type=int, help='one X4 for this slot (default: every slot, one X4 each)')
        p.add_argument('--minutes', type=float, default=6)
        p.add_argument('--repeat', type=int, default=1, help='runs per build, interleaved (default 1)')
        p.add_argument('--ablate', default='', help="session keys (without native_gate_) to turn off in extra candidate runs; 'new': %s" % ','.join(NEW_MODULES))
        p.add_argument('--ablate-repeat', type=int, default=1, help='rounds of --repeat that include the --ablate runs (default 1)')
        p.add_argument('--sample-ms', type=int, default=0, help='native_gate_sample_profile period (0: off)')
        continued(p)
        common(p)
    missions = sub.add_parser('missions', help='the mission manager per player on a continued save')
    missions.add_argument('--gate-dll', required=True)
    missions.add_argument('--players', type=int, default=2)
    missions.add_argument('--solo', type=int)
    missions.add_argument('--minutes', type=float, default=14, help='default 14 (the Terran story offers by a ten-minute timer)')
    missions.add_argument('--offer', default='Defenders', help='a part of the story offer\'s name (default Defenders)')
    continued(missions)
    common(missions)
    sat = sub.add_parser('satellite', help='every player on the cadet start flies its first chapter (M1)')
    sat.add_argument('--gate-dll', required=True)
    sat.add_argument('--players', type=int, default=2)
    sat.add_argument('--solo', type=int)
    sat.add_argument('--minutes', type=float, default=20)
    sat.add_argument('--plan', choices=('satellite', 'suit'), default='satellite',
                     help='satellite: every player flies the cadet chapter 1; suit: one player gets out in the spacesuit and floats beside its ship')
    sat.add_argument('--suit-slot', type=int, default=1)
    sat.add_argument('--capture-skips-queue', action='store_true',
                     help='the session capture (one slot, a few minutes) does not wait behind queued pairs (when the coordinator allows it)')
    sat.add_argument('--census-every', type=int, default=0, help='a census every N ticks in one step, the last 16 records, digest parts (a split named)')
    sat.add_argument('--hosted', metavar='DIR', help='a made session of the cadet start instead of one made here: e.g. a '
                     'hosted game\'s (<kit>/sessions/host-x4ep1_gamestart_terran1-<N>p-coop-<dll>-n)')
    common(sat)
    every = sub.add_parser('all', help='perf, missions and satellite')
    every.add_argument('--candidate', required=True)
    every.add_argument('--baseline', action='append', default=[])
    every.add_argument('--players', type=int, default=2)
    every.add_argument('--solo', type=int)
    every.add_argument('--minutes', type=float, default=6, help='of the perf runs')
    every.add_argument('--repeat', type=int, default=1)
    every.add_argument('--ablate', default='')
    every.add_argument('--ablate-repeat', type=int, default=1)
    every.add_argument('--sample-ms', type=int, default=0)
    every.add_argument('--offer', default='Defenders')
    every.add_argument('--skip-checks', default='', help='comma separated: perf, missions, satellite')
    continued(every)
    common(every)
    logs = sub.add_parser('perf-logs', help='two client logs compared (no X4)')
    logs.add_argument('base', type=Path)
    logs.add_argument('cand', type=Path)
    logs.add_argument('--base-run', type=Path)
    logs.add_argument('--cand-run', type=Path)
    logs.add_argument('--skip', type=int, default=PERF_SKIP)
    logs.add_argument('--end', type=int)
    logs.add_argument('--tolerance', type=float, default=TOLERANCE)
    rep = sub.add_parser('report', help='the verdicts of finished runs again (no X4)')
    rep.add_argument('kind', choices=('perf', 'missions', 'satellite'))
    rep.add_argument('runs', nargs='+', help='run folders; perf: NAME=RUN (candidate=..., rk9=..., without-KEY=...; a name twice: repeats)')
    rep.add_argument('--players', type=int)
    rep.add_argument('--offer', default='Defenders')
    rep.add_argument('--skip', type=int, default=PERF_SKIP)
    rep.add_argument('--tolerance', type=float, default=TOLERANCE)
    args = parser.parse_args(argv)
    if args.command == 'perf-logs':
        return perf_logs(args)
    if args.command == 'report':
        return cmd_report(args)
    if args.command == 'all':
        args.gate_dll = args.candidate
        args.skip_checks = [c for c in args.skip_checks.split(',') if c]
        sat_minutes = 20
        perf_minutes = args.minutes
        code = 0
        for name in ('perf', 'missions', 'satellite'):
            if name in args.skip_checks:
                continue
            args.minutes = perf_minutes if name == 'perf' else 14 if name == 'missions' else sat_minutes
            fn = {'perf': cmd_perf, 'missions': cmd_missions, 'satellite': cmd_satellite}[name]
            print('\n######## %s' % name, flush=True)
            try:
                code = max(code, fn(args))
            except (RuntimeError, TimeoutError) as error:
                print('%s: the run did not work: %s' % (name, error))
                code = 2
        return code
    return {'perf': cmd_perf, 'missions': cmd_missions, 'satellite': cmd_satellite}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
