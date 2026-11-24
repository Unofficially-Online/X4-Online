"""x4ls master: the HTTP API v1, the game registry, the echo and the relay pool in one process (docs/NETPLAY.md).

    python3 tools/x4ls.py master --bind 0.0.0.0 --http-port 47100 --echo-port 47101 --relay-ports 47200-47263 --data DIR

A public internet service, so every request is treated as hostile until it proves otherwise:
- ThreadingHTTPServer with caps on open connections (all, and per IP), a socket timeout, JSON bodies of at most
  64 KiB and file uploads capped per name; a token bucket per IP and one for the whole server on every request, and
  stricter ones on new games, joins, code lookups and probes.
- A game's `secret` and a player's `ticket` are 32 hex digits from `secrets`, compared with hmac.compare_digest, sent
  in the JSON body or the X-X4MP-Auth header, and never logged. The join code is not logged either (it admits to a
  private game).
- Nothing uploaded is executed: the files are stored and hashed (files.py), the recipe is read as data
  (relays.recipe_settings), a join package is checked by its reader before it is served.
- The probe connects only to the request's own source IP; the echo answers only the source, never with more bytes than
  it got.

Behind a reverse proxy (nginx with HTTPS, --http-bind 127.0.0.1 --trust-proxy): the client's IP is the last address of
X-Forwarded-For, taken only from connections of the proxy itself (127.0.0.1 or ::1), and that IP is the one every
decision uses (rate limits, caps, the probe target, LAN candidates, the relay's allow list). The echo and the relays
are UDP and never pass the proxy, so their address is --public-host.

The registry is in memory: a restart forgets every game, and the data folder's games/ is emptied at the start."""
from dataclasses import dataclass, field
import hmac
import http.server
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import threading
import time
from urllib.parse import parse_qs, urlsplit

from . import codes, files, games, page, relays, udp
from .limits import MasterLimits, RateLimiter, per_minute
from ..launcher import session

API_VERSION = 1
AUTH_HEADER = 'X-X4MP-Auth'
SHA_HEADER = 'X-X4MP-Sha256'
LIST_MAX = 100                    # public games in one answer
PREFIX = re.compile(r'(?:/[A-Za-z0-9._~-]+)*')
LOOPBACK_PEERS = ('127.0.0.1', '::1', '::ffff:127.0.0.1')
PROBE_TARGET_FIELDS = ('ip', 'host', 'address', 'target')
DRAIN_S = 2.0                     # a refused request body is read this long at most (a graceful close, not a reset)
COMMON_HEADERS = {'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer'}
PAGE_HEADERS = {'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; "
                                           "form-action 'none'; frame-ancestors 'none'",
                'X-Frame-Options': 'DENY'}
JOIN_FILE_STATES = ('lobby', 'started')


@dataclass
class MasterOptions:
    bind: str = '0.0.0.0'          # the UDP side: the echo and the relays (and the relays' join files, TCP)
    http_port: int = 47100
    echo_port: int = 47101
    relay_ports: tuple = (47200, 47263)
    data: Path = session.ROOT / '.local/master'
    limits: MasterLimits = field(default_factory=MasterLimits)
    http_bind: str = ''            # the HTTP API's address (default: bind); 127.0.0.1 behind a reverse proxy
    public_host: str = ''          # the relays' IPv4 address players send to (default: the address a request reached)
    public_prefix: str = ''        # the HTML view's link prefix behind a proxy ('/x4mp')
    trust_proxy: bool = False      # take the client IP from X-Forwarded-For of connections from 127.0.0.1/::1
    relay_releases: tuple = ()     # the releases the relay pool serves (empty: any release)
    client_ip: object = None       # test hook: handler -> the client's public IP


class ApiError(Exception):
    """An answer {"ok": false, "error": code, "why": text} with an HTTP status."""
    def __init__(self, status, code, why):
        super().__init__(why)
        self.status = status
        self.code = code
        self.why = why


def normal_ip(text):
    """An address as one spelling: IPv4-mapped IPv6 as IPv4, everything else as ipaddress prints it."""
    address = ipaddress.ip_address(text.strip())
    if isinstance(address, ipaddress.IPv6Address) and address.ipv4_mapped:
        return str(address.ipv4_mapped)
    return str(address)


def is_loopback(text):
    try:
        return ipaddress.ip_address(text).is_loopback
    except ValueError:
        return text == 'localhost'


def check_options(options):
    """SystemExit for options the master cannot run with (before anything binds)."""
    first, last = options.relay_ports
    if not 1024 <= first <= last <= 65535:
        raise SystemExit('--relay-ports must be FIRST-LAST within 1024..65535')
    if not PREFIX.fullmatch(options.public_prefix):
        raise SystemExit('--public-prefix must be a path like /x4mp (letters, digits, . _ ~ -)')
    if options.public_host and not udp.is_ipv4(options.public_host):
        raise SystemExit('--public-host must be a dotted IPv4 address (the clients send their UDP there)')
    http_bind = options.http_bind or options.bind
    behind = options.trust_proxy or (is_loopback(http_bind) and not is_loopback(options.bind))
    if behind and not options.public_host:
        raise SystemExit('the HTTP side is behind a proxy or on loopback: --public-host must name the address players '
                         'send UDP to (the echo and the relays)')


