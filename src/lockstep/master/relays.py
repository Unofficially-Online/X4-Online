"""The master's relay pool: tools/dev/native_gate_relay.py for each relay-mode game (docs/NETPLAY.md, "Starting a game").

The relay is the one x4ls serve runs, started the same way (session.relay_command, the drop-in arguments, the join file
service on the relay's port number over TCP) and ended the same way (the relay exits, no step for --idle-end/--idle-left
seconds, or the session's last tick). What differs is where its settings come from: serve reads them from the kit's
play overlay (session.play_settings reads the overlay's config.lua.append with the session defaults applied), and the
master has no overlay, only the host's uploaded play-recipe.json. session.relay_command itself needs no overlay once it
is given the tick rate, the lookahead and the tick count, so recipe_settings computes those three from the recipe as
data: the pilot window's end (the recipe's `window`), the host's --set lines for native_gate_tick_hz and
native_gate_lookahead, and otherwise the defaults table (tools/dev/native_gate_defaults.lua), each checked against the
relay's ranges. Nothing of the recipe is executed or passed through as an argument.

Who may send: --allow-from the players' public IPs as the master saw them, and --allow-file for a player admitted later
(a late joiner, or a player whose address changed). The relay's own folder (relay.log, the report, the status file,
the join files) lives in the game's data folder and goes with the game."""
from dataclasses import dataclass
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

from ..launcher import hotjoin, serve, session

RECIPE_KIND = 'x4ls-play-recipe'     # tools/build_play_overlay.py RECIPE_KIND, RECIPE_VERSION
RECIPE_VERSION = 1
TICK_MAX = 8388607                   # the pilot window bound (lockstep_pilot_inputs.h PILOT_TICK_MAX)
TICK_RATES = (60, 40, 30, 20)        # the relay's --tick-hz choices
LOOKAHEAD_MAX = 8                    # the relay's --lookahead bound
SETS_MAX = 1024
SET_TEXT_MAX = 512
# Settings a master relay cannot serve: an adaptive window is protocol 5 (relay --adaptive), wire crypto needs a key
# file and peer links an extension; the relay command has none of these, so the clients and the relay would disagree.
UNSERVED = ('native_gate_wire_crypto', 'native_gate_peer_window')
DONE_GRACE_S = 10                    # after the session's last tick the clients get their last permits, then the relay ends
FALLBACK_DEFAULTS = {'native_gate_tick_hz': 30, 'native_gate_lookahead': 1}


class RecipeError(ValueError):
    pass


class RelayError(RuntimeError):
    pass


@dataclass
class RelaySettings:
    players: int
    open_slots: tuple
    ticks: int          # the relay's --ticks: the pilot window's end + session.PLAY_TAIL_TICKS, as x4ls play and serve
    tick_hz: int
    lookahead: int

    @property
    def present(self):
        """The slots of the common load (every slot that is not open)."""
        return [slot for slot in range(1, self.players + 1) if slot not in self.open_slots]


DEFAULTS_CACHE = {}


def default_value(key):
    """The session default of `key` from tools/dev/native_gate_defaults.lua (the one source of truth), as an int."""
    if not DEFAULTS_CACHE:
        try:
            sys.path.insert(0, str(session.ROOT / 'tools/dev'))
            import native_gate_defaults
            for entry_key, value, _needs, _why in native_gate_defaults.load()['on']:
                if entry_key in FALLBACK_DEFAULTS and value.strip().isdigit():
                    DEFAULTS_CACHE[entry_key] = int(value.strip())
        except (OSError, ValueError, ImportError):
            pass
        for fallback_key, fallback in FALLBACK_DEFAULTS.items():
            DEFAULTS_CACHE.setdefault(fallback_key, fallback)
    return DEFAULTS_CACHE[key]


def set_value(sets, key):
    """The value text of the last --set line for `key` ('native_gate_KEY=VALUE'), unquoted, or None."""
    found = None
    for item in sets:
        name, equals, value = item.replace(' ', '').partition('=')
        if equals and name == key:
            found = value.strip('\'"')
    return found


def int_setting(sets, key, allowed):
    text = set_value(sets, key)
    if text is None:
        return default_value(key)
    if not text.isdigit() or int(text) not in allowed:
        raise RecipeError(f'{key}={text} is not one the relay serves')
    return int(text)


