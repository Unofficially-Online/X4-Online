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
# ERROR rows fail the session, except the defaults' loud warnings: DEGRADED (a watched status counter grew) and
# DEFAULT_SKIPPED (a default that cannot apply), which the probe writes while the session runs on.
FAIL = re.compile(r'NATIVE_GATE (?:ERROR (?!DEGRADED |DEFAULT_SKIPPED )|REFUSED)|NATIVE_GATE STOP .*reason=(?!1 )[0-9]+|'
                  r'Error while executing.*native_(?:gate|construction|management)_[a-z_]+')
# Play sessions (x4ls play): the same rows are logged to errors.log instead of ending the session; KIND names a row's kind
# (the console shows the first row of each kind per client). The relay's notices go to desyncs.log, never into the game.
KIND = re.compile(r'NATIVE_GATE (ERROR \S+|REFUSED \S+|STOP .*?reason=\d+)|(Error while executing)')
STOP_ANY = re.compile(r'NATIVE_GATE STOP .*?reason=(\d+)')
PLAY_NOTICE = re.compile(r'NATIVE_GATE (DESYNC|RESYNC|CLOCK_SPLIT|STALL|DROP) ')
PLAY_STALL_DROP_S = 15.0  # a client the others waited for this long is dropped (its gate stopped, it froze or it exited)


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
    stop_after_desync: int | None = None  # observe: end the session this many ticks after the first mismatch
    relay_args: list = field(default_factory=list)
    play: bool = False  # x4ls play: silent desyncs, errors logged, a stopped or exited client dropped, no teardown on errors
    stall_drop_ms: float = 0  # relay --stall-drop-ms (protocol 4: needs a lookahead)
    close_when_over: bool = False  # play: end (close the windows, collect) once every client stopped or exited
    # Two or more PCs (x4ls play --remote-slots): these slots run on other PCs (x4ls join); the relay binds `bind` on
    # `port` and takes reports from `allow_from` only; the arm time leaves the joiners `join_wait` seconds more.
    remote_slots: tuple = ()
    bind: str = '127.0.0.1'
    allow_from: tuple = ()
    port: int = 0
    join_wait: int = 240
    public_host: str = ''  # the address the other PCs use (default: bind; the router's when it forwards the port)
    # Hot join (x4ls play --open K, an overlay built with build_play_overlay.py --open K): these slots start absent and a
    # player joins one while the session runs (x4ls join without --arm-at; hotjoin.py). The host's own clients save the
    # join and its file service hands the join files out on the relay's port number (TCP).
    open_slots: tuple = ()
    relay_hook: object = None  # monitor_play hands it every relay log line (hotjoin.HostJoins.line)


@dataclass
class JoinOptions:
    """x4ls join: one client on this PC in a session whose relay runs on another PC (x4ls play --remote-slots)."""
    relay: str          # HOST:PORT, dotted IPv4 (native_gate_relay_host)
    token: str
    slot: int
    clients: int
    arm_at: int | None  # the agreed UTC second every client loads at (printed by the host); None: a hot joiner loads at once
    overlay: Path
    snapshot: Path
    width: int = 1280
    height: int = 720
    game: Path | None = None
    lanes: Path | None = None
    runs: Path = ROOT / '.local/runs'
    state: Path = ROOT / '.local/x4ls'
    startup: int = 70
    steam_fix: bool = False
    timeout: int = 0
    close_when_over: bool = False
    tick_hz: int | None = None
    run_dir: Path | None = None  # default: .local/runs/<time>-join<slot> (a hot join brings its own, with the join files)
    same_pc: bool = False  # test: the host's X4 runs on this PC too (no "X4 is running" refusal; own state file)


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


def overlay_effective(overlay, clients=2):
    """The overlay's config with the session defaults applied (tools/dev/native_gate_defaults.py): {key: Lua value text}."""
    sys.path.insert(0, str(ROOT / 'tools/dev'))
    import native_gate_defaults
    text = (Path(overlay) / 'ui/config.lua.append').read_text()
    for placeholder, value in zip(extension.PLACEHOLDERS, ('5000', '0' * 32, 'p1', str(clients))):
        text = text.replace(placeholder, value)
    values, _ = native_gate_defaults.apply(native_gate_defaults.parse(text))
    return values


