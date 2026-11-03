"""Missions and conversations of slot 1 (lockstep_ui_missions.h, native_gate_ui_replay families 'missions' and 'conversation',
default off; reports/LOCKSTEP-UI-MISSIONS-20261101.txt).

Covers the Lua half under LuaJIT with a mocked gate: the family bits, mission and offer IDs as cue references in the value codec
(and refused without the families), the conversation wrappers (slot 1 captured, nothing runs before the apply, every client the
same stock call; slots 2+ refused with an info line at most every 90 ticks; the probe's, the simulation's and the apply's own
calls untouched), the natively captured dialog choice decoding as the Lua one, the mission events reaching gate.ui_missions_run,
the accept with its offer as a cue, the frame's rows and receipt part, the scripted ops and the canonical fixture; the real probe
arming the families after the guard; the mirrors (Lua, C, Python, the guard's inventory, the MD fixture); the option checker
and the overlay builder. The native half is tested in C: src/native/engine/test_lockstep_ui_missions.h."""
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

ENGINE = ROOT/'src/native/engine'
MISSIONS_H = (ENGINE/'lockstep_ui_missions.h').read_text(encoding='utf-8')
MD = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))
MASK = 256 + 2048 + 16384      # events, missions, conversation

# Mission and offer IDs 9001.. are MD cues (not components); their reference is the ID and a check of their module.
MOCK = r'''
  gate.ui_replay_local=function() return LOCAL or 0 end
  CUES={['9001']='c0ffee01',['9002']='c0ffee03'}
  gate.ui_missions_cue_ref=function(d) local c=CUES[d];if not c then return nil end;return string.format('%016x',tonumber(d))..c end
  gate.ui_missions_cue_resolve=function(h)
    local d=string.format('%d',tonumber(h:sub(1,16),16))
    if not CUES[d] then return nil,'gone' end
    if CUES[d]~=h:sub(17,24) then FAILED='ui_ref_mismatch';return nil,'mismatch' end
    return d end
  gate.ui_missions_push=function(op,hex) pushed[#pushed+1]={op,hex};return PUSH_RESULT or 1 end
  gate.ui_missions_run=function(slot,hex) mission_runs[#mission_runs+1]=slot..':'..hex;return RUN_OK or 1,RUN_WHAT or 'AbortMission(9001)' end
  gate.ui_missions_rows=function() local r=MIS_ROWS;MIS_ROWS=nil;return r end
  gate.ui_missions_state=function() return MIS_STATE or 'missions=0 offers=1 conversation=1.0' end
  gate.ui_missions_status=function() return 'ui_missions=armed' end
  gate.ui_missions_frame=function() return 'UIMIS_1:o1/c1.0' end
  mission_runs={};notices={}
  env.SignalObject=function(object,param,value) stock_calls[#stock_calls+1]='SignalObject:'..tostring(ids[object])..'/'..tostring(param)..'/'..
    (value~=nil and (type(value)=='userdata' and 'id:'..tostring(ids[value]) or tostring(value)) or 'nil') end
  env.AddUITriggeredEvent=function(screen,control,value) stock_calls[#stock_calls+1]='AddUITriggeredEvent:'..screen..'/'..tostring(control) end
  local function conv(name) return function(...)
    local out={};for i=1,select('#',...) do local v=select(i,...)
      out[i]=type(v)=='userdata' and 'id:'..tostring(ids[v]) or type(v)=='table' and '{'..table.concat(v,'/')..'}' or tostring(v) end
    stock_calls[#stock_calls+1]=name..':'..table.concat(out,',') end end
  for _,name in ipairs(X4LAN_UI_REPLAY_CORE.CONVERSATION) do env[name]=conv(name) end
  X4LAN_UI_REPLAY_STOCK=nil
  function missions_session(slot,mask,extra)
    local p={gate=gate,C=C,env=env,log=log,mask=mask,slot_ships=function() return SLOT_SHIPS end,own_slot=slot,
      notify=function(text) notices[#notices+1]=(S and S.tick or 0)..':'..text end}
    for k,v in pairs(extra or {}) do p[k]=v end
    S=X4LAN_UI_REPLAY_CORE.session(p)
    return S
  end
'''


def session(slot, mask=MASK, extra=''):
    lua, g = uirep.client()
    lua.execute(MOCK)
    lua.execute(f'missions_session({slot},{mask}{"," + extra if extra else ""})')
    return lua, g


