"""A lockstep session from any usable stock game start, with per-player starts (no installation, no launch).

Every client loads ONE session save (option B of reports/LOCKSTEP-STARTS-20261101.txt: X4's own new game is not
reproducible across processes, the save is). This tool makes that save's overlay, registers the captured save and
hands it to the lane runner and the overlay builders:

  spec             choose the session base start S (slot 1, the save's own start) and per-player starts for slots
                   2..N; every start is classified from the stock data (native_gate_gamestarts.py) and refused
                   loudly when it cannot be applied, naming the start and the parts that block it
  capture-overlay  the capture overlay for one disposable lane run: X4's new game for S, the battle fixture
                   (census anchor), and one hull per per-player start (its ship macro and stock loadout at its
                   start position), created before the save so the pilot slots get saved IDs
  register         read the captured save (start, seed, player ship, fixture, hulls; sha256) into session.json
  rebase           bind a census/full-stack overlay to the session save (native_gate_scenarios.build_rebased)

Then: python tools/dev/native_gate_players.py overlay --session <session.json> --base <rebased overlay> ...
applies the per-player starts (identity plan kits) at the setup tick on every client, by default.

    python tools/dev/native_gate_session_start.py spec --base-start x4ep1_gamestart_fight --players 2 --mode coop \\
        --start 2=x4ep1_gamestart_discover --omit 2=inventory,reputation --out .local/sessions/fd/spec.json
    python tools/dev/native_gate_session_start.py capture-overlay --spec .local/sessions/fd/spec.json \\
        --fixedtick-dll <capture-005 x4lan_fixedtick_64.dll> --out .local/sessions/fd/capture-overlay
    python tools/dev/native_gate_session_start.py register --spec .local/sessions/fd/spec.json \\
        --snapshot <collect>/native-snapshot/save_009.xml.gz --out .local/sessions/fd/session.json
    python tools/dev/native_gate_session_start.py rebase --session .local/sessions/fd/session.json \\
        --base <full-stack overlay> --out .local/sessions/fd/overlay-base

Stock game data is read at setup (--libraries/--scripts or X4LAN_GAME_LIBRARIES/X4LAN_GAME_SCRIPTS) and only
written into the local spec, overlay and session files, never into git.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_gamestarts as gamestarts   # noqa: E402
import native_gate_player_identity as identity  # noqa: E402
import native_gate_scenarios as scenarios     # noqa: E402

SPEC_FORMAT = 'X4LAN_SESSION_START_SPEC_1'
SESSION_FORMAT = 'X4LAN_SESSION_1'
HULLS_MD = 'x4lan_session_hulls.xml'
HULLS_SCRIPT, HULLS_CUE = 'X4LAN_SessionHulls', 'Create'
HULL_SEED = 5100            # + slot: fixed create_ship seeds (one capture; the save is the truth afterwards)
MAX_PLAYERS = 4             # the pilot module drives at most four slot ships
NAME = re.compile(r'[A-Za-z][A-Za-z0-9_]*')
PATH = re.compile(r'(?:\.\./)?[A-Za-z0-9_]+')
LANE_TOOL = '../lanes/release-5d5a680ec6ef/dev/x4lane.py'
LANE_BASE = '.local/lockstep-live/frozen-v3/src/extension/x4_lan_lab'
CAPTURE_SEED = 276944566    # capture 005's seed


class SessionError(ValueError):
    """A session that cannot be built as asked (always names the start, slot or file)."""


def slot_values(texts, what):
    """K=VALUE options -> {K: VALUE} (each slot once)."""
    out = {}
    for text in texts or []:
        m = re.fullmatch(r'(\d{1,2})=(.+)', text)
        if not m:
            raise SessionError(f'{what} takes K=VALUE, got {text!r}')
        if int(m[1]) in out:
            raise SessionError(f'{what}: slot {m[1]} given twice')
        out[int(m[1])] = m[2]
    return out


def _check_hull(hull, slot):
    for key in ('macro', 'zone', 'sector'):
        if not NAME.fullmatch(hull[key]):
            raise SessionError(f'slot {slot}: unsafe {key} {hull[key]!r}')
    for e in hull['equipment']:
        if e['kind'] not in ('engine', 'weapon', 'shield', 'turret') or not NAME.fullmatch(e['macro'] or '') or \
                (e.get('path') is not None and not PATH.fullmatch(e['path'])):
            raise SessionError(f'slot {slot}: unsafe loadout entry {e}')
    for name in list(hull['ammunition']) + list(hull['software']) + list(hull['thrusters']):
        if not NAME.fullmatch(name or ''):
            raise SessionError(f'slot {slot}: unsafe loadout name {name!r}')
    for axis in 'xyz':
        if not isinstance(hull[axis], int) or abs(hull[axis]) > identity.COORD_LIMIT:
            raise SessionError(f'slot {slot}: hull {axis} {hull[axis]!r} out of range')


def build_spec(catalog, base_start, players, mode, starts, omit=None):
    """The session-start spec: base start S for slot 1 and a resolved per-player start (kit + hull) per slot."""
    omit = {k: [p for p in v.split(',') if p] if isinstance(v, str) else list(v) for k, v in (omit or {}).items()}
    if mode not in identity.MODES:
        raise SessionError(f'mode {mode!r}')
    if not 2 <= players <= MAX_PLAYERS:
        raise SessionError(f'2..{MAX_PLAYERS} players (the pilot module drives at most {MAX_PLAYERS} slot ships)')
    rec = catalog.record(base_start)
    why = gamestarts.session_base_refusal(rec)
    if why:
        raise SessionError('session base refused: ' + why)
    base_class, base_parts = gamestarts.classify(rec)
    for k in starts:
        if not 2 <= k <= players:
            raise SessionError(f'--start {k}: per-player starts are for slots 2..{players}; slot 1 plays the session base '
                               f'start {base_start} (choose it with --base-start)')
    for k in omit:
        if k not in starts:
            raise SessionError(f'--omit {k}: slot {k} has no per-player start')
    slots, used = {}, {}
    for k in sorted(starts):
        try:
            tpl = gamestarts.template(catalog, starts[k])
            kit = gamestarts.slot_kit(tpl, mode, k, omit.get(k, ()))
        except gamestarts.CatalogError as error:
            raise SessionError(f'slot {k}: {error}') from None
        n = used.get(tpl['gamestart'], 0)
        used[tpl['gamestart']] = n + 1
        place = tpl['place']
        hull = dict(tpl['ship'], zone=place['zone'], sector=place['sector'], x=place['x'] + n * gamestarts.SAME_START_OFFSET,
                    y=place['y'], z=place['z'], yaw=place['yaw'], seed=HULL_SEED + k)
        _check_hull(hull, k)
        slots[str(k)] = {'gamestart': tpl['gamestart'], 'class': tpl['class'], 'kit': kit, 'hull': hull,
                         'offset_x': n * gamestarts.SAME_START_OFFSET, 'presentation': tpl['presentation'],
                         'inventory_not_applied': tpl['inventory']}
    # The kits must pass the identity plan's own rules (the same ones Lua applies on every client).
    ships = [f'{i:016x}' for i in range(1, players + 1)]
    try:
        identity.build_plan(players, mode, ships=ships, occupied_ship=ships[0], kits=[s['kit'] for s in slots.values()])
    except ValueError as error:
        raise SessionError(str(error)) from None
    return {'format': SPEC_FORMAT, 'base_start': base_start, 'base_class': base_class, 'base_parts': base_parts,
            'players': players, 'mode': mode, 'slots': slots, 'catalog': catalog.sources}


def load_json(path, fmt):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    if data.get('format') != fmt:
        raise SessionError(f'{path}: not a {fmt} file')
    return data


def write_new(path, text):
    path = Path(path)
    if path.exists():
        raise SessionError(f'{path} exists; refusing to overwrite')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return path


# ---------------------------------------------------------------- capture

def hulls_md(spec):
    """The generated MD that creates every per-player start's hull in the capture (overlay only, never in git):
    the stock ship macro with its stock loadout, at the start's zone position, owned by the player, one Wait order.
    It answers the capture script with the count, or -1 when any hull failed."""
    root = ET.Element('mdscript', {'name': HULLS_SCRIPT, 'xmlns:xsi': 'http://www.w3.org/2001/XMLSchema-instance',
                                   'xsi:noNamespaceSchemaLocation': 'md.xsd'})
    cue = ET.SubElement(ET.SubElement(root, 'cues'), 'cue', {'name': HULLS_CUE})
    ET.SubElement(ET.SubElement(cue, 'conditions'), 'event_ui_triggered',
                  {'screen': f"'{HULLS_SCRIPT}'", 'control': f"'{HULLS_CUE}'"})
    actions = ET.SubElement(cue, 'actions')
    ET.SubElement(actions, 'set_value', {'name': '$ships', 'exact': '[]'})
    ET.SubElement(actions, 'set_value', {'name': '$failed', 'exact': "''"})
    for key in sorted(spec['slots'], key=int):
        slot = spec['slots'][key]
        hull = slot['hull']
        _check_hull(hull, key)
        loadout = ET.SubElement(actions, 'create_loadout', {'result': '$loadout'})
        macros = ET.SubElement(loadout, 'macros')
        for e in hull['equipment']:
            ET.SubElement(macros, e['kind'], {'macro': e['macro'], **({'path': e['path']} if e.get('path') else {})})
        if hull['ammunition']:
            ammo = ET.SubElement(loadout, 'ammunition')
            for macro, amount in sorted(hull['ammunition'].items()):
                ET.SubElement(ammo, 'ammunition', {'macro': macro, 'exact': str(int(amount))})
        if hull['software']:
            soft = ET.SubElement(loadout, 'software')
            for ware in hull['software']:
                ET.SubElement(soft, 'software', {'ware': ware})
        if hull['thrusters']:
            virtual = ET.SubElement(loadout, 'virtualmacros')
            for macro in hull['thrusters']:
                ET.SubElement(virtual, 'thruster', {'macro': macro})
        ET.SubElement(actions, 'set_value', {'name': '$ship', 'exact': 'null'})
        ET.SubElement(actions, 'find_zone', {'name': '$zone', 'macro': f"macro.{hull['zone']}"})
        found = ET.SubElement(actions, 'do_if', {'value': '@$zone.isclass.zone'})
        ship = ET.SubElement(found, 'create_ship', {'name': '$ship', 'macro': f"macro.{hull['macro']}", 'zone': '$zone',
                                                    'seed': str(int(hull['seed']))})
        ET.SubElement(ship, 'owner', {'exact': 'faction.player', 'overridenpc': 'true'})
        ET.SubElement(ship, 'loadout', {'loadout': '$loadout'})
        ET.SubElement(ET.SubElement(ship, 'pilot'), 'select', {'faction': 'faction.argon', 'tags': 'tag.aipilot'})
        ET.SubElement(ship, 'position', {'x': f"{hull['x']}m", 'y': f"{hull['y']}m", 'z': f"{hull['z']}m"})
        ET.SubElement(ship, 'rotation', {'yaw': f"{hull['yaw']}deg", 'pitch': '0deg', 'roll': '0deg'})
        made = ET.SubElement(actions, 'do_if', {'value': '@$ship.isoperational'})
        ET.SubElement(made, 'create_order', {'object': '$ship', 'id': "'Wait'", 'default': 'true'})
        ET.SubElement(made, 'append_to_list', {'name': '$ships', 'exact': '$ship'})
        ET.SubElement(made, 'debug_text', {'text': f"'[X4LAN] SESSION_HULL slot={key} gamestart={slot['gamestart']} "
                                                   "macro=%s zone=%s sector=%s owner=%s'.[$ship.macro.id, $ship.zone.macro.id, "
                                                   "$ship.sector.macro.id, $ship.owner.id]"})
        failed = ET.SubElement(actions, 'do_else')
        ET.SubElement(failed, 'set_value', {'name': '$failed', 'exact': f"$failed + ' {key}'"})
        ET.SubElement(failed, 'debug_text', {'text': f"'[X4LAN] SESSION_HULL_FAILED slot={key} zone={hull['zone']} found=%s'.[@$zone.isclass.zone]"})
    ET.SubElement(actions, 'debug_text', {'text': "'[X4LAN] SESSION_HULLS created=%s failed=%s'.[$ships.count, $failed]"})
    ET.SubElement(actions, 'raise_lua_event', {'name': f"'{HULLS_SCRIPT}.Ready'", 'param': "if $failed == '' then $ships.count else -1"})
    ET.indent(root, ' ')
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<!-- Generated by tools/dev/native_gate_session_start.py for one session save capture (overlay only, never in git). -->\n'
            + ET.tostring(root, encoding='unicode') + '\n')


def build_capture(spec, fixedtick_dll, out):
    hulls = len(spec['slots'])
    extra_md = {HULLS_MD: hulls_md(spec)} if hulls else {}
    extra = [f'X4LAN_CONFIG.native_gate_snapshot_hulls = {hulls}'] if hulls else []
    return scenarios.build_capture_overlay(spec['base_start'], fixedtick_dll, out, allowed={spec['base_start']},
                                           extra_md=extra_md, extra_config=extra)


def capture_command(spec, overlay, collect, lane_tool=LANE_TOOL, base=LANE_BASE, label='native-session'):
    S = spec['base_start']
    hulls = len(spec['slots'])
    expect = ['NATIVE_SNAPSHOT SAVE_REQUEST', f'NATIVE_SNAPSHOT GAMESTART id={S}'] + ([f'NATIVE_SNAPSHOT HULLS_READY count={hulls}'] if hulls else [])
    fail = ('NATIVE_SNAPSHOT ERROR|Error while executing.*native_gate_snapshot|Error in MD cue md.X4LAN_NativeGate'
            '|Error in MD cue md.X4LAN_SessionHulls|SESSION_HULL_FAILED')
    parts = ['python tools/dev/native_gate_lane.py', f'--lane-tool {lane_tool}', '--capture-snapshot --', 'run --agent native-lockstep',
             f'--label {label}', '--timeout 420 --wait 180 --settle 2', f'--base {base}', f'--overlay {overlay}', f'--gamestart {S}',
             f'--seed {CAPTURE_SEED}', '--until "NATIVE_SNAPSHOT SAVED"'] + [f'--expect "{e}"' for e in expect] + \
            [f'--fail-on "{fail}"', f'--collect {collect}']
    return ' \\\n    '.join(parts)


# ---------------------------------------------------------------- register

def _stream(path):
    stack = []
    with gzip.open(path, 'rb') as source:
        for event, element in ET.iterparse(source, events=('start', 'end')):
            if event == 'start':
                stack.append(element)
                yield stack
            else:
                stack.pop()
                if stack:
                    stack[-1].remove(element)
                element.clear()


def saved_list(path, script, cue, var):
    """The saved component IDs of one completed cue's list variable, in list order."""
    ref = []
    for nodes in _stream(path):
        if [n.tag for n in nodes] == ['savegame', 'md', 'script', 'cue', 'vars', 'value'] and nodes[2].get('name') == script \
                and nodes[3].get('name') == cue and nodes[-1].get('name') == var:
            if nodes[3].get('state') != 'complete' or nodes[-1].get('type') != 'list':
                raise SessionError(f'{script}.{cue}.{var} is not a completed saved list')
            ref.append(nodes[-1].get('value'))
    if len(ref) != 1 or not re.fullmatch(r'[1-9][0-9]*', ref[0] or ''):
        raise SessionError(f'save needs exactly one {script}.{cue}.{var} list, found {len(ref)}')
    ids, lists = [], 0
    for nodes in _stream(path):
        if len(nodes) < 4 or [n.tag for n in nodes[:4]] != ['savegame', 'script', 'refs', 'ref'] or nodes[2].get('type') != 'list' \
                or nodes[3].get('id') != ref[0]:
            continue
        if len(nodes) == 4:
            lists += 1
            continue
        value = nodes[-1]
        if len(nodes) != 5 or value.tag != 'value' or value.get('type') != 'component' or \
                not re.fullmatch(r'\[0x[0-9a-f]{1,16}\]', value.get('value', '')):
            raise SessionError(f'{script}.{cue}.{var} must hold direct component references')
        ids.append(f"{int(value.get('value')[3:-1], 16):016x}")
    if lists != 1:
        raise SessionError(f'{script}.{cue}.{var}: list {ref[0]} found {lists} times')
    return ids


