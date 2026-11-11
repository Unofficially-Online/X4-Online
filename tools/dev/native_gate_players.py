"""N-player lockstep session builder: player factions or coop, one ship per player (no installation, no launch).

Sibling of the two-player sandbox (tools/dev/native_gate_sandbox.py, docs/LOCKSTEP-SANDBOX.txt); it reuses
the sandbox's base-overlay checks and adds the player identity plan
(tools/dev/native_gate_player_identity*.{lua,xml,py}, reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt):

  --mode factions  slot k's ship is owned by x4lan_player<k>; the slot flying the occupied player ship keeps
                   the vanilla `player` faction (--occupied pool moves it too); relations between the slot
                   factions come from --relations / --relation A-B=NAME
  --mode coop      every slot ship is owned by `player` (one faction, different ships)

  --source wire    humans: each client samples its device and flies its own slot ship (pilot by wire)
  --source script  no humans: every slot ship follows the deterministic pilot script on every client
  --source stock   every slot flies through X4's own player control path on every client, recorded from
                   its own client's stock input (lockstep_pilot_stock.h, lockstep_pilot_slots.h; default
                   window 40..640 scripted, 40..72000 for people); slot ships default to Argon scouts with a
                   working cockpit, moved near slot 1 at the setup tick (identity.STOCK_SHIPS/STOCK_STARTS,
                   --ships replaces them, --start replaces one slot's place); --stock-script demo flies them without humans (demo,
                   demo2..demo4 per slot), --present makes client k's stock camera, HUD and UI present slot
                   k's ship (lockstep_pilot_present.h), --fire record lets each slot fire its primary guns
                   (lockstep_pilot_fire.h: straight ahead), --fire script on a fixed schedule, --ui-replay turns stock UI
                   actions into events of the slot (lockstep_ui_replay.h: travel, soft target, orders; needs --present)
  --source none    identity only (no piloting); allows ships created by MD (--create)
  --orders demo    scripted orders check the ownership table (default with --source script); they share the
                   input blob with the pilot records through the input multiplexer, so wire works too
  --start K=SECTOR@X,Y,Z[/YAW]
                   different starts: after ownership, MD moves slot K's ship (or a saved ship ID in place of K)
                   to a fixed sector-local point of the saved sector; --start-counterparts also moves each
                   moved fixture actor's battle counterpart 1500 m further along z, so its Attack order (and
                   with it the Steering solver a pilot slot needs) stays local. SECTOR is a sector macro
                   (e.g. cluster_01_sector001_macro; any sector, known or not) or a saved sector ID of a
                   sector the player knows (snapshot 005: 0000000000000729 Argon Prime, the start)
  --third-sector   slots may start in more sectors than are covered. Without census keys, physics scenes,
                   camera-centred traffic and highway parameters answer for two sectors (lockstep_region_camera.h):
                   slot 1's and the first other one in slot order. --set native_gate_view_scenes=true with
                   --set native_gate_viewpoint_standins=N (needs native_gate_viewpoint_policy) covers N sectors, up to
                   eight; a scene a slot leaves serves the next sector once its bodies are gone, VSC_GRACE (600) ticks
                   after the release at the latest (lockstep_view_scenes.h). A slot beyond the covered sectors has none
                   of them on any client, so it is refused unless this asks for it; keys in the base overlay are not
                   seen here, so pass this for those. A ship without a --start that the builder does not know gets a
                   warning; in a run, VIEW_POINTS_UNCOVERED lines and REGION_CAMERA uncovered= show what happened.

  --session S.json a session from any usable stock start (tools/dev/native_gate_session_start.py register): the
                   session save's player ship, fixture actors and sectors replace the snapshot-005 defaults below,
                   slots with a per-player start fly their hull from the save, and their kits (money, reputation,
                   blueprints, research, map knowledge) are applied at the setup tick (reports/LOCKSTEP-STARTS-20261101.txt)

Defaults (snapshot capture 005, one zone of Argon Prime): slot 1 = 00000000000ae79c (the player's scout,
occupied on every client), slots 2..4 = fixture actors 3..5 (0000000000176ad5, 0000000000176b36,
0000000000176b97: Argon S fighters at zone offset -395/0/+700..900 m from slot 1). Up to 8 players
(PILOT_SLOTS): slots 5..8 take --ships, --create or a --session's starts (stock piloting knows four). --battle engage
(default, as in the sandbox) gives them their scripted Attack orders: a waiting fixture has no Steering
solver, so calm slots 2..N cannot be flown (live pf3/pc3: s2/s3 applied 0, no_solver).

  --tick187 observe|canonical, --fp default|observe, --set native_gate_KEY=VALUE
                   census keys on top of the base overlay (they replace the base's own lines)

  python tools/dev/native_gate_players.py config --players 3 --mode coop --source script
  python tools/dev/native_gate_players.py overlay --base <overlay> --gate-dll <dll> --probe tools/dev/native_gate_probe.lua \\
      --out <new overlay dir> --players 4 --source stock --stock-script demo --present --tick187 observe --fp default
  python tools/dev/native_gate_players.py overlay --base <protocol-3 full-stack overlay> --gate-dll <dll> \\
      --probe tools/dev/native_gate_probe.lua --out <new overlay dir> --players 3 --mode factions --source script
  python tools/dev/native_gate_players.py plan --players 3 --mode factions
  python tools/dev/native_gate_players.py compare <client logs> --players 3 --mode factions --source script
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_sandbox as sandbox          # noqa: E402  (shared base checks and helpers; not modified here)
import native_gate_player_identity as identity  # noqa: E402
import native_gate_player_orders as player_orders  # noqa: E402
import native_gate_defaults as session_defaults   # noqa: E402  (the session defaults: tools/dev/native_gate_defaults.lua)

MAX_PLAYERS = 8   # the pilot modules' slots (PILOT_SLOTS, lockstep_pilot_inputs.h; the wire carries 8 clients)
import check_gate_options as gate_options          # noqa: E402
import native_gate_session_start as session_start  # noqa: E402  (a session from a stock start, per-player starts)

ROOT = Path(__file__).resolve().parents[2]
HEADER = '-- Lockstep players (tools/dev/native_gate_players.py); reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt'
SOURCES = ('wire', 'script', 'stock', 'none')
STOCK_DEMOS = ('demo', 'demo2', 'demo3', 'demo4')   # --stock-script demo: one per slot (native_gate_probe.lua)
ORDERS_DELAY, ORDERS_TICK = 3, 40
IDENTITY_FILES = ('ui/native_gate_player_identity_core.lua', 'ui/native_gate_player_identity.lua')
RESPAWN_MD = 'md/native_gate_pilot_respawn.xml'   # native_gate_pilot_respawn: X4's own create_ship and teleport_player
# The pool factions' library (tools/dev/build_player_factions.py): x4lan_player1..16 with their own accounts. A factions
# plan with kits sets slot k's money on x4lan_player<k>'s own account (the identity wallet step, which refuses a faction
# without one); the overlay ships the source library so a base frozen before the accounts (frozen-v3) gets them.
POOL_LIBRARY = session_start.POOL_LIBRARY
ORDERS_FILES = ('ui/native_gate_player_orders_core.lua', 'ui/native_gate_player_orders.lua')
MUX_FILES = ('ui/native_gate_inputs_core.lua',)   # protocol 3 input multiplexer (pilot, orders, hit claims)
UI_REPLAY_FILES = ('ui/native_gate_ui_replay_core.lua', 'ui/native_gate_ui_build_core.lua', 'ui/native_gate_ui_inventory_core.lua')
UI_REPLAY_FAMILIES = ('travel', 'target', 'orders', 'autopilot', 'dock', 'weapons', 'missiles', 'queue', 'events', 'flight', 'trade', 'missions', 'build',
                      'assets', 'conversation', 'faction', 'inventory')
UI_GUARD_GROUPS = ('flight', 'orders', 'fleet', 'trade', 'faction', 'assets', 'build', 'conflict', 'missions', 'events', 'conversation',
                   'session', 'cheat', 'editor')   # lockstep_ui_guard.h
UI_REPLAY_FLIGHT = ('autopilot', 'dock', 'weapons')
# The scripted run without people, per client, ticks relative to the window start: the J key's record and the FFI
# entry for travel, target and match speed; slot 1 as well, it takes the same path through its own functions.
# Travel spans 240..300 ticks: a drive charges for seconds before it engages. Meant for runs without --stock-script: the
# demos' full stop ends a travel, as it should, and the J key is a toggle. Slot 2 accelerates while slot 1 matches it.
UI_REPLAY_DEMO = {1: ((60, 'travel'), (300, 'travel_stop'), (540, 'target_slot', 2), (580, 'match', 1), (680, 'match', 0), (720, 'target_none')),
                  2: ((30, 'travel_key'), (330, 'travel_key'), (360, 'travel'), (600, 'travel_stop'), (605, 'input', 'state', 0xd, 1), (655, 'input', 'state', 0xd, 0),
                      (620, 'target_slot', 1), (690, 'match', 1), (740, 'match', 0), (780, 'target_none'))}
# The flight families, a run of their own (autopilot and travel would fight): each slot flies to the other on autopilot and
# stops, selects primary group 2 by its key and group 1 by the FFI, asks to dock and undock (nothing near: X4 refuses on every
# client alike; with --ui-replay-dock slot 2 asks that station).
UI_REPLAY_FLIGHT_DEMO = {1: ((60, 'ap_to', 2), (180, 'ap_toggle'), (200, 'wkey', 0x8c), (260, 'wgroup', 1, 1), (300, 'quickdock'), (330, 'undock')),
                         2: ((30, 'ap_to', 1), (150, 'ap_stop'), (180, 'wkey', 0x8c), (240, 'wgroup', 1, 1), (320, 'quickdock'), (350, 'undock'))}
# Missiles (--ui-replay target,missiles --fire record|script): each slot targets the other and holds the secondary trigger;
# its launchers fly at that target. Client 2 drops its target while holding (the launchers stop), client 1 releases.
UI_REPLAY_MISSILE_DEMO = {1: ((60, 'target_slot', 2), (120, 'missile', 1), (420, 'missile', 0), (480, 'target_none')),
                          2: ((90, 'target_slot', 1), (150, 'missile', 1), (300, 'target_none'), (450, 'missile', 0))}
# Ships of snapshot 005 with missile launchers in their secondary group (U6c, read from save_009): fixture actors 3..5, Argon S
# fighters, one weapon_gen_s_torpedo_02_mk1 in secondary group 1 and 8 light torpedoes (missile_gen_s_torpedo_01_mk1) each.
# The scouts (slot 1, the stock slots) have two lasers or plasma guns and no launcher connection; slot 1 must stay the player's.
MISSILE_SHIPS = ('0000000000176ad5', '0000000000176b36', '0000000000176b97')
# Missions and conversations (--ui-replay events,missions,conversation --ui-guard all): the test offer (MD fixture, both clients at
# the same tick), slot 1 briefs, accepts, activates, talks twice (the wrapped dialog and the C function), then clears the active
# mission and aborts; slot 2 tries the same entries and is refused (why=slot or the guard's row), nothing reaches MD for it.
UI_REPLAY_MISSIONS_DEMO = {1: ((60, 'mis_fixture', 1), (120, 'briefing', 'offer'), (150, 'briefing_stop', 'offer'), (180, 'offer_accept'),
                               (240, 'mis_active'), (270, 'briefing', 'mission'), (300, 'briefing_button', 'mission'), (330, 'briefing_stop', 'mission'),
                               (360, 'conv_start', 'fixture'), (420, 'dialog', 1), (480, 'conv_start', 'fixture'), (540, 'dialog_native', 2),
                               (600, 'mis_none'), (660, 'mis_abort')),
                           2: ((60, 'mis_fixture', 1), (150, 'offer_accept'), (250, 'briefing', 'mission'), (400, 'mis_abort'),
                               (430, 'dialog_native', 1), (450, 'conv_start', 'fixture'), (630, 'mis_active'))}
# Personal missions per player (--missions-per-player, lockstep_mission_owners.h): two offers on both clients; slot 1
# accepts one and slot 2 the other, so each mission is its own player's on every client (MD's player.entity in its rows)
# and each client lists and shows its own. Slot 2 then clears its active mission, sets it again and aborts the mission:
# slot 1's active mission stays as it was through all of that.
UI_REPLAY_MPP_DEMO = {1: ((60, 'mis_fixture', 1), (90, 'mis_fixture', 1), (150, 'offer_accept')),
                      2: ((60, 'mis_fixture', 1), (90, 'mis_fixture', 1), (200, 'offer_accept'), (400, 'mis_none'), (450, 'mis_active'),
                          (600, 'mis_abort'))}
UI_REPLAY_OP_FAMILY = {'missile': 'missiles', 'travel': 'travel', 'travel_stop': 'travel', 'travel_key': 'travel', 'target_slot': 'target', 'target_none': 'target',
                       'match': 'target', 'input': 'target', 'ap_to': 'autopilot', 'ap_obj': 'autopilot', 'ap_stop': 'autopilot', 'ap_toggle': 'autopilot',
                       'quickdock': 'dock', 'dock_at': 'dock', 'undock': 'dock', 'wgroup': 'weapons', 'wkey': 'weapons',
                       'flight_assist': 'flight', 'turret_mode': 'flight', 'turret_missile': 'flight', 'drones': 'flight', 'drone_mode': 'flight', 'launch': 'flight', 'launch_stock': 'flight', 'turrets_mode': 'flight', 'turrets_armed': 'flight', 'drones_armed': 'flight',
                       'drop_cargo': 'flight',
                       'mis_fixture': 'missions', 'gm_limit': 'missions', 'slot_kill': 'events', 'briefing': 'missions', 'briefing_stop': 'missions', 'briefing_button': 'missions',
                       'offer_accept': 'events', 'mis_active': 'missions', 'mis_none': 'missions', 'mis_abort': 'missions',
                       'conv_start': 'conversation', 'dialog': 'conversation', 'dialog_native': 'conversation',
                       'hire_talk': 'conversation', 'hire_confirm': 'conversation'}
UI_REPLAY_FLEET_ORDERS = 500        # window-relative tick of the scripted orders, client 1's
# The queue editor (--ui-replay orders,queue --ui-replay-fleet <ship>): both clients edit the fleet ship's queue by
# index as the map's rows do. Client 1 clears it, adds MoveWait (create + parameter + enable) and Wait in the next two
# ticks (their indices predicted before the clear applied: mapped), moves Wait to the front, removes it; client 2 adds a
# Wait and removes it. FLEET and SECTOR stand for the ship and the stock sector.
UI_REPLAY_QUEUE_DEMO = {1: ((500, 'clear', 'FLEET'), (501, 'q_move', 'FLEET', 'SECTOR', 3000, 100000, 3000), (502, 'q_wait', 'FLEET'),
                            (600, 'q_adjust', 'FLEET', 2, 1), (700, 'q_remove', 'FLEET', 1)),
                        2: ((550, 'q_wait', 'FLEET'), (650, 'q_remove', 'FLEET', 3))}
UI_REPLAY_WINDOW = 800              # the script's last event, window-relative
OWN_KEYS = re.compile(r'native_gate_player_identity[a-z_]*|native_gate_player_orders[a-z_]*')
CREATE = re.compile(r'(\d+)=([a-z][a-z0-9_]*_macro)@(-?\d+),(-?\d+),(-?\d+)(?:/(-?\d+))?(?:#(\d+))?')
START = re.compile(r'(\d{1,2}|[0-9a-f]{16})=([0-9a-f]{16}|[a-z][a-z0-9_]*_macro)@(-?\d+),(-?\d+),(-?\d+)(?:/(-?\d+))?')
COUNTERPART_OFFSET = 1500   # metres along z between a moved fixture actor and its moved counterpart


def wallet_library(plan):
    """True when the plan's wallet steps set pool faction accounts (factions mode with kits)."""
    return plan.get('mode') == 'factions' and bool(plan.get('kits'))


