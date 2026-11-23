"""LAN discovery without a server (docs/NETPLAY.md, "LAN discovery" and "VLANs, several networks and virtual LANs").

A host announces its game on UDP 47102 every 2 s:

    X4MPLAN|1|<release>|<code or ->|<name, url-encoded>|<tcp lobby port>|<players>/<max>|<candidate ip,ip,...>

Each announcement reaches every network the PC is on: for every interface address it is sent from that address to
the interface's directed broadcast (a /24's .255), to 255.255.255.255 and to the multicast group 239.255.71.2 with
IP_MULTICAST_IF set to the interface and TTL 8, so a site whose router forwards multicast between VLANs sees it too.
The candidates are all of the host's addresses (netifs.addresses()): a joiner tries the one on its own subnet first.

scan() listens on 0.0.0.0:47102 (SO_REUSEADDR: several launchers on one PC may browse at once), joins the group on
every interface, ignores malformed and oversized datagrams and keeps one entry per (sender, code), the newest.

The name is cut to NAME_BYTES_MAX bytes of UTF-8 (at a character boundary) before it is url-encoded, so '|' and ','
can never break the fields. Interfaces and sockets are injectable: tests run everything on loopback.
"""
import socket
import threading
import time
import urllib.parse

from . import netifs
from .nat import normalize_code

LAN_PORT = 47102
LAN_GROUP = '239.255.71.2'
ANNOUNCE_INTERVAL = 2.0
MULTICAST_TTL = 8
LIMITED_BROADCAST = '255.255.255.255'
PREFIX = 'X4MPLAN'
VERSION = '1'
FIELDS = 8
NAME_BYTES_MAX = 40
# Every byte of a cut name may be percent-encoded: three characters each.
NAME_FIELD_MAX = 3 * NAME_BYTES_MAX
RELEASE_MAX = 64
RELEASE_CHARACTERS = frozenset('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_+')
CANDIDATES_MAX = 16
PLAYERS_MAX = 8
PLAYERS_MAX_MIN = 2
# The largest valid announcement is about 480 bytes; anything above this is not ours.
DATAGRAM_MAX = 512
RECEIVE_BUFFER = 65535


def cut_name(name):
    """At most NAME_BYTES_MAX bytes of UTF-8, never half a character; control characters become spaces."""
    clean = ''.join(' ' if ord(character) < 32 or ord(character) == 127 else character for character in str(name))
    return clean.encode('utf-8')[:NAME_BYTES_MAX].decode('utf-8', 'ignore')


def release_valid(release):
    """A release string as release.json writes it (<12 hex>-<12 hex>), or any short token of the same characters."""
    if not isinstance(release, str) or not release or len(release) > RELEASE_MAX:
        return False
    return all(character in RELEASE_CHARACTERS for character in release)


def encode(release, code, name, tcp_port, players, players_max, candidates):
    """The announcement datagram (bytes). code None or '-' = a game without a join code."""
    if not release_valid(release):
        raise ValueError(f'not a release string: {release!r}')
    code_field = normalize_code(code) if code not in (None, '-') else '-'
    if code_field is None:
        raise ValueError(f'not a join code: {code!r}')
    name_field = urllib.parse.quote(cut_name(name), safe='')
    candidate_field = ','.join(list(candidates)[:CANDIDATES_MAX])
    fields = [PREFIX, VERSION, release, code_field, name_field, str(tcp_port), f'{players}/{players_max}',
              candidate_field]
    return '|'.join(fields).encode('ascii')


def _canonical_number(text, smallest, largest):
    if not text.isdigit() or not text.isascii() or str(int(text)) != text:
        return None
    value = int(text)
    return value if smallest <= value <= largest else None


