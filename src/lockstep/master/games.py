"""A game on the master: what the host registered, who has joined, how it was started, and the API's answers about it.

Everything here is held in memory under the server's lock (server.Master); a restart of the master forgets every game
(the hosts register again). The registration is checked field by field: a game name is the only free text anyone can
publish, so it is short, printable and escaped wherever it is shown; nothing personal is asked for or kept beyond the
public IP each request comes from (it decides who may send to a relay and who sees a host's LAN addresses)."""
from dataclasses import dataclass, field
import ipaddress
import re
import threading
import unicodedata

NAME_MAX = 40                     # characters of a game name (the LAN announcement's 40 bytes, url-encoded there)
RELEASE = re.compile(r'[0-9a-z][0-9a-z._-]{0,63}')   # release.json's '<12 hex>-<12 hex>', and test builds
SHA256 = re.compile(r'[0-9a-f]{64}')
SECRET = re.compile(r'[0-9a-f]{32}')
GAME_ID = re.compile(r'[0-9a-f]{16}')
WANTS = ('auto', 'direct', 'relay')
PLAYERS_MIN = 2
PLAYERS_MAX = 8
PORT_MIN = 1024
PORT_MAX = 65535
LAN_CANDIDATES_MAX = 16
CLIENT_STATE = re.compile(r'[a-z_]{0,24}')
GAMESTART = re.compile(r'[a-z0-9_]{1,64}')   # a game's start, as X4's gamestarts.xml names it
TICK_LIMIT = 1 << 31


class BadRequest(ValueError):
    """A request body that cannot be taken; the text says which field (shown on the page)."""


@dataclass
class Player:
    slot: int
    ticket: str
    host: bool
    ip: str                  # the public IP of this player's last request (learned, never claimed)
    joined: float
    seen: float              # monotonic time of the last request with this ticket
    ready: bool = False
    ready_at: float = 0.0    # wall time of the readiness (the load second is 30 s after the last one)
    tick: int = 0            # the last heartbeat's tick and state (shown, never trusted for anything)
    state: str = ''


@dataclass
class StoredFile:
    path: object
    size: int
    sha256: str


@dataclass
class Game:
    game_id: str
    code: str
    secret: str
    name: str
    public: bool
    release: str
    players_max: int
    session_sha256: str
    open_slots: tuple
    want: str
    direct: dict | None
    lan: dict | None
    host_ip: str
    created: float           # wall time
    heartbeat: float         # monotonic time of the last heartbeat (or of the registration)
    changed: threading.Condition
    players: dict = field(default_factory=dict)      # slot -> Player
    files: dict = field(default_factory=dict)        # name -> StoredFile (the session's data files)
    join_sizes: dict = field(default_factory=dict)   # name -> StoredFile of the join files received (the budget's part)
    bytes_stored: int = 0                            # every stored file of the game (released with the game)
    uploading: set = field(default_factory=set)      # names being uploaded now (one upload per name at a time)
    state: str = 'lobby'     # 'lobby', 'started', 'ended'
    mode: str | None = None  # 'direct' or 'relay' once started
    start: dict | None = None
    starting: bool = False   # a start (a relay being started) is under way
    relay: object = None     # relays.RelayGame of a relay-mode game
    stamp: int = 1           # grows with every change a lobby answer shows
    ended_at: float = 0.0    # monotonic
    end_reason: str = ''
    gone_slots: set = field(default_factory=set)     # slots whose player left after the start (not given out again)
    gamestart: str = ''      # the session's start (a gamestart id) as the host named it, for the list; '' unknown

    @property
    def present_slots(self):
        """The slots that load together at the start (every slot that is not open)."""
        return [slot for slot in range(1, self.players_max + 1) if slot not in self.open_slots]

    def touch(self):
        """Something a lobby answer shows has changed: wake the long polls."""
        self.stamp += 1
        self.changed.notify_all()

    def free_slot(self):
        """The slot a new player gets now, or None: before the start the slots of the common load first, then the open
        ones (their players join the running game); after the start only open slots nobody has had."""
        taken = set(self.players) | self.gone_slots
        if self.state == 'lobby':
            for slot in self.present_slots + list(self.open_slots):
                if slot not in taken:
                    return slot
            return None
        if self.state == 'started':
            for slot in self.open_slots:
                if slot not in taken:
                    return slot
        return None

    def joinable_count(self):
        taken = set(self.players) | self.gone_slots
        if self.state == 'lobby':
            return sum(1 for slot in range(1, self.players_max + 1) if slot not in taken)
        if self.state == 'started':
            return sum(1 for slot in self.open_slots if slot not in taken)
        return 0


def require(condition, why):
    if not condition:
        raise BadRequest(why)


def is_int(value):
    return type(value) is int


def clean_name(value):
    """A game name as the host typed it: 1..40 printable characters, surrounding spaces dropped."""
    require(isinstance(value, str), 'name: text')
    name = value.strip()
    require(1 <= len(name) <= NAME_MAX, f'name: 1 to {NAME_MAX} characters')
    for character in name:
        category = unicodedata.category(character)
        require(category[0] != 'C' and category not in ('Zl', 'Zp'), 'name: printable characters only')
    return name


