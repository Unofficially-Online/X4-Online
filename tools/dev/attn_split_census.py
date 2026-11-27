"""Where a census split lies: each differing object of two clients' census records (x4lan-census-*.txt), sorted into
the players' sectors and the rest of the universe, per capture, with the classes (docs/ATTENTION-PER-PLAYER.md).

    python tools/dev/attn_split_census.py RUN --save SESSION_SAVE.xml.gz --sector 1=cluster_101_sector001_macro
        --sector 2=cluster_113_sector001_macro [--x4 X4.exe] [--json OUT]

A saved object's sector comes from the session save's component tree; a created object (key bit 63) follows its parent
keys through the records' rows until a saved one. The records hold only the rows that changed since the capture before, so
after the first differing capture the object lists are leads (an object that differed earlier and did not change since is
not listed again); the union over captures is the footprint of the split.
"""
import argparse
import gzip
import json
import sys
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
import census_record_diff as crd  # noqa: E402


def save_sectors(path):
    """{saved key: sector macro or None} of every component of a save."""
    out, stack = {}, []
    with gzip.open(path, 'rb') as f:
        for ev, el in ET.iterparse(f, events=('start', 'end')):
            if el.tag != 'component':
                if ev == 'end' and el.tag not in ('connection', 'connections'):
                    el.clear()
                continue
            if ev == 'start':
                stack.append((el.get('class'), el.get('macro')))
                cid = el.get('id')
                if cid:
                    out['%016x' % int(cid.strip('[]'), 16)] = next((m for c, m in reversed(stack) if c == 'sector'), None)
            else:
                stack.pop()
                el.clear()
    return out


def classify(run, sectors_of, regions, x4=None):
    a, b = crd.records(Path(run) / 'client1'), crd.records(Path(run) / 'client2')
    parents = {}
    for rec in list(a.values()) + list(b.values()):
        for key, (kind, flags, parent, hashes) in rec['items'].items():
            parents[key] = parent

    def sector(key, depth=0):
        if key in sectors_of:
            return sectors_of[key]
        if depth > 24 or key not in parents:
            return None
        return sector(parents[key], depth + 1)

    names_needed, report, ever = set(), [], {}
    for t in sorted(set(a) & set(b)):
        equal = a[t]['state'] == b[t]['state']
        objects = [] if equal else crd.differing(a[t], b[t])
        by_region, by_class = Counter(), {}
        for key, side, kind, parent, domains in objects:
            s = sector(key)
            region = regions.get(s, 'outside' if s else 'unplaced')
            by_region[region] += 1
            by_class.setdefault(region, Counter())[kind] += 1
            names_needed.add(kind)
            ever.setdefault(key, (region, kind, t))
        report.append({'tick': t, 'equal': equal, 'roots': [d for d in crd.DOMAINS if crd.roots(a[t]['state']).get(d) != crd.roots(b[t]['state']).get(d)],
                       'objects': a[t]['objects'], 'differing': len(objects), 'by_region': dict(by_region),
                       'by_class': {r: c.most_common(8) for r, c in by_class.items()}})
    names = crd.class_names(x4, names_needed) if x4 else {}
    for e in report:
        e['by_class'] = {r: [(names.get(k, k), n) for k, n in c] for r, c in e['by_class'].items()}
    footprint = Counter(r for r, k, t in ever.values())
    first_outside = sorted(((t, key, names.get(k, k)) for key, (r, k, t) in ever.items() if r == 'outside'))[:20]
    return {'captures': report, 'footprint': dict(footprint), 'first_outside': first_outside}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('run', type=Path)
    p.add_argument('--save', type=Path, required=True)
    p.add_argument('--sector', action='append', default=[], help='K=SECTOR_MACRO: slot K\'s sector')
    p.add_argument('--x4', type=Path)
    p.add_argument('--json', type=Path)
    args = p.parse_args(argv)
    regions = {}
    for spec in args.sector:
        k, macro = spec.split('=', 1)
        regions[macro] = 'S%s' % k
    out = classify(args.run, save_sectors(args.save), regions, args.x4)
    for e in out['captures']:
        print('tick %6d %-5s objects=%s differing=%d %s roots=%s' % (e['tick'], 'equal' if e['equal'] else 'DIFF', e['objects'], e['differing'],
                                                               json.dumps(e['by_region']), ','.join(e['roots'])))
        for region, classes in sorted(e['by_class'].items()):
            print('    %-8s %s' % (region, ', '.join('%s %d' % c for c in classes)))
    print('footprint (distinct objects ever listed as differing):', json.dumps(out['footprint']))
    print('first outside objects:', out['first_outside'][:10])
    if args.json:
        args.json.write_text(json.dumps(out, indent=1, default=str) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
