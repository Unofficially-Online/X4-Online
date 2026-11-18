"""Station construction test: both players of a live two-client session build a station by REAL mouse clicks in the
stock menus; the test checks that each click did in the game what it does in single player and that the clients stayed
in sync until the modules stood.

    python tools/dev/station_build_test.py run --snapshot SAVE.xml.gz --gate-dll x4lan_gate_64.dll
    python tools/dev/station_build_test.py report CLIENT1.log CLIENT2.log --run RUNDIR
    python tools/dev/station_build_test.py plan

What each player does, one after the other, in its own window (the mouse and the keyboard of the PC are taken for each
input, as a person at that window would; ui_clicks.py):
  the map (M), Manage Plots, "Create new plot", a click in the sector to place it, "Buy licence", "Continue" into the
  station menu, a module from the left list, a click to put it into the plot, "Confirm module changes", "Assign or hire
  a Builder", the player's construction vessel in the property list, "Select".
The test's fixtures (script ops in every client's plan at the same tick, done by MD on every client) give what a new
game does not have: money, the blueprints of a few modules, a construction vessel, building material in the build
storage, and they put the vessel next to the station it was sent to instead of letting it fly 100 km.

After every click the test asks that client's own menu what it now shows (the test console: a Lua file the probe loads,
native_gate_ui_replay_console) and reads both clients' logs for the replayed call. A click the menu did not take is made
again; a call that was sent is never sent twice.

What it checks:
  running   no client's gate stopped and the relay saw no error
  clicks    every step of each player's click-through showed in its menu and, where the click changes the game, as the
            same replayed call at the same tick on both clients
  planned   each station has its planned module on both clients
  built     each station's module stands, at the same reading on both clients
  readings  what both clients read of the stations at the same ticks is the same text all session long: modules, tasks,
            the build storage's wares (a station that is built a little earlier on one client shows here first)
  in sync   the world, the players' records and their inputs agree at every tick (the clients' digest parts); the
            runtime ID word alone is reported as a note (see station_walk_test.py)

The click places are fractions of a 1600x900 window at UI scale 1 (measured 17 Nov 2026 on X4 9.00); another size or
scale needs them measured again. `run` starts its own windowed clients with disposable profiles (x4ls play), beside
other sessions when --first-profile, --lanes, --display and --tile-skip keep them apart, and stops the clients it
started. Exit code: 0 all checks passed, 1 a check failed, 2 the run itself did not work.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import action_tests as at          # noqa: E402  (log parsing, the checks of every action test)
import station_walk_test as rig    # noqa: E402  (lua values, log readers, window pictures, priorities)
import ui_clicks                   # noqa: E402

PLAYERS = (1, 2)
SIZE = '1600x900'
LOOK_EVERY, WARES_EVERY, WARP_EVERY = 300, 1800, 600
LOOK = re.compile(r'UI_REPLAY_SCRIPT tick=(\d+) op=station_look ok=true result=(.*)')
CONSOLE = re.compile(r'UI_REPLAY_CONSOLE tick=(\d+) n=(\d+) ok=(\w+) result=(.*)')
STATION = re.compile(r'(\S+?)/built=(\d+)/standing=(\d+)/planned=(\d+)/tasks=(\d+)/(\d+)/builders=(\d+)/manager=(\w+)(?:/budget=(-?\d+)/stored=(\S+))?')

# What the menus answer (read only, through the test console).
MAP = ("local m=tools.menu('MapMenu') return 'map shown='..tostring(m and m.shown and true or false)..' info='..tostring(m and m.infoTableMode)"
       "..' mode='..tostring(m and m.mode)..' plot='..tostring(m and m.plotData and m.plotData.component and true or false)"
       "..' active='..tostring(m and m.plotData and m.plotData.active and true or false)")
MENU = ("local m=tools.menu('StationConfigurationMenu') return 'station shown='..tostring(m and m.shown and true or false)"
        "..' changes='..tostring(m and m.haschanges and true or false)..' plan='..tostring(m and m.constructionplan and #m.constructionplan)"
        "..' left='..tostring(m and m.modulesMode)")

# Click places: (x, y) fractions of the window; y of rows that move down 12 of 720 pixels with every plot in the list.
PLOTS_ICON = (0.021, 0.381)            # the map's left bar: Manage Plots
PROPERTY_ICON = (0.021, 0.153)         # the map's left bar: Property Owned
PLOT_COLUMN, LIST_STEP = 0.268, 12 / 720.0
NEW_PLOT_Y, LICENCE_Y, CONTINUE_Y = 318 / 720.0, 380 / 720.0, 442 / 720.0
DOCKS_ICON = (0.021, 0.253)            # the station menu's left bar: dock modules
FIRST_MODULE = (0.072, 0.17)           # the first module of the open list
PUT = (0.48, 0.52)                     # into the plot
CONFIRM = (0.80, 0.30)                 # "Confirm module changes" with one planned module
ASSIGN = (0.853, 0.476)                # "Assign or hire a Builder" with one planned module
CLOSE_MENU = (0.908, 0.94)             # the station menu's "Close Menu"
BUILDER_X, BUILDER_Y, SELECT_X, SELECT_Y = 0.11, 225 / 720.0, 0.17, 236 / 720.0
PLACE = {1: (0.55, 0.62), 2: (0.45, 0.72)}       # where each player puts its plot in the sector


def plan(minutes):
    """Every client's plan: the fixtures and the readings, at the same ticks in both."""
    last = int(minutes * 60 * 30) - 150
    rows = [(30, 'station_kit', 1, True), (40, 'wallet', 50000000)]
    rows += [(t, 'station_wares', 3000) for t in range(900, last, WARES_EVERY)]
    rows += [(t, 'builder_warp') for t in range(1200, last, WARP_EVERY)]
    rows += [(t, 'station_look') for t in range(LOOK_EVERY, last, LOOK_EVERY)]
    return [rows, rows]


