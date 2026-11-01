"""Align two clients' FP_ATTACK_ROW lines (FR29b, native_gate_fp_attack_capture) and name the first difference.

The capture (src/native/engine/lockstep_fp_attack.h) records the out-of-sight attack computation call by call on both
executables: each get_attackstrength call ('gas', seq 0) and inside it, in order of entry (seq), the object distances
('dist'), the weapon loop ('wl'), each weapon's damage ('dps'), the damage adds ('add', with the accumulator they add
into: gas.out1/2, wl.out1/2 or a local) and the RNG draws ('rng'); apply_attackstrength's damage call on its own ('apply',
call 0). Damage structures are printed from +0x8 as 64 bytes: five doubles (hullshield, hullonly, shieldonly,
hullnoshield, result), four floats, the flags.

  python tools/dev/census_fp_attack.py LEFT RIGHT [--target 7336a] [--through TICK] [--detail]

LEFT/RIGHT: a run directory's client (.../client1) or its engine-output.log (Windows and Linux client of the same lane).
The get_attackstrength calls are matched by (tick, attacker, target, n-th such call); inside a matched pair the events
are matched by kind (dist, wl, dps and the loop's adds in order; the drones' and GetAttackStrength's own adds by their
accumulator and its value before; draws by the RNG state they step, since Linux leaves eight sites crossing a 16-byte
line unhooked). Every matched pair is compared inputs first (time, RNG word, arguments, structures before), then outputs;
the earliest pair (in the left side's order) with a difference is the FIRST DIFFERENCE: the field, both values as bits
and decoded, the distance in units in the last place. For a hullshield that differs, the float32 rounding the apply
takes is shown (and whether the double lies on a rounding midpoint). Then the apply rows of the same calls. Read only.

The adds (Windows 0xf0260, Linux 0xf05be0) read only the five doubles, +0x38 and +0x3c of both structures and write only
those of the accumulator; Linux's GetAttackStrength leaves +0x30, +0x34 and the flags (+0x40) of its local accumulators
unset (stack bytes; FR30, SESSION 23). So add rows are compared without f0, f1 and the flags; their differences are
counted on the last line ('add fields never read'), not reported.
"""
import argparse
import re
import struct
from collections import defaultdict
from pathlib import Path

ROW = re.compile(rb'NATIVE_GATE FP_ATTACK_ROW (.*?)\s*$')
BLOB_FIELDS = (('hullshield', 'd'), ('hullonly', 'd'), ('shieldonly', 'd'), ('hullnoshield', 'd'), ('result', 'd'),
               ('f0', 'f'), ('f1', 'f'), ('f2', 'f'), ('f3', 'f'), ('flags', 'h'))
# The structure fields the damage add never reads nor writes (see above): not compared in add rows, counted.
ADD_UNREAD = frozenset(('f0', 'f1', 'flags'))
unread_differences = [0]
# Arguments per kind (lockstep_fp_attack.h); ':d' a double's bits, ':f' a float's; names starting with 'w_' only Windows passes.
ARGS = {
    'gas': ('list4', 'list5', 'b7', 'b8', 'b9', 'i10', 'w_f11'),
    'wl': ('a6', 'a7:d', 'a8:f', 'a10', 'a11', 'a12', 'a14:f', 'a15:f', 'a17', 'a18', 'list4', 'list5', 'w_a16:d', 'w_a19'),
    'dps': ('flag',), 'add': (), 'rng': ('bound:f', 'result:f'), 'dist': ('time:d', 'c3', 'mode', 'result:f'), 'apply': ('flag',),
}
# (inputs, outputs) per kind: record keys, 'args:<name>' for arguments, 'bN' for structures.
FIELDS = {
    'gas': (('time', 'rng_in', 'args', 'b0', 'b2'), ('b1', 'b3', 'rng_out')),
    'wl': (('time', 'rng_in', 'obj', 'obj2', 'args', 'b0', 'b2'), ('b1', 'b3', 'rng_out')),
    'dps': (('rng_in', 'obj', 'args', 'b0'), ('b1', 'rng_out')),
    'add': (('label', 'b0', 'b1'), ('b2',)),
    'rng': (('rng_in', 'args:bound'), ('args:result', 'rng_out')),
    'dist': (('obj', 'obj2', 'args:time', 'args:c3', 'args:mode'), ('args:result',)),
    'apply': (('rng_in', 'args', 'b0'), ('ret',)),
}