def double_hex(value):
    import struct
    return struct.pack('<d', value).hex()


class FamilyTests(unittest.TestCase):
    def test_bits_and_the_refusal_text(self):
        lua, _ = uirep.client()
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'missions'})"), 2048)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'conversation'})"), 16384)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'events','missions','conversation'})"), MASK)
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.families(true)'), 63)     # true keeps the first families only
        mask, why = lua.eval("X4LAN_UI_REPLAY_CORE.families({'mission'})")
        self.assertIsNone(mask)
        mask, why = lua.eval("X4LAN_UI_REPLAY_CORE.families(1)")
        self.assertTrue(why.endswith('missions, build, assets, conversation, faction'))

    def test_the_mirrors_agree(self):
        lua, g = uirep.client()
        inputs = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
        bits = {k: int(v) for k, v in re.findall(r'UIREP_FAMILY_(\w+)=(\d+)', inputs)}
        self.assertEqual((bits['MISSIONS'], bits['CONVERSATION']), (2048, 16384))
        families = sum(int(v) for v in re.search(r'UIREP_FAMILIES=([\d|]+)', inputs).group(1).split('|'))
        self.assertEqual(families & (2048 | 16384), 2048 | 16384)   # implemented (bits: docs/LOCKSTEP-REGISTRY.txt)
        replay = (ENGINE/'lockstep_ui_replay.h').read_text(encoding='utf-8')
        self.assertIn('UIREP_OP_MISSION=0x48,UIREP_OP_CONVERSATION=0x49', replay)
        self.assertEqual((lua.eval('X4LAN_UI_REPLAY_CORE.OP_MISSION'), lua.eval('X4LAN_UI_REPLAY_CORE.OP_CONVERSATION')), (0x48, 0x49))
        # The conversation globals: the wire's numbers are the Lua list's order and the native names.
        native = re.search(r'uimis_conv_names\[UIMIS_CONV_KINDS\]=\{"",(.*?)\};', MISSIONS_H, re.S).group(1)
        self.assertEqual(re.findall(r'"(\w+)"', native), uirep.values(g.X4LAN_UI_REPLAY_CORE.CONVERSATION))
        # Every entry the families take is the guard's, in its group.
        exports = re.findall(r'\{"(\w+)",0x([0-9a-f]+)u\}', re.search(r'uimis_exports\[UIMIS_KINDS\]=\{(.*?)\};', MISSIONS_H, re.S).group(1))
        self.assertEqual([e[0] for e in exports], ['AbortMission', 'SetActiveMission', 'MissionBriefingInteractButtonClicked', 'StartBriefing', 'StopBriefing'])
        for name, _ in exports:
            self.assertEqual(inv.INVENTORY[('ffi', name)][1], 'missions', name)
        for name in uirep.values(g.X4LAN_UI_REPLAY_CORE.CONVERSATION):
            self.assertEqual(inv.INVENTORY[('lua', name)][1], 'conversation', name)
        # The Python tools know the names; the MD fixture's names are the script's.
        for families in (uirep.options.UI_REPLAY_FAMILIES, uirep.players.UI_REPLAY_FAMILIES):
            self.assertIn('missions', families)
            self.assertIn('conversation', families)
        for name in ('X4LAN test offer', 'X4LAN test mission', 'X4LAN_Talk', 'X4LAN_UiMissions'):
            self.assertIn(name, MD)
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.FIXTURE_OFFER'), 'X4LAN test offer')


