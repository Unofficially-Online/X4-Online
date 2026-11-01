"""Compare the per-tick WORLD_HEALTH rows of two clients (FR28, native_gate_world_health_ticks).

The census hashes hull and shield every stride ticks; WORLD_HEALTH rows carry them for every
watched object on every tick of the configured window, as bits:
  tick=N id=SAVED present=1 time=GAMETIME hull=HULL shield=VALUE CHANGED MAXIMUM DELAY RATE FACTOR
  GENERATORS MODIFIED MOD0 MOD1 MOD2 (one hex string, lockstep_state.h shield_state; 'none' without
  a shield group). Both executables print the same text for the same memory.

  python tools/dev/census_world_health.py LEFT RIGHT [--id 7336a ...] [--changes 12]

LEFT/RIGHT: a run directory's client (…/client1) or its engine-output.log. Per watched object:
the first tick at which the two differ and, for each differing field, both values and the
distance in units in the last place; then each side's changes (ticks where the stored value,
change time or hull moved, with the stored deltas). Read only.
"""
import argparse
import re
import struct
from pathlib import Path

ROW = re.compile(rb'NATIVE_GATE WORLD_HEALTH tick=(\d+) id=([0-9a-f]{16}) present=([01]) time=([0-9a-f]{16}) '
                 rb'hull=([0-9a-f]{16}|-) shield=([0-9a-f]+|none|-)\s*$')
# (name, start, end, kind) inside the shield text of lockstep_state.h shield_state.
SHIELD = (('value', 0, 16, 'd'), ('changed', 16, 32, 'd'), ('maximum', 32, 48, 'd'), ('delay', 48, 64, 'd'),
          ('rate', 64, 72, 'f'), ('factor', 72, 80, 'f'), ('generators', 80, 96, 'q'), ('modified', 96, 97, 'b'),
          ('mod0', 97, 105, 'f'), ('mod1', 105, 113, 'f'), ('mod2', 113, 121, 'f'))


def decode(time, hull, shield):
    """Field -> (hex, kind) of one row; floats keep their exact bits."""
    fields = {'time': (time, 'd'), 'hull': (hull, 'd')}
    if shield in ('none', '-'):
        fields['shield'] = (shield, 's')
    elif len(shield) == 121:
        for name, start, end, kind in SHIELD:
            fields[name] = (shield[start:end], kind)
    else:
        raise ValueError(f'shield text of {len(shield)} characters')
    return fields


def value(text, kind):
    if text in ('-', 'none'):
        return text
    if kind == 'd':
        return struct.unpack('>d', bytes.fromhex(text))[0]
    if kind == 'f':
        return struct.unpack('>f', bytes.fromhex(text))[0]
    return int(text, 16)


def ordered(text, kind):
    """The float's bits as an integer whose difference counts units in the last place."""
    bits = int(text, 16)
    width = 64 if kind == 'd' else 32
    sign = 1 << (width - 1)
    return bits if not bits & sign else sign - bits


def ulp(a, b, kind):
    if kind not in ('d', 'f') or '-' in (a, b) or 'none' in (a, b):
        return None
    return ordered(b, kind) - ordered(a, kind)


def rows(path, wanted=None):
    """id -> tick -> {'present': bool, 'fields': ...} from one engine-output.log."""
    path = Path(path)
    if path.is_dir():
        path = path / 'engine-output.log'
    result = {}
    with path.open('rb') as stream:
        for line in stream:
            if b'WORLD_HEALTH' not in line:
                continue
            m = ROW.search(line)
            if not m:
                raise ValueError(f'malformed WORLD_HEALTH row: {line[:200]!r}')
            identity = m[2].decode()
            if wanted and identity not in wanted:
                continue
            tick = int(m[1])
            ticks = result.setdefault(identity, {})
            if tick in ticks:
                raise ValueError(f'two WORLD_HEALTH rows for {identity} at tick {tick}')
            ticks[tick] = {'present': m[3] == b'1', 'fields': decode(m[4].decode(), m[5].decode(), m[6].decode())}
    return result