def play_settings(overlay, clients=2):
    """(lookahead, tick rate, last pilot tick or None) of a play overlay, the session defaults included."""
    values = overlay_effective(overlay, clients)

    def number(key, default):
        text = str(values.get(key) or '').strip()
        return int(text) if text.isdigit() else default
    window = re.fullmatch(r'\{\s*\d+\s*,\s*(\d+)\s*\}', str(values.get('native_gate_pilot_window') or '').strip())
    return number('native_gate_lookahead', 0), number('native_gate_tick_hz', 60), int(window.group(1)) if window else None


def relay_command(o, port, token, run):
    cmd = [sys.executable, str(RELAY), '--port', str(port), '--token', token, '--clients', str(o.clients),
           '--ticks', str(o.ticks), '--report', str(run / 'native-barrier.json'),
           '--input-journal', str(run / 'native-inputs.jsonl')]
    if o.snapshot:
        cmd.append('--relative-real')
    tick_hz = o.tick_hz or overlay_tick_hz(o.overlay)
    if tick_hz != 60:
        cmd += ['--tick-hz', str(tick_hz)]
    if o.observe or o.play:
        cmd += ['--observe', '--status-file', str(run / 'desync-status.json')]
    if o.play:
        cmd.append('--keep-going')  # a clock split is a notice too
    if o.lookahead:
        cmd += ['--lookahead', str(o.lookahead)]
    if o.stall_drop_ms:
        cmd += ['--stall-drop-ms', f'{o.stall_drop_ms:g}']
    if o.remote_slots or (o.open_slots and o.bind != '127.0.0.1'):
        # the host's own clients send to the bound address, so it is one of the allowed sources too
        cmd += ['--bind', o.bind, '--allow-from', ','.join(dict.fromkeys((*o.allow_from, o.bind)))]
    if o.open_slots:
        from . import hotjoin
        savers = [k for k in range(1, o.clients + 1) if k not in o.open_slots and k not in o.remote_slots]
        cmd += hotjoin.relay_join_args(sorted(o.open_slots), savers, given=o.relay_args)
    return cmd + list(o.relay_args)


IPV4 = re.compile(r'(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}')


def client_tail(arm_at=None, checkpoint_at=None, relay_host=None):
    """The config lines x4ls adds to every client: arm timing, a checkpoint, a relay on another PC."""
    tail = 'X4LAN_CONFIG.native_gate_arm_timeout_ms = 120000\n'  # slack for different load times
    if arm_at:
        tail += f'X4LAN_CONFIG.native_gate_arm_at = {arm_at}\n'
    if checkpoint_at:
        tail += f'X4LAN_CONFIG.native_gate_checkpoint_at = {checkpoint_at}\n'
    if relay_host:
        if not IPV4.fullmatch(relay_host):
            raise SystemExit(f'relay host must be a dotted IPv4 address, not {relay_host!r}')
        tail += f"X4LAN_CONFIG.native_gate_relay_host = '{relay_host}'\n"
    return tail


def remote_config(host, port, token, slot, clients, arm_at):
    '''The session a client on another PC joins, for a launcher without x4ls join (the native Linux runner):
    {"config": the lines to append to the same play overlay's config, plus the same values as fields}.'''
    lines = (f'X4LAN_CONFIG.port = {port}\nX4LAN_CONFIG.token = "{token}"\nX4LAN_CONFIG.slot = "p{slot}"\n'
             f'X4LAN_CONFIG.native_gate_clients = {clients}\n' + client_tail(arm_at, relay_host=host))
    return {'relay_host': host, 'port': port, 'token': token, 'slot': f'p{slot}', 'clients': clients, 'arm_at': arm_at,
            'config': lines}


