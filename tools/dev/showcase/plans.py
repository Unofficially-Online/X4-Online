"""The showcase takes: per take the two players' plans (native_gate_ui_replay_script: one list per slot, ticks after
the pilot window's start, 30 ticks a second), the session's length and its settings.

What a plan is made of (tools/dev/native_gate_ui_replay_core.lua):
- fixtures, the same row at the same tick in both plans (each client's MD runs it at that step): 'slot_near' (player
  2's ship beside player 1's) and 'hostile' (ships of a faction ahead of a player's ship, patrolling: the battle);
- a player's own inputs, replayed on every client as a person's keys are ('input': throttle, steering, stop; 'travel';
  'aim_fire'; 'target_named');
- a client's own presentation keys, local to its window ('input_local': the cockpit, external and target views, the
  HUD, the map). Ids are X4 9.00's input ids (INPUT_ACTION_/INPUT_STATE_/INPUT_RANGE_ as X4.exe's table names them)."""

# X4 9.00 input ids
A_STOP, A_MAP, A_COCKPIT_VIEW, A_EXTERNAL_VIEW, A_TARGET_VIEW, A_TOGGLE_HUD = 0x7b, 0x71, 0xb4, 0xb5, 0xb6, 0xdf
S_ACCELERATE, S_DECELERATE, S_BOOST, S_ROLL_LEFT, S_ROLL_RIGHT = 0xd, 0xe, 0x11, 0x12, 0x13
S_CAMERA_ZOOMIN, S_CAMERA_ZOOMOUT, S_CAMERA_LEFT, S_CAMERA_UP, S_CAMERA_DOWN = 0x46, 0x47, 0x48, 0x4a, 0x4b
R_YAW, R_PITCH, R_ROLL, R_THROTTLE = 1, 2, 3, 4

# the battle: (name, faction, macro without ship_/_macro, metres ahead, count, the slot it is put ahead of)
BLUE = [('Kukri', 'terran', 'ter_s_fighter_01_a', 1400, 10, 1),
        ('Gladius', 'terran', 'ter_s_heavyfighter_01_a', 1650, 6, 2),
        ('Katana', 'terran', 'ter_m_corvette_01_a', 2000, 3, 1),
        ('Falx', 'terran', 'ter_m_frigate_01_a', 2400, 2, 2),
        ('Vanguard', 'terran', 'ter_l_destroyer_01_a', 3300, 1, 1)]
RED = [('Raider', 'xenon', 'xen_s_fighter_01_a', 4600, 10, 1),
       ('Hunter', 'xenon', 'xen_s_fighter_02_a', 4800, 8, 2),
       ('Reaver', 'xenon', 'xen_m_corvette_01_a', 5100, 3, 1),
       ('Devastator', 'xenon', 'xen_xl_destroyer_01_a', 7500, 1, 2)]


def battle(tick, rows, min_hull=60):
    """'hostile' rows: every group ahead of a player's ship, whose hull never falls below min_hull percent."""
    return [[tick, 'hostile', slot, name, faction, macro, metres, count, None, None, min_hull]
            for name, faction, macro, metres, count, slot in rows]


def both(shared, own1, own2=None):
    """The two plans: the shared fixture rows in each, and each player's own rows; sorted by tick."""
    own2 = own1 if own2 is None else own2
    return [sorted(shared + own1, key=lambda e: e[0]), sorted(shared + own2, key=lambda e: e[0])]


