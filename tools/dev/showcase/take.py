"""One recorded take for the showcase video: a two-client `x4ls play` session whose players are driven by a scripted
plan (native_gate_ui_replay_script, plans.py), with both X4 windows recorded side by side into one file.

  python tools/dev/showcase/take.py build NAME           # the take's play overlay (no X4)
  python tools/dev/showcase/take.py run NAME [--no-record] [--keep-logs]
  python tools/dev/showcase/take.py plan NAME            # the plan as the overlay gets it
  python tools/dev/showcase/take.py page                 # the Multiplayer page's states, one X4 (page_states.json)

Rules of the PC it runs on (C:/x4ls-locks/PROFILES.txt), all kept here:
- both slots x4-a + x4-b through tools/dev/release_check.py's Locks (queue line in want-both.txt, X4LS_FIRST_PROFILE=4);
- nothing starts while USER-PLAYING.txt exists, and a running take stops at once when it appears;
- no start with less than 8 GB free; own lanes, display 3, windowed, never the focus;
- only processes of the take's own lanes are ever stopped.
The recorder (ffmpeg's gfxcapture) is given the two X4 window handles of those lanes and nothing else: the picture is
X4's client area only, never the desktop, another window or the cursor."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools/dev'))
import plans       # noqa: E402
import windows     # noqa: E402

WORK = ROOT / '.local/showcase'
SESSION = WORK / 'session'
LANES = 'C:/x4ls-lanes-showcase'
LOCKS = Path('C:/x4ls-locks')
NAME = 'X4-Multiplayer-showcase'
GATE_DLL = ROOT.parent / 'X4-Multiplayer-p2all/.local/try/build-rel18/x4lan_gate_64.dll'
MIN_FREE_GB = 8
BELOW_NORMAL = 0x4000


def log(text):
    print(time.strftime('%H:%M:%S ') + text, flush=True)


def lua(value):
    """A plan value as Lua source."""
    if value is None:
        return 'nil'
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        assert re.fullmatch(r"[\w .\-]*", value), value
        return "'" + value + "'"
    return '{' + ','.join(lua(v) for v in value) + '}'


def free_gb():
    return shutil.disk_usage('C:/').free / 2 ** 30


def user_playing():
    return (LOCKS / 'USER-PLAYING.txt').exists()


def overlay_dir(name):
    return WORK / 'overlays' / name


def session_of(take):
    """The take's session folder (session.json, session-save.xml.gz): .local/showcase/session, the Terran cadet start for
    two players, or the take's own 'session' folder name beside it (session-big: bigger ships)."""
    return WORK / take.get('session', 'session')


def snapshot_of(take):
    """The take's save: its session's, or the take's own 'snapshot' (the two-player snapshot 005 of the station
    tests, Argon Prime, which needs no session.json)."""
    return Path(take['snapshot']) if take.get('snapshot') else session_of(take) / 'session-save.xml.gz'


def build(name):
    take = plans.TAKES[name]()
    out = overlay_dir(name)
    if out.exists():
        log(f'overlay {out} exists')
        return out
    # nothing of the session's defaults is switched off: a default set to false makes the session say so on screen
    # ("Multiplayer: N default features are off", tools/dev/native_gate_defaults.lua)
    sets = ['native_gate_ui_replay_script=' + lua(take['plan'])] + list(take.get('set', []))
    cmd = [sys.executable, str(ROOT / 'tools/build_play_overlay.py'), '--snapshot', str(snapshot_of(take)),
           '--gate-dll', str(take.get('gate_dll', GATE_DLL)), '--players', '2', '--mode', 'coop',
           '--minutes', str(take['minutes']), '--save-every', '0', '--out', str(out)]
    if not take.get('snapshot'):
        cmd += ['--session', str(session_of(take) / 'session.json')]
    for item in sets:
        cmd += ['--set', item]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    (WORK / f'build-{name}.log').write_text(done.stdout + done.stderr, encoding='utf-8')
    if done.returncode:
        raise SystemExit(f'build_play_overlay failed ({done.returncode}): {WORK / ("build-" + name + ".log")}')
    no_hints(out)
    log(f'overlay {out}')
    return out