def _candidates(text, sender):
    """The candidate list, or None if any entry is not a usable unicast IPv4 address.

    Loopback candidates count only from a loopback sender: another PC naming 127.0.0.1 would point a joiner's
    connect at the joiner itself."""
    if not text:
        return []
    entries = text.split(',')
    if len(entries) > CANDIDATES_MAX:
        return None
    sender_address = netifs.parse_ipv4(sender)
    sender_loopback = sender_address is not None and sender_address.is_loopback
    found = []
    for entry in entries:
        address = netifs.parse_ipv4(entry)
        if address is None or address.is_multicast or address.is_unspecified or entry == LIMITED_BROADCAST:
            return None
        if address.is_loopback and not sender_loopback:
            return None
        if entry not in found:
            found.append(entry)
    return found


def decode(data, sender):
    """The game an announcement describes: {ip (the sender), candidates, release, code (None for '-'), name,
    tcp_port, players, players_max}; None for anything malformed, oversized or of another version."""
    if len(data) > DATAGRAM_MAX:
        return None
    try:
        fields = data.decode('ascii').split('|')
    except UnicodeDecodeError:
        return None
    if len(fields) != FIELDS or fields[0] != PREFIX or fields[1] != VERSION:
        return None
    release, code_field, name_field, port_field, players_field, candidate_field = fields[2:]
    if not release_valid(release):
        return None
    code = None
    if code_field != '-':
        code = normalize_code(code_field)
        if code != code_field:
            return None
    if len(name_field) > NAME_FIELD_MAX:
        return None
    try:
        name = urllib.parse.unquote(name_field, errors='strict')
    except UnicodeDecodeError:
        return None
    tcp_port = _canonical_number(port_field, 1, 65535)
    players_text, separator, maximum_text = players_field.partition('/')
    players_max = _canonical_number(maximum_text, PLAYERS_MAX_MIN, PLAYERS_MAX) if separator else None
    players = _canonical_number(players_text, 0, players_max) if players_max else None
    candidates = _candidates(candidate_field, sender)
    if tcp_port is None or players is None or candidates is None:
        return None
    return {'ip': sender, 'candidates': candidates, 'release': release, 'code': code, 'name': cut_name(name),
            'tcp_port': tcp_port, 'players': players, 'players_max': players_max}


def destinations(entry, port, group=LAN_GROUP):
    """Where one interface's copy goes: its directed broadcast (if it has one), 255.255.255.255, the group."""
    targets = []
    if entry.get('broadcast'):
        targets.append((entry['broadcast'], port))
    targets.append((LIMITED_BROADCAST, port))
    targets.append((group, port))
    return targets


def _interface_source(interfaces):
    """A callable returning the current interface rows: netifs.addresses() by default (asked every round: a VPN
    may connect while the host waits), a fixed list, or a callable."""
    if interfaces is None:
        return netifs.addresses
    if callable(interfaces):
        return interfaces
    rows = list(interfaces)
    return lambda: rows


