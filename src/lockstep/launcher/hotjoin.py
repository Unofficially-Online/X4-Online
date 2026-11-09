"""Hot join for people: a player joins a running x4ls play session (reports/LOCKSTEP-HOTJOIN-20261101.txt, the J1/W1 tests'
flow without the test harness).

Host, x4ls play --open K (an overlay built with tools/build_play_overlay.py --open K): slot K starts absent; the relay runs
--join --absent K --join-savers <the host's own present slots>, and a join file service (joinfiles.py) listens on the relay's
port number over TCP. HostJoins reads the relay's log: on a join's SAVED notice it finds the saver's j<T>.x4join and
j<T>.xml.gz in that client's profile (save/mpjoin), checks the package against the hash the saver announced and the save
against the hash the package names, and offers both.

Joiner, x4ls join --relay HOST:PORT --token T --slot K --overlay <a copy of the host's overlay> (no --arm-at): NGJOIN to the
relay (resent as a poll) until the state is saved, then both files from the host's file service, verified; the joiner's
overlay is the host's recipe (play-recipe.json) rebuilt with the join save and the package (build_play_overlay.py
--join-package, checked as native_gate_join_overlays.py checks a joiner's), and X4 starts on this PC with the join save as
its snapshot: it loads at once (no arm time), the relay replays the session's rows from T to it and hands slot K over at H.
"""
from dataclasses import dataclass
import json
from pathlib import Path
import re
import secrets
import socket
import subprocess
import sys
import threading
import time

from . import joinfiles, profile

ROOT = Path(__file__).resolve().parents[3]
DEV = ROOT / 'tools/dev'
SAVED = re.compile(r'NATIVE_GATE JOIN slot=(\d+) saved slot=(\d+) tick=(\d+) bytes=(\d+) package=([0-9a-f]{64}) '
                   r'portable=([0-9a-f]{32})')
JOIN_NOTICE = re.compile(r'NATIVE_GATE JOIN slot=\d+ ')
STATES = ('requested', 'queued', 'saved', 'catching_up', 'handover', 'joined', 'failed')


def recipe_tools():
    sys.path.insert(0, str(ROOT / 'tools'))
    import build_play_overlay
    return build_play_overlay


def join_tools():
    sys.path.insert(0, str(DEV))
    import native_gate_join
    return native_gate_join


# A people session's joiner is seldom faster than the session (on one PC it shares the CPU; hj2 trailed by its load time to
# the end): the relay hands over soon (20 s instead of 300 s of catch-up) and may pause everyone up to 180 s (instead of 60)
# while the joiner closes the gap, rather than refusing the join (pause_too_long). A host's own --join-* arguments win.
JOIN_TIMING = (('--join-catchup-limit', '20'), ('--join-max-pause', '180'))


def relay_join_args(open_slots, savers, given=(), observe=False):
    """The relay arguments of a session with hot-joinable slots. observe (x4ls play: the session's desyncs are logged, not
    fatal): a joiner whose digest differs during its catch-up is handed over all the same, the mismatches counted and logged
    (relay --join-observe; hj8..hj13, 8-9 Nov: the joiner equal for 1 to 9 ticks, then one step apart)."""
    args = ['--join', '--absent', ','.join(map(str, open_slots)), '--join-savers', ','.join(map(str, savers))]
    for key, value in JOIN_TIMING:
        if key not in given:
            args += [key, value]
    if observe and '--join-observe' not in given:
        args.append('--join-observe')
    return args


class HostJoins:
    """The host's side: offer each join's files once the relay announced them (see the module text)."""
    def __init__(self, service, save_dirs, log=print, wait_s=60):
        self.service, self.save_dirs, self.log, self.wait_s = service, dict(save_dirs), log, wait_s
        self.offered, self.failed, self.threads = {}, {}, []

    def line(self, line):
        if not JOIN_NOTICE.match(line):
            return
        self.log(f'relay: {line}')
        m = SAVED.match(line)
        if not m:
            return
        slot, saver, tick, package_sha = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(5)
        thread = threading.Thread(target=self.offer, args=(slot, saver, tick, package_sha), daemon=True)
        self.threads.append(thread)
        thread.start()

    def offer(self, slot, saver, tick, package_sha):
        folder = self.save_dirs.get(saver)
        key = (slot, tick, saver)
        if folder is None:
            self.failed[key] = 'saver_not_local'
            self.log(f'join slot {slot}: saver {saver} does not play on this PC; its files cannot be served from here')
            return
        package, save = Path(folder) / 'mpjoin' / f'j{tick}.x4join', Path(folder) / 'mpjoin' / f'j{tick}.xml.gz'
        deadline = time.monotonic() + self.wait_s
        while not (package.is_file() and save.is_file()) and time.monotonic() < deadline:
            time.sleep(0.5)
        join = join_tools()
        try:
            loaded = join.read_package(package.read_bytes())
            if loaded['sha'] != package_sha:
                raise ValueError(f'the package is {loaded["sha"][:16]}.., the saver announced {package_sha[:16]}..')
            self.service.offer(package.name, package, package_sha)
            size, _ = self.service.offer(save.name, save, loaded['header']['save_sha'])
        except (OSError, ValueError, join.JoinError) as error:
            self.failed[key] = str(error)
            self.log(f'join slot {slot}: the saver\'s files are not served ({error})')
            return
        self.offered[key] = (package, save)
        self.log(f'join slot {slot}: serving j{tick} from saver {saver} ({(package.stat().st_size + size) >> 20} MB) '
                 f'on TCP port {self.service.port}')


