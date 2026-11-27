"""Per-player attention measurement (docs/ATTENTION-PER-PLAYER.md): two clients, the players in two sectors, run once with
every player's sector at high attention on every client (today's lockstep) and once with each client keeping only its own
player's sector high (lockstep_hybrid.h, native_gate_hybrid, a default-off test switch). Then what diverges, how fast, and
what it saves.

    python tools/dev/attn_split.py run --gate-dll DLL --variants base,hyb12 [--minutes 6] [--locks ...]
    python tools/dev/attn_split.py report RUN [RUN ...]            (no X4: the numbers of finished runs)

Session: the release check's continued save (rk9-save c3-t18000, both players at the Terran cadet start in
cluster_101_sector001). Slot 2's ship is put into another busy sector at plan tick 60 (goto_obj: 12 km from the Pioneers
trade station OVL-841 of cluster_113_sector001, 15 stations and ~130 ships), before any variant's split, so every variant
has the same world up to the split. Both players then turn and fire on a fixed schedule; slot 1 also throttles (it stays
inside its sector: no travel drive, a crossing would end the hybrid run with stop 187).

Variants (keys added to the session; every client gets the same overlay):
  base    today: both players' sectors at high attention on both clients
  hyb12   native_gate_hybrid={owners={1,2}}: client k keeps only slot k's sector high; the other player's sector runs at low
          attention there (the user's proposal); the census declares the outside roots
  hyb12f  hyb12 with a census every 100 ticks (run it with --minutes 1): the time course of the split
  hyb1    native_gate_hybrid={owners={1}}: client 2 drops slot 1's sector, client 1 keeps both (one-sided)
  mask2   native_gate_view_points_mask=1: slot 2 is no view point on ANY client (symmetric, stays in sync): the price of
          slot 2's bubble without any divergence
  base_s, mask2_s   base and mask2 with the MoveWorker sampler (native_gate_sample_profile=2)
Rows read: FRAME_TIMING (sim_us per tick, capture ticks left out), DIGEST_PARTS (every tick: which declared part parts
first), WORLD_HASH / census records (x4lan-census-*.txt: the objects that differ, by sector), HYBRID (members, proxies and
the low-attention copy's distance from the owner's published poses), VIEW_POINTS (evaluations per attention level),
the clients' memory (working set and private bytes, sampled every 5 s).
"""
import argparse
import json
import statistics
import os
import sys
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import release_check as rc  # noqa: E402

ROOT = rc.ROOT
WORK = ROOT / '.local/attn-split'
# The rk9 save folder (c3-t18000.xml.gz + session.json): X4LS_ATTN_SAVE_DIR, else .local/rk9-save in this checkout.
SAVE_DIR = Path(os.environ.get('X4LS_ATTN_SAVE_DIR') or (ROOT / '.local/rk9-save'))
TARGET = '000000000003cb84'          # OVL-841, Pioneers trade station, cluster_113_sector001 (rk9-save c3-t18000)
SPOT = [0, 2000, -12000]             # metres in the station's frame: 12 km behind it, 2 km above
SPLIT_TICK = 400                     # the hybrid split's latch (gate ticks; the move is at gate tick ~100)
STRIDE = 900                         # census every 900 ticks, read in one step, its record kept (16 records)
FINE = {'hyb12f': 100}               # variants with a census every 100 ticks (a 1-minute run: 16 records from the split on)
YAW, ACCEL = 0xd, 13


def plan(slot, end, start=300, cycle=1800):
    """Slot 1: throttle, yaw turns, the guns ahead. Slot 2: moved at 60, then yaw turns and guns only (no throttle: it
    stays 12 km from the station)."""
    out = [[60, 'goto_obj', TARGET, SPOT]] if slot == 2 else []
    for c in range(start, end - cycle + 1, cycle):
        if slot == 1:
            out += [[c, 'input', 'state', ACCEL, 1], [c + 150, 'input', 'state', ACCEL, 0]]
        out += [[c + 900, 'every', 2, c + 1200, [['input', 'range', YAW, 0.5]]], [c + 1202, 'input', 'range', YAW, 0.0],
                [c + 1300, 'aim_fire', 1, 0, 0, 1000], [c + 1500, 'aim_fire', 0]]
    out.append([90, 'every', 300, end, [['place']]])
    return out


VARIANTS = {
    'base': [],
    'hyb12': ['native_gate_hybrid={owners={1,2},tick=%d,poses=4}' % SPLIT_TICK],
    'hyb1': ['native_gate_hybrid={owners={1},tick=%d,poses=4}' % SPLIT_TICK],
    'mask2': ['native_gate_view_points_mask=1'],
    'hyb12f': ['native_gate_hybrid={owners={1,2},tick=%d,poses=4}' % SPLIT_TICK],   # hyb12 with the fine census (FINE)
    # the same two with the MoveWorker sampler (2 ms; tools/dev/census_profile.py RUN_A --against RUN_B: where a bubble's time goes)
    'base_s': ['native_gate_sample_profile=2'],
    'mask2_s': ['native_gate_view_points_mask=1', 'native_gate_sample_profile=2'],
}