def looks(log):
    """One client's station readings: [(tick, text)]."""
    out = []
    for line in rig.read_lines(log):
        m = LOOK.search(line)
        if m:
            out.append((int(m.group(1)), m.group(2).strip()))
    return out


def stations(text):
    """A reading's stations: code -> its numbers."""
    out = {}
    for m in STATION.finditer(text):
        out[m.group(1)] = {'built': int(m.group(2)), 'standing': int(m.group(3)), 'planned': int(m.group(4)),
                           'tasks': (int(m.group(5)), int(m.group(6))), 'builders': int(m.group(7)), 'stored': m.group(10) or '-'}
    return out


class Rig:
    """The running session: each client's pid and log, the console, pictures."""

    def __init__(self, pids, logs, work, run_dir):
        self.pids, self.logs, self.work, self.run_dir = pids, logs, work, run_dir
        self.asked = int(time.time()) % 100000 * 10        # console numbers only grow
        self.shots = 0
        self.steps = []
        (run_dir / 'shots').mkdir(exist_ok=True)

    def size(self, k):
        return self.logs[k - 1].stat().st_size

    def wait_row(self, k, pattern, start, seconds):
        end = time.time() + seconds
        while time.time() < end:
            with open(self.logs[k - 1], 'rb') as f:
                f.seek(start)
                found = [m for m in (re.search(pattern, line.decode('latin1')) for line in f.read().split(b'\n')) if m]
            if found:
                return found[-1]
            time.sleep(0.5)
        return None

    def ask(self, k, body, seconds=8):
        """Lua on client k through its console file (read only here): the answer's text, or None."""
        self.asked += 1
        mark = self.size(k)
        (self.work / 'console' / ('c%d.lua' % k)).write_text('return {n=%d, run=function(tools,S) %s end}' % (self.asked, body), encoding='utf-8')
        m = self.wait_row(k, r'UI_REPLAY_CONSOLE tick=\d+ n=%d ok=(\w+) result=(.*)' % self.asked, mark, seconds)
        return m.group(2).strip() if m else None

    def shot(self, k, label):
        self.shots += 1
        try:
            rig.window_shot(self.pids[k - 1], self.run_dir / 'shots' / ('%03d-p%d-%s.png' % (self.shots, k, label)))
        except (OSError, rig.ctypes_error) as error:      # a picture is extra: never the run's end
            print('no picture of client %d: %s' % (k, error))

    def step(self, k, label, action, row=None, expect=None, wait=2.0, tries=3):
        """One real input of player k. row: a pattern both clients' logs must then hold (the replayed call); expect:
        (console Lua, pattern) the menu must then answer. Without a row the input is made again when the menu did not
        take it; with a row it is made again only when the clicking client's log shows that nothing was sent."""
        record = {'player': k, 'step': label, 'ok': False, 'tries': 0, 'rows': {}, 'menu': None}
        for attempt in range(tries):
            marks = {c: self.size(c) for c in PLAYERS}
            ui_clicks.act(self.pids[k - 1], action)
            record['tries'] = attempt + 1
            good = True
            sent = False
            if row:
                # the clicking client's own log first: a click that was captured shows there within a second or two
                # (capture, the input delay, the apply); then the other client must hold the same call
                m = self.wait_row(k, row, marks[k], 8)
                sent = bool(m)
                record['rows'][str(k)] = m.group(0)[:200] if m else None
                good = sent
                for c in PLAYERS:
                    if sent and c != k:
                        m = self.wait_row(c, row, marks[c], 20)
                        record['rows'][str(c)] = m.group(0)[:200] if m else None
                        good = good and bool(m)
            time.sleep(wait)
            if expect:
                record['menu'] = self.ask(k, expect[0])
                good = good and bool(record['menu']) and bool(re.search(expect[1], record['menu']))
            self.shot(k, label)
            record['ok'] = good
            if good or sent:          # a call that was sent is never sent twice
                break
        self.steps.append(record)
        print('player %d %-9s %s%s%s' % (k, label, 'ok' if record['ok'] else 'FAILED', ' (try %d)' % record['tries'] if record['tries'] > 1 else '',
                                         '  ' + (record['menu'] or '')[:110] if not record['ok'] else ''), flush=True)
        return record['ok']