def no_hints(overlay):
    """The takes' own MD file in the overlay (x4lan_showcase_hints.xml: X4's remove_help every two seconds, so no hint
    box sits in the picture), and the overlay's manifest written again with it."""
    import hashlib
    shutil.copyfile(HERE / 'x4lan_showcase_hints.xml', overlay / 'md/x4lan_showcase_hints.xml')
    manifest = {str(f.relative_to(overlay)).replace(chr(92), '/'): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in sorted(overlay.rglob('*')) if f.is_file() and f.name != 'x4lane-manifest.json'}
    (overlay / 'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + chr(10))


NOTICE = LOCKS / 'SHOWCASE-RECORDING.txt'


def notice(what=None):
    """The coordinator's sign for the other agents on this PC: while the file exists a showcase take is being recorded
    (or waits for its slots) and they hold their own runs, so the PC is quiet. Written before a take, removed after
    (SHOWCASE_NOTICE_KEEP=1 keeps it between the takes of one recording day; then remove it by hand at the end)."""
    if what:
        NOTICE.write_text(f'{NAME}: recording the showcase video ({what}), x4-a + x4-b, lanes {LANES}. Written '
                          + time.strftime('%Y-%m-%d %H:%M:%S') + '; removed when the take is over.' + chr(10), encoding='utf-8')
    elif what is None and os.environ.get('SHOWCASE_NOTICE_KEEP') != '1':
        NOTICE.unlink(missing_ok=True)


def kill_own():
    """Stops the take's own X4 clients and their launcher: only X4.exe and python processes whose command line or image
    names the take's lanes (a shell that merely mentions the path is left alone)."""
    for row in windows.lane_processes(LANES):
        if not (row.get('Name') or '').lower().startswith(('x4.exe', 'python')):
            continue
        log(f'stop own {row["ProcessId"]} {row["Name"]}')
        subprocess.run(['taskkill', '/PID', str(row['ProcessId']), '/T', '/F'], capture_output=True)


BENIGN = ('MILESTONE_SAVE', 'PLAYER_SAVE', 'PERIODIC_SAVE')      # a save X4 refused and tries again: the session runs on


def session_stopped(errors_log):
    """The first row of the run's errors.log that means a client left the session (a probe error, a gate stop, an X4
    that exited), or None."""
    try:
        rows = Path(errors_log).read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return None
    for row in rows:
        if re.search(r'NATIVE_GATE ERROR|SESSION_STOPPED| STOP |X4 exited', row) and not any(word in row for word in BENIGN):
            return row.strip()[:200]
    return None


def last_tick(path):
    """The last tick the relay's log names (NATIVE_GATE tick=N), or None."""
    try:
        with open(path, 'rb') as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 400))
            found = re.findall(rb'NATIVE_GATE tick=(-?\d+)', f.read())
    except OSError:
        return None
    return int(found[-1]) if found else None


class Recorder:
    """ffmpeg with one gfxcapture source per client window, side by side (client 1 left), constant 60 fps."""

    def __init__(self, out, size, clients=(1, 2)):
        self.out, self.size, self.proc, self.started, self.clients = Path(out), size, None, None, tuple(clients)

    def start(self):
        found = windows.client_windows(LANES)
        if sorted(found) != sorted(self.clients) or any(found[k][1] != self.size for k in found):
            return False
        parts = []
        for k in self.clients:
            parts.append(f'gfxcapture=hwnd={found[k][0]}:capture_cursor=0:max_framerate=60,hwdownload,format=bgra,fps=60[v{k}]')
        graph = ';'.join(parts) + (';[v1][v2]hstack=inputs=2[v]' if len(self.clients) == 2 else ';[v1]null[v]')
        cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'warning', '-y', '-filter_complex', graph, '-map', '[v]',
               '-c:v', 'h264_nvenc', '-preset', 'p5', '-rc', 'constqp', '-qp', '18', '-g', '30', '-pix_fmt', 'yuv420p',
               str(self.out)]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                     stderr=open(self.out.with_suffix('.ffmpeg.log'), 'w'))
        self.started = time.time()
        log(f'recording {self.out.name} windows ' + ' '.join(str(found[k][0]) for k in self.clients))
        return True

    def stop(self):
        if not self.proc:
            return
        try:
            self.proc.stdin.write(b'q')
            self.proc.stdin.flush()
            self.proc.wait(30)
        except (OSError, subprocess.TimeoutExpired):
            self.proc.kill()
        log(f'recorded {self.out.name}: {self.out.stat().st_size / 2 ** 20:.0f} MB' if self.out.exists() else 'no recording')
        self.proc = None


