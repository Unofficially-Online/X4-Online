"""Who allocates runtime IDs, and which allocations one client made more often: RUNTIME_IDS_CALLER rows of a run.

  python tools/dev/census_runtime_callers.py RUN [--left 1] [--right 2] [--right-run RUN2] [--tick N] [--phase load|play] [--limit 40]
  python tools/dev/census_runtime_callers.py RUN --first
  python tools/dev/census_runtime_callers.py RUN --rows [--left 1] [--right 3] [--from N]

RUN is a directory with client1, client2, ... Each client logs the table at tick 1 (Linux) and every 300 ticks, or
every native_gate_runtime_ids_caller_stride ticks.
Rows are compared at the last tick both clients logged, or at --tick, by (phase, thread, route, frames); only those
whose counts differ are listed, largest first. Frames are return addresses, innermost first, relative to the image
base (0x400000 on Linux, 0x140000000 on Windows). Reads logs only.

--first walks the ticks in order and stops at the first play-phase difference in the shared list, the one
native_gate_runtime_ids_barrier folds into the digest: the ui and view lists differ between clients by design. A
simulation allocation one client calls local shows here too, because the route is part of the row.

--rows (native_gate_runtime_ids_rows): the per-tick RUNTIME_IDS_ROW lines, one per tick with shared allocations, are
compared tick by tick from --from on; the first tick whose row differs (next ID, count, keys, or a row on one side
only) is printed with both rows, the keys whose counts differ and their stacks from the RUNTIME_IDS_CALLER key=
column. An allocation a tick early on one client shows as +1 on its tick and -1 on the next.
"""
import argparse
import collections
import re
from pathlib import Path

ROW = re.compile(r'RUNTIME_IDS_CALLER tick=(\d+) phase=(\w+) thread=(\w+) route=(\w+) count=(\d+) frames=([0-9a-f,]+)(?: cue=([\w.$]+))?')


def read_table(path):
    """{tick: {(phase, thread, route, frames): count}} from one client's log; an MD cue instance's frames end in
    ' cue=<name>' (the same stack for another cue is another row)."""
    tables = collections.defaultdict(dict)
    with open(path, errors='replace') as log:
        for line in log:
            match = ROW.search(line)
            if match:
                tick, phase, thread, route, count, frames, cue = match.groups()
                tables[int(tick)][(phase, thread, route, frames + (' cue=' + cue if cue else ''))] = int(count)
    return tables


def compare(left, right):
    """Rows whose counts differ, (difference, key, left count, right count), largest difference first."""
    rows = []
    for key in sorted(set(left) | set(right)):
        a, b = left.get(key, 0), right.get(key, 0)
        if a != b:
            rows.append((a - b, key, a, b))
    rows.sort(key=lambda row: (-abs(row[0]), row[1]))
    return rows


def first_difference(left, right, phase='play', route='shared'):
    """(first differing tick or None, last tick that agreed or None, its rows) over the ticks both clients logged."""
    agreed = None
    for tick in sorted(set(left) & set(right)):
        a = {k: v for k, v in left[tick].items() if k[0] == phase and k[2] == route}
        b = {k: v for k, v in right[tick].items() if k[0] == phase and k[2] == route}
        rows = compare(a, b)
        if rows:
            return tick, agreed, rows
        agreed = tick
    return None, agreed, []


ROWS = re.compile(r'RUNTIME_IDS_ROW index=\d+ tick=(\d+) next=([0-9a-f]+) count=(\d+) first=\w+ last=\w+ threads=(\S+) seq=\w+ sum=\w+ keys=(\S*)(?: keys_other=(\d+))?')
KEY = re.compile(r'RUNTIME_IDS_CALLER tick=\d+ phase=(\w+) thread=(\w+) route=(\w+) count=\d+ frames=([0-9a-f,]+)(?: cue=([\w.$]+))? key=([0-9a-f]{8})')


def read_rows(path):
    """({tick: (next, count, threads, {key: count}, other)}, {key: 'phase thread route frames[ cue=..]'}) of one client."""
    rows, keys = {}, {}
    with open(path, errors='replace') as log:
        for line in log:
            if 'RUNTIME_IDS_' not in line:
                continue
            match = ROWS.search(line)
            if match:
                tick, next_id, count, threads, listed, other = match.groups()
                counts = {}
                for item in filter(None, listed.split(',')):
                    key, value = item.split(':')
                    counts[key] = int(value)
                rows[int(tick)] = (next_id, int(count), threads, counts, int(other or 0))
                continue
            match = KEY.search(line)
            if match:
                phase, thread, route, frames, cue, key = match.groups()
                keys.setdefault(key, f'{phase} {thread} {route} {frames}' + (f' cue={cue}' if cue else ''))
    return rows, keys


