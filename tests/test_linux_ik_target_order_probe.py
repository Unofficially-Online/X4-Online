"""Offline Lua scheduling tests for the opt-in angular-target correction."""
import unittest

import test_native_gate_probe as fixture


class IKTargetOrderProbeTests(unittest.TestCase):
    def runtime(self, extra=''):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_ik_target_order=true
          X4LAN_CONFIG.native_gate_stock_trap_exclusive=true
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_snapshot_fixture={}
          for i=1,12 do X4LAN_CONFIG.native_gate_snapshot_fixture[i]=string.format('%016x',i) end
          order_calls={};stops=0;status_calls=0
          local gate=package.loadlib('gate')()
          gate.snapshot_fixture=function() return 1 end
          gate.snapshot_actor=function(index) return 'id='..index end
          state.reason=1;state.snapshot_bound=12;state.snapshot_prepared=12
          state.snapshot_enrolled=12;state.snapshot_failures=0;state.snapshot_boundary='complete'
          state.world_identity_calls=0;state.world_identity_count=0;state.world_identity_late=0
          state.world_identity_restored=1;state.world_identity_failures=0
          gate.platform=function() return 'linux' end
          gate.ik_target_order=function()
            order_calls[#order_calls+1]='arm:'..state.boot_phase..':'..loads
            return refuse_order and 0 or 1
          end
          gate.ik_target_order_status=function()
            status_calls=status_calls+1;return 'installed=1 restored=0'
          end
          local stop=gate.stop
          gate.stop=function() stops=stops+1;return stop() end
        ''')
        lua.execute(extra)
        return lua

    def boot(self, lua):
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')

    def lines(self, lua, marker):
        return [line for line in lua.globals().logs.values() if marker in line]

    def test_arm_before_load_tick_status_and_normal_stop(self):
        lua = self.runtime()
        self.boot(lua)
        self.assertEqual(list(lua.globals().order_calls.values()), ['arm:0:0'])
        self.assertEqual(lua.globals().loads, 1)
        lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_ARMED ')), 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER tick=1 ')), 1)
        lua.execute(fixture.STOP_FIELDS + 'state.active=0;update()')
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_STOP ')), 1)
        self.assertEqual(lua.globals().stops, 0)

    def test_ui_reload_rechecks_native_owner_and_stopped_reload_skips_arm(self):
        lua = self.runtime()
        self.boot(lua)
        lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        lua.execute(fixture.SOURCE)
        lua.execute('update()')
        self.assertEqual(list(lua.globals().order_calls.values()), ['arm:0:0', 'arm:3:1'])
        lua.execute(fixture.STOP_FIELDS + 'state.active=0')
        lua.execute(fixture.SOURCE)
        lua.execute('update()')
        self.assertEqual(len(lua.globals().order_calls), 2)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_STOP ')), 1)

    def test_native_refusal_aborts_and_reports_final_status(self):
        for change in ('refuse_order=true', "package.loadlib('gate')().ik_target_order=nil"):
            with self.subTest(change=change):
                lua = self.runtime(change)
                self.boot(lua)
                self.assertEqual(lua.globals().loads, 0)
                self.assertEqual(lua.globals().stops, 1)
                self.assertTrue(self.lines(lua, 'IK target order'))
                self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_STOP ')), 1)

    def test_linux_missing_platform_export_does_not_become_windows_noop(self):
        lua = self.runtime("package.loadlib('gate')().platform=nil")
        self.boot(lua)
        self.assertEqual(lua.globals().loads, 0)
        self.assertEqual(lua.globals().stops, 1)
        self.assertEqual(self.lines(lua, 'IK_TARGET_ORDER_NOOP'), [])

    def test_disabled_requires_no_native_api(self):
        for disabled in ('nil', 'false'):
            with self.subTest(disabled=disabled):
                lua = self.runtime('''
                  X4LAN_CONFIG.native_gate_ik_target_order=%s
                  local gate=package.loadlib('gate')()
                  gate.platform=nil;gate.ik_target_order=nil;gate.ik_target_order_status=nil
                ''' % disabled)
                self.boot(lua)
                self.assertEqual(lua.globals().loads, 1)
                self.assertEqual(len(lua.globals().order_calls), 0)
                self.assertEqual(self.lines(lua, 'IK_TARGET_ORDER'), [])

    def test_windows_noop_and_no_linux_api(self):
        lua = self.runtime('''
          X4LAN_CONFIG.native_gate_world_identities=false
          X4LAN_CONFIG.native_gate_snapshot_fixture=nil
          package.config=string.char(92)..package.config:sub(2)
          local gate=package.loadlib('gate')()
          gate.platform=nil  -- The Windows gate has no platform export.
          gate.ik_target_order=nil;gate.ik_target_order_status=nil
        ''')
        self.boot(lua)
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_NOOP windows')), 1)
        self.assertEqual(len(lua.globals().order_calls), 0)

    def test_linux_requires_native_identity_and_fixture_preconditions(self):
        for change in ('X4LAN_CONFIG.native_gate_world_identities=false',
                       'X4LAN_CONFIG.native_gate_snapshot_fixture=nil'):
            with self.subTest(change=change):
                lua = self.runtime(change)
                self.boot(lua)
                self.assertEqual(lua.globals().loads, 0)
                self.assertEqual(lua.globals().stops, 1)
                self.assertEqual(len(lua.globals().order_calls), 0)
                self.assertTrue(self.lines(lua, 'requires world identities'))

    def test_reload_owner_refusal_stops_session(self):
        lua = self.runtime()
        self.boot(lua)
        lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;refuse_order=true')
        lua.execute(fixture.SOURCE)
        lua.execute('update()')
        self.assertEqual(len(lua.globals().order_calls), 2)
        self.assertEqual(lua.globals().stops, 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_STOP ')), 1)

    def test_invalid_config_and_conflicts_refuse_before_boot(self):
        for change in ("X4LAN_CONFIG.native_gate_ik_target_order='true'",
                       'X4LAN_CONFIG.native_gate_ik_target_order=0',
                       'X4LAN_CONFIG.native_gate_snapshot_sha=nil',
                       'X4LAN_CONFIG.native_gate_stock_trap_exclusive=false',
                       'X4LAN_CONFIG.native_gate_ik_target_observer={}',
                       'X4LAN_CONFIG.native_gate_ik_step=true',
                       'X4LAN_CONFIG.native_gate_coverage=60',
                       'X4LAN_CONFIG.native_gate_fork_save={}'):
            with self.subTest(change=change):
                lua = self.runtime(change)
                with self.assertRaises(fixture.LuaError):
                    lua.execute(fixture.SOURCE)
                self.assertEqual(lua.globals().boots, 0)

    def test_exception_after_arm_reports_status(self):
        lua = self.runtime('''
          package.loadlib('gate')().load_requested=function() error('simulated load failure') end
        ''')
        self.boot(lua)
        self.assertEqual(lua.globals().stops, 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ORDER_STOP ')), 1)
        self.assertEqual(lua.globals().loads, 0)


if __name__ == '__main__':
    unittest.main()
