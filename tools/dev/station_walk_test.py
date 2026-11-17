"""Station test: two players fly to a station, dock, get up, walk, see each other's figure, sit down and undock.

    python tools/dev/station_walk_test.py run --snapshot SAVE.xml.gz --gate-dll x4lan_gate_64.dll [--scenario walk|meet|undock]
    python tools/dev/station_walk_test.py report CLIENT1.log CLIENT2.log [--run RUNDIR] [--scenario walk|meet|undock]
    python tools/dev/station_walk_test.py plan [--scenario walk|meet|undock]

What it runs (scripted inputs, no person; the snapshot is the two-player one: both ships side by side, the station
STATION about 118 km away):
  walk    both ships fly to the station at the same time by X4's autopilot, ask for a dock as the station menu does and
          are docked by X4 (the test installs X4's docking computer and takes the NPC pilot out of the hulls, as a
          session's player hulls have none). Player 2 gets up in its docked ship, walks and turns; player 1 gets up a
          little later, so both are on foot together; then player 2 sits down, undocks and flies off by hand while
          player 1 is still on foot.
  meet    both dock and get up, each walks out of its ship onto the dock (backwards: a small ship is left through its
          rear), player 1 takes a few steps towards player 2, and player 2 walks the 40 m across the dock up to
          player 1 (the script op foot_meet steers by where the figures are). The pictures at the end show, on each
          client, the other player's figure three metres away.
  undock  both dock, stay in their seats, undock one after the other and fly off by hand.

What it checks, from each client's own log and the relay's:
  docked    each player's ship reports docked (the script op dock_station on that player's client)
  running   no client's gate stopped and the relay saw no error
  on foot   each player that got up is on foot (X4: no occupied ship) and still on the station
  figures   every client has a stand-in figure for EVERY player that walks, in that player's room at that player's
            place: the FOOT_AVATAR_AT rows (written by the script that places the figures) are the same on both
            clients, for both players
  walked    a walking player's place changed after its walk keys
  met       (meet) each player stopped as near the other as asked, with the other ahead of it
  undocked  a ship that undocked is no longer docked and moved after its thrust key
  in sync   the world, the players' records and their inputs agree at every tick (the clients' digest parts);
            the runtime ID word alone is reported as a note (see report())
Pictures of each client's own window (PrintWindow: nothing else of the screen) go to RUN/shots at the marks.

`run` starts its own windowed clients with disposable profiles (x4ls play), beside other sessions when
--first-profile, --lanes, --display and --tile-skip keep them apart, and stops the clients it started. Exit code: 0 all
checks passed, 1 a check failed, 2 the run itself did not work. Scripted input is not a person at the controls: what a
person sees and clicks (the dock menu, the seat, the crosshair) is not covered here.
"""
import argparse
import ctypes
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ctypes_error = ctypes.ArgumentError
STATION = '00000000000434b1'      # the Argon factory of the two-player snapshot
GET_UP = 0x115                     # INPUT_ACTION_GET_UP: up from the seat, and down again for a player 2..N on foot
# input states: FP_MOVE_FORWARD 0x1a, BACKWARD, TURN_LEFT, TURN_RIGHT, LOOK_UP, LOOK_DOWN, STRAFE_LEFT, STRAFE_RIGHT (seen live, 16 Nov)
WALK, BACK, TURN_LEFT, TURN_RIGHT, LOOK_UP, LOOK_DOWN, STRAFE_LEFT, STRAFE_RIGHT, ACCELERATE = 26, 27, 28, 29, 30, 31, 32, 33, 13

