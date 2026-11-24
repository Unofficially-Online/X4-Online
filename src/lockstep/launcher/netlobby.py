"""x4ls lobby with netplay: this PC's X4 opens on the Multiplayer page and the game is hosted or joined there, at the
master server or on the LAN (docs/NETPLAY.md; the requests and the state table are netplay.py's).

Every kit but one with a server (x4ls serve: lobby.lobby_run, as it always was) comes here: a release kit, a playtest
kit, this checkout. The launcher's X4 runs in its lane's view and disposable profile, as x4ls lobby's always did:
- At its start it runs the kit's own session in lobby form (a playtest kit: its overlay installed, the snapshot
  staged), or the Multiplayer page alone (a release kit has no session until a game is chosen).
- Hosting a game a release kit has no session for yet: X4 closes, x4ls new-session makes the session (its capture
  client needs the capture profile and X4 closed), and X4 starts again with it. The session is kept in the kit's
  sessions/ for the next game of that many players.
- Joining: the session's files are downloaded and the overlay built here; X4 starts again with them, because what X4
  reads only at its start (the session seed on the command line, the extension's libraries and macros) must be the
  session's. Then READY.
- The start: this launcher writes the session into the extension's config.lua, the page runs the overlay's files, and
  from here on it is the session x4ls play and x4ls join make (session.monitor_play), with the relay on this PC in
  direct and LAN modes. The state keeps being written for the probe's "Multiplayer session" page.

A pump thread carries the page's requests (rows of the game's log) to the flow and the flow's state to
lobby/state.lua (and to --state-mirror, which the Steam helper answers its pipe with)."""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

from . import extension, game, instance, lobby, netkit, netplay, profile, session, steam

PUMP_S = 0.2
START_MENU_WAIT_S = 240
LATE_OFFER_WAIT_S = 780   # a drop-in joiner: the files go from the saver's PC to the master first (as lobby.join_running)


@dataclass(frozen=True)
class Form:
    """What this lane's X4 runs: the Multiplayer page alone (overlay None), or a session's overlay in lobby form with its
    save staged."""
    overlay: Path | None = None
    snapshot: Path | None = None
    clients: int = 0

    def same(self, other):
        if other is None:
            return False
        return (resolved(self.overlay), resolved(self.snapshot)) == (resolved(other.overlay), resolved(other.snapshot))


def resolved(path):
    return str(Path(path).resolve()).lower() if path else ''


def install_page_only(extensions, show=False):
    """The extension of an X4 that waits on the Multiplayer page without a session (a release kit): the base files,
    the page and an empty lobby config."""
    target = Path(extensions) / extension.EXTENSION_ID
    if target.exists():
        raise SystemExit(f'{target} already exists')
    shutil.copytree(extension.BASE, target)
    shutil.copyfile(lobby.LOBBY_LUA, target / lobby.LOBBY_UI)
    (target / 'content.xml').write_text(extension.CONTENT)
    (target / 'ui.xml').write_text(extension.ui_xml([*extension.BASE_UI, lobby.LOBBY_UI]))
    (target / 'ui' / 'config.lua').write_text(lobby.lobby_config([], show, netplay=True))
    (target / 'lobby').mkdir()
    return target, []


