"""Candidates for one unpaired Windows function, from its paired callers and callees (linux_pair_calls.py's graphs).

For each paired caller P -> P' of W, the Linux callees of P' that are not paired yet are candidates; the count is how
many of W's paired callers agree. The same for W's paired callees (Linux functions that call their pairs). Sizes are
printed for a check by eye: MSVC and GCC bodies of one function are usually within a factor of two.

  python tools/dev/linux_pair_candidates.py --cache DIR --pairs pairs-calls.json d8e5b0 ea7600 ...
Reads the cached sweeps and the pairs file only. Candidates are leads for a reading of the code, not pairs."""
import argparse
import json
import os
import pickle
import re
import sys

import capstone

DISP = re.compile(r'\[(?!rip)[a-z0-9]+(?: [+-] [a-z0-9]+\*\d)? \+ (0x[0-9a-f]+)\]')


def offsets(image, lo, hi):
    """The set of struct field displacements a function's memory operands use ([reg + 0x..], 0x10 and more)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.skipdata = True
    out = set()
    for _, _, _, op in md.disasm_lite(image[lo:hi], lo):
        for m in DISP.finditer(op):
            v = int(m.group(1), 16)
            if v >= 0x10:
                out.add(v)
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--cache', required=True)
    p.add_argument('--pairs', required=True)
    p.add_argument('--win')
    p.add_argument('--lin')
    p.add_argument('rvas', nargs='+')
    args = p.parse_args(argv)
    wimg = limg = None
    if args.win and args.lin:   # field offset similarity of each candidate (needs the images)
        sys.path.insert(0, os.path.dirname(__file__))
        import linux_pair_calls
        wimg = linux_pair_calls.win_functions(args.win)[0]
        limg = linux_pair_calls.lin_functions(args.lin)[0]
    wg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-win.pickle'), 'rb'))
    lg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-lin.pickle'), 'rb'))
    pairs = {int(k, 16): int(v['lin'], 16) for k, v in json.load(open(args.pairs)).items()}
    rev = {v: k for k, v in pairs.items()}
    wstarts, lstarts = sorted(wg), sorted(lg)
    wsize = {a: b - a for a, b in zip(wstarts, wstarts[1:] + [wstarts[-1] + 1])}
    lsize = {a: b - a for a, b in zip(lstarts, lstarts[1:] + [lstarts[-1] + 1])}
    wcallers, lcallers = {}, {}
    for f, (calls, _, _) in wg.items():
        for t in set(calls):
            wcallers.setdefault(t, set()).add(f)
    for f, (calls, _, _) in lg.items():
        for t in set(calls):
            lcallers.setdefault(t, set()).add(f)
    for text in args.rvas:
        w = int(text, 16)
        if w not in wg:
            print(f'{w:x}: not a function start')
            continue
        if w in pairs:
            print(f'{w:x}: paired -> {pairs[w]:x}')
            continue
        votes = {}
        callers = [c for c in wcallers.get(w, ()) if c in pairs]
        for c in callers:
            for t in set(lg[pairs[c]][0]):
                if t not in rev:
                    votes[t] = votes.get(t, 0) + 1
        callees = [t for t in set(wg[w][0]) if t in pairs]
        for t in callees:
            for c in lcallers.get(pairs[t], ()):
                if c not in rev:
                    votes[c] = votes.get(c, 0) + 1
        ranked = sorted(votes.items(), key=lambda kv: -kv[1])[:5]
        print(f'{w:x} (size {wsize[w]}, {len(wcallers.get(w, ()))} callers of which {len(callers)} paired, '
              f'{len(set(wg[w][0]))} callees of which {len(callees)} paired):')
        wo = offsets(wimg, w, w + wsize[w]) if wimg else None
        for l, n in ranked:
            extra = ''
            if wo is not None:
                lo_ = offsets(limg, l, l + lsize[l])
                union = wo | lo_
                extra = f'  fields {len(wo & lo_)}/{len(union)} shared' if union else ''
            print(f'    {l:x}  votes {n}  size {lsize.get(l)}{extra}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
