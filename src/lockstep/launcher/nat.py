"""Port mapping, the public endpoint and reachability of a hosting PC (docs/NETPLAY.md, "Port mapping and reachability").

A host in `direct` mode runs the lockstep relay (UDP) and the lobby (TCP) on its own PC. Players outside its home
network reach them only through a port mapping on the home router, made here with, in this order:

- PCP (RFC 6887): a MAP request per port to the default gateway's UDP 5351, with a random 12-byte nonce per mapping;
- NAT-PMP (RFC 6886): only when the gateway answered PCP with a version 0 "unsupported version" (a NAT-PMP-only
  router; a gateway that stays silent on 5351 speaks neither);
- UPnP IGD: an SSDP M-SEARCH for InternetGatewayDevice:2 and :1 sent from every interface (a PC on several networks
  finds the router on whichever it is), the device description's WANIPConnection:2, :1 or WANPPPConnection:1 control
  URL, then the SOAP actions GetExternalIPAddress, GetSpecificPortMappingEntry, DeletePortMapping and AddPortMapping.

Mappings are made only while hosting, with a lease that keep_mapped() renews at half its length, and Mapping.release()
deletes them. A mapping of ours left behind by a crash is deleted before the new one is made: PCP needs the old nonce
for that, so the nonces of live PCP mappings are kept in a small state file (.local/netplay/nat-state.json);
NAT-PMP deletes by internal port (lifetime 0); UPnP asks GetSpecificPortMappingEntry and deletes an entry that names
this PC's address. A router that hands out a private external address (another NAT above it) is refused as
'double_nat': nobody outside could use that mapping.

public_endpoint() asks the master's UDP echo which address and port the internet sees (and the round trip);
reachable() asks the master's HTTP probe to connect back, the final word on whether `direct` works.
Tests use the loopback fakes of tools/dev/nat_fake_gateway.py: every network target here can be injected.
"""
import http.client
import ipaddress
import json
import os
import secrets
import select
import socket
import struct
import sys
import threading
import time
import urllib.parse
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from xml.sax.saxutils import escape

from . import netifs

ROOT = Path(__file__).resolve().parents[3]
STATE_FILE = ROOT / '.local' / 'netplay' / 'nat-state.json'
METHODS = ('pcp', 'natpmp', 'upnp')
PROTOCOL_NUMBERS = {'udp': 17, 'tcp': 6}

# Join codes (docs/NETPLAY.md): 8 characters of this alphabet (no 0, 1, I, O), shown XXXX-XXXX.
CODE_ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'
CODE_LENGTH = 8
CODE_HALF = 4

# A private, shared (carrier-grade NAT), link-local, loopback or "this network" external address means another NAT
# sits above the router. Explicit list: ipaddress.is_private also calls the documentation ranges private.
PRIVATE_NETWORKS = tuple(ipaddress.IPv4Network(network) for network in (
    '0.0.0.0/8', '10.0.0.0/8', '100.64.0.0/10', '127.0.0.0/8', '169.254.0.0/16', '172.16.0.0/12', '192.168.0.0/16'))

# UDP exchanges with the gateway (PCP, NAT-PMP): first retransmission after 250 ms, doubling, within the timeout.
RETRANSMIT_FIRST = 0.25
REPLY_MAX = 1100
# Deleting a stale mapping is best effort: it waits less than a real request.
STALE_DELETE_TIMEOUT = 1.0

PCP_PORT = 5351
PCP_VERSION = 2
PCP_OPCODE_MAP = 1
PCP_RESPONSE = 0x80
PCP_MAP_SIZE = 60
PCP_NONCE_SIZE = 12
PCP_RESULTS = ('success', 'unsupported_version', 'not_authorized', 'malformed_request', 'unsupported_opcode',
               'unsupported_option', 'malformed_option', 'network_failure', 'no_resources', 'unsupported_protocol',
               'user_exceeded_quota', 'cannot_provide_external', 'address_mismatch', 'excessive_remote_peers')
NATPMP_VERSION = 0
NATPMP_OPCODE_ADDRESS = 0
NATPMP_OPCODES = {'udp': 1, 'tcp': 2}
NATPMP_RESPONSE = 128
NATPMP_RESULTS = ('success', 'unsupported_version', 'not_authorized', 'network_failure', 'no_resources',
                  'unsupported_opcode')

SSDP_MULTICAST = ('239.255.255.250', 1900)
# UPnP Device Architecture: M-SEARCH datagrams leave with TTL 2 (one router hop at most).
SSDP_TTL = 2
SSDP_MX = 2
SSDP_ANSWER_GRACE = 0.5
IGD_DEVICES = ('urn:schemas-upnp-org:device:InternetGatewayDevice:2',
               'urn:schemas-upnp-org:device:InternetGatewayDevice:1')
WAN_SERVICES = ('urn:schemas-upnp-org:service:WANIPConnection:2',
                'urn:schemas-upnp-org:service:WANIPConnection:1',
                'urn:schemas-upnp-org:service:WANPPPConnection:1')
IGD_TRIES_MAX = 4
DESCRIPTION_MAX = 256 * 1024
SOAP_ANSWER_MAX = 64 * 1024
UPNP_NO_SUCH_ENTRY = 714
UPNP_CONFLICT = 718
UPNP_SAME_PORT_REQUIRED = 724
UPNP_PERMANENT_ONLY = 725
# On a conflict (718, or a foreign entry on the port) the next external ports are tried, as the C core does.
UPNP_CONFLICT_TRIES = 4
UPNP_DESCRIPTION = 'X4 Multiplayer host'

# keep_mapped(): renew at half the lease, never more often than every MIN_RENEW seconds; a permanent UPnP mapping
# (lease 0) is added again every PERMANENT_RECHECK seconds in case the router restarted; after a failed renewal the
# next try comes sooner.
MIN_RENEW = 30.0
PERMANENT_RECHECK = 1800.0
RENEW_RETRY = 60.0

