"""UI replay (lockstep_ui_replay.h, native_gate_ui_replay_core.lua, stop reason 164): stock UI actions as events of the
client's slot, applied on every client at the same tick.

Covers the Lua half under LuaJIT with a mocked gate: the value codec, the wrapped Lua C functions (nothing runs before
the apply, the same stock call on every client), the slot ship rule, the scripted run, the section adapter; the probe's
arming with the real probe and multiplexer; the blocked menu entries; the wire section in all three mirrors; the
builder and the option rules. The native half is tested in C: src/native/engine/test_lockstep_ui_replay.h."""
import contextlib
import importlib.util
import io
from pathlib import Path
import re
import tempfile
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT/'tools/dev'
CORE = (DEV/'native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
MUX = (DEV/'native_gate_inputs_core.lua').read_text(encoding='utf-8')
PROBE = (DEV/'native_gate_probe.lua').read_text(encoding='utf-8')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT/path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ref = load('native_gate_inputs_uirep', 'tools/dev/native_gate_inputs.py')
options = load('check_gate_options_uirep', 'tools/dev/check_gate_options.py')
players = load('native_gate_players_uirep', 'tools/dev/native_gate_players.py')

# A world two clients share: runtime IDs are equal, Lua IDs are userdata in X4 and tables here.
HARNESS = r'''
  ffi=require('ffi')
  logs={};pushed={};stock_calls={};staged={};results={}
  function log(s) logs[#logs+1]=s end
  -- A Lua ID as X4 hands them out: not a number and not cdata. Userdata cannot be made from Lua, a marked table
  -- stands in; the codec is told through ctx.
  LUAIDS=setmetatable({},{__mode='k'})
  function LuaID(decimal) return newproxy and newproxy(true) end
  ids={}
  function make_id(decimal) local u=newproxy(true);ids[u]=decimal;return u end
  env={
    ConvertIDTo64Bit=function(u) return ids[u] and (ids[u]..'ULL') or error('not an id') end,
    ConvertStringToLuaID=function(d) return make_id(d) end,
    ConvertStringTo64Bit=function(d) return tonumber(d)+0ULL end,
    AddUITriggeredEvent=function(screen,control,value) stock_calls[#stock_calls+1]='ui:'..screen..'/'..control end,
    RemoveSofttarget=function() stock_calls[#stock_calls+1]='remove_target' end,
  }
  local function show(v)
    if type(v)=='userdata' then return 'id:'..tostring(ids[v]) end
    if type(v)=='table' then
      local keys={};for k in pairs(v) do keys[#keys+1]=k end
      table.sort(keys,function(a,b) return tostring(a)<tostring(b) end)
      local out={};for _,k in ipairs(keys) do out[#out+1]=tostring(k)..'='..show(v[k]) end
      return '{'..table.concat(out,',')..'}'
    end
    return tostring(v)
  end
  env.CreateOrder=function(...)
    local args,n={...},select('#',...)
    local out={};for i=1,n do out[i]=show(args[i]) end
    stock_calls[#stock_calls+1]='CreateOrder('..table.concat(out,';')..')'
    return 3
  end
  env.SetOrderParam=function(...) stock_calls[#stock_calls+1]='SetOrderParam';return true end
  env.RemoveOrderListParam=function(...) stock_calls[#stock_calls+1]='RemoveOrderListParam';return true end
  KNOWN={['100']='aabbccdd',['200']='11223344',['300']='55667788'}
  SLOT_SHIPS={}
  gate={
    ui_replay_ref=function(d) local c=KNOWN[d];if not c then return nil end;return string.format('%016x',tonumber(d))..c end,
    ui_replay_resolve=function(h)
      local d=string.format('%d',tonumber(h:sub(1,16),16))
      if not KNOWN[d] then return nil,'gone' end
      if KNOWN[d]~=h:sub(17,24) then return nil,'mismatch' end
      return d end,
    ui_replay_push=function(op,hex) pushed[#pushed+1]={op,hex};return PUSH_RESULT or 1 end,
    ui_replay_lua=function(tick) local v=DUE and DUE[tick];return v end,
    ui_replay_bypass=function(on) BYPASS=on end,
    ui_replay_result=function(tick,ok) results[#results+1]=tick..':'..ok end,
    ui_replay_stage=function(slot,hex) staged[#staged+1]=slot..':'..hex;return STAGE_RESULT or 1,STAGE_WHY end,
    ui_replay_take=function(tick,budget) return 'take'..tick..'/'..budget end,
    ui_replay_refusals=function() return REFUSALS end,
    ui_replay_status=function() return 'status' end,
    ui_replay_frame=function() return 'PUIREP_1:x' end,
    ui_replay_fail=function(why) FAILED=why end,
    pilot_stock_inject=function(kind,id,value) stock_calls[#stock_calls+1]='inject:'..kind..'/'..id..'/'..value;return 1 end,
  }
  C={RemoveAllOrders=function(id) stock_calls[#stock_calls+1]='RemoveAllOrders:'..tostring(id) end,
     RemoveAllOrders2=function(id,a,b) stock_calls[#stock_calls+1]='RemoveAllOrders2:'..tostring(id)..':'..tostring(a)..':'..tostring(b) end,
     StartPlayerActivity=function(a) stock_calls[#stock_calls+1]='Start:'..a end,
     StopPlayerActivity=function(a) stock_calls[#stock_calls+1]='Stop:'..a end,
     SetSofttarget=function(id,c) stock_calls[#stock_calls+1]='SetSofttarget:'..tostring(id)..':'..c;return true end}
  function new_session(extra)
    local p={gate=gate,C=C,env=env,log=log,mask=(extra and extra.mask) or 7,slot_ships=function() return SLOT_SHIPS end,script=extra and extra.script,
      fleet=extra and extra.fleet,fleet_tick=extra and extra.fleet_tick,saved_lua=extra and extra.saved_lua,queue_of=extra and extra.queue_of,
      local_mark=extra and extra.local_mark,present=extra and extra.present,min_hull=extra and extra.min_hull,slots=extra and extra.slots,
      slot_lua=extra and extra.slot_lua}
    return X4LAN_UI_REPLAY_CORE.session(p)
  end
'''


def client(**globals_):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(CORE)
    lua.execute(HARNESS)
    g = lua.globals()
    for name, value in globals_.items():
        g[name] = value
    return lua, g


def values(table):
    return list(table.values())


class CodecTests(unittest.TestCase):
    def test_families(self):
        lua, _ = client()
        cases = {'true': 63, "{'flight'}": 512, "{'travel'}": 1, "{'target','orders'}": 6, "{'travel','target','orders'}": 7, "{'autopilot','dock','weapons'}": 56, "{'target','missiles'}": 66, "{'orders','queue'}": 132}
        for text, mask in cases.items():
            self.assertEqual(lua.eval(f'X4LAN_UI_REPLAY_CORE.families({text})'), mask, text)
        for text in ('false', '{}', "{'seta'}", "{'travel','travel'}", "'travel'"):
            mask, why = lua.eval(f'X4LAN_UI_REPLAY_CORE.families({text})')
            self.assertIsNone(mask, text)
            self.assertTrue(why, text)

    def test_a_call_survives_the_wire_with_one_encoding(self):
        lua, g = client()
        lua.execute('''
          ctx={ref=function(u) return gate.ui_replay_ref(ids[u]) end,
               resolve=function(h,kind) local d,why=gate.ui_replay_resolve(h);if not d then return nil,why end;return kind=='luaid' and make_id(d) or d end}
          local a=make_id('100')
          call={a,'MoveWait',{destination={make_id('200'),{1.5,-2.25,1e7}},playerprecise=true,count=3},false,nil,nil,true}
          hex=assert(X4LAN_UI_REPLAY_CORE.encode_call(call,7,ctx))
          back,n=X4LAN_UI_REPLAY_CORE.decode_call(hex,ctx)
          same={back[2],back[3].playerprecise,back[3].count,back[3].destination[2][1],back[3].destination[2][2],back[3].destination[2][3],
            ids[back[1]],ids[back[3].destination[1]],back[4],back[5],back[7],n}
          -- Keys in canonical order: built the other way round, the same bytes.
          local other={count=3,playerprecise=true,destination={make_id('200'),{1.5,-2.25,1e7}}}
          hex2=assert(X4LAN_UI_REPLAY_CORE.encode_call({make_id('100'),'MoveWait',other,false,nil,nil,true},7,ctx))
        ''')
        same = g.same
        self.assertEqual([same[i] for i in range(1, 9)], ['MoveWait', True, 3, 1.5, -2.25, 1e7, '100', '200'])
        self.assertEqual(same[9], False)
        self.assertIsNone(same[10])
        self.assertEqual(same[11], True)
        self.assertEqual(same[12], 7)
        self.assertEqual(g.hex, g.hex2)
        self.assertTrue(re.fullmatch('[0-9a-f]+', g.hex))
        self.assertLessEqual(len(g.hex)/2, 200)

    def test_refusals(self):
        lua, g = client()
        lua.execute('''
          ctx={ref=function(u) return gate.ui_replay_ref(ids[u]) end,
               resolve=function(h,kind) local d,why=gate.ui_replay_resolve(h);if not d then return nil,why end;return make_id(d) end}
          K=X4LAN_UI_REPLAY_CORE
          function enc(v) return select(2,K.encode_call({v},1,ctx)) end
          deep={{{{{}}}}}
          long=string.rep('x',65)
          big={};for i=1,33 do big[i]=i end
          many={};for i=1,30 do many[i]=make_id('100') end
          r={enc(make_id('999')),enc(0/0),enc(math.huge),enc(deep),enc(long),enc(big),enc(function() end),enc(many)}
          d={select(2,K.decode_call('zz',ctx)),select(2,K.decode_call('01ff',ctx)),select(2,K.decode_call('010000',ctx))}
        ''')
        self.assertEqual([g.r[i] for i in range(1, 9)], ['unmapped', 'number', 'number', 'depth', 'string', 'table', 'type', 'too_long'])
        self.assertEqual(g.d[1], 'encoding')
        self.assertEqual(g.d[2], 'tag')
        self.assertEqual(g.d[3], 'trailing')
        self.assertEqual(lua.eval("select(2,K.decode_call('0105'..string.format('%016x',999)..'aabbccdd',ctx))"), 'gone')
        self.assertEqual(lua.eval("select(2,K.decode_call('0105'..string.format('%016x',100)..'00000000',ctx))"), 'mismatch')


class SessionTests(unittest.TestCase):
    def test_nothing_runs_at_capture_and_every_client_makes_the_same_stock_call(self):
        calls = []
        for slot_client in (1, 2):
            lua, g = client()
            lua.execute('''
              S=new_session()
              ret=env.CreateOrder(make_id('100'),'Attack',{primarytarget=make_id('200')},false,true,false,nil,nil,nil,true)
              captured=#stock_calls
            ''')
            self.assertEqual(g.captured, 0)
            self.assertEqual(g.ret, 0)
            self.assertEqual(len(values(g.pushed)), 1)
            self.assertEqual(g.pushed[1][1], 0x43)
            hex_ = g.pushed[1][2]
            # The apply, on every client alike (the event arrives from the wire).
            lua.execute(f"DUE={{[50]='2:67:{hex_}'}};S.apply(50)")
            calls.append(values(g.stock_calls))
            self.assertEqual(values(g.results), ['50:1'])
            self.assertEqual(g.BYPASS, 0)
            self.assertEqual(g.S.replaying, False)
            self.assertTrue(any('UI_REPLAY_APPLIED tick=50 slot=2 op=67 result=applied what=order=Attack' in l for l in values(g.logs)))
        self.assertEqual(calls[0], calls[1])
        self.assertEqual(calls[0], ['CreateOrder(id:100;Attack;{primarytarget=id:200};false;true;false;nil;nil;nil;true)'])

    def test_a_slot_ship_takes_no_orders(self):
        lua, g = client()
        lua.execute("SLOT_SHIPS={['100']=2};S=new_session();env.CreateOrder(make_id('100'),'Attack',{},false)")
        self.assertEqual(values(g.pushed), [])
        self.assertTrue(any('UI_REPLAY_REFUSED what=CreateOrder why=slot_ship' in l for l in values(g.logs)))
        # Also at the apply, the same on every client: it became a slot ship after the capture.
        lua2, g2 = client()
        lua2.execute("S=new_session();env.CreateOrder(make_id('100'),'Attack',{},false)")
        hex_ = g2.pushed[1][2]
        lua2.execute(f"SLOT_SHIPS={{['100']=2}};DUE={{[50]='1:67:{hex_}'}};S.apply(50)")
        self.assertEqual(values(g2.stock_calls), [])
        self.assertEqual(values(g2.results), ['50:0'])

    def test_a_reference_gone_is_equal_everywhere_a_mismatch_stops(self):
        lua, g = client()
        lua.execute("S=new_session();env.CreateOrder(make_id('100'),'Attack',{primarytarget=make_id('200')},false)")
        hex_ = g.pushed[1][2]
        lua.execute(f"KNOWN['200']=nil;DUE={{[50]='1:67:{hex_}'}};S.apply(50)")
        self.assertEqual(values(g.stock_calls), [])
        self.assertEqual(values(g.results), ['50:0'])
        self.assertIsNone(g.FAILED)
        lua.execute(f"KNOWN['200']='deadbeef';DUE={{[51]='1:67:{hex_}'}};S.apply(51)")
        self.assertEqual(g.FAILED, 'ui_ref_mismatch')

    def test_the_stock_function_errors_on_every_client_alike_and_the_session_goes_on(self):
        lua, g = client()
        lua.execute("S=new_session();env.CreateOrder(make_id('100'),'Attack',{},false)")
        hex_ = g.pushed[1][2]
        lua.execute(f"X4LAN_UI_REPLAY_STOCK=nil;S.stock.CreateOrder=function() error('bad order') end;DUE={{[50]='1:67:{hex_}'}};S.apply(50)")
        self.assertEqual(values(g.results), ['50:0'])
        self.assertEqual(g.BYPASS, 0)
        self.assertTrue(any('result=refused' in l and 'bad order' in l for l in values(g.logs)))

    def test_order_clears_run_in_call_order(self):
        lua, g = client()
        hex1 = '%016x' % 100 + 'aabbccdd'
        lua.execute(f"S=new_session();DUE={{[50]='1:65:{hex1};2:66:{hex1}03;2:66:{hex1}01'}};S.apply(50)")
        self.assertEqual(values(g.stock_calls), ['RemoveAllOrders:100ULL', 'RemoveAllOrders2:100ULL:true:true', 'RemoveAllOrders2:100ULL:true:false'])
        self.assertEqual(values(g.results), ['50:1', '50:1', '50:1'])
        lua.execute(f"S=new_session();DUE={{[51]='1:99:{hex1}'}};S.apply(51)")
        self.assertEqual(g.FAILED, 'ui_lua_op')

    def test_queue_editor_parameters_are_refused_and_replays_reach_the_stock_functions(self):
        lua, g = client()
        lua.execute("S=new_session();ok=env.SetOrderParam(1,2,3);ok2=env.RemoveOrderListParam(1)")
        self.assertEqual(values(g.stock_calls), [])
        self.assertTrue(any('what=SetOrderParam why=not_replayed_yet' in l for l in values(g.logs)))
        lua.execute('S.replaying=true;env.SetOrderParam(1,2,3);env.CreateOrder(make_id("100"));S.replaying=false')
        self.assertEqual(len(values(g.stock_calls)), 2)

    def test_the_queue_family_captures_parameters_and_maps_their_index_at_apply(self):
        lua, g = client()
        lua.execute('''
          env.SetOrderParam=function(id,idx,param,list,value)
            stock_calls[#stock_calls+1]='SetOrderParam:'..tostring(ids[id])..':'..idx..':'..param..':'..tostring(list)..':'..tostring(value);return true end
          gate.ui_replay_queue_seen=function() return 2 end
          gate.ui_replay_queue_index=function(slot,ctl,idx,seen) mapped=slot..'/'..ctl..'/'..idx..'/'..seen;return idx==5 and 7 or idx end
          gate.ui_replay_queue_run=function(slot,hex) stock_calls[#stock_calls+1]='queue_run:'..slot..':'..hex;return 1,'EnableOrder=1' end
          S=new_session({mask=4+128})
          env.SetOrderParam(make_id('100'),5,1,nil,true)
          SLOT_SHIPS['200']=2;env.SetOrderParam(make_id('200'),5,1,nil,true);SLOT_SHIPS={}
        ''')
        self.assertEqual(values(g.stock_calls), [])   # nothing runs at capture
        pushed = values(g.pushed)
        self.assertEqual(len(pushed), 1)
        self.assertEqual(pushed[0][1], 0x45)
        self.assertTrue(any('what=SetOrderParam why=slot_ship' in l for l in values(g.logs)))
        lua.execute(f"DUE={{[50]='2:69:{pushed[0][2]};2:68:ab01'}};S.apply(50)")
        self.assertEqual(values(g.stock_calls), ['SetOrderParam:100:7:1:nil:true', 'queue_run:2:ab01'])
        self.assertEqual(g.mapped, '2/100/5/2')
        self.assertEqual(values(g.results), ['50:1', '50:1'])
        self.assertTrue(any('result=applied what=SetOrderParam@7' in l for l in values(g.logs)))
        self.assertTrue(any('result=applied what=EnableOrder=1' in l for l in values(g.logs)))
        self.assertIsNone(g.FAILED)
        self.assertTrue(g.S.unblocked['MapMenu.buttonNewOrder'])
        self.assertIsNone(g.S.unblocked['MapMenu.buttonDefaultOrderConfirm'])

    def test_queue_script_ops_call_the_entries_as_the_map_rows_do(self):
        lua, g = client()
        lua.execute('''
          gate.ui_replay_queue_seen=function() return 0 end
          C.CreateOrder=function(id,def,default) stock_calls[#stock_calls+1]='CreateOrder:'..tostring(id)..':'..def..':'..tostring(default);return 4 end
          C.EnableOrder=function(id,idx) stock_calls[#stock_calls+1]='EnableOrder:'..tostring(id)..':'..idx;return true end
          C.AdjustOrder=function(id,a,b,c,d,e) stock_calls[#stock_calls+1]='AdjustOrder:'..tostring(id)..':'..a..':'..b..':'..tostring(c)..':'..tostring(d)..':'..tostring(e);return true end
          C.RemoveOrder=function(id,a,b,c) stock_calls[#stock_calls+1]='RemoveOrder:'..tostring(id)..':'..a..':'..tostring(b)..':'..tostring(c);return true end
          plan={{0,'q_move','F','S',1,2,3},{1,'q_wait','F'},{2,'q_adjust','F',2,1},{3,'q_remove','F',1},{4,'q_dock','F','ST'}}
          tools={gate=gate,C=C,env=env,own_slot=1,saved_id=function(s) return 7ULL end,saved_lua=function(s) return make_id(s=='F' and '100' or '300') end}
          S=new_session({mask=4+128,script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)})
          for t=40,45 do S.frame(t) end
        ''')
        self.assertEqual(values(g.stock_calls), ['CreateOrder:7ULL:MoveWait:false', 'EnableOrder:7ULL:4', 'CreateOrder:7ULL:Wait:false',
                                                 'EnableOrder:7ULL:4', 'AdjustOrder:7ULL:2:1:true:false:false', 'RemoveOrder:7ULL:1:false:false',
                                                 'CreateOrder:7ULL:DockAndWait:false', 'EnableOrder:7ULL:4'])
        self.assertEqual([p[1] for p in values(g.pushed)], [0x45, 0x45])   # the parameters, through the wrapper
        self.assertTrue(any('UI_REPLAY_SCRIPT tick=40 op=q_move ok=true result=4' in l for l in values(g.logs)))

    def test_a_family_off_leaves_the_globals_alone(self):
        lua, g = client()
        lua.execute('before=env.CreateOrder;S=new_session({mask=3});same=before==env.CreateOrder')
        self.assertTrue(g.same)
        self.assertTrue(g.S.unblocked['InteractMenu.buttonTravelMode'])
        self.assertIsNone(g.S.unblocked['MapMenu.orderAttack'])

    def test_the_rearm_finds_the_stock_function_not_the_wrapper(self):
        lua, g = client()
        lua.execute("S=new_session();first=env.CreateOrder;S2=new_session();S2.replaying=true;env.CreateOrder(make_id('100'))")
        self.assertEqual(len(values(g.stock_calls)), 1)

    def test_section_adapter(self):
        lua, g = client()
        lua.execute("S=new_session();A=S.section();r=A.apply(10,{'','aa','bb'});p=A.proposal(9,472)")
        self.assertEqual(g.r, 'ui=2')
        self.assertEqual(values(g.staged), ['2:aa', '3:bb'])
        self.assertEqual(g.p, 'take9/236')
        lua.execute("STAGE_RESULT=0;STAGE_WHY='ui_late';ok,err=pcall(A.apply,11,{'cc'})")
        self.assertFalse(g.ok)
        self.assertIn('why=ui_late', g.err)

    def test_frame_order_receipt_and_logs(self):
        lua, g = client()
        lua.execute('''
          plan={{0,'travel'},{0,'travel_stop'},{1,'travel_key'},{2,'match',1},{3,'target_slot',2},{4,'target_none'},{5,'clear','s1'},{6,'attack','s1','s2'},
                {7,'move','s1','sector',1,2,3},{8,'dock','s1','station'},{8,'input','state',13,1}}
          tools={gate=gate,C=C,env=env,slot_id=function(s) return s+1000ULL end,saved_id=function(s) return 7ULL end,saved_lua=function(s) return make_id('100') end}
          sc=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)
          marks={}
          S=new_session({script=sc,local_mark=function(on) marks[#marks+1]=on end})
          for t=40,48 do receipt=S.frame(t) end
          REFUSALS='40:x:y';S.frame(600)
        ''')
        self.assertEqual(g.receipt, 'PUIREP_1:x')
        self.assertEqual(values(g.marks)[:4], [0, 1, 0, 1])    # the script's UI half is this client's alone
        calls = values(g.stock_calls)
        self.assertEqual(calls[:6], ['Start:travel', 'Stop:travel', 'inject:action/303/0', 'inject:state/91/1', 'SetSofttarget:1002ULL:', 'remove_target'])
        self.assertEqual(calls[6], 'RemoveAllOrders2:7ULL:false:false')
        self.assertEqual(calls[7], 'inject:state/13/1')
        self.assertEqual(len(values(g.pushed)), 3)  # attack, move, dock: captured by the wrapper, not run
        logs = values(g.logs)
        self.assertTrue(any('UI_REPLAY_SCRIPT tick=40 op=travel ok=true' in l for l in logs))
        self.assertTrue(any('UI_REPLAY_REFUSED_NATIVE tick=600 40:x:y' in l for l in logs))
        self.assertTrue(any('UI_REPLAY_STATUS tick=600 status' in l for l in logs))
        lua.execute('S.frame(601);S.frame(720)')
        logs = values(g.logs)
        self.assertFalse(any('UI_REPLAY_STATUS tick=601' in l for l in logs))
        self.assertTrue(any('UI_REPLAY_STATUS tick=720' in l for l in logs))
        with self.assertRaises(Exception):
            lua.execute("X4LAN_UI_REPLAY_CORE.script({{1,'nope'}},40,tools)")

    def test_flight_script_ops_reach_the_stock_entries(self):
        lua, g = client()
        lua.execute('''
          C.ToggleAutoPilot=function(c) stock_calls[#stock_calls+1]='ToggleAutoPilot:'..tostring(c);return true end
          C.QuickDock=function() stock_calls[#stock_calls+1]='QuickDock';return true end
          C.RequestDockAt=function(id,c) stock_calls[#stock_calls+1]='RequestDockAt:'..tostring(id)..':'..tostring(c);return true end
          C.UndockPlayerShip=function(c) stock_calls[#stock_calls+1]='Undock:'..tostring(c);return 'granted' end
          C.SetDefensibleActiveWeaponGroup=function(id,p,n) stock_calls[#stock_calls+1]='Group:'..tostring(id)..':'..tostring(p)..':'..n end
          env.StartAutoPilot=function(target) stock_calls[#stock_calls+1]='StartAutoPilot:'..tostring(ids[target]);return true end
          env.StopAutoPilot=function() stock_calls[#stock_calls+1]='StopAutoPilot' end
          plan={{0,'ap_to',1},{1,'ap_stop'},{2,'ap_toggle'},{3,'quickdock'},{4,'dock_at','st'},{5,'undock'},{6,'wgroup',1,2},{7,'wkey',140},
                {8,'ap_obj','st'}}
          tools={gate=gate,C=C,env=env,own_slot=2,slot_id=function(s) return s+1000ULL end,slot_lua=function(s) return make_id(tostring(s+1000)) end,
            saved_id=function(s) return 7ULL end,saved_lua=function(s) return make_id('100') end}
          S=new_session({script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)})
          for t=40,49 do S.frame(t) end
        ''')
        self.assertEqual(values(g.stock_calls), ['StartAutoPilot:1001', 'StopAutoPilot', 'ToggleAutoPilot:false', 'QuickDock', 'RequestDockAt:7ULL:false',
                                                 'Undock:false', 'Group:1002ULL:true:2', 'inject:action/140/0', 'StartAutoPilot:100'])
        logs = values(g.logs)
        self.assertTrue(any('UI_REPLAY_SCRIPT tick=45 op=undock ok=true result=granted' in l for l in logs))

    def test_the_missile_op_holds_and_releases_the_recorded_trigger(self):
        lua, g = client()
        lua.execute('''
          gate.ui_replay_missile_hold=function(v) stock_calls[#stock_calls+1]='missile:'..v;return 1 end
          plan={{0,'missile',1},{2,'missile',0}}
          tools={gate=gate,C=C,env=env,own_slot=1}
          S=new_session({script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)})
          for t=40,43 do S.frame(t) end
        ''')
        self.assertEqual(values(g.stock_calls), ['missile:1', 'missile:0'])

    def test_the_target_ops_run_in_the_presentation_scope_as_a_click_does(self):
        """U6b: client 2's SetSofttarget(slot 1's ship) ran in the probe's canonical frame, where that ship is the player's
        own and X4 refuses it (untargetableconditions is_playercontrolled); the target ops now run in the scope."""
        lua, g = client()
        lua.execute('''
          OPEN=false
          present=function(on)
            stock_calls[#stock_calls+1]=on and 'resume' or 'pause'
            if on then OPEN=CAN_OPEN;return OPEN end
            OPEN=false;return true end
          C.SetSofttarget=function(id,c) stock_calls[#stock_calls+1]='SetSofttarget:'..tostring(OPEN);return OPEN end
          env.RemoveSofttarget=function() stock_calls[#stock_calls+1]='remove_target:'..tostring(OPEN) end
          gate.ui_replay_missile_hold=function(v) stock_calls[#stock_calls+1]='missile:'..tostring(OPEN);return 1 end
          plan={{0,'target_slot',1},{1,'missile',1},{2,'target_none'},{3,'target_slot',1}}
          tools={gate=gate,C=C,env=env,own_slot=2,slot_id=function(s) return s+1000ULL end}
          CAN_OPEN=true
          S=new_session({script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools),present=present})
          for t=40,42 do S.frame(t) end
          CAN_OPEN=false   -- slot 1's client, or no presentation: the call runs where it is
          S.frame(43)
          bare=new_session({script=X4LAN_UI_REPLAY_CORE.script({{0,'target_slot',1}},50,tools)})
          bare.frame(50)
        ''')
        self.assertEqual(values(g.stock_calls), ['resume', 'SetSofttarget:true', 'pause', 'missile:false', 'resume', 'remove_target:true', 'pause',
                                                 'resume', 'SetSofttarget:false', 'SetSofttarget:false'])
        rows = [line for line in values(g.logs) if 'UI_REPLAY_SCRIPT' in line]
        self.assertEqual(rows, ['UI_REPLAY_SCRIPT tick=40 op=target_slot ok=true result=true scope=1',
                                'UI_REPLAY_SCRIPT tick=41 op=missile ok=true result=1',
                                'UI_REPLAY_SCRIPT tick=42 op=target_none ok=true result=nil scope=1',
                                'UI_REPLAY_SCRIPT tick=43 op=target_slot ok=true result=false scope=0',
                                'UI_REPLAY_SCRIPT tick=50 op=target_slot ok=true result=false scope=0'])
        self.assertEqual(dict(g.X4LAN_UI_REPLAY_CORE.SCOPED), {'target_slot': True, 'target_none': True, 'guidance_slot': True, 'guidance_end': True})

    def test_the_guidance_ops_set_and_end_it_where_the_maps_click_runs(self):
        """play39p-long: client 2's map set guidance on slot 1's ship (SetGuidance) inside its presentation scope; the ops do
        the same through the stock entries for runs without people (the guidance is the client's own: nothing is captured)."""
        lua, g = client()
        lua.execute('''
          local ffi=require('ffi')
          if not pcall(ffi.typeof,'UIPosRot') then ffi.cdef('typedef struct {float x,y,z,yaw,pitch,roll;} UIPosRot;') end
          OPEN=false
          present=function(on)
            stock_calls[#stock_calls+1]=on and 'resume' or 'pause'
            if on then OPEN=true;return true end
            OPEN=false;return true end
          C.SetGuidance=function(id,offset)
            stock_calls[#stock_calls+1]='SetGuidance:'..tostring(id)..':'..offset.x..','..offset.y..','..offset.z..','..offset.yaw..':'..tostring(OPEN) end
          C.EndGuidance=function() stock_calls[#stock_calls+1]='EndGuidance:'..tostring(OPEN) end
          tools={gate=gate,C=C,env=env,own_slot=2,slot_id=function(s) return s+1000ULL end}
          S=new_session({script=X4LAN_UI_REPLAY_CORE.script({{0,'guidance_slot',1},{2,'guidance_end'}},40,tools),present=present})
          for t=40,43 do S.frame(t) end
        ''')
        self.assertEqual(values(g.stock_calls), ['resume', 'SetGuidance:1001ULL:0,0,0,0:true', 'pause', 'resume', 'EndGuidance:true', 'pause'])
        rows = [line for line in values(g.logs) if 'UI_REPLAY_SCRIPT' in line]
        self.assertEqual(rows, ['UI_REPLAY_SCRIPT tick=40 op=guidance_slot ok=true result=ffi scope=1',
                                'UI_REPLAY_SCRIPT tick=42 op=guidance_end ok=true result=ffi scope=1'])
        self.assertEqual(g.S.captured, 0)

    def test_a_failing_scoped_op_still_pauses_the_scope(self):
        lua, g = client()
        lua.execute('''
          present=function(on) stock_calls[#stock_calls+1]=on and 'resume' or 'pause';return true end
          C.SetSofttarget=function() error('boom') end
          tools={gate=gate,C=C,env=env,own_slot=2,slot_id=function(s) return s+1000ULL end}
          S=new_session({script=X4LAN_UI_REPLAY_CORE.script({{0,'target_slot',1}},40,tools),present=present})
          S.frame(40)
        ''')
        self.assertEqual(values(g.stock_calls), ['resume', 'pause'])
        self.assertTrue(any('op=target_slot ok=false' in line and line.endswith('scope=1') for line in values(g.logs)))

    def test_the_slot_ships_get_their_minimum_hull_once_at_the_fleet_tick(self):
        lua, g = client()
        lua.execute('''
          events={}
          env.AddUITriggeredEvent=function(screen,control,value) events[#events+1]=screen..'/'..control..':'..tostring(ids[value.ship])..'/'..value.hull end
          S=new_session({min_hull=50,slots=2,fleet_tick=10,slot_lua=function(s) return make_id(tostring(1000+s)) end})
          S.frame(9);c9=#events;S.frame(10);S.frame(11);S.frame(12)
          plain=new_session({fleet_tick=10,slots=2,slot_lua=function(s) error('not asked') end})
          plain.frame(10)
        ''')
        self.assertEqual(g.c9, 0)
        self.assertEqual(values(g.events), ['X4LAN_UiReplay/MinHull:1001/50', 'X4LAN_UiReplay/MinHull:1002/50'])
        self.assertEqual([line for line in values(g.logs) if 'MIN_HULL' in line], ['UI_REPLAY_MIN_HULL tick=10 slots=2 hull=50'])

    def test_the_slot_column_is_logged_as_it_changes(self):
        lua, g = client()
        lua.execute('''
          column='0/0,0/0'
          gate.ui_replay_frame=function() return 'PUIREP_1:x|'..column end
          S=new_session()
          S.frame(1);S.frame(2);column='0/0,1/0';S.frame(3);S.frame(4);column='0/0,1/7a8fe';r=S.frame(5)
        ''')
        rows = [line for line in values(g.logs) if 'UI_REPLAY_SLOTS' in line]
        self.assertEqual(rows, ['UI_REPLAY_SLOTS tick=1 0/0,0/0', 'UI_REPLAY_SLOTS tick=3 0/0,1/0', 'UI_REPLAY_SLOTS tick=5 0/0,1/7a8fe'])
        self.assertEqual(g.r, 'PUIREP_1:x|0/0,1/7a8fe')

    def test_the_fleet_is_owned_once_and_its_queue_joins_the_receipt(self):
        lua, g = client()
        lua.execute('''
          S=new_session({fleet={'f1','f2'},fleet_tick=10,saved_lua=function(s) return make_id('100') end,queue_of=function(s) return s=='f1' and '1/0/Wait/ready' or 'dead' end})
          r9=S.frame(9);c9=#stock_calls;r10=S.frame(10);r11=S.frame(11);c11=#stock_calls
        ''')
        self.assertEqual((g.c9, g.c11), (0, 2))
        self.assertEqual(g.r9, 'PUIREP_1:x|UIFLEET_1:f1:1/0/Wait/ready,f2:dead')
        self.assertEqual(g.r9, g.r11)


PROBE_MOCK = CORE + r"""
  ui_calls={};ui_staged={};ui_queue={}
  gate.runtime_ids=function() return 1 end
  gate.runtime_ids_status=function() return 'set' end
  gate.runtime_ids_word=function() return '0' end
  gate.runtime_ids_map_row=function() return 'map' end
  gate.runtime_ids_caller_row=function() return nil end
  gate.ui_replay_pins=function() ui_calls[#ui_calls+1]='pins';return PINS or 1,PINS and 'pins_ui_replay:1' or nil end
  gate.ui_replay=function(mask) ui_calls[#ui_calls+1]='arm:'..mask;return ARM or 1,ARM and 'needs_present' or nil end
  gate.ui_replay_take=function(tick,budget)
    local h=ui_queue[tick]
    if h then ui_queue[tick]=nil;return h end
    return nil end
  gate.ui_replay_stage=function(slot,hex) ui_staged[#ui_staged+1]=state.issued..'/'..slot..':'..hex;return 1 end
  gate.ui_replay_lua=function() return nil end
  gate.ui_replay_refusals=function() return nil end
  gate.ui_replay_status=function() return 'status' end
  gate.ui_replay_frame=function() return 'PUIREP_1:'..state.issued end
  gate.pilot_ship=function(s) return nil end
  CreateOrder=function() return 3 end;SetOrderParam=function() end;RemoveOrderListParam=function() end
  ConvertStringTo64Bit=function(d) return d end;ConvertStringToLuaID=function(d) return d end
"""


class ProbeTests(unittest.TestCase):
    """The real probe and multiplexer on the stock tests' mocked gate: arming, the section, every client alike."""
    @classmethod
    def setUpClass(cls):
        import sys
        sys.path.insert(0, str(ROOT/'tests'))
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def clients(self, **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=True, **extra)
        return [self.stock.StockClient(k, slots_all=True, prelude=PROBE_MOCK, **cfg) for k in (1, 2)]

    def test_arming_the_section_and_the_receipt(self):
        clients = self.clients()
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertEqual(calls[0], 'pins')
            self.assertIn('arm:63', calls)
            logs = c.values('logs')
            self.assertTrue(any('UI_REPLAY_ARMED' in line and 'families=63' in line for line in logs))
            self.assertTrue(any('INPUT_SECTION ARMED section=ui owner=ui' in line for line in logs))
        # Client 2's UI event rides its blob next to its stock record; every client stages it for slot 2.
        section = '01090001' + '01020100'
        clients[1].lua.execute(f"ui_queue[9]='{section}'")
        blobs = self.stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
        decoded = ref.decode(blobs[9][1])
        self.assertEqual(decoded['ui'], section)
        self.assertIn('stock', decoded)
        self.assertEqual(clients[0].values('ui_staged'), clients[1].values('ui_staged'))
        self.assertEqual(clients[0].values('ui_staged'), ['10/2:'+section])
        for c in clients:
            self.assertTrue(any('PUIREP_1:12' in data for data in c.values('hashes')))
        self.assertTrue(all('stock' in ref.decode(blobs[t][0]) and 'ui' not in ref.decode(blobs[t][0]) for t in range(self.stock.WINDOW[0], 12)))

    def test_the_script_is_per_client(self):
        clients = self.clients(native_gate_ui_replay_script=[[[7, 'travel_key']], []])
        self.stock.run(clients, 13)
        for c, count in zip(clients, (1, 0)):
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            self.assertTrue(any(f'script={count}' in line and 'UI_REPLAY_ARMED' in line for line in c.values('logs')))
        self.assertEqual(clients[0].values('injected'), [f'{self.stock.WINDOW[0]+7}/action/303/0'])
        self.assertEqual(clients[1].values('injected'), [])

    def test_the_script_tools_pass_one_value_to_the_stock_entries(self):
        """U6: slot_id handed assert's message to ffi.new as a second initializer, so target_slot and wgroup failed live."""
        prelude = PROBE_MOCK + '''
          gate.pilot_ship=function(s) return 1000+s end
          local ffi=require('ffi')
          ffi.new=function(t,...) assert(select('#',...)<=1,'too many initializers for '..t);return {t=t,v=...} end
          ffi.C.SetSofttarget=function(id,s) ui_calls[#ui_calls+1]='soft:'..id.t..':'..id.v;return true end
        '''
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['target'],
                   native_gate_ui_replay_script=[[[7, 'target_slot', 2]], []])
        clients = [self.stock.StockClient(k, slots_all=True, prelude=prelude, **cfg) for k in (1, 2)]
        self.stock.run(clients, 13)
        rows = [line for line in clients[0].values('logs') if 'UI_REPLAY_SCRIPT' in line]
        self.assertEqual(len(rows), 1)
        self.assertIn('op=target_slot ok=true', rows[0])
        self.assertIn('soft:UniverseID:1002', clients[0].values('ui_calls'))

    def test_client_2_targets_slot_1_in_its_presentation_scope(self):
        """U6b: the probe's frame is paused (canonical); the scripted target op opens the scope around the stock call."""
        prelude = PROBE_MOCK + '''
          gate.pilot_ship=function(s) return 1000+s end
          local ffi=require('ffi')
          ffi.new=function(t,...) return {t=t,v=...} end
          OPEN=true;PAUSED=false
          gate.pilot_present_pause=function() if not OPEN then return 0 end;OPEN=false;PAUSED=true;ui_calls[#ui_calls+1]='pause';return 1 end
          gate.pilot_present_resume=function() if not PAUSED then return 0 end;PAUSED=false;OPEN=true;ui_calls[#ui_calls+1]='resume';return 1 end
          ffi.C.SetSofttarget=function(id,s) ui_calls[#ui_calls+1]='soft:'..id.v..':'..tostring(OPEN);return OPEN end
        '''
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['target'],
                   native_gate_ui_replay_script=[[], [[7, 'target_slot', 1]]])
        clients = [self.stock.StockClient(k, slots_all=True, prelude=prelude, **cfg) for k in (1, 2)]
        self.stock.run(clients, 13)
        self.assertEqual(clients[1].errors(), [], clients[1].values('logs')[-3:])
        calls = clients[1].values('ui_calls')
        at = calls.index('soft:1001:true')
        self.assertEqual(calls[at-2:at+3], ['pause', 'resume', 'soft:1001:true', 'pause', 'resume'])   # frame, click, frame
        rows = [line for line in clients[1].values('logs') if 'UI_REPLAY_SCRIPT' in line]
        self.assertEqual(len(rows), 1)
        self.assertTrue(rows[0].endswith(f'UI_REPLAY_SCRIPT tick={self.stock.WINDOW[0]+7} op=target_slot ok=true result=true scope=1'), rows)

    def test_the_people_families_arm_and_client_2_sets_guidance_in_its_scope(self):
        """overlay-Play40PS: every family but missiles (no pilot fire in the play bases); client 2 sets guidance on slot 1's ship
        and ends it inside its presentation scope, as play39p-long's map did; client 1 sets it on slot 2's ship."""
        prelude = PROBE_MOCK + '''
          gate.pilot_ship=function(s) return 1000+s end
          local ffi=require('ffi')
          ffi.new=function(t,...) return {t=t,v=...} end
          OPEN=true;PAUSED=false
          gate.pilot_present_pause=function() if not OPEN then return 0 end;OPEN=false;PAUSED=true;return 1 end
          gate.pilot_present_resume=function() if not PAUSED then return 0 end;PAUSED=false;OPEN=true;return 1 end
          ffi.C.SetGuidance=function(id,offset) ui_calls[#ui_calls+1]='guidance:'..id.v..':'..offset.t..':'..tostring(OPEN) end
          ffi.C.EndGuidance=function() ui_calls[#ui_calls+1]='end_guidance:'..tostring(OPEN) end
        '''
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True,
                   native_gate_ui_replay=['travel', 'target', 'orders', 'autopilot', 'dock', 'weapons', 'queue'],
                   native_gate_ui_replay_script=[[[7, 'guidance_slot', 2]], [[7, 'guidance_slot', 1], [8, 'guidance_end']]])
        clients = [self.stock.StockClient(k, slots_all=True, prelude=prelude, **cfg) for k in (1, 2)]
        self.stock.run(clients, 13)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            self.assertTrue(any('UI_REPLAY_ARMED' in line and 'families=191' in line for line in c.values('logs')))
        self.assertIn('guidance:1001:UIPosRot:true', clients[1].values('ui_calls'))
        self.assertIn('end_guidance:true', clients[1].values('ui_calls'))
        rows = [line[line.index('UI_REPLAY_SCRIPT'):] for line in clients[1].values('logs') if 'UI_REPLAY_SCRIPT' in line]
        start = self.stock.WINDOW[0]
        self.assertEqual(rows, [f'UI_REPLAY_SCRIPT tick={start+7} op=guidance_slot ok=true result=ffi scope=1',
                                f'UI_REPLAY_SCRIPT tick={start+8} op=guidance_end ok=true result=ffi scope=1'])
        self.assertTrue(any(c.startswith('guidance:1002:UIPosRot:') for c in clients[0].values('ui_calls')))

    def test_the_minimum_hull_reaches_md_for_every_slot_ship(self):
        prelude = PROBE_MOCK + '''
          gate.pilot_ship=function(s) return 1000+s end
          AddUITriggeredEvent=function(screen,control,v) ui_calls[#ui_calls+1]=screen..'/'..control..':'..tostring(v.ship)..'/'..v.hull end
        '''
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['target'], native_gate_ui_replay_min_hull=50)
        clients = [self.stock.StockClient(k, slots_all=True, prelude=prelude, **cfg) for k in (1, 2)]
        self.stock.run(clients, 13)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            self.assertEqual([v for v in c.values('ui_calls') if 'MinHull' in v], ['X4LAN_UiReplay/MinHull:1001/50', 'X4LAN_UiReplay/MinHull:1002/50'])
        bad = [self.stock.StockClient(1, slots_all=True, prelude=prelude, **dict(cfg, native_gate_ui_replay_min_hull=0))]
        self.stock.run(bad, 13)
        self.assertTrue(any('native_gate_ui_replay_min_hull' in e for e in bad[0].errors()), bad[0].errors())

    def test_its_rows_are_written_on_ticks_the_status_stride_holds(self):
        clients = self.clients(native_gate_status_stride=1000, native_gate_ui_replay_script=[[[7, 'travel_key']], []])
        self.stock.run(clients, 13)
        self.assertEqual(clients[0].errors(), [], clients[0].values('logs')[-3:])
        self.assertTrue(any(f'UI_REPLAY_SCRIPT tick={self.stock.WINDOW[0]+7} op=travel_key' in line for line in clients[0].values('logs')))

    def test_off_nothing_changes(self):
        c = self.stock.StockClient(1, slots_all=True, prelude=PROBE_MOCK, native_gate_pilot_present=True)
        self.assertEqual(c.values('ui_calls'), [])
        self.assertFalse(any('UI_REPLAY' in line for line in c.values('logs')))

    def test_refusals(self):
        for extra, prelude, why in ((dict(native_gate_ui_replay=['seta']), '', 'unknown family seta'),
                                    (dict(native_gate_ui_replay=True, native_gate_pilot_present=None), '', 'needs native_gate_pilot_present'),
                                    (dict(native_gate_ui_replay=True, native_gate_runtime_ids=None), '', 'needs native_gate_runtime_ids'),
                                    (dict(native_gate_ui_replay=True), 'PINS=0', 'pins_ui_replay:1'),
                                    (dict(native_gate_ui_replay=True), 'ARM=0', 'needs_present')):
            with self.subTest(why=why):
                cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True)
                cfg.update(extra)
                c = self.stock.StockClient(1, slots_all=True, prelude=PROBE_MOCK+prelude, **cfg)
                self.assertTrue(any(why in line for line in c.errors()), c.values('logs')[-3:])


