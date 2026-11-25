"""Action tests: both players of a live two-client session do a chain of player actions by script; the test checks
that each action happened in the game and that the clients stayed in sync through it.

    python tools/dev/action_tests.py list
    python tools/dev/action_tests.py run SCENARIO --snapshot SAVE.xml.gz --gate-dll x4lan_gate_64.dll
    python tools/dev/action_tests.py report SCENARIO CLIENT1.log CLIENT2.log [--run RUNDIR]
    python tools/dev/action_tests.py plan SCENARIO

Scenarios (the snapshot is the two-player one: both ships side by side in a sector of Argon stations):
  trade   each player gets money, flies to a station by X4's autopilot, docks, opens the trade menu with the dock
          menu's Trade button, sets an amount and presses Confirm (the stock menus' own handlers), undocks, flies to a
          second station, docks there and sells the ware the same way. --by calls makes the Confirm button's calls
          without the menus.
  orders  each player orders a fleet ship (an Argon fighter with its pilot, handed to the player faction by a fixture)
          as the map and the interact menu do: fly to a place, explore the sector, patrol it, patrol as the default
          behaviour, dock at a station.
  crew    for a fleet ship with a pilot and two crew, each player fires a crew member, fires the captain and promotes
          the remaining crew member to captain (the crew page's calls); then docks at a wharf and hires a person of
          the station for that ship (the comm, the 'Hire' choice, the map's Hire button).
  shipbuy each player buys a scout at the wharf as the ship configuration's Confirm does and waits for it to be built.
  refit   each player docks at the wharf and has its own ship refitted with the wharf's high preset, then undocks.
          The player who asks second is sent to the wharf's second dock, which X4's autopilot does not reach.
  refitorder each player orders that refit for a fleet fighter out in space (the map's "Upgrade" at the wharf, the
          configuration's Confirm); X4's Equip order has the ship's pilot fly it in and dock, and the wharf refits it.
  orders2 more of the interact menu's orders for fleet ships: follow a ship, protect a station, collect the cargo
          the player dropped, pick up a satellite the player launched, attack in range, withdraw from combat.
  repair  each player sends a damaged fleet fighter to the wharf to dock and has it repaired there.
  wing    each player makes one fleet fighter the subordinate of another ("Assign as subordinate"), sends the leader
          8 km off (the subordinate follows), orders the leader to attack a Xenon fighter, and removes the assignment.
  shipsell each player sends a fleet fighter to the wharf to dock and sells it there (the map's "Sell ships").
  crewmove each player moves a crew member from one fleet ship to another (the crew transfer's Confirm).
  fleettrade each player has a fleet courier buy a ware at one station (a trade order), fly to a second station and
          sell it there.
  deploy  each player renames its own ship, launches a satellite, a nav beacon, a resource probe, a mine and a laser
          tower from it, drops cargo, switches its satellite off and on and has it destroy itself.
  systems each player sets its own ship's weapon groups, flight assist and travel mode, then a fleet warship's turret
          modes, armed state and drones (the docked menu and the map's loadout page).
  settings each player changes settings of the player information menu (standing orders, waiting ships, resupply
          level, weapon mode, name, paint, logo) and its own ship's answer to a signal and resupply level.
  storage each player has a fleet fighter dock at a fleet capital ship (a carrier, a resupply ship), puts it into the
          ship's internal storage and retrieves it (the interact menu's entries).
  claim   each player has a fleet frigate claim an abandoned scout (the interact menu's "Claim"): a marine flies over
          and the ship changes to the player faction.
  mining  each player has a fleet miner drop two resource probes at places three kilometres off (the interact menu's
          "Deploy" on a place of the sector) and, when the sector yields ore, mine it ("Mine").
  wingset each player forms a wing of two fleet fighters, switches each of the group's five settings off and on
          (attack on sight, dock at commander, ...), changes the group's role, names the fleet and moves the
          subordinate to a second group.
  routines each player gives a fleet courier automatic trading of one ware and a fleet miner automatic mining, both in
          this sector, as their default behaviour (the map's behaviour page: the routine, its ware, its range, Confirm).
  exchange each player has a fleet courier hand 30 of its 50 energy cells to a fleet capital ship (the map's ware
          exchange between two ships of the player's).
  tradeloop each player puts a fleet courier on repeat orders (the behaviour page) and gives it a buy at one station
          and a sale at another (the trade menu's loop Confirm); the courier buys, sells and buys again.
  hostile each player marks a neutral freighter as hostile (the interact menu's "Mark as hostile" and the Yes of
          X4's question).
  rules   each player makes a blacklist, a fight rule and a trade rule for the faction (the global orders of the
          player information menu), sets the first two for its own ship, renames the blacklist, removes the trade rule.
  dockpair both players dock at one station and watch their dock menus every 10 ticks while the other player docks and
          undocks: does one player's dock or undock close the other's dock menu; after its own undock, is each player's
          dock menu closed and the HUD's top level menu open (--swap: the other player leaves first).
  interact each player opens Ship Interactions in flight from the HUD's top level bar (the button's own handler),
          presses its Flight Assist, opens it from the map's tabs, goes to Ship Information and Back to it, presses
          Flight Assist again; player 1 first, then player 2, the other client's dock menu watched to stay closed.
  mapopen a diagnostic: one player's client opens the map and keeps it open for 100 seconds; nothing else happens.
  reps    each player comms the Argon representative (one conversation at a time: player 1 first), chooses "I would
          like to buy a blueprint", picks the cheapest blueprint it lacks and presses Confirm, then "Show me your
          licences" and the same for a licence (the shop's own handlers), and ends the conversation. Each player docks
          at the Argon headquarters NLN-075 first: the representative has its shop only while X4 gives it an office
          there. --players 2: player 2 alone at the headquarters, player 1 stays out.
  itemtrader each player docks at a station, comms an item trader of its docks, opens its wares, buys one piece and ends
          the conversation; checks that player's own inventory.
  hack    each player docks, gets up, takes the transporter to the station's Security Office and hacks one of its
          control panels (the question's Yes) with a Security Decryption System from its own inventory.

How a scenario runs: every client's plan (native_gate_ui_replay_script) holds the fixtures (X4's docking computer on
the players' ships, the money) and one `chain` for its own player: steps that each begin when the one before has ended
(native_gate_ui_replay_core.lua K.chain), because flying and docking take as long as they take. The calls are the ones
the stock menus make, captured and replayed on every client as a person's click is; nobody clicks, so what a person
sees on the way (the menu itself, its sliders) is not covered.

What every scenario checks, from each client's own log and the relay's:
  running   no client's gate stopped and the relay saw no error
  chain     each player's chain ran to its last step
  in sync   the world, the players' records and their inputs agree at every tick (the clients' digest parts); the
            runtime ID word alone is reported as a note (see station_walk_test.py). The whole world is hashed every
            1,800 ticks: a run goes on to the capture after its last step, and one without a capture fails this check
and the scenario's own checks of what the game then held (trade: docked, cargo after the buy, undocked, docked at the
second station, cargo after the sale, the account's moves the same on both clients).

`run` starts its own windowed clients with disposable profiles (x4ls play), beside other sessions when
--first-profile, --lanes, --display and --tile-skip keep them apart, and stops the clients it started. Exit code: 0 all
checks passed, 1 a check failed, 2 the run itself did not work.
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import station_walk_test as rig  # noqa: E402  (lua values, log readers, window pictures, priorities)

ACCELERATE = 13                      # input state: throttle up
SCRIPT = re.compile(r'UI_REPLAY_SCRIPT tick=(\d+) op=(\w+) ok=(\w+) result=(.*)')
STEP = re.compile(r'^step=(\S+) n=(\d+) at=(\d+) ok=(\w+) ?(.*)$')
END = re.compile(r'^chain_end ok=(\w+) steps=(\d+)/(\d+)(?: why=(\S+))?')
MONEY = re.compile(r'UI_TRADE_MONEY tick=(\d+) UITRADE_1:([^;\s]*)')
APPLIED = re.compile(r'UI_REPLAY_APPLIED tick=(\d+) slot=(\d+) op=(\d+)(?: result=\S+)? what=(\S+)')
REFUSED = re.compile(r'(UI_REPLAY_REFUSED|UI_GUARD_REFUSED|UI_BUILD_REFUSED)\b(?: tick=\d+)? (?:what|op)=(\S+)(?: (?:why|group)=(\S+))?')
BUILT = re.compile(r'UI_BUILD_APPLIED tick=(\d+) slot=(\d+) what=(\S+)')          # the build and assets families' replayed calls
PLAYERS = (1, 2)


def parse(log):
    """One client's log: its chain's step answers, the chain's end, its other script rows, the account's rows, the
    replayed calls, stops."""
    out = {'steps': {}, 'end': None, 'ops': {}, 'money': [], 'applied': [], 'built': [], 'stops': [], 'rows': [], 'refused': {}}
    for line in rig.read_lines(log):
        if 'NATIVE_GATE STOP' in line or 'PILOT_STOP_WHY' in line:
            out['stops'].append(line.split('[X4LAN] ')[-1][:300])
        m = MONEY.search(line)
        if m:
            out['money'].append((int(m.group(1)), m.group(2)))
            continue
        m = APPLIED.search(line)
        if m:
            out['applied'].append((int(m.group(1)), int(m.group(2)), m.group(4)))
            continue
        m = BUILT.search(line)
        if m:
            out['built'].append((int(m.group(1)), int(m.group(2)), m.group(3)))
            continue
        m = REFUSED.search(line)
        if m:          # what this client's own actions were refused (the session shares no such call, or not for this player)
            key = '%s (%s)' % (m.group(2), m.group(3) or m.group(1).split('_')[1].lower())
            out['refused'][key] = out['refused'].get(key, 0) + 1
            continue
        if 'UI_REPLAY_WALLET' in line or 'UI_REPLAY_DOCK_KIT' in line:
            out['rows'].append(line.split('[X4LAN] ')[-1].strip())
            continue
        m = SCRIPT.search(line)
        if not m:
            continue
        tick, op, ok, result = int(m.group(1)), m.group(2), m.group(3) == 'true', re.sub(r' scope=\d\s*$', '', m.group(4).rstrip())
        if op != 'chain':
            out['ops'].setdefault(op, []).append((tick, ok, result))
            continue
        if not ok:          # the chain's own step raised: nothing after it runs
            out['end'] = {'ok': False, 'done': 0, 'of': 0, 'why': 'error: ' + result[:200], 'tick': tick}
            continue
        for part in result.split(' | '):
            s = STEP.match(part)
            if s:
                out['steps'].setdefault(s.group(1), []).append({'tick': tick, 'n': int(s.group(2)), 'at': int(s.group(3)),
                                                                'ok': s.group(4) == 'true', 'text': s.group(5)})
                continue
            e = END.match(part)
            if e:
                out['end'] = {'ok': e.group(1) == 'true', 'done': int(e.group(2)), 'of': int(e.group(3)), 'why': e.group(4), 'tick': tick}
    return out


def answers(client, name):
    return [r for r in client['steps'].get(name, [])]


def last_answer(client, name, starts=None):
    """The last answer of that step (that begins with `starts`), or None."""
    for r in reversed(client['steps'].get(name, [])):
        if r['ok'] and (starts is None or r['text'].startswith(starts)):
            return r
    return None


class Checks:
    def __init__(self):
        self.failed = 0

    def __call__(self, name, ok, detail):
        self.failed += 0 if ok else 1
        print('%-9s %-4s %s' % (name, 'ok' if ok else 'FAIL', detail))

    def note(self, name, detail):
        print('%-9s %-4s %s' % (name, 'note', detail))


def common_checks(check, clients, logs, run_dir):
    stops = [x for c in clients.values() for x in c['stops']]
    last, mismatch, errors = rig.relay_state(run_dir)
    check('running', not stops and not errors, stops[0] if stops else errors[0][:200] if errors else 'no stop, no relay error; ' + last[:60])
    for k in PLAYERS:
        end = clients[k]['end']
        check('chain', bool(end) and end['ok'], 'player %d: %s' % (k, 'no end row (the chain was still running)' if not end else
              'all %d steps, the last at tick %d' % (end['of'], end['tick']) if end['ok'] else
              'ended after %d of %d steps: %s' % (end['done'], end['of'], end['why'])))
    for k in PLAYERS:          # a refused action is the session saying it does not share that call (for that player)
        refused = clients[k]['refused']
        if refused:
            check.note('refused', "player %d's client was refused: %s" % (k, ', '.join('%s x%d' % (what, n) for what, n in sorted(refused.items()))))


WATCH_FROM, WATCH_EVERY = 300, 300      # ticks into the window: the first reading, and the stride
CAPTURE_EVERY = 1800                    # the clients hash the whole world every so many ticks (the digest's world part)


def watch_rows(ops, minutes):
    """Readings for both clients' plans: every op of `ops` at the same ticks in both, to the window's end (one plan
    entry: the script op `every`)."""
    last = int(minutes * 60 * 30) - 150
    return [(WATCH_FROM, 'every', WATCH_EVERY, last - 1, [tuple(op) for op in ops])] if ops else []


def watch_check(check, clients, ops):
    # What both clients read at the same tick must be the same text: each reads the same game.
    names = sorted({op[0] for op in ops})
    if not names:
        return
    rows = [[(tick, name, result) for name in names for tick, ok, result in clients[k]['ops'].get(name, [])] for k in PLAYERS]
    upto = min((max(r[0] for r in rows[k]) if rows[k] else 0) for k in range(2))
    a, b = (sorted(r for r in rows[k] if r[0] <= upto) for k in range(2))
    differ = [x for x in a if x not in b] + [x for x in b if x not in a]
    check('readings', bool(a) and not differ, 'the %d readings (%s) both clients took to tick %d are the same' % (len(a), ', '.join(names), upto) if a and not differ else
          'no reading' if not a else '%d readings differ, the first at tick %d: %s' % (len(differ), min(x[0] for x in differ), sorted(differ)[0][2][:160]))


def world_objects(log, tick):
    """One client's per-object rows of the world capture at `tick` (native_gate_world_pages_window): id -> fields."""
    out, want = {}, b'WORLD_OBJECT tick=%d ' % tick
    try:
        with open(log, 'rb') as f:
            for line in f:
                at = line.find(want)
                if at >= 0:
                    fields = dict(re.findall(r'(\w+)=(\S+)', line[at:].decode('latin1')))
                    fields.pop('nav_raw', None)
                    out[fields.get('id')] = fields
    except OSError:
        pass
    return out


def class_names():
    """Object class by the type word of a WORLD_OBJECT row (the class's vtable in X4.exe), when x4-re's symbols are at hand."""
    names = {}
    try:
        with open(ROOT.parent / 'x4-re' / 'symbols.csv', encoding='utf-8', errors='replace') as f:
            for line in f:          # rva,va,name,kind,...: the vtable rows
                m = re.match(r'0x([0-9a-f]+),0x[0-9a-f]+,([^,]+)::vftable,vtable,', line)
                if m:
                    names[m.group(1).lstrip('0')] = m.group(2)
    except OSError:
        pass
    return names


def world_difference(logs, tick, limit=12):
    """What the two clients' captures at `tick` disagree on, object by object: lines to print."""
    a, b = (world_objects(path, tick) for path in logs)
    if not a or not b:
        return ['  no per-object rows of tick %d in the logs (run with the pages: not --no-pages)' % tick]
    names = class_names()
    lines = ['  %d objects in client 1\'s capture, %d in client 2\'s' % (len(a), len(b))]
    only = [(1, k, a[k]) for k in sorted(set(a) - set(b))] + [(2, k, b[k]) for k in sorted(set(b) - set(a))]
    for client, key, row in only[:limit]:
        lines.append('  only on client %d: object %s class %s parent %s' % (client, key, names.get(row.get('type', '').lstrip('0'), row.get('type')), row.get('parent')))
    differ = [(k, [f for f in a[k] if a[k][f] != b[k].get(f)]) for k in sorted(set(a) & set(b)) if a[k] != b[k]]
    for key, fields in differ[:limit]:
        lines.append('  differs: object %s class %s parent %s in %s' % (key, names.get(a[key].get('type', '').lstrip('0'), a[key].get('type')), a[key].get('parent'), ', '.join(fields)))
    if len(only) > limit or len(differ) > limit:
        lines.append('  ... %d objects on one client only, %d that differ' % (len(only), len(differ)))
    if not only and not differ:
        lines.append('  every object both captures list is the same: the difference is in an object neither lists (unchanged since the load)')
    return lines


def sync_check(check, logs, run_dir, need_capture=False):
    # As station_walk_test.py: which part of the digest differed. The world, the records and the inputs must agree at
    # every tick; the runtime ID word alone is a note.
    parts = [rig.digest_parts(path) for path in logs]
    _, mismatch, _ = rig.relay_state(run_dir)
    if parts[0] and parts[1]:
        differ = {}
        both = sorted(set(parts[0]) & set(parts[1]))
        for tick in both:
            for name in parts[0][tick]:
                if name != 'hash' and parts[0][tick][name] != parts[1][tick].get(name):
                    differ.setdefault(name, []).append(tick)
        hard = {name: ticks for name, ticks in differ.items() if name != 'rid'}
        captures = len([tick for tick in both if tick % CAPTURE_EVERY == 0])
        check('in sync', not hard and (captures > 0 or not need_capture), 'world, records and inputs agree at all %d ticks both clients logged (to tick %d; %d captures of the whole world)' %
              (len(both), both[-1] if both else 0, captures) if not hard and (captures or not need_capture) else
              'the run ended before the first capture of the whole world (tick %d): records and inputs agree at the %d ticks logged' % (CAPTURE_EVERY, len(both)) if not hard else
              '; '.join('%s differs at %d ticks from tick %d' % (name, len(ticks), ticks[0]) for name, ticks in sorted(hard.items())))
        if 'world' in hard:          # which objects: the captures' per-object rows
            for line in world_difference(logs, hard['world'][0]):
                print(line)
        if 'rid' in differ:
            check.note('ids', 'the runtime ID word differed at %d ticks, first %d (not counted as a failure)' % (len(differ['rid']), differ['rid'][0]))
    else:
        check('in sync', mismatch is None, 'no digest mismatch' if mismatch is None else 'first mismatch at tick %d, %d mismatching ticks' % mismatch)