# The master's UDP echo (docs/NETPLAY.md, "UDP echo").
ECHO_PORT = 47101
ECHO_SIZE = 64
ECHO_ATTEMPTS = 3
ECHO_NONCE_HEX = 16
PROBE_GRACE = 0.5
TCP_STAND_IN_BACKLOG = 4
POLL_INTERVAL = 0.2


class MappingRefused(Exception):
    """One method could not map a port; reason is short and machine-readable ('pcp: not_authorized' in the log).

    version0 is set when a PCP request was answered by a NAT-PMP-only gateway (NAT-PMP is then worth a try)."""

    def __init__(self, reason, version0=False):
        super().__init__(reason)
        self.reason = reason
        self.version0 = version0


def normalize_code(text):
    """'K7QX-3FNA' for a join code typed in any case, with or without the dash or spaces; None for anything else.

    A long LAN invite code (X4MP-<ip>-<port>-<token>) or an address is not a join code: None."""
    if not isinstance(text, str):
        return None
    compact = ''.join(text.split()).replace('-', '').upper()
    if len(compact) != CODE_LENGTH:
        return None
    if any(character not in CODE_ALPHABET for character in compact):
        return None
    return f'{compact[:CODE_HALF]}-{compact[CODE_HALF:]}'


def is_private(ip):
    try:
        address = ipaddress.IPv4Address(ip)
    except ValueError:
        return True
    return any(address in network for network in PRIVATE_NETWORKS)


def _address(value, default_port):
    """(ip or host, port) from 'host', 'host:port' or a tuple; None stays None."""
    if value is None:
        return None
    if isinstance(value, tuple):
        return value[0], int(value[1])
    host, separator, port = str(value).rpartition(':')
    if separator and port.isdigit():
        return host, int(port)
    return str(value), default_port


def local_ip_towards(address):
    """This PC's address on the route to address (ip, port). A UDP connect sends nothing; it only picks the route."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect(address)
        return probe.getsockname()[0]


# ---- the default gateway -------------------------------------------------------------------------------------------

PROC_ROUTE = Path('/proc/net/route')
RTF_UP = 0x1
RTF_GATEWAY = 0x2
FORWARD_ROW_SIZE = 56          # MIB_IPFORWARDROW: 14 DWORDs
FORWARD_NEXT_HOP_AT = 12
FORWARD_METRIC_AT = 36
ERROR_INSUFFICIENT_BUFFER = 122


def parse_proc_route(text):
    """The gateway of the default route with the lowest metric in /proc/net/route (hex, little endian), or None."""
    best = None
    for line in text.splitlines()[1:]:
        fields = line.split()
        if len(fields) < 8:
            continue
        try:
            destination = int(fields[1], 16)
            gateway = int(fields[2], 16)
            flags = int(fields[3], 16)
            metric = int(fields[6])
            mask = int(fields[7], 16)
        except ValueError:
            continue
        if destination or mask or not gateway:
            continue
        if flags & (RTF_UP | RTF_GATEWAY) != RTF_UP | RTF_GATEWAY:
            continue
        if best is None or metric < best[0]:
            best = (metric, socket.inet_ntoa(struct.pack('<I', gateway)))
    return best[1] if best else None


def parse_forward_table(raw):
    """The next hop of the 0.0.0.0/0 route with the lowest metric in a MIB_IPFORWARDTABLE buffer, or None.

    Addresses in the rows are in network byte order; on Windows Vista and later dwForwardMetric1 already includes
    the interface metric, so the lowest one is the route Windows uses."""
    if len(raw) < 4:
        return None
    count = struct.unpack_from('<I', raw, 0)[0]
    best = None
    for index in range(count):
        start = 4 + index * FORWARD_ROW_SIZE
        if start + FORWARD_ROW_SIZE > len(raw):
            break
        destination = raw[start:start + 4]
        mask = raw[start + 4:start + 8]
        next_hop = raw[start + FORWARD_NEXT_HOP_AT:start + FORWARD_NEXT_HOP_AT + 4]
        metric = struct.unpack_from('<I', raw, start + FORWARD_METRIC_AT)[0]
        if destination != bytes(4) or mask != bytes(4) or next_hop == bytes(4):
            continue
        if best is None or metric < best[0]:
            best = (metric, socket.inet_ntoa(next_hop))
    return best[1] if best else None


def _windows_forward_table():
    import ctypes
    function = ctypes.WinDLL('iphlpapi').GetIpForwardTable
    function.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.c_int]
    function.restype = ctypes.c_ulong
    size = ctypes.c_ulong(0)
    result = function(None, ctypes.byref(size), 0)
    if result != ERROR_INSUFFICIENT_BUFFER:
        return b''
    buffer = ctypes.create_string_buffer(size.value)
    if function(buffer, ctypes.byref(size), 0) != 0:
        return b''
    return buffer.raw[:size.value]


def default_gateway():
    """The IPv4 default gateway (the home router, usually), or None. Reads the routing table only."""
    try:
        if os.name == 'nt':
            return parse_forward_table(_windows_forward_table())
        if sys.platform.startswith('linux'):
            return parse_proc_route(PROC_ROUTE.read_text())
    except OSError:
        return None
    return None


# ---- PCP and NAT-PMP messages ----------------------------------------------------------------------------------------

def _mapped_ipv6(ip):
    """IPv4-mapped IPv6 (::ffff:a.b.c.d), as PCP carries IPv4 addresses."""
    return bytes(10) + b'\xff\xff' + socket.inet_aton(ip)


def pcp_map_request(client_ip, nonce, protocol, internal_port, suggested_port, lifetime):
    """A 60-byte PCP MAP request; suggested external address ::ffff:0.0.0.0 = no preference."""
    header = struct.pack('>BBHI', PCP_VERSION, PCP_OPCODE_MAP, 0, lifetime) + _mapped_ipv6(client_ip)
    payload = nonce + struct.pack('>B3xHH', PROTOCOL_NUMBERS[protocol], internal_port, suggested_port)
    return header + payload + _mapped_ipv6('0.0.0.0')


def pcp_map_parse(data, nonce, protocol, internal_port):
    """The fields of a MAP response to this request, or None to ignore the datagram.

    {'version0': True, 'result': code} for a NAT-PMP-only gateway's "unsupported version" answer; error results
    count by their code alone (a gateway may shorten the payload of an error)."""
    if len(data) >= 4 and data[0] == NATPMP_VERSION and data[1] >= NATPMP_RESPONSE:
        return {'version0': True, 'result': struct.unpack_from('>H', data, 2)[0]}
    if len(data) < PCP_MAP_SIZE or len(data) > REPLY_MAX:
        return None
    if data[0] != PCP_VERSION or data[1] != PCP_RESPONSE | PCP_OPCODE_MAP:
        return None
    result = data[3]
    lifetime, epoch = struct.unpack_from('>II', data, 4)
    reply = {'version0': False, 'result': result, 'lifetime': lifetime, 'epoch': epoch}
    if result:
        return reply
    if data[24:36] != nonce or data[36] != PROTOCOL_NUMBERS[protocol]:
        return None
    if struct.unpack_from('>H', data, 40)[0] != internal_port:
        return None
    if data[44:56] != bytes(10) + b'\xff\xff':
        return None
    reply['external_port'] = struct.unpack_from('>H', data, 42)[0]
    reply['external_ip'] = socket.inet_ntoa(data[56:60])
    return reply


def pcp_result_name(result):
    return PCP_RESULTS[result] if result < len(PCP_RESULTS) else f'result_{result}'


def natpmp_address_request():
    return struct.pack('>BB', NATPMP_VERSION, NATPMP_OPCODE_ADDRESS)


def natpmp_address_parse(data):
    if len(data) < 4 or data[0] != NATPMP_VERSION or data[1] != NATPMP_RESPONSE + NATPMP_OPCODE_ADDRESS:
        return None
    result = struct.unpack_from('>H', data, 2)[0]
    if result:
        return {'result': result}
    if len(data) < 12:
        return None
    return {'result': 0, 'epoch': struct.unpack_from('>I', data, 4)[0], 'external_ip': socket.inet_ntoa(data[8:12])}


def natpmp_map_request(protocol, internal_port, suggested_port, lifetime):
    return struct.pack('>BBHHHI', NATPMP_VERSION, NATPMP_OPCODES[protocol], 0, internal_port, suggested_port, lifetime)


def natpmp_map_parse(data, protocol, internal_port):
    if len(data) < 4 or data[0] != NATPMP_VERSION or data[1] != NATPMP_RESPONSE + NATPMP_OPCODES[protocol]:
        return None
    result = struct.unpack_from('>H', data, 2)[0]
    if result:
        return {'result': result}
    if len(data) < 16:
        return None
    epoch, internal, external, lifetime = struct.unpack_from('>IHHI', data, 4)
    if internal != internal_port:
        return None
    return {'result': 0, 'epoch': epoch, 'external_port': external, 'lifetime': lifetime}


def natpmp_result_name(result):
    return NATPMP_RESULTS[result] if result < len(NATPMP_RESULTS) else f'result_{result}'


def udp_exchange(server, request, accept, timeout):
    """Sends request to server (ip, port) until accept(datagram) returns something, retransmitting after 250 ms and
    then at doubling intervals; returns that, or None after timeout seconds.

    The socket is connected, so only the server's datagrams arrive, and an ICMP "port unreachable" (no service
    there) ends the exchange at once: MappingRefused('unreachable')."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(server)
        started = time.perf_counter()
        deadline = started + timeout
        next_send = started
        interval = RETRANSMIT_FIRST
        while True:
            now = time.perf_counter()
            if now >= deadline:
                return None
            try:
                if now >= next_send:
                    sock.send(request)
                    next_send = now + interval
                    interval *= 2
                sock.settimeout(max(0.001, min(deadline, next_send) - now))
                data = sock.recv(REPLY_MAX + 1)
            except socket.timeout:
                continue
            except (ConnectionRefusedError, ConnectionResetError):
                raise MappingRefused('unreachable') from None
            answer = accept(data)
            if answer is not None:
                return answer