def recipe_settings(data, players_max, open_slots):
    """The relay settings of an uploaded play-recipe.json (bytes), for a game registered with `players_max` slots and
    `open_slots`. RecipeError says why a recipe cannot run on the master's relay."""
    try:
        recipe = json.loads(data.decode('utf-8'))
    except (UnicodeDecodeError, ValueError, RecursionError):
        raise RecipeError('play-recipe.json is not JSON') from None
    if not isinstance(recipe, dict) or recipe.get('kind') != RECIPE_KIND or recipe.get('version') != RECIPE_VERSION:
        raise RecipeError('play-recipe.json is not a play recipe of this version')
    players = recipe.get('players')
    if type(players) is not int or players != players_max:
        raise RecipeError(f'the recipe is for {players!r} players, the game has {players_max} slots')
    opened = recipe.get('open', [])
    if not isinstance(opened, list) or not all(type(slot) is int for slot in opened):
        raise RecipeError('the recipe\'s open slots are not slot numbers')
    if sorted(set(opened)) != sorted(open_slots):
        raise RecipeError('the recipe\'s open slots are not the game\'s')
    window = recipe.get('window')
    if not isinstance(window, list) or len(window) != 2 or not all(type(tick) is int for tick in window):
        raise RecipeError('the recipe has no pilot window')
    if not 0 <= window[0] < window[1] <= TICK_MAX:
        raise RecipeError('the recipe\'s pilot window is out of range')
    sets = recipe.get('set', [])
    if not isinstance(sets, list) or len(sets) > SETS_MAX:
        raise RecipeError('the recipe\'s settings are not a list')
    if not all(isinstance(item, str) and len(item) <= SET_TEXT_MAX for item in sets):
        raise RecipeError('the recipe\'s settings are not text lines')
    for key in UNSERVED:
        if set_value(sets, key) not in (None, 'false', 'nil'):
            raise RecipeError(f'{key} needs a relay option the master does not run')
    if set_value(sets, 'native_gate_lookahead') == 'adaptive':
        raise RecipeError('an adaptive window (protocol 5) cannot use the master\'s relay')
    tick_hz = int_setting(sets, 'native_gate_tick_hz', TICK_RATES)
    lookahead = int_setting(sets, 'native_gate_lookahead', range(1, LOOKAHEAD_MAX + 1))
    return RelaySettings(players=players, open_slots=tuple(sorted(set(opened))),
                         ticks=window[1] + session.PLAY_TAIL_TICKS, tick_hz=tick_hz, lookahead=lookahead)


def relay_command(settings, port, token, run_dir, bind, addresses, allow_file, stall_drop_s=session.PLAY_STALL_DROP_S,
                  join_catchup=120):
    """The relay's command line: session.relay_command as serve calls it (every slot a remote one, play mode, a stall
    drop), then --allow-file, and for open slots serve's drop-in arguments."""
    play = session.Options(clients=settings.players, overlay=Path('play-recipe.json'), snapshot=Path('snapshot'),
                           ticks=settings.ticks, tick_hz=settings.tick_hz, lookahead=settings.lookahead, play=True,
                           stall_drop_ms=1000 * stall_drop_s, port=port,
                           remote_slots=tuple(range(1, settings.players + 1)), bind=bind, allow_from=tuple(addresses))
    command = session.relay_command(play, port, token, Path(run_dir))
    command += ['--allow-file', str(allow_file)]
    if settings.open_slots:
        given = ('--join-max-pause', '--join-catchup-limit')
        command += hotjoin.relay_join_args(list(settings.open_slots), settings.present, given=given, observe=True)
        command += ['--join-max-pause', str(serve.join_pause_limit(0)), '--join-catchup-limit', str(join_catchup)]
    return command


class MasterDropIn(serve.DropIn):
    """serve.DropIn without a lobby connection: the allow list, the open slots the relay reports taken, and the join
    files (verified, then served by joinfiles.FileService on the relay's port). Where serve asks the saver's launcher
    over its TCP connection, the master lists the join in the saver's lobby answers (send_join) and the saver uploads
    the two files over HTTP."""
    def __init__(self, run_dir, token, bind, port, open_slots, addresses, log):
        super().__init__(None, run_dir, token, bind, port, open_slots, addresses, log)

    def poll(self):
        """Follow the relay's join rows; True when a saver has a join to send (the lobby answers change)."""
        asked = False
        for line in self.relay_log.lines():
            row = serve.JOIN_ROW.match(line)
            if not row:
                continue
            slot = int(row.group(1))
            state = row.group(2)
            if state in serve.TAKEN:
                self.taken[slot] = state
            elif state == 'failed':
                self.taken.pop(slot, None)
            self.log(f'relay: {line.strip()[:200]}')
            saved = hotjoin.SAVED.match(line)
            if saved:
                saver = int(saved.group(2))
                tick = int(saved.group(3))
                package_sha = saved.group(5)
                with self.lock:
                    self.asked[tick] = (slot, saver, package_sha)
                asked = True
        return asked

    def offer_packed(self, path):
        """A compressed package the master checked (server.unpack_join): offered before the raw one, so that a joiner
        takes it (joinfiles.fetch_package)."""
        size, _ = self.service.offer(path.name, path)
        self.log(f'relay: join {path.name}: offered compressed ({size >> 20} MB)')

    def played(self, slot):
        """Whether the relay has taken `slot` for a join (requested and on); a slot it never took, or whose join failed,
        may be joined again."""
        with self.lock:
            return slot in self.taken

    def pending(self):
        """The joins whose files the master still waits for: {tick: (slot, saver, package sha)}."""
        with self.lock:
            return dict(self.asked)


