"""LuaJIT probe checks for protocol 5, the adaptive window (native_gate_lookahead = 'adaptive')."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'tools/dev/native_gate_probe.lua').read_text()
MUX = (ROOT/'tools/dev/native_gate_inputs_core.lua').read_text()


class AdaptiveProbeTests(unittest.TestCase):
    def setUp(self, config="slot='p2',native_gate_clients=3,native_gate_lookahead='adaptive'"):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_defaults=false,native_gate_test=true,test_profile='45841',%s,
            native_gate_hash='clock',native_gate_snapshot_sha=string.rep('a',64),
            port=12345,token=string.rep('c',32)}
          X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
          logs={};loads=0;boots=0;is_menu=true;calls={};reports={};proposals=0
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0',wire_version=3,clients=3,inputs={'','',''},
            adaptive=1,lookahead=1,pilot_delay=3,quiet=0,lag=2,dropped=0}
          local gate={boot=function(slot) boots=boots+1;calls[#calls+1]='boot';return 0 end,
            poll=function() return state end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick,hash,blob) reports[#reports+1]=tick..':'..tostring(blob);state.report_tick=tick;return 1 end,
            propose=function(tick,blob) proposals=proposals+1;return 1 end,
            adaptive_status=function() return 'adaptive kmax=4 epoch='..state.adaptive..' k='..state.lookahead end,
            stop=function() state.active=0;return 1 end}
          gate.wire=function(version,clients) calls[#calls+1]='wire '..version..'/'..clients;return 1 end
          gate.lookahead=function(k) calls[#calls+1]='lookahead '..k;return 1 end
          gate.lookahead_adaptive=function(k) calls[#calls+1]='adaptive '..k;return adaptive_result or 1 end
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,
              GetCurrentGameTime=function() return 7 end,IsStartmenu=function() return is_menu end,
              ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          SetScript=function(name,fn) update=fn end
          GetSaveList=function() return {{filename='save_009'}} end
          LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
        ''' % config)

    def values(self, name):
        return list(self.lua.globals()[name].values())

    def logs(self):
        return self.values('logs')

    def admitted(self, tick=1, extra=''):
        """Boot, load, reload the UI and run one admitted tick."""
        self.lua.execute(SOURCE)
        self.lua.execute('update();update();update()')
        self.lua.execute(f'state.boot_phase=3;state.issued={tick};is_menu=false;{extra}')
        self.lua.execute(SOURCE)
        self.lua.execute('update()')

    def test_protocol_5_is_configured_after_the_wire_before_boot_and_after_reload(self):
        self.admitted()
        self.assertEqual(self.values('calls'), ['wire 3/3', 'adaptive 4', 'boot', 'wire 3/3', 'adaptive 4', 'boot'])
        self.assertEqual(self.values('reports'), ['1:nil'])
        logs = self.logs()
        self.assertTrue(any(line.endswith('ARMED slot=p2 hash_scope=clock wire=3 clients=3 lookahead=adaptive max=4') for line in logs))
        self.assertTrue(any('ADAPTIVE_PLAN tick=1 epoch=1 k=1 pilot_delay=3 adaptive kmax=4 epoch=1 k=1' in line for line in logs), logs)
        self.assertFalse(any('ERROR' in line for line in logs))
        # A later entry is logged once, on the tick its first permit is applied.
        self.lua.execute('state.adaptive=2;state.lookahead=3;state.issued=2;update();state.issued=3;update()')
        self.assertEqual(sum('ADAPTIVE_SCHEDULE tick=2 epoch=2 k=3' in line for line in self.logs()), 1)
        self.assertEqual(sum('ADAPTIVE_' in line for line in self.logs()), 2)
        self.lua.execute('state.active=0;state.completed=3;setmetatable(state,{__index=function() return 0 end});update()')
        self.assertTrue(any('ADAPTIVE_STOP adaptive kmax=4 epoch=2' in line for line in self.logs()))
        self.assertFalse(any('LOOKAHEAD_STOP' in line for line in self.logs()))

    def test_window_maximum_is_passed_and_checked(self):
        self.setUp("slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=2")
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.values('calls'), ['wire 3/2', 'adaptive 2', 'boot'])
        bad = {"slot='p1',native_gate_lookahead='adaptive'": 'needs the protocol 3 input wire',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=0": 'maximum must be 1..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=9": 'maximum must be 1..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=1.5": 'maximum must be 1..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max='2'": 'maximum must be 1..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='Adaptive'": 'must be 0..8 or adaptive',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_lookahead_max=2": 'needs native_gate_lookahead adaptive',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_pilot_inputs=true,"
               "native_gate_pilot_source='wire',native_gate_pilot_delay=6": 'relay chooses the pilot delay',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_pilot_predict=true": 'excludes native_gate_pilot_predict',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_player_orders=true": 'player order delay must be at least',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=3,native_gate_player_orders=true,"
               "native_gate_player_orders_delay=3": 'player order delay must be at least'}
        for config, message in bad.items():
            self.setUp(config)
            with self.assertRaisesRegex(LuaError, message, msg=config): self.lua.execute(SOURCE)
            self.assertEqual((self.lua.globals().boots, self.lua.globals().loads), (0, 0))
        # Orders whose delay covers the largest window pass the check (their adapter is then required as usual).
        self.setUp("slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=2,native_gate_player_orders=true")
        with self.assertRaisesRegex(LuaError, 'player order adapter missing'): self.lua.execute(SOURCE)

    def test_dll_without_protocol_5_or_refusing_it_never_boots(self):
        self.lua.execute('adaptive_result=0')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED lookahead=adaptive max=4' in line for line in self.logs()))
        self.setUp()   # an older gate DLL (or today's Linux gate): no gate.lookahead_adaptive
        self.lua.execute('''
          local load=package.loadlib;package.loadlib=function(path) local gate=load(path)();gate.lookahead_adaptive=nil;return function() return gate end end''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED lookahead=adaptive max=4' in line for line in self.logs()))

    def test_inputs_name_their_proposal_tick_and_gaps_carry_none(self):
        self.lua.execute('''X4LAN_NATIVE_INPUTS={
          apply=function(tick,inputs) applied=tick..'|'..table.concat(inputs,',');return 'ORDERS' end,
          proposal=function(tick) proposed_at=tick;return nil end}''')
        self.admitted(tick=7, extra="state.inputs={'01','','0203'};state.inputs_tick=4;state.lookahead=2")
        self.assertEqual(self.lua.globals().applied, '7|01,,0203')
        self.assertTrue(any('INPUTS tick=7 proposed=4 blobs=01,,0203' in line for line in self.logs()), self.logs())
        self.assertEqual(self.lua.globals().proposed_at, 7)
        self.lua.execute("state.issued=8;state.inputs={'','',''};state.inputs_tick=nil;update()")   # a gap permit
        self.assertEqual(self.values('reports'), ['7:nil', '8:nil'])
        self.assertFalse(any('ERROR' in line for line in self.logs()))
        for extra in ('state.adaptive=nil', 'state.adaptive=0', 'state.inputs_tick=7', 'state.inputs_tick=1', 'state.lookahead=nil'):
            self.setUp()
            self.admitted(tick=7, extra=extra)
            self.assertEqual(self.values('reports'), [], extra)
            self.assertTrue(any('ERROR' in line and 'adaptive mismatch' in line for line in self.logs()), (extra, self.logs()))

    def test_quiet_tick_seals_the_empty_proposal_and_keeps_the_adapter_inputs(self):
        self.lua.execute('''X4LAN_NATIVE_INPUTS={apply=function() return nil end,
          proposal=function(tick) asked=(asked or 0)+1;return '0a' end}''')
        self.admitted(tick=5, extra='state.quiet=1;state.lag=nil')
        self.assertEqual((self.lua.globals().asked, self.lua.globals().proposals), (None, 0))
        self.assertEqual(self.values('reports'), ['5:nil'])
        self.lua.execute('state.quiet=0;state.lag=2;state.issued=6;update()')
        self.assertEqual((self.lua.globals().asked, self.lua.globals().proposals), (1, 1))
        self.assertFalse(any('ERROR' in line for line in self.logs()))

    PILOT = '''
      X4LAN_CONFIG.native_gate_serial_pathing=true
      X4LAN_CONFIG.native_gate_world_identities=true
      X4LAN_CONFIG.native_gate_pilot_inputs=true
      X4LAN_CONFIG.native_gate_pilot_slots={'0000000000176a13','0000000000176a74'}
      X4LAN_CONFIG.native_gate_pilot_source='wire'
      local gate=package.loadlib('gate')();delays={}
      gate.pilot_inputs=function(manifest,source,delay,first,last,suppress) delays[#delays+1]=delay;return 1 end
      gate.pilot_inputs_status=function() return 'armed=1 source=wire failures=0' end
      gate.pilot_inputs_frame=function() return 'PILOT_1:1:0:0000000000000000' end
      gate.pilot_apply=function(slot,record) return 1 end
      gate.pilot_capture=function() return '01'..string.rep('1',54) end
      gate.adaptive_plan=function() if plan then return plan,plan-1,1 end return nil end
    '''

    def test_pilot_records_arm_with_the_largest_delay_then_the_plans(self):
        self.setUp("slot='p1',native_gate_clients=2,native_gate_lookahead='adaptive',native_gate_lookahead_max=3")
        self.lua.execute(self.PILOT)
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        # The reloaded UI re-arms after the plan arrived (pilot delay 4: k=2).
        self.lua.execute("plan=4;state.boot_phase=3;state.issued=1;is_menu=false;state.clients=2;state.inputs={'',''};state.pilot_delay=4")
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.values('delays'), [5, 4])
        logs = self.logs()
        self.assertTrue(any('PILOT_INPUTS_ARMED source=wire delay=5 ' in line for line in logs))
        self.assertTrue(any('PILOT_INPUTS_ARMED source=wire delay=4 ' in line for line in logs))
        self.assertFalse(any('ERROR' in line for line in logs), logs)


if __name__ == '__main__': unittest.main()
