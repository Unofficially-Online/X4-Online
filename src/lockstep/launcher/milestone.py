"""x4ls continue: a new session from a session's save, every player as saved: a milestone save
(RUN/saves/milestones/<story>-M<n>-slot<k>.xml.gz), a person's save (RUN/saves/player/<name>-t<T>.xml.gz) or a periodic save
(RUN/saves/c<K>-t<T>.xml.gz), each with its sidecar (<name>.json beside it). A save of an older build has none: then
--session (its session.json) and --overlay (a folder with its play-recipe.json) stand in for it, and the slots' ships and
inventory come from their saved player objects (one per slot 2..N that sat in a ship) and the stories are claimed again
by their scripts' names only.

A story milestone (tools/dev/native_gate_milestones.py) is a completed mission chapter; at each one every client of a play
session saved the world at one tick, and x4ls play kept the save with a JSON sidecar (saves.py: the milestone, every
client's hash, each slot's player, ship, inventory and research at the save, and the session it came from). This turns
that save into a session that goes on from there, so a playthrough is not played again from its start:

  1. one player object: X4 loads a save with exactly one (UniverseClass::Import, "Invalid number of players"), and a
     session's save holds one per seated slot. X4's own player (slot 1) stays as saved, with everything it holds; the
     others are taken out of a copy, their inventory, blueprints and research read from it first;
  2. the session (session.json): the first session's registration with this save, no fixture, slot 1 in the ship the
     save has X4's player in, slot k in the ship the milestone's row names (its saved occupied ship; the ship it sat in
     when it was on foot), no kits (money, standing, the shared library and every mission's state are in the save), and
     a 'continue' block: the slots' saved inventory and, with factions, their research and blueprints;
  3. the play overlay from the first session's recipe (tools/build_play_overlay.py --session): its stories' files and
     copies as before, but no story begins again; the probe gives the missions back (native_gate_continue: the MD's
     saved owner table and the session's story roots, claimed for their slots at tick 32) and the slots' inventory at
     the window's tick 8.
Then x4ls play as for any session (or x4ls host --milestone for netplay). No X4 runs here: the save is used as it is.

What cannot be kept (the gate's own state, not in an X4 save; rebuilt as said):
  - the slots' player objects 2..N themselves: new ones at the take, as in every session, seated in their saved ships
    (a slot on foot at the save sits in its ship again); their inventory, research and blueprints come back, their
    encyclopedia and map knowledge of their own (coop shares the player faction's anyway) and the values scripts kept on
    them do not (the Terran stories keep none: they read player.entity each time);
  - the mission owner table: rebuilt from the MD lists the probe filled before the save and from the session's story
    roots; the frames of an owned cue before tick 32 run as nobody's (as any story before its begin);
  - the active mission each player 2..N had chosen (the player chooses it again); slot 1's is X4's, kept;
  - in factions mode the relation steps between the slots' factions are set again from the recipe (neutral unless named);
  - the gate's per-tick state (clocks, RNG words, caches): a new session from tick 0, as every session from a save.
"""
from dataclasses import dataclass, field
import copy
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
DEV = ROOT / 'tools/dev'
SESSION_SAVE = 'session-save.xml.gz'
SESSION_JSON = 'session.json'
COMPONENT = re.compile(rb'^\s*<component\b([^>]*)>')
ATTR = re.compile(rb'\b(class|id|macro|owner|name)="([^"]*)"')
SAVED = re.compile(r'\[0x([0-9a-f]{1,16})\]')
# Our MD scripts whose state belongs to one session start and must begin again in a continued one: the identity setup's
# step counter (X4LAN_PlayerIdentity State.$ordinal; a continued session's step 1 was refused as refused_md_sequence and
# its probe stopped at tick 20, live rk8 continue 24 Nov). Left out of the copy, X4 starts the script afresh at the load,
# as the prepare step of x4ls resume did (its capture overlay has no such script).
FRESH_SCRIPTS = (b'X4LAN_PlayerIdentity',)
MD_SCRIPT = re.compile(rb'^\s*<script id="\d+" name="([A-Za-z0-9_]+)"(/?)>')
# A ship a player flew keeps X4's player movement in the save (<movement ... class="player">: its PlayerController, made
# again at the load). For slots 2..N the gate makes that controller itself at the take and refuses a ship that has one
# (lockstep_pilot_slots.h pslot_seat_ship: ship_controlled, stop 161; live rk8 continue 24 Nov, both clients at tick 35).
# So only X4's own player's ship keeps it: every other one leaves the copy without a movement element, as an unpiloted
# hull of a session save has none (it starts at rest; its place is its <offset>).
PLAYER_MOVEMENT = re.compile(rb'^\s*<movement\b[^>]*\bclass="player"[^>]*?(/?)>')
# A continued session starts every player's ship at rest (default; x4ls continue --keep-flight keeps the saved flight):
# release rk9, 24 Nov, continued from rk8's c3-t18000, where player 2's ship was saved in travel mode at full speed on
# the way to a sector border. The continued game went on with it at once, and the ship flew far into the void before
# the player saw it. What a save keeps of a ship's flight, all of it in the save (the same file on every client, so the
# same world at load everywhere):
#   - its movement's velocity and acceleration, and in the player movement the autopilot's path (<targetpoints>, behavior
#     playerautopilot2; the ship computer flying the player to a target): emptied, as X4 writes a ship at rest;
#   - the travel drive: <travel active="1" factor="14"/> in its engine, plus two effect objects (the drive's exhaust and
#     the travel bubble, effect durations -1): left out;
#   - <boost start="T"/> (boost running since T) becomes <boost/>; <sound travel="1"/> loses travel;
#   - X4's own player's <autopilot target="..."/> (the ship computer's autopilot): the target is left out.
# Throttle is not saved: X4 takes it from the speed. Orders in a ship's queue stay, since X4's AI scripts refer to them by
# ID; a player's ship runs them only on the autopilot, which is off.
REST_ELEMENTS = (b'velocity', b'acceleration')
TRAVEL = re.compile(rb'^\s*<travel\b[^>]*/>\s*$')
TRAVEL_ATTR = re.compile(rb'\s+travel="[^"]*"')
TARGET_ATTR = re.compile(rb'\s+target="[^"]*"')