def ids(text):
    values = [sandbox.saved_id(v) for v in text.split(',') if v]
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError('slot ships must be distinct')
    return values


def relation_override(text):
    m = re.fullmatch(r'(\d+)-(\d+)=(allied|neutral|hostile)', text)
    if not m or not int(m[1]) < int(m[2]):
        raise argparse.ArgumentTypeError('relation overrides are A-B=allied|neutral|hostile with A < B')
    return (int(m[1]), int(m[2])), m[3]


def create_spec(text):
    """K=MACRO@X,Y,Z[/YAW][#SEED]: a ship MD creates at the setup tick in the occupied ship's sector, at
    sector-local metres (snapshot 005: the fixture area is at y=100000, e.g. 3=ship_arg_s_fighter_01_a_macro@0,100000,-600)."""
    m = CREATE.fullmatch(text)
    if not m:
        raise argparse.ArgumentTypeError('create specs are SLOT=MACRO@X,Y,Z[/YAW][#SEED] (sector-local metres, degrees)')
    return int(m[1]), {'macro': m[2], 'seed': int(m[7] or 4800 + int(m[1])), 'sector': 'player',
                       'x': int(m[3]), 'y': int(m[4]), 'z': int(m[5]), 'yaw': int(m[6] or 0)}


def start_spec(text):
    """K=SECTOR@X,Y,Z[/YAW] or SHIP=SECTOR@X,Y,Z[/YAW]: slot K's ship (or a saved ship) moved at the setup tick
    to sector-local metres of a sector macro or saved sector (e.g. 2=cluster_01_sector001_macro@0,30000,0)."""
    m = START.fullmatch(text)
    if not m:
        raise argparse.ArgumentTypeError('start specs are K=SECTOR@X,Y,Z[/YAW] or SHIP=SECTOR@X,Y,Z[/YAW] '
                                         '(SECTOR: sector macro or saved ID; metres, degrees)')
    return {'target': int(m[1]) if len(m[1]) < 16 else m[1], 'sector': m[2],
            'x': int(m[3]), 'y': int(m[4]), 'z': int(m[5]), 'yaw': int(m[6] or 0)}


COVERED_SECTORS = 2   # without the census keys below: slot 1's sector and the first other one in slot order