class LobbyGame:
    """This lane's X4 (see the module text): started in its view and profile, started again with another session."""

    def __init__(self, o, install, view, lane, run_dir, record, save_record, log):
        self.o = o
        self.install = install
        self.view = view
        self.lane = lane
        self.profile_id = profile.PROFILES[lane - 1]
        self.log_name = f'x4ls-c{lane}.log'
        self.run_dir = run_dir
        self.record = record
        self.save_record = save_record
        self.log = log
        self.lock = threading.RLock()
        self.proc = None
        self.form = None
        self.target = None
        self.files = []
        self.generation = 0
        self.busy = False      # stopped or starting on purpose: its exit is not the player closing X4
        self.started_once = False

    @property
    def log_path(self):
        return profile.profile_dir(self.profile_id) / self.log_name

    def runs(self, form):
        with self.lock:
            return self.proc is not None and self.proc.poll() is None and form.same(self.form)

    def exited(self):
        with self.lock:
            return self.proc is not None and self.proc.poll() is not None and not self.busy

    def hold(self):
        """Close X4 for a while (a session is being made); start() brings it back."""
        with self.lock:
            self.busy = True
            self.stop_process()

    def start(self, form):
        """X4 with this form: the view made again, the extension installed, the profile wiped and staged."""
        with self.lock:
            self.busy = True
            try:
                self.stop_process()
                if self.view.exists():
                    game.remove_view(self.view)
                game.create_view(self.install, self.view)
                if str(self.view) not in self.record['views']:
                    self.record['views'].append(str(self.view))
                extensions = self.view / 'extensions'
                if form.overlay:
                    self.target, self.files = lobby.install_lobby(form.overlay, extensions, form.clients, self.profile_id,
                                                                  self.o.show, False, netplay=True)
                else:
                    self.target, self.files = install_page_only(extensions, self.o.show)
                profile.prepare(self.profile_id, [*game.dlc_ids(self.view), extension.EXTENSION_ID], self.o.width,
                                self.o.height, form.snapshot)
                seed = session.snapshot_seed(form.snapshot) if form.snapshot else session.SESSION_SEED
                self.proc = subprocess.Popen(session.client_command(self.view, self.profile_id, self.lane, seed),
                                             cwd=self.view, env=steam.client_environment())
                self.record['pids'].append(self.proc.pid)
                self.save_record()
                self.form = form
                self.generation += 1
                self.started_once = True
                what = f'the session {Path(form.overlay).name}' if form.overlay else 'the Multiplayer page'
                self.log(f'lobby: X4 pid {self.proc.pid}, profile {self.profile_id}, view {self.view}: {what}')
            except BaseException:
                self.busy = False
                raise
        session.wait_for_log(self.log_path, 'Entering startmenu', self.proc, START_MENU_WAIT_S)
        self.log(f'lobby: released {instance.release_instance_mutex(self.proc.pid)} instance mutex handle(s)')
        with self.lock:
            self.busy = False

    def stop_process(self):
        """End X4 (if it runs) and keep its log: a restart wipes the profile."""
        if self.proc is None:
            return
        if self.proc.poll() is None:
            session.kill(self.proc.pid)
            try:
                self.proc.wait(30)
            except subprocess.TimeoutExpired:
                pass
        time.sleep(2)
        out = self.run_dir / f'client{self.lane}-start{self.generation}'
        profile.collect(self.profile_id, self.log_name, out)
        profile.unstage(self.profile_id)
        self.proc = None


class LobbyBackend(netplay.Backend):
    """netplay's backend in x4ls lobby: sessions are made and installed here, in this lane's X4."""

    def __init__(self, o, kit, lobby_game, run_dir, log):
        self.o = o
        self.kit = kit
        self.game = lobby_game
        self.run_dir = run_dir
        self.log = log
        self.own = None

    def kit_session(self):
        if self.own is None:
            self.own = self.kit.own_session()
        return self.own

    def installed_dlcs(self):
        return game.installed_dlcs(self.game.install)

    def default_form(self):
        own = self.kit_session()
        if own is None:
            return Form()
        return Form(own.overlay, own.snapshot, own.players)

    def work_folder(self):
        folder = self.run_dir / 'netplay'
        folder.mkdir(parents=True, exist_ok=True)
        return folder

    def idle_lines(self):
        own = self.kit_session()
        lines = []
        if own is not None:
            recipe = own.recipe
            lines.append(f'{own.players} players, {recipe.get("mode", "coop")}, {recipe.get("minutes", "?")} minutes.')
        lines.append(netplay.IDLE_LINES[0])
        if not self.o.master_url:
            lines.append('No master server for this kit: games on the LAN, by invite code or by address.')
        return lines

    def host_session(self, players, progress, cancelled, open_slots=()):
        mode = self.o.mode or netkit.HOST_SESSION_MODE
        base_start = self.o.base_start or netkit.HOST_SESSION_START
        cached = netkit.cached_session(self.kit, players, mode, base_start, open_slots)
        if cached is not None:
            return cached
        opened = f' (drop-in slots {", ".join(map(str, open_slots))})' if open_slots else ''
        self.log(f'lobby: no session for {players} players{opened} yet: X4 closes while x4ls new-session makes one')
        self.game.hold()
        try:
            return netkit.make_session(self.kit, players, mode, base_start, progress, self.log, cancelled,
                                       steam_fix=self.o.steam_fix, open_slots=open_slots, game=self.o.game,
                                       lanes=self.o.lanes)
        except BaseException:
            self.game.start(self.default_form())   # back to the page, which says why
            raise

    def save_folder(self):
        return profile.profile_dir(self.game.profile_id) / 'save'

    def install_session(self, files, progress):
        form = Form(files.overlay, files.snapshot, files.players)
        if self.game.runs(form):
            return
        extension.verify_overlay(files.overlay)
        progress(0.1)
        self.game.start(form)
        progress(1.0)


