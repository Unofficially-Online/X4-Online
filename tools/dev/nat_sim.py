"""Virtual-time simulation of home routers for the NAT traversal tests (roadmap B7).

Nothing here touches a real network. Datagrams travel through simulated NATs with the
RFC 4787 behaviours: mapping endpoint-independent (eim), address-dependent (adm) or
address-and-port-dependent (apdm, "symmetric"); filtering endpoint-independent (eif),
address-dependent (adf) or address-and-port-dependent (apdf); optional hairpinning,
port allocation (preserve, sequential, random), mapping timeouts, static port mappings
(what PCP/NAT-PMP/UPnP would create) and nested NATs (carrier-grade NAT). The classic
names are presets: full_cone = eim+eif, restricted = eim+adf, port_restricted = eim+apdf,
symmetric = apdm+apdf.

Clients are the real C core (src/native/engine/lockstep_nat.h) through
`test_lockstep_nat_tool driver` (a line protocol on stdin/stdout); the relay side is the
real Rendezvous class of native_gate_rendezvous.py. Every filter decision is taken when a
packet arrives at a NAT, so simultaneous opens behave as on the internet.
"""
import heapq
import importlib.util
from pathlib import Path
import random
import subprocess

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('native_gate_rendezvous_sim', HERE/'native_gate_rendezvous.py')
rendezvous_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rendezvous_module)

PRESETS = {'full_cone': ('eim', 'eif'), 'restricted': ('eim', 'adf'), 'port_restricted': ('eim', 'apdf'),
           'symmetric': ('apdm', 'apdf'), 'address_symmetric': ('adm', 'adf')}


def fmt(addr):
    return f'{addr[0]}:{addr[1]}'


def parse(text):
    if text == '-':
        return None
    host, _, port = text.rpartition(':')
    return host, int(port)


class Mapping:
    def __init__(self, external_port, internal, now):
        self.external_port, self.internal, self.last, self.permits = external_port, internal, now, set()


class Nat:
    def __init__(self, name, ip, mapping='eim', filtering='eif', hairpin=False, allocation='preserve', timeout_ms=120000,
                 wan_ms=10, inbound_refresh=False, seed=0):
        assert mapping in ('eim', 'adm', 'apdm') and filtering in ('eif', 'adf', 'apdf')
        assert allocation in ('preserve', 'sequential', 'random')
        self.name, self.ip, self.mapping, self.filtering = name, ip, mapping, filtering
        self.hairpin, self.allocation, self.timeout_ms, self.wan_ms = hairpin, allocation, timeout_ms, wan_ms
        self.inbound_refresh = inbound_refresh
        self.inside = {}        # ip -> node (host or nested Nat) behind this NAT
        self.realm = None       # the NAT this one sits behind (None: the internet)
        self.mappings, self.by_port, self.static = {}, {}, {}
        self.next_port = 50000
        self.rng = random.Random(seed)
        self.created = 0

    def _key(self, internal, dest):
        return {'eim': (internal,), 'adm': (internal, dest[0]), 'apdm': (internal, dest)}[self.mapping]

    def _expire(self, now):
        for key, m in list(self.mappings.items()):
            if now - m.last >= self.timeout_ms:
                del self.mappings[key]
                self.by_port.pop(m.external_port, None)

    def _allocate(self, internal):
        used = set(self.by_port) | set(self.static.values())
        if self.allocation == 'preserve' and internal[1] not in used:
            return internal[1]
        if self.allocation == 'random':
            while True:
                port = self.rng.randrange(20000, 60000)
                if port not in used:
                    return port
        while self.next_port in used:
            self.next_port += 1
        self.next_port += 1
        return self.next_port - 1

    def outbound(self, internal, dest, now):
        """The public source of a packet from internal to dest; records the filter permit."""
        if internal in self.static:
            return (self.ip, self.static[internal])
        self._expire(now)
        key = self._key(internal, dest)
        m = self.mappings.get(key)
        if m is None:
            m = Mapping(self._allocate(internal), internal, now)
            self.mappings[key] = m
            self.by_port[m.external_port] = m
            self.created += 1
        m.last = now
        m.permits.add(dest[0] if self.filtering == 'adf' else dest)
        return (self.ip, m.external_port)

    def inbound(self, port, source, now):
        """The internal destination of a packet arriving at our public port, or None (dropped)."""
        for internal, external in self.static.items():
            if external == port:
                return internal
        self._expire(now)
        m = self.by_port.get(port)
        if m is None:
            return None
        if self.filtering == 'adf' and source[0] not in m.permits:
            return None
        if self.filtering == 'apdf' and source not in m.permits:
            return None
        if self.inbound_refresh:
            m.last = now
        return m.internal

    def add_static(self, internal, external_port):
        self.static[internal] = external_port


