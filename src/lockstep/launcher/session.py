"""One lockstep session: relay + N windowed clients, monitored, collected, cleaned up.

State for crash recovery lives in <state>/session.json; `x4ls clean` uses it to stop
owned processes and remove views even after the launcher itself died.
"""
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time

from . import extension, game, profile, steam

ROOT = Path(__file__).resolve().parents[3]
RELAY = ROOT / 'tools/dev/native_gate_relay.py'
STOP_OK = re.compile(r'NATIVE_GATE STOP .*reason=1 restored=1')
FAIL = re.compile(r'NATIVE_GATE (?:ERROR|REFUSED)|NATIVE_GATE STOP .*reason=(?!1 )[0-9]+|'
                  r'Error while executing.*native_(?:gate|construction|management)_[a-z_]+')


@dataclass
class Options:
    clients: int
    overlay: Path
    snapshot: Path | None = None
    ticks: int = 1800
    tick_hz: int = 30
    observe: bool = False
    lookahead: int = 0
    timeout: int = 900
    width: int = 960
    height: int = 540
    game: Path | None = None
    lanes: Path | None = None
    runs: Path = ROOT / '.local/runs'
    state: Path = ROOT / '.local/x4ls'
    keep_views: bool = False
    relay_args: list = field(default_factory=list)


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def x4_running():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq X4.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True).stdout
    return '"X4.exe"' in out


def relay_command(o, port, token, run):
    cmd = [sys.executable, str(RELAY), '--port', str(port), '--token', token, '--clients', str(o.clients),
           '--ticks', str(o.ticks), '--report', str(run / 'native-barrier.json'),
           '--input-journal', str(run / 'native-inputs.jsonl')]
    if o.snapshot:
        cmd.append('--relative-real')
    if o.tick_hz != 60:
        cmd += ['--tick-hz', str(o.tick_hz)]
    if o.observe:
        cmd += ['--observe', '--status-file', str(run / 'desync-status.json')]
    if o.lookahead:
        cmd += ['--lookahead', str(o.lookahead)]
    return cmd + list(o.relay_args)


def client_command(view, profile_id, k):
    return [str(view / 'X4.exe'), '-personalfolderid', str(profile_id), '-skipintro', '-noworkshopsync',
            '-debug', 'scripts', '-logfile', f'x4ls-c{k}.log']


def kill(pid):
    subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], capture_output=True)