def sets_for(variant, minutes):
    end = int(minutes * 60 * 30)
    plans = rc.lua([plan(1, end), plan(2, end)])
    return list(rc.PERF_SETS) + ['native_gate_ui_replay_script=' + plans,
                                 'native_gate_world_hash_stride=%d' % FINE.get(variant, STRIDE), 'native_gate_world_hash_spread=false',
                                 'native_gate_world_record=16', 'native_gate_digest_parts={1,400000}'] + VARIANTS[variant]


def memory_sampler(out_path):
    """An until() for rc.play: every call samples the run's X4 memory (pids from the host log); the user's game comes first:
    USER-PLAYING.txt ends the run."""
    import psutil
    import re

    def until(_logs, host_log):
        if (rc.LOCKS / 'USER-PLAYING.txt').exists():
            return 'the user is playing (USER-PLAYING.txt)'
        if (rc.LOCKS / 'SHOWCASE-RECORDING.txt').exists():
            return 'the showcase video is recording (SHOWCASE-RECORDING.txt)'
        try:
            text = Path(host_log).read_text(encoding='utf-8', errors='replace')
        except OSError:
            return None
        row = {'t': round(time.time(), 1)}
        for slot, pid in re.findall(r'client (\d+): pid (\d+)', text):
            try:
                m = psutil.Process(int(pid)).memory_info()
                row['c%s' % slot] = [round(m.rss / 2 ** 20), round(getattr(m, 'private', m.vms) / 2 ** 20)]
            except (psutil.Error, ValueError):
                pass
        with open(out_path, 'a', encoding='utf-8') as f:
            f.write(json.dumps(row) + '\n')
        return None
    return until


def others_first(log, quiet=300, poll=15):
    """Before taking a pair: the showcase video goes first on this PC (coordinator, 26 Nov). Wait until no
    SHOWCASE-RECORDING.txt, no showcase lock and no showcase line in the queue have been seen for `quiet` seconds."""
    since, said = None, False
    while True:
        busy = (rc.LOCKS / 'SHOWCASE-RECORDING.txt').exists() or (rc.LOCKS / 'USER-PLAYING.txt').exists()
        for lock in ('x4-a', 'x4-b'):
            try:
                busy = busy or 'showcase' in (rc.LOCKS / lock / 'owner.txt').read_text(encoding='utf-8', errors='replace').lower()
            except OSError:
                pass
        try:
            busy = busy or 'showcase' in (rc.LOCKS / 'want-both.txt').read_text(encoding='utf-8', errors='replace').lower()
        except OSError:
            pass
        if busy:
            since = None
            if not said:
                log('others first: the showcase (or the user) has the PC; waiting')
                said = True
        elif since is None:
            since = time.time()
        elif time.time() - since >= quiet:
            return
        time.sleep(poll)


def cmd_run(args):
    runner = rc.Runner(SimpleNamespace(lanes=args.lanes, display=args.display, size=args.size, lock_slots=args.lock_slots,
                                       lock_name=args.lock_name, lock_what=args.lock_what, beside_user=False, locks=args.locks,
                                       first_profile=args.first_profile, min_free_gb=8, skip_queue=False))
    dll = Path(args.gate_dll)
    sha = rc.file_sha(dll)[:8]
    done = []
    for r in range(args.repeat):
        for variant in args.variants.split(','):
            session = WORK / 'sessions' / ('%s-%s-%dm' % (variant, sha, args.minutes))
            overlay, snapshot = rc.continue_session(ROOT, dll, SAVE_DIR / 'c3-t18000.xml.gz', session, args.minutes,
                                                    sets_for(variant, args.minutes), session=SAVE_DIR / 'session.json',
                                                    recipe=SAVE_DIR / 'overlay')
            name = 'attn-%s-%s-r%d' % (time.strftime('%Y%m%d-%H%M%S'), variant, r + 1)
            others_first(runner.log)
            mem = WORK / 'mem' / (name + '.jsonl')
            mem.parent.mkdir(parents=True, exist_ok=True)
            run = runner.play(ROOT, overlay, snapshot, name, clients=2, timeout=int(args.minutes * 60 * 3 + 900),
                              until=memory_sampler(mem))
            (Path(run) / 'attn-split.json').write_text(json.dumps({'variant': variant, 'dll': str(dll), 'sha': sha,
                                                                    'memory': str(mem), 'name': name}), encoding='utf-8')
            runner.log('%s: %s' % (name, run))
            extract(run)
            if not args.keep_logs:
                prune(run, runner.log)
            done.append(str(run))
    print('\n'.join(done))
    return 0


