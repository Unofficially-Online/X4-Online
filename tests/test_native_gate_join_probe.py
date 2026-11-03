"""Hot join, the probe's own state (tools/dev/native_gate_probe.lua, providers of native_gate_join_core.lua) under LuaJIT:
a session's probe runs the battle fixture to its save tick T, its providers' text is what the package carries, and a joiner's
probe restored from it at T+1 must declare exactly the session's state of T+1 (J1 j1-10: the joiner stopped at its first
fixture sample, 'native health sample missing', because its actors were FFI uint64s that GetComponentData refuses).
reports/LOCKSTEP-HOTJOIN-20261101.txt 1.5. Synthetic world, no X4."""
from pathlib import Path
import re
import sys
import unittest
from lupa.luajit21 import LuaRuntime, LuaError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_gate_join as join  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'tools/dev/native_gate_probe.lua').read_text()
MUX = (ROOT/'tools/dev/native_gate_inputs_core.lua').read_text()
JOIN = (ROOT/'tools/dev/native_gate_join_core.lua').read_text()
SAVED = [f'{0x176a13 + 0x61*i:016x}' for i in range(12)]
T, DIES = 12, 7          # the save tick; actor 8 dies (is a wreck from) this tick on

# The engine and the world, the same for the session and the joiner except the runtime IDs (the joiner's load numbers
# its objects anew: base 200 against the session's 100) and what the test changes. A component ID is a Lua number, as
# ConvertIDTo64Bit gives it; ConvertStringToLuaID gives a LuaID (a table here); GetComponentData answers both and refuses
# anything else as X4 does ('got cdata, expected component ID'), returning nothing.
ENV = r'''
X4LAN_SHA256={digest=function(data) return string.rep('b',64) end}
logs={};reports=0;boots=0;is_menu=false;hashed={};registered={};refusals={}
state={active=1,boot_phase=3,issued=1,report_tick=0,rng='0',wire_version=3,clients=3,inputs={'','',''}}
world={tick=1,base=RUNTIME_BASE,wreck_from=DIES,wreck_live=false,wreck_guns=true}
local function actor_of(id)
 local n=tonumber(id)
 if n and n>world.base and n<=world.base+12 then return n-world.base end
end
local function gun_of(id)   -- a gun: base*100 + actor*10 + slot
 local n=tonumber(id)
 if n and n>=world.base*100 then local rest=n-world.base*100;return math.floor(rest/10),rest%10 end
end
local function alive(i) return i~=8 or world.tick<world.wreck_from or world.wreck_live end
local LUAID={}
ConvertStringToLuaID=function(decimal) assert(type(decimal)=='string' and decimal:match('^%d+$'),'luaid '..tostring(decimal));return setmetatable({id=decimal},LUAID) end
ConvertIDTo64Bit=function(luaid) assert(getmetatable(luaid)==LUAID,'ConvertIDTo64Bit needs a LuaID');return tonumber(luaid.id) end
local function component(value)
 if type(value)=='number' then return value end
 if getmetatable(value)==LUAID then return tonumber(value.id) end
 refusals[#refusals+1]='got '..type(value);return nil
end
GetComponentData=function(value,...)
 local id=component(value)
 if not id then return nil end
 local keys={...}
 if keys[1]=='macro' then return 'weapon_gen_s_plasma_01_mk2_macro' end
 local i=assert(actor_of(id),'not an actor '..id)
 assert(keys[1]=='hullpercent' and keys[2]=='shieldpercent')
 return 100-i,90-i
end
local gate={boot=function() boots=boots+1;return 0 end,
 poll=function() return state end,
 load_requested=function() state.boot_phase=1;return 1 end,
 report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
 stop=function() state.active=0;return 1 end,
 wire=function(version,clients) assert(version==3 and clients==3);return 1 end,
 propose=function() return 1 end,
 snapshot_fixture=function(manifest) assert(#manifest==203);return 1 end,
 snapshot_enroll=function(i,runtime) assert(runtime==tostring(world.base+i));return 1 end,
 snapshot_actor=function(i) return string.format('id=%d saved=%s runtime=%016x flight=steering bound_tick=0',i,SAVED[i],world.base+i) end,
 saved_identity=function(decimal) local i=assert(actor_of(decimal),'saved identity of '..decimal);return SAVED[i] end,
 actor=function(id,i)
  local v=string.rep('3f8000004000000040400000deadbeef',6)
  return 'attention=9;current='..v..';previous='..v..';hull_bits=4004000000000000;shield=none'
 end,
 body=function(i,which) return 'none' end,
 weapon=function(i,slot) return 'tick='..world.tick..' calls=1' end,
 register_weapon=function(id,i,slot)
  local a,s=gun_of(id);assert(a==i and s==slot,'gun '..tostring(id)..' is not actor '..i..' slot '..slot)
  if world.refuse_guns_of==i then return 0 end
  registered[#registered+1]=i..':'..slot;return 1
 end,
 world_capture=function(tick) return 'tick='..tick..' objects=12 state=WORLD_IMPORTED_1:'..tick..':12:'..string.rep('ab',32)..':'..string.rep('cd',32)..':'..string.rep('ef',32)..':'..string.rep('01',32) end,
 world_banks=function() return 'read=0 write=1' end,
 world_page=function(n) assert(n==0);return 0 end,
 world_detail=function(n) assert(n==0);return 0 end,
 world_watch=function(saved) watched=(watched or '')..saved..',';return 1 end,
 join_arm=function() return 1 end,join_result=function() return nil end,join_handover=function() return 1 end,
 join_load=function() return 1 end,join_lua=function() return 1 end,join_status=function() return 'state=idle' end,
 join_base=function() return JOIN_BASE end,join_lua_state=function() return JOIN_TEXT end,join_unpaired=function() return nil end}
package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
-- FFI calls take a number or an FFI uint64 (cdata), as LuaJIT converts both to UniverseID.
local real=require('ffi');pcall(real.cdef,'typedef uint64_t UniverseID;')
local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,new=real.new,
 C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
  IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end,
  IsComponentOperational=function(id)
   id=tonumber(id)
   local i=actor_of(id);if i then return alive(i) end
   return gun_of(id)~=nil
  end,
  GetObjectPositionInSector=function(id)
   local i=assert(actor_of(tonumber(id)));return {x=i*100+world.tick,y=100000,z=-i,yaw=0.25*i,pitch=0,roll=0}
  end,
  GetNumUpgradeSlots=function() return 4 end,
  GetUpgradeSlotCurrentComponent=function(id,kind,slot)
   local i=assert(actor_of(tonumber(id)));assert(kind=='weapon')
   if slot>3 or (i==8 and not alive(8) and not world.wreck_guns) then return 0 end
   return world.base*100+i*10+slot
  end}}
require=function(name) assert(name=='ffi');return ffi end
DebugError=function(s) logs[#logs+1]=s end
events={};RegisterEvent=function(name,fn) events[name]=fn end
SetScript=function(name,fn) update=fn end
raised={};AddUITriggeredEvent=function(screen,control) raised[#raised+1]=control..'@'..state.issued end
GetSaveList=function() return {{filename='save_009'}} end
LoadGame=function() error('no load in this test') end
'''


