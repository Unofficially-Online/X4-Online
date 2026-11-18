"""Mission type tests: every player runs X4's own mission libraries, one after the other, and the results are compared.

    python tools/dev/mission_matrix.py list
    python tools/dev/mission_matrix.py plan [--types flyto,collect] [--players 2]
    python tools/dev/mission_matrix.py run --snapshot SAVE.xml.gz --session session.json --gate-dll x4lan_gate_64.dll
    python tools/dev/mission_matrix.py report CLIENT1.log CLIENT2.log [--run RUNDIR]
    python tools/dev/mission_matrix.py offers --snapshot ... --session ... --gate-dll ... [--accept-as 1,2] [--count 13]
    python tools/dev/mission_matrix.py real --offer NAME --snapshot ... --session ... --gate-dll ... [--accept-as 1,2]
    python tools/dev/mission_matrix.py meet --snapshot ... --session ... --gate-dll ... [--mover 2] [--to 1]
    python tools/dev/mission_matrix.py script --op "2 300 ui_ping" --rows REGEX --snapshot ... --session ... --gate-dll ...

What a test is (MD: tools/dev/native_gate_ui_replay.xml, cue MissionTest; script ops mt_offer / mt_accept): the probe
makes a test offer at the same tick on every client, the player under test accepts it as the stock menus accept an
offer, and the accept starts ONE stock mission library (md/rml_*.xml) on a target the test made. Five seconds later the
test does, through MD actions, what a player would have to do for that library; the library's own end signal is the
result (its feedback value; positive is X4's success). Player 1 is X4's own player, so player 1's row is the reference:
a test passes for player k when player k's value equals player 1's and player 1's is the answer the library is known
to give for that action.

`offers` is the other half: the offers X4 itself made in the session's world (the generic missions of the stations
around the players) are accepted one after the other, by player 1 in one session and by player 2 in the next; for each
it names whether a mission started and how many script errors came with it. It completes none of them.

`real` finishes one of them: a stock "destroy these objects" offer, named by a part of its name, is accepted by the
player under test; round after round that player's ship is put near the first object of the mission's objective
(the flight a person would make), the object becomes its target and its own guns fire. Everything else is the stock
mission's script: the kill credit, the end, the reward. The row of the mission's end names the player it ended for.

`run` starts its own windowed test clients with disposable profiles (the launcher's `x4ls play`), beside other running
sessions when X4LS_FIRST_PROFILE / --first-profile, --lanes and --display keep them apart, waits for the rows, prints
the table and stops the clients it started. Every session logs the per-tick digest parts and runtime ID rows; when the
clients split, the report names the parts and the ID keys that differ at that tick. Exit code: 0 all passed, 1 a test failed or the clients split, 2 the run
itself did not work. A mocked or scripted completion is not a person playing the mission: the actions that need a
person or a menu (docking, talking) are not covered by these tests.
"""
import argparse
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# type -> (the stock library, what the test does in place of the player, the library's answer for X4's own player).
# The MD cues of the same names start them. destroy: the target destroyed by nobody (the library's "destroyed, but
# not by the player" answer); kill: by the player's own guns; scan: a station's modules revealed by being close;
# killmatch: the same through the kill notice; deepscan: a ship scanned by the player's ship; lrs: a ship found by
# the player's long-range scan. Not here because
# MD has no action for it: a hull repair, a dock, a talk.
TYPES = {
    'flyto': ('RML_FlyTo.FlyTo', 'the target is moved to 300 m from the player (arrival is a distance to that player)', '1'),
    'collect': ('RML_Collect_Inventory.CollectInventory', 'two inventory items are added to the player', '1'),
    'destroy': ('RML_Destroy_Components.DestroyComponents', 'the target is destroyed by nobody: the library must say "not by the player"', '-1'),
    'reputation': ('RML_Reputation.Reputation', "the player faction's standing with the Argon is raised", '1'),
    'capture': ('RML_Capture.Capture', "the target comes to the player, then becomes the player faction's", '1'),
    'target': ('RML_Long_Range_Scan.LongRangeScan', 'the player targets the object it was sent to find', '1'),
    'deploy': ('RML_DeployInPlace.DeployInPlace', 'the player launches a satellite with the call the stock menu makes', '1'),
    'kill': ('RML_Destroy_Components.DestroyComponents', "the player's own guns destroy targets ahead: the library must count a kill by the player", '1'),
    'killmatch': ('RML_Destroy_Matching_Objects.DestroyMatchingObjects', "the player's own guns destroy targets ahead: the library must count a kill (its own target, or the player-killed-an-object notice with its filters)", '1'),
    'lrs': ('RML_Long_Range_Scan.LongRangeScan', "the player's long-range scan (mode, a full charge, the send) pings a ship 5 km ahead, which the test then targets", '1'),
    'deepscan': ('RML_Scan.Scan', "the player's ship scans a ship ahead of it (scan mode, the scan's start, two seconds on target): the library must hear the scan finish", '1'),
    # last: it leaves the player's ship at a station
    'scan': ('RML_Scan.Scan', "the player's ship is put close to a station module, looking at it: the library must see the station revealed", '1'),
}
SLOT = '<slot>'
# What a test needs in the clients' plans beside its offer and accept: ('all' every client | 'own' the player under
# test, ticks after the offer, op, arguments; SLOT stands for the player under test).
OPS = {
    'deploy': (('all', 120, 'launch_stock', SLOT, 'eq_arg_satellite_01_macro', 1),
               ('own', 270, 'launch', 'satellite', 'eq_arg_satellite_01_macro')),
    # the aim assist on the player's target and the trigger, from 2 s after the accept for 7 s (an input state is a key
    # press and counts only in the window with the focus)
    'kill': (('own', 120, 'aim_fire', 1), ('own', 330, 'aim_fire', 0)),
    'killmatch': (('own', 120, 'aim_fire', 1), ('own', 330, 'aim_fire', 0)),
    # the long-range scan mode (its key, 0x131) 1 s after the accept, the secondary trigger held for 2.1 s (a full
    # charge is 2 s, lost 0.2 s later), the mode off again at the test's end
    'lrs': (('own', 90, 'input', 'action', 0x131, 0), ('own', 150, 'missile', 1), ('own', 213, 'missile', 0), ('own', 480, 'input', 'action', 0x131, 0)),
    # the scan mode 1 s after the accept, the scan's start 2 s later, the mode off again at the test's end
    'deepscan': (('own', 90, 'scan_key'), ('own', 150, 'scan_start'), ('own', 180, 'scan_diag'), ('own', 480, 'scan_key')),
}
START = 60          # window-relative tick of the first offer
SPACING = 600       # ticks per test (20 s at 30 Hz): offer, accept after 2 s, the test's action 5 s later
ACCEPT = 60
ROW = re.compile(r'\[X4LAN\] MT_RESULT type=(\w+) slot=(\d+) player=(\S+) value=(\S+) took=(\S+) time=(\S+)')


