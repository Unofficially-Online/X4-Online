"""x4ls lobby with netplay: this PC's X4 opens on the Multiplayer page and the game is hosted or joined there, at the
master server or on the LAN (docs/NETPLAY.md; the requests and the state table are netplay.py's).

Every kit but one with a server (x4ls serve: lobby.lobby_run, as it always was) comes here: a release kit, a playtest
kit, this checkout. The launcher's X4 runs in its lane's view and disposable profile, as x4ls lobby's always did:
- At its start it runs the kit's own session in lobby form (a playtest kit: its overlay installed, the snapshot
  staged), a host session the kit keeps (a release ships some) in lobby form, or the Multiplayer page alone.
- What X4 reads only at its own start must be the session's: the -seed: of its command line and the extension's
  md/, aiscripts/, libraries/ (start_parts). The page runs the overlay's ui files at the session's start, the probe
  loads the gate DLL and the staged save then, and config.lua is written then. So an X4 that runs one session's overlay
  takes another session of the same release in place (LobbyGame.take: its runtime files copied in, its save staged):
  no restart. Only another seed or other start-time files (another release, the page alone) start X4 again.
- Hosting a game the kit has no session for yet: x4ls new-session makes it beside this X4, whose page shows the
  progress (the capture is a second X4 in the capture profile; this X4's instance mutex is free since its start
  menu). Too little memory free for a second X4, or a capture beside that failed: X4 closes while the session is made.
  No X4 yet (x4ls host, the Steam helper's hand-over): the session is made before X4 starts. The session is kept in
  the kit's sessions/ for the next game of that many players.
- Joining: the session's files are downloaded and the overlay built here; X4 takes it (in place, or starting again
  once). Then READY.
- A first request that hosts or joins (the Steam helper's hand-over, x4ls host, x4ls join) starts no X4 before its
  job installs the session: the one start goes straight into the game. Every X4 the player looked at that closes for
  another is a netkit.RESTART_MARK row of the run's launcher.log (netkit.count_restarts).
- The start: this launcher writes the session into the extension's config.lua, the page runs the overlay's files, and
  from here on it is the session x4ls play and x4ls join make (session.monitor_play), with the relay on this PC in
  direct and LAN modes. The state keeps being written for the probe's "Multiplayer session" page.

A pump thread carries the page's requests (rows of the game's log) to the flow and the flow's state to
lobby/state.lua (and to --state-mirror, which the Steam helper answers its pipe with)."""
from dataclasses import dataclass
import filecmp
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

    def seed(self):
        return session.snapshot_seed(self.snapshot) if self.snapshot else session.SESSION_SEED


def resolved(path):
    return str(Path(path).resolve()).lower() if path else ''


# What X4 reads only when it starts, of an overlay installed in lobby form, is everything but these: the page runs the
# overlay's ui files from the extension folder at the session's start (loadfile), the probe loads the gate DLL then
# (package.loadlib) and the staged save (LoadGame save_009), config.lua is written at the start, and the launcher's own
# files are not X4's. md/, aiscripts/, libraries/ and the rest (a story's patches of stock extensions, plot copies)
# count, and so does the -seed: of the command line.
RUNTIME_PREFIXES = ('ui/', 'native/')
LAUNCHER_FILES = frozenset(('play-recipe.json', 'session.json', 'player-identity-plan.json', 'ui.xml.files',
                            'x4lane-manifest.json'))
NOT_COPIED = frozenset(('x4lane-manifest.json', 'ui.xml.files', 'ui/config.lua.append'))   # as extension.compose


def start_parts(overlay):
    """{file: sha-256} of an overlay's files that X4 reads only at its own start (see RUNTIME_PREFIXES). Two sessions of
    one release have the same (their md, aiscripts and libraries are the release's, 25 Nov: 2, 3 and 4 players)."""
    manifest = json.loads((Path(overlay) / 'x4lane-manifest.json').read_text(encoding='utf-8'))
    return {name.replace('\\', '/'): digest for name, digest in manifest.items()
            if not name.replace('\\', '/').startswith(RUNTIME_PREFIXES) and name.replace('\\', '/') not in LAUNCHER_FILES}


