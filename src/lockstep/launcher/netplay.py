"""Netplay on the launcher's side: the Multiplayer page's requests, the state table it draws, and the flow that hosts,
joins or lists games (docs/NETPLAY.md: "The page and the launcher: requests and state", "Starting a game", "VLANs").

Both backends share this module:
- x4ls lobby (netlobby.py): the page runs in the launcher's own X4 (a disposable profile). A request is a row of the
  game's log ([X4LAN] LOBBY REQUEST <text>); the state is lobby/state.lua in the installed extension.
- the Steam-start helper (steamplay.py): the page runs in an X4 Steam started (the player's own profile, where no
  session may run). A request is spelled through named pipes (<pipe>-tbegin, one <pipe>-t<hex digit> per digit of its
  UTF-8 bytes, <pipe>-tend); the state is the answer to every open. A game is hosted or joined there by starting X4 again
  in the disposable profile with the request already chosen (x4ls lobby --request <text>), so the flow below runs in
  x4ls lobby in both cases.

The flow talks to the network through package 2's modules (masterclient, nat, netifs, landisco), imported when first
used and replaced by fakes in the tests, and to its X4 through a backend: make and install the session, the clipboard.
It never runs what another player sent: a joiner gets the session's data and builds its overlay here, with its own
release (netkit.py)."""
from dataclasses import dataclass
import importlib
from pathlib import Path
import queue
import re
import threading
import time

from . import lobby, session

PLAYERS_MIN = 2
PLAYERS_MAX = 8
GAME_NAME_MAX_BYTES = 40      # the LAN announcement's limit for a name (NETPLAY.md, "LAN discovery")
REQUEST_MAX_BYTES = 512       # a longer row is not a request of the page
JOIN_TEXT_MAX = 400           # a pasted join code, invite code or list of addresses
LINES_MAX = 6                 # the page's header has six rows
LINE_MAX_CHARS = 160
GAMES_SHOWN_MAX = 20          # public games the state carries (the master lists up to 100; the page shows a screenful)
LAN_SHOWN_MAX = 10
RELAY_UDP_PORT = 47000        # the host's relay; its TCP lobby listens on the next port (NETPLAY.md, "Ports")
LOBBY_TCP_PORT = RELAY_UDP_PORT + 1
GAME_ID = re.compile(r'[A-Za-z0-9_-]{1,64}')
HEX32 = re.compile(r'[0-9a-f]{32}')
# Requests without fields. JOIN takes the code from the clipboard; HOST alone is the older page's Host (the kit's own
# session, an invite code); JOIN<k> is a kit server's start picker (lobby.py, steamplay.py).
SIMPLE_REQUESTS = ('BROWSE', 'START', 'READY', 'CANCEL', 'COPY', 'LEAVE', 'JOIN', 'HOST', 'FIREWALL')
JOIN_KINDS = ('JOIN', 'JOINCODE', 'JOINGAME', 'JOINLAN')
# What an 8-character join code looks like (NETPLAY.md: XXXX-XXXX, case and dash insensitive). Text of this shape that
# nat.normalize_code refuses is a mistyped code, not a host name.
CODE_SHAPE = re.compile(r'[A-Za-z0-9]{4}-?[A-Za-z0-9]{4}')
HOST_LABEL = r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?'
HOST_NAME = re.compile(rf'(?=.{{1,253}}$){HOST_LABEL}(?:\.{HOST_LABEL})*\.?')


# ---- requests ----
@dataclass(frozen=True)
class Request:
    """One request of the page: kind is its first field, the others its arguments."""
    kind: str
    public: bool = False
    players: int = 0        # HOST|...: 2..8 (1..7 with drop-in slots); 0 is the older page's HOST (the kit's own session)
    open: int = 0           # HOST|...: drop-in slots, joined while the game runs (players + open <= 8)
    name: str = ''          # HOST|...: the game's name as the host typed it
    text: str = ''          # JOINCODE: what was typed or pasted (a join code, an invite code, addresses)
    game_id: str = ''       # JOINGAME
    index: int = 0          # JOINLAN: the row of the state's lan list, from 1
    slot: int = 0           # JOINSLOT (the page sends JOIN<k>): a start of a kit server's running game

    @property
    def joins(self):
        return self.kind in JOIN_KINDS


def clean_name(text):
    """A game name as the master and the LAN announcement may carry it: printable, no field separator, at most 40 bytes
    of UTF-8 (cut at a character boundary)."""
    kept = ''.join(ch for ch in str(text) if ch.isprintable() and ch != '|').strip()
    data = kept.encode('utf-8')[:GAME_NAME_MAX_BYTES]
    return data.decode('utf-8', 'ignore').strip()


def parse_host(fields):
    """HOST|<public 0/1>|<players>[+<drop-in slots>]|<name> without its kind; None when a field is wrong. Without
    drop-in slots players is 2..8; with them (1..7 more slots, joined while the game runs) the game may start with
    the host alone, and players + slots is at most 8."""
    parts = fields.split('|', 2)
    if len(parts) != 3:
        return None
    public_text = parts[0]
    players_text, plus, open_text = parts[1].partition('+')
    if public_text not in ('0', '1') or not players_text.isdigit() or (plus and not open_text.isdigit()):
        return None
    players = int(players_text)
    extra = int(open_text) if plus else 0
    if not host_counts_valid(players, extra):
        return None
    return Request('HOST', public=public_text == '1', players=players, open=extra, name=clean_name(parts[2]))


