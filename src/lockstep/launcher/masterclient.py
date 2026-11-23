"""The launcher's client of the central server's HTTP API v1 (docs/NETPLAY.md, "The central server").

One method per row of the API table. Every answer is JSON {"ok": true, ...}; anything else becomes a MasterError
with the server's error code, its text for the page ("why") and the HTTP status, or the code 'unreachable' when no
answer came at all (connection refused, timeout, TLS failure, DNS). The page shows `why`; the launcher decides by
`code`.

The base URL is either the master itself ('http://203.0.113.10:47100') or a reverse proxy with a path prefix
('https://master.example.org/x4mp'): API paths are joined onto the prefix. HTTPS is verified with the default SSL
context (system certificates, host name check); there is deliberately no switch to turn that off. The UDP echo never
goes through a proxy: echo_address() is the base URL's host name and the echo_port the health answer names.

Uploads stream from disk with their sha-256 in X-X4MP-Sha256; downloads resume with Range into <dest>.part, check size
and sha-256 and only then rename, so a half-downloaded session save is never taken for a whole one. A game's secret or
a player's ticket goes in the header X-X4MP-Auth on every call that needs one (the API accepts it there) and also in
the JSON body field the API table names.
"""
import hashlib
import http.client
import json
import os
import ssl
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from .nat import normalize_code

API_PREFIX = '/api/v1'
DEFAULT_PORT = 47100
DEFAULT_ECHO_PORT = 47101
ENVIRONMENT_VARIABLE = 'X4MP_MASTER'
MASTER_FILE = 'master.json'
RELEASE_FILE = 'release.json'
ROOT = Path(__file__).resolve().parents[3]
# JSON answers are small (a list of at most 100 games); a larger one is refused rather than read into memory.
ANSWER_MAX = 4 * 1024 * 1024
ERROR_TEXT_MAX = 200
CHUNK = 64 * 1024
USER_AGENT = 'x4mp-launcher/1'
AUTH_HEADER = 'X-X4MP-Auth'
SHA256_HEADER = 'X-X4MP-Sha256'
HTTP_PARTIAL = 206
HTTP_RANGE_NOT_SATISFIABLE = 416


class MasterError(Exception):
    """The server's error (code, why, HTTP status), or code 'unreachable' / a local check with status 0."""

    def __init__(self, code, why='', status=0):
        super().__init__(f'{code}: {why}' if why else code)
        self.code = code
        self.why = why
        self.status = status


def normalize_base_url(text):
    """'http://host:47100' for 'host' or 'host:port'; a URL with a scheme is kept as given (a reverse proxy's
    'https://master.example.org/x4mp'), less a trailing slash. None for anything that is not http(s) with a host."""
    if not isinstance(text, str) or not text.strip():
        return None
    value = text.strip()
    if '://' not in value:
        host, separator, port = value.rpartition(':')
        if not separator:
            host = value
            port = str(DEFAULT_PORT)
        if not port.isdigit() or not host:
            return None
        value = f'http://{host}:{port}'
    parts = urllib.parse.urlsplit(value)
    if parts.scheme not in ('http', 'https') or not parts.hostname:
        return None
    if parts.query or parts.fragment:
        return None
    try:
        parts.port
    except ValueError:
        return None
    return urllib.parse.urlunsplit((parts.scheme, parts.netloc, parts.path.rstrip('/'), '', ''))


def _from_master_file(data):
    """The base URL a master.json names: {"url": ...}, {"master": "host[:port]"} or {"host": ..., "port": ...}."""
    if not isinstance(data, dict):
        return None
    for key in ('url', 'master'):
        if isinstance(data.get(key), str):
            return normalize_base_url(data[key])
    host = data.get('host')
    if isinstance(host, str) and host:
        port = data.get('port', DEFAULT_PORT)
        if isinstance(port, int) and 0 < port < 65536:
            return normalize_base_url(f'{host}:{port}')
    return None