# Ticks after a ship's own docking: what its player does then.
SCENARIOS = {
    'walk': {
        1: [(60, 'foot_where'), (900, 'input', 'action', GET_UP, 0), (1000, 'foot_where'), (1030, 'input', 'state', BACK, 1),
            (1060, 'input', 'state', BACK, 0), (1100, 'input', 'state', TURN_LEFT, 1), (1107, 'input', 'state', TURN_LEFT, 0),
            (1300, 'foot_where'), (2700, 'foot_where')],
        2: [(60, 'foot_where'), (150, 'input', 'action', GET_UP, 0), (270, 'foot_where'), (300, 'input', 'state', BACK, 1),
            (330, 'input', 'state', BACK, 0), (360, 'input', 'state', TURN_RIGHT, 1), (367, 'input', 'state', TURN_RIGHT, 0),
            (500, 'foot_where'), (1300, 'input', 'action', GET_UP, 0), (1400, 'foot_where'), (1500, 'undock'),
            (1900, 'input', 'state', ACCELERATE, 1), (1990, 'input', 'state', ACCELERATE, 0), (2200, 'foot_where')],
        'up': {1: (1000, 1300), 2: (270, 500)}, 'undocks': {2: 2200}, 'last': {1: 2700, 2: 2200},
    },
    # Both leave their ships (a docked small ship is left by walking backwards, out of its rear), player 1 takes a few
    # steps towards player 2 and stops, player 2 walks across the dock up to player 1 (the op foot_meet steers).
    'meet': {
        1: [(60, 'foot_where'), (700, 'input', 'action', GET_UP, 0), (860, 'input', 'state', BACK, 1), (1060, 'input', 'state', BACK, 0),
            (1090, 'foot_where'), (1100, 'foot_meet', 2, 34, 700), (1500, 'foot_where'), (1810, 'foot_where')],
        2: [(60, 'foot_where'), (150, 'input', 'action', GET_UP, 0), (300, 'input', 'state', BACK, 1), (500, 'input', 'state', BACK, 0),
            (520, 'foot_where'), (540, 'foot_meet', 1, 3, 720), (1000, 'foot_where'), (1270, 'foot_where')],
        'up': {1: (1090, 1810), 2: (520, 1270)}, 'undocks': {}, 'last': {1: 1810, 2: 1270}, 'meets': {1: 34, 2: 3},
    },
    'undock': {
        1: [(60, 'foot_where'), (300, 'undock'), (700, 'input', 'state', ACCELERATE, 1), (790, 'input', 'state', ACCELERATE, 0),
            (1000, 'foot_where')],
        2: [(60, 'foot_where'), (300, 'undock'), (700, 'input', 'state', ACCELERATE, 1), (790, 'input', 'state', ACCELERATE, 0),
            (1000, 'foot_where')],
        'up': {}, 'undocks': {1: 1000, 2: 1000}, 'last': {1: 1000, 2: 1000},
    },
}
LIMIT = 13000      # ticks the docking may take before the script gives up
SCRIPT = re.compile(r'UI_REPLAY_SCRIPT tick=(\d+) op=(\w+) ok=(\w+) result=(.*)')
AFTER = re.compile(r'docked\+(\d+) (\w+) ok=(\w+) (.*?)(?= \| docked\+| scope=\d|$)')
FIGURE = re.compile(r'FOOT_AVATAR_AT slot=(\d+) room=(\S+) at=position\.\[([^\]]*)\] yaw=(\S+) body_room=(\S+) steps=(\d+) time=(\S+)')


def lua(value):
    if isinstance(value, (list, tuple)):
        return '{' + ','.join(lua(x) for x in value) + '}'
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, str):
        return "'" + value + "'"
    return str(value)


def plan(scenario, station=STATION):
    """The Lua text of native_gate_ui_replay_script: one plan per client."""
    s = SCENARIOS[scenario]
    kit = [(30, 'dock_kit', 1), (30, 'dock_kit', 2)]
    return lua([kit + [(60, 'dock_station', station, LIMIT, s[1])], kit + [(90, 'dock_station', station, LIMIT, s[2])]])


def read_lines(path):
    try:
        return Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return []