def remove_join_files(save_dirs):
    """The join saves and packages a session left in its clients' disposable profiles (hundreds of MB each)."""
    for folder in save_dirs.values():
        for path in (Path(folder) / 'mpjoin').glob('j*.*'):
            if joinfiles.NAME.fullmatch(path.name):
                path.unlink(missing_ok=True)


# ---- the joiner ----
@dataclass
class HotJoinOptions:
    """x4ls join without --arm-at: join slot `slot` of a running session (x4ls play --open) at `relay`."""
    relay: str          # HOST:PORT of the session's relay; its join file service listens on the same port number (TCP)
    token: str
    slot: int
    overlay: Path       # a copy of the host's play overlay (its play-recipe.json and gate DLL)
    wait: int = 600     # seconds for the session to save the join (it saves at its next join tick)
    nonce: str | None = None   # resume a join asked for before (default: the last one of this slot and relay, if recorded)
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
    same_pc: bool = False  # test: the host runs on this PC too (session.JoinOptions.same_pc)


def request(host, port, token, slot, wait_s=600, log=print, sock=None, interval=1.0, nonce=None):
    """NGJOIN until the relay answers saved (or failed): the state's fields as a dict. Raises SystemExit. nonce: resume the
    join this PC asked for earlier (the relay keeps one join per slot and answers another nonce busy)."""
    join = join_tools()
    nonce = nonce or secrets.token_hex(8)
    own = sock is None
    sock = sock or socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(interval)
    seen, deadline = None, time.monotonic() + wait_s
    try:
        while time.monotonic() < deadline:
            sock.sendto(join.message('NGJOIN', token, slot, nonce), (host, port))
            try:
                data, _ = sock.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                continue
            f = join.parse(data, token, 'NGJOINSTATE', 8)
            if not f or f[0] != str(slot) or f[1] != nonce:
                continue
            state = {'slot': slot, 'nonce': nonce, 'state': f[2], 'tick': int(f[3]) if f[3].isdigit() else 0,
                     'saver': int(f[4]) if f[4].isdigit() else 0, 'bytes': int(f[5]) if f[5].isdigit() else 0,
                     'package_sha': f[6], 'detail': f[7]}
            if state['state'] != seen:
                seen = state['state']
                log(f"join: {seen}" + (f" (save tick {state['tick']})" if state['tick'] else '') +
                    (f" {state['detail']}" if state['detail'] not in ('-', '') else ''))
            if state['state'] == 'failed':
                raise SystemExit(f"the relay refused the join: {state['detail']}")
            if state['state'] == 'saved':
                if not joinfiles.HEX64.fullmatch(state['package_sha']) or state['tick'] <= 0:
                    raise SystemExit(f'the relay announced no usable package: {state}')
                return state
            if state['state'] not in STATES:
                raise SystemExit(f"unknown join state {state['state']!r}")
        raise SystemExit(f'no saved join after {wait_s} s (state {seen or "none: is the relay reachable, the token right?"})')
    finally:
        if own:
            sock.close()


def previous_nonce(runs, slot, relay, max_age_s=900):
    """The nonce of this PC's last join of slot at relay (its state.json), if it is younger than the relay's joiner timeout."""
    for path in sorted(Path(runs).glob(f'*-hotjoin{slot}/join/state.json'), reverse=True):
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        if data.get('relay') == relay and time.time() - path.stat().st_mtime < max_age_s and re.fullmatch(r'[0-9a-f]{16}', str(data.get('nonce'))):
            return data['nonce']
        return None
    return None