def host_counts_valid(players, extra):
    """players at the start and drop-in slots of a hosted game (parse_host's rule)."""
    if extra == 0:
        return PLAYERS_MIN <= players <= PLAYERS_MAX
    return 1 <= players and 1 <= extra and players + extra <= PLAYERS_MAX


def drop_in_slots(players, extra):
    """The session's open slots for `players` at the start and `extra` drop-in slots: the last ones."""
    return tuple(range(players + 1, players + extra + 1))


def parse_request(text):
    """The page's request text as a Request, or None when it is not one (a request with a bad field is none)."""
    if not isinstance(text, str):
        return None
    text = text.strip()
    if not text or len(text.encode('utf-8')) > REQUEST_MAX_BYTES:
        return None
    if text in SIMPLE_REQUESTS:
        return Request(text)
    legacy_slot = re.fullmatch(r'JOIN([2-8])', text)
    if legacy_slot:
        return Request('JOINSLOT', slot=int(legacy_slot.group(1)))
    kind, _, fields = text.partition('|')
    if kind == 'HOST':
        return parse_host(fields)
    if kind == 'JOINCODE':
        code = fields.strip()
        if not code or len(code) > JOIN_TEXT_MAX:
            return None
        return Request('JOINCODE', text=code)
    if kind == 'JOINGAME':
        game_id = fields.strip()
        if not GAME_ID.fullmatch(game_id):
            return None
        return Request('JOINGAME', game_id=game_id)
    if kind == 'JOINLAN':
        index_text = fields.strip()
        if not index_text.isdigit() or not 1 <= int(index_text) <= LAN_SHOWN_MAX:
            return None
        return Request('JOINLAN', index=int(index_text))
    return None


def request_text(request):
    """The text of a Request, as the page sends it (parse_request's inverse)."""
    if request.kind == 'HOST' and request.players:
        public = '1' if request.public else '0'
        extra = f'+{request.open}' if request.open else ''
        return f'HOST|{public}|{request.players}{extra}|{request.name}'
    if request.kind == 'JOINCODE':
        return f'JOINCODE|{request.text}'
    if request.kind == 'JOINGAME':
        return f'JOINGAME|{request.game_id}'
    if request.kind == 'JOINLAN':
        return f'JOINLAN|{request.index}'
    if request.kind == 'JOINSLOT':
        return f'JOIN{request.slot}'
    return request.kind


def request_hex(text):
    """A request as a Steam-started page spells it through the helper's pipes (steamplay.TextRequests): the lowercase
    hex of its UTF-8 bytes."""
    return text.encode('utf-8').hex()


def request_from_hex(digits):
    """The request text of spelled hex digits, or None when they are not the hex of UTF-8 text."""
    if not digits or len(digits) % 2 or not re.fullmatch(r'[0-9a-f]+', digits):
        return None
    try:
        return bytes.fromhex(digits).decode('utf-8')
    except UnicodeDecodeError:
        return None


# ---- what a join names ----
@dataclass(frozen=True)
class JoinTarget:
    """What a typed or pasted text names: a join code of the master ('code'), a long LAN invite code ('invite'), host
    addresses ('address'), or nothing usable ('invalid', with the reason for the page)."""
    kind: str
    code: str = ''
    addresses: tuple = ()   # (host, TCP lobby port) pairs, tried in this order
    token: str = ''         # an invite code's token
    why: str = ''


def parse_address(text):
    """'10.20.0.5', '10.20.0.5:47001', 'host.example', 'host.example:47001' -> (host, TCP lobby port), or None. The
    port is the host's lobby (TCP, the relay's port + 1); without one the default lobby port."""
    host, colon, port_text = text.rpartition(':')
    if not colon:
        host = text
        port_text = ''
    if port_text and (not port_text.isdigit() or not 0 < int(port_text) < 65536):
        return None
    port = int(port_text) if port_text else LOBBY_TCP_PORT
    if session.IPV4.fullmatch(host):
        return host, port
    if HOST_NAME.fullmatch(host) and re.search(r'[A-Za-z]', host):
        return host.rstrip('.'), port
    return None


def join_target(text, normalize_code=None):
    """Classify what a player typed or pasted (NETPLAY.md: the text box takes a join code, a long LAN invite code, or
    an address). normalize_code: nat.normalize_code (None when package 2 is missing: codes are then unknown)."""
    text = (text or '').strip()[:JOIN_TEXT_MAX]
    if not text:
        return JoinTarget('invalid', why='Nothing to join: type or paste a join code, an invite code or an address.')
    invite = lobby.parse_invite(text)
    if invite:
        address, udp_port, token = invite
        return JoinTarget('invite', addresses=((address, udp_port + 1),), token=token)
    code = normalize_code(text) if normalize_code else None
    if code:
        return JoinTarget('code', code=code)
    if CODE_SHAPE.fullmatch(text):
        return JoinTarget('invalid', why=f'{text} is not a join code (codes use 2-9 and A-Z without I and O).')
    addresses = []
    for part in re.split(r'[,;]', text):   # a list of addresses: comma or semicolon; text with spaces is no address
        part = part.strip()
        if not part:
            continue
        address = parse_address(part)
        if address is None:
            return JoinTarget('invalid', why=f'{part[:60]} is neither a join code, an invite code nor an address.')
        addresses.append(address)
    return JoinTarget('address', addresses=tuple(dict.fromkeys(addresses)))


