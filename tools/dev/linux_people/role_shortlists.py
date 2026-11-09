"""Short lists for the minimal path's hooked Windows functions: the Linux functions that first ran (x4mods-e7's lx32b
trace) in the input event where the Windows function's role fires, ranked by shared struct field offsets and size.
The roles come from our own headers (lockstep_pilot_native.h, lockstep_pilot_stock.h): leads for a reading per site.
Environment: LINUX_PAIR_WORK (the sweeps, pairs-calls.json), LINUX_COVERAGE_RESULT, X4_WINDOWS_EXE, X4_LINUX_BIN."""
import os, sys, json, pickle
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from linux_pair_candidates import offsets  # noqa: E402
import linux_pair_calls as L  # noqa: E402

ROLES = [  # (Windows function, role, lx32b event tags)
    ('d86c80', 'stock handler: states/ranges (accelerate, roll, strafe)', ('accelerate', 'roll left', 'strafe left', 'strafe up')),
    ('d870b0', 'stock handler thunk', ('accelerate', 'roll left', 'strafe left')),
    ('d87f70', 'stock handler thunk', ('accelerate', 'roll left', 'strafe left')),
    ('71c640', 'stock handler (actions)', ('stop', 'travel', 'boost')),
    ('d88a10', 'cursor / direct mouse steering outputs', ('cursor',)),
    ('d88710', 'SPACE context edge, simulation half', ('map',)),
    ('d9c250', 'Flight Stopped (SPACE edge)', ('map',)),
    ('d9c590', 'Flight Started (SPACE edge)', ('map close', 'map')),
    ('ea7600', 'boost active', ('boost',)),
    ('ea7650', 'travel active', ('travel',)),
    ('d93440', 'flight control solver', ('accelerate',)),
]


def main():
    S = os.environ.get('LINUX_PAIR_WORK', '.local/linux-pairs')
    rows = {}
    for line in open(os.environ['LINUX_COVERAGE_RESULT']):
        if line.startswith('#') or not line.strip():
            continue
        va, t1, t2, tag = line.split()[:4]
        rows[int(va, 16)] = tag.split(':', 1)[1] if tag.startswith('input:') else tag
    wimg = L.win_functions(os.environ.get('X4_WINDOWS_EXE', '../x4-re/bin/X4.exe'))[0]
    limg = L.lin_functions(os.environ.get('X4_LINUX_BIN', '../x4-re/bin-linux/X4'))[0]
    wg = pickle.load(open(os.path.join(S, 'paircache', 'calls2-win.pickle'), 'rb'))[0]
    lg = pickle.load(open(os.path.join(S, 'paircache', 'calls2-lin.pickle'), 'rb'))[0]
    taken = {int(v['lin'], 16) for v in json.load(open(os.path.join(S, 'pairs-calls.json'))).values()}
    wst, lst = sorted(wg), sorted(lg)
    wsize = dict(zip(wst, [b - a for a, b in zip(wst, wst[1:] + [wst[-1] + 1])]))
    lsize = dict(zip(lst, [b - a for a, b in zip(lst, lst[1:] + [lst[-1] + 1])]))
    out = {}
    for w, role, events in ROLES:
        wa = int(w, 16)
        wo = offsets(wimg, wa, wa + min(wsize.get(wa, 256), 1 << 16))
        cands = [l for l, tag in rows.items() if any(tag.startswith(e) for e in events) and l not in taken and l in lsize]
        scored = []
        for l in cands:
            lo = offsets(limg, l, l + min(lsize[l], 1 << 16))
            union = wo | lo
            share = len(wo & lo) / len(union) if union else 0.0
            ratio = lsize[l] / max(wsize.get(wa, 1), 1)
            scored.append((round(share, 2), round(ratio, 2), '%x' % l, rows[l]))
        scored.sort(key=lambda s: (-s[0], abs(1 - s[1])))
        out[w] = {'role': role, 'size': wsize.get(wa), 'candidates': scored[:6], 'pool': len(cands)}
        print(f'{w} {role} (size {wsize.get(wa)}, pool {len(cands)}):')
        for s in scored[:6]:
            print(f'    {s[2]}  fields {s[0]:.2f}  size ratio {s[1]:.2f}  ({s[3]})')
    json.dump(out, open(os.path.join(S, 'role-shortlists.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