def fetch_files(host, port, token, slot, state, folder, log=print):
    """The join package and save of a saved join into folder, verified: (save, package, the package's save SHA-256)."""
    join = join_tools()
    tick, folder = state['tick'], Path(folder)
    shown = {}

    def progress(name, got, size):
        step = got * 10 // max(size, 1)
        if shown.get(name) != step:
            shown[name] = step
            log(f'download {name}: {got >> 20} of {size >> 20} MB')
    package = folder / f'j{tick}.x4join'
    joinfiles.fetch(host, port, token, slot, package.name, package, expect_sha=state['package_sha'], progress=progress)
    try:
        loaded = join.read_package(package.read_bytes())
    except join.JoinError as error:
        raise SystemExit(f'the join package does not verify: {error}') from None
    if loaded['sha'] != state['package_sha'] or loaded['sections']['gate']['tick'] != tick:
        raise SystemExit('the join package is not the one the relay announced')
    save = folder / f'j{tick}.xml.gz'
    joinfiles.fetch(host, port, token, slot, save.name, save, expect_sha=loaded['header']['save_sha'], progress=progress)
    return save, package, loaded


def build_joiner_overlay(host_overlay, save, package, package_sha, out, log=print):
    """The joiner's overlay: the host overlay's recipe with the join save and package (build_play_overlay.py)."""
    tools = recipe_tools()
    recipe = tools.read_recipe(host_overlay)
    if recipe is None or not recipe['open']:
        raise SystemExit(f'{host_overlay}: not a hot-joinable play overlay (build it with tools/build_play_overlay.py --open K)')
    dll = Path(host_overlay) / 'native/x4lan_gate_64.dll'
    if tools.sha256(dll) != recipe['gate_dll_sha']:
        raise SystemExit(f'{dll}: not the gate the overlay was built with')
    session = None
    if recipe.get('session'):
        session = Path(out).parent / 'session.json'   # the session's per-player starts, as the host built them
        session.write_text(json.dumps(recipe['session'], indent=1, sort_keys=True) + '\n', encoding='utf-8')
    cmd = [sys.executable, str(ROOT / 'tools/build_play_overlay.py'),
           *tools.joiner_command(recipe, save, dll, out, package, package_sha, session)]
    log(f'joiner overlay: {out}')
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.stdout.write(result.stdout[-3000:])
        sys.stderr.write(result.stderr[-3000:])
        raise SystemExit(f'the joiner overlay was refused ({result.returncode})')
    line = next((l for l in result.stdout.splitlines() if l.startswith('joiner: ')), '')
    if line:
        log(line)
    return Path(out), recipe


def hot_join(o, log=print):
    """x4ls join without --arm-at: request slot o.slot of the session at o.relay, fetch and verify its join files, build the
    joiner's overlay and play the slot on this PC (session.join with the join save as the snapshot and no arm time)."""
    from . import session
    host, _, port = o.relay.rpartition(':')
    if not session.IPV4.fullmatch(host) or not port.isdigit() or not 0 < int(port) < 65536:
        raise SystemExit('--relay must be HOST:PORT with a dotted IPv4 host')
    if not re.fullmatch(r'[0-9a-f]{32}', o.token):
        raise SystemExit('--token must be the 32 hex digits the host printed')
    recipe = recipe_tools().read_recipe(o.overlay)
    if recipe is None or o.slot not in recipe['open']:
        raise SystemExit(f'{o.overlay}: slot {o.slot} is not an open slot of this overlay (build_play_overlay.py --open)')
    if (o.state / 'session.json').exists():
        raise SystemExit('a previous session is still recorded; run x4ls clean first')
    if session.x4_running() and not o.same_pc:
        raise SystemExit('X4 is running; close it first')
    folder = o.runs / (time.strftime('%Y%m%d-%H%M%S') + f'-hotjoin{o.slot}')
    nonce = o.nonce or previous_nonce(o.runs, o.slot, o.relay)
    (folder / 'join').mkdir(parents=True)
    port = int(port)
    if nonce:
        log(f'join: resuming the join of nonce {nonce}')
    state = request(host, port, o.token, o.slot, o.wait, log, nonce=nonce)
    state['relay'] = o.relay
    (folder / 'join/state.json').write_text(json.dumps(state, indent=1) + '\n')
    save, package, loaded = fetch_files(host, port, o.token, o.slot, state, folder / 'join', log)
    log(f"join: T={state['tick']} saver={state['saver']} save {save.stat().st_size >> 20} MB, package "
        f"{package.stat().st_size >> 20} MB, verified")
    overlay, recipe = build_joiner_overlay(o.overlay, save, package, state['package_sha'], folder / 'overlay', log)
    return session.join(session.JoinOptions(relay=o.relay, token=o.token, slot=o.slot, clients=recipe['players'],
                                            arm_at=None, overlay=overlay, snapshot=save, width=o.width, height=o.height,
                                            game=o.game, lanes=o.lanes, runs=o.runs, state=o.state, startup=o.startup,
                                            steam_fix=o.steam_fix, timeout=o.timeout, close_when_over=o.close_when_over,
                                            tick_hz=None, run_dir=folder, same_pc=o.same_pc), log)