# ---- the state table ----
def clean_value(value):
    """A state value without None entries (the page's Lua table has no use for nil fields)."""
    if isinstance(value, dict):
        return {key: clean_value(item) for key, item in value.items() if item is not None}
    if isinstance(value, (list, tuple)):
        return [clean_value(item) for item in value if item is not None]
    return value


class State:
    """The state table the page draws (NETPLAY.md), and the stamp that tells the page it changed. Thread safe: the flow's
    jobs write it, the backend reads it."""

    def __init__(self):
        self.lock = threading.Lock()
        self.stamp = 0
        self.phase = 'idle'
        self.lines = ()
        self.fields = {}

    def set(self, phase, lines, **fields):
        """A new phase and lines; fields replace the earlier ones."""
        lines = tuple(str(line)[:LINE_MAX_CHARS] for line in lines if line is not None)[:LINES_MAX]
        fields = clean_value(fields)
        with self.lock:
            if (phase, lines, fields) == (self.phase, self.lines, self.fields):
                return
            self.stamp += 1
            self.phase = phase
            self.lines = lines
            self.fields = fields

    def update(self, **fields):
        """Change some fields and keep the phase and lines (players coming, a ping); None takes a field away."""
        with self.lock:
            merged = dict(self.fields)
            for key, value in fields.items():
                if value is None:
                    merged.pop(key, None)
                else:
                    merged[key] = clean_value(value)
            if merged == self.fields:
                return
            self.stamp += 1
            self.fields = merged

    def view(self):
        """(stamp, phase, lines, fields) as they are now."""
        with self.lock:
            return self.stamp, self.phase, self.lines, dict(self.fields)

    def text(self, stamp=None):
        """The table as the Lua file the page reads (lobby.state_text); stamp: another stamp sequence's number."""
        own_stamp, phase, lines, fields = self.view()
        return lobby.state_text(own_stamp if stamp is None else stamp, phase, lines, **fields)


# ---- package 2's modules ----
class Network:
    """masterclient, nat, netifs and landisco (package 2), imported when first asked for. A module this checkout does
    not have is None, and the flow goes on without it (no master: the LAN lobby). Tests pass fakes by name."""
    NAMES = ('masterclient', 'nat', 'netifs', 'landisco')

    def __init__(self, **modules):
        unknown = set(modules) - set(self.NAMES)
        if unknown:
            raise ValueError(f'unknown network modules: {sorted(unknown)}')
        self.modules = dict(modules)
        self.lock = threading.Lock()

    def get(self, name):
        with self.lock:
            if name not in self.modules:
                try:
                    self.modules[name] = importlib.import_module(f'{__package__}.{name}')
                except ImportError:
                    self.modules[name] = None
            return self.modules[name]


def master_error_types(net):
    """The exceptions a master call may raise: the client's MasterError, and the socket's own."""
    module = net.get('masterclient')
    error = getattr(module, 'MasterError', None) if module else None
    return (error, OSError) if isinstance(error, type) else (OSError,)


def error_why(error):
    """The page's text for a master error: its 'why', or the exception's own text."""
    why = getattr(error, 'why', '') or str(error)
    return str(why)[:LINE_MAX_CHARS]


# ---- what the flow hands to its backend ----
@dataclass
class RelayPlan:
    """The relay this PC runs for the session (direct and LAN modes): bound to every interface, sources allowed as the
    host's lobby saw them (allow_from now, allow_file for the ones that check in later; NETPLAY.md "VLANs")."""
    port: int
    token: str
    allow_file: Path
    allow_from: tuple = ()


@dataclass
class SessionStart:
    """Everything the backend needs to load the session: this PC's slot and relay, the common load second, the
    overlay and session save its X4 runs (installed already), and the relay it runs itself (None: the relay is
    elsewhere: on the host's PC, or the master's)."""
    slot: int
    clients: int
    relay_host: str       # the IPv4 address this PC's game sends to
    port: int             # the relay's UDP port
    token: str            # the relay token (32 hex)
    arm_at: int           # the UTC second every client loads at
    overlay: Path
    snapshot: Path
    mode: str             # 'direct', 'relay' or 'lan'
    relay: RelayPlan | None = None
    late: bool = False    # a drop-in slot: this PC joins the running game (hotjoin.hot_join; arm_at is past or unused)


@dataclass
class FlowConfig:
    """What the flow plays with. kit: a netkit.Kit; master_url: None for no master (the LAN lobby only)."""
    kit: object
    release: str
    master_url: str | None = None
    port: int = RELAY_UDP_PORT    # the relay's UDP port when hosting; the lobby takes port + 1 (TCP)
    advertise: str = ''           # --bind: the address put into invite codes (the listeners bind 0.0.0.0 all the same)
    load_wait: int = 30           # seconds from the last ready player to the common load (LAN games)
    lan_discovery: bool = True
    mode: str = 'coop'            # a new session's mode and start (netkit.HOST_SESSION_START)
    base_start: str = ''


class Cancelled(Exception):
    """The player cancelled, or the flow ended: a job unwinds and cleans up."""


COMMAND_POLL_S = 0.1   # how often a job waiting for a request looks at its queue and its cancel
LOADING_POLL_S = 0.2