class Announcer:
    """Announces a hosted game on every network every ANNOUNCE_INTERVAL seconds until stop() (a daemon thread)."""

    def __init__(self, release, code, name, tcp_port, players, players_max, port=LAN_PORT, *, interfaces=None,
                 socket_factory=socket.socket, interval=ANNOUNCE_INTERVAL, group=LAN_GROUP, log=None):
        if not 1 <= int(tcp_port) <= 65535 or not PLAYERS_MAX_MIN <= int(players_max) <= PLAYERS_MAX:
            raise ValueError('tcp_port must be 1..65535 and players_max 2..8')
        self.release = release
        self.code = code
        self.name = name
        self.tcp_port = int(tcp_port)
        self.players = int(players)
        self.players_max = int(players_max)
        self.port = port
        self.group = group
        self.interval = interval
        self.sent = 0
        self.errors = 0
        self._interfaces = _interface_source(interfaces)
        self._socket_factory = socket_factory
        self._log = log
        self._logged = set()
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread = None
        encode(release, code, name, self.tcp_port, self.players, self.players_max, [])   # fail early on bad fields

    def update(self, players=None, code=None):
        """New player count or join code; the next announcement goes out at once."""
        with self._lock:
            if players is not None:
                self.players = int(players)
            if code is not None:
                encode(self.release, code, self.name, self.tcp_port, self.players, self.players_max, [])
                self.code = code
        self._wake.set()

    def datagram(self, interfaces):
        with self._lock:
            candidates = [entry['ip'] for entry in interfaces]
            return encode(self.release, self.code, self.name, self.tcp_port, self.players, self.players_max,
                          candidates)

    def _note(self, key, text):
        """Logs each kind of failure once: a missing network would otherwise log every 2 s."""
        if self._log is not None and key not in self._logged:
            self._logged.add(key)
            self._log(text)

    def _open_sender(self, source):
        sock = self._socket_factory(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, MULTICAST_TTL)
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(source))
            sock.bind((source, 0))
        except OSError:
            sock.close()
            raise
        return sock

    def announce(self):
        """One round: every interface, every destination. Returns the number of datagrams sent."""
        try:
            interfaces = list(self._interfaces())
        except OSError as error:
            self._note('interfaces', f'LAN discovery: no interface list: {error}')
            interfaces = []
        data = self.datagram(interfaces)
        sent = 0
        for entry in interfaces:
            source = entry['ip']
            try:
                sock = self._open_sender(source)
            except OSError as error:
                self.errors += 1
                self._note(('open', source), f'LAN discovery: cannot send from {source}: {error}')
                continue
            with sock:
                for destination in destinations(entry, self.port, self.group):
                    try:
                        sock.sendto(data, destination)
                        sent += 1
                    except OSError as error:
                        self.errors += 1
                        self._note(('send', source, destination[0]),
                                   f'LAN discovery: {source} -> {destination[0]} failed: {error}')
        self.sent += sent
        return sent

    def _run(self):
        while not self._stop.is_set():
            self.announce()
            self._wake.wait(self.interval)
            self._wake.clear()

    def start(self):
        if self._thread is None:
            self._thread = threading.Thread(target=self._run, name='x4mp-lan-announce', daemon=True)
            self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        self._wake.set()
        if self._thread is not None:
            self._thread.join(self.interval + 1)
            self._thread = None


def open_listener(port=LAN_PORT, interfaces=None, bind='0.0.0.0', group=LAN_GROUP):
    """The discovery socket: bound with SO_REUSEADDR, a member of the group on every interface (or, with none, on
    the default one). A join that fails (an interface without multicast) is skipped."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((bind, port))
    except OSError:
        sock.close()
        raise
    rows = _interface_source(interfaces)()
    joined = 0
    for entry in rows:
        membership = socket.inet_aton(group) + socket.inet_aton(entry['ip'])
        try:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
            joined += 1
        except OSError:
            continue
    if not joined:
        try:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                            socket.inet_aton(group) + socket.inet_aton('0.0.0.0'))
        except OSError:
            pass
    return sock


def scan(seconds=2.5, port=LAN_PORT, *, interfaces=None, bind='0.0.0.0', group=LAN_GROUP, sock=None):
    """The LAN games announced within `seconds`: [{ip (the sender), candidates, release, code, name, tcp_port,
    players, players_max}], one per (sender, code), in the order first seen with the newest data.

    sock: an already open listener (tests); it is left open. 2.5 s covers one 2 s announcement round."""
    own = sock is None
    if own:
        sock = open_listener(port, interfaces, bind, group)
    found = {}
    deadline = time.perf_counter() + seconds
    try:
        while True:
            left = deadline - time.perf_counter()
            if left <= 0:
                break
            sock.settimeout(left)
            try:
                data, source = sock.recvfrom(RECEIVE_BUFFER)
            except socket.timeout:
                break
            except ConnectionResetError:
                continue          # Windows reports an earlier ICMP error here; not a datagram
            game = decode(data, source[0])
            if game is not None:
                found[(game['ip'], game['code'])] = game
    finally:
        if own:
            sock.close()
    return list(found.values())
