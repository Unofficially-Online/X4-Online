"""Place Windows functions by the data they reference (report 3i, generalized from the input bank to every block).

A block is a run of Windows globals laid out alike on Linux (linux_pair_data.place_blocks: one address delta for the
run; the input bank 2f4b720..828 at +0xE42280 is one of them). A Windows function's references into blocks map to
Linux addresses; every Linux function that references the same blocks is scored by the Jaccard index of its references
into those blocks' Linux ranges against the mapped set. A function is placed with a clear lead: at least `min_shared`
mapped fields in common, Jaccard >= min_j, ahead of the runner-up by `margin`, sizes within `ratio`, the Linux
function not paired already. Call-graph votes (paired callers and callees that agree) are reported, not required.

  python tools/dev/linux_people/block_key.py [--holdout] [--all] [WINDOWS_FUNCTION ...]
--holdout scores the known pairs as if open (precision of the key); without functions, the open sites of
tools/dev/linux_people_sites.json (fn_open, and the functions holding in_open_fn points) are scored.
Environment: LINUX_PAIR_WORK (paircache/calls2-*.pickle, pairs-calls.json)."""
import argparse
import bisect
import json
import os
import pickle
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from linux_pair_data import place_blocks, LIN_RELRO_END  # noqa: E402

SITES = os.path.join(HERE, '..', 'linux_people_sites.json')
BANK = (0x2f4b720, 0x2f4b828, 0xE42280)   # report 3d: the input bank, placed by a reading


def sizes(graph):
    st = sorted(graph)
    return st, dict(zip(st, [b - a for a, b in zip(st, st[1:] + [st[-1] + 1])]))


def containing(starts, va):
    k = bisect.bisect_right(starts, va) - 1
    return starts[k] if k >= 0 else None


class Key:
    def __init__(self, wg, lg, pairs, blocks):
        self.wg, self.lg, self.pairs = wg, lg, pairs
        self.rev = {v: k for k, v in pairs.items()}
        self.blocks = sorted(blocks)
        self.bstart = [b[0] for b in self.blocks]
        self.lranges = sorted((b[0] + b[2], b[1] + b[2], i) for i, b in enumerate(self.blocks))
        self.lstart = [r[0] for r in self.lranges]
        self.wst, self.wsize = sizes(wg)
        self.lst, self.lsize = sizes(lg)
        self.users = {}   # Linux global inside a block -> Linux functions referencing it
        for f, (_, _, d) in lg.items():
            for g, _ in d:
                if g >= LIN_RELRO_END and self.lblock(g) is not None:
                    self.users.setdefault(g, set()).add(f)
        self.callers = {}
        for f, (c, _, _) in lg.items():
            for t in c:
                self.callers.setdefault(t, set()).add(f)
        self.wcallers = {}
        for f, (c, _, _) in wg.items():
            for t in c:
                self.wcallers.setdefault(t, set()).add(f)

    def wblock(self, g):
        k = bisect.bisect_right(self.bstart, g) - 1
        return k if k >= 0 and self.blocks[k][0] <= g <= self.blocks[k][1] else None

    def lblock(self, g):
        k = bisect.bisect_right(self.lstart, g) - 1
        return self.lranges[k][2] if k >= 0 and self.lranges[k][0] <= g <= self.lranges[k][1] else None

    def mapped(self, w):
        out, used = set(), set()
        for g, _ in self.wg[w][2]:
            b = self.wblock(g)
            if b is not None:
                out.add(g + self.blocks[b][2])
                used.add(b)
        return out, used

    def lin_refs(self, f, used):
        return {g for g, _ in self.lg[f][2] if self.lblock(g) in used}

    def votes(self, w, l):
        """Paired neighbours that agree: callees of w paired to callees of l, callers likewise."""
        wc, lc = set(self.wg[w][0]), set(self.lg[l][0])
        n = sum(1 for t in wc if self.pairs.get(t) in lc)
        n += sum(1 for t in self.wcallers.get(w, ()) if self.pairs.get(t) in self.callers.get(l, ()))
        return n

    def score(self, w, exclude_paired=True, ignore=None):
        m, used = self.mapped(w)
        if not m:
            return m, []
        cands = set()
        for g in m:
            cands |= self.users.get(g, set())
        rows = []
        for l in cands:
            if exclude_paired and l in self.rev and l != ignore:
                continue
            r = self.lin_refs(l, used)
            shared = len(m & r)
            union = len(m | r)
            ratio = self.lsize[l] / max(self.wsize.get(w, 1), 1)
            rows.append((shared / union if union else 0.0, shared, ratio, l))
        rows.sort(key=lambda r: (-r[0], -r[1], abs(1 - r[2])))
        return m, rows

    def place(self, w, min_shared=2, min_j=0.6, margin=0.15, ratio=2.5, exclude_paired=True, ignore=None):
        m, rows = self.score(w, exclude_paired, ignore)
        if not rows:
            return None, m, rows
        j, shared, r, l = rows[0]
        runner = rows[1][0] if len(rows) > 1 else 0.0
        ok = shared >= min_shared and j >= min_j and j >= runner + margin and 1 / ratio <= r <= ratio
        return (l if ok else None), m, rows