class CueReferenceTests(unittest.TestCase):
    def test_an_offer_travels_as_a_cue_and_resolves_on_every_client(self):
        calls = []
        for _ in (1, 2):
            lua, g = session(1)
            lua.execute("LOCAL=1;env.SignalObject(make_id('100'),'accept',make_id('9001'))")
            self.assertEqual(uirep.values(g.stock_calls), [])
            pushed = uirep.values(g.pushed)
            self.assertEqual(len(pushed), 1)
            op, hex_ = pushed[0][1], pushed[0][2]
            self.assertEqual(op, 0x47)
            self.assertIn('08' + '0000000000002329' + 'c0ffee01', hex_)     # tag cue_luaid, ID 9001, its check
            lua.execute(f"DUE={{[50]='1:71:{hex_}'}};S.apply(50)")
            calls.append(uirep.values(g.stock_calls))
        self.assertEqual(calls[0], calls[1])
        self.assertEqual(calls[0], ['SignalObject:100/accept/id:9001'])

    def test_a_64_bit_mission_id_is_a_cue_too_gone_and_mismatch(self):
        lua, g = session(1)
        lua.execute('''
          ctx=S.ctx
          h=X4LAN_UI_REPLAY_CORE.encode_call({9001ULL},1,ctx)
          args,n=X4LAN_UI_REPLAY_CORE.decode_call(h,ctx)
          first=tostring(args[1])
        ''')
        self.assertTrue(g.h.startswith('01' + '09'))   # tag cue_id
        self.assertEqual(g.first, '9001ULL')
        lua.execute("CUES['9001']=nil;args,why=X4LAN_UI_REPLAY_CORE.decode_call(h,ctx)")
        self.assertIsNone(g.args)
        self.assertEqual(g.why, 'gone')
        lua.execute("CUES['9001']='deadbeef';args,why=X4LAN_UI_REPLAY_CORE.decode_call(h,ctx)")
        self.assertEqual(g.why, 'mismatch')
        self.assertEqual(g.FAILED, 'ui_ref_mismatch')

    def test_without_the_families_a_cue_stays_unmapped(self):
        lua, g = session(1, mask=256)
        lua.execute("LOCAL=1;env.SignalObject(make_id('100'),'accept',make_id('9001'))")
        self.assertEqual(uirep.values(g.pushed), [])
        self.assertTrue(any('UI_REPLAY_REFUSED what=SignalObject why=unmapped args=accept' in l for l in uirep.values(g.logs)))


class ConversationTests(unittest.TestCase):
    def test_slot_1_captures_and_every_client_makes_the_same_stock_call(self):
        calls = []
        for _ in (1, 2):
            lua, g = session(1)
            lua.execute("LOCAL=1;env.StartConversationFromMenu('X4LAN_Talk',make_id('100'),{1,2});env.SelectDialogOption(2);env.CancelConversation()")
            self.assertEqual(uirep.values(g.stock_calls), [])
            pushed = uirep.values(g.pushed)
            self.assertEqual([p[1] for p in pushed], [0x49, 0x49, 0x49])
            self.assertEqual([p[2][:2] for p in pushed], ['01', '07', '05'])
            self.assertEqual(pushed[1][2], '07' + '01' + '03' + double_hex(2.0))
            self.assertEqual(pushed[2][2], '05' + '00')
            due = ';'.join('1:73:' + p[2] for p in pushed)
            lua.execute(f"DUE={{[50]='{due}'}};S.apply(50)")
            calls.append(uirep.values(g.stock_calls))
            self.assertEqual(uirep.values(g.results), ['50:1', '50:1', '50:1'])
            logs = uirep.values(g.logs)
            self.assertTrue(any('UI_MISSIONS_CAPTURED' in l and 'what=SelectDialogOption via=lua' in l for l in logs))
            self.assertTrue(any('UI_REPLAY_APPLIED tick=50 slot=1 op=73 result=applied what=SelectDialogOption(2)' in l for l in logs))
        self.assertEqual(calls[0], calls[1])
        self.assertEqual(calls[0], ['StartConversationFromMenu:X4LAN_Talk,id:100,{1/2}', 'SelectDialogOption:2', 'CancelConversation:'])

    def test_the_native_capture_decodes_as_the_lua_one(self):
        # lockstep_ui_missions.h uimis_guard: index 7, count 1, tag number, the double's bytes; index 5/6/8 with count 0.
        lua, g = session(2)        # any client applies slot 1's
        hex_ = '07' + '01' + '03' + double_hex(3.0)
        lua.execute(f"DUE={{[60]='1:73:{hex_};1:73:0800'}};S.apply(60)")
        self.assertEqual(uirep.values(g.stock_calls), ['SelectDialogOption:3', 'SelectBackOption:'])

    def test_slots_2_and_up_are_refused_with_an_info_line_at_most_every_90_ticks(self):
        lua, g = session(2)
        lua.execute("LOCAL=1;S.tick=100;env.StartConversationFromMenu('default',make_id('100'));env.SelectDialogOption(1)")
        lua.execute("S.tick=150;env.ProceedFromMenu('x');S.tick=190;env.ReturnFromMenu()")
        self.assertEqual(uirep.values(g.stock_calls), [])
        self.assertEqual(uirep.values(g.pushed), [])
        logs = uirep.values(g.logs)
        for name in ('StartConversationFromMenu', 'SelectDialogOption', 'ProceedFromMenu', 'ReturnFromMenu'):
            self.assertIn(f'UI_REPLAY_REFUSED what={name} why=slot', logs)
        self.assertEqual(uirep.values(g.notices), ['100:Multiplayer: StartConversationFromMenu works for player 1 only for now - nothing happened.',
                                                   '190:Multiplayer: ReturnFromMenu works for player 1 only for now - nothing happened.'])
        # A section of slot 2 holding one stops (the gate refuses it natively too).
        lua.execute("DUE={[50]='2:73:0500'};S.apply(50)")
        self.assertEqual(g.FAILED, 'ui_lua_op')

    def test_the_probe_the_simulation_and_the_apply_run_the_stock_function_at_once(self):
        lua, g = session(2)
        lua.execute("LOCAL=0;env.SelectDialogOption(1);LOCAL=1;S.replaying=true;env.CancelConversation();S.replaying=false")
        self.assertEqual(uirep.values(g.stock_calls), ['SelectDialogOption:1', 'CancelConversation:'])
        self.assertEqual(uirep.values(g.pushed), [])

    def test_what_does_not_encode_is_refused_and_a_native_refusal_is_named(self):
        lua, g = session(1)
        lua.execute("LOCAL=1;env.StartConversationFromMenu('default',make_id('777'))")
        self.assertEqual(uirep.values(g.pushed), [])
        self.assertIn('UI_REPLAY_REFUSED what=StartConversationFromMenu why=unmapped', uirep.values(g.logs))
        lua.execute("PUSH_RESULT=0;env.CancelConversation()")
        self.assertIn('UI_REPLAY_REFUSED what=CancelConversation why=native', uirep.values(g.logs))

    def test_a_missing_global_refuses_the_arming_and_off_leaves_them_alone(self):
        lua, g = uirep.client()
        lua.execute(MOCK + 'env.SelectBackOption=nil')
        with self.assertRaises(Exception) as caught:
            lua.execute(f'missions_session(1,{MASK})')
        self.assertIn('conversation family without the Lua global SelectBackOption', str(caught.exception))
        lua, g = uirep.client()
        lua.execute(MOCK + "before=env.SelectDialogOption;missions_session(1,256+2048);same=before==env.SelectDialogOption")
        self.assertTrue(g.same)