# --------------------------------------------------------------------------------------------------------------------
# After a run: the rows the report reads are copied out of each client's log (clientK/attn-rows.txt, a few MB), then the
# raw logs and saves go (a run is 2-4 GB; this PC's drive runs full).
KEEP_KINDS = {'FRAME_TIMING', 'DIGEST_PARTS', 'HYBRID', 'HYBRID_ARMED', 'HYBRID_CENSUS', 'HYBRID_STOP', 'VIEW_POINTS', 'WORLD_HASH',
              'WORLD_RECORD', 'UI_REPLAY_SCRIPT', 'RUNTIME_IDS', 'PROBE_PROFILE', 'REGION_CAMERA', 'VIEW_SCENES', 'PHYSICS_SCENES',
              'STOP', 'GATE_STOP', 'MT_GOTO', 'DEFAULT_SKIPPED', 'SAMPLE_PROFILE', 'SAMPLE_PROFILE_ROW',
              'SAMPLE_PROFILE_STOP'}


def extract(run):
    for k, log in rc.client_logs(run).items():
        out = Path(log).parent / 'attn-rows.txt'
        with open(out, 'w', encoding='latin-1') as f:
            for line in rc.read_lines(log):
                if '[X4LAN]' not in line:
                    continue
                r = rc.ROW.search(line)
                # every status row (the per-module counters, ~30 MB a client for 6 minutes) and the rows named above
                if (r and (r.group(1) in KEEP_KINDS or r.group(1).endswith('_STOP') or r.group(2).startswith('tick='))) or \
                        'MT_GOTO' in line or ' ERROR ' in line:
                    f.write(line + chr(10))


def prune(run, log=print, limit=8 * 2 ** 20):
    freed = 0
    for path in Path(run).rglob('*'):
        if path.is_file() and path.stat().st_size > limit and path.name != 'attn-rows.txt' and not path.name.startswith('x4lan-census-'):
            freed += path.stat().st_size
            path.unlink()
    log('%s: %.0f MB of raw logs and saves removed (attn-rows.txt and the census records kept)' % (run, freed / 2 ** 20))


# --------------------------------------------------------------------------------------------------------------------
# Reading a run.
def tick_rows(log, kinds):
    for _, kind, text in rc.rows(log, kinds):
        if text.startswith('tick='):
            f = rc.fields(text)
            t = rc.number(f.get('tick'))
            if t is not None:
                yield int(t), kind, f, text


def sim_profile(log, skip=600, split=SPLIT_TICK, stride=STRIDE):
    """Median and mean sim_us (+ pre_us) per tick before and after the split; capture ticks (and the two after) left out."""
    before, after = [], []
    for t, _, f, _ in tick_rows(log, {'FRAME_TIMING'}):
        s, p = rc.number(f.get('sim_us')), rc.number(f.get('pre_us')) or 0
        if not s or t < skip or t % stride in (0, 1, 2):
            continue
        (after if t >= split + 300 else before).append(s + p)
    def summary(v):
        return {'n': len(v), 'median_us': statistics.median(v) if v else None, 'mean_us': statistics.fmean(v) if v else None,
                'p90_us': sorted(v)[int(0.9 * (len(v) - 1))] if v else None}
    return {'before': summary(before), 'after': summary(after)}


def digest_split(logs):
    """{part: (first tick it differs, ticks it differs, ticks compared)} between clients 1 and 2."""
    parts = []
    for k in (1, 2):
        found = {}
        for _, kind, text in rc.rows(logs[k], {'DIGEST_PARTS'}):
            f = dict(rc.PART.findall(text))
            if f.get('tick'):
                found[int(f['tick'])] = f
        parts.append(found)
    both = sorted(set(parts[0]) & set(parts[1]))
    out = {}
    for t in both:
        a, b = parts[0][t], parts[1][t]
        for key in set(a) | set(b):
            if key == 'tick':
                continue
            entry = out.setdefault(key, [None, 0, 0])
            entry[2] += 1
            if a.get(key) != b.get(key):
                entry[1] += 1
                if entry[0] is None:
                    entry[0] = t
    return out, (both[0], both[-1]) if both else None


TIMING_FIELD = __import__('re').compile(r'(_us|_ms|_ns|_x100|^us|^ms|wait|pace|frame|outside|scan|qpc|time|age|period|fps)')


