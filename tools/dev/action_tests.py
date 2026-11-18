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
  mapopen a diagnostic: one player's client opens the map and keeps it open for 100 seconds; nothing else happens.

How a scenario runs: every client's plan (native_gate_ui_replay_script) holds the fixtures (X4's docking computer on
the players' ships, the money) and one `chain` for its own player: steps that each begin when the one before has ended
(native_gate_ui_replay_core.lua K.chain), because flying and docking take as long as they take. The calls are the ones
the stock menus make, captured and replayed on every client as a person's click is; nobody clicks, so what a person
sees on the way (the menu itself, its sliders) is not covered.

What every scenario checks, from each client's own log and the relay's:
  running   no client's gate stopped and the relay saw no error
  chain     each player's chain ran to its last step
  in sync   the world, the players' records and their inputs agree at every tick (the clients' digest parts); the
            runtime ID word alone is reported as a note (see station_walk_test.py)
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


def watch_rows(ops, minutes):
    """Readings for both clients' plans: every op of `ops` at the same ticks in both, to the window's end."""
    last = int(minutes * 60 * 30) - 150
    return [(tick,) + tuple(op) for tick in range(WATCH_FROM, last, WATCH_EVERY) for op in ops]


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


def sync_check(check, logs, run_dir):
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
        check('in sync', not hard, 'world, records and inputs agree at all %d ticks both clients logged (to tick %d)' % (len(both), both[-1] if both else 0)
              if not hard else '; '.join('%s differs at %d ticks from tick %d' % (name, len(ticks), ticks[0]) for name, ticks in sorted(hard.items())))
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

    @staticmethod
    def chain(k, a):
        ship, station = Crew.SHIPS[k], (a.station if k == 1 else a.station2)
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
            ('talk', 'op', 'hire_talk', station),
            ('talk_wait', 'wait', 90),
            ('choice', 'op', 'dialog', a.choice),
            ('hire_menu', 'try', 'mode=hire', 5, 300, 'menu_look', 'MapMenu'),
            ('hire', 'op', 'hire_confirm', ship, 'service'),
            ('hired_wait', 'wait', 600),
            ('hired_look', 'op', 'ship_look', ship),
            ('money_hired', 'op', 'account'),
        ]

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
    minutes = 10
    marks = ('ordered', 'refitted')
    WHARF, PRICE = '000000000001b5af', 200000
    sets = ()
    LIMIT = 16000

    @staticmethod
    def options(s):
        s.add_argument('--station', default=Refit.WHARF, help='saved ID of the wharf or equipment dock')
        s.add_argument('--price', type=int, default=Refit.PRICE, help='credits paid for each refit')
        s.add_argument('--level', type=float, default=1.0, help="the wharf's preset: 0 minimal .. 1 high")
        s.add_argument('--lead', type=int, default=0, help='this player flies first, the other starts 50 seconds later (0: both at once)')
        s.add_argument('--credits', type=int, default=1000000, help='money the fixture adds to the account before the run')

    @staticmethod
    def chain(k, a):
        return [
            ('dock', 'until', '^docked', 'dock_station', a.station, 9000),
            ('settle', 'wait', 120),
            ('fit_start', 'op', 'fit_look', k),
            ('money_start', 'op', 'account'),
            # the dock menu's "Upgrade / repair ship", the configuration's Confirm: pay, order the refit of the docked ship
            ('order', 'op', 'build', 'refit', a.station, k, a.price, a.level),
            ('ordered', 'try', 'building=[1-9]', 10, 600, 'build_look', a.station),
            ('money_ordered', 'op', 'account'),
            ('refitted', 'try', 'building=0 queued=0', 60, 12000, 'build_look', a.station),
            ('fit_end', 'op', 'fit_look', k),
            ('undock_ready', 'poll', '^granted', 10, 900, 'undock_check'),
            ('undock', 'op', 'undock'),
            ('undocked', 'poll', 'docked=false', 30, 1800, 'foot_where'),
        ]

    @staticmethod
    def watch(a):
        return [('fit_look', 1), ('fit_look', 2), ('build_look', a.station)]

    @staticmethod
    def plan(a):
        kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', a.credits)]
        return [kit + [(60 + 30 * (k - 1) + (1500 if a.lead and k != a.lead else 0), 'chain', Refit.LIMIT, Refit.chain(k, a))] for k in PLAYERS]

    @staticmethod
    def report(check, clients, a):
        for k in PLAYERS:
            c = clients[k]

            def shown(name):
                rows = answers(c, name)
                return rows[-1]['text'][:200] if rows else 'no row'

            docked = last_answer(c, 'dock', 'docked')
            check('docked', bool(docked), 'player %d at the wharf: %s' % (k, 'tick %d' % docked['tick'] if docked else shown('dock')))
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


SCENARIOS = {s.name: s for s in (Trade, Orders, Crew, ShipBuy, Refit, MapOpen)}


def full_plan(scenario, args, minutes=None):
    """The scenario's plan per client with the watch's readings added to each."""
    readings = watch_rows(scenario.watch(args), minutes or getattr(args, 'minutes', None) or scenario.minutes)
    return [list(client) + readings for client in scenario.plan(args)]


def report(scenario, logs, run_dir, args):
    clients = {k: parse(path) for k, path in zip(PLAYERS, logs)}
    check = Checks()
    common_checks(check, clients, logs, run_dir)
    scenario.report(check, clients, args)
    watch_check(check, clients, scenario.watch(args))
    sync_check(check, logs, run_dir)
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
    for more in (list(scenario.sets), args.set):          # the scenario's keys replace ours, the caller's replace both
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
                for mark in scenario.marks:
                    if (k, mark) not in shots and clients[k]['steps'].get(mark):
                        shots.add((k, mark))
                        (run_dir / 'shots').mkdir(exist_ok=True)
                        for c, pid in enumerate(pids, 1):
                            try:
                                rig.window_shot(pid, run_dir / 'shots' / ('p%d-%s-client%d.png' % (k, mark, c)))
                            except (OSError, rig.ctypes_error) as error:      # a picture is extra: never the run's end
                                print('no picture of client %d: %s' % (c, error))
            if all(c['end'] for c in clients.values()) or any(c['stops'] for c in clients.values()):
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
    if args.command == 'plan':
        print(rig.lua(full_plan(scenario, args)))
        return 0
    if args.command == 'report':
        return 1 if report(scenario, args.logs, args.run, args) else 0
    return run(scenario, args)


if __name__ == '__main__':
    sys.exit(main())