class Master:
    """The server's state and its API. start() binds and starts the threads; close() ends every game and stops."""
    def __init__(self, options, log=print):
        check_options(options)
        self.options = options
        self.limits = options.limits
        self.log = log
        self.lock = threading.RLock()
        self.games = {}            # game id -> games.Game (ended games stay limits.ended_keep_s)
        self.codes = {}            # display code -> game id, live games only
        self.probes = {}           # ip -> {(udp_port, tcp_port): (reached, monotonic time)}
        self.trash = []            # folders whose deletion failed (Windows: a download still has a file open)
        self.budget = files.DataBudget(self.limits.data_bytes_max)
        limits = self.limits
        self.requests = RateLimiter(limits.requests_per_s, limits.requests_burst, limits.global_requests_per_s,
                                    limits.global_requests_burst)
        self.creates = per_minute(limits.creates_per_minute)
        self.joins = per_minute(limits.joins_per_minute)
        self.lookups = per_minute(limits.codes_per_minute)
        self.probe_rates = per_minute(limits.probes_per_minute, global_count=limits.global_probes_per_minute)
        first, last = options.relay_ports
        self.pool = relays.RelayPool(first, last, options.bind, limits.relays_max)
        self.started_at = time.monotonic()
        self.stopping = threading.Event()
        self.http = None
        self.echo = None
        self.threads = []

    # ---- life cycle ----
    @property
    def games_dir(self):
        return Path(self.options.data) / 'games'

    @property
    def http_port(self):
        return self.http.server_address[1]

    @property
    def echo_port(self):
        return self.echo.port

    def start(self):
        shutil.rmtree(self.games_dir, ignore_errors=True)   # games do not survive a restart, nor do their files
        self.games_dir.mkdir(parents=True, exist_ok=True)
        self.echo = udp.EchoServer(self.options.bind, self.options.echo_port, self.limits, self.log).start()
        http_bind = self.options.http_bind or self.options.bind
        try:
            self.http = MasterHTTPServer((http_bind, self.options.http_port), self)
        except OSError:
            self.echo.close()
            raise
        serving = threading.Thread(target=self.http.serve_forever, kwargs={'poll_interval': 0.2}, name='master-http',
                                   daemon=True)
        sweeping = threading.Thread(target=self.housekeeping, name='master-sweep', daemon=True)
        self.threads = [serving, sweeping]
        for thread in self.threads:
            thread.start()
        first, last = self.options.relay_ports
        self.log(f'master: HTTP on {http_bind}:{self.http_port}' + (' (behind a proxy)' if self.options.trust_proxy else '')
                 + f', echo on UDP {self.options.bind}:{self.echo_port}, relays on {self.options.bind}:{first}-{last}, '
                 f'data {self.options.data}')
        return self

    def close(self):
        self.stopping.set()
        with self.lock:
            live = [game for game in self.games.values() if game.state != 'ended']
            for game in self.games.values():
                game.changed.notify_all()
        for game in live:
            self.finish(game, 'the master stops')
        if self.http:
            self.http.shutdown()
            self.http.server_close()
        if self.echo:
            self.echo.close()
        for thread in self.threads:
            thread.join(5)
        self.empty_trash()
        self.log('master: stopped')

    # ---- who asks ----
    def client_ip(self, handler):
        """The public IP of a request: the TCP peer, or behind a trusted proxy the address it appended last."""
        if self.options.client_ip:
            return self.options.client_ip(handler)
        peer = normal_ip(handler.client_address[0])
        if not (self.options.trust_proxy and peer in LOOPBACK_PEERS):
            return peer
        forwarded = handler.headers.get('X-Forwarded-For')
        if not forwarded:
            return peer   # a local process, not the proxy
        try:
            return normal_ip(forwarded.split(',')[-1])
        except ValueError:
            raise ApiError(400, 'bad_proxy', 'the proxy sent no client address') from None

    def credentials(self, handler, body=None, query=None):
        """The secret or ticket a request carries (32 hex digits): the X-X4MP-Auth header, which every endpoint takes
        and which wins when it is there (the package 2 client sends only the header); else the body's or the query's."""
        header = handler.headers.get(AUTH_HEADER, '').strip()
        if games.SECRET.fullmatch(header):
            return [header]
        given = []
        if body:
            given += [body.get('secret'), body.get('ticket')]
        if query:
            given += query.get('ticket', [])[:1]
        return [value for value in given if isinstance(value, str) and games.SECRET.fullmatch(value)]

    @staticmethod
    def matches(given, expected):
        """Whether one of `given` is `expected`, in constant time per value (no early exit either)."""
        found = False
        for value in given:
            if hmac.compare_digest(value.encode('ascii'), expected.encode('ascii')):
                found = True
        return found

    def find_player(self, game, given):
        """The player whose ticket is given (the game's secret stands for the host), or None."""
        found = None
        for player in game.players.values():
            if self.matches(given, player.ticket):
                found = player
        if found is None and self.matches(given, game.secret):
            found = game.players.get(1)
        return found

    def saw(self, game, player, ip):
        """An authenticated request of `player` from `ip`: the player and its game are alive (any such request counts
        as a heartbeat), and the game's relay may hear from that address."""
        now = time.monotonic()
        player.seen = now
        player.ip = ip
        game.heartbeat = now
        relay = game.relay
        if relay and udp.is_ipv4(ip) and relay.admit(ip):
            self.log(f'master: game {game.game_id}: {ip} may send to the relay (slot {player.slot})')

    def game_or_404(self, game_id, ended_too=False):
        game = self.games.get(game_id) if games.GAME_ID.fullmatch(game_id or '') else None
        if game is None or (game.state == 'ended' and not ended_too):
            raise ApiError(404, 'not_found', 'no such game (it may have ended)')
        return game

    # ---- the endpoints ----
    def api_health(self, handler, route, query):
        with self.lock:
            live = sum(1 for game in self.games.values() if game.state != 'ended')
        return {'version': API_VERSION, 'games': live, 'relays': self.pool.running(),
                'uptime_s': int(time.monotonic() - self.started_at), 'echo_port': self.echo_port}

    def release_query(self, query):
        values = query.get('release')
        if not values:
            return None
        if not games.RELEASE.fullmatch(values[0]):
            raise ApiError(400, 'bad_request', 'release: the release string of release.json')
        return values[0]

    def public_games(self, release):
        now = time.time()
        with self.lock:
            listed = [game for game in self.games.values() if game.public and game.state != 'ended']
            listed.sort(key=lambda game: game.created, reverse=True)
            return [games.summary(game, release, now) for game in listed[:LIST_MAX]]

    def api_list_games(self, handler, route, query):
        return {'games': self.public_games(self.release_query(query))}

    def api_create(self, handler, route, query):
        ip = handler.ip
        if not self.creates.allow(ip):
            raise ApiError(429, 'rate_limited', 'too many new games from your address; wait a minute')
        fields = self.parse(games.parse_new_game, handler.read_json())
        now = time.monotonic()
        with self.lock:
            live = [game for game in self.games.values() if game.state != 'ended']
            if len(live) >= self.limits.games_max:
                raise ApiError(503, 'server_full', 'the server holds as many games as it can; try again later')
            if sum(1 for game in live if game.host_ip == ip) >= self.limits.games_per_ip:
                raise ApiError(429, 'too_many_games', f'your address hosts {self.limits.games_per_ip} games already')
            game_id = secrets.token_hex(8)
            while game_id in self.games:
                game_id = secrets.token_hex(8)
            code = codes.new_code(self.codes)
            game = games.Game(game_id=game_id, code=code, secret=secrets.token_hex(16), host_ip=ip, created=time.time(),
                              heartbeat=now, changed=threading.Condition(self.lock), **fields)
            host = games.Player(slot=1, ticket=secrets.token_hex(16), host=True, ip=ip, joined=time.time(), seen=now)
            game.players[1] = host
            self.games[game_id] = game
            self.codes[code] = game_id
        self.log(f'master: game {game_id} created by {ip}: {"public" if game.public else "private"}, '
                 f'{game.players_max} slots' + (f' (open {",".join(map(str, game.open_slots))})' if game.open_slots else '')
                 + f', want {game.want}, release {game.release}')
        return {'game_id': game_id, 'code': code, 'secret': game.secret, 'host_ticket': host.ticket, 'slot': 1}

    def api_lookup(self, handler, route, query):
        ip = handler.ip
        if not self.lookups.allow(ip):
            raise ApiError(429, 'rate_limited', 'too many code lookups from your address; wait a minute')
        code = codes.normalize_code(route['code'])
        if code is None:
            raise ApiError(400, 'bad_code', 'a join code is 8 letters and digits, like K7QX-3FNA')
        release = self.release_query(query)
        with self.lock:
            game = self.games.get(self.codes.get(code, ''))
            if game is None or game.state == 'ended':
                raise ApiError(404, 'not_found', 'no game has this code (it may have ended)')
            answer = games.summary(game, release, time.time())
            answer['public'] = game.public
            host = game.players.get(1)
            host_ip = host.ip if host else game.host_ip
            if game.lan and ip == host_ip:   # the same site: no LAN address leaves it
                answer['lan'] = dict(game.lan)
        return answer

    def api_join(self, handler, route, query):
        ip = handler.ip
        if not self.joins.allow(ip):
            raise ApiError(429, 'rate_limited', 'too many joins from your address; wait a minute')
        body = handler.read_json()
        release = body.get('release')
        if not isinstance(release, str) or not games.RELEASE.fullmatch(release):
            raise ApiError(400, 'bad_request', 'release: the release string of release.json')
        given_code = body.get('code')
        with self.lock:
            game = self.game_or_404(route['game'])
            if not game.public:
                typed = codes.normalize_code(given_code) if isinstance(given_code, str) else None
                if typed is None or not hmac.compare_digest(typed, game.code):
                    raise ApiError(403, 'forbidden', 'a private game is joined with its code')
            if release != game.release:
                raise ApiError(409, 'incompatible', f'the game runs release {game.release}, you have {release}')
            slot = game.free_slot()
            if slot is None:
                if game.state == 'started' and not game.open_slots:
                    raise ApiError(409, 'started', 'the game has started and takes no more players')
                raise ApiError(409, 'full', 'every slot of the game is taken')
            now = time.monotonic()
            player = games.Player(slot=slot, ticket=secrets.token_hex(16), host=False, ip=ip, joined=time.time(), seen=now)
            game.players[slot] = player
            game.touch()
            self.saw(game, player, ip)
            listed = {name: {'size': stored.size, 'sha256': stored.sha256} for name, stored in game.files.items()}
            state = game.state
        self.log(f'master: game {game.game_id}: {ip} joined slot {slot} ({state})')
        return {'ticket': player.ticket, 'slot': slot, 'files': listed}

    def api_upload(self, handler, route, query):
        name = route['name']
        cap = files.cap_for(name)
        if cap is None:
            raise ApiError(400, 'bad_name', 'the master stores session.json, session-save.xml.gz, play-recipe.json and '
                                            'the join files j<T>.x4join, j<T>.x4join.gz, j<T>.xml.gz')
        length = handler.body_length(required=True)
        if length > cap:
            raise ApiError(413, 'too_large', f'{name} may have at most {cap >> 20} MiB')
        expect_sha = handler.headers.get(SHA_HEADER, '').strip().lower()
        if not files.SHA256.fullmatch(expect_sha):
            raise ApiError(400, 'bad_sha256', f'the header {SHA_HEADER} must carry the SHA-256 of the bytes')
        given = self.credentials(handler)
        with self.lock:
            game = self.game_or_404(route['game'])
            folder, final_name = self.upload_target(game, name, given, expect_sha, handler.ip)
            if name in game.uploading:
                raise ApiError(409, 'busy', f'{name} is being uploaded already')
            if not self.budget.reserve(length):
                raise ApiError(503, 'server_full', 'the server has no room for more files now')
            game.uploading.add(name)
        try:
            path = files.receive(handler.rfile, length, folder, expect_sha, final_name)
            handler.body_left = 0
        except files.UploadError as error:
            handler.body_left = 0
            self.budget.release(length)
            with self.lock:
                game.uploading.discard(name)
            raise ApiError(error.status, error.code, error.why) from None
        joined_file = self.store(game, name, path, length, expect_sha)
        if joined_file:
            game_relay = game.relay
            if game_relay and game_relay.dropin:
                if name.endswith('.x4join.gz'):
                    path = self.unpack_join(game, game_relay, path)
                game_relay.dropin.stored(path)   # once a join's two files are here: verified, then served
            with self.lock:
                game.touch()   # the saver's send_join list may be shorter now
        return {'name': name, 'size': length, 'sha256': expect_sha}

    def upload_target(self, game, name, given, expect_sha, ip):
        """Where an upload of `name` goes and whether this request may make it; ApiError otherwise. A session file is the
        host's (the secret), before the start. A join file is the saver's: the relay must have asked a saver for that
        tick, and the request carries that saver's ticket (or the game's secret)."""
        tick = files.join_tick(name)
        if tick is None:
            if not self.matches(given, game.secret):
                raise ApiError(403, 'forbidden', 'the session files are uploaded with the game\'s secret')
            if game.state != 'lobby':
                raise ApiError(409, 'state', 'the session files are fixed once the game has started')
            return self.games_dir / game.game_id / 'files', None
        relay = game.relay
        if relay is None or relay.dropin is None:
            raise ApiError(409, 'state', 'this game takes no join files (a relay-mode game with open slots does)')
        pending = relay.dropin.pending()
        if tick not in pending:
            raise ApiError(409, 'not_asked', f'the relay has not asked for the join files of tick {tick}')
        _slot, saver, package_sha = pending[tick]
        player = self.find_player(game, given)
        if player is None or (player.slot != saver and not self.matches(given, game.secret)):
            raise ApiError(403, 'forbidden', f'the join files of tick {tick} are uploaded by the saver (slot {saver})')
        if name.endswith('.x4join') and expect_sha != package_sha:
            raise ApiError(400, 'bad_sha256', 'this is not the package the relay announced')
        self.saw(game, player, ip)
        return relay.dropin.run_dir / 'join', name

    def unpack_join(self, game, relay, path):
        """A saver's compressed package (j<T>.x4join.gz, about a sixth of the raw one): unpacked beside it as
        j<T>.x4join (counted like an upload), checked against the package the relay announced, and offered to the
        joiner compressed (joinfiles.fetch_package takes it first). Returns the raw package's path; ApiError when the
        bytes are not that package (the saver then sends the raw one)."""
        tick = files.join_tick(path.name)
        asked = relay.dropin.pending().get(tick)
        if asked is None:
            raise ApiError(409, 'not_asked', f'the relay no longer waits for the join files of tick {tick}')
        raw = path.with_name(f'j{tick}.x4join')
        try:
            size, sha = files.gunzip(path, raw, files.JOIN_FILE_MAX)
        except files.UploadError as error:
            raise ApiError(error.status, error.code, error.why) from None
        if sha != asked[2]:
            raw.unlink(missing_ok=True)
            raise ApiError(400, 'bad_sha256', 'the unpacked package is not the one the relay announced')
        if not self.budget.reserve(size):
            raw.unlink(missing_ok=True)
            raise ApiError(503, 'server_full', 'the server has no room for the unpacked package now')
        self.store(game, raw.name, raw, size, sha)
        relay.dropin.offer_packed(path)
        return raw

    def store(self, game, name, path, size, sha):
        """A finished upload: keep it in the game (True for a join file), or drop it when the game ended meanwhile."""
        join_file = files.join_tick(name) is not None
        replaced = None
        with self.lock:
            game.uploading.discard(name)
            alive = self.games.get(game.game_id) is game and game.state != 'ended'
            if alive:
                kept = game.join_sizes if join_file else game.files
                previous = kept.get(name)
                kept[name] = games.StoredFile(path=path, size=size, sha256=sha)
                if previous:
                    self.budget.release(previous.size)
                    if not join_file:
                        replaced = previous.path   # a join file is replaced in place, under its own name
                session_bytes = sum(stored.size for stored in game.files.values())
                join_bytes = sum(stored.size for stored in game.join_sizes.values())
                game.bytes_stored = session_bytes + join_bytes
        if not alive:
            self.budget.release(size)
            Path(path).unlink(missing_ok=True)
            raise ApiError(404, 'not_found', 'the game ended during the upload')
        if replaced:
            self.delete_later(replaced)
        self.log(f'master: game {game.game_id}: {name} stored ({size >> 10} KiB)')
        return join_file

    def api_download(self, handler, route, query):
        name = route['name']
        given = self.credentials(handler)
        with self.lock:
            game = self.game_or_404(route['game'])
            player = self.find_player(game, given)
            if player is None:
                raise ApiError(403, 'forbidden', 'a file is downloaded with a ticket of the game')
            self.saw(game, player, handler.ip)
            found = self.downloadable(game, name)
        if found is None:
            raise ApiError(404, 'not_found', f'{name} is not offered (not uploaded, or not verified yet)')
        path, size, sha = found
        try:
            wanted = files.parse_range(handler.headers.get('Range'), size)
        except ValueError:
            raise ApiError(416, 'bad_range', f'the file has {size} bytes') from None
        try:
            source = open(path, 'rb')
        except OSError:
            raise ApiError(404, 'not_found', f'{name} is gone') from None
        with source:
            handler.send_file(source, size, sha, wanted)
        return None

    def downloadable(self, game, name):
        """(path, size, sha) of a file a player may fetch: a session file, or a join file once it was verified."""
        stored = game.files.get(name)
        if stored is not None:
            return stored.path, stored.size, stored.sha256
        relay = game.relay
        if files.join_tick(name) is not None and relay and relay.dropin:
            with relay.dropin.service.lock:
                offered = relay.dropin.service.offered.get(name)
            if offered:
                return offered
        return None

    def api_lobby(self, handler, route, query):
        given = self.credentials(handler, query=query)
        since = self.int_query(query, 'since', 0)
        wait = min(self.limits.lobby_wait_s, self.int_query(query, 'wait', int(self.limits.lobby_wait_s)))
        deadline = time.monotonic() + max(0, wait)
        with self.lock:
            game = self.game_or_404(route['game'], ended_too=True)
            player = self.find_player(game, given)
            if player is None:
                raise ApiError(403, 'forbidden', 'the lobby is read with a ticket of the game')
            self.saw(game, player, handler.ip)
            while game.stamp <= since and game.state != 'ended' and not self.stopping.is_set():
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                game.changed.wait(left)
            if game.players.get(player.slot) is not player:
                raise ApiError(403, 'forbidden', 'you are no longer in this game')
            relay = game.relay
            pending = relay.dropin.pending() if relay and relay.dropin else None
            return games.lobby_answer(game, player, pending)

    @staticmethod
    def int_query(query, key, default):
        values = query.get(key)
        if not values:
            return default
        if not values[0].isdigit() or len(values[0]) > 12:
            raise ApiError(400, 'bad_request', f'{key}: a whole number')
        return int(values[0])

    def api_ready(self, handler, route, query):
        body = handler.read_json()
        ready = body.get('ready', True)
        if not isinstance(ready, bool):
            raise ApiError(400, 'bad_request', 'ready: true or false')
        with self.lock:
            game = self.game_or_404(route['game'])
            player = self.find_player(game, self.credentials(handler, body))
            if player is None:
                raise ApiError(403, 'forbidden', 'ready is said with a ticket of the game')
            self.saw(game, player, handler.ip)
            player.ready = ready
            player.ready_at = time.time()
            game.touch()
            starts = self.auto_start_due(game)
        if starts:
            relay_host = self.relay_host(handler)
            threading.Thread(target=self.auto_start, args=(game, relay_host), name='master-autostart', daemon=True).start()
        return {}

    def auto_start_due(self, game):
        """Every slot is taken and every player of the common load is ready: the game starts without the host's click."""
        if game.state != 'lobby' or game.starting:
            return False
        if len(game.players) < game.players_max:
            return False
        return all(game.players[slot].ready for slot in game.present_slots)

    def auto_start(self, game, relay_host):
        try:
            with self.lock:
                if not self.auto_start_due(game):
                    return
                host = game.players.get(1)
                mode = self.decide(game, host.ip if host else game.host_ip)
                game.starting = True
            self.begin(game, mode, relay_host)
        except ApiError as error:
            self.log(f'master: game {game.game_id}: no start when every slot was ready ({error.code}: {error.why})')
        except Exception as error:
            with self.lock:
                game.starting = False
            self.log(f'master: game {game.game_id}: the automatic start failed ({error.__class__.__name__})')

    def api_start(self, handler, route, query):
        body = handler.read_json()
        given = self.credentials(handler, body)
        with self.lock:
            game = self.game_or_404(route['game'])
            if not self.matches(given, game.secret):
                raise ApiError(403, 'forbidden', 'the game is started with its secret')
            if game.state != 'lobby' or game.starting:
                raise ApiError(409, 'state', 'the game has started already')
            host = game.players.get(1)
            if host is None:
                raise ApiError(409, 'state', 'the host has left the game')
            self.saw(game, host, handler.ip)
            if not host.ready:
                host.ready = True   # the host's click says it is ready
                host.ready_at = time.time()
            missing = [slot for slot in game.present_slots if not (slot in game.players and game.players[slot].ready)]
            if missing:
                raise ApiError(409, 'not_ready', 'every slot of the common load needs a ready player; not ready: slot '
                                                 + ', '.join(map(str, missing)))
            mode = self.decide(game, handler.ip)
            game.starting = True
        start = self.begin(game, mode, self.relay_host(handler))
        return {'mode': start['mode'], 'load_at': start['load_at']}

    def relay_host(self, handler):
        """The address players send to a master relay: --public-host, else the address this request reached."""
        if self.options.public_host:
            return self.options.public_host
        return normal_ip(handler.connection.getsockname()[0])

    def probe_passed(self, ip, ports):
        record = self.probes.get(ip, {}).get((ports['udp_port'], ports['tcp_port']))
        if not record:
            return False
        reached, when = record
        return reached and time.monotonic() - when <= self.limits.probe_valid_s

    def decide(self, game, host_ip):
        """direct or relay (docs/NETPLAY.md, "Starting a game"); ApiError when the game can have neither."""
        direct_ok = bool(game.direct) and self.probe_passed(host_ip, game.direct)
        if game.want == 'direct' and not direct_ok:
            raise ApiError(409, 'unreachable', 'the server\'s probe did not reach your direct ports (or is older than an '
                                               'hour): probe again, or host with want relay')
        if game.want != 'relay' and direct_ok:
            return 'direct'
        if self.options.relay_releases and game.release not in self.options.relay_releases:
            raise ApiError(409, 'incompatible', 'the server\'s relay runs release ' + ', '.join(self.options.relay_releases)
                                                + f', the game {game.release}: host with reachable direct ports instead')
        return 'relay'

    def begin(self, game, mode, relay_host):
        """Start a decided game: a relay of the pool for relay mode, the start record for everyone, the load second."""
        token = secrets.token_hex(16)
        relay = None
        try:
            if mode == 'relay':
                if not udp.is_ipv4(relay_host):
                    raise ApiError(503, 'no_public_host', 'the server does not know its IPv4 address (--public-host)')
                relay = self.start_relay(game, token)
                relay_info = {'host': relay_host, 'port': relay.port, 'token': token}
            else:
                with self.lock:
                    host = game.players.get(1)
                    relay_info = {'host': host.ip if host else game.host_ip, 'port': game.direct['udp_port'], 'token': token}
        except ApiError:
            with self.lock:
                game.starting = False
            raise
        with self.lock:
            game.starting = False
            if self.games.get(game.game_id) is not game or game.state != 'lobby':
                alive = False
            else:
                alive = True
                now = time.time()
                last_ready = max(player.ready_at for player in game.players.values())
                load_at = int(max(now + self.limits.start_wait_s, last_ready + self.limits.load_wait_s))
                game.start = {'load_at': load_at, 'relay': relay_info, 'mode': mode}
                game.state = 'started'
                game.mode = mode
                game.relay = relay
                game.touch()
                for player in game.players.values():
                    if relay and udp.is_ipv4(player.ip):
                        relay.admit(player.ip)
        if not alive:
            if relay:
                self.stop_relay(game.game_id, relay)
            raise ApiError(404, 'not_found', 'the game ended while it was starting')
        self.log(f'master: game {game.game_id} started: {mode}, {len(game.players)} players, loads at {load_at}'
                 + (f', relay port {relay.port}' if relay else ''))
        return game.start

    def start_relay(self, game, token):
        with self.lock:
            stored = game.files.get('play-recipe.json')
            addresses = sorted({player.ip for player in game.players.values() if udp.is_ipv4(player.ip)})
        if stored is None:
            raise ApiError(409, 'no_recipe', 'relay mode needs the session\'s play-recipe.json uploaded')
        try:
            data = Path(stored.path).read_bytes()
            settings = relays.recipe_settings(data, game.players_max, game.open_slots)
        except OSError:
            raise ApiError(409, 'no_recipe', 'the uploaded play-recipe.json cannot be read') from None
        except relays.RecipeError as error:
            raise ApiError(409, 'bad_recipe', str(error)) from None
        if not addresses:
            raise ApiError(409, 'ipv4_only', 'the relay needs the players\' IPv4 addresses')
        port = self.pool.take()
        if port is None:
            raise ApiError(503, 'relays_full', 'every relay of the server is in use; host with direct ports or try later')
        game_id = game.game_id

        def relay_log(text):
            self.log(f'master: game {game_id}: ' + text.removeprefix('serve: '))
        relay = relays.RelayGame(settings, port, token, self.games_dir / game_id / 'relay', self.options.bind, addresses,
                                 relay_log, self.limits)
        try:
            relay.start()
        except (OSError, relays.RelayError) as error:
            self.pool.give_back(port)
            self.log(f'master: game {game_id}: the relay did not start ({error})')
            raise ApiError(503, 'relay_failed', 'the server\'s relay did not start; try again') from None
        return relay

    def stop_relay(self, game_id, relay):
        step = relay.step()
        relay.stop()
        self.pool.give_back(relay.port)
        try:
            status = json.loads(relay.status_file.read_text())
        except (OSError, ValueError):
            status = {}
        self.log(f'master: game {game_id}: relay port {relay.port} stopped at step {step}, desync {status.get("desync")}, '
                 f'mismatches {status.get("mismatches")}')

    def api_heartbeat(self, handler, route, query):
        body = handler.read_json()
        tick, state = self.parse(games.parse_heartbeat, body)
        given = self.credentials(handler, body)
        with self.lock:
            game = self.game_or_404(route['game'])
            player = self.find_player(game, given)
            if player is None and not self.matches(given, game.secret):
                raise ApiError(403, 'forbidden', 'a heartbeat carries the game\'s secret or a ticket')
            game.heartbeat = time.monotonic()
            if player is not None:
                self.saw(game, player, handler.ip)
                player.tick = tick
                player.state = state
        return {}

    def api_leave(self, handler, route, query):
        body = handler.read_json()
        with self.lock:
            game = self.game_or_404(route['game'])
            player = self.find_player(game, self.credentials(handler, body))
            if player is None:
                raise ApiError(403, 'forbidden', 'leave carries a ticket of the game')
            host_left_lobby = player.host and game.state == 'lobby'
            if not host_left_lobby:
                del game.players[player.slot]
                if game.state == 'started' and not self.unplayed(game, player.slot):
                    game.gone_slots.add(player.slot)
                game.touch()
        if host_left_lobby:
            self.finish(game, 'the host left')
        else:
            self.log(f'master: game {game.game_id}: slot {player.slot} left')
        return {}

    def api_delete(self, handler, route, query):
        body = handler.read_json()
        with self.lock:
            game = self.game_or_404(route['game'])
            if not self.matches(self.credentials(handler, body), game.secret):
                raise ApiError(403, 'forbidden', 'a game is deleted with its secret')
        self.finish(game, 'deleted by its host')
        return {}

    def api_probe(self, handler, route, query):
        ip = handler.ip
        if not self.probe_rates.allow(ip):
            raise ApiError(429, 'rate_limited', f'at most {self.limits.probes_per_minute:g} probes a minute')
        body = handler.read_json()
        if any(key in body for key in PROBE_TARGET_FIELDS):
            raise ApiError(400, 'bad_request', 'the probe goes to your own address only; it takes no target')
        ports = []
        for key in ('udp_port', 'tcp_port'):
            port = body.get(key)
            if not games.is_int(port) or not udp.PORT_MIN <= port <= udp.PORT_MAX:
                raise ApiError(400, 'bad_request', f'{key}: a port {udp.PORT_MIN}..{udp.PORT_MAX}')
            ports.append(port)
        nonce = body.get('nonce')
        if not isinstance(nonce, str) or not udp.PROBE_NONCE.fullmatch(nonce):
            raise ApiError(400, 'bad_request', 'nonce: 16 to 64 hex digits')
        if not udp.is_ipv4(ip):
            raise ApiError(400, 'ipv4_only', 'the probe and the relays need IPv4; this request came over IPv6')
        reached = udp.probe(ip, ports[0], ports[1], nonce, self.limits.probe_timeout_s)
        with self.lock:
            self.probes.setdefault(ip, {})[(ports[0], ports[1])] = (reached, time.monotonic())
        self.log(f'master: probe of {ip} udp {ports[0]} tcp {ports[1]}: tcp {"reached" if reached else "not reached"}')
        return {'tcp': reached, 'ip': ip}

    def api_page(self, handler, route, query):
        health = self.api_health(handler, route, query)
        text = page.render(self.public_games(None), health, self.options.public_prefix)
        handler.send_page(text)
        return None

    @staticmethod
    def parse(parser, body):
        try:
            return parser(body)
        except games.BadRequest as error:
            raise ApiError(400, 'bad_request', str(error)) from None

    # ---- housekeeping ----
    def housekeeping(self):
        while not self.stopping.wait(1.0):
            try:
                self.sweep()
            except Exception as error:   # one bad game must not stop the sweeps of all the others
                self.log(f'master: sweep failed: {error.__class__.__name__}: {error}')

    def sweep(self):
        """Once a second: games without heartbeats end, idle lobby players leave, relays are followed and ended, ended
        games are forgotten, failed deletions are retried."""
        now = time.monotonic()
        ending = []
        followed = []
        with self.lock:
            for game in list(self.games.values()):
                if game.state == 'ended':
                    if now - game.ended_at > self.limits.ended_keep_s:
                        del self.games[game.game_id]
                    continue
                if now - game.heartbeat > self.limits.heartbeat_s:
                    ending.append((game, f'no heartbeat for {self.limits.heartbeat_s:g} s'))
                    continue
                if game.state == 'lobby':
                    host = game.players.get(1)
                    if host is None or now - host.seen > self.limits.player_idle_s:
                        ending.append((game, f'the host sent nothing for {self.limits.player_idle_s:g} s'))
                        continue
                    self.drop_idle_players(game, now)
                elif game.open_slots and game.relay:
                    self.drop_idle_players(game, now)
                if game.relay:
                    followed.append(game)
            self.prune_probes(now)
        for game in followed:
            relay = game.relay
            if relay is None:
                continue
            changed = relay.poll()
            reason = relay.over(time.time())
            if changed:
                with self.lock:
                    game.touch()
            if reason:
                ending.append((game, reason))
        for game, reason in ending:
            self.finish(game, reason)
        self.empty_trash()

    @staticmethod
    def unplayed(game, slot):
        """An open slot of a running relay-mode game that the relay has not taken for a join (none asked, or the join
        failed): its player leaving frees it for the next one."""
        relay = game.relay
        return (slot in game.open_slots and relay is not None and relay.dropin is not None
                and not relay.dropin.played(slot))

    def drop_idle_players(self, game, now):
        """Players that sent nothing for player_idle_s leave: in the lobby any but the host, in a running game a drop-in
        player whose join the relay never took (its slot is free again)."""
        idle = [slot for slot, player in game.players.items()
                if not player.host and now - player.seen > self.limits.player_idle_s
                and (game.state == 'lobby' or self.unplayed(game, slot))]
        for slot in idle:
            del game.players[slot]
            self.log(f'master: game {game.game_id}: slot {slot} left (no request for {self.limits.player_idle_s:g} s)')
        if idle:
            game.touch()

    def prune_probes(self, now):
        for ip in list(self.probes):
            fresh = {ports: record for ports, record in self.probes[ip].items()
                     if now - record[1] <= self.limits.probe_valid_s}
            if fresh:
                self.probes[ip] = fresh
            else:
                del self.probes[ip]

    def finish(self, game, reason):
        """The game is over: its code is free, its polls see 'ended', its relay stops, its files are deleted."""
        with self.lock:
            if game.state == 'ended':
                return
            game.state = 'ended'
            game.ended_at = time.monotonic()
            game.end_reason = reason
            if self.codes.get(game.code) == game.game_id:
                del self.codes[game.code]
            relay = game.relay
            game.relay = None
            stored = game.bytes_stored
            game.bytes_stored = 0
            game.files = {}
            game.join_sizes = {}
            game.touch()
        if relay:
            self.stop_relay(game.game_id, relay)
        self.budget.release(stored)
        self.delete_later(self.games_dir / game.game_id)
        self.log(f'master: game {game.game_id} over ({reason})')

    def delete_later(self, path):
        with self.lock:
            self.trash.append(Path(path))
        self.empty_trash()

    def empty_trash(self):
        with self.lock:
            waiting = list(self.trash)
            self.trash = []
        kept = []
        for path in waiting:
            if path.is_dir():
                shutil.rmtree(path, ignore_errors=True)
            else:
                try:
                    path.unlink(missing_ok=True)
                except OSError:
                    pass
            if path.exists():
                kept.append(path)
        if kept:
            with self.lock:
                self.trash.extend(kept)


