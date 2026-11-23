"""Loopback stand-ins for a home router's port-mapping services (roadmap B7 tests, netplay package 2 tests).

FakePcp answers PCP MAP (RFC 6887) and NAT-PMP (RFC 6886) on 127.0.0.1; FakeIgd answers
an SSDP search and the UPnP IGD WANIPConnection/WANPPPConnection SOAP actions on 127.0.0.1.
Both record every request so tests can check lease, renewal and release. They bind loopback
only and never contact a real gateway. Modes model what routers do: grant, refuse, speak only
NAT-PMP, stay silent, report a conflict (718), accept only permanent leases (725), hand
out a private external address (double NAT).

UDP and TCP mappings are kept apart (PCP protocol 17/6, NAT-PMP opcodes 1/2, UPnP NewProtocol).
FakePcp(nonce_check=True) refuses a MAP for an existing mapping with another nonce, as RFC 6887
11.3 asks of a real server; FakeIgd answers GetSpecificPortMappingEntry from its table, which a
test may fill beforehand (a stale or foreign mapping).
"""
import http.server
import socket
import struct
import threading

PCP_RESULTS = {'success': 0, 'unsupported_version': 1, 'not_authorized': 2, 'no_resources': 8}
PCP_TRANSPORTS = {17: 'udp', 6: 'tcp'}
NATPMP_TRANSPORTS = {1: 'udp', 2: 'tcp'}


class FakePcp:
    def __init__(self, mode='pcp', external_ip='203.0.113.7', lifetime_cap=7200, epoch=1000, nonce_check=False):
        assert mode in ('pcp', 'natpmp', 'refuse', 'silent')
        self.mode = mode
        self.external_ip = external_ip
        self.lifetime_cap = lifetime_cap
        self.epoch = epoch
        self.nonce_check = nonce_check
        self.requests = []
        self.mappings = {}          # (transport, internal port) -> external port
        self.nonces = {}            # (transport, internal port) -> PCP nonce of the mapping
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.settimeout(0.1)
        self.address = self.sock.getsockname()
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
            reply = self.handle(data)
            if reply:
                self.sock.sendto(reply, source)

    def _grant(self, key, suggested, lifetime):
        if not lifetime:
            self.mappings.pop(key, None)
            self.nonces.pop(key, None)
            return 0, 0
        external = self.mappings.get(key) or suggested or key[1]
        self.mappings[key] = external
        return external, min(lifetime, self.lifetime_cap)

    def handle(self, data):
        if self.mode == 'silent' or len(data) < 2:
            return None
        if data[0] == 2 and len(data) >= 60 and data[1] == 1:
            return self._pcp_map(data)
        if data[0] == 0 and data[1] == 0:
            self.requests.append({'protocol': 'natpmp', 'op': 'address'})
            if self.mode == 'refuse':
                return struct.pack('>BBHI', 0, 128, 2, self.epoch)
            return struct.pack('>BBHI', 0, 128, 0, self.epoch) + socket.inet_aton(self.external_ip)
        if data[0] == 0 and data[1] in NATPMP_TRANSPORTS and len(data) >= 12:
            return self._natpmp_map(data)
        return None

    def _pcp_map(self, data):
        lifetime = struct.unpack('>I', data[4:8])[0]
        nonce = data[24:36]
        protocol = data[36]
        internal, suggested = struct.unpack('>HH', data[40:44])
        transport = PCP_TRANSPORTS.get(protocol, str(protocol))
        self.requests.append({'protocol': 'pcp', 'lifetime': lifetime, 'nonce': nonce.hex(), 'internal': internal,
                              'suggested': suggested, 'client': socket.inet_ntoa(data[20:24]), 'transport': transport})
        if self.mode == 'natpmp':
            return struct.pack('>BBHI', 0, 128 + data[1], 1, self.epoch)    # NAT-PMP: unsupported version
        key = (transport, internal)
        result = PCP_RESULTS['not_authorized'] if self.mode == 'refuse' else 0
        if not result and self.nonce_check and key in self.nonces and self.nonces[key] != nonce:
            result = PCP_RESULTS['not_authorized']     # RFC 6887 11.3: only the mapping's own nonce may change it
        if result:
            external, granted = 0, 0
        else:
            external, granted = self._grant(key, suggested, lifetime)
            if lifetime:
                self.nonces[key] = nonce
        header = struct.pack('>BBBBII', 2, 0x81, 0, result, granted, self.epoch) + bytes(12)
        external_ip = self.external_ip if not result else '0.0.0.0'
        payload = (nonce + bytes([protocol, 0, 0, 0]) + struct.pack('>HH', internal, external) + bytes(10) +
                   b'\xff\xff' + socket.inet_aton(external_ip))
        return header + payload

    def _natpmp_map(self, data):
        opcode = data[1]
        internal, suggested, lifetime = struct.unpack('>HHI', data[4:12])
        self.requests.append({'protocol': 'natpmp', 'op': 'map', 'internal': internal, 'suggested': suggested,
                              'lifetime': lifetime, 'transport': NATPMP_TRANSPORTS[opcode]})
        if self.mode == 'refuse':
            return struct.pack('>BBHI', 0, 128 + opcode, 2, self.epoch)
        external, granted = self._grant((NATPMP_TRANSPORTS[opcode], internal), suggested, lifetime)
        return struct.pack('>BBHIHHI', 0, 128 + opcode, 0, self.epoch, internal, external, granted)

    def close(self):
        self.stopped.set()
        self.sock.close()
        self.thread.join(1)


