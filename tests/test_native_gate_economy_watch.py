"""Run the real Lua probe under mocks; native reads are covered separately in C."""
import unittest

import test_native_gate_probe as fixture



class EconomyProbeTests(unittest.TestCase):
    def run_probe(self, extra='', enabled=True):
        probe = fixture.SnapshotProbeTests()
        probe.setUp()
        lua = probe.lua
        lua.globals().economy_rows = lua.table_from({tick: f'version=1 tick={tick} id=0000000000036fe9 present=1 class=station last=0 next=0 time=0' for tick in (2, 3)})
        lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_status_stride=60
          local gate=package.loadlib('gate')()
          economy_calls={};digest_inputs={}
          gate.economy_watch_config=function(ids,first,last)
            assert(ids=='0000000000036fe9' and first==2 and last==3);return 1
          end
          gate.economy_watch_capture=function(tick) economy_calls[#economy_calls+1]=tick;return economy_rows[tick] end
          gate.economy_watch_status=function() return 'count=1 first=2 last=3 samples=2 failures=0' end
          X4LAN_SHA256.digest=function(data) digest_inputs[#digest_inputs+1]=data;return string.rep('a',64) end
          state.boot_phase=3;is_menu=false
        ''')
        if enabled:
            lua.execute("X4LAN_CONFIG.native_gate_economy_watch='0000000000036fe9';"
                        'X4LAN_CONFIG.native_gate_economy_watch_window={2,3}')
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
        self.assertEqual(list(lua.globals().economy_calls.values()), [2, 3])
        self.assertEqual(observed_inputs, baseline_inputs)
        self.assertEqual(sum('NATIVE_GATE ECONOMY_WATCH ' in line for line in logs), 2)
        self.assertEqual(sum('NATIVE_GATE ECONOMY_WATCH_ARMED ' in line for line in logs), 1)
        self.assertEqual(sum('NATIVE_GATE ECONOMY_WATCH_STOP ' in line for line in logs), 1)
        self.assertFalse(any('ECONOMY_WATCH' in line for line in baseline_logs))

    def test_missing_configuration_exports_and_failed_capture_refuse(self):
        for extra in ('X4LAN_CONFIG.native_gate_economy_watch_window=nil',
                      'X4LAN_CONFIG.native_gate_economy_watch_window={1,36001}',
                      'X4LAN_CONFIG.native_gate_world_identities=false',
                      "package.loadlib('gate')().economy_watch_capture=nil"):
            with self.subTest(extra=extra), self.assertRaises(fixture.LuaError):
                self.run_probe(extra)
        for extra in ("package.loadlib('gate')().economy_watch_config=function() return 0 end",
                      "package.loadlib('gate')().economy_watch_capture=function() return nil,'identity_recheck' end"):
            with self.subTest(extra=extra):
                _, logs, _ = self.run_probe(extra)
                self.assertTrue(any('NATIVE_GATE ERROR ' in line and 'economy watch' in line for line in logs))
                self.assertFalse(any('NATIVE_GATE ECONOMY_WATCH ' in line for line in logs))


if __name__ == '__main__':
    unittest.main()
