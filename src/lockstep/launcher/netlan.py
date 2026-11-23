"""The host's TCP lobby of a netplay game (the relay's port + 1, every interface) and the joiner's side of it.

One JSON line comes in on each connection (lobby.send / lobby.Lines, as x4ls lobby's admissions always had):
- a join of a game hosted without a master server: {'join': 1, 'release': R, 'kit': K or None, 'token': T or None}.
  A public game takes anyone of the same release; a private one only with the invite code's token. The answer
  names the slot, the relay token and the session files the host's file service offers (TCP, the relay's port);
  the joiner says {'ready': true} once it has built its overlay. The start goes to every player on the connection.
- a check-in of a master game's player in direct mode: {'checkin': GAME_ID, 'slot': K}, once, before the load. The
  address the host sees it from (public, LAN, VLAN or VPN) is what the relay allows (docs/NETPLAY.md "VLANs": who may
  send to a relay is learned, never claimed).
- an older launcher's join (x4ls lobby before netplay): {'token': T, 'kit': K}: a LAN game of the same playtest kit;
  it is ready at once and its start message has the fields it knows.
A join of a game the master holds is refused with the game's code, so the joiner can take the code path instead.
"""
from dataclasses import dataclass
import hmac
from pathlib import Path
import select
import socket
import time

from . import lobby, session

HELLO_WAIT_S = 10          # a connection that says nothing for this long is closed
CONNECT_TIMEOUT_S = 1.5    # each LAN candidate gets this long (NETPLAY.md "Same site, other VLAN")
BACKLOG = 16


class AllowList:
    """The relay's allowed sources: the addresses the host's lobby took connections from, in a set (the session file
    service reads it live) and in a file the relay reads again whenever it changes (--allow-file)."""

    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.addresses = {'127.0.0.1'}   # this PC's own game sends from the loopback address
        self.path.write_text('127.0.0.1\n', encoding='ascii')

    def add(self, address):
        if not session.IPV4.fullmatch(address) or address in self.addresses:
            return False
        self.addresses.add(address)
        with self.path.open('a', encoding='ascii') as out:
            out.write(address + '\n')
        return True


@dataclass
class Player:
    lines: object          # lobby.Lines of the joiner's connection
    ip: str                # the source address the host saw (the relay allows it)
    local: str             # this PC's address the joiner reached (the relay host for that joiner)
    ready: bool
    legacy: bool           # an older launcher: no ready message, the start in the fields it knows