# ---- the state file (PCP nonces of live mappings) ---------------------------------------------------------------------

class NonceStore:
    """PCP nonces of this PC's live mappings, by gateway, local address, protocol and internal port.

    A PCP server refuses to change or delete a mapping without the nonce it was made with (RFC 6887 11.3), so the
    nonce of every live mapping is written here and read again on the next start: a mapping a crashed host left
    behind is deleted with it. A file that cannot be read or written only loses that clean-up, never a mapping."""

    _lock = threading.Lock()

    def __init__(self, path):
        self.path = Path(path) if path is not None else None

    def _read(self):
        if self.path is None:
            return {}
        try:
            data = json.loads(self.path.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            return {}
        nonces = data.get('pcp') if isinstance(data, dict) else None
        return nonces if isinstance(nonces, dict) else {}

    def _write(self, nonces):
        if self.path is None:
            return
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_suffix('.tmp')
            temporary.write_text(json.dumps({'pcp': nonces}, indent=1, sort_keys=True), encoding='utf-8')
            temporary.replace(self.path)
        except OSError:
            pass

    def get(self, key):
        with self._lock:
            text = self._read().get(key)
        try:
            nonce = bytes.fromhex(text) if isinstance(text, str) else None
        except ValueError:
            return None
        return nonce if nonce is not None and len(nonce) == PCP_NONCE_SIZE else None

    def put(self, key, nonce):
        with self._lock:
            nonces = self._read()
            nonces[key] = nonce.hex()
            self._write(nonces)

    def drop(self, key):
        with self._lock:
            nonces = self._read()
            if nonces.pop(key, None) is not None:
                self._write(nonces)


# ---- the three methods --------------------------------------------------------------------------------------------
# Each backend maps one (protocol, internal port) at a time: add() -> entry, renew(entry) -> entry or None,
# remove(entry). external_ip is known after prepare() (NAT-PMP, UPnP) or after the first add() (PCP).

class PcpBackend:
    method = 'pcp'

    def __init__(self, gateway, timeout, store):
        self.gateway = gateway
        self.timeout = timeout
        self.store = store
        self.local_ip = local_ip_towards(gateway)
        self.external_ip = None

    def describe(self):
        return f'{self.gateway[0]}:{self.gateway[1]}'

    def prepare(self):
        pass

    def _key(self, protocol, internal_port):
        return f'{self.gateway[0]}|{self.local_ip}|{protocol}|{internal_port}'

    def _request(self, protocol, internal_port, suggested_port, lifetime, nonce, timeout):
        request = pcp_map_request(self.local_ip, nonce, protocol, internal_port, suggested_port, lifetime)

        def accept(data):
            return pcp_map_parse(data, nonce, protocol, internal_port)
        return udp_exchange(self.gateway, request, accept, timeout)

    def _granted(self, reply):
        if reply is None:
            raise MappingRefused('no_reply')
        if reply['version0']:
            raise MappingRefused('unsupported_version', version0=True)
        if reply['result']:
            raise MappingRefused(pcp_result_name(reply['result']))
        if not reply['lifetime'] or not reply['external_port']:
            raise MappingRefused('no_lifetime')
        return reply

    def add(self, protocol, internal_port, lease):
        key = self._key(protocol, internal_port)
        stale = self.store.get(key)
        if stale is not None:
            # A mapping of ours from an earlier start (a crash, a killed launcher): delete it with its own nonce.
            try:
                self._request(protocol, internal_port, 0, 0, stale, min(self.timeout, STALE_DELETE_TIMEOUT))
            except MappingRefused:
                pass
            self.store.drop(key)
        nonce = secrets.token_bytes(PCP_NONCE_SIZE)
        reply = self._granted(self._request(protocol, internal_port, internal_port, lease, nonce, self.timeout))
        self.store.put(key, nonce)
        self.external_ip = reply['external_ip']
        return {'protocol': protocol, 'internal_port': internal_port, 'external_port': reply['external_port'],
                'lease': reply['lifetime'], 'requested': lease, 'nonce': nonce}

    def renew(self, entry):
        """The same nonce and the granted external port: the server extends the mapping it already holds."""
        try:
            reply = self._granted(self._request(entry['protocol'], entry['internal_port'], entry['external_port'],
                                                entry['requested'], entry['nonce'], self.timeout))
        except MappingRefused:
            return None
        self.external_ip = reply['external_ip']
        return dict(entry, external_port=reply['external_port'], lease=reply['lifetime'])

    def remove(self, entry):
        try:
            self._request(entry['protocol'], entry['internal_port'], 0, 0, entry['nonce'], self.timeout)
        except MappingRefused:
            pass
        self.store.drop(self._key(entry['protocol'], entry['internal_port']))


class NatPmpBackend:
    method = 'natpmp'

    def __init__(self, gateway, timeout):
        self.gateway = gateway
        self.timeout = timeout
        self.local_ip = local_ip_towards(gateway)
        self.external_ip = None

    def describe(self):
        return f'{self.gateway[0]}:{self.gateway[1]}'

    def prepare(self):
        reply = udp_exchange(self.gateway, natpmp_address_request(), natpmp_address_parse, self.timeout)
        if reply is None:
            raise MappingRefused('no_reply')
        if reply['result']:
            raise MappingRefused(natpmp_result_name(reply['result']))
        self.external_ip = reply['external_ip']

    def _request(self, protocol, internal_port, suggested_port, lifetime, timeout):
        request = natpmp_map_request(protocol, internal_port, suggested_port, lifetime)

        def accept(data):
            return natpmp_map_parse(data, protocol, internal_port)
        return udp_exchange(self.gateway, request, accept, timeout)

    def _granted(self, reply):
        if reply is None:
            raise MappingRefused('no_reply')
        if reply['result']:
            raise MappingRefused(natpmp_result_name(reply['result']))
        if not reply['lifetime'] or not reply['external_port']:
            raise MappingRefused('no_lifetime')
        return reply

    def add(self, protocol, internal_port, lease):
        # Lifetime 0 and suggested port 0 delete whatever mapping this PC holds for the internal port (RFC 6886 3.4).
        try:
            self._request(protocol, internal_port, 0, 0, min(self.timeout, STALE_DELETE_TIMEOUT))
        except MappingRefused:
            pass
        reply = self._granted(self._request(protocol, internal_port, internal_port, lease, self.timeout))
        return {'protocol': protocol, 'internal_port': internal_port, 'external_port': reply['external_port'],
                'lease': reply['lifetime'], 'requested': lease}

    def renew(self, entry):
        try:
            reply = self._granted(self._request(entry['protocol'], entry['internal_port'], entry['external_port'],
                                                entry['requested'], self.timeout))
        except MappingRefused:
            return None
        return dict(entry, external_port=reply['external_port'], lease=reply['lifetime'])

    def remove(self, entry):
        try:
            self._request(entry['protocol'], entry['internal_port'], 0, 0, self.timeout)
        except MappingRefused:
            pass


def soap_envelope(service, action, arguments):
    """The SOAP body of one IGD action; arguments are (name, value) pairs in the order the service defines."""
    fields = ''.join(f'<{name}>{escape(str(value))}</{name}>' for name, value in arguments)
    return ('<?xml version="1.0"?>\r\n'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
            f'<u:{action} xmlns:u="{service}">{fields}</u:{action}></s:Body></s:Envelope>\r\n')


def _local_name(tag):
    return tag.rpartition('}')[2]


def xml_fields(body):
    """{local tag name: text} of every leaf element of an XML text; {} for anything that does not parse."""
    try:
        root = ElementTree.fromstring(body)
    except ElementTree.ParseError:
        return {}
    fields = {}
    for element in root.iter():
        if len(element) == 0:
            fields.setdefault(_local_name(element.tag), (element.text or '').strip())
    return fields


def _http_request(url, method, timeout, body=None, headers=None, limit=SOAP_ANSWER_MAX):
    """(status, body bytes) of one plain-HTTP request to a gateway; at most limit bytes are read."""
    parts = urllib.parse.urlsplit(url)
    path = parts.path or '/'
    if parts.query:
        path += '?' + parts.query
    connection = http.client.HTTPConnection(parts.hostname, parts.port or 80, timeout=timeout)
    try:
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        data = response.read(limit + 1)
        return response.status, data[:limit]
    finally:
        connection.close()


def soap_call(control_url, service, action, arguments, timeout):
    """(answer fields, 0) for a 200 answer, else ({}, the UPnP errorCode or -HTTP status). OSError on no answer."""
    body = soap_envelope(service, action, arguments).encode('utf-8')
    headers = {'Content-Type': 'text/xml; charset="utf-8"', 'SOAPAction': f'"{service}#{action}"',
               'Connection': 'close'}
    try:
        status, answer = _http_request(control_url, 'POST', timeout, body, headers)
    except http.client.HTTPException as error:
        raise OSError(f'bad HTTP answer: {error}') from None
    fields = xml_fields(answer)
    if status == 200:
        return fields, 0
    code = fields.get('errorCode', '')
    return {}, int(code) if code.isdigit() else -status


class UpnpBackend:
    method = 'upnp'

    def __init__(self, control_url, service, timeout):
        self.control_url = control_url
        self.service = service
        self.timeout = timeout
        parts = urllib.parse.urlsplit(control_url)
        self.local_ip = local_ip_towards((parts.hostname, parts.port or 80))
        self.external_ip = None

    def describe(self):
        return f'{self.control_url} ({self.service.rpartition(":service:")[2]})'

    def _call(self, action, arguments):
        try:
            return soap_call(self.control_url, self.service, action, arguments, self.timeout)
        except OSError as error:
            raise MappingRefused(f'no_answer {action}: {error}') from None

    def prepare(self):
        fields, error = self._call('GetExternalIPAddress', [])
        address = fields.get('NewExternalIPAddress', '')
        if error or netifs.parse_ipv4(address) is None:
            # Some routers do not implement it; the reachability probe still decides.
            self.external_ip = None
            return
        if address == '0.0.0.0':
            raise MappingRefused('no_external_address')
        self.external_ip = address

    def _entry(self, external_port, protocol):
        """The router's mapping on this external port, or None (none, or the router does not tell)."""
        arguments = [('NewRemoteHost', ''), ('NewExternalPort', external_port), ('NewProtocol', protocol.upper())]
        fields, error = self._call('GetSpecificPortMappingEntry', arguments)
        if error or not fields.get('NewInternalClient'):
            return None
        return fields

    def _ours(self, existing, internal_port):
        """An entry this PC may delete: it names our address and either our internal port or our description."""
        if existing.get('NewInternalClient') != self.local_ip:
            return False
        same_port = existing.get('NewInternalPort') == str(internal_port)
        return same_port or existing.get('NewPortMappingDescription', '').startswith(UPNP_DESCRIPTION)

    def _delete(self, external_port, protocol):
        arguments = [('NewRemoteHost', ''), ('NewExternalPort', external_port), ('NewProtocol', protocol.upper())]
        self._call('DeletePortMapping', arguments)

    def _add(self, external_port, protocol, internal_port, lease):
        arguments = [('NewRemoteHost', ''), ('NewExternalPort', external_port), ('NewProtocol', protocol.upper()),
                     ('NewInternalPort', internal_port), ('NewInternalClient', self.local_ip), ('NewEnabled', 1),
                     ('NewPortMappingDescription', UPNP_DESCRIPTION), ('NewLeaseDuration', lease)]
        return self._call('AddPortMapping', arguments)[1]

    def add(self, protocol, internal_port, lease):
        for attempt in range(UPNP_CONFLICT_TRIES):
            external_port = internal_port + attempt
            if external_port > 65535:
                break
            existing = self._entry(external_port, protocol)
            if existing is not None:
                if not self._ours(existing, internal_port):
                    continue                     # another device's mapping: never touched, try the next port
                self._delete(external_port, protocol)   # ours, left by an earlier start
            granted = lease
            error = self._add(external_port, protocol, internal_port, granted)
            if error == UPNP_PERMANENT_ONLY and granted:
                # The router keeps only permanent mappings: lease 0, and release() deletes it.
                granted = 0
                error = self._add(external_port, protocol, internal_port, granted)
            if error == UPNP_CONFLICT:
                continue
            if error:
                raise MappingRefused(f'error_{error}' if error > 0 else f'http_{-error}')
            return {'protocol': protocol, 'internal_port': internal_port, 'external_port': external_port,
                    'lease': granted, 'requested': granted}
        raise MappingRefused('conflict')

    def renew(self, entry):
        """Adding the same mapping again refreshes its lease (and restores it after a router restart)."""
        try:
            error = self._add(entry['external_port'], entry['protocol'], entry['internal_port'], entry['requested'])
        except MappingRefused:
            return None
        return None if error else dict(entry)

    def remove(self, entry):
        try:
            self._delete(entry['external_port'], entry['protocol'])
        except MappingRefused:
            pass


# ---- UPnP discovery ------------------------------------------------------------------------------------------------

def ssdp_search(device):
    return ('M-SEARCH * HTTP/1.1\r\n'
            f'HOST: {SSDP_MULTICAST[0]}:{SSDP_MULTICAST[1]}\r\n'
            'MAN: "ssdp:discover"\r\n'
            f'MX: {SSDP_MX}\r\n'
            f'ST: {device}\r\n\r\n').encode('ascii')


def ssdp_headers(data):
    """{lower-case header: value} of an SSDP answer ('HTTP/1.1 200 OK' first), or None."""
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        return None
    lines = text.split('\r\n')
    if not lines[0].upper().startswith('HTTP/1.1 200'):
        return None
    headers = {}
    for line in lines[1:]:
        name, separator, value = line.partition(':')
        if separator:
            headers[name.strip().lower()] = value.strip()
    return headers


def location_valid(location, source_ip):
    """A LOCATION a gateway may give: http to a dotted IPv4 address, the same address the answer came from.

    A device answering for another host could otherwise make this PC send its requests anywhere."""
    parts = urllib.parse.urlsplit(location)
    if parts.scheme != 'http' or netifs.parse_ipv4(parts.hostname) is None:
        return False
    return parts.hostname == source_ip


def _ssdp_sources(target, interfaces):
    """The addresses to send M-SEARCH from: every interface's, or loopback only for a loopback target (tests)."""
    if ipaddress.IPv4Address(target[0]).is_loopback:
        return ['127.0.0.1']
    if interfaces is None:
        interfaces = netifs.addresses()
    sources = [entry['ip'] for entry in interfaces if entry.get('ip')]
    return sources or ['0.0.0.0']


def _open_ssdp_socket(source, target):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((source, 0))
        if ipaddress.IPv4Address(target[0]).is_multicast:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, SSDP_TTL)
            if source != '0.0.0.0':
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(source))
    except OSError:
        sock.close()
        raise
    return sock