class ContinueError(SystemExit):
    """A milestone save that cannot be continued as asked (the text says why)."""


def tools():
    sys.path.insert(0, str(DEV))
    sys.path.insert(0, str(ROOT / 'tools'))
    import build_play_overlay
    import native_gate_milestones as milestones
    import native_gate_scenarios as scenarios
    import native_gate_session_start as session_start
    return build_play_overlay, milestones, scenarios, session_start


@dataclass
class ContinueOptions:
    milestone: Path              # the save (RUN/saves/milestones/<name>.xml.gz, player/..., c<K>-t<T>...; its <name>.json beside it)
    gate_dll: Path
    out: Path                    # new folder: session-save.xml.gz, session.json, overlay
    overlay: Path | None = None  # the first session's play overlay (default: the recipe the sidecar holds)
    minutes: float | None = None
    sets: list = field(default_factory=list)
    save_every: float | None = None
    keep_player: str | None = None   # the saved ID of X4's own player when neither the sidecar nor a name tells
    session: Path | None = None      # a save without a sidecar: the session.json of the session that saved it
    keep_flight: bool = False        # keep the ships' saved flight (travel, speed, autopilot); default: every one at rest


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def sidecar_of(save):
    """The sidecar beside a milestone save: <name>.json for <name>.xml.gz (or .xml)."""
    save = Path(save)
    name = save.name[:-len('.xml.gz')] if save.name.endswith('.xml.gz') else save.name[:-len('.xml')] if save.name.endswith('.xml') else None
    if not name:
        raise ContinueError(f'{save}: not a milestone save (<story>-M<n>-slot<k>.xml.gz)')
    return save.with_name(name + '.json')


TICK = re.compile(r'-t(\d{1,10})$')


