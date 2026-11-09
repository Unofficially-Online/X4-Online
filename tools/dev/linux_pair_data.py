"""Linux counterparts of Windows X4.exe globals and vtables (the people-stack port's data sites).

Globals: every paired function that references the Windows global (linux_pair_calls.py's sweep: rip-relative
references into writable sections) votes for each global its Linux pair references; a global is placed when one Linux
global has the votes of every referencing pair (at least --min-votes) and no other global has as many.
Vtables: by the RTTI pairs (linux_pair_rtti.py output); an address inside a vtable maps to the same slot (+1 after the
destructor, GCC's two).

  python tools/dev/linux_pair_data.py --cache DIR --pairs pairs-calls.json --rtti pairs-rtti.json 2f4b7e4 2c39488 ...
Reads the cached sweeps and the pairs files only. A global's offset inside a structure (a field read as rip+X+8) is a
global of its own here; nearby Windows globals that land next to each other on Linux confirm one another."""
import argparse
import json
import os
import pickle
import sys


def first_use(refs):
    """The distinct globals of a function in the order of their first reference."""
    seen, out = set(), []
    for g, _ in refs:
        if g not in seen:
            seen.add(g)
            out.append(g)
    return out


LIN_RELRO_END = 0x3d8ab80   # .data.rel.ro of the pinned Linux build (vtables, constant pointers): no mutable globals


def place_globals(wg, lg, pairs, targets, min_votes=2, share=0.8, lin_floor=LIN_RELRO_END):
    """Votes from aligned functions: in a paired function whose distinct globals (in order of first use) are as many on
    both sides, the n-th pair up. A target is placed on the Linux global with at least min_votes, share of the target's
    aligned votes and twice the runner-up; greedily by strength, each Linux global once (MSVC addresses a structure's
    fields directly where GCC loads its base once: such fields find no aligned function and stay open)."""
    want = set(targets)
    votes, users = {t: {} for t in targets}, {t: 0 for t in targets}
    for f, (_, _, d) in wg.items():
        if f not in pairs:
            continue
        gw = first_use(d)
        hit = want.intersection(gw)
        if not hit:
            continue
        for t in hit:
            users[t] += 1
        gl = first_use(lg[pairs[f]][2])
        if len(gw) != len(gl):
            continue
        for a, b in zip(gw, gl):
            if a in want and b >= lin_floor:
                votes[a][b] = votes[a].get(b, 0) + 1
    cands = []
    for t, vs in votes.items():
        ranked = sorted(vs.items(), key=lambda kv: -kv[1])
        if not ranked:
            continue
        (g, n), second = ranked[0], (ranked[1][1] if len(ranked) > 1 else 0)
        total = sum(vs.values())
        if n >= min_votes and n >= share * total and n >= 2 * second:
            cands.append((n / total, n, t, g))
    out, taken = {}, set()
    for _, n, t, g in sorted(cands, reverse=True):
        if g not in taken:
            taken.add(g)
            out[t] = g
    return {t: {'users': users[t], 'placed': out.get(t), 'top': sorted(votes[t].items(), key=lambda kv: -kv[1])[:4]}
            for t in targets}