class Network:
    """Event queue + routing. Nodes: Nat, ClientHost, ServerHost (each has .ip, .realm)."""

    def __init__(self, seed=1, lan_ms=1, loss=0.0):
        self.now, self.events, self.sequence = 0, [], 0
        self.internet = {}
        self.lan_ms, self.loss, self.rng = lan_ms, loss, random.Random(seed)
        self.trace = []          # (time, node name, source, data) of every delivery to a host
        self.dropped = 0

    def schedule(self, time, *event):
        self.sequence += 1
        heapq.heappush(self.events, (time, self.sequence, event))

    def place(self, node, realm=None):
        node.realm = realm
        (realm.inside if realm else self.internet)[node.ip] = node
        return node

    def egress(self, realm, source, dest, data, t):
        """A packet leaving source inside realm at time t."""
        inside = realm.inside if realm else self.internet
        node = inside.get(dest[0])
        if node is not None and node is not realm:
            delay = node.wan_ms if realm is None or isinstance(node, Nat) else self.lan_ms
            if realm is None and self.loss and self.rng.random() < self.loss:
                self.dropped += 1
                return
            self.schedule(t + delay, 'arrive', node, source, dest, data)
            return
        if realm is None:
            self.dropped += 1              # nobody owns that address
            return
        if dest[0] == realm.ip:            # hairpin through our own NAT
            if not realm.hairpin:
                self.dropped += 1
                return
            translated = realm.outbound(source, dest, t)
            internal = realm.inbound(dest[1], translated, t)
            target = realm.inside.get(internal[0]) if internal else None
            if target is None:
                self.dropped += 1
                return
            self.schedule(t + 2*self.lan_ms, 'arrive', target, translated, internal, data)
            return
        translated = realm.outbound(source, dest, t)
        self.schedule(t + realm.wan_ms, 'egress', realm.realm, translated, dest, data)

    def arrive(self, node, source, dest, data, t):
        if isinstance(node, Nat):
            internal = node.inbound(dest[1], source, t)
            target = node.inside.get(internal[0]) if internal else None
            if target is None:
                self.dropped += 1
                return
            self.schedule(t + (target.wan_ms if isinstance(target, Nat) else self.lan_ms), 'arrive', target, source, internal, data)
            return
        node.deliver(self, source, dest, data, t)

    def run(self, until, stop=None):
        while self.events and self.events[0][0] <= until:
            t, _, event = heapq.heappop(self.events)
            self.now = t
            kind, *rest = event
            if kind == 'egress':
                self.egress(*rest, t)
            elif kind == 'arrive':
                self.arrive(*rest, t)
            elif kind == 'poll':
                rest[0].poll(self, t, rest[1])
            if stop and stop():
                return
        self.now = max(self.now, until)


