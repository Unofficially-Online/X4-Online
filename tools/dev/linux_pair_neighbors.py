"""Neighbour extension of Windows <-> Linux function pairs (after linux_pair_calls.py).

Within a source file both compilers keep functions in order; GCC moves whole runs elsewhere, but a run stays together
(the input module d9c920/d9cc30/d9d550 -> 165d700/165d860/165dfa0). So for a pair (w, l) the next (previous) function
on each side is a candidate pair, accepted when both are unpaired, their sizes are within a factor of `ratio` and they
share at least `share` of their struct field offsets ([reg + 0x..], 0x10 and more; at least `min_fields` in all).
Repeated until nothing changes.

  python tools/dev/linux_pair_neighbors.py --win X4.exe --lin X4 --cache DIR --pairs pairs-calls.json --out pairs-nb.json
Reads both binaries, the cached sweeps and the pairs file."""
import argparse
import json
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(__file__))
import linux_pair_calls  # noqa: E402
from linux_pair_candidates import offsets  # noqa: E402


def extend(wst, lst, wimg, limg, pairs, why, ratio=2.0, share=0.3, min_fields=4, rounds=20):
    rev = {v: k for k, v in pairs.items()}
    wi = {a: i for i, a in enumerate(wst)}
    li = {a: i for i, a in enumerate(lst)}
    wsize = {a: b - a for a, b in zip(wst, wst[1:] + [wst[-1] + 1])}
    lsize = {a: b - a for a, b in zip(lst, lst[1:] + [lst[-1] + 1])}
    cache = {}

    def fields(side, a):
        key = (side, a)
        if key not in cache:
            img, size = (wimg, wsize) if side == 'w' else (limg, lsize)
            cache[key] = offsets(img, a, a + min(size[a], 1 << 16))
        return cache[key]
    added = 0
    for _ in range(rounds):
        new = []
        for w, l in list(pairs.items()):
            if w not in wi or l not in li:
                continue
            for step in (1, -1):
                i, j = wi[w] + step, li[l] + step
                if not (0 <= i < len(wst) and 0 <= j < len(lst)):
                    continue
                a, b = wst[i], lst[j]
                if a in pairs or b in rev:
                    continue
                r = lsize[b] / max(wsize[a], 1)
                if not 1 / ratio <= r <= ratio:
                    continue
                fa, fb = fields('w', a), fields('l', b)
                union = fa | fb
                if len(union) < min_fields or len(fa & fb) < share * len(union):
                    continue
                new.append((a, b, w, len(fa & fb) / len(union)))
        if not new:
            break
        for a, b, w, s in new:
            if a not in pairs and b not in rev:
                pairs[a], rev[b] = b, a
                why[a] = f'neighbour of {w:x} (fields {s:.2f})'
                added += 1
    return added


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--win', required=True)
    p.add_argument('--lin', required=True)
    p.add_argument('--cache', required=True)
    p.add_argument('--pairs', required=True)
    p.add_argument('--out', required=True)
    args = p.parse_args(argv)
    wg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-win.pickle'), 'rb'))
    lg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-lin.pickle'), 'rb'))
    raw = json.load(open(args.pairs))
    pairs = {int(k, 16): int(v['lin'], 16) for k, v in raw.items()}
    why = {int(k, 16): v['why'] for k, v in raw.items()}
    wimg = linux_pair_calls.win_functions(args.win)[0]
    limg = linux_pair_calls.lin_functions(args.lin)[0]
    n = extend(sorted(wg), sorted(lg), wimg, limg, pairs, why)
    json.dump({'%x' % w: {'lin': '%x' % l, 'why': why[w]} for w, l in sorted(pairs.items())}, open(args.out, 'w'), indent=0)
    print(f'neighbours added {n}; {len(pairs)} pairs -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