def plans(types, players):
    """The script of every client: all hold every offer (the probe's own call), each holds its own accepts."""
    out = [[] for _ in range(players)]
    tick = START
    for name in types:
        for slot in range(1, players + 1):
            for plan in out:
                plan.append((tick, 'mt_offer', name, slot))
            out[slot - 1].append((tick + ACCEPT, 'mt_accept', name, slot))
            for who, after, *op in OPS.get(name, ()):
                entry = (tick + after,) + tuple(slot if v == SLOT else v for v in op)
                for plan in (out if who == 'all' else [out[slot - 1]]):
                    plan.append(entry)
            tick += SPACING
    return out, tick + SPACING


def lua(value):
    if isinstance(value, (list, tuple)):
        return '{' + ','.join(lua(v) for v in value) + '}'
    return "'%s'" % value if isinstance(value, str) else str(value)


def script(types, players):
    out, end = plans(types, players)
    return lua([sorted(plan, key=lambda e: e[0]) for plan in out]), end


def read_rows(path):
    """{(type, slot): value} of one client log; the first row of a test counts (a late timeout row cannot follow it)."""
    rows = {}
    with open(path, encoding='latin-1') as log:
        for line in log:
            match = ROW.search(line)
            if match:
                rows.setdefault((match.group(1), int(match.group(2))), match.group(4))
    return rows


def table(logs, types, players, run=None):
    """Prints the table; returns the number of failed tests (a split between the clients counts as one)."""
    per_client = [read_rows(path) for path in logs]
    failed = 0
    same = all(rows == per_client[0] for rows in per_client)
    rows = per_client[0] if per_client else {}
    width = max(len(name) for name in types) + 2
    print('%-*s %s  verdict' % (width, 'type', ''.join('%-12s' % ('player %d' % k) for k in range(1, players + 1))))
    for name in types:
        values = [rows.get((name, k), 'no row') for k in range(1, players + 1)]
        if values[0] != TYPES[name][2]:
            verdict = "REFERENCE FAILED (player 1 got %s, the library answers %s for X4's own player: the test itself is at fault)" % (values[0], TYPES[name][2])
            failed += 1
        elif all(v == values[0] for v in values):
            verdict = 'pass'
        else:
            verdict = 'FAIL: ' + ', '.join('player %d' % (k + 1) for k, v in enumerate(values) if v != values[0])
            failed += 1
        print('%-*s %s  %s' % (width, name, ''.join('%-12s' % v for v in values), verdict))
    if not same:
        failed += 1
        print('FAIL: the clients logged different rows (each client must see every player\'s result alike):')
        for path, client in zip(logs, per_client):
            print('  %s: %s' % (path, sorted(client.items())))
    if run:
        run = Path(run)
        desyncs = [l for l in read_lines(run / 'desyncs.log') if 'DESYNC' in l]
        errors = read_lines(run / 'errors.log')
        if desyncs:
            failed += 1
            print('FAIL: the clients split: ' + desyncs[0].strip()[:160])
            print_lines(split_note(logs, desyncs[0]))
        if errors:
            failed += 1
            print('FAIL: %d error rows, first: %s' % (len(errors), errors[0].strip()[:200]))
    md = sum(1 for path in logs for line in read_lines(path) if 'Error in MD' in line or 'Error in AI' in line)
    if md:
        print('note: %d MD/AI script error lines in the client logs (look for X4LAN_UiReplay and RML_ names)' % md)
    print('%d of %d tests passed' % (len(types) - min(failed, len(types)), len(types)) if not failed else '%d failure(s)' % failed)
    return failed