def parse(log):
    """One client's log: when its ship docked, what its player's later steps answered, the figures' rows, stops."""
    out = {'docked': None, 'after': {}, 'figures': {}, 'stops': [], 'limit': None}
    for line in read_lines(log):
        if 'NATIVE_GATE STOP' in line or 'PILOT_STOP_WHY' in line:
            out['stops'].append(line.split('[X4LAN] ')[-1][:300])
        m = FIGURE.search(line)
        if m:
            out['figures'].setdefault(int(m.group(1)), {})[int(m.group(6))] = (m.group(2), m.group(3), m.group(4), m.group(5), m.group(7))
            continue
        m = SCRIPT.search(line)
        if not m or m.group(2) != 'dock_station':
            continue
        tick, result = int(m.group(1)), m.group(4)
        if result.startswith('docked after=') and out['docked'] is None:
            out['docked'] = tick
        if result.startswith('limit '):
            out['limit'] = result
        for step in AFTER.finditer(result):
            out['after'].setdefault(int(step.group(1)), []).append((step.group(2), step.group(3) == 'true', step.group(4).strip()))
    return out


def digest_parts(log):
    """tick -> {part: value} of one client's DIGEST_PARTS rows (native_gate_digest_parts)."""
    out = {}
    try:
        with open(log, 'rb') as f:
            for line in f:
                at = line.find(b'DIGEST_PARTS')
                if at < 0:
                    continue
                m = re.search(r'tick=(\d+) (.*)', line[at:].decode('latin1'))
                if m:
                    out[int(m.group(1))] = dict(re.findall(r'(\w+)=(\S+)', m.group(2)))
    except OSError:
        pass
    return out


def field(text, name):
    m = re.search(r'\b' + name + r'=(\S+)', text)
    return m.group(1) if m else None


def where(client, at):
    for op, ok, text in client['after'].get(at, []):
        if op == 'foot_where' and ok:
            return text
    return None


def relay_state(run_dir):
    lines = read_lines(Path(run_dir) / 'relay.log') if run_dir else []
    last = next((x for x in reversed(lines) if x.startswith('NATIVE_GATE tick=')), '')
    mismatch = re.search(r'first_mismatch=(\d+) mismatches=(\d+)', last)
    errors = [x for x in (read_lines(Path(run_dir) / 'errors.log') if run_dir else []) if x.strip()]
    return last, (int(mismatch.group(1)), int(mismatch.group(2))) if mismatch else None, errors