def _send_searches(sockets, target):
    for sock in sockets:
        for device in IGD_DEVICES:
            try:
                sock.sendto(ssdp_search(device), target)
            except OSError:
                pass


def discover_igds(timeout, target=None, interfaces=None):
    """LOCATION URLs of the Internet gateway devices that answer an SSDP search, in the order they answered.

    The search goes out from every interface (IP_MULTICAST_IF): on a PC with several networks the router may be on
    any of them. It is sent again halfway through if nobody answered (UDP loss); after the first answer the others
    have SSDP_ANSWER_GRACE seconds."""
    target = target or SSDP_MULTICAST
    sockets = []
    for source in _ssdp_sources(target, interfaces):
        try:
            sockets.append(_open_ssdp_socket(source, target))
        except OSError:
            continue
    locations = []
    try:
        _send_searches(sockets, target)
        started = time.perf_counter()
        deadline = started + timeout
        resent = False
        while sockets:
            now = time.perf_counter()
            if now >= deadline:
                break
            if not resent and not locations and now >= started + timeout / 2:
                _send_searches(sockets, target)
                resent = True
            readable, _, _ = select.select(sockets, [], [], min(POLL_INTERVAL, deadline - now))
            for sock in readable:
                try:
                    data, source = sock.recvfrom(REPLY_MAX * 2)
                except OSError:
                    continue
                headers = ssdp_headers(data)
                if not headers or 'internetgatewaydevice' not in headers.get('st', '').lower():
                    continue
                location = headers.get('location', '')
                if location_valid(location, source[0]) and location not in locations:
                    if not locations:
                        deadline = min(deadline, time.perf_counter() + SSDP_ANSWER_GRACE)
                    locations.append(location)
    finally:
        for sock in sockets:
            sock.close()
    return locations