def load(work):
    wg = pickle.load(open(os.path.join(work, 'paircache', 'calls2-win.pickle'), 'rb'))[0]
    lg = pickle.load(open(os.path.join(work, 'paircache', 'calls2-lin.pickle'), 'rb'))[0]
    pairs = {int(k, 16): int(v['lin'], 16) for k, v in json.load(open(os.path.join(work, 'pairs-calls.json'))).items()}
    for s in json.load(open(SITES))['sites']:
        if s.get('status') == 'matched' and s.get('kind') == 'fn_paired' and s.get('lin'):
            pairs.setdefault(int(s['win'], 16), int(s['lin'], 16))
    return wg, lg, pairs


def blocks_with_bank(wg, lg, pairs):
    blocks = place_blocks(wg, lg, pairs)
    if all(b[1] < BANK[0] or b[0] > BANK[1] for b in blocks):
        blocks.append((BANK[0], BANK[1], BANK[2], 0))
    return sorted(blocks)


def site_functions(wst):
    out = []
    for s in json.load(open(SITES))['sites']:
        if s.get('status') != 'open' or s.get('kind') not in ('fn_open', 'in_open_fn'):
            continue
        va = int(s['win'], 16)
        f = va if s['kind'] == 'fn_open' else containing(wst, va)
        if f is not None and f not in out:
            out.append(f)
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('functions', nargs='*')
    p.add_argument('--holdout', action='store_true')
    p.add_argument('--all', action='store_true', help='show the short list of every scored function, placed or not')
    p.add_argument('--min-j', type=float, default=0.6)
    p.add_argument('--margin', type=float, default=0.15)
    p.add_argument('--min-shared', type=int, default=2)
    p.add_argument('--out')
    args = p.parse_args(argv)
    work = os.environ.get('LINUX_PAIR_WORK', '.local/linux-pairs')
    wg, lg, pairs = load(work)
    blocks = blocks_with_bank(wg, lg, pairs)
    key = Key(wg, lg, pairs, blocks)
    print(f'{len(blocks)} blocks, {sum(b[1] - b[0] for b in blocks):#x} bytes of Windows data; {len(key.users)} Linux globals in them')
    kw = dict(min_shared=args.min_shared, min_j=args.min_j, margin=args.margin)
    if args.holdout:
        right = wrong = none = 0
        wrongs = []
        for w, l in sorted(pairs.items()):
            if w not in wg or l not in lg:
                continue
            got, m, rows = key.place(w, exclude_paired=True, ignore=l, **kw)
            if len(m) < args.min_shared:
                continue
            if got is None:
                none += 1
            elif got == l:
                right += 1
            else:
                wrong += 1
                wrongs.append((w, l, got, rows[0]))
        print(f'held out: {right} right, {wrong} wrong, {none} no clear lead')
        for w, l, got, r in wrongs[:20]:
            print(f'  wrong {w:x}: placed {got:x} (J {r[0]:.2f}, {r[1]} shared), pair is {l:x}')
        return 0
    targets = [int(x, 16) for x in args.functions] or site_functions(key.wst)
    found = {}
    for w in targets:
        if w not in wg:
            print(f'{w:x}: not a function start')
            continue
        got, m, rows = key.place(w, **kw)
        if got is not None:
            found[w] = got
            j, shared, r, l = rows[0]
            print(f'{w:x} -> {l:x}  J {j:.2f} ({shared}/{len(m)} fields), size {r:.2f}, votes {key.votes(w, l)}'
                  + (f'; next J {rows[1][0]:.2f}' if len(rows) > 1 else ''))
        elif args.all and rows:
            top = ', '.join(f'{l:x} J{j:.2f}/{s} x{r:.1f}' for j, s, r, l in rows[:4])
            print(f'{w:x}: open ({len(m)} mapped) {top}')
        elif args.all:
            print(f'{w:x}: no block references')
    print(f'{len(found)} placed of {len(targets)}')
    if args.out:
        json.dump({'%x' % w: {'lin': '%x' % l, 'why': 'block key'} for w, l in found.items()}, open(args.out, 'w'), indent=0)
    return 0


if __name__ == '__main__':
    sys.exit(main())
