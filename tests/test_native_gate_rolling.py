"""Exercise the opt-in slice probe with the real Lua control flow."""
import unittest

import test_native_gate_probe as fixture
from test_linux_gate_rolling_evidence import capture as rolling


class RollingProbeTests(unittest.TestCase):
    def run_probe(self, config='', *, available=True, ticks=5):
        probe = fixture.SnapshotProbeTests()
        probe.setUp()
        lua = probe.lua
        # The native schema and schedule are separately exercised by C tests.
        summaries = {tick: rolling(tick, slices=3).split('WORLD_ROLLING ', 1)[1]
                     for tick in range(1, ticks + 1)}
        lua.globals().summaries = lua.table_from(summaries)
        lua.execute('''
          X4LAN_CONFIG.native_gate_world_hash_stride=120
          X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'
          X4LAN_CONFIG.native_gate_world_detail_rows=false
          X4LAN_CONFIG.native_gate_world_rolling_slices=3
          local gate=package.loadlib('gate')()
          calls={}; digest_inputs={}
          gate.world_capture_rolling=function(tick,slices)
            assert(slices==3); calls[#calls+1]=tick
            state.world_hash_scans=#calls; state.world_hash_failures=0
            return summaries[tick]
          end
          gate.world_capture_nav_v2=function() error('instantaneous capture in rolling mode') end
          gate.world_page=function() error('pages in rolling mode') end
          gate.world_detail=function() error('details in rolling mode') end
          gate.world_banks=function() return 'diagnostic banks' end
          X4LAN_SHA256.digest=function(data)
            digest_inputs[#digest_inputs+1]=data;return string.rep('a',64)
          end
          state.boot_phase=3;is_menu=false
        ''')
        if not available:
            lua.execute("package.loadlib('gate')().world_capture_rolling=nil")
        lua.execute(config)
        lua.execute(fixture.SOURCE)
        for tick in range(1, ticks + 1):
            lua.globals().state.issued = tick
            lua.execute('update()')
        return lua, list(lua.globals().logs.values())

    def test_every_tick_enters_digest_and_uses_separate_stop(self):
        lua, logs = self.run_probe()
        self.assertEqual(list(lua.globals().calls.values()), [1, 2, 3, 4, 5])
        self.assertEqual(lua.globals().reports, 5)
        self.assertEqual(sum('NATIVE_GATE WORLD_ROLLING ' in line for line in logs), 5)
        self.assertFalse(any('NATIVE_GATE WORLD_HASH ' in line for line in logs))
        inputs = [data for data in lua.globals().digest_inputs.values() if '|WORLD_ROLLING_1:' in data]
        self.assertEqual(len(inputs), 5)
        for tick, data in enumerate(inputs, 1):
            self.assertIn(rolling(tick, slices=3).split(' state=')[1], data)
        lua.execute('state.active=0;state.completed=5;setmetatable(state,{__index=function() return 0 end});update()')
        logs = list(lua.globals().logs.values())
        self.assertTrue(any('WORLD_ROLLING_STOP scans=5 cycles=1 failures=0' in line for line in logs))
        self.assertFalse(any('WORLD_HASH_STOP' in line for line in logs))

    def test_configuration_and_unavailable_native_export_fail_closed(self):
        for configuration in (
            'X4LAN_CONFIG.native_gate_world_rolling_slices=0',
            'X4LAN_CONFIG.native_gate_world_rolling_slices=18001',
            'X4LAN_CONFIG.native_gate_world_rolling_slices=1.5',
            'X4LAN_CONFIG.native_gate_world_rolling_slices=true',
            "X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_1'",
            'X4LAN_CONFIG.native_gate_world_detail_rows=true',
            'X4LAN_CONFIG.native_gate_join=true',
            'X4LAN_CONFIG.native_gate_world_nav_stride=2',
            "X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000001'}",
            'X4LAN_NATIVE_HYBRID={}',
        ):
            with self.subTest(configuration=configuration), self.assertRaises(fixture.LuaError):
                self.run_probe(configuration)
        with self.assertRaises(fixture.LuaError):
            self.run_probe(available=False)

    def test_native_failure_cannot_be_submitted_as_a_slice(self):
        lua, logs = self.run_probe("""
            package.loadlib('gate')().world_capture_rolling=function()
              return nil,'bank phase changed'
            end
        """, ticks=1)
        self.assertEqual(lua.globals().reports, 0)
        self.assertTrue(any('native rolling census failed bank phase changed' in line for line in logs))
        self.assertFalse(any('NATIVE_GATE WORLD_ROLLING ' in line for line in logs))


if __name__ == '__main__':
    unittest.main()