# ---- trade ---------------------------------------------------------------------------------------------------------
class Trade:
    """Buy at one station, sell at another, each player with its own ship."""
    name = 'trade'
    minutes = 8
    sets = ()                           # extra config keys of the scenario's session
    marks = ('set_buy', 'bought', 'sold')          # a picture of both windows when a player's step of that name answers
    # The two-player snapshot's sector: MMJ-623 makes food rations, the shipyard NLN-075 ten kilometres on buys them.
    BUY, SELL, WARE, AMOUNT, CREDITS = '0000000000040496', '000000000001451a', 'foodrations', 20, 50000
    LIMIT = 12000

    @staticmethod
    def options(s):
        s.add_argument('--buy-station', default=Trade.BUY, help='saved ID of the station that sells the ware')
        s.add_argument('--sell-station', default=Trade.SELL, help='saved ID of the station that buys it')
        s.add_argument('--ware', default=Trade.WARE)
        s.add_argument('--amount', type=int, default=Trade.AMOUNT)
        s.add_argument('--credits', type=int, default=Trade.CREDITS, help='money the fixture adds to the account before the run')
        s.add_argument('--by', choices=('menu', 'calls'), default='menu',
                       help="menu: the dock menu's Trade button, the trade menu's amount and its Confirm button, by the "
                            "stock menus' own handlers; calls: the calls that Confirm button makes, without the menu")

    @staticmethod
    def chain(k, a):
        ware, amount = a.ware, a.amount
        return [
            ('dock_buy', 'until', '^docked', 'dock_station', a.buy_station, 9000),
            ('settle_buy', 'wait', 60),
            ('at_buy', 'op', 'foot_where'),
            # is the click this stands for offered to a person here? (a script's own calls pass a blocked menu entry)
            ('entry_trade', 'op', 'menu_entry', 'MapMenu', 'buttonConfirmTrade'),
        ] + Trade.deal(k, a, 'buy', a.buy_station, amount) + [
            ('bought', 'poll', '^amount=%d ' % amount, 10, 600, 'cargo', ware),
            # X4 answers 'tradecomputerbusy' for a few seconds after a docked trade, and its dock menu offers the
            # button only once the answer is 'granted': the script waits for that answer as the menu does. (An undock
            # sent while busy is still replayed, and goes through if the computer is free by then: run 2, 17 Nov.)
            ('undock_ready', 'poll', '^granted', 10, 900, 'undock_check'),
            ('undock', 'op', 'undock'),
            ('undocked', 'poll', 'docked=false', 30, 1800, 'foot_where'),
            ('thrust_on', 'op', 'input', 'state', ACCELERATE, 1),
            ('thrust', 'wait', 90),
            ('thrust_off', 'op', 'input', 'state', ACCELERATE, 0),
            ('dock_sell', 'until', '^docked', 'dock_station', a.sell_station, 9000),
            ('settle_sell', 'wait', 60),
            ('at_sell', 'op', 'foot_where'),
        ] + Trade.deal(k, a, 'sell', a.sell_station, -amount) + [
            ('sold', 'poll', '^amount=0 ', 10, 600, 'cargo', ware),
            ('settle_end', 'wait', 360),          # the station pays about seven seconds after the sale
        ]

    @staticmethod
    def deal(k, a, name, station, amount):
        """The steps of one docked trade: `amount` units into the ship (negative: out of it)."""
        look = [('offer_' + name, 'op', 'trade_look', station, a.ware)] + ([('cargo_before', 'op', 'cargo', a.ware)] if name == 'buy' else [])
        if a.by == 'calls':
            return look + [(name, 'op', 'trade_' + name, station, a.ware, abs(amount), k, 1)]
        return [
            # X4 puts the dock menu on screen about five seconds after the docking
            ('dockmenu_' + name + '_wait', 'wait', 180),
            ('dockmenu_' + name, 'op', 'menu_look', 'DockedMenu'),
            ('open_' + name, 'op', 'trade_menu_open', station),                          # "Trade with station"
            ('menu_' + name, 'poll', 'context=trade', 5, 300, 'menu_look', 'MapMenu'),
            ('menu_' + name + '_settle', 'wait', 45),                                    # the menu builds its rows over a few frames
        ] + look + [
            ('amount_' + name, 'op', 'trade_menu_amount', a.ware, amount),               # the ware's amount slider
            ('set_' + name, 'poll', 'amounts=%-?%d', 2, 60, 'menu_look', 'MapMenu'),
            (name, 'op', 'menu_call', 'MapMenu', 'buttonConfirmTrade'),                  # "Confirm"
        ]

    @staticmethod
    def watch(a):
        return [('cargo', a.ware, 1), ('cargo', a.ware, 2)]          # each player's hold and the account, on both clients

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', a.credits)]
        return [kit + [(60 + 30 * (k - 1), 'chain', Trade.LIMIT, Trade.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        ware, amount = a.ware, a.amount
        stations = {}
        for k in PLAYERS:
            c = clients[k]
            docked = last_answer(c, 'dock_buy', 'docked')
            check('docked', bool(docked), 'player %d at the selling station: %s' % (k, 'tick %d (%s)' % (docked['tick'], docked['text']) if docked else
                  (answers(c, 'dock_buy') or [{'text': 'no row'}])[-1]['text'][:160]))
            entry = last_answer(c, 'entry_trade')
            check('offered', bool(entry) and entry['text'].startswith('open'), "player %d: the trade menu's Confirm button (%s) is %s" % (
                k, 'MapMenu.buttonConfirmTrade', 'open to a person on this client' if entry and entry['text'].startswith('open') else
                'REFUSED to a person in this session (%s): the calls of the script trade, a click would not' % (entry['text'] if entry else 'no row')))
            if a.by == 'menu':
                for name in ('buy', 'sell'):
                    shown, pressed, via, amounts = (last_answer(c, x) for x in ('menu_' + name, name, 'open_' + name, 'set_' + name))
                    opened = bool(shown) and 'context=trade' in shown['text'] and 'immediate=true' in shown['text']
                    check('menu', opened and bool(pressed) and pressed['text'].startswith('called') and bool(amounts), 'player %d, %s: %s; amounts set: %s; Confirm answered %s' % (
                        k, name, 'the trade menu opened for the docked ship (%s; %s)' % (via['text'] if via else '?', shown['text'][:100]) if opened else
                        'the trade menu did not open for the docked ship (%s)' % ((answers(c, 'menu_' + name) or [{'text': 'no row'}])[-1]['text'][:110]),
                        rig.field(amounts['text'], 'amounts') if amounts else 'none', pressed['text'] if pressed else 'nothing'))
            offer = last_answer(c, 'offer_buy')
            price = re.search(r'sell=([\d.]+)/(\d+)', offer['text']) if offer else None
            check('offer', bool(price), "player %d: the station's sell offer for %s: %s" % (k, ware, '%s Cr a unit, %s in stock' % price.groups() if price else
                  (offer['text'] if offer else 'no row')))
            before, got = last_answer(c, 'cargo_before'), last_answer(c, 'bought', 'amount=%d ' % amount)
            had = rig.field(before['text'], 'amount') if before else None
            check('bought', bool(got) and had == '0', 'player %d: %s of %s in its hold before, %s' % (k, had, ware, '%d at tick %d' % (amount, got['tick']) if got else
                  'never %d (%s)' % (amount, (answers(c, 'bought') or [{'text': 'no row'}])[-1]['text'][:120])))
            away = last_answer(c, 'undocked', None)
            away = away if away and 'docked=false' in away['text'] else None
            check('undocked', bool(away), 'player %d: %s' % (k, 'released at tick %d' % away['tick'] if away else 'still docked'))
            docked = last_answer(c, 'dock_sell', 'docked')
            check('docked', bool(docked), 'player %d at the buying station: %s' % (k, 'tick %d (%s)' % (docked['tick'], docked['text']) if docked else
                  (answers(c, 'dock_sell') or [{'text': 'no row'}])[-1]['text'][:160]))
            a_buy, a_sell = last_answer(c, 'at_buy'), last_answer(c, 'at_sell')
            first, second = (rig.field(x['text'], 'station') if x else None for x in (a_buy, a_sell))
            stations[k] = (first, second)
            check('moved', bool(first) and bool(second) and first != second and first != '0' and second != '0',
                  'player %d: docked at station %s for the buy, at station %s for the sale' % (k, first, second))
            offer = last_answer(c, 'offer_sell')
            price = re.search(r'buy=([\d.]+)/(\d+)', offer['text']) if offer else None
            check('offer', bool(price), "player %d: the second station's buy offer for %s: %s" % (k, ware, '%s Cr a unit, wants %s' % price.groups() if price else
                  (offer['text'] if offer else 'no row')))
            sold = last_answer(c, 'sold', 'amount=0 ')
            check('sold', bool(sold), 'player %d: %s' % (k, 'hold empty of %s at tick %d' % (ware, sold['tick']) if sold else
                  'still holds it (%s)' % (answers(c, 'sold') or [{'text': 'no row'}])[-1]['text'][:120]))
        if all(stations.get(k) for k in PLAYERS):
            check('alike', stations[1] == stations[2], 'both players used the same two stations' if stations[1] == stations[2] else 'stations differ: %s' % stations)
        # The account (coop: one wallet; hundredths of a credit): every move at the same tick with the same balance on
        # both clients; after the fixture's money it falls with the buys and rises with the sales.
        rows = [clients[k]['money'] for k in PLAYERS]
        upto = min((r[-1][0] if r else 0) for r in rows)
        same = bool(rows[0]) and [r for r in rows[0] if r[0] <= upto] == [r for r in rows[1] if r[0] <= upto]
        check('account', same, 'the account moved %d times, at the same ticks to the same balances on both clients' % (len(rows[0]) - 1) if same else
              'the account rows differ: client 1 %s, client 2 %s' % (rows[0][-4:], rows[1][-4:]))
        values = []
        for tick, text in rows[0]:
            first = text.split(',')[0]
            if first.isdigit() and (not values or values[-1][1] != int(first)):
                values.append((tick, int(first)))
        moves = [(values[i][0], values[i][1] - values[i - 1][1]) for i in range(1, len(values))]
        gift = next((i for i, (_, d) in enumerate(moves) if d == a.credits * 100), None)
        trades = moves[gift + 1:] if gift is not None else moves
        paid, earned = -sum(d for _, d in trades if d < 0), sum(d for _, d in trades if d > 0)
        check('money', gift is not None and paid > 0 and earned > 0, 'the fixture paid %s; then %s' % (
            ('%d Cr at tick %d' % (a.credits, moves[gift][0])) if gift is not None else 'NOTHING',
            ', '.join('%+.2f Cr at tick %d' % (d / 100, t) for t, d in trades) or 'no move'))
        last = min((max(x[0] for x in clients[k]['applied']) if clients[k]['applied'] else 0) for k in PLAYERS)
        calls = [[x for x in clients[k]['applied'] if x[2] == 'AddTradeToShipQueue' and x[0] <= last] for k in PLAYERS]
        check('replayed', calls[0] == calls[1] and len(calls[0]) == 2 * len(PLAYERS),
              'the %d trade calls ran on both clients at the same ticks: %s' % (len(calls[0]), ', '.join('slot %d at %d' % (s, t) for t, s, _ in calls[0]))
              if calls[0] == calls[1] else 'trade calls differ: client 1 %s, client 2 %s' % (calls[0], calls[1]))


# ---- orders --------------------------------------------------------------------------------------------------------
class Orders:
    """Each player orders a ship of the shared fleet around: fly to a place, explore, patrol, a default behaviour, dock."""
    name = 'orders'
    minutes = 8
    marks = ('arrived', 'docked')
    # Two Argon fighters with pilots that start beside the players (the snapshot's AAO-226 and NLI-119): the fleet
    # fixture hands them to the player faction at the window's start, on every client.
    SHIPS = {1: '0000000000176a13', 2: '0000000000176a74'}
    # The dock order goes to two other ships (Argon police fighters of the sector, ROQ-362 and YME-361): the six
    # fighters beside the players are the snapshot's fixture, whose state the probe samples every step, and that
    # sampling has no docking state in a play session: a fixture fighter that begins to dock stops every client
    # ("native dock controller requires docking state capture", run 10, 17 Nov).
    DOCKERS = {1: '00000000000a56bf', 2: '00000000000a57a0'}
    SECTOR, STATION = '0000000000000729', '0000000000040496'
    # ...and the snapshot's fight between those fighters and six Xenon stays off: a ship under fire keeps an Attack
    # order in front of whatever it is told (run 8, 17 Nov: the fly-to waited behind it).
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (SHIPS[1], SHIPS[2], DOCKERS[1], DOCKERS[2]), 'native_gate_battle_engage=false')
    LIMIT = 14000
    QUEUED = (('explore', 'Explore'), ('patrol', 'Patrol'), ('dock', 'DockAndWait'))
    # the stock menu entry each scripted order stands for: is that click offered to a person in the session?
    ENTRIES = (('move', 'InteractMenu', 'orderMoveWait'), ('explore', 'InteractMenu', 'orderExplore'), ('patrol', 'MapMenu', 'buttonNewOrder'),
               ('behaviour', 'MapMenu', 'buttonDefaultOrderConfirm'), ('dock', 'InteractMenu', 'orderDock'))

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Orders.STATION, help='saved ID of the station the ships are sent to dock at')
        s.add_argument('--metres', type=int, default=20000, help='how far the first order sends each ship')

    @staticmethod
    def chain(k, a):
        ship, sector, docker = Orders.SHIPS[k], Orders.SECTOR, Orders.DOCKERS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
        ] + [('entry_' + name, 'op', 'menu_entry', menu, function) for name, menu, function in Orders.ENTRIES] + [
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
            ('look_cleared', 'op', 'ship_look', ship),
            ('move', 'op', 'move_by', ship, sector, a.metres if k == 1 else -a.metres, 0, a.metres),
            ('move_queued', 'poll', 'MoveWait/', 10, 300, 'ship_look', ship),
            ('arrived', 'poll', '^near=true', 30, 3600, 'ship_arrived', ship, 2000),
            ('explore', 'op', 'explore', ship, sector),
            ('explore_queued', 'poll', 'Explore/', 10, 300, 'ship_look', ship),
            ('patrol', 'op', 'q_patrol', ship, sector),
            ('patrol_queued', 'poll', 'Patrol/', 10, 300, 'ship_look', ship),
            ('behaviour', 'op', 'behaviour', ship, 'Patrol', sector),
            ('behaviour_wait', 'wait', 30),
            ('behaviour_planned', 'op', 'ship_look', ship),
            ('behaviour_confirm', 'op', 'behaviour_confirm', ship),
            ('behaviour_set', 'try', 'default=Patrol', 10, 300, 'ship_look', ship),
            ('clear_again', 'op', 'clear', ship),
            ('look_end', 'op', 'ship_look', ship),
            ('docker_start', 'op', 'ship_look', docker),
            ('docker_clear', 'op', 'clear', docker),
            ('settle_again', 'wait', 30),
            ('dock', 'op', 'dock', docker, a.station),
            ('dock_queued', 'try', 'DockAndWait/', 10, 300, 'ship_look', docker),
            ('docked', 'try', 'docked=true', 60, 9000, 'ship_look', docker),
            ('docker_end', 'op', 'ship_look', docker),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', ship) for ship in (Orders.SHIPS[1], Orders.SHIPS[2], Orders.DOCKERS[1], Orders.DOCKERS[2])]

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', Orders.LIMIT, Orders.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            start = last_answer(c, 'look_start')
            text = start['text'] if start else ''
            check('fleet', rig.field(text, 'owner') == 'player' and rig.field(text, 'pilot') not in (None, '0'),
                  "player %d's ship: %s" % (k, text[:170] or 'no row'))
            for name, menu, function in Orders.ENTRIES:
                entry = last_answer(c, 'entry_' + name)
                opened = bool(entry) and entry['text'].startswith('open')
                check('offered', opened, 'player %d: %s (%s.%s) is %s' % (k, name, menu, function, 'open to a person on this client' if opened else
                      'REFUSED to a person in this session (%s)' % (entry['text'] if entry else 'no row')))
            move, there = last_answer(c, 'move'), last_answer(c, 'arrived', 'near=true')
            check('moved', bool(there), 'player %d: %s' % (k, 'the ship reached the place (%s) at tick %d, %s m off' %
                  (rig.field(move['text'], 'to') if move else '?', there['tick'], rig.field(there['text'], 'distance')) if there else
                  'the ship never got there (%s; %s)' % (move['text'][:90] if move else 'no order row', (answers(c, 'arrived') or [{'text': 'no row'}])[-1]['text'])))
            for name, order in Orders.QUEUED:
                asked, row = last_answer(c, name), last_answer(c, name + '_queued')
                queued = bool(row) and order + '/' in row['text']
                check('queued', queued, "player %d: %s %s" % (k, order, 'in the queue of the ship at tick %d (%s)' % (row['tick'], rig.field(row['text'], 'orders')) if queued else
                      'never in the queue (the call answered %s; %s)' % (asked['text'][:60] if asked else 'nothing', rig.field((answers(c, name + '_queued') or [{'text': ''}])[-1]['text'], 'orders'))))
            row = last_answer(c, 'behaviour_set')
            done = bool(row) and 'default=Patrol' in row['text']
            asked, confirmed = last_answer(c, 'behaviour'), last_answer(c, 'behaviour_confirm')
            seen = [x for x in answers(c, 'behaviour_set') if 'default=' in x['text']]
            check('behaviour', done, 'player %d: %s' % (k, 'the default order is Patrol at tick %d' % row['tick'] if done else
                  'the default order stayed %s (the new default order answered %s, the confirm %s)' % (
                      rig.field(seen[-1]['text'], 'default') if seen else '?', asked['text'][:40] if asked else 'nothing', confirmed['text'][:40] if confirmed else 'nothing')))
            start = last_answer(c, 'docker_start')
            text = start['text'] if start else ''
            check('fleet', rig.field(text, 'owner') == 'player' and rig.field(text, 'pilot') not in (None, '0'),
                  "player %d's second ship (for the dock order): %s" % (k, text[:170] or 'no row'))
            rows = [x for x in answers(c, 'docked') if 'docked=' in x['text']]
            done = bool(rows) and 'docked=true' in rows[-1]['text']
            check('docked', done, "player %d: %s" % (k, 'the ship docked at tick %d' % rows[-1]['tick'] if done else
                  'the ship did not dock (%s)' % (rows[-1]['text'][:170] if rows else 'no row')))


# ---- crew ----------------------------------------------------------------------------------------------------------
class Crew:
    """Each player fires a crew member of a fleet ship, fires its captain, promotes a crew member, then hires a person at a station."""
    name = 'crew'
    minutes = 9
    marks = ('fired', 'captain', 'hired_look')
    # Two Argon fighters of the sector that are not the snapshot's fixture (ROQ-362 on police duty: a pilot, a marine
    # and a service crew member; RVS-616 on patrol: a pilot and two service crew). The fleet fixture hands them to the
    # player faction at the window's start, on every client.
    SHIPS = {1: '00000000000a56bf', 2: '00000000000a7949'}
    WHARF = '000000000001b5af'          # AVK-777: S docks and people for hire
    # ...and the shipyard NLN-075 for player 2: at the wharf its ship stays off its granted dock while player 1 is
    # docked there (three runs of 17 Nov; the cause is open)
    YARD = '000000000001451a'
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 15000

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Crew.WHARF, help='saved ID of the station player 1 docks at to hire')
        s.add_argument('--station2', default=Crew.YARD, help="saved ID of the station player 2 docks at to hire")
        s.add_argument('--credits', type=int, default=5000000, help='money the fixture adds to the account before the run')
        s.add_argument('--choice', type=int, default=1, help="the conversation's choice that is 'Hire'")
        # Conversations of players 2..N (native_gate_conversation_slots, a session default since 19 Nov): player 2 hires
        # through its own conversation; each player talks when nobody is in one (X4 has one at a time) and ends it after
        # the hire. --no-conversations: the default off, player 2's hire refused as before (--conversations: kept).
        s.add_argument('--conversations', dest='conversations', action='store_true', default=True, help=argparse.SUPPRESS)
        s.add_argument('--no-conversations', dest='conversations', action='store_false',
                       help="conversation owners off (native_gate_conversation_slots=false): player 2's hire is refused, as before 19 Nov")

    @staticmethod
    def more_sets(a):
        return [] if a.conversations else ['native_gate_conversation_slots=false']

    @staticmethod
    def chain(k, a):
        ship, station = Crew.SHIPS[k], (a.station if k == 1 else a.station2)
        # with conversation owners: talk when the world's one conversation is free, check it is this player's, end it
        # (the menus of another player's conversation open on every client for now: what is open is read, then closed
        # as a person closes a menu that came up by itself)
        free = [('free', 'poll', 'owner=0 .*manager=1%.0 ', 30, 6000, 'conversation_look'),
                ('stray', 'op', 'menu_look', 'MapMenu'), ('stray_close', 'op', 'menu_close', 'MapMenu'),
                ('stray_wait', 'wait', 30)] if a.conversations else []
        owned = [('owned', 'try', 'owner=%d ' % k, 5, 150, 'conversation_look')] if a.conversations else []
        bye = [('bye', 'op', 'conv_cancel'), ('bye_wait', 'wait', 60), ('bye_look', 'op', 'conversation_look'),
               # the other player's conversation held back on this screen too (it may come after this player's)
               ('screen_end', 'try', 'dialogs=[1-9]%d*/[1-9]', 30, 3600, 'conversation_look')] if a.conversations else []
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('crew_fire', 'op', 'crew_fire', ship, 'service'),                  # the crew page's "Fire" on a crew member
            ('fired', 'try', 'people=1/', 10, 600, 'ship_look', ship),
            ('captain_fire', 'op', 'captain_fire', ship),                       # "Fire" on the pilot
            ('no_captain', 'try', ' pilot=0 ', 10, 900, 'ship_look', ship),
            ('promote', 'op', 'crew_promote', ship),                            # "Promote best crew member to captain"
            ('captain', 'try', ' pilot=[1-9]', 10, 1800, 'ship_look', ship),
            ('look_promoted', 'op', 'ship_look', ship),
            # hiring: dock, comm a person of the station who can be hired, the 'Hire' choice, the map's hire menu, its
            # Hire button with the fleet ship and the role
            ('dock', 'until', '^docked', 'dock_station', station, 9000),
            ('settle', 'wait', 240),
            ('money_start', 'op', 'account'),
        ] + free + [
            ('talk', 'op', 'hire_talk', station),
        ] + owned + [
            ('talk_wait', 'wait', 90),
            ('choice', 'op', 'dialog', a.choice),
            ('hire_menu', 'try', 'shown=true mode=hire', 5, 300, 'menu_look', 'MapMenu'),
            ('hire', 'op', 'hire_confirm', ship, 'service'),
            ('hired_wait', 'wait', 600),
            ('hired_look', 'op', 'ship_look', ship),
            ('money_hired', 'op', 'account'),
        ] + bye

    @staticmethod
    def watch(a):
        return [('ship_look', Crew.SHIPS[1]), ('ship_look', Crew.SHIPS[2])]

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', a.credits)]
        return [kit + [(60 + 30 * (k - 1), 'chain', Crew.LIMIT, Crew.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def people(text):
        """How many people a ship_look row lists, or None."""
        m = re.search(r'people=(\d+)/', text or '')
        return int(m.group(1)) if m else None

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]

            def shown(name):
                rows = [x for x in answers(c, name) if x['text'] != 'gave_up']
                return rows[-1]['text'][:170] if rows else 'no row'

            def held(name, pattern):
                rows = [x for x in answers(c, name) if x['ok'] and re.search(pattern, x['text'])]
                return rows[-1] if rows else None

            start = last_answer(c, 'look_start')
            text = start['text'] if start else ''
            before, crew = rig.field(text, 'pilot'), Crew.people(text)
            check('fleet', rig.field(text, 'owner') == 'player' and before not in (None, '0') and (crew or 0) >= 2,
                  "player %d's ship (a pilot and two crew wanted): %s" % (k, text[:200] or 'no row'))
            row = held('fired', r'people=1/')
            check('fired', bool(row), 'player %d: %s' % (k, 'the service crew member was fired at tick %d (%s -> %s people)' % (row['tick'], crew, Crew.people(row['text'])) if row else
                  'the crew member stayed (fire answered %s; ship %s)' % (shown('crew_fire'), shown('fired'))))
            row = held('no_captain', r' pilot=0 ')
            check('captain', bool(row), 'player %d: %s' % (k, 'the captain was fired, the ship has no pilot at tick %d' % row['tick'] if row else
                  'the captain stayed (fire answered %s; ship %s)' % (shown('captain_fire'), shown('no_captain'))))
            row = held('captain', r' pilot=[1-9]')
            after = rig.field(row['text'], 'pilot') if row else None
            done = bool(row) and after != before
            check('promoted', done, 'player %d: %s' % (k, 'a crew member is the captain at tick %d (pilot %s, was %s; %s people left)' % (row['tick'], after, before, Crew.people(row['text'])) if done else
                  'nobody new became captain (promote answered %s; ship %s)' % (shown('promote'), shown('captain'))))
            docked = last_answer(c, 'dock', 'docked')
            check('docked', bool(docked), 'player %d at the station: %s' % (k, 'tick %d' % docked['tick'] if docked else shown('dock')))
            was, now = (Crew.people((last_answer(c, x) or {'text': ''})['text']) for x in ('look_promoted', 'hired_look'))
            done = was is not None and now is not None and now == was + 1
            check('hired', done, 'player %d: %s' % (k, 'a person of the station is aboard the ship (%d -> %d people; talked to %s)' % (was, now, shown('talk')) if done else
                  'nobody hired (%s -> %s people): talk %s; choice %s; hire menu %s; Hire %s' % (was, now, shown('talk'), shown('choice'), shown('hire_menu'), shown('hire'))))
            if a.conversations:
                # the owner's screen (lockstep_conversation_owner.h, 19 Nov): each client showed its own conversation's
                # dialog and menu and held back the other player's (the status counters: shown/hidden)
                def full(name):
                    rows = [x for x in answers(c, name) if x['text'] != 'gave_up']
                    return rows[-1]['text'] if rows else ''
                look = full('screen_end') if 'dialogs=' in full('screen_end') else full('bye_look')
                screen = re.search(r'dialogs=(\d+)/(\d+) menus=(\d+)/(\d+)', look)
                if screen:
                    ds, dh, ms, mh = (int(x) for x in screen.groups())
                    check('screen', ds > 0 and dh > 0 and ms > 0 and mh > 0,
                          "player %d's client: dialogs shown %d, held back %d; conversation menus shown %d, held back %d "
                          "(its own shown, the other player's held back)" % (k, ds, dh, ms, mh))
                stray = shown('stray')
                if 'shown=true' in stray:
                    check.note('stray', "player %d's client had a menu of another player's conversation open before talking: %s" % (k, stray[:100]))
                mine = held('owned', r'owner=%d ' % k)
                check('talked', bool(mine), 'player %d: %s' % (k, "the conversation was this player's from tick %d (%s)" % (mine['tick'], mine['text'][:120]) if mine else
                      "the conversation was not this player's (free %s; talk %s; owner %s)" % (shown('free')[:120], shown('talk'), shown('owned')[:120])))
            paid = [last_answer(c, name) for name in ('money_start', 'money_hired')]
            if all(paid):
                check.note('fee', 'player %d: the account read %s before and %s after the hire (one wallet for both players)' % (k, paid[0]['text'], paid[1]['text']))


# ---- ship purchase -------------------------------------------------------------------------------------------------
class ShipBuy:
    """Each player buys a ship at a wharf and waits for it to be built."""
    name = 'shipbuy'
    minutes = 10
    marks = ('ordered', 'built')
    WHARF, MACRO, PRICE = '000000000001b5af', 'ship_arg_s_scout_01_a_macro', 150000
    sets = ()
    LIMIT = 16000

    @staticmethod
    def options(s):
        s.add_argument('--station', default=ShipBuy.WHARF, help='saved ID of the wharf or shipyard')
        s.add_argument('--macro', default=ShipBuy.MACRO, help='the ship to buy')
        s.add_argument('--price', type=int, default=ShipBuy.PRICE, help='credits paid for each ship')
        s.add_argument('--credits', type=int, default=1000000, help='money the fixture adds to the account before the run')

    @staticmethod
    def ship_name(k):
        return 'X4LAN_Bought_%d' % k

    @staticmethod
    def chain(k, a):
        name = ShipBuy.ship_name(k)
        return [
            ('fleet_start', 'op', 'fleet_look'),
            ('money_start', 'op', 'account'),
            ('queue_start', 'op', 'build_look', a.station),
            # the ship configuration's Confirm at the wharf: pay, order the build with the station's own preset
            ('order', 'op', 'build', 'buy', a.station, a.macro, a.price, name),
            ('ordered', 'poll', a.macro, 10, 600, 'build_look', a.station),
            ('order_wait', 'wait', 30),
            ('money_ordered', 'op', 'account'),
            # the ship is the player's from the order on; it is built when the wharf's list no longer holds a task
            ('built', 'try', 'building=0 queued=0', 60, 14000, 'build_look', a.station),
            ('fleet_end', 'op', 'fleet_look'),
            ('ship_end', 'op', 'fleet_look'),
        ]

    @staticmethod
    def watch(a):
        return [('fleet_look',), ('build_look', a.station)]

    @staticmethod
    def plan(a):
        return [[(40, 'wallet', a.credits), (90 + 300 * (k - 1), 'chain', ShipBuy.LIMIT, ShipBuy.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]

            def shown(name):
                rows = answers(c, name)
                return rows[-1]['text'][:200] if rows else 'no row'

            order, queued = last_answer(c, 'order'), last_answer(c, 'ordered')
            token = order['text'].split(' ')[0] if order else ''
            # the replayed purchase of this player: X4's build task ID, the same on both clients at the same tick
            tasks = [[x for x in clients[n]['built'] if x[1] == k and x[2].startswith('AddBuildTask6=')] for n in PLAYERS]
            paid = [[x for x in clients[n]['built'] if x[1] == k and x[2].startswith('TransferPlayerMoneyTo(')] for n in PLAYERS]
            done = bool(tasks[0]) and tasks[0] == tasks[1] and tasks[0][0][2] != 'AddBuildTask6=0' and bool(paid[0]) and paid[0] == paid[1] and bool(queued)
            check('ordered', done, 'player %d: %s' % (k, 'paid and ordered on both clients at tick %d (%s, %s); the wharf: %s' %
                  (tasks[0][0][0], paid[0][0][2], tasks[0][0][2], queued['text'][:90]) if done else
                  'no build task on both clients (the order answered %s; client 1 %s, client 2 %s; the wharf: %s)' % (token or 'nothing', tasks[0][:1], tasks[1][:1], shown('ordered'))))
            paid = [last_answer(c, name) for name in ('money_start', 'money_ordered')]
            if all(paid):
                check.note('price', 'player %d: the account read %s before and %s after its order (one wallet for both players)' % (k, paid[0]['text'], paid[1]['text']))
            rows = [x for x in answers(c, 'built') if 'building=' in x['text']]
            fleet = last_answer(c, 'fleet_end')
            done = bool(rows) and 'building=0 queued=0' in rows[-1]['text'] and bool(fleet) and ShipBuy.ship_name(k) in fleet['text']
            before = rig.field((last_answer(c, 'fleet_start') or {'text': ''})['text'], 'ships')
            check('built', done, 'player %d: %s' % (k, 'the wharf finished at tick %d and the ship %s is in the fleet (%s ships before, %s now)' %
                  (rows[-1]['tick'], ShipBuy.ship_name(k), before, rig.field(fleet['text'], 'ships')) if done else
                  'the ship was not built in time (the wharf: %s; the fleet: %s)' % (rows[-1]['text'][:150] if rows else 'no row', fleet['text'][:150] if fleet else 'no row')))


# ---- refit ---------------------------------------------------------------------------------------------------------
class Refit:
    """Each player docks at a wharf and has its ship refitted with the wharf's high preset."""
    name = 'refit'
    minutes = 10          # --in-turn wants --minutes 11
    # ...and 'stall': the ship standing 300-349 m off its dock with the grant (the wharf's second pad, 19-20 Nov)
    marks = ('ordered', 'refitted', ('stall', 'dock', r'^stage=granted .*dock_gap=3[0-4]\d\b'), ('hop_up', 'dock', r'^hop_point '),
             ('hop_end', 'dock', r'^hop_(end|stop) '), ('quickdock', 'dock', r'^quickdock='), ('settle_end', 'dock', r'^settle_end '))
    WHARF, PRICE = '000000000001b5af', 200000
    sets = ()
    LIMIT = 19000
    # The hop to the wharf's second pad, in that pad's frame (y up from the pad, z towards the station's centre; 'here':
    # the ship's own value): the pads of AVK-777 lie in a row inside the open frame of its build module, with beams over
    # them. The first pad is at the row's end and its approach (runs of 20 Nov) came in from beyond the end of the
    # module, 270 m up, down to 105 m and along the row 65 m over the pads, and its autopilot ended 56 m over the pad,
    # where X4's docking took it in; the points are that path, 40 m short of the first pad, and that last place over the
    # second pad (then the dock key; run 20261120-012919 flew the path to 106 m of the pad, and the approach autopilot
    # from there flew the ship back out along the row).
    HOP_ROUTE = (('here', 500, 'here'), (-640, 500, 150), (-640, 270, 150), (-254, 105, -78), (-79, 65, -48), (-10, 56, -47))

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Refit.WHARF, help='saved ID of the wharf or equipment dock')
        s.add_argument('--price', type=int, default=Refit.PRICE, help='credits paid for each refit')
        s.add_argument('--level', type=float, default=1.0, help="the wharf's preset: 0 minimal .. 1 high")
        s.add_argument('--lead', type=int, default=0, help='this player flies first, the other starts 50 seconds later (0: both at once)')
        s.add_argument('--together', action='store_true', help='both players fly to the wharf at the same time (the default; kept as a word for it)')
        s.add_argument('--in-turn', action='store_true',
                       help='player 2 goes when player 1 has undocked and flown clear. It is sent to the second dock all the same '
                            '(runs of 18 Nov), so this only takes longer; kept for looking at that fault')
        s.add_argument('--credits', type=int, default=1000000, help='money the fixture adds to the account before the run')
        s.add_argument('--no-detour', action='store_true',
                       help="no detour to the dock's own face when the dock autopilot stalls (the second ship then stays "
                            'parked on the near side, as on 17-19 Nov: the police ship hunt reproduces with it; no hop either)')
        s.add_argument('--no-hop', action='store_true',
                       help='no hop over the module when the approach stalls within a kilometre of the dock (the second ship '
                            "then stays 300 m off the wharf's second pad, against the module's wall, as on 19-20 Nov)")

    @staticmethod
    def chain(k, a):
        dock = ('dock', 'until', '^docked', 'dock_station', a.station, 9000) + ((False, False, 0, 0) if a.no_detour else
                                                                                (False, False, True, 0) if a.no_hop else
                                                                                (False, False, True, Refit.HOP_ROUTE))
        # one after the other: player 2 waits until player 1's ship has docked and has left again
        turn = [('first_docked', 'try', 'docked=true', 60, 9000, 'ship_look', 1),
                ('first_away', 'try', 'docked=false', 60, 9000, 'ship_look', 1),
                ('first_gone', 'wait', 900)] if k == 2 and a.in_turn else []
        return turn + [
            dock,
            ('settle', 'wait', 120),
            ('fit_start', 'op', 'fit_look', k),
            ('money_start', 'op', 'account'),
            # the dock menu's "Upgrade / repair ship", the configuration's Confirm: pay, order the refit of the docked ship
            ('order', 'op', 'build', 'refit', a.station, k, a.price, a.level),
            ('ordered', 'try', 'building=[1-9]', 10, 600, 'build_look', a.station),
            # the own ship's order queue: X4 gives it an Equip order, which the wharf's refit start reads
            # (aiscripts/build.shiptrader.xml; tools/dev/native_gate_player_refit.xml); the refit call reaches the wharf
            # a few ticks after the build_look above can already answer (the other player's build)
            ('ship_ordered', 'try', 'Equip', 10, 900, 'ship_look', k),
            ('money_ordered', 'op', 'account'),
            ('refitted', 'try', 'building=0 queued=0', 60, 12000, 'build_look', a.station),
            ('fit_end', 'op', 'fit_look', k),
            ('undock_ready', 'poll', '^granted', 10, 900, 'undock_check'),
            ('undock', 'op', 'undock'),
            ('undocked', 'poll', 'docked=false', 30, 1800, 'foot_where'),
            # ...and away from the pad, so the ship does not hover over it for the rest of the run
            ('thrust_on', 'op', 'input', 'state', ACCELERATE, 1),
            ('thrust', 'wait', 300),
            ('thrust_off', 'op', 'input', 'state', ACCELERATE, 0),
        ]

    @staticmethod
    def watch(a):
        return [('fit_look', 1), ('fit_look', 2), ('build_look', a.station)]

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', a.credits)]
        # the wharf's queued builds and the parts of its start test, every 1,500 ticks once the refits can be ordered
        why = [(t, 'refit_why', a.station) for t in range(4500, Refit.LIMIT, 1500)]
        return [kit + why + [(60 + 30 * (k - 1) + (1500 if a.lead and not a.in_turn and k != a.lead else 0), 'chain', Refit.LIMIT, Refit.chain(k, a))]
                for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]

            def shown(name):
                rows = answers(c, name)
                return rows[-1]['text'][:200] if rows else 'no row'

            docked = last_answer(c, 'dock', 'docked')
            check('docked', bool(docked), 'player %d at the wharf: %s' % (k, 'tick %d' % docked['tick'] if docked else shown('dock')))
            detours = [x['text'][:220] for x in answers(c, 'dock') if x['text'].startswith(('detour ', 'detour_end'))]
            if detours:
                check.note('detour', 'player %d: the dock autopilot stalled; %s' % (k, ' / '.join(detours)))
            hops = [x['text'][:220] for x in answers(c, 'dock') if x['text'].startswith(('hop ', 'hop_point', 'hop_end', 'hop_stop', 'quickdock=',
                                                                                          'settle_end'))]
            if hops:
                check.note('hop', 'player %d: flown by its keys over to the dock; %s' % (k, ' / '.join(hops)))
            order, queued = last_answer(c, 'order'), last_answer(c, 'ordered')
            tasks = [[x for x in clients[n]['built'] if x[1] == k and x[2].startswith('AddBuildTask6=')] for n in PLAYERS]
            done = bool(tasks[0]) and tasks[0] == tasks[1] and tasks[0][0][2] != 'AddBuildTask6=0'
            check('ordered', done, 'player %d: %s' % (k, 'the refit was ordered on both clients at tick %d (%s); the wharf: %s' %
                  (tasks[0][0][0], tasks[0][0][2], queued['text'][:100] if queued else '?') if done else
                  'no refit task on both clients (the order answered %s; client 1 %s, client 2 %s; the wharf: %s)' %
                  (order['text'][:80] if order else 'nothing', tasks[0][:1], tasks[1][:1], shown('ordered'))))
            first, last = last_answer(c, 'fit_start'), last_answer(c, 'fit_end')
            changed = bool(first) and bool(last) and first['text'] != last['text']
            check('refitted', changed, 'player %d: %s' % (k, 'the equipment changed: %s -> %s' % (first['text'][:150], last['text'][:150]) if changed else
                  'the equipment is as before (%s; the wharf: %s)' % (first['text'][:150] if first else 'no row', shown('refitted'))))
            away = last_answer(c, 'undocked')
            away = away if away and 'docked=false' in away['text'] else None
            check('undocked', bool(away), 'player %d: %s' % (k, 'flew off the wharf at tick %d' % away['tick'] if away else 'did not undock after the refit (%s)' % shown('undock_ready')))


class RefitOrder:
    """Each player orders a refit at the wharf for a fleet ship out in space; X4's own pilot flies it in and docks it."""
    name = 'refitorder'
    minutes = 10
    marks = ('equip', 'docked', 'refitted')
    # Two Argon fighters of the sector with their pilots (ROQ-362 and RVS-616), handed to the player faction by the
    # fleet fixture. A ship nobody of the players flies is refitted as a person does it from the map: the ship selected,
    # the wharf's "Upgrade", the configuration's Confirm. X4 gives the ship its 'Equip' order with the build task
    # (order.build.equip: fly there, DockAndWait, ask the station), and the wharf refits it once it is docked.
    SHIPS = {1: '00000000000a56bf', 2: '00000000000a7949'}
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 19000

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Refit.WHARF, help='saved ID of the wharf or equipment dock')
        s.add_argument('--price', type=int, default=Refit.PRICE, help='credits paid for each refit')
        s.add_argument('--level', type=float, default=1.0, help="the wharf's preset: 0 minimal .. 1 high")
        s.add_argument('--credits', type=int, default=1000000, help='money the fixture adds to the account before the run')

    @staticmethod
    def chain(k, a):
        ship = RefitOrder.SHIPS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('fit_start', 'op', 'fit_look', ship),
            ('entry_upgrade', 'op', 'menu_entry', 'InteractMenu', 'buttonUpgrade'),
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
            ('money_start', 'op', 'account'),
            # the ship configuration's Confirm for a ship that is not at the wharf: pay, add the build task for it
            ('order', 'op', 'build', 'refit', a.station, ship, a.price, a.level),
            ('equip', 'try', 'orders=[^ ]*Equip/', 10, 600, 'ship_look', ship),       # X4's Equip order on the ship
            ('queued', 'try', '=[1-9]:', 10, 600, 'build_look', a.station),
            ('money_ordered', 'op', 'account'),
            ('docked', 'poll', 'docked=true', 60, 10000, 'ship_look', ship),
            ('refitted', 'try', 'building=0 queued=0', 60, 12000, 'build_look', a.station),
            ('fit_end', 'op', 'fit_look', ship),
            # the Equip order ends with the build; X4 undocks the ship for what comes next (orders.base)
            ('undocked', 'try', 'docked=false', 30, 1800, 'ship_look', ship),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', RefitOrder.SHIPS[1]), ('ship_look', RefitOrder.SHIPS[2]), ('fit_look', RefitOrder.SHIPS[1]),
                ('fit_look', RefitOrder.SHIPS[2]), ('build_look', a.station)]

    @staticmethod
    def plan(a):
        kit = [(40, 'wallet', a.credits)]
        return [kit + [(120 + 30 * (k - 1), 'chain', RefitOrder.LIMIT, RefitOrder.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            text = fleet_check(check, c, k, 'look_start', 'ship')
            check('away', rig.field(text, 'docked') == 'false', "player %d's ship is out in space at the start (%s)" % (k, rig.field(text, 'at')))
            entry_checks(check, c, k, (('upgrade', 'InteractMenu', 'buttonUpgrade'),))
            order = last_answer(c, 'order')
            tasks = [[x for x in clients[n]['built'] if x[1] == k and x[2].startswith('AddBuildTask6=')] for n in PLAYERS]
            done = bool(tasks[0]) and tasks[0] == tasks[1] and tasks[0][0][2] != 'AddBuildTask6=0'
            check('ordered', done, 'player %d: %s' % (k, 'the refit was ordered on both clients at tick %d (%s); the wharf: %s' %
                  (tasks[0][0][0], tasks[0][0][2], shown(c, 'queued', 100)) if done else
                  'no refit task on both clients (the order answered %s; client 1 %s, client 2 %s)' %
                  (order['text'][:80] if order else 'nothing', tasks[0][:1], tasks[1][:1])))
            row = held(c, 'equip', r'orders=[^ ]*Equip/')
            check('equip', bool(row), 'player %d: %s' % (k, "X4 gave the ship its Equip order at tick %d (%s)" % (row['tick'], rig.field(row['text'], 'orders')) if row else
                  'no Equip order on the ship (%s)' % rig.field(shown(c, 'equip', 600), 'orders')))
            docked = held(c, 'docked', r'docked=true')
            check('docked', bool(docked), 'player %d: %s' % (k, "the ship's pilot docked it at the wharf at tick %d (pad %s)" % (docked['tick'], rig.field(docked['text'], 'bay')) if docked else
                  'the ship did not dock (%s)' % shown(c, 'docked')))
            first, last = last_answer(c, 'fit_start'), last_answer(c, 'fit_end')
            changed = bool(first) and bool(last) and first['text'] != last['text']
            check('refitted', changed, 'player %d: %s' % (k, 'the equipment changed: %s -> %s' % (first['text'][:150], last['text'][:150]) if changed else
                  'the equipment is as before (%s; the wharf: %s)' % (first['text'][:150] if first else 'no row', shown(c, 'refitted'))))
            away = held(c, 'undocked', r'docked=false')
            check.note('undocked', 'player %d: %s' % (k, 'X4 undocked the ship at tick %d after the refit' % away['tick'] if away else
                       'the ship is still docked (%s)' % shown(c, 'undocked', 120)))
            paid = [last_answer(c, name) for name in ('money_start', 'money_ordered')]
            if all(paid):
                check.note('price', 'player %d: the account read %s before and %s after its order (one wallet for both players)' % (k, paid[0]['text'], paid[1]['text']))


# ---- a menu open on one client -------------------------------------------------------------------------------------
class MapOpen:
    """One player opens the map and leaves it open; nobody does anything else (does an open menu change the simulation?)."""
    name = 'mapopen'
    minutes = 3
    marks = ('open_look',)
    sets = ('native_gate_world_hash_stride=300', 'native_gate_world_pages_window={300,5400}')
    LIMIT = 5000

    @staticmethod
    def options(s):
        s.add_argument('--who', type=int, default=2, help='the player whose client opens the menu')
        s.add_argument('--menu', default='MapMenu')
        s.add_argument('--ticks', type=int, default=3000, help='how long it stays open')

    @staticmethod
    def chain(k, a):
        if k != a.who:
            return [('idle', 'wait', 600 + a.ticks + 300)]
        return [
            ('before', 'wait', 600),
            ('open', 'op', 'menu_open', a.menu),
            ('open_wait', 'wait', 60),
            ('open_look', 'op', 'menu_look', a.menu),
            ('held', 'wait', a.ticks),
            ('held_look', 'op', 'menu_look', a.menu),
            ('close', 'op', 'menu_close', a.menu),
            ('after', 'wait', 240),
            ('closed_look', 'op', 'menu_look', a.menu),
        ]

    @staticmethod
    def watch(a):
        return []

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', MapOpen.LIMIT, MapOpen.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        c = clients[a.who]
        shown, held = last_answer(c, 'open_look'), last_answer(c, 'held_look')
        check('open', bool(shown) and 'shown=true' in shown['text'], "player %d's %s: %s" % (a.who, a.menu, shown['text'] if shown else 'no row'))
        check('held', bool(held) and 'shown=true' in held['text'], "still open %d ticks later: %s" % (a.ticks, held['text'] if held else 'no row'))


# ---- helpers of the fleet scenarios --------------------------------------------------------------------------------
def shown(client, name, width=170):
    """The last answer of a step that is not a poll's 'gave_up', cut for a report line."""
    rows = [x for x in answers(client, name) if x['text'] != 'gave_up']
    return rows[-1]['text'][:width] if rows else 'no row'


def held(client, name, pattern):
    """The last answer of a step that matches `pattern`, or None."""
    rows = [x for x in answers(client, name) if x['ok'] and re.search(pattern, x['text'])]
    return rows[-1] if rows else None


def place(text):
    """The x/y/z of a ship_look row, or None."""
    m = re.search(r' at=(-?\d+)/(-?\d+)/(-?\d+)', text or '')
    return tuple(int(x) for x in m.groups()) if m else None


def entry_checks(check, client, k, entries):
    """Is each stock menu entry a scripted action stands for offered to a person in the session?"""
    for name, menu, function in entries:
        entry = last_answer(client, 'entry_' + name)
        opened = bool(entry) and entry['text'].startswith('open')
        check('offered', opened, 'player %d: %s (%s.%s) is %s' % (k, name, menu, function, 'open to a person on this client' if opened else
              'REFUSED to a person in this session (%s)' % (entry['text'] if entry else 'no row')))


def fleet_check(check, client, k, name, what, people=0):
    row = last_answer(client, name)
    text = row['text'] if row else ''
    crew = Crew.people(text)
    check('fleet', rig.field(text, 'owner') == 'player' and rig.field(text, 'pilot') not in (None, '0') and (crew or 0) >= people,
          "player %d's %s: %s" % (k, what, text[:200] or 'no row'))
    return text


# ---- wings ---------------------------------------------------------------------------------------------------------
class Wing:
    """Each player makes one fleet ship the subordinate of another, flies the wing to a place, orders an attack, and removes the assignment."""
    name = 'wing'
    minutes = 8
    marks = ('assigned', 'followed', 'hit')
    # Four of the six Argon fighters beside the players (AAO-226 and NLI-119 lead, UDW-502 and XHK-693 follow) and two
    # of the six Xenon fighters that face them. The snapshot's fight stays off: the players' orders start it.
    LEADERS = {1: '0000000000176a13', 2: '0000000000176a74'}
    SUBS = {1: '0000000000176ad5', 2: '0000000000176b36'}
    TARGETS = {1: '0000000000176c59', 2: '0000000000176c77'}
    SECTOR = '0000000000000729'
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (LEADERS[1], LEADERS[2], SUBS[1], SUBS[2]), 'native_gate_battle_engage=false')
    LIMIT = 13000
    ENTRIES = (('assign', 'InteractMenu', 'buttonAssignCommander'), ('attack', 'InteractMenu', 'orderAttack'), ('remove', 'InteractMenu', 'buttonRemoveAssignment'),
               ('remove_map', 'MapMenu', 'buttonRemoveAssignment'))

    @staticmethod
    def options(s):
        s.add_argument('--metres', type=int, default=8000, help='how far the wing is sent')
        s.add_argument('--assignment', default='defence', help="the subordinate's assignment (defence, attack, interception, ...)")
        s.add_argument('--fight', type=int, default=2400, help='ticks the test waits for the attacked ship to be destroyed')

    @staticmethod
    def chain(k, a):
        leader, sub, target = Wing.LEADERS[k], Wing.SUBS[k], Wing.TARGETS[k]
        return [
            ('look_leader', 'op', 'ship_look', leader),
            ('look_sub', 'op', 'ship_look', sub),
        ] + [('entry_' + name, 'op', 'menu_entry', menu, function) for name, menu, function in Wing.ENTRIES] + [
            ('clear_leader', 'op', 'clear', leader),
            ('clear_sub', 'op', 'clear', sub),
            ('settle', 'wait', 30),
            ('assign', 'op', 'assign', sub, leader, a.assignment, 1),          # "Assign as subordinate"
            ('assigned', 'try', 'commander=[1-9]', 10, 600, 'ship_look', sub),
            ('leader_has', 'op', 'ship_look', leader),
            ('move', 'op', 'move_by', leader, Wing.SECTOR, 0, a.metres if k == 1 else -a.metres, 0),      # the leader flies; the wing follows
            ('arrived', 'try', '^near=true', 30, 3000, 'ship_arrived', leader, 2000),
            ('follow_wait', 'wait', 300),
            ('followed', 'op', 'ship_look', sub),
            ('followed_leader', 'op', 'ship_look', leader),
            ('target_start', 'op', 'ship_look', target),
            ('attack', 'op', 'attack', leader, target),                         # "Attack" on an enemy ship
            ('attack_queued', 'try', 'Attack/', 10, 300, 'ship_look', leader),
            ('hit', 'try', '^state=[dg]', 30, a.fight, 'ship_look', target),      # until it is destroyed (or the wait is over)
            ('leader_after', 'op', 'ship_look', leader),
            ('unassign', 'op', 'unassign', sub),                                # "Remove assignment"
            ('unassigned', 'try', 'commander=0 ', 10, 300, 'ship_look', sub),
            ('look_end', 'op', 'ship_look', sub),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', ship) for group in (Wing.LEADERS, Wing.SUBS, Wing.TARGETS) for ship in (group[1], group[2])]

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', Wing.LIMIT, Wing.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_leader', 'wing leader')
            fleet_check(check, c, k, 'look_sub', 'subordinate')
            entry_checks(check, c, k, Wing.ENTRIES)
            row, has = held(c, 'assigned', r'commander=[1-9]'), last_answer(c, 'leader_has')
            check('assigned', bool(row), 'player %d: %s' % (k, 'the ship has a commander at tick %d (commander %s; the leader lists %s subordinates)' %
                  (row['tick'], rig.field(row['text'], 'commander'), rig.field(has['text'], 'subs') if has else '?') if row else
                  'no commander (the assignment answered %s; the ship: %s)' % (shown(c, 'assign'), shown(c, 'assigned'))))
            there = held(c, 'arrived', r'^near=true')
            check('moved', bool(there), 'player %d: %s' % (k, 'the leader reached the place at tick %d' % there['tick'] if there else
                  'the leader never got there (%s; %s)' % (shown(c, 'move', 90), shown(c, 'arrived'))))
            spots = [place((last_answer(c, name) or {'text': ''})['text']) for name in ('followed', 'followed_leader')]
            apart = round(sum((x - y) ** 2 for x, y in zip(*spots)) ** 0.5) if all(spots) else None
            check('followed', apart is not None and apart <= 5000, 'player %d: %s' % (k, 'no positions' if apart is None else
                  'the subordinate is %d m from its leader ten seconds after the leader arrived' % apart))
            queued = held(c, 'attack_queued', r'Attack/')
            check('attack', bool(queued), 'player %d: %s' % (k, 'Attack in the queue of the leader at tick %d (%s)' % (queued['tick'], rig.field(queued['text'], 'orders')) if queued else
                  'never in the queue (the order answered %s; %s)' % (shown(c, 'attack', 60), shown(c, 'attack_queued'))))
            start, rows = last_answer(c, 'target_start'), [x for x in answers(c, 'hit') if x['text'] != 'gave_up']
            dead = held(c, 'hit', r'^state=[dg]')
            hurt = [x for x in rows if rig.field(x['text'], 'hull') not in (None, '100') or rig.field(x['text'], 'shield') not in (None, '100')]
            check('hit', bool(dead or hurt), 'player %d: %s' % (k, 'the attacked ship was destroyed by tick %d' % dead['tick'] if dead else
                  'the attacked ship was hit (hull %s, shield %s at tick %d; not destroyed in %d ticks)' %
                  (rig.field(hurt[-1]['text'], 'hull'), rig.field(hurt[-1]['text'], 'shield'), hurt[-1]['tick'], a.fight) if hurt else
                  'the attacked ship was never hit (at the start: %s; at the end: %s)' % (start['text'][:120] if start else 'no row', rows[-1]['text'][:160] if rows else 'no row')))
            gone = held(c, 'unassigned', r'commander=0 ')
            check('removed', bool(gone), 'player %d: %s' % (k, 'the subordinate has no commander at tick %d' % gone['tick'] if gone else
                  'the assignment stayed (the removal answered %s; the ship: %s)' % (shown(c, 'unassign'), shown(c, 'unassigned'))))


# ---- a ship's sale -------------------------------------------------------------------------------------------------
class ShipSell:
    """Each player sends a fleet ship to the wharf and sells it there."""
    name = 'shipsell'
    minutes = 9
    marks = ('docked', 'sold')
    # Two Argon police fighters of the sector with their pilots (HSG-795 and YME-361), handed to the player faction by
    # the fleet fixture; the wharf AVK-777 builds S and M ships, so it takes them.
    SHIPS = {1: '00000000000a5722', 2: '00000000000a57a0'}
    WHARF = '000000000001b5af'
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 15000
    ENTRIES = (('dock', 'InteractMenu', 'orderDock'), ('sell', 'InteractMenu', 'buttonSellShips'), ('confirm', 'MapMenu', 'buttonSellShips'))

    @staticmethod
    def options(s):
        s.add_argument('--station', default=ShipSell.WHARF, help='saved ID of the wharf or shipyard that buys the ships')

    @staticmethod
    def chain(k, a):
        ship = ShipSell.SHIPS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('fleet_start', 'op', 'fleet_look'),
        ] + [('entry_' + name, 'op', 'menu_entry', menu, function) for name, menu, function in ShipSell.ENTRIES] + [
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
            ('dock', 'op', 'dock', ship, a.station),
            ('docked', 'poll', 'docked=true', 60, 10000, 'ship_look', ship),
            ('docked_wait', 'wait', 120),
            ('value', 'op', 'ship_value', ship, a.station),
            ('money_start', 'op', 'account'),
            ('sell', 'op', 'build', 'sell', a.station, ship),                   # the map's "Sell ships": Confirm
            ('sold', 'try', '^mine=false', 10, 600, 'ship_owned', ship),
            ('paid_wait', 'wait', 120),
            ('money_end', 'op', 'account'),
            ('fleet_end', 'op', 'fleet_look'),
        ]

    @staticmethod
    def watch(a):
        return [('fleet_look',), ('ship_owned', ShipSell.SHIPS[1]), ('ship_owned', ShipSell.SHIPS[2]), ('account',)]

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', ShipSell.LIMIT, ShipSell.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_start', 'ship')
            entry_checks(check, c, k, ShipSell.ENTRIES)
            docked = held(c, 'docked', r'docked=true')
            check('docked', bool(docked), 'player %d: %s' % (k, 'the ship docked at the wharf at tick %d' % docked['tick'] if docked else 'the ship did not dock (%s)' % shown(c, 'docked')))
            value = last_answer(c, 'value')
            text = value['text'] if value else ''
            price = rig.field(text, 'price')
            check('offer', rig.field(text, 'takes') == 'true' and rig.field(text, 'sellable') == 'true', 'player %d: the wharf on the ship: %s' % (k, text or 'no row'))
            sold = held(c, 'sold', r'^mine=false')
            # the replayed sale of this player on both clients: the same calls at the same tick
            calls = [[x for x in clients[n]['built'] if x[1] == k and (x[2].startswith('SellPlayerShip') or x[2].startswith('TransferMoneyToPlayer'))] for n in PLAYERS]
            done = bool(sold) and len(calls[0]) == 2 and calls[0] == calls[1]
            check('sold', done, 'player %d: %s' % (k, 'paid and sold on both clients at tick %d (%s); the ship is no longer the player\'s at tick %d (%s)' %
                  (calls[0][0][0], ', '.join(x[2] for x in calls[0]), sold['tick'], sold['text']) if done else
                  'not sold (the sale answered %s; the ship: %s; replayed on client 1 %s, on client 2 %s)' % (shown(c, 'sell'), shown(c, 'sold'), calls[0][:1], calls[1][:1])))
            before, after = (rig.field((last_answer(c, name) or {'text': ''})['text'], 'ships') for name in ('fleet_start', 'fleet_end'))
            paid = [last_answer(c, name) for name in ('money_start', 'money_end')]
            if all(paid):
                check.note('price', 'player %d: the wharf offered %s Cr; the account read %s before and %s after (one wallet for both players); %s ships before, %s after' %
                           (k, price, paid[0]['text'], paid[1]['text'], before, after))
        # the account at the end against the start: both sales' prices arrived (the watch compares it between the clients)
        rows = sorted(clients[1]['ops'].get('account', []))
        prices = [rig.field((last_answer(clients[k], 'value') or {'text': ''})['text'], 'price') for k in PLAYERS]
        sold = [bool(held(clients[k], 'sold', r'^mine=false')) for k in PLAYERS]
        if rows and all(prices):
            first, last = float(rows[0][2]), float(rows[-1][2])
            due = sum(float(p) for p, s in zip(prices, sold) if s)
            check('paid', abs(last - first - due) < 1.5, 'the account went from %.0f to %.0f Cr: %+.0f, the ships sold were offered %.0f' % (first, last, last - first, due))


# ---- crew from ship to ship ----------------------------------------------------------------------------------------
class CrewMove:
    """Each player transfers a crew member from one fleet ship to another."""
    name = 'crewmove'
    minutes = 6
    marks = ('left', 'arrived')
    # Argon fighters of the sector with their pilots: ROQ-362 (a marine and a service crew member) gives to COO-093,
    # RVS-616 (two service crew) gives to LAG-170. The fleet fixture hands all four to the player faction.
    FROM = {1: '00000000000a56bf', 2: '00000000000a7949'}
    TO = {1: '00000000000a7784', 2: '00000000000a7984'}
    ABOARD = {1: 2, 2: 3}          # people aboard each target ship once the crew member has arrived
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (FROM[1], FROM[2], TO[1], TO[2]),)
    LIMIT = 9000

    @staticmethod
    def options(s):
        s.add_argument('--role', default='service', help='the role of the crew member that moves')

    @staticmethod
    def chain(k, a):
        source, target = CrewMove.FROM[k], CrewMove.TO[k]
        return [
            ('look_from', 'op', 'ship_look', source),
            ('look_to', 'op', 'ship_look', target),
            ('ask', 'op', 'crew_move', source, target, a.role, True),          # the menu's own question before its Confirm is offered
            ('move', 'op', 'crew_move', source, target, a.role),               # the crew transfer's Confirm
            ('left', 'try', 'people=%d/' % 1, 10, 900, 'ship_look', source),
            # the crew list shows the person on the other ship at once; aboard it is when X4's own count says so
            ('arrived', 'try', 'aboard=%d' % CrewMove.ABOARD[k], 30, 6000, 'ship_look', target),
            ('end_from', 'op', 'ship_look', source),
            ('end_to', 'op', 'ship_look', target),
        ]

    @staticmethod
    def apart(first, second):
        spots = [place(first), place(second)]
        return round(sum((x - y) ** 2 for x, y in zip(*spots)) ** 0.5) if all(spots) else '?'

    @staticmethod
    def watch(a):
        return [('ship_look', ship) for group in (CrewMove.FROM, CrewMove.TO) for ship in (group[1], group[2])]

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', CrewMove.LIMIT, CrewMove.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            first = fleet_check(check, c, k, 'look_from', 'ship that gives (two crew wanted)', 2)
            second = fleet_check(check, c, k, 'look_to', 'ship that takes')
            had, got = Crew.people(first), Crew.people(second)
            asked = last_answer(c, 'ask')
            check('allowed', bool(asked) and 'reason=-' in asked['text'], "player %d: X4's answer to the menu's question: %s" % (k, asked['text'] if asked else 'no row'))
            # the replayed transfer of this player on both clients
            calls = [[x for x in clients[n]['built'] if x[1] == k and x[2].startswith('PerformCrewExchange2')] for n in PLAYERS]
            moved = last_answer(c, 'move')
            seed = (re.search(r'moved=(\d+)', moved['text']) or [None, None])[1] if moved else None
            left = last_answer(c, 'end_from')
            gone = bool(left) and bool(seed) and Crew.people(left['text']) == (had or 0) - 1 and seed not in left['text']
            check('left', gone and bool(calls[0]) and calls[0] == calls[1], 'player %d: %s' % (k, 'the crew member %s left its ship (%s -> %s people), replayed on both clients at tick %d (%s)' %
                  (seed, had, Crew.people(left['text']), calls[0][0][0], calls[0][0][2]) if gone and calls[0] and calls[0] == calls[1] else
                  'the crew member stayed (the transfer answered %s; the ship: %s; replayed on client 1 %s, on client 2 %s)' % (shown(c, 'move'), shown(c, 'end_from'), calls[0][:1], calls[1][:1])))
            there = last_answer(c, 'end_to')
            listed = bool(there) and bool(seed) and Crew.people(there['text']) == (got or 0) + 1 and seed in there['text']
            before, after = rig.field(second, 'aboard'), rig.field(there['text'], 'aboard') if there else None
            came = listed and before is not None and after is not None and int(after) == int(before) + 1
            row = held(c, 'arrived', r'aboard=%s\b' % after) if came else None
            check('arrived', came, 'player %d: %s' % (k, 'the crew member is aboard the other ship (%s -> %s aboard, by tick %s; the ships are %s m apart)' %
                  (before, after, row['tick'] if row else '?', CrewMove.apart(first, second)) if came else
                  'the crew member is %s (aboard %s -> %s; %s)' % ('listed for the other ship, not aboard it' if listed else 'not on the other ship', before, after, shown(c, 'end_to', 260))))