class BlockedMenuTests(unittest.TestCase):
    def test_probe_block_leaves_what_the_replay_captures_open(self):
        """The real block_orders on a stand-in: the menus' entries and hotkey, with and without the replay."""
        source = PROBE[PROBE.index('-- Sandbox order block (native_gate_pilot_block_orders)'):PROBE.index('-- The pilot section of the input multiplexer')]
        for open_entries, expect in ((None, []), ({'InteractMenu.buttonTravelMode': True, 'MapMenu.orderAttack': True, 'hotkey.INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE': True},
                                                  ['buttonTravelMode', 'map.orderAttack', 'hotkey:INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE'])):
            lua = LuaRuntime(unpack_returned_tuples=True)
            lua.execute('stock={};logs={};function log(s) logs[#logs+1]=s end;pilot_view={}')
            if open_entries:
                lua.execute('pilot_view.ui_replay={unblocked={' + ','.join(f"['{k}']=true" for k in open_entries) + '}}')
            lua.execute(source + '''
              function run_block() block_orders() end
              local function entry(name) return function() stock[#stock+1]=name;return 1 end end
              Menus={{name='InteractMenu',buttonTravelMode=entry('buttonTravelMode'),orderAttack=entry('orderAttack')},
                     {name='MapMenu',orderAttack=entry('map.orderAttack'),buttonNewOrder=entry('buttonNewOrder'),hotkey=function(a) stock[#stock+1]='hotkey:'..a end}}
              run_block()
              Menus[1].buttonTravelMode();Menus[1].orderAttack();Menus[2].orderAttack();Menus[2].buttonNewOrder()
              Menus[2].hotkey('INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE');Menus[2].hotkey('INPUT_ACTION_ADDON_DETAILMONITOR_SETA')
            ''')
            self.assertEqual(values(lua.globals().stock), expect, open_entries)