def report(logs, run_dir, scenario):
    """Prints the checks; returns the number that failed."""
    s = SCENARIOS[scenario]
    clients = {k + 1: parse(path) for k, path in enumerate(logs)}
    failed = 0

    def check(name, ok, detail):
        nonlocal failed
        failed += 0 if ok else 1
        print('%-9s %-4s %s' % (name, 'ok' if ok else 'FAIL', detail))

    for k in (1, 2):
        c = clients[k]
        check('docked', c['docked'] is not None, 'player %d: %s' % (k, 'tick %d' % c['docked'] if c['docked'] else (c['limit'] or 'no docking row')))
    stops = [x for c in clients.values() for x in c['stops']]
    last, mismatch, errors = relay_state(run_dir)
    check('running', not stops and not errors, (stops[0] if stops else errors[0][:200] if errors else 'no stop, no relay error; ' + last[:60]))
    for k, (first, later) in sorted(s['up'].items()):
        a, b = where(clients[k], first), where(clients[k], later)
        up = bool(a) and field(a, 'occupied') == '0' and field(a, 'station') not in (None, '0')
        check('on foot', up, 'player %d: %s' % (k, a or 'no row'))
        places = {v[1] for v in clients[k]['figures'].get(k, {}).values()}
        check('walked', bool(b) and len(places) > 1, 'player %d: %d places in its room after its walk keys (%s)' % (k, len(places), '; '.join(sorted(places)[:3])))
    for k in sorted(s['up']):
        rows = [clients[c]['figures'].get(k, {}) for c in (1, 2)]
        # The logs are read while the clients run: one of them may hold a row or two more. The rows both have must agree.
        common = sorted(set(rows[0]) & set(rows[1]))
        same = bool(common) and all(rows[0][n] == rows[1][n] for n in common) and abs(len(rows[0]) - len(rows[1])) <= 2
        places = len({rows[0][n][1] for n in common})
        check('figures', same and places > 1, "player %d's figure: %d rows on client 1, %d on client 2, the %d both have are %s, %d places" %
              (k, len(rows[0]), len(rows[1]), len(common), 'the same' if same else 'DIFFERENT', places))
    for k, wanted in sorted(s.get('meets', {}).items()):
        there = [text for steps in clients[k]['after'].values() for op, ok, text in steps if op == 'foot_meet' and ok and text.startswith('there')]
        distance = float(field(there[0], 'distance')) if there else None
        heading, bearing = (field(there[0], 'heading'), field(there[0], 'bearing')) if there else (None, None)
        ahead = None
        if heading not in (None, '-') and bearing is not None:
            ahead = abs((float(bearing) - float(heading) + math.pi) % (2 * math.pi) - math.pi)
        check('met', distance is not None and distance <= wanted + 0.6 and ahead is not None and ahead < 0.7,
              'player %d: %s' % (k, 'stopped %.1f m from the other player (asked: %s m), the other %.0f degrees off straight ahead'
                                 % (distance, wanted, math.degrees(ahead if ahead is not None else 9)) if there else 'never got there'))
    def place(text):
        value = field(text or '', 'ship_at')
        return tuple(float(x) for x in value.split('/')) if value else None

    for k, at in sorted(s['undocks'].items()):
        before, after = where(clients[k], 60), where(clients[k], at)
        a, b = place(before), place(after)
        flown = math.dist(a, b) if a and b else 0.0
        gone = bool(after) and field(after, 'docked') == 'false' and field(before or '', 'docked') == 'true'
        # a ship that only undocked hangs 30 m above its pad: flying off by its player's thrust key is more than that
        check('undocked', gone and flown > 60, 'player %d: docked %s -> %s, the ship %.0f m from its pad after the thrust key (%s -> %s)' %
              (k, field(before or '', 'docked'), field(after or '', 'docked'), flown, field(before or '', 'ship_at'), field(after or '', 'ship_at')))
    # Which part of the digest differed (the clients' DIGEST_PARTS rows, when the run logged them). The world, the
    # players' records and inputs must agree at every tick. The runtime ID word alone is reported, not failed: at a
    # station the people's scripts run a tick apart now and then (their animations end at each client's own time),
    # which shows as a few ticks of that word and nothing else (17 Nov; docs/PLAYTEST.md).
    parts = [digest_parts(path) for path in logs]
    if parts[0] and parts[1]:
        differ = {}
        for tick in sorted(set(parts[0]) & set(parts[1])):
            for name in parts[0][tick]:
                if name != 'hash' and parts[0][tick][name] != parts[1][tick].get(name):
                    differ.setdefault(name, []).append(tick)
        hard = {name: ticks for name, ticks in differ.items() if name != 'rid'}
        check('in sync', not hard, 'world, records and inputs agree at all %d ticks both clients logged' % len(set(parts[0]) & set(parts[1])) if not hard else
              '; '.join('%s differs at %d ticks from tick %d' % (name, len(ticks), ticks[0]) for name, ticks in sorted(hard.items())))
        if 'rid' in differ:
            print('%-9s %-4s %s' % ('ids', 'note', 'the runtime ID word differed at %d ticks, first %d (not counted as a failure)' % (len(differ['rid']), differ['rid'][0])))
    else:
        check('in sync', mismatch is None, 'no digest mismatch' if mismatch is None else 'first mismatch at tick %d, %d mismatching ticks' % mismatch)
    return failed