def port_free(bind, port):
    """Whether `port` can be bound on `bind` for UDP (the relay) and TCP (its join files) both."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind((bind, port))
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp:
            tcp.bind((bind, port))
        return True
    except OSError:
        return False


class RelayPool:
    """The ports of --relay-ports and how many relays run (MasterLimits.relays_max)."""
    def __init__(self, first, last, bind, relays_max):
        self.ports = range(first, last + 1)
        self.bind = bind
        self.relays_max = relays_max
        self.taken = set()
        self.lock = threading.Lock()

    def take(self):
        """A free port, or None when the pool is full."""
        with self.lock:
            if len(self.taken) >= self.relays_max:
                return None
            for port in self.ports:
                if port not in self.taken and port_free(self.bind, port):
                    self.taken.add(port)
                    return port
        return None

    def give_back(self, port):
        with self.lock:
            self.taken.discard(port)

    def running(self):
        with self.lock:
            return len(self.taken)


class RelayGame:
    """One relay-mode game's relay: its process, its folder, who may send to it, and (open slots) its join files."""
    def __init__(self, settings, port, token, run_dir, bind, addresses, log, limits):
        self.settings = settings
        self.port = port
        self.token = token
        self.run_dir = Path(run_dir)
        self.bind = bind
        self.log = log
        self.limits = limits
        self.allowed = set(addresses)
        self.allow_file = self.run_dir / 'allow.txt'
        self.status_file = self.run_dir / 'desync-status.json'
        self.process = None
        self.output = None
        self.dropin = None
        self.seen = [None, time.time()]   # serve.idle_seconds: the relay's step and when it last changed
        self.done_at = None
        self.lock = threading.Lock()

    def start(self):
        """Start the relay and wait until it listens; RelayError (and nothing left running) when it does not."""
        self.run_dir.mkdir(parents=True, exist_ok=True)
        if self.settings.open_slots:
            self.dropin = MasterDropIn(self.run_dir, self.token, self.bind, self.port, self.settings.open_slots,
                                       sorted(self.allowed), self.log)
        else:
            self.allow_file.write_text('', encoding='ascii')
        command = relay_command(self.settings, self.port, self.token, self.run_dir, self.bind, sorted(self.allowed),
                                self.allow_file)
        self.output = (self.run_dir / 'relay.log').open('w', encoding='utf-8')
        try:
            self.process = subprocess.Popen(command, cwd=session.ROOT, stdout=self.output, stderr=subprocess.STDOUT,
                                            stdin=subprocess.DEVNULL)
            self.wait_ready()
        except (OSError, RelayError):
            self.stop()
            raise
        return self

    def wait_ready(self):
        deadline = time.monotonic() + self.limits.relay_ready_s
        log_path = self.run_dir / 'relay.log'
        while time.monotonic() < deadline:
            text = log_path.read_text(encoding='utf-8', errors='replace')
            if 'NATIVE_GATE RELAY READY' in text:
                return
            if self.process.poll() is not None:
                tail = ' | '.join(text.strip().splitlines()[-2:])[:300]
                raise RelayError(f'the relay ended with code {self.process.returncode}: {tail}')
            time.sleep(0.05)
        raise RelayError('the relay did not start listening')

    def admit(self, address):
        """A player's address may send to the relay (the relay reads --allow-file again when it changes)."""
        with self.lock:
            if address in self.allowed:
                return False
            self.allowed.add(address)
            if self.dropin:
                self.dropin.admit(address)
            else:
                with self.allow_file.open('a', encoding='ascii') as allow:
                    allow.write(address + '\n')
        return True

    def poll(self):
        """Follow the relay's join rows (drop-in games); True when the saver's lobby answers change."""
        return bool(self.dropin and self.dropin.poll())

    def over(self, now):
        """Why the game is over, or None while it runs: serve's three ends."""
        if self.process.poll() is not None:
            return f'the relay ended with code {self.process.returncode}'
        idle = serve.idle_seconds(self.status_file, self.seen, now)
        limit = serve.idle_limit(self.limits, self.seen[0], self.dropin)
        if idle > limit:
            return f'no step for {limit} s'
        if serve.session_done(self.status_file):
            if self.done_at is None:
                self.done_at = now
            elif now - self.done_at > DONE_GRACE_S:
                return 'the session is over'
        return None

    def step(self):
        try:
            return json.loads(self.status_file.read_text()).get('tick')
        except (OSError, ValueError, AttributeError):
            return None

    def stop(self):
        """End the relay and its join file service; the join files are deleted (the folder goes with the game)."""
        if self.process and self.process.poll() is None:
            self.process.kill()
            try:
                self.process.wait(10)
            except subprocess.TimeoutExpired:
                pass
        if self.output:
            self.output.close()
            self.output = None
        if self.dropin:
            self.dropin.close(keep=False)
            self.dropin = None