def parse(path, through=None):
    path = Path(path)
    if path.is_dir():
        path = path / 'engine-output.log'
    out = []
    with path.open('rb') as stream:
        for line in stream:
            if b'FP_ATTACK_ROW' not in line:
                continue
            m = ROW.search(line)
            if not m:
                raise ValueError(f'malformed FP_ATTACK_ROW: {line[:200]!r}')
            r = dict(tok.split('=', 1) for tok in m[1].decode().split(' '))
            for k in ('index', 'tick', 'call', 'seq'):
                r[k] = int(r[k])
            r['rng_in'], r['rng_out'] = r.pop('rng').split(':')
            r['argv'] = [] if r['args'] == '-' else [int(x, 16) for x in r['args'].split(',')]
            if through is None or r['tick'] <= through:
                out.append(r)
    return out


def blob(text):
    if text == '-':
        return None
    raw = bytes.fromhex(text)
    vals = struct.unpack('<5d4fH6x', raw)
    bits = [raw[0 + 8 * i:8 + 8 * i] for i in range(5)] + [raw[40 + 4 * i:44 + 4 * i] for i in range(4)] + [raw[56:58]]
    return [(name, kind, v, b[::-1].hex()) for (name, kind), v, b in zip(BLOB_FIELDS, vals, bits)]


def ordered(bits, width):
    sign = 1 << (width - 1)
    return bits if not bits & sign else sign - bits


def ulp_text(a, b, kind):
    if kind not in ('d', 'f'):
        return ''
    width = 64 if kind == 'd' else 32
    return f' ({ordered(int(b, 16), width) - ordered(int(a, 16), width):+d} ulp)'


def decode(bits, kind):
    if kind == 'd':
        return repr(struct.unpack('>d', bytes.fromhex(bits.rjust(16, '0')))[0])
    if kind == 'f':
        return repr(struct.unpack('>f', bytes.fromhex(bits.rjust(8, '0')[-8:]))[0])
    return str(int(bits, 16))


def arg(r, name):
    names = [n.split(':')[0] for n in ARGS[r['kind']]]
    kinds = {n.split(':')[0]: (n.split(':')[1] if ':' in n else 'i') for n in ARGS[r['kind']]}
    i = names.index(name)
    v = r['argv'][i] if i < len(r['argv']) else 0
    width = 16 if kinds[name] == 'd' else 8
    return f'{v:0{width}x}', kinds[name]


def differences(left, right, keys, skip=frozenset()):
    """[(field, left, right, note)] for the record fields named in keys (in order); structure fields in skip are only
    counted (unread_differences)."""
    out = []
    for key in keys:
        if key == 'args':
            for n in ARGS[left['kind']]:
                name = n.split(':')[0]
                if name.startswith('w_'):
                    continue
                a, kind = arg(left, name)
                b, _ = arg(right, name)
                if a != b:
                    out.append((f'arg {name}', a, b, f'{decode(a, kind)} vs {decode(b, kind)}{ulp_text(a, b, kind)}'))
        elif key.startswith('args:'):
            name = key[5:]
            a, kind = arg(left, name)
            b, _ = arg(right, name)
            if a != b:
                out.append((f'arg {name}', a, b, f'{decode(a, kind)} vs {decode(b, kind)}{ulp_text(a, b, kind)}'))
        elif key in ('b0', 'b1', 'b2', 'b3'):
            x, y = blob(left[key]), blob(right[key])
            if x is None or y is None:
                if left[key] != right[key]:
                    out.append((key, left[key], right[key], 'present on one side only'))
                continue
            for (name, kind, va, ba), (_, _, vb, bb) in zip(x, y):
                if ba != bb and name in skip:
                    unread_differences[0] += 1
                elif ba != bb:
                    out.append((f'{key}.{name}', ba, bb, f'{va!r} vs {vb!r}{ulp_text(ba, bb, kind)}'))
        elif key == 'time':
            if left[key] != right[key]:
                out.append((key, left[key], right[key], f'{decode(left[key], "d")} vs {decode(right[key], "d")}{ulp_text(left[key], right[key], "d")}'))
        else:
            if left[key] != right[key]:
                out.append((key, left[key], right[key], ''))
    return out


def calls(rows):
    """call number -> its records by seq; the gas calls' identity keys in call order; the apply rows."""
    by = defaultdict(list)
    applies = []
    for r in rows:
        (applies if r['kind'] == 'apply' else by[r['call']]).append(r)
    for c in by.values():
        c.sort(key=lambda r: r['seq'])
    keyed, seen = {}, defaultdict(int)
    for number in sorted(by):
        head = by[number][0]
        if head['kind'] != 'gas':
            continue
        ident = (head['tick'], head['attacker'], head['target'])
        keyed[ident + (seen[ident],)] = by[number]
        seen[ident] += 1
    return keyed, applies


