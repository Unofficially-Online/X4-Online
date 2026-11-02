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

from . import extension, game, instance, profile, steam

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
    tick_hz: int | None = None  # default: the overlay's native_gate_tick_hz (60 when unset)
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
    fast: bool = False  # render-only settings down, no frame cap, no sound (profile.FAST)
    startup: int = 70  # seconds budgeted per client to reach the start menu; snapshot clients then arm together
    steam_fix: bool = False  # re-register Steam's pid before each client (see steam.py); restored at the end
    checkpoint_at: int | None = None  # every client saves save_001 after this tick; the session ends there
    relay_args: list = field(default_factory=list)


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def x4_running():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq X4.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True).stdout
    return '"X4.exe"' in out


def overlay_tick_hz(overlay):
    m = re.search(r'native_gate_tick_hz\s*=\s*(\d+)', (Path(overlay) / 'ui/config.lua.append').read_text())
    return int(m.group(1)) if m else 60


def relay_command(o, port, token, run):
    cmd = [sys.executable, str(RELAY), '--port', str(port), '--token', token, '--clients', str(o.clients),
           '--ticks', str(o.ticks), '--report', str(run / 'native-barrier.json'),
           '--input-journal', str(run / 'native-inputs.jsonl')]
    if o.snapshot:
        cmd.append('--relative-real')
    tick_hz = o.tick_hz or overlay_tick_hz(o.overlay)
    if tick_hz != 60:
        cmd += ['--tick-hz', str(tick_hz)]
    if o.observe:
        cmd += ['--observe', '--status-file', str(run / 'desync-status.json')]
    if o.lookahead:
        cmd += ['--lookahead', str(o.lookahead)]
    return cmd + list(o.relay_args)


SESSION_SEED = 276944566  # fallback when there is no snapshot (capture runs)


def snapshot_seed(path):
    """The session seed stored in a save's header (<info><game seed=...>): the world's own seed, so every
    client of every session that loads this save uses the same one."""
    import gzip
    with gzip.open(path, 'rb') as f:
        head = f.read(8192).decode('utf-8', 'replace')
    m = re.search(r'<game [^>]*\bseed="(\d+)"', head)
    if not m:
        raise SystemExit(f'{path}: no seed in the save header')
    return int(m.group(1))


def client_command(view, profile_id, k, seed=SESSION_SEED):
    # -seed: X4's session seed; JobDB seeds every job's generator at import from job ID xor this seed
    # (lockstep_spawn_rng.h), so without one each client spawns different job ships from tick 0.
    # -nocputhrottle: background client windows keep full speed.
    return [str(view / 'X4.exe'), '-personalfolderid', str(profile_id), '-skipintro', '-noworkshopsync',
            '-nocputhrottle', f'-seed:{seed}', '-debug', 'scripts', '-logfile', f'x4ls-c{k}.log']


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


