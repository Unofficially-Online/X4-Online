"""A small stand-in for the central server (docs/NETPLAY.md, "HTTP API v1" and "UDP echo"), on 127.0.0.1 only.

Built from the contract's API table for the package 2 tests (MasterClient, nat.reachable, nat.public_endpoint); it is
not package 1's master and holds everything in memory. Every request is recorded (method, path, auth header, Range).
Faults a test may switch on: `cut_download_after` (the next file download sends that many bytes of a full-length
answer, then drops the connection), `ignore_range` (downloads always answer 200 with the whole file), `prefix`
(served under a path prefix, as behind a reverse proxy). The echo answers X4MPECHO with X4MPSEEN padded to the
request's size; the probe sends X4MPPROBE|1|<nonce> to the request's source IP and tries a TCP connect there."""
import hashlib
import http.server
import json
import secrets
import socket
import threading
import time
import urllib.parse

CODE_ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'
BODY_MAX = 64 * 1024
FILE_CAPS = {'session.json': 1024 * 1024, 'session-save.xml.gz': 64 * 1024 * 1024, 'play-recipe.json': 1024 * 1024}
PROBES_PER_MINUTE = 6
LOBBY_HOLD = 1.0


class FakeEcho:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.settimeout(0.1)
        self.address = self.sock.getsockname()
        self.silent = False
        self.seen = []
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while not self.stopped.is_set():
            try:
                data, source = self.sock.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                continue
            except OSError:
                return
            self.seen.append((data, source))
            fields = data.split(b'|')
            if self.silent or len(fields) < 3 or fields[:2] != [b'X4MPECHO', b'1']:
                continue
            reply = b'X4MPSEEN|1|' + fields[2] + f'|{source[0]}:{source[1]}'.encode('ascii')
            if len(reply) < len(data):
                reply += b'|' + b'.' * (len(data) - len(reply) - 1)
            self.sock.sendto(reply[:len(data)], source)

    def close(self):
        self.stopped.set()
        self.sock.close()
        self.thread.join(1)


class Game:
    def __init__(self, number, body):
        self.game_id = f'g{number}'
        self.code = ''.join(secrets.choice(CODE_ALPHABET) for _ in range(8))
        self.secret = secrets.token_hex(16)
        self.name = body['name']
        self.public = body['public']
        self.release = body['release']
        self.players_max = body['players_max']
        self.lan = body.get('lan')
        self.state = 'lobby'
        self.stamp = 1
        self.files = {}
        self.players = {secrets.token_hex(16): {'slot': 1, 'ready': False, 'host': True}}
        self.start = None
        self.created = time.monotonic()
        self.changed = threading.Condition()

    @property
    def host_ticket(self):
        return next(ticket for ticket, player in self.players.items() if player['host'])

    def touch(self):
        with self.changed:
            self.stamp += 1
            self.changed.notify_all()

    def fields(self, release):
        return {'game_id': self.game_id, 'name': self.name, 'release': self.release,
                'compatible': self.release == release, 'players': len(self.players), 'players_max': self.players_max,
                'state': self.state, 'mode': 'auto', 'open_slots_free': self.players_max - len(self.players),
                'age_s': int(time.monotonic() - self.created)}


class Refusal(Exception):
    def __init__(self, status, code, why):
        super().__init__(code)
        self.status = status
        self.code = code
        self.why = why