def config(slot, join, extra=''):
    saved = ','.join(f"'{s}'" for s in SAVED)
    return f'''
X4LAN_CONFIG={{native_gate_defaults=false,native_gate_test=true,test_profile='45841',slot='{slot}',
  native_gate_hash='battle',native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),
  native_gate_clients=3,native_gate_join={join},native_gate_world_identities=true,native_gate_world_hash_stride=5,
  native_gate_world_hash_watch={{'{SAVED[0]}'}},
  native_gate_actor_diag=true,native_gate_weapon_diag=true,native_gate_strict_state=true,native_gate_strict_physics=true,
  native_gate_snapshot_fixture={{{saved}}}}}
{extra}'''


def lua_list(values):
    return '{' + ','.join(f"'{v}'" for v in values) + '}'


class JoinProbeState(unittest.TestCase):
    def runtime(self, slot, join, base, extra='', join_base='nil', text='nil'):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(f'SAVED={lua_list(SAVED)};RUNTIME_BASE={base};DIES={DIES};JOIN_BASE={join_base};JOIN_TEXT={text}')
        lua.execute(ENV)
        # The digest preimage of each tick (X4LAN_SHA256 sees what gate.report gets the hash of).
        lua.execute("X4LAN_SHA256.digest=function(data) hashed[state.issued]=data;return string.rep('b',64) end")
        lua.execute(config(slot, join, extra))
        lua.execute(MUX);lua.execute(JOIN);lua.execute(SOURCE)
        return lua

    def session(self, extra='', last=T+1, inputs=None, slot='p1'):
        """Slot 1 of a running session: ticks 1..last (the fixture enrolled at tick 1); returns it and its providers' text at T.
        inputs: {tick: (slot, blob)} admitted at that tick (a relay record in an absent slot's stand-in blob)."""
        lua = self.runtime(slot, "{save='report',absent={3}}", 100, extra)
        lua.execute('update()')
        lua.execute('''for i=1,12 do events['X4LAN_NativeGate.Actor'..i](nil,ConvertStringToLuaID(tostring(100+i))) end
                       events['X4LAN_NativeGate.Ready'](nil,12)''')
        text = None
        for tick in range(2, last+1):
            blobs = ['', '', '']
            if inputs and tick in inputs:
                blobs[inputs[tick][0]-1] = inputs[tick][1]
            lua.execute("state.inputs={" + ','.join(f"'{b}'" for b in blobs) + "}")
            lua.execute(f'world.tick={tick};state.issued={tick};update()')
            if tick == T: text = lua.eval('X4LAN_JOIN_CORE.state_text()')
        self.assertEqual(lua.eval('reports'), last)
        return lua, text

    def joiner(self, text, extra='', before=''):
        lua = self.runtime('p3', "{joiner={package='./extensions/x4_lan_lab/native/join.x4join',sha=string.rep('d',64),mode='report'},absent={3}}",
                           200, extra, join_base=str(T), text=_lua_string(text))
        lua.execute(f'world.tick={T+1};state.issued={T+1};state.report_tick={T};{before}')
        lua.execute('update()')
        return lua

    @staticmethod
    def lines(lua):
        return [line for call in lua.globals().logs.values() for line in str(call).split('\n')]

    def refused(self, lua, pattern):
        """The probe's frame turns a failed assertion into an ERROR line and stops the gate: no report of T+1."""
        errors = [l for l in self.lines(lua) if 'NATIVE_GATE ERROR' in l]
        self.assertTrue(any(re.search(pattern, l) for l in errors), errors)
        self.assertEqual(lua.eval('reports'), 0)
        self.assertEqual(lua.eval('state.active'), 0)

    def test_the_joiner_declares_the_session_state_of_t_plus_1_wreck_included(self):
        session, text = self.session()
        self.assertTrue(text.startswith('fixture=v2|'+','.join(SAVED)+'|000000010000|1.2.3/1.2.3/'), text)
        self.assertIn('\nworld_hash=WORLD_IMPORTED_1:10:12:', text)       # the last capture before T (stride 5)
        joiner = self.joiner(text)
        self.assertEqual(joiner.eval('reports'), 1)
        mine, theirs = joiner.eval(f'hashed[{T+1}]'), session.eval(f'hashed[{T+1}]')
        self.assertEqual(mine, theirs)                                     # the whole declared state of T+1
        self.assertIn('|8/dead/body0:none/body1:none|', mine)              # the wreck's row, as the session's
        self.assertIn(f'|1/{100+T+1}/100000/-1/0.25/0/0/99/89/raw:', mine)  # actor 1 sampled through GetComponentData
        self.assertIn('WORLD_IMPORTED_1:10:12:', mine)                     # the session's world state until the joiner's own capture
        self.assertEqual(len(joiner.globals().refusals), 0)
        lines = self.lines(joiner)
        self.assertFalse([l for l in lines if 'ERROR' in l], lines)
        self.assertEqual(sum('SNAPSHOT_JOIN_ACTOR id=' in l for l in lines), 12)
        self.assertTrue(any('SNAPSHOT_JOIN_ACTOR id=8 ' in l and l.endswith(' session=dead joiner=dead') for l in lines))
        # The session's guns, registered at the restore (the wreck's too: they are still on it), none by the sample.
        self.assertEqual(sorted(joiner.globals().registered.values()), sorted(session.globals().registered.values()))
        self.assertEqual(len(joiner.globals().registered), 36)
        self.assertIn('JOIN_FIXTURE_GUNS id=8 session=1.2.3 registered=1.2.3 absent=- refused=-', '\n'.join(lines))
        self.assertEqual(joiner.eval('watched'), SAVED[0]+',')            # the world watch, armed before the joiner's capture
        # The next ticks go on from the restored state: the joiner's own capture at the stride tick.
        for tick in (T+2, T+3):
            joiner.execute(f'world.tick={tick};state.issued={tick};update()')
            session.execute(f'world.tick={tick};state.issued={tick};update()')
            self.assertEqual(joiner.eval(f'hashed[{tick}]'), session.eval(f'hashed[{tick}]'))
        self.assertIn(f'WORLD_IMPORTED_1:{T+3}:12:', joiner.eval(f'hashed[{T+3}]'))

    CENSUS = 'X4LAN_CONFIG.native_gate_world_hash_join=true\nX4LAN_CONFIG.native_gate_status_stride=5\n'

    def test_the_join_census_captures_the_world_at_t_plus_1_on_the_session_and_the_joiner(self):
        # native_gate_world_hash_join (1.6): a session client that applied the SAVE_AT of T captures its world at T+1 (in the
        # mask or not), the joiner at its tick base + 1; both declare that capture from T+1 on, the strides stay. T+1 is a
        # written tick (status stride 5: 13 is none), so its WORLD_HASH line is in the log.
        save_at = {4: (3, join.record_blob(join.encode_record(op=join.OP_SAVE_AT, slot=3, tick=T, mask=3, nonce=7)))}
        session, text = self.session(extra=self.CENSUS, inputs=save_at)
        self.assertIn(f'WORLD_IMPORTED_1:{T+1}:12:', session.eval(f'hashed[{T+1}]'))
        self.assertIn('WORLD_IMPORTED_1:10:12:', session.eval(f'hashed[{T}]'))
        self.assertIn(f'NATIVE_GATE WORLD_HASH tick={T+1} objects=12', '\n'.join(self.lines(session)))
        self.assertIn('NATIVE_GATE WORLD_HASH_JOIN_ARMED slot=1 joiner=0', '\n'.join(self.lines(session)))
        self.assertIn('NATIVE_GATE JOIN_ARMED slot=1 save=report joiner=0 absent=', '\n'.join(self.lines(session)))   # the J1 --expect
        joiner = self.joiner(text, extra=self.CENSUS)
        self.assertEqual(joiner.eval(f'hashed[{T+1}]'), session.eval(f'hashed[{T+1}]'))
        lines = '\n'.join(self.lines(joiner))
        self.assertIn(f'NATIVE_GATE WORLD_HASH tick={T+1} objects=12', lines)
        self.assertNotIn('ERROR', lines)
        # A joiner's first frame is a written tick with or without the census (J1 j1-11's joiner held tick 634 and lost its
        # JOIN_ADMITTED line and JOIN_UNPAIRED rows).
        held = self.joiner(text, extra='X4LAN_CONFIG.native_gate_status_stride=5')
        self.assertTrue(any(l.startswith('[X4LAN] NATIVE_GATE JOIN_ADMITTED base=12 ') for l in self.lines(held)), self.lines(held)[-5:])
        # A client outside the saver mask captures too (every client applies the SAVE_AT); without one, nothing extra.
        other = {4: (3, join.record_blob(join.encode_record(op=join.OP_SAVE_AT, slot=3, tick=T, mask=2, nonce=7)))}
        outside, _ = self.session(extra=self.CENSUS, inputs=other)
        self.assertEqual(outside.eval(f'hashed[{T+1}]'), session.eval(f'hashed[{T+1}]'))
        plain, _ = self.session(extra=self.CENSUS)
        self.assertIn('WORLD_IMPORTED_1:10:12:', plain.eval(f'hashed[{T+1}]'))
        # The key needs the join and the stride; only true.
        with self.assertRaisesRegex(LuaError, 'native_gate_world_hash_join needs native_gate_join'):
            self.runtime('p1', 'nil', 100, extra=self.CENSUS)
        with self.assertRaisesRegex(LuaError, 'native_gate_world_hash_join must be true'):
            self.runtime('p1', "{save='report',absent={3}}", 100, extra="X4LAN_CONFIG.native_gate_world_hash_join='yes'")

    def test_a_wreck_without_its_guns_is_logged_and_a_live_actor_without_one_refused(self):
        _, text = self.session()
        joiner = self.joiner(text, before='world.wreck_guns=false')
        self.assertIn('JOIN_FIXTURE_GUNS id=8 session=1.2.3 registered=- absent=1.2.3 refused=-', '\n'.join(self.lines(joiner)))
        self.assertEqual(len(joiner.globals().registered), 33)
        self.assertEqual(joiner.eval('reports'), 1)
        joiner = self.joiner(text, before='world.refuse_guns_of=8')       # the gate refuses the wreck's guns: logged
        self.assertIn('JOIN_FIXTURE_GUNS id=8 session=1.2.3 registered=- absent=- refused=1.2.3', '\n'.join(self.lines(joiner)))
        self.assertEqual(joiner.eval('reports'), 1)
        changed = text.replace('1.2.3/1.2.3/', '1.2.4/1.2.3/', 1)          # actor 1: a slot this world has no gun in
        self.refused(self.joiner(changed), r'fixture actor 1 \(live at T\) has no gun in slot 4')
        self.refused(self.joiner(text, before='world.refuse_guns_of=2'), 'fixture gun registration refused actor=2 slot=1')

    def test_a_wreck_of_the_session_operational_on_the_joiner_is_refused(self):
        _, text = self.session()
        self.refused(self.joiner(text, before='world.wreck_live=true'),
                     'fixture actor 8 was a wreck in the session at T .* operational component on this joiner')

    def test_a_fixture_state_of_an_older_saver_or_another_fixture_is_refused(self):
        _, text = self.session()
        older = text.split('\n')
        older[0] = 'fixture=v1|' + ','.join(SAVED)
        self.refused(self.joiner('\n'.join(older)), r'v2\|ids\|dead\|guns')
        other = text.replace(SAVED[3], f'{1:016x}', 1)
        self.refused(self.joiner(other), 'is not this client fixture')

    def test_the_world_state_without_a_capture_is_refused(self):
        _, text = self.session()
        lines = [l if not l.startswith('world_hash=') else 'world_hash=-' for l in text.split('\n')]
        self.refused(self.joiner('\n'.join(lines)), 'session world hash state - is no WORLD_IMPORTED_1 state')

    def test_the_session_dead_digits_follow_the_samples(self):
        _, early = self.session(last=T)
        lua, _ = self.session(extra='', last=DIES-1)
        self.assertIn('|000000000000|', lua.eval('X4LAN_JOIN_CORE.state_text()'))
        self.assertIn('|000000010000|', early)

    PILOT = '''
      X4LAN_CONFIG.native_gate_serial_pathing=true
      X4LAN_CONFIG.native_gate_pilot_inputs=true
      X4LAN_CONFIG.native_gate_pilot_slots={SAVED[1],SAVED[2],SAVED[3]}
      X4LAN_CONFIG.native_gate_pilot_source='script'
      X4LAN_CONFIG.native_gate_pilot_window={%d,%d}
      local gate=package.loadlib('gate')()
      gate.pilot_inputs=function() return 1 end
      gate.pilot_inputs_status=function() return 'armed=1' end
      gate.pilot_inputs_frame=function() return 'PILOT_1:0' end
      gate.pilot_absent=function(mask) assert(mask==4);return 1 end
      gate.pilot_absent_state=function() return 'v1|absent=4' end
      gate.pilot_absent_restore=function(text) assert(text=='v1|absent=4');return 1 end
    '''

    def test_the_pilot_window_state_joins_only_before_take_2(self):
        # The session's window and its take-2 (start - 5) after T+1: the window's Lua state is still unset on the session.
        session, text = self.session(extra=self.PILOT % (T+6, 600))
        self.assertIn(f'\npilot_window=v1|{T+6},600\n', text)
        self.assertIn('\npilot_absent=v1|absent=4\n', text)
        joiner = self.joiner(text, extra=self.PILOT % (T+6, 600))
        self.assertEqual(joiner.eval(f'hashed[{T+1}]'), session.eval(f'hashed[{T+1}]'))
        self.assertFalse([l for l in self.lines(joiner) if 'ERROR' in l])
        # A session at or past take-2 (inside the window, or after it) and a joiner with another window are refused.
        for window in ((T+5, 600), (2, 5)):
            _, text = self.session(extra=self.PILOT % window)
            self.refused(self.joiner(text, extra=self.PILOT % window), f'session tick T={T} is past the pilot window take-2')
        _, text = self.session(extra=self.PILOT % (T+6, 600))
        self.refused(self.joiner(text, extra=self.PILOT % (T+7, 600)), f'session pilot window v1\\|{T+6},600 is not this client window')

    def test_adapters_without_a_provider_and_the_tick_1_world_trace_are_refused(self):
        stub = 'X4LAN_NATIVE_PLAYER_ORDERS={bind=function() end,apply=function() end,proposal=function() end};'
        for key in ('native_gate_hybrid', 'native_gate_player_orders'):
            with self.assertRaisesRegex(LuaError, 'hot join: '+key+' keeps Lua state across ticks'):
                self.runtime('p1', "{save='report'}", 100, extra=stub+f'X4LAN_CONFIG.{key}=true')
        with self.assertRaisesRegex(LuaError, 'arms at the session tick 1 only'):
            self.runtime('p3', "{joiner={package='p',sha=string.rep('d',64),mode='report'}}", 200,
                         extra='X4LAN_CONFIG.native_gate_world_trace_ticks=10')


def _lua_string(text):
    return '[==[' + text + ']==]'


if __name__ == '__main__':
    unittest.main()