def add_read(text):
    """The part of a damage structure the add reads (the five doubles, +0x38, +0x3c), for matching."""
    return text[:80] + text[96:112] if len(text) == 128 else text


def match_events(left, right):
    """[(l, r)] pairs (r or l None when unmatched) of one call's events."""
    pairs = []
    def stream(events, kind):
        return [e for e in events if e['kind'] == kind]
    for kind in ('gas', 'dist', 'wl'):
        a, b = stream(left, kind), stream(right, kind)
        pairs += [(a[i] if i < len(a) else None, b[i] if i < len(b) else None) for i in range(max(len(a), len(b)))]
    a, b = stream(left, 'dps'), stream(right, 'dps')
    groups = defaultdict(lambda: ([], []))
    for e in a:
        groups[(e['obj'], e['in'])][0].append(e)
    for e in b:
        groups[(e['obj'], e['in'])][1].append(e)
    for x, y in groups.values():
        pairs += [(x[i] if i < len(x) else None, y[i] if i < len(y) else None) for i in range(max(len(x), len(y)))]
    # the loop's adds in order; the others by accumulator and its value before (the first that fits)
    a = [e for e in stream(left, 'add') if e['label'].startswith('wl.')]
    b = [e for e in stream(right, 'add') if e['label'].startswith('wl.')]
    pairs += [(a[i] if i < len(a) else None, b[i] if i < len(b) else None) for i in range(max(len(a), len(b)))]
    a = [e for e in stream(left, 'add') if not e['label'].startswith('wl.')]
    b = [e for e in stream(right, 'add') if not e['label'].startswith('wl.')]
    # three passes, each in order: accumulator and addend equal (the fields the add reads), then the accumulator, then the
    # label alone; so an add one side made with a zero addend (Linux leaves two of GetAttackStrength's add sites unhooked)
    # stays unmatched instead of taking the next add's partner
    used, hits = set(), {}
    for tier in range(3):
        for k, e in enumerate(a):
            if k in hits:
                continue
            hit = next((i for i, f in enumerate(b) if i not in used and f['label'] == e['label'] and f['in'] == e['in'] and
                        (tier == 2 or add_read(f['b0']) == add_read(e['b0'])) and (tier >= 1 or add_read(f['b1']) == add_read(e['b1']))), None)
            if hit is not None:
                used.add(hit)
                hits[k] = hit
    pairs += [(e, b[hits[k]] if k in hits else None) for k, e in enumerate(a)]
    pairs += [(None, f) for i, f in enumerate(b) if i not in used]
    # draws by the state they step
    a, b = stream(left, 'rng'), stream(right, 'rng')
    states = {f['rng_in']: f for f in b}
    taken = set()
    for e in a:
        f = states.get(e['rng_in'])
        pairs.append((e, f))
        if f is not None:
            taken.add(id(f))
    pairs += [(None, f) for f in b if id(f) not in taken]
    return pairs


def describe(e):
    if e is None:
        return '-'
    extra = f" obj={e['obj']}" if e['obj'] != '-' else ''
    extra += f" label={e['label']}" if e['label'] != '-' else ''
    return f"seq {e['seq']} {e['kind']} in={e['in']}{extra}"


def compare_call(left, right, detail):
    """The first difference of one matched call: (left seq, text) or None; prints every pair with --detail.

    The order is the left side's timeline: an event's inputs at its entry (seq), a leaf's outputs right after it, the
    weapon loop's outputs after the last event inside it, get_attackstrength's after everything."""
    pairs = sorted(match_events(left, right), key=lambda p: (p[0]['seq'] if p[0] else 1 << 30, p[1]['seq'] if p[1] else 0))
    last = max(e['seq'] for e in left)
    last_wl = max([e['seq'] for e in left if e['in'] == 'wl'] or [0])
    checks = []
    for l, r in pairs:
        if l is None or r is None:
            if detail:
                print(f'    unmatched  left {describe(l)}  right {describe(r)}')
            continue
        inputs, outputs = FIELDS[l['kind']]
        skip = ADD_UNREAD if l['kind'] == 'add' else frozenset()
        di, do = differences(l, r, inputs, skip), differences(l, r, outputs, skip)
        if detail:
            print(f"    {'=' if not di and not do else 'X'}  left {describe(l)}  right {describe(r)}"
                  + (f'  inputs differ: {", ".join(d[0] for d in di)}' if di else '') + (f'  outputs differ: {", ".join(d[0] for d in do)}' if do else ''))
        done = last + 1 if l['kind'] == 'gas' else max(l['seq'], last_wl) + 0.5 if l['kind'] == 'wl' else l['seq'] + 0.5
        if di:
            checks.append((l['seq'], 'INPUT', l, r, di))
        elif do:
            checks.append((done, 'OPERATION (equal inputs, different output)', l, r, do))
    if not checks:
        return None
    key, what, l, r, diffs = min(checks, key=lambda c: c[0])
    lines = [f"  {what} at left {describe(l)} / right {describe(r)}"]
    for field, a, b, note in diffs:
        lines.append(f'    {field}: left {a}  right {b}  {note}')
        if field.endswith('hullshield') and len(a) == 16:
            lines.append('      ' + rounding(a) + ' | ' + rounding(b))
    return (key, chr(10).join(lines))