def components(path, ids):
    """saved ID -> {class, macro, owner, zone, sector} of the given components (ancestor zone and sector macros)."""
    wanted, out = set(ids), {}
    for nodes in _stream(path):
        e = nodes[-1]
        if e.tag != 'component' or not re.fullmatch(r'\[0x[0-9a-f]{1,16}\]', e.get('id', '')):
            continue
        saved = f"{int(e.get('id')[3:-1], 16):016x}"
        if saved not in wanted:
            continue
        if saved in out:
            raise SessionError(f'component {saved} appears twice in the save')
        above = [n for n in nodes[:-1] if n.tag == 'component']
        zone = next((n.get('macro') for n in reversed(above) if n.get('class') == 'zone'), None)
        sector = next((n.get('macro') for n in reversed(above) if n.get('class') == 'sector'), None)
        out[saved] = {'class': e.get('class'), 'macro': e.get('macro'), 'owner': e.get('owner'), 'zone': zone, 'sector': sector}
    return out


def register(spec, snapshot):
    """session.json content for a captured save of this spec; refuses a save that does not match it."""
    snapshot = Path(snapshot)
    sha = scenarios.digest(snapshot)
    header = scenarios.snapshot_header(snapshot)
    if header['start'] != spec['base_start']:
        raise SessionError(f"save start {header['start']} is not the session base start {spec['base_start']}")
    import build_native_gate_overlay as builder
    fixture = builder.snapshot_fixture(snapshot)
    hulls = {}
    if spec['slots']:
        ids = saved_list(snapshot, HULLS_SCRIPT, HULLS_CUE, '$ships')
        order = sorted(spec['slots'], key=int)
        if len(ids) != len(order) or len(set(ids)) != len(ids):
            raise SessionError(f'save has {len(ids)} session hulls, the spec {len(order)}')
        found = components(snapshot, ids)
        for key, saved in zip(order, ids):
            hull, got = spec['slots'][key]['hull'], found.get(saved)
            if got is None:
                raise SessionError(f'slot {key}: hull {saved} not in the save')
            want = {'macro': hull['macro'], 'owner': 'player', 'zone': hull['zone'], 'sector': hull['sector']}
            bad = {k: (got[k], v) for k, v in want.items() if got[k] != v}
            if bad or not (got['class'] or '').startswith('ship_'):
                raise SessionError(f'slot {key}: hull {saved} differs from the spec (got, want): {bad or got["class"]}')
            hulls[key] = saved
    if scenarios.digest(snapshot) != sha:
        raise SessionError('save changed while reading it')
    return {'format': SESSION_FORMAT, 'base_start': spec['base_start'], 'base_class': spec['base_class'], 'mode': spec['mode'],
            'players': spec['players'], 'snapshot': {'path': str(snapshot.resolve()), 'sha256': sha, 'bytes': snapshot.stat().st_size},
            'seed': header.get('seed'), 'build': header.get('build'), 'player_ship': header['player']['ship'],
            'player_macro': header['player']['macro'], 'player_sector': header['player']['sector'], 'fixture': fixture,
            'hulls': hulls, 'slots': {k: {'gamestart': v['gamestart'], 'class': v['class'], 'sector': v['hull']['sector'],
                                          'kit': v['kit']} for k, v in spec['slots'].items()},
            'spec_sha256': hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest()}


