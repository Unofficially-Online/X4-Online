"""A LAN host's session files for its joiners (docs/NETPLAY.md: without a master server the host's PC holds what the
master would hold: session.json, the session save and play-recipe.json). Standard library only.

One request per TCP connection, on the relay's port number:
  joiner -> host  X4NS|1|<slot>|<nonce>|<name>|<offset>|<mac>\\n
                  nonce: 16 hex digits, fresh per request; mac: HMAC-SHA256 hex of the line before '|<mac>', keyed with
                  the game's token (which the joiner got when the host's lobby admitted it; it never crosses this link)
  host -> joiner  OK|<size>|<sha256>\\n and the bytes from <offset>, or NO|<reason>\\n
Only the three names, only what the host offered, only to addresses its lobby admitted. A download resumes a partial
file (<dest>.part) and is kept only when its SHA-256 is the one the host's lobby announced. The shape is that of the
hot join's file service (joinfiles.py), kept apart from it: that one serves a running session's join files."""
import hashlib
import hmac
from pathlib import Path
import re
import secrets
import socket
import socketserver
import threading
import time

PROTOCOL = 1
TAG = 'X4NS'
NAMES = re.compile(r'session\.json|session-save\.xml\.gz|play-recipe\.json')
HEX64 = re.compile(r'[0-9a-f]{64}')
HEX32 = re.compile(r'[0-9a-f]{32}')
LINE_MAX = 400
CHUNK = 1 << 20
TIMEOUT_S = 60
CONNECTIONS_MAX = 8
NONCES_MAX = 4096
RETRY_PAUSE_MAX_S = 10
REFUSE_DRAIN_S = 2.0


class FetchError(Exception):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        for block in iter(lambda: source.read(CHUNK), b''):
            digest.update(block)
    return digest.hexdigest()


def mac(token, text):
    return hmac.new(token.encode('ascii'), text.encode('ascii'), hashlib.sha256).hexdigest()


def request_line(token, slot, name, offset=0, nonce=None):
    nonce = nonce or secrets.token_hex(8)
    text = f'{TAG}|{PROTOCOL}|{slot}|{nonce}|{name}|{offset}'
    return f'{text}|{mac(token, text)}\n'.encode('ascii')


class FileHost:
    """The host's side. allow: a set the host's lobby adds admitted addresses to (read live); slots: the joiners'."""

    def __init__(self, token, port, allow, slots, bind='0.0.0.0'):
        if not HEX32.fullmatch(token):
            raise ValueError('the game token is 32 hex digits')
        self.token = token
        self.allow = allow
        self.slots = frozenset(slots)
        self.offered = {}
        self.nonces = set()
        self.lock = threading.Lock()
        self.active = 0
        self.served = []
        service = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                service.handle(self.request, self.client_address)

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        self.server = Server((bind, port), Handler)
        self.port = self.server.server_address[1]
        self.thread = None

    def offer(self, name, path):
        if not NAMES.fullmatch(name):
            raise ValueError(f'not a session file name: {name}')
        path = Path(path)
        entry = (path, path.stat().st_size, sha256_file(path))
        with self.lock:
            self.offered[name] = entry
        return entry[1], entry[2]

    def start(self):
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={'poll_interval': 0.2}, daemon=True)
        self.thread.start()
        return self

    def close(self):
        self.server.shutdown()
        self.server.server_close()

    def refuse(self, conn, reason):
        """NO and a graceful close: closing with the request unread resets the connection, and on Windows the reset can
        overtake the answer. So: half-close, then read what is left until the joiner closes (a short while at most)."""
        try:
            conn.sendall(f'NO|{reason}\n'.encode('ascii'))
            conn.shutdown(socket.SHUT_WR)
            deadline = time.monotonic() + REFUSE_DRAIN_S
            while True:
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                conn.settimeout(left)
                if not conn.recv(4096):
                    break
        except OSError:
            pass

    def handle(self, conn, address):
        conn.settimeout(TIMEOUT_S)
        if address[0] not in self.allow:
            return self.refuse(conn, 'address')
        with self.lock:
            busy = self.active >= CONNECTIONS_MAX
            if not busy:
                self.active += 1
        if busy:
            return self.refuse(conn, 'busy')
        try:
            self.serve_request(conn, address)
        except OSError:
            pass
        finally:
            with self.lock:
                self.active -= 1

    def read_line(self, conn):
        line = b''
        while not line.endswith(b'\n'):
            data = conn.recv(LINE_MAX + 1 - len(line))
            if not data or len(line) + len(data) > LINE_MAX:
                return None
            line += data
        try:
            return line.decode('ascii').rstrip('\n')
        except UnicodeError:
            return None

    def serve_request(self, conn, address):
        line = self.read_line(conn)
        fields = line.split('|') if line else []
        if len(fields) != 7 or fields[0] != TAG or fields[1] != str(PROTOCOL) or not fields[2].isdigit() \
                or not re.fullmatch(r'[0-9a-f]{16}', fields[3]) or not fields[5].isdigit():
            return self.refuse(conn, 'request')
        text = line.rpartition('|')[0]
        if not hmac.compare_digest(fields[6], mac(self.token, text)):
            return self.refuse(conn, 'mac')
        slot = int(fields[2])
        nonce = fields[3]
        name = fields[4]
        offset = int(fields[5])
        if slot not in self.slots:
            return self.refuse(conn, 'slot')
        with self.lock:
            replay = nonce in self.nonces
            if not replay:
                if len(self.nonces) >= NONCES_MAX:
                    self.nonces.clear()   # bounded; a replayed request still needs the token's mac
                self.nonces.add(nonce)
            entry = self.offered.get(name)
        if replay:
            return self.refuse(conn, 'nonce')
        if entry is None:
            return self.refuse(conn, 'name')
        path, size, digest = entry
        if offset > size:
            return self.refuse(conn, 'offset')
        conn.sendall(f'OK|{size}|{digest}\n'.encode('ascii'))
        with open(path, 'rb') as source:
            source.seek(offset)
            sent = offset
            while sent < size:
                block = source.read(min(CHUNK, size - sent))
                if not block:
                    break
                conn.sendall(block)
                sent += len(block)
        with self.lock:
            self.served.append((address[0], name, offset, sent))


