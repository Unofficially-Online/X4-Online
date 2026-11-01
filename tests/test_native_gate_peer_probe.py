"""LuaJIT probe checks for opt-in peer links (native_gate_peer_window)."""
import importlib.util
from pathlib import Path
import unittest
from lupa.luajit21 import LuaError

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('native_gate_lookahead_probe_for_peer', ROOT/'tests/test_native_gate_lookahead_probe.py')
probe = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(probe)
SOURCE = probe.SOURCE
BASE = probe.LookaheadProbeTests


class PeerProbeTests(unittest.TestCase):
    values, logs, admitted = BASE.values, BASE.logs, BASE.admitted

    def setUp(self, config="slot='p2',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=3"):
        BASE.setUp(self, config)
        self.lua.execute('''state.clients=2;state.inputs={'',''};state.lookahead=1
          local load=package.loadlib
          package.loadlib=function(path) local gate=load(path)()
            gate.peer_links=function(m,p) calls[#calls+1]='peer '..m..(p and ' '..p or '');return peer_result or 1 end
            gate.peer_status=function() return 'peer=active window=3 sent=9 received=9 peer_runs=8' end
            if no_peer then gate.peer_links=nil end
            return function() return gate end end''')

    def test_window_is_configured_after_the_lookahead_before_boot_and_after_reload(self):
        self.admitted(inputs="{'',''}")
        self.assertEqual(self.values('calls'), ['wire 3/2', 'lookahead 1', 'peer 3', 'boot', 'wire 3/2', 'lookahead 1', 'peer 3', 'boot'])
        self.assertTrue(any(line.endswith('ARMED slot=p2 hash_scope=clock wire=3 clients=2 lookahead=1 peer_window=3')
                            for line in self.logs()), self.logs())
        self.assertFalse(any('ERROR' in line for line in self.logs()), self.logs())
        self.lua.execute('state.active=0;update()')
        self.assertTrue(any('PEER_STOP peer=active window=3 sent=9' in line for line in self.logs()), self.logs())

    def test_records_only_window_zero_is_still_configured(self):
        self.setUp("slot='p2',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=0")
        self.admitted(inputs="{'',''}")
        self.assertIn('peer 0', self.values('calls'))
        self.assertTrue(any(line.endswith('lookahead=1 peer_window=0') for line in self.logs()), self.logs())

    def test_dll_without_peer_links_or_refusing_them_never_boots(self):
        for flag in ('peer_result=0', 'no_peer=true'):
            self.setUp()
            self.lua.execute(flag)
            self.lua.execute(SOURCE)
            self.lua.execute('update();update()')
            self.assertEqual(self.lua.globals().boots, 0, flag)
            self.assertTrue(any('REFUSED peer_window=3' in line for line in self.logs()), (flag, self.logs()))

    def test_window_values_are_checked_before_loading(self):
        bad = {"slot='p1',native_gate_clients=2,native_gate_peer_window=1": 'needs native_gate_lookahead',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=0,native_gate_peer_window=1": 'needs native_gate_lookahead',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_peer_window=7": 'k [+] window <= 8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=8": 'must be 0..7',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=-1": 'must be 0..7',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=1.5": 'must be 0..7',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window='2'": 'must be 0..7',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=true": 'must be 0..7'}
        for config, message in bad.items():
            self.setUp(config)
            with self.assertRaisesRegex(LuaError, message, msg=config):
                self.lua.execute(SOURCE)
            self.assertEqual((self.lua.globals().boots, self.lua.globals().loads), (0, 0))
        self.setUp("slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=7")
        self.lua.execute(SOURCE)

    def test_the_nat_traversal_rendezvous_port_reaches_the_gate(self):
        # native_gate_peer_rendezvous_port = P: the NAT traversal core (lockstep_nat.h) with the relay's --rendezvous-port P.
        self.setUp("slot='p2',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=3,native_gate_peer_rendezvous_port=40000")
        self.admitted(inputs="{'',''}")
        self.assertIn('peer 3 40000', self.values('calls'))
        self.setUp("slot='p2',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=3,native_gate_peer_rendezvous_port=40000")
        self.lua.execute('peer_result=0')
        self.lua.execute(SOURCE)
        self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED peer_window=3 rendezvous=40000' in line for line in self.logs()), self.logs())
        bad = {"slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_rendezvous_port=40000": 'needs native_gate_peer_window',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=1,native_gate_peer_rendezvous_port=80": '1024..65534',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=1,native_gate_peer_rendezvous_port=65535": '1024..65534',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_peer_window=1,native_gate_peer_rendezvous_port='40000'": '1024..65534',
               # Peer links extend protocol 4; protocol 5 (adaptive) has none.
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_peer_window=1": 'adaptive is protocol 5'}
        for config, message in bad.items():
            self.setUp(config)
            with self.assertRaisesRegex(LuaError, message, msg=config):
                self.lua.execute(SOURCE)
            self.assertEqual((self.lua.globals().boots, self.lua.globals().loads), (0, 0))

    def test_without_the_setting_peer_links_are_never_called(self):
        self.setUp("slot='p2',native_gate_clients=2,native_gate_lookahead=1")
        self.admitted(inputs="{'',''}")
        self.assertNotIn('peer', ' '.join(self.values('calls')))
        self.lua.execute('state.active=0;update()')
        self.assertFalse(any('PEER' in line or 'peer_window' in line for line in self.logs()), self.logs())


if __name__ == '__main__':
    unittest.main()
