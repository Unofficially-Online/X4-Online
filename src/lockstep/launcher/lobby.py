"""x4ls lobby: this PC's X4 opens in its main menu, and the session is chosen there (Multiplayer: Host or Join).

Every PC of a playtest runs the same command with the same play overlay and snapshot (tools/make_playtest_kit.py puts
both into one folder):

    python tools/x4ls.py lobby --overlay DIR --snapshot FILE [--port 47000] [--bind ADDRESS]

- X4 starts with the overlay installed in lobby form: only tools/dev/native_gate_lobby.lua runs in the start menu. It
  adds the Multiplayer page. X4's UI Lua may not call Win32 and has no io library, so the player's choice reaches this
  launcher as a row of the game's log ([X4LAN] LOBBY REQUEST ...), and the launcher answers in lobby/state.lua of the
  installed extension, which the game reads with loadfile. The clipboard is read and written here, not in the game.
- Host: this launcher makes a token, listens for the other launchers (TCP, the relay's port number + 1) and puts an
  invite code on the clipboard (X4MP-<address>-<port>-<token>). Join: the launcher connects to the code's address,
  proves the token and that it holds the same overlay and snapshot, and gets its slot.
- When every slot of the overlay is taken, the host starts the relay for those addresses, picks the common load
  second and tells every launcher. Each one writes the session into its extension's config.lua (relay, token, slot,
  arm time) and tells its game, which then runs the overlay's own files: from here on the session is x4ls play's.

The lobby decides who plays and when the load happens. It adds nothing to the session itself: the relay, the gate and
the overlay are the ones x4ls play --remote-slots and x4ls join use."""
import hashlib
import hmac
import json
import os
import re
import secrets
import select
import shutil
import socket
import subprocess
import threading
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path

from . import extension, game, instance, profile, session, steam

LOBBY_LUA = session.ROOT / 'tools/dev/native_gate_lobby.lua'
LOBBY_UI = 'ui/native_gate_lobby.lua'
CODE = re.compile(r'X4MP-((?:\d{1,3}\.){3}\d{1,3})-(\d{1,5})-([0-9a-f]{32})')


@dataclass
class LobbyOptions:
    overlay: Path
    snapshot: Path
    port: int = 47000          # the relay's UDP port on the host; the lobby listens on TCP port + 1
    bind: str = ''             # the host's address the other PCs reach (default: this PC's primary IPv4 address)
    lane: int = 1              # which local view and disposable profile this X4 uses (0: the first free one)
    load_wait: int = 30        # seconds from the last player's arrival to the common load
    width: int = 1280
    height: int = 720
    game: Path | None = None
    lanes: Path | None = None
    runs: Path = session.ROOT / '.local/runs'
    state: Path = session.ROOT / '.local/x4ls'
    steam_fix: bool = False
    timeout: int = 0
    close_when_over: bool = False
    stall_drop: float = 15
    lobby_timeout: int = 0     # seconds to wait in the menu (0: until X4 is closed)
    auto: str = ''             # test: 'host' or an invite code, taken as the menu's first choice (no click)
    show: bool = False         # test: the Multiplayer page opens by itself
    server_file: Path | None = None   # a kit's server.json: the page has Join only, the server (x4ls serve) runs the relay


def invite(address, port, token):
    return f'X4MP-{address}-{port}-{token}'


def parse_invite(text):
    m = CODE.search(text or '')
    if not m or not session.IPV4.fullmatch(m.group(1)) or not 0 < int(m.group(2)) < 65535:
        return None
    return m.group(1), int(m.group(2)), m.group(3)


def local_address():
    """This PC's primary IPv4 address (the one a route to the internet uses; nothing is sent)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(('10.255.255.255', 1))
            return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'


def kit_id(overlay, snapshot_sha):
    """What every PC must hold alike: the overlay's manifest and the snapshot the overlay was built for."""
    manifest = (Path(overlay) / 'x4lane-manifest.json').read_bytes()
    return hashlib.sha256(manifest + b'|' + str(snapshot_sha).encode()).hexdigest()


def overlay_files(overlay):
    return [line.strip() for line in (Path(overlay) / 'ui.xml.files').read_text().splitlines() if line.strip()]


def lua_list(names):
    return '{' + ','.join("'" + n.replace('\\', '/').replace("'", '') + "'" for n in names) + '}'


def lobby_config(files, show=False, server=False):
    """config.lua while the game waits in the menu: nothing of a session, the overlay's files only named."""
    return (f'X4LAN_CONFIG = {{enabled = false}}\nX4LAN_CONFIG.lobby = {{files = {lua_list(files)}'
            + (', show = true' if show else '') + (', server = true' if server else '') + '}\n')


def session_config(overlay, files, slot, port, token, clients, profile_id, arm_at, relay_host):
    """config.lua of the session: what x4ls play/join install for this client, and the lobby's mark that it is on."""
    return (extension.config_text(overlay, slot, port, token, clients, profile_id=profile_id,
                                  config_tail=session.client_tail(arm_at, relay_host=relay_host))
            + f'X4LAN_CONFIG.lobby = {{files = {lua_list(files)}, session = true}}\n')


def lua_string(text):
    text = str(text).replace('\\', '\\\\').replace("'", "\\'").replace('\r', ' ').replace('\n', ' ')
    return "'" + text + "'"