def wait_for_log(path, text, proc, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.is_file() and text in path.read_text(errors='replace'):
            return
        if proc.poll() is not None:
            raise SystemExit(f'X4 exited ({proc.returncode}) before "{text}" in {path.name}')
        time.sleep(1)
    raise SystemExit(f'no "{text}" in {path.name} after {timeout} s (a Steam or startup dialog may be open)')


def log_tail(path, size=1 << 20):
    """The last `size` bytes of a log (the client logs grow to hundreds of MB)."""
    if not path.is_file():
        return ''
    with open(path, 'rb') as f:
        f.seek(max(0, path.stat().st_size - size))
        return f.read().decode(errors='replace')


def checkpoint_results(path, proc, timeout=300):
    """Wait until a client has answered every CHECKPOINT_REQUEST; {tick: saved|failed|refused}, None if one never came."""
    deadline = time.time() + timeout
    while True:
        text = log_tail(path, 16 << 20)
        requested = {int(t) for t in re.findall(r'CHECKPOINT_REQUEST slot=save_\d+ tick=(\d+)', text)}
        answered = {int(t): r for r, t in re.findall(r'CHECKPOINT (saved|failed|refused) slot=save_\d+ tick=(\d+)', text)}
        if requested <= answered.keys():
            return dict(sorted(answered.items()))
        if proc.poll() is not None or time.time() > deadline:
            return None
        time.sleep(2)


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
    if (Path(state_dir) / 'steam-fix.json').exists():
        steam.restore(Path(state_dir) / 'steam-fix.json')
    return f"cleaned session {data.get('run')}"


def run(o: Options, log=print):
    if not 2 <= o.clients <= len(profile.PROFILES):
        raise SystemExit(f'clients must be 2..{len(profile.PROFILES)}')
    if (o.state / 'session.json').exists():
        raise SystemExit('a previous session is still recorded; run x4ls clean first')
    if x4_running():
        raise SystemExit('X4 is running; close it first')
    ok, message = steam.status()
    if not ok and not o.steam_fix:
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
    # Every client arms and loads at the same UTC second (native_gate_arm_at in the probe): after the load the
    # gate adopts X4's new UI thread only once the first tick is issued, which needs all clients.
    arm_at = int(time.time()) + o.startup * o.clients + 20 if o.snapshot else None
    seed = snapshot_seed(o.snapshot) if o.snapshot else SESSION_SEED  # the same world seed for every client
    log(f'session seed {seed}')
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
            profile_id = profile.PROFILES[k - 1]
            tail = 'X4LAN_CONFIG.native_gate_relay_timeout_ms = 120000\n'  # slack for different load times
            if arm_at:
                tail += f'X4LAN_CONFIG.native_gate_arm_at = {arm_at}\n'
            if o.checkpoint_at:
                tail += f'X4LAN_CONFIG.native_gate_checkpoint_at = {o.checkpoint_at}\n'
            target = extension.compose(o.overlay, view / 'extensions', k, port, token, o.clients, profile_id=profile_id,
                                       config_tail=tail)
            installed = run_dir / f'client{k}' / 'installed'  # the census tools read the installed config here
            installed.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(target / 'ui/config.lua', installed / 'x4_lan_lab-config.lua')
            profile.prepare(profile_id, [*game.dlc_ids(view), extension.EXTENSION_ID], o.width, o.height, o.snapshot, fast=o.fast)
            if o.steam_fix:
                steam.fix(o.state / 'steam-fix.json')  # Steam can reset it once a client connects
            proc = subprocess.Popen(client_command(view, profile_id, k, seed), cwd=view, env=steam.client_environment())
            clients.append((k, profile_id, proc))
            state['pids'].append(proc.pid)
            save_state(o, state)
            log(f'client {k}: pid {proc.pid}, profile {profile_id}, view {view}')
            # Start the next client only once this one has reached the start menu.
            wait_for_log(profile.profile_dir(profile_id) / f'x4ls-c{k}.log', 'Entering startmenu', proc, 180)
            if arm_at and time.time() > arm_at - 5:
                raise SystemExit(f'client {k} reached the start menu after the common arm time; raise --startup')
            if k < o.clients:  # X4 refuses to start while another process holds EGOSOFT_X4_INSTANCE
                log(f'client {k}: released {instance.release_instance_mutex(proc.pid)} instance mutex handle(s)')
        result = monitor(o, run_dir, relay, clients, log)
        if o.checkpoint_at:  # a save may still be writing when the relay ends
            result['checkpoints'] = {f'client{k}': checkpoint_results(profile.profile_dir(profile_id) / f'x4ls-c{k}.log', proc)
                                     for k, profile_id, proc in clients}
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
        if o.steam_fix and (o.state / 'steam-fix.json').exists():
            steam.restore(o.state / 'steam-fix.json')
    result.update(overlay=str(o.overlay), snapshot=str(o.snapshot) if o.snapshot else None, ticks=o.ticks,
                  run_dir=str(run_dir))  # for x4ls triage
    saves = result.get('checkpoints') or {}
    for client, ticks in saves.items():
        for tick, outcome in (ticks or {}).items():
            log(f'checkpoint {client} tick {tick}: {outcome}' + (f" ({run_dir / client / 'save_001.xml.gz'})" if outcome == 'saved' else ''))
    # The save pauses X4 (game speed 0) and the gate stops with reason 3: the expected end of a checkpoint session.
    if saves and all(t and set(t.values()) == {'saved'} for t in saves.values()) and result['outcome'] == 'failed' and \
            re.search(r'completed=(\d+) reason=3\b', result.get('line', '')) and \
            int(re.search(r'completed=(\d+)', result['line']).group(1)) >= o.checkpoint_at - 1:
        result['outcome'] = 'checkpointed'
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


def progress(run_dir, tick_hz, since):
    """'tick N (game Xs, Y.YYx real time)' from the relay log, plus the first desync if any."""
    try:
        lines = (run_dir / 'relay.log').read_text(errors='replace').splitlines()
    except OSError:
        return None
    ticks = [l for l in lines if l.startswith('NATIVE_GATE tick=')]
    if not ticks:
        return None
    tick = int(re.match(r'NATIVE_GATE tick=(-?\d+)', ticks[-1]).group(1))
    game_s, real_s = tick / tick_hz, time.time() - since
    desync = re.search(r'first_mismatch=(\d+) mismatches=(\d+)', ticks[-1])
    return (f'tick {tick} (game {game_s / 60:.1f} min, {game_s / max(real_s, 1):.2f}x real time)'
            + (f', desync since tick {desync.group(1)} ({desync.group(2)} ticks)' if desync else ', in sync'))


def monitor(o, run_dir, relay, clients, log):
    started, tiled, stopped, reported = time.time(), False, set(), time.time()
    tick_hz, armed_at = o.tick_hz or overlay_tick_hz(o.overlay), None
    while True:
        time.sleep(2)
        if armed_at is None and 'NATIVE_GATE tick=0' in ((run_dir / 'relay.log').read_text(errors='replace')
                                                         if (run_dir / 'relay.log').is_file() else ''):
            armed_at = time.time()
        if armed_at and time.time() - reported >= 60:
            reported = time.time()
            line = progress(run_dir, tick_hz, armed_at)
            if line:
                log(line)
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
        if len(stopped) == len(clients):
            # The relay stays up after done=True; its report carries the verdict.
            report = run_dir / 'native-barrier.json'
            verdict = json.loads(report.read_text()).get('verdict') if report.is_file() else None
            if verdict in ('pass', 'fail', 'diverged'):
                data = json.loads(report.read_text())
                return {'outcome': {'pass': 'passed', 'diverged': 'diverged'}.get(verdict, 'relay-failed'), 'verdict': verdict,
                        'common_ticks': len(data.get('common_ticks') or []), 'digest_mismatch_ticks': data.get('digest_mismatch_ticks'),
                        'first_digest_mismatch': data.get('first_digest_mismatch'), 'seconds': round(time.time() - started)}
        if relay.poll() is not None and len(stopped) == len(clients):
            return {'outcome': 'passed' if relay.returncode == 0 else 'relay-failed', 'relay_exit': relay.returncode,
                    'seconds': round(time.time() - started)}
        if relay.poll() is not None and relay.returncode != 0:
            return {'outcome': 'relay-failed', 'relay_exit': relay.returncode, 'seconds': round(time.time() - started)}
        if time.time() - started > o.timeout:
            return {'outcome': 'timeout', 'stopped': sorted(stopped), 'seconds': o.timeout}