def build_station(r, k, plots_before, builder_row, leave):
    """Player k's click-through; False at the first step that failed. leave: the player then closes its menus ("Close
    Menu", Escape on the map) and sits in its cockpit while the station is built; else the station menu stays open."""
    down = LIST_STEP * plots_before
    applied = r'UI_BUILD_APPLIED tick=\d+ slot=%d what=' % k
    todo = [
        ('map', ('key', 0x4D), None, (MAP, 'map shown=true'), 3),
        ('dialog', ('key', 0x0D), None, None, 3),                 # a new profile's "priority orders" question: Enter takes "No"
        ('plots', ('click',) + PLOTS_ICON, None, (MAP, 'info=plots'), 2),
        ('newplot', ('click', PLOT_COLUMN, NEW_PLOT_Y + down), None, (MAP, 'active=true'), 1.5),
        ('place', ('click',) + PLACE[k], applied + r'ReserveBuildPlot=[1-9]\d*', (MAP, 'plot=true'), 3),
        ('licence', ('click', PLOT_COLUMN, LICENCE_Y + down), applied + 'PayBuildPlotSize', None, 2),
        ('continue', ('click', PLOT_COLUMN, CONTINUE_Y + down), None, (MENU, 'station shown=true'), 6),
        ('docks', ('click',) + DOCKS_ICON, None, (MENU, 'left=moduletypes_dock'), 2),
        ('module', ('click',) + FIRST_MODULE, None, None, 1.5),
        ('put', ('click',) + PUT, None, (MENU, 'changes=true'), 2),
        ('confirm', ('click',) + CONFIRM, applied + 'SetConstructionSequenceFromConstructionMap task=1', (MENU, 'changes=false plan=1'), 3),
        ('assign', ('click',) + ASSIGN, None, (MAP, 'mode=selectCV'), 4),
        ('owned', ('click',) + PROPERTY_ICON, None, (MAP, 'info=propertyowned'), 2),
        ('builder', ('click', BUILDER_X, BUILDER_Y + LIST_STEP * builder_row, 'right'), None, None, 2),
        ('select', ('click', SELECT_X, SELECT_Y + LIST_STEP * builder_row),
         r'UI_REPLAY_APPLIED tick=\d+ slot=%d op=\d+ result=applied what=CreateDeployToStationOrder=[1-9]' % k, (MENU, 'station shown=true'), 3),
    ]
    if leave:
        todo += [('close', ('click',) + CLOSE_MENU, None, (MENU, 'station shown=false'), 3),
                 ('cockpit', ('key', 0x1B), None, (MAP, 'map shown=false'), 2)]
    for label, action, row, expect, wait in todo:
        if not r.step(k, label, action, row, expect, wait):
            return False
    return True