def igd_services(description, location):
    """[(service type, control URL)] of a device description, WANIPConnection:2, :1, then WANPPPConnection:1.

    Control URLs are resolved against URLBase (UPnP 1.0) or the description's own URL, and must stay on the
    description's host."""
    try:
        root = ElementTree.fromstring(description)
    except ElementTree.ParseError:
        return []
    base = location
    services = []
    for element in root.iter():
        name = _local_name(element.tag)
        if name == 'URLBase' and (element.text or '').strip():
            base = element.text.strip()
        if name == 'service':
            fields = {_local_name(child.tag): (child.text or '').strip() for child in element}
            services.append(fields)
    host = urllib.parse.urlsplit(location).hostname
    found = []
    for wanted in WAN_SERVICES:
        for fields in services:
            if fields.get('serviceType') != wanted or not fields.get('controlURL'):
                continue
            control = urllib.parse.urljoin(base, fields['controlURL'])
            parts = urllib.parse.urlsplit(control)
            if parts.scheme == 'http' and parts.hostname == host:
                found.append((wanted, control))
    return found


# ---- the mapping ---------------------------------------------------------------------------------------------------

class Mapping:
    """A UDP and a TCP port mapping on the router, made by one method.

    udp_port and tcp_port are the EXTERNAL ports (what players outside connect to; usually the internal ones, but a
    router may give others); internal_udp_port and internal_tcp_port are this PC's. lease is the granted lifetime in
    seconds, 0 for a permanent UPnP mapping (a router that keeps only those). external_ip may be None when a UPnP
    router does not tell it."""

    def __init__(self, backend, entries):
        self.method = backend.method
        self.external_ip = backend.external_ip
        self.local_ip = backend.local_ip
        self.gateway = backend.describe()
        self.released = False
        self._backend = backend
        self._entries = entries
        self._lock = threading.Lock()
        self._take(entries)

    def _take(self, entries):
        by_protocol = {entry['protocol']: entry for entry in entries}
        self.udp_port = by_protocol['udp']['external_port']
        self.tcp_port = by_protocol['tcp']['external_port']
        self.internal_udp_port = by_protocol['udp']['internal_port']
        self.internal_tcp_port = by_protocol['tcp']['internal_port']
        leases = [entry['lease'] for entry in entries if entry['lease']]
        self.lease = min(leases) if leases else 0

    def renew(self):
        """Extends both mappings. False if either could not be renewed or its external port changed (players would
        then reach a dead port: the caller should map again or fall back to the relay)."""
        with self._lock:
            if self.released:
                return False
            renewed = []
            for entry in self._entries:
                fresh = self._backend.renew(entry)
                if fresh is None or fresh['external_port'] != entry['external_port']:
                    return False
                renewed.append(fresh)
            self._entries = renewed
            self._take(renewed)
            if self._backend.external_ip:
                self.external_ip = self._backend.external_ip
            return True

    def release(self):
        """Deletes both mappings on the router; a second call does nothing."""
        with self._lock:
            if self.released:
                return
            self.released = True
            for entry in self._entries:
                self._backend.remove(entry)

    def __repr__(self):
        return (f'Mapping({self.method} {self.external_ip}: udp {self.internal_udp_port}->{self.udp_port}, '
                f'tcp {self.internal_tcp_port}->{self.tcp_port}, lease {self.lease})')


