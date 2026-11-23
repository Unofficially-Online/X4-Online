"""This PC's IPv4 networks, for LAN discovery and for choosing which of a host's addresses to try first.

docs/NETPLAY.md, "VLANs, several networks and virtual LANs": there is no "primary interface". A PC may be on a cable
LAN, Wi-Fi, a VLAN adapter, Hyper-V/WSL vEthernet and a virtual LAN (ZeroTier, Hamachi, Radmin VPN, Tailscale,
WireGuard) at once, and any of them may be the network a friend shares. addresses() lists every usable IPv4 address of
every interface that is up; best_for() orders candidate addresses so that the one on a peer's own subnet comes first.

Left out: loopback (127/8) and link-local (169.254/16, what an adapter takes when no DHCP server answered; it reaches
nobody). Kept, although their ranges look public or odd: Hamachi (25.0.0.0/8), Radmin VPN (26.0.0.0/8), Tailscale and
carrier-grade NAT space (100.64.0.0/10).

Windows: GetAdaptersAddresses (iphlpapi) through ctypes; every address carries its on-link prefix length.
Linux: the SIOCGIFCONF, SIOCGIFFLAGS and SIOCGIFNETMASK ioctls, and `ip -o -4 addr show up` for the secondary
addresses SIOCGIFCONF does not list (an address added without a label). Both readers return plain rows that
usable_rows() filters, so the filter and the parsers are tested without the operating system.
"""
import ipaddress
import os
import socket
import struct
import subprocess
import sys

LOOPBACK = ipaddress.IPv4Network('127.0.0.0/8')
LINK_LOCAL = ipaddress.IPv4Network('169.254.0.0/16')
# A /31 or /32 has no broadcast address (RFC 3021 point-to-point links, and the /32 adapters of WireGuard and
# Tailscale): directed broadcast is only sent on networks with at least four addresses.
BROADCAST_PREFIX_MAX = 30

# Windows (iphlpapi.h, ws2def.h, ipifcons.h).
AF_INET = 2
GAA_FLAG_SKIP_ANYCAST = 0x2
GAA_FLAG_SKIP_MULTICAST = 0x4
GAA_FLAG_SKIP_DNS_SERVER = 0x8
ERROR_BUFFER_OVERFLOW = 111
ERROR_NO_DATA = 232
IF_OPER_STATUS_UP = 1
IF_TYPE_SOFTWARE_LOOPBACK = 24
# IP_DAD_STATE: 0 invalid, 1 tentative, 2 duplicate, 3 deprecated, 4 preferred. A tentative or duplicate address is
# not usable yet (or never); a deprecated one still works.
DAD_STATES_USABLE = (3, 4)
ADAPTER_BUFFER_START = 16 * 1024
ADAPTER_BUFFER_TRIES = 4

# Linux (linux/sockios.h, linux/if.h).
SIOCGIFCONF = 0x8912
SIOCGIFFLAGS = 0x8913
SIOCGIFNETMASK = 0x891B
IFF_UP = 0x1
IFF_BROADCAST = 0x2
IFF_LOOPBACK = 0x8
IFNAMSIZ = 16
IFCONF_ENTRIES_MAX = 256
IP_COMMAND_TIMEOUT = 3


def ifreq_size():
    """sizeof(struct ifreq): the name plus a union whose largest member is struct ifmap (24 bytes on 64-bit)."""
    if struct.calcsize('P') == 8:
        return IFNAMSIZ + 24
    return IFNAMSIZ + 16


def prefix_netmask(prefix):
    """'255.255.255.0' for 24."""
    return str(ipaddress.IPv4Network(f'0.0.0.0/{prefix}').netmask)


def netmask_prefix(netmask):
    """24 for '255.255.255.0'; None for a mask that is not contiguous."""
    try:
        return ipaddress.IPv4Network(f'0.0.0.0/{netmask}').prefixlen
    except ValueError:
        return None


def parse_ipv4(text):
    """An IPv4Address for a canonical dotted quad ('10.0.0.5'), else None (no host names, no '010.0.0.5')."""
    if not isinstance(text, str):
        return None
    parts = text.split('.')
    if len(parts) != 4:
        return None
    for part in parts:
        if not part.isdigit() or not part.isascii() or str(int(part)) != part or int(part) > 255:
            return None
    return ipaddress.IPv4Address(text)