def covered_sectors(args):
    """The sectors a run covers, from its --set keys: native_gate_view_scenes (a physics scene per sector, up to eight)
    with native_gate_viewpoint_standins=N (traffic and highway parameters for N sectors) cover N, anything else the
    region camera's two. Keys in the base overlay are not seen here; with --defaults-only the session defaults count
    (both keys are defaults there: eight sectors)."""
    sets = {}
    for text in getattr(args, 'set', None) or []:
        m = re.fullmatch(r'(native_gate_[a-z0-9_]+)=(.+)', text)
        if m:
            sets[m[1]] = m[2].strip()
    # --defaults-only (a people session: tools/dev/native_gate_defaults.lua is the run): the defaults arm a physics scene per
    # sector and eight view point stand-ins, unless a --set turns one off (ns4, 10 Nov: a four-start session was refused).
    defaults = getattr(args, 'defaults_only', False)
    scenes = sets.get('native_gate_view_scenes', 'true' if defaults else None)
    standins = sets.get('native_gate_viewpoint_standins', '8' if defaults else None)
    if scenes == 'true' and standins in ('3', '4', '5', '6', '7', '8'):
        return int(standins)
    return COVERED_SECTORS


def sector_name(token):
    """One name per sector: the macro and the saved ID of the start sector are the same place."""
    token = str(token).lower()
    return identity.SECTOR_ALIASES.get(token, token)


def slot_sectors(args, ships, starts, creates):
    """Slot -> sector after the setup tick, in slot order; None where the builder cannot know (a ship it has no record of, no --start)."""
    moved = {row['object']: sector_name(row['sector']) for row in starts}
    def of(ship):
        if ship in moved:
            return moved[ship]
        known = ship_sectors(args)
        return sector_name(known[ship]) if ship in known else None
    return {k + 1: of(args.occupied_ship) if k + 1 in creates else of(ship) for k, ship in enumerate(ships)}


def sector_warnings(args, ships, starts, creates):
    """The warnings of a session's sectors; a slot beyond the covered sectors is a ValueError without --third-sector."""
    sectors = slot_sectors(args, ships, starts, creates)
    limit = covered_sectors(args)
    out, covered = [], []
    for slot, sector in sectors.items():
        if sector and sector not in covered and len(covered) < limit:
            covered.append(sector)
    beyond = [f'slot {slot} ({sector})' for slot, sector in sectors.items() if sector and sector not in covered]
    if beyond:
        message = (', '.join(beyond) + f': beyond the {limit} covered sectors ({", ".join(covered)}). A view point there has no physics scene '
                   '(without native_gate_view_scenes), no camera-centred mass traffic and no highway parameter (without '
                   'native_gate_viewpoint_standins=N) on any client (lockstep_region_camera.h). --set native_gate_view_scenes=true with '
                   '--set native_gate_viewpoint_standins=N covers N sectors, up to eight.')
        if not args.third_sector:
            raise ValueError(message + ' Put the slot in a covered sector, set those keys, or pass --third-sector to build it anyway '
                             '(keys in the base overlay are not seen here).')
        out.append('WARNING ' + message)
    unknown = [str(slot) for slot, sector in sectors.items() if sector is None]
    if unknown:
        out.append('WARNING slot ' + ', '.join(unknown) + ': no --start and a ship this builder has no sector for: check VIEW_POINTS_UNCOVERED in the run.')
    return out


def starts_of(args, ships):
    """Start placements in command-line order; slot targets resolve to the slot's saved ship."""
    out = []
    for spec in args.start or []:
        target = spec['target']
        if isinstance(target, int):
            if not 1 <= target <= len(ships) or ships[target - 1] is None:
                raise ValueError(f'--start slot {target} has no saved slot ship')
            target = ships[target - 1]
        row = {'object': target, 'sector': spec['sector'], 'x': spec['x'], 'y': spec['y'], 'z': spec['z'], 'yaw': spec['yaw']}
        out.append(row)
        if args.start_counterparts and target in fixtures(args)[:6]:
            out.append(dict(row, object=counterpart(target, fixtures(args)), z=row['z'] + COUNTERPART_OFFSET))
    return out


def fixtures(args):
    """The 12 battle fixture actors of the save the session loads (Argon 1..6, Xenon 7..12)."""
    world = getattr(args, 'session_data', None)
    return tuple(world['fixture']) if world else sandbox.FIXTURES


def ship_sectors(args):
    """Saved ship -> sector (macro or saved ID) where the builder knows it before the setup tick."""
    world = getattr(args, 'session_data', None)
    if not world:
        return identity.SHIP_SECTORS
    out = {ship: world['player_sector'] for ship in [world['player_ship']] + list(world['fixture'])}
    out.update({world['hulls'][k]: world['slots'][k]['sector'] for k in world['hulls']})
    return out


def apply_session(args):
    """--session (native_gate_session_start.py register): the session save's player ship, fixture and per-player start
    hulls replace the snapshot-005 defaults; the per-player starts (kits) go into the plan by default."""
    world = session_start.session_of(args.session)
    args.session_data = world
    if args.players != world['players'] or args.mode != world['mode']:
        raise ValueError(f"--session {args.session}: {world['players']} players in {world['mode']} mode, not {args.players} in {args.mode} "
                         '(the per-player starts were resolved for that mode)')
    if args.occupied_ship not in (identity.PLAYER_SHIP, world['player_ship']):
        raise ValueError(f"--session: the occupied ship is the session save's player ship {world['player_ship']}")
    args.occupied_ship = world['player_ship']
    if world['hulls'] and args.source in ('script', 'wire'):
        raise ValueError('--session with per-player start hulls needs --source stock or none: a hull waits in its Wait order without the '
                         'Steering solver that wire/script piloting drives')
    hulls = {int(k): v for k, v in world['hulls'].items()}
    if args.create and {slot for slot, _ in args.create} & set(hulls):
        raise ValueError('--create: a slot with a per-player start flies its session hull')
    if args.ships:
        wrong = [k for k, ship in hulls.items() if k > len(args.ships) or args.ships[k - 1] != ship]
        if wrong:
            raise ValueError(f'--ships: slots {wrong} have per-player starts; their ships are the session hulls {[hulls[k] for k in wrong]}')
    else:
        # Slot 1: the save's player ship; slot k: its hull, else fixture actor k+1 (as the snapshot-005 defaults).
        args.ships = [world['player_ship']] + [hulls.get(k) or world['fixture'][k] for k in range(2, args.players + 1)]
    return args


def defaults(args):
    """Fill source-dependent defaults in place (window, world stride, orders, capture log, devices)."""
    if getattr(args, 'session', None):
        apply_session(args)
    people = args.source == 'stock' and not args.stock_script
    if args.source == 'stock' and not args.ships and not args.create:
        if args.players > len(identity.STOCK_SHIPS):
            raise ValueError(f'stock piloting knows {len(identity.STOCK_SHIPS)} cockpit ships: give --ships or --create for slots '
                             f'{len(identity.STOCK_SHIPS) + 1}..{args.players}')
        # The stock ships sit far apart in the save: every slot without its own --start comes to slot 1's sector, or a partial
        # --start would leave the rest in sectors of their own.
        given = {s['target'] for s in args.start or []}
        args.start = list(args.start or []) + [{'target': k, 'sector': identity.STOCK_SECTOR, 'x': x, 'y': y, 'z': z, 'yaw': yaw}
                                               for k, (x, y, z, yaw) in sorted(identity.STOCK_STARTS.items())
                                               if k <= args.players and k not in given and identity.STOCK_SHIPS[k - 1] not in given]
        args.ships = list(identity.STOCK_SHIPS[:args.players])
    if args.window is None:
        args.window = [40, sandbox.PILOT_WINDOW_PEOPLE] if people else {'wire': [30, sandbox.PILOT_WINDOW_DEFAULT], 'stock': [40, 640]}.get(args.source, [30, 600])
    if args.world_stride is None:
        args.world_stride = 18000 if people else 1800 if args.source in ('wire', 'stock') else 300
    if args.orders is None:
        args.orders = 'demo' if args.source == 'script' else 'none'
    if args.capture_log is None:
        args.capture_log = args.source in ('wire', 'stock')
    if args.block_orders is None:
        args.block_orders = args.source in ('wire', 'stock')
    if args.devices is None:
        args.devices = ['kbm'] + ['xinput'] * (args.players - 1)
    if args.pads is None:
        args.pads = [0] + list(range(args.players - 1))
    return args


def plan_of(args):
    if not 2 <= args.players <= MAX_PLAYERS:
        raise ValueError(f'2..{MAX_PLAYERS} players (the pilot modules drive at most {MAX_PLAYERS} slot ships)')
    ships = args.ships or list(identity.DEFAULT_SHIPS[:args.players])
    creates = dict(args.create or [])
    if creates and args.source != 'none':
        raise ValueError('created slot ships have no saved ID: the pilot module cannot drive them yet (use --source none)')
    if args.start_counterparts and not args.start:
        raise ValueError('--start-counterparts needs --start')
    starts = starts_of(args, [None if k + 1 in creates else s for k, s in enumerate(ships)])
    args.sector_warnings = sector_warnings(args, ships, starts, creates)
    world = getattr(args, 'session_data', None)
    kits = [world['slots'][k]['kit'] for k in sorted(world['slots'], key=int)] if world else None
    return identity.build_plan(args.players, args.mode, ships, args.relations, dict(args.relation or []), args.occupied_ship,
                               args.occupied, args.reputation, args.setup_tick, creates, starts, kits)