ROUTES = (
    ('GET', re.compile(r'/api/v1/health'), 'health'),
    ('GET', re.compile(r'/api/v1/games'), 'list_games'),
    ('POST', re.compile(r'/api/v1/games'), 'create'),
    ('GET', re.compile(r'/api/v1/codes/(?P<code>[^/]+)'), 'lookup'),
    ('PUT', re.compile(r'/api/v1/games/(?P<game>[^/]+)/files/(?P<name>[^/]+)'), 'upload'),
    ('GET', re.compile(r'/api/v1/games/(?P<game>[^/]+)/files/(?P<name>[^/]+)'), 'download'),
    ('POST', re.compile(r'/api/v1/games/(?P<game>[^/]+)/join'), 'join'),
    ('GET', re.compile(r'/api/v1/games/(?P<game>[^/]+)/lobby'), 'lobby'),
    ('POST', re.compile(r'/api/v1/games/(?P<game>[^/]+)/ready'), 'ready'),
    ('POST', re.compile(r'/api/v1/games/(?P<game>[^/]+)/start'), 'start'),
    ('POST', re.compile(r'/api/v1/games/(?P<game>[^/]+)/heartbeat'), 'heartbeat'),
    ('POST', re.compile(r'/api/v1/games/(?P<game>[^/]+)/leave'), 'leave'),
    ('DELETE', re.compile(r'/api/v1/games/(?P<game>[^/]+)'), 'delete'),
    ('POST', re.compile(r'/api/v1/probe'), 'probe'),
    ('GET', re.compile(r'/'), 'page'),
)