def _map_with(backend, udp_port, tcp_port, lease):
    """Both ports with one backend, or MappingRefused with nothing left mapped."""
    backend.prepare()
    if backend.external_ip is not None and is_private(backend.external_ip):
        raise MappingRefused('double_nat')
    entries = []
    try:
        for protocol, internal_port in (('udp', udp_port), ('tcp', tcp_port)):
            entries.append(backend.add(protocol, internal_port, lease))
            if backend.external_ip is not None and is_private(backend.external_ip):
                raise MappingRefused('double_nat')
    except BaseException:
        for entry in entries:
            backend.remove(entry)
        raise
    return Mapping(backend, entries)


def _upnp_mappings(udp_port, tcp_port, lease, timeout, ssdp, interfaces, gateway_ip, reasons):
    locations = discover_igds(timeout, _address(ssdp, SSDP_MULTICAST[1]), interfaces)
    if not locations:
        reasons.append('upnp: no gateway answered the search')
        return None
    # The device on the default gateway first: on a PC with several networks that is the router to the internet.
    locations.sort(key=lambda location: urllib.parse.urlsplit(location).hostname != gateway_ip)
    for location in locations[:IGD_TRIES_MAX]:
        try:
            status, description = _http_request(location, 'GET', timeout, limit=DESCRIPTION_MAX)
        except (OSError, http.client.HTTPException) as error:
            reasons.append(f'upnp: {location}: {error}')
            continue
        services = igd_services(description, location) if status == 200 else []
        if not services:
            reasons.append(f'upnp: {location}: no WAN connection service')
            continue
        for service, control in services:
            try:
                return _map_with(UpnpBackend(control, service, timeout), udp_port, tcp_port, lease)
            except MappingRefused as refusal:
                reasons.append(f'upnp: {refusal.reason}')
            except OSError as error:
                reasons.append(f'upnp: {error}')
    return None