def read_lines(path):
    try:
        return Path(path).read_text(encoding='latin-1').splitlines()
    except OSError:
        return []


# Every test session logs, for each tick, the parts of the digest the clients compare and the runtime IDs taken (a
# few megabytes a run): a split that shows once in many runs is then named by the run that had it.
DIAGNOSTIC_SETS = ('native_gate_digest_parts={1,400000}', 'native_gate_runtime_ids_rows=true')
SPLIT_TICK = re.compile(r'DESYNC tick=(\d+)')
PARTS_ROW = re.compile(r'DIGEST_PARTS tick=(\d+) (.*)')
IDS_ROW = re.compile(r'RUNTIME_IDS_ROW index=\d+ tick=(\d+) .*? keys=(\S*)')


def print_lines(lines):
    for line in lines:
        print(line)


def with_diagnostics(sets):
    """The session's settings with the diagnostic rows, unless the caller set those keys itself."""
    named = {extra.split('=', 1)[0] for extra in sets}
    return list(sets) + [extra for extra in DIAGNOSTIC_SETS if extra.split('=', 1)[0] not in named]


def split_note(logs, desync_line):
    """What the clients' logs say about the tick of a split: the digest parts that differ there, and the runtime ID
    keys taken a different number of times in that tick or the one before. Lines to print; [] without the rows."""
    match = SPLIT_TICK.search(desync_line or '')
    if not match:
        return []
    tick = int(match.group(1))
    parts, keys = [], []
    for path in logs:
        mine, taken = {}, {}
        for line in read_lines(path):
            if 'DIGEST_PARTS tick=' in line:
                row = PARTS_ROW.search(line)
                if row and int(row.group(1)) == tick:
                    mine = dict(re.findall(r'(\w+)=(\w+)', row.group(2)))
            elif 'RUNTIME_IDS_ROW ' in line:
                row = IDS_ROW.search(line)
                if row and int(row.group(1)) in (tick - 1, tick):
                    for item in row.group(2).split(','):
                        key, _, count = item.partition(':')
                        if key and count.isdigit():
                            taken[(int(row.group(1)), key)] = int(count)
        parts.append(mine)
        keys.append(taken)
    out = []
    if all(parts):
        differ = sorted({k for p in parts for k in p if any(q.get(k) != parts[0].get(k) for q in parts)} - {'hash'})
        out.append('  at tick %d the clients differ in: %s' % (tick, ' '.join(differ) if differ else 'no part (only the whole)'))
    if any(keys):
        odd = sorted(k for k in set().union(*keys) if any(c.get(k, 0) != keys[0].get(k, 0) for c in keys))
        for at, key in odd[:12]:
            out.append('  runtime IDs, tick %d, key %s: %s' % (at, key, ' / '.join(str(c.get((at, key), 0)) for c in keys)))
        if not odd:
            out.append('  runtime IDs: the same keys the same number of times in ticks %d and %d' % (tick - 1, tick))
    return out


def below_normal(pids):
    """Windows: the test clients run below normal priority, so a person's own work on this PC comes first."""
    if os.name != 'nt':
        return
    import ctypes
    kernel = ctypes.WinDLL('kernel32')
    kernel.OpenProcess.restype = ctypes.c_void_p
    for pid in pids:
        handle = kernel.OpenProcess(0x0200, False, int(pid))          # PROCESS_SET_INFORMATION
        if handle:
            kernel.SetPriorityClass(ctypes.c_void_p(handle), 0x4000)  # BELOW_NORMAL_PRIORITY_CLASS
            kernel.CloseHandle(ctypes.c_void_p(handle))