def usable_rows(rows):
    """The contract's dicts from raw rows (name, ip, prefix, broadcast_capable), filtered and de-duplicated.

    A row whose prefix is not 1..32 is taken as a /32: a prefix 0 would put every address of the internet on this
    "subnet" and best_for() would then prefer anything."""
    entries = []
    seen = set()
    for name, ip, prefix, broadcast_capable in rows:
        address = parse_ipv4(ip)
        if address is None or ip in seen:
            continue
        if address in LOOPBACK or address in LINK_LOCAL:
            continue
        if address.is_multicast or address.is_unspecified or ip == '255.255.255.255':
            continue
        if not isinstance(prefix, int) or not 1 <= prefix <= 32:
            prefix = 32
        network = ipaddress.IPv4Network(f'{ip}/{prefix}', strict=False)
        broadcast = None
        if broadcast_capable and prefix <= BROADCAST_PREFIX_MAX:
            broadcast = str(network.broadcast_address)
        seen.add(ip)
        entries.append({'ip': ip, 'netmask': str(network.netmask), 'broadcast': broadcast, 'name': name,
                        'prefix': prefix})
    return entries


def addresses():
    """Every usable IPv4 address of every interface that is up: [{ip, netmask, broadcast, name, prefix}].

    broadcast is the directed broadcast address, or None on a /31, a /32 or an interface without broadcast.
    An operating system this module cannot read gives an empty list, never an exception: discovery then simply
    finds nothing, and joining by code or address still works."""
    try:
        if os.name == 'nt':
            rows = windows_rows()
        elif sys.platform.startswith('linux'):
            rows = linux_rows()
        else:
            rows = ip_command_rows()
    except OSError:
        rows = []
    return usable_rows(rows)


# ---- Windows: GetAdaptersAddresses ---------------------------------------------------------------------------------

def _windows_structures():
    """The ctypes layouts of IP_ADAPTER_UNICAST_ADDRESS_LH and IP_ADAPTER_ADDRESSES_LH, as far as this module reads.

    Built on demand so the module imports on every system. Only pointers into the API's own buffer are ever cast to
    these types, so declaring just the leading fields is enough."""
    import ctypes

    class SocketAddress(ctypes.Structure):
        _fields_ = [('lpSockaddr', ctypes.c_void_p), ('iSockaddrLength', ctypes.c_int)]

    class UnicastAddress(ctypes.Structure):
        pass

    UnicastAddress._fields_ = [
        ('Length', ctypes.c_ulong),
        ('Flags', ctypes.c_ulong),
        ('Next', ctypes.POINTER(UnicastAddress)),
        ('Address', SocketAddress),
        ('PrefixOrigin', ctypes.c_int),
        ('SuffixOrigin', ctypes.c_int),
        ('DadState', ctypes.c_int),
        ('ValidLifetime', ctypes.c_ulong),
        ('PreferredLifetime', ctypes.c_ulong),
        ('LeaseLifetime', ctypes.c_ulong),
        ('OnLinkPrefixLength', ctypes.c_ubyte),
    ]

    class AdapterAddresses(ctypes.Structure):
        pass

    AdapterAddresses._fields_ = [
        ('Length', ctypes.c_ulong),
        ('IfIndex', ctypes.c_ulong),
        ('Next', ctypes.POINTER(AdapterAddresses)),
        ('AdapterName', ctypes.c_char_p),
        ('FirstUnicastAddress', ctypes.POINTER(UnicastAddress)),
        ('FirstAnycastAddress', ctypes.c_void_p),
        ('FirstMulticastAddress', ctypes.c_void_p),
        ('FirstDnsServerAddress', ctypes.c_void_p),
        ('DnsSuffix', ctypes.c_wchar_p),
        ('Description', ctypes.c_wchar_p),
        ('FriendlyName', ctypes.c_wchar_p),
        ('PhysicalAddress', ctypes.c_ubyte * 8),
        ('PhysicalAddressLength', ctypes.c_ulong),
        ('Flags', ctypes.c_ulong),
        ('Mtu', ctypes.c_ulong),
        ('IfType', ctypes.c_ulong),
        ('OperStatus', ctypes.c_int),
    ]
    return SocketAddress, UnicastAddress, AdapterAddresses