class MissionTests(unittest.TestCase):
    def test_mission_events_run_natively_on_every_client(self):
        for client in (1, 2):
            lua, g = session(client)
            lua.execute("RUN_WHAT='AbortMission(9001)';DUE={[50]='1:72:01'..string.rep('00',12)};S.apply(50)")
            self.assertEqual(uirep.values(g.mission_runs), ['1:01' + '00'*12])
            self.assertIn('UI_REPLAY_APPLIED tick=50 slot=1 op=72 result=applied what=AbortMission(9001)', uirep.values(g.logs))
            lua.execute("RUN_OK=0;RUN_WHAT='AbortMission:gone';DUE={[51]='1:72:01'};S.apply(51)")
            self.assertIn('UI_REPLAY_APPLIED tick=51 slot=1 op=72 result=refused what=AbortMission:gone', uirep.values(g.logs))
            self.assertEqual(uirep.values(g.results), ['50:1', '51:0'])
        lua.execute("DUE={[52]='2:72:01'};S.apply(52)")
        self.assertEqual(g.FAILED, 'ui_lua_op')

    def test_the_frame_logs_captures_state_and_status_and_the_receipt_carries_the_managers(self):
        lua, g = session(1, extra="{active_mission=function() return ACTIVE or '0' end}")
        lua.execute("MIS_ROWS='118:72:AbortMission:9001,118:73:SelectDialogOption:0';r1=S.frame(119)")
        lua.execute("r2=S.frame(120);MIS_STATE='missions=1 offers=0 conversation=4.1';ACTIVE='9001';r3=S.frame(121)")
        logs = uirep.values(g.logs)
        self.assertIn('UI_MISSIONS_CAPTURED tick=118 what=AbortMission id=9001 op=72 via=native', logs)
        self.assertIn('UI_MISSIONS_CAPTURED tick=118 what=SelectDialogOption id=0 op=73 via=native', logs)
        states = [l for l in logs if l.startswith('UI_MISSIONS_STATE')]
        self.assertEqual(states, ['UI_MISSIONS_STATE tick=119 missions=0 offers=1 conversation=1.0 active=0',
                                  'UI_MISSIONS_STATE tick=121 missions=1 offers=0 conversation=4.1 active=9001'])
        self.assertIn('UI_MISSIONS_STATUS tick=120 ui_missions=armed', logs)
        self.assertTrue(g.r1.endswith('|UIMIS_1:o1/c1.0'))
        lua, g = session(1, mask=256)
        lua.execute('r=S.frame(120)')
        self.assertNotIn('UIMIS', g.r)
        self.assertFalse(any('UI_MISSIONS' in l for l in uirep.values(g.logs)))