class WireTests(unittest.TestCase):
    SECTION = '01640001' + '01020100'

    def test_python_reference(self):
        blob = ref.encode({'ui': self.SECTION})
        self.assertEqual(blob, '4d01' + '55' + '08' + self.SECTION)
        self.assertEqual(ref.decode(blob), {'ui': self.SECTION})
        stock = '03' + '01' + '0201' + '00' * 38 + '00'
        both = ref.encode({'stock': stock, 'ui': self.SECTION})
        self.assertEqual(ref.decode(both), {'stock': stock, 'ui': self.SECTION})
        self.assertTrue(both.startswith('4d0153'))     # ascending kinds: stock before ui
        with self.assertRaises(ref.Refused):
            ref.encode({'ui': '02' + self.SECTION[2:]})
        with self.assertRaises(ref.Refused):
            ref.decode('4d01' + '55' + '08' + '02' + self.SECTION[2:])

    def test_lua_mirror(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(MUX)
        K = lua.globals().X4LAN_INPUT_MUX_CORE
        self.assertEqual(K.encode(lua.table(ui=self.SECTION)), ref.encode({'ui': self.SECTION}))
        self.assertEqual(K.decode(ref.encode({'ui': self.SECTION})).ui, self.SECTION)
        result = K.decode('4d01' + '55' + '08' + '02' + self.SECTION[2:])
        self.assertEqual(result, (None, 'ui_version'))
        self.assertEqual(list(K.PRIORITY.values()), list(ref.PRIORITY))
        self.assertEqual(list(K.SECTIONS.values()), list(ref.SECTIONS))
        # The stock record keeps its room: the ui section gets what is left, and waits when nothing is.
        lua.execute('''
          M=X4LAN_INPUT_MUX_CORE.mux(function() end)
          budget=nil
          M.register('stock','stock',{proposal=function() return '03'..string.rep('00',228) end})
          M.register('ui','ui',{proposal=function(tick,b) budget=b;return nil end})
          blob=M.proposal(1)
        ''')
        self.assertEqual(lua.globals().budget, 2*(240-2-2-229-2))

    def test_native_header_knows_the_kind(self):
        text = (ROOT/'src/native/engine/lockstep_hit_claims.h').read_text(encoding='utf-8')
        self.assertIn('#define INPUT_KIND_UI 0x55u', text)
        self.assertIn('out->ui=b+at;out->ui_len=length', text)


def stock_config(**override):
    base = dict(native_gate_world_identities='true', native_gate_agent_rng='true', native_gate_ship_rng='true',
                native_gate_bullet_rng='true', native_gate_nav_rng='true', native_gate_serial_pathing='true', native_gate_render_rng_split='true',
                native_gate_pilot_inputs='true', native_gate_pilot_suppress_local='true', native_gate_pilot_slots="{'a','b'}",
                native_gate_pilot_source="'wire'", native_gate_pilot_stock='true', native_gate_pilot_stock_slots="'all'",
                native_gate_pilot_capture_source="{'stock','stock'}", native_gate_pilot_device='{false,false}',
                native_gate_pilot_present='true', native_gate_runtime_ids='true', native_gate_snapshot_sha='"'+'a'*64+'"')
    base.update(override)
    return '\n'.join(f'X4LAN_CONFIG.{k} = {v}' for k, v in base.items() if v is not None) + '\n'


def failures(text):
    return [f['rule'] for f in options.ui_replay_rules(text, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', text)))]


class OptionTests(unittest.TestCase):
    def test_rules(self):
        self.assertEqual(failures(stock_config()), [])
        for value in ('true', "{'travel'}", "{'target','orders'}", "{'orders','queue'}", "{'travel','flight'}"):
            self.assertEqual(failures(stock_config(native_gate_ui_replay=value)), [], value)
        for rule, override in (
                ('native_gate_ui_replay must be true or a list of travel, target, orders, autopilot, dock, weapons, missiles, queue, events, flight, trade, missions, build, assets, conversation, faction', dict(native_gate_ui_replay='1')),
                ('native_gate_ui_replay must be true or a list of travel, target, orders, autopilot, dock, weapons, missiles, queue, events, flight, trade, missions, build, assets, conversation, faction', dict(native_gate_ui_replay="{'seta'}")),
                ('native_gate_ui_replay must be true or a list of travel, target, orders, autopilot, dock, weapons, missiles, queue, events, flight, trade, missions, build, assets, conversation, faction', dict(native_gate_ui_replay="{'travel','travel'}")),
                ("native_gate_ui_replay needs native_gate_pilot_stock_slots = 'all'", dict(native_gate_ui_replay='true', native_gate_pilot_stock_slots=None)),
                ("native_gate_ui_replay needs native_gate_pilot_stock_slots = 'all'", dict(native_gate_ui_replay='true', native_gate_pilot_stock=None)),
                ('native_gate_ui_replay needs native_gate_pilot_present', dict(native_gate_ui_replay='true', native_gate_pilot_present=None)),
                ('native_gate_ui_replay needs native_gate_runtime_ids in set mode', dict(native_gate_ui_replay='true', native_gate_runtime_ids=None)),
                ('native_gate_ui_replay needs native_gate_runtime_ids in set mode', dict(native_gate_ui_replay='true', native_gate_runtime_ids="'observe'")),
                ('native_gate_ui_replay needs native_gate_world_identities', dict(native_gate_ui_replay='true', native_gate_world_identities=None)),
                ('native_gate_ui_replay_script needs native_gate_ui_replay', dict(native_gate_ui_replay_script='{}')),
                ('native_gate_ui_replay_fleet needs native_gate_ui_replay', dict(native_gate_ui_replay_fleet="{'a'}")),
                ('native_gate_ui_replay_min_hull needs native_gate_ui_replay', dict(native_gate_ui_replay_min_hull='50')),
                ('native_gate_ui_replay_min_hull must be a whole percentage 1..100', dict(native_gate_ui_replay='true', native_gate_ui_replay_min_hull='0')),
                ('native_gate_ui_replay_min_hull must be a whole percentage 1..100', dict(native_gate_ui_replay='true', native_gate_ui_replay_min_hull='50.5')),
                ('native_gate_ui_replay missiles needs the target family', dict(native_gate_ui_replay="{'missiles'}")),
                ('native_gate_ui_replay missiles needs native_gate_pilot_fire', dict(native_gate_ui_replay="{'target','missiles'}"))):
            self.assertIn(rule, failures(stock_config(**override)), override)
        self.assertIn('native_gate_ui_replay', (DEV/'check_gate_options.py').read_text(encoding='utf-8'))
        # Reached from the checker's entry point.
        self.assertTrue(any(f['rule'].startswith('native_gate_ui_replay needs') for f in options.probe_rules(
            stock_config(native_gate_ui_replay='true', native_gate_pilot_present=None), {'native_gate_world_identities'})))


class BuilderTests(unittest.TestCase):
    ARGS = ['--players', '2', '--mode', 'coop', '--source', 'stock', '--present']

    def lines(self, *extra):
        args = players.defaults(players.build_parser().parse_args(['config', *self.ARGS, *extra]))
        return players.config_block(args)[0]

    def test_off_by_default(self):
        self.assertFalse(any('ui_replay' in line for line in self.lines()))

    def test_block(self):
        lines = self.lines('--ui-replay')
        self.assertIn('X4LAN_CONFIG.native_gate_ui_replay = true', lines)
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'travel','target'}", self.lines('--ui-replay', 'travel,target'))
        scripted = self.lines('--ui-replay', 'travel', '--ui-replay-script', '--window', '40', '900')
        script = next(line for line in scripted if 'native_gate_ui_replay_script' in line)
        self.assertIn("{60,'travel'}", script)
        self.assertIn("{30,'travel_key'}", script)
        self.assertIn("{330,'travel_key'}", script)
        with self.assertRaises(ValueError):
            self.lines('--ui-replay', 'travel', '--ui-replay-script', '--window', '40', '700')
        self.assertNotIn('target_slot', script)   # only the armed families
        self.assertNotIn("'input'", script)
        both = next(line for line in self.lines('--ui-replay', 'travel,target', '--ui-replay-script', '--window', '40', '900') if 'native_gate_ui_replay_script' in line)
        self.assertIn("{605,'input','state',13,1}", both)
        fleet = self.lines('--ui-replay', '--ui-replay-script', '--ui-replay-fleet', '0000000000176a13', '--ui-replay-dock', '00000000000abcde',
                           '--window', '40', '900')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay_fleet = {'0000000000176a13'}", fleet)
        script = next(line for line in fleet if 'native_gate_ui_replay_script' in line)
        self.assertIn("'attack','0000000000176a13'", script)
        self.assertIn("'dock','0000000000176a13','00000000000abcde'", script)

    def test_flight_block(self):
        lines = self.lines('--ui-replay', 'autopilot,dock,weapons', '--ui-replay-script', '--window', '40', '900', '--ui-replay-dock', '00000000000abcde')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'autopilot','dock','weapons'}", lines)
        script = next(line for line in lines if 'native_gate_ui_replay_script' in line)
        for row in ("{60,'ap_to',2}", "{180,'ap_toggle'}", "{200,'wkey',140}", "{260,'wgroup',1,1}", "{30,'ap_to',1}", "{150,'ap_stop'}",
                    "{320,'quickdock'}", "{350,'undock'}", "{380,'dock_at','00000000000abcde'}"):
            self.assertIn(row, script)
        self.assertNotIn('travel', script)
        only = next(line for line in self.lines('--ui-replay', 'autopilot', '--ui-replay-script', '--window', '40', '900') if 'ui_replay_script' in line)
        self.assertIn("'ap_to'", only)
        self.assertNotIn("'wkey'", only)
        self.assertNotIn("'quickdock'", only)
        with self.assertRaises(ValueError):
            self.lines('--ui-replay', 'travel,autopilot', '--ui-replay-script', '--window', '40', '900')
        text = chr(10).join(lines).replace('@X4LANE_CLIENTS@', '2') + chr(10) + 'X4LAN_CONFIG.native_gate_runtime_ids = true' + chr(10) + 'X4LAN_CONFIG.native_gate_world_identities = true' + chr(10)
        self.assertEqual(failures(text), [])

    def test_missile_block(self):
        lines = self.lines('--ui-replay', 'target,missiles', '--fire', 'record', '--ui-replay-script', '--window', '40', '900')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'target','missiles'}", lines)
        script = next(line for line in lines if 'native_gate_ui_replay_script' in line)
        for row in ("{60,'target_slot',2}", "{120,'missile',1}", "{420,'missile',0}", "{90,'target_slot',1}", "{150,'missile',1}",
                    "{300,'target_none'}", "{450,'missile',0}"):
            self.assertIn(row, script)
        self.assertNotIn("'match'", script)
        for extra in (['--ui-replay', 'missiles', '--fire', 'record'], ['--ui-replay', 'target,missiles'],
                      ['--ui-replay', 'travel,target,missiles', '--fire', 'record', '--ui-replay-script', '--window', '40', '900'],
                      ['--ui-replay', 'target,missiles,weapons', '--fire', 'record', '--ui-replay-script', '--window', '40', '900']):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.lines(*extra)
        text = chr(10).join(lines).replace('@X4LANE_CLIENTS@', '2') + chr(10) + 'X4LAN_CONFIG.native_gate_runtime_ids = true' + chr(10) + 'X4LAN_CONFIG.native_gate_world_identities = true' + chr(10)
        self.assertFalse([f for f in failures(text) if 'ui_replay' in f['rule']])

    def test_missile_pair_and_minimum_hull(self):
        """U6c: slot 2 a fixture fighter with a torpedo launcher; MD keeps the slot ships (slot 1's is the player's) alive."""
        base = ['--ui-replay', 'target,missiles', '--fire', 'record', '--ui-replay-script', '--window', '40', '900']
        args = players.defaults(players.build_parser().parse_args(['config', *self.ARGS, *base]))
        players.config_block(args)
        self.assertTrue(any('known launcher ships' in w for w in args.sector_warnings))   # the scouts of U6b
        args = players.defaults(players.build_parser().parse_args(
            ['config', '--players', '2', '--mode', 'factions', '--relations', 'hostile', '--source', 'stock', '--present', *base,
             '--ships', '00000000000ae79c,0000000000176ad5', '--start', '2=0000000000000729@0,100000,2000/180', '--ui-replay-min-hull', '50']))
        lines = players.config_block(args)[0]
        self.assertFalse(any('launcher' in w for w in args.sector_warnings))
        self.assertIn('X4LAN_CONFIG.native_gate_ui_replay_min_hull = 50', lines)
        self.assertIn('0000000000176ad5', players.MISSILE_SHIPS)
        text = chr(10).join(lines).replace('@X4LANE_CLIENTS@', '2') + chr(10) + 'X4LAN_CONFIG.native_gate_runtime_ids = true' + chr(10) + 'X4LAN_CONFIG.native_gate_world_identities = true' + chr(10)
        self.assertFalse([f for f in failures(text) if 'ui_replay' in f['rule']])
        for extra in (['--ui-replay-min-hull', '50'], ['--ui-replay', 'target', '--ui-replay-min-hull', '0'], ['--ui-replay', 'target', '--ui-replay-min-hull', '101']):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.lines(*extra)
        md = (DEV/'native_gate_ui_replay.xml').read_text(encoding='utf-8')
        self.assertIn('<set_object_min_hull object="$ship" exact="event.param3.$hull"/>', md)
        self.assertIn("control=\"'MinHull'\"", md)

    def test_queue_block(self):
        lines = self.lines('--ui-replay', 'orders,queue', '--ui-replay-fleet', '0000000000176a13', '--ui-replay-script', '--window', '40', '900')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'orders','queue'}", lines)
        script = next(line for line in lines if 'native_gate_ui_replay_script' in line)
        for row in ("{500,'clear','0000000000176a13'}", "{501,'q_move','0000000000176a13','0000000000000729',3000,100000,3000}",
                    "{502,'q_wait','0000000000176a13'}", "{600,'q_adjust','0000000000176a13',2,1}", "{700,'q_remove','0000000000176a13',1}",
                    "{550,'q_wait','0000000000176a13'}", "{650,'q_remove','0000000000176a13',3}"):
            self.assertIn(row, script)
        self.assertNotIn("'attack'", script)
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'queue'}", self.lines('--ui-replay', 'queue', '--ui-replay-fleet', '0000000000176a13'))
        for extra in (['--ui-replay', 'queue', '--ui-replay-script', '--window', '40', '900'],
                      ['--ui-replay', 'queue', '--ui-replay-fleet', '0000000000176a13', '--ui-replay-script', '--window', '40', '900']):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.lines(*extra)
        text = chr(10).join(lines).replace('@X4LANE_CLIENTS@', '2') + chr(10) + 'X4LAN_CONFIG.native_gate_runtime_ids = true' + chr(10) + 'X4LAN_CONFIG.native_gate_world_identities = true' + chr(10)
        self.assertEqual(failures(text), [])

    def test_refusals(self):
        for extra in (['--ui-replay', 'seta'], ['--ui-replay-script'], ['--ui-replay-fleet', '0000000000176a13'],
                      ['--ui-replay', 'travel', '--ui-replay-dock', '00000000000abcde'], ['--ui-replay', 'travel', '--ui-replay-fleet', '0000000000176a13']):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.lines(*extra)
        args = players.defaults(players.build_parser().parse_args(['config', '--players', '2', '--mode', 'factions', '--source', 'stock', '--present',
                                                                  '--ui-replay', '--ui-replay-fleet', '0000000000176a13']))
        with self.assertRaises(ValueError):
            players.config_block(args)
        args = players.defaults(players.build_parser().parse_args(['config', '--players', '2', '--source', 'stock', '--ui-replay']))
        with self.assertRaises(ValueError):
            players.config_block(args)

    BASE = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_snapshot_sha = "%s"