def run(name, record=True, keep_logs=False, then_page=False):
    import release_check as rc
    take = plans.TAKES[name]()
    overlay = build(name)
    if user_playing():
        raise SystemExit('USER-PLAYING.txt: nothing starts')
    if free_gb() < MIN_FREE_GB:
        raise SystemExit(f'{free_gb():.1f} GB free: no run below {MIN_FREE_GB} GB')
    width, height = take.get('size', (1920, 1080))
    ticks = 40 + int(take['minutes'] * 60 * 30) + 1
    raw = WORK / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%H%M%S')
    marks = raw / f'{name}-{stamp}.marks.txt'
    locks = rc.Locks(NAME, f'showcase video take {name}: two clients, lanes {LANES}', slots=['x4-a', 'x4-b'], log=log, poll=5)
    recorder = Recorder(raw / f'{name}-{stamp}.mkv', (width, height))
    stopped = None
    try:
        while user_playing():
            time.sleep(10)
        notice(f'take {name}')
        locks.acquire('two')
        if user_playing() or free_gb() < MIN_FREE_GB:
            raise SystemExit('the user plays or the disk is short: no run')
        env = dict(os.environ, X4LS_FIRST_PROFILE=str(locks.index if locks.index is not None else 4), X4LS_BESIDE='1',
                   SteamNoOverlayUIDrawing='1')
        if take.get('client_config'):
            env['X4LS_CLIENT_CONFIG'] = take['client_config']
        cmd = [sys.executable, str(ROOT / 'tools/x4ls.py'), 'play', '--clients', '2', '--overlay', str(overlay),
               '--snapshot', str(snapshot_of(take)), '--size', f'{width}x{height}', '--display', '3',
               '--never-focus', '--close-when-over', '--lanes', LANES]
        log('start: X4LS_FIRST_PROFILE=' + env['X4LS_FIRST_PROFILE'] + f', {free_gb():.1f} GB free, {ticks} ticks')
        # the clients take the launcher's priority: normal for a take (a take is recorded while the other agents hold
        # their runs; at below normal the two X4 drew about 21 pictures a second), 'priority': 'below' for the old way
        proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                creationflags=BELOW_NORMAL if take.get('priority') == 'below' else 0)
        lines = []

        def reader():
            with open(marks, 'w', encoding='utf-8') as out:
                for line in proc.stdout:
                    line = line.rstrip()
                    lines.append(line)
                    since = '%.2f' % (time.time() - recorder.started) if recorder.started else '-'
                    out.write(f'{time.time():.2f} video={since} {line}\n')
                    out.flush()
                    if re.match(r'(tick \d+|all clients|client \d|play:|played|.*desync|.*ERROR)', line):
                        log('x4ls: ' + line[:200])
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        began, run_dir, tick, said, checked, moved = time.time(), None, -1, -1, 0.0, [-1, time.time()]
        record_from = take.get('record_from')          # an absolute tick; None: from the start menu on
        tick_marks = open(raw / f'{name}-{stamp}.ticks.txt', 'w', encoding='utf-8')
        while proc.poll() is None:
            time.sleep(0.1)
            if user_playing():
                stopped = 'USER-PLAYING.txt appeared'
                break
            if run_dir is None:
                # this session's folder: named by its start time (an older run's folder may still be written to)
                first = time.strftime('%Y%m%d-%H%M%S', time.localtime(began - 2))
                new = [d for d in (ROOT / '.local/runs').glob('20*') if d.is_dir() and first <= d.name[:15] and '-' not in d.name[15:]]
                run_dir = min(new, key=lambda d: d.name) if new else None
            if run_dir is not None:
                now = last_tick(run_dir / 'relay.log')
                if now is not None and now != tick:
                    tick = now
                    # the picture's time of every tick the relay has released (to find a plan's tick in the take)
                    if recorder.started:
                        tick_marks.write(f'{time.time() - recorder.started:.2f} {tick}' + chr(10))
                    if tick - said >= 300:
                        said = tick
                        tick_marks.flush()
            # a session that has stopped is never filmed as if it were live: a probe error or a gate stop on a client
            # (after it that X4 plays on alone, ungated), or a relay that stands still
            if run_dir is not None and time.time() - checked > 1:
                checked = time.time()
                why = session_stopped(run_dir / 'errors.log')
                if tick != moved[0]:
                    moved = [tick, time.time()]
                elif why is None and 0 < tick < ticks - 2 and time.time() - moved[1] > 25:
                    why = f'the relay stands still at tick {tick}'
                if why:
                    stopped = 'the session stopped: ' + why
                    break
            if record and not recorder.proc and not recorder.started and (
                    tick >= record_from if record_from is not None
                    else any(l.startswith('all clients are at the start menu') for l in lines)):
                recorder.start()
            if recorder.proc and recorder.proc.poll() is not None:
                log('the recorder ended by itself: ' + str(recorder.proc.returncode))
                recorder.proc = None
                record = False
        recorder.stop()
        tick_marks.close()
        if stopped:
            log('STOP: ' + stopped)
            kill_own()
        try:
            proc.wait(180)
        except subprocess.TimeoutExpired:
            kill_own()
        thread.join(10)
        text = '\n'.join(lines)
        (raw / f'{name}-{stamp}.launcher.txt').write_text(text, encoding='utf-8')
        match = re.search(r'"run_dir": "([^"]+)"', text)
        desync = re.search(r'"first_desync": ([^,\n]+)', text)
        log(f'launcher exit {proc.returncode}; first_desync={desync.group(1) if desync else "?"}')
        if match and not keep_logs:
            run_dir = Path(match.group(1).replace('\\\\', '/'))
            for client in run_dir.glob('client*'):
                shutil.rmtree(client, ignore_errors=True)
            shutil.rmtree(run_dir / 'saves', ignore_errors=True)
            for big in run_dir.glob('native-inputs*'):
                big.unlink(missing_ok=True)
            log(f'raw logs of {run_dir.name} removed')
        if then_page and not stopped and not user_playing():
            page(held=locks)          # the page's states in one X4 while the pair is still this run's
    finally:
        recorder.stop()
        if windows.lane_processes(LANES):
            kill_own()
        locks.done()
        notice()
        log('slots free again')