def session(args, plan, end, done, report, what):
    """Overlay, clients, wait until done(logs), report(logs, run) -> failures, stop. Only the clients this call started
    are stopped (the launcher's own state file)."""
    env = dict(os.environ)
    if args.first_profile is not None:
        env['X4LS_FIRST_PROFILE'] = str(args.first_profile)
        os.environ['X4LS_FIRST_PROFILE'] = str(args.first_profile)
    if args.beside:
        env['X4LS_BESIDE'] = '1'
    sys.path.insert(0, str(ROOT / 'src'))
    from lockstep.launcher import profile          # after X4LS_FIRST_PROFILE: it names the profiles of clients 1..N
    if (ROOT / '.local/x4ls/session.json').exists():
        print('this checkout has a launcher session recorded: run `python tools/x4ls.py clean` first')
        return 2
    minutes = max(2, math.ceil((end + 600) / 1800) + 1)
    work = ROOT / '.local/mission-matrix'
    work.mkdir(parents=True, exist_ok=True)
    overlay = work / 'overlay'
    for stale in (overlay, Path(str(overlay) + '.base')):
        if stale.exists():
            import shutil
            shutil.rmtree(stale)
    made = subprocess.run([sys.executable, str(ROOT / 'tools/build_play_overlay.py'), '--snapshot', str(args.snapshot), '--session', str(args.session),
                           '--gate-dll', str(args.gate_dll), '--out', str(overlay), '--players', str(args.players), '--mode', 'coop',
                           '--minutes', str(minutes), '--set', 'native_gate_ui_replay_script=' + plan] +
                          [x for extra in with_diagnostics(args.set) for x in ('--set', extra)], cwd=ROOT, env=env, capture_output=True, text=True)
    (work / 'overlay.log').write_text(made.stdout + made.stderr, encoding='utf-8')
    if made.returncode:
        print('the overlay was not built (%s):\n%s' % (work / 'overlay.log', (made.stdout + made.stderr)[-600:]))
        return 2
    before = set((ROOT / '.local/runs').glob('2*')) if (ROOT / '.local/runs').exists() else set()
    command = [sys.executable, str(ROOT / 'tools/x4ls.py'), 'play', '--clients', str(args.players), '--overlay', str(overlay),
               '--snapshot', str(args.snapshot), '--size', args.size, '--no-focus']
    if args.display is not None:
        command += ['--display', str(args.display)]
    if args.tile_skip:
        command += ['--tile-skip', str(args.tile_skip)]
    if args.lanes:
        command += ['--lanes', str(args.lanes)]
    host_log = open(work / 'host.log', 'w', encoding='utf-8')
    host = subprocess.Popen(command, cwd=ROOT, env=env, stdout=host_log, stderr=subprocess.STDOUT)
    logs = [profile.profile_dir(profile.PROFILES[k]) / ('x4ls-c%d.log' % (k + 1)) for k in range(args.players)]
    outcome, run_dir = 2, None
    try:
        deadline = time.time() + 420 + minutes * 60
        started = None
        while time.time() < deadline:
            time.sleep(3)
            if run_dir is None:
                fresh = sorted(set((ROOT / '.local/runs').glob('2*')) - before) if (ROOT / '.local/runs').exists() else []
                run_dir = fresh[-1] if fresh else None
            errors = read_lines(run_dir / 'errors.log') if run_dir else []
            if host.poll() is not None or (errors and started is None):
                print('the session did not get going%s; see %s' % (': ' + errors[0].strip()[:300] if errors else '', work / 'host.log'))
                break
            if run_dir and started is None and re.search(r'^NATIVE_GATE tick=\d{3,} ', '\n'.join(read_lines(run_dir / 'relay.log')[-3:]), re.M):
                started = time.time()
                print('the clients step (run %s); %s, about %d s' % (run_dir.name, what, end // 30))
                if args.low_priority:
                    below_normal(re.findall(r'client \d+: pid (\d+)', (work / 'host.log').read_text(encoding='utf-8', errors='replace')))
            if started and done(logs):
                outcome = 0
                break
            if started and time.time() - started > end / 30 + 180:
                print('not every row arrived in time')
                outcome = 0      # the table names what is missing
                break
        if outcome == 0:
            time.sleep(2)
            outcome = 1 if report(logs, run_dir) else 0
    finally:
        if not args.keep:
            subprocess.run([sys.executable, str(ROOT / 'tools/x4ls.py'), 'clean'], cwd=ROOT, env=env, capture_output=True, text=True)
            try:
                host.wait(timeout=30)
            except subprocess.TimeoutExpired:
                host.kill()
        host_log.close()
    return outcome


def run(args, types):
    plan, end = script(types, args.players)
    wanted = {(name, slot) for name in types for slot in range(1, args.players + 1)}
    return session(args, plan, end, lambda logs: all(path.exists() and wanted <= set(read_rows(path)) for path in logs),
                   lambda logs, run_dir: table(logs, types, args.players, run_dir), '%d tests' % len(wanted))


# ---- the stock offers ----
# Every offer X4 itself made in the session's world (the generic missions of the stations around the players), accepted
# by one player after the other in sessions of their own: does a mission start for that player, and do the mission's
# scripts run without an error? The same world gives the same offers, so player 1's session is the reference again.
OFFER_START = 700      # after the offers of every slot were generated
OFFER_SPACING = 300    # ticks per offer: accept, 5 s later the new missions are named, then aborted
OFFER_ROW = re.compile(r'UI_REPLAY_SCRIPT tick=(\d+) op=(offers_list|offer_accept_next|mis_new|mis_look|mis_abort_new) ok=(\w+) result=(.*)')


def offers_script(player, players, count, look=False):
    out = [[] for _ in range(players)]
    own = out[player - 1]
    own.append((OFFER_START, 'offers_list'))
    tick = OFFER_START + 60
    for _ in range(count):
        own += [(tick, 'offer_accept_next'), (tick + 150, 'mis_new')]
        if look:
            own.append((tick + 165, 'mis_look'))
        own.append((tick + 180, 'mis_abort_new'))
        tick += OFFER_SPACING
    return lua(out), tick


def read_offers(path):
    """[{offer, started, missions, errors}] of the accepting client's log, in the order the offers were taken."""
    out, current, errors = [], None, 0
    for line in read_lines(path):
        if 'Error in MD' in line or 'Error in AI' in line:
            errors += 1
            continue
        match = OFFER_ROW.search(line)
        if not match:
            continue
        op, ok, result = match.group(2), match.group(3), match.group(4).strip()
        if op == 'offer_accept_next':
            current = {'offer': result[len('accepted '):] if result.startswith('accepted ') else None, 'refused': ok != 'true',
                       'started': None, 'missions': '', 'errors': 0, 'look': ''}
            errors = 0
        elif op == 'mis_new' and current is not None:
            found = re.match(r'new=(\d+) ?(.*)', result)
            current['started'] = int(found.group(1)) if found else 0
            current['missions'] = found.group(2) if found else result
        elif op == 'mis_look' and current is not None:
            current['look'] = result if ok == 'true' else 'look failed: ' + result
        elif op == 'mis_abort_new' and current is not None:
            current['errors'] = errors
            if current['offer'] or current['refused']:
                out.append(current)
            current = None
    return out


def offers(args, players_under_test):
    """One session per player under test; the table of every offer; failures = offers that did worse than for player 1."""
    results, code = {}, 0
    for player in players_under_test:
        plan, end = offers_script(player, args.players, args.count, args.look)

        def done(logs, player=player):
            return logs[player - 1].exists() and sum(1 for l in read_lines(logs[player - 1]) if ' op=mis_abort_new ' in l) >= args.count

        def report(logs, run_dir, player=player):
            results[player] = read_offers(logs[player - 1])
            bad = [l for l in read_lines(Path(run_dir) / 'desyncs.log') if 'DESYNC' in l] if run_dir else []
            rows = read_lines(Path(run_dir) / 'errors.log') if run_dir else []
            if bad:
                print("FAIL (player %d's session): the clients split: %s" % (player, bad[0].strip()[:160]))
                print_lines(split_note(logs, bad[0]))
            if rows:
                print("FAIL (player %d's session): %d error rows, first: %s" % (player, len(rows), rows[0].strip()[:200]))
            return len(bad) + len(rows)

        print('player %d accepts the stock offers' % player)
        outcome = session(args, plan, end, done, report, 'up to %d offers' % args.count)
        if outcome == 2:
            return 2
        code = max(code, outcome)
    return max(code, 1 if offers_table(results) else 0)


def offers_table(results):
    """Prints every offer with what each player got; returns how many did worse for a player than for the first one."""
    players = sorted(results)
    names = []
    for player in players:
        for entry in results[player]:
            if entry['offer'] and entry['offer'] not in names:
                names.append(entry['offer'])
    by = {player: {entry['offer']: entry for entry in results[player] if entry['offer']} for player in players}
    failed = 0
    width = max([len(n) for n in names] + [5]) + 2
    print('%-*s %s  verdict' % (width, 'offer', ''.join('%-34s' % ('player %d' % k) for k in players)))
    for name in names:
        cells, verdict = [], 'pass'
        reference = by[players[0]].get(name)
        for player in players:
            entry = by[player].get(name)
            cells.append('not offered' if entry is None else '%s%s' % ('mission started' if entry['started'] else 'NO MISSION',
                                                                      ', %d script errors' % entry['errors'] if entry['errors'] else ''))
            if entry is None or reference is None:
                continue
            if (reference['started'] and not entry['started']) or entry['errors'] > reference['errors']:
                verdict = 'FAIL: player %d' % player
        if verdict != 'pass':
            failed += 1
        print('%-*s %s  %s' % (width, name, ''.join('%-34s' % c for c in cells), verdict))
    for player in players:
        for entry in results[player]:
            if entry.get('look'):
                print('player %d, %s: %s' % (player, entry['offer'], entry['look']))
    for player in players:
        refused = sum(1 for entry in results[player] if entry['refused'])
        if refused:
            print('note: player %d: %d accept(s) refused by the script op (see its UI_REPLAY_SCRIPT rows)' % (player, refused))
    print('%d offers, %d failure(s)' % (len(names), failed))
    return failed


# ---- any small plan ----
# The ops given on the command line ("SLOT TICK NAME [ARGS...]", numbers as numbers), a session of --ticks ticks, and
# every log line of every client that matches --rows. Fails only when the clients split or log an error row.
def custom(args):
    out = [[] for _ in range(args.players)]
    for text in args.op:
        slot, tick, name, *rest = text.split()
        out[int(slot) - 1].append((int(tick), name) + tuple(int(v) if re.fullmatch(r'-?\d+', v) else v for v in rest))
    out[0].append((args.ticks - 30, 'account'))
    plan = lua([sorted(p, key=lambda e: e[0]) for p in out])
    rows = re.compile(args.rows)

    def done(logs):
        return logs[0].exists() and any(' op=account ' in l for l in read_lines(logs[0]))

    def report(logs, run_dir):
        for k, path in enumerate(logs):
            for line in read_lines(path):
                if rows.search(line):
                    print('  client %d: %s' % (k + 1, line.strip()[:args.width]))
        desyncs = [l for l in read_lines(Path(run_dir) / 'desyncs.log') if 'DESYNC' in l] if run_dir else []
        errors = read_lines(Path(run_dir) / 'errors.log') if run_dir else []
        if desyncs:
            print('FAIL: the clients split: %s' % desyncs[0].strip()[:160])
            print_lines(split_note(logs, desyncs[0]))
        if errors:
            print('FAIL: %d error rows, first: %s' % (len(errors), errors[0].strip()[:200]))
        return len(desyncs) + len(errors)

    return session(args, plan, args.ticks, done, report, '%d ops' % len(args.op))


# ---- two players meet ----
# One player's ship is put next to another player's ship, in that player's sector (script op goto_slot), and the
# rows of the next seconds say where it stands and at what attention. With --keep the clients stay up for a look at
# the ships' physics bodies. No verdict of its own beyond the clients agreeing.
MEET_AT = 300


def meet(args):
    out = [[] for _ in range(args.players)]
    late = 400 if args.leave else 0
    out[args.mover - 1] += [(MEET_AT, 'goto_slot', args.to, args.metres), (MEET_AT + 330 + late, 'account')]
    if args.leave:   # then the other player takes a stock destroy offer and jumps to its first target: the two part again
        out[args.to - 1] += [(MEET_AT + 200, 'offers_list'), (MEET_AT + 260, 'offer_accept_named', args.leave), (MEET_AT + 440, 'mis_goto', 5000)]
    plan, end = lua(out), MEET_AT + 400 + late

    def done(logs):
        return logs[args.mover - 1].exists() and any(' op=account ' in l for l in read_lines(logs[args.mover - 1]))

    def report(logs, run_dir):
        rows = [[re.search(r'\[X4LAN\] (MT_GOTO|MT_WATCH) (.*)', l) for l in read_lines(path)] for path in logs]
        rows = [[(m.group(1), m.group(2)) for m in client if m] for client in rows]
        for name, text in rows[args.mover - 1]:
            print('  %s %s' % (name.lower()[3:], text[:260]))
        bad = 0
        if not rows[0] or any(client != rows[0] for client in rows):
            print('FAIL: no rows, or the clients logged different rows')
            bad += 1
        desyncs = [l for l in read_lines(Path(run_dir) / 'desyncs.log') if 'DESYNC' in l] if run_dir else []
        errors = read_lines(Path(run_dir) / 'errors.log') if run_dir else []
        if desyncs:
            print('FAIL: the clients split: %s' % desyncs[0].strip()[:160])
            print_lines(split_note(logs, desyncs[0]))
        if errors:
            print('FAIL: %d error rows, first: %s' % (len(errors), errors[0].strip()[:200]))
        return bad + len(desyncs) + len(errors)

    print("player %d's ship is put %d m from player %d's" % (args.mover, args.metres, args.to))
    return session(args, plan, end, done, report, 'one jump')


# ---- one real stock offer, finished ----
REAL_ROUND = 300       # ticks per target: the flight, the target, the guns
REAL_END = re.compile(r'\[X4LAN\] MT_REAL end=(\w+) name=(.*?) reward=(\S+) player=(\S+) money=(\S+) time=(\S+)')
REAL_GOTO = re.compile(r'\[X4LAN\] MT_GOTO slot=(\d+) player=(\S+) ship=(\S+) target=(\S+)(.*)')
REAL_OP = re.compile(r'UI_REPLAY_SCRIPT tick=(\d+) op=(offer_accept_named|mis_look|mis_goto|mis_target|mis_list|account) ok=(\w+) result=(.*)')


def real_script(player, players, offer, rounds, metres):
    out = [[] for _ in range(players)]
    own = out[player - 1]
    own += [(OFFER_START, 'offers_list'), (OFFER_START + 30, 'account'), (OFFER_START + 60, 'offer_accept_named', offer),
            (OFFER_START + 210, 'mis_look')]
    tick = OFFER_START + 240
    for _ in range(rounds):
        own += [(tick, 'mis_goto', metres), (tick + 45, 'mis_target'), (tick + 60, 'aim_fire', 1), (tick + REAL_ROUND - 30, 'aim_fire', 0)]
        tick += REAL_ROUND
    own += [(tick, 'mis_list'), (tick + 30, 'account')]
    return lua(out), tick + 60


def read_real(path):
    """(the mission end rows, the flight rows, the plan's op rows, script error lines) of one client log."""
    ends, flights, ops, errors = [], [], [], 0
    for line in read_lines(path):
        if 'Error in MD' in line or 'Error in AI' in line:
            errors += 1
        match = REAL_END.search(line)
        if match:
            ends.append(match.groups())
        match = REAL_GOTO.search(line)
        if match:
            flights.append(match.groups())
        match = REAL_OP.search(line)
        if match:
            ops.append((match.group(1), match.group(2), match.group(3), match.group(4).strip()))
    return ends, flights, ops, errors


def real(args, players_under_test):
    """One session per player under test. Fails when the mission did not end as completed for a player, when the
    clients logged different rows, split or logged an error row."""
    failed = 0
    for player in players_under_test:
        plan, end = real_script(player, args.players, args.offer, args.rounds, args.metres)

        def done(logs, player=player):
            return logs[player - 1].exists() and sum(1 for l in read_lines(logs[player - 1]) if ' op=account ' in l) >= 2

        def report(logs, run_dir, player=player):
            bad = 0
            per_client = [read_real(path) for path in logs]
            ends, flights, ops, errors = per_client[player - 1]
            for tick, op, ok, result in ops:
                print('  tick %s %s%s: %s' % (tick, op, '' if ok == 'true' else ' (failed)', result[:300]))
            for row in flights:
                print('  flight: slot %s player %s ship %s target %s%s' % row)
            for line in read_lines(logs[player - 1]):
                found = re.search(r'\[X4LAN\] (MT_WATCH|MT_HIT|MT_DEAD|MT_DECOY) (.*)', line)
                if found:
                    print('  %s %s' % (found.group(1).lower()[3:], found.group(2)[:260]))
            for row in ends:
                print('  the mission ended: %s, "%s", reward %s, for player %s (account %s) at %s' % row)
            if not any(row[0] == 'completed' for row in ends):
                print('FAIL (player %d): the mission did not end as completed' % player)
                bad += 1
            if any(client[:2] != per_client[0][:2] for client in per_client):
                print('FAIL (player %d): the clients logged different end or flight rows' % player)
                bad += 1
            if errors:
                print('note: %d MD/AI script error lines in player %d\'s client log' % (errors, player))
            desyncs = [l for l in read_lines(Path(run_dir) / 'desyncs.log') if 'DESYNC' in l] if run_dir else []
            rows = read_lines(Path(run_dir) / 'errors.log') if run_dir else []
            if desyncs:
                print("FAIL (player %d's session): the clients split: %s" % (player, desyncs[0].strip()[:160]))
                print_lines(split_note(logs, desyncs[0]))
            if rows:
                print("FAIL (player %d's session): %d error rows, first: %s" % (player, len(rows), rows[0].strip()[:200]))
            return bad + len(desyncs) + len(rows)

        print('player %d takes the stock offer "%s" and finishes it' % (player, args.offer))
        outcome = session(args, plan, end, done, report, 'up to %d targets' % args.rounds)
        if outcome == 2:
            return 2
        print('player %d: %s' % (player, 'FAIL' if outcome else 'pass'))
        failed += 1 if outcome else 0
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('list', help='the mission types and what each test does')
    for name in ('plan', 'run', 'report', 'offers', 'real', 'meet', 'script'):
        sub = commands.add_parser(name)
        sub.add_argument('--types', default=','.join(TYPES), help='comma separated, in this order (default: all)')
        sub.add_argument('--players', type=int, default=2)
        if name == 'offers':
            sub.add_argument('--accept-as', default='1,2', help='the players that take the offers, one session each (default 1,2)')
            sub.add_argument('--count', type=int, default=13, help='how many offers each of them tries (default 13)')
            sub.add_argument('--look', action='store_true', help="also print what each started mission asks for (objective, progress, targets)")
        if name == 'real':
            sub.add_argument('--accept-as', default='1,2', help='the players that take the offer, one session each (default 1,2)')
            sub.add_argument('--offer', required=True, help='a part of the offer\'s name, blanks as _ (see `offers --look`)')
            sub.add_argument('--rounds', type=int, default=5, help='how many targets at most (default 5)')
            sub.add_argument('--metres', type=int, default=500, help='how far from a target the ship is put (default 500); negative: a diagnosis, the same distance with an ownerless scout on the line of fire')
        if name == 'meet':
            sub.add_argument('--mover', type=int, default=2, help='the player whose ship is moved (default 2)')
            sub.add_argument('--to', type=int, default=1, help='the player it is put next to (default 1)')
            sub.add_argument('--metres', type=int, default=300, help='how far from that ship (default 300)')
            sub.add_argument('--leave', metavar='OFFER', help='afterwards the other player takes this stock destroy offer and jumps to its first target')
        if name == 'script':
            sub.add_argument('--op', action='append', default=[], metavar='"SLOT TICK NAME [ARGS]"', help='one script op of one player; repeat')
            sub.add_argument('--ticks', type=int, default=700, help='how long the plan runs (default 700 ticks)')
            sub.add_argument('--rows', default=r'\[X4LAN\] MPP_AUDIT', help='log lines to print (a regular expression)')
            sub.add_argument('--width', type=int, default=260)
        if name in ('run', 'offers', 'real', 'meet', 'script'):
            sub.add_argument('--snapshot', type=Path, required=True, help="the session's save (session-save.xml.gz)")
            sub.add_argument('--session', type=Path, required=True, help="the session's session.json")
            sub.add_argument('--gate-dll', type=Path, required=True)
            sub.add_argument('--size', default='960x540')
            sub.add_argument('--display', type=int)
            sub.add_argument('--tile-skip', type=int, default=0)
            sub.add_argument('--lanes', type=Path, help="the clients' game views (default: the launcher's)")
            sub.add_argument('--first-profile', type=int, help='X4LS_FIRST_PROFILE: beside another session, use other profiles')
            sub.add_argument('--beside', action='store_true', help='X4LS_BESIDE=1: other launcher clients may be running')
            sub.add_argument('--set', action='append', default=[], metavar='KEY=VALUE', help='more session settings for the overlay')
            sub.add_argument('--keep', action='store_true', help='leave the clients running')
            sub.add_argument('--low-priority', action='store_true', help='Windows: run the test clients below normal priority')
        if name == 'report':
            sub.add_argument('logs', nargs='+', type=Path)
            sub.add_argument('--run', type=Path, help="the run directory (desyncs.log, errors.log)")
    args = parser.parse_args()
    if args.command == 'list':
        for name, (library, does, answer) in TYPES.items():
            print('%-11s md.%s: %s (answer %s)' % (name, library, does, answer))
        return 0
    types = [t for t in args.types.split(',') if t]
    unknown = [t for t in types if t not in TYPES]
    if unknown or not types or args.players < 1:
        parser.error('unknown type(s): ' + ', '.join(unknown) if unknown else 'nothing to test')
    if args.command == 'plan':
        text, end = script(types, args.players)
        print(text)
        print('-- %d ticks (%d s); pass it as native_gate_ui_replay_script' % (end, end // 30), file=sys.stderr)
        return 0
    if args.command == 'report':
        return 1 if table(args.logs, types, args.players, args.run) else 0
    if args.command == 'script':
        if not args.op:
            parser.error('--op: at least one')
        return custom(args)
    if args.command == 'meet':
        if not (1 <= args.mover <= args.players and 1 <= args.to <= args.players and args.mover != args.to):
            parser.error('--mover and --to name two different players of this session')
        return meet(args)
    if args.command == 'real':
        takers = [int(k) for k in args.accept_as.split(',') if k.strip()]
        if not takers or any(k < 1 or k > args.players for k in takers):
            parser.error('--accept-as names players 1..%d' % args.players)
        return real(args, takers)
    if args.command == 'offers':
        takers = [int(k) for k in args.accept_as.split(',') if k]
        if not takers or any(k < 1 or k > args.players for k in takers):
            parser.error('--accept-as: players of this session')
        return offers(args, takers)
    return run(args, types)


if __name__ == '__main__':
    sys.exit(main())