def map_ports(udp_port, tcp_port, lease=3600, timeout=3.0, log=None, *, gateway=None, ssdp=None, interfaces=None,
              methods=METHODS, state_file=STATE_FILE, reasons=None):
    """Maps UDP udp_port and TCP tcp_port on the router (PCP, NAT-PMP, UPnP IGD in turn): a Mapping, or None.

    timeout bounds each step (one UDP exchange, the SSDP search, one HTTP request). The reasons each method failed go
    to log (one line) and, when given, into the list `reasons`. Tests and diagnostics name the targets: gateway (the
    PCP/NAT-PMP server, 'ip', 'ip:port' or a tuple; default the default gateway's 5351), ssdp (where M-SEARCH goes;
    default 239.255.255.250:1900 on every interface), interfaces (netifs rows for the SSDP sources), methods, and
    state_file (the PCP nonces; None keeps none)."""
    say = log or (lambda text: None)
    reasons = reasons if reasons is not None else []
    gateway_address = _address(gateway, PCP_PORT)
    if gateway_address is None:
        found = default_gateway()
        gateway_address = (found, PCP_PORT) if found else None
    gateway_ip = gateway_address[0] if gateway_address else None
    store = NonceStore(state_file)
    natpmp_worth_trying = 'pcp' not in methods
    mapping = None
    if 'pcp' in methods:
        if gateway_address is None:
            reasons.append('pcp: no default gateway')
        else:
            try:
                mapping = _map_with(PcpBackend(gateway_address, timeout, store), udp_port, tcp_port, lease)
            except MappingRefused as refusal:
                reasons.append(f'pcp: {refusal.reason}')
                natpmp_worth_trying = refusal.version0
            except OSError as error:
                reasons.append(f'pcp: {error}')
    if mapping is None and 'natpmp' in methods:
        if gateway_address is None:
            reasons.append('natpmp: no default gateway')
        elif not natpmp_worth_trying:
            reasons.append('natpmp: skipped (the gateway did not answer PCP as a NAT-PMP router)')
        else:
            try:
                mapping = _map_with(NatPmpBackend(gateway_address, timeout), udp_port, tcp_port, lease)
            except MappingRefused as refusal:
                reasons.append(f'natpmp: {refusal.reason}')
            except OSError as error:
                reasons.append(f'natpmp: {error}')
    if mapping is None and 'upnp' in methods:
        mapping = _upnp_mappings(udp_port, tcp_port, lease, timeout, ssdp, interfaces, gateway_ip, reasons)
    if mapping is None:
        say('NAT no port mapping: ' + '; '.join(reasons))
        return None
    say(f'NAT mapped {mapping.method} via {mapping.gateway}: external {mapping.external_ip} udp {mapping.udp_port} '
        f'tcp {mapping.tcp_port} lease {mapping.lease}')
    return mapping


def renew_interval(lease):
    """Seconds until the next renewal: half the lease, at least MIN_RENEW; PERMANENT_RECHECK for lease 0."""
    if not lease:
        return PERMANENT_RECHECK
    return max(MIN_RENEW, lease / 2)


def keep_mapped(mapping, stop, log=None):
    """A daemon thread that renews mapping at half its lease until stop is set (or the mapping is released).

    It never releases the mapping itself: the host calls mapping.release() when its game ends. A failed renewal is
    logged and tried again after RENEW_RETRY seconds (or sooner for a short lease)."""
    say = log or (lambda text: None)

    def run():
        wait = renew_interval(mapping.lease)
        while not stop.wait(wait):
            if mapping.released:
                return
            if mapping.renew():
                wait = renew_interval(mapping.lease)
            else:
                say(f'NAT renewal failed: {mapping!r}')
                wait = min(RENEW_RETRY, renew_interval(mapping.lease))

    thread = threading.Thread(target=run, name='x4mp-nat-renew', daemon=True)
    thread.start()
    return thread


# ---- the master's echo and probe -----------------------------------------------------------------------------------

