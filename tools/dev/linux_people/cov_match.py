"""Match the open minimal-path Windows functions against the Linux functions a coverage trace saw run on the input
client only (x4mods-e7's lx32b result: VA, first tick client 1, client 2 or -, both|input:<event>). Leads only: report
3f. Environment: LINUX_PAIR_WORK (the sweeps), LINUX_COVERAGE_RESULT, X4_WINDOWS_EXE, X4_LINUX_BIN."""
import sys, os, json, pickle, re
S = os.environ.get('LINUX_PAIR_WORK', os.path.join(os.path.dirname(__file__), '..', '..', '..', '.local', 'linux-pairs'))
os.environ['LINUX_PAIR_WORK'] = S
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.dirname(__file__))
import coverage
import linux_pair_calls as L
from linux_pair_candidates import offsets
RES = os.environ['LINUX_COVERAGE_RESULT']
rows = {}
for line in open(RES):
    if line.startswith('#') or not line.strip():
        continue
    parts = line.split()
    rows[int(parts[0], 16)] = (parts[1], parts[2], parts[3])
inputs = {va: r[2].split(':', 1)[1] for va, r in rows.items() if r[2].startswith('input:')}
both = {va for va, r in rows.items() if r[2] == 'both'}
print('rows', len(rows), 'input-only', len(inputs), 'both', len(both))
wimg = L.win_functions(os.environ.get('X4_WINDOWS_EXE', os.path.join('..', 'x4-re', 'bin', 'X4.exe')))[0]
limg = L.lin_functions(os.environ.get('X4_LINUX_BIN', os.path.join('..', 'x4-re', 'bin-linux', 'X4')))[0]
wg = coverage.wg
lg = pickle.load(open(os.path.join(S, 'paircache', 'calls2-lin.pickle'), 'rb'))[0]
wst, lst = sorted(wg), sorted(lg)
wsize = dict(zip(wst, [b - a for a, b in zip(wst, wst[1:] + [wst[-1] + 1])]))
lsize = dict(zip(lst, [b - a for a, b in zip(lst, lst[1:] + [lst[-1] + 1])]))
pairs = {k: int(v['lin'], 16) for k, v in coverage.pairs.items()}
taken = set(pairs.values())
lfields = {l: offsets(limg, l, l + min(lsize.get(l, 256), 1 << 16)) for l in inputs}
opens = []
for m in ['pilot_inputs', 'pilot_native', 'pilot_stock', 'pilot_slots', 'pilot_present', 'pilot_stock_view', 'pilot_camera', 'pilot_eye']:
    for w in coverage.classify(m)['fn_open']:
        if w not in [o[1] for o in opens]:
            opens.append((m, w))
results = []
for m, w in opens:
    wo = offsets(wimg, w, w + min(wsize.get(w, 256), 1 << 16))
    scored = []
    for l, ev in inputs.items():
        if l in taken:
            continue
        lo = lfields[l]
        union = wo | lo
        if len(union) < 3:
            continue
        share = len(wo & lo) / len(union)
        ratio = lsize.get(l, 1) / max(wsize.get(w, 1), 1)
        if 0.4 <= ratio <= 2.5:
            scored.append((share, ratio, l, ev))
    scored.sort(reverse=True)
    if scored:
        best = scored[0]
        second = scored[1][0] if len(scored) > 1 else 0
        flag = 'STRONG' if best[0] >= 0.3 and best[0] >= second + 0.1 and len(wo) >= 4 else ''
        results.append((m, w, best, second, flag))
        print(f'{m:16s} {w:x} size {wsize.get(w)} -> {best[2]:x} ({best[3]}) fields {best[0]:.2f} ratio {best[1]:.2f} second {second:.2f} {flag}')
json.dump([{'module': m, 'win': '%x' % w, 'lin': '%x' % b[2], 'event': b[3], 'fields': round(b[0], 2), 'ratio': round(b[1], 2),
            'second': round(s, 2)} for m, w, b, s, f in results if f], open(os.path.join(S, 'cov-strong.json'), 'w'), indent=1)
print('strong', sum(1 for r in results if r[4]))