def adapter_rows(first_adapter):
    """Rows (name, ip, prefix, True) from a GetAdaptersAddresses list (a ctypes pointer to its first adapter).

    Adapters that are not up and the software loopback adapter are skipped, as are addresses that duplicate address
    detection has not accepted (yet). Windows has no broadcast flag per adapter; the prefix rule decides."""
    import ctypes
    rows = []
    adapter_pointer = first_adapter
    while adapter_pointer:
        adapter = adapter_pointer.contents
        usable = adapter.OperStatus == IF_OPER_STATUS_UP and adapter.IfType != IF_TYPE_SOFTWARE_LOOPBACK
        name = adapter.FriendlyName or (adapter.AdapterName or b'').decode('ascii', 'replace')
        unicast_pointer = adapter.FirstUnicastAddress if usable else None
        while unicast_pointer:
            unicast = unicast_pointer.contents
            length = unicast.Address.iSockaddrLength
            if unicast.Address.lpSockaddr and length >= 8 and unicast.DadState in DAD_STATES_USABLE:
                sockaddr = ctypes.string_at(unicast.Address.lpSockaddr, length)
                # struct sockaddr_in: family (little endian u16 on Windows), port, then the address bytes.
                family = struct.unpack_from('<H', sockaddr)[0]
                if family == AF_INET:
                    ip = socket.inet_ntoa(sockaddr[4:8])
                    rows.append((name, ip, int(unicast.OnLinkPrefixLength), True))
            unicast_pointer = unicast.Next
        adapter_pointer = adapter.Next
    return rows


def windows_rows():
    import ctypes
    _, _, adapter_type = _windows_structures()
    function = ctypes.WinDLL('iphlpapi').GetAdaptersAddresses
    function.argtypes = [ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_void_p,
                         ctypes.POINTER(ctypes.c_ulong)]
    function.restype = ctypes.c_ulong
    flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
    size = ctypes.c_ulong(ADAPTER_BUFFER_START)
    result = ERROR_BUFFER_OVERFLOW
    buffer = None
    # The adapter list can grow between the size query and the call (a VPN connecting): ask again, a few times.
    for _ in range(ADAPTER_BUFFER_TRIES):
        buffer = ctypes.create_string_buffer(size.value)
        result = function(AF_INET, flags, None, buffer, ctypes.byref(size))
        if result != ERROR_BUFFER_OVERFLOW:
            break
    if result == ERROR_NO_DATA:
        return []
    if result != 0:
        raise OSError(f'GetAdaptersAddresses failed: {result}')
    first = ctypes.cast(buffer, ctypes.POINTER(adapter_type))
    # buffer stays referenced until the walk is over: the list lives inside it.
    rows = adapter_rows(first)
    del buffer
    return rows


# ---- Linux: ioctls and `ip -o -4 addr` ------------------------------------------------------------------------------

def parse_ifconf(data):
    """(name, ip) pairs from the ifreq array SIOCGIFCONF filled in."""
    size = ifreq_size()
    pairs = []
    for offset in range(0, len(data) - size + 1, size):
        name = data[offset:offset + IFNAMSIZ].split(b'\0', 1)[0].decode('ascii', 'replace')
        # struct sockaddr_in after the name: family (u16), port (u16), then the address.
        family = struct.unpack_from('=H', data, offset + IFNAMSIZ)[0]
        if family != socket.AF_INET:
            continue
        ip = socket.inet_ntoa(data[offset + IFNAMSIZ + 4:offset + IFNAMSIZ + 8])
        pairs.append((name, ip))
    return pairs


def ioctl_rows():
    import array
    import fcntl
    size = ifreq_size()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        buffer = array.array('B', bytes(size * IFCONF_ENTRIES_MAX))
        address, _ = buffer.buffer_info()
        # struct ifconf {int ifc_len; char *ifc_buf;} with native alignment.
        request = struct.pack('iP', len(buffer), address)
        answer = fcntl.ioctl(probe.fileno(), SIOCGIFCONF, request)
        length = struct.unpack('iP', answer)[0]
        pairs = parse_ifconf(buffer.tobytes()[:length])
        rows = []
        for name, ip in pairs:
            query = struct.pack('256s', name.encode('ascii', 'replace')[:IFNAMSIZ - 1])
            flags = struct.unpack_from('=H', fcntl.ioctl(probe.fileno(), SIOCGIFFLAGS, query), IFNAMSIZ)[0]
            if not flags & IFF_UP or flags & IFF_LOOPBACK:
                continue
            mask_answer = fcntl.ioctl(probe.fileno(), SIOCGIFNETMASK, query)
            netmask = socket.inet_ntoa(mask_answer[IFNAMSIZ + 4:IFNAMSIZ + 8])
            prefix = netmask_prefix(netmask)
            rows.append((name, ip, prefix, bool(flags & IFF_BROADCAST)))
    return rows


