"""Routes for native_gate_phantom_players, picked from a session save.

A phantom player is one more view point of the session (lockstep_view_points.h): it rides one object of its route and
moves to the next every `hop` ticks. This tool reads a save (.xml or .xml.gz), lists the ships that fly free in space
(not docked) with the sector they are in, and gives each phantom player a route of `--stops` ships in different
sectors, so that its hops take it in and out of sectors. The choice follows `--seed` alone: the same save and
arguments give the same routes.

  python tools/dev/pick_phantom_players.py SAVE --players 10 --stops 4 [--hop 300] [--stagger 37] [--classes ship_l,ship_xl]
      [--avoid-sector MACRO ...] [--seed 1]

Prints the option as one Lua value (for `--set "native_gate_phantom_players=..."`) and, on stderr, where each stop is.
"""
import argparse
import gzip
import random
import re
import sys

OPEN = re.compile(r'<component\b([^>]*?)(/?)>')
ATTR = re.compile(r'(\w+)="([^"]*)"')


def ships_by_sector(path, classes):
    """{sector macro: [(saved id, class, macro, code)]} of the ships whose parent chain is zone, sector (free in space)."""
    opener = gzip.open if path.endswith('.gz') else open
    stack, out = [], {}
    with opener(path, 'rt', encoding='utf-8', errors='replace') as handle:
        for line in handle:
            if '<component' not in line and '</component>' not in line:
                continue
            for match in re.finditer(r'<component\b[^>]*>|</component>', line):
                text = match.group(0)
                if text == '</component>':
                    if stack:
                        stack.pop()
                    continue
                attrs = dict(ATTR.findall(text))
                kind = attrs.get('class', '')
                if kind in classes and attrs.get('connection') == 'space' and stack and stack[-1][0] == 'zone':
                    sector = next((macro for cls, macro in reversed(stack) if cls == 'sector'), None)
                    saved = re.fullmatch(r'\[0x([0-9a-fA-F]+)\]', attrs.get('id', ''))
                    if sector and saved:
                        out.setdefault(sector, []).append((int(saved.group(1), 16), kind, attrs.get('macro', ''), attrs.get('code', '')))
                if not text.endswith('/>'):
                    stack.append((kind, attrs.get('macro', '')))
    return out


def pick(sectors, players, stops, seed, avoid):
    """players routes of stops ships, a route's ships in different sectors, no ship twice; sectors used as evenly as they last."""
    rng = random.Random(seed)
    names = sorted(name for name in sectors if name not in avoid)
    pools = {name: sorted(sectors[name]) for name in names}
    for name in names:
        rng.shuffle(pools[name])
    rng.shuffle(names)
    routes, at = [], 0
    for _ in range(players):
        route, used = [], set()
        for _ in range(len(names) * 2):
            if len(route) == stops:
                break
            name = names[at % len(names)]
            at += 1
            if name in used or not pools[name]:
                continue
            route.append((name,) + pools[name].pop())
            used.add(name)
        if len(route) < stops:
            raise SystemExit('not enough sectors with free ships for %d players of %d stops' % (players, stops))
        routes.append(route)
    return routes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('save')
    parser.add_argument('--players', type=int, default=10)
    parser.add_argument('--stops', type=int, default=4)
    parser.add_argument('--hop', type=int, default=300)
    parser.add_argument('--stagger', type=int, default=37)
    parser.add_argument('--classes', default='ship_l,ship_xl')
    parser.add_argument('--avoid-sector', action='append', default=[])
    parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.players <= 16 or not 1 <= args.stops <= 8:
        raise SystemExit('1..16 players, 1..8 stops')
    sectors = ships_by_sector(args.save, set(args.classes.split(',')))
    routes = pick(sectors, args.players, args.stops, args.seed, set(args.avoid_sector))
    for k, route in enumerate(routes, 1):
        print('phantom player %d: %s' % (k, ', '.join('%x %s %s in %s' % (saved, code, macro, sector) for sector, saved, _, macro, code in route)),
              file=sys.stderr)
    print('sectors with free ships: %d, ships: %d' % (len(sectors), sum(len(v) for v in sectors.values())), file=sys.stderr)
    body = ','.join('{' + ','.join("'%016x'" % saved for _, saved, _, _, _ in route) + '}' for route in routes)
    print('{hop=%d,stagger=%d,routes={%s}}' % (args.hop, args.stagger, body))


if __name__ == '__main__':
    main()