def lua_value(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, int):
        return str(value)
    if isinstance(value, dict):
        return '{' + ', '.join(f'{k} = {lua_value(v)}' for k, v in value.items() if re.fullmatch(r'[A-Za-z_]\w*', str(k))) + '}'
    if isinstance(value, (list, tuple)):
        return '{' + ', '.join(lua_value(v) for v in value) + '}'
    return lua_string(value)


def state_text(stamp, phase, lines, **fields):
    """lobby/state.lua: a Lua file that returns what the page shows (the game reads it with loadfile)."""
    rows = [f'stamp = {int(stamp)}', f'phase = {lua_string(phase)}']
    rows += [f'{k} = {lua_value(v)}' for k, v in fields.items() if v is not None]
    rows.append('lines = {' + ', '.join(lua_string(t) for t in lines) + '}')
    return 'return {' + ', '.join(rows) + '}\n'


def clipboard_get():
    """The clipboard's text ('' when there is none). The launcher does this: the game's Lua may not call Win32."""
    if os.name != 'nt':
        return ''
    import ctypes
    user, kernel = ctypes.WinDLL('user32'), ctypes.WinDLL('kernel32')
    user.OpenClipboard.argtypes, user.GetClipboardData.argtypes = [ctypes.c_void_p], [ctypes.c_uint]
    user.GetClipboardData.restype = kernel.GlobalLock.restype = ctypes.c_void_p
    kernel.GlobalLock.argtypes = kernel.GlobalUnlock.argtypes = [ctypes.c_void_p]
    for _ in range(10):   # another program may hold it for a moment
        if user.OpenClipboard(None):
            break
        time.sleep(0.02)
    else:
        return ''
    try:
        handle = user.GetClipboardData(13)   # CF_UNICODETEXT
        pointer = kernel.GlobalLock(handle) if handle else None
        if not pointer:
            return ''
        try:
            return ctypes.wstring_at(pointer)
        finally:
            kernel.GlobalUnlock(handle)
    finally:
        user.CloseClipboard()


def clipboard_set(text):
    if os.name != 'nt':
        return False
    import ctypes
    user, kernel = ctypes.WinDLL('user32'), ctypes.WinDLL('kernel32')
    user.OpenClipboard.argtypes = [ctypes.c_void_p]
    user.SetClipboardData.argtypes, user.SetClipboardData.restype = [ctypes.c_uint, ctypes.c_void_p], ctypes.c_void_p
    kernel.GlobalAlloc.argtypes, kernel.GlobalAlloc.restype = [ctypes.c_uint, ctypes.c_size_t], ctypes.c_void_p
    kernel.GlobalLock.argtypes, kernel.GlobalLock.restype = [ctypes.c_void_p], ctypes.c_void_p
    kernel.GlobalUnlock.argtypes = kernel.GlobalFree.argtypes = [ctypes.c_void_p]
    data = (text + '\0').encode('utf-16-le')
    for _ in range(10):
        if user.OpenClipboard(None):
            break
        time.sleep(0.02)
    else:
        return False
    try:
        handle = kernel.GlobalAlloc(2, len(data))   # GMEM_MOVEABLE
        pointer = kernel.GlobalLock(handle) if handle else None
        if not pointer:
            return False
        ctypes.memmove(pointer, data, len(data))
        kernel.GlobalUnlock(handle)
        user.EmptyClipboard()
        if not user.SetClipboardData(13, handle):
            kernel.GlobalFree(handle)
            return False
        return True
    finally:
        user.CloseClipboard()


def write_atomic(path, text):
    tmp = Path(str(path) + '.new')
    tmp.write_text(text, encoding='utf-8')
    for _ in range(50):   # the game may hold the file open for a read
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.02)
    os.replace(tmp, path)


def install_lobby(overlay, extensions, clients, profile_id, show=False, server=False):
    """The overlay as x4ls installs it, in lobby form: ui.xml runs config.lua, sha256.lua and the lobby file only."""
    target = extension.compose(overlay, extensions, 1, 0, '0' * 32, clients, extra={LOBBY_UI: LOBBY_LUA}, profile_id=profile_id)
    files = overlay_files(overlay)
    (target / 'ui.xml').write_text(extension.ui_xml([*extension.BASE_UI, LOBBY_UI]))
    (target / 'ui/config.lua').write_text(lobby_config(files, show, server))
    (target / 'lobby').mkdir()
    return target, files


class Request:
    """The game's choices, in order: the rows '[X4LAN] LOBBY REQUEST <text>' of its log, each one once."""
    ROW = re.compile(rb'\[X4LAN\] LOBBY REQUEST ([^\r\n]*)')

    def __init__(self, path):
        self.path, self.offset, self.tail, self.queue = Path(path), 0, b'', []

    def poll(self):
        try:
            with self.path.open('rb') as f:
                f.seek(self.offset)
                data = f.read()
        except OSError:
            data = b''
        self.offset += len(data)
        *rows, self.tail = (self.tail + data).split(b'\n')   # a row counts once its line is complete
        for row in rows:
            m = self.ROW.search(row)
            if m:
                self.queue.append(m.group(1).decode('utf-8', 'replace').strip())
        return self.queue.pop(0) if self.queue else None


def key_proof(key, nonce, kit):
    """A joiner's answer to a server's nonce: it holds the kit's server key (which never crosses the wire)."""
    return hmac.new(bytes.fromhex(key), f'join|{nonce}|{kit}'.encode(), hashlib.sha256).hexdigest()


