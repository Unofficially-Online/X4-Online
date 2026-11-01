"""Loopback stand-ins for a home router's port-mapping services (roadmap B7 tests).

FakePcp answers PCP MAP (RFC 6887) and NAT-PMP (RFC 6886) on 127.0.0.1; FakeIgd answers
an SSDP search and the UPnP IGD WANIPConnection SOAP actions on 127.0.0.1. Both record
every request so tests can check lease, renewal and release. They bind loopback only and
never contact a real gateway. Modes model what routers do: grant, refuse, speak only
NAT-PMP, stay silent, report a conflict (718), accept only permanent leases (725), hand
out a private external address (double NAT).
"""
import http.server
import socket
import struct
import threading

PCP_RESULTS = {'success': 0, 'unsupported_version': 1, 'not_authorized': 2, 'no_resources': 8}


class FakePcp:
    def __init__(self, mode='pcp', external_ip='203.0.113.7', lifetime_cap=7200, epoch=1000):
        assert mode in ('pcp', 'natpmp', 'refuse', 'silent')
        self.mode, self.external_ip, self.lifetime_cap, self.epoch = mode, external_ip, lifetime_cap, epoch
        self.requests = []
        self.mappings = {}
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

    def _grant(self, internal, suggested, lifetime):
        if not lifetime:
            self.mappings.pop(internal, None)
            return 0, 0
        external = self.mappings.get(internal) or suggested or internal
        self.mappings[internal] = external
        return external, min(lifetime, self.lifetime_cap)

    def handle(self, data):
        if self.mode == 'silent' or len(data) < 2:
            return None
        if data[0] == 2 and len(data) >= 60 and data[1] == 1:
            lifetime = struct.unpack('>I', data[4:8])[0]
            nonce, internal, suggested = data[24:36], *struct.unpack('>HH', data[40:44])
            self.requests.append({'protocol': 'pcp', 'lifetime': lifetime, 'nonce': nonce.hex(), 'internal': internal,
                                  'suggested': suggested, 'client': socket.inet_ntoa(data[20:24])})
            if self.mode == 'natpmp':
                return struct.pack('>BBHI', 0, 128+data[1], 1, self.epoch)    # NAT-PMP: unsupported version
            result = PCP_RESULTS['not_authorized'] if self.mode == 'refuse' else 0
            external, granted = (0, 0) if result else self._grant(internal, suggested, lifetime)
            header = struct.pack('>BBBBII', 2, 0x81, 0, result, granted, self.epoch) + bytes(12)
            payload = nonce + bytes([17, 0, 0, 0]) + struct.pack('>HH', internal, external) + bytes(10) + b'\xff\xff' + \
                socket.inet_aton(self.external_ip if not result else '0.0.0.0')
            return header + payload
        if data[0] == 0 and data[1] == 0:
            self.requests.append({'protocol': 'natpmp', 'op': 'address'})
            if self.mode == 'refuse':
                return struct.pack('>BBHI', 0, 128, 2, self.epoch)
            return struct.pack('>BBHI', 0, 128, 0, self.epoch) + socket.inet_aton(self.external_ip)
        if data[0] == 0 and data[1] == 1 and len(data) >= 12:
            internal, suggested, lifetime = struct.unpack('>HHI', data[4:12])
            self.requests.append({'protocol': 'natpmp', 'op': 'map', 'internal': internal, 'suggested': suggested, 'lifetime': lifetime})
            if self.mode == 'refuse':
                return struct.pack('>BBHI', 0, 129, 2, self.epoch)
            external, granted = self._grant(internal, suggested, lifetime)
            return struct.pack('>BBHIHHI', 0, 129, 0, self.epoch, internal, external, granted)
        return None

    def close(self):
        self.stopped.set()
        self.sock.close()
        self.thread.join(1)


DESCRIPTION = '''<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion>
<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType><controlURL>/ctl/L3F</controlURL></service></serviceList>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service></serviceList>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>
<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId><controlURL>/ctl/IPConn</controlURL></service></serviceList>
</device></deviceList></device></deviceList></device></root>
'''


def tag(body, name):
    start = body.find(f'<{name}>')
    end = body.find(f'</{name}>')
    return body[start+len(name)+2:end] if start >= 0 and end > start else None


class FakeIgd:
    def __init__(self, mode='grant', external_ip='203.0.113.9', chunked=False):
        assert mode in ('grant', 'conflict_first', 'permanent_only', 'refuse')
        self.mode, self.external_ip, self.chunked = mode, external_ip, chunked
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
                    self.respond(200, DESCRIPTION)
                else:
                    self.respond(404, '')

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
                action = (self.headers.get('SOAPAction') or '').strip('"').rpartition('#')[2]
                record = {'action': action, 'path': self.path}
                for field in ('NewExternalPort', 'NewInternalPort', 'NewInternalClient', 'NewLeaseDuration', 'NewProtocol'):
                    if tag(body, field) is not None:
                        record[field] = tag(body, field)
                owner.actions.append(record)
                if self.path != '/ctl/IPConn':
                    return self.respond(500, owner.error(401))
                if action == 'GetExternalIPAddress':
                    return self.respond(200, owner.envelope(f'<u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1">'
                                                            f'<NewExternalIPAddress>{owner.external_ip}</NewExternalIPAddress></u:GetExternalIPAddressResponse>'))
                if action == 'AddPortMapping':
                    if owner.mode == 'refuse':
                        return self.respond(500, owner.error(606))
                    if owner.mode == 'conflict_first' and not owner.conflicted:
                        owner.conflicted = True
                        return self.respond(500, owner.error(718))
                    if owner.mode == 'permanent_only' and record.get('NewLeaseDuration') != '0':
                        return self.respond(500, owner.error(725))
                    owner.mappings[record['NewExternalPort']] = record
                    return self.respond(200, owner.envelope('<u:AddPortMappingResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"/>'))
                if action == 'DeletePortMapping':
                    owner.mappings.pop(record.get('NewExternalPort'), None)
                    return self.respond(200, owner.envelope('<u:DeletePortMappingResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"/>'))
                return self.respond(500, owner.error(401))

        self.http = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.http_thread = threading.Thread(target=self.http.serve_forever, kwargs={'poll_interval': 0.05}, daemon=True)
        self.http_thread.start()
        self.ssdp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.ssdp.bind(('127.0.0.1', 0))
        self.ssdp.settimeout(0.1)
        self.ssdp_address = self.ssdp.getsockname()
        self.searches = []
        self.stopped = threading.Event()
        self.ssdp_thread = threading.Thread(target=self._ssdp, daemon=True)
        self.ssdp_thread.start()

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
            if text.startswith('M-SEARCH * HTTP/1.1') and 'InternetGatewayDevice' in text:
                port = self.http.server_address[1]
                reply = ('HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n'
                         f'USN: uuid:fake::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\nEXT:\r\nSERVER: fake/1.0 UPnP/1.0\r\n'
                         f'LOCATION: http://127.0.0.1:{port}/rootDesc.xml\r\n\r\n')
                self.ssdp.sendto(reply.encode(), source)

    def close(self):
        self.stopped.set()
        self.ssdp.close()
        self.http.shutdown()
        self.http.server_close()
        self.ssdp_thread.join(1)
