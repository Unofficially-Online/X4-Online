"""Run the real Lua probe under mocks; native reads are covered separately in C."""
import unittest

import test_native_gate_probe as fixture
from test_world_motion_evidence import SAVED, motion_row


class MotionProbeTests(unittest.TestCase):
    def run_probe(self, extra='', enabled=True):
        probe = fixture.SnapshotProbeTests()
        probe.setUp()
        lua = probe.lua
        lua.globals().motion_rows = lua.table_from({tick: motion_row(tick).split('WORLD_MOTION ', 1)[1] for tick in (2, 3)})
        lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_status_stride=60
          local gate=package.loadlib('gate')()
          motion_calls={};digest_inputs={}
          gate.world_motion_config=function(ids,first,last)
            assert(ids=='000000000006ec76' and first==2 and last==3);return 1
          end
          gate.world_motion_capture=function(tick) motion_calls[#motion_calls+1]=tick;return 1 end
          gate.world_motion_row=function(index) assert(index==1);return motion_rows[state.issued] end
          gate.world_motion_status=function() return 'count=1 first=2 last=3 samples=2 failures=0' end
          X4LAN_SHA256.digest=function(data) digest_inputs[#digest_inputs+1]=data;return string.rep('a',64) end
          state.boot_phase=3;is_menu=false
        ''')
        if enabled:
            lua.execute("X4LAN_CONFIG.native_gate_world_motion_watch={'000000000006ec76'};"
                        'X4LAN_CONFIG.native_gate_world_motion_window={2,3}')
        lua.execute(extra)
        lua.execute(fixture.SOURCE)
        for tick in range(1, 6):
            lua.globals().state.issued = tick
            lua.execute('update()')
        inputs = list(lua.globals().digest_inputs.values())
        lua.execute('state.active=0;state.completed=5;setmetatable(state,{__index=function() return 0 end});update()')
        return lua, list(lua.globals().logs.values()), inputs

    def test_mandatory_window_rows_survive_status_thinning_without_digest_changes(self):
        lua, logs, observed_inputs = self.run_probe()
        _, baseline_logs, baseline_inputs = self.run_probe(enabled=False)
        self.assertEqual(list(lua.globals().motion_calls.values()), [2, 3])
        self.assertEqual(observed_inputs, baseline_inputs)
        self.assertEqual(sum('NATIVE_GATE WORLD_MOTION ' in line for line in logs), 2)
        self.assertEqual(sum('NATIVE_GATE WORLD_MOTION_ARMED ' in line for line in logs), 1)
        self.assertEqual(sum('NATIVE_GATE WORLD_MOTION_STOP ' in line for line in logs), 1)
        self.assertFalse(any('WORLD_MOTION' in line for line in baseline_logs))

    def test_missing_configuration_exports_and_failed_capture_refuse(self):
        for extra in ('X4LAN_CONFIG.native_gate_world_motion_window=nil',
                      'X4LAN_CONFIG.native_gate_world_motion_window={1,18001}',
                      'X4LAN_CONFIG.native_gate_world_identities=false',
                      "package.loadlib('gate')().world_motion_capture=nil"):
            with self.subTest(extra=extra), self.assertRaises(fixture.LuaError):
                self.run_probe(extra)
        for extra in ("package.loadlib('gate')().world_motion_config=function() return 0 end",
                      "package.loadlib('gate')().world_motion_capture=function() return nil,'identity_recheck' end"):
            with self.subTest(extra=extra):
                _, logs, _ = self.run_probe(extra)
                self.assertTrue(any('NATIVE_GATE ERROR ' in line and 'world motion' in line for line in logs))
                self.assertFalse(any('NATIVE_GATE WORLD_MOTION ' in line for line in logs))


if __name__ == '__main__':
    unittest.main()