def port_pair(value, what):
    require(isinstance(value, dict), f'{what}: an object with udp_port and tcp_port')
    for key in ('udp_port', 'tcp_port'):
        port = value.get(key)
        require(is_int(port) and PORT_MIN <= port <= PORT_MAX, f'{what}.{key}: a port {PORT_MIN}..{PORT_MAX}')
    return {'udp_port': value['udp_port'], 'tcp_port': value['tcp_port']}


def lan_address(text):
    """A LAN candidate a host advertises: a dotted IPv4 address a PC can have (no loopback, link-local, multicast)."""
    try:
        address = ipaddress.ip_address(text)
    except ValueError:
        return False
    if not isinstance(address, ipaddress.IPv4Address):
        return False
    if address.is_loopback or address.is_link_local or address.is_multicast or address.is_unspecified:
        return False
    return str(address) == text and text != '255.255.255.255'


def parse_lan(value):
    if value is None:
        return None
    require(isinstance(value, dict), 'lan: an object or null')
    candidates = value.get('candidates')
    require(isinstance(candidates, list) and 1 <= len(candidates) <= LAN_CANDIDATES_MAX,
            f'lan.candidates: 1 to {LAN_CANDIDATES_MAX} addresses')
    require(all(isinstance(ip, str) and lan_address(ip) for ip in candidates), 'lan.candidates: dotted IPv4 addresses')
    ports = port_pair(value, 'lan')
    return {'candidates': list(dict.fromkeys(candidates)), **ports}


def parse_new_game(body):
    """The fields of POST /api/v1/games, checked; BadRequest names the first wrong one."""
    name = clean_name(body.get('name'))
    public = body.get('public')
    require(isinstance(public, bool), 'public: true or false')
    release = body.get('release')
    require(isinstance(release, str) and RELEASE.fullmatch(release), 'release: the release string of release.json')
    players_max = body.get('players_max')
    require(is_int(players_max) and PLAYERS_MIN <= players_max <= PLAYERS_MAX, 'players_max: 2..8')
    session_sha = body.get('session_sha256')
    require(isinstance(session_sha, str) and SHA256.fullmatch(session_sha), 'session_sha256: 64 lowercase hex digits')
    open_slots = body.get('open_slots', [])
    require(isinstance(open_slots, list) and all(is_int(slot) for slot in open_slots), 'open_slots: slot numbers')
    require(len(set(open_slots)) == len(open_slots), 'open_slots: each slot once')
    require(all(2 <= slot <= players_max for slot in open_slots), f'open_slots: slots 2..{players_max} (slot 1 is the host)')
    want = body.get('want', 'auto')
    require(want in WANTS, 'want: auto, direct or relay')
    direct = body.get('direct')
    if direct is not None:
        direct = port_pair(direct, 'direct')
    require(want != 'direct' or direct is not None, 'want direct: needs direct ports')
    gamestart = body.get('gamestart', '')
    require(isinstance(gamestart, str) and (gamestart == '' or GAMESTART.fullmatch(gamestart)),
            'gamestart: a gamestart id (a-z, 0-9, _; at most 64)')
    return {'name': name, 'public': public, 'release': release, 'players_max': players_max, 'session_sha256': session_sha,
            'open_slots': tuple(sorted(open_slots)), 'want': want, 'direct': direct, 'lan': parse_lan(body.get('lan')),
            'gamestart': gamestart}


def parse_heartbeat(body):
    tick = body.get('tick', 0)
    require(is_int(tick) and 0 <= tick < TICK_LIMIT, 'tick: a tick number')
    state = body.get('state', '')
    require(isinstance(state, str) and CLIENT_STATE.fullmatch(state), 'state: up to 24 letters a-z or _')
    return tick, state


def summary(game, release, now):
    """A game as the public list and the code lookup show it."""
    return {'game_id': game.game_id, 'name': game.name, 'release': game.release,
            'compatible': (release == game.release) if release is not None else None,
            'players': len(game.players), 'players_max': game.players_max, 'state': game.state, 'mode': game.mode,
            'open_slots_free': game.joinable_count(), 'age_s': max(0, int(now - game.created)),
            **({'gamestart': game.gamestart} if game.gamestart else {})}


def lobby_answer(game, player, pending_joins=None):
    """GET .../lobby for `player`: the players, the state, the start. Additions to the contract's fields: `slot` (the
    asker's own); for the host of a direct game each player's `ip` (its relay's --allow-file, learned by the master);
    `send_join` for a saver of a relay-mode drop-in game (the join files it is to upload)."""
    rows = []
    for slot in sorted(game.players):
        member = game.players[slot]
        row = {'slot': slot, 'ready': member.ready, 'host': member.host}
        if player.host and game.mode == 'direct':
            row['ip'] = member.ip
        rows.append(row)
    answer = {'ok': True, 'stamp': game.stamp, 'state': game.state, 'slot': player.slot, 'players': rows,
              'start': game.start}
    if pending_joins:
        wanted = [{'tick': tick, 'slot': slot} for tick, (slot, saver, _sha) in sorted(pending_joins.items())
                  if saver == player.slot]
        if wanted:
            answer['send_join'] = wanted
    return answer