class GameHost:
    """The host's TCP lobby. port: the relay's UDP port; the lobby listens on TCP port + 1 of every interface
    (NETPLAY.md: no primary interface)."""

    def __init__(self, port, clients, release, kit, allow, log=print, bind='0.0.0.0', public=False, token=''):
        self.port = port
        self.clients = clients
        self.release = release
        self.kit = kit
        self.allow = allow
        self.log = log
        self.public = public
        self.token = token
        self.game_id = None        # a master game: check-ins are taken for it, joins are sent to its code
        self.code = ''
        self.checkin_slots = set() # the master's joined slots that may check in
        self.checked_in = {}       # slot -> the address it checked in from
        self.files = {}            # the session files a LAN joiner downloads: name -> {size, sha256}
        self.file_port = 0
        self.players = {}          # slot -> Player
        self.pending = []          # (Lines, ip, local, since)
        self.told = None
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((bind, port + 1))
        self.server.listen(BACKLOG)
        self.server.setblocking(False)

    def close(self):
        for entry in [*self.pending, *self.players.values()]:
            lines = entry[0] if isinstance(entry, tuple) else entry.lines
            try:
                lines.sock.close()
            except OSError:
                pass
        self.pending = []
        self.players = {}
        try:
            self.server.close()
        except OSError:
            pass

    def free_slots(self):
        return [slot for slot in range(2, self.clients + 1) if slot not in self.players]

    def ready_slots(self):
        return sorted(slot for slot, player in self.players.items() if player.ready)

    def all_ready(self):
        return len(self.ready_slots()) == self.clients - 1

    def rows(self):
        """The players as the state's players list: the host first."""
        rows = [{'slot': 1, 'ready': True, 'host': True}]
        for slot in sorted(self.players):
            rows.append({'slot': slot, 'ready': self.players[slot].ready, 'host': False})
        return rows

    def poll(self):
        """Accept, answer and follow the players; call it often (it never blocks)."""
        self.accept()
        for entry in list(self.pending):
            self.read_hello(entry)
        for slot, player in list(self.players.items()):
            for message in player.lines.poll():
                if isinstance(message, dict) and message.get('ready'):
                    player.ready = True
            if player.lines.closed:
                del self.players[slot]
                player.lines.sock.close()
                self.log(f'netplay: player {slot} ({player.ip}) left')
        self.tell_here()

    def accept(self):
        while select.select([self.server], [], [], 0)[0]:
            try:
                sock, peer = self.server.accept()
            except OSError:
                return
            sock.setblocking(True)
            sock.settimeout(5)
            local = sock.getsockname()[0]
            self.pending.append((lobby.Lines(sock), peer[0], local, time.time()))

    def read_hello(self, entry):
        lines, ip, local, since = entry
        messages = lines.poll()
        if not messages:
            if lines.closed or time.time() - since > HELLO_WAIT_S:
                self.pending.remove(entry)
                lines.sock.close()
            return
        self.pending.remove(entry)
        hello = messages[0]
        if isinstance(hello, dict) and 'checkin' in hello:
            self.check_in(lines, ip, hello)
            return
        why = self.refusal(hello)
        if why:
            self.refuse(lines, ip, why)
            return
        self.admit(lines, ip, local, hello)

    def check_in(self, lines, ip, hello):
        """A master game's player says where it sends from: its address goes into the relay's allow list."""
        slot = hello.get('slot')
        if not self.game_id or hello.get('checkin') != self.game_id or slot not in self.checkin_slots:
            self.refuse(lines, ip, 'not a player of this game')
            return
        self.checked_in[slot] = ip
        if self.allow.add(ip):
            self.log(f'netplay: player {slot} checked in from {ip}')
        self.answer(lines, {'ok': True, 'checked_in': slot})
        lines.sock.close()

    def refusal(self, hello):
        if not isinstance(hello, dict):
            return 'not a lobby message'
        if self.game_id:   # the master holds this game: its slots are the master's
            return 'this game is joined with its code'
        legacy = 'release' not in hello
        token_ok = bool(self.token) and hmac.compare_digest(str(hello.get('token') or ''), self.token)
        if not self.public and not token_ok:
            return 'the invite code is not this game\'s'
        if legacy and hello.get('kit') != self.kit:
            return 'the overlay or the snapshot differs from the host\'s (copy the host\'s kit again)'
        if not legacy and str(hello.get('release')) != self.release:
            return f'this game runs release {self.release}, not {str(hello.get("release"))[:40]}'
        if not self.free_slots():
            return 'the game is full'
        return None

    def admit(self, lines, ip, local, hello):
        slot = self.free_slots()[0]
        legacy = 'release' not in hello
        answer = {'ok': True, 'slot': slot, 'clients': self.clients, 'token': self.token, 'kit': self.kit}
        if self.files and not legacy:
            answer['files'] = self.files
            answer['file_port'] = self.file_port
        if self.answer(lines, answer):
            self.players[slot] = Player(lines, ip, local, ready=legacy, legacy=legacy)
            self.allow.add(ip)
            self.log(f'netplay: player {slot} joined from {ip}')

    def refuse(self, lines, ip, why):
        message = {'ok': False, 'why': why}
        if self.game_id and self.code:
            message['code'] = self.code
        self.answer(lines, message)
        lines.sock.close()
        self.log(f'netplay: refused {ip}: {why}')

    def answer(self, lines, message):
        try:
            lobby.send(lines.sock, message)
            return True
        except OSError:
            lines.closed = True
            return False

    def tell_here(self):
        """Every player's page says how many are here and who is ready."""
        here = (len(self.players) + 1, tuple(self.ready_slots()))
        if here == self.told:
            return
        self.told = here
        for player in self.players.values():
            self.answer(player.lines, {'here': here[0], 'clients': self.clients, 'players': self.rows()})

    def start(self, arm_at, token):
        """Tell every player the load second; the joiners' source addresses (the relay allows them)."""
        for slot, player in self.players.items():
            start = {'host': player.local, 'port': self.port, 'arm_at': arm_at, 'slot': slot, 'token': token}
            self.answer(player.lines, {'start': start})
        return tuple(dict.fromkeys(player.ip for player in self.players.values()))


# ---- the joiner's side ----
def connect_first(addresses, timeout=CONNECT_TIMEOUT_S, log=print):
    """A TCP connection to the first of (host, port) that answers, in the order given (the host's LAN candidates on this
    PC's subnet first, then the others, then its public address). Returns (socket, the IPv4 address connected to, the
    port). Raises OSError naming every address tried."""
    tried = []
    for host, port in addresses:
        try:
            found = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)
        except (OSError, UnicodeError) as error:
            tried.append(f'{host}: {error}')
            continue
        for entry in found:
            address = entry[4][0]
            try:
                sock = socket.create_connection((address, port), timeout=timeout)
            except OSError as error:
                tried.append(f'{address}:{port}: {error}')
                continue
            log(f'netplay: reached {address}:{port}' + (f' ({host})' if host != address else ''))
            return sock, address, port
    raise OSError('no address answered: ' + '; '.join(tried[:6]))


def ask(sock, message, timeout):
    """Send one message and wait for one answer on the same connection; (Lines, the answer or None)."""
    sock.settimeout(timeout)
    lines = lobby.Lines(sock)
    lobby.send(sock, message)
    return lines, lines.wait(timeout)


def check_in(addresses, game_id, slot, timeout=CONNECT_TIMEOUT_S, log=print):
    """A master game's joiner in direct mode: check in at the host's lobby, at the first address that takes it. The
    address that worked is the one this PC's game sends to. None when none did."""
    for host, port in addresses:
        try:
            sock, address, _ = connect_first([(host, port)], timeout, log)
        except OSError as error:
            log(f'netplay: no check-in at {host}:{port}: {error}')
            continue
        with sock:
            try:
                _, answer = ask(sock, {'checkin': game_id, 'slot': slot}, timeout + 3)
            except OSError as error:
                log(f'netplay: no check-in at {address}:{port}: {error}')
                continue
        if isinstance(answer, dict) and answer.get('ok'):
            return address
        why = answer.get('why') if isinstance(answer, dict) else 'no answer'
        log(f'netplay: the host at {address}:{port} refused the check-in: {why}')
    return None