def probe():
    """The first look: what works and what the windows show (views, HUD, battle, travel, map)."""
    shared = [[150, 'slot_near', 2, 1, 80]] + battle(300, BLUE + RED)
    own = [[200, 'input', 'state', S_ACCELERATE, 1], [290, 'input', 'state', S_ACCELERATE, 0],
           [330, 'input', 'action', A_STOP, 0],
           [520, 'target_named', 'Vanguard 1', 'terran'], [540, 'input_local', 'action', A_TARGET_VIEW, 0],
           [1500, 'input_local', 'action', A_COCKPIT_VIEW, 0],
           [1560, 'input_local', 'action', A_EXTERNAL_VIEW, 0],
           [1800, 'input_local', 'action', A_TOGGLE_HUD, 0], [2100, 'input_local', 'action', A_TOGGLE_HUD, 0],
           [2160, 'input_local', 'action', A_COCKPIT_VIEW, 0],
           [2250, 'target_named', 'Raider 1', 'xenon'], [2300, 'aim_fire', 1], [2600, 'aim_fire', 0],
           [2700, 'input_local', 'action', A_TARGET_VIEW, 0],
           [3200, 'input_local', 'action', A_COCKPIT_VIEW, 0],
           [3300, 'travel'], [3700, 'travel_stop'],
           [3900, 'input_local', 'action', A_MAP, 0]]
    return {'plan': both(shared, own), 'minutes': 2.5, 'client_config': '1:antialiasing=5'}


# saved IDs of the session's save (the Terran cadet start, Mars): the two players' ships, the trade station IDJ-703
SHIP1, SHIP2, STATION = '00000000000aff44', '00000000001734f4', '000000000002340e'


def local(tick, action):
    return [tick, 'input_local', 'action', action, 0]


def main():
    """Every flying shot in one session. Ticks after the window's start (the world captures, every 1,800 absolute
    ticks, fall at 1760, 3560, 5360 and 7160 here):
      0     the start's own help box and calls play out (nothing is used before 1500)
      1500  face to face: player 1 300 m in front of player 2, both in the cockpit, each sees the other's ship
      1800  both accelerate and pass each other
      2100  side by side 35 m apart, external views, the HUD off; 2200 both accelerate
      2500  the battle is made ahead of them (22 Terran ships against 22 Xenon)
      2700  target views: player 1 on the Terran destroyer, player 2 on a Xenon corvette (two angles on one battle)
      3300  both on the Terran destroyer: the same viewpoint on both clients; 4200 both on the Xenon destroyer
      4800  cockpits with the HUD; 4900 off to the trade station, side by side, external views, travel drive
      5700  cockpits again to the end"""
    shared = [[150, 'slot_near', 2, 1, 80], [2100, 'slot_near', 2, 1, 35], [5030, 'slot_near', 2, 1, 45]] + battle(2500, BLUE + RED)
    fly = [[1800, 'input', 'state', S_ACCELERATE, 1], [1960, 'input', 'state', S_ACCELERATE, 0], [1975, 'input', 'action', A_STOP, 0],
           local(2130, A_EXTERNAL_VIEW), local(2160, A_TOGGLE_HUD),
           [2200, 'input', 'state', S_ACCELERATE, 1], [2380, 'input', 'state', S_ACCELERATE, 0], [2400, 'input', 'action', A_STOP, 0],
           local(3300, A_COCKPIT_VIEW), [3310, 'target_named', 'Vanguard 1', 'terran'], local(3330, A_TARGET_VIEW),
           local(4200, A_COCKPIT_VIEW), [4210, 'target_named', 'Devastator 1', 'xenon'], local(4230, A_TARGET_VIEW),
           local(4800, A_COCKPIT_VIEW), local(4810, A_TOGGLE_HUD),
           local(5060, A_EXTERNAL_VIEW), local(5070, A_TOGGLE_HUD),
           [5100, 'travel'], [5500, 'travel_stop'], [5520, 'input', 'action', A_STOP, 0],
           local(5700, A_COCKPIT_VIEW), local(5710, A_TOGGLE_HUD)]
    own1 = fly + [[1500, 'goto_obj', SHIP2, [60, 10, 300]], [2700, 'target_named', 'Vanguard 1', 'terran'], local(2720, A_TARGET_VIEW),
                  [5000, 'goto_obj', STATION, [0, 400, -9000]]]
    own2 = fly + [[2700, 'target_named', 'Reaver 1', 'xenon'], local(2720, A_TARGET_VIEW)]
    return {'plan': both(shared, own1, own2), 'minutes': 4.1, 'record_from': 1300}


