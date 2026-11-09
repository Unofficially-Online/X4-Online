"""Execute the production Lua probe with a fake Linux stock-state observer.

These tests cover Lua scheduling, row retention and refusals, not native hooks
or complete pilot input capture.
"""
import unittest

import test_native_gate_probe as fixture


class StockStateObserverProbeTests(unittest.TestCase):
    def runtime(self, extra=''):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_stock_state_observer=true
          X4LAN_CONFIG.native_gate_stock_trap_exclusive=true
          observer_arms=0;observer_status_reads=0;observer_reads={};observer_rows={}
          observer_arm_ticks={};observer_events={};stops=0
          observer_event=function(event) observer_events[#observer_events+1]=event end
          local gate=package.loadlib('gate')()
          gate.platform=function() return 'linux' end
          gate.stock_state_observer=function()
            observer_event('arm')
            observer_arms=observer_arms+1
            observer_arm_ticks[#observer_arm_ticks+1]=state.issued
            return 1
          end
          gate.stock_state_observer_status=function()
            observer_event('status')
            observer_status_reads=observer_status_reads+1
            return 'installed=1 failures=0'
          end
          gate.stock_state_observer_row=function(index)
            observer_event('row:'..index)
            observer_reads[#observer_reads+1]=index
            if observer_row_limit then
              if index<observer_row_limit then return 'index='..index end
            else
              return observer_rows[index]
            end
          end
          local stop=gate.stop
          gate.stop=function() observer_event('stop');stops=stops+1;return stop() end
        ''')
        lua.execute(extra)
        return lua

    def boot(self, lua):
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(lua.globals().observer_arms, 0)

    def frame(self, lua, tick):
        lua.globals().state.issued = tick
        lua.execute('state.boot_phase=3;is_menu=false;update()')

    def lines(self, lua, prefix):
        marker = '[X4LAN] NATIVE_GATE ' + prefix
        return [line[len(marker):] for line in lua.globals().logs.values()
                if line.startswith(marker)]

    def assert_clean(self, lua):
        self.assertEqual(self.lines(lua, 'ERROR '), [])
        self.assertEqual(lua.globals().stops, 0)

    def test_arm_once_at_first_admitted_frame_and_resume_after_empty_row(self):
        lua = self.runtime("observer_rows[0]='index=0';observer_rows[1]='index=1'")
        self.boot(lua)
        self.frame(lua, 1)
        lua.execute('update()')  # Already reported: neither re-arm nor re-drain.
        self.frame(lua, 2)       # Empty index 2 must remain the next cursor.
        lua.execute("observer_rows[2]='index=2'")
        self.frame(lua, 3)
        g = lua.globals()
        self.assertEqual(g.observer_arms, 1)
        self.assertEqual(list(g.observer_arm_ticks.values()), [1])
        self.assertEqual(list(g.observer_reads.values()), [0, 1, 2, 2, 2, 3])
        self.assertEqual(self.lines(lua, 'STOCK_STATE_ROW '),
                         ['index=0', 'index=1', 'index=2'])
        self.assertEqual(g.observer_status_reads, 3)
        self.assertEqual(self.lines(lua, 'STOCK_STATE_OBSERVER '),
                         [f'tick={tick} installed=1 failures=0' for tick in (1, 2, 3)])
        self.assertEqual(g.reports, 3)
        self.assert_clean(lua)

    def test_each_frame_drains_at_most_512_rows_without_resetting_cursor(self):
        lua = self.runtime('observer_row_limit=1025')
        self.boot(lua)
        for tick, count in ((1, 512), (2, 1024), (3, 1025)):
            self.frame(lua, tick)
            self.assertEqual(len(self.lines(lua, 'STOCK_STATE_ROW ')), count)
        self.assertEqual(list(lua.globals().observer_reads.values()), list(range(1026)))
        self.assertEqual(self.lines(lua, 'STOCK_STATE_ROW '),
                         [f'index={index}' for index in range(1025)])
        self.assertEqual(lua.globals().observer_arms, 1)
        self.assert_clean(lua)

    def test_rows_are_retained_when_status_stride_holds_a_frame(self):
        lua = self.runtime('X4LAN_CONFIG.native_gate_status_stride=2')
        self.boot(lua)
        for tick in range(1, 5):
            lua.execute(f"observer_rows[{tick-1}]='index={tick-1}'")
            self.frame(lua, tick)
        # Tick 3's status is deliberately thinned; its observation row is immediate.
        self.assertEqual(lua.globals().observer_status_reads, 3)
        self.assertEqual(self.lines(lua, 'STOCK_STATE_ROW '),
                         [f'index={index}' for index in range(4)])
        self.assertEqual(lua.globals().observer_arms, 1)
        self.assert_clean(lua)

    def test_disabled_option_needs_no_observer_api(self):
        for value in ('nil', 'false'):
            with self.subTest(value=value):
                lua = self.runtime(f'''
                  X4LAN_CONFIG.native_gate_stock_state_observer={value}
                  local gate=package.loadlib('gate')()
                  gate.platform=nil;gate.stock_state_observer=nil
                  gate.stock_state_observer_status=nil;gate.stock_state_observer_row=nil
                ''')
                self.boot(lua)
                self.frame(lua, 1)
                self.assertEqual(lua.globals().observer_arms, 0)
                self.assertEqual(lua.globals().observer_status_reads, 0)
                self.assertEqual(list(lua.globals().observer_reads.values()), [])
                self.assertEqual(self.lines(lua, 'STOCK_STATE'), [])
                self.assertEqual(lua.globals().reports, 1)
                self.assert_clean(lua)

    def test_refused_arm_stops_before_status_rows_or_report(self):
        lua = self.runtime('''
          package.loadlib('gate')().stock_state_observer=function()
            observer_event('arm');observer_arms=observer_arms+1;return 0
          end
        ''')
        self.boot(lua)
        self.frame(lua, 1)
        self.assertEqual(lua.globals().observer_arms, 1)
        self.assert_observer_aborted(lua, 'Linux stock state observer refused', attempted=True)
        self.assertEqual(list(lua.globals().observer_events.values()),
                         ['arm', 'stop', 'status', 'row:0'])

    def assert_observer_aborted(self, lua, reason, attempted=False):
        self.assertTrue(any(reason in line for line in self.lines(lua, 'ERROR ')))
        self.assertEqual(lua.globals().stops, 1)
        self.assertEqual(lua.globals().state.active, 0)
        self.assertEqual(lua.globals().reports, 0)
        self.assertEqual(lua.globals().observer_status_reads, int(attempted))
        self.assertEqual(list(lua.globals().observer_reads.values()), [0] if attempted else [])
        self.assertEqual(self.lines(lua, 'STOCK_STATE_ROW '), [])

    def test_wrong_platform_and_each_missing_api_stop_before_arm(self):
        changes = ["gate.platform=function() return 'windows' end"]
        changes += [f'gate.{name}=nil' for name in
                    ('platform', 'stock_state_observer', 'stock_state_observer_status',
                     'stock_state_observer_row')]
        for change in changes:
            with self.subTest(change=change):
                lua = self.runtime("local gate=package.loadlib('gate')();" + change)
                self.boot(lua)
                self.frame(lua, 1)
                self.assertEqual(lua.globals().observer_arms, 0)
                self.assert_observer_aborted(lua, 'Linux stock state observer is unavailable')
                self.assertEqual(list(lua.globals().observer_events.values()), ['stop'])

    def test_missing_status_value_stops_before_rows_or_report(self):
        lua = self.runtime('''
          package.loadlib('gate')().stock_state_observer_status=function()
            observer_event('status');observer_status_reads=observer_status_reads+1;return nil
          end
        ''')
        self.boot(lua)
        self.frame(lua, 1)
        self.assertTrue(any('stock state observer status unavailable' in line
                            for line in self.lines(lua, 'ERROR ')))
        self.assertEqual(lua.globals().observer_arms, 1)
        self.assertEqual(lua.globals().observer_status_reads, 2)
        self.assertEqual(lua.globals().stops, 1)
        self.assertEqual(lua.globals().reports, 0)
        self.assertEqual(list(lua.globals().observer_reads.values()), [0])
        self.assertEqual(list(lua.globals().observer_events.values()),
                         ['arm', 'status', 'stop', 'status', 'row:0'])
        self.assertEqual(len(self.lines(lua, 'ERROR ')), 1)

    def test_invalid_config_and_coverage_conflict_refuse_before_boot(self):
        cases = [(f'X4LAN_CONFIG.native_gate_stock_state_observer={value}',
                  'native_gate_stock_state_observer must be a boolean')
                 for value in ("'true'", '0', '1', '{}')]
        cases.append(('X4LAN_CONFIG.native_gate_coverage=true',
                      'stock state observation cannot share code pages with coverage breakpoints'))
        for change, reason in cases:
            with self.subTest(change=change):
                lua = self.runtime(change)
                with self.assertRaisesRegex(fixture.LuaError, reason):
                    lua.execute(fixture.SOURCE)
                self.assertEqual(lua.globals().boots, 0)
                self.assertEqual(lua.globals().loads, 0)
                self.assertEqual(lua.globals().observer_arms, 0)
                self.assertEqual(list(lua.globals().observer_reads.values()), [])


if __name__ == '__main__':
    unittest.main()