def page(states=None, size=(1920, 1080), held=None):
    """The Multiplayer page in X4's main menu, state after state (x4ls lobby --page-preview: no flow, no network,
    only the example data of the states file), recorded from the one X4 window. One free slot."""
    import release_check as rc
    states = Path(states) if states else HERE / 'page_states.json'
    if user_playing():
        raise SystemExit('USER-PLAYING.txt: nothing starts')
    if free_gb() < MIN_FREE_GB:
        raise SystemExit(f'{free_gb():.1f} GB free: no run below {MIN_FREE_GB} GB')
    raw = WORK / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%H%M%S')
    locks = held or rc.Locks(NAME, f'showcase video: the Multiplayer page preview, one X4, lanes {LANES}',
                             slots=['x4-a', 'x4-b', 'x4-d'], log=log, poll=5)
    recorder = Recorder(raw / f'page-{stamp}.mkv', size, clients=(1,))
    try:
        if not held:
            notice('the Multiplayer page preview')
            locks.acquire('one')
        index = locks.index
        env = dict(os.environ, X4LS_FIRST_PROFILE=str(index), X4LS_BESIDE='1', SteamNoOverlayUIDrawing='1')
        cmd = [sys.executable, str(ROOT / 'tools/x4ls.py'), 'lobby', '--page-preview', str(states), '--shots',
               str(raw / f'page-{stamp}-shots'), '--display', '3', '--lanes', LANES, '--lane', '1',
               '--size', f'{size[0]}x{size[1]}', '--no-master']
        log(f'page preview: {locks.held} X4LS_FIRST_PROFILE={index}, {free_gb():.1f} GB free')
        proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                creationflags=BELOW_NORMAL)
        marks = open(raw / f'page-{stamp}.marks.txt', 'w', encoding='utf-8')

        def reader():
            for line in proc.stdout:
                since = '%.2f' % (time.time() - recorder.started) if recorder.started else '-'
                marks.write(f'{time.time():.2f} video={since} {line.rstrip()}' + chr(10))
                marks.flush()
                log('x4ls: ' + line.rstrip()[:200])
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        started = time.time()
        stopped = None
        while proc.poll() is None:
            time.sleep(1)
            if user_playing():
                stopped = 'USER-PLAYING.txt appeared'
            elif time.time() - started > 900:
                stopped = 'no end within 900 s'
            if stopped:
                log('STOP: ' + stopped)
                break
            if not recorder.proc and not recorder.out.exists():
                recorder.start()
        recorder.stop()
        if stopped:
            kill_own()
        try:
            proc.wait(120)
        except subprocess.TimeoutExpired:
            kill_own()
        thread.join(10)
        marks.close()
        log(f'launcher exit {proc.returncode}')
    finally:
        recorder.stop()
        if windows.lane_processes(LANES):
            kill_own()
        if not held:
            locks.done()
            notice()
            log('slot free again')


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('what', choices=['build', 'run', 'plan', 'page'])
    p.add_argument('name', nargs='?', choices=sorted(plans.TAKES))
    p.add_argument('--no-record', action='store_true')
    p.add_argument('--keep-logs', action='store_true')
    p.add_argument('--then-page', action='store_true', help='after the take, the page preview in the same slots')
    args = p.parse_args(argv)
    if args.what == 'page':
        page()
    elif args.what == 'plan':
        print(lua(plans.TAKES[args.name]()['plan']))
    elif args.what == 'build':
        build(args.name)
    else:
        run(args.name, record=not args.no_record, keep_logs=args.keep_logs, then_page=args.then_page)


if __name__ == '__main__':
    main(sys.argv[1:])