def session_token(key, salt):
    """The relay token of one session on a server: the server sends the salt, both sides derive the token."""
    return hmac.new(bytes.fromhex(key), f'token|{salt}'.encode(), hashlib.sha256).hexdigest()[:32]


def read_server_file(path):
    """server.json of a kit (tools/make_playtest_kit.py --server): (address, relay port, key)."""
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    host, port, key = str(data.get('host', '')), int(data.get('port', 0)), str(data.get('key', ''))
    if not session.IPV4.fullmatch(host) or not 0 < port < 65535 or not re.fullmatch(r'[0-9a-f]{64}', key):
        raise SystemExit(f'{path}: needs "host" (an IPv4 address), "port" and "key" (64 hex digits)')
    return host, port, key


def send(sock, message):
    sock.sendall(json.dumps(message).encode() + b'\n')


class Lines:
    """A peer's JSON lines over a socket, without blocking."""
    def __init__(self, sock):
        self.sock, self.buffer, self.closed, self.ready = sock, b'', False, []

    def poll(self):
        try:
            while select.select([self.sock], [], [], 0)[0]:
                data = self.sock.recv(4096)
                if not data:
                    self.closed = True
                    break
                self.buffer += data
                if len(self.buffer) > 65536:
                    self.closed = True
                    break
        except OSError:
            self.closed = True
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            try:
                self.ready.append(json.loads(line))
            except ValueError:
                self.closed = True
        out, self.ready = self.ready, []
        return out

    def wait(self, timeout):
        """The next message, or None when the peer closed or nothing came in time. Later ones stay for poll()."""
        end = time.time() + timeout
        while True:
            self.ready = self.poll()
            if self.ready:
                return self.ready.pop(0)
            if self.closed or time.time() > end:
                return None
            time.sleep(0.02)


UPLOAD_LIMIT = 300 * 1024 * 1024   # one launcher's packed logs
JOIN_FILE_LIMIT = 300 * 1024 * 1024   # one join file (the save is about 190 MB)


def pack_logs(run_dir, archive):
    """A run folder's logs as one zip (no saves: they are large and the snapshot is the kit's)."""
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(Path(run_dir).rglob('*')):
            if path.is_file() and not path.name.endswith(('.xml.gz', '.xml')) and path.stat().st_size <= UPLOAD_LIMIT:
                z.write(path, path.relative_to(run_dir).as_posix())
    return Path(archive).stat().st_size


def send_file(server, kit, path, game, slot, name=None):
    """One file to the kit's server (x4ls serve): a launcher's packed logs, or with `name` a join file (j<T>.x4join,
    j<T>.xml.gz) a saver hands over for a joiner. Returns the name the server stored it under."""
    address, port, key = server
    size = Path(path).stat().st_size
    with socket.create_connection((address, port + 1), timeout=10) as sock:
        sock.settimeout(120)
        lines = Lines(sock)
        nonce = str((lines.wait(10) or {}).get('nonce', ''))
        send(sock, {'upload': {'game': game or '', 'slot': int(slot or 0), 'size': size, **({'file': name} if name else {})},
                    'kit': kit, 'proof': key_proof(key, nonce, kit)})
        answer = lines.wait(10)
        if not isinstance(answer, dict) or not answer.get('ok'):
            raise OSError('the server did not take the file: ' + str((answer or {}).get('why', 'no answer')))
        with Path(path).open('rb') as f:
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                sock.sendall(chunk)
        answer = lines.wait(300)
        if not isinstance(answer, dict) or not answer.get('ok'):
            raise OSError('the server did not confirm the file')
    return answer.get('stored')


def send_logs(server, kit, run_dir, game, slot, log=print):
    """After the game: this launcher's run folder goes to the kit's server (x4ls serve keeps it with the game's relay files)."""
    archive = Path(run_dir).parent / (Path(run_dir).name + '.zip')
    size = pack_logs(run_dir, archive)
    stored = send_file(server, kit, archive, game, slot)
    archive.unlink()
    log(f'lobby: logs sent to the server ({size // 1024} KiB, {stored})')
    return stored


def ask_server(server, kit, what):
    """One question to the kit's server: 'status' (is a game running, who is there, which starts are free) or 'join'
    (admit this PC to the running game: the answer's 'running' has the session's salt). The answer, or None."""
    address, port, key = server
    try:
        with socket.create_connection((address, port + 1), timeout=6) as sock:
            sock.settimeout(6)
            lines = Lines(sock)
            nonce = str((lines.wait(6) or {}).get('nonce', ''))
            send(sock, {'kit': kit, 'proof': key_proof(key, nonce, kit), **({'status': True} if what == 'status' else {'late': True})})
            return lines.wait(6)
    except OSError:
        return None