def in_place_refusal(running, wanted):
    """Why an X4 that runs the form `running` cannot take the session `wanted` without a restart, or None: it runs the
    page alone (none of a session's start-time files), another start-time file, or another seed."""
    if running is None or running.overlay is None:
        return 'it runs the Multiplayer page without a session\'s files'
    if wanted.overlay is None:
        return 'the page alone is wanted'
    try:
        if start_parts(running.overlay) != start_parts(wanted.overlay):
            return 'the sessions differ in files X4 reads at its start'
        if running.seed() != wanted.seed():
            return 'the sessions have different seeds'
    except (OSError, ValueError, SystemExit) as error:
        return f'the sessions could not be compared ({error})'
    return None


def install_page_only(extensions, show=False, single=False):
    """The extension of an X4 that waits on the Multiplayer page without a session (a release kit): the base files,
    the page and an empty lobby config."""
    target = Path(extensions) / extension.EXTENSION_ID
    if target.exists():
        raise SystemExit(f'{target} already exists')
    shutil.copytree(extension.BASE, target)
    shutil.copyfile(lobby.LOBBY_LUA, target / lobby.LOBBY_UI)
    (target / 'content.xml').write_text(extension.CONTENT)
    (target / 'ui.xml').write_text(extension.ui_xml([*extension.BASE_UI, lobby.LOBBY_UI]))
    (target / 'ui' / 'config.lua').write_text(lobby.lobby_config([], show, netplay=True, single=single))
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

    def single(self):
        """Steam's Play started this X4 through X4 Online: its page offers Single-player."""
        return bool(getattr(self.o, 'single_player_mark', None))

    def open(self):
        """True while this lane's X4 runs (the player looks at it)."""
        with self.lock:
            return self.proc is not None and self.proc.poll() is None

    def hold(self):
        """Close X4 for a while (a session is being made); start() brings it back."""
        with self.lock:
            self.busy = True
            self.stop_process('it is held while a session is made')

    def take(self, form):
        """The running X4 takes another session without a restart (in_place_refusal said it can): the overlay's runtime
        files into the installed extension, its file list into the lobby config, its save staged. True when done."""
        with self.lock:
            if self.proc is None or self.proc.poll() is not None or self.target is None:
                return False
            why = in_place_refusal(self.form, form)
            if why is not None:
                self.log(f'lobby: X4 starts again for the session {Path(form.overlay).name if form.overlay else ""}: {why}')
                return False
            overlay = Path(form.overlay)
            try:
                files = self.copy_runtime(overlay)
                lobby.write_atomic(self.target / 'ui' / 'config.lua',
                                   lobby.lobby_config(files, self.o.show, False, netplay=True, single=self.single()))
                if form.snapshot:
                    self.stage(form.snapshot)
            except OSError as error:   # a file X4 holds open (a loaded DLL): it starts again instead
                self.log(f'lobby: X4 starts again for the session {overlay.name}: {error}')
                return False
            self.files = files
            self.form = form
            self.log(f'lobby: X4 pid {self.proc.pid} takes the session {overlay.name} in place (the same start-time files '
                     'and seed): no restart')
            return True

    def copy_runtime(self, overlay):
        """The overlay's files into the installed extension, but those X4 has already (the release's own: the gate DLL,
        the probe) and those compose leaves out; the overlay's ui file list."""
        overlay = Path(overlay)
        for path in sorted(overlay.rglob('*')):
            name = path.relative_to(overlay).as_posix()
            installed = self.target / name
            if not path.is_file() or name in NOT_COPIED:
                continue
            if installed.is_file() and filecmp.cmp(path, installed, shallow=False):
                continue
            installed.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, installed)
        return lobby.overlay_files(overlay)

    def take_join(self, overlay, slot, port, token, clients, relay_host, seed):
        """A drop-in join played by this X4 without a restart (session.join_running): the joiner overlay (an early
        joiner's: its save comes later, staged by hotjoin.EarlyJoin) must have this X4's start-time files and seed; its
        runtime files and the session's config (slot, relay, no arm time) go into the extension. The page runs them when the
        flow says 'session'. False, with nothing changed, when it cannot."""
        with self.lock:
            if self.proc is None or self.proc.poll() is not None or self.target is None:
                return False
            running = self.form
            why = None
            if running is None or running.overlay is None:
                why = 'it runs the Multiplayer page without a session\'s files'
            else:
                try:
                    if start_parts(running.overlay) != start_parts(overlay):
                        why = 'the joiner\'s overlay differs in files X4 reads at its start'
                    elif running.seed() != int(seed):
                        why = 'the session has another seed'
                except (OSError, ValueError, SystemExit) as error:
                    why = f'the overlays could not be compared ({error})'
            if why is not None:
                self.log(f'lobby: the drop-in join starts X4 again: {why}')
                return False
            try:
                files = self.copy_runtime(overlay)
                config = lobby.session_config(overlay, files, slot, port, token, clients, self.profile_id, None, relay_host)
                lobby.write_atomic(self.target / 'ui' / 'config.lua', config)
            except OSError as error:
                self.log(f'lobby: the drop-in join starts X4 again: {error}')
                return False
            self.files = files
            self.form = Form(Path(overlay), None, clients)
            self.log(f'lobby: X4 pid {self.proc.pid} plays the drop-in slot {slot} in place (the same start-time files and '
                     'seed): no restart')
            return True

    def start(self, form):
        """X4 with this form: the view made again, the extension installed, the profile wiped and staged."""
        with self.lock:
            self.busy = True
            try:
                what = f'the session {Path(form.overlay).name}' if form.overlay else 'the Multiplayer page'
                self.stop_process(f'it starts again with {what}')
                self.proc = self.launch(form)
                self.record['pids'].append(self.proc.pid)
                self.save_record()
                self.form = form
                self.generation += 1
                self.started_once = True
                self.log(f'lobby: X4 pid {self.proc.pid}, profile {self.profile_id}, view {self.view}: {what}')
            except BaseException:
                self.busy = False
                raise
        self.wait_start_menu()
        with self.lock:
            self.busy = False

    def launch(self, form):
        """The view made again, the extension installed (self.target, self.files), the profile wiped and staged, X4
        started: its process."""
        if self.view.exists():
            game.remove_view(self.view)
        game.create_view(self.install, self.view)
        if str(self.view) not in self.record['views']:
            self.record['views'].append(str(self.view))
        extensions = self.view / 'extensions'
        if form.overlay:
            self.target, self.files = lobby.install_lobby(form.overlay, extensions, form.clients, self.profile_id,
                                                          self.o.show, False, netplay=True, single=self.single())
        else:
            self.target, self.files = install_page_only(extensions, self.o.show, single=self.single())
        profile.prepare(self.profile_id, [*game.dlc_ids(self.view), extension.EXTENSION_ID], self.o.width,
                        self.o.height, form.snapshot)
        return subprocess.Popen(session.client_command(self.view, self.profile_id, self.lane, form.seed()),
                                cwd=self.view, env=steam.client_environment())

    def wait_start_menu(self):
        session.wait_for_log(self.log_path, 'Entering startmenu', self.proc, START_MENU_WAIT_S)
        self.log(f'lobby: released {instance.release_instance_mutex(self.proc.pid)} instance mutex handle(s)')

    def stage(self, snapshot):
        profile.stage(self.profile_id, snapshot)

    def stop_process(self, why=''):
        """End X4 (if it runs) and keep its log: a restart wipes the profile. An X4 that still runs is one the player
        looks at: its end is a restart (netkit.RESTART_MARK, counted by netkit.count_restarts)."""
        if self.proc is None:
            return
        if self.proc.poll() is None:
            self.log(f'{netkit.RESTART_MARK}: lobby X4 pid {self.proc.pid} closes: {why or "it starts again"}')
        self.end_process()

    def end_process(self):
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
        """What this X4 starts with before a game is chosen: the kit's own session (a playtest kit), else a host session
        the kit keeps (a release ships them; netkit.host_sessions) in lobby form, so that a game hosted or joined later
        is taken in place (LobbyGame.take: the same start-time files), else the Multiplayer page alone."""
        own = self.kit_session()
        if own is not None:
            return Form(own.overlay, own.snapshot, own.players)
        kept = self.kept_session()
        if kept is not None:
            return Form(kept.overlay, kept.snapshot, kept.players)
        return Form()

    def kept_session(self):
        """The kit's kept session this X4 waits with, so that a game hosted or joined later is taken in place: a host
        session (the default start's and mode's first, the fewest players, no drop-in slots first), else a game this kit
        joined before (any session of the release has the same start-time files); None without one."""
        try:
            found = list(netkit.host_sessions(self.kit).values())
            joined = list(netkit.joined_sessions(self.kit).values())
        except (OSError, SystemExit, ValueError) as error:
            self.log(f'lobby: the kept sessions could not be read: {error}')
            return None
        start = self.o.base_start or netkit.HOST_SESSION_START
        mode = self.o.mode or netkit.HOST_SESSION_MODE
        found.sort(key=lambda files: ((files.recipe.get('session') or {}).get('base_start') != start,
                                      files.recipe.get('mode') != mode, bool(files.recipe.get('open')), files.players))
        usable = found + joined
        return usable[0] if usable else None

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
        if getattr(self.o, 'milestone', None):   # x4ls host --milestone: the game goes on from that save (no X4 needed for it)
            self.log(f'lobby: the game continues from {self.o.milestone}')
            return netkit.milestone_session(self.kit, self.o.milestone, players, open_slots, progress, self.log,
                                            session=getattr(self.o, 'milestone_session', None),
                                            overlay=getattr(self.o, 'milestone_overlay', None),
                                            keep_flight=bool(getattr(self.o, 'milestone_keep_flight', False)))
        mode = self.o.mode or netkit.HOST_SESSION_MODE
        base_start = self.o.base_start or netkit.HOST_SESSION_START
        cached = netkit.cached_session(self.kit, players, mode, base_start, open_slots)
        if cached is not None:
            return cached
        opened = f' (drop-in slots {", ".join(map(str, open_slots))})' if open_slots else ''

        def make(beside):
            return netkit.make_session(self.kit, players, mode, base_start, progress, self.log, cancelled,
                                       steam_fix=self.o.steam_fix, open_slots=open_slots, game=self.o.game,
                                       lanes=self.o.lanes, beside=beside)
        if self.game.open():
            if netkit.beside_fits():   # the page stays and shows the progress; the capture is a second X4 beside it
                self.log(f'lobby: no session for {players} players{opened} yet: x4ls new-session makes one beside this X4')
                try:
                    return make(beside=True)
                except netkit.KitError as error:
                    if cancelled is not None and cancelled.is_set():
                        raise
                    self.log(f'lobby: the session could not be made beside this X4 ({error}): once more with X4 closed')
            else:
                self.log(f'lobby: less than {netkit.BESIDE_FREE_GIB} GB of memory free for a second X4')
            self.log(f'lobby: no session for {players} players{opened} yet: X4 closes while x4ls new-session makes one')
            self.game.hold()
        else:   # no X4 runs yet (x4ls host, or the Steam helper's hand-over when it could not make the session)
            self.log(f'lobby: no session for {players} players{opened} yet: x4ls new-session makes one before X4 starts')
        try:
            return make(beside=False)
        except BaseException:
            self.game.start(self.default_form())   # back to the page, which says why
            raise

    def save_folder(self):
        return profile.profile_dir(self.game.profile_id) / 'save'

    # how this PC's Steam starts X4 (steamlib.SteamSetup: the helper writes Steam's files once Steam is closed)
    def steam_setup(self):
        from . import steamlib
        return steamlib.SteamSetup(self.kit.root, log=self.log) if self.kit.root else None

    def steam_view(self):
        setup = self.steam_setup()
        return setup.view() if setup is not None and setup.installed() else None

    def steam_play(self, on):
        setup = self.steam_setup()
        if setup is None or not setup.installed():
            return 'Only an installed X4 Online changes how Steam starts X4.'
        setup.want_play(on)
        return ('It takes effect once Steam is closed (the helper writes it then; Steam keeps that setting while it '
                'runs).')

    def single_player(self):
        """Single-player: this lobby ends, and helper.pyw --steam-play starts X4 with Steam's own command line."""
        mark = getattr(self.o, 'single_player_mark', None)
        if not mark:
            return 'Single-player is for an X4 that Steam\'s Play started: start X4 from Steam for that.'
        Path(mark).write_text('single-player\n', encoding='utf-8')
        self.single = True
        return 'X4 starts again in your own profile, for single-player.'

    def install_session(self, files, progress):
        """X4 runs this session: it does already, it takes it in place (LobbyGame.take: no restart), or it starts with
        it (the first start of this lobby, or a restart when the start-time files or the seed differ)."""
        form = Form(files.overlay, files.snapshot, files.players)
        if self.game.runs(form):
            return
        extension.verify_overlay(files.overlay)
        progress(0.1)
        if not self.game.take(form):
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
    # A first host or join (the Steam helper's hand-over, x4ls host, x4ls join) starts no X4 of its own here: its job
    # starts X4 with the session, so the player's one restart goes straight into the game. (A first join used to start
    # the page first and then X4 again with the session: two X4s for one join.) If the job fails before that, the page
    # is started below to say why.
    if first_request is None or not (first_request.kind == 'HOST' or first_request.joins) or getattr(o, 'page_first', False):
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
        if getattr(backend, 'single', False):   # Single-player (helper.pyw --steam-play starts X4 for it)
            return {'outcome': 'closed', 'ended': 'single-player'}, None
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