# the battle the players fly through: rows ahead of player 1 (from its place to the right) and ahead of player 2, who
# stands 1,300 m to its right while they are made; afterwards both players go to the middle of the front
MELEE = [('Kukri', 'terran', 'ter_s_fighter_01_a', 1100, 10, 1),
         ('Vanguard', 'terran', 'ter_l_destroyer_01_a', 1800, 1, 1),
         ('Gladius', 'terran', 'ter_s_heavyfighter_01_a', 1250, 6, 2),
         ('Katana', 'terran', 'ter_m_corvette_01_a', 1500, 3, 2),
         ('Jian', 'terran', 'ter_m_gunboat_01_a', 900, 2, 2),
         ('Raider', 'xenon', 'xen_s_fighter_01_a', 3000, 10, 1),
         ('Hunter', 'xenon', 'xen_s_fighter_02_a', 3200, 8, 2),
         ('Reaver', 'xenon', 'xen_m_corvette_01_a', 3500, 3, 2),
         ('Devastator', 'xenon', 'xen_xl_destroyer_01_a', 5400, 1, 2)]


def key(tick, kind, ident, value):
    return [tick, 'input', kind, ident, value]


def battle_flight():
    """The centrepiece: both players fly together through a fleet battle, each in its own cockpit with X4's HUD, each
    with the other's ship as its target (the target box marks it).
      200   each player targets the other's ship
      300   face to face 300 m apart; 480 both accelerate and pass each other
      800   side by side again; 830 player 2 1,300 m to the right, 850 the fleets are made ahead of both (22 against 22)
      960   both to the middle of the front, 45 m apart
      1000  player 2 accelerates, 1045 player 1 (player 2 leads, in player 1's view); 1330 player 1 boosts past
      1760  both turn around; 1900 back through the fight; 2400 stop"""
    shared = ([[150, 'slot_near', 2, 1, 45], [800, 'slot_near', 1, 2, -45], [830, 'slot_near', 2, 1, 1300]] +
              battle(850, MELEE) + [[960, 'slot_near', 2, 1, 1000], [966, 'slot_near', 1, 2, -45]])
    fly = [key(480, 'state', S_ACCELERATE, 1), key(640, 'state', S_ACCELERATE, 0), key(650, 'action', A_STOP, 0),
           key(1300, 'state', S_ACCELERATE, 0),
           key(1760, 'range', R_YAW, 1.0), key(1870, 'range', R_YAW, 0.0), key(2400, 'action', A_STOP, 0)]
    own1 = fly + [[200, 'target_slot', 2], [300, 'goto_obj', SHIP2, [60, 10, 300]], key(1045, 'state', S_ACCELERATE, 1),
                  key(1330, 'state', S_BOOST, 1), key(1420, 'state', S_BOOST, 0)]
    own2 = fly + [[200, 'target_slot', 1], key(1000, 'state', S_ACCELERATE, 1)]
    return {'plan': both(shared, own1, own2), 'minutes': 1.5, 'record_from': 200}


def missions():
    """Each player on its own mission at the same time: the release check's satellite plan (tools/dev/release_check.py
    satellite_plan, op rc_drive): every player flies the Terran cadet's first chapter by its own inputs (the mission made
    active, Shift+A to each objective, the satellite launched, out in the spacesuit, the repair laser) and completes it
    for itself."""
    end = int(7.0 * 1800) - 100
    plan = [[200, 'rc_look'], [210, 'rc_offers'], [300, 'every', 30, end, [['rc_drive']]], [330, 'every', 300, end, [['rc_look']]]]
    return {'plan': [plan, [list(row) for row in plan]], 'minutes': 7.0, 'record_from': 200}


def station():
    """The trade station IDJ-703 from two places, for a time lapse of its traffic: player 1 2.6 km in front of it,
    player 2 off to one side and above, both at rest in their cockpits with the HUD. Before that, the flight there:
    both 14 km out, side by side, travel drive."""
    shared = [[330, 'slot_near', 2, 1, 60]]
    own1 = [[300, 'goto_obj', STATION, 14000], [360, 'target_slot', 2], [400, 'travel'], [640, 'travel_stop'],
            key(660, 'action', A_STOP, 0), [900, 'goto_obj', STATION, 2600]]
    own2 = [[360, 'target_slot', 1], [400, 'travel'], [640, 'travel_stop'], key(660, 'action', A_STOP, 0),
            [900, 'goto_obj', STATION, [1900, 700, -1500]]]
    return {'plan': both(shared, own1, own2), 'minutes': 6.0, 'record_from': 250}