def master_url(kit_dir=None, env=os.environ):
    """The master's base URL: the environment variable X4MP_MASTER, else master.json beside the kit, else None.

    Without kit_dir the kit is the folder holding this checkout (a release kit's repo/ folder), and only when it is
    one (release.json beside it): a development checkout never picks up a stray master.json from its parent."""
    configured = env.get(ENVIRONMENT_VARIABLE, '') if env is not None else ''
    if configured.strip():
        return normalize_base_url(configured)
    if kit_dir is None:
        kit = ROOT.parent
        if not (kit / RELEASE_FILE).is_file():
            return None
    else:
        kit = Path(kit_dir)
    try:
        data = json.loads((kit / MASTER_FILE).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None
    return _from_master_file(data)


def _segment(value):
    return urllib.parse.quote(str(value), safe='')


def _file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        while True:
            block = handle.read(CHUNK)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


class MasterClient:
    def __init__(self, base_url, timeout=10):
        self.base_url = normalize_base_url(base_url)
        if self.base_url is None:
            raise ValueError(f'not a master URL: {base_url!r}')
        self.timeout = timeout
        self.ssl_context = ssl.create_default_context()
        self._opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=self.ssl_context))
        self._echo = None

    # ---- plumbing ----------------------------------------------------------------------------------------------

    def url(self, path, query=None):
        """The full URL of an API path ('/games/g1/join') on this base URL, prefix included."""
        text = self.base_url + API_PREFIX + path
        if query:
            text += '?' + urllib.parse.urlencode(query)
        return text

    def _request(self, method, path, body=None, auth=None, query=None, headers=None, data=None):
        if body is not None:
            data = json.dumps(body).encode('utf-8')
        request = urllib.request.Request(self.url(path, query), data=data, method=method)
        request.add_header('User-Agent', USER_AGENT)
        request.add_header('Accept', 'application/json')
        if body is not None:
            request.add_header('Content-Type', 'application/json')
        if auth:
            request.add_header(AUTH_HEADER, auth)
        for name, value in (headers or {}).items():
            request.add_header(name, value)
        return request

    def _open(self, request, timeout=None):
        """The response of a 2xx answer; MasterError for an error answer or none at all."""
        try:
            return self._opener.open(request, timeout=timeout or self.timeout)
        except urllib.error.HTTPError as error:
            try:
                raw = error.read(ANSWER_MAX + 1)
            except (OSError, AttributeError, http.client.HTTPException):
                raw = b''
            finally:
                error.close()
            raise self._error(error.code, raw) from None
        except (urllib.error.URLError, OSError, http.client.HTTPException, ValueError) as error:
            reason = getattr(error, 'reason', error)
            raise MasterError('unreachable', str(reason), 0) from error

    @staticmethod
    def _error(status, raw):
        try:
            answer = json.loads(raw.decode('utf-8'))
        except (UnicodeDecodeError, ValueError):
            answer = None
        if isinstance(answer, dict) and isinstance(answer.get('error'), str):
            return MasterError(answer['error'], str(answer.get('why') or ''), status)
        # Not the master's JSON: a proxy's or a web server's page. Its first line is enough for a log.
        text = raw[:ERROR_TEXT_MAX].decode('utf-8', 'replace').strip().splitlines()
        return MasterError(f'http_{status}', text[0] if text else '', status)

    @staticmethod
    def _answer(status, raw):
        if len(raw) > ANSWER_MAX:
            raise MasterError('bad_answer', 'the answer is too large', status)
        try:
            answer = json.loads(raw.decode('utf-8'))
        except (UnicodeDecodeError, ValueError):
            raise MasterError('bad_answer', 'the answer is not JSON', status) from None
        if not isinstance(answer, dict):
            raise MasterError('bad_answer', 'the answer is not a JSON object', status)
        if answer.get('ok') is not True:
            raise MasterError(str(answer.get('error') or 'error'), str(answer.get('why') or ''), status)
        return answer

    def _call(self, method, path, body=None, auth=None, query=None, timeout=None):
        request = self._request(method, path, body=body, auth=auth, query=query)
        response = self._open(request, timeout)
        try:
            with response:
                raw = response.read(ANSWER_MAX + 1)
        except (OSError, http.client.HTTPException) as error:
            raise MasterError('unreachable', str(error), 0) from error
        return self._answer(response.status, raw)

    # ---- the API -----------------------------------------------------------------------------------------------

    def health(self):
        """{version, games, relays, uptime_s, echo_port}."""
        return self._call('GET', '/health')

    def echo_address(self):
        """(host, port) of the master's UDP echo for nat.public_endpoint(), or None (no health answer, no echo).

        The host is the base URL's: behind a reverse proxy only HTTP is proxied, the echo is the same machine. The
        first good answer is kept."""
        if self._echo is not None:
            return self._echo
        try:
            answer = self.health()
        except MasterError:
            return None
        port = answer.get('echo_port')
        if not isinstance(port, int) or not 0 < port < 65536:
            return None
        self._echo = (urllib.parse.urlsplit(self.base_url).hostname, port)
        return self._echo

    def create_game(self, name, public, release, players_max, session_sha256, open_slots=(), want='auto',
                    direct=None, lan=None):
        """{game_id, code, secret, host_ticket, slot}."""
        body = {'name': name, 'public': bool(public), 'release': release, 'players_max': players_max,
                'session_sha256': session_sha256, 'open_slots': list(open_slots), 'want': want, 'direct': direct,
                'lan': lan}
        return self._call('POST', '/games', body=body)

    def upload(self, game_id, secret, name, path):
        """Streams a file to the game ({name, size, sha256}); MasterError('upload_mismatch') if the server stored
        something else."""
        path = Path(path)
        size = path.stat().st_size
        digest = _file_sha256(path)
        headers = {'Content-Type': 'application/octet-stream', 'Content-Length': str(size), SHA256_HEADER: digest}
        with path.open('rb') as handle:
            request = self._request('PUT', f'/games/{_segment(game_id)}/files/{_segment(name)}', auth=secret,
                                    headers=headers, data=handle)
            response = self._open(request)
            try:
                with response:
                    raw = response.read(ANSWER_MAX + 1)
            except (OSError, http.client.HTTPException) as error:
                raise MasterError('unreachable', str(error), 0) from error
        answer = self._answer(response.status, raw)
        if answer.get('sha256') != digest or answer.get('size') != size:
            raise MasterError('upload_mismatch', 'the server stored other bytes than were sent', response.status)
        return answer

    def games(self, release):
        """The public games, newest first: [{game_id, name, release, compatible, players, players_max, ...}]."""
        answer = self._call('GET', '/games', query={'release': release})
        games = answer.get('games')
        if not isinstance(games, list):
            raise MasterError('bad_answer', 'no game list', 200)
        return games

    def code(self, code):
        """One game by its join code (public or private). A text that is no join code fails here, unsent."""
        normalized = normalize_code(code)
        if normalized is None:
            raise MasterError('bad_code', 'That is not a join code (8 letters and digits, like K7QX-3FNA).', 0)
        return self._call('GET', f'/codes/{_segment(normalized)}')

    def join(self, game_id, release, code=None):
        """{ticket, slot, files: {name: {size, sha256}}}; 409 incompatible / full / started."""
        body = {'release': release}
        if code is not None:
            body['code'] = normalize_code(code) or code
        return self._call('POST', f'/games/{_segment(game_id)}/join', body=body)

    def lobby(self, game_id, ticket, since=0, wait=25):
        """The long poll: {stamp, state, players, start}. The server holds it at most 25 s; this waits that long plus
        the normal timeout (wait is the server's hold, not a query field: the API table has none)."""
        query = {'ticket': ticket, 'since': since}
        return self._call('GET', f'/games/{_segment(game_id)}/lobby', auth=ticket, query=query,
                          timeout=wait + self.timeout)

    def ready(self, game_id, ticket):
        return self._call('POST', f'/games/{_segment(game_id)}/ready', body={'ticket': ticket, 'ready': True},
                          auth=ticket)

    def start(self, game_id, secret):
        return self._call('POST', f'/games/{_segment(game_id)}/start', body={'secret': secret}, auth=secret)

    def heartbeat(self, game_id, auth, tick=0, state='lobby'):
        """auth is the host's secret or a player's ticket; it travels in X-X4MP-Auth only (the body would have to
        name which of the two it is)."""
        return self._call('POST', f'/games/{_segment(game_id)}/heartbeat', body={'tick': tick, 'state': state},
                          auth=auth)

    def leave(self, game_id, ticket):
        return self._call('POST', f'/games/{_segment(game_id)}/leave', body={'ticket': ticket}, auth=ticket)

    def delete(self, game_id, secret):
        return self._call('DELETE', f'/games/{_segment(game_id)}', body={'secret': secret}, auth=secret)

    def probe(self, udp_port, tcp_port, nonce):
        """{tcp, ip}: the master connects to this PC's public IP; nat.reachable() listens for the UDP half."""
        return self._call('POST', '/probe', body={'udp_port': udp_port, 'tcp_port': tcp_port, 'nonce': nonce})

    # ---- downloads ---------------------------------------------------------------------------------------------

    def download(self, game_id, ticket, name, dest, sha256, size, progress=None):
        """Fetches a game file into dest and returns dest as a Path.

        Bytes land in <dest>.part; a .part left by an interrupted download is continued with Range. Before the
        rename the size and sha-256 must equal what the join answer announced, else the .part is deleted and
        MasterError('download_mismatch') is raised (a resumed download that fails is first tried once from the
        start). A dest that already holds the announced bytes is returned at once. progress(done_bytes, total_bytes)
        is called as bytes arrive. A broken connection keeps the .part for the next call: MasterError('unreachable')."""
        dest = Path(dest)
        expected = sha256.lower()
        if dest.is_file() and dest.stat().st_size == size and _file_sha256(dest) == expected:
            return dest
        dest.parent.mkdir(parents=True, exist_ok=True)
        part = dest.with_name(dest.name + '.part')
        for attempt in range(2):
            try:
                resumed = self._fetch(game_id, ticket, name, part, size, progress)
            except MasterError as error:
                if error.code == 'download_mismatch':
                    part.unlink(missing_ok=True)      # the server sent too much: nothing of it is worth resuming
                raise
            if part.stat().st_size == size and _file_sha256(part) == expected:
                os.replace(part, dest)
                return dest
            part.unlink()
            if not resumed or attempt:
                break
        raise MasterError('download_mismatch', f'{name}: the bytes do not match the announced size and sha-256', 0)

    def _fetch(self, game_id, ticket, name, part, size, progress):
        """Fills part up to size bytes; True when it continued an earlier .part."""
        have = part.stat().st_size if part.is_file() else 0
        if have > size:
            part.unlink()
            have = 0
        if part.is_file() and have == size:
            return True
        headers = {'Range': f'bytes={have}-'} if have else {}
        request = self._request('GET', f'/games/{_segment(game_id)}/files/{_segment(name)}', auth=ticket,
                                headers=headers)
        try:
            response = self._open(request)
        except MasterError as error:
            if error.status == HTTP_RANGE_NOT_SATISFIABLE and have:
                return True        # the .part already holds everything the server has; the check decides
            raise
        with response:
            if have and response.status != HTTP_PARTIAL:
                have = 0           # the server sent the whole file instead of the range
            elif have and not response.headers.get('Content-Range', '').startswith(f'bytes {have}-'):
                raise MasterError('bad_answer', 'the server answered another range', response.status)
            resumed = bool(have)
            self._copy(response, part, have, size, progress)
        return resumed

    @staticmethod
    def _copy(response, part, have, size, progress):
        mode = 'ab' if have else 'wb'
        with part.open(mode) as out:
            while have < size:
                try:
                    block = response.read(min(CHUNK, size - have + 1))
                except (OSError, http.client.HTTPException) as error:
                    raise MasterError('unreachable', f'the download broke off after {have} bytes: {error}', 0) from error
                if not block:
                    break
                if have + len(block) > size:
                    raise MasterError('download_mismatch', 'the server sent more bytes than announced', response.status)
                out.write(block)
                have += len(block)
                if progress is not None:
                    progress(have, size)
        if have < size:
            raise MasterError('unreachable', f'the download broke off after {have} of {size} bytes', 0)