class Pump:
    """The page and the flow, every 0.2 s: requests from the game's log to the flow, the state to lobby/state.lua."""

    def __init__(self, lobby_game, flow, mirror, log):
        self.game = lobby_game
        self.flow = flow
        self.mirror = Path(mirror) if mirror else None
        self.log = log
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, name='netplay-pump', daemon=True)

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.stopped.set()
        self.thread.join(5)

    def run(self):
        reader = None
        generation = None
        written = None
        while not self.stopped.wait(PUMP_S):
            target = self.game.target
            if self.game.generation != generation and target is not None:
                generation = self.game.generation
                reader = lobby.Request(self.game.log_path)   # a new X4: its own log from the start
                written = None
            text = reader.poll() if reader is not None else None
            if text:
                self.log(f'lobby: the page asks {text[:80]}')
                self.flow.request(text)
            stamp = self.flow.state.view()[0]
            if stamp != written and target is not None:
                written = stamp
                self.write(target / 'lobby' / 'state.lua', self.flow.state.text())

    def write(self, path, text):
        for destination in (path, self.mirror):
            if destination is None:
                continue
            try:
                lobby.write_atomic(destination, text)
            except OSError:   # the view is being made again (a restart): the next stamp writes it
                pass


def auto_request(auto):
    """--auto (an older test switch) as a request: host, join (the clipboard), or an invite code."""
    if not auto:
        return ''
    if auto == 'host':
        return 'HOST'
    if auto == 'join':
        return 'JOIN'
    return 'JOINCODE|' + auto


def master_url(o, kit):
    """--master, else the kit's (masterclient.master_url: X4MP_MASTER, else master.json beside the kit); None with
    --no-master or without package 2's client."""
    if o.no_master:
        return None
    if o.master:
        return o.master
    module = netplay.Network().get('masterclient')
    finder = getattr(module, 'master_url', None) if module else None
    if not callable(finder):
        return None
    return finder(kit.root)


def run(o, log=print):
    """x4ls lobby for every kit without a server: see the module text."""
    kit = netkit.Kit(o.kit or netkit.kit_root_of_checkout(), overlay=o.overlay, snapshot=o.snapshot)
    own = kit.own_session()
    if own is not None:
        extension.verify_overlay(own.overlay)
    if not 0 <= o.lane <= len(profile.PROFILES):
        raise SystemExit(f'--lane must be 1..{len(profile.PROFILES)} (0: the first free one)')
    ok, message = steam.status()
    if not ok and not o.steam_fix:
        raise SystemExit(message)
    install = Path(o.game) if o.game else game.find_install()
    game.verify_exe(install)
    lanes = Path(o.lanes) if o.lanes else Path(os.path.splitdrive(str(install))[0] + '\\x4ls-lanes')
    if not o.lane:
        o.lane = lobby.free_lane(o.state, lanes, log)
    elif lobby.lane_in_use(o.state, o.lane, log):
        raise SystemExit(f'lane {o.lane} is in use: its X4 window is still open')
    o.master_url = master_url(o, kit)
    run_dir = o.runs / (time.strftime('%Y%m%d-%H%M%S') + f'-lobby{o.lane}')
    run_dir.mkdir(parents=True)
    log = run_log(run_dir, log)
    o.state.mkdir(parents=True, exist_ok=True)
    state_file = o.state / f'lobby-{o.lane}.json'
    record = {'run': str(run_dir), 'views': [], 'pids': [], 'relay_pid': None, 'launcher': os.getpid()}

    def save_record():
        state_file.write_text(json.dumps(record, indent=2) + '\n')
    save_record()
    lobby_game = LobbyGame(o, install, lanes / f'c{o.lane}', o.lane, run_dir, record, save_record, log)
    backend = LobbyBackend(o, kit, lobby_game, run_dir, log)
    config = netplay.FlowConfig(kit=kit, release=kit.release, master_url=o.master_url, port=o.port, advertise=o.bind,
                                load_wait=o.load_wait, mode=o.mode or netkit.HOST_SESSION_MODE,
                                base_start=o.base_start or netkit.HOST_SESSION_START)
    flow = netplay.Flow(config, backend, log=log)
    log(f'lobby: kit {kit.kind} {kit.root or session.ROOT}, release {kit.release}, master '
        f'{"none (LAN only)" if not o.master_url else "configured"}')
    pump = Pump(lobby_game, flow, o.state_mirror, log)
    relay = None
    relay_log = None
    result = {'outcome': 'failed'}
    if o.steam_fix:
        steam.fix(o.state / 'steam-fix.json')
    try:
        pump.start()
        result, start = wait_for_start(o, flow, lobby_game, backend, log)
        if start is not None and start.late:
            result = late_join(o, flow, lobby_game, start, log)
        elif start is not None:
            relay, relay_log = start_relay(o, start, run_dir, record, save_record, log)
            result = play_session(o, flow, lobby_game, start, relay, run_dir, log)
    finally:
        flow.end_session(result)
        pump.stop()
        if relay is not None and relay.poll() is None:
            session.kill(relay.pid)
        if relay_log is not None:
            relay_log.close()
        finish(o, lobby_game, run_dir, state_file, result, log)
    return result