def place_blocks(wg, lg, pairs, lin_floor=LIN_RELRO_END, gap=0x100, min_block=3, share=0.6):
    """Blocks of globals laid out alike in both builds (a structure, an array, a translation unit's statics): each
    Windows global referenced by paired functions gets the address delta most of its users' Linux pairs support (every
    Linux global of the paired function is a candidate: delta = linux - windows); neighbouring globals (gap at most
    `gap`) with the same best delta form a block, kept with at least min_block members.
    Returns [(windows start, windows end, delta, members)]."""
    votes, users = {}, {}
    for f, (_, _, d) in wg.items():
        if f not in pairs:
            continue
        gw = {g for g, _ in d}
        gl = {g for g, _ in lg[pairs[f]][2] if g >= lin_floor}
        if not gw or not gl or len(gw) > 300 or len(gl) > 300:
            continue
        for a in gw:
            users[a] = users.get(a, 0) + 1
            va = votes.setdefault(a, {})
            for b in gl:
                va[b - a] = va.get(b - a, 0) + 1
    # A global supports every delta with at least 2 votes and `share` of its users (ties are common: two paired
    # functions share several globals); a block is a run of neighbouring globals that support one delta.
    support = {}
    for a, va in votes.items():
        for dlt, n in va.items():
            if n >= 2 and n >= share * users[a]:
                support.setdefault(dlt, []).append(a)
    found = []
    for dlt, members in support.items():
        if len(members) < min_block:
            continue
        run = []
        for a in sorted(members):
            if run and a - run[-1] > gap:
                if len(run) >= min_block:
                    found.append((run[0], run[-1], dlt, len(run)))
                run = []
            run.append(a)
        if len(run) >= min_block:
            found.append((run[0], run[-1], dlt, len(run)))
    # overlapping blocks: the one with more members wins
    blocks = []
    for b in sorted(found, key=lambda b: -b[3]):
        if all(b[1] < c[0] or b[0] > c[1] for c in blocks):
            blocks.append(b)
    return sorted(blocks)


def place_in_blocks(blocks, targets, slack=0):
    """{target: (linux address, block)} for targets inside a block's range. No slack: held out against the known data
    pairs, 2 of 6 targets just outside a block's members (0x28 and 0x2c before it) were wrong."""
    out = {}
    for t in targets:
        for b in blocks:
            if b[0] - slack <= t <= b[1] + slack:
                out[t] = (t + b[2], b)
                break
    return out


def dtor_slots(rtti):
    """{(class, offset): d} for the vtable pairs with one more Linux slot than Windows slots (GCC's two destructors
    where MSVC has one): d is the destructor's Windows slot; Windows slot i is Linux slot i + (i > d). The primary
    vtable declares the destructor first (d = 0); an interface's secondary vtable can declare it last (Engine at
    offset 48: 11 slots, d = 10, so slot 5 stays slot 5, not 6). Siblings (the same offset and slot count, at least
    half the slots shared) differ in the destructor on both builds: the Windows slots where they differ, mapped by d,
    plus d + 1 must be Linux slots where they differ (a subset: MSVC folds identical functions, so two classes' equal
    overrides can share one Windows address and differ on Linux). A sibling votes when exactly one d fits. A row
    without a decisive vote (best twice the runner-up) takes its (offset, slot count) group's decision; otherwise the
    row keeps the range of the d that fit any
    sibling: {(class, offset): (lo, hi)}, lo == hi when decided, (0, 0) for rows without siblings. lin_slot maps a
    slot through it (slots inside an undecided range stay open)."""
    rows = []
    for cls, rs in rtti.items():
        for r in rs:
            ws = [int(x, 16) for x in r['win_slots']]
            ls = [int(x, 16) for x in r.get('lin_slots', [])]
            if len(ls) == len(ws) + 1 and ws:
                rows.append((cls, r['offset'], ws, ls))
    groups = {}
    for row in rows:
        groups.setdefault((row[1], len(row[2])), []).append(row)
    own, seen, fits = {}, set(), {}
    for group in groups.values():
        for cls, off, ws, ls in group:
            votes = {}
            for c2, _, ws2, ls2 in group:
                if c2 == cls or sum(1 for a, b in zip(ws, ws2) if a == b) < len(ws) // 2:
                    continue
                seen.add((cls, off))
                wd = {i for i in range(len(ws)) if ws[i] != ws2[i]}
                ld = {i for i in range(len(ls)) if ls[i] != ls2[i]}
                fit = [d for d in wd if {i + (1 if i > d else 0) for i in wd} | {d + 1} <= ld]
                fits.setdefault((cls, off), set()).update(fit)
                if len(fit) == 1:
                    votes[fit[0]] = votes.get(fit[0], 0) + 1
            ranked = sorted(votes.items(), key=lambda kv: -kv[1])
            if ranked and (len(ranked) == 1 or ranked[0][1] >= 2 * ranked[1][1]):
                own[(cls, off)] = ranked[0][0]
    decided = {}
    for key, group in groups.items():
        ds = {}
        for cls, off, _, _ in group:
            if (cls, off) in own:
                ds[own[(cls, off)]] = ds.get(own[(cls, off)], 0) + 1
        ranked = sorted(ds.items(), key=lambda kv: -kv[1])
        if ranked and (len(ranked) == 1 or ranked[0][1] >= 2 * ranked[1][1]):
            decided[key] = ranked[0][0]
    out = {}
    for cls, off, ws, _ in rows:
        if (cls, off) in own:
            d = own[(cls, off)]
        elif (off, len(ws)) in decided:
            d = decided[(off, len(ws))]
        elif (cls, off) not in seen:
            d = 0
        else:
            f = fits.get((cls, off)) or {0, len(ws) - 1}
            out[(cls, off)] = (min(f), max(f))
            continue
        out[(cls, off)] = (d, d)
    return out