def read_head(conn):
    head = b''
    while not head.endswith(b'\n'):
        data = conn.recv(1)
        if not data or len(head) > LINE_MAX:
            raise FetchError('the host closed the connection before its answer')
        head += data
    return head.decode('ascii', 'replace').rstrip('\n').split('|')


def fetch(host, port, token, slot, name, dest, expect_sha, progress=None, retries=3, timeout=TIMEOUT_S):
    """Download name from the host into dest (resuming dest.part); returns dest. Raises FetchError. progress(done,
    total) as bytes arrive."""
    if not NAMES.fullmatch(name) or not HEX64.fullmatch(str(expect_sha or '')):
        raise FetchError(f'not a session file or no SHA-256: {name}')
    dest = Path(dest)
    part = dest.with_name(dest.name + '.part')
    dest.parent.mkdir(parents=True, exist_ok=True)
    last = None
    for attempt in range(retries + 1):
        if attempt:
            time.sleep(min(2 ** attempt, RETRY_PAUSE_MAX_S))
        offset = part.stat().st_size if part.is_file() else 0
        try:
            got = fetch_once(host, port, token, slot, name, part, offset, expect_sha, progress, timeout)
        except OSError as error:
            last = FetchError(f'{name}: {error}')
            continue
        except FetchError as error:
            if 'refused' in str(error):
                raise
            last = error
            continue
        if got is None:
            continue
        if sha256_file(part) != expect_sha:
            part.unlink()
            last = FetchError(f'{name}: the downloaded bytes do not match the announced SHA-256')
            continue
        part.replace(dest)
        return dest
    raise last or FetchError(f'{name}: no attempt left')


def fetch_once(host, port, token, slot, name, part, offset, expect_sha, progress, timeout):
    """One connection: the bytes from offset into part. True when complete, None when a resume is needed."""
    with socket.create_connection((host, port), timeout=timeout) as conn:
        conn.settimeout(timeout)
        conn.sendall(request_line(token, slot, name, offset))
        fields = read_head(conn)
        if fields[0] == 'NO':
            raise FetchError(f'{name}: refused by the host ({fields[1] if len(fields) > 1 else "?"})')
        if len(fields) != 3 or fields[0] != 'OK' or not fields[1].isdigit() or fields[2] != expect_sha:
            raise FetchError(f'{name}: the host offers another file than the one it announced')
        size = int(fields[1])
        if offset > size:
            part.unlink()
            return None
        with open(part, 'ab') as out:
            got = offset
            while got < size:
                data = conn.recv(min(CHUNK, size - got))
                if not data:
                    break
                out.write(data)
                got += len(data)
                if progress:
                    progress(got, size)
    return True if got >= size else None
