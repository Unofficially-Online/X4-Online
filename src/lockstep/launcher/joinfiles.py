"""Join file service: a hot joiner (x4ls join without --arm-at) fetches the join save and the join package of a running
x4ls play session (--open K) from the host over TCP. Standard library only.

Protocol 1, one request per connection: an ASCII line, then raw bytes.
  joiner -> host  X4JF|1|<slot>|<nonce>|<name>|<offset>|<mac>\\n
                  nonce: 16 hex digits, fresh per request; mac: HMAC-SHA256 hex of 'X4JF|1|<slot>|<nonce>|<name>|<offset>'
                  keyed with the session token (the token itself never crosses this link)
  host -> joiner  OK|<size>|<sha256>\\n and then the bytes from <offset> to <size>, or NO|<reason>\\n
Names: j<T>.xml.gz and j<T>.x4join, and only what the host offered: a join's two files once the relay's SAVED notice named
them and they matched their hashes (hotjoin.HostJoins). A wrong mac, a nonce seen before, an unknown name or an offset past
the end is NO; so is a source address the session does not allow. A fetch resumes a partial download (<dest>.part) from its
length and keeps the file only when the size and SHA-256 match the host's, which must match the hash the caller expects
(the relay's NGJOINSTATE names the package's).

Compressed package (optional, same protocol): j<T>.x4join.gz is the gzip of j<T>.x4join (about 30 MB for a 192 MB
package of a late join: two thirds of it are zero bytes). A host that has it offers it before the raw package; the
identity stays the raw name and the raw SHA-256 the relay announced: the joiner checks the unpacked bytes against it
(fetch_package). offered() asks for a name at an offset past any file: NO|offset means offered, NO|name not (yet); no
bytes cross. A host without the .gz (an older launcher, a server that only stores the raw files) is served raw."""
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
NAME = re.compile(r'j\d{1,7}\.(?:xml\.gz|x4join(?:\.gz)?)')
PROBE_OFFSET = 1 << 62   # offered(): past the end of any file
GZ_LEVEL = 1             # fast: 192 MB in about a second, 30 MB out (level 6: 27 MB in 2.7 s)
HEX64 = re.compile(r'[0-9a-f]{64}')
LINE_MAX = 512
CHUNK = 1 << 20
REFUSE_DRAIN_S = 2.0    # a refusal waits this long at most for the client to close (a graceful close, not a reset)
TIMEOUT_S = 60         # a stalled connection (either side) gives up after this long without progress
CONNECTIONS_MAX = 8     # concurrent downloads the host serves
NONCES_MAX = 4096


class FetchError(Exception):
    pass


def mac(token, slot, nonce, name, offset):
    text = f'X4JF|{PROTOCOL}|{slot}|{nonce}|{name}|{offset}'
    return hmac.new(token.encode('ascii'), text.encode('ascii'), hashlib.sha256).hexdigest()


def request_line(token, slot, name, offset=0, nonce=None):
    nonce = nonce or secrets.token_hex(8)
    return f'X4JF|{PROTOCOL}|{slot}|{nonce}|{name}|{offset}|{mac(token, slot, nonce, name, offset)}\n'.encode('ascii')


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(CHUNK), b''):
            h.update(block)
    return h.hexdigest()