def route_for(method, path):
    """(endpoint name, match) for a request; ApiError 404 for an unknown path, 400 for a known path's wrong method."""
    known = False
    for route_method, pattern, name in ROUTES:
        match = pattern.fullmatch(path)
        if match is None:
            continue
        known = True
        if route_method == method:
            return name, match
    if known:
        raise ApiError(400, 'bad_method', f'{method} is not an answer of {path}')
    raise ApiError(404, 'not_found', 'no such path (the API is /api/v1/...)')


def busy_answer(request):
    """The answer to a connection over the caps, written at once (the accept thread must not wait on it). Then the
    sending side is closed: the socket goes to the closer, which reads the request away before closing. A socket closed
    with unread data is reset on Windows, and the client then sees "connection aborted" instead of this answer (the
    connection-cap test under load, 23 Nov)."""
    body = json.dumps({'ok': False, 'error': 'server_full', 'why': 'too many connections; try again shortly'}).encode()
    head = (f'HTTP/1.0 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: {len(body)}\r\n'
            'Retry-After: 5\r\nConnection: close\r\n\r\n').encode('ascii')
    try:
        request.settimeout(1.0)
        request.sendall(head + body)
        request.shutdown(socket.SHUT_WR)
    except OSError:
        pass


class Closer:
    """One thread that closes answered over-cap connections gracefully: it reads what the client sent (at most 64 KiB,
    at most 0.5 s per socket) and closes. A full queue closes at once, so a flood never holds threads or memory."""
    def __init__(self, size=256):
        import queue
        self.queue = queue.Queue(maxsize=size)
        threading.Thread(target=self.run, name='master-closer', daemon=True).start()

    def hand(self, sock):
        try:
            self.queue.put_nowait(sock)
        except Exception:
            close_quietly(sock)

    def run(self):
        while True:
            sock = self.queue.get()
            deadline = time.monotonic() + 0.5
            read = 0
            try:
                while read < 65536 and time.monotonic() < deadline:
                    sock.settimeout(max(0.01, deadline - time.monotonic()))
                    data = sock.recv(4096)
                    if not data:
                        break
                    read += len(data)
            except OSError:
                pass
            close_quietly(sock)