def ui_replay_lines(args):
    """The UI replay block: families, the script without people, the test fleet (a `player` ship with a pilot)."""
    if args.ui_replay is None:
        if (args.ui_replay_script or args.ui_replay_fleet or args.ui_replay_dock or args.ui_replay_min_hull is not None or args.ui_guard
                or args.missions_per_player):
            raise ValueError('--ui-replay-script, --ui-replay-fleet, --ui-replay-dock, --ui-replay-min-hull, --ui-guard and '
                             '--missions-per-player need --ui-replay')
        return []
    if args.source != 'stock' or not args.present:
        raise ValueError('--ui-replay needs --source stock --present')
    names = [n for n in args.ui_replay.split(',') if n]
    if args.ui_replay != 'all' and (not names or any(n not in UI_REPLAY_FAMILIES for n in names) or len(set(names)) != len(names)):
        raise ValueError(f"--ui-replay is all or a comma list of {', '.join(UI_REPLAY_FAMILIES)}")
    orders = args.ui_replay == 'all' or 'orders' in names
    queue = 'queue' in names
    if args.ui_replay_fleet and not (orders or queue):
        raise ValueError('--ui-replay-fleet needs the orders or the queue family')
    if queue and args.ui_replay_script and not (args.ui_replay_fleet and orders):
        raise ValueError('--ui-replay queue with --ui-replay-script needs the orders family and --ui-replay-fleet (the plan clears its queue first)')
    if args.ui_replay_fleet and args.mode != 'coop':
        raise ValueError("--ui-replay-fleet needs --mode coop: slot factions are not `player`, the stock UI orders `player` ships")
    dock = args.ui_replay == 'all' or 'dock' in names
    if 'missiles' in names and ('target' not in names or getattr(args, 'fire', 'off') == 'off'):
        raise ValueError('--ui-replay missiles needs the target family and --fire record or script (pilot fire leases the launchers)')
    if args.ui_replay_dock and not (args.ui_replay_fleet or dock):
        raise ValueError('--ui-replay-dock needs --ui-replay-fleet or the dock family')
    if ('build' in names or 'assets' in names) and not args.ui_guard:
        raise ValueError('--ui-replay build/assets need --ui-guard (they claim its hooks)')
    if 'faction' in names and not args.ui_guard:
        raise ValueError('--ui-replay faction needs --ui-guard (it claims its hooks)')
    if args.ui_replay_min_hull is not None and not 1 <= args.ui_replay_min_hull <= 100:
        raise ValueError('--ui-replay-min-hull is a hull percentage 1..100')
    if 'missiles' in names and not set(args.ships or identity.DEFAULT_SHIPS[:args.players]) & set(MISSILE_SHIPS):
        warning = ('WARNING --ui-replay missiles: no slot ship is one of the known launcher ships of snapshot 005 '
                   f'({", ".join(MISSILE_SHIPS)}); PFIRE missiles= will count empty only (U6b). Give --ships with one of them.')
        warnings = list(getattr(args, 'sector_warnings', []))
        args.sector_warnings = warnings + ([warning] if warning not in warnings else [])
    lines = ['X4LAN_CONFIG.native_gate_ui_replay = ' + ('true' if args.ui_replay == 'all' else '{' + ','.join(repr(n) for n in names) + '}')]
    if args.ui_guard:
        # The UI guard (lockstep_ui_guard.h): what no armed family replays is refused in the local phases.
        groups = [g for g in args.ui_guard.split(',') if g]
        if args.ui_guard != 'all' and (not groups or any(g not in UI_GUARD_GROUPS for g in groups) or len(set(groups)) != len(groups)):
            raise ValueError(f"--ui-guard is all or a comma list of {', '.join(UI_GUARD_GROUPS)}")
        lines.append("X4LAN_CONFIG.native_gate_ui_guard = " + ("'all'" if args.ui_guard == 'all' else '{' + ','.join(repr(g) for g in groups) + '}'))
    # Missions and conversations (lockstep_ui_missions.h): captured in the guard's hook, so the guard holds their groups.
    for family in ('missions', 'conversation'):
        if family in names and not (args.ui_guard == 'all' or family in (args.ui_guard or '').split(',')):
            raise ValueError(f'--ui-replay {family} needs --ui-guard all or with the {family} group')
    if args.missions_per_player:
        # Slots 2..N's accepted missions in their own scope (lockstep_mission_owners.h): the events and missions families carry them.
        if args.ui_replay == 'all' or not {'events', 'missions'} <= set(names):
            raise ValueError('--missions-per-player needs --ui-replay with the events and missions families (named)')
        lines.append('X4LAN_CONFIG.native_gate_missions_per_player = true')
    if args.ui_replay_min_hull is not None:
        lines.append(f'X4LAN_CONFIG.native_gate_ui_replay_min_hull = {args.ui_replay_min_hull}')
    if args.ui_replay_fleet:
        lines.append(f'X4LAN_CONFIG.native_gate_ui_replay_fleet = {sandbox.lua_list([repr(s) for s in args.ui_replay_fleet])}')
    if args.ui_replay_script:
        if args.window[1] - args.window[0] < UI_REPLAY_WINDOW:
            raise ValueError(f'--ui-replay-script needs a window of {UI_REPLAY_WINDOW} ticks (--window 40 {40 + UI_REPLAY_WINDOW})')
        families = names if args.ui_replay != 'all' else [f for f in UI_REPLAY_FAMILIES if f != 'missiles']   # all = true: missiles is named
        explicit = args.ui_replay != 'all'   # all: the first demo (travel, target, orders) as before
        flight = explicit and any(f in UI_REPLAY_FLIGHT for f in families)
        missiles = 'missiles' in families
        if flight and ('travel' in families or 'target' in families or missiles):
            raise ValueError('--ui-replay-script: the flight families (autopilot, dock, weapons) run without travel, target and missiles')
        if missiles and 'travel' in families:
            raise ValueError('--ui-replay-script: missiles run without travel (its own plan: target and missiles)')
        talk = explicit and ('missions' in families or 'conversation' in families)
        if talk and (flight or missiles or 'travel' in families or 'target' in families):
            raise ValueError('--ui-replay-script: missions and conversation run on their own plan (with events)')
        demo = (UI_REPLAY_FLIGHT_DEMO if flight else UI_REPLAY_MISSILE_DEMO if missiles else
                (UI_REPLAY_MPP_DEMO if args.missions_per_player else UI_REPLAY_MISSIONS_DEMO) if talk else UI_REPLAY_DEMO)
        plans = []
        for client in range(1, args.players + 1):
            plan = [row for row in demo[1 if client == 1 else 2] if UI_REPLAY_OP_FAMILY[row[1]] in families]
            if client == 2 and args.ui_replay_dock and flight and 'dock' in families:
                plan.append((380, 'dock_at', args.ui_replay_dock))
            if queue and args.ui_replay_fleet:
                ship = args.ui_replay_fleet[0]
                plan += [tuple(ship if v == 'FLEET' else identity.STOCK_SECTOR if v == 'SECTOR' else v for v in row)
                         for row in UI_REPLAY_QUEUE_DEMO[1 if client == 1 else 2]]
            # Client 1 commands the fleet: attack the next fixture, then move, then dock if a station was given.
            elif client == 1 and args.ui_replay_fleet:
                ship = args.ui_replay_fleet[0]
                plan += [(UI_REPLAY_FLEET_ORDERS, 'attack', ship, fixtures(args)[-1]),
                         (UI_REPLAY_FLEET_ORDERS + 60, 'clear', ship),
                         (UI_REPLAY_FLEET_ORDERS + 61, 'move', ship, identity.STOCK_SECTOR, 3000, 100000, 3000)]
                if args.ui_replay_dock:
                    plan += [(UI_REPLAY_FLEET_ORDERS + 90, 'clear', ship), (UI_REPLAY_FLEET_ORDERS + 91, 'dock', ship, args.ui_replay_dock)]
            def item(v):
                return repr(v) if isinstance(v, str) else str(v)
            plans.append('{' + ','.join('{' + ','.join(item(v) for v in row) + '}' for row in plan) + '}')
        lines.append('X4LAN_CONFIG.native_gate_ui_replay_script = {' + ','.join(plans) + '}')
    return lines


def counterpart(ship, fixture=None):
    """The Xenon fixture actor an Argon fixture actor attacks in the scripted battle (actor i -> i+6)."""
    fixture = tuple(fixture or sandbox.FIXTURES)
    argon = fixture[:6]
    if ship not in argon:
        raise ValueError(f'the orders demo needs Argon fixture actors in slots 2..3 (got {ship}); use --orders none')
    return fixture[argon.index(ship) + 6]


def demo_rows(args, plan):
    """(slot, subject, target, result) of the orders demo. Slot 2 orders its own ship to attack its battle
    counterpart (applied: an Attack order keeps the Steering model a pilot slot needs, where a Wait/hold
    would remove it); slot 3 orders slot 2's ship and slot 1 slot 3's (2 players: slot 2's) ship
    (refused_not_owner). With different starts every other moved fixture slot (3..N) also orders its own ship
    to attack its moved counterpart: the move drops the Steering solver (live st-F3: slot 3 no_solver 589)."""
    slots = plan['slots']
    if any('ship' not in s for s in slots):
        raise ValueError('the orders demo needs saved slot ships')
    ship = [s['ship'] for s in slots]
    other = ship[2] if args.players >= 3 else ship[1]
    fixture = fixtures(args)
    rows = [(1, other, counterpart(other, fixture), 'refused_not_owner'), (2, ship[1], counterpart(ship[1], fixture), 'applied')]
    if args.players >= 3:
        rows.append((3, ship[1], counterpart(ship[1], fixture), 'refused_not_owner'))
    moved = {s['object'] for s in plan.get('starts', [])}
    for k in range(3, args.players + 1):
        if ship[k - 1] in moved and ship[k - 1] in fixture[:6]:
            rows.append((k, ship[k - 1], counterpart(ship[k - 1], fixture), 'applied'))
    return rows


