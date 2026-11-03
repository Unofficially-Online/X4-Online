"""Compare two X4 saves element by element: does a loaded save still equal the world it was written from (D14)?

  python tools/dev/save_diff.py LEFT.xml.gz RIGHT.xml.gz [--limit 40] [--ignore TAG.ATTR ...] [--out result.json]

Two saves of the same tick from two processes (the server's fork save and a joiner's after loading it, or both after k
more steps) are compared without runtime IDs: both files are streamed in step (gzip or plain XML), the same element
sequence is expected, and each element's attributes and text are compared. Counts per (tag, attribute), the first
--limit differences with their path (tag[id] from the root) and, at the first structural difference (another tag, or
one file ending first), where the walk stopped. Ignored: the <signature> element (signed per file) and the attributes
named with --ignore (default: the save header's date and name, which a fork save sets itself). Read only.
"""
import argparse
import collections
import gzip
import itertools
import json
from pathlib import Path
import xml.etree.ElementTree as ElementTree

DEFAULT_IGNORE = ('save.date', 'save.name')


def open_save(path):
    with open(path, 'rb') as probe:
        gz = probe.read(2) == b'\x1f\x8b'
    return gzip.open(path, 'rb') if gz else open(path, 'rb')


def events(path):
    """(event, tag, attributes, text, path) per element start and end, skipping <signature> and everything in it."""
    stack, skip = [], 0
    with open_save(path) as stream:
        for event, element in ElementTree.iterparse(stream, events=('start', 'end')):
            if event == 'start':
                if skip or element.tag == 'signature':
                    skip += 1
                    continue
                stack.append(element.tag + (f"[{element.attrib['id']}]" if 'id' in element.attrib else ''))
                yield 'start', element.tag, dict(element.attrib), None, '/'.join(stack)
            else:
                if skip:
                    skip -= 1
                    element.clear()
                    continue
                yield 'end', element.tag, None, (element.text or '').strip(), '/'.join(stack)
                stack.pop()
                element.clear()


def compare(left, right, ignore=DEFAULT_IGNORE, limit=40):
    ignore = set(ignore)
    counts, samples = collections.Counter(), []
    elements, structure = 0, None
    marker = object()
    for a, b in itertools.zip_longest(events(left), events(right), fillvalue=marker):
        if a is marker or b is marker:
            structure = {'at': (a if b is marker else b)[4], 'reason': 'left ends first' if a is marker else 'right ends first'}
            break
        if a[0] != b[0] or a[1] != b[1]:
            structure = {'at': a[4], 'other': b[4], 'reason': f'{a[0]} {a[1]} against {b[0]} {b[1]}'}
            break
        if a[0] == 'start':
            elements += 1
            for name in sorted(set(a[2]) | set(b[2])):
                if f'{a[1]}.{name}' in ignore or a[2].get(name) == b[2].get(name):
                    continue
                counts[f'{a[1]}.{name}'] += 1
                if len(samples) < limit:
                    samples.append({'path': a[4], 'attribute': name, 'left': a[2].get(name), 'right': b[2].get(name)})
        elif a[3] != b[3]:
            counts[f'{a[1]}#text'] += 1
            if len(samples) < limit:
                samples.append({'path': a[4], 'attribute': '#text', 'left': a[3][:80], 'right': b[3][:80]})
    return {'left': str(left), 'right': str(right), 'elements': elements, 'differing_values': sum(counts.values()),
            'by_attribute': dict(counts.most_common()), 'samples': samples, 'structure': structure,
            'equal': not counts and structure is None}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('left', type=Path)
    p.add_argument('right', type=Path)
    p.add_argument('--limit', type=int, default=40)
    p.add_argument('--ignore', nargs='*', default=list(DEFAULT_IGNORE), metavar='TAG.ATTR')
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    result = compare(args.left, args.right, args.ignore, args.limit)
    print(f"{result['elements']} elements, {result['differing_values']} differing values, equal={result['equal']}")
    for name, count in list(result['by_attribute'].items())[:20]:
        print(f'  {count:8} {name}')
    if result['structure']:
        print(f"structure: {result['structure']}")
    for sample in result['samples'][:10]:
        print(f"  {sample['path']} {sample['attribute']}: {sample['left']} | {sample['right']}")
    if args.out:
        args.out.write_text(json.dumps(result, indent=1) + '\n')
    raise SystemExit(0 if result['equal'] else 1)


if __name__ == '__main__':
    main()