def join_running(overlay, server_file, slot=None, start=None, log=print, **options):
    """x4ls join --server-file: join the game that runs on the kit's server in an open slot (by number or by start).
    The server admits this PC and names the session; the rest is hotjoin.hot_join against the server's relay, and the
    run folder goes to the server at the end, as after any game."""
    from . import hotjoin
    overlay = Path(overlay).resolve()   # hot_join builds the joiner's overlay from the checkout's folder, not from here
    recipe = json.loads((Path(overlay) / 'play-recipe.json').read_text(encoding='utf-8'))
    kit, server = kit_id(overlay, recipe.get('snapshot_sha')), read_server_file(server_file)
    answer = ask_server(server, kit, 'late')
    if not isinstance(answer, dict):
        raise SystemExit(f'the server at {server[0]}:{server[1] + 1} does not answer')
    if not answer.get('ok') or not isinstance(answer.get('running'), dict):
        raise SystemExit('the server refused: ' + str(answer.get('why', 'no game is running')))
    salt = str(answer['running']['salt'])
    o = hotjoin.HotJoinOptions(relay=f'{server[0]}:{server[1]}', token=session_token(server[2], salt), slot=slot, start=start,
                               overlay=Path(overlay), offer_wait=780, **options)
    before = set(o.runs.glob('*-hotjoin*')) if o.runs.is_dir() else set()
    try:
        return hotjoin.hot_join(o, log)
    finally:
        for folder in sorted(set(o.runs.glob('*-hotjoin*')) - before):
            try:
                send_logs(server, kit, folder, salt, o.slot or 0, log)
            except Exception as error:   # the game is over either way; the folder stays here
                log(f'lobby: logs not sent to the server ({error}); they are in {folder}')


def hand_over_joins(lines, server, kit, save_dir, game, slot, log=print, wait_s=90):
    """A saver's launcher during a drop-in game (its own thread): the server names a join the relay had this slot save
    ({'send_join': {'tick': T}}), and the two files X4 wrote for it go to the server, which serves them to the joiner.
    Ends when the server closes the connection."""
    while not lines.closed:
        for message in lines.poll():
            wanted = message.get('send_join') if isinstance(message, dict) else None
            if not isinstance(wanted, dict):
                continue
            try:
                tick = int(wanted.get('tick', 0))
                files = [Path(save_dir) / 'mpjoin' / f'j{tick}.x4join', Path(save_dir) / 'mpjoin' / f'j{tick}.xml.gz']
                deadline = time.time() + wait_s
                while not all(p.is_file() for p in files) and time.time() < deadline:
                    time.sleep(0.5)
                started = time.time()
                for path in files:
                    send_file(server, kit, path, game, slot, name=path.name)
                log(f'lobby: join files of tick {tick} handed to the server ({sum(p.stat().st_size for p in files) >> 20} MB '
                    f'in {time.time() - started:.0f} s)')
            except (OSError, ValueError) as error:
                log(f'lobby: join files not handed over: {error}')
        time.sleep(0.3)


def receive_upload(sock, first, size, path, log, done=None):
    """The server's side of send_file, in its own thread: `size` bytes into `path`; then done(path)."""
    part = path.with_name(path.name + '.part')
    try:
        sock.settimeout(60)
        send(sock, {'ok': True})
        path.parent.mkdir(parents=True, exist_ok=True)
        got, data = 0, first[:size]
        with part.open('wb') as f:
            while True:
                f.write(data)
                got += len(data)
                if got >= size:
                    break
                data = sock.recv(min(65536, size - got))
                if not data:
                    raise OSError(f'the connection closed after {got} of {size} bytes')
        os.replace(part, path)
        send(sock, {'ok': True, 'stored': path.name})
        log(f'lobby: {"join file" if done else "logs"} stored: {path} ({size // 1024} KiB)')
        if done:
            done(path)
    except OSError as error:
        log(f'lobby: logs for {path.name} not stored: {error}')
        part.unlink(missing_ok=True)
    finally:
        sock.close()


