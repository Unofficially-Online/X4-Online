"""Compare two X4 saves of the same tick (x4ls checkpoints) and name the components that differ.

  python tools/dev/checkpoint_diff.py A.xml.gz B.xml.gz [--limit 40]

X4 writes one element per line, so a line diff (GNU diff on the decompressed files) finds every
difference; each hunk is reported with the chain of <component> elements that encloses it on side A
(class, macro, id). The <info> header (save date, play time, guid) differs between any two saves and
is skipped. By default only the world part is compared (everything above <script>): the script
values below it are pools keyed by process-local allocation ids (string refs, lists, positions) that
differ between two clients in sync; --all compares them too. Consecutive <listener/> lines are
compared as sets (the engine keeps them in pointer order), and <part> blocks holding only animation
index parameters (thrusters, flames: render state) are left out. Checkpoints of two clients in sync are
identical in the world part; the first hunks of a pair that is not are where the world state parts.
"""
import argparse
import gzip
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

COMPONENT = re.compile(r'<component\b([^>]*?)(/?)>')
ATTR = re.compile(r'(\w+)="([^"]*)"')
HUNK = re.compile(r'^(\d+)(?:,(\d+))?([acd])(\d+)(?:,(\d+))?$')


UNORDERED = re.compile(rb'^\s*<(listener)\b[^>]*/>\s*$')  # kept in pointer order by the engine: compare as sets
PART_OPEN = re.compile(rb'^\s*<part ref="[^"]*">\s*$')
ANIMATION = re.compile(rb'^\s*<parameter value="[^"]*" name="index" type="float"/>\s*$')


def without_animation(lines):
    """Drop <part> blocks that hold only animation index parameters (thrusters, flames: what a client rendered)."""
    lines = iter(lines)
    for line in lines:
        if not PART_OPEN.match(line):
            yield line
            continue
        block = [line]
        for inner in lines:
            block.append(inner)
            if not ANIMATION.match(inner):
                break
        if block[-1].strip() == b'</part>' and all(ANIMATION.match(x) for x in block[1:-1]):
            continue
        yield from block


def decompress(src, dst, world_only=True):
    """Decompress, sorting each run of consecutive UNORDERED lines (same tag) so that their order does not count.
    world_only stops at <script>: the script values below it are pools keyed by process-local allocation ids."""
    with gzip.open(src, 'rb') as f, open(dst, 'wb') as out:
        run, tag = [], None
        for line in without_animation(f):
            if world_only and line.strip() == b'<script>':
                break
            m = UNORDERED.match(line)
            if m and (tag is None or m.group(1) == tag):
                run.append(line)
                tag = m.group(1)
                continue
            if run:
                out.writelines(sorted(run))
                run, tag = [], None
                m = UNORDERED.match(line)
                if m:
                    run, tag = [line], m.group(1)
                    continue
            out.write(line)
        out.writelines(sorted(run))


def info_end(path):
    """The line number after </info> (1-based), so that header differences are ignored."""
    with open(path, 'rb') as f:
        for number, line in enumerate(f, 1):
            if line.strip() == b'</info>':
                return number + 1
            if number > 10000:
                break
    return 1


def hunks(a, b):
    """(first, last, lines_a, lines_b) per diff hunk, line numbers of side A."""
    proc = subprocess.Popen(['diff', '--speed-large-files', str(a), str(b)], stdout=subprocess.PIPE, text=True,
                            errors='replace')
    current = None
    for line in proc.stdout:
        m = HUNK.match(line.rstrip('\n'))
        if m:
            if current:
                yield current
            first = int(m.group(1))
            last = int(m.group(2) or first)
            current = (first, last if m.group(3) != 'a' else first, [], [])
        elif current and line.startswith('< '):
            current[2].append(line[2:].rstrip('\n'))
        elif current and line.startswith('> '):
            current[3].append(line[2:].rstrip('\n'))
    if current:
        yield current
    proc.wait()
    if proc.returncode > 1:
        raise SystemExit(f'diff failed ({proc.returncode})')


def describe(attrs):
    a = dict(ATTR.findall(attrs))
    return f"{a.get('class', '?')}:{a.get('macro', '')}:{a.get('id', '')}"


def contexts(path, lines):
    """For each wanted line number (ascending), the enclosing component chain on side A."""
    wanted = iter(lines)
    target = next(wanted, None)
    stack = []
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for number, line in enumerate(f, 1):
            while target is not None and target == number:
                yield number, list(stack)
                target = next(wanted, None)
            if target is None:
                return
            m = COMPONENT.search(line)
            if m and not m.group(2):
                stack.append(describe(m.group(1)))
            elif '</component>' in line and stack:
                stack.pop()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('a', type=Path)
    p.add_argument('b', type=Path)
    p.add_argument('--limit', type=int, default=40, help='hunks to report')
    p.add_argument('--all', action='store_true', help='also compare the script section below <script>')
    args = p.parse_args(argv)
    with tempfile.TemporaryDirectory() as tmp:
        a, b = Path(tmp) / 'a.xml', Path(tmp) / 'b.xml'
        decompress(args.a, a, not args.all)
        decompress(args.b, b, not args.all)
        skip = info_end(a)
        found = [h for h in hunks(a, b) if h[1] >= skip]
        print(f"{len(found)} differing hunks below the <info> header{'' if args.all else ', above <script>'}")
        shown = found[:args.limit]
        where = dict(contexts(a, sorted({h[0] for h in shown})))
        for first, last, left, right in shown:
            chain = where.get(first, [])
            print(f'--- line {first}' + (f'-{last}' if last != first else '') + ': ' + (' > '.join(chain[-3:]) or '(no component)'))
            for line in left[:4]:
                print(f'  A {line.strip()[:220]}')
            for line in right[:4]:
                print(f'  B {line.strip()[:220]}')
    return 0 if not found else 1


if __name__ == '__main__':
    raise SystemExit(main())