WAN_IP = 'urn:schemas-upnp-org:service:WANIPConnection:1'

DESCRIPTION = '''<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion>
<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:{version}</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType><controlURL>/ctl/L3F</controlURL></service></serviceList>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:{version}</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service></serviceList>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:{version}</deviceType>
<serviceList><service><serviceType>{service}</serviceType>
<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId><controlURL>/ctl/IPConn</controlURL></service></serviceList>
</device></deviceList></device></deviceList></device></root>
'''


def tag(body, name):
    start = body.find(f'<{name}>')
    end = body.find(f'</{name}>')
    return body[start+len(name)+2:end] if start >= 0 and end > start else None


class FakeIgd:
    """service: the WAN connection service the description lists (WANIPConnection:1 by default, the
    one the C core's tests expect; WANIPConnection:2 or WANPPPConnection:1 for the others).
    mappings: (NewProtocol, NewExternalPort as text) -> the AddPortMapping record."""

    def __init__(self, mode='grant', external_ip='203.0.113.9', chunked=False, service=WAN_IP, version=1):
        assert mode in ('grant', 'conflict_first', 'permanent_only', 'refuse')
        self.mode = mode
        self.external_ip = external_ip
        self.chunked = chunked
        self.service = service
        self.version = version
        self.actions = []
        self.mappings = {}
        self.conflicted = False
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def respond(self, status, body):
                payload = body.encode()
                head = f'HTTP/1.1 {status} {"OK" if status == 200 else "Internal Server Error"}\r\nContent-Type: text/xml\r\nConnection: close\r\n'
                if owner.chunked:
                    parts = [payload[i:i+50] for i in range(0, len(payload), 50)]
                    payload = b''.join(b'%x\r\n%s\r\n' % (len(p), p) for p in parts) + b'0\r\n\r\n'
                    head += 'Transfer-Encoding: chunked\r\n'
                else:
                    head += f'Content-Length: {len(payload)}\r\n'
                self.wfile.write(head.encode() + b'\r\n' + payload)
                self.close_connection = True

            def do_GET(self):
                owner.actions.append({'action': 'GET', 'path': self.path})
                if self.path == '/rootDesc.xml':
                    self.respond(200, DESCRIPTION.format(version=owner.version, service=owner.service))
                else:
                    self.respond(404, '')

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
                service, _, action = (self.headers.get('SOAPAction') or '').strip('"').rpartition('#')
                record = {'action': action, 'path': self.path}
                for field in ('NewExternalPort', 'NewInternalPort', 'NewInternalClient', 'NewLeaseDuration', 'NewProtocol',
                              'NewPortMappingDescription'):
                    if tag(body, field) is not None:
                        record[field] = tag(body, field)
                owner.actions.append(record)
                if self.path != '/ctl/IPConn' or service != owner.service:
                    return self.respond(500, owner.error(401))
                return owner.answer(self, action, record)

        self.http = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.http_thread = threading.Thread(target=self.http.serve_forever, kwargs={'poll_interval': 0.05}, daemon=True)
        self.http_thread.start()
        self.ssdp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.ssdp.bind(('127.0.0.1', 0))
        self.ssdp.settimeout(0.1)
        self.ssdp_address = self.ssdp.getsockname()
        self.searches = []
        self.search_sources = []
        self.stopped = threading.Event()
        self.ssdp_thread = threading.Thread(target=self._ssdp, daemon=True)
        self.ssdp_thread.start()

    def answer(self, handler, action, record):
        key = (record.get('NewProtocol'), record.get('NewExternalPort'))
        if action == 'GetExternalIPAddress':
            return handler.respond(200, self.envelope(f'<u:GetExternalIPAddressResponse xmlns:u="{self.service}">'
                                                      f'<NewExternalIPAddress>{self.external_ip}</NewExternalIPAddress>'
                                                      '</u:GetExternalIPAddressResponse>'))
        if action == 'GetSpecificPortMappingEntry':
            entry = self.mappings.get(key)
            if entry is None:
                return handler.respond(500, self.error(714))      # NoSuchEntryInArray
            fields = ''.join(f'<{name}>{entry.get(name, "")}</{name}>' for name in
                             ('NewInternalPort', 'NewInternalClient', 'NewPortMappingDescription', 'NewLeaseDuration'))
            return handler.respond(200, self.envelope(f'<u:GetSpecificPortMappingEntryResponse xmlns:u="{self.service}">'
                                                      f'{fields}<NewEnabled>1</NewEnabled>'
                                                      '</u:GetSpecificPortMappingEntryResponse>'))
        if action == 'AddPortMapping':
            if self.mode == 'refuse':
                return handler.respond(500, self.error(606))
            if self.mode == 'conflict_first' and not self.conflicted:
                self.conflicted = True
                return handler.respond(500, self.error(718))
            if self.mode == 'permanent_only' and record.get('NewLeaseDuration') != '0':
                return handler.respond(500, self.error(725))
            existing = self.mappings.get(key)
            if existing is not None and existing.get('NewInternalClient') != record.get('NewInternalClient'):
                return handler.respond(500, self.error(718))     # another device holds this external port
            self.mappings[key] = record
            return handler.respond(200, self.envelope(f'<u:AddPortMappingResponse xmlns:u="{self.service}"/>'))
        if action == 'DeletePortMapping':
            # Answered 200 even for an unknown entry, as before (the C core's tests rely on it; a real router says 714).
            self.mappings.pop(key, None)
            return handler.respond(200, self.envelope(f'<u:DeletePortMappingResponse xmlns:u="{self.service}"/>'))
        return handler.respond(500, self.error(401))

    @staticmethod
    def envelope(inner):
        return ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
                f's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>{inner}</s:Body></s:Envelope>')

    def error(self, code):
        return self.envelope('<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>'
                             f'<UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>{code}</errorCode>'
                             '<errorDescription>refused</errorDescription></UPnPError></detail></s:Fault>')

    def _ssdp(self):
        while not self.stopped.is_set():
            try:
                data, source = self.ssdp.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                continue
            except OSError:
                return
            text = data.decode('ascii', 'replace')
            self.searches.append(text)
            self.search_sources.append(source)
            if text.startswith('M-SEARCH * HTTP/1.1') and 'InternetGatewayDevice' in text:
                port = self.http.server_address[1]
                device = f'urn:schemas-upnp-org:device:InternetGatewayDevice:{self.version}'
                reply = (f'HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: {device}\r\n'
                         f'USN: uuid:fake::{device}\r\nEXT:\r\nSERVER: fake/1.0 UPnP/1.0\r\n'
                         f'LOCATION: http://127.0.0.1:{port}/rootDesc.xml\r\n\r\n')
                self.ssdp.sendto(reply.encode(), source)

    def close(self):
        self.stopped.set()
        self.ssdp.close()
        self.http.shutdown()
        self.http.server_close()
        self.ssdp_thread.join(1)