def run_log(run_dir, printer):
    """The launcher's own lines are part of the run folder."""
    def log(text):
        printer(text)
        try:
            with (run_dir / 'launcher.log').open('a', encoding='utf-8') as out:
                out.write(f'{time.strftime("%H:%M:%S")} {text}\n')
        except OSError:
            pass
    return log


def wait_for_start(o, flow, lobby_game, backend, log):
    """The menu: X4 started (unless the first request hosts: its job starts X4 with the session), the requests, until
    a session starts or X4 is closed. (result, SessionStart or None)."""
    first = o.request or auto_request(o.auto)
    first_request = netplay.parse_request(first) if first else None
    hosting_first = first_request is not None and first_request.kind == 'HOST'
    # A first join waits for this X4: its job starts X4 again with the session, and two starts must not overlap.
    if not hosting_first:
        lobby_game.start(backend.default_form())
    if first:
        log(f'lobby: first request {first[:80]}')
        flow.request(first)
    started = time.time()
    while True:
        time.sleep(PUMP_S)
        start = flow.take_start()
        if start is not None:
            return {'outcome': 'failed'}, start
        if lobby_game.exited():
            return {'outcome': 'closed', 'ended': 'X4 was closed in the menu'}, None
        if not lobby_game.started_once and not lobby_game.busy and flow.state.phase in ('idle', 'error'):
            lobby_game.start(backend.default_form())   # the first host failed before X4 was started: show why
        if o.lobby_timeout and time.time() - started > o.lobby_timeout:
            return {'outcome': 'failed', 'ended': f'no game within {o.lobby_timeout} s'}, None


def start_relay(o, start, run_dir, record, save_record, log):
    """Direct and LAN modes: the relay on this PC, on every interface, allowing what the host's lobby saw."""
    if start.relay is None:
        return None, None
    plan = start.relay
    play = play_options(o, start)
    play.remote_slots = tuple(range(2, start.clients + 1))
    play.bind = '0.0.0.0'
    play.allow_from = tuple(dict.fromkeys(('127.0.0.1', *plan.allow_from)))
    command = session.relay_command(play, plan.port, plan.token, run_dir) + ['--allow-file', str(plan.allow_file)]
    relay_log = (run_dir / 'relay.log').open('w')
    relay = subprocess.Popen(command, cwd=session.ROOT, stdout=relay_log, stderr=subprocess.STDOUT)
    record['relay_pid'] = relay.pid
    save_record()
    log(f'lobby: relay pid {relay.pid} on UDP {plan.port} (every interface), allowed {", ".join(play.allow_from)}')
    return relay, relay_log


def play_options(o, start):
    lookahead, tick_hz, last = session.play_settings(start.overlay, start.clients)
    if not last or not lookahead:
        raise SystemExit('the overlay has no pilot window or no lookahead: build it with tools/build_play_overlay.py')
    return session.Options(clients=start.clients, overlay=start.overlay, snapshot=start.snapshot,
                           ticks=last + session.PLAY_TAIL_TICKS, tick_hz=tick_hz, lookahead=lookahead, timeout=o.timeout,
                           width=o.width, height=o.height, play=True, stall_drop_ms=1000 * o.stall_drop,
                           close_when_over=o.close_when_over, port=start.port)


