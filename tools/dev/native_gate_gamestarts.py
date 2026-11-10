"""Stock X4 game starts: catalogue, classes and per-player start templates (read-only, no installation, no launch).

Reads the game's own libraries and MD scripts at session setup (never copied into git): libraries/gamestarts.xml of
the base game with every stock extension's diff applied, the god.xml entries that only exist for one start, and the
MD cues that run only for one start (cue module="<id>" or a player.module == '<id>' test). Each start is catalogued
by what it touches and put into one class (reports/LOCKSTEP-STARTS-20261101.txt):

  a  per-player safe       only player-level parts (ship and loadout, money, inventory, blueprints, research,
                           position, reputation toward factions)
  b  session base only     at least one world-level part (pre-placed or player-owned stations, plot cues, start MD
                           with world effects, relations between NPC factions, universe settings, another galaxy).
                           It can be the session's own start (the save every client loads), never a second player's.
  c  per-player, rebuilt   world-level parts that this code rebuilds deterministically, each listed and applied by
                           the identity plan at the same tick on every client (today: the start MD's map uncover,
                           a literal sector list passed to the stock library action)
  -  refused               not a playable stock start here (tutorials, Timelines scenarios, debug and test starts,
                           the station designer, another galaxy)

A start is never applied in part without saying so: a per-player start whose class is b or refused, or whose parts
this code does not know, is refused at session setup, naming the start and the parts that block it. MD effects of a
start ran only for the session base (new-game MD keys on player.module, which is the base start on every client); for
a per-player start they are either rebuilt (class c parts) or refused.

Ship only (world=True; x4ls new-session --omit K=world): a class b start whose every part this reader knows may give
slot k its player-level parts alone - the ship with its loadout at the start's place, the money and the rest of the
kit - while its world-level parts (its plot, stations, faction relations) are left out and listed in the spec. The
session's world is then the base start's; the player has that start's ship and place, not its story.

    python tools/dev/native_gate_gamestarts.py catalog --libraries <gamelibs root> --scripts <gamescripts root>
    python tools/dev/native_gate_gamestarts.py template x4ep1_gamestart_fight --libraries ... --scripts ...

Roots hold one directory per extension (base, ego_dlc_*), each with libraries/ resp. md/ (the extracted and
resolved catalog layout); X4LAN_GAME_LIBRARIES / X4LAN_GAME_SCRIPTS name them when the options are absent.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

BASE = 'base'
# Stock extensions in load order (alphabetical: none of them edits another's start).
STOCK_EXTENSIONS = ('ego_dlc_boron', 'ego_dlc_mini_01', 'ego_dlc_mini_02', 'ego_dlc_pirate', 'ego_dlc_split',
                    'ego_dlc_terran', 'ego_dlc_timelines')
MAIN_GALAXY = 'xu_ep2_universe_macro'
CLASSES = ('a', 'b', 'c', '-')
# Tags of starts that are not a game in the shared universe (gamestart tags="...").
REFUSED_TAGS = {'tutorial': 'tutorial', 'nosave': 'no saving (tutorial/hub/designer)', 'timelineshub': 'Timelines hub',
                'stationdesigner': 'station designer'}
# Observed, not in the libraries: starts that open with a fullscreen plot cutscene the snapshot capture cannot pass
# (docs/LOCKSTEP-SCENARIOS.txt "OTHER PLAYER STARTS").
CUTSCENE_STARTS = {'x4ep1_gamestart_boron1', 'x4ep1_gamestart_pirate1'}
# MD actions that only build cue structure or log; anything else in a start-keyed cue is an effect.
MD_STRUCTURE = {'conditions', 'actions', 'cues', 'cue', 'library', 'delay', 'do_if', 'do_elseif', 'do_else', 'do_all',
                'do_while', 'do_any', 'debug_text', 'set_value', 'remove_value', 'param', 'params', 'patch', 'check_value',
                'check_any', 'check_all', 'event_cue_signalled', 'event_game_started', 'event_game_loaded', 'force',
                'checktime', 'reset_cue', 'cancel_cue', 'signal_cue', 'signal_cue_instantly', 'return', 'break', 'continue'}
UNCOVER = 'md.LIB_Generic.UncoverMap_SectorsAndGates'
UNCOVER_PARAMS = {'InitialMap', 'UncoverStationFaction', 'UncoverShipyardsAndWharfs'}
MACRO_REF = re.compile(r'macro\.([a-z][a-z0-9_]*_macro)')
FACTION_REF = re.compile(r'faction\.([a-z][a-z0-9_]*)')
MODULE_TEST = re.compile(r"player\.module\s*==\s*'([a-z0-9_]+)'")
SPACESUIT = re.compile(r'_spacesuit_')
# Where each player-level part of a start can go for slot k >= 2 (slot 1 plays the session base start, the save).
#   money       factions: slot k's own account (set); coop: added to the one shared wallet
#   reputation  factions: slot k's faction (after the mirror); coop: one reputation set (the base start's) -> omit
#   blueprints, research, known (map uncover)
#               coop: the one player library and map; factions: X4 keeps them for the player faction only -> omit
#   inventory   the player entity P_k of slot k is not reachable from MD (lockstep_pilot_slots.h) -> omit
#   ship, place a hull with the start's macro and loadout, created at its start position in the session save
#               (the capture), because pilot slots need saved IDs fixed before the load
APPLIES = {'factions': {'reputation'}, 'coop': {'blueprints', 'research', 'known'}}
OMITTABLE = ('blueprints', 'inventory', 'known', 'reputation', 'research')
SAME_START_OFFSET = 300   # metres along x between hulls of slots that take the same start


class CatalogError(ValueError):
    """Stock data this reader does not understand: never guessed around."""


# ---------------------------------------------------------------- libraries and diffs

def parse(path):
    """XML with comments kept (stock diffs replace placeholder comments)."""
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    try:
        return ET.parse(path, parser).getroot()
    except ET.ParseError as error:
        raise CatalogError(f'{path}: {error}') from None


STEP = re.compile(r"""^(?P<name>@?[A-Za-z_][\w.-]*|comment\(\))(?P<preds>(?:\[[^\]]*\])*)$""")
PRED = re.compile(r"""\[\s*(?:@(?P<attr>[\w.-]+)\s*=\s*(?P<q>['"])(?P<value>.*?)(?P=q)|\.\s*=\s*(?P<q2>['"])(?P<text>.*?)(?P=q2)|(?P<index>\d+))\s*\]""")


def _steps(sel):
    if not sel.startswith('/'):
        raise CatalogError(f'diff selector {sel!r} is not absolute')
    parts, depth, current = [], 0, ''
    for ch in sel[1:]:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == '/' and depth == 0:
            parts.append(current)
            current = ''
        else:
            current += ch
    parts.append(current)
    out = []
    for part in parts:
        m = STEP.match(part)
        if not m:
            raise CatalogError(f'diff selector step {part!r} of {sel!r} is not supported')
        preds = []
        rest = m['preds']
        while rest:
            p = PRED.match(rest)
            if not p:
                raise CatalogError(f'diff selector predicate {rest!r} of {sel!r} is not supported')
            preds.append(('attr', p['attr'], p['value']) if p['attr'] else ('text', None, p['text']) if p['q2'] else ('index', None, int(p['index'])))
            rest = rest[p.end():]
        out.append((m['name'], preds))
    return out


def select(root, sel):
    """Nodes of a (stock subset) diff selector: element steps with [@a='v'], [n] and comment()[. = 'text'],
    a final @attribute. Returns (nodes, attribute or None)."""
    steps = _steps(sel)
    name, preds = steps[0]
    if name != root.tag or preds:
        return [], None
    nodes, attribute = [root], None
    for index, (name, preds) in enumerate(steps[1:], 1):
        if name.startswith('@'):
            if index != len(steps) - 1 or preds:
                raise CatalogError(f'attribute step must be last in {sel!r}')
            return [n for n in nodes if name[1:] in n.attrib], name[1:]
        found = []
        for node in nodes:
            if name == 'comment()':
                kids = [c for c in node if c.tag is ET.Comment]
            else:
                kids = [c for c in node if c.tag == name]
            for kind, attr, value in preds:
                if kind == 'attr':
                    kids = [c for c in kids if c.get(attr) == value]
                elif kind == 'text':
                    kids = [c for c in kids if (c.text or '') == value]
                else:
                    kids = kids[value - 1:value]
            found += kids
        nodes = found
    return nodes, attribute


def _parent_map(root):
    return {child: parent for parent in root.iter() for child in parent}


def apply_diff(root, diff, source='diff'):
    """Apply a stock <diff> (add/replace/remove, the subset stock gamestarts and god files use) in place.
    A selector that matches nothing fails unless the operation is silent, as the game logs it."""
    if diff.tag != 'diff':
        raise CatalogError(f'{source}: not a diff')
    for op in diff:
        if op.tag is ET.Comment:
            continue
        sel = op.get('sel')
        if op.tag not in ('add', 'replace', 'remove') or not sel:
            raise CatalogError(f'{source}: unsupported diff operation {op.tag} {sel!r}')
        nodes, attribute = select(root, sel)
        if not nodes:
            if op.get('silent') == 'true':
                continue
            raise CatalogError(f'{source}: {op.tag} {sel!r} matches nothing')
        if len(nodes) > 1:
            raise CatalogError(f'{source}: {op.tag} {sel!r} matches {len(nodes)} nodes')
        node = nodes[0]
        parents = _parent_map(root)
        children = [c for c in op]
        if attribute is not None:
            if op.tag == 'replace':
                node.set(attribute, op.text or '')
            elif op.tag == 'remove':
                del node.attrib[attribute]
            else:
                raise CatalogError(f'{source}: add to attribute {sel!r}')
            continue
        if op.get('type'):
            if op.tag != 'add' or not op.get('type').startswith('@'):
                raise CatalogError(f'{source}: unsupported diff type {op.get("type")!r}')
            node.set(op.get('type')[1:], op.text or '')
            continue
        if op.tag == 'remove':
            parents[node].remove(node)
        elif op.tag == 'replace':
            parent = parents[node]
            at = list(parent).index(node)
            parent.remove(node)
            for offset, child in enumerate(children):
                parent.insert(at + offset, child)
        else:
            pos = op.get('pos', 'append')
            if pos == 'append':
                node.extend(children)
            elif pos == 'prepend':
                for offset, child in enumerate(children):
                    node.insert(offset, child)
            elif pos in ('after', 'before'):
                parent = parents[node]
                at = list(parent).index(node) + (1 if pos == 'after' else 0)
                for offset, child in enumerate(children):
                    parent.insert(at + offset, child)
            else:
                raise CatalogError(f'{source}: unsupported add position {pos!r}')
    return root


def extensions(root, extra=()):
    """Present stock extension directories under a resolved root, base first."""
    root = Path(root)
    if not (root / BASE).is_dir():
        raise CatalogError(f'{root} has no {BASE}/ directory (expected the resolved per-extension layout)')
    names = [BASE] + [e for e in STOCK_EXTENSIONS + tuple(extra) if (root / e).is_dir()]
    return names


def load_library(libraries, name, extra=()):
    """The base library file with every present stock extension's diff applied, and the list of sources."""
    root, sources = None, []
    for ext in extensions(libraries, extra):
        path = Path(libraries) / ext / 'libraries' / name
        if not path.is_file():
            continue
        tree = parse(path)
        if root is None:
            if ext != BASE:
                raise CatalogError(f'{name}: {ext} patches a library the base game does not have')
            root = tree
        elif tree.tag == 'diff':
            apply_diff(root, tree, f'{ext}/libraries/{name}')
        else:
            raise CatalogError(f'{ext}/libraries/{name} is not a diff')
        sources.append(ext)
    if root is None:
        raise CatalogError(f'{libraries}: no base libraries/{name}')
    return root, sources


def god_entries(libraries, extra=()):
    """start id -> world parts the galaxy generator (god.xml) creates only for that start. Each file is scanned
    as it is (a diff adds its stations inside <add>); no diff needs applying for that."""
    out = {}
    for ext in extensions(libraries, extra):
        path = Path(libraries) / ext / 'libraries' / 'god.xml'
        if not path.is_file():
            continue
        root = parse(path)
        parents = _parent_map(root)
        for node in root.iter():
            if node.tag is ET.Comment:
                continue
            start = node.get('gamestart') if node.tag == 'quota' else node.get('ref') if node.tag == 'gamestart' else None
            if not start:
                continue
            owner = node
            while owner in parents and owner.tag in ('quota', 'quotas', 'location'):
                owner = parents[owner]
            if node.tag == 'gamestart':
                parts = [f'god {c.tag} {c.get("id") or c.get("macro") or ""}'.strip() for c in node.iter()
                         if c is not node and c.tag in ('station', 'object', 'ship', 'product')]
                for part in parts or ['god gamestart block']:
                    out.setdefault(start, []).append({'part': part, 'source': f'{ext}/libraries/god.xml'})
                continue
            desc = owner.tag
            for key in ('id', 'owner', 'type', 'race'):
                if owner.get(key):
                    desc += f' {key}={owner.get(key)}'
            out.setdefault(start, []).append({'part': f'god {desc}', 'source': f'{ext}/libraries/god.xml'})
    return out


# ---------------------------------------------------------------- MD keyed on the start

def _cue_path(parents, node):
    names = []
    while node is not None:
        if node.tag in ('cue', 'library') and node.get('name'):
            names.append(node.get('name'))
        node = parents.get(node)
    return '.'.join(reversed(names))


def _effects(node):
    """Action tags in a cue subtree that are not structure or logging (sorted, unique)."""
    out = set()
    for e in node.iter():
        if not isinstance(e.tag, str) or e.tag in MD_STRUCTURE or e.tag.startswith('event_') or e.tag.startswith('check_'):
            continue
        out.add(e.tag)
    return out


def _uncover(cue):
    """A start cue whose only effect is the stock map uncover with literal lists: the explicit part, else None."""
    effects = _effects(cue)
    if effects - {'run_actions'}:
        return None
    runs = [e for e in cue.iter('run_actions')]
    if len(runs) != 1 or runs[0].get('ref') != UNCOVER:
        return None
    values = {}
    for e in cue.iter('set_value'):
        values[e.get('name')] = e.get('exact') or ''
    params = {}
    for p in runs[0].findall('param'):
        name, value = p.get('name'), p.get('value') or ''
        if name not in UNCOVER_PARAMS:
            return None
        params[name] = values.get(value, value) if value.startswith('$') else value
    sectors = MACRO_REF.findall(params.get('InitialMap', ''))
    if not sectors or len(params.get('InitialMap', '').split(',')) != len(sectors):
        return None
    station = params.get('UncoverStationFaction')
    faction = None
    if station is not None:
        m = FACTION_REF.fullmatch(station.strip())
        if not m:
            return None
        faction = m[1]
    yards = params.get('UncoverShipyardsAndWharfs')
    if yards not in (None, 'true', 'false'):
        return None
    ordered = []
    for s in sectors:
        if s not in ordered:
            ordered.append(s)
    return {'sectors': ordered, 'station_faction': faction, 'shipyards': yards == 'true'}


def md_keys(scripts, extra=()):
    """start id -> cues (MD) or scripts (AI) that run only for that start: {source, cue, how, effects, uncover}.
    Keyed means a cue with module="<id>" (the whole subtree) or a player.module == '<id>' test (its cue)."""
    out = {}
    for ext in extensions(scripts, extra):
        for kind in ('md', 'aiscripts'):
            directory = Path(scripts) / ext / kind
            for path in sorted(directory.glob('*.xml')) if directory.is_dir() else ():
                root = parse(path)
                if root.tag not in ('mdscript', 'aiscript'):
                    continue
                for start, entry in _keyed(root):
                    out.setdefault(start, []).append(dict(entry, source=f'{ext}/{kind}/{path.name}'))
    return out


def _keyed(root):
    parents = _parent_map(root)
    keyed, seen = [], set()
    for node in root.iter():
        if not isinstance(node.tag, str):
            continue
        module = node.get('module') if node.tag in ('cue', 'mdscript') else None
        if module and module not in ('true', 'none'):
            keyed.append((module, node, 'module'))
        for value in node.attrib.values():
            for start in MODULE_TEST.findall(value):
                cue = node
                while cue is not None and cue.tag not in ('cue', 'library'):
                    cue = parents.get(cue)
                keyed.append((start, cue if cue is not None else root, 'test'))
    for start, cue, how in keyed:
        if (start, id(cue)) in seen:
            continue
        seen.add((start, id(cue)))
        yield start, {'cue': _cue_path(parents, cue) or root.get('name'), 'how': how, 'effects': sorted(_effects(cue)),
                      'uncover': _uncover(cue) if how == 'module' else None}


# ---------------------------------------------------------------- one start

def _num(text, what):
    try:
        value = float(text)
    except (TypeError, ValueError):
        raise CatalogError(f'{what}: {text!r} is not a number') from None
    return value


def _ship(ship):
    loadout = ship.find('loadout')
    equipment, ammunition, software, thrusters, extra = [], {}, [], [], []
    if loadout is not None:
        if loadout.get('ref'):
            extra.append(f"loadout ref {loadout.get('ref')}")
        for child in loadout:
            if not isinstance(child.tag, str):
                continue
            if child.tag == 'macros':
                for m in child:
                    if isinstance(m.tag, str):
                        if m.tag not in ('engine', 'weapon', 'shield', 'turret'):
                            raise CatalogError(f'loadout macro kind {m.tag!r}')
                        equipment.append({'kind': m.tag, 'macro': m.get('macro'), 'path': m.get('path')})
            elif child.tag == 'ammunition':
                for a in child:
                    if isinstance(a.tag, str):
                        ammunition[a.get('macro')] = ammunition.get(a.get('macro'), 0) + int(a.get('exact', '1'))
            elif child.tag == 'software':
                software += [s.get('ware') for s in child if isinstance(s.tag, str)]
            elif child.tag == 'virtualmacros':
                thrusters += [t.get('macro') for t in child if isinstance(t.tag, str)]
            else:
                extra.append(f'loadout {child.tag}')
    unknown = [c.tag for c in ship if isinstance(c.tag, str) and c.tag != 'loadout']
    return {'macro': ship.get('macro'), 'equipment': equipment, 'ammunition': ammunition, 'software': software,
            'thrusters': thrusters, 'unsupported': extra + [f'ship {t}' for t in unknown]}


def describe(start, god=(), md=()):
    """Every part of one start, split into player-level and world-level, plus presentation-only parts."""
    sid = start.get('id')
    rec = {'id': sid, 'tags': sorted((start.get('tags') or '').split()), 'debug': start.get('debug') == 'true',
           'player': {}, 'world': [], 'presentation': [], 'unknown': [], 'refused': []}
    for tag in rec['tags']:
        if tag in REFUSED_TAGS:
            rec['refused'].append(REFUSED_TAGS[tag])
    if rec['debug']:
        rec['refused'].append('debug start')
    if sid.startswith(('test_', 'scenario_')) or sid in ('default', 'thevoid', 'testbed1'):
        rec['refused'].append('test or Timelines scenario start')
    if sid in CUTSCENE_STARTS:
        rec['refused'].append('fullscreen plot cutscene at start (observed)')
    player = rec['player']
    for child in start:
        if not isinstance(child.tag, str):
            continue
        tag = child.tag
        if tag in ('info', 'cutscene', 'intro', 'type', 'loadingscreen', 'extension', 'unlock'):
            rec['presentation'].append(tag)
        elif tag == 'location':
            galaxy = child.get('galaxy')
            if galaxy != MAIN_GALAXY:
                rec['world'].append(f'galaxy {galaxy}')
                rec['refused'].append(f'galaxy {galaxy} (not the shared main galaxy)')
            loc = {'zone': child.get('zone'), 'sector': child.get('sector')}
            pos, rot = child.find('position'), child.find('rotation')
            if pos is not None:
                loc['position'] = [_num(pos.get(a, '0'), f'{sid} position') for a in 'xyz']
            if rot is not None:
                loc['yaw'] = _num(rot.get('yaw', '0'), f'{sid} yaw')
            kind = 'space'
            if child.get('station') or child.get('room'):
                kind = 'docked'
                if child.get('faction') == 'player':
                    rec['world'].append(f"player-owned station {child.get('station')} (location)")
            inner = child.find('player')
            if (inner is not None and inner.get('disembarked') == 'true') or child.find('interior') is not None:
                kind = 'on_foot'
            loc['kind'] = kind
            for key in ('equipmentdock', 'station', 'room'):
                if child.get(key):
                    loc[key] = child.get(key)
            player['location'] = loc
        elif tag == 'player':
            player['money'] = int(_num(child.get('money', '0'), f'{sid} money'))
            player['character'] = child.get('macro')
            for part in child:
                if not isinstance(part.tag, str):
                    continue
                if part.tag == 'ship':
                    player['ship'] = _ship(part)
                    rec['unknown'] += player['ship'].pop('unsupported')
                elif part.tag == 'inventory':
                    inv = {}
                    for w in part:
                        if isinstance(w.tag, str):
                            inv[w.get('ware')] = inv.get(w.get('ware'), 0) + int(w.get('amount', '1'))
                    player['inventory'] = inv
                elif part.tag in ('blueprints', 'research'):
                    player[part.tag] = [w.get('ware') for w in part if isinstance(w.tag, str)]
                elif part.tag == 'encyclopedia':
                    player['encyclopedia'] = [(e.get('type'), e.get('ref')) for e in part if isinstance(e.tag, str)]
                elif part.tag in ('theme', 'timeline'):
                    rec['presentation'].append(f'player {part.tag}')
                elif part.tag == 'knownspace':
                    rec['world'].append('known space (player/knownspace)')
                else:
                    rec['unknown'].append(f'player {part.tag}')
        elif tag == 'universe':
            for part in child:
                if not isinstance(part.tag, str):
                    continue
                if part.tag == 'factions':
                    for sub in part:
                        if not isinstance(sub.tag, str):
                            continue
                        if sub.tag != 'relations':
                            rec['world'].append(f'universe factions {sub.tag}')
                            continue
                        for r in sub:
                            if not isinstance(r.tag, str):
                                continue
                            a, b = r.get('faction'), r.get('otherfaction')
                            value = _num(r.get('relation'), f'{sid} relation')
                            if 'player' in (a, b) and a != b:
                                other = b if a == 'player' else a
                                player.setdefault('reputation', {})[other] = value
                            else:
                                rec['world'].append(f'relation {a}/{b} {r.get("relation")}')
                elif part.tag in ('god', 'jobs', 'masstraffic'):
                    if part.get('enabled') == 'false':
                        rec['world'].append(f'universe {part.tag} disabled')
                    elif part.attrib or len(part):
                        rec['world'].append(f'universe {part.tag} settings')
                else:
                    rec['world'].append(f'universe {part.tag}')
        elif tag in ('custom', 'budget'):
            rec['world'].append(f'custom editor {tag} (stories, known space, people, stations)')
        else:
            rec['unknown'].append(tag)
    for g in god:
        rec['world'].append(f"{g['part']} ({g['source']})")
    rec['rebuild'] = []
    for cue in md:
        where = f"MD {cue['source']} {cue['cue']}"
        if cue['uncover']:
            rec['rebuild'].append({'part': 'map uncover', 'source': where, **cue['uncover']})
        elif cue['effects']:
            rec['world'].append(f"{where} ({cue['how']}): {', '.join(cue['effects'][:8])}{' ...' if len(cue['effects']) > 8 else ''}")
        else:
            rec['presentation'].append(f'{where} (no effect)')
    return rec


def classify(rec):
    """(class, reasons): a/b/c by world parts, '-' for refused starts. Unknown parts are world parts here:
    whatever this reader cannot name cannot be applied per player."""
    if rec['refused']:
        return '-', list(rec['refused'])
    blocking = list(rec['world']) + [f'unknown part {u}' for u in rec['unknown']]
    if blocking:
        return 'b', blocking
    if rec['rebuild']:
        return 'c', [f"rebuilt: {r['part']} ({r['source']})" for r in rec['rebuild']]
    return 'a', []


def per_player_refusal(rec, klass=None, world=False):
    """None when slot k may take this start in a shared session; else the reason (start id and blocking parts).
    world: the ship-only form (module text): a class b start's world-level parts are left out by the caller's own word,
    and a place may be a sector position instead of a zone."""
    klass = klass or classify(rec)[0]
    reasons = classify(rec)[1]
    if klass == '-' or (klass == 'b' and not world):
        return f"start {rec['id']} is class {klass}: " + '; '.join(reasons)
    if klass == 'b' and rec['unknown']:
        return f"start {rec['id']}: parts this reader does not know: " + '; '.join(rec['unknown'])
    loc = rec['player'].get('location', {})
    if loc.get('kind') != 'space':
        return f"start {rec['id']}: player starts {loc.get('kind', 'without a location')} (slot ships start in space)"
    if 'ship' not in rec['player']:
        return f"start {rec['id']}: no player ship"
    if SPACESUIT.search(rec['player']['ship']['macro'] or ''):
        return f"start {rec['id']}: the player starts in a spacesuit (slot ships are ships)"
    if not loc.get('zone') and not (world and loc.get('sector') and loc.get('position')):
        return f"start {rec['id']}: location without a zone" + (' or a sector position' if world else '')
    return None


def session_base_refusal(rec):
    """None when the start can be the session's own start (the snapshot capture: player in a ship in open space of
    the main galaxy, fixture spawned next to it)."""
    klass, reasons = classify(rec)
    if klass == '-':
        return f"start {rec['id']} is refused: " + '; '.join(reasons)
    loc = rec['player'].get('location', {})
    if loc.get('kind') != 'space' or 'ship' not in rec['player']:
        return f"start {rec['id']}: the snapshot capture needs the player in a ship in open space ({loc.get('kind', 'no location')})"
    if SPACESUIT.search(rec['player']['ship']['macro'] or ''):
        return f"start {rec['id']}: the player starts in a spacesuit (the capture's fixture needs a ship)"
    return None


class Catalog:
    """Every stock start of one game data set."""

    def __init__(self, libraries, scripts, extra=()):
        self.libraries, self.scripts = Path(libraries), Path(scripts)
        root, self.sources = load_library(libraries, 'gamestarts.xml', extra)
        self.god = god_entries(libraries, extra)
        self.md = md_keys(scripts, extra)
        self.starts = {}
        for g in root.findall('gamestart'):
            sid = g.get('id')
            if not sid or sid in self.starts:
                raise CatalogError(f'gamestart id {sid!r} missing or repeated')
            self.starts[sid] = g
        self.records = {sid: describe(g, self.god.get(sid, ()), self.md.get(sid, ())) for sid, g in self.starts.items()}
        orphans = sorted(set(self.god) - set(self.starts))
        self.orphans = orphans   # god.xml entries keyed to a start id that does not exist (logged by the report)

    def record(self, sid):
        if sid not in self.records:
            raise CatalogError(f'unknown gamestart {sid!r}')
        return self.records[sid]

    def rows(self):
        out = []
        for sid, rec in sorted(self.records.items()):
            klass, reasons = classify(rec)
            out.append({'id': sid, 'class': klass, 'reasons': reasons, 'base': session_base_refusal(rec),
                        'per_player': per_player_refusal(rec, klass), 'ship_only': per_player_refusal(rec, klass, world=True),
                        'player': rec['player'], 'rebuild': rec['rebuild'],
                        'presentation': rec['presentation']})
        return out


def zone_sector(zone):
    """Sector macro of a stock zone macro (zoneNNN_cluster_X_sectorY_macro -> cluster_X_sectorY_macro)."""
    m = re.fullmatch(r'zone\d+_(cluster_\d+_sector\d+_macro)', zone or '')
    if not m:
        raise CatalogError(f'zone {zone!r} does not name its sector')
    return m[1]


def template(catalog, sid, world=False):
    """The resolved per-player start of a class a/c start (values only; refuses everything else). world: the ship-only
    form of a class b start; the world-level parts it leaves out are listed in 'world_omitted'."""
    rec = catalog.record(sid)
    why = per_player_refusal(rec, world=world)
    if why:
        raise CatalogError(why)
    p = rec['player']
    loc = p['location']
    x, y, z = (int(round(v)) for v in loc.get('position', (0, 0, 0)))
    yaw = int(round(loc.get('yaw', 0))) % 360
    ship = p['ship']
    known = [dict(r) for r in rec['rebuild'] if r['part'] == 'map uncover']
    if len(known) > 1 or len(known) != len(rec['rebuild']):
        raise CatalogError(f'start {sid}: {len(rec["rebuild"])} rebuilt parts, this code rebuilds one map uncover')
    return {'gamestart': sid, 'class': classify(rec)[0],
            'ship': {'macro': ship['macro'], 'equipment': [dict(e) for e in ship['equipment']],
                     'ammunition': dict(sorted(ship['ammunition'].items())), 'software': sorted(ship['software']),
                     'thrusters': list(ship['thrusters'])},
            'place': {'zone': loc.get('zone') or None, 'sector': zone_sector(loc['zone']) if loc.get('zone') else loc['sector'],
                      'x': x, 'y': y, 'z': z, 'yaw': yaw - 360 if yaw > 180 else yaw},
            'world_omitted': list(rec['world']) if classify(rec)[0] == 'b' else [],
            'money': p.get('money', 0), 'inventory': dict(sorted(p.get('inventory', {}).items())),
            'blueprints': sorted(set(p.get('blueprints', []))), 'research': sorted(set(p.get('research', []))),
            'reputation': dict(sorted(p.get('reputation', {}).items())),
            'known': known[0] if known else None,
            'presentation': sorted(set(rec['presentation']) | ({'player encyclopedia'} if p.get('encyclopedia') else set()))}


def slot_kit(tpl, mode, slot, omit=()):
    """The identity plan kit of slot k (2..N) from a resolved template (native_gate_player_identity kits). Every
    non-empty part that cannot be applied for this mode must be named in omit; otherwise the start is refused,
    naming the slot, the start and the parts (a start is never applied in part without saying so)."""
    if mode not in APPLIES:
        raise CatalogError(f'mode {mode!r}')
    if not isinstance(slot, int) or slot < 2:
        raise CatalogError(f'slot {slot!r}: per-player starts are for slots 2..N (slot 1 plays the session base start)')
    omit = sorted(set(omit))
    unknown = [o for o in omit if o not in OMITTABLE]
    if unknown:
        raise CatalogError(f'slot {slot}: unknown parts to omit {unknown} (parts: {", ".join(OMITTABLE)})')
    present = {'reputation': bool(tpl['reputation']), 'blueprints': bool(tpl['blueprints']), 'research': bool(tpl['research']),
               'known': tpl['known'] is not None, 'inventory': bool(tpl['inventory'])}
    blocked = [part for part in OMITTABLE if present[part] and part not in APPLIES[mode] and part not in omit]
    if blocked:
        raise CatalogError(f"slot {slot} start {tpl['gamestart']} ({mode}): parts {', '.join(blocked)} cannot be applied for "
                           f"slot {slot} in {mode} mode; name them in --omit {slot}={','.join(blocked)} or choose another start")
    kit = {'slot': slot, 'gamestart': tpl['gamestart'], 'money': int(tpl['money']), 'omitted': omit}
    if 'reputation' in APPLIES[mode] and 'reputation' not in omit and tpl['reputation']:
        kit['reputation'] = [{'faction': f, 'value': int(round(v * 1_000_000))} for f, v in sorted(tpl['reputation'].items())]
    for part in ('blueprints', 'research'):
        if part in APPLIES[mode] and part not in omit and tpl[part]:
            kit[part] = sorted(tpl[part])
    if 'known' in APPLIES[mode] and 'known' not in omit and tpl['known']:
        known = tpl['known']
        kit['known'] = {'sectors': list(known['sectors']), 'shipyards': bool(known['shipyards'])}
        if known.get('station_faction'):
            kit['known']['faction'] = known['station_faction']
    return kit


# ---------------------------------------------------------------- command line

def roots(args):
    libraries = args.libraries or os.environ.get('X4LAN_GAME_LIBRARIES')
    scripts = args.scripts or os.environ.get('X4LAN_GAME_SCRIPTS')
    if not libraries or not scripts:
        raise CatalogError('--libraries and --scripts (or X4LAN_GAME_LIBRARIES / X4LAN_GAME_SCRIPTS) are required: '
                           'the stock game data is read at session setup, never copied into git')
    return libraries, scripts


def add_roots(parser):
    parser.add_argument('--libraries', help='resolved libraries root (<ext>/libraries/*.xml per extension)')
    parser.add_argument('--scripts', help='resolved scripts root (<ext>/md/*.xml per extension)')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('catalog', help='every stock start: class, why, player-level parts')
    add_roots(c)
    c.add_argument('--json', action='store_true')
    c.add_argument('--all', action='store_true', help='also list refused (tutorial, test, Timelines) starts')
    t = sub.add_parser('template', help='the resolved per-player start of a class a or c start (JSON)')
    add_roots(t)
    t.add_argument('gamestart')
    args = p.parse_args(argv)
    try:
        catalog = Catalog(*roots(args))
        if args.command == 'template':
            print(json.dumps(template(catalog, args.gamestart), indent=1, sort_keys=True))
            return 0
        rows = [r for r in catalog.rows() if args.all or r['class'] != '-']
        if args.json:
            print(json.dumps(rows, indent=1, sort_keys=True))
            return 0
        for r in rows:
            pl = r['player']
            loc = pl.get('location', {})
            ship = pl.get('ship', {}).get('macro', '-')
            print(f"{r['id']:34} class={r['class']} base={'yes' if not r['base'] else 'no'} "
                  f"per-player={'yes' if not r['per_player'] else 'no'} loc={loc.get('kind', '-')} "
                  f"zone={loc.get('zone') or loc.get('sector') or '-'} ship={ship} money={pl.get('money', '-')}")
            for reason in r['reasons']:
                print(f'    {reason}')
            if r['per_player'] and r['class'] in ('a', 'c'):
                print(f"    per-player refused: {r['per_player']}")
    except (CatalogError, OSError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
