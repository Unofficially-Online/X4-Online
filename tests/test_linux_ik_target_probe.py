"""Offline Lua scheduling and failure tests; no native observer is exercised."""
import unittest

import test_native_gate_probe as fixture


class IKTargetProbeTests(unittest.TestCase):
    def runtime(self, extra=''):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_hash='battle'
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_snapshot_fixture={}
          for i=1,12 do X4LAN_CONFIG.native_gate_snapshot_fixture[i]=string.format('%016x',i) end
          X4LAN_CONFIG.native_gate_stock_trap_exclusive=true
          X4LAN_CONFIG.native_gate_ik_target_observer={first=12,last=18,
            objects={{actor=1,weapon_slot=0},{actor=1,weapon_slot=2}}}
          events={};calls={};rows={};reads={};stops=0
          RegisterEvent=function(name,callback) events[name]=callback end
          ConvertIDTo64Bit=function(value) return value end
          AddUITriggeredEvent=function() end
          local c=require('ffi').C
          c.IsComponentOperational=function() return false end
          c.GetNumUpgradeSlots=function() return 2 end
          c.GetUpgradeSlotCurrentComponent=function(ship,kind,slot) return ship*100+slot end
          local gate=package.loadlib('gate')()
          gate.platform=function() return 'linux' end
          gate.snapshot_fixture=function() return 1 end
          gate.snapshot_actor=function(i) return 'id='..i end
          gate.snapshot_enroll=function() return 1 end
          gate.saved_identity=function(value) return string.format('%016x',tonumber(value)) end
          gate.ik_target_observer=function(first,last)
            calls[#calls+1]='arm:'..first..':'..last..':'..loads;return 1
          end
          gate.ik_target_bind=function(actor,slot,runtime)
            calls[#calls+1]='bind:'..actor..':'..slot..':'..runtime..':'..state.issued
            return refuse_bind and 0 or 1
          end
          gate.ik_target_status=function() return 'installed=1 first=12 last=18' end
          gate.ik_target_row=function(index) reads[#reads+1]=index;return rows[index] end
          local stop=gate.stop
          gate.stop=function() stops=stops+1;return stop() end
        ''')
        lua.execute(extra)
        return lua

    def boot(self, lua):
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')

    def enroll(self, lua):
        lua.execute('''
          for i=1,12 do events['X4LAN_NativeGate.Actor'..i](nil,100+i) end
          events['X4LAN_NativeGate.Ready'](nil,12)
        ''')

    def frame(self, lua, tick):
        lua.execute(f'state.boot_phase=3;state.issued={tick};is_menu=false;update()')

    def lines(self, lua, marker):
        return [line for line in lua.globals().logs.values() if marker in line]

    def test_arm_before_load_bind_once_and_retry_unready_row(self):
        lua = self.runtime()
        self.boot(lua)
        self.assertEqual(list(lua.globals().calls.values()), ['arm:12:18:0'])
        self.frame(lua, 1)
        self.enroll(lua)
        lua.execute("rows[0]='first'")
        self.frame(lua, 2)
        self.frame(lua, 3)
        lua.execute("rows[1]='second'")
        self.frame(lua, 4)
        self.assertEqual(list(lua.globals().calls.values()),
                         ['arm:12:18:0', 'bind:1:0:101:2', 'bind:1:2:10102:2'])
        self.assertEqual(list(lua.globals().reads.values()), [0, 0, 1, 1, 1, 2])
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ROW first')), 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ROW second')), 1)
        self.assertEqual(lua.globals().stops, 0)

    def test_bind_refusal_aborts_and_stop_drains_available_rows(self):
        lua = self.runtime('refuse_bind=true;rows[0]="stop-row"')
        self.boot(lua)
        self.frame(lua, 1)
        self.enroll(lua)
        self.frame(lua, 2)
        self.assertEqual(lua.globals().stops, 1)
        self.assertTrue(self.lines(lua, 'IK target fixture bind refused'))
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_STOP ')), 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ROW stop-row')), 1)

    def test_missing_api_or_refused_arm_stops_before_load(self):
        for change in ("gate.platform=function() return 'windows' end",
                       'gate.ik_target_bind=nil',
                       'gate.ik_target_observer=function() return 0 end'):
            with self.subTest(change=change):
                lua = self.runtime("local gate=package.loadlib('gate')();"+change)
                self.boot(lua)
                self.assertEqual(lua.globals().loads, 0)
                self.assertEqual(lua.globals().stops, 1)

    def test_invalid_config_refuses_before_boot(self):
        changes = [
            "X4LAN_CONFIG.native_gate_ik_target_observer='yes'",
            'X4LAN_CONFIG.native_gate_ik_target_observer.first=0',
            'X4LAN_CONFIG.native_gate_ik_target_observer.last=72',
            'X4LAN_CONFIG.native_gate_ik_target_observer.objects[1].actor=13',
            'X4LAN_CONFIG.native_gate_ik_target_observer.objects[2].weapon_slot=33',
            'X4LAN_CONFIG.native_gate_stock_trap_exclusive=false',
            'X4LAN_CONFIG.native_gate_coverage=true',
            'X4LAN_CONFIG.native_gate_fork_save={}',
            'X4LAN_CONFIG.native_gate_ik_step=true',
            'X4LAN_CONFIG.native_gate_world_identities=false',
        ]
        for change in changes:
            with self.subTest(change=change):
                lua = self.runtime(change)
                with self.assertRaises(fixture.LuaError):
                    lua.execute(fixture.SOURCE)
                self.assertEqual(lua.globals().boots, 0)

    def test_missing_enrollment_by_window_aborts(self):
        lua = self.runtime()
        self.boot(lua)
        self.frame(lua, 12)
        self.assertEqual(lua.globals().stops, 1)
        self.assertTrue(self.lines(lua, 'fixture was not enrolled before the window'))

    def test_stop_drains_row_without_claiming_late_publication(self):
        lua = self.runtime()
        self.boot(lua)
        self.frame(lua, 1)
        self.enroll(lua)
        self.frame(lua, 2)
        lua.execute(fixture.STOP_FIELDS + 'state.active=0;rows[0]="final-row";update()')
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_STOP ')), 1)
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_ROW final-row')), 1)

    def test_drain_is_bounded_per_frame_and_by_native_capacity(self):
        lua = self.runtime('''
          local gate=package.loadlib('gate')()
          gate.ik_target_row=function(index)
            reads[#reads+1]=index;return 'index='..index
          end
        ''')
        self.boot(lua)
        self.frame(lua, 1)
        self.assertEqual(len(lua.globals().reads), 512)
        lua.execute(fixture.STOP_FIELDS + 'state.active=0;update()')
        self.assertEqual(len(lua.globals().reads), 4096)
        self.assertEqual(lua.globals().reads[4096], 4095)

    def test_disabled_needs_no_native_api(self):
        lua = self.runtime('''
          X4LAN_CONFIG.native_gate_ik_target_observer=false
          local gate=package.loadlib('gate')()
          gate.ik_target_observer=nil;gate.ik_target_bind=nil
          gate.ik_target_status=nil;gate.ik_target_row=nil
        ''')
        self.boot(lua)
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(list(lua.globals().calls.values()), [])
        self.assertEqual(lua.globals().stops, 0)

    def test_ui_reload_revalidates_existing_native_observer(self):
        for accepted in (True, False):
            with self.subTest(accepted=accepted):
                lua = self.runtime()
                self.boot(lua)
                lua.execute('''
                  local gate=package.loadlib('gate')()
                  gate.ik_target_observer=function(first,last)
                    calls[#calls+1]='recheck:'..first..':'..last
                    return %d
                  end
                ''' % int(accepted))
                lua.execute('state.boot_phase=3;state.issued=2;state.report_tick=1;is_menu=false')
                lua.execute(fixture.SOURCE)
                lua.execute('update()')
                self.assertEqual(list(lua.globals().calls.values()),
                                 ['arm:12:18:0', 'recheck:12:18'])
                self.assertEqual(lua.globals().stops, 0 if accepted else 1)
                if not accepted:
                    self.assertTrue(self.lines(lua, 'Linux IK target observer refused'))

    def test_ui_reload_after_stop_does_not_rearm(self):
        lua = self.runtime()
        self.boot(lua)
        lua.execute(fixture.STOP_FIELDS + 'state.active=0;state.boot_phase=3;state.issued=1;is_menu=false')
        lua.execute(fixture.SOURCE)
        lua.execute('update()')
        self.assertEqual(list(lua.globals().calls.values()), ['arm:12:18:0'])
        self.assertEqual(len(self.lines(lua, 'IK_TARGET_STOP ')), 1)


if __name__ == '__main__':
    unittest.main()