def report(logs, run_dir, steps=None):
    clients = {k: at.parse(path) for k, path in zip(PLAYERS, logs)}
    check = at.Checks()
    stops = [x for c in clients.values() for x in c['stops']]
    last, mismatch, errors = rig.relay_state(run_dir)
    check('running', not stops and not errors, stops[0] if stops else errors[0][:200] if errors else 'no stop, no relay error; ' + last[:60])
    if steps is None and run_dir and (Path(run_dir) / 'clicks.json').exists():
        steps = json.loads((Path(run_dir) / 'clicks.json').read_text(encoding='utf-8'))
    for k in PLAYERS:
        mine = [s for s in steps or [] if s['player'] == k]
        failed = [s for s in mine if not s['ok']]
        done = bool(mine) and not failed and any(s['step'] == 'select' for s in mine)
        check('clicks', done,
              "player %d: all %d steps to the builder's order%s%s" % (k, len(mine), ''.join(', %s at try %d' % (s['step'], s['tries']) for s in mine if s['tries'] > 1),
                                                                      '; then back in the cockpit' if mine and mine[-1]['step'] == 'cockpit' else '; the station menu stayed open')
              if done else
              'player %d: %s' % (k, 'no click record' if not mine else 'stopped at "%s": menu %s, rows %s' % (mine[-1]['step'], mine[-1]['menu'], mine[-1]['rows'])))
    # The replayed build calls: the same list on both clients.
    calls = [[(m.group(1), m.group(2), m.group(3)) for m in (re.search(r'UI_BUILD_APPLIED tick=(\d+) slot=(\d+) what=(\S+)', line) for line in rig.read_lines(path)) if m]
             for path in logs]
    upto = min((int(c[-1][0]) if c else 0) for c in calls)
    same = bool(calls[0]) and [c for c in calls[0] if int(c[0]) <= upto] == [c for c in calls[1] if int(c[0]) <= upto]
    check('replayed', same, 'the %d build calls ran on both clients at the same ticks with the same answers' % len(calls[0]) if same else
          'build calls differ: client 1 %s, client 2 %s' % (calls[0][-3:], calls[1][-3:]))
    read = [looks(path) for path in logs]
    final = [stations(r[-1][1]) if r else {} for r in read]
    codes = sorted(set(final[0]) | set(final[1]))
    check('planned', len(codes) == len(PLAYERS) and all(final[c].get(code, {}).get('planned', 0) + final[c].get(code, {}).get('built', 0) >= 1 for c in (0, 1) for code in codes),
          'stations %s: %s' % (', '.join(codes) or 'none', '; '.join('client %d %s' % (c + 1, {code: (v['planned'], v['built']) for code, v in final[c].items()}) for c in (0, 1))))
    for code in codes:
        first = []
        for c in (0, 1):
            first.append(next((tick for tick, text in read[c] if stations(text).get(code, {}).get('built', 0) >= 1), None))
        check('built', first[0] is not None and first[0] == first[1], 'station %s: %s' % (code,
              'its module stands on both clients from the reading of tick %d' % first[0] if first[0] is not None and first[0] == first[1] else
              'never built in this run (the last reading: %s)' % (read[0][-1][1][:200] if read[0] else 'none') if first == [None, None] else
              'built at the reading of tick %s on client 1 and of tick %s on client 2' % tuple(first)))
    upto = min((r[-1][0] if r else 0) for r in read)
    a, b = ([x for x in r if x[0] <= upto] for r in read)
    differ = [x for x in a if x not in b]
    check('readings', bool(a) and not differ, 'the %d station readings both clients took to tick %d are the same' % (len(a), upto) if a and not differ else
          'no reading' if not a else '%d readings differ, the first at tick %d:\n            client 1 %s\n            client 2 %s' % (
              len(differ), differ[0][0], differ[0][1][:300], dict(b).get(differ[0][0], '')[:300]))
    at.sync_check(check, logs, run_dir)
    return check.failed