class Host:
    """The admissions: the listening socket, the players who joined, the start.

    Two forms. A player's launcher hosts (x4ls lobby, Host a game): a joiner shows the invite code's token and takes
    one of the slots 2..N; slot 1 is the host's own game. A server hosts (x4ls serve, `key`): every slot is a joiner's,
    and a joiner proves the kit's server key by answering the nonce it is sent."""
    def __init__(self, address, port, token, clients, kit, log, key=None):
        self.address, self.port, self.token, self.clients, self.kit, self.log, self.key = address, port, token, clients, kit, log, key
        self.slots = range(1 if key else 2, clients + 1)
        self.busy = None   # a server whose game is running: what every joiner is told
        self.upload_dir = None   # a server: game id -> the folder a launcher's logs go to (x4ls serve sets it)
        self.join_dir = None     # a drop-in server: game id -> the folder a saver's join files go to, or None
        self.join_done = None    # ... and what is called with each stored join file
        self.status = None       # a server: () -> what a launcher's 'status' question is answered with
        self.running = None      # a drop-in server while its game runs: (address) -> the 'running' answer for a late joiner
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((address, port + 1))
        self.server.listen(8)
        self.server.setblocking(False)
        self.pending, self.players, self.told = [], {}, None   # [(Lines, ip, since, nonce)], {slot: (Lines, ip)}

    def release(self):
        """Let the players go (their sockets close); the listening socket stays."""
        for lines, *_ in [*self.pending, *self.players.values()]:
            try:
                lines.sock.close()
            except OSError:
                pass
        self.pending, self.players, self.told = [], {}, None

    def close(self):
        self.release()
        self.server.close()

    def here(self):
        return len(self.players) + (0 if self.key else 1)

    def refusal(self, hello, nonce):
        if not isinstance(hello, dict):
            return 'not a lobby message'
        if self.key:
            if not hmac.compare_digest(str(hello.get('proof', '')), key_proof(self.key, nonce, str(hello.get('kit', '')))):
                return 'this build is not the one the server runs (another key)'
        elif not hmac.compare_digest(str(hello.get('token', '')), self.token):
            return 'the invite code is not this game\'s'
        if hello.get('kit') != self.kit:
            return 'the overlay or the snapshot differs from the host\'s (copy the host\'s kit again)'
        if hello.get('ignore_full'):
            return None
        if self.busy:
            return self.busy
        if not [k for k in self.slots if k not in self.players]:
            return 'the game is full'
        return None

    def poll(self):
        """Accept and admit; True when every slot is taken."""
        while select.select([self.server], [], [], 0)[0]:
            try:
                sock, peer = self.server.accept()
            except OSError:
                break
            sock.setblocking(True)
            sock.settimeout(5)
            nonce = secrets.token_hex(16) if self.key else None
            try:
                if nonce:
                    send(sock, {'nonce': nonce})
            except OSError:
                sock.close()
                continue
            self.pending.append((Lines(sock), peer[0], time.time(), nonce))
        for entry in list(self.pending):
            lines, ip, since, nonce = entry
            for hello in lines.poll():
                self.pending.remove(entry)
                if self.key and isinstance(hello, dict) and 'upload' in hello:   # a launcher's logs after its game
                    self.take_upload(lines, ip, nonce, hello)
                    break
                # a question; or, while a drop-in game runs, anyone who comes: the game is joined, not started again
                if self.key and isinstance(hello, dict) and ('status' in hello or 'late' in hello or self.running):
                    self.answer_question(lines, ip, nonce, hello)
                    break
                why = self.refusal(hello, nonce)
                try:
                    if why:
                        send(lines.sock, {'ok': False, 'why': why})
                        lines.sock.close()
                        self.log(f'lobby: refused {ip}: {why}')
                    else:
                        slot = [k for k in self.slots if k not in self.players][0]
                        send(lines.sock, {'ok': True, 'slot': slot, 'clients': self.clients})
                        self.players[slot] = (lines, ip)
                        self.log(f'lobby: player {slot} joined from {ip}')
                except OSError:
                    pass
                break
            else:
                if lines.closed or time.time() - since > 10:
                    self.pending.remove(entry)
                    lines.sock.close()
        for slot, (lines, ip) in list(self.players.items()):
            lines.poll()
            if lines.closed:
                del self.players[slot]
                lines.sock.close()
                self.log(f'lobby: player {slot} ({ip}) left')
        if self.here() != self.told:   # every player's page says how many are there
            self.told = self.here()
            for lines, _ in self.players.values():
                try:
                    send(lines.sock, {'here': self.told, 'clients': self.clients})
                except OSError:
                    lines.closed = True
        return len(self.players) == len(self.slots)

    def answer_question(self, lines, ip, nonce, hello):
        """'status' (anyone with the key and the kit) or 'late' (a player for the running game): one answer, then closed."""
        why = self.refusal({**hello, 'ignore_full': True}, nonce)
        try:
            if why and why not in (self.busy, 'the game is full'):
                send(lines.sock, {'ok': False, 'why': why})
            elif 'status' in hello:
                send(lines.sock, {'ok': True, 'status': self.status() if self.status else {}})
            elif not self.running:
                send(lines.sock, {'ok': False, 'why': 'no game is running on the server that can be joined'})
            else:
                send(lines.sock, {'ok': True, 'running': self.running(ip)})
                self.log(f'lobby: {ip} admitted to the running game')
        except OSError:
            pass
        lines.sock.close()

    def take_upload(self, lines, ip, nonce, hello):
        meta = hello['upload'] if isinstance(hello['upload'], dict) else {}
        try:
            size, slot = int(meta.get('size', -1)), int(meta.get('slot', 0))
        except (TypeError, ValueError):
            size, slot = -1, 0
        game = re.sub(r'[^0-9a-f]', '', str(meta.get('game', '')))[:64]
        why = None
        if not hmac.compare_digest(str(hello.get('proof', '')), key_proof(self.key, nonce, str(hello.get('kit', '')))):
            why = 'this build is not the one the server runs (another key)'
        join_file = str(meta.get('file', ''))
        if not why and not join_file and (not self.upload_dir or not 0 < size <= UPLOAD_LIMIT or not 0 <= slot <= 8):
            why = 'the logs are not taken (too large, or this server keeps none)'
        if not why and join_file:   # a saver's join file: only while a drop-in game runs, only the two names of a join
            folder = self.join_dir(game) if self.join_dir else None
            if not folder or not re.fullmatch(r'j[0-9]{1,7}[.](?:xml[.]gz|x4join)', join_file) or not 0 < size <= JOIN_FILE_LIMIT:
                why = 'no join file is expected here'
        if why:
            try:
                send(lines.sock, {'ok': False, 'why': why})
            except OSError:
                pass
            lines.sock.close()
            self.log(f'lobby: logs from {ip} refused: {why}')
            return
        if join_file:
            threading.Thread(target=receive_upload, daemon=True,
                             args=(lines.sock, lines.buffer, size, Path(folder) / join_file, self.log, self.join_done)).start()
            return
        name = f'{time.strftime("%Y%m%d-%H%M%S")}-player{slot}-{ip.replace(".", "_")}-{secrets.token_hex(2)}.zip'
        threading.Thread(target=receive_upload, args=(lines.sock, lines.buffer, size, Path(self.upload_dir(game)) / name, self.log),
                         daemon=True).start()

    def start(self, arm_at, salt=None, drop_in=False):
        """Tell every player the load second (and, on a server, the salt of the session's token); their addresses.
        drop_in: the game takes more players while it runs, and these launchers stay connected as its savers."""
        for slot, (lines, _) in self.players.items():
            start = {'host': self.address, 'port': self.port, 'arm_at': arm_at, 'slot': slot}
            if salt:
                start['salt'] = salt
            if drop_in:
                start['drop_in'] = True
            try:
                send(lines.sock, {'start': start})
            except OSError:
                pass
        return tuple(dict.fromkeys(ip for _, ip in self.players.values()))