def parse_ip_addr(text):
    """Rows from `ip -o -4 addr show up`, one line per address:

        2: eth0    inet 192.168.1.20/24 brd 192.168.1.255 scope global dynamic eth0\\       valid_lft ...
        7: wg0    inet 10.8.0.2/32 scope global wg0\\ ...
        9: ppp0    inet 10.64.0.9 peer 10.64.0.1/32 scope global ppp0\\ ...

    A 'peer' address is a point-to-point link: the local address has no prefix of its own (a /32 here)."""
    rows = []
    for line in text.splitlines():
        tokens = line.replace('\\', ' ').split()
        if len(tokens) < 4 or 'inet' not in tokens:
            continue
        name = tokens[1].rstrip(':').split('@', 1)[0]
        at = tokens.index('inet')
        if at + 1 >= len(tokens):
            continue
        local = tokens[at + 1]
        point_to_point = 'peer' in tokens
        ip, _, prefix_text = local.partition('/')
        prefix = int(prefix_text) if prefix_text.isdigit() and not point_to_point else 32
        broadcast_capable = 'brd' in tokens and not point_to_point
        rows.append((name, ip, prefix, broadcast_capable))
    return rows


def ip_command_rows():
    try:
        completed = subprocess.run(['ip', '-o', '-4', 'addr', 'show', 'up'], capture_output=True, text=True,
                                   timeout=IP_COMMAND_TIMEOUT, check=False)
    except (OSError, subprocess.SubprocessError):
        return []
    if completed.returncode != 0:
        return []
    return parse_ip_addr(completed.stdout)


def linux_rows():
    """The ioctl rows, plus addresses only `ip` lists (secondary addresses without a label)."""
    try:
        rows = ioctl_rows()
    except OSError:
        rows = []
    known = {row[1] for row in rows}
    for row in ip_command_rows():
        if row[1] not in known:
            rows.append(row)
            known.add(row[1])
    return rows


# ---- choosing an address ------------------------------------------------------------------------------------------

def _networks(interfaces):
    networks = []
    for entry in interfaces or ():
        address = parse_ipv4(entry.get('ip'))
        if address is None:
            continue
        prefix = entry.get('prefix')
        if not isinstance(prefix, int):
            prefix = netmask_prefix(entry.get('netmask') or '255.255.255.255') or 32
        networks.append(ipaddress.IPv4Network(f'{address}/{prefix}', strict=False))
    return networks


def common_prefix(first, second):
    """Leading bits two IPv4 addresses share (32 for equal ones)."""
    return 32 - (int(first) ^ int(second)).bit_length()


def _rank(address, peer, networks):
    """(group, -closeness): group 0 = on the peer's own subnet (or the peer itself), 1 = on one of this PC's
    subnets, 2 = elsewhere; within a group the address sharing more leading bits with the peer comes first."""
    closeness = common_prefix(address, peer) if peer is not None else 0
    if peer is not None:
        if address == peer:
            return 0, -closeness
        for network in networks:
            if address in network and peer in network:
                return 0, -closeness
    for network in networks:
        if address in network:
            return 1, -closeness
    return 2, -closeness


def best_for(peer_ip, candidates, interfaces=None):
    """The candidates (IPv4 strings), the ones on peer_ip's own subnet first.

    Two uses: a host orders its own addresses for a joiner it sees at peer_ip (the joiner's subnet decides), and a
    joiner orders a host's announced candidates with peer_ip = the address the announcement came from (that address
    first, then the candidates on a subnet this PC is on, then the rest). peer_ip may be None: then only "on one of my
    subnets" counts. Subnets are those of this PC's interfaces (addresses(), or `interfaces` in tests); without a
    netmask for a foreign address the longest common prefix with peer_ip breaks ties. Strings that are not dotted
    IPv4 addresses and repeats are dropped; equal ranks keep their given order."""
    if interfaces is None:
        interfaces = addresses()
    networks = _networks(interfaces)
    peer = parse_ipv4(peer_ip)
    ranked = []
    seen = set()
    for index, text in enumerate(candidates):
        address = parse_ipv4(text)
        if address is None or text in seen:
            continue
        seen.add(text)
        group, closeness = _rank(address, peer, networks)
        ranked.append((group, closeness, index, text))
    ranked.sort()
    return [entry[3] for entry in ranked]