def differences(left, right):
    """[(field, left hex, right hex, kind, ulp)] of two rows, the game time excluded; a presence
    difference is the only one listed for that row."""
    if left['present'] != right['present']:
        return [('present', str(int(left['present'])), str(int(right['present'])), 'b', None)]
    out = []
    names = [n for n in left['fields'] if n != 'time'] + [n for n in right['fields'] if n not in left['fields']]
    for name in names:
        kind = (left['fields'].get(name) or right['fields'][name])[1]
        a = left['fields'].get(name, ('-',))[0]
        b = right['fields'].get(name, ('-',))[0]
        if a != b:
            out.append((name, a, b, kind, ulp(a, b, kind)))
    return out


def changes(ticks):
    """[(tick, [(field, before hex, after hex)])] where value, changed or hull moved between consecutive rows."""
    out = []
    previous = None
    for tick in sorted(ticks):
        row = ticks[tick]
        if previous is not None:
            moved = []
            for name in ('value', 'changed', 'hull', 'shield'):
                a = previous['fields'].get(name, ('-',))[0]
                b = row['fields'].get(name, ('-',))[0]
                if a != b:
                    moved.append((name, a, b))
            if previous['present'] != row['present']:
                moved = [('present', str(int(previous['present'])), str(int(row['present'])))]
            if moved:
                out.append((tick, moved))
        previous = row
    return out


def report(left, right, wanted=None, limit=12):
    lines = []
    identities = sorted(set(left) | set(right))
    if wanted:
        identities = [i for i in identities if i in wanted]
    for identity in identities:
        a, b = left.get(identity, {}), right.get(identity, {})
        common = sorted(set(a) & set(b))
        lines.append(f'== {identity} ticks left {len(a)} right {len(b)} common {len(common)}'
                     + (f' ({common[0]}..{common[-1]})' if common else ''))
        first = next((t for t in common if differences(a[t], b[t])), None)
        if first is None:
            lines.append('   equal on every common tick')
        else:
            time_a, time_b = a[first]['fields']['time'][0], b[first]['fields']['time'][0]
            lines.append(f'   first difference at tick {first} (time {value(time_a, "d")!r}'
                         + ('' if time_a == time_b else f' / right {value(time_b, "d")!r}') + ')')
            for name, x, y, kind, distance in differences(a[first], b[first]):
                text = f'     {name}: left {x} right {y}'
                if kind in ('d', 'f') and '-' not in (x, y):
                    text = f'     {name}: left {x} ({value(x, kind)!r}) right {y} ({value(y, kind)!r})'
                if distance is not None:
                    text += f' ulp {distance:+d}'
                lines.append(text)
        for side, ticks in (('left', a), ('right', b)):
            moved = changes(ticks)
            lines.append(f'   {side} changes: {len(moved)}')
            for tick, fields in moved[:limit]:
                parts = []
                for name, x, y in fields:
                    kind = 'd' if name in ('value', 'changed', 'hull') else 's'
                    if kind == 'd' and '-' not in (x, y):
                        parts.append(f'{name} {value(x, kind)!r} -> {value(y, kind)!r} (d {value(y, kind) - value(x, kind)!r})')
                    else:
                        parts.append(f'{name} {x} -> {y}')
                lines.append(f'     tick {tick}: ' + '; '.join(parts))
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('left')
    parser.add_argument('right')
    parser.add_argument('--id', action='append', default=[], help='saved ID (hex, leading zeros optional)')
    parser.add_argument('--changes', type=int, default=12, help='changes listed per side and object')
    args = parser.parse_args()
    wanted = {i.lower().rjust(16, '0') for i in args.id} or None
    print(report(rows(args.left, wanted), rows(args.right, wanted), wanted, args.changes))


if __name__ == '__main__':
    main()