def status_diff(logs, skip=0):
    """{kind.field: (first tick the two clients' values differ, ticks differing, ticks compared)} over every status row
    both clients logged at the same tick (timing fields left out: they differ on every client by nature)."""
    seen = []
    for k in (1, 2):
        rows = {}
        for t, kind, f, _ in tick_rows(logs[k], None):
            if t < skip or kind in ('FRAME_TIMING', 'DIGEST_PARTS', 'DIGEST_PILOT'):
                continue
            rows[(kind, t)] = f
        seen.append(rows)
    out = {}
    for key in set(seen[0]) & set(seen[1]):
        kind, t = key
        a, b = seen[0][key], seen[1][key]
        for name in set(a) | set(b):
            if name == 'tick' or TIMING_FIELD.search(name):
                continue
            entry = out.setdefault('%s.%s' % (kind, name), [None, 0, 0])
            entry[2] += 1
            if a.get(name) != b.get(name):
                entry[1] += 1
                if entry[0] is None or t < entry[0]:
                    entry[0] = t
    return {k: v for k, v in out.items() if v[1]}


def status_last(log, kind):
    last = None
    for _, k, f, text in tick_rows(log, {kind}):
        last = text
    return last


def status_series(log, kind, keys, every=900):
    out = []
    for t, _, f, _ in tick_rows(log, {kind}):
        if t % every == 0 or not out:
            out.append([t] + [f.get(k) for k in keys])
    return out


def memory(path, skip_s=150):
    rows = []
    try:
        rows = [json.loads(l) for l in Path(path).read_text(encoding='utf-8').splitlines() if l.strip()]
    except (OSError, ValueError):
        return None
    if not rows:
        return None
    t0 = rows[0]['t']
    out = {}
    for c in ('c1', 'c2'):
        vals = [r[c] for r in rows if c in r and r['t'] - t0 >= skip_s]
        if vals:
            out[c] = {'rss_mb_max': max(v[0] for v in vals), 'private_mb_max': max(v[1] for v in vals),
                      'rss_mb_median': statistics.median(v[0] for v in vals), 'samples': len(vals)}
    return out


def cmd_report(args):
    out = {}
    for run in args.runs:
        run = Path(run)
        meta = json.loads((run / 'attn-split.json').read_text(encoding='utf-8')) if (run / 'attn-split.json').exists() else {}
        logs = rc.client_logs(run)
        for path in Path(run).glob('client*/attn-rows.txt'):
            logs[int(path.parent.name[6:])] = path
        rep = {'variant': meta.get('variant'), 'run': str(run)}
        for k, log in sorted(logs.items()):
            rep['c%d' % k] = {'sim': sim_profile(log, stride=FINE.get(meta.get('variant'), STRIDE)), 'hybrid': status_last(log, 'HYBRID'),
                              'hybrid_series': status_series(log, 'HYBRID', ['proxies', 'err_mean_m', 'err_max_m', 'members', 'movers',
                                                                             'eval_inside', 'eval_outside', 'parent_mismatch', 'unknown'])}
        if 1 in logs and 2 in logs:
            parts, span = digest_split(logs)
            rep['digest'] = {'span': span, 'parts': parts}
            rep['census'] = rc.census_diff(run, limit=40)
            rep['status_diff'] = dict(sorted(status_diff(logs).items(), key=lambda e: (e[1][0], e[0])))
        rep['memory'] = memory(meta.get('memory')) if meta.get('memory') else None
        rep['tps'] = rc.relay_tps(run, SPLIT_TICK + 300, None)
        rep['cpu_us_per_tick'] = rc.cpu_per_tick(run, SPLIT_TICK + 300, None)
        rep['sync'] = rc.sync_state(run)
        out[str(run)] = rep
        print(json.dumps(rep, indent=1, default=str)[:20000])
    if args.json:
        Path(args.json).write_text(json.dumps(out, indent=1, default=str), encoding='utf-8')
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    r = sub.add_parser('run')
    r.add_argument('--gate-dll', required=True)
    r.add_argument('--variants', default='base,hyb12')
    r.add_argument('--minutes', type=float, default=6)
    r.add_argument('--repeat', type=int, default=1)
    r.add_argument('--lanes', default='C:/x4ls-lanes-attnsplit/run')
    r.add_argument('--display', type=int, default=3)
    r.add_argument('--size', default='960x540')
    r.add_argument('--locks', action='store_true')
    r.add_argument('--lock-name', default='X4-Multiplayer-attnsplit')
    r.add_argument('--lock-what', default='attention split measurement: two clients, lanes C:/x4ls-lanes-attnsplit/run')
    r.add_argument('--lock-slots', default='a,b')
    r.add_argument('--first-profile', type=int)
    r.add_argument('--keep-logs', action='store_true')
    q = sub.add_parser('report')
    q.add_argument('runs', nargs='+')
    q.add_argument('--json')
    args = p.parse_args(argv)
    return cmd_run(args) if args.command == 'run' else cmd_report(args)


if __name__ == '__main__':
    sys.exit(main())