def echo_request(nonce):
    """X4MPECHO|1|<nonce>|<pad>: exactly ECHO_SIZE bytes, so the answer (never larger than the request) fits."""
    head = f'X4MPECHO|1|{nonce}|'
    return (head + '.' * (ECHO_SIZE - len(head))).encode('ascii')


def parse_echo_reply(data):
    """(nonce, ip, port) of X4MPSEEN|1|<nonce>|<ip>:<port>[pad], or None."""
    try:
        fields = data.decode('ascii').split('|')
    except UnicodeDecodeError:
        return None
    if len(fields) < 4 or fields[0] != 'X4MPSEEN' or fields[1] != '1':
        return None
    nonce = fields[2]
    if len(nonce) != ECHO_NONCE_HEX or any(character not in '0123456789abcdef' for character in nonce):
        return None
    endpoint = fields[3].rstrip('.\0 ')
    ip, separator, port = endpoint.rpartition(':')
    if not separator or netifs.parse_ipv4(ip) is None:
        return None
    if not port.isdigit() or str(int(port)) != port or not 1 <= int(port) <= 65535:
        return None
    return nonce, ip, int(port)


def _echo_host(host):
    """The host name of 'host', 'host:port' or a URL ('https://master.example.org/x4mp')."""
    if '://' in host:
        return urllib.parse.urlsplit(host).hostname
    if host.count(':') == 1:
        return host.partition(':')[0]
    return host


def public_endpoint(host, echo_port=ECHO_PORT, timeout=2.0, *, local_port=0, bind=''):
    """(ip, port, rtt_ms) as the master's UDP echo sees this PC, or None (no answer within timeout).

    host is the master's host name, a base URL or the (host, port) of MasterClient.echo_address(). local_port picks
    the local UDP port to ask for (0: any; the relay's port before the relay starts, to learn its public mapping).
    Up to ECHO_ATTEMPTS requests with their own nonces go out across the timeout; the round trip is that of the
    request whose answer arrived."""
    if isinstance(host, tuple):
        host, echo_port = host
    try:
        infos = socket.getaddrinfo(_echo_host(host), echo_port, socket.AF_INET, socket.SOCK_DGRAM)
    except (OSError, UnicodeError):
        return None
    server = infos[0][4]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((bind, local_port))
        sent = {}
        started = time.perf_counter()
        deadline = started + timeout
        spacing = timeout / ECHO_ATTEMPTS
        next_send = started
        while True:
            now = time.perf_counter()
            if now >= deadline:
                return None
            if len(sent) < ECHO_ATTEMPTS and now >= next_send:
                nonce = secrets.token_hex(ECHO_NONCE_HEX // 2)
                try:
                    sock.sendto(echo_request(nonce), server)
                except OSError:
                    return None
                sent[nonce] = now
                next_send = now + spacing
            sock.settimeout(max(0.001, min(deadline, next_send) - now))
            try:
                data, source = sock.recvfrom(REPLY_MAX)
            except socket.timeout:
                continue
            except ConnectionResetError:
                continue      # Windows: an ICMP error from an earlier request; keep waiting
            if source != server:
                continue
            answer = parse_echo_reply(data)
            if answer is not None and answer[0] in sent:
                rtt_ms = round((time.perf_counter() - sent[answer[0]]) * 1000.0, 1)
                return answer[1], answer[2], rtt_ms


class _TcpStandIn:
    """A listener on the probed TCP port while nothing else listens there (the lobby starts later), so the master's
    connect has something to reach. Connections are accepted and closed at once."""

    def __init__(self, bind, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            # No SO_REUSEADDR: on Windows it would let this socket take a port another listener (the lobby) holds.
            self.sock.bind((bind, port))
            self.sock.listen(TCP_STAND_IN_BACKLOG)
            self.sock.settimeout(POLL_INTERVAL)
        except OSError:
            self.sock.close()
            raise
        self.accepted = 0
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self._serve, name='x4mp-probe-tcp', daemon=True)
        self.thread.start()

    def _serve(self):
        while not self.stopped.is_set():
            try:
                connection, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            self.accepted += 1
            connection.close()

    def close(self):
        self.stopped.set()
        self.sock.close()
        self.thread.join(1)


def _probe_arrived(sock, nonce, deadline):
    while True:
        left = deadline - time.perf_counter()
        if left <= 0:
            return False
        sock.settimeout(left)
        try:
            data, _ = sock.recvfrom(REPLY_MAX)
        except socket.timeout:
            return False
        except ConnectionResetError:
            continue
        fields = data.split(b'|')
        if fields[:3] == [b'X4MPPROBE', b'1', nonce.encode('ascii')]:
            return True


def reachable(client, udp_port, tcp_port, timeout=4.0, *, bind='0.0.0.0'):
    """{tcp, udp, ip}: whether the master reached this PC's TCP and UDP ports from the internet, and the public IP.

    client is a MasterClient (anything with probe(udp_port, tcp_port, nonce)). While it asks, this PC listens on UDP
    udp_port for X4MPPROBE|1|<nonce> and, if nothing listens on TCP tcp_port yet, accepts there. A port another
    program already holds is not taken over: the relay or lobby there answers instead, and a UDP port in use makes
    udp False. MasterError (unreachable, rate limited) passes to the caller."""
    nonce = secrets.token_hex(ECHO_NONCE_HEX // 2)
    started = time.perf_counter()
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        udp_sock.bind((bind, udp_port))
    except OSError:
        udp_sock.close()
        udp_sock = None
    try:
        stand_in = _TcpStandIn(bind, tcp_port)
    except OSError:
        stand_in = None
    try:
        answer = client.probe(udp_port, tcp_port, nonce)
        deadline = max(started + timeout, time.perf_counter() + PROBE_GRACE)
        udp_ok = _probe_arrived(udp_sock, nonce, deadline) if udp_sock is not None else False
    finally:
        if udp_sock is not None:
            udp_sock.close()
        if stand_in is not None:
            stand_in.close()
    return {'tcp': bool(answer.get('tcp')), 'udp': udp_ok, 'ip': answer.get('ip')}
