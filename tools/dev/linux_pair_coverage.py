"""Place Windows functions on Linux by a coverage trace of scripted input (the people stack's input path).

The Linux gate's one-shot coverage (native_gate_coverage, native_gate_coverage_from) notes the tick at which each listed
function first runs. With input events spaced apart (an input script: [{"tick", "keys"|"mouse", "what"}]) a function
whose first run falls in [event, event + window) belongs to that event; first runs before the first event are the
frame's own work. For each Windows function with an expected event (--expect RVA=EVENT_TICK), the Linux functions of
that event are ranked by size ratio and shared struct field offsets (linux_pair_candidates.offsets).

  python tools/dev/linux_pair_coverage.py --log client.log --script input.json --win X4.exe --lin X4 --cache DIR \\
      --expect d88a10=920 --expect d88710=1040 ...
The log is scanned for 'COVERAGE_ROW' lines; every 0x-less hex VA followed by a tick (fn=<hex> ... tick=<n>, or
<hex>:<n>) is read, so small format changes do not matter."""
import argparse
import json
import os
import pickle
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from linux_pair_candidates import offsets  # noqa: E402

PAIR = re.compile(r'(?:fn=)?([0-9a-f]{5,8})(?::|\s+tick=|\s*@)(\d+)')


def read_log(path):
    first = {}
    for line in open(path, errors='replace'):
        if 'COVERAGE_ROW' not in line:
            continue
        for va, tick in PAIR.findall(line.split('COVERAGE_ROW', 1)[1]):
            first.setdefault(int(va, 16), int(tick))
    return first


def by_event(first, events, window=40):
    ticks = sorted(e['tick'] for e in events)
    out = {t: [] for t in ticks}
    out['frame'] = []
    for va, tick in first.items():
        owner = None
        for t in ticks:
            if t - 2 <= tick < t + window:
                owner = t
        if owner is None:
            if tick < ticks[0] - 2:
                out['frame'].append(va)
        else:
            out[owner].append(va)
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--log', required=True)
    p.add_argument('--script', required=True)
    p.add_argument('--win', required=True)
    p.add_argument('--lin', required=True)
    p.add_argument('--cache', required=True)
    p.add_argument('--expect', action='append', default=[], metavar='RVA=TICK')
    p.add_argument('--window', type=int, default=40)
    args = p.parse_args(argv)
    events = json.load(open(args.script))
    first = read_log(args.log)
    groups = by_event(first, events, args.window)
    print(f'{len(first)} functions ran; frame work {len(groups["frame"])}')
    for e in events:
        print(f'  tick {e["tick"]:5d} {e.get("what", "")}: {len(groups[e["tick"]])} first runs')
    if not args.expect:
        return 0
    import linux_pair_calls
    wimg = linux_pair_calls.win_functions(args.win)[0]
    limg = linux_pair_calls.lin_functions(args.lin)[0]
    wg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-win.pickle'), 'rb'))
    lg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-lin.pickle'), 'rb'))
    wst, lst = sorted(wg), sorted(lg)
    wsize = dict(zip(wst, [b - a for a, b in zip(wst, wst[1:] + [wst[-1] + 1])]))
    lsize = dict(zip(lst, [b - a for a, b in zip(lst, lst[1:] + [lst[-1] + 1])]))
    for item in args.expect:
        rva, tick = item.split('=')
        w, tick = int(rva, 16), int(tick)
        wo = offsets(wimg, w, w + wsize.get(w, 256))
        ranked = []
        for l in groups.get(tick, []):
            lo = offsets(limg, l, l + lsize.get(l, 256))
            union = wo | lo
            share = len(wo & lo) / len(union) if union else 0
            ratio = lsize.get(l, 0) / max(wsize.get(w, 1), 1)
            ranked.append((share - abs(1 - min(ratio, 1 / ratio if ratio else 0)) * 0.2, share, ratio, l))
        ranked.sort(reverse=True)
        print(f'{w:x} (event {tick}, size {wsize.get(w)}):')
        for score, share, ratio, l in ranked[:5]:
            print(f'    {l:x}  fields {share:.2f}  size ratio {ratio:.2f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