X4LAN_CONFIG.native_gate_snapshot_fixture = {'0000000000176a13'}
X4LAN_CONFIG.native_gate_serial_pathing = true
X4LAN_CONFIG.native_gate_world_identities = true
X4LAN_CONFIG.native_gate_bullet_rng = true
X4LAN_CONFIG.native_gate_nav_rng = true
X4LAN_CONFIG.native_gate_agent_rng = true
X4LAN_CONFIG.native_gate_ship_rng = true
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
''' % ('7' * 64)

    def overlay(self, root, name, base_extra, *extra):
        base = root/('base'+name)
        for d in ('ui', 'native', 'md'):
            (base/d).mkdir(parents=True, exist_ok=True)
        (base/'ui/config.lua.append').write_text(self.BASE + base_extra)
        (base/'ui/native_gate_probe.lua').write_text('-- probe\n')
        (base/'native/x4lan_gate_64.dll').write_bytes(b'dll')
        (base/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
        out = root/('out'+name)
        with contextlib.redirect_stdout(io.StringIO()):
            players.main(['overlay', '--base', str(base), '--out', str(out), '--probe', str(DEV/'native_gate_probe.lua'), *self.ARGS, *extra])
        return out

    def test_overlay(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            out = self.overlay(root, 'a', '', '--ui-replay', '--ui-replay-fleet', '0000000000176a13')
            files = (out/'ui.xml.files').read_text().split()
            self.assertLess(files.index('ui/native_gate_ui_replay_core.lua'), files.index('ui/native_gate_probe.lua'))
            self.assertTrue((out/'ui/native_gate_ui_replay_core.lua').is_file())
            self.assertTrue((out/'md/native_gate_ui_replay.xml').is_file())
            text = (out/'ui/config.lua.append').read_text()
            self.assertIn('X4LAN_CONFIG.native_gate_runtime_ids = true', text)   # the base had none
            # A base's own value stays; without the option nothing of it arrives.
            out = self.overlay(root, 'b', 'X4LAN_CONFIG.native_gate_runtime_ids = 70000\n', '--ui-replay', 'travel')
            text = (out/'ui/config.lua.append').read_text()
            self.assertIn('native_gate_runtime_ids = 70000', text)
            self.assertNotIn('native_gate_runtime_ids = true', text)
            self.assertFalse((out/'md/native_gate_ui_replay.xml').exists())
            out = self.overlay(root, 'd', '', '--ui-replay', 'target', '--ui-replay-min-hull', '50')
            self.assertTrue((out/'md/native_gate_ui_replay.xml').is_file())   # the MinHull cue
            self.assertIn('native_gate_ui_replay_min_hull = 50', (out/'ui/config.lua.append').read_text())
            out = self.overlay(root, 'c', '')
            self.assertNotIn('ui_replay', (out/'ui/config.lua.append').read_text())
            self.assertNotIn('ui/native_gate_ui_replay_core.lua', (out/'ui.xml.files').read_text())
            self.assertFalse((out/'ui/native_gate_ui_replay_core.lua').exists())

    def test_guidance_local(self):
        self.assertFalse(any('guidance_local' in line for line in self.lines()))
        self.assertIn('X4LAN_CONFIG.native_gate_guidance_local = true', self.lines('--ui-replay', '--guidance-local'))
        with self.assertRaises(ValueError):
            players.config_block(players.defaults(players.build_parser().parse_args(
                ['config', '--players', '2', '--mode', 'coop', '--source', 'stock', '--guidance-local'])))
        frames = ('X4LAN_CONFIG.native_gate_runtime_ids_local = true\nX4LAN_CONFIG.native_gate_script_rng = "key"\n'
                  'X4LAN_CONFIG.native_gate_md_timed_rng = true\n')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            out = self.overlay(root, 'g', frames, '--ui-replay', '--guidance-local')
            self.assertIn('X4LAN_CONFIG.native_gate_guidance_local = true', (out/'ui/config.lua.append').read_text())
            for base in ('', frames.replace('X4LAN_CONFIG.native_gate_md_timed_rng = true\n', ''),
                         frames.replace('"key"', 'false')):
                error = io.StringIO()
                with self.assertRaises(SystemExit), contextlib.redirect_stderr(error):
                    self.overlay(root, 'h%d' % len(base), base, '--ui-replay', '--guidance-local')
                self.assertIn('--guidance-local needs a base overlay with', error.getvalue())

    def test_the_block_passes_the_checker(self):
        text = '\n'.join(self.lines('--ui-replay')) + '\nX4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_world_identities = true\n'
        text = text.replace('@X4LANE_CLIENTS@', '2')
        self.assertEqual(failures(text), [])


if __name__ == '__main__':
    unittest.main()