# ---- a fleet ship trades -------------------------------------------------------------------------------------------
class FleetTrade:
    """Each player has a fleet freighter buy a ware at one station and sell it at another (trade orders)."""
    name = 'fleettrade'
    minutes = 12
    marks = ('bought', 'sold')
    # Two Argon couriers of the sector with their pilots (JHA-419 and XGJ-466, on trade routines of their own until
    # the fleet fixture hands them to the player faction).
    SHIPS = {1: '00000000000a1885', 2: '00000000000a16fe'}
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 20000

    @staticmethod
    def options(s):
        s.add_argument('--buy-station', default=Trade.BUY, help='saved ID of the station that sells the ware')
        s.add_argument('--sell-station', default=Trade.SELL, help='saved ID of the station that buys it')
        s.add_argument('--ware', default=Trade.WARE)
        s.add_argument('--amount', type=int, default=Trade.AMOUNT)
        s.add_argument('--credits', type=int, default=Trade.CREDITS, help='money the fixture adds to the account before the run')

    @staticmethod
    def chain(k, a):
        ship = FleetTrade.SHIPS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('cargo_start', 'op', 'cargo', a.ware, ship),
            ('entry_trade', 'op', 'menu_entry', 'MapMenu', 'buttonConfirmTrade'),
            # the courier's own trade routine ends first: Wait as its default behaviour (the behaviour page's Confirm)
            ('behaviour', 'op', 'behaviour', ship, 'Wait'),
            ('behaviour_wait', 'wait', 30),
            ('behaviour_confirm', 'op', 'behaviour_confirm', ship),
            ('behaviour_set', 'try', 'default=Wait', 10, 300, 'ship_look', ship),
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
            ('offer_buy', 'op', 'trade_look', a.buy_station, a.ware),
            ('buy', 'op', 'trade_buy', a.buy_station, a.ware, a.amount, ship),          # the trade menu's Confirm for a ship that is elsewhere
            ('buy_queued', 'try', 'TradePerform/', 10, 300, 'ship_look', ship),
            ('bought', 'poll', '^amount=%d ' % a.amount, 60, 9000, 'cargo', a.ware, ship),
            # A station's offers are known to the player while one of its ships is near it (a person sees no offer to
            # click before that either: run n2, 18 Nov): the freighter flies to the buyer, then gets the order.
            ('offer_far', 'op', 'trade_look', a.sell_station, a.ware),
            ('go_sell', 'op', 'dock', ship, a.sell_station),                          # "Dock at" the buying station
            ('offer_sell', 'poll', 'buy=', 60, 9000, 'trade_look', a.sell_station, a.ware),
            ('clear_sell', 'op', 'clear', ship),
            ('settle_sell', 'wait', 30),
            ('sell', 'op', 'trade_sell', a.sell_station, a.ware, a.amount, ship),
            ('sell_queued', 'try', 'TradePerform/', 10, 300, 'ship_look', ship),
            ('sold', 'poll', '^amount=0 ', 60, 9000, 'cargo', a.ware, ship),
            ('settle_end', 'wait', 360),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('cargo', a.ware, FleetTrade.SHIPS[1]), ('cargo', a.ware, FleetTrade.SHIPS[2]), ('ship_look', FleetTrade.SHIPS[1]), ('ship_look', FleetTrade.SHIPS[2])]

    @staticmethod
    def plan(a):
        return [[(40, 'wallet', a.credits), (60 + 30 * (k - 1), 'chain', FleetTrade.LIMIT, FleetTrade.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_start', 'freighter')
            start = last_answer(c, 'cargo_start')
            check('fleet', bool(start) and start['text'].startswith('amount=0 '), "player %d's freighter holds none of the ware at the start: %s" % (k, start['text'][:150] if start else 'no row'))
            entry_checks(check, c, k, (('trade', 'MapMenu', 'buttonConfirmTrade'),))
            for name, word, pattern in (('bought', 'buy', r'^amount=%d ' % a.amount), ('sold', 'sell', r'^amount=0 ')):
                queued, done = held(c, word + '_queued', r'TradePerform/'), held(c, name, pattern)
                check(name, bool(done), 'player %d: %s' % (k, 'the freighter holds %s of %s at tick %d (the trade order was in its queue at tick %s)' %
                      (rig.field(done['text'], 'amount'), a.ware, done['tick'], queued['tick'] if queued else '?') if done else
                      'the freighter did not %s (the order answered %s; the station: %s; its queue: %s; its hold: %s)' %
                      (word, shown(c, word, 40), shown(c, 'offer_' + word, 80), rig.field(shown(c, word + '_queued', 400), 'orders'), shown(c, name, 80))))
        rows = sorted(clients[1]['ops'].get('cargo', []))
        if rows:
            check.note('money', 'the account read %s at tick %d and %s at tick %d' % (rig.field(rows[0][2], 'money'), rows[0][0], rig.field(rows[-1][2], 'money'), rows[-1][0]))


# ---- a ship's own actions ------------------------------------------------------------------------------------------
class Deploy:
    """Each player renames its ship, launches a satellite, a nav beacon, a resource probe, a mine and a laser tower, and drops cargo."""
    name = 'deploy'
    minutes = 5
    marks = ('named', 'launched_lasertower', 'dropped', 'destroyed')
    MINE, SATELLITE = 'weapon_gen_mine_01_macro', 'eq_arg_satellite_01_macro'
    KINDS = (('satellite', 'eq_arg_satellite_01_macro', 'Satellites'), ('navbeacon', 'env_deco_nav_beacon_t1_macro', 'NavBeacons'),
             ('probe', 'eq_arg_resourceprobe_01_macro', 'ResourceProbes'), ('mine', 'weapon_gen_mine_01_macro', 'Mines'),
             ('lasertower', 'ship_gen_xs_lasertower_01_a_macro', 'LaserTowers'))
    WARE, HELD, DROPPED = 'energycells', 50, 20
    sets = ()
    LIMIT = 6000

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def ship_name(k):
        return 'X4LAN_Named_%d' % k

    @staticmethod
    def chain(k, a):
        steps = [
            ('look_start', 'op', 'ship_look', k),
            ('deploy_start', 'op', 'deploy_look', k),
            ('name', 'op', 'ship_name', k, Deploy.ship_name(k)),                 # the map's rename
            ('named', 'try', 'name=' + Deploy.ship_name(k), 10, 300, 'ship_look', k),
        ]
        for kind, macro, word in Deploy.KINDS:                                   # the ship menu's "Deploy"
            steps += [('held_' + kind, 'op', 'deploy_count', word),               # how many the ship holds before
                      ('launch_' + kind, 'op', 'launch', kind, macro),
                      ('launched_' + kind, 'try', 'launched=1', 5, 300, 'deploy_count', word),
                      ('mark_' + kind, 'op', 'deploy_mark', macro)]            # the one just launched is this player's
        return steps + [
            ('cargo_start', 'op', 'cargo', Deploy.WARE),
            ('drop', 'op', 'drop_cargo', Deploy.WARE, Deploy.DROPPED),           # the map's "Drop" on a ware of the hold
            ('dropped', 'try', '^amount=%d ' % (Deploy.HELD - Deploy.DROPPED), 10, 300, 'cargo', Deploy.WARE),
            # the interact menu on a deployable of the player's: the satellite off and on again, then destroyed (a
            # mine reads "not active" and "Activate" leaves it so: run p4 of 19 Nov; the entry is the satellite's)
            ('mine_look', 'op', 'deployable_look', Deploy.MINE),
            ('sat_start', 'op', 'deployable_look', Deploy.SATELLITE),
            ('sat_off', 'op', 'deployable_active', Deploy.SATELLITE, False),
            ('sat_isoff', 'try', 'active=false', 10, 300, 'deployable_look', Deploy.SATELLITE),
            ('sat_on', 'op', 'deployable_active', Deploy.SATELLITE, True),
            ('sat_ison', 'try', 'active=true', 10, 300, 'deployable_look', Deploy.SATELLITE),
            ('destroy', 'op', 'deployable_destroy', Deploy.SATELLITE),
            ('destroyed', 'try', 'state=gone', 10, 600, 'deployable_look', Deploy.SATELLITE),
            ('settle', 'wait', 300),
            ('deploy_end', 'op', 'deploy_look', k),
        ]

    @staticmethod
    def watch(a):
        return [('deploy_look', 1), ('deploy_look', 2), ('ship_look', 1), ('ship_look', 2), ('cargo', Deploy.WARE, 1), ('cargo', Deploy.WARE, 2)]

    @staticmethod
    def plan(a):
        kit = [(30 + i, 'launch_stock', k, macro, 1) for k in PLAYERS for i, (kind, macro, word) in enumerate(Deploy.KINDS)]
        kit += [(40, 'hold_stock', k, Deploy.WARE, Deploy.HELD) for k in PLAYERS]
        return [kit + [(120 + 30 * (k - 1), 'chain', Deploy.LIMIT, Deploy.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            start = last_answer(c, 'deploy_start')
            stocked = bool(start) and all(re.search(word.lower() + r':[1-9]', start['text']) for kind, macro, word in Deploy.KINDS)
            check('fixture', stocked, "player %d's ship holds every kind of deployable at the start: %s" % (k, start['text'][:200] if start else 'no row'))
            row = held(c, 'named', 'name=' + Deploy.ship_name(k))
            check('named', bool(row), 'player %d: %s' % (k, 'its ship is named %s at tick %d' % (Deploy.ship_name(k), row['tick']) if row else
                  'the name stayed (the rename answered %s; the ship: %s)' % (shown(c, 'name'), rig.field(shown(c, 'named', 600), 'name'))))
            for kind, macro, word in Deploy.KINDS:
                row = held(c, 'launched_' + kind, 'launched=1')
                check('launched', bool(row), 'player %d: %s' % (k, 'the %s left the ship by tick %d (%s left aboard)' % (kind, row['tick'], rig.field(row['text'], 'left')) if row else
                      'the %s was not launched (the launch answered %s; %s)' % (kind, shown(c, 'launch_' + kind, 40), shown(c, 'launched_' + kind))))
            row = held(c, 'dropped', r'^amount=%d ' % (Deploy.HELD - Deploy.DROPPED))
            check('dropped', bool(row), 'player %d: %s' % (k, '%d %s dropped, %d left in the hold at tick %d' % (Deploy.DROPPED, Deploy.WARE, Deploy.HELD - Deploy.DROPPED, row['tick']) if row else
                  'nothing dropped (the drop answered %s; before: %s; after: %s)' % (shown(c, 'drop', 40), shown(c, 'cargo_start', 60), shown(c, 'dropped', 60))))
            off, on = held(c, 'sat_isoff', r'active=false'), held(c, 'sat_ison', r'active=true')
            check('switched', bool(off and on) and shown(c, 'sat_off', 20) == 'ffi' and shown(c, 'sat_on', 20) == 'ffi', 'player %d: %s' % (k,
                  'its satellite went off at tick %d and on again at tick %d (it was %s)' % (off['tick'], on['tick'], shown(c, 'sat_start', 40).strip()) if off and on else
                  'its satellite did not switch (at the start %s; off: %s, %s; on: %s, %s)' %
                  (shown(c, 'sat_start', 40), shown(c, 'sat_off', 20), shown(c, 'sat_isoff', 40), shown(c, 'sat_on', 20), shown(c, 'sat_ison', 40))))
            row = held(c, 'destroyed', r'state=gone')
            check('destroyed', bool(row), 'player %d: %s' % (k, 'its satellite destroyed itself by tick %d' % row['tick'] if row else
                  'its satellite stayed (the call answered %s; %s)' % (shown(c, 'destroy', 20), shown(c, 'destroyed', 40))))
        # what is out in space at the end, as both clients list it (the watch compares the lists between the clients)
        rows = sorted(clients[1]['ops'].get('deploy_look', []))
        if rows:
            text = rows[-1][2]
            out = int(rig.field(text, 'deployed') or 0)
            check('in space', out >= 2 * len(Deploy.KINDS) - 4, "the player faction's deployables at tick %d: %s" % (rows[-1][0], text.split(' holds=')[0][:260]))


# ---- more orders ---------------------------------------------------------------------------------------------------
class Orders2:
    """Each player gives a fleet ship more of the interact menu's orders: follow, protect a station, collect drops, pick up a satellite, attack in range, withdraw."""
    name = 'orders2'
    minutes = 10
    marks = ('followed', 'collected', 'picked', 'withdraw_queued')
    # The same fighters as the wing scenario's (two each of the six beside the players) and its Xenon targets.
    FIRST, SECOND, TARGETS, SECTOR = Wing.LEADERS, Wing.SUBS, Wing.TARGETS, Wing.SECTOR
    STATION = '0000000000040496'          # MMJ-623
    XENON = ('0000000000176c59', '0000000000176c77', '0000000000176c95', '0000000000176cb3', '0000000000176cd1', '0000000000176cef')
    SATELLITE, WARE, HELD, DROPPED = 'eq_arg_satellite_01_macro', 'energycells', 50, 20
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (FIRST[1], FIRST[2], SECOND[1], SECOND[2]), 'native_gate_battle_engage=false')
    LIMIT = 16000
    ENTRIES = (('follow', 'InteractMenu', 'orderFollow'), ('protect', 'InteractMenu', 'orderProtect'), ('collect', 'InteractMenu', 'orderCollectRadius'),
               ('pickup', 'InteractMenu', 'orderCollectDeployable'), ('attack_range', 'InteractMenu', 'orderAttackInRange'), ('withdraw', 'MapMenu', 'orderWithdrawFromCombat'))

    @staticmethod
    def options(s):
        s.add_argument('--metres', type=int, default=6000, help='how far the followed ship is sent')
        s.add_argument('--fight', type=int, default=1500, help='ticks the test waits for the attack in range to hit a ship')

    @staticmethod
    def chain(k, a):
        first, second, target, sector = Orders2.FIRST[k], Orders2.SECOND[k], Orders2.TARGETS[k], Orders2.SECTOR
        side = 1 if k == 1 else -1
        return [
            ('look_first', 'op', 'ship_look', first),
            ('look_second', 'op', 'ship_look', second),
        ] + [('entry_' + name, 'op', 'menu_entry', menu, function) for name, menu, function in Orders2.ENTRIES] + [
            ('clear_first', 'op', 'clear', first),
            ('clear_second', 'op', 'clear', second),
            ('settle', 'wait', 30),
            # "Follow": the second ship follows the first, which is sent away
            ('follow', 'op', 'order', second, 'Follow', (('target', ('id', first)),)),
            ('follow_queued', 'try', 'Follow/', 10, 300, 'ship_look', second),
            ('move', 'op', 'move_by', first, sector, 0, side * a.metres, 0),
            ('arrived', 'try', '^near=true', 30, 3000, 'ship_arrived', first, 2000),
            ('followed', 'try', '^gap=1?%d?%d?%d$', 30, 1800, 'ship_gap', second, first),          # within 2 km of it
            # "Protect station" for the first ship
            ('protect', 'op', 'order', first, 'ProtectStation', (('station', ('id', Orders2.STATION)),), True),
            ('protect_queued', 'try', 'ProtectStation/', 10, 300, 'ship_look', first),
            # the player drops cargo; "Collect drops in radius" around the player's ship for the second ship
            ('cargo_start', 'op', 'cargo', Orders2.WARE, second),
            ('drop', 'op', 'drop_cargo', Orders2.WARE, Orders2.DROPPED),
            ('dropped', 'try', '^amount=%d ' % (Orders2.HELD - Orders2.DROPPED), 10, 300, 'cargo', Orders2.WARE),
            ('collect', 'op', 'order', second, 'CollectDropsInRadius', (('destination', ('at', sector, k)),), True),
            ('collect_queued', 'try', 'CollectDropsInRadius/', 10, 300, 'ship_look', second),
            ('collected', 'try', '^amount=[1-9]', 30, 3600, 'cargo', Orders2.WARE, second),
            # the player launches a satellite; "Collect" on it for the second ship
            ('held_start', 'op', 'deploy_look', second),
            ('launch', 'op', 'launch', 'satellite', Orders2.SATELLITE),
            ('launched', 'try', 'launched=1', 5, 300, 'deploy_count', 'Satellites'),
            ('launch_wait', 'wait', 120 + 300 * (k - 1)),          # both players' satellites are out before either is picked
            ('pickup', 'op', 'order', second, 'CollectDeployables', (('targetlist', ('list', ('deployed', Orders2.SATELLITE, k))),), True),
            ('pickup_queued', 'try', 'CollectDeployables/', 10, 300, 'ship_look', second),
            ('picked', 'try', 'holds=satellites:[1-9]', 30, 3600, 'deploy_look', second),
            # "Attack targets in range" at the place of a Xenon fighter, then "Withdraw from combat"
            ('attack', 'op', 'order', second, 'AttackInRange', (('destination', ('at', sector, target)),), True),
            ('attack_queued', 'try', 'AttackInRange/', 10, 300, 'ship_look', second),
            ('hit', 'try', '^state=[dg]', 30, a.fight, 'ship_look', target),          # the fight goes on for a while
            ('withdraw', 'op', 'order', second, 'Flee', (('method', 'boost'), ('donotdrop', True), ('deploydistraction', True)), True),
            ('withdraw_queued', 'try', 'Flee/', 5, 300, 'ship_look', second),
            ('look_end', 'op', 'ship_look', second),
        ]

    @staticmethod
    def watch(a):
        ships = [ship for group in (Orders2.FIRST, Orders2.SECOND) for ship in (group[1], group[2])] + list(Orders2.XENON)
        return [('ship_look', ship) for ship in ships] + [('cargo', Orders2.WARE, Orders2.SECOND[1]), ('cargo', Orders2.WARE, Orders2.SECOND[2]),
                                                          ('deploy_look', Orders2.SECOND[1]), ('deploy_look', Orders2.SECOND[2])]

    @staticmethod
    def plan(a):
        kit = [(30, 'launch_stock', k, Orders2.SATELLITE, 1) for k in PLAYERS] + [(40, 'hold_stock', k, Orders2.WARE, Orders2.HELD) for k in PLAYERS]
        return [kit + [(120 + 30 * (k - 1), 'chain', Orders2.LIMIT, Orders2.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_first', 'first ship')
            fleet_check(check, c, k, 'look_second', 'second ship')
            entry_checks(check, c, k, Orders2.ENTRIES)

            def queued(name, order, step):
                # the order as both clients replayed it for this player (a short order is done before a reading of
                # the queue catches it: the satellite 30 m away was aboard within ten ticks, run n7), and the queue
                made = [[x for x in clients[n]['applied'] if x[1] == k and x[2] == 'order=' + order] for n in PLAYERS]
                row = held(c, step + '_queued', order + '/')
                done = bool(made[0]) and made[0] == made[1]
                check('ordered', done, 'player %d: %s %s' % (k, order, 'given on both clients at tick %d%s' % (made[0][0][0], '; in the queue of the ship at tick %d (%s)' %
                      (row['tick'], rig.field(row['text'], 'orders')) if row else '; done before a reading of the queue saw it') if done else
                      'not given on both clients (the order answered %s; client 1 %s, client 2 %s; the queue: %s)' %
                      (shown(c, step, 60), made[0][:1], made[1][:1], rig.field(shown(c, step + '_queued', 400), 'orders'))))
                return row

            queued('follow', 'Follow', 'follow')
            there, near = held(c, 'arrived', r'^near=true'), held(c, 'followed', r'^gap=1?\d?\d?\d$')
            check('followed', bool(there) and bool(near), 'player %d: %s' % (k, 'the followed ship never arrived (%s)' % shown(c, 'arrived') if not there else
                  'the second ship is %s m from the first at tick %d (that one arrived %d m away at tick %d)' % (rig.field(near['text'], 'gap'), near['tick'], a.metres, there['tick']) if near else
                  'the second ship stayed away from the first: %s' % shown(c, 'followed')))
            queued('protect', 'ProtectStation', 'protect')
            queued('collect', 'CollectDropsInRadius', 'collect')
            before, row = last_answer(c, 'cargo_start'), held(c, 'collected', r'^amount=[1-9]')
            check('collected', bool(row), 'player %d: %s' % (k, 'the second ship holds %s %s at tick %d (%s before; the player dropped %d)' %
                  (rig.field(row['text'], 'amount'), Orders2.WARE, row['tick'], rig.field(before['text'], 'amount') if before else '?', Orders2.DROPPED) if row else
                  'the second ship collected nothing (the drop: %s; its hold: %s)' % (shown(c, 'dropped', 50), shown(c, 'collected', 80))))
            queued('pickup', 'CollectDeployables', 'pickup')
            row = held(c, 'picked', r'holds=satellites:[1-9]')
            check('picked', bool(row), 'player %d: %s' % (k, 'the second ship has the satellite aboard at tick %d' % row['tick'] if row else
                  'the satellite was not picked up (the launch: %s; the order answered %s; %s)' % (shown(c, 'launched', 50), shown(c, 'pickup', 60), shown(c, 'picked', 200))))
            queued('attack', 'AttackInRange', 'attack')
            queued('withdraw', 'Flee', 'withdraw')
        # The attack in range takes whatever enemy is near the place, not one ship: the six Xenon fighters as both
        # clients read them during the run (the watch compares every reading between the clients). A shield is full
        # again some time after the ships withdrew, so every reading counts, not the last one.
        seen, dead, hurt = set(), {}, {}
        for tick, ok, text in sorted(clients[1]['ops'].get('ship_look', [])):
            ship = rig.field(text, 'ship')
            if not ok or not ship:
                continue
            if rig.field(text, 'owner') == 'xenon':
                seen.add(ship)
                if rig.field(text, 'hull') != '100' or rig.field(text, 'shield') != '100':
                    hurt.setdefault(ship, (tick, rig.field(text, 'hull'), rig.field(text, 'shield')))
            elif text.startswith('state=dead') and ship in seen:
                dead.setdefault(ship, tick)
        first = min(list(hurt.values()) + [(tick, '0', '0') for tick in dead.values()] or [(0, '-', '-')])
        check('fight', bool(dead or hurt), 'of the %d Xenon fighters read, %d were destroyed and %d damaged%s' %
              (len(seen), len(dead), len(set(hurt) - set(dead)), '; the first damage read at tick %d (hull %s, shield %s)' % first if dead or hurt else ''))


# ---- a repair ------------------------------------------------------------------------------------------------------
class Repair:
    """Each player sends a damaged fleet ship to the wharf and has it repaired there (--own: its own ship, docked by X4's AI)."""
    name = 'repair'
    minutes = 10
    marks = ('docked', 'repaired')
    # Two Argon fighters of the sector with their pilots (ROQ-362 and RVS-616), handed to the player faction by the
    # fleet fixture and damaged by the hull fixture.
    SHIPS = {1: '00000000000a56bf', 2: '00000000000a7949'}
    WHARF = '000000000001b5af'
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 16000

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Repair.WHARF, help='saved ID of the wharf, shipyard or equipment dock')
        s.add_argument('--hull', type=int, default=50, help='the hull percentage the fixture leaves the ships with')
        s.add_argument('--credits', type=int, default=500000, help='money the fixture adds to the account before the run')
        s.add_argument('--own', action='store_true',
                       help="each player's own ship instead: damaged by the fixture, docked at the wharf by X4's own AI (ai_dock_station), "
                            "repaired through the dock menu's path (the station's signal repairs_initiate), then undocked")

    @staticmethod
    def chain(k, a):
        if a.own:
            return [
                ('look_start', 'op', 'ship_look', k),
                ('dock', 'until', '^docked', 'ai_dock_station', a.station, 9000),
                ('docked_wait', 'wait', 120),
                ('money_start', 'op', 'account'),
                ('repair', 'op', 'build', 'repair', a.station, k),          # the ship configuration's Confirm with the repair
                ('repaired', 'try', 'hull=100 ', 30, 5400, 'ship_look', k),
                ('money_end', 'op', 'account'),
                ('look_end', 'op', 'ship_look', k),
                ('undock_ready', 'poll', '^granted', 10, 900, 'undock_check'),
                ('undock', 'op', 'undock'),
                ('undocked', 'poll', 'docked=false', 30, 1800, 'foot_where'),
            ]
        ship = Repair.SHIPS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('entry_dock', 'op', 'menu_entry', 'InteractMenu', 'orderDock'),
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
            ('dock', 'op', 'dock', ship, a.station),
            ('docked', 'poll', 'docked=true', 60, 10000, 'ship_look', ship),
            ('docked_wait', 'wait', 120),
            ('money_start', 'op', 'account'),
            ('repair', 'op', 'build', 'repair', a.station, ship),          # the ship configuration's Confirm with the repair
            ('repaired', 'try', 'hull=100 ', 30, 5400, 'ship_look', ship),
            ('money_end', 'op', 'account'),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', Repair.SHIPS[1]), ('ship_look', Repair.SHIPS[2]), ('account',)]

    @staticmethod
    def plan(a):
        if a.own:   # the players' own ships: X4's docking computer and no NPC pilot (as refit), then the damage
            kit = [(30, 'dock_kit', k) for k in PLAYERS] + [(40, 'wallet', a.credits)] + [(50, 'hull_set', k, a.hull) for k in PLAYERS]
        else:
            kit = [(40, 'wallet', a.credits)] + [(50, 'hull_set', Repair.SHIPS[k], a.hull) for k in PLAYERS]
        return [kit + [(120 + 30 * (k - 1), 'chain', Repair.LIMIT, Repair.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            if a.own:
                start = last_answer(c, 'look_start')
                text = start['text'] if start else ''
                docked = last_answer(c, 'dock', 'docked')
            else:
                text = fleet_check(check, c, k, 'look_start', 'ship')
                entry_checks(check, c, k, (('dock', 'InteractMenu', 'orderDock'),))
                docked = held(c, 'docked', r'docked=true')
            check('fixture', rig.field(text, 'hull') == str(a.hull), "player %d's ship is damaged at the start: hull %s %%" % (k, rig.field(text, 'hull')))
            check('docked', bool(docked), 'player %d: %s' % (k, 'the ship docked at the wharf at tick %d' % docked['tick'] if docked else 'the ship did not dock (%s)' %
                  shown(c, 'dock' if a.own else 'docked')))
            # the replayed repair of this player on both clients: the price paid, then the station's signal
            calls = [[x for x in clients[n]['built'] if x[1] == k and (x[2].startswith('TransferPlayerMoneyTo') or x[2].startswith('SignalObject'))] for n in PLAYERS]
            done = len(calls[0]) >= 2 and calls[0] == calls[1]
            check('ordered', done, 'player %d: %s' % (k, 'paid and ordered on both clients at tick %d (%s; the order answered %s)' %
                  (calls[0][0][0], ', '.join(x[2] for x in calls[0]), shown(c, 'repair', 40)) if done else
                  'no repair on both clients (the order answered %s; client 1 %s, client 2 %s)' % (shown(c, 'repair', 60), calls[0][:2], calls[1][:2])))
            row = held(c, 'repaired', r'hull=100 ')
            check('repaired', bool(row), 'player %d: %s' % (k, 'the hull is at 100 %% at tick %d (was %s %%)' % (row['tick'], rig.field(text, 'hull')) if row else
                  'the hull stayed damaged: hull %s %% (%s)' % (rig.field(shown(c, 'repaired', 600), 'hull'), shown(c, 'repaired', 120))))
            paid = [last_answer(c, name) for name in ('money_start', 'money_end')]
            if all(paid):
                check.note('price', 'player %d: the account read %s before and %s after (one wallet for both players)' % (k, paid[0]['text'], paid[1]['text']))
            if a.own:
                away = last_answer(c, 'undocked')
                away = away if away and 'docked=false' in away['text'] else None
                check('undocked', bool(away), 'player %d: %s' % (k, 'flew off the wharf at tick %d' % away['tick'] if away else 'did not undock after the repair (%s)' % shown(c, 'undock_ready')))


# ---- the faction's rules -------------------------------------------------------------------------------------------
class Rules:
    """Each player makes a blacklist, a fight rule and a trade rule for the faction, sets two of them for its own ship, renames one and removes one."""
    name = 'rules'
    minutes = 4
    marks = ('traderule_made', 'removed')
    sets = ()
    LIMIT = 5000
    KINDS = (('blacklist', 'blacklists'), ('fightrule', 'fightrules'), ('traderule', 'traderules'))

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def names(k):
        return {'blacklist': 'X4LAN_Blacklist_%d' % k, 'fightrule': 'X4LAN_Fightrule_%d' % k, 'traderule': 'X4LAN_Traderule_%d' % k, 'renamed': 'X4LAN_Renamed_%d' % k}

    @staticmethod
    def chain(k, a):
        n = Rules.names(k)
        return [
            ('rules_start', 'op', 'rules_look'),
            # the player information menu's global orders: a new blacklist (sector travel), then this ship uses it
            ('blacklist', 'op', 'fac_blacklist', n['blacklist'], 'sectortravel'),
            ('blacklist_made', 'try', 'has=true', 10, 300, 'rules_has', n['blacklist']),
            ('blacklist_ship', 'op', 'fac_blacklist_ship', 'sectortravel'),
            # a new fight rule for one faction, then this ship uses it
            ('fightrule', 'op', 'fac_fightrule', n['fightrule'], 'xenon'),
            ('fightrule_made', 'try', 'has=true', 10, 300, 'rules_has', n['fightrule']),
            ('fightrule_ship', 'op', 'fac_fightrule_ship', 'attack'),
            # a new trade rule: a whitelist of one faction
            ('traderule', 'op', 'fac_traderule', n['traderule'], ('argon',), True),
            ('traderule_made', 'try', 'has=true', 10, 300, 'rules_has', n['traderule']),
            ('rules_made', 'op', 'rules_look'),
            # the blacklist gets another name; the trade rule is removed
            ('rename', 'op', 'fac_blacklist_name', n['renamed'], 'sectortravel'),
            ('renamed', 'try', 'has=true', 10, 300, 'rules_has', n['renamed']),
            ('remove', 'op', 'fac_traderule_remove'),
            ('removed', 'try', 'has=false', 10, 300, 'rules_has', n['traderule']),
            ('rules_end', 'op', 'rules_look'),
        ]

    @staticmethod
    def watch(a):
        return [('rules_look',)]

    @staticmethod
    def plan(a):
        return [[(120 + 300 * (k - 1), 'chain', Rules.LIMIT, Rules.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c, n = clients[k], Rules.names(k)
            for kind, word in Rules.KINDS:
                row = held(c, kind + '_made', r'has=true')
                check('made', bool(row) and 'kind=' + word in row['text'], 'player %d: %s' % (k, 'the %s %s is listed at tick %d (%s)' % (kind, n[kind], row['tick'], row['text']) if row else
                      'no %s %s (the menu\'s call answered %s; listed: %s)' % (kind, n[kind], shown(c, kind, 40), shown(c, 'rules_made', 200))))
            row = held(c, 'renamed', r'has=true')
            end = last_answer(c, 'rules_end')
            gone = bool(end) and n['blacklist'] not in end['text']
            check('renamed', bool(row) and gone, 'player %d: %s' % (k, 'the blacklist is called %s at tick %d' % (n['renamed'], row['tick']) if row and gone else
                  'the blacklist kept its name (%s; listed: %s)' % (shown(c, 'rename', 40), end['text'][:200] if end else 'no row')))
            row = held(c, 'removed', r'has=false')
            check('removed', bool(row), 'player %d: %s' % (k, 'the trade rule %s is gone at tick %d' % (n['traderule'], row['tick']) if row else
                  'the trade rule stayed (%s; %s)' % (shown(c, 'remove', 40), shown(c, 'removed', 80))))
        # the list at the end, as both clients read it: both players' blacklists and fight rules, no trade rule of theirs
        rows = sorted(clients[1]['ops'].get('rules_look', []))
        if rows:
            text = rows[-1][2]
            want = [Rules.names(k)[x] for k in PLAYERS for x in ('renamed', 'fightrule')]
            check('listed', all(x in text for x in want) and 'X4LAN_Traderule' not in text, 'the faction\'s rules at tick %d: %s' % (rows[-1][0], text[:300]))


# ---- a wing's settings ---------------------------------------------------------------------------------------------
class WingSet:
    """Each player forms a wing, sets its group's switches and role, names the fleet and moves the subordinate to another group."""
    name = 'wingset'
    minutes = 5
    marks = ('assigned', 'named')
    LEADERS, SUBS = Wing.LEADERS, Wing.SUBS
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (LEADERS[1], LEADERS[2], SUBS[1], SUBS[2]), 'native_gate_battle_engage=false')
    LIMIT = 7000
    SWITCHES = (('sight', 'attack on sight'), ('dock', 'dock at the commander'), ('reinforce', 'reinforce the fleet'),
                ('distress', 'respond to distress calls'), ('resupply', 'resupply at the fleet'))
    ENTRIES = (('assign', 'InteractMenu', 'buttonAssignCommander'), ('change', 'InteractMenu', 'buttonChangeAssignment'))

    @staticmethod
    def options(s):
        s.add_argument('--role', default='attack', help="the group's new role (defence at first)")

    @staticmethod
    def fleet(k):
        return 'X4LAN_Fleet_%d' % k

    @staticmethod
    def chain(k, a):
        leader, sub = WingSet.LEADERS[k], WingSet.SUBS[k]
        steps = [
            ('look_leader', 'op', 'ship_look', leader),
            ('look_sub', 'op', 'ship_look', sub),
        ] + [('entry_' + name, 'op', 'menu_entry', menu, function) for name, menu, function in WingSet.ENTRIES] + [
            ('clear_leader', 'op', 'clear', leader),
            ('clear_sub', 'op', 'clear', sub),
            ('settle', 'wait', 30),
            ('assign', 'op', 'assign', sub, leader, 'defence', 1),              # "Assign as subordinate", group 1
            ('assigned', 'try', 'commander=[1-9]', 10, 600, 'ship_look', sub),
            ('wing_start', 'op', 'wing_look', leader, 1),
        ]
        for what, words in WingSet.SWITCHES:                                    # each switch off, then on
            steps += [(what + '_off', 'op', 'wing_set', leader, 1, what, False),
                      (what + '_isoff', 'try', what + '=false', 10, 300, 'wing_look', leader, 1),
                      (what + '_on', 'op', 'wing_set', leader, 1, what, True),
                      (what + '_ison', 'try', what + '=true', 10, 300, 'wing_look', leader, 1)]
        return steps + [
            # the group's role, as the map's dropdown changes it: the ship's order, then the group's own setting
            ('role_ship', 'op', 'assign', sub, leader, a.role, 1),
            ('role', 'op', 'wing_set', leader, 1, 'role', a.role),
            ('role_done', 'try', 'role=' + a.role + ' ', 10, 300, 'wing_look', leader, 1),
            ('name', 'op', 'fleet_name', leader, WingSet.fleet(k)),             # the map's rename of the fleet
            ('named', 'try', 'fleet=' + WingSet.fleet(k), 10, 300, 'wing_look', leader, 1),
            ('regroup', 'op', 'assign', sub, leader, a.role, 2),                # "Change assignment": the same role in group 2
            ('regroup_wait', 'wait', 120),
            ('group2', 'op', 'wing_look', leader, 2),
            ('wing_end', 'op', 'wing_look', leader, 1),
            ('look_end', 'op', 'ship_look', sub),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', ship) for group in (WingSet.LEADERS, WingSet.SUBS) for ship in (group[1], group[2])] + \
               [('wing_look', WingSet.LEADERS[k], g) for k in PLAYERS for g in (1, 2)]

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', WingSet.LIMIT, WingSet.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_leader', 'wing leader')
            fleet_check(check, c, k, 'look_sub', 'subordinate')
            entry_checks(check, c, k, WingSet.ENTRIES)
            row = held(c, 'assigned', r'commander=[1-9]')
            check('assigned', bool(row), 'player %d: %s' % (k, 'the ship has a commander at tick %d; its group: %s' % (row['tick'], shown(c, 'wing_start', 140)) if row else
                  'no commander (the assignment answered %s; the ship: %s)' % (shown(c, 'assign'), shown(c, 'assigned'))))
            for what, words in WingSet.SWITCHES:
                off, on = held(c, what + '_isoff', what + '=false'), held(c, what + '_ison', what + '=true')
                check('switch', bool(off and on), 'player %d: "%s" %s' % (k, words, 'went off at tick %d and on at tick %d' % (off['tick'], on['tick']) if off and on else
                      'did not change (off: %s, %s; on: %s, %s)' % (shown(c, what + '_off', 30), rig.field(shown(c, what + '_isoff', 300), what),
                                                                   shown(c, what + '_on', 30), rig.field(shown(c, what + '_ison', 300), what))))
            row = held(c, 'role_done', 'role=' + a.role + ' ')
            check('role', bool(row), 'player %d: %s' % (k, "the group's role is %s at tick %d" % (a.role, row['tick']) if row else
                  'the role stayed %s (the call answered %s)' % (rig.field(shown(c, 'role_done', 300), 'role'), shown(c, 'role', 40))))
            row = held(c, 'named', 'fleet=' + WingSet.fleet(k))
            check('named', bool(row), 'player %d: %s' % (k, 'the fleet is called %s at tick %d' % (WingSet.fleet(k), row['tick']) if row else
                  'the fleet kept its name %s (the call answered %s)' % (rig.field(shown(c, 'named', 300), 'fleet'), shown(c, 'name', 40))))
            end = last_answer(c, 'look_end')
            check('grouped', bool(end) and rig.field(end['text'], 'commander') not in (None, '0'), 'player %d: after the move to group 2 the ship still has its commander (%s); group 2: %s' %
                  (k, rig.field(end['text'], 'commander') if end else 'no row', shown(c, 'group2', 140)))
        # the calls of both players on both clients, as the faction family applied them
        for n in PLAYERS:
            refused = {key: count for key, count in clients[n]['refused'].items() if 'Subordinate' in key or 'FleetName' in key}
            check('shared', not refused, "client %d: %s" % (n, 'none of the wing calls was refused' if not refused else 'refused: %s' % refused))


# ---- automatic trading and mining ----------------------------------------------------------------------------------
class Routines:
    """Each player gives a fleet courier automatic trading and a fleet miner automatic mining as their default behaviour (the map's behaviour page)."""
    name = 'routines'
    minutes = 8
    marks = ('trade_set', 'mine_set', 'traded')
    TRADERS, MINERS = FleetTrade.SHIPS, {1: '000000000008822a', 2: '0000000000095f9a'}
    SECTOR = '0000000000000729'
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (TRADERS[1], TRADERS[2], MINERS[1], MINERS[2]),)
    LIMIT = 24000
    KINDS = (('trade', 'TradeRoutine', TRADERS), ('mine', 'MiningRoutine', MINERS))

    @staticmethod
    def options(s):
        s.add_argument('--ware', default=Trade.WARE, help='the ware the courier trades')
        s.add_argument('--resource', default='ore', help='the ware the miner mines')
        s.add_argument('--credits', type=int, default=Trade.CREDITS, help='money the fixture adds to the account before the run')

    @staticmethod
    def chain(k, a):
        steps = [('entry_behaviour', 'op', 'menu_entry', 'MapMenu', 'buttonDefaultOrderConfirm')]
        for word, routine, ships in Routines.KINDS:
            ship, ware = ships[k], a.ware if word == 'trade' else a.resource
            steps += [
                (word + '_look', 'op', 'ship_look', ship),
                # the ship's own routine ends first: Wait as its default behaviour, and an empty queue
                (word + '_wait', 'op', 'behaviour', ship, 'Wait'),
                (word + '_wait_pause', 'wait', 30),
                (word + '_wait_confirm', 'op', 'behaviour_confirm', ship),
                (word + '_waiting', 'try', 'default=Wait', 10, 300, 'ship_look', ship),
                (word + '_clear', 'op', 'clear', ship),
                (word + '_pause', 'wait', 60),
                (word + '_hold', 'op', 'cargo_gain', ship),
                # the behaviour page: the routine, one ware for its list, this sector as its range, Confirm
                (word + '_new', 'op', 'behaviour', ship, routine),
                (word + '_ware', 'op', 'behaviour_param', ship, 1, ware),
                (word + '_range', 'op', 'behaviour_param', ship, 2, ('id', Routines.SECTOR)),
            ] + ([
                # this sector yields nothing to mine (run p2 of 19 Nov): the miner may gather and sell two gates away
                (word + '_reach', 'op', 'behaviour_param', ship, 4, 2),
                (word + '_sell', 'op', 'behaviour_param', ship, 6, 2),
                (word + '_place', 'op', 'ship_moved', ship, 5000),
            ] if word == 'mine' else []) + [
                (word + '_new_pause', 'wait', 30),
                (word + '_confirm', 'op', 'behaviour_confirm', ship),
                (word + '_set', 'try', 'default=' + routine, 10, 300, 'ship_look', ship),
            ]
        # both run on their own from here: the courier gets a trade order, the miner sets off for its resources
        return steps + [
            ('account_start', 'op', 'account'),
            ('traded', 'try', 'TradePerform/', 30, 5400, 'ship_look', Routines.TRADERS[k]),
            ('mine_off', 'try', 'moved=true', 60, 1800, 'ship_moved', Routines.MINERS[k], 5000),
            ('settle', 'wait', 120),
            ('mined', 'op', 'cargo_gain', Routines.MINERS[k]),
            ('trade_end', 'op', 'ship_look', Routines.TRADERS[k]),
            ('mine_end', 'op', 'ship_look', Routines.MINERS[k]),
            ('account_end', 'op', 'account'),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', ships[k]) for word, routine, ships in Routines.KINDS for k in PLAYERS] + [('account',)]

    @staticmethod
    def plan(a):
        return [[(40, 'wallet', a.credits), (120 + 30 * (k - 1), 'chain', Routines.LIMIT, Routines.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            entry_checks(check, c, k, (('behaviour', 'MapMenu', 'buttonDefaultOrderConfirm'),))
            for word, routine, ships in Routines.KINDS:
                what = 'courier' if word == 'trade' else 'miner'
                fleet_check(check, c, k, word + '_look', what)
                row = held(c, word + '_set', 'default=' + routine)
                check('behaviour', bool(row), 'player %d: %s' % (k, "the %s's default behaviour is %s at tick %d" % (what, routine, row['tick']) if row else
                      'the %s did not get %s (new %s, ware %s, range %s, confirm %s; the ship: default %s)' %
                      (what, routine, shown(c, word + '_new', 20), shown(c, word + '_ware', 20), shown(c, word + '_range', 20), shown(c, word + '_confirm', 20),
                       rig.field(shown(c, word + '_set', 600), 'default'))))
            # what the routines then did is the game's: one buyer and one seller of the ware are in the sector, and the
            # sector yields nothing to mine (runs p3 of 19 Nov: one courier took the deal, the miners stayed)
            row = held(c, 'traded', r'TradePerform/')
            check.note('traded', 'player %d: %s' % (k, 'the courier took a trade of its own at tick %d (queue %s)' % (row['tick'], rig.field(row['text'], 'orders')) if row else
                       'the courier found no trade in three minutes (its queue at the end: %s)' % rig.field(shown(c, 'trade_end', 600), 'orders')))
            row = held(c, 'mine_off', r'moved=true')
            check.note('mining', 'player %d: %s' % (k, 'the miner set off on its own by tick %d (%s; its hold: %s)' % (row['tick'], row['text'].strip(), shown(c, 'mined', 40)) if row else
                       'the miner stayed where it was for a minute (%s)' % shown(c, 'mine_off', 60).strip()))
            paid = [last_answer(c, name) for name in ('account_start', 'account_end')]
            if all(paid):
                check.note('money', 'player %d: the account read %s when the routines began and %s at the end (one wallet for both players)' % (k, paid[0]['text'], paid[1]['text']))
        took = [k for k in PLAYERS if held(clients[k], 'traded', r'TradePerform/')]
        check('traded', bool(took), 'a courier on automatic trading took a trade of its own: %s' % (', '.join("player %d's" % k for k in took) or 'none in three minutes'))


# ---- a ship's systems ----------------------------------------------------------------------------------------------
class Systems:
    """Each player sets its own ship's weapon groups, flight assist and travel mode, and a fleet warship's turrets and drones."""
    name = 'systems'
    minutes = 5
    marks = ('travel_on', 'part_done')
    # An Argon frigate and an Argon destroyer of the sector with their pilots (ILV-889 and EDR-066, on patrols of their
    # own until the fleet fixture hands them to the player faction): ships with turrets, which the players' own
    # scouts lack. The frigate has four single turrets and defence drones (run p1, 19 Nov).
    SHIPS = {1: '00000000000a214c', 2: '000000000006f1e7'}
    # other ships whose settings the last reading lists (what another frigate and a carrier hold)
    SURVEY = ('000000000008f987', '0000000000074c58')
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 7000
    DRONES = 'defence'

    @staticmethod
    def options(s):
        s.add_argument('--turrets', default='attackfighters', help='the mode all turrets of the warship get')
        s.add_argument('--group', default='missiledefence', help='the mode its first turret or turret group gets after that')

    @staticmethod
    def chain(k, a):
        ship = Systems.SHIPS[k]
        steps = [
            ('own_start', 'op', 'systems_look', k),
            ('pilot_start', 'op', 'pilot_look'),
            # the docked menu's weapon configuration: weapon 1 joins secondary group 2, which becomes the active one
            ('wg', 'op', 'weapon_group', k, 1, False, 2, True),
            ('wg_done', 'try', r'weapons=%d+:[^, ]*s2', 10, 300, 'systems_look', k),
            ('active', 'op', 'wgroup', 0, 2),
            ('active_done', 'try', r'active=%d+/2 ', 10, 300, 'systems_look', k),
            # flight assist off and on again, travel mode on and off (the keys and the docked menu's mode list)
            ('assist', 'op', 'flight_assist'),
            ('assist_off', 'try', 'assist=false', 10, 300, 'pilot_look'),
            ('assist_back', 'op', 'flight_assist'),
            ('assist_on', 'try', 'assist=true', 10, 300, 'pilot_look'),
            ('travel', 'op', 'travel'),
            ('travel_on', 'try', 'activity=travel', 10, 600, 'pilot_look'),
            ('travel_wait', 'wait', 240),
            ('travel_stop', 'op', 'travel_stop'),
            ('travel_off', 'try', 'activity=none', 10, 600, 'pilot_look'),
            # the fleet warship, as the map's loadout page sets it
            ('look_start', 'op', 'ship_look', ship),
            ('fleet_start', 'op', 'systems_look', ship),
            ('turrets', 'op', 'turrets_mode', a.turrets, ship),
            ('turrets_done', 'try', 'allmode=%s ' % a.turrets, 10, 300, 'systems_look', ship),
            ('part', 'op', 'part_mode', ship, a.group),                 # one turret (or turret group) gets another mode
            ('part_done', 'try', 'first=' + a.group + '/', 10, 300, 'systems_look', ship),
            ('partoff', 'op', 'part_armed', ship, False),               # ...and is disarmed
            ('partoff_done', 'try', 'first=' + a.group + '/false', 10, 300, 'systems_look', ship),
            ('disarm', 'op', 'turrets_armed', False, ship),
            ('disarm_done', 'try', 'allarmed=false ', 10, 300, 'systems_look', ship),
            ('arm', 'op', 'turrets_armed', True, ship),
            ('arm_done', 'try', 'allarmed=true ', 10, 300, 'systems_look', ship),
            ('drone', 'op', 'drone_set', ship, Systems.DRONES, 2),
            ('drone_done', 'try', Systems.DRONES + '/[^/]+#2/', 10, 150, 'systems_look', ship),
            ('dronearm', 'op', 'drone_armed', ship, Systems.DRONES, False),
            ('dronearm_done', 'try', Systems.DRONES + '/[^/]+/false/', 10, 150, 'systems_look', ship),
            ('settle', 'wait', 120),
            ('own_end', 'op', 'systems_look', k),
            ('fleet_end', 'op', 'systems_look', ship),
        ]
        if k == 1:
            steps += [('survey_%d' % i, 'op', 'systems_look', other) for i, other in enumerate(Systems.SURVEY)]
        return steps

    @staticmethod
    def watch(a):
        return [('systems_look', 1), ('systems_look', 2), ('systems_look', Systems.SHIPS[1]), ('systems_look', Systems.SHIPS[2])]

    @staticmethod
    def plan(a):
        return [[(120 + 30 * (k - 1), 'chain', Systems.LIMIT, Systems.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            start = last_answer(c, 'own_start')
            text = start['text'] if start else ''
            check('fixture', (rig.field(text, 'weapons') or '0')[0] != '0', "player %d's own ship has weapons to group: %s" % (k, text[:200] or 'no row'))
            for name, step, pattern, did in (
                    ('grouped', 'wg_done', r'weapons=\d+:[^, ]*s2', 'weapon 1 is in secondary group 2'),
                    ('active', 'active_done', r'active=\d+/2 ', 'secondary group 2 is the active one'),
                    ('assist', 'assist_off', r'assist=false', 'flight assist is off'),
                    ('assist', 'assist_on', r'assist=true', 'flight assist is on again'),
                    ('travel', 'travel_on', r'activity=travel', 'travel mode is on'),
                    ('travel', 'travel_off', r'activity=none', 'travel mode is off again')):
                row = held(c, step, pattern)
                check(name, bool(row), 'player %d: %s' % (k, '%s at tick %d' % (did, row['tick']) if row else
                      'not done: %s (the call answered %s; read: %s)' % (did, shown(c, step.split('_')[0], 40), shown(c, step, 150))))
            text = fleet_check(check, c, k, 'look_start', 'warship')
            start = last_answer(c, 'fleet_start')
            before = start['text'] if start else ''
            check('fixture', rig.field(before, 'first') not in (None, 'none'), "player %d's warship has turrets: %s" % (k, before[:300] or 'no row'))
            for name, step, pattern, did in (
                    ('turrets', 'turrets_done', r'allmode=%s ' % a.turrets, 'all its turrets are set to %s' % a.turrets),
                    ('part', 'part_done', r'first=%s/' % a.group, 'its first turret part is set to %s (%s)' % (a.group, shown(c, 'part', 40))),
                    ('part', 'partoff_done', r'first=%s/false' % a.group, 'that part is disarmed'),
                    ('disarmed', 'disarm_done', r'allarmed=false ', 'all its turrets are disarmed'),
                    ('armed', 'arm_done', r'allarmed=true ', 'all its turrets are armed again')):
                row = held(c, step, pattern)
                check(name, bool(row), 'player %d: %s' % (k, '%s at tick %d' % (did, row['tick']) if row else
                      'not done: %s (the call answered %s; read: %s)' % (did, shown(c, step.split('_')[0], 40), shown(c, step, 260))))
            if (rig.field(before, 'drones') or '0')[0] == '0':
                check.note('drones', "player %d's warship holds no drones (%s): their mode was not set" % (k, rig.field(before, 'drones')))
            else:
                for name, step, pattern, did in (
                        ('drones', 'drone_done', Systems.DRONES + r'/[^/]+#2/', 'its %s drones have their second mode' % Systems.DRONES),
                        ('drones', 'dronearm_done', Systems.DRONES + r'/[^/]+/false/', 'its %s drones are disarmed' % Systems.DRONES)):
                    row = held(c, step, pattern)
                    check(name, bool(row), 'player %d: %s' % (k, '%s at tick %d (%s)' % (did, row['tick'], rig.field(row['text'], 'drones')) if row else
                          'not done: %s (the call answered %s; read: %s)' % (did, shown(c, step.split('_')[0], 40), rig.field(shown(c, step, 900), 'drones'))))
        for i, other in enumerate(Systems.SURVEY):
            row = last_answer(clients[1], 'survey_%d' % i)
            if row:
                check.note('survey', 'ship %s: %s' % (other.lstrip('0'), row['text'][:300]))


# ---- an enemy of one's own choosing --------------------------------------------------------------------------------
class Hostile:
    """Each player marks a neutral ship as hostile (the interact menu's entry and X4's confirmation)."""
    name = 'hostile'
    minutes = 4
    marks = ('marked',)
    # Two Argon freighters of the sector on trade routines of their own (KRI-213 and ZOZ-983): nobody's fleet.
    SHIPS = {1: '000000000009e58a', 2: '0000000000080742'}
    sets = ()
    LIMIT = 4000

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def chain(k, a):
        ship = Hostile.SHIPS[k]
        return [
            ('look_start', 'op', 'relation_look', ship),
            ('mark', 'op', 'mark_hostile', ship),
            ('marked', 'try', 'hostile=true', 10, 600, 'relation_look', ship),
            ('settle', 'wait', 300),
            ('look_end', 'op', 'relation_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('relation_look', Hostile.SHIPS[k]) for k in PLAYERS]

    @staticmethod
    def plan(a):
        return [[(120 + 60 * (k - 1), 'chain', Hostile.LIMIT, Hostile.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            start = shown(c, 'look_start', 80)
            check('fixture', 'hostile=false' in start and 'owner=player' not in start, "player %d's target at the start: %s" % (k, start.strip()))
            row = held(c, 'marked', r'hostile=true')
            check('marked', bool(row), 'player %d: %s' % (k, 'the ship is hostile at tick %d (%s); ten seconds later: %s' % (row['tick'], row['text'].strip(), shown(c, 'look_end', 60).strip()) if row else
                  'the ship is not hostile (the call answered %s; %s)' % (shown(c, 'mark', 30), shown(c, 'marked', 60).strip())))
        # the other client reads the same relation: one faction
        rows = sorted(clients[2]['ops'].get('relation_look', []))
        if rows:
            check.note('relations', 'client 2 reads at tick %d: %s' % (rows[-1][0], ' | '.join(x[2].strip() for x in rows[-2:])))
        for n in PLAYERS:
            refused = {key: count for key, count in clients[n]['refused'].items() if 'RelationBoost' in key}
            check('shared', not refused, 'client %d: %s' % (n, 'the call was not refused' if not refused else 'refused: %s' % refused))


# ---- repeat orders -------------------------------------------------------------------------------------------------
class TradeLoop:
    """Each player puts a fleet courier on repeat orders: buy at one station, sell at another, and again."""
    name = 'tradeloop'
    minutes = 15
    marks = ('bought', 'sold', 'again')
    SHIPS = FleetTrade.SHIPS
    # A police fighter of the player's (HSG-795) docks at the buying station first: a station's offers are known to
    # the player only while a ship of the player's is near it, and a sale order with no offer in sight never sells (run
    # p6 of 19 Nov: the loop went round, SingleSell found nothing 27 km away). A person would send a scout the same way.
    SPOTTER = '00000000000a5722'
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s'}" % (SHIPS[1], SHIPS[2], SPOTTER),)
    LIMIT = 24000

    @staticmethod
    def options(s):
        s.add_argument('--buy-station', default=Trade.BUY, help='saved ID of the station that sells the ware')
        s.add_argument('--sell-station', default=Trade.SELL, help='saved ID of the station that buys it')
        s.add_argument('--ware', default=Trade.WARE)
        s.add_argument('--amount', type=int, default=Trade.AMOUNT)
        s.add_argument('--credits', type=int, default=Trade.CREDITS, help='money the fixture adds to the account before the run')

    @staticmethod
    def chain(k, a):
        ship = TradeLoop.SHIPS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('cargo_start', 'op', 'cargo', a.ware, ship),
            ('entry_loop', 'op', 'menu_entry', 'MapMenu', 'buttonConfirmTradeLoop'),
            # the courier's own routine ends: Wait as its default behaviour
            ('behaviour', 'op', 'behaviour', ship, 'Wait'),
            ('behaviour_wait', 'wait', 30),
            ('behaviour_confirm', 'op', 'behaviour_confirm', ship),
            ('behaviour_set', 'try', 'default=Wait', 10, 300, 'ship_look', ship),
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
        ] + ([('spot', 'op', 'dock', TradeLoop.SPOTTER, a.sell_station),
              ('spotted', 'poll', 'buy=', 60, 9000, 'trade_look', a.sell_station, a.ware)] if k == 1 else
             [('spotted', 'poll', 'buy=', 60, 9000, 'trade_look', a.sell_station, a.ware)]) + [
            # the behaviour page's "Repeat orders", then the trade menu's loop Confirm twice: buy here, sell there
            ('loop', 'op', 'behaviour_loop', ship),
            ('loop_wait', 'wait', 30),
            ('buy', 'op', 'trade_loop_order', ship, 'SingleBuy', a.buy_station, a.ware, a.amount),
            ('buy_wait', 'wait', 20),
            ('sell', 'op', 'trade_loop_order', ship, 'SingleSell', a.sell_station, a.ware, 0),
            ('queued', 'try', 'orders=[2-9],.*SingleBuy.*SingleSell', 10, 300, 'ship_look', ship),
            ('bought', 'poll', '^amount=%d ' % a.amount, 60, 9000, 'cargo', a.ware, ship),
            ('sold', 'poll', '^amount=0 ', 60, 9000, 'cargo', a.ware, ship),
            ('sold_look', 'op', 'ship_look', ship),
            ('again', 'poll', '^amount=%d ' % a.amount, 60, 9000, 'cargo', a.ware, ship),     # the loop: the buy once more
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('cargo', a.ware, TradeLoop.SHIPS[k]) for k in PLAYERS] + [('ship_look', TradeLoop.SHIPS[k]) for k in PLAYERS]

    @staticmethod
    def plan(a):
        return [[(40, 'wallet', a.credits), (60 + 30 * (k - 1), 'chain', TradeLoop.LIMIT, TradeLoop.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_start', 'courier')
            entry_checks(check, c, k, (('loop', 'MapMenu', 'buttonConfirmTradeLoop'),))
            queued = held(c, 'queued', r'SingleSell')
            check('queued', bool(queued), 'player %d: %s' % (k, 'repeat orders and both orders in the queue at tick %d (%s; %s, %s)' %
                  (queued['tick'], rig.field(queued['text'], 'orders'), shown(c, 'buy', 40), shown(c, 'sell', 40)) if queued else
                  'the orders are not in the queue (loop %s, buy %s, sell %s; the queue: %s)' %
                  (shown(c, 'loop', 20), shown(c, 'buy', 40), shown(c, 'sell', 40), rig.field(shown(c, 'queued', 600), 'orders'))))
            for name, did in (('bought', 'bought'), ('sold', 'sold'), ('again', 'bought again: the queue went round')):
                pattern = r'^amount=0 ' if name == 'sold' else r'^amount=%d ' % a.amount
                row = held(c, name, pattern)
                check(name, bool(row), 'player %d: %s' % (k, 'the courier %s at tick %d' % (did, row['tick']) if row else
                      'the courier has not %s (its hold: %s; its queue: %s)' % (did, shown(c, name, 60), rig.field(shown(c, 'look_end', 600) or shown(c, 'sold_look', 600), 'orders'))))
        rows = sorted(clients[1]['ops'].get('cargo', []))
        if rows:
            check.note('money', 'the account read %s at tick %d and %s at tick %d' % (rig.field(rows[0][2], 'money'), rows[0][0], rig.field(rows[-1][2], 'money'), rows[-1][0]))


# ---- wares from ship to ship ---------------------------------------------------------------------------------------
class Exchange:
    """Each player has a fleet courier hand part of its cargo to a fleet capital ship (the map's ware exchange)."""
    name = 'exchange'
    minutes = 10
    marks = ('queued', 'given')
    # The two couriers of `fleettrade` and the two capital ships of `storage`, handed to the player faction by the
    # fleet fixture; the hold fixture puts energy cells into the couriers.
    SHIPS, HOSTS = FleetTrade.SHIPS, {1: '0000000000074c58', 2: '00000000000751bf'}
    WARE, HELD, GIVEN = 'energycells', 50, 30
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (SHIPS[1], SHIPS[2], HOSTS[1], HOSTS[2]),)
    LIMIT = 16000

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def chain(k, a):
        ship, host = Exchange.SHIPS[k], Exchange.HOSTS[k]
        steps = []
        for who in (ship, host):          # both end their own routines: Wait as the default behaviour, an empty queue
            word = 'ship' if who == ship else 'host'
            steps += [(word + '_look', 'op', 'ship_look', who),
                      (word + '_wait', 'op', 'behaviour', who, 'Wait'),
                      (word + '_pause', 'wait', 30),
                      (word + '_confirm', 'op', 'behaviour_confirm', who),
                      (word + '_waiting', 'try', 'default=Wait', 10, 300, 'ship_look', who),
                      (word + '_clear', 'op', 'clear', who)]
        return steps + [
            ('settle', 'wait', 60),
            ('entry_trade', 'op', 'menu_entry', 'MapMenu', 'buttonConfirmTrade'),
            ('hold_start', 'op', 'cargo', Exchange.WARE, ship),
            ('host_start', 'op', 'cargo', Exchange.WARE, host),
            # the trade menu in its exchange mode: virtual cargo on both ships, the list, Confirm, the menu's close
            ('vcargo_on', 'op', 'exchange_vcargo', ship, host, True),
            ('offers', 'poll', 'offers=[1-9]', 10, 600, 'exchange_look', ship, host, Exchange.WARE),
            ('give', 'op', 'trade_exchange', ship, host, Exchange.WARE, Exchange.GIVEN),   # the exchange menu's Confirm
            ('vcargo_off', 'op', 'exchange_vcargo', ship, host, False),
            ('queued', 'try', 'orders=[1-9]', 10, 300, 'ship_look', ship),
            ('given', 'poll', '^amount=%d ' % (Exchange.HELD - Exchange.GIVEN), 60, 9000, 'cargo', Exchange.WARE, ship),
            # the wares reach the capital ship a few seconds after they leave the courier (run p6 of 19 Nov)
            ('host_end', 'try', '^amount=%d ' % Exchange.GIVEN, 30, 900, 'cargo', Exchange.WARE, host),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('cargo', Exchange.WARE, ship) for group in (Exchange.SHIPS, Exchange.HOSTS) for ship in (group[1], group[2])] + \
               [('ship_look', Exchange.SHIPS[k]) for k in PLAYERS]

    @staticmethod
    def plan(a):
        kit = [(40 + k, 'hold_stock', Exchange.SHIPS[k], Exchange.WARE, Exchange.HELD) for k in PLAYERS]
        return [kit + [(120 + 30 * (k - 1), 'chain', Exchange.LIMIT, Exchange.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'ship_look', 'courier')
            fleet_check(check, c, k, 'host_look', 'capital ship')
            entry_checks(check, c, k, (('trade', 'MapMenu', 'buttonConfirmTrade'),))
            start = last_answer(c, 'hold_start')
            had = rig.field(start['text'], 'amount') if start else None
            check('fixture', had is not None and int(had) >= Exchange.HELD, "player %d's courier holds %s %s at the start" % (k, had, Exchange.WARE))
            queued = held(c, 'queued', r'orders=[1-9]')
            row = held(c, 'given', r'^amount=%d ' % (Exchange.HELD - Exchange.GIVEN))
            before, after = (rig.field(shown(c, name, 60), 'amount') for name in ('host_start', 'host_end'))
            check('given', bool(row), 'player %d: %s' % (k, 'the courier holds %d %s at tick %d and the capital ship %s (was %s; the order in the queue at tick %s: %s)' %
                  (Exchange.HELD - Exchange.GIVEN, Exchange.WARE, row['tick'], after, before, queued['tick'] if queued else '?',
                   rig.field(queued['text'], 'orders') if queued else '-') if row else
                  'nothing changed hands (the exchange list: %s; the call answered %s; the queue: %s; the courier: %s)' %
                  (shown(c, 'offers', 60), shown(c, 'give', 30), rig.field(shown(c, 'look_end', 600), 'orders'), shown(c, 'given', 60))))
            if row and before is not None and after is not None:
                check('received', int(after) - int(before) == Exchange.GIVEN, 'player %d: the capital ship gained %d %s' % (k, int(after) - int(before), Exchange.WARE))


# ---- a ship aboard a ship ------------------------------------------------------------------------------------------
class Storage:
    """Each player docks a fleet fighter at a fleet capital ship, puts it into that ship's internal storage and takes it out again."""
    name = 'storage'
    minutes = 10
    marks = ('docked', 'stored', 'fetched')
    # Two Argon fighters with their pilots (ROQ-362 and RVS-616) and two capital ships of the sector that take small
    # ships aboard (the carrier FBA-908 and the resupply ship NPH-107), all handed to the player faction by the fixture.
    SHIPS = {1: '00000000000a56bf', 2: '00000000000a7949'}
    HOSTS = {1: '0000000000074c58', 2: '00000000000751bf'}
    sets = ("native_gate_ui_replay_fleet={'%s','%s','%s','%s'}" % (SHIPS[1], SHIPS[2], HOSTS[1], HOSTS[2]),)
    LIMIT = 16000

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def chain(k, a):
        ship, host = Storage.SHIPS[k], Storage.HOSTS[k]
        return [
            ('look_start', 'op', 'ship_look', ship),
            ('host_start', 'op', 'ship_look', host),
            ('entry_dock', 'op', 'menu_entry', 'InteractMenu', 'orderDock'),
            # the capital ship ends its own patrol or supply run: Wait as its default behaviour
            ('behaviour', 'op', 'behaviour', host, 'Wait'),
            ('behaviour_wait', 'wait', 30),
            ('behaviour_confirm', 'op', 'behaviour_confirm', host),
            ('behaviour_set', 'try', 'default=Wait', 10, 300, 'ship_look', host),
            ('host_clear', 'op', 'clear', host),
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
            ('dock', 'op', 'dock', ship, host),                                   # "Dock at" the capital ship
            ('docked', 'poll', 'docked=true', 60, 10000, 'ship_look', ship),
            ('docked_wait', 'wait', 120),
            ('place_docked', 'op', 'storage_look', ship),
            ('store', 'poll', '^put', 10, 3000, 'store', ship),                    # "Put into storage", once the entry is active
            ('stored', 'try', 'outside=false', 10, 1800, 'storage_look', ship),
            ('stored_wait', 'wait', 300),
            ('retrieve', 'poll', '^requested', 10, 3000, 'retrieve', ship),        # "Retrieve from storage"
            ('fetched', 'try', 'outside=true', 10, 3600, 'storage_look', ship),
            ('fetched_wait', 'wait', 120),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', Storage.SHIPS[k]) for k in PLAYERS] + [('storage_look', Storage.SHIPS[k]) for k in PLAYERS] + [('ship_look', Storage.HOSTS[k]) for k in PLAYERS]

    @staticmethod
    def plan(a):
        return [[(120 + 30 * (k - 1), 'chain', Storage.LIMIT, Storage.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_start', 'fighter')
            fleet_check(check, c, k, 'host_start', 'capital ship')
            entry_checks(check, c, k, (('dock', 'InteractMenu', 'orderDock'),))
            docked = held(c, 'docked', r'docked=true')
            check('docked', bool(docked), 'player %d: %s' % (k, 'the fighter docked at the capital ship at tick %d (%s)' % (docked['tick'], shown(c, 'place_docked', 110)) if docked else
                  'the fighter did not dock (%s)' % shown(c, 'docked')))
            # the replayed calls of this player on both clients
            calls = [[x for x in clients[n]['built'] if x[1] == k and (x[2].startswith('PutShipIntoStorage') or x[2].startswith('RequestShipFromInternalStorage2'))] for n in PLAYERS]
            row = held(c, 'stored', r'outside=false')
            put, greyed = held(c, 'store', r'^put'), [x for x in answers(c, 'store') if x['text'].startswith('greyed')]
            check('stored', bool(row and put), 'player %d: %s' % (k, 'the fighter is in the internal storage at tick %d (four seconds after the docking the entry was greyed out for another %d ticks: %s)' %
                  (row['tick'], put['tick'] - greyed[0]['tick'] if greyed else 0, greyed[0]['text'] if greyed else 'never') if row and put else
                  'the fighter was not stored (the call answered %s; before: %s; then: %s)' % (shown(c, 'store', 40), shown(c, 'place_docked', 110), shown(c, 'stored', 110))))
            row = held(c, 'fetched', r'outside=true')
            check('fetched', bool(row) and bool(held(c, 'retrieve', r'^requested')), 'player %d: %s' % (k, 'the fighter is back at an outside dock at tick %d' % row['tick'] if row and held(c, 'retrieve', r'^requested') else
                  'the fighter did not come out (the call answered %s; then: %s)' % (shown(c, 'retrieve', 40), shown(c, 'fetched', 110))))
            check('replayed', len(calls[0]) >= 2 and calls[0] == calls[1], 'player %d: both calls ran on both clients (%s)' % (k, ', '.join('%s at tick %d' % (x[2], x[0]) for x in calls[0]) or
                  'client 1 %s, client 2 %s' % (calls[0][:2], calls[1][:2])))


# ---- an abandoned ship ---------------------------------------------------------------------------------------------
class Claim:
    """Each player has a fleet frigate claim an abandoned ship: a marine flies over and the ship is the player's."""
    name = 'claim'
    minutes = 8
    marks = ('claim', 'claimed')
    # The two Argon frigates of `systems`; the fixtures give each two marines and put an ownerless scout near it.
    SHIPS = {1: '00000000000a214c', 2: '000000000008f987'}
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 12000
    ABANDON_AT = 900          # when the fixture makes the abandoned ships: the frigates have stopped by then

    @staticmethod
    def options(s):
        s.add_argument('--metres', type=int, default=1500, help='how far from the frigate its abandoned ship appears')
        s.add_argument('--ask', action='store_true', help='leave the standing order for a found abandoned ship as it is: the frigate asks its player for orders '
                                                          '(an interactive notification; it parted the clients until the gate of 19 Nov)')
        s.add_argument('--answer', action='store_true', help='with the question on: a player answers the first one (the key on the notification) and picks '
                                                             '"Claim it" in the conversation, so frigate 1 claims its ship by itself; the other player tries to '
                                                             'answer two seconds later (nothing raised: the conversation is taken or the question over) and has '
                                                             'frigate 2 claim the second ship by order when the questions are over')
        s.add_argument('--answer-by', type=int, choices=(1, 2), default=1, help='with --answer: the player who answers (2: a player 2..N answers, with '
                                                                                'conversation owners; the conversation is its own)')

    @staticmethod
    def wreck(k):
        return 'X4LAN_Abandoned_%d' % k

    @staticmethod
    def chain(k, a):
        ship, name = Claim.SHIPS[k], Claim.wreck(k)
        # the faction's standing order for "found an abandoned ship" (signal 2): ignore, and do not ask (the op's call
        # sets no "ask"); one player sets it for the faction
        quiet = [('quiet', 'op', 'fac_response', 2, 2), ('quiet_wait', 'wait', 20)] if k == 1 and not (a.ask or a.answer) else []
        if a.answer:
            # The question is on every client's monitor about a second after the abandoned ship appears, for ten seconds:
            # frigate 1's comes first. The answering player presses the key at once, the other two seconds later (while
            # a player is in the conversation that follows an answer, X4 lets no other ship ask, run p6 of 19 Nov). The
            # answer makes frigate 1 claim its ship; the other player has frigate 2 claim the second by order.
            first = k == a.answer_by
            asked = [('question', 'poll', 'question=[1-9]', 5, 2400, 'question_look')] + \
                ([] if first else [('answer_pause', 'wait', 60)]) + [('answer', 'op', 'question_answer')]
            ours = asked + [('answer_wait', 'wait', 90),
                            ('choose', 'op', 'dialog', 1),                        # the conversation's first choice: "Claim it"
                            ('crew', 'op', 'ship_look', ship)] if first else \
                asked + [('questions_over', 'wait', 900),
                         ('crew', 'op', 'ship_look', Claim.SHIPS[2]),
                         ('claim', 'op', 'claim', Claim.SHIPS[2], Claim.wreck(2))]
        else:
            ours = [('wreck_wait', 'wait', 60),
                    ('crew', 'op', 'ship_look', ship),
                    ('claim', 'op', 'claim', ship, name)]                         # the interact menu's "Claim"
        return quiet + [
            ('orders_start', 'op', 'settings_look'),
            ('look_start', 'op', 'ship_look', ship),
            # the frigate ends its patrol and stops: Wait as its default behaviour
            ('behaviour', 'op', 'behaviour', ship, 'Wait'),
            ('behaviour_wait', 'wait', 30),
            ('behaviour_confirm', 'op', 'behaviour_confirm', ship),
            ('behaviour_set', 'try', 'default=Wait', 10, 300, 'ship_look', ship),
            ('clear', 'op', 'clear', ship),
            ('wreck', 'poll', 'state=ok', 30, 2400, 'wreck_look', name),          # the fixture's abandoned ship is there
        ] + ours + [
            ('claimed', 'poll', 'owner=player', 60, 7200, 'wreck_look', name),
            ('settle', 'wait', 120),
            ('wreck_end', 'op', 'wreck_look', name),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', Claim.SHIPS[k]) for k in PLAYERS] + [('wreck_look', Claim.wreck(k)) for k in PLAYERS]

    @staticmethod
    def plan(a):
        kit = [(50, 'marines_stock', Claim.SHIPS[k], 2) for k in PLAYERS] + [(Claim.ABANDON_AT + 30 * (k - 1), 'abandon', Claim.SHIPS[k], Claim.wreck(k), a.metres) for k in PLAYERS]
        return [kit + [(120 + 30 * (k - 1), 'chain', Claim.LIMIT, Claim.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_start', 'frigate')
            if k == 1:
                asks = a.ask or a.answer
                check.note('question', 'the standing order for a found abandoned ship: %s (%s)' %
                           (shown(c, 'quiet', 60) if not asks else 'left as it is', 'the frigates ask nobody' if not asks else 'the frigates ask their player: an interactive notification'))
            if a.answer:
                # the answers as every client applied them (the conversation family's event of the answering slot), and the
                # choice; the other player's answer raised nothing (the conversation taken, or the question over)
                rows = [[x for x in clients[n]['applied'] if x[2].startswith('answer(') and x[1] == k] for n in PLAYERS]
                said = shown(c, 'answer', 60)
                if k == a.answer_by:
                    check('answered', said.startswith('answered') and len(rows[0]) == 1 and rows[0] == rows[1],
                          'player %d: %s' % (k, 'its answer to the frigate\'s question was raised on both clients at tick %d (%s), then "Claim it" (%s)' %
                                            (rows[0][0][0], rows[0][0][2], shown(c, 'choose', 20)) if said.startswith('answered') and rows[0] and rows[0] == rows[1] else
                                            'no answer on both clients (the call said %s; client 1 %s, client 2 %s; the question: %s)' %
                                            (said, rows[0][:2], rows[1][:2], shown(c, 'question', 40))))
                else:
                    # the other player's key two seconds later: nothing raised (the conversation taken, the question over),
                    # or the answer to another ship's question that came up by then, the same on both clients; never the
                    # first question a second time
                    refused = {key: count for key, count in c['refused'].items() if 'RaisePlayerInteractionEvent' in key}
                    first = [[x for x in clients[n]['applied'] if x[2].startswith('answer(') and x[1] == a.answer_by] for n in PLAYERS]
                    again = any(x[2] == y[2] for x in rows[0] for y in first[0])
                    check('second', rows[0] == rows[1] and not again, "player %d: its key after player %d's answer %s (the call said %s)" %
                          (k, a.answer_by, ('answered another question on both clients: %s' % ', '.join(x[2] for x in rows[0])) if rows[0] and rows[0] == rows[1] and not again else
                           ('raised nothing on either client (%s)' % (('refused at the capture: %s' % refused) if refused else 'taken or over at the apply')) if not rows[0] and not rows[1] else
                           'differs between the clients or answered the first question again: client 1 %s, client 2 %s' % (rows[0], rows[1]), said))
                    check.note('asked', "player %d's turn: %s" % (k, 'a second question came' if len([x for x in answers(c, 'question') if 'question=' in x['text']]) > 1 else
                               'no other question while the conversation ran (X4 lets one question through at a time)'))
            crew = shown(c, 'crew', 900)
            check('fixture', 'marine:' in crew, "player %d's frigate has marines aboard: people %s" % (k, rig.field(crew, 'people')))
            before = held(c, 'wreck', r'state=ok')
            check('fixture', bool(before) and 'owner=ownerless' in before['text'] and 'claimable=true' in before['text'],
                  "player %d's abandoned ship: %s" % (k, before['text'] if before else shown(c, 'wreck')))
            calls = [[x for x in clients[n]['built'] if x[1] == k and x[2].startswith('ClaimShip')] for n in PLAYERS]
            answer = shown(c, 'claim', 60)
            if not (a.answer and k == a.answer_by):   # (frigate 1 claims by itself after the answer)
                check('ordered', answer.startswith('claimed=true') and len(calls[0]) >= 1 and calls[0] == calls[1], 'player %d: %s' % (k, 'the claim ran on both clients at tick %d (%s)' % (calls[0][0][0], answer)
                      if answer.startswith('claimed=true') and calls[0] and calls[0] == calls[1] else 'no claim on both clients (the call answered %s; client 1 %s, client 2 %s)' % (answer, calls[0][:2], calls[1][:2])))
            row = held(c, 'claimed', r'owner=player')
            check('claimed', bool(row), 'player %d: %s' % (k, 'the ship is the player faction\'s at tick %d (%s)' % (row['tick'], row['text'].strip()) if row else
                  'the ship stayed ownerless (%s)' % shown(c, 'claimed', 120)))


# ---- a fleet ship's deployables, and mining ------------------------------------------------------------------------
class Mining:
    """Each player has a fleet miner drop two resource probes at places a few kilometres off, and mine if the sector yields anything."""
    name = 'mining'
    minutes = 10
    marks = ('dropped_east', 'dropped_west')
    # Two miners for solids of the sector with their pilots (DAH-048 and IYB-310, on mining routines of their own until
    # the fleet fixture hands them to the player faction).
    SHIPS = {1: '000000000008822a', 2: '0000000000095f9a'}
    SECTOR = '0000000000000729'
    # (a miner's deployable storage was full of probes in run p3, so the fixture's satellite did not go in: probes only)
    PROBE, WORD = 'eq_arg_resourceprobe_01_macro', 'ResourceProbes'
    KINDS = (('east', 3000), ('west', -3000))
    sets = ("native_gate_ui_replay_fleet={'%s','%s'}" % (SHIPS[1], SHIPS[2]),)
    LIMIT = 16000

    @staticmethod
    def options(s):
        s.add_argument('--ware', default='ore', help='what to mine')

    @staticmethod
    def chain(k, a):
        ship, sector = Mining.SHIPS[k], Mining.SECTOR
        steps = [
            ('look_start', 'op', 'ship_look', ship),
            ('hold_start', 'op', 'cargo', a.ware, ship),
            ('spot', 'op', 'mine_spot', sector, a.ware, ship),
            # the miner's own routine ends first: Wait as its default behaviour
            ('behaviour', 'op', 'behaviour', ship, 'Wait'),
            ('behaviour_wait', 'wait', 30),
            ('behaviour_confirm', 'op', 'behaviour_confirm', ship),
            ('behaviour_set', 'try', 'default=Wait', 10, 300, 'ship_look', ship),
            ('clear', 'op', 'clear', ship),
            ('settle', 'wait', 30),
        ]
        for n, (kind, east) in enumerate(Mining.KINDS, 1):
            # the interact menu's "Deploy" entry on a place of the sector for the selected ship: an order with its
            # place, the macro and the amount (longer than one event: it travels in parts)
            steps += [('held_' + kind, 'op', 'deploy_count', Mining.WORD, ship),
                      ('drop_' + kind, 'op', 'order', ship, 'DeployObjectAtPosition',
                       (('destination', ('at', sector, ship, east, 0, 2000)), ('objectstodeploy', Mining.PROBE), ('amountstodeploy', 1))),
                      ('dropped_' + kind, 'poll', 'launched=%d' % n, 60, 5000, 'deploy_count', Mining.WORD, ship)]
        return steps + [
            ('gain_start', 'op', 'cargo_gain', ship),
            ('mine', 'op', 'mine', ship, a.ware, sector),                       # the interact menu's "Mine" on a place that yields
            ('mining', 'try', 'MiningPlayer/', 10, 300, 'ship_look', ship),
            ('mined', 'try', 'gain=[1-9]', 60, 600, 'cargo_gain', ship),
            ('hold_end', 'op', 'cargo', a.ware, ship),
            ('look_end', 'op', 'ship_look', ship),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', Mining.SHIPS[k]) for k in PLAYERS] + [('cargo', a.ware, Mining.SHIPS[k]) for k in PLAYERS] + [('deploy_look', 1)]

    @staticmethod
    def plan(a):
        kit = [(40 + k, 'launch_stock', Mining.SHIPS[k], Mining.PROBE, 2) for k in PLAYERS]
        return [kit + [(120 + 30 * (k - 1), 'chain', Mining.LIMIT, Mining.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            fleet_check(check, c, k, 'look_start', 'miner')
            for n, (kind, east) in enumerate(Mining.KINDS, 1):
                row = held(c, 'dropped_' + kind, r'launched=%d' % n)
                check('dropped', bool(row), 'player %d: %s' % (k, 'the miner dropped a probe at the place 3 km %s by tick %d (%s)' % (kind, row['tick'], row['text'].strip()) if row else
                      "no probe dropped %s (the order answered %s; the stock: %s; the miner's queue: %s)" %
                      (kind, shown(c, 'drop_' + kind, 40), shown(c, 'dropped_' + kind, 60), rig.field(shown(c, 'look_end', 600), 'orders'))))
            # the order's parts as both clients applied them
            parts = [[x for x in clients[n]['applied'] if x[1] == k and x[2] in ('part', 'order=DeployObjectAtPosition')] for n in PLAYERS]
            check('order', len(parts[0]) >= 4 and parts[0] == parts[1], 'player %d: both deploy orders ran on both clients, each in two parts (%s)' %
                  (k, ', '.join('%s at tick %d' % (x[2], x[0]) for x in parts[0]) or 'client 1 %s, client 2 %s' % (parts[0][:4], parts[1][:4])))
            spot = shown(c, 'spot', 160)
            if not spot.startswith('at='):
                check.note('mining', 'player %d: no place within 200 km of the miner yields %s (%s): the "Mine" order was not given (%s)' % (k, a.ware, spot, shown(c, 'mine', 30)))
                continue
            queued = held(c, 'mining', r'MiningPlayer/')
            row = held(c, 'mined', r'gain=[1-9]')
            check('mining', bool(queued), 'player %d: %s' % (k, 'the mining order is in the queue at tick %d (the place: %s; the order answered %s; the hold gained %s)' %
                  (queued['tick'], spot, shown(c, 'mine', 60), rig.field(row['text'], 'gain') if row else 'nothing in 20 seconds') if queued else
                  'no mining order (the order answered %s; the queue: %s)' % (shown(c, 'mine', 60), rig.field(shown(c, 'look_end', 600), 'orders'))))


# ---- the faction's settings ----------------------------------------------------------------------------------------
class Settings:
    """Each player changes settings of the player information menu: standing orders, waiting ships, resupply, weapon mode, name, paint and logo; and its own ship's."""
    name = 'settings'
    minutes = 4
    marks = ('look_made',)
    sets = ()
    LIMIT = 5000
    NAME = 'X4LAN_Faction'
    # which half of the settings each player changes (--swap: the other half)
    ROLE = {1: 'a', 2: 'b'}

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def chain(k, a):
        mine = [
            # the faction's standing order for one signal (its first response, then its second), ships that wait for the
            # player, the faction's name
            ('response_a', 'op', 'fac_response', 1, 1),
            ('response_a_wait', 'wait', 20),
            ('look_a', 'op', 'settings_look'),
            ('response', 'op', 'fac_response', 1, 2),
            ('wait_off', 'op', 'fac_setting', 'ships_wait', False),
            ('wait_off_done', 'try', 'ships_wait=false', 10, 300, 'settings_look'),
            ('wait_on', 'op', 'fac_setting', 'ships_wait', True),
            ('wait_on_done', 'try', 'ships_wait=true', 10, 300, 'settings_look'),
            ('name', 'op', 'fac_setting', 'name', Settings.NAME),
            ('name_done', 'try', 'name=' + Settings.NAME, 10, 300, 'settings_look'),
        ] if Settings.ROLE[k] == 'a' else [
            # another signal's standing order, the resupply level, the turrets' default mode, the paint and the logo
            ('response_a', 'op', 'fac_response', 2, 1),
            ('response_a_wait', 'wait', 20),
            ('look_a', 'op', 'settings_look'),
            ('response', 'op', 'fac_response', 2, 2),
            ('loadout', 'op', 'fac_setting', 'loadout', 0.5),
            ('loadout_done', 'try', 'loadout=0.50', 10, 300, 'settings_look'),
            ('mode', 'op', 'fac_setting', 'weapon_mode', 'attackenemies'),
            ('mode_done', 'try', 'weapon_mode=attackenemies', 10, 300, 'settings_look'),
            ('paint', 'op', 'fac_setting', 'paint', 2),
            ('logo', 'op', 'fac_setting', 'logo', 2),
        ]
        return [('look_start', 'op', 'settings_look'), ('own_start', 'op', 'systems_look', k)] + mine + [
            # this player's own ship: its answer to a signal and its resupply level (the map's behaviour and loadout pages)
            ('ship_response_a', 'op', 'fac_ship_response', 1, 1),
            ('ship_a_wait', 'wait', 20),
            ('own_a', 'op', 'systems_look', k),
            ('ship_response', 'op', 'fac_ship_response', 1, 2),
            ('ship_resupply', 'op', 'fac_setting', 'resupply', 0.5),
            ('ship_done', 'try', 'resupply=0.50', 10, 300, 'systems_look', k),
            ('settle', 'wait', 90),
            ('look_made', 'op', 'settings_look'),
            ('own_made', 'op', 'systems_look', k),
        ]

    @staticmethod
    def watch(a):
        return [('settings_look',), ('systems_look', 1), ('systems_look', 2)]

    @staticmethod
    def plan(a):
        return [[(120 + 300 * (k - 1), 'chain', Settings.LIMIT, Settings.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        def pair(c, step, signal_first=True):
            """'signal:response' of a response op's answer 'signal/response'."""
            row = last_answer(c, step)
            return row['text'].split()[0].replace('/', ':') if row and '/' in row['text'] else None

        for k in PLAYERS:
            c = clients[k]
            made = last_answer(c, 'look_made')
            after = made['text'] if made else ''
            first, want = pair(c, 'response_a'), pair(c, 'response')
            was = rig.field(shown(c, 'look_a', 900), 'responses') or ''
            done = bool(first and want) and first != want and first in was and want in (rig.field(after, 'responses') or '')
            check('order', done, 'player %d: %s' % (k, "the faction's standing order read %s, then %s" % (first, want) if done else
                  'the standing order did not follow (the calls answered %s and %s; read: %s, then %s)' %
                  (shown(c, 'response_a', 60), shown(c, 'response', 60), was, rig.field(after, 'responses'))))
            tests = ((('waiting', 'wait_off_done', r'ships_wait=false', 'ships do not wait for the player'), ('waiting', 'wait_on_done', r'ships_wait=true', 'ships wait for the player again'),
                      ('name', 'name_done', r'name=' + Settings.NAME, 'the faction is called ' + Settings.NAME)) if Settings.ROLE[k] == 'a' else
                     (('resupply', 'loadout_done', r'loadout=0\.50', 'the faction resupplies to the medium level'), ('mode', 'mode_done', r'weapon_mode=attackenemies', "the turrets' default mode is attackenemies")))
            for name, step, pattern, did in tests:
                row = held(c, step, pattern)
                check(name, bool(row), 'player %d: %s' % (k, '%s at tick %d' % (did, row['tick']) if row else
                      'not done: %s (the call answered %s; read: %s)' % (did, shown(c, step[:-5], 40), shown(c, step, 200))))
            if Settings.ROLE[k] == 'b':
                for what in ('paint', 'logo'):
                    row = last_answer(c, what)
                    value = row['text'].split()[0] if row else None
                    check(what, bool(value) and rig.field(after, what) == value.replace(' ', '_'), 'player %d: %s' % (k, 'the %s is %s' % (what, value) if value and rig.field(after, what) == value else
                          'the %s did not change (the call answered %s; read: %s)' % (what, shown(c, what, 60), rig.field(after, what))))
            own = last_answer(c, 'own_made')
            text = own['text'] if own else ''
            first, want = pair(c, 'ship_response_a'), pair(c, 'ship_response')
            was = rig.field(shown(c, 'own_a', 900), 'responses') or ''
            check('ship', bool(first and want) and first != want and first in was and want in (rig.field(text, 'responses') or '') and rig.field(text, 'resupply') == '0.50',
                  "player %d's own ship: its answer read %s, then %s; resupply level %s (was %s)" %
                  (k, first if first and first in was else 'not ' + str(first), want if want and want in text else 'not ' + str(want), rig.field(text, 'resupply'),
                   rig.field(shown(c, 'own_start', 900), 'resupply')))
        rows = sorted(clients[1]['ops'].get('settings_look', []))
        if rows:
            check.note('settings', 'at tick %d: %s' % (rows[-1][0], rows[-1][2][:330]))


# ---- the transporter between a station's rooms ------------------------------------------------------------------------
class Rooms:
    """Each player docks at a station, gets up, walks to the dock's transporter and travels to another room of the station."""
    name = 'rooms'
    minutes = 10
    sets = ()
    marks = ('out', 'at_pad', 'aimed', 'room', 'look_1', 'look_2', 'look_3', 'look_4') + tuple('cal_%d' % n for n in range(9))
    STATION = '00000000000434b1'      # the Argon factory of the station walk tests
    GET_UP, BACK, TURN_RIGHT = 0x115, 27, 29   # INPUT_ACTION_GET_UP; FP_MOVE_BACKWARD (a docked small ship is left backwards); FP_TURN_RIGHT
    LIMIT = 17000
    FIND_EVERY = 300

    # The dock area's central transporter of CZF-533, in the station's frame (x, z): its floor (a docking bay's
    # connection02) offered the station's rooms, and the walk there from a ship's rear is open (run 1 of 21 Nov). The
    # bays' own transporters (connection27, X4's find_player_transporter_slot) lie behind walls: X4's walk check called
    # them out of reach and a straight walk stopped 25 m short.
    PAD = (0.0, 0.0)

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Rooms.STATION, help='saved ID of the station to dock at')
        s.add_argument('--room', default='Security Office', help="the room to travel to (a name in the transporter's list)")
        s.add_argument('--pad', type=float, nargs=2, default=Rooms.PAD, metavar=('X', 'Z'),
                       help="the transporter pad to walk to, in the station's frame")
        s.add_argument('--look', action='store_true', help='a look round only: out of the ship, then a picture of both windows at each quarter turn')
        s.add_argument('--direct', action='store_true', help="travel by the menu's call (TransportPlayerToTarget) instead of the crosshair, click and menu")
        s.add_argument('--calibrate', action='store_true', help='on the pad: each way of tilting and turning the view (recorded or local; mouselook or the FP axes), a picture after each')

    @staticmethod
    def chain(k, a):
        return [
            ('dock', 'until', '^docked', 'ai_dock_station', a.station, 9000),
            ('settle', 'wait', 150),
            ('seat', 'op', 'foot_where'),
            ('up', 'op', 'input', 'action', Rooms.GET_UP, 0),
            ('up_wait', 'wait', 150),
            ('stood', 'op', 'foot_where'),
            ('back_on', 'op', 'input', 'state', Rooms.BACK, 1),
            ('back', 'wait', 200),
            ('back_off', 'op', 'input', 'state', Rooms.BACK, 0),
            ('out', 'op', 'foot_where'),
            # the transporters around this player (MD finds them every FIND_EVERY ticks, canonical), the nearest walked to
        ] + ([step for n in range(1, 5) for step in (
                ('turn_%d_on' % n, 'op', 'input', 'state', Rooms.TURN_RIGHT, 1), ('turn_%d' % n, 'wait', 7),
                ('turn_%d_off' % n, 'op', 'input', 'state', Rooms.TURN_RIGHT, 0), ('settle_%d' % n, 'wait', 45),
                ('look_%d' % n, 'op', 'foot_where'), ('look_%d_wait' % n, 'wait', 60))] if a.look else []) + ([] if a.look else [
            # what the transporters around it are (MD, every FIND_EVERY ticks), and what the floor it stands on offers
            ('found', 'try', 'points=[1-9]', 30, 900, 'transporter_look'),
            ('targets_out', 'op', 'transport_list'),
            # the player's figure walks ahead by X4's NPC walk to the transporter's NPC waypoint (round the ledge
            # between the walkway and the pads), the player follows its trail and steps onto the nearest player
            # transporter; then the figure follows its player again (--pad X Z: a straight walk to a known pad instead)
        ] + ([('walk', 'until', '^there', 'walk_point', a.pad[0], a.pad[1], 0.5, 2400)] if a.pad != Rooms.PAD else [
            ('guide', 'op', 'avatar_guide', True),
            ('walk', 'until', '^there', 'walk_guided', 0.5, 5400),
            ('unguide', 'op', 'avatar_guide', False),
        ]) + [
            ('at_pad', 'op', 'foot_where'),
            ('targets', 'op', 'transport_list'),
            # as a person: look at the transporter, click, X4's TransporterMenu, the room's row, "Travel to"
            # (--direct: the menu's call itself, TransportPlayerToTarget)
        ] + (Rooms.calibration() if a.calibrate else []) + ([] if a.calibrate else [('travel', 'op', 'transport_named', a.room)] if a.direct else [
            ('aim', 'until', '^aim', 'transporter_aim', 4200),
            ('aimed', 'op', 'foot_where'),
            ('travel', 'until', '^there', 'transporter_use', a.room, 900),
        ]) + [
            ('travel_wait', 'wait', 300),
            ('room', 'op', 'foot_where'),
            ('targets_room', 'op', 'transport_list'),
        ])

    # The calibration (--calibrate): (name, op, kind, id, value, times) - each pressed `times` times 5 ticks apart, then
    # 40 ticks and a picture (mark cal_N). MOUSELOOK_YAW/PITCH 7/8, FP_YAW/FP_PITCH 0xd/0xe (an axis: set, then 0).
    CALIBRATION = [
        ('cal_1', 'input', 'range', 8, -0.1, 6),          # recorded mouselook pitch, down
        ('cal_2', 'input_local', 'range', 8, -0.1, 6),    # local mouselook pitch, down
        ('cal_3', 'input', 'range', 0xe, -1.0, 6),        # recorded FP pitch axis
        ('cal_4', 'input_local', 'range', 0xe, -1.0, 6),  # local FP pitch axis
        ('cal_5', 'input', 'range', 7, 0.1, 6),           # recorded mouselook yaw
        ('cal_6', 'input_local', 'range', 7, 0.1, 6),     # local mouselook yaw
        ('cal_7', 'input', 'range', 0xd, 1.0, 6),         # recorded FP yaw axis
        ('cal_8', 'input_local', 'range', 0xd, 1.0, 6),   # local FP yaw axis
    ]

    @staticmethod
    def calibration():
        steps = [('cal_0', 'op', 'foot_where')]
        for name, op, kind, ident, value, times in Rooms.CALIBRATION:
            for n in range(times):
                steps.append(('%s_%d' % (name, n), 'op', op, kind, ident, value))
                if ident in (0xd, 0xe):
                    steps.append(('%s_%d_zero' % (name, n), 'op', op, kind, ident, 0.0))
                steps.append(('%s_%d_wait' % (name, n), 'wait', 5))
            steps += [('%s_settle' % name, 'wait', 40), (name, 'op', 'foot_where'), ('%s_look' % name, 'wait', 30)]
        return steps

    @staticmethod
    def watch(a):
        return [('ship_look', 1), ('ship_look', 2), ('figure_look',)]

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2)]
        find = [(t, 'transporter_find', k) for t in range(1500, Rooms.LIMIT, Rooms.FIND_EVERY) for k in PLAYERS]
        return [kit + find + [(60 + 30 * (k - 1), 'chain', Rooms.LIMIT, Rooms.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            docked = last_answer(c, 'dock', 'docked')
            check('docked', bool(docked), 'player %d at the station: %s' % (k, 'tick %d' % docked['tick'] if docked else shown(c, 'dock')))
            out = last_answer(c, 'out')
            check('out', bool(out) and 'docked=' in out['text'], 'player %d out of its ship: %s' % (k, out['text'][:200] if out else shown(c, 'out')))
            found = last_answer(c, 'found', 'points=')
            check.note('found', 'player %d: player transporters around it (X4\'s find): %s; where it stood: %s' %
                       (k, found['text'][:160] if found else shown(c, 'found'), shown(c, 'targets_out', 160)))
            there = last_answer(c, 'walk', 'there')
            check('walked', bool(there), 'player %d: %s' % (k, 'at the pad at tick %d (%s)' % (there['tick'], there['text'][:160]) if there else shown(c, 'walk')))
            targets = last_answer(c, 'targets')
            check('targets', bool(targets) and a.room in targets['text'], "player %d: the transporter offers %s: %s" % (k, a.room, targets['text'][:300] if targets else shown(c, 'targets')))
            pad, room, went = last_answer(c, 'at_pad'), last_answer(c, 'room'), last_answer(c, 'travel')
            r0 = rig.field(pad['text'], 'room') if pad else None
            r1 = rig.field(room['text'], 'room') if room else None
            # the room the transporter offered under that name (its list where the player stood on the pad)
            offered = re.search(r'(\d+)/[a-z]+/[^/ ]+/' + re.escape(a.room), targets['text']) if targets else None
            to = offered.group(1) if offered else None
            if not a.direct:
                aimed, opened = last_answer(c, 'aim', 'aimed'), last_answer(c, 'travel', 'menu open')
                check('aimed', bool(aimed), 'player %d: %s' % (k, aimed['text'][:220] if aimed else shown(c, 'aim', 220)))
                check('menu', bool(opened), 'player %d: %s' % (k, opened['text'][:160] if opened else shown(c, 'travel', 160)))
            check('travelled', bool(r0 and r1 and r0 != r1 and to and r1 == to), 'player %d: room %s at the pad, room %s after "Travel to" %s (%s)' %
                  (k, r0, r1, a.room, went['text'][:120] if went else shown(c, 'travel', 160)))


# ---- dock pair ------------------------------------------------------------------------------------------------------
class DockPair:
    """Both players dock at one station; one undocks while the other stays docked with its dock menu open: does one player's dock or undock reach the other player's screen?"""
    name = 'dockpair'
    minutes = 6
    # X4's UI hears the engine's playerUndock / playerUndocked events (menu_docked.lua closes the dock menu on the first,
    # menu_toplevel.lua opens the HUD's top level menu on the second); in a session they may be raised for one player
    # only. The player who leaves and the one who stays (--swap exchanges them).
    ROLE = {1: 'leaves', 2: 'stays'}
    STATION = '0000000000040496'          # MMJ-623, where both players dock in the trade scenario
    marks = ('menu_up', ('left', 'undocked', 'docked=false'), 'top_after')
    sets = ()
    LIMIT = 9000

    @staticmethod
    def options(s):
        s.add_argument('--station', dest='dock_at', default=DockPair.STATION, help='saved ID of the station both players dock at')

    @staticmethod
    def chain(k, a):
        leaves = DockPair.ROLE[k] == 'leaves'
        return [
            ('dock', 'until', '^docked', 'dock_station', a.dock_at, 9000),
            ('menu_up', 'poll', 'shown=true', 10, 900, 'menu_look', 'DockedMenu'),     # X4 opens it a few seconds after the docking
            # ...and keeps it open while the ship is docked: read every 10 ticks, through the other player's docking (the one
            # who leaves waits there) and its undocking too (the one who stays); a row 'shown=false' is the tick it closed
            ('held', 'try', 'shown=false', 10, 1500 if leaves else 3000, 'menu_look', 'DockedMenu'),
            ('top_docked', 'op', 'menu_look', 'TopLevelMenu'),
            ('undock_ready', 'poll', '^granted', 10, 900, 'undock_check'),
            ('undock', 'op', 'undock'),
            ('undocked', 'poll', 'docked=false', 30, 1800, 'foot_where'),
            ('top_wait', 'wait', 150),            # menu_toplevel.lua opens the top level menu at the engine's playerUndocked
            ('top_after', 'op', 'menu_look', 'TopLevelMenu'),
            ('dock_after', 'op', 'menu_look', 'DockedMenu'),     # menu_docked.lua closes it at the engine's playerUndock
        ] + ([('after', 'wait', 1800)] if leaves else [])

    @staticmethod
    def watch(a):
        return []

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2)]
        return [kit + [(60 + 30 * (k - 1), 'chain', DockPair.LIMIT, DockPair.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        events = {}
        for k in PLAYERS:
            c = clients[k]
            docked, undock = last_answer(c, 'dock', 'docked'), last_answer(c, 'undock')
            events[k] = {'docked': docked['tick'] if docked else None, 'undock': undock['tick'] if undock else None}
        for k in PLAYERS:
            c, other = clients[k], 3 - k
            check('docked', events[k]['docked'] is not None, 'player %d (%s): %s' % (k, DockPair.ROLE[k], 'tick %d' % events[k]['docked'] if events[k]['docked'] else 'never'))
            up = last_answer(c, 'menu_up', 'shown=true')
            check('menu', bool(up), 'player %d: its dock menu opened after the docking%s' % (k, ' at tick %d' % up['tick'] if up else ': NEVER'))
            if up:
                rows = answers(c, 'held')
                closed = next((r for r in rows if 'shown=false' in r['text']), None)
                until = closed['tick'] if closed else (events[k]['undock'] or 10 ** 9)          # what the other player did meanwhile
                seen = ', '.join('player %d %s at %d' % (other, name, t) for name, t in sorted(events[other].items(), key=lambda x: x[1] or 0)
                                 if t and up['tick'] <= t <= until)
                check('kept', closed is None, "player %d's dock menu while docked (from tick %d; %s): %s" % (
                    k, up['tick'], seen or 'nothing of player %d meanwhile' % other, 'stayed open' if closed is None else 'CLOSED at tick %d' % closed['tick']))
            top, still = last_answer(c, 'top_after'), last_answer(c, 'dock_after')
            check('top', bool(top) and 'shown=true' in top['text'], "player %d: the HUD's top level menu 150 ticks after its undock: %s" % (k, top['text'][:60] if top else 'no row'))
            check('closed', bool(still) and 'shown=false' in still['text'], "player %d: its dock menu after its undock: %s" % (k, still['text'][:60] if still else 'no row'))


# ---- a plotted course ------------------------------------------------------------------------------------------------
class Guidance:
    """Each player plots a course to a station (the map's Plot course), flies it by Shift+A, then plots another and removes it."""
    name = 'guidance'
    minutes = 7
    sets = ()
    marks = ('plotted', 'flying', 'after')
    # Each player's own station, so a course that reached the wrong client shows: player 1 to the shipyard NLN-075,
    # player 2 to the food factory MMJ-623 (the trade scenario's stations, ten kilometres apart).
    TARGETS = {1: Trade.SELL, 2: Trade.BUY}
    LIMIT = 11000

    @staticmethod
    def options(s):
        s.add_argument('--fly-ticks', type=int, default=4500, help='how long the autopilot flies before the course is looked at again')

    @staticmethod
    def chain(k, a):
        t, other = Guidance.TARGETS[k], Guidance.TARGETS[3 - k]
        return [
            ('before', 'op', 'guidance_look', t),
            ('plot', 'op', 'guidance_set', t),                                      # Plot course
            ('plotted', 'poll', '^guidance=[A-Z]', 5, 300, 'guidance_look', t),
            ('autopilot', 'op', 'ap_toggle'),                                      # Shift+A: the autopilot to the course's target
            ('flying', 'poll', 'autopilot=true', 5, 300, 'guidance_look', t),
            # X4's guidance ends by itself within a kilometre of a station (its arrival check, now from this player)
            ('arrived', 'try', '^guidance=none', 30, a.fly_ticks, 'guidance_look', t),
            ('after', 'op', 'guidance_look', t),
            ('stop', 'op', 'ap_stop'),
            ('plot2', 'op', 'guidance_set', other),                                 # another course...
            ('plotted2', 'poll', '^guidance=[A-Z]', 5, 300, 'guidance_look', other),
            ('remove', 'op', 'guidance_end'),                                       # ...and Remove course
            ('removed', 'poll', '^guidance=none', 5, 300, 'guidance_look'),
        ]

    @staticmethod
    def watch(a):
        return [('ship_look', 1), ('ship_look', 2)]   # the ships, on both clients (a guidance itself is its client's own)

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2)]
        return [kit + [(60 + 30 * (k - 1), 'chain', Guidance.LIMIT, Guidance.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            want = {Trade.SELL: 'NLN-075', Trade.BUY: 'MMJ-623'}
            target, other = want[Guidance.TARGETS[k]], want[Guidance.TARGETS[3 - k]]
            first, plotted, flying, after = (last_answer(c, x) for x in ('before', 'plotted', 'flying', 'after'))
            check('plotted', bool(plotted) and ('guidance=%s/' % target) in plotted['text'],
                  'player %d: its own client shows the course to %s: %s' % (k, target, plotted['text'] if plotted else shown(c, 'plotted')))
            check('flying', bool(flying) and 'autopilot=true' in flying['text'],
                  'player %d: Shift+A flies the course: %s' % (k, flying['text'] if flying else shown(c, 'flying')))
            d0 = rig.field(first['text'], 'distance') if first else None
            d1 = rig.field(after['text'], 'distance') if after else None
            closer = bool(d0 and d1 and d0.isdigit() and d1.isdigit() and int(d1) < int(d0) - 1000)
            check('closer', closer, 'player %d: %s m from %s before, %s m after the flight' % (k, d0, target, d1))
            arrived = last_answer(c, 'arrived', 'guidance=none')
            check.note('arrival', 'player %d: %s' % (k, 'the course ended by itself at tick %d (%s)' % (arrived['tick'], arrived['text']) if arrived else
                       'still on at the look (%s)' % (after['text'] if after else '?')))
            plotted2, removed = last_answer(c, 'plotted2'), last_answer(c, 'removed', 'guidance=none')
            check('second', bool(plotted2) and ('guidance=%s/' % other) in plotted2['text'],
                  'player %d: a second course, to %s: %s' % (k, other, plotted2['text'] if plotted2 else shown(c, 'plotted2')))
            check('removed', bool(removed), 'player %d: Remove course %s' % (k, 'cleared it at tick %d' % removed['tick'] if removed else 'left it (%s)' % shown(c, 'removed')))


# ---- the HUD's Ship Interactions -----------------------------------------------------------------------------------
class Interact:
    """Each player opens Ship Interactions in flight from the HUD's top level bar, presses its Flight Assist button, opens
    it again from the map's tabs, goes to Ship Information and Back to it, and presses Flight Assist again. Player 1
    first, then player 2; meanwhile the other client's dock menu must stay closed (each player's menu is its own)."""
    name = 'interact'
    minutes = 3
    sets = ()
    marks = ('open', 'open2', 'open3')
    OPEN = r'^shown=true mode=cockpit .* own=true'
    LIMIT = 4000
    QUIET = 1200          # ticks the other player's client watches its own dock menu stay closed

    @staticmethod
    def options(s):
        pass

    @staticmethod
    def chain(k, a):
        uses = [
            ('top', 'op', 'menu_open', 'TopLevelMenu'),                             # the HUD's bar (open in flight anyway)
            ('top_up', 'poll', '^shown=true', 5, 150, 'menu_look', 'TopLevelMenu'),
            ('assist_start', 'op', 'pilot_look'),
            ('tab', 'op', 'toplevel_tab', 'cockpit'),                              # click: Ship Interactions
            ('open', 'poll', Interact.OPEN, 5, 300, 'menu_look', 'DockedMenu'),
            ('assist', 'op', 'menu_call', 'DockedMenu', 'buttonFlightAssist'),     # its Flight Assist (the menu closes)
            ('assist_off', 'try', 'assist=false', 10, 300, 'pilot_look'),
            ('closed', 'poll', '^shown=false', 5, 300, 'menu_look', 'DockedMenu'),
            ('map', 'op', 'menu_open', 'MapMenu'),
            ('map_up', 'poll', '^shown=true', 5, 300, 'menu_look', 'MapMenu'),
            ('maptab', 'op', 'toplevel_tab', 'cockpit', 'MapMenu', True),         # the map's tabs: Ship Interactions
            ('open2', 'poll', Interact.OPEN, 5, 300, 'menu_look', 'DockedMenu'),
            ('info', 'op', 'menu_call', 'DockedMenu', 'buttonShipInfo'),          # Ship Information: the map on the own ship
            ('info_up', 'poll', '^shown=true', 5, 300, 'menu_look', 'MapMenu'),
            ('back', 'op', 'menu_back', 'MapMenu'),                                # Back: to Ship Interactions
            ('open3', 'poll', Interact.OPEN, 5, 300, 'menu_look', 'DockedMenu'),
            ('assist_back', 'op', 'menu_call', 'DockedMenu', 'buttonFlightAssist'),
            ('assist_on', 'try', 'assist=true', 10, 300, 'pilot_look'),
            ('end', 'poll', '^shown=false', 5, 300, 'menu_look', 'DockedMenu'),
        ]
        quiet = ('other', 'try', '^shown=true', 10, Interact.QUIET, 'menu_look', 'DockedMenu')   # must give up
        return [('before', 'wait', 300)] + (uses + [quiet] if k == 1 else [quiet] + uses)

    @staticmethod
    def watch(a):
        return [('ship_look', 1), ('ship_look', 2)]

    @staticmethod
    def plan(a):
        return [[(60 + 30 * (k - 1), 'chain', Interact.LIMIT, Interact.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]
            for step, want in (('tab', 'pressed opens=DockedMenu'), ('maptab', 'pressed opens=DockedMenu'), ('back', 'back to=DockedMenu')):
                row = last_answer(c, step)
                check(step, bool(row) and row['text'] == want, 'player %d: %s answered %s' % (k, step, row['text'] if row else shown(c, step)))
            for step, how in (('open', "the HUD's top level bar"), ('open2', "the map's tabs"), ('open3', 'Back from Ship Information')):
                row = held(c, step, Interact.OPEN)
                check('opened', bool(row), "player %d: Ship Interactions for its own ship from %s: %s" % (k, how, row['text'] if row else shown(c, step)))
            for step, pattern, did in (('assist_off', 'assist=false', 'its Flight Assist button turned flight assist off'),
                                       ('assist_on', 'assist=true', 'and on again')):
                row = held(c, step, pattern)
                check('assist', bool(row), 'player %d: %s' % (k, '%s at tick %d' % (did, row['tick']) if row else 'not done: %s (%s)' % (did, shown(c, step))))
            other = answers(c, 'other')
            seen = [r for r in other if r['ok'] and r['text'].startswith('shown=true')]
            quiet = bool(other) and not seen and other[-1]['text'] == 'gave_up'
            check('own', quiet, "player %d's client kept its dock menu closed while the other player used its own (%s)" % (
                k, 'watched %d ticks' % Interact.QUIET if quiet else seen[0]['text'] if seen else 'not watched: ' + shown(c, 'other')))


# ---- a faction representative's blueprints and licences ---------------------------------------------------------------
class Reps:
    """Each player docks at a faction's headquarters, comms the faction's representative, buys a blueprint and then a
    licence through the conversation's shop (BlueprintOrLicenceTraderMenu), and ends the conversation."""
    name = 'reps'
    minutes = 10
    sets = ()
    marks = ('bp_bought', 'lic_bought', 'bye_look')
    FACTION = 'argon'
    # The representative has its shop only while X4 has given it an office on the headquarters (md/npc_factionrepresentative.xml
    # CreateFactionRepresentativeInterior: when the headquarters comes near a player); from afar its conversation is the
    # generic one ("Where can I find...?", "Goodbye": run 20261121-184129). The Argon headquarters is the shipyard NLN-075 of
    # the snapshot's sector.
    HQ = Crew.YARD
    BLUEPRINTS, LICENCES = 2, 1      # the conversation's choices: 'I would like to buy a blueprint' (left), 'Show me your licences' (top left)
    LIMIT = 16000

    @staticmethod
    def options(s):
        s.add_argument('--faction', default=Reps.FACTION, help="the faction whose representative each player talks to")
        s.add_argument('--hq', default=Reps.HQ, help="saved ID of the faction's headquarters each player docks at first")
        s.add_argument('--no-dock', dest='dock', action='store_false', help='talk by comm from where the players start (no office, no shop)')
        s.add_argument('--players', default='1,2', help="the players who take part, e.g. '2': player 2 alone at the headquarters, player 1 stays out")
        s.add_argument('--credits', type=int, default=20000000, help='money the fixture adds to the account before the run')
        s.add_argument('--no-licence', dest='licence', action='store_false', help='buy only a blueprint')

    @staticmethod
    def who(a):
        return [int(x) for x in str(a.players).split(',') if x.strip()]

    @staticmethod
    def shop(k, a, tag, choice):
        """One visit of the shop from the conversation: the choice, the menu on this player's screen, a pick, Confirm."""
        return [
            (tag + '_choice', 'op', 'dialog', choice),
            (tag + '_menu', 'try', 'shown=BlueprintOrLicenceTraderMenu', 5, 300, 'trader_look'),
            (tag + '_before', 'op', 'library_look', a.faction),
            (tag + '_bought', 'op', 'trader_pick', 1),
            (tag + '_wait', 'wait', 150),
            (tag + '_after', 'op', 'library_look', a.faction),
            (tag + '_back', 'try', 'shown=none', 5, 300, 'trader_look'),
            (tag + '_back_wait', 'wait', 60),
        ]

    @staticmethod
    def chain(k, a):
        # X4 has one conversation at a time: player 1 talks first, player 2 when it is free again
        dock = [
            ('rep_far', 'op', 'rep_look', a.faction),
            ('dock', 'until', '^docked', 'dock_station', a.hq, 9000),
            ('settle', 'wait', 240),
        ] if a.dock else []
        steps = dock + [
            ('rep_near', 'op', 'rep_look', a.faction),
            ('stagger', 'wait', 1 + 300 * (k - 1)),
            ('free', 'poll', 'owner=0 .*manager=1%.0 ', 30, 9000, 'conversation_look'),
            ('start', 'op', 'library_look', a.faction),
            ('talk', 'op', 'rep_talk', a.faction),
            ('owned', 'try', 'owner=%d ' % k, 5, 150, 'conversation_look'),
            ('talk_wait', 'wait', 90),
        ] + Reps.shop(k, a, 'bp', Reps.BLUEPRINTS)
        if a.licence:
            steps += Reps.shop(k, a, 'lic', Reps.LICENCES)
        return steps + [
            ('bye', 'op', 'conv_cancel'), ('bye_wait', 'wait', 60), ('bye_look', 'op', 'conversation_look'),
            # the other player's conversation held back on this screen too, its dialogs and its shop (it may come later)
            ('screen_end', 'try', 'dialogs=[1-9]%d*/[1-9]%d* menus=[1-9]%d*/[1-9]', 30, 4800, 'conversation_look'),
            ('end', 'op', 'library_look', a.faction),
        ]

    @staticmethod
    def watch(a):
        return [('library_look', a.faction), ('rep_look', a.faction)]

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', a.credits)]
        return [kit + ([(60 + 30 * (k - 1), 'chain', Reps.LIMIT, Reps.chain(k, a) if k in Reps.who(a) else [('idle', 'wait', 30)])]) for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        def num(text, name):
            v = rig.field(text or '', name)
            return int(v) if v and v.lstrip('-').isdigit() else None
        def held(c, name, pattern):
            rows = [x for x in answers(c, name) if x['ok'] and re.search(pattern, x['text'])]
            return rows[-1] if rows else None
        for k in PLAYERS:
            if k not in Reps.who(a):
                check.note('out', 'player %d takes no part (--players %s)' % (k, a.players))
                continue
            c = clients[k]
            if a.dock:
                docked = last_answer(c, 'dock', 'docked')
                check('docked', bool(docked), 'player %d at the headquarters: %s' % (k, 'tick %d' % docked['tick'] if docked else shown(c, 'dock')))
            far, near = last_answer(c, 'rep_far'), last_answer(c, 'rep_near')
            check.note('office', 'player %d: the representative before the dock: %s; after: %s' % (k, far['text'] if far else '-', near['text'] if near else '-'))
            mine = held(c, 'owned', r'owner=%d ' % k)
            check('talked', bool(mine), 'player %d: %s (talk %s)' % (k, "the conversation was this player's from tick %d" % mine['tick'] if mine else
                  "the conversation was not this player's (%s)" % shown(c, 'owned'), shown(c, 'talk')))
            visits = [('bp', 'blueprints', 'blueprint')] + ([('lic', 'licences', 'licence')] if a.licence else [])
            for tag, count, mode in visits:
                menu = last_answer(c, tag + '_menu', 'shown=BlueprintOrLicenceTraderMenu')
                check(tag + '_menu', bool(menu) and ('mode=%s ' % mode) in menu['text'],
                      "player %d: the %s shop on its own screen: %s" % (k, mode, menu['text'] if menu else shown(c, tag + '_menu')))
                bought = last_answer(c, tag + '_bought', 'bought')
                none = last_answer(c, tag + '_bought', 'nothing_to_buy offers=0')
                if none and menu:
                    # the shop showed, but X4 lets this player buy nothing there (a licence needs its faction's standing
                    # or the licence before it: the game's rule, Select greyed out); Cancel closed it
                    check.note(tag + '_bought', 'player %d: the shop offers nothing this player may buy (%s)' % (k, none['text']))
                    back = last_answer(c, tag + '_back', 'shown=none')
                    check(tag + '_closed', bool(back), 'player %d: the shop closed by its Cancel (%s)' % (k, back['text'] if back else shown(c, tag + '_back')))
                    continue
                check(tag + '_bought', bool(bought), 'player %d: %s' % (k, bought['text'] if bought else 'nothing bought (%s)' % shown(c, tag + '_bought')))
                b0, b1 = (last_answer(c, tag + x) for x in ('_before', '_after'))
                n0, n1 = num(b0 and b0['text'], count), num(b1 and b1['text'], count)
                m0, m1 = num(b0 and b0['text'], 'money'), num(b1 and b1['text'], 'money')
                check(tag + '_held', n0 is not None and n1 is not None and n1 > n0,
                      'player %d: %s %s -> %s, money %s -> %s' % (k, count, n0, n1, m0, m1))
                if bought and m0 is not None and m1 is not None:
                    price = num(bought['text'], 'price')
                    check.note(tag + '_paid', 'player %d: paid %s for a price of %s (one wallet for both players in coop)' % (k, m0 - m1, price))
                back = last_answer(c, tag + '_back', 'shown=none')
                check(tag + '_closed', bool(back), 'player %d: the shop closed after Confirm (%s)' % (k, back['text'] if back else shown(c, tag + '_back')))
            look = last_answer(c, 'screen_end') or last_answer(c, 'bye_look')
            screen = re.search(r'dialogs=(\d+)/(\d+) menus=(\d+)/(\d+)', look['text'] if look else '')
            if screen:
                ds, dh, ms, mh = (int(x) for x in screen.groups())
                check('screen', ds > 0 and dh > 0 and ms > 0 and mh > 0,
                      "player %d's client: dialogs shown %d, held back %d; conversation menus shown %d, held back %d" % (k, ds, dh, ms, mh))


# ---- an item trader on a station's dock ------------------------------------------------------------------------------
class ItemTrader:
    """Each player docks at a station, comms an item trader of its docks (a platform actor), opens 'Show me your
    wares' (InventoryTraderMenu), buys one piece of the cheapest ware the trader holds and ends the conversation."""
    name = 'itemtrader'
    minutes = 10
    sets = ()
    marks = ('bought', 'after', 'bye_look')
    STATION = Trade.BUY
    LIMIT = 16000
    INVENTORY = 1                   # the trader's choice 'Show me your wares' (top left)

    @staticmethod
    def options(s):
        s.add_argument('--station', default=ItemTrader.STATION, help='saved ID of the station both players dock at')
        s.add_argument('--type', default='trader', help="the person's type: 'trader' or 'shadyguy'")
        s.add_argument('--players', default='1,2', help="the players who take part, e.g. '2'")
        s.add_argument('--credits', type=int, default=2000000, help='money the fixture adds to the account before the run')

    @staticmethod
    def who(a):
        return [int(x) for x in str(a.players).split(',') if x.strip()]

    @staticmethod
    def chain(k, a):
        return [
            ('dock', 'until', '^docked', 'dock_station', a.station, 9000),
            ('settle', 'wait', 300),
            ('stagger', 'wait', 1 + 300 * (k - 1)),
            ('free', 'poll', 'owner=0 .*manager=1%.0 ', 30, 9000, 'conversation_look'),
            ('before', 'op', 'inv_total'),
            ('talk', 'poll', 'name=', 60, 1800, 'npc_talk', a.station, a.type),
            ('owned', 'try', 'owner=%d ' % k, 5, 150, 'conversation_look'),
            ('talk_wait', 'wait', 90),
            ('choice', 'op', 'dialog', ItemTrader.INVENTORY),
            ('menu', 'try', 'shown=InventoryTraderMenu', 5, 300, 'trader_look'),
            ('bought', 'op', 'itemtrader_buy', 1),
            ('wait', 'wait', 150),
            ('after', 'op', 'inv_total'),
            ('back', 'try', 'shown=none', 5, 300, 'trader_look'),
            ('bye', 'op', 'conv_cancel'), ('bye_wait', 'wait', 60), ('bye_look', 'op', 'conversation_look'),
        ]

    @staticmethod
    def watch(a):
        return []

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', a.credits)]
        return [kit + [(60 + 30 * (k - 1), 'chain', ItemTrader.LIMIT, ItemTrader.chain(k, a) if k in ItemTrader.who(a) else [('idle', 'wait', 30)])]
                for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        def held(c, name, pattern):
            rows = [x for x in answers(c, name) if x['ok'] and re.search(pattern, x['text'])]
            return rows[-1] if rows else None

        def num(text, name):
            v = rig.field(text or '', name)
            return int(v) if v and v.lstrip('-').isdigit() else None
        for k in PLAYERS:
            if k not in ItemTrader.who(a):
                check.note('out', 'player %d takes no part (--players %s)' % (k, a.players))
                continue
            c = clients[k]
            docked = last_answer(c, 'dock', 'docked')
            check('docked', bool(docked), 'player %d at the station: %s' % (k, 'tick %d' % docked['tick'] if docked else shown(c, 'dock')))
            talk = held(c, 'talk', r'name=')
            check('trader', bool(talk), 'player %d: %s' % (k, 'talks to %s' % talk['text'] if talk else 'no %s on the station (%s)' % (a.type, shown(c, 'talk'))))
            mine = held(c, 'owned', r'owner=%d ' % k)
            check('talked', bool(mine), 'player %d: %s' % (k, "the conversation was this player's" if mine else "not this player's (%s)" % shown(c, 'owned')))
            menu = held(c, 'menu', r'^shown=InventoryTraderMenu')
            check('menu', bool(menu), "player %d: the trader's wares on its own screen: %s" % (k, menu['text'] if menu else shown(c, 'menu')))
            bought = held(c, 'bought', r'^bought')
            check('bought', bool(bought), 'player %d: %s' % (k, bought['text'] if bought else 'nothing bought (%s)' % shown(c, 'bought')))
            b0, b1 = last_answer(c, 'before'), last_answer(c, 'after')
            n0, n1 = num(b0 and b0['text'], 'pieces'), num(b1 and b1['text'], 'pieces')
            m0, m1 = num(b0 and b0['text'], 'money'), num(b1 and b1['text'], 'money')
            check('inventory', n0 is not None and n1 == n0 + 1,
                  "player %d's own inventory: %s -> %s pieces, money %s -> %s" % (k, n0, n1, m0, m1))
            back = held(c, 'back', r'^shown=none')
            check('closed', bool(back), 'player %d: the menu closed after Confirm (%s)' % (k, back['text'] if back else shown(c, 'back')))


# ---- a station's control panel ----------------------------------------------------------------------------------------
class Hack:
    """Each player docks at a station, gets up, walks out of its ship onto the station and hacks a control panel of the
    station's Security Office (the question's Yes: StartControlPanelHack) with a Security Decryption System from its
    own inventory. The room is found by the dock's transporter (its target 'Security Office'); the player does not walk
    there (the walk to a transporter pad is x4mods-ed's open Rooms work), and X4's hack does not ask where the player
    stands, only that it is on the station. The transporter's 'Security Office' is the interior's corridor; the panels
    are connections of the office room beside it (room_gen_securityoffice_01: connection32 hack_watchdogs, 34
    hack_all_turrets, 37 hack_shieldgenerators, 40 hack_engineer), found by --office among the room IDs next to the
    corridor's. Passed for player 2 alone (run 20261121-211253)."""
    name = 'hack'
    minutes = 8
    sets = ()
    marks = ('out', 'hack_wait', 'panels_after')
    STATION = '00000000000434b1'     # CZF-533, an Argon factory: its transporters offer a Security Office (x4mods-ed)
    GET_UP, BACK = 0x115, 27         # INPUT_ACTION_GET_UP; FP_MOVE_BACKWARD (a docked small ship is left backwards)
    TYPES = {1: 'hack_shieldgenerators', 2: 'hack_watchdogs'}
    WARE = 'inv_securitydecryptionsystem'
    LIMIT = 13000

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Hack.STATION, help='saved ID of the station to dock at')
        s.add_argument('--room', default='Security', help="the room with the panels, by a part of its transporter name")
        s.add_argument('--office', default='securityoffice', help="the panels' room beside it, by a part of its macro ('' : the transporter's room)")
        s.add_argument('--players', default='1,2', help="the players who take part, e.g. '2'")

    @staticmethod
    def who(a):
        return [int(x) for x in str(a.players).split(',') if x.strip()]

    # the dock's transporter slots (x4mods-ed: each bay's own is <bay>/connection27) asked from where the player stands
    sets = ("native_gate_transport_connection={'connection27','connection02'}",)

    @staticmethod
    def chain(k, a):
        return [
            ('dock', 'until', '^docked', 'dock_station', a.station, 9000),
            ('settle', 'wait', 240),
            ('up', 'op', 'input', 'action', Hack.GET_UP, 0),
            ('up_wait', 'wait', 150),
            ('seat', 'op', 'foot_where'),
            ('back_on', 'op', 'input', 'state', Hack.BACK, 1),
            ('back', 'wait', 200),
            ('back_off', 'op', 'input', 'state', Hack.BACK, 0),
            ('out', 'op', 'foot_where'),
            ('targets', 'op', 'transport_list'),
            ('panels', 'op', 'panel_find', a.room, a.office),
            ('inv_before', 'op', 'inv_read', Hack.WARE),
            ('hack', 'op', 'hack_yes', Hack.TYPES[k], a.room, a.office),
            ('hack_wait', 'wait', 150),
            ('inv_after', 'op', 'inv_read', Hack.WARE),
            ('panels_after', 'op', 'panel_find', a.room, a.office),
        ]

    @staticmethod
    def watch(a):
        return [('inv_look', k, Hack.WARE) for k in Hack.who(a)]

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2)] + [(45, 'inv_seed', k, Hack.WARE, 3) for k in Hack.who(a)]
        return [kit + [(60 + 30 * (k - 1), 'chain', Hack.LIMIT, Hack.chain(k, a) if k in Hack.who(a) else [('idle', 'wait', 30)])]
                for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        def num(text, name):
            v = rig.field(text or '', name)
            return int(v) if v and v.lstrip('-').isdigit() else None
        for k in PLAYERS:
            if k not in Hack.who(a):
                check.note('out', 'player %d takes no part (--players %s)' % (k, a.players))
                continue
            c, kind = clients[k], Hack.TYPES[k]
            docked = last_answer(c, 'dock', 'docked')
            check('docked', bool(docked), 'player %d at the station: %s' % (k, 'tick %d' % docked['tick'] if docked else shown(c, 'dock')))
            check.note('targets', 'player %d: %s' % (k, shown(c, 'targets', 600)))
            out = last_answer(c, 'out')
            check('out', bool(out), 'player %d out of its ship: %s' % (k, out['text'][:200] if out else shown(c, 'out')))
            panels = last_answer(c, 'panels')
            check('panels', bool(panels) and kind in panels['text'], 'player %d: %s' % (k, panels['text'] if panels else shown(c, 'panels')))
            hack = last_answer(c, 'hack', 'asked')
            check('asked', bool(hack), 'player %d: %s' % (k, hack['text'] if hack else shown(c, 'hack')))
            w0 = num((last_answer(c, 'inv_before') or {}).get('text'), Hack.WARE)
            w1 = num((last_answer(c, 'inv_after') or {}).get('text'), Hack.WARE)
            check('ware', w0 is not None and w1 == w0 - 1, "player %d's own %s: %s -> %s" % (k, Hack.WARE, w0, w1))
            after = last_answer(c, 'panels_after')
            check.note('panels_after', 'player %d: %s' % (k, after['text'][:300] if after else shown(c, 'panels_after')))
            # the hack applied on every client: the ware left the player's own inventory in its scope, on both clients
            # alike (the readings), and each client's compslot status counted the hack applied
            mine = [r for r in c['ops'].get('inv_look', []) if r[2].startswith('slot=%d ' % k)]
            first, last = (mine[0][2] if mine else ''), (mine[-1][2] if mine else '')
            check('used', bool(mine) and '=3' in first.split()[-1] and first.split()[-1].endswith('=3') and last.split()[-1].endswith('=2'),
                  "player %d's decryption systems read in its scope: first %s, last %s" % (k, first[-60:], last[-60:]))


SCENARIOS = {s.name: s for s in (Trade, Orders, Orders2, Crew, ShipBuy, Refit, RefitOrder, Repair, Wing, ShipSell, CrewMove, FleetTrade, Deploy, Rules, Systems, Settings, Storage, Exchange, Hostile, TradeLoop, Claim, Mining, WingSet, Routines, DockPair, MapOpen, Guidance, Interact, Reps, ItemTrader, Hack, Rooms)}


def ai_docks(plan):
    """--ai-dock: every chain's dock_station step docks the player's own ship by X4's own AI instead (the script op
    ai_dock_station: the autopilot to the station, then X4's dock_player_ship_at, which runs the fleet ships' dock order
    on the ship computer), with the same station and tick limit; the autopilot's detour and hop are left out."""
    def step(s):
        if len(s) >= 5 and s[1] == 'until' and s[3] == 'dock_station':
            return tuple(s[:3]) + ('ai_dock_station', s[4], s[5] if len(s) > 5 else 3000)
        return s
    return [[(e[0], e[1], e[2], [step(s) for s in e[3]]) + tuple(e[4:]) if len(e) >= 4 and e[1] == 'chain' else e for e in client]
            for client in plan]


def ai_dock_note(check, clients, logs):
    """--ai-dock: what each player's AI docks answered, and what the MD cue AiDock did on each client (the ship it
    docked must be the asking player's on both)."""
    for k in PLAYERS:
        rows = ['%s: %s' % (name, r['text'][:140]) for name, got in clients[k]['steps'].items() for r in got
                if r['text'].startswith(('ai_dock', 'autopilot_stop')) or (r['text'].startswith('docked after=') and 'by=ai' in r['text'])]
        check.note('ai_dock', 'player %d: %s' % (k, ' / '.join(rows) or 'no AI dock row'))
    md_note(check, logs, 'UI_REPLAY_AI_DOCK', 'ai_dock_md')


def media_note(check, logs):
    """Each client's last MD_MEDIA row: conversation pictures taken over (conv=installed/adds) and what its monitor was
    not told (ui=starts/none/stops/releases/hidden/shown: hidden = another player's picture)."""
    for n, path in enumerate(logs, 1):
        rows = [x for x in rig.read_lines(Path(path)) if 'MD_MEDIA tick=' in x]
        last = rows[-1] if rows else ''
        conv, ui = re.search(r' conv=(\S+)', last), re.search(r' ui=(\S+)', last)
        check.note('media', 'client %d: conv=%s ui=%s' % (n, conv.group(1) if conv else '-', ui.group(1) if ui else '-'))


def md_note(check, logs, marker, name):
    """The MD rows with that marker on each client, as a note."""
    for n, path in enumerate(logs, 1):
        md = [re.sub(r'^.*?' + marker, marker, x.strip())[:200] for x in rig.read_lines(Path(path)) if marker in x]
        check.note(name, 'client %d: %s' % (n, ' / '.join(md) or 'no %s row' % marker))


def swap_targets(scenario, args):
    """--swap: player 1 gets what the scenario gives player 2 (its fleet ships, stations, targets: the scenario's
    {1: ..., 2: ...} tables, exchanged in place for this process) and player 2 player 1's; full_plan then starts player
    2's chain first. Beside a plain run it tells a difference between the players that stays with the player (the
    session's) from one that moves with the ship or the place (the world's). The names of what was exchanged."""
    swapped, done = [], set()
    for klass in scenario.__mro__:
        for name, value in vars(klass).items():
            if isinstance(value, dict) and set(value) == set(PLAYERS) and id(value) not in done:
                done.add(id(value))
                value[1], value[2] = value[2], value[1]
                swapped.append(name)
    if hasattr(args, 'station') and hasattr(args, 'station2'):          # crew: the stations each player hires at
        args.station, args.station2 = args.station2, args.station
        swapped.append('--station/--station2')
    return swapped


def full_plan(scenario, args, minutes=None):
    """The scenario's plan per client with the watch's readings added to each."""
    readings = watch_rows(scenario.watch(args), minutes or getattr(args, 'minutes', None) or scenario.minutes)
    plan = scenario.plan(args)
    if getattr(args, 'swap', False):          # player 2's chain starts when player 1's would have, and the other way round
        starts = [[e[0] for e in client if len(e) > 1 and e[1] == 'chain'] for client in plan]
        if all(len(x) == 1 for x in starts):
            plan = [[(starts[1 - i][0],) + tuple(e[1:]) if len(e) > 1 and e[1] == 'chain' else e for e in client] for i, client in enumerate(plan)]
    for k, ticks in (getattr(args, 'delay', None) or []):          # --delay K:TICKS: player K's chain starts that much later
        plan = [[(e[0] + ticks,) + tuple(e[1:]) if i == k - 1 and len(e) > 1 and e[1] == 'chain' else e for e in client] for i, client in enumerate(plan)]
    if getattr(args, 'ai_dock', False):
        plan = ai_docks(plan)
    return [list(client) + readings for client in plan]


def delay_value(text):
    """--delay K:TICKS -> (K, TICKS)."""
    k, _, ticks = text.partition(':')
    if int(k) not in PLAYERS or int(ticks) < 0:
        raise argparse.ArgumentTypeError('K:TICKS with K a player (1 or 2) and TICKS >= 0')
    return int(k), int(ticks)


def report(scenario, logs, run_dir, args):
    clients = {k: parse(path) for k, path in zip(PLAYERS, logs)}
    check = Checks()
    common_checks(check, clients, logs, run_dir)
    scenario.report(check, clients, args)
    if getattr(args, 'ai_dock', False):
        ai_dock_note(check, clients, logs)
    if scenario is Guidance:   # what the guidance script heard for players 2..N, on each client (native_gate_player_guidance.xml)
        md_note(check, logs, 'GUIDANCE_OWNER', 'guidance_md')
    if scenario is Rooms:      # the transporters MD found around each player, on each client
        md_note(check, logs, 'UI_REPLAY_TRANSPORTER slot=', 'transporter_md')
        md_note(check, logs, 'FOOT_AVATAR_GUIDE', 'guide_md')
        md_note(check, logs, 'FOOT_AVATAR_HELD', 'held_md')
        md_note(check, logs, 'FOOT_AVATAR_GUIDE_TRY', 'guide_try_md')
    if scenario is Interact:   # each client's dock menu opens: a person's (via=tab|back) and the automatic ones it kept out
        md_note(check, logs, 'UI_DOCKED_MENU', 'dock_menu')
    if scenario in (Reps, ItemTrader, Crew):   # the conversation's picture on its owner's monitor only (lockstep_md_media.h talk)
        media_note(check, logs)
    watch_check(check, clients, scenario.watch(args))
    sync_check(check, logs, run_dir, need_capture=True)
    return check.failed


def run(scenario, args):
    env = dict(os.environ)
    if args.first_profile is not None:
        env['X4LS_FIRST_PROFILE'] = str(args.first_profile)
        os.environ['X4LS_FIRST_PROFILE'] = str(args.first_profile)
    sys.path.insert(0, str(ROOT / 'src'))
    from lockstep.launcher import profile          # after X4LS_FIRST_PROFILE: it names the profiles of clients 1 and 2
    if (ROOT / '.local/x4ls/session.json').exists():
        print('this checkout has a launcher session recorded: run `python tools/x4ls.py clean` first')
        return 2
    minutes = args.minutes or scenario.minutes
    work = ROOT / '.local/action-tests'
    work.mkdir(parents=True, exist_ok=True)
    overlay = work / 'overlay'
    for stale in (overlay, Path(str(overlay) + '.base')):
        if stale.exists():
            import shutil
            shutil.rmtree(stale)
    # Every world capture with its per-object rows (about 11 MB a capture in each client's log), so a run whose world
    # differs names the objects; and the runtime ID allocations of every tick by caller (RUNTIME_IDS_ROW), which tell a
    # body built a tick apart from a flight spline computed on one client only. A --set of the same key replaces ours.
    sets = ['native_gate_ui_replay_script=' + rig.lua(full_plan(scenario, args, minutes)), 'native_gate_digest_parts={1,90000}']
    if not args.no_pages:
        sets += ['native_gate_world_pages_window={1800,%d}' % (minutes * 1800), 'native_gate_created_rows=true', 'native_gate_runtime_ids_rows=true']
    for more in (list(scenario.sets) + list(getattr(scenario, 'more_sets', lambda a: [])(args)), args.set):   # the scenario's keys replace ours, the caller's replace both
        given = {x.split('=', 1)[0].strip() for x in more}
        sets = [x for x in sets if x.split('=', 1)[0].strip() not in given] + more
    made = subprocess.run([sys.executable, str(ROOT / 'tools/build_play_overlay.py'), '--snapshot', str(args.snapshot), '--gate-dll', str(args.gate_dll),
                           '--out', str(overlay), '--players', '2', '--mode', 'coop', '--minutes', str(minutes), '--save-every', '0'] +
                          [x for extra in sets for x in ('--set', extra)], cwd=ROOT, env=env, capture_output=True, text=True)
    (work / 'overlay.log').write_text(made.stdout + made.stderr, encoding='utf-8')
    if made.returncode:
        print('the overlay was not built (%s):\n%s' % (work / 'overlay.log', (made.stdout + made.stderr)[-600:]))
        return 2
    before = set((ROOT / '.local/runs').glob('2*')) if (ROOT / '.local/runs').exists() else set()
    command = [sys.executable, str(ROOT / 'tools/x4ls.py'), 'play', '--clients', '2', '--overlay', str(overlay), '--snapshot', str(args.snapshot),
               '--size', args.size, '--never-focus']
    for name in ('display', 'tile_skip', 'lanes'):
        if getattr(args, name) is not None:
            command += ['--' + name.replace('_', '-'), str(getattr(args, name))]
    host_log = open(work / 'host.log', 'w', encoding='utf-8')
    host = subprocess.Popen(command, cwd=ROOT, env=env, stdout=host_log, stderr=subprocess.STDOUT)
    logs = [profile.profile_dir(profile.PROFILES[k]) / ('x4ls-c%d.log' % (k + 1)) for k in range(2)]
    outcome, run_dir, started, pids, shots = 2, None, None, [], set()
    try:
        deadline = time.time() + 420 + minutes * 60
        while time.time() < deadline:
            time.sleep(3)
            if run_dir is None:
                fresh = sorted(set((ROOT / '.local/runs').glob('2*')) - before) if (ROOT / '.local/runs').exists() else []
                run_dir = fresh[-1] if fresh else None
            errors = [x for x in rig.read_lines(run_dir / 'errors.log') if x.strip()] if run_dir else []
            if host.poll() is not None or (errors and started is None):
                print('the session did not get going%s; see %s' % (': ' + errors[0].strip()[:300] if errors else '', work / 'host.log'))
                break
            if run_dir and started is None and re.search(r'^NATIVE_GATE tick=\d{3,} ', '\n'.join(rig.read_lines(run_dir / 'relay.log')[-3:]), re.M):
                started = time.time()
                pids = [int(x) for x in re.findall(r'client \d+: pid (\d+)', (work / 'host.log').read_text(encoding='utf-8', errors='replace'))]
                print('the clients step (run %s, pids %s): scenario %s' % (run_dir.name, pids, scenario.name), flush=True)
                rig.below_normal(pids)
            if not started:
                continue
            clients = {k: parse(path) for k, path in zip(PLAYERS, logs)}
            for k in PLAYERS:          # a picture of both windows when a marked step of a player has answered
                for mark in tuple(scenario.marks) + tuple(getattr(args, 'mark', None) or ()):
                    # a mark is a step's name, or (picture name, step, pattern): the first row of that step that matches
                    label, step, pattern = (mark, mark, None) if isinstance(mark, str) else mark
                    rows = clients[k]['steps'].get(step) or []
                    if (k, label) not in shots and any(pattern is None or re.search(pattern, r['text']) for r in rows):
                        shots.add((k, label))
                        (run_dir / 'shots').mkdir(exist_ok=True)
                        for c, pid in enumerate(pids, 1):
                            try:
                                rig.window_shot(pid, run_dir / 'shots' / ('p%d-%s-client%d.png' % (k, label, c)))
                            except (OSError, rig.ctypes_error) as error:      # a picture is extra: never the run's end
                                print('no picture of client %d: %s' % (c, error))
            if any(c['stops'] for c in clients.values()):
                outcome = 0
                break
            if all(c['end'] for c in clients.values()):
                # ...and on to the world capture after the last step (every CAPTURE_EVERY ticks): what the actions left
                # behind is compared between the clients there, not only the players' records on the way
                due = (max(c['end']['tick'] for c in clients.values()) // CAPTURE_EVERY + 1) * CAPTURE_EVERY + 60
                reached = [int(m.group(1)) for m in (re.search(r'^NATIVE_GATE tick=(\d+) ', line) for line in rig.read_lines(run_dir / 'relay.log')[-3:]) if m]
                if (reached and max(reached) >= due) or due > minutes * 1800 - 150:
                    outcome = 0
                    break
        if started and outcome != 0:
            print('not every step ran in time')
            outcome = 0      # the report names what is missing
        if outcome == 0:
            time.sleep(4)    # the last steps' rows and digests reach both logs
            outcome = 1 if report(scenario, logs, run_dir, args) else 0
            print('run %s; pictures in %s' % (run_dir, run_dir / 'shots'))
    finally:
        if not args.keep:
            subprocess.run([sys.executable, str(ROOT / 'tools/x4ls.py'), 'clean'], cwd=ROOT, env=env, capture_output=True, text=True)
            try:
                host.wait(timeout=30)
            except subprocess.TimeoutExpired:
                host.kill()
        host_log.close()
    return outcome


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('list')
    for name in ('run', 'report', 'plan'):
        s = sub.add_parser(name)
        each = s.add_subparsers(dest='scenario', required=True)
        for scenario in SCENARIOS.values():
            one = each.add_parser(scenario.name, help=scenario.__doc__)
            scenario.options(one)
            one.add_argument('--ai-dock', action='store_true',
                             help="the players' own ships dock by X4's own AI, as a fleet ship ordered to dock (a debug tool: "
                                  "X4's dock_player_ship_at; the autopilot flies only to a dock's approach point)")
            one.add_argument('--delay', action='append', type=delay_value, default=[], metavar='K:TICKS',
                             help="player K's chain starts TICKS later (who comes second at a shared station, menu or person)")
            one.add_argument('--swap', action='store_true',
                             help="player 1 gets player 2's ships, stations and targets and player 2 player 1's, and player 2's "
                                  "chain starts first (beside a plain run: is a difference between the players the session's or the world's)")
            if name == 'run':
                one.add_argument('--snapshot', type=Path, required=True, help='the two-player snapshot save (save_009.xml.gz)')
                one.add_argument('--gate-dll', type=Path, required=True)
                one.add_argument('--minutes', type=int, help='length of the session window (default: the scenario\'s)')
                one.add_argument('--size', default='1280x720')
                one.add_argument('--first-profile', type=int, help='X4LS_FIRST_PROFILE: beside another session, use other profiles')
                one.add_argument('--lanes', help='where this run keeps its client views (beside another session: a folder of its own)')
                one.add_argument('--display', type=int)
                one.add_argument('--tile-skip', type=int)
                one.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE')
                one.add_argument('--keep', action='store_true', help='leave the clients running after the report')
                one.add_argument('--no-pages', action='store_true', help="no per-object rows of the world captures (smaller logs; a world difference is then not named)")
                one.add_argument('--mark', action='append', default=[], metavar='STEP',
                                 help="also a picture of both windows when a player's step of that name answers (beside the scenario's own)")
            if name == 'plan':
                one.add_argument('--minutes', type=int, help='length of the session window (default: that of the scenario)')
            if name == 'report':
                one.add_argument('logs', nargs=2, type=Path)
                one.add_argument('--run', type=Path, help='the run directory (relay.log, errors.log)')
    args = p.parse_args(argv)
    if args.command == 'list':
        for scenario in SCENARIOS.values():
            print('%-10s %s' % (scenario.name, scenario.__doc__))
        return 0
    scenario = SCENARIOS[args.scenario]
    if getattr(args, 'swap', False):
        print('swapped between the players: %s; player 2 starts first' % (', '.join(swap_targets(scenario, args)) or 'nothing but the start'), flush=True)
    if args.command == 'plan':
        print(rig.lua(full_plan(scenario, args)))
        return 0
    if args.command == 'report':
        return 1 if report(scenario, args.logs, args.run, args) else 0
    return run(scenario, args)


if __name__ == '__main__':
    sys.exit(main())