SCRIPT_MOCK = r'''
  OFFERS={['9001']={name='X4LAN test offer',actor='500'},['9002']={name='Other',actor='501'}}
  MISSIONS={{id='9003',name='Other mission'},{id='9001',name='X4LAN test mission'}}
  env.GetMissionOfferDetails=function(luaid)
    local o=OFFERS[ids[luaid]];local out={o.name};for i=2,21 do out[i]=false end;out[22]=make_id(o.actor);return unpack(out,1,22) end
  env.GetNumMissions=function() return #MISSIONS end
  env.GetMissionDetails=function(i) return MISSIONS[i].id,MISSIONS[i].name end
  C.GetNumCurrentMissionOffers=function(bbs) return bbs and 0 or 2 end
  C.GetCurrentMissionOffers=function(list,n,bbs) list[0]=9002ULL;list[1]=9001ULL;return 2 end
  C.SetActiveMission=function(id) stock_calls[#stock_calls+1]='SetActiveMission:'..tostring(id);return true end
  C.AbortMission=function(id) stock_calls[#stock_calls+1]='AbortMission:'..tostring(id) end
  C.StartBriefing=function(id,t,s) stock_calls[#stock_calls+1]='StartBriefing:'..tostring(id)..':'..t..':'..tostring(s) end
  C.StopBriefing=function(id,s) stock_calls[#stock_calls+1]='StopBriefing:'..tostring(id)..':'..tostring(s) end
  C.MissionBriefingInteractButtonClicked=function(id) stock_calls[#stock_calls+1]='Button:'..tostring(id) end
  tools={gate=gate,C=C,env=env,own_slot=1,slot_lua=function(s) return make_id(tostring(1000+s)) end,saved_lua=function(s) return make_id(s) end}
  tools.missions=X4LAN_UI_REPLAY_CORE.mission_tools(C,env)
'''


class ScriptTests(unittest.TestCase):
    def test_the_ops_call_the_stock_entries_as_the_menus_do_and_the_fixture_is_canonical(self):
        lua, g = uirep.client()
        lua.execute(MOCK + SCRIPT_MOCK + r'''
          env.ConvertStringTo64Bit=function(d) return tonumber(d)+0ULL end
          plan={{0,'mis_fixture',1},{1,'offer_accept'},{2,'offer_decline'},{3,'briefing','offer'},{4,'briefing_stop','mission'},
            {5,'briefing_button','mission'},{6,'mis_active'},{7,'mis_none'},{8,'mis_abort'},{9,'conv_start','fixture'},
            {10,'conv_start','0000000000000abc','Other'},{11,'dialog',2},{12,'dialog_native',3},{13,'conv_cancel'}}
          marks={}
          S=missions_session(1,256+2048+16384,{script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools),local_mark=function(on) marks[#marks+1]=on end})
          for t=40,53 do S.frame(t) end
        ''')
        calls = uirep.values(g.stock_calls)
        # The fixture: the probe's own call (AddUITriggeredEvent runs at once), no person's mark around it.
        self.assertEqual(calls[0], 'AddUITriggeredEvent:X4LAN_UiMissions/Fixture')
        self.assertEqual(uirep.values(g.marks), [0, 1] * 13)
        # The mock's ui_replay_local is 0 (the frame): the wrapped calls pass to the stock function, as on a mocked client.
        self.assertEqual(calls[1:], ['SignalObject:500/accept/id:9001', 'SignalObject:500/decline/id:9001',
                                     'StartBriefing:9001ULL:x4lan_rt:false', 'StopBriefing:9001ULL:false', 'Button:9001ULL',
                                     'SetActiveMission:9001ULL', 'SetActiveMission:0', 'AbortMission:9001ULL',
                                     'StartConversationFromMenu:X4LAN_Talk,id:500', 'StartConversationFromMenu:Other,id:0000000000000abc',
                                     'SelectDialogOption:2', 'SelectDialogOption:3', 'CancelConversation:'])
        logs = uirep.values(g.logs)
        self.assertFalse([l for l in logs if 'UI_REPLAY_SCRIPT' in l and 'ok=false' in l], logs)
        self.assertTrue(any('UI_REPLAY_SCRIPT tick=41 op=offer_accept ok=true result=9001' in l for l in logs))

    def test_a_missing_fixture_is_named(self):
        lua, g = uirep.client()
        lua.execute(MOCK + SCRIPT_MOCK + r'''
          OFFERS['9001']=nil;C.GetCurrentMissionOffers=function(list,n,bbs) list[0]=9002ULL;return 1 end;C.GetNumCurrentMissionOffers=function(bbs) return bbs and 0 or 1 end
          S=missions_session(1,256+2048,{script=X4LAN_UI_REPLAY_CORE.script({{0,'offer_accept'},{1,'mis_abort'}},40,tools)})
          MISSIONS={}
          for t=40,41 do S.frame(t) end
        ''')
        logs = uirep.values(g.logs)
        self.assertTrue(any('op=offer_accept ok=false' in l and 'no mission offer named X4LAN test offer' in l for l in logs))
        self.assertTrue(any('op=mis_abort ok=false' in l and 'no mission named X4LAN test mission' in l for l in logs))