SNAPSHOT_005 = str(__import__('pathlib').Path(__file__).resolve().parents[4] /
                   'X4-Multiplayer-parallel-mp/.local/native-lockstep/snapshot-capture-005/native-snapshot/save_009.xml.gz')
WHARF_005 = '00000000000434b1'
# The release line's gate (build-rel18) stopped player 2's client with stop 223 when it walked from its docked ship
# onto the dock (the take of 26 Nov 15:24, tick 2,935); the walk is recorded with the gate that has that fix.
STOP223_DLL = str(__import__('pathlib').Path(__file__).resolve().parents[4] /
                  'X4-Multiplayer-stop223/.local/try/build-stop223/x4lan_gate_64.dll')
A_GET_UP, S_FOOT_BACK = 0x115, 0x1b


def walk():
    """Both players on foot on the same dock, each seeing the other's figure: the station walk test's scenario `meet`
    (tools/dev/station_walk_test.py plan --scenario meet), on its own save (snapshot 005, Argon Prime). Each player's
    ship gets X4's docking computer and docks at the station by the autopilot; then the get-up key, a few steps out of
    the ship, and each walks towards the other (player 1 to 34 m, player 2 up to 3 m). The inner ticks count from each
    ship's docking."""
    def chain(first, get_up, back, meet):
        return [[30, 'dock_kit', 1], [30, 'dock_kit', 2],
                [first, 'dock_station', WHARF_005, 13000,
                 [[60, 'foot_where'], key(get_up, 'action', A_GET_UP, 0), key(back, 'state', S_FOOT_BACK, 1),
                  key(back + 200, 'state', S_FOOT_BACK, 0), [back + 220 + (10 if first == 60 else 0), 'foot_where'], meet]]]
    return {'plan': [chain(60, 700, 860, [1100, 'foot_meet', 2, 34, 700]), chain(90, 150, 300, [540, 'foot_meet', 1, 3, 720])],
            'minutes': 2.4, 'record_from': 1800, 'snapshot': SNAPSHOT_005, 'gate_dll': STOP223_DLL}


# The session with bigger ships (.local/showcase/session-big: the Teladi start x4ep1_gamestart_dlc_mini_02 as
# x4ls new-session captured it for the turret tests, session tm1). Player 1 flies the start's own Teladi bomber
# (M); slot 2 of its session.json names a Teladi destroyer of the save (L, 000000000009d2e0) as player 2's ship, the
# way tm1 named a second bomber: the session hands a saved ship to the slot's faction and seats the player in it.
BIG_SHIP1, BIG_SHIP2 = '00000000000ae8fe', '000000000009d2e0'
BIG_MELEE = [('Kestrel', 'teladi', 'tel_s_fighter_01_a', 700, 10, 1),
             ('Phoenix', 'teladi', 'tel_l_destroyer_01_a', 1500, 1, 1),
             ('Falcon', 'teladi', 'tel_s_fighter_02_a', 850, 6, 2),
             ('Osprey', 'teladi', 'tel_m_frigate_01_a', 1100, 3, 2),
             ('Peregrine', 'teladi', 'tel_m_bomber_01_a', 600, 2, 2),
             ('Raider', 'xenon', 'xen_s_fighter_01_a', 2300, 10, 1),
             ('Hunter', 'xenon', 'xen_s_fighter_02_a', 2500, 8, 2),
             ('Reaver', 'xenon', 'xen_m_corvette_01_a', 2800, 3, 2),
             ('Devastator', 'xenon', 'xen_xl_destroyer_01_a', 4600, 1, 2)]


