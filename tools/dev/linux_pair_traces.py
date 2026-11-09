"""Pair Windows and Linux functions by two coverage traces of the same input events (the people stack's input path).

Each side: the first-run tick of every listed function (COVERAGE_ROW index=N address=HEX tick=T in a client log, or
x4mods-e7's result file: VA, tick client 1, tick client 2 or -, both|input:<event>). The events are the same script
ticks on both sides (Linux: xdotool keys, report 3f; Windows: the stock script, tools/dev/linux_people/stock-events.lua,
and native_gate_map_test). A function belongs to the event whose window [tick - 2, tick + window) holds its first
run; first runs before the first event are frame work. Within each event, Windows and Linux functions pair by shared
struct field offsets and size (linux_pair_candidates.offsets), only with a clear lead (best >= min_share, ahead of the
runner-up by margin on both sides, sizes within a factor of `ratio`), and never against an existing pair.

  python tools/dev/linux_pair_traces.py --win-log client1.log --lin-result coverage-lx32b-result.txt \\
      --win X4.exe --lin X4 --cache DIR --pairs pairs-calls.json --out pairs-traces.json"""
import argparse
import json
import os
import pickle
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from linux_pair_candidates import offsets  # noqa: E402

EVENTS = {360: 'accelerate', 420: 'decelerate', 480: 'roll left', 540: 'roll right', 600: 'strafe left',
          660: 'strafe right', 720: 'strafe up', 780: 'strafe down', 840: 'boost', 980: 'stop', 1040: 'map open',
          1100: 'map close', 1160: 'travel'}
ROW = re.compile(r'COVERAGE_ROW\s+index=\d+\s+address=([0-9a-f]+)\s+tick=(\d+)')


def win_first(path):
    out = {}
    for line in open(path, errors='replace'):
        m = ROW.search(line)
        if m:
            out.setdefault(int(m.group(1), 16), int(m.group(2)))
    return out


def lin_first(path):
    out = {}
    for line in open(path, errors='replace'):
        if line.startswith('#') or not line.strip():
            continue
        va, t1 = line.split()[:2]
        if t1.isdigit():
            out[int(va, 16)] = int(t1)
    return out


def bucket(first, window=40):
    ticks = sorted(EVENTS)
    out = {t: [] for t in ticks}
    out['frame'] = []
    for va, tick in first.items():
        owner = None
        for t in ticks:
            if t - 2 <= tick < t + window:
                owner = t
        if owner is not None:
            out[owner].append(va)
        elif tick < ticks[0] - 2:
            out['frame'].append(va)
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--win-log', required=True)
    p.add_argument('--lin-result', required=True)
    p.add_argument('--win', required=True)
    p.add_argument('--lin', required=True)
    p.add_argument('--cache', required=True)
    p.add_argument('--pairs', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--min-share', type=float, default=0.3)
    p.add_argument('--margin', type=float, default=0.1)
    p.add_argument('--ratio', type=float, default=2.0)
    p.add_argument('--frame', action='store_true', help='also pair within the frame work (first runs before the first event), '
                   'with --frame-share and --frame-margin')
    p.add_argument('--frame-share', type=float, default=0.5)
    p.add_argument('--frame-margin', type=float, default=0.2)
    args = p.parse_args(argv)
    import linux_pair_calls
    wb, lb = bucket(win_first(args.win_log)), bucket(lin_first(args.lin_result))
    for t in sorted(EVENTS):
        print(f'{t:5d} {EVENTS[t]:12s} windows {len(wb[t]):4d}  linux {len(lb[t]):4d}')
    wimg = linux_pair_calls.win_functions(args.win)[0]
    limg = linux_pair_calls.lin_functions(args.lin)[0]
    wg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-win.pickle'), 'rb'))
    lg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-lin.pickle'), 'rb'))
    wst, lst = sorted(wg), sorted(lg)
    wsize = dict(zip(wst, [b - a for a, b in zip(wst, wst[1:] + [wst[-1] + 1])]))
    lsize = dict(zip(lst, [b - a for a, b in zip(lst, lst[1:] + [lst[-1] + 1])]))
    pairs = {int(k, 16): int(v['lin'], 16) for k, v in json.load(open(args.pairs)).items()}
    rev = {v: k for k, v in pairs.items()}
    found = {}
    buckets = sorted(EVENTS) + (['frame'] if args.frame else [])
    for t in buckets:
        min_share, margin = (args.frame_share, args.frame_margin) if t == 'frame' else (args.min_share, args.margin)
        ws = [w for w in wb[t] if w not in pairs and w in wsize]
        ls = [l for l in lb[t] if l not in rev and l in lsize]
        wf = {w: offsets(wimg, w, w + min(wsize[w], 1 << 16)) for w in ws}
        lf = {l: offsets(limg, l, l + min(lsize[l], 1 << 16)) for l in ls}
        score = {}
        for w in ws:
            for l in ls:
                r = lsize[l] / max(wsize[w], 1)
                union = wf[w] | lf[l]
                if len(union) < 3 or not 1 / args.ratio <= r <= args.ratio:
                    continue
                score[(w, l)] = len(wf[w] & lf[l]) / len(union)
        for (w, l), s in sorted(score.items(), key=lambda kv: -kv[1]):
            if s < min_share or w in found or l in found.values():
                continue
            rival_w = max((v for (a, b), v in score.items() if a == w and b != l), default=0)
            rival_l = max((v for (a, b), v in score.items() if b == l and a != w), default=0)
            if s >= rival_w + margin and s >= rival_l + margin:
                found[w] = l
                print(f'  {EVENTS.get(t, t):12s} {w:x} -> {l:x}  fields {s:.2f} (rivals {rival_w:.2f}/{rival_l:.2f})')
    json.dump({'%x' % w: {'lin': '%x' % l, 'why': 'traces'} for w, l in found.items()}, open(args.out, 'w'), indent=0)
    print(f'{len(found)} pairs -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