def tile(pids, width, height, columns=2):
    """Best effort: place each client's main window on a grid."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    found = {}

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and user32.IsWindowVisible(hwnd) and pid.value not in found:
            found[pid.value] = hwnd
        return True

    user32.EnumWindows(visit, 0)
    for i, pid in enumerate(pids):
        if pid in found:
            user32.SetWindowPos(found[pid], 0, (i % columns) * width, (i // columns) * (height + 40), 0, 0, 0x0001 | 0x0004)
    return len(found)


def save_state(o, data):
    o.state.mkdir(parents=True, exist_ok=True)
    (o.state / 'session.json').write_text(json.dumps(data, indent=2) + '\n')


def clean(state_dir, keep_views=False):
    """Stop owned processes and remove owned views recorded by a previous session."""
    path = Path(state_dir) / 'session.json'
    if not path.exists():
        return 'no recorded session'
    data = json.loads(path.read_text())
    for pid in data.get('pids', []):
        kill(pid)
    if data.get('relay_pid'):
        kill(data['relay_pid'])
    if not keep_views:
        time.sleep(2)
        for view in data.get('views', []):
            if Path(view).exists():
                game.remove_view(view)
    path.unlink()
    return f"cleaned session {data.get('run')}"


def run(o: Options, log=print):
    if not 2 <= o.clients <= len(profile.PROFILES):
        raise SystemExit(f'clients must be 2..{len(profile.PROFILES)}')
    if (o.state / 'session.json').exists():
        raise SystemExit('a previous session is still recorded; run x4ls clean first')
    if x4_running():
        raise SystemExit('X4 is running; close it first')
    ok, message = steam.status()
    if not ok:
        raise SystemExit(message)
    extension.verify_overlay(o.overlay)
    install = Path(o.game) if o.game else game.find_install()
    game.verify_exe(install)
    lanes = Path(o.lanes) if o.lanes else Path(os.path.splitdrive(str(install))[0] + '\\x4ls-lanes')
    run_dir = o.runs / time.strftime('%Y%m%d-%H%M%S')
    run_dir.mkdir(parents=True)
    port, token = free_udp_port(), secrets.token_hex(16)
    state = {'run': str(run_dir), 'views': [], 'pids': [], 'relay_pid': None}
    save_state(o, state)
    clients, relay, relay_log = [], None, None
    try:
        relay_log = (run_dir / 'relay.log').open('w')
        relay = subprocess.Popen(relay_command(o, port, token, run_dir), cwd=ROOT, stdout=relay_log,
                                 stderr=subprocess.STDOUT)
        state['relay_pid'] = relay.pid
        save_state(o, state)
        for k in range(1, o.clients + 1):
            view = lanes / f'c{k}'
            game.create_view(install, view)
            state['views'].append(str(view))
            save_state(o, state)
            extension.compose(o.overlay, view / 'extensions', k, port, token, o.clients)
            profile_id = profile.PROFILES[k - 1]
            profile.prepare(profile_id, [*game.dlc_ids(view), extension.EXTENSION_ID], o.width, o.height, o.snapshot)
            proc = subprocess.Popen(client_command(view, profile_id, k), cwd=view, env=steam.client_environment())
            clients.append((k, profile_id, proc))
            state['pids'].append(proc.pid)
            save_state(o, state)
            log(f'client {k}: pid {proc.pid}, profile {profile_id}, view {view}')
            time.sleep(4)
        result = monitor(o, run_dir, relay, clients, log)
    finally:
        for _, _, proc in clients:
            if proc.poll() is None:
                kill(proc.pid)
        if relay and relay.poll() is None:
            kill(relay.pid)
        if relay_log:
            relay_log.close()
        time.sleep(2)
        for k, profile_id, _ in clients:
            profile.collect(profile_id, f'x4ls-c{k}.log', run_dir / f'client{k}')
        if not o.keep_views:
            for view in state['views']:
                game.remove_view(view)
        (o.state / 'session.json').unlink(missing_ok=True)
    (run_dir / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    log(f"{result['outcome']}: {run_dir}")
    return result


def capture_snapshot(out, gamestart='custom_creative', game_dir=None, lanes=None, timeout=900,
                     runs=ROOT / '.local/runs', state_dir=ROOT / '.local/x4ls', log=print):
    """One client starts `gamestart`, spawns the 12-ship gate fixture and saves save_009; copied to `out`."""
    sys.path.insert(0, str(ROOT / 'tools/dev'))
    from native_gate_scenarios import build_capture_overlay
    out = Path(out)
    if out.exists():
        raise SystemExit(f'{out} exists')
    if (Path(state_dir) / 'session.json').exists():
        raise SystemExit('a previous session is still recorded; run x4ls clean first')
    if x4_running():
        raise SystemExit('X4 is running; close it first')
    ok, message = steam.status()
    if not ok:
        raise SystemExit(message)
    install = Path(game_dir) if game_dir else game.find_install()
    game.verify_exe(install)
    lanes = Path(lanes) if lanes else Path(os.path.splitdrive(str(install))[0] + '\\x4ls-lanes')
    run_dir = Path(runs) / time.strftime('snapshot-%Y%m%d-%H%M%S')
    overlay = build_capture_overlay(gamestart, None, run_dir / 'overlay')
    o = Options(clients=1, overlay=overlay, state=Path(state_dir))
    view, proc, log_name = lanes / 'capture', None, 'x4ls-capture.log'
    state = {'run': str(run_dir), 'views': [], 'pids': [], 'relay_pid': None}
    save_state(o, state)
    try:
        game.create_view(install, view)
        state['views'].append(str(view))
        save_state(o, state)
        extension.compose(overlay, view / 'extensions', 1, 0, '0' * 32, 1,
                          extra={'ui/x4ls_autostart.lua': Path(__file__).with_name('autostart.lua')},
                          config_tail=f'X4LAN_CONFIG.x4ls_autostart = "{gamestart}"\n')
        profile.prepare(profile.CAPTURE_PROFILE, [*game.dlc_ids(view), extension.EXTENSION_ID], o.width, o.height)
        args = client_command(view, profile.CAPTURE_PROFILE, 1)[:-1] + [log_name]
        proc = subprocess.Popen(args, cwd=view, env=steam.client_environment())
        state['pids'].append(proc.pid)
        save_state(o, state)
        log(f'capture client pid {proc.pid}: starting {gamestart}, waiting for the fixture save')
        started, outcome = time.time(), 'timeout'
        while time.time() - started < timeout:
            time.sleep(2)
            path = profile.profile_dir(profile.CAPTURE_PROFILE) / log_name
            text = path.read_text(errors='replace') if path.is_file() else ''
            if 'NATIVE_SNAPSHOT SAVED' in text:
                time.sleep(5)  # let X4 finish writing the file
                outcome = 'saved'
                break
            if 'NATIVE_SNAPSHOT ERROR' in text or proc.poll() is not None:
                outcome = 'failed'
                break
        if outcome == 'saved':
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(profile.profile_dir(profile.CAPTURE_PROFILE) / 'save' / profile.SNAPSHOT_SLOT, out)
    finally:
        if proc and proc.poll() is None:
            kill(proc.pid)
        time.sleep(2)
        profile.collect(profile.CAPTURE_PROFILE, log_name, run_dir / 'client1')
        for v in state['views']:
            game.remove_view(v)
        (Path(state_dir) / 'session.json').unlink(missing_ok=True)
    log(f'{outcome}: {out if outcome == "saved" else run_dir}')
    return outcome


def monitor(o, run_dir, relay, clients, log):
    started, tiled, stopped = time.time(), False, set()
    while True:
        time.sleep(2)
        if not tiled and time.time() - started > 20:
            tiled = tile([proc.pid for _, _, proc in clients], o.width, o.height) == len(clients)
        for k, profile_id, proc in clients:
            path = profile.profile_dir(profile_id) / f'x4ls-c{k}.log'
            text = path.read_text(errors='replace') if path.is_file() else ''
            failure = FAIL.search(text)
            if failure:
                return {'outcome': 'failed', 'client': k, 'line': failure.group(0), 'seconds': round(time.time() - started)}
            if STOP_OK.search(text):
                stopped.add(k)
            if proc.poll() is not None and k not in stopped:
                return {'outcome': 'failed', 'client': k, 'line': f'X4 exited with {proc.returncode}',
                        'seconds': round(time.time() - started)}
        if relay.poll() is not None and len(stopped) == len(clients):
            return {'outcome': 'passed' if relay.returncode == 0 else 'relay-failed', 'relay_exit': relay.returncode,
                    'seconds': round(time.time() - started)}
        if relay.poll() is not None and relay.returncode != 0:
            return {'outcome': 'relay-failed', 'relay_exit': relay.returncode, 'seconds': round(time.time() - started)}
        if time.time() - started > o.timeout:
            return {'outcome': 'timeout', 'stopped': sorted(stopped), 'seconds': o.timeout}