# ---- jobs ----
class Job:
    """One thing the flow does at a time, in its own thread: host a game, join one, or list games. Requests that
    belong to it (START, READY, LEAVE) reach it through commands."""
    replaceable = False   # a browse may give way to another request; a host or a join is cancelled first

    def __init__(self, flow, request):
        self.flow = flow
        self.request = request
        self.cancelled = threading.Event()
        self.commands = queue.Queue()
        self.thread = None
        self.cleanups = []   # what to undo when the job ends, latest first

    @property
    def net(self):
        return self.flow.net

    @property
    def config(self):
        return self.flow.config

    def start(self):
        self.thread = threading.Thread(target=self.main, name=f'netplay-{type(self).__name__}', daemon=True)
        self.thread.start()
        return self

    def alive(self):
        return self.thread is not None and self.thread.is_alive()

    def cancel(self):
        self.cancelled.set()

    def current(self):
        return self.flow.job is self and not self.cancelled.is_set()

    def check(self):
        if self.cancelled.is_set():
            raise Cancelled()

    def sleep(self, seconds):
        """Wait, but not past a cancel."""
        if self.cancelled.wait(seconds):
            raise Cancelled()

    def command(self, timeout=0.0):
        """The next request for this job, or None after timeout seconds; a cancel ends the wait (Cancelled)."""
        deadline = time.time() + timeout
        while True:
            try:
                return self.commands.get_nowait()
            except queue.Empty:
                pass
            self.check()
            left = deadline - time.time()
            if left <= 0:
                return None
            self.cancelled.wait(min(COMMAND_POLL_S, left))

    def show(self, phase, *lines, **fields):
        """The page's state, while this job is the flow's."""
        if self.current():
            self.flow.state.set(phase, lines, **fields)

    def update(self, **fields):
        if self.current():
            self.flow.state.update(**fields)

    def progress(self, what, done):
        self.update(progress={'what': what, 'done': round(max(0.0, min(1.0, float(done))), 3)})

    def on_end(self, action):
        """Run action when the job ends (cancelled, failed or done)."""
        self.cleanups.append(action)

    def wait_until_running(self, start, **fields):
        """After the start: 'loading' until the backend has written the session's config (Flow.session_running)."""
        while not self.flow.session_started.is_set():
            self.check()
            if self.flow.session_over.is_set():
                return
            seconds = max(0, start.arm_at - int(time.time()))
            self.show('loading', f'The game loads in {seconds} s: you are player {start.slot} of {start.clients}.', **fields)
            self.sleep(LOADING_POLL_S)

    def main(self):
        try:
            self.run()
        except Cancelled:
            pass
        except JobFailed as failure:
            self.flow.failed(self, failure.lines)
        except SystemExit as error:   # the launcher's own refusals (netkit, session) carry the reason as text
            self.flow.failed(self, (str(error),))
        except Exception as error:   # a bug must not leave the page waiting forever
            self.flow.log(f'netplay: {type(self).__name__} failed: {type(error).__name__}: {error}')
            self.flow.failed(self, ('Something went wrong in the launcher:', f'{type(error).__name__}: {error}'))
        finally:
            for action in reversed(self.cleanups):
                try:
                    action()
                except Exception as error:   # every cleanup runs, whatever the one before did
                    self.flow.log(f'netplay: cleanup failed: {error}')
            self.flow.ended(self)

    def run(self):
        raise NotImplementedError


class JobFailed(Exception):
    """A job ends with lines for the page (phase 'error')."""
    def __init__(self, *lines):
        super().__init__(' '.join(lines))
        self.lines = lines


LAN_SCAN_S = 2.5          # landisco.scan's listening time
BROWSE_REFRESH_S = 10     # the list is asked again this often while the page shows it


class BrowseJob(Job):
    """BROWSE: the public games of the master server and the games announced on the LAN, again every few seconds."""
    replaceable = True

    def run(self):
        self.show('browsing', 'Looking for games...')
        master, master_note = self.flow.master()
        # a LAN scan listens for broadcasts: inbound, which Windows Firewall blocks for a program it has no rule for
        # (the laptop heard nothing with the release's own python.exe, 23 Nov); asked once per BROWSE job
        blocked = firewall_allowed() is False
        while True:
            games = self.public_games(master)
            lan = self.lan_games()
            self.flow.listing = {'games': games, 'lan': lan}
            lines = [f'{len(games)} public game(s), {len(lan)} on the local network.']
            if master_note:
                lines.append(master_note)
            if not games and not lan:
                lines.append('Nobody is hosting right now. Host a game, or join with a code.')
            extra = {'firewall': 'blocked'} if blocked and not lan else {}
            self.show('browsing', *lines, games=[game_row(game, self.config.release) for game in games],
                      lan=[lan_row(index, entry, self.config.release) for index, entry in enumerate(lan, 1)], **extra)
            self.command(BROWSE_REFRESH_S)   # BROWSE again asks at once; otherwise the list is asked again after a while

    def public_games(self, master):
        if master is None:
            return []
        try:
            games = master.games(self.config.release)
        except master_error_types(self.net) as error:
            self.flow.log(f'netplay: the public list failed: {error_why(error)}')
            return []
        return [game for game in games if isinstance(game, dict) and GAME_ID.fullmatch(str(game.get('game_id', '')))][:GAMES_SHOWN_MAX]

    def lan_games(self):
        landisco = self.net.get('landisco')
        if landisco is None or not self.config.lan_discovery:
            return []
        try:
            found = landisco.scan(LAN_SCAN_S)
        except OSError as error:   # the discovery port is taken (another launcher on this PC listens)
            self.flow.log(f'netplay: no LAN scan: {error}')
            return []
        return [entry for entry in found if isinstance(entry, dict)][:LAN_SHOWN_MAX]