def orders_plan(args, plan):
    """Scripted ownership check over the input wire (demo_rows)."""
    slots = plan['slots']
    script = {}
    for slot, subject, target, _ in demo_rows(args, plan):
        script.setdefault(str(slot), {}).setdefault(str(ORDERS_TICK), []).append({'op': 'attack', 'subject': subject, 'target': target})
    ship = [s['ship'] for s in slots]
    result = {'players': args.players, 'delay': ORDERS_DELAY, 'script': script}
    if args.mode == 'factions':
        # Slot k may order every ship its faction owns (MD checks the owner at application).
        result.update(ownership='faction', factions=[s['faction'] for s in slots])
    else:
        # Coop: one faction, but slot k may order only its own slot ship. Another slot's ship is flown by
        # that human; an AI order would take its flight away (refused not_owner by the agreed table).
        result.update(ownership='table', owners={s: k for k, s in enumerate(ship, 1)})
    return result


def expected_orders(args, plan):
    if args.orders != 'demo':
        return []
    tick = ORDERS_TICK + ORDERS_DELAY
    return [f'tick={tick} slot={slot} op=attack subject={subject} target={target} result={result}'
            for slot, subject, target, result in demo_rows(args, plan)]


def pilot_lines(args, plan):
    if args.source == 'none':
        if args.fire != 'off' or args.hit_claims:
            raise ValueError('--fire and --hit-claims need piloted slots')
        return []
    ships = [s['ship'] for s in plan['slots']]
    start, stop = args.window
    stock, wired = args.source == 'stock', args.source in ('wire', 'stock')
    delay = args.delay if wired else 0
    if not 5 <= start <= stop <= sandbox.PILOT_TICK_MAX or (wired and not (2 <= delay <= 30 and delay < start)):
        raise ValueError(f'pilot window needs 5 <= start <= stop <= {sandbox.PILOT_TICK_MAX} (wire: delay 2..30 < start)')
    if start <= args.setup_tick + 4:
        raise ValueError('the identity setup tick must come before the controls park (window start - 3)')
    if stock and start - 5 <= args.setup_tick + identity.FOLD_LAG:
        raise ValueError('--source stock: the identity must be ready before the slot players are built (window start - 5)')
    if args.stock_script and not stock:
        raise ValueError('--stock-script needs --source stock')
    if args.death and not stock:
        raise ValueError('--death needs --source stock')
    if args.respawn is not None and not args.death:
        raise ValueError('--respawn needs --death')
    if args.respawn is not None and not 1 <= args.respawn <= sandbox.PILOT_TICK_MAX:
        raise ValueError(f'--respawn is a delay of 1..{sandbox.PILOT_TICK_MAX} ticks')
    if args.death_suit and not (args.death and args.present):
        raise ValueError('--death-suit needs --death and --present')
    if args.foot and not (stock and args.ui_replay is not None and args.ui_guard):
        raise ValueError("--foot needs --source stock, --ui-replay and --ui-guard (the docked menu's Get Up is claimed in the guard)")
    if args.foot_bodies and not args.foot:
        raise ValueError('--foot-bodies needs --foot')
    if args.foot_slots and not args.foot:
        raise ValueError('--foot-slots needs --foot')
    if args.fire_slots is not None:
        if args.fire != 'script':
            raise ValueError('--fire-slots needs --fire script')
        if not args.fire_slots or len(set(args.fire_slots)) != len(args.fire_slots) or not all(1 <= s <= args.players for s in args.fire_slots):
            raise ValueError(f'--fire-slots is a list of slots 1..{args.players}, each once')
    if args.present and not stock:
        raise ValueError('--present needs --source stock')
    if args.present and args.camera_follow:
        raise ValueError("--present excludes --camera-follow (each client sits in its own ship with X4's own camera)")
    if stock and not args.capture_log:
        raise ValueError('--source stock needs the capture rows')
    lines = ['X4LAN_CONFIG.native_gate_pilot_inputs = true',
             f"X4LAN_CONFIG.native_gate_pilot_slots = {sandbox.lua_list([repr(s) for s in ships])}",
             f"X4LAN_CONFIG.native_gate_pilot_source = '{'wire' if wired else args.source}'"]
    if wired:
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_delay = {delay}')
    lines += [f'X4LAN_CONFIG.native_gate_pilot_window = {{{start},{stop}}}',
              'X4LAN_CONFIG.native_gate_pilot_suppress_local = true']
    if args.source == 'wire':
        if len(args.devices) != args.players or len(args.pads) != args.players:
            raise ValueError('one device and one pad per client')
        if any(d not in sandbox.DEVICES for d in args.devices) or any(not 0 <= p <= 3 for p in args.pads):
            raise ValueError('devices are kbm|xinput|both|none, pads 0..3')
        device = [repr(d) if d != 'none' else 'false' for d in args.devices]
        lines += [f'X4LAN_CONFIG.native_gate_pilot_device = {sandbox.lua_list(device)}',
                  f'X4LAN_CONFIG.native_gate_pilot_pad = {sandbox.lua_list([str(p) for p in args.pads])}']
        invert = [name for name in args.invert.split(',') if name] if args.invert else []
        if any(name not in sandbox.INVERT for name in invert) or len(set(invert)) != len(invert):
            raise ValueError(f'invert axes are {", ".join(sandbox.INVERT)}, each once')
        if invert:
            lines.append(f"X4LAN_CONFIG.native_gate_pilot_invert = {sandbox.lua_list([repr(n) for n in invert])}")
    elif stock:
        # Every client captures its own slot's stock dispatch stream and replays every slot.
        if args.invert:
            raise ValueError('--invert needs the device sampler (--source wire)')
        lines += ['X4LAN_CONFIG.native_gate_pilot_stock = true',
                  "X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'",
                  f"X4LAN_CONFIG.native_gate_pilot_capture_source = {sandbox.lua_list([repr('stock')] * args.players)}",
                  f"X4LAN_CONFIG.native_gate_pilot_device = {sandbox.lua_list(['false'] * args.players)}"]
        if args.stock_script:
            lines.append("X4LAN_CONFIG.native_gate_pilot_stock_script = 'aim'" if args.stock_script == 'aim' else
                         f'X4LAN_CONFIG.native_gate_pilot_stock_script = {sandbox.lua_list([repr(s) for s in STOCK_DEMOS[:args.players]])}')
        if args.present:
            lines.append('X4LAN_CONFIG.native_gate_pilot_present = ' + ('true' if args.present == 'hud' else "'camera'"))
            lines.append("X4LAN_CONFIG.native_gate_camera_anchor = nil  -- a play base's chase camera: X4's own camera instead")
        if args.death:
            lines.append('X4LAN_CONFIG.native_gate_pilot_death = true')
        if args.respawn is not None:
            lines.append(f'X4LAN_CONFIG.native_gate_pilot_respawn = {args.respawn}')
        if args.death_suit:
            lines.append('X4LAN_CONFIG.native_gate_pilot_death_suit = true')
        if args.foot:
            lines.append('X4LAN_CONFIG.native_gate_pilot_foot = true')
        if args.foot_bodies:
            lines.append('X4LAN_CONFIG.native_gate_pilot_foot_bodies = true')
        if args.foot_slots:
            lines.append('X4LAN_CONFIG.native_gate_pilot_foot_slots = true')
    elif args.invert or args.camera_follow:
        raise ValueError('--invert and --camera-follow need human pilots (--source wire or stock)')
    if args.crash_note:
        lines.append('X4LAN_CONFIG.native_gate_crash_note = true')
    if args.capture_log:
        lines.append('X4LAN_CONFIG.native_gate_pilot_capture = true')
    lines.append('X4LAN_CONFIG.native_gate_pilot_ship_state = true')
    if args.fire != 'off':
        lines.append(f"X4LAN_CONFIG.native_gate_pilot_fire = '{args.fire}'")
    if args.fire_slots is not None:
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_fire_slots = {sandbox.lua_list([str(s) for s in args.fire_slots])}')
    if args.hit_claims:
        lines.append('X4LAN_CONFIG.native_gate_hit_claims = ' + ('false' if args.hit_claims == 'off' else repr(args.hit_claims)))
    if args.camera_follow:
        # Clients whose slot ship is not the occupied one watch it through the stock target view.
        follow = ['false' if s == args.occupied_ship else 'true' for s in ships]
        lines.append('-- camera follow: client cameras differ; attention uses view_points, other camera reads remain')
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_camera_follow = {sandbox.lua_list(follow)}')
    if args.block_orders:
        # Human sessions: orders, travel, SETA and autopilot from the stock menus would run on one client
        # only (the probe's pilot order block, as in the sandbox). The UI replay opens what it captures.
        lines.append('X4LAN_CONFIG.native_gate_pilot_block_orders = true')
    lines += ui_replay_lines(args)
    if args.guidance_local:
        # A client's guidance is its own (lockstep_guidance_local.h): what it runs in the simulation takes local IDs.
        if not args.present:
            raise ValueError('--guidance-local needs --present (each client presents its own slot, its guidance is its own)')
        lines.append('X4LAN_CONFIG.native_gate_guidance_local = true')
    return lines