def join_command(o, host, port, token, slot, arm_at):
    """The x4ls join line the host prints for a remote slot (arm_at None: an open slot's hot join)."""
    if arm_at is None:
        return (f'python tools/x4ls.py join --relay {host}:{port} --token {token} --slot {slot} '
                f'--overlay <a copy of this overlay folder>')
    return (f'python tools/x4ls.py join --relay {host}:{port} --token {token} --slot {slot} --clients {o.clients} '
            f'--arm-at {arm_at} --overlay <this overlay> --snapshot <this snapshot>')


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
    remote = sorted(set(o.remote_slots))
    if remote and (not o.port or not all(2 <= k <= o.clients for k in remote) or not o.allow_from or not o.snapshot
                   or not IPV4.fullmatch(o.bind) or o.bind == '127.0.0.1' or not all(IPV4.fullmatch(a) for a in o.allow_from)
                   or (o.public_host and not IPV4.fullmatch(o.public_host))):
        raise SystemExit("--remote-slots needs --port, slots 2..clients, --bind (this PC's address, not 127.0.0.1) and "
                         '--allow-from (and --public-host) as dotted IPv4, and --snapshot')
    opened = sorted(set(o.open_slots))
    if opened:
        from . import hotjoin
        recipe = hotjoin.recipe_tools().read_recipe(o.overlay)
        if not o.snapshot or not all(2 <= k <= o.clients for k in opened) or set(opened) & set(remote):
            raise SystemExit('--open needs --snapshot and slots 2..clients that are not --remote-slots')
        if recipe is None or recipe['open'] != opened or recipe['players'] != o.clients:
            raise SystemExit(f'--open {",".join(map(str, opened))} needs an overlay built for {o.clients} players with that '
                             f'--open (tools/build_play_overlay.py --players {o.clients} --open ...)')
        if not o.lookahead:
            raise SystemExit('--open needs a protocol 4 overlay (a lookahead): the join and the drop of a joiner use it')
        if o.bind != '127.0.0.1' and (not IPV4.fullmatch(o.bind) or not o.allow_from or not all(IPV4.fullmatch(a) for a in o.allow_from)):
            raise SystemExit('--open with --bind needs --allow-from (the joiners\' addresses) as dotted IPv4')
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
    local = [k for k in range(1, o.clients + 1) if k not in remote and k not in opened]
    port, token = o.port or free_udp_port(), secrets.token_hex(16)
    state = {'run': str(run_dir), 'views': [], 'pids': [], 'relay_pid': None}
    save_state(o, state)
    clients, relay, relay_log, files, save_dirs = [], None, None, None, {}
    # Every client arms and loads at the same UTC second (native_gate_arm_at in the probe): after the load the
    # gate adopts X4's new UI thread only once the first tick is issued, which needs all clients.
    arm_at = int(time.time()) + o.startup * len(local) + 20 + (o.join_wait if remote else 0) if o.snapshot else None
    seed = snapshot_seed(o.snapshot) if o.snapshot else SESSION_SEED  # the same world seed for every client
    log(f'session seed {seed}')
    try:
        relay_log = (run_dir / 'relay.log').open('w')
        relay = subprocess.Popen(relay_command(o, port, token, run_dir), cwd=ROOT, stdout=relay_log,
                                 stderr=subprocess.STDOUT)
        state['relay_pid'] = relay.pid
        save_state(o, state)
        for k in remote:
            log(f'slot {k} plays on another PC; there, within {o.join_wait} s, with the same overlay and snapshot:\n  '
                + join_command(o, o.public_host or o.bind, port, token, k, arm_at))
            # The same session for a launcher without x4ls join (the native Linux runner): run/join-slotK.json.
            join_file = run_dir / f'join-slot{k}.json'
            join_file.write_text(json.dumps(remote_config(o.public_host or o.bind, port, token, k, o.clients, arm_at), indent=2)
                                 + '\n')
            log(f'  the same for another launcher: {join_file}')
        if opened:
            from . import hotjoin, joinfiles
            files = joinfiles.FileService(token, o.bind, port, allow=o.allow_from, slots=opened).start()
            save_dirs = {k: profile.profile_dir(profile.PROFILES[k - 1]) / 'save' for k in local}
            hosting = hotjoin.HostJoins(files, save_dirs, log)
            o.relay_hook = hosting.line
            for k in opened:
                log(f'slot {k} is open: a player joins it while the session runs (on any PC the relay allows), with:\n  '
                    + join_command(o, o.public_host or o.bind, port, token, k, None))
        for k in local:
            view = lanes / f'c{k}'
            game.create_view(install, view)
            state['views'].append(str(view))
            save_state(o, state)
            profile_id = profile.PROFILES[k - 1]
            tail = client_tail(arm_at, o.checkpoint_at, o.bind if remote else None)
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
            if k != local[-1] or remote or opened:  # X4 refuses to start while another process holds EGOSOFT_X4_INSTANCE
                log(f'client {k}: released {instance.release_instance_mutex(proc.pid)} instance mutex handle(s)')
        result = (monitor_play if o.play else monitor)(o, run_dir, relay, clients, log)
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
        if files:
            files.close()
        time.sleep(2)
        for k, profile_id, _ in clients:
            profile.collect(profile_id, f'x4ls-c{k}.log', run_dir / f'client{k}')
            profile.unstage(profile_id)  # every client's: the lane tool shares these profiles (not only hot joins')
        if save_dirs:
            from . import hotjoin
            hotjoin.remove_join_files(save_dirs)
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