class FileService:
    """The host's side: serve the offered files to the session's allowed addresses (127.0.0.1 always)."""
    def __init__(self, token, bind='127.0.0.1', port=0, allow=(), slots=()):
        if not re.fullmatch(r'[0-9a-f]{32}', token):
            raise ValueError('the session token is 32 hex digits')
        self.token, self.allow = token, frozenset({'127.0.0.1', *allow})
        self.slots = frozenset(slots)   # the slots a joiner may name (the session's open slots); empty: any 2..8
        self.offered, self.nonces, self.lock = {}, set(), threading.Lock()
        self.served, self.refused, self.active = [], [], 0
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

    def offer(self, name, path, sha=None):
        """Offer a file under name (checked against sha when given); returns (size, sha)."""
        if not NAME.fullmatch(name):
            raise ValueError(f'not a join file name: {name}')
        path = Path(path)
        digest = sha256_file(path)
        if sha is not None and digest != sha:
            raise ValueError(f'{path}: SHA-256 {digest[:16]}.. is not the announced {sha[:16]}..')
        size = path.stat().st_size
        with self.lock:
            self.offered[name] = (path, size, digest)
        return size, digest

    def start(self):
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={'poll_interval': 0.2}, daemon=True)
        self.thread.start()
        return self

    def close(self):
        self.server.shutdown()
        self.server.server_close()

    def refuse(self, conn, address, reason):
        with self.lock:
            self.refused.append((address[0], reason))
        try:
            conn.sendall(f'NO|{reason}\n'.encode('ascii'))
            # Close gracefully: closing with the request still unread resets the connection, and on Windows the reset can
            # overtake the answer (the client then sees WinError 10053, not the reason). Half-close, then read what is left
            # until the client closes, for at most REFUSE_DRAIN_S.
            conn.shutdown(socket.SHUT_WR)
            deadline = time.monotonic() + REFUSE_DRAIN_S
            while (left := deadline - time.monotonic()) > 0:
                conn.settimeout(left)
                if not conn.recv(4096):
                    break
        except OSError:
            pass

    def handle(self, conn, address):
        conn.settimeout(TIMEOUT_S)
        if address[0] not in self.allow:
            return self.refuse(conn, address, 'address')
        with self.lock:
            if self.active >= CONNECTIONS_MAX:
                busy = True
            else:
                busy, self.active = False, self.active + 1
        if busy:
            return self.refuse(conn, address, 'busy')
        try:
            line = b''
            while not line.endswith(b'\n'):
                data = conn.recv(LINE_MAX + 1 - len(line))
                if not data or len(line) + len(data) > LINE_MAX:
                    return self.refuse(conn, address, 'request')
                line += data
            try:
                f = line.decode('ascii').rstrip('\n').split('|')
            except UnicodeError:
                return self.refuse(conn, address, 'request')
            if len(f) != 7 or f[0] != 'X4JF' or f[1] != str(PROTOCOL) or not f[2].isdigit() or not f[5].isdigit() or \
                    not re.fullmatch(r'[0-9a-f]{16}', f[3]) or not re.fullmatch(r'[0-9a-f]{64}', f[6]):
                return self.refuse(conn, address, 'request')
            slot, nonce, name, offset = int(f[2]), f[3], f[4], int(f[5])
            if not hmac.compare_digest(f[6], mac(self.token, f[2], nonce, name, f[5])):
                return self.refuse(conn, address, 'mac')
            if (self.slots and slot not in self.slots) or not 2 <= slot <= 8:
                return self.refuse(conn, address, 'slot')
            with self.lock:
                replay = nonce in self.nonces
                if not replay:
                    if len(self.nonces) >= NONCES_MAX:
                        self.nonces.clear()   # bounded; a replay of a cleared nonce still needs the token's mac
                    self.nonces.add(nonce)
                entry = self.offered.get(name)
            if replay:
                return self.refuse(conn, address, 'nonce')
            if entry is None:
                return self.refuse(conn, address, 'name')
            path, size, digest = entry
            if offset > size:
                return self.refuse(conn, address, 'offset')
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
        except OSError:
            pass
        finally:
            with self.lock:
                self.active -= 1


OFFER_WAIT_S = 180   # the relay answers saved before the host has read the notice and hashed the files: 'name' is retried this long