def close_quietly(sock):
    try:
        sock.close()
    except OSError:
        pass


class MasterHTTPServer(http.server.ThreadingHTTPServer):
    """ThreadingHTTPServer with caps on open connections, in all and per peer address (behind a trusted proxy every
    connection comes from the proxy, which then counts toward the total only)."""
    daemon_threads = True
    allow_reuse_address = os.name != 'nt'   # on Windows SO_REUSEADDR would let another process take the port
    request_queue_size = 128

    def __init__(self, address, master):
        self.master = master
        self.count_lock = threading.Lock()
        self.connections = 0
        self.per_peer = {}
        self.closer = Closer()
        self.answered_busy = set()   # over-cap sockets the closer takes instead of socketserver's immediate close
        if ':' in address[0]:
            self.address_family = socket.AF_INET6
        super().__init__(address, Handler)

    def verify_request(self, request, client_address):
        peer = client_address[0]
        limits = self.master.limits
        trusted = self.master.options.trust_proxy and peer in LOOPBACK_PEERS
        with self.count_lock:
            over_total = self.connections >= limits.connections_max
            over_peer = not trusted and self.per_peer.get(peer, 0) >= limits.connections_per_ip
            if not over_total and not over_peer:
                self.connections += 1
                self.per_peer[peer] = self.per_peer.get(peer, 0) + 1
        if over_total or over_peer:
            busy_answer(request)
            with self.count_lock:
                self.answered_busy.add(request)
            return False
        return True

    def shutdown_request(self, request):
        with self.count_lock:
            busy = request in self.answered_busy
            self.answered_busy.discard(request)
        if busy:
            self.closer.hand(request)
        else:
            super().shutdown_request(request)

    def release(self, peer):
        with self.count_lock:
            self.connections -= 1
            left = self.per_peer.get(peer, 1) - 1
            if left > 0:
                self.per_peer[peer] = left
            else:
                self.per_peer.pop(peer, None)

    def process_request(self, request, client_address):
        try:
            super().process_request(request, client_address)
        except Exception:
            self.release(client_address[0])
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.release(client_address[0])