# Keys of the default stack this builder decides (piloting, identity, UI): in a session overlay one of them that the
# run leaves off but the session defaults would turn on is written as an explicit false (the overlay runs what was built).
BUILDER_KEYS = tuple(k for k in session_defaults.ON_KEYS if k.startswith('native_gate_pilot_')) + (
    'native_gate_hit_claims', 'native_gate_crash_note', 'native_gate_ui_replay', 'native_gate_ui_guard', 'native_gate_guidance_local',
    'native_gate_player_identity', 'native_gate_own_predict', 'native_gate_own_predict_rows', 'native_gate_player_settings_per_slot',
    'native_gate_missions_per_player')
DEFAULTS_ONLY_FLAGS = ('present', 'death', 'respawn', 'death_suit', 'foot', 'foot_bodies', 'foot_slots', 'hit_claims', 'ui_replay', 'ui_guard',
                       'guidance_local', 'stock_script', 'crash_note', 'ui_replay_script', 'ui_replay_fleet', 'ui_replay_dock',
                       'ui_replay_min_hull', 'fire_slots', 'missions_per_player')


def config_block(args):
    """(lines, plan, orders plan or None) for the session; validates everything."""
    if args.world_stride < 1 or args.world_stride > 18000 or not 1 <= args.trace_stride <= 18000:
        raise ValueError('world capture and trace strides must be 1..18000')
    plan = plan_of(args)
    if getattr(args, 'defaults_only', False):
        # The people session of the session defaults: the clients and the identity plan; the default stack does the rest.
        given = [name for name in DEFAULTS_ONLY_FLAGS if getattr(args, name, None) not in (None, False, 'off')]
        if getattr(args, 'fire', 'off') != 'off':
            given.append('fire')
        if args.source != 'stock' or args.orders != 'none' or given:
            raise ValueError('--defaults-only is the people session of the session defaults: --source stock, no --orders and none of '
                             + ', '.join('--' + g.replace('_', '-') for g in (given or DEFAULTS_ONLY_FLAGS[:4])) +
                             ' (the defaults set them; --set native_gate_KEY=false turns one off)')
        lines = [HEADER, 'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@'] + identity.config_lines(plan) + sandbox.census_lines(args)
        return lines, plan, None
    # Orders and pilot records share each slot's blob (input multiplexer, priority pilot > orders > hits).
    orders = orders_plan(args, plan) if args.orders == 'demo' else None
    lines = [HEADER, 'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@']
    lines += identity.config_lines(plan)
    if orders is not None:
        def reference(text):
            raise ValueError(f'fixture references are not resolved here: {text}')
        lines += player_orders.overlay_config(orders, reference).splitlines()
    lines += pilot_lines(args, plan)
    # The sandbox stack (native_gate_sandbox.py): render RNG split, camera-independent bullet and nav
    # cadence, keyed fixture guns, attention from the pilot-slot ships, every portable ship keyed.
    lines += ['X4LAN_CONFIG.native_gate_render_rng_split = true',
              "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'",
              "X4LAN_CONFIG.native_gate_nav_cadence = 'far'",
              'X4LAN_CONFIG.native_gate_actor_diag = true',
              'X4LAN_CONFIG.native_gate_weapon_diag = true',
              "X4LAN_CONFIG.native_gate_view_points = 'players'",
              'X4LAN_CONFIG.native_gate_actor_rng_all = true',
              f'X4LAN_CONFIG.native_gate_world_hash_stride = {args.world_stride}',
              f'X4LAN_CONFIG.native_gate_trace_stride = {args.trace_stride}']
    if args.battle == 'calm':
        lines.append('X4LAN_CONFIG.native_gate_battle_engage = false')
    lines += sandbox.census_lines(args)
    return lines, plan, orders


def strip_own(base_text):
    """Drop a previous players block (marker and everything after it that this builder owns)."""
    kept, own = [], False
    for line in base_text.splitlines():
        if line.startswith(HEADER.split(';')[0]):
            own = True
            continue
        m = sandbox.LINE.match(line)
        if m and OWN_KEYS.fullmatch(m[1]) and (own or m[1].startswith('native_gate_player_identity')):
            continue
        if line.startswith('-- camera follow:'):
            continue
        kept.append(line)
    return '\n'.join(kept) + '\n'


def merge_block(base_text, block, lean=True):
    """merged_config's merge without its base checks (they ran on the effective base): the base without the keys this
    builder owns or the block sets, then the block."""
    keys = {m[1] for m in map(sandbox.LINE.match, block) if m}
    kept = []
    for line in base_text.splitlines():
        m = sandbox.LINE.match(line)
        if m and (sandbox.OWNED.fullmatch(m[1]) or m[1] in keys or (lean and m[1] in sandbox.LOG_ONLY)):
            continue
        if line.startswith(('-- Lockstep sandbox', '-- camera follow:', '-- native controls:', '-- stock piloting:')):
            continue
        kept.append(line)
    while kept and not kept[-1].strip():
        kept.pop()
    return '\n'.join(kept + list(block)) + '\n'


def session_block(lines, base_text, defaults_only=False):
    """(block, dropped, off) for a session base: the lines that differ from the session defaults, plus an explicit false
    for every BUILDER_KEYS default the run leaves off (not with --defaults-only: there the defaults are the run)."""
    kept = session_defaults.overrides(lines, base_text)
    dropped = [line for line in lines if line not in kept]
    if defaults_only:
        return kept, dropped, []
    own = set(session_defaults.parse('\n'.join(lines)))
    context = session_defaults.parse(base_text)
    context.update(session_defaults.parse('\n'.join(kept)))
    off = []
    while True:
        _, report = session_defaults.apply(context)
        extra = [k for k in report['applied'] if k in BUILDER_KEYS and k not in own and k not in off]
        if not extra:
            break
        context[extra[0]] = 'false'
        off.append(extra[0])
    kept = kept + [f'X4LAN_CONFIG.{key} = false  -- not in this run (native_gate_players.py flags)' for key in off]
    return kept, dropped, off