# the same battle with fewer ships (14 against 14): the 22 against 22 ran at 0.6 of real time on the recording PC
BIG_MELEE_14 = [('Kestrel', 'teladi', 'tel_s_fighter_01_a', 700, 6, 1),
                ('Phoenix', 'teladi', 'tel_l_destroyer_01_a', 1500, 1, 1),
                ('Falcon', 'teladi', 'tel_s_fighter_02_a', 850, 4, 2),
                ('Osprey', 'teladi', 'tel_m_frigate_01_a', 1100, 2, 2),
                ('Peregrine', 'teladi', 'tel_m_bomber_01_a', 600, 1, 2),
                ('Raider', 'xenon', 'xen_s_fighter_01_a', 2300, 6, 1),
                ('Hunter', 'xenon', 'xen_s_fighter_02_a', 2500, 5, 2),
                ('Reaver', 'xenon', 'xen_m_corvette_01_a', 2800, 2, 2),
                ('Devastator', 'xenon', 'xen_xl_destroyer_01_a', 4600, 1, 2)]


def big_battle_14():
    """big_battle with 14 ships a side."""
    made = big_battle()
    rows = {(row[3]): row for row in battle(750, BIG_MELEE_14)}
    made['plan'] = [[rows[row[3]] if row[1] == 'hostile' else row for row in plan] for plan in made['plan']]
    # after the flight, three seconds with both cameras at the same place (X4's target view of the same ship): the only
    # way to measure how far apart the two windows draw the same step (align.py), for the cut's "lag"
    same = [[2150, 'target_named', 'Devastator 1', 'xenon'], local(2170, A_TARGET_VIEW), local(2290, A_COCKPIT_VIEW)]
    made['plan'] = [sorted(plan + same, key=lambda e: e[0]) for plan in made['plan']]
    return made


def big_battle_late():
    """big_battle_14 with everything after the first placement 900 ticks later: the start's story (the Teladi start's
    "The Envoy") calls both players between about tick 600 and 1,500, and its comm window sat in every battle shot."""
    made = big_battle_14()
    made['plan'] = [[[row[0] + 900] + row[1:] if row[0] >= 250 else row for row in plan] for plan in made['plan']]
    # X4's external chase camera for the flight, the HUD stays on: from the bomber's cockpit (and the destroyer's
    # bridge) the other player's ship was hardly to be seen. The bomber starts 180 m to the destroyer's left, behind
    # it at first (the destroyer ahead in player 1's picture), then passes it (the bomber ahead in player 2's).
    made['plan'] = [sorted([row[:4] + [-180] if row[:4] == [1766, 'slot_near', 1, 2] else row for row in plan] +
                           [local(1790, A_EXTERNAL_VIEW)], key=lambda e: e[0]) for plan in made['plan']]
    made['minutes'] = 1.8
    made['record_from'] = 900
    return made


def big_battle():
    """The centrepiece with two different ships: player 1 in a Teladi bomber (M), player 2 in a Teladi destroyer (L),
    flying together through a fleet battle, each in its own cockpit with X4's HUD and the other's ship as its target.
      150   the destroyer beside the bomber; 200 each player targets the other's ship
      260   the bomber 900 m in front of the destroyer, looking at it; 300 it flies past the destroyer
      700   side by side; 730 the destroyer 1,300 m to the right, 750 the fleets are made ahead of both (22 against 22)
      860   both to the middle of the front, 250 m apart
      900   the destroyer sets off, 1100 the bomber after it: it catches up and passes; 2100 both stop"""
    shared = ([[150, 'slot_near', 2, 1, 300], [700, 'slot_near', 1, 2, -250], [730, 'slot_near', 2, 1, 1300]] +
              battle(750, BIG_MELEE) + [[860, 'slot_near', 2, 1, 1000], [866, 'slot_near', 1, 2, -250]])
    own1 = [[200, 'target_slot', 2], [260, 'goto_obj', BIG_SHIP2, [300, 40, 900]],
            key(300, 'state', S_ACCELERATE, 1), key(520, 'state', S_ACCELERATE, 0), key(560, 'action', A_STOP, 0),
            key(1100, 'state', S_ACCELERATE, 1), key(1400, 'state', S_ACCELERATE, 0), key(2100, 'action', A_STOP, 0)]
    own2 = [[200, 'target_slot', 1], key(900, 'state', S_ACCELERATE, 1), key(1300, 'state', S_ACCELERATE, 0),
            key(2100, 'action', A_STOP, 0)]
    return {'plan': both(shared, own1, own2), 'minutes': 1.3, 'record_from': 100, 'session': 'session-big'}