def join(o: JoinOptions, log=print):
    """x4ls join: start this PC's client for slot o.slot against the relay on another PC, log as x4ls play does (errors
    in errors.log, nothing ends the session early) and collect into .local/runs/<time>-join<slot>/ when X4 is closed."""
    host, _, port = o.relay.rpartition(':')
    if not IPV4.fullmatch(host) or not port.isdigit() or not 0 < int(port) < 65536:
        raise SystemExit('--relay must be HOST:PORT with a dotted IPv4 host')
    if not re.fullmatch(r'[0-9a-f]{32}', o.token):
        raise SystemExit('--token must be the 32 hex digits the host printed')
    if not 2 <= o.slot <= o.clients <= len(profile.PROFILES):
        raise SystemExit(f'--slot must be 2..clients and clients at most {len(profile.PROFILES)}')
    if (o.state / 'session.json').exists():
        raise SystemExit('a previous session is still recorded; run x4ls clean first')
    if x4_running() and not o.same_pc:
        raise SystemExit('X4 is running; close it first')
    ok, message = steam.status()
    if not ok and not o.steam_fix:
        raise SystemExit(message)
    extension.verify_overlay(o.overlay)
    install = Path(o.game) if o.game else game.find_install()
    game.verify_exe(install)
    if o.arm_at is not None and time.time() > o.arm_at - o.startup:
        raise SystemExit(f'the arm time is {round(o.arm_at - time.time())} s away, less than --startup {o.startup} s: '
                         'the host has to start again')
    lanes = Path(o.lanes) if o.lanes else Path(os.path.splitdrive(str(install))[0] + '\\x4ls-lanes')
    run_dir = o.run_dir or o.runs / (time.strftime('%Y%m%d-%H%M%S') + f'-join{o.slot}')
    run_dir.mkdir(parents=True, exist_ok=bool(o.run_dir))
    state = {'run': str(run_dir), 'views': [], 'pids': [], 'relay_pid': None}
    save_state(o, state)
    view, profile_id, proc, result = lanes / f'c{o.slot}', profile.PROFILES[o.slot - 1], None, {'outcome': 'failed'}
    play = Options(clients=o.clients, overlay=o.overlay, snapshot=o.snapshot, tick_hz=o.tick_hz, width=o.width,
                   height=o.height, timeout=o.timeout, play=True, close_when_over=o.close_when_over)
    try:
        game.create_view(install, view)
        state['views'].append(str(view))
        save_state(o, state)
        target = extension.compose(o.overlay, view / 'extensions', o.slot, int(port), o.token, o.clients, profile_id=profile_id,
                                   config_tail=client_tail(o.arm_at, relay_host=host))
        installed = run_dir / f'client{o.slot}' / 'installed'
        installed.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(target / 'ui/config.lua', installed / 'x4_lan_lab-config.lua')
        profile.prepare(profile_id, [*game.dlc_ids(view), extension.EXTENSION_ID], o.width, o.height, o.snapshot)
        if o.steam_fix:
            steam.fix(o.state / 'steam-fix.json')
        proc = subprocess.Popen(client_command(view, profile_id, o.slot, snapshot_seed(o.snapshot)), cwd=view,
                                env=steam.client_environment())
        state['pids'].append(proc.pid)
        save_state(o, state)
        log(f'client {o.slot}: pid {proc.pid}, profile {profile_id}, relay {host}:{port}, '
            + (f'loads in {round(o.arm_at - time.time())} s' if o.arm_at is not None else 'loads the join save at the start menu'))
        result = monitor_play(play, run_dir, None, [(o.slot, profile_id, proc)], log)
    finally:
        if proc and proc.poll() is None:
            kill(proc.pid)
        time.sleep(2)
        profile.collect(profile_id, f'x4ls-c{o.slot}.log', run_dir / f'client{o.slot}')
        profile.unstage(profile_id)
        for v in state['views']:
            game.remove_view(v)
        (o.state / 'session.json').unlink(missing_ok=True)
        if o.steam_fix and (o.state / 'steam-fix.json').exists():
            steam.restore(o.state / 'steam-fix.json')
    result.update(overlay=str(o.overlay), snapshot=str(o.snapshot), slot=o.slot, relay=o.relay, run_dir=str(run_dir))
    (run_dir / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    log(f"{result['outcome']}: {run_dir}")
    return result


CAPTURE_FAIL = ('NATIVE_SNAPSHOT ERROR', 'SESSION_HULL_FAILED', 'Error in MD cue md.X4LAN_SessionHulls')


def capture_snapshot(out, gamestart='custom_creative', game_dir=None, lanes=None, timeout=900,
                     runs=ROOT / '.local/runs', state_dir=ROOT / '.local/x4ls', log=print, seed=SESSION_SEED, steam_fix=False,
                     overlay=None):
    """One client starts `gamestart` with session seed `seed`, spawns the 12-ship gate fixture and saves
    save_009; copied to `out`. The save header carries the seed, so sessions from it use the same one.
    overlay: a capture overlay built elsewhere (x4ls new-session: native_gate_session_start.build_capture, with the
    per-player start hulls); its hull failures end the capture too."""
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
    if not ok and not steam_fix:
        raise SystemExit(message)
    install = Path(game_dir) if game_dir else game.find_install()
    game.verify_exe(install)
    lanes = Path(lanes) if lanes else Path(os.path.splitdrive(str(install))[0] + '\\x4ls-lanes')
    run_dir = Path(runs) / time.strftime('snapshot-%Y%m%d-%H%M%S')
    overlay = Path(overlay) if overlay else build_capture_overlay(gamestart, None, run_dir / 'overlay')
    run_dir.mkdir(parents=True, exist_ok=True)
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
        if steam_fix:
            steam.fix(Path(state_dir) / 'steam-fix.json')
        args = client_command(view, profile.CAPTURE_PROFILE, 1, seed)[:-1] + [log_name]
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
            if any(mark in text for mark in CAPTURE_FAIL) or proc.poll() is not None:
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
        if steam_fix and (Path(state_dir) / 'steam-fix.json').exists():
            steam.restore(Path(state_dir) / 'steam-fix.json')
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


class LogTail:
    """The complete lines a growing log gained since the last call (a play session's logs reach gigabytes)."""
    def __init__(self, path):
        self.path, self.offset, self.rest = Path(path), 0, b''

    def lines(self):
        try:
            with self.path.open('rb') as f:
                f.seek(self.offset)
                data = f.read(1 << 24)
        except OSError:
            return []
        self.offset += len(data)
        *full, self.rest = (self.rest + data).split(b'\n')
        return [line.decode('utf-8', 'replace').rstrip('\r') for line in full]


def monitor_play(o, run_dir, relay, clients, log):
    """x4ls play: nothing a client logs ends the session. Every row a test session fails on goes to errors.log (the
    console shows the first of each kind per client), the relay's DESYNC/RESYNC/CLOCK_SPLIT/STALL/DROP notices go to
    desyncs.log and the console only (the clients never see them). A client whose gate stops or whose X4 exits is logged;
    the relay drops it after --stall-drop-ms and the others play on. The session is over when every client stopped or
    exited; the launcher then waits until the X4 windows are closed (or Ctrl+C, or --timeout seconds when given)."""
    started, tiled, reported = time.time(), False, time.time()
    tick_hz, armed_at = o.tick_hz or overlay_tick_hz(o.overlay), None
    tails = {k: LogTail(profile.profile_dir(profile_id) / f'x4ls-c{k}.log') for k, profile_id, _ in clients}
    relay_tail = LogTail(run_dir / 'relay.log')
    counts, seen, stopped, exited, over = {}, set(), {}, {}, False

    def count(kind):
        counts[kind] = counts.get(kind, 0) + 1

    def result(ended):
        try:
            status = json.loads((run_dir / 'desync-status.json').read_text())
        except (OSError, ValueError):
            status = {}
        return {'outcome': 'played', 'ended': ended, 'seconds': round(time.time() - started), 'tick': status.get('tick'),
                'first_desync': status.get('first_mismatch'), 'desync_ticks': status.get('mismatches'),
                'clock_splits': status.get('clock_splits'), 'errors': counts.get('error', 0), 'drops': counts.get('drop', 0),
                'stopped': {f'client{k}': reason for k, reason in sorted(stopped.items())},
                'exited': {f'client{k}': code for k, code in sorted(exited.items())},
                'errors_log': str(run_dir / 'errors.log'), 'desyncs_log': str(run_dir / 'desyncs.log')}

    with (run_dir / 'errors.log').open('a', encoding='utf-8') as errors, \
            (run_dir / 'desyncs.log').open('a', encoding='utf-8') as desyncs:
        try:
            while True:
                time.sleep(2)
                stamp = time.strftime('%H:%M:%S')
                for line in relay_tail.lines():
                    if armed_at is None and line.startswith('NATIVE_GATE tick=0 '):
                        armed_at = time.time()
                    if o.relay_hook:
                        o.relay_hook(line)
                    notice = PLAY_NOTICE.match(line)
                    if notice:
                        desyncs.write(f'{stamp} {line}\n')
                        desyncs.flush()
                        count(notice.group(1).lower())
                        log(f'relay: {line}')
                if armed_at and time.time() - reported >= 60:
                    reported = time.time()
                    line = progress(run_dir, tick_hz, armed_at)
                    if line:
                        log(line + (f", {counts['error']} error row(s) in errors.log" if counts.get('error') else ''))
                if not tiled and time.time() - started > 20:
                    tiled = tile([proc.pid for _, _, proc in clients], o.width, o.height) == len(clients)
                for k, _, proc in clients:
                    for line in tails[k].lines():
                        if FAIL.search(line):
                            errors.write(f'{stamp} client{k} {line}\n')
                            errors.flush()
                            count('error')
                            kind = KIND.search(line)
                            kind = (kind.group(1) or kind.group(2)) if kind else 'row'
                            if (k, kind) not in seen:
                                seen.add((k, kind))
                                log(f'client {k}: {kind} (logged, the session runs on; every row in errors.log)')
                        stop = STOP_ANY.search(line)
                        if stop and k not in stopped:
                            stopped[k] = int(stop.group(1))
                            if stopped[k] != 1:
                                log(f'client {k}: its gate stopped (reason {stopped[k]}): it plays on alone, the relay drops it '
                                    f'and the others play on')
                    if proc.poll() is not None and k not in exited:
                        exited[k] = proc.returncode
                        errors.write(f'{stamp} client{k} X4 exited with {proc.returncode}\n')
                        errors.flush()
                        log(f'client {k}: X4 exited ({proc.returncode})' + ('' if over else '; the others play on'))
                if len(exited) == len(clients):
                    return result('every X4 window closed')
                if not over and all(k in stopped or k in exited for k, _, _ in clients):
                    over = True
                    if o.close_when_over:
                        return result('session over')
                    log('the session is over: close the X4 windows (or Ctrl+C) to collect the logs')
                if o.timeout and time.time() - started > o.timeout:
                    return result(f'timeout {o.timeout} s')
        except KeyboardInterrupt:
            return result('Ctrl+C')


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
        if o.stop_after_desync is not None and o.observe:  # a soak need not run hours past its first mismatch
            try:
                status = json.loads((run_dir / 'desync-status.json').read_text())
            except (OSError, ValueError):
                status = {}
            first = status.get('first_mismatch')
            if first is not None and (status.get('tick') or 0) >= first + o.stop_after_desync:
                return {'outcome': 'diverged', 'verdict': 'diverged', 'first_digest_mismatch': first, 'stopped_at': status.get('tick'),
                        'digest_mismatch_ticks': [first], 'seconds': round(time.time() - started)}
        if time.time() - started > o.timeout:
            return {'outcome': 'timeout', 'stopped': sorted(stopped), 'seconds': o.timeout}
