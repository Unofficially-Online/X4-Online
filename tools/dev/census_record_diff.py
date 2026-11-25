"""Name the objects behind a census split of a live game from the clients' census records.

  python tools/dev/census_record_diff.py RUN/client1 RUN/client2 [--tick T] [--limit 40] [--x4 X4.exe] [--json OUT]

Every session client writes, after each published census capture but the first, one record file beside its log
(native_gate_world_record, lockstep_world_hash.h: x4lan-census-<k>.txt, the last N captures; x4ls collects them into the
client's run folder): the header 'WORLD_RECORD_1 tick=T objects=O changed=C rows=R state=S' and one row per object whose
hashes changed since the capture before: saved key, type (vtable RVA), flags, parent key and the first 8 bytes of the
topology, health, motion and timing hashes.

For each capture tick both folders hold, the clients agree when their states (the four roots) agree. At the first capture
whose states differ after one that agreed, the record names every differing object exactly: one listed by both clients
with other hashes, and one listed by one client only (it changed there and not on the other, and the capture before
agreed). Later captures are reported the same way, as leads: an object that differed already can come back to equal
without a row on both sides. --x4 names the types (the vtable's RTTI class in that X4.exe).
"""
import argparse
import json
import re
import struct
from pathlib import Path

DOMAINS = ('topology', 'health', 'motion', 'timing')
HEADER = re.compile(r'^WORLD_RECORD_1 tick=(\d+) objects=(\d+) changed=(\d+) rows=(\d+) state=(\S+)$')
ROW = re.compile(r'^([0-9a-f]{16}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{16})((?: [0-9a-f]{16}){4})$')


def read_record(path):
    """{'tick', 'objects', 'changed', 'rows', 'state', 'items': {saved: (type, flags, parent, (h0, h1, h2, h3))}} of one file."""
    lines = Path(path).read_text(encoding='ascii').splitlines()
    m = HEADER.match(lines[0]) if lines else None
    if not m:
        raise ValueError(f'{path}: not a census record')
    items = {}
    for line in lines[1:]:
        r = ROW.match(line)
        if not r:
            raise ValueError(f'{path}: bad row {line[:80]!r}')
        items[r.group(1)] = (r.group(2), r.group(3), r.group(4), tuple(r.group(5).split()))
    if len(items) != int(m.group(4)):
        raise ValueError(f'{path}: {len(items)} rows, the header says {m.group(4)}')
    return {'tick': int(m.group(1)), 'objects': int(m.group(2)), 'changed': int(m.group(3)), 'rows': int(m.group(4)),
            'state': m.group(5), 'items': items}


def records(folder):
    """{tick: record} of the record files in a client's run folder (or a profile folder)."""
    out = {}
    for path in sorted(Path(folder).glob('x4lan-census-*.txt')):
        record = read_record(path)
        out[record['tick']] = record
    return out


def differing(a, b):
    """[(saved, side, type, parent, domains)] of two records of one tick: listed by both with other hashes ('both'), or by one
    only ('a' / 'b': it changed on that client alone). domains: the differing ones ('changed' for a one-sided row)."""
    out = []
    for saved in sorted(set(a['items']) | set(b['items'])):
        x, y = a['items'].get(saved), b['items'].get(saved)
        if x and y:
            if x == y:
                continue
            domains = [d for d, p, q in zip(DOMAINS, x[3], y[3]) if p != q]
            if x[:3] != y[:3]:
                domains = ['type/flags/parent'] + domains
            out.append((saved, 'both', x[0], x[2], domains))
        else:
            row = x or y
            out.append((saved, 'a' if x else 'b', row[0], row[2], ['changed']))
    return out


def roots(state):
    """The four roots of a WORLD_IMPORTED state (schema:tick:objects:topology:health:motion:timing)."""
    parts = state.split(':')
    return dict(zip(DOMAINS, parts[-4:])) if len(parts) >= 7 else {}


def class_names(x4, types):
    """{type RVA hex: RTTI class} from X4.exe (the complete object locator before each vtable)."""
    data = Path(x4).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<Q', data, pe + 48)[0]
    sections, at = [], pe + 24 + optional
    for _ in range(count):
        size, va, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
        sections.append((va, raw_size, raw))
        at += 40

    def offset(rva):
        for va, raw_size, raw in sections:
            if va <= rva < va + raw_size:
                return raw + rva - va
        return None

    def qword(rva):
        o = offset(rva)
        return struct.unpack_from('<Q', data, o)[0] if o is not None else 0

    names = {}
    for t in types:
        locator = offset(qword(int(t, 16) - 8) - base)
        descriptor = offset(struct.unpack_from('<I', data, locator + 12)[0]) if locator is not None else None
        names[t] = data[descriptor + 16:descriptor + 216].split(b'\0')[0].decode('latin1') if descriptor is not None else '?'
    return names


def compare(folder_a, folder_b, tick=None):
    a, b = records(folder_a), records(folder_b)
    report, agreed_before = [], None
    for t in sorted(set(a) & set(b)):
        if tick is not None and t != tick:
            continue
        ra, rb = roots(a[t]['state']), roots(b[t]['state'])
        equal = a[t]['state'] == b[t]['state']
        entry = {'tick': t, 'equal': equal, 'roots': [d for d in DOMAINS if ra.get(d) != rb.get(d)],
                 'exact': not equal and agreed_before is True, 'objects': [] if equal else differing(a[t], b[t])}
        report.append(entry)
        agreed_before = equal
    return report


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('a', type=Path, help="client A's run folder (RUN/client1) or profile folder")
    p.add_argument('b', type=Path, help="client B's")
    p.add_argument('--tick', type=int, help='only this capture tick')
    p.add_argument('--limit', type=int, default=40, help='objects listed per capture (default 40)')
    p.add_argument('--x4', type=Path, help='X4.exe: name the types by their RTTI class')
    p.add_argument('--json', type=Path, help='write the whole report as JSON')
    args = p.parse_args(argv)
    report = compare(args.a, args.b, args.tick)
    if not report:
        print('no capture tick in both folders (x4lan-census-*.txt)')
        return 1
    names = class_names(args.x4, {o[2] for e in report for o in e['objects']}) if args.x4 else {}
    for e in report:
        if e['equal']:
            print(f"tick {e['tick']}: equal")
            continue
        exact = 'exact (the capture before agreed)' if e['exact'] else 'a lead (no agreeing capture recorded before it)'
        print(f"tick {e['tick']}: roots differ in {', '.join(e['roots'])}; {len(e['objects'])} objects, {exact}")
        for saved, side, kind, parent, domains in e['objects'][:args.limit]:
            name = f" {names.get(kind, '')}" if names else ''
            only = '' if side == 'both' else f' (a row on client {side.upper()} only)'
            print(f"  {saved} type={kind}{name} parent={parent} {'/'.join(domains)}{only}")
    if args.json:
        args.json.write_text(json.dumps(report, indent=1) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