class FakeMaster:
    def __init__(self, prefix=''):
        self.prefix = prefix.rstrip('/')
        self.games = {}
        self.requests = []
        self.probes = {}
        self.cut_download_after = None
        self.ignore_range = False
        self.echo = FakeEcho()
        self.started = time.monotonic()
        self.counter = 0
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def log_message(self, *args):
                pass

            def do_GET(self):
                owner.dispatch(self, 'GET')

            def do_POST(self):
                owner.dispatch(self, 'POST')

            def do_PUT(self):
                owner.dispatch(self, 'PUT')

            def do_DELETE(self):
                owner.dispatch(self, 'DELETE')

        self.http = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.http.daemon_threads = True
        self.port = self.http.server_address[1]
        self.base_url = f'http://127.0.0.1:{self.port}{self.prefix}'
        self.thread = threading.Thread(target=self.http.serve_forever, kwargs={'poll_interval': 0.05}, daemon=True)
        self.thread.start()

    def close(self):
        self.http.shutdown()
        self.http.server_close()
        self.echo.close()

    # ---- plumbing --------------------------------------------------------------------------------------------

    @staticmethod
    def send(handler, status, payload, content_type='application/json', headers=None):
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode('utf-8')
        handler.send_response(status)
        handler.send_header('Content-Type', content_type)
        handler.send_header('Content-Length', str(len(data)))
        for name, value in (headers or {}).items():
            handler.send_header(name, value)
        handler.end_headers()
        handler.wfile.write(data)

    def dispatch(self, handler, method):
        parts = urllib.parse.urlsplit(handler.path)
        path = parts.path
        self.requests.append({'method': method, 'path': path, 'auth': handler.headers.get('X-X4MP-Auth'),
                              'range': handler.headers.get('Range')})
        if self.prefix and not path.startswith(self.prefix + '/'):
            # What a reverse proxy answers outside its prefix: a page, not the master's JSON.
            return self.send(handler, 404, b'<html>not here</html>', 'text/html')
        path = path[len(self.prefix):]
        query = dict(urllib.parse.parse_qsl(parts.query))
        try:
            if method == 'PUT':
                return self.upload(handler, path)
            if method == 'GET' and '/files/' in path:
                return self.download(handler, path)
            body = self.body(handler) if method in ('POST', 'DELETE') else {}
            answer = self.route(handler, method, path, query, body)
        except Refusal as refusal:
            return self.send(handler, refusal.status, {'ok': False, 'error': refusal.code, 'why': refusal.why})
        self.send(handler, 200, dict({'ok': True}, **answer))

    @staticmethod
    def body(handler):
        length = int(handler.headers.get('Content-Length') or 0)
        if length > BODY_MAX:
            raise Refusal(413, 'too_large', 'The request is too large.')
        raw = handler.rfile.read(length) if length else b'{}'
        try:
            body = json.loads(raw.decode('utf-8'))
        except ValueError:
            raise Refusal(400, 'bad_request', 'The request is not JSON.') from None
        if not isinstance(body, dict):
            raise Refusal(400, 'bad_request', 'The request is not a JSON object.')
        return body

    def game(self, game_id):
        game = self.games.get(game_id)
        if game is None:
            raise Refusal(404, 'unknown_game', 'This game is gone.')
        return game

    @staticmethod
    def auth(handler, body, *names):
        header = handler.headers.get('X-X4MP-Auth')
        if header:
            return header
        for name in names:
            if isinstance(body.get(name), str):
                return body[name]
        return None

    # ---- routes ----------------------------------------------------------------------------------------------

    def route(self, handler, method, path, query, body):
        segments = path.strip('/').split('/')
        if segments[:2] != ['api', 'v1'] or len(segments) < 3:
            raise Refusal(404, 'not_found', 'No such API path.')
        segments = segments[2:]
        if method == 'GET' and segments == ['health']:
            return {'version': 'fake', 'games': len(self.games), 'relays': 0,
                    'uptime_s': int(time.monotonic() - self.started), 'echo_port': self.echo.address[1]}
        if method == 'POST' and segments == ['games']:
            return self.create(body)
        if method == 'GET' and segments == ['games']:
            release = query.get('release', '')
            games = [game.fields(release) for game in reversed(list(self.games.values())) if game.public]
            return {'games': games[:100]}
        if method == 'GET' and len(segments) == 2 and segments[0] == 'codes':
            wanted = segments[1].replace('-', '').upper()
            for game in self.games.values():
                if game.code == wanted:
                    return game.fields(query.get('release', ''))
            raise Refusal(404, 'unknown_code', 'No game has this code.')
        if method == 'POST' and segments == ['probe']:
            return self.probe(handler, body)
        if len(segments) >= 2 and segments[0] == 'games':
            game = self.game(segments[1])
            action = segments[2] if len(segments) > 2 else None
            return self.game_action(handler, method, game, action, query, body)
        raise Refusal(404, 'not_found', 'No such API path.')

    def create(self, body):
        if not isinstance(body.get('name'), str) or not isinstance(body.get('release'), str):
            raise Refusal(400, 'bad_request', 'name and release are needed.')
        if body.get('players_max') not in range(2, 9) or body.get('want') not in ('auto', 'direct', 'relay'):
            raise Refusal(400, 'bad_request', 'players_max must be 2..8.')
        self.counter += 1
        game = Game(self.counter, body)
        self.games[game.game_id] = game
        return {'game_id': game.game_id, 'code': f'{game.code[:4]}-{game.code[4:]}', 'secret': game.secret,
                'host_ticket': game.host_ticket, 'slot': 1}

    def game_action(self, handler, method, game, action, query, body):
        if method == 'DELETE' and action is None:
            if self.auth(handler, body, 'secret') != game.secret:
                raise Refusal(403, 'forbidden', 'Wrong secret.')
            del self.games[game.game_id]
            return {}
        if method == 'POST' and action == 'join':
            if body.get('release') != game.release:
                raise Refusal(409, 'incompatible', 'This game runs another release.')
            if len(game.players) >= game.players_max:
                raise Refusal(409, 'full', 'This game is full.')
            ticket = secrets.token_hex(16)
            game.players[ticket] = {'slot': len(game.players) + 1, 'ready': False, 'host': False}
            game.touch()
            files = {name: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                     for name, data in game.files.items()}
            return {'ticket': ticket, 'slot': game.players[ticket]['slot'], 'files': files}
        if method == 'GET' and action == 'lobby':
            return self.lobby(game, query)
        if method == 'POST' and action == 'ready':
            player = game.players.get(self.auth(handler, body, 'ticket'))
            if player is None:
                raise Refusal(403, 'forbidden', 'Wrong ticket.')
            player['ready'] = bool(body.get('ready'))
            game.touch()
            return {}
        if method == 'POST' and action == 'start':
            if self.auth(handler, body, 'secret') != game.secret:
                raise Refusal(403, 'forbidden', 'Wrong secret.')
            game.state = 'starting'
            game.start = {'load_at': int(time.time()) + 20, 'relay': {'host': '127.0.0.1', 'port': 47200,
                                                                      'token': secrets.token_hex(16)}, 'mode': 'relay'}
            game.touch()
            return {}
        if method == 'POST' and action == 'heartbeat':
            auth = self.auth(handler, body, 'secret', 'ticket')
            if auth != game.secret and auth not in game.players:
                raise Refusal(403, 'forbidden', 'Wrong secret or ticket.')
            return {}
        if method == 'POST' and action == 'leave':
            if game.players.pop(self.auth(handler, body, 'ticket'), None) is None:
                raise Refusal(403, 'forbidden', 'Wrong ticket.')
            game.touch()
            return {}
        raise Refusal(404, 'not_found', 'No such API path.')

    def lobby(self, game, query):
        if query.get('ticket') not in game.players:
            raise Refusal(403, 'forbidden', 'Wrong ticket.')
        since = int(query.get('since', '0') or 0)
        with game.changed:
            game.changed.wait_for(lambda: game.stamp > since, timeout=LOBBY_HOLD)
        players = [{'slot': player['slot'], 'ready': player['ready'], 'host': player['host']}
                   for player in game.players.values()]
        return {'stamp': game.stamp, 'state': game.state, 'players': players, 'start': game.start}

    def probe(self, handler, body):
        ip = handler.client_address[0]
        recent = [moment for moment in self.probes.get(ip, []) if time.monotonic() - moment < 60]
        if len(recent) >= PROBES_PER_MINUTE:
            raise Refusal(429, 'rate_limited', 'Too many probes; try again in a minute.')
        self.probes[ip] = recent + [time.monotonic()]
        udp_port = body.get('udp_port')
        tcp_port = body.get('tcp_port')
        nonce = str(body.get('nonce', ''))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(f'X4MPPROBE|1|{nonce}'.encode('ascii'), (ip, udp_port))
        try:
            with socket.create_connection((ip, tcp_port), timeout=1.0):
                tcp = True
        except OSError:
            tcp = False
        return {'tcp': tcp, 'ip': ip}

    # ---- files -----------------------------------------------------------------------------------------------

    def upload(self, handler, path):
        segments = path.strip('/').split('/')
        try:
            if len(segments) != 6 or segments[:3] != ['api', 'v1', 'games'] or segments[4] != 'files':
                raise Refusal(404, 'not_found', 'No such API path.')
            game = self.game(segments[3])
            name = segments[5]
            length = int(handler.headers.get('Content-Length') or 0)
            if handler.headers.get('X-X4MP-Auth') != game.secret:
                handler.rfile.read(length)
                raise Refusal(403, 'forbidden', 'Wrong secret.')
            if name not in FILE_CAPS or length > FILE_CAPS[name]:
                handler.rfile.read(length)
                raise Refusal(413, 'too_large', 'This file is too large or not expected.')
            data = handler.rfile.read(length)
            digest = hashlib.sha256(data).hexdigest()
            if handler.headers.get('X-X4MP-Sha256') != digest:
                raise Refusal(400, 'sha256_mismatch', 'The upload does not match its sha-256.')
        except Refusal as refusal:
            return self.send(handler, refusal.status, {'ok': False, 'error': refusal.code, 'why': refusal.why})
        game.files[name] = data
        self.send(handler, 200, {'ok': True, 'name': name, 'size': len(data), 'sha256': digest})

    def download(self, handler, path):
        segments = path.strip('/').split('/')
        try:
            if len(segments) != 6 or segments[:3] != ['api', 'v1', 'games'] or segments[4] != 'files':
                raise Refusal(404, 'not_found', 'No such API path.')
            game = self.game(segments[3])
            auth = handler.headers.get('X-X4MP-Auth')
            if auth != game.secret and auth not in game.players:
                raise Refusal(403, 'forbidden', 'Wrong ticket.')
            data = game.files.get(segments[5])
            if data is None:
                raise Refusal(404, 'unknown_file', 'No such file.')
        except Refusal as refusal:
            return self.send(handler, refusal.status, {'ok': False, 'error': refusal.code, 'why': refusal.why})
        start = 0
        wanted = handler.headers.get('Range', '')
        if wanted.startswith('bytes=') and wanted.endswith('-') and not self.ignore_range:
            start = int(wanted[6:-1])
            if start >= len(data):
                return self.send(handler, 416, {'ok': False, 'error': 'range', 'why': 'Nothing left.'})
        status = 206 if start else 200
        body = data[start:]
        handler.send_response(status)
        handler.send_header('Content-Type', 'application/octet-stream')
        handler.send_header('Content-Length', str(len(body)))
        if start:
            handler.send_header('Content-Range', f'bytes {start}-{len(data) - 1}/{len(data)}')
        handler.end_headers()
        if self.cut_download_after is not None:
            cut = self.cut_download_after
            self.cut_download_after = None
            handler.wfile.write(body[:cut])
            handler.wfile.flush()
            handler.close_connection = True
            handler.connection.shutdown(socket.SHUT_RDWR)
            return
        handler.wfile.write(body)