def game_row(game, release):
    """A public game as the state's games list has it."""
    compatible = game.get('compatible')
    if compatible is None:
        compatible = game.get('release') == release
    return {'id': str(game.get('game_id')), 'name': clean_name(game.get('name') or 'A game'),
            'players': int(game.get('players') or 0), 'max': int(game.get('players_max') or 0),
            'state': str(game.get('state') or 'lobby'), 'compatible': bool(compatible),
            'mode': str(game.get('mode') or '') or None, 'open': int(game.get('open_slots_free') or 0)}


def lan_row(index, entry, release):
    """A LAN announcement as the state's lan list has it."""
    return {'index': index, 'name': clean_name(entry.get('name') or 'A LAN game'), 'players': int(entry.get('players') or 0),
            'max': int(entry.get('players_max') or 0), 'compatible': entry.get('release') == release}


# ---- the master's lobby of one game ----
LOBBY_POLL_WAIT_S = 25    # the master's long poll holds an answer this long at most (NETPLAY.md)
LOBBY_RETRY_MAX_S = 10
HEARTBEAT_S = 30          # a game without a heartbeat for 90 s is gone at the master


class LobbyWatch:
    """The master's lobby of one game (players, ready, the start), long-polled in its own thread, so that a job's loop
    never waits 25 s for it. latest() is the newest answer; error the last failure (None after an answer)."""

    def __init__(self, client, game_id, ticket, errors, log=print, wait=LOBBY_POLL_WAIT_S):
        self.client = client
        self.game_id = game_id
        self.ticket = ticket
        self.errors = errors
        self.log = log
        self.wait = wait
        self.lock = threading.Lock()
        self.answer = None
        self.since = 0
        self.error = None
        self.failures = 0
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, name='netplay-lobby', daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopped.is_set():
            try:
                answer = self.client.lobby(self.game_id, self.ticket, since=self.since, wait=self.wait)
            except self.errors as error:
                with self.lock:
                    self.error = error
                    self.failures += 1
                    pause = min(2 * self.failures, LOBBY_RETRY_MAX_S)
                self.stopped.wait(pause)
                continue
            if not isinstance(answer, dict):
                self.stopped.wait(1)
                continue
            with self.lock:
                self.answer = answer
                self.error = None
                self.failures = 0
                stamp = answer.get('stamp')
                if isinstance(stamp, int) and stamp > self.since:
                    self.since = stamp

    def latest(self):
        with self.lock:
            return self.answer

    def gone(self):
        """True when the master no longer knows the game (deleted, or no heartbeat for 90 s)."""
        with self.lock:
            error = self.error
        return error is not None and getattr(error, 'status', 0) == 404

    def stop(self):
        self.stopped.set()