def pid_alive(pid):
    if os.name != 'nt':
        try:
            os.kill(int(pid), 0)
            return True
        except (OSError, ValueError):
            return False
    import ctypes
    kernel = ctypes.WinDLL('kernel32')
    kernel.OpenProcess.restype, kernel.OpenProcess.argtypes = ctypes.c_void_p, [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    kernel.GetExitCodeProcess.argtypes, kernel.CloseHandle.argtypes = [ctypes.c_void_p, ctypes.c_void_p], [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x1000, 0, int(pid))   # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong()
    known = kernel.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel.CloseHandle(handle)
    return bool(known) and code.value == 259   # STILL_ACTIVE


def lane_in_use(state, lane, log=print):
    """True while a lobby of this lane is running. What an ended launcher left behind (a crash, a closed console) is
    cleared: its view, its staged snapshot and its record, so the next start works without anyone deleting files."""
    state_file = Path(state) / f'lobby-{lane}.json'
    if not state_file.exists():
        return False
    try:
        record = json.loads(state_file.read_text())
    except ValueError:
        record = {}
    if any(pid_alive(pid) for pid in [*record.get('pids', []), record.get('launcher')] if pid):
        return True
    for view in record.get('views', []):
        if Path(view).exists():
            game.remove_view(view)
    profile.unstage(profile.PROFILES[lane - 1])
    state_file.unlink(missing_ok=True)
    log(f'lobby: cleared what an earlier start left on lane {lane}')
    return False


def free_lane(state, lanes, log=print):
    """The first lane with no running lobby and no game view of another tool or folder."""
    for lane in range(1, len(profile.PROFILES) + 1):
        if not lane_in_use(state, lane, log) and not (Path(lanes) / f'c{lane}').exists():
            return lane
    raise SystemExit('every lane is in use: close an X4 window that was started for multiplayer')


def run(o: LobbyOptions, log=print):
    recipe = json.loads((Path(o.overlay) / 'play-recipe.json').read_text(encoding='utf-8'))
    clients = int(recipe['players'])
    if recipe.get('open') and not o.server_file:   # with a server the open slots are joined while the game runs
        raise SystemExit('x4ls lobby takes an overlay without open slots (every player is there at the start)')
    if not 0 <= o.lane <= len(profile.PROFILES):
        raise SystemExit(f'--lane must be 1..{len(profile.PROFILES)} (0: the first free one)')
    ok, message = steam.status()
    if not ok and not o.steam_fix:
        raise SystemExit(message)
    extension.verify_overlay(o.overlay)
    install = Path(o.game) if o.game else game.find_install()
    game.verify_exe(install)
    lookahead, tick_hz, last = session.play_settings(o.overlay, clients)
    if not last or not lookahead:
        raise SystemExit('the overlay has no pilot window or no lookahead: build it with tools/build_play_overlay.py')
    lanes = Path(o.lanes) if o.lanes else Path(os.path.splitdrive(str(install))[0] + '\\x4ls-lanes')
    if not o.lane:
        o.lane = free_lane(o.state, lanes, log)
    elif lane_in_use(o.state, o.lane, log):
        raise SystemExit(f'lane {o.lane} is in use: its X4 window is still open')
    state_file = o.state / f'lobby-{o.lane}.json'
    run_dir = o.runs / (time.strftime('%Y%m%d-%H%M%S') + f'-lobby{o.lane}')
    run_dir.mkdir(parents=True)
    printer = log

    def log(text):   # the launcher's own lines are part of the run folder (and of what a server gets)
        printer(text)
        try:
            with (run_dir / 'launcher.log').open('a', encoding='utf-8') as f:
                f.write(f'{time.strftime("%H:%M:%S")} {text}\n')
        except OSError:
            pass
    view, profile_id = lanes / f'c{o.lane}', profile.PROFILES[o.lane - 1]
    kit = kit_id(o.overlay, recipe.get('snapshot_sha'))
    server = read_server_file(o.server_file) if o.server_file else None   # (address, port, key)
    play = session.Options(clients=clients, overlay=o.overlay, snapshot=o.snapshot, ticks=last + session.PLAY_TAIL_TICKS, tick_hz=tick_hz,
                           lookahead=lookahead, timeout=o.timeout, width=o.width, height=o.height, play=True,
                           stall_drop_ms=1000 * o.stall_drop, close_when_over=o.close_when_over, port=o.port)
    o.state.mkdir(parents=True, exist_ok=True)
    record = {'run': str(run_dir), 'views': [str(view)], 'pids': [], 'relay_pid': None, 'launcher': os.getpid()}
    state_file.write_text(json.dumps(record, indent=2) + '\n')
    proc = relay = relay_log = host = peer = None
    result = {'outcome': 'failed'}
    game_id, slot = '', 0   # a server's game: the salt of its token (the server files this launcher's logs under it)
    drop_in = False         # ... and whether it takes more players while it runs
    try:
        game.create_view(install, view)
        target, files = install_lobby(o.overlay, view / 'extensions', clients, profile_id, o.show, bool(server))
        box = target / 'lobby'

        shown = [0, None]   # the state's stamp and what was written last

        def show(phase, *lines, **fields):
            if (phase, lines, fields) != shown[1]:
                shown[:] = shown[0] + 1, (phase, lines, fields)
                write_atomic(box / 'state.lua', state_text(shown[0], phase, lines, **fields))

        show('starting', 'Starting...')
        profile.prepare(profile_id, [*game.dlc_ids(view), extension.EXTENSION_ID], o.width, o.height, o.snapshot)
        if o.steam_fix:
            steam.fix(o.state / 'steam-fix.json')
        proc = subprocess.Popen(session.client_command(view, profile_id, o.lane, session.snapshot_seed(o.snapshot)), cwd=view,
                                env=steam.client_environment())
        record['pids'].append(proc.pid)
        state_file.write_text(json.dumps(record, indent=2) + '\n')
        log(f'lobby: X4 pid {proc.pid}, profile {profile_id}, view {view}; choose Multiplayer in its main menu')
        session.wait_for_log(profile.profile_dir(profile_id) / f'x4ls-c{o.lane}.log', 'Entering startmenu', proc, 240)
        log(f'lobby: released {instance.release_instance_mutex(proc.pid)} instance mutex handle(s)')
        idle = (f'{clients} players, {recipe.get("mode", "coop")}, {recipe.get("minutes", "?"):g} minutes',
                f'Server {server[0]}. Choose Join: the game loads when {clients} players are here.' if server
                else 'Host a game, or copy the host\'s invite code and join.')
        show('idle', *idle)
        game_log = profile.profile_dir(profile_id) / f'x4ls-c{o.lane}.log'
        requests, started, chosen = Request(game_log), time.time(), None   # chosen: (slot, token, port, arm_at, relay_host)
        note, typed = '', True   # typed: the choice came from the menu (the clipboard is the player's to use)
        while chosen is None:
            time.sleep(0.2)
            if proc.poll() is not None:
                result = {'outcome': 'closed', 'ended': 'X4 was closed in the menu'}
                return result
            if o.lobby_timeout and time.time() - started > o.lobby_timeout:
                result = {'outcome': 'failed', 'ended': f'nobody chose within {o.lobby_timeout} s'}
                return result
            text, typed = requests.poll(), True
            if o.auto and not text and not host and not peer:
                text, typed = ('HOST' if o.auto == 'host' else 'JOIN' if o.auto == 'join' else 'JOIN ' + o.auto), False
                o.auto = ''
                log(f'lobby: --auto {text.split()[0].lower()}')
            if text == 'JOIN' and not server:   # the menu's Join: the code is on the clipboard
                text = 'JOIN ' + clipboard_get()[:400]
            if text == 'CANCEL':
                if host:
                    host.close()
                    host = None
                if peer:
                    peer[0].sock.close()
                    peer = None
                show('idle', *idle)
                log('lobby: cancelled')
            elif text == 'COPY' and host:
                clipboard_set(note)
            elif text == 'HOST' and not host and not peer and not server:
                address, token = o.bind or local_address(), secrets.token_hex(16)
                try:
                    host = Host(address, o.port, token, clients, kit, log)
                except OSError as error:
                    show('idle', *idle, f'Cannot host on {address}:{o.port + 1}: {error}')
                    continue
                note = invite(address, o.port, token)
                if typed:
                    clipboard_set(note)
                log(f'lobby: hosting on {address}, UDP {o.port} (the session) and TCP {o.port + 1} (the lobby); invite code:\n  {note}')
            elif text and text.startswith('JOIN') and not host and not peer:
                code = (server[0], server[1], None) if server else parse_invite(text[5:])
                if not code:
                    show('idle', *idle, "No invite code on the clipboard (it starts with X4MP-). Copy the host's code, then press Join.")
                    continue
                address, port, token = code
                show('joining', f'Joining {address}...')
                answer = None
                try:
                    sock = socket.create_connection((address, port + 1), timeout=8)
                    sock.settimeout(8)
                    lines = Lines(sock)
                    if server:   # the server sends a nonce first; the answer proves the kit's key
                        nonce = str((lines.wait(8) or {}).get('nonce', ''))
                        send(sock, {'kit': kit, 'proof': key_proof(server[2], nonce, kit)})
                    else:
                        send(sock, {'token': token, 'kit': kit})
                    answer = lines.wait(8)
                    if answer is None:
                        raise OSError('no answer')
                except OSError as error:
                    show('idle', *idle, f'Could not reach {"the server" if server else "the host"} at {address}:{port + 1} ({error}).')
                    log(f'lobby: could not join {address}:{port + 1}: {error}')
                    continue
                if isinstance(answer, dict) and answer.get('ok') and 'running' in answer:
                    sock.close()   # this X4 is in the menu for a common load; a running game is joined by x4ls join
                    show('idle', *idle, 'A game is running on the server. Close X4 and join it with its start picker',
                         '(Play in Steam, Multiplayer) or with x4ls join --server-file.')
                    log('lobby: a game is running on the server (join it with x4ls join --server-file)')
                    continue
                if not isinstance(answer, dict) or not answer.get('ok'):
                    sock.close()
                    why = str(answer.get('why', 'no reason given')) if isinstance(answer, dict) else 'no reason given'
                    show('idle', *idle, f'{"The server" if server else "The host"} refused: {why}')
                    log(f'lobby: refused: {why}')
                    continue
                peer = (lines, address, port, token, int(answer['slot']), int(answer['clients']))
                show('joined', f'Joined {address} as player {peer[4]} of {peer[5]}.', 'Waiting for the other players...')
                log(f'lobby: joined {address} as player {peer[4]}')
            if host:
                full = host.poll()
                names = ', '.join(f'player {k}' for k in sorted(host.players)) or 'nobody yet'
                show('hosting', f'Hosting on {host.address} (UDP {o.port}, TCP {o.port + 1}).',
                     f'Invite code (on the clipboard): {note}', f'Joined: {names} ({len(host.players) + 1} of {clients}).',
                     'The game loads when every player has joined.', invite=note)
                if full:
                    arm_at = int(time.time()) + o.load_wait
                    play.remote_slots, play.bind = tuple(range(2, clients + 1)), host.address
                    play.allow_from = host.start(arm_at)
                    relay_log = (run_dir / 'relay.log').open('w')
                    relay = subprocess.Popen(session.relay_command(play, o.port, host.token, run_dir), cwd=session.ROOT,
                                             stdout=relay_log, stderr=subprocess.STDOUT)
                    record['relay_pid'] = relay.pid
                    state_file.write_text(json.dumps(record, indent=2) + '\n')
                    chosen = (1, host.token, o.port, arm_at, host.address)
                    time.sleep(0.5)   # the start lines leave before the sockets close
                    host.close()
                    host = None
            elif peer:
                lines = peer[0]
                for message in lines.poll():
                    start = message.get('start') if isinstance(message, dict) else None
                    if start and server:   # the relay is the server's; the token comes from the kit's key and this salt
                        game_id = str(start['salt'])
                        drop_in = bool(start.get('drop_in'))
                        chosen = (int(start['slot']), session_token(server[2], str(start['salt'])), server[1],
                                  int(start['arm_at']), server[0])
                    elif start:
                        chosen = (int(start['slot']), peer[3], int(start['port']), int(start['arm_at']), str(start['host']))
                    elif isinstance(message, dict) and 'here' in message:
                        show('joined', f'Joined {peer[1]} as player {peer[4]} of {peer[5]}.',
                             f'{int(message["here"])} of {peer[5]} players are here. The game loads when everyone has joined.')
                if chosen is None and lines.closed:
                    peer = None
                    show('idle', *idle, 'The host closed the game.')
                    log('lobby: the host closed the game')
        slot, token, port, arm_at, relay_host = chosen
        if peer and drop_in:   # the server asks this launcher for the join files its X4 saves; the connection stays
            threading.Thread(target=hand_over_joins, daemon=True,
                             args=(peer[0], server, kit, profile.profile_dir(profile_id) / 'save', game_id, slot, log)).start()
        elif peer:
            peer[0].sock.close()
        text = session_config(o.overlay, files, slot, port, token, clients, profile_id, arm_at, relay_host)
        write_atomic(target / 'ui/config.lua', text)
        installed = run_dir / f'client{o.lane}' / 'installed'
        installed.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(target / 'ui/config.lua', installed / 'x4_lan_lab-config.lua')
        show('session', f'You are player {slot} of {clients}.', 'The game loads in a moment...', slot=slot, arm_at=arm_at)
        log(f'lobby: player {slot} of {clients}, relay {relay_host}:{port}, loads in {arm_at - int(time.time())} s')
        result = session.monitor_play(play, run_dir, relay, [(o.lane, profile_id, proc)], log)
        result.update(slot=slot, relay=f'{relay_host}:{port}')
    finally:
        if host:
            host.close()
        if proc and proc.poll() is None:
            session.kill(proc.pid)
        if relay and relay.poll() is None:
            session.kill(relay.pid)
        if relay_log:
            relay_log.close()
        time.sleep(2)
        profile.collect(profile_id, f'x4ls-c{o.lane}.log', run_dir / f'client{o.lane}')
        profile.unstage(profile_id)
        if drop_in:   # the join saves this game's X4 wrote for joiners (about 200 MB each) are the server's now
            from . import hotjoin
            hotjoin.remove_join_files({o.lane: profile.profile_dir(profile_id) / 'save'})
        game.remove_view(view)
        state_file.unlink(missing_ok=True)
        if o.steam_fix and (o.state / 'steam-fix.json').exists():
            steam.restore(o.state / 'steam-fix.json')
        result.update(overlay=str(o.overlay), snapshot=str(o.snapshot), run_dir=str(run_dir))
        (run_dir / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
        log(f"{result['outcome']}: {run_dir}")
        if server:   # every launcher's logs go to the kit's server, also after a failure
            try:
                send_logs(server, kit, run_dir, game_id, slot, log)
            except Exception as error:   # the game is over either way; the folder stays here
                log(f'lobby: logs not sent to the server ({error}); they are in {run_dir}')
    return result