def play_session(o, flow, lobby_game, start, relay, run_dir, log):
    """The start: the session into config.lua (before the page hears 'session'), then x4ls play's monitor."""
    if not Form(start.overlay, start.snapshot, start.clients).same(lobby_game.form):
        raise SystemExit('the session to load is not the one this X4 runs (the flow installs it before the start)')
    text = lobby.session_config(start.overlay, lobby_game.files, start.slot, start.port, start.token, start.clients,
                                lobby_game.profile_id, start.arm_at, start.relay_host)
    lobby.write_atomic(lobby_game.target / 'ui/config.lua', text)
    installed = run_dir / f'client{o.lane}' / 'installed'
    installed.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(lobby_game.target / 'ui/config.lua', installed / 'x4_lan_lab-config.lua')
    status_file = run_dir / 'desync-status.json'
    flow.session_running(run_dir=run_dir, tick=lambda: relay_tick(status_file))
    log(f'lobby: player {start.slot} of {start.clients}, {start.mode}, relay {start.relay_host}:{start.port}, '
        f'loads in {start.arm_at - int(time.time())} s')
    play = play_options(o, start)
    result = session.monitor_play(play, run_dir, relay, [(o.lane, lobby_game.profile_id, lobby_game.proc)], log)
    result.update(slot=start.slot, relay=f'{start.relay_host}:{start.port}', mode=start.mode)
    return result


def late_join(o, flow, lobby_game, start, log):
    """A drop-in slot: this lane's X4 closes and hotjoin.hot_join plays the slot in the running game (its own lane,
    view and profile: c<slot>), with the overlay this PC built from the host's recipe. The join files come from the
    relay's file service (the master's relay keeps them for the joiner); X4 starts early, while the session saves."""
    from . import hotjoin
    lobby_game.hold()
    same_pc = session.x4_running()   # another player's X4 on this PC (a second window), as lobby.run's join_running
    flow.session_running(run_dir=None, tick=lambda: 0)
    log(f'lobby: player {start.slot} of {start.clients} joins the running game at the relay {start.relay_host}:{start.port}')
    options = hotjoin.HotJoinOptions(relay=f'{start.relay_host}:{start.port}', token=start.token, slot=start.slot,
                                     overlay=Path(start.overlay).resolve(), offer_wait=LATE_OFFER_WAIT_S, width=o.width,
                                     height=o.height, game=o.game, lanes=o.lanes, runs=o.runs,
                                     state=session.ROOT / '.local/x4ls-join' if same_pc else o.state,
                                     steam_fix=o.steam_fix, timeout=o.timeout, close_when_over=o.close_when_over,
                                     same_pc=same_pc)
    try:
        result = hotjoin.hot_join(options, log)
    except SystemExit as error:   # the join's own refusals carry the reason as text
        log(f'lobby: the drop-in join failed: {error}')
        result = {'outcome': 'failed', 'ended': f'the drop-in join failed: {error}'}
    if not isinstance(result, dict):
        result = {'outcome': 'failed', 'ended': str(result)}
    result.update(slot=start.slot, relay=f'{start.relay_host}:{start.port}', mode=start.mode, late=True)
    return result


def relay_tick(status_file):
    try:
        return int(json.loads(Path(status_file).read_text()).get('tick') or 0)
    except (OSError, ValueError, TypeError):
        return 0


def finish(o, lobby_game, run_dir, state_file, result, log):
    """X4 closed, its log kept, its view and staged save removed, the lane free again."""
    with lobby_game.lock:
        lobby_game.busy = True
        if lobby_game.proc is not None and lobby_game.proc.poll() is None:
            session.kill(lobby_game.proc.pid)
        time.sleep(2)
        try:
            profile.collect(lobby_game.profile_id, lobby_game.log_name, run_dir / f'client{o.lane}')
            profile.unstage(lobby_game.profile_id)
        except SystemExit as error:
            log(f'lobby: the profile was not collected: {error}')
        if lobby_game.view.exists():
            game.remove_view(lobby_game.view)
        from . import hotjoin   # a drop-in game's join saves (about 200 MB each) are the master's now
        hotjoin.remove_join_files({o.lane: profile.profile_dir(lobby_game.profile_id) / 'save'})
    state_file.unlink(missing_ok=True)
    if o.steam_fix and (o.state / 'steam-fix.json').exists():
        steam.restore(o.state / 'steam-fix.json')
    result.update(overlay=str(lobby_game.form.overlay if lobby_game.form else ''), run_dir=str(run_dir))
    (run_dir / 'summary.json').write_text(json.dumps(result, indent=2, default=str) + '\n')
    log(f"{result['outcome']}: {run_dir}")