class Heartbeat:
    """The master's heartbeat of one game (the host's secret) or one player (a ticket), every 30 s in its own thread:
    an upload, a download, an overlay build or X4 starting again takes minutes, and a game without a heartbeat for
    90 s is gone. state() gives (the game's state, the relay's tick) at each beat."""

    def __init__(self, client, game_id, auth, errors, log=print, state=None, interval=HEARTBEAT_S):
        self.client = client
        self.game_id = game_id
        self.auth = auth
        self.errors = errors
        self.log = log
        self.state = state or (lambda: ('lobby', 0))
        self.interval = interval
        self.stopped = threading.Event()
        self.beats = 0
        self.thread = threading.Thread(target=self.run, name='netplay-heartbeat', daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopped.wait(self.interval):
            state, tick = self.state()
            try:
                self.client.heartbeat(self.game_id, self.auth, tick=tick, state=state)
                self.beats += 1
            except self.errors as error:
                self.log(f'netplay: heartbeat failed: {error_why(error)}')

    def stop(self):
        self.stopped.set()


def player_rows(answer, fallback=()):
    """The players of a lobby answer as the state's players list ({slot, ready, host})."""
    rows = []
    for player in (answer or {}).get('players') or []:
        if not isinstance(player, dict) or not isinstance(player.get('slot'), int):
            continue
        rows.append({'slot': player['slot'], 'ready': bool(player.get('ready')), 'host': bool(player.get('host'))})
    return sorted(rows, key=lambda row: row['slot']) or list(fallback)


def resolve_ipv4(host):
    """A relay host as the dotted IPv4 address the gate needs (native_gate_relay_host takes no names). Raises
    JobFailed when the name has no IPv4 address."""
    host = str(host or '')
    if session.IPV4.fullmatch(host):
        return host
    import socket
    try:
        found = socket.getaddrinfo(host, None, socket.AF_INET, socket.SOCK_DGRAM)
    except (OSError, UnicodeError):
        found = []
    for entry in found:
        address = entry[4][0]
        if session.IPV4.fullmatch(address):
            return address
    raise JobFailed(f'The relay address {host[:60]} has no IPv4 address this PC can use.')


def checked_start(start):
    """A lobby answer's start ({load_at, relay: {host, port, token}, mode}) checked field by field; JobFailed when a
    value could not go into the game's config as it is (a token is 32 hex, a port 1..65535)."""
    relay = start.get('relay') if isinstance(start, dict) else None
    if not isinstance(relay, dict):
        raise JobFailed('The master server sent a start without a relay.')
    token = str(relay.get('token') or '')
    port = relay.get('port')
    load_at = start.get('load_at')
    mode = str(start.get('mode') or 'relay')
    if not HEX32.fullmatch(token) or not isinstance(port, int) or not 0 < port < 65536:
        raise JobFailed('The master server sent a start this launcher cannot use (relay token or port).')
    if not isinstance(load_at, (int, float)) or mode not in ('direct', 'relay'):
        raise JobFailed('The master server sent a start this launcher cannot use (load time or mode).')
    return {'mode': mode, 'host': str(relay.get('host') or ''), 'port': port, 'token': token, 'load_at': int(load_at)}


# ---- the flow ----
IDLE_LINES =('Host a game, or join one: with a code, from the list, or by address.',)


class Flow:
    """The page's requests and its state, and the one job that runs at a time.

    backend: what the flow needs from the launcher it runs in (see Backend). The flow hands a session's start to the
    backend through starts (a queue of SessionStart) and goes on writing the state during the session."""

    def __init__(self, config, backend, net=None, log=print):
        self.config = config
        self.backend = backend
        self.net = net or Network()
        self.log = log
        self.state = State()
        self.lock = threading.RLock()
        self.job = None
        self.starts = queue.Queue()
        self.handover = None              # the Steam helper: the request X4 is started again for (backend.restart_for)
        self.listing = {'games': [], 'lan': []}   # the last browse: what JOINGAME and JOINLAN name
        self.copy_text = ''               # what COPY puts on the clipboard (a join code or an invite code)
        self.session_over = threading.Event()
        self.session_started = threading.Event()   # the backend wrote the session's config: the page may run it
        self.session_info = {}            # the backend's view of the running session (run folder, tick)
        self.idle()

    def idle(self, *lines):
        self.state.set('idle', [*self.idle_lines(), *lines])

    def idle_lines(self):
        lines = getattr(self.backend, 'idle_lines', None)
        return tuple(lines()) if callable(lines) else IDLE_LINES

    # requests
    def request(self, text):
        """A request of the page (its text). Returns the parsed request, None when the text is not one."""
        request = parse_request(text)
        if request is None:
            self.log(f'netplay: not a request: {str(text)[:80]!r}')
            return None
        with self.lock:
            self.dispatch(request)
        return request

    def dispatch(self, request):
        job = self.job if self.job is not None and self.job.alive() else None
        if request.kind == 'COPY':
            self.copy()
            return
        if request.kind == 'FIREWALL':
            self.allow_firewall()
            return
        if request.kind == 'CANCEL' and job is not None and self.state.phase in ('loading', 'session'):
            # the start is out and the others load: this player leaves, the game is not taken away from them
            job.commands.put(Request('LEAVE'))
            return
        if request.kind == 'CANCEL':
            self.cancel()
            return
        if request.kind in ('START', 'READY', 'LEAVE'):
            if job is not None:
                job.commands.put(request)
            return
        if job is not None and not job.replaceable:
            self.log(f'netplay: {request.kind} while a game is being hosted or joined: Cancel first')
            return
        if job is not None:
            job.cancel()
        self.begin(request)

    def begin(self, request):
        if request.kind == 'JOINSLOT':
            self.state.set('error', ('Choosing a start of a running game needs a kit with a server.',))
            return
        if self.backend.restart_for(request):
            self.hand_over(request)
            return
        # The first phase at once: the answer to a pipe's open is the state right after the request. The job is the
        # flow's before its thread runs, or what it shows first would not count as its own (Job.current).
        if request.kind == 'BROWSE':
            self.state.set('browsing', ('Looking for games...',))
            job = BrowseJob(self, request)
        elif request.kind == 'HOST':
            from . import nethost
            self.state.set('preparing', ('Getting the session ready...',))
            job = nethost.HostJob(self, request)
        else:
            from . import netjoin
            self.state.set('joining', ('Joining...',))
            job = netjoin.JoinJob(self, request)
        self.job = job
        job.start()

    def hand_over(self, request):
        """The Steam helper: the request is carried out by X4 started again in the disposable profile (x4ls lobby
        --request). A join is resolved here first (the clipboard, the LAN row), so that nothing it needs is lost."""
        if request.kind == 'HOST':
            text = request_text(request)
            what = 'It hosts the game there.'
        else:
            plan = self.join_plan(request)
            if plan[0] == 'invalid':
                self.state.set('error', (plan[1],))
                return
            text = plan_text(plan)
            what = 'It joins the game there.'
        self.handover = text
        self.state.set('restart', ('X4 closes now and comes back in a moment, in its multiplayer profile.', what))

    def take_handover(self):
        with self.lock:
            text = self.handover
            self.handover = None
            return text

    def join_plan(self, request):
        """What a join request names: ('game', game id, listed game or None), ('target', JoinTarget, LAN entry or None),
        or ('invalid', why, None)."""
        if request.kind == 'JOINGAME':
            listed = next((game for game in self.listing['games'] if str(game.get('game_id')) == request.game_id), None)
            return 'game', request.game_id, listed
        if request.kind == 'JOINLAN':
            lan = self.listing['lan']
            if not 1 <= request.index <= len(lan):
                return 'invalid', 'That LAN game is not in the list any more: Browse again.', None
            entry = lan[request.index - 1]
            return 'target', lan_target(entry, self.net), entry
        text = request.text
        if request.kind == 'JOIN':
            text = self.backend.clipboard_get()[:JOIN_TEXT_MAX]
            if not text.strip():
                return 'invalid', 'The clipboard is empty: copy the join code or invite code first, then press Join.', None
        nat = self.net.get('nat')
        target = join_target(text, getattr(nat, 'normalize_code', None))
        if target.kind == 'invalid':
            return 'invalid', target.why, None
        return 'target', target, None

    def copy(self):
        if self.copy_text:
            self.backend.clipboard_set(self.copy_text)

    def allow_firewall(self):
        """FIREWALL: ask Windows for an inbound rule for this launcher's own Python, through Windows' own elevation prompt
        (the player sees it and decides). The router had opened the ports and the master still could not reach this PC:
        Windows Defender Firewall knows nothing of a release's own python.exe (the live runs of 23 Nov). The rule takes
        effect for the next game this PC hosts."""
        rule = firewall_rule()
        if rule is None:
            self.log('netplay: FIREWALL: nothing to do off Windows')
            return
        try:
            import ctypes
            started = ctypes.windll.shell32.ShellExecuteW(None, 'runas', 'netsh.exe', rule, None, 0)
        except (OSError, AttributeError) as error:
            self.log(f'netplay: FIREWALL: could not ask Windows: {error}')
            return
        if started > 32:
            self.log('netplay: FIREWALL: Windows was asked for the rule')
            self.note('Windows Firewall: once allowed, the next game you host is direct (Cancel, then Host again).')
        else:
            self.log(f'netplay: FIREWALL: not allowed (ShellExecute {started})')
            self.note('Windows Firewall was not changed: the game goes on through the server\'s relay.')

    def note(self, line):
        """One more line under the page's current lines (the phase and the rest of the state stay)."""
        _stamp, phase, lines, fields = self.state.view()
        kept = [text for text in lines if not str(text).startswith('Windows Firewall')][:LINES_MAX - 1]
        self.state.set(phase, [*kept, line], **fields)

    def cancel(self):
        """CANCEL: the job unwinds in its thread (it leaves or deletes its game there); the page is idle at once."""
        job = self.job
        self.job = None
        self.handover = None
        if job is not None:
            job.cancel()
        self.copy_text = ''
        self.idle()

    # what the jobs report
    def failed(self, job, lines):
        with self.lock:
            if self.job is job:
                self.state.set('error', [*lines, 'Cancel to go back.'])

    def ended(self, job):
        with self.lock:
            if self.job is job and self.state.phase not in ('error', 'session', 'loading'):
                self.job = None
                self.idle()

    def begin_session(self, start):
        """A job's session starts: the backend takes it from starts."""
        self.starts.put(start)

    def take_start(self, timeout=0.0):
        try:
            return self.starts.get(timeout=timeout) if timeout else self.starts.get_nowait()
        except queue.Empty:
            return None

    def session_running(self, **info):
        """The backend: the session's config is written and it runs (the run folder, a reader of the relay's tick).
        Only now may the page hear 'session': it runs the config and the overlay's files on that phase."""
        self.session_info.update(info)
        self.session_started.set()

    def end_session(self, result=None):
        """The backend: the session is over (its X4 closed); the job ends its game."""
        self.session_info['result'] = result
        self.session_over.set()
        job = self.job
        if job is not None:
            job.cancel()

    # the master
    def master(self):
        """(a client of the master server or None, a line for the page or ''). None: no master is configured, this
        checkout has no master client, or the server does not answer; the flow then keeps to the LAN."""
        url = self.config.master_url
        module = self.net.get('masterclient')
        if not url:
            return None, ''
        if module is None:
            return None, 'This launcher has no master client: LAN games only.'
        client = module.MasterClient(url)
        try:
            client.health()
        except master_error_types(self.net) as error:
            self.log(f'netplay: the master server does not answer: {error_why(error)}')
            return None, 'The master server does not answer: LAN games only.'
        return client, ''

    def master_ping(self, client):
        """(public IP known, round trip in ms or None) through the master's UDP echo (MasterClient.echo_address)."""
        nat = self.net.get('nat')
        echo = getattr(client, 'echo_address', None)
        if nat is None or not callable(echo):
            return False, None
        errors = (ValueError, TypeError) + master_error_types(self.net)
        try:
            address = echo()
            if not address:
                return False, None
            host, port = address
            seen = nat.public_endpoint(host, echo_port=port)
        except errors as error:
            self.log(f'netplay: no echo from the master: {error}')
            return False, None
        if not seen:
            return False, None
        return True, int(round(float(seen[2])))


def lan_target(entry, net):
    """A LAN announcement as a JoinTarget: its code when it has one (the game is the master's), else its candidate
    addresses at its lobby port, the one on this PC's subnet first (netifs.best_for), then the sender's."""
    tcp_port = int(entry.get('tcp_port') or LOBBY_TCP_PORT)
    candidates = [str(ip) for ip in entry.get('candidates') or [] if session.IPV4.fullmatch(str(ip))]
    sender = str(entry.get('ip') or '')
    ordered = order_candidates(candidates, net)
    if session.IPV4.fullmatch(sender) and sender not in ordered:
        ordered.append(sender)
    addresses = tuple((ip, tcp_port) for ip in ordered)
    code = str(entry.get('code') or '-')
    nat = net.get('nat')
    normalized = nat.normalize_code(code) if nat is not None and code != '-' else None
    if normalized:
        return JoinTarget('code', code=normalized, addresses=addresses)
    return JoinTarget('address', addresses=addresses)


def order_candidates(candidates, net, own_ip=None):
    """A host's LAN candidates in the order a joiner tries them: the ones on this PC's own subnet first
    (netifs.best_for with this PC's primary address), then the others as the host gave them."""
    candidates = list(dict.fromkeys(candidates))
    netifs = net.get('netifs')
    if netifs is None or not candidates:
        return candidates
    try:
        ordered = list(netifs.best_for(own_ip or lobby.local_address(), candidates))
    except (OSError, ValueError, TypeError):
        return candidates
    return [ip for ip in ordered if ip in candidates] + [ip for ip in candidates if ip not in ordered]


def plan_text(plan):
    """A resolved join as the request text x4ls lobby --request takes (the Steam helper's hand-over)."""
    kind, value, _ = plan
    if kind == 'game':
        return f'JOINGAME|{value}'
    target = value
    if target.kind == 'code':
        return f'JOINCODE|{target.code}'
    if target.kind == 'invite':
        host, tcp_port = target.addresses[0]
        return f'JOINCODE|{lobby.invite(host, tcp_port - 1, target.token)}'
    return 'JOINCODE|' + ','.join(f'{host}:{port}' for host, port in target.addresses)


class Backend:
    """What the flow needs from the launcher it runs in. x4ls lobby (netlobby.LobbyBackend) makes and installs sessions;
    the Steam helper (steamplay) hands hosting and joining over to x4ls lobby (restart_for)."""

    def restart_for(self, request):
        """True: this request is carried out by starting X4 again elsewhere (the flow says 'restart')."""
        return False

    def clipboard_get(self):
        return lobby.clipboard_get()

    def clipboard_set(self, text):
        return lobby.clipboard_set(text)

    def host_session(self, players, progress, cancelled, open_slots=()):
        """The session to host for this many players (netkit.SessionFiles), open_slots joined while it runs; may close
        X4 to make one."""
        raise NotImplementedError

    def save_folder(self):
        """The save folder of this PC's X4 in the session (its join saves are in save/mpjoin), or None."""
        return None

    def install_session(self, files, progress):
        """X4 runs this session's overlay with its save staged (starts X4 again when it runs another)."""
        raise NotImplementedError

    def kit_session(self):
        """The kit's own session (a playtest kit's overlay and snapshot), or None."""
        return None

    def installed_dlcs(self):
        """{DLC id: version} of the official DLCs this PC's X4 runs with (game.installed_dlcs), or None when unknown: a
        joiner then skips the DLC check."""
        return None

    def work_folder(self):
        """A folder of this run for a job's own files (the relay's allow list, the copies a host uploads)."""
        raise NotImplementedError


def wait_until(predicate, timeout, step=0.05):
    """Test and launcher helper: poll predicate until it is true or timeout seconds pass; its last value."""
    deadline = time.time() + timeout
    while True:
        value = predicate()
        if value or time.time() > deadline:
            return value
        time.sleep(step)


__all__ = ['Request', 'parse_request', 'request_text', 'request_hex', 'request_from_hex', 'JoinTarget', 'join_target',
           'State', 'Network', 'Flow', 'FlowConfig', 'SessionStart', 'RelayPlan', 'Backend', 'Job', 'JobFailed', 'LobbyWatch']


def firewall_rule(executable=None):
    """netsh's arguments for an inbound rule allowing this Python (python.exe beside pythonw.exe), or None off Windows."""
    import os
    import sys
    if os.name != 'nt':
        return None
    exe = Path(executable or sys.executable)
    if exe.name.lower() == 'pythonw.exe':
        exe = exe.with_name('python.exe')
    name = f'X4 Online ({exe.parent.parent.name or exe.parent.name})'
    return (f'advfirewall firewall add rule name="{name}" dir=in action=allow program="{exe}" enable=yes '
            'profile=any description="X4 Online: other players reach the game on this PC directly"')


def firewall_allowed(executable=None, rules_text=None):
    """Whether Windows Firewall has an enabled inbound Allow rule for this Python (python.exe beside pythonw.exe):
    True, False, or None off Windows or when netsh cannot be asked. Windows itself adds such rules ("python.exe", TCP
    and UDP) when the player answers its security alert, which comes when a new program first listens; this tells the
    page whether to offer FIREWALL. Reading the rules needs no administrator rights (netsh, under a second)."""
    import os
    import re as _re
    import subprocess
    import sys
    if os.name != 'nt' and rules_text is None:
        return None
    exe = Path(executable or sys.executable)
    if exe.name.lower() == 'pythonw.exe':
        exe = exe.with_name('python.exe')
    wanted = os.path.normcase(os.path.normpath(str(exe)))
    if rules_text is None:
        # Popen itself, not subprocess.run: the launcher's tests stand in for run() (the overlay build), and an answer
        # nobody can get is 'unknown' (no button), never an error of the page
        try:
            process = subprocess.Popen(['netsh', 'advfirewall', 'firewall', 'show', 'rule', 'name=all', 'dir=in', 'verbose'],
                                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            out, _ = process.communicate(timeout=20)
        except Exception:   # noqa: BLE001 - OSError, TimeoutExpired, or a stand-in that does not take these arguments
            return None
        if process.returncode:
            return None
        rules_text = out.decode('mbcs' if os.name == 'nt' else 'utf-8', errors='replace')
    for block in _re.split(r'\r?\n\s*\r?\n', rules_text):
        fields = {key.strip().lower(): value.strip() for key, value in _re.findall(r'^([A-Za-z ]+):\s*(.*)$', block, _re.M)}
        program = fields.get('program', '')
        if not program or os.path.normcase(os.path.normpath(program)) != wanted:
            continue
        if fields.get('enabled', '').lower() == 'yes' and fields.get('action', '').lower() == 'allow':
            return True
    return False