def build_overlay(args):
    base, out = Path(args.base), Path(args.out)
    if out.exists():
        raise ValueError(f'{out} exists; refusing to overwrite')
    config = base/'ui/config.lua.append'
    files = (base/'ui.xml.files').read_text().split()
    if 'ui/native_gate_probe.lua' not in files or not config.is_file() or not (base/'native/x4lan_gate_64.dll').is_file():
        raise ValueError('base is not a native gate overlay (probe, config or gate DLL missing)')
    lines, plan, orders = config_block(args)
    # A session's start stories (native_gate_session_start.py story_files): slot k's start script runs for slot k.
    stories = session_start.story_slots((getattr(args, 'session_data', None) or {}).get('slots'))
    if stories:
        lines.append('X4LAN_CONFIG.native_gate_story = {' + ','.join(f"[{k}]='{s['script']}.{s['cue']}'" for k, s in stories.items()) + '}')
    stripped = strip_own(config.read_text())
    # A session base (native_gate_test, slot, port, token) runs the session defaults (native_gate_defaults.lua): its checks
    # read the effective base, and the overlay writes only the lines that differ from the defaults.
    effective_base, report = session_defaults.effective(stripped)
    session = bool(report and report.get('session'))
    if args.defaults_only and not session:
        raise ValueError('--defaults-only needs a session base (native_gate_test, slot, port and token, not native_gate_defaults = false)')
    checked = effective_base if session else stripped
    # The UI replay carries runtime IDs: a base overlay without them gets the default, one with them keeps its own.
    if args.ui_replay is not None and sandbox.parse_config(checked).get('native_gate_runtime_ids') in (None, 'false', 'nil', "'observe'", '"observe"'):
        lines.append('X4LAN_CONFIG.native_gate_runtime_ids = true')
    if args.guidance_local:
        base_values = sandbox.parse_config(checked)
        missing = [key for key in ('native_gate_runtime_ids_local', 'native_gate_md_timed_rng') if base_values.get(key) != 'true']
        if base_values.get('native_gate_script_rng', '').strip('\'"') not in ('key', 'observe'):
            missing.append('native_gate_script_rng')
        if missing:
            raise ValueError('--guidance-local needs a base overlay with ' + ', '.join(missing))
    if sandbox.parse_config(stripped).get('native_gate_player_orders') not in (None, 'false', 'nil'):
        raise ValueError('base overlay already has a player orders block; this builder writes its own (--orders)')
    world = getattr(args, 'session_data', None)
    base_sha = sandbox.parse_config(stripped).get('native_gate_snapshot_sha', '').strip('"' + "'")
    joiner_base = 'joiner = {package' in (sandbox.parse_config(stripped).get('native_gate_join') or '')
    if getattr(args, 'session_joiner', False) and not (world and joiner_base):
        raise ValueError('--session-joiner needs --session and a joiner base overlay (build_native_gate_overlay.py --join-package)')
    if world and base_sha != world['snapshot']['sha256'] and not (args.session_joiner and joiner_base):
        raise ValueError(f"--session: the base overlay boots snapshot {base_sha or 'none'}, the session save is {world['snapshot']['sha256']} "
                         '(rebase it: native_gate_session_start.py rebase)')
    if session:
        if not args.defaults_only:
            sandbox.merged_config(effective_base, lines, lean=args.lean_logs)   # the sandbox's base checks, on the effective base
        block, dropped, off = session_block(lines, stripped, args.defaults_only)
        text = merge_block(stripped, block, lean=args.lean_logs)
        _, failures, final = gate_options.check_report(text)
        if failures:
            raise ValueError('the overlay config fails the option rules: ' +
                             '; '.join(f['rule'] + ' -> ' + ', '.join(map(str, f['bits'])) for f in failures))
        args.defaults_notes = [f'SESSION DEFAULTS: {len(block)} lines written, {len(dropped)} equal to the defaults left out'
                               + (', off in this run: ' + ', '.join(k[len('native_gate_'):] for k in off) if off else '')]
        args.defaults_notes += [f'DEFAULT_SKIPPED {key}: {why}' for key, why, _ in final['skipped']]
    else:
        text = sandbox.merged_config(stripped, lines, lean=args.lean_logs)
    probe = Path(args.probe) if args.probe else base/'ui/native_gate_probe.lua'
    if 'X4LAN_NATIVE_PLAYER_IDENTITY' not in probe.read_text(encoding='utf-8'):
        raise ValueError(f'{probe} has no player identity hook; pass --probe tools/dev/native_gate_probe.lua')
    shutil.copytree(base, out, ignore=shutil.ignore_patterns('x4lane-manifest.json'))
    session_start.write_story_files(out, stories)
    if args.gate_dll:
        shutil.copy2(args.gate_dll, out/'native/x4lan_gate_64.dll')
    if args.probe:
        shutil.copy2(args.probe, out/'ui/native_gate_probe.lua')
    added = list(MUX_FILES + IDENTITY_FILES)
    shutil.copy2(ROOT/'tools/dev/native_gate_player_identity.xml', out/'md/native_gate_player_identity.xml')
    if args.ui_replay is not None:
        added += UI_REPLAY_FILES
        if args.ui_replay_fleet or args.ui_replay_min_hull is not None or args.ui_guard or \
                {'events', 'missions', 'conversation'} & set(args.ui_replay.split(',')):
            shutil.copy2(ROOT/'tools/dev/native_gate_ui_replay.xml', out/'md/native_gate_ui_replay.xml')   # fleet, MinHull, the guard's and missions' MD rows
    if orders is not None:
        added += ORDERS_FILES
        shutil.copy2(ROOT/'tools/dev/native_gate_player_orders.xml', out/'md/native_gate_player_orders.xml')
    if args.respawn is not None or args.death_suit:
        shutil.copy2(ROOT/'tools/dev/native_gate_pilot_respawn.xml', out/RESPAWN_MD)
    if wallet_library(plan):
        (out/POOL_LIBRARY).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(session_start.EXTENSION/POOL_LIBRARY, out/POOL_LIBRARY)
    for name in added:
        shutil.copy2(ROOT/'tools/dev'/Path(name).name, out/name)
    # Adapters load before the probe, which binds them while arming; a session overlay ships the default stack's files with
    # the defaults module first (the adapters read X4LAN_CONFIG while loading).
    entries = [e for e in files if e not in MUX_FILES + IDENTITY_FILES + ORDERS_FILES + UI_REPLAY_FILES]
    order = added + entries
    if session:
        order = session_defaults.ship_files(out, order)
    (out/'ui.xml.files').write_text('\n'.join(order) + '\n')
    (out/'ui/config.lua.append').write_text(text)
    (out/'player-identity-plan.json').write_text(json.dumps(plan, indent=1, sort_keys=True) + '\n')
    if world:
        (out/'session.json').write_text(json.dumps(world, indent=1, sort_keys=True) + '\n')
    manifest = {str(f.relative_to(out)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return out, plan


def session_notes(args, plan, overlay='<overlay>'):
    ticks = args.window[1] + 2
    lanes = ','.join(f'b{k}' for k in range(1, args.players + 1))
    # Four clients: the released lane tool caps at 3 (.local/census/x4lane4.py through runN4.sh).
    people = args.source == 'stock' and not args.stock_script
    runner = 'runN4.sh' if args.players == 4 else 'runPlay.sh' if people else 'runSandbox.sh' if args.source == 'wire' else 'runN.sh'
    P = identity.validate(plan)
    notes = [f'clients: {args.players} (lanes {lanes}); relay ticks: {ticks}' + (
                 f' (pilot window {args.window[0]}-{args.window[1]}, controls restored at {args.window[1] + 1})' if args.source != 'none' else ''),
             f'coordinator: .local/census/{runner} <name> {overlay} {lanes} {ticks} {args.players}',
             f'identity: setup tick {P["tick"]}, READY at {P["tick"] + identity.FOLD_LAG}, plan {identity.digest(P)}',
             f'check: python tools/dev/native_gate_players.py compare <client logs> {" ".join(echo_args(args))}']
    if args.source == 'wire':
        notes.append('desync watcher: python tools/dev/native_gate_desync_watch.py --status <collect>/desync-status.json --beep --toast')
    world = getattr(args, 'session_data', None)
    if world:
        notes.append(f"session: base start {world['base_start']}, save {world['snapshot']['path']} (runner --snapshot-in), per-player starts " +
                     (', '.join(f"slot {k} {v['gamestart']}" for k, v in sorted(world['slots'].items(), key=lambda kv: int(kv[0]))) or 'none'))
    if wallet_library(plan):
        notes.append(f'wallet: {POOL_LIBRARY} (pool factions with their own accounts) is in the overlay; the wallet step refuses '
                     'refused_wallet_account (account=none) where X4 still gives a pool faction no account of its own')
    return notes


def echo_args(args):
    """The plan-relevant options, to repeat for `compare`."""
    out = ['--players', str(args.players), '--mode', args.mode, '--source', args.source]
    if getattr(args, 'session', None):
        out += ['--session', str(args.session)]
    if args.ships and not getattr(args, 'session', None):
        out += ['--ships', ','.join(args.ships)]
    for flag, value, default in (('--relations', args.relations, 'neutral'), ('--occupied', args.occupied, 'player'),
                                 ('--reputation', args.reputation, 'mirror'), ('--setup-tick', str(args.setup_tick), '20'),
                                 ('--orders', args.orders, 'demo' if args.source == 'script' else 'none')):
        if value != default:
            out += [flag, value]
    for (a, b), name in args.relation or []:
        out += ['--relation', f'{a}-{b}={name}']
    for s in args.start or []:
        out += ['--start', f"{s['target']}={s['sector']}@{s['x']},{s['y']},{s['z']}/{s['yaw']}"]
    if args.start_counterparts:
        out.append('--start-counterparts')
    if args.third_sector:
        out.append('--third-sector')
    return out


def compare_logs(args, paths):
    texts = [Path(p).read_text(encoding='utf-8', errors='replace') for p in paths]
    if len(texts) != args.players:
        return False, f'{args.players} client logs expected, got {len(texts)}'
    lines, plan, orders = config_block(args)
    ok, details = identity.compare(texts, plan)
    if not ok:
        return False, 'identity: ' + details
    report = ['identity: ' + details]
    if orders is not None:
        ok, more = player_orders.compare(texts)
        if not ok:
            return False, 'orders: ' + more
        # Refusals by the agreed table are logged by Lua at once, MD results a frame later: compare in
        # canonical (tick, slot) order, the order in which every client decided them.
        def canonical(lines):
            return sorted(lines, key=lambda line: tuple(int(v) for v in re.match(r'tick=(\d+) slot=(\d+)', line).groups()))
        got = canonical([line.split(' seq=')[0] for line in player_orders.applied_lines(texts[0])])
        want = canonical(expected_orders(args, plan))
        if got != want:
            return False, f'orders: {got} != {want}'
        report.append('orders: ' + more)
    return True, '; '.join(report)


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--players', type=int, default=2, help=f'clients = slots = players, 2..{MAX_PLAYERS}')
    common.add_argument('--mode', choices=identity.MODES, default='factions')
    common.add_argument('--source', choices=SOURCES, default='wire')
    common.add_argument('--ships', type=ids, help='comma list of saved slot ship IDs (default: player scout, fixtures 3..5)')
    common.add_argument('--fixture', type=ids, help="the snapshot's twelve fixture actor IDs in order (default: snapshot 005's)")
    common.add_argument('--session-joiner', action='store_true',
                        help="with --session: the base boots a join save of that session (a hot joiner's overlay, "
                             'build_native_gate_overlay.py --join-package), not the session save')
    common.add_argument('--session', help='session.json of native_gate_session_start.py register: the session save (any usable stock start) '
                                           'replaces snapshot 005; its per-player starts apply at the setup tick')
    common.add_argument('--create', type=create_spec, action='append', help='SLOT=MACRO@X,Y,Z[/YAW][#SEED], sector-local metres (fixture area y=100000); needs --source none')
    common.add_argument('--start', type=start_spec, action='append',
                        help='K=SECTOR@X,Y,Z[/YAW] (or SHIP=...): different starts, moved at the setup tick')
    common.add_argument('--third-sector', action='store_true',
                        help='allow slots beyond the covered sectors (two; N with --set native_gate_view_scenes=true and '
                             'native_gate_viewpoint_standins=N); they have no physics, traffic or highways there')
    common.add_argument('--start-counterparts', action='store_true',
                        help="also move each moved fixture actor's battle counterpart (+1500 m z)")
    common.add_argument('--relations', choices=sorted(identity.RELATIONS), default='neutral', help='factions mode: every slot pair')
    common.add_argument('--relation', type=relation_override, action='append', help='factions mode: A-B=allied|neutral|hostile')
    common.add_argument('--occupied', choices=identity.OCCUPIED, default='player',
                        help='factions mode: the occupied player ship keeps `player` (default) or moves to its pool faction')
    common.add_argument('--occupied-ship', type=sandbox.saved_id, default=identity.PLAYER_SHIP)
    common.add_argument('--reputation', choices=identity.REPUTATION, default='mirror')
    common.add_argument('--setup-tick', type=int, default=20)
    common.add_argument('--orders', choices=('demo', 'none'), help='scripted ownership check over the input wire, also with --source wire (default: demo for script)')
    common.add_argument('--no-block-orders', dest='block_orders', action='store_false', default=None,
                        help='wire sessions: leave the stock order/travel menus active (they act on one client only)')
    common.add_argument('--stock-script', choices=('demo', 'aim'),
                        help='--source stock without humans: demo, demo2..demo4 per slot; aim: stop and steer at the next slot (hit test)')
    common.add_argument('--fire', choices=('off', 'record', 'script'), default='off',
                        help='primary guns: the trigger in each record, or a fixed schedule for runs without humans')
    common.add_argument('--hit-claims', choices=('off', 'shooter', 'victim', 'first'),
                        help='owners decide hits on and by slot ships (reports/LOCKSTEP-HIT-CLAIMS-NATIVE-20261029.txt); '
                             'not given: as the base overlay has it, off: native hits')
    common.add_argument('--death', action='store_true',
                        help="--source stock: a slot ship's death does not end the session (stop reason 163: the slot is dead on the "
                             "same tick everywhere, slot 1's player in X4's own emergency eject suit)")
    common.add_argument('--respawn', type=int, metavar='TICKS',
                        help='--death: a new ship of the slot\'s macro at its start after that many ticks (X4\'s create_ship and teleport_player)')
    common.add_argument('--death-suit', action='store_true',
                        help="--death --present: a dead slot 2..N waits in a spacesuit of its own (MD's create_ship, the slot's seat), its client "
                             "presents that instead of slot 1's view")
    common.add_argument('--foot', action='store_true',
                        help="--source stock --ui-replay --ui-guard: slot 1 gets up (GET_UP key, the docked menu's Get Up) and walks through the "
                             "stock record on every client (lockstep_pilot_foot.h, stop reason 185); slots 2..N's walking keys are refused")
    common.add_argument('--foot-bodies', action='store_true',
                        help="--foot: slots 2..N's players sit in their seats' rooms as X4 seats a pilot, on every client "
                             "(lockstep_pilot_foot_body.h, stage 1 of their bodies, stop reason 185); with --death a kill guard takes a body "
                             "off its room before X4's kill reaches it")
    common.add_argument('--foot-slots', action='store_true',
                        help="--foot: slots 2..N get up (GET_UP key, the docked menu's Get Up on their client) and walk through their stock "
                             "records in their scopes on every client (lockstep_pilot_foot_slots.h, stop reason 185); with --death a walking "
                             "slot's ship lost is no death (stranded until --respawn seats it)")
    common.add_argument('--crash-note', action='store_true',
                        help='an unhandled exception writes one CRASH line (address, registers, chain, tick) to x4lan-crash.txt in the lane profile')
    common.add_argument('--fire-slots', type=lambda t: [int(v) for v in t.split(',') if v],
                        help='--fire script: only these slots fire, a list (a hit test with one shooter: 1 kills slot 2)')
    common.add_argument('--present', nargs='?', choices=('hud', 'camera'), const='hud', default=None,
                        help="--source stock: client k's stock camera, HUD and UI present slot k's ship as the player "
                             "(camera: the HUD's anchor stays on slot 1's cockpit)")
    common.add_argument('--ui-replay', nargs='?', const='all', default=None, metavar='FAMILIES',
                        help='stock UI actions as events of the slot: all (no value) or a comma list of ' + ', '.join(UI_REPLAY_FAMILIES) + ' '
                             '(lockstep_ui_replay.h, stop reason 164); needs --source stock --present and runtime IDs')
    common.add_argument('--ui-replay-script', action='store_true', help='--ui-replay without people: a fixed plan through the stock entries')
    common.add_argument('--missions-per-player', action='store_true',
                        help='--ui-replay with events and missions: slots 2..N accept missions as their own players, every client runs the '
                             "mission's MD in the slot's scope (lockstep_mission_owners.h; on by default in play sessions, a builder run "
                             "without this flag writes it off); with --ui-replay-script slot 2 accepts")
    common.add_argument('--ui-guard', nargs='?', const='all', default=None, metavar='GROUPS',
                        help='with --ui-replay: refuse every other world-changing stock UI entry point in the local phases (lockstep_ui_guard.h): '
                             'all (no value) or a comma list of ' + ', '.join(UI_GUARD_GROUPS))
    common.add_argument('--guidance-local', action='store_true',
                        help="--present: what a client's own guidance runs in the simulation (md.Guidance, the guidance route) takes local "
                             'runtime IDs (lockstep_guidance_local.h); the base needs runtime_ids_local, script_rng and md_timed_rng')
    common.add_argument('--ui-replay-fleet', type=ids, help='saved IDs: ships handed to `player` at the setup tick, to give orders to (--mode coop)')
    common.add_argument('--ui-replay-dock', type=sandbox.saved_id, help='a saved station ID for the scripted dock order')
    common.add_argument('--ui-replay-min-hull', type=int, metavar='PCT',
                        help='test protection: MD keeps every slot ship at PCT %% hull or more from the fleet tick (missile runs: '
                             "slot 1's ship is the player's)")
    sandbox.add_census_options(common)
    common.add_argument('--defaults-only', action='store_true',
                        help='--source stock on a session base: write only the clients and the identity plan; the session defaults '
                             '(tools/dev/native_gate_defaults.lua) supply the whole people stack (--set native_gate_KEY=false turns one off)')
    common.add_argument('--delay', type=int, default=3)
    common.add_argument('--window', type=int, nargs=2, metavar=('START', 'STOP'))
    common.add_argument('--devices', type=lambda t: t.split(','), help='wire: one of kbm|xinput|both|none per client')
    common.add_argument('--pads', type=lambda t: [int(v) for v in t.split(',')], help='wire: XInput pad index per client')
    common.add_argument('--invert', default='', help='wire: comma list of axes to invert on every client')
    common.add_argument('--camera-follow', action='store_true', help='wire: clients not in the occupied ship watch their own ship')
    common.add_argument('--world-stride', type=int)
    common.add_argument('--trace-stride', type=int, default=60)
    common.add_argument('--battle', choices=('calm', 'engage'), default='engage',
                        help='engage (default): fixture Attack orders keep the slot ships flyable; calm: they wait (no solver)')
    common.add_argument('--capture-log', dest='capture_log', action='store_true', default=None)
    common.add_argument('--no-capture-log', dest='capture_log', action='store_false')
    sub.add_parser('config', parents=[common], help='print the config block')
    overlay = sub.add_parser('overlay', parents=[common], help='copy a protocol-3 base overlay and apply the players block')
    overlay.add_argument('--base', required=True)
    overlay.add_argument('--out', required=True)
    overlay.add_argument('--gate-dll')
    overlay.add_argument('--probe')
    overlay.add_argument('--full-logs', dest='lean_logs', action='store_false')
    sub.add_parser('plan', parents=[common], help='print the identity plan, digest and expected lines')
    compare = sub.add_parser('compare', parents=[common], help='check client logs against the plan')
    compare.add_argument('logs', nargs='+')
    return p


def main(argv=None):
    p = build_parser()
    parsed = p.parse_args(argv)
    if getattr(parsed, 'fixture', None):  # another snapshot's fixture (tools/build_soak_overlay.py passes it)
        if len(parsed.fixture) != 12:
            p.error('--fixture needs twelve IDs')
        sandbox.FIXTURES = tuple(parsed.fixture)
    args = parsed
    try:
        args = defaults(args)
        if args.command == 'config':
            lines, plan, _ = config_block(args)
            print('\n'.join(lines))
            for note in getattr(args, 'sector_warnings', []) + session_notes(args, plan):
                print('-- ' + note)
        elif args.command == 'overlay':
            out, plan = build_overlay(args)
            print(f'overlay {out}')
            for note in getattr(args, 'sector_warnings', []) + getattr(args, 'defaults_notes', []) + session_notes(args, plan, str(out)):
                print(note)
        elif args.command == 'plan':
            _, plan, orders = config_block(args)
            P = identity.validate(plan)
            print(json.dumps(plan, indent=1, sort_keys=True))
            print(identity.canonical(P))
            print('digest', identity.digest(P))
            for step in identity.steps(P):
                print('PLAYER_IDENTITY_APPLIED ' + step['head'] + ' result=applied|unchanged')
            for line in expected_orders(args, plan):
                print('PLAYER_ORDER_APPLIED ' + line)
        else:
            ok, details = compare_logs(args, args.logs)
            print(('PASS ' if ok else 'FAIL ') + details)
            return 0 if ok else 1
    except (ValueError, OSError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