class CDriver:
    """The C core behind `test_lockstep_nat_tool driver`."""

    def __init__(self, exe, config):
        self.process = subprocess.Popen([str(exe), 'driver'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        out = self.command('INIT ' + ' '.join(f'{k}={v}' for k, v in config.items()))
        if out != ['OK']:
            raise RuntimeError(f'driver refused INIT: {out}')

    def command(self, line):
        self.process.stdin.write(line + '\n')
        self.process.stdin.flush()
        out = []
        while True:
            reply = self.process.stdout.readline()
            if not reply:
                raise RuntimeError('driver exited')
            reply = reply.rstrip('\r\n')
            if reply == 'END':
                return out
            out.append(reply)

    @staticmethod
    def _sends(lines):
        sends, due = [], None
        for line in lines:
            if line.startswith('SEND '):
                _, dest, data = line.split(' ')
                sends.append((parse(dest), bytes.fromhex(data)))
            elif line.startswith('NEXT '):
                value = int(line.split(' ')[1])
                due = None if value < 0 else value
            elif line.startswith('ERR'):
                raise RuntimeError(line)
        return sends, due

    def recv(self, now, source, data):
        return self._sends(self.command(f'RECV {now} {fmt(source)} {data.hex()}'))

    def poll(self, now):
        return self._sends(self.command(f'POLL {now}'))

    def close(self):
        return self._sends(self.command('CLOSE'))[0]

    def path(self, peer):
        _, k, state, addr, rtt, reason = self.command(f'PATH {peer}')[0].split(' ')
        return state, parse(addr), int(rtt), reason

    def describe(self):
        return self.command('DESCRIBE')[0][5:]

    def quit(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write('QUIT\n')
                self.process.stdin.flush()
            except OSError:
                pass
            try:
                self.process.wait(5)
            except subprocess.TimeoutExpired:
                self.process.kill()


class ClientHost:
    wan_ms = 5     # a host on the internet itself (no NAT): its own access link

    def __init__(self, name, ip, port, driver):
        self.name, self.ip, self.port, self.driver = name, ip, port, driver
        self.due, self.realm = None, None
        self.history = []        # (time, peer, state) on every path change

    def _emit(self, network, sends, due, t):
        for dest, data in sends:
            network.egress(self.realm, (self.ip, self.port), dest, data, t)
        if due is not None and (self.due is None or due < self.due or self.due < t):
            self.due = max(due, t)
            network.schedule(self.due, 'poll', self, self.due)

    def start(self, network, t):
        self._emit(network, *self.driver.poll(t), t)

    def poll(self, network, t, scheduled):
        if scheduled != self.due:
            return               # superseded by an earlier wake-up
        self.due = None
        self._emit(network, *self.driver.poll(t), t)

    def deliver(self, network, source, dest, data, t):
        if dest[1] != self.port:
            network.dropped += 1
            return
        network.trace.append((t, self.name, source, data))
        self._emit(network, *self.driver.recv(t, source, data), t)


class ServerHost:
    wan_ms = 5

    def __init__(self, ip, port, rendezvous):
        self.name, self.ip, self.ports, self.rendezvous, self.realm = 'relay', ip, (port, port+1), rendezvous, None
        self.received = 0

    def deliver(self, network, source, dest, data, t):
        if dest[1] not in self.ports:
            network.dropped += 1
            return
        self.received += 1
        for index, to, packet in self.rendezvous.receive(data, source, self.ports.index(dest[1]), t/1000.0):
            network.egress(None, (self.ip, self.ports[index]), to, packet, t)


class Scenario:
    """A relay on the internet plus clients behind simulated NATs, driven in virtual milliseconds."""
    TOKEN = '0123456789abcdef0123456789abcdef'

    def __init__(self, exe, clients=2, seed=1, loss=0.0, server_ip='198.51.100.10', port=47000, **server_options):
        self.exe, self.clients = exe, clients
        self.network = Network(seed=seed, loss=loss)
        self.rendezvous = rendezvous_module.Rendezvous(self.TOKEN, clients=clients, **server_options)
        self.server = self.network.place(ServerHost(server_ip, port, self.rendezvous))
        self.hosts = {}
        self.seed = seed

    def nat(self, name, ip, kind='full_cone', realm=None, **options):
        mapping, filtering = PRESETS[kind] if kind in PRESETS else kind
        return self.network.place(Nat(name, ip, mapping, filtering, seed=self.seed, **options), realm)

    def client(self, slot, ip, realm=None, port=5000, mapped_port=None, start=0, **config):
        options = {'slot': slot, 'clients': self.clients, 'token': self.TOKEN, 'inst': f'{0x1000*slot+self.seed:016x}',
                   's0': fmt((self.server.ip, self.server.ports[0])), 's1': fmt((self.server.ip, self.server.ports[1])),
                   'lan': fmt((ip, port))}
        if mapped_port is not None:
            realm.add_static((ip, port), mapped_port)
            options['map'] = fmt((realm.ip, mapped_port))
        options.update(config)
        host = self.network.place(ClientHost(f'client{slot}', ip, port, CDriver(self.exe, options)), realm)
        self.hosts[slot] = host
        self.network.schedule(start, 'poll', host, start)
        host.due = start
        return host

    def path(self, slot, peer):
        return self.hosts[slot].driver.path(peer)

    def run(self, until):
        self.network.run(until)

    def run_until_decided(self, limit, pairs=None):
        """Advance until every (slot, peer) is direct or relay (or limit); returns the decision times."""
        pairs = pairs or [(a, b) for a in self.hosts for b in self.hosts if a != b]
        decided = {}
        step = 50
        t = self.network.now
        while t < limit and len(decided) < len(pairs):
            t += step
            self.network.run(t)
            for pair in pairs:
                if pair not in decided and self.path(*pair)[0] != 'pending':
                    decided[pair] = t
        return decided

    def close(self, slot):
        host = self.hosts[slot]
        for dest, data in host.driver.close():
            self.network.egress(host.realm, (host.ip, host.port), dest, data, self.network.now)

    def shutdown(self):
        for host in self.hosts.values():
            host.driver.quit()