def first_row_difference(left, right, start=0):
    """(tick, left row or None, right row or None, [(key, left count, right count)]) at the first tick from start whose
    rows differ, or None."""
    for tick in sorted(t for t in set(left) | set(right) if t >= start):
        a, b = left.get(tick), right.get(tick)
        if a == b:
            continue
        ka, kb = (a[3] if a else {}), (b[3] if b else {})
        keys = [(key, ka.get(key, 0), kb.get(key, 0)) for key in sorted(set(ka) | set(kb)) if ka.get(key, 0) != kb.get(key, 0)]
        return tick, a, b, keys
    return None


def log_of(run, client):
    return Path(run) / f'client{client}' / 'engine-output.log'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run')
    parser.add_argument('--left', type=int, default=1)
    parser.add_argument('--right', type=int, default=2)
    parser.add_argument('--right-run')
    parser.add_argument('--tick', type=int)
    parser.add_argument('--phase', choices=('load', 'play'))
    parser.add_argument('--limit', type=int, default=40)
    parser.add_argument('--first', action='store_true', help='the first tick the shared list differs, and the stacks that did it')
    parser.add_argument('--rows', action='store_true', help='RUNTIME_IDS_ROW: the first tick whose shared allocations differ')
    parser.add_argument('--from', dest='start', type=int, default=0, help='with --rows: compare from this tick on')
    args = parser.parse_args()
    if args.rows:
        left, left_keys = read_rows(log_of(args.run, args.left))
        right, right_keys = read_rows(log_of(args.right_run or args.run, args.right))
        if not left or not right:
            raise SystemExit(f'no RUNTIME_IDS_ROW lines (left {len(left)}, right {len(right)}): native_gate_runtime_ids_rows off?')
        found = first_row_difference(left, right, args.start)
        if found is None:
            print(f'rows agree on every tick from {args.start}: {len(left)}/{len(right)} rows, last tick {max(left)}/{max(right)}')
            return
        tick, a, b, keys = found
        print(f'first differing row at tick {tick}')
        for side, row in (('left', a), ('right', b)):
            print(f'  {side}: ' + (f'next={row[0]} count={row[1]} threads={row[2]} keys_other={row[4]}' if row else 'no row'))
        for key, count_a, count_b in keys[:args.limit]:
            print(f'  {key} {count_a:4d} {count_b:4d} {left_keys.get(key) or right_keys.get(key) or "(not in a caller table yet)"}')
        return
    left = read_table(log_of(args.run, args.left))
    right = read_table(log_of(args.right_run or args.run, args.right))
    common = sorted(set(left) & set(right))
    if not common:
        raise SystemExit(f'no tick with RUNTIME_IDS_CALLER rows on both clients (left {sorted(left)}, right {sorted(right)})')
    if args.first:
        tick, agreed, rows = first_difference(left, right)
        if tick is None:
            print(f'shared list agrees on all {len(common)} ticks logged on both clients, through {agreed}')
            return
        print(f'first difference in the shared list at tick {tick}; last agreed tick {agreed}; {len(rows)} rows differ')
        for difference, (phase, thread, route, frames), count_a, count_b in rows[:args.limit]:
            print(f'{difference:+6d} {count_a:8d} {count_b:8d} {phase} {thread} {route} {frames}')
        return
    tick = args.tick if args.tick is not None else common[-1]
    if tick not in common:
        raise SystemExit(f'tick {tick} is not logged on both clients; ticks {common}')
    a, b = left[tick], right[tick]
    if args.phase:
        a = {k: v for k, v in a.items() if k[0] == args.phase}
        b = {k: v for k, v in b.items() if k[0] == args.phase}
    print(f'tick {tick}: rows {len(a)}/{len(b)}, allocations {sum(a.values())}/{sum(b.values())}, difference {sum(a.values()) - sum(b.values())}; ticks logged {common}')
    rows = compare(a, b)
    print(f'{len(rows)} rows differ')
    for difference, (phase, thread, route, frames), count_a, count_b in rows[:args.limit]:
        print(f'{difference:+6d} {count_a:8d} {count_b:8d} {phase} {thread} {route} {frames}')


if __name__ == '__main__':
    main()