def gzip_file(source, dest):
    """dest = gzip of source (the host's compressed package), written to dest.part first; returns dest."""
    import gzip
    import shutil
    dest = Path(dest)
    part = dest.with_name(dest.name + '.part')
    with open(source, 'rb') as src, open(part, 'wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, compresslevel=GZ_LEVEL, mtime=0) as out:
            shutil.copyfileobj(src, out, CHUNK)
    part.replace(dest)
    return dest


def offered(host, port, token, slot, name, timeout=TIMEOUT_S):
    """True when the host offers name now, False when not (yet); raises FetchError on any other answer."""
    if not NAME.fullmatch(name):
        raise FetchError(f'not a join file name: {name}')
    try:
        with socket.create_connection((host, port), timeout=timeout) as conn:
            conn.settimeout(timeout)
            conn.sendall(request_line(token, slot, name, PROBE_OFFSET))
            head = b''
            while not head.endswith(b'\n') and len(head) <= LINE_MAX:
                data = conn.recv(1)
                if not data:
                    break
                head += data
    except OSError as error:
        raise FetchError(f'{name}: {error}') from None
    f = head.decode('ascii', 'replace').rstrip('\n').split('|')
    if f[0] == 'NO' and len(f) > 1 and f[1] in ('offset', 'name', 'busy'):
        return f[1] == 'offset'
    raise FetchError(f'{name}: unexpected answer {head[:80]!r}')


def fetch_package(host, port, token, slot, name, dest, expect_sha, progress=None, offer_wait_s=OFFER_WAIT_S, log=print):
    """The package into dest, by its compressed copy when the host offers one (the module text), else raw. Returns
    'gz' or 'raw'. The unpacked bytes must hash to expect_sha. Raises FetchError."""
    import gzip
    import shutil
    dest = Path(dest)
    gz = dest.with_name(dest.name + '.gz')
    deadline = time.monotonic() + offer_wait_s
    while True:
        if offered(host, port, token, slot, gz.name):
            fetch(host, port, token, slot, gz.name, gz, progress=progress)
            part = dest.with_name(dest.name + '.part')
            with gzip.open(gz, 'rb') as src, open(part, 'wb') as out:
                shutil.copyfileobj(src, out, CHUNK)
            if sha256_file(part) != expect_sha:
                part.unlink()
                gz.unlink()
                raise FetchError(f'{gz.name}: the unpacked package does not match the announced SHA-256 {expect_sha[:16]}..')
            part.replace(dest)
            return 'gz'
        if offered(host, port, token, slot, dest.name):
            fetch(host, port, token, slot, dest.name, dest, expect_sha=expect_sha, progress=progress)
            return 'raw'
        if time.monotonic() > deadline:
            raise FetchError(f'{dest.name}: not offered by the host within {offer_wait_s} s (its saver may not play on that PC)')
        time.sleep(0.5)


def fetch(host, port, token, slot, name, dest, expect_sha=None, retries=5, progress=None, timeout=TIMEOUT_S, offer_wait_s=OFFER_WAIT_S):
    """Download name from the host into dest (resuming dest.part); returns the SHA-256. Raises FetchError."""
    if not NAME.fullmatch(name):
        raise FetchError(f'not a join file name: {name}')
    dest = Path(dest)
    part = dest.with_name(dest.name + '.part')
    dest.parent.mkdir(parents=True, exist_ok=True)
    last, attempt, offer_deadline = None, 0, time.monotonic() + offer_wait_s
    while attempt <= retries:
        if attempt:
            time.sleep(min(2 ** attempt, 20))
        attempt += 1
        offset = part.stat().st_size if part.is_file() else 0
        try:
            with socket.create_connection((host, port), timeout=timeout) as conn:
                conn.settimeout(timeout)
                conn.sendall(request_line(token, slot, name, offset))
                head = b''
                while not head.endswith(b'\n'):
                    data = conn.recv(1)
                    if not data or len(head) > LINE_MAX:
                        raise FetchError('the host closed the connection before its answer')
                    head += data
                f = head.decode('ascii', 'replace').rstrip('\n').split('|')
                if f[0] == 'NO':
                    reason = f[1] if len(f) > 1 else '?'
                    if reason == 'name' and time.monotonic() < offer_deadline:
                        attempt -= 1          # not offered yet: wait for the host, without using up a retry
                        time.sleep(2)
                        last = FetchError(f'{name}: not offered by the host within {offer_wait_s} s')
                        continue
                    if reason in ('busy',):
                        last = FetchError(f'{name}: host busy')
                        continue
                    if reason == 'name':
                        raise FetchError(f'{name}: not offered by the host within {offer_wait_s} s (its saver may not play on that PC)')
                    raise FetchError(f'{name}: refused by the host ({reason})')
                if len(f) != 3 or f[0] != 'OK' or not f[1].isdigit() or not HEX64.fullmatch(f[2]):
                    raise FetchError(f'{name}: bad answer {head[:80]!r}')
                size, digest = int(f[1]), f[2]
                if expect_sha and digest != expect_sha:
                    raise FetchError(f'{name}: the host offers SHA-256 {digest[:16]}.., not the announced {expect_sha[:16]}..')
                if offset > size:
                    part.unlink()
                    continue
                with open(part, 'ab') as out:
                    got = offset
                    while got < size:
                        data = conn.recv(min(CHUNK, size - got))
                        if not data:
                            break
                        out.write(data)
                        got += len(data)
                        if progress:
                            progress(name, got, size)
                if got < size:
                    last = FetchError(f'{name}: connection lost at {got} of {size} bytes')
                    continue
            if sha256_file(part) != digest:
                part.unlink()
                last = FetchError(f'{name}: the downloaded bytes do not match SHA-256 {digest[:16]}..')
                continue
            part.replace(dest)
            return digest
        except FetchError:
            raise
        except (OSError, ValueError) as error:
            last = FetchError(f'{name}: {error}')
    raise last or FetchError(f'{name}: no attempt left')