def lin_slot(i, rng):
    """The Linux slot of Windows slot i under a dtor_slots range; None inside an undecided range."""
    lo, hi = rng
    if i <= lo:   # below the destructor, or the destructor itself (GCC's first, D1, takes its place)
        return i
    if i > hi:
        return i + 1
    return None


def place_vtables(rtti, targets):
    """An address inside a paired vtable maps to its slot on Linux: i + (i > d) with the destructor slot d of
    dtor_slots; vtables whose slot counts differ by more than one (overloads ordered differently: Weapon has 1047
    Windows and 1065 Linux slots, +1 up to slot 573 and +18 from 983) are left open, as are ambiguous ones."""
    dpos = dtor_slots(rtti)
    slots = {}
    for cls, rows in rtti.items():
        for r in rows:
            wv, lv = int(r['win_vtable'], 16), int(r['lin_vtable'], 16)
            slots[wv] = (cls, r['offset'], wv, lv, len(r['win_slots']), len(r.get('lin_slots', [])))
    out = {}
    for t in targets:
        hit = [v for v in slots.values() if v[2] <= t < v[2] + 8 * v[4]]
        if hit:
            cls, off, wv, lv, n, nl = hit[0]
            k = (t - wv) // 8
            if k == 0:   # the vtable itself (class identity by vptr): slot 0 on both builds
                j = 0
            else:
                j = lin_slot(k, dpos[(cls, off)]) if nl == n + 1 and (cls, off) in dpos else None
            if j is not None:
                out[t] = {'class': cls, 'offset': off, 'slot': k, 'placed': lv + 8 * j}
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--cache', required=True)
    p.add_argument('--pairs', required=True)
    p.add_argument('--rtti', required=True)
    p.add_argument('--min-votes', type=int, default=2)
    p.add_argument('rvas', nargs='+')
    args = p.parse_args(argv)
    wg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-win.pickle'), 'rb'))
    lg, _ = pickle.load(open(os.path.join(args.cache, 'calls2-lin.pickle'), 'rb'))
    pairs = {int(k, 16): int(v['lin'], 16) for k, v in json.load(open(args.pairs)).items()}
    targets = [int(x, 16) for x in args.rvas]
    vt = place_vtables(json.load(open(args.rtti))['classes'], targets)
    gl = place_globals(wg, lg, pairs, [t for t in targets if t not in vt], args.min_votes)
    for t in targets:
        if t in vt:
            v = vt[t]
            print(f'{t:x}: vtable {v["class"]} (offset {v["offset"]}) slot {v["slot"]} -> {v["placed"]:x}')
        else:
            g = gl[t]
            top = ', '.join(f'{a:x}:{n}' for a, n in g['top'])
            print(f'{t:x}: {g["users"]} paired users; ' + (f'placed {g["placed"]:x}' if g['placed'] else 'open') + f' (votes {top})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
