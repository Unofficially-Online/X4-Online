"""Regenerate tools/dev/linux_people_sites.json for the minimal Linux player path from the tools' evidence, keeping the
hand-read entries (status matched/candidate/open with evidence) as overrides."""
import sys, os, json, pickle, bisect
S = os.environ.get('LINUX_PAIR_WORK', os.path.join(os.path.dirname(__file__), '..', '..', '..', '.local', 'linux-pairs'))
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import coverage, linux_pair_data as D
os.chdir(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
MODS = ['pilot_inputs', 'pilot_native', 'pilot_stock', 'pilot_slots', 'pilot_present', 'pilot_stock_view', 'pilot_camera',
        'pilot_eye']
path = 'tools/dev/linux_people_sites.json'
doc = json.load(open(path))
# hand-written entries (a 'what', a reading) and batch candidates survive a regeneration; generated ones are rebuilt
manual = {s['win']: s for s in doc['sites'] if s.get('what') or 'read' in s.get('evidence', '')
          or s.get('evidence', '').startswith('batch')}
if os.path.exists(os.path.join(S, 'strong.json')):   # linux_people batch candidates (votes, shared fields, size)
    for b in json.load(open(os.path.join(S, 'strong.json'))):
        if b['win'] not in manual or not manual[b['win']].get('lin'):
            manual[b['win']] = dict(manual.get(b['win'], {}), module=b['module'], win=b['win'], lin=b['lin'],
                                    status='candidate',
                                    evidence=f"batch: votes {b['votes']}, fields {b['fields']}, size ratio {b['ratio']}")
lg = pickle.load(open(os.path.join(S, 'paircache', 'calls2-lin.pickle'), 'rb'))[0]
pairs = coverage.pairs
blocks = pickle.load(open(os.path.join(S, 'blocks.pickle'), 'rb'))
rtti = json.load(open(os.path.join(S, 'pairs-rtti.json')))['classes']
starts = sorted(coverage.wg)
sites, seen = [], set()
for m in MODS:
    c = coverage.classify(m)
    data_targets = c['data'] + c['rdata']
    vt = D.place_vtables(rtti, data_targets)
    singles = D.place_globals(coverage.wg, lg, {k: int(v['lin'], 16) for k, v in pairs.items()}, [t for t in c['data']])
    inblock = D.place_in_blocks(blocks, c['data'])
    for kind, rvas in c.items():
        for r in rvas:
            key = '%x' % r
            if key in seen:
                continue
            seen.add(key)
            e = {'module': m, 'win': key, 'kind': kind, 'lin': None, 'status': 'open', 'evidence': ''}
            if key in manual:
                e.update({k: manual[key][k] for k in ('lin', 'status', 'evidence', 'what') if k in manual[key]})
            elif r in pairs:
                e.update(lin=pairs[r]['lin'], status='candidate', evidence='linux_pair_calls: ' + pairs[r]['why'])
            elif r in vt:
                e.update(lin='%x' % vt[r]['placed'], status='candidate', evidence=f'vtable {vt[r]["class"]} slot {vt[r]["slot"]}')
            elif singles.get(r, {}).get('placed'):
                g = singles[r]
                e.update(lin='%x' % g['placed'], status='candidate', evidence=f'aligned global votes {g["top"][0][1]}')
            elif r in inblock:
                a, b = inblock[r]
                e.update(lin='%x' % a, status='candidate', evidence=f'block {b[0]:x}..{b[1]:x} delta {b[2]:+x} ({b[3]} members)')
            elif kind in ('in_paired_fn', 'in_open_fn'):
                k = bisect.bisect_right(starts, r) - 1
                e['evidence'] = f'inside {starts[k]:x}' + (f' (paired {pairs[starts[k]]["lin"]})' if starts[k] in pairs else '')
            sites.append(e)
for key, s in manual.items():   # hand-read entries outside the modules' code constants
    if key not in seen:
        sites.append(s)
doc['sites'] = sites
doc['coverage'] = {'sites': len(sites), 'placed': sum(1 for s in sites if s.get('lin')),
                   'matched': sum(1 for s in sites if s['status'] == 'matched')}
json.dump(doc, open(path, 'w'), indent=1)
print(doc['coverage'])
by = {}
for s in sites:
    by.setdefault(s['module'], [0, 0])
    by[s['module']][0] += 1
    by[s['module']][1] += bool(s.get('lin'))
print({m: f'{p}/{n}' for m, (n, p) in by.items()})