# ---- pictures of a client's own window ----
def window_shot(pid, path):
    """The content of that process's main window (PrintWindow), whatever is in front of it; False when it has none."""
    if os.name != 'nt':
        return False
    import ctypes
    from ctypes import wintypes
    user, gdi = ctypes.WinDLL('user32'), ctypes.WinDLL('gdi32')
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        owner = wintypes.DWORD(0)
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd) and not user.GetWindow(hwnd, 4):
            found.append(hwnd)
        return True
    user.EnumWindows(each, 0)
    if not found:
        return False
    hwnd = found[0]
    rect = wintypes.RECT()
    user.GetClientRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return False
    handle = ctypes.c_void_p
    user.GetDC.argtypes, user.GetDC.restype = [wintypes.HWND], handle
    user.ReleaseDC.argtypes = [wintypes.HWND, handle]
    user.PrintWindow.argtypes = [wintypes.HWND, handle, wintypes.UINT]
    gdi.CreateCompatibleDC.argtypes, gdi.CreateCompatibleDC.restype = [handle], handle
    gdi.CreateCompatibleBitmap.argtypes, gdi.CreateCompatibleBitmap.restype = [handle, ctypes.c_int, ctypes.c_int], handle
    gdi.SelectObject.argtypes, gdi.SelectObject.restype = [handle, handle], handle
    gdi.GetDIBits.argtypes = [handle, handle, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
    gdi.DeleteObject.argtypes, gdi.DeleteDC.argtypes = [handle], [handle]
    screen = user.GetDC(hwnd)
    dc = gdi.CreateCompatibleDC(screen)
    bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
    gdi.SelectObject(dc, bitmap)
    user.PrintWindow(hwnd, dc, 3)          # PW_CLIENTONLY | PW_RENDERFULLCONTENT

    class Header(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('width', wintypes.LONG), ('height', wintypes.LONG), ('planes', wintypes.WORD),
                    ('bits', wintypes.WORD), ('compression', wintypes.DWORD), ('image', wintypes.DWORD), ('xppm', wintypes.LONG),
                    ('yppm', wintypes.LONG), ('used', wintypes.DWORD), ('important', wintypes.DWORD)]
    header = Header(ctypes.sizeof(Header), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(width * height * 4)
    gdi.GetDIBits(dc, bitmap, 0, height, pixels, ctypes.byref(header), 0)
    gdi.DeleteObject(bitmap)
    gdi.DeleteDC(dc)
    user.ReleaseDC(hwnd, screen)
    try:
        from PIL import Image
        Image.frombuffer('RGB', (width, height), pixels.raw, 'raw', 'BGRX', 0, 1).save(str(path))
    except ImportError:      # no Pillow: an uncompressed bitmap
        import struct
        path = Path(path).with_suffix('.bmp')
        with open(path, 'wb') as f:
            f.write(b'BM' + struct.pack('<IHHI', 54 + len(pixels.raw), 0, 0, 54) + bytes(header) + pixels.raw)
    return True


def below_normal(pids):
    """The test clients run below normal priority, so a person's own work on this PC comes first."""
    if os.name != 'nt':
        return
    import ctypes
    kernel = ctypes.WinDLL('kernel32')
    kernel.OpenProcess.restype = ctypes.c_void_p
    for pid in pids:
        handle = kernel.OpenProcess(0x0200, False, int(pid))
        if handle:
            kernel.SetPriorityClass(ctypes.c_void_p(handle), 0x4000)
            kernel.CloseHandle(ctypes.c_void_p(handle))


def run(args):
    s = SCENARIOS[args.scenario]
    env = dict(os.environ)
    if args.first_profile is not None:
        env['X4LS_FIRST_PROFILE'] = str(args.first_profile)
        os.environ['X4LS_FIRST_PROFILE'] = str(args.first_profile)
    sys.path.insert(0, str(ROOT / 'src'))
    from lockstep.launcher import profile          # after X4LS_FIRST_PROFILE: it names the profiles of clients 1 and 2
    if (ROOT / '.local/x4ls/session.json').exists():
        print('this checkout has a launcher session recorded: run `python tools/x4ls.py clean` first')
        return 2
    minutes = args.minutes
    work = ROOT / '.local/station-walk'
    work.mkdir(parents=True, exist_ok=True)
    overlay = work / 'overlay'
    for stale in (overlay, Path(str(overlay) + '.base')):
        if stale.exists():
            import shutil
            shutil.rmtree(stale)
    made = subprocess.run([sys.executable, str(ROOT / 'tools/build_play_overlay.py'), '--snapshot', str(args.snapshot), '--gate-dll', str(args.gate_dll),
                           '--out', str(overlay), '--players', '2', '--mode', 'coop', '--minutes', str(minutes), '--save-every', '0',
                           '--set', 'native_gate_ui_replay_script=' + plan(args.scenario, args.station),
                           '--set', 'native_gate_digest_parts={1,90000}'] +
                          [x for extra in args.set for x in ('--set', extra)], cwd=ROOT, env=env, capture_output=True, text=True)
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
    marks = sorted({at for k in (1, 2) for at in ([x[0] for x in s[k] if x[1] == 'foot_where'])})
    try:
        deadline = time.time() + 420 + minutes * 60
        while time.time() < deadline:
            time.sleep(3)
            if run_dir is None:
                fresh = sorted(set((ROOT / '.local/runs').glob('2*')) - before) if (ROOT / '.local/runs').exists() else []
                run_dir = fresh[-1] if fresh else None
            errors = [x for x in read_lines(run_dir / 'errors.log') if x.strip()] if run_dir else []
            if host.poll() is not None or (errors and started is None):
                print('the session did not get going%s; see %s' % (': ' + errors[0].strip()[:300] if errors else '', work / 'host.log'))
                break
            if run_dir and started is None and re.search(r'^NATIVE_GATE tick=\d{3,} ', '\n'.join(read_lines(run_dir / 'relay.log')[-3:]), re.M):
                started = time.time()
                pids = [int(x) for x in re.findall(r'client \d+: pid (\d+)', (work / 'host.log').read_text(encoding='utf-8', errors='replace'))]
                print('the clients step (run %s, pids %s): scenario %s' % (run_dir.name, pids, args.scenario))
                below_normal(pids)
            if not started:
                continue
            clients = {k + 1: parse(path) for k, path in enumerate(logs)}
            for k in (1, 2):          # a picture of both windows at each of a player's marks
                for at in marks:
                    if (k, at) not in shots and at in clients[k]['after']:
                        shots.add((k, at))
                        (run_dir / 'shots').mkdir(exist_ok=True)
                        for c, pid in enumerate(pids, 1):
                            try:
                                window_shot(pid, run_dir / 'shots' / ('p%d-docked+%d-client%d.png' % (k, at, c)))
                            except (OSError, ctypes_error) as error:      # a picture is extra: never the run's end
                                print('no picture of client %d: %s' % (c, error))
            done = all(s['last'][k] in clients[k]['after'] for k in (1, 2))
            stopped = any(c['stops'] or c['limit'] for c in clients.values())
            if done or stopped:
                outcome = 0
                break
        if started and outcome != 0:
            print('not every step ran in time')
            outcome = 0      # the report names what is missing
        if outcome == 0:
            time.sleep(2)
            outcome = 1 if report(logs, run_dir, args.scenario) else 0
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


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    for name in ('run', 'report', 'plan'):
        s = sub.add_parser(name)
        s.add_argument('--scenario', choices=sorted(SCENARIOS), default='walk')
        s.add_argument('--station', default=STATION, help='the saved ID of the station to dock at')
        if name == 'run':
            s.add_argument('--snapshot', type=Path, required=True, help='the two-player snapshot save (save_009.xml.gz)')
            s.add_argument('--gate-dll', type=Path, required=True)
            s.add_argument('--minutes', type=int, default=8, help='length of the session window')
            s.add_argument('--size', default='1280x720')
            s.add_argument('--first-profile', type=int, help='X4LS_FIRST_PROFILE: beside another session, use other profiles')
            s.add_argument('--lanes', help='where this run keeps its client views (beside another session: a folder of its own)')
            s.add_argument('--display', type=int)
            s.add_argument('--tile-skip', type=int)
            s.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE')
            s.add_argument('--keep', action='store_true', help='leave the clients running after the report')
        if name == 'report':
            s.add_argument('logs', nargs=2, type=Path)
            s.add_argument('--run', type=Path, help='the run directory (relay.log, errors.log)')
    args = p.parse_args()
    if args.command == 'plan':
        print(plan(args.scenario, args.station))
        return 0
    if args.command == 'report':
        return 1 if report(args.logs, args.run, args.scenario) else 0
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
