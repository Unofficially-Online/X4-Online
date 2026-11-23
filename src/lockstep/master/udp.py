"""The master's UDP side: the echo (NAT discovery and ping) and the reachability probe (docs/NETPLAY.md).

Echo, on --echo-port:
    client -> master  X4MPECHO|1|<nonce, 16 hex>|<padding to 64 bytes>
    master -> client  X4MPSEEN|1|<nonce>|<ip>:<port>|<dots to the request's size>
                      (the source address the master saw; the padding field is left out when fewer than 2 bytes remain)
The answer goes only to the datagram's source and is never larger than the datagram, so a spoofed source gains an
attacker nothing (no amplification); a request too short to carry the answer is not answered. Padded to the request's
size, the answer crosses the same path MTU as the request did. Per-IP and global token buckets cut floods; a refused
datagram is dropped without an answer.

Probe (POST /api/v1/probe): one TCP connect and one UDP datagram X4MPPROBE|1|<nonce> to the requesting IP only, so the
master cannot be pointed at anyone else. The datagram leaves from a fresh socket, not the echo's: a NAT that let the
host's own echo traffic back in would otherwise pass a port that strangers cannot reach."""
import ipaddress
import re
import socket
import threading

from .limits import RateLimiter

ECHO_MAGIC = b'X4MPECHO'
SEEN_MAGIC = b'X4MPSEEN'
PROBE_MAGIC = 'X4MPPROBE'
PROTOCOL = b'1'
ECHO_NONCE = re.compile(rb'[0-9a-fA-F]{16}')
DATAGRAM_MAX = 1200        # larger requests are not echo requests (the netplay datagram limit)
PROBE_NONCE = re.compile(r'[0-9a-fA-F]{16,64}')
PORT_MIN = 1024            # the probe does not touch well-known service ports
PORT_MAX = 65535


def echo_reply(data, address):
    """The answer to one echo datagram from `address` (ip, port), or None when it is not answered."""
    if len(data) > DATAGRAM_MAX:
        return None
    fields = data.split(b'|', 3)
    if len(fields) < 3 or fields[0] != ECHO_MAGIC or fields[1] != PROTOCOL:
        return None
    if not ECHO_NONCE.fullmatch(fields[2]):
        return None
    reply = SEEN_MAGIC + b'|' + PROTOCOL + b'|' + fields[2] + b'|' + f'{address[0]}:{address[1]}'.encode('ascii')
    room = len(data) - len(reply)
    if room < 0:
        return None
    if room >= 2:   # a '|' and at least one dot; one spare byte is left unpadded
        reply += b'|' + b'.' * (room - 1)
    return reply


class EchoServer:
    """The echo socket and its thread. close() stops it within half a second."""
    def __init__(self, bind, port, limits, log=print):
        self.limits = limits
        self.log = log
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind((bind, port))
        self.socket.settimeout(0.5)
        self.port = self.socket.getsockname()[1]
        self.rates = RateLimiter(limits.echo_per_s, limits.echo_burst, limits.global_echo_per_s, limits.global_echo_burst)
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.serve, name='master-echo', daemon=True)
        self.answered = 0
        self.dropped = 0

    def start(self):
        self.thread.start()
        return self

    def serve(self):
        while not self.stopped.is_set():
            try:
                data, address = self.socket.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                # Windows reports an earlier answer to a closed port here (ICMP port unreachable); keep serving.
                if self.stopped.is_set():
                    return
                continue
            if not self.rates.allow(address[0]):
                self.dropped += 1
                continue
            reply = echo_reply(data, address)
            if reply is None:
                self.dropped += 1
                continue
            try:
                self.socket.sendto(reply, address)
                self.answered += 1
            except OSError:
                pass

    def close(self):
        self.stopped.set()
        self.thread.join(2)
        self.socket.close()


def is_ipv4(text):
    try:
        return isinstance(ipaddress.ip_address(text), ipaddress.IPv4Address)
    except ValueError:
        return False


def probe(ip, udp_port, tcp_port, nonce, timeout=3.0):
    """Try `ip`: a TCP connect to tcp_port (closed at once) and one UDP datagram to udp_port. True when the connect
    succeeded; whether the datagram arrived only the client can tell. The caller passes the request's own source IP."""
    datagram = f'{PROBE_MAGIC}|1|{nonce}'.encode('ascii')
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(datagram, (ip, udp_port))
    except OSError:
        pass
    try:
        with socket.create_connection((ip, tcp_port), timeout=timeout):
            return True
    except OSError:
        return False