def rounding(bits):
    """The float32 the apply rounds a binary64 to, and whether the double lies on a rounding midpoint."""
    d = struct.unpack('>d', bytes.fromhex(bits))[0]
    f = struct.unpack('<f', struct.pack('<f', d))[0]
    fb = struct.unpack('<I', struct.pack('<f', d))[0]
    m = int(bits, 16) & ((1 << 52) - 1)
    tie = (m & ((1 << 29) - 1)) == (1 << 28)
    return f'float {fb:08x} ({f!r}){" ON A MIDPOINT (ties to even)" if tie else ""}'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('left')
    p.add_argument('right')
    p.add_argument('--target', action='append', default=[], help='only calls on these targets (saved IDs, leading zeros optional)')
    p.add_argument('--through', type=int, help='only ticks up to this one')
    p.add_argument('--detail', action='store_true', help='every matched event pair of the compared calls')
    args = p.parse_args()
    targets = {t.lower().rjust(16, '0') for t in args.target}
    lrows, rrows = parse(args.left, args.through), parse(args.right, args.through)
    lcalls, lapply = calls(lrows)
    rcalls, rapply = calls(rrows)
    print(f'left: {len(lrows)} rows, {len(lcalls)} get_attackstrength calls, {len(lapply)} applies; '
          f'right: {len(rrows)} rows, {len(rcalls)} calls, {len(rapply)} applies')
    keys = sorted(set(lcalls) | set(rcalls))
    if targets:
        keys = [k for k in keys if k[2] in targets]
    only = [k for k in keys if (k in lcalls) != (k in rcalls)]
    for k in only[:20]:
        print(f"  only {'left' if k in lcalls else 'right'}: tick {k[0]} {k[1]} -> {k[2]} #{k[3]}")
    found = None
    for k in keys:
        if k not in lcalls or k not in rcalls:
            continue
        if args.detail:
            print(f'call tick {k[0]} {k[1]} -> {k[2]} #{k[3]} (left call {lcalls[k][0]["call"]}, right call {rcalls[k][0]["call"]})')
        first = compare_call(lcalls[k], rcalls[k], args.detail)
        if first and found is None:
            found = (k, first)
            if not args.detail:
                break
    if found:
        k, (seq, text) = found
        print(f'FIRST DIFFERENCE: tick {k[0]} attacker {k[1]} -> target {k[2]} #{k[3]} '
              f'(left call {lcalls[k][0]["call"]}, right call {rcalls[k][0]["call"]})')
        print(text)
    else:
        print(f'no difference in {sum(1 for k in keys if k in lcalls and k in rcalls)} matched calls')
    # the apply rows of the same pairs (tick, attacker, target, n-th)
    def index(rows):
        out, seen = {}, defaultdict(int)
        for r in sorted(rows, key=lambda r: r['index']):
            ident = (r['tick'], r['attacker'], r['target'])
            out[ident + (seen[ident],)] = r
            seen[ident] += 1
        return out
    la, ra = index(lapply), index(rapply)
    akeys = sorted(set(la) | set(ra))
    if targets:
        akeys = [k for k in akeys if k[2] in targets]
    shown = 0
    for k in akeys:
        if k not in la or k not in ra:
            print(f"apply only {'left' if k in la else 'right'}: tick {k[0]} {k[1]} -> {k[2]}")
            continue
        d = differences(la[k], ra[k], FIELDS['apply'][0] + FIELDS['apply'][1])
        if d and shown < 10:
            shown += 1
            print(f'apply tick {k[0]} {k[1]} -> {k[2]} #{k[3]} differs: ' + '; '.join(f'{f} {a} vs {b} {n}' for f, a, b, n in d))
    print(f'applies matched {sum(1 for k in akeys if k in la and k in ra)}, differing {shown}{"+" if shown == 10 else ""}')
    print(f'add fields never read (f0, f1, flags) differing, not compared: {unread_differences[0]}')


if __name__ == '__main__':
    main()
