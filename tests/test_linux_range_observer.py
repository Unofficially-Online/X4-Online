"""Lua scheduling and bounded final reads, using fake observer APIs."""
import unittest

import test_native_gate_probe as fixture


class RangeObserverProbeTests(unittest.TestCase):
    def runtime(self, kind, extra=''):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_stock_trap_exclusive=true
          events={};stops=0;row_limit=0
          event=function(value) events[#events+1]=value end
          local gate=package.loadlib('gate')()
          gate.platform=function() return 'linux' end
          X4LAN_CONFIG.native_gate_stock_KIND_observer=true
          gate.stock_KIND_observer=function() event('arm');return 1 end
          gate.stock_KIND_observer_status=function() event('status');return 'ready' end
          gate.stock_KIND_observer_row=function(index)
            event('row:'..index)
            if index<row_limit then return 'index='..index end
          end
          local stop=gate.stop
          gate.stop=function() event('stop');stops=stops+1;return stop() end
        '''.replace('KIND', kind))
        lua.execute("local gate=package.loadlib('gate')();"+extra.replace('KIND', kind))
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update();state.issued=1;state.boot_phase=3;is_menu=false;update()')
        return lua

    def test_final_reads_require_an_arm_attempt_for_either_observer(self):
        changes = ["gate.platform=function() return 'windows' end"]
        changes += [f'gate.{name}=nil' for name in
                    ('platform', 'stock_KIND_observer', 'stock_KIND_observer_status',
                     'stock_KIND_observer_row')]
        for kind in ('state', 'range'):
            for change in changes:
                with self.subTest(kind=kind, change=change):
                    lua = self.runtime(kind, change)
                    lua.execute('update();update()')
                    self.assertEqual(list(lua.globals().events.values()), ['stop'])
                    self.assertEqual(lua.globals().reports, 0)
                    errors = [row for row in lua.globals().logs.values() if ' ERROR ' in row]
                    self.assertEqual(len(errors), 1)
                    self.assertIn(f'Linux stock {kind} observer is unavailable', errors[0])

    def test_refused_or_throwing_arm_retains_only_bounded_post_stop_evidence(self):
        for kind in ('state', 'range'):
            for result, reason in (('return 0', f'Linux stock {kind} observer refused'),
                                   ("error('partial arm failed')", 'partial arm failed')):
                with self.subTest(kind=kind, result=result):
                    lua = self.runtime(kind, "row_limit=1000;gate.stock_KIND_observer=function() "
                                       "event('arm');"+result+' end')
                    expected = ['arm', 'stop', 'status']+[f'row:{i}' for i in range(256)]
                    self.assertEqual(list(lua.globals().events.values()), expected)
                    lua.execute('update();update()')
                    self.assertEqual(list(lua.globals().events.values()), expected)
                    self.assertEqual(lua.globals().stops, 1)
                    self.assertEqual(lua.globals().reports, 0)
                    errors = [row for row in lua.globals().logs.values() if ' ERROR ' in row]
                    self.assertEqual(len(errors), 1)
                    self.assertIn(reason, errors[0])

    def test_nil_status_keeps_original_error_and_final_rows_without_reporting(self):
        for kind in ('state', 'range'):
            with self.subTest(kind=kind):
                lua = self.runtime(kind, "row_limit=2;gate.stock_KIND_observer_status=function() "
                                   "event('status');return nil end")
                self.assertEqual(list(lua.globals().events.values()),
                                 ['arm', 'status', 'stop', 'status', 'row:0', 'row:1', 'row:2'])
                self.assertEqual(lua.globals().stops, 1)
                self.assertEqual(lua.globals().reports, 0)
                lines = list(lua.globals().logs.values())
                errors = [row for row in lines if ' ERROR ' in row]
                self.assertEqual(len(errors), 1)
                self.assertIn(f'stock {kind} observer status unavailable', errors[0])
                self.assertTrue(any(f'STOCK_{kind.upper()}_OBSERVER_STOP nil' in row for row in lines))
                self.assertEqual(sum(f'STOCK_{kind.upper()}_ROW index=' in row for row in lines), 2)

    def test_abort_final_rows_resume_cursor_and_do_not_retry_after_stop(self):
        for kind in ('state', 'range'):
            with self.subTest(kind=kind):
                lua = self.runtime(kind, 'row_limit=1')
                lua.execute("row_limit=1000;local gate=package.loadlib('gate')();"
                            f"gate.stock_{kind}_observer_status=function() event('status');return nil end;"
                            'state.issued=2;update()')
                expected = ['arm', 'status', 'row:0', 'row:1', 'status', 'stop', 'status']
                expected += [f'row:{i}' for i in range(1, 257)]
                self.assertEqual(list(lua.globals().events.values()), expected)
                lua.execute('update();update()')
                self.assertEqual(list(lua.globals().events.values()), expected)
                self.assertEqual(lua.globals().stops, 1)
                self.assertEqual(lua.globals().reports, 1)

    def test_admission_and_stop_read_for_both_observers(self):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_stock_trap_exclusive=true
          X4LAN_CONFIG.native_gate_stock_state_observer=true
          X4LAN_CONFIG.native_gate_stock_range_observer=true
          local gate=package.loadlib('gate')()
          gate.platform=function() return 'linux' end
          state_arms=0;range_arms=0;state_rows={};range_rows={}
          gate.stock_state_observer=function() state_arms=state_arms+1;return 1 end
          gate.stock_range_observer=function() range_arms=range_arms+1;return 1 end
          gate.stock_state_observer_status=function() return 'state-status' end
          gate.stock_range_observer_status=function() return 'range-status' end
          gate.stock_state_observer_row=function(index) return state_rows[index] end
          gate.stock_range_observer_row=function(index) return range_rows[index] end
        ''')
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')
        self.assertEqual(lua.globals().range_arms, 0)
        lua.execute("state.issued=1;state.boot_phase=3;is_menu=false;state_rows[0]='state-first';range_rows[0]='range-first';update()")
        lua.execute("state_rows[1]='state-late';range_rows[1]='range-late';" + fixture.STOP_FIELDS + "state.active=0;state.reason=1;update()")
        lines = list(lua.globals().logs.values())
        for expected in ('STOCK_STATE_ROW state-first', 'STOCK_RANGE_ROW range-first',
                         'STOCK_STATE_ROW state-late', 'STOCK_RANGE_ROW range-late',
                         'STOCK_STATE_OBSERVER_STOP state-status',
                         'STOCK_RANGE_OBSERVER_STOP range-status'):
            self.assertTrue(any(expected in line for line in lines), expected)
        self.assertEqual(lua.globals().state_arms, 1)
        self.assertEqual(lua.globals().range_arms, 1)

    def test_exclusive_contract_is_required_for_both_observers(self):
        for key in ('native_gate_stock_state_observer', 'native_gate_stock_range_observer'):
            harness = fixture.SnapshotProbeTests()
            harness.setUp()
            harness.lua.execute('X4LAN_CONFIG.' + key + '=true')
            with self.assertRaisesRegex(fixture.LuaError, 'native_gate_stock_trap_exclusive=true'):
                harness.lua.execute(fixture.SOURCE)

    def test_fork_save_and_either_observer_are_refused(self):
        for key in ('native_gate_stock_state_observer', 'native_gate_stock_range_observer'):
            harness = fixture.SnapshotProbeTests()
            harness.setUp()
            harness.lua.execute('X4LAN_CONFIG.native_gate_stock_trap_exclusive=true;'
                                'X4LAN_CONFIG.native_gate_fork_save={tick=2,name="x"};'
                                'X4LAN_CONFIG.' + key + '=true')
            with self.assertRaisesRegex(fixture.LuaError, 'stock observers and fork save'):
                harness.lua.execute(fixture.SOURCE)

    def test_range_and_coverage_refused(self):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        harness.lua.execute('X4LAN_CONFIG.native_gate_stock_trap_exclusive=true;X4LAN_CONFIG.native_gate_stock_range_observer=true;X4LAN_CONFIG.native_gate_coverage=10')
        with self.assertRaisesRegex(fixture.LuaError, 'range observer and coverage'):
            harness.lua.execute(fixture.SOURCE)


if __name__ == '__main__':
    unittest.main()