PROBE_MISSIONS = uirep.PROBE_MOCK + r'''
  gate.ui_guard_pins=function() return 1 end
  gate.ui_guard=function(mask) ui_calls[#ui_calls+1]='guard:'..mask;return 1,282 end
  gate.ui_guard_refusals=function() return nil end
  gate.ui_guard_status=function() return 'ui_guard=live' end
  gate.runtime_ids_probe=function(on) end
  gate.ui_replay_local=function() return 0 end
  gate.ui_replay_saved=function() return nil end
  gate.ui_missions=function() ui_calls[#ui_calls+1]='missions';if MARM then return 0,MARM end;return 1 end
  gate.ui_missions_rows=function() return nil end
  gate.ui_missions_state=function() return 'missions=0 offers=0 conversation=1.0' end
  gate.ui_missions_status=function() return 'ui_missions=armed' end
  gate.ui_missions_frame=function() return 'UIMIS_1:o0/c1.0' end
  gate.ui_missions_cue_ref=function() return nil end
  gate.ui_missions_cue_resolve=function() return nil,'gone' end
  for _,name in ipairs(X4LAN_UI_REPLAY_CORE.CONVERSATION) do _G[name]=function() end end
  SignalObject=function() end;AddUITriggeredEvent=function() end
  require('ffi').C.GetActiveMissionID=function() return 0ULL end
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def client(self, slot=1, prelude=PROBE_MISSIONS, **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['events', 'missions', 'conversation'],
                   native_gate_ui_guard='all')
        cfg.update(extra)
        return self.stock.StockClient(slot, slots_all=True, prelude=prelude, **cfg)

    def test_the_families_arm_after_the_guard_and_their_receipt_joins(self):
        clients = [self.client(1), self.client(2)]
        self.stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertEqual(calls[calls.index(f'arm:{MASK}')+1:calls.index(f'arm:{MASK}')+3], ['guard:16383', 'missions'])
            logs = c.values('logs')
            self.assertTrue(any(f'UI_MISSIONS_ARMED slot={c.slot} families={MASK}' in line for line in logs))
            self.assertTrue(any('UI_MISSIONS_STATE' in line and 'active=0' in line for line in logs))
            self.assertTrue(any('|UIMIS_1:o0/c1.0' in data for data in c.values('hashes')))

    def test_a_refusal_stops_the_arming(self):
        c = self.client(prelude=PROBE_MISSIONS + "MARM='ui_missions_need_guard_group'")
        self.assertTrue(any('ui missions refused: ui_missions_need_guard_group' in line for line in c.errors()), c.values('logs')[-3:])
        c = self.client(prelude=PROBE_MISSIONS + 'gate.ui_missions=nil')
        self.assertTrue(any('ui missions refused: gate without ui_missions' in line for line in c.errors()), c.values('logs')[-3:])

    def test_off_nothing_is_asked(self):
        c = self.client(native_gate_ui_replay=['events'])
        self.stock.run([c], 12)
        self.assertNotIn('missions', c.values('ui_calls'))
        self.assertFalse(any('UI_MISSIONS' in line for line in c.values('logs')))


class OptionTests(unittest.TestCase):
    def test_rules(self):
        base = uirep.stock_config(native_gate_ui_replay="{'events','missions','conversation'}", native_gate_ui_guard="'all'")
        self.assertEqual(uirep.failures(base), [])
        self.assertEqual(uirep.failures(uirep.stock_config(native_gate_ui_replay="{'missions'}", native_gate_ui_guard="{'missions'}")), [])
        self.assertIn('native_gate_ui_replay missions needs native_gate_ui_guard with the missions group',
                      uirep.failures(uirep.stock_config(native_gate_ui_replay="{'missions'}")))
        self.assertIn('native_gate_ui_replay conversation needs native_gate_ui_guard with the conversation group',
                      uirep.failures(uirep.stock_config(native_gate_ui_replay="{'conversation'}", native_gate_ui_guard="{'missions'}")))


class BuilderTests(unittest.TestCase):
    ARGS = ['--players', '2', '--mode', 'coop', '--source', 'stock', '--present']

    def lines(self, *extra):
        args = uirep.players.defaults(uirep.players.build_parser().parse_args(['config', *self.ARGS, *extra]))
        return uirep.players.config_block(args)[0]

    def test_block_and_plan(self):
        lines = self.lines('--ui-replay', 'events,missions,conversation', '--ui-guard', 'all', '--ui-replay-script', '--window', '40', '900')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'events','missions','conversation'}", lines)
        script = next(line for line in lines if 'native_gate_ui_replay_script' in line)
        for op in ("{60,'mis_fixture',1}", "{180,'offer_accept'}", "{540,'dialog_native',2}", "{660,'mis_abort'}", "{150,'offer_accept'}",
                   "{430,'dialog_native',1}"):
            self.assertIn(op, script)
        self.assertEqual(script.count("'mis_fixture'"), 2)        # both clients, the same tick
        with self.assertRaises(ValueError):
            self.lines('--ui-replay', 'missions', '--ui-guard', 'trade')
        with self.assertRaises(ValueError):
            self.lines('--ui-replay', 'travel,missions', '--ui-guard', 'all', '--ui-replay-script', '--window', '40', '900')

    def test_the_overlay_brings_the_md_fixture(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)/'base'
            for sub in ('ui', 'md', 'native'):
                (base/sub).mkdir(parents=True)
            (base/'content.xml').write_text('<content/>', encoding='utf-8')
            source = (ROOT/'tests/test_native_gate_ui_guard.py').read_text(encoding='utf-8')
            self.assertIn("'md/native_gate_ui_replay.xml'", source)   # the guard's overlay test covers the copy; the condition names ours too
            players = (ROOT/'tools/dev/native_gate_players.py').read_text(encoding='utf-8')
            self.assertIn("{'events', 'missions', 'conversation'} & set(args.ui_replay.split(','))", players)


class FixtureTests(unittest.TestCase):
    def test_the_md_fixture_has_its_cues(self):
        import xml.etree.ElementTree as ET
        doc = ET.parse(ROOT/'tools/dev/native_gate_ui_replay.xml')
        names = [c.get('name') for c in doc.iter('cue')]
        for name in ('MissionFixture', 'FixtureAccept', 'FixtureDecline', 'FixtureAborted', 'FixtureBriefingStarted', 'FixtureBriefingCancelled',
                     'FixtureBriefingButton', 'Talk', 'TalkSection', 'TalkFinished', 'Own', 'GuardEvent', 'MinHull'):
            self.assertIn(name, names)

    @unittest.skipUnless(os.environ.get('X4LAN_MD_XSD') == '1', 'the stock md.xsd takes about two minutes to compile: X4LAN_MD_XSD=1')
    def test_the_md_fixture_is_valid_md(self):
        xsd = X4RE/'gamescripts/cats/08/libraries/md.xsd'
        if not xsd.exists():
            self.skipTest('x4-re md.xsd not at hand')
        from lxml import etree
        schema = etree.XMLSchema(etree.parse(str(xsd)))
        doc = etree.parse(str(ROOT/'tools/dev/native_gate_ui_replay.xml'))
        self.assertTrue(schema.validate(doc), schema.error_log)


if __name__ == '__main__':
    unittest.main()