class InPlaceJoin:
    """session.join_running's view of this lane's X4 (late_join): it plays a drop-in slot without a restart."""

    def __init__(self, lobby_game, flow):
        self.game = lobby_game
        self.flow = flow
        self.view = lobby_game.view
        self.profile_id = lobby_game.profile_id
        self.proc = lobby_game.proc
        self.lane = lobby_game.lane

    def take(self, overlay, slot, port, token, clients, relay_host, seed):
        return self.game.take_join(overlay, slot, port, token, clients, relay_host, seed)

    def started(self):   # the page runs the session's config and files (the probe waits in the start menu)
        self.flow.session_running(run_dir=None, tick=lambda: 0)


def others_running(lobby_game):
    """Another X4 on this PC than this lane's (another player's second window)."""
    own = lobby_game.proc.pid if lobby_game.proc is not None else None
    return any(pid != own for pid, _client in session.other_clients())


def late_join(o, flow, lobby_game, start, log):
    """A drop-in slot: hotjoin.hot_join plays it in the running game, with the overlay this PC built from the host's
    recipe. The join files come from the relay's file service (the master's relay keeps them for the joiner); X4 starts
    early, while the session saves. This lane's X4, when it runs with a session of the release (the page in the
    multiplayer profile), plays the slot itself, no restart (InPlaceJoin, session.join_running); else it closes and the
    hot join starts its own X4 (lane, view and profile c<slot>)."""
    from . import hotjoin
    running = InPlaceJoin(lobby_game, flow) if lobby_game.open() else None
    if running is None:
        lobby_game.hold()
    log(f'lobby: player {start.slot} of {start.clients} joins the running game at the relay {start.relay_host}:{start.port}')

    def options(in_place):
        same_pc = others_running(lobby_game) if in_place else session.x4_running()   # another player's X4 on this PC
        return hotjoin.HotJoinOptions(relay=f'{start.relay_host}:{start.port}', token=start.token, slot=start.slot,
                                      overlay=Path(start.overlay).resolve(), offer_wait=LATE_OFFER_WAIT_S, width=o.width,
                                      height=o.height, game=o.game, lanes=o.lanes, runs=o.runs,
                                      state=session.ROOT / '.local/x4ls-join' if same_pc else o.state,
                                      steam_fix=o.steam_fix, timeout=o.timeout, close_when_over=o.close_when_over,
                                      same_pc=same_pc, running=running if in_place else None)
    try:
        if running is not None:
            try:
                result = hotjoin.hot_join(options(True), log)
            except session.NotInPlace as error:   # nothing was asked or changed: an X4 of the hot join's own
                log(f'lobby: the drop-in join is not played by this X4 ({error})')
                running = None
                lobby_game.hold()
        if running is None:
            flow.session_running(run_dir=None, tick=lambda: 0)
            result = hotjoin.hot_join(options(False), log)
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