def stand_in_sidecar(save, session):
    """The sidecar of a save that has none (a periodic save of a build before 25 Nov): its name, hash and tick from the
    file, the session from its session.json; no slot rows (continue_session then reads the saved player objects)."""
    try:
        data = json.loads(Path(session).read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise ContinueError(f'{session}: not a session.json ({error})') from None
    if data.get('format') != 'X4LAN_SESSION_1':
        raise ContinueError(f'{session}: not a session.json (format {data.get("format")!r})')
    name = sidecar_of(save).stem
    tick = TICK.search(name)
    return {'format': 'X4LAN_MILESTONE_SAVE_1', 'kind': 'periodic', 'name': name, 'story': None, 'milestone': None, 'slot': None,
            'title': None, 'tick': int(tick.group(1)) if tick else 0, 'file': Path(save).name, 'sha256': sha256(save), 'slots': {},
            'session': data, 'recipe': None, 'spec': None, 'stand_in': True}


def read_sidecar(save, session=None):
    _, milestones, _, _ = tools()
    path = sidecar_of(save)
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except OSError:
        if session:
            return stand_in_sidecar(save, session)
        raise ContinueError(f'{path}: no sidecar beside the save (x4ls play writes it; for a save of an older build give '
                            '--session with its session.json and --overlay with its play overlay)') from None
    except ValueError as error:
        raise ContinueError(f'{path}: not JSON ({error})') from None
    if data.get('format') != milestones.SIDECAR_FORMAT or not milestones.SAVE_NAME.fullmatch(str(data.get('name'))):
        raise ContinueError(f'{path}: not a save sidecar')
    if not isinstance(data.get('session'), dict):
        raise ContinueError(f'{path}: names no session (a play overlay without session.json saved it)')
    return data


def _attrs(line):
    return {k.decode(): v.decode('utf-8', 'replace') for k, v in ATTR.findall(line)}


def _saved(text):
    m = SAVED.fullmatch(text or '')
    return f'{int(m.group(1), 16):016x}' if m else None


def player_state(lines):
    """A saved player component's own inventory, blueprints and research (its direct children; not those of the
    components below it)."""
    element = ET.fromstring(b''.join(lines))
    inventory, blueprints, research = [], [], []
    for child in element:
        if child.tag == 'inventory':
            for ware in child.iter('ware'):
                name, amount = ware.get('ware'), ware.get('amount', '1')
                if name and amount.isdigit() and int(amount) > 0:
                    inventory.append({'ware': name, 'amount': int(amount)})
        elif child.tag == 'blueprints':
            blueprints += [b.get('ware') for b in child.iter('blueprint') if b.get('ware')]
        elif child.tag == 'research':
            research += [r.get('ware') for r in child.iter('research') if r.get('ware')]
    merged = {}
    for item in inventory:
        merged[item['ware']] = merged.get(item['ware'], 0) + item['amount']
    return {'inventory': [{'ware': w, 'amount': n} for w, n in sorted(merged.items())],
            'blueprints': sorted(set(blueprints)), 'research': sorted(set(research))}


def _ending(line):
    return b'\r\n' if line.endswith(b'\r\n') else b'\n'


def _indent(line):
    return line[:len(line) - len(line.lstrip())]


class _Sink:
    """The copy's writer while ships rest: a <connections> left empty by a travel effect object taken out goes too (X4
    writes none empty)."""

    def __init__(self, target):
        self.target, self.held = target, None

    def write(self, line):
        if self.held is not None:
            held, self.held = self.held, None
            if line.strip() == b'</connections>':
                return
            self.target.write(held)
        if line.strip() == b'<connections>':
            self.held = line
        else:
            self.target.write(line)

    def close(self):
        if self.held is not None:
            self.target.write(self.held)
            self.held = None


def one_player(save, out, keep=None, rest=None, report=None):
    """Copies a session's save with X4's own player only; returns (kept saved ID, {removed saved ID: state}). keep: the
    saved ID of X4's own player; else the one player object with a name (the game start names X4's own player, the gate's
    have none). X4 writes one element per line, so a component is its line through the matching </component>; each
    removed player's ancestor ship is noted ('ship', its saved ID) with its own state (player_state). Every ship but X4's own
    player's loses its player movement (PLAYER_MOVEMENT); our MD scripts in FRESH_SCRIPTS lose their state.
    rest: None keeps the saved flight; else saved ship IDs (the slots' rows) that start at rest with the ship every player
    object sits in (REST_ELEMENTS ...). report (a dict) gets the counts of what was stopped."""
    found, stack, ships = [], [], {}
    with gzip.open(save, 'rb') as source:
        for line in source:
            text = line.strip()
            m = COMPONENT.match(line)
            if m:
                a = _attrs(m.group(1))
                if a.get('class') == 'player':
                    found.append((_saved(a.get('id')), 'name' in a))
                    ships[found[-1][0]] = next((sid for c, sid in reversed(stack) if (c or '').startswith('ship_')), None)
                if not text.endswith(b'/>'):
                    stack.append((a.get('class'), _saved(a.get('id'))))
            elif text.startswith(b'</component>') and stack:
                stack.pop()
    if not found:
        raise ContinueError(f'{save}: no player object in this save')
    if keep is None:
        named = [saved for saved, has_name in found if has_name]
        if len(found) == 1:
            named = [found[0][0]]
        if len(named) != 1:
            raise ContinueError(f"{save}: {len(found)} player objects, {len(named)} of them named: say which is X4's own with "
                                f"--keep-player ID ({', '.join(str(s) for s, _ in found)})")
        keep = named[0]
    keep = f'{int(keep, 16):016x}'
    if keep not in [saved for saved, _ in found]:
        raise ContinueError(f'{save}: no player object {keep}')
    own_ship = ships.get(keep)
    resting = set()
    if rest is not None:
        resting = {s for s in ships.values() if s} | {f'{int(s, 16):016x}' for s in rest if s}
    counts = {'ships': sorted(resting), 'still': 0, 'travel': 0, 'effects': 0, 'boost': 0, 'autopilot': 0}

    def ship_of(stack):   # the innermost ship this line is in
        return next((sid for c, sid in reversed(stack) if (c or '').startswith('ship_')), None)

    def resting_ship(stack):   # this line is an element of a resting ship itself (not of a part of it)
        return bool(stack) and (stack[-1][0] or '').startswith('ship_') and stack[-1][1] in resting

    removed, stack, taking, fresh, moving = {}, [], None, False, False
    still, blank, dropping, drop_close, pending, connection = None, None, 0, False, None, None
    with gzip.open(save, 'rb') as source, gzip.open(out, 'wb', compresslevel=5) as target:
        sink = _Sink(target) if resting else target
        for line in source:
            text = line.strip()
            if fresh:
                fresh = not text.startswith(b'</script>')
                continue
            if moving:
                moving = not text.startswith(b'</movement>')
                continue
            if blank is not None:   # inside a velocity, acceleration or path written empty
                if text.startswith(blank):
                    blank = None
                continue
            if dropping:   # inside a travel effect object left out
                if text.startswith(b'<component') and not text.endswith(b'/>'):
                    dropping += 1
                elif text.startswith(b'</component>'):
                    dropping -= 1
                    drop_close = connection if not dropping else False
                continue
            if drop_close:   # ...and the connection it sat in (one component per connection; else it stays)
                held, drop_close = drop_close, False
                if text.startswith(b'</connection>'):
                    continue
                sink.write(held)
            if pending is not None:   # a connection of a resting ship: left out with the travel effect object it holds
                held, pending = pending, None
                m = COMPONENT.match(line)
                a = _attrs(m.group(1)) if m else {}
                if a.get('class') == 'effectobject' and 'travel' in (a.get('macro') or ''):
                    counts['effects'] += 1
                    connection = held
                    if text.endswith(b'/>'):
                        drop_close = connection
                    else:
                        dropping = 1
                    continue
                sink.write(held)
            if still is not None:   # the movement of a resting ship
                if text.startswith(b'</movement>'):
                    still = None
                else:
                    tag = next((t for t in still if text.startswith(b'<' + t + b'>') or text.startswith(b'<' + t + b' ')), None)
                    if tag is not None:
                        if not text.endswith(b'/>'):
                            blank = b'</' + tag + b'>'
                            counts['still'] += 1
                        sink.write(_indent(line) + b'<' + tag + b'/>' + _ending(line))
                        continue
            if text.startswith(b'<movement'):
                movement = PLAYER_MOVEMENT.match(line)
                if movement and stack and (stack[-1][0] or '').startswith('ship_') and stack[-1][1] != own_ship:
                    moving = not movement.group(1)
                    continue
                if resting_ship(stack) and taking is None and not text.endswith(b'/>'):
                    still = REST_ELEMENTS + ((b'targetpoints',) if movement else ())
                    sink.write(line)
                    continue
            if resting and taking is None and stack:
                if resting_ship(stack) and text.startswith(b'<boost ') and text.endswith(b'/>'):
                    counts['boost'] += 1
                    sink.write(_indent(line) + b'<boost/>' + _ending(line))
                    continue
                if resting_ship(stack) and text.startswith(b'<sound ') and b' travel=' in text:
                    rest_of = TRAVEL_ATTR.sub(b'', text)
                    if rest_of != b'<sound/>':
                        sink.write(_indent(line) + rest_of + _ending(line))
                    continue
                if stack[-1][0] == 'engine' and TRAVEL.match(line) and ship_of(stack) in resting:
                    counts['travel'] += 1
                    continue
                if text.startswith(b'<connection ') and not text.endswith(b'/>') and ship_of(stack) in resting:
                    pending = line
                    continue
                if stack[-1] == ('player', keep) and text.startswith(b'<autopilot ') and b' target=' in text:
                    counts['autopilot'] += 1
                    sink.write(_indent(line) + TARGET_ATTR.sub(b'', text) + _ending(line))
                    continue
            script = MD_SCRIPT.match(line) if text.startswith(b'<script id=') else None
            if script and script.group(1) in FRESH_SCRIPTS:
                fresh = not script.group(2)
                continue
            if taking is not None:
                taking['lines'].append(line)
                if text.startswith(b'<component ') and not text.endswith(b'/>'):
                    taking['depth'] += 1
                elif text.startswith(b'</component>'):
                    taking['depth'] -= 1
                    if not taking['depth']:
                        removed[taking['id']] = dict(player_state(taking['lines']), ship=taking['ship'])
                        taking = None
                continue
            m = COMPONENT.match(line)
            if m:
                a = _attrs(m.group(1))
                if a.get('class') == 'player' and _saved(a.get('id')) != keep:
                    ship = next((s for c, s in reversed(stack) if (c or '').startswith('ship_')), None)
                    taking = {'id': _saved(a.get('id')), 'ship': ship, 'depth': 0 if text.endswith(b'/>') else 1, 'lines': [line]}
                    if not taking['depth']:
                        removed[taking['id']] = dict(player_state(taking['lines']), ship=ship)
                        taking = None
                    continue
                if not text.endswith(b'/>'):
                    stack.append((a.get('class'), _saved(a.get('id'))))
            elif text.startswith(b'</component>') and stack:
                stack.pop()
            sink.write(line)
        if resting:
            sink.close()
    if taking is not None:
        raise ContinueError(f'{save}: a player object does not end (not a whole save?)')
    if pending is not None or dropping or blank is not None or still is not None:
        raise ContinueError(f'{save}: a ship does not end (not a whole save?)')
    if report is not None:
        report.update(counts if rest is not None else {'ships': []})
    return keep, removed


def player_of(k, sidecar, removed, slots):
    """The saved ID of slot k's player object in the save: the one the probe's row names; without rows (a save of an older
    build) the one removed player object when slot k is the one slot without a row, else None."""
    rows = sidecar.get('slots') or {}
    named = (rows.get(str(k)) or {}).get('player')
    if named:
        return named
    claimed = {(rows.get(str(j)) or {}).get('player') for j in slots}
    unnamed = [j for j in slots if not (rows.get(str(j)) or {}).get('player')]
    left = [saved for saved in removed if saved not in claimed]
    return left[0] if unnamed == [k] and len(left) == 1 else None


def slot_state(k, sidecar, removed, mode, slots=()):
    """(ship, state) of slot k (2..N) from the milestone: its ship as the probe's row names it (else the ship its saved
    player object sat in) and what the session gives it back (inventory; in factions mode its research and blueprints)."""
    row = (sidecar.get('slots') or {}).get(str(k)) or {}
    saved = removed.get(player_of(k, sidecar, removed, slots or [k]) or '') or {}
    ship = row.get('ship') or saved.get('ship')
    inventory = row.get('inventory') if row.get('inventory') is not None else saved.get('inventory') or []
    state = {'inventory': sorted(inventory, key=lambda i: i['ware']), 'walking': bool(row.get('walking')),
             'player_saved': bool(saved)}
    if mode == 'factions':   # coop: the one shared library is X4's player's, kept in the save
        state['research'] = saved.get('research') or [w for w in row.get('research') or []]
        state['blueprints'] = saved.get('blueprints') or []
    return ship, state


def continue_session(sidecar, first, save, header, removed, components):
    """session.json of the continued session (native_gate_session_start.session_of checks it)."""
    _, milestones, _, _ = tools()
    out = copy.deepcopy(first)
    slots_state, hulls = {}, {}
    keys = sorted(out.get('slots') or {}, key=int)
    for key in keys:
        k = int(key)
        ship, state = slot_state(k, sidecar, removed, out['mode'], [int(j) for j in keys])
        if not ship:
            raise ContinueError(f'slot {k}: the milestone names no ship for it (no SAVE_SLOT row and no saved player object)')
        got = components.get(ship)
        if got is None or not (got['class'] or '').startswith('ship_'):
            raise ContinueError(f'slot {k}: ship {ship} is not a ship of the save ({got and got["class"]})')
        if got['owner'] not in ('player', f'x4lan_player{k}'):
            raise ContinueError(f"slot {k}: ship {ship} belongs to {got['owner']}, not to the players")
        if ship == header['player']['ship'] or ship in hulls.values():
            raise ContinueError(f'slot {k}: ship {ship} is another slot\'s too (two players in one ship cannot continue)')
        hulls[key] = ship
        slot = out['slots'][key]
        slot.update({'sector': got['sector'], 'ship': got['macro'], 'kit': None})
        slots_state[key] = state
    block = {'format': milestones.CONTINUE_FORMAT, 'name': sidecar['name'], 'kind': sidecar.get('kind', 'milestone'),
             'story': sidecar.get('story'), 'milestone': sidecar.get('milestone'), 'slot': sidecar.get('slot'),
             'title': sidecar.get('title'), 'tick': sidecar['tick'],
             'from': {'file': Path(sidecar.get('file') or '').name, 'sha256': sidecar['sha256']},
             'slots': {k: {part: v[part] for part in ('inventory', 'blueprints', 'research') if part in v} for k, v in slots_state.items()}}
    milestones.check_continue(block)
    out.update({'snapshot': {'path': str(Path(save).resolve()), 'sha256': sha256(save), 'bytes': Path(save).stat().st_size},
                'seed': header.get('seed'), 'build': header.get('build'), 'player_ship': header['player']['ship'],
                'player_macro': header['player']['macro'], 'player_sector': header['player']['sector'], 'fixture': [],
                'fixture_form': 'none', 'hulls': hulls, 'continue': block})
    return out, slots_state


def prepare(o, log=print):
    """Save, session.json and play overlay of a continued session in o.out; returns the result dict."""
    build_play_overlay, milestones, scenarios, session_start = tools()
    from . import resume
    save, out = Path(o.milestone), Path(o.out)
    if not save.is_file():
        raise ContinueError(f'{save}: no such milestone save')
    if out.exists():
        raise ContinueError(f'{out} exists (a session folder is made once; pick a new --out)')
    sidecar = read_sidecar(save, o.session)
    if o.session and not sidecar.get('stand_in'):
        raise ContinueError(f'{save} has its own sidecar: --session is for a save without one')
    if sha256(save) != sidecar.get('sha256'):
        raise ContinueError(f'{save} is not the save its sidecar names (SHA-256 differs)')
    first = sidecar['session']
    missing = [key for key in ('base_start', 'mode', 'players', 'slots') if key not in first]
    if missing:
        raise ContinueError(f"the milestone's session names no {', '.join(missing)}")
    if o.overlay:
        recipe = build_play_overlay.read_recipe(o.overlay)
    else:
        recipe = sidecar.get('recipe')
    if not recipe or recipe.get('role', 'session') != 'session' or recipe.get('kind') != build_play_overlay.RECIPE_KIND:
        raise ContinueError('no play recipe of the first session (the sidecar holds none: give --overlay with its play overlay)')
    if recipe['players'] != first.get('players') or recipe['mode'] != first.get('mode'):
        raise ContinueError(f"the recipe is for {recipe['players']} players in {recipe['mode']} mode, the session {first.get('players')} in {first.get('mode')}")
    if recipe.get('open'):
        raise ContinueError('a session with open slots cannot be continued yet (its milestones are not saved: x4ls play --open)')
    keep = o.keep_player or ((sidecar.get('slots') or {}).get('1') or {}).get('player')
    out.mkdir(parents=True)
    prepared = out / SESSION_SAVE
    log(f"continue: {sidecar['name']} ({sidecar.get('title') or '-'}), tick {sidecar['tick']}, {save.stat().st_size / 1e6:.1f} MB")
    rest = None if o.keep_flight else [((sidecar.get('slots') or {}).get(str(k)) or {}).get('ship') for k in range(1, first['players'] + 1)]
    flight = {}
    try:
        kept, removed = one_player(save, prepared, keep, rest, flight)
    except ContinueError:
        if keep and not o.keep_player:   # the row's ID is not in the file: try X4's own by its name
            kept, removed = one_player(save, prepared, None, rest, flight)
        else:
            raise
    log(f"one player: X4's own player object {kept} kept" + (f", {len(removed)} other slot(s)' taken out ({', '.join(sorted(removed))})" if removed else ''))
    if o.keep_flight:
        log('flight: kept as saved (--keep-flight)')
    else:
        log(f"flight: {len(flight['ships'])} player ship(s) at rest (speeds and paths emptied {flight['still']}, travel drives off "
            f"{flight['travel']}, travel effects {flight['effects']}, boosts {flight['boost']}, autopilot targets {flight['autopilot']})")
    try:
        header = scenarios.snapshot_header(prepared)
    except ValueError as error:
        raise ContinueError(f"{save}: X4's own player is not in a ship there ({error}); a milestone saved while player 1 walked or "
                            'flew the suit cannot start a session') from None
    if header['start'] != first['base_start']:
        raise ContinueError(f"{save}: a save of {header['start']}, the session's base start is {first['base_start']}")
    ships = [s for s in (((sidecar.get('slots') or {}).get(str(k)) or {}).get('ship') for k in range(2, first['players'] + 1)) if s]
    ships += [v['ship'] for v in removed.values() if v.get('ship')]
    components = session_start.components(prepared, sorted(set(ships)))
    session, states = continue_session(sidecar, first, prepared, header, removed, components)
    (out / SESSION_JSON).write_text(json.dumps(session, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    (out / 'continued-from.json').write_text(json.dumps({'milestone': str(save.resolve()), 'sidecar': str(sidecar_of(save).resolve()),
                                                         'name': sidecar['name'], 'sha256': sidecar['sha256'], 'kept_player': kept,
                                                         'removed_players': sorted(removed), 'slots': states,
                                                         'flight': 'kept' if o.keep_flight else dict(flight, form='rest')},
                                                        indent=1, sort_keys=True) + '\n', encoding='utf-8')
    try:
        session_start.session_of(out / SESSION_JSON)
    except session_start.SessionError as error:
        raise ContinueError(f'the continued session is refused: {error}') from None
    for key, state in sorted(states.items(), key=lambda kv: int(kv[0])):
        log(f"slot {key}: ship {session['hulls'][key]} ({session['slots'][key]['ship']} in {session['slots'][key]['sector']}), "
            f"{len(state['inventory'])} ware(s) back" + (', was on foot: seated again' if state['walking'] else '')
            + ('' if state['player_saved'] else ', its player object was not in the save (on foot: parked)'))
    cmd = [sys.executable, str(ROOT / 'tools/build_play_overlay.py')] + \
        resume.overlay_command(recipe, prepared, out / SESSION_JSON, o.gate_dll, out / 'overlay', o.minutes, o.sets, o.save_every,
                               build_play_overlay.PLAY_LUA_BLOCK)
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.stdout.write(result.stdout[-3000:])
        sys.stderr.write(result.stderr[-3000:])
        raise ContinueError(f'the play overlay was refused ({result.returncode})')
    run = f'python tools/x4ls.py play --clients {recipe["players"]} --overlay "{out / "overlay"}" --snapshot "{prepared}"'
    log(f"continued session {out}: from {sidecar['name']}; slot 1 flies {session['player_macro']} in {session['player_sector']}; "
        f'play it with:\n  {run}')
    return {'outcome': 'ready', 'session': str(out / SESSION_JSON), 'save': str(prepared), 'overlay': str(out / 'overlay'), 'run': run,
            'milestone': sidecar['name']}