class Handler(http.server.BaseHTTPRequestHandler):
    """One request (HTTP/1.0: one request per connection, then closed). The endpoints are Master.api_<name>."""
    server_version = 'x4mp-master/1'
    sys_version = ''

    def setup(self):
        self.timeout = self.server.master.limits.socket_timeout_s
        super().setup()
        self.body_left = 0
        self.responded = False
        self.ip = None

    def log_message(self, format, *args):
        """No access log: the master logs one line per notable event (server.Master), never a request's secrets."""

    def do_GET(self):
        self.dispatch('GET')

    def do_POST(self):
        self.dispatch('POST')

    def do_PUT(self):
        self.dispatch('PUT')

    def do_DELETE(self):
        self.dispatch('DELETE')

    def dispatch(self, method):
        master = self.server.master
        try:
            self.body_left = self.declared_length()
            self.ip = master.client_ip(self)
            if not master.requests.allow(self.ip):
                raise ApiError(429, 'rate_limited', 'too many requests from your address; slow down')
            parts = urlsplit(self.path)
            name, match = route_for(method, parts.path)
            try:
                query = parse_qs(parts.query, keep_blank_values=True, max_num_fields=16)
            except ValueError:
                raise ApiError(400, 'bad_request', 'too many query fields') from None
            answer = getattr(master, 'api_' + name)(self, match, query)
            if answer is not None:
                self.send_json(200, {'ok': True, **answer})
        except ApiError as error:
            self.send_error_json(error)
        except Exception as error:
            master.log(f'master: {method} {urlsplit(self.path).path[:80]} failed: {error.__class__.__name__}')
            self.send_error_json(ApiError(500, 'internal', 'the server failed on this request'))

    def declared_length(self):
        text = self.headers.get('Content-Length')
        if text is None:
            return 0
        text = text.strip()
        if not text.isdigit() or len(text) > 15:
            return 0
        return int(text)

    def body_length(self, required=False):
        """The request body's length (Content-Length; chunked bodies are not taken)."""
        if 'chunked' in self.headers.get('Transfer-Encoding', '').lower():
            raise ApiError(400, 'bad_request', 'send a Content-Length (chunked bodies are not taken)')
        text = self.headers.get('Content-Length')
        if text is None:
            if required:
                raise ApiError(400, 'bad_request', 'send a Content-Length')
            return 0
        text = text.strip()
        if not text.isdigit() or len(text) > 15:
            raise ApiError(400, 'bad_request', 'Content-Length: a byte count')
        return int(text)

    def read_json(self):
        """The JSON object of the request body ({} for none), at most MasterLimits.json_bytes_max bytes."""
        length = self.body_length()
        if length > self.server.master.limits.json_bytes_max:
            raise ApiError(413, 'too_large', f'a request has at most {self.server.master.limits.json_bytes_max >> 10} KiB')
        if length == 0:
            return {}
        data = b''
        try:
            while len(data) < length:
                block = self.rfile.read(length - len(data))
                if not block:
                    break
                data += block
        except OSError:
            pass
        self.body_left = 0
        if len(data) < length:
            raise ApiError(400, 'bad_request', 'the request body ended early')
        try:
            body = json.loads(data.decode('utf-8'))
        except (UnicodeDecodeError, ValueError, RecursionError):
            raise ApiError(400, 'bad_json', 'the body is not JSON') from None
        if not isinstance(body, dict):
            raise ApiError(400, 'bad_json', 'the body is not a JSON object')
        return body

    def send_common(self, status, content_type, length, extra=None):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(length))
        for key, value in COMMON_HEADERS.items():
            self.send_header(key, value)
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.responded = True

    def send_json(self, status, payload, extra=None):
        data = json.dumps(payload, separators=(',', ':')).encode('utf-8')
        try:
            self.send_common(status, 'application/json; charset=utf-8', len(data), extra)
            self.wfile.write(data)
        except OSError:
            pass

    def send_error_json(self, error):
        if self.responded:
            return
        extra = {'Retry-After': '10'} if error.status in (429, 503) else {}
        self.send_json(error.status, {'ok': False, 'error': error.code, 'why': error.why}, extra)
        if self.body_left:
            self.drain()

    def drain(self):
        """The request body was not read (refused before it): half-close and read what comes for a moment, so the
        answer is not overtaken by a reset (Windows resets a connection closed with unread data)."""
        try:
            self.wfile.flush()
            self.connection.shutdown(socket.SHUT_WR)
            deadline = time.monotonic() + DRAIN_S
            while True:
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                self.connection.settimeout(left)
                if not self.connection.recv(65536):
                    break
        except OSError:
            pass
        self.body_left = 0
        self.close_connection = True

    def send_page(self, text):
        data = text.encode('utf-8')
        try:
            self.send_common(200, 'text/html; charset=utf-8', len(data), PAGE_HEADERS)
            self.wfile.write(data)
        except OSError:
            pass

    def send_file(self, source, size, sha, wanted):
        """A file's bytes, whole (200) or the asked range (206)."""
        extra = {'Accept-Ranges': 'bytes', SHA_HEADER: sha}
        if wanted is None:
            first = 0
            last = size - 1
            status = 200
        else:
            first, last = wanted
            status = 206
            extra['Content-Range'] = f'bytes {first}-{last}/{size}'
        length = max(0, last - first + 1)
        try:
            self.send_common(status, 'application/octet-stream', length, extra)
            source.seek(first)
            left = length
            while left > 0:
                block = source.read(min(files.CHUNK, left))
                if not block:
                    break
                self.wfile.write(block)
                left -= len(block)
        except OSError:
            pass


def run(options, log=print):
    """x4ls master: serve until stopped (Ctrl+C, or SIGTERM from systemd)."""
    import signal
    master = Master(options, log).start()
    stop = threading.Event()

    def ask_stop(*_):
        stop.set()
    try:
        signal.signal(signal.SIGTERM, ask_stop)
    except (ValueError, OSError):   # not the main thread, or a platform without it
        pass
    try:
        while not stop.wait(1.0):
            pass
    except KeyboardInterrupt:
        pass
    finally:
        master.close()
    return 0