def run(args):
    env = dict(os.environ)
    if args.first_profile is not None:
        env['X4LS_FIRST_PROFILE'] = str(args.first_profile)
        os.environ['X4LS_FIRST_PROFILE'] = str(args.first_profile)
    sys.path.insert(0, str(ROOT / 'src'))
    from lockstep.launcher import profile          # after X4LS_FIRST_PROFILE: it names the profiles of clients 1 and 2
    if (ROOT / '.local/x4ls/session.json').exists():
        print('this checkout has a launcher session recorded: run `python tools/x4ls.py clean` first')
        return 2
    work = ROOT / '.local/station-build'
    (work / 'console').mkdir(parents=True, exist_ok=True)
    for k in PLAYERS:
        (work / 'console' / ('c%d.lua' % k)).write_text('return {n=0}', encoding='utf-8')
    overlay = work / 'overlay'
    for stale in (overlay, Path(str(overlay) + '.base')):
        if stale.exists():
            import shutil
            shutil.rmtree(stale)
    console = str(work / 'console' / 'c%d.lua').replace('\\', '/')
    made = subprocess.run([sys.executable, str(ROOT / 'tools/build_play_overlay.py'), '--snapshot', str(args.snapshot), '--gate-dll', str(args.gate_dll),
                           '--out', str(overlay), '--players', '2', '--mode', 'coop', '--minutes', str(args.minutes), '--save-every', '0',
                           '--set', 'native_gate_ui_replay_script=' + rig.lua(plan(args.minutes)),
                           '--set', "native_gate_ui_replay_console='%s'" % console,
                           '--set', 'native_gate_digest_parts={1,%d}' % (args.minutes * 1800)] +
                          ([] if args.no_pages else ['--set', 'native_gate_world_pages_window={1800,%d}' % (args.minutes * 1800), '--set', 'native_gate_created_rows=true']) +
                          [x for extra in args.set for x in ('--set', extra)], cwd=ROOT, env=env, capture_output=True, text=True)
    (work / 'overlay.log').write_text(made.stdout + made.stderr, encoding='utf-8')
    if made.returncode:
        print('the overlay was not built (%s):\n%s' % (work / 'overlay.log', (made.stdout + made.stderr)[-600:]))
        return 2
    before = set((ROOT / '.local/runs').glob('2*')) if (ROOT / '.local/runs').exists() else set()
    command = [sys.executable, str(ROOT / 'tools/x4ls.py'), 'play', '--clients', '2', '--overlay', str(overlay), '--snapshot', str(args.snapshot),
               '--size', SIZE, '--no-focus', '--timeout', str(args.minutes * 60 + 600)]
    for name in ('display', 'tile_skip', 'lanes'):
        if getattr(args, name) is not None:
            command += ['--' + name.replace('_', '-'), str(getattr(args, name))]
    host_log = open(work / 'host.log', 'w', encoding='utf-8')
    host = subprocess.Popen(command, cwd=ROOT, env=env, stdout=host_log, stderr=subprocess.STDOUT)
    logs = [profile.profile_dir(profile.PROFILES[k]) / ('x4ls-c%d.log' % (k + 1)) for k in range(2)]
    outcome, run_dir, started, r = 2, None, False, None
    try:
        deadline = time.time() + 420
        while time.time() < deadline and not started:
            time.sleep(3)
            if run_dir is None:
                fresh = sorted(set((ROOT / '.local/runs').glob('2*')) - before) if (ROOT / '.local/runs').exists() else []
                run_dir = fresh[-1] if fresh else None
            errors = [x for x in rig.read_lines(run_dir / 'errors.log') if x.strip()] if run_dir else []
            if host.poll() is not None or errors:
                print('the session did not get going%s; see %s' % (': ' + errors[0].strip()[:300] if errors else '', work / 'host.log'))
                break
            # the fixtures are in by tick 300; the first minute of a session a client is still loading what it shows and
            # its menus answer a click late (run t1: three clicks of the first forty seconds were not taken)
            started = bool(run_dir) and bool(re.search(r'^NATIVE_GATE tick=(\d{5,}|[2-9]\d{3}|1[89]\d\d) ', '\n'.join(rig.read_lines(run_dir / 'relay.log')[-3:]), re.M))
        if started:
            pids = [int(x) for x in re.findall(r'client \d+: pid (\d+)', (work / 'host.log').read_text(encoding='utf-8', errors='replace'))]
            print('the clients step (run %s, pids %s)' % (run_dir.name, pids), flush=True)
            rig.below_normal(pids)
            r = Rig(pids, logs, work, run_dir)
            for k in PLAYERS:
                read = looks(logs[k - 1])
                if not build_station(r, k, len(stations(read[-1][1])) if read else 0, k - 1, k in args.leave):
                    break
            (run_dir / 'clicks.json').write_text(json.dumps(r.steps, indent=1), encoding='utf-8')
            # until every station's module stands on both clients, or the window is nearly over
            end = time.time() + args.minutes * 60
            while time.time() < end and host.poll() is None:
                time.sleep(10)
                last = [looks(path) for path in logs]
                done = [all(v['built'] >= 1 for v in stations(x[-1][1]).values()) and len(stations(x[-1][1])) == len(PLAYERS) for x in last if x]
                tick = re.search(r'^NATIVE_GATE tick=(\d+)', '\n'.join(rig.read_lines(run_dir / 'relay.log')[-2:]), re.M)
                if (len(done) == 2 and all(done)) or (tick and int(tick.group(1)) > args.minutes * 1800 - 600) or not all(s['ok'] for s in r.steps):
                    break
            time.sleep(LOOK_EVERY / 30.0 + 4)        # one more reading of both, and the rows of both logs
            for k in PLAYERS:
                r.shot(k, 'end')
            outcome = 1 if report(logs, run_dir, r.steps) else 0
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
    s = sub.add_parser('run')
    s.add_argument('--snapshot', type=Path, required=True, help='the two-player snapshot save (save_009.xml.gz)')
    s.add_argument('--gate-dll', type=Path, required=True)
    s.add_argument('--minutes', type=int, default=25, help='length of the session window')
    s.add_argument('--first-profile', type=int, help='X4LS_FIRST_PROFILE: beside another session, use other profiles')
    s.add_argument('--lanes', help='where this run keeps its client views (beside another session: a folder of its own)')
    s.add_argument('--display', type=int)
    s.add_argument('--tile-skip', type=int)
    s.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE')
    s.add_argument('--leave', type=lambda text: [int(x) for x in text.split(',') if x], default=[], metavar='PLAYERS',
                   help="players (1,2) who close their menus after the builder's order; the others keep the station menu open while it is built")
    s.add_argument('--keep', action='store_true', help='leave the clients running after the report')
    s.add_argument('--no-pages', action='store_true', help='no per-object rows of the world captures (smaller logs)')
    s = sub.add_parser('report')
    s.add_argument('logs', nargs=2, type=Path)
    s.add_argument('--run', type=Path, help='the run directory (relay.log, errors.log, clicks.json)')
    s = sub.add_parser('plan')
    s.add_argument('--minutes', type=int, default=25)
    args = p.parse_args(argv)
    if args.command == 'plan':
        print(rig.lua(plan(args.minutes)))
        return 0
    if args.command == 'report':
        return 1 if report(args.logs, args.run) else 0
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