WHARF_AVK = '000000000001b5af'      # snapshot 005: the wharf AVK-777 of Argon Prime


def timelapse():
    """A station's traffic for a time lapse, from two places: both players at rest in their cockpits in front of the
    wharf AVK-777 of Argon Prime (snapshot 005), player 1 3 km in front of it, player 2 off to one side and above."""
    own1 = [[200, 'goto_obj', WHARF_AVK, 3000], key(230, 'action', A_STOP, 0)]
    own2 = [[200, 'goto_obj', WHARF_AVK, [2300, 800, -2000]], key(230, 'action', A_STOP, 0)]
    return {'plan': both([], own1, own2), 'minutes': 5.5, 'record_from': 300, 'snapshot': SNAPSHOT_005}


SECTOR_005 = '0000000000000729'
# snapshot 005: ships the action tests hand to the players' faction as its fleet (fighters, couriers, warships)
FLEET_005 = ['0000000000176a13', '0000000000176a74', '0000000000176ad5', '0000000000176b36', '00000000000a56bf',
             '00000000000a7949', '00000000000a57a0', '00000000000a5722', '00000000000a7784', '00000000000a7984',
             '00000000000a1885', '00000000000a16fe']


def docks():
    """A time lapse of the players' own fleet at a station: twelve ships of the players' faction are ordered to dock at
    the Argon factory CZF-533 (X4's own DockAndWait order, as the interact menu's "Dock" gives it), later to fly off
    (MoveWait) and to dock again, each player ordering six of them, one after the other. Both players dock their own
    ships on the same platform (autopilot, as the station walk test does) and watch from their pads: two views of
    one dock."""
    def orders(ships, first):
        rows = []
        for i, ship in enumerate(ships):
            rows += [[first + 40 * i, 'clear', ship], [first + 40 * i + 20, 'dock', ship, WHARF_005],
                     [3900 + 60 * i, 'move_by', ship, SECTOR_005, 600, 900, 4000],
                     [5200 + 60 * i, 'dock', ship, WHARF_005]]
        return rows
    kit = [[30, 'dock_kit', 1], [30, 'dock_kit', 2]]
    own1 = orders(FLEET_005[0::2], 300) + kit + [[60, 'dock_station', WHARF_005, 13000, [[60, 'foot_where']]]]
    own2 = orders(FLEET_005[1::2], 320) + kit + [[90, 'dock_station', WHARF_005, 13000, [[60, 'foot_where']]]]
    return {'plan': both([], own1, own2), 'minutes': 3.8, 'record_from': 1900, 'snapshot': SNAPSHOT_005,
            'set': ['native_gate_ui_replay_fleet=' + '{' + ','.join("'%s'" % ship for ship in FLEET_005) + '}',
                    'native_gate_battle_engage=false']}


SHIP1_005 = '00000000000ae79c'       # snapshot 005: player 1's own ship


def pads():
    """A time lapse of a station's own traffic, with no fixture: player 1 docks its ship on the platform of the Argon
    factory CZF-533 (autopilot) and watches from its pad; player 2 hangs 250 m above the platform, 350 m behind player
    1's pad, and looks down on it."""
    kit = [[30, 'dock_kit', 1], [30, 'dock_kit', 2]]
    own1 = kit + [[60, 'dock_station', WHARF_005, 13000, [[60, 'foot_where']]]]
    own2 = kit + [[2500, 'goto_obj', SHIP1_005, [0, 250, -350]], key(2530, 'action', A_STOP, 0)]
    return {'plan': both([], own1, own2), 'minutes': 5.6, 'record_from': 2540, 'snapshot': SNAPSHOT_005}


TAKES = {'biglate': big_battle_late, 'pads': pads, 'big14': big_battle_14, 'docks': docks, 'timelapse': timelapse, 'big': big_battle, 'probe': probe, 'main': main, 'battle': battle_flight, 'missions': missions, 'station': station, 'walk': walk}