def session_of(path):
    """A registered session (session.json), checked for the fields the overlay builders read."""
    s = load_json(path, SESSION_FORMAT)
    for key in ('base_start', 'mode', 'players', 'snapshot', 'player_ship', 'player_sector', 'fixture', 'hulls', 'slots'):
        if key not in s:
            raise SessionError(f'{path}: {key} missing')
    if len(s['fixture']) != 12 or not all(identity.saved(f) for f in s['fixture'] + [s['player_ship']] + list(s['hulls'].values())):
        raise SessionError(f'{path}: invalid saved IDs')
    if set(s['hulls']) != set(s['slots']):
        raise SessionError(f'{path}: every per-player start needs its hull')
    return s


def rebase(session, base, out):
    """A full-stack overlay bound to the session save (checksum, fixture, watch list)."""
    snapshot = Path(session['snapshot']['path'])
    if scenarios.digest(snapshot) != session['snapshot']['sha256']:
        raise SessionError(f'{snapshot} is not the registered session save (sha256 differs)')
    out, header = scenarios.build_rebased(base, snapshot, out)
    if header['start'] != session['base_start']:
        raise SessionError('rebased save start differs from the session')
    return out


# ---------------------------------------------------------------- command line

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    s = sub.add_parser('spec', help='choose the base start and per-player starts; write spec.json')
    gamestarts.add_roots(s)
    s.add_argument('--base-start', required=True, help="the session's own start (slot 1; the save every client loads)")
    s.add_argument('--players', type=int, default=2)
    s.add_argument('--mode', choices=identity.MODES, default='factions')
    s.add_argument('--start', action='append', help='K=GAMESTART: slot K (2..N) takes this stock start')
    s.add_argument('--omit', action='append', help='K=PART,PART: parts of slot K\'s start that are not applied (' +
                   ', '.join(gamestarts.OMITTABLE) + ')')
    s.add_argument('--out', required=True)
    c = sub.add_parser('capture-overlay', help='the capture overlay of a spec (one disposable lane run)')
    c.add_argument('--spec', required=True)
    c.add_argument('--fixedtick-dll', required=True)
    c.add_argument('--out', required=True)
    c.add_argument('--collect', default='<collect dir>')
    r = sub.add_parser('register', help='read the captured save into session.json')
    r.add_argument('--spec', required=True)
    r.add_argument('--snapshot', required=True)
    r.add_argument('--out', required=True)
    b = sub.add_parser('rebase', help='bind a full-stack overlay to the session save')
    b.add_argument('--session', required=True)
    b.add_argument('--base', required=True)
    b.add_argument('--out', required=True)
    args = p.parse_args(argv)
    try:
        if args.command == 'spec':
            catalog = gamestarts.Catalog(*gamestarts.roots(args))
            starts = slot_values(args.start, '--start')
            spec = build_spec(catalog, args.base_start, args.players, args.mode, starts, slot_values(args.omit, '--omit'))
            out = write_new(args.out, json.dumps(spec, indent=1, sort_keys=True) + '\n')
            print(f'spec {out}: base {spec["base_start"]} (class {spec["base_class"]}), {spec["players"]} players, {spec["mode"]}')
            for key, slot in sorted(spec['slots'].items(), key=lambda kv: int(kv[0])):
                kit = slot['kit']
                print(f"  slot {key}: {slot['gamestart']} (class {slot['class']}) hull {slot['hull']['macro']} in {slot['hull']['sector']}, "
                      f"money {kit['money']}, omitted {','.join(kit['omitted']) or '-'}; presentation only: {', '.join(slot['presentation'])}")
        elif args.command == 'capture-overlay':
            spec = load_json(args.spec, SPEC_FORMAT)
            out = build_capture(spec, args.fixedtick_dll, args.out)
            print(f'capture overlay {out}')
            print('coordinator (X4 closed, one disposable lane):')
            print(capture_command(spec, out, args.collect))
        elif args.command == 'register':
            spec = load_json(args.spec, SPEC_FORMAT)
            session = register(spec, args.snapshot)
            out = write_new(args.out, json.dumps(session, indent=1, sort_keys=True) + '\n')
            print(f"session {out}: {session['base_start']} sha256 {session['snapshot']['sha256']} player ship {session['player_ship']} "
                  f"in {session['player_sector']}, hulls {session['hulls'] or '-'}")
            print(f'next: python tools/dev/native_gate_session_start.py rebase --session {out} --base <full-stack overlay> --out <dir>')
        else:
            session = session_of(args.session)
            out = rebase(session, args.base, args.out)
            print(f"overlay {out} bound to {session['snapshot']['path']}")
            print(f'next: python tools/dev/native_gate_players.py overlay --session {args.session} --base {out} --gate-dll <dll> '
                  '--probe tools/dev/native_gate_probe.lua --out <dir> --players N --mode M --source stock|none')
            print(f"runner: --snapshot-in {session['snapshot']['path']} (e.g. .local/census/runStart.sh, docs/LOCKSTEP-SCENARIOS.txt)")
    except (SessionError, gamestarts.CatalogError, ValueError, OSError, ET.ParseError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
