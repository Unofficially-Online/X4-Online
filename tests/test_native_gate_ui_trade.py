"""UI replay, trade family (lockstep_ui_trade.h, X4LAN_UI_TRADE in native_gate_ui_replay_core.lua; default off, stop 164).

Covers the Lua half under LuaJIT with a mocked gate and world, in both session modes (coop: every slot `player`, one wallet;
factions: slot 2 is x4lan_player2 with its own account): the wrapped trade globals capture and nothing runs, every client
makes the same stock call at the apply in the slot's faction scope, the portable trade offer reference (resolved again at
the apply, refused when it changed), the authority rule (the slot's faction, no other slot's ship, the own ship only through
the trade computer), the docked trade's signal for every slot next to the events family, the FFI events' path, the test
fund, the receipt and money rows, the trade view of factions mode, the scripted ops; the mirrors (family bit, ops, the
globals, the hooked exports against the inventory and the generated table, the specs against the stock prototypes); the
real probe arming the family on the mocked gate; the option checker and the builder. The native half is tested in C:
src/native/engine/test_lockstep_ui_trade.h."""
import os
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

ENGINE = ROOT/'src/native/engine'
TRADE_H = (ENGINE/'lockstep_ui_trade.h').read_text(encoding='utf-8')
REPLAY_H = (ENGINE/'lockstep_ui_replay.h').read_text(encoding='utf-8')
INPUTS_H = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
TABLE = (ENGINE/'lockstep_ui_guard_table.h').read_text(encoding='utf-8')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))

# The world both clients share: slot 1's ship 100 (`player`), slot 2's ship 200, station 300 (slot 2's faction), station 400
# (`player`), a player ship 500 that is no slot ship. OWNERS['200'] and OWNERS['300'] say the mode: 'x4lan_player2' factions,
# 'player' coop. Station 300 sells energycells (offer 9001) and buys ore (9002); the gate's faction scope sets SCOPE.
WORLD = r'''
  KNOWN['400']='99887766';KNOWN['500']='12121212';KNOWN['4242']='42424242'
  SLOT_SHIPS={['100']=1,['200']=2}
  OWNERS={['100']='player',['200']='x4lan_player2',['300']='x4lan_player2',['400']='player',['500']='player'}
  SCOPE='player';scopes={};runs={};MONEY={[1]=500000,[2]=1234500};LOOP={}
  local function dec(id)
    if type(id)=='cdata' then return (tostring(id):gsub('ULL$','')) end
    if type(id)=='number' then return string.format('%.0f',id) end
    return ids[id]
  end
  DEC=dec
  env.GetComponentData=function(id,key)
    local d=dec(id)
    if key=='owner' then return OWNERS[d] end
    if key=='money' then return d=='200' and 10 or 0 end
  end
  OFFERS={['300']={{id='9001',ware='energycells',isselloffer=true},{id='9002',ware='ore',isbuyoffer=true}}}
  local function offers(station) local out={};for i,o in ipairs(OFFERS[dec(station)] or {}) do out[i]={id=make_id(o.id),ware=o.ware,isbuyoffer=o.isbuyoffer,isselloffer=o.isselloffer,ismissionoffer=o.ismissionoffer} end;return out end
  env.GetTradeList=function(station) return offers(station) end
  env.GetTradeData=function(offer)
    for station,list in pairs(OFFERS) do for _,o in ipairs(list) do if o.id==ids[offer] then return {station=make_id(station),ware=o.ware} end end end
    return {}
  end
  local function record(name) return function(...)
    local args,n={...},select('#',...);local out={}
    for i=1,n do out[i]=(type(args[i])=='userdata' or type(args[i])=='cdata') and 'id:'..tostring(dec(args[i])) or tostring(args[i]) end
    stock_calls[#stock_calls+1]=name..'('..table.concat(out,';')..')@'..SCOPE
  end end
  for _,name in ipairs({'AddTradeToShipQueue','TransferPlayerMoneyTo','TransferMoneyToPlayer','SetContainerWarePriceOverride','ClearContainerWarePriceOverride',
    'SetContainerStockLimitOverride','ClearContainerStockLimitOverride','SetMinBudget','SetMaxBudget','SignalObject','SetVirtualCargoMode'}) do env[name]=record(name) end
  env.GetPlayerMoney=function() return 777 end
  env.GetTradeShipList=function() return {{shipid=make_id('500')}} end
  gate.ui_replay_local=function() return LOCAL or 0 end
  gate.ui_trade_scope=function(slot)
    local f=OWNERS[slot==1 and '100' or '200']
    if not f then return -1 end
    if f=='player' then return 0 end
    SCOPE=f;scopes[#scopes+1]=slot;return 1
  end
  gate.ui_trade_scope_end=function() SCOPE='player';return SCOPE_END or 1 end
  gate.ui_trade_subject=function(hex) return gate.ui_replay_resolve(hex:sub(3,26)) end
  gate.ui_trade_run=function(slot,hex) runs[#runs+1]=slot..':'..hex;return 1,'SetContainerTradeRule' end
  gate.ui_trade_money=function(slot) return MONEY[slot] end
  gate.ui_trade_frame=function() return 'UITRADE_1:'..MONEY[1]..','..MONEY[2] end
  gate.ui_trade_status=function() return 'ui_trade=live' end
  C.GetPlayerID=function() return 4242ULL end
  C.AddPlayerMoney=function(cents) stock_calls[#stock_calls+1]='AddPlayerMoney('..tostring(cents)..')@'..SCOPE end
  C.HasShipTradeLoopCargoReservationOverride=function(id) return LOOP[dec(id)] or false end
  C.SetShipTradeLoopCargoReservationOverride=function(id,v) stock_calls[#stock_calls+1]='SetLoop:'..dec(id)..':'..tostring(v) end
  C.RemoveShipTradeLoopCargoReservationOverride=function(id) stock_calls[#stock_calls+1]='RemoveLoop:'..dec(id) end
  X4LAN_UI_REPLAY_STOCK=nil
  function trade_session(slot,extra)
    extra=extra or {}
    return X4LAN_UI_REPLAY_CORE.session({gate=gate,C=C,env=env,log=log,mask=extra.mask or 1024,slot_ships=function() return SLOT_SHIPS end,
      own_slot=slot,slots=2,fleet_tick=extra.fleet_tick,trade_fund=extra.fund,script=extra.script})
  end
'''


def client(coop=False):
    lua, g = uirep.client()
    lua.execute(WORLD)
    if coop:
        lua.execute("OWNERS['200']='player';OWNERS['300']='player'")
    return lua, g


def values(table):
    return list(table.values()) if table is not None else []


def captured(lua, g, slot, code, mask=1024):
    """Run `code` on client `slot` in its UI (a local phase); the events it pushed."""
    lua.execute(f'S=trade_session({slot},{{mask={mask}}});LOCAL=1;{code};LOCAL=0')
    return values(g.pushed)


class MirrorTests(unittest.TestCase):
    def test_the_family_and_its_ops_agree_everywhere(self):
        lua, _ = uirep.client()
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.FAMILY.trade'), 1024)
        self.assertIn('UIREP_FAMILY_TRADE=1024', INPUTS_H)
        families = sum(int(v) for v in re.search(r'UIREP_FAMILIES=([0-9|]+)', INPUTS_H).group(1).split('|'))
        self.assertEqual(families & 1024, 1024)   # implemented (bits: docs/LOCKSTEP-REGISTRY.txt)
        self.assertEqual(inv.TRADE, 1024)
        self.assertIn('trade', uirep.options.UI_REPLAY_FAMILIES)
        self.assertIn('trade', uirep.players.UI_REPLAY_FAMILIES)
        self.assertEqual(lua.eval('X4LAN_UI_TRADE.OP_LUA'), int(re.search(r'#define UITRADE_OP_LUA (0x[0-9a-f]+)u', REPLAY_H).group(1), 16))
        self.assertEqual(lua.eval('X4LAN_UI_TRADE.OP_FFI'), int(re.search(r'#define UITRADE_OP_FFI (0x[0-9a-f]+)u', REPLAY_H).group(1), 16))
        globals_ = list(lua.eval('X4LAN_UI_TRADE.GLOBALS').values())
        self.assertEqual(len(globals_), int(re.search(r'#define UITRADE_LUA_GLOBALS (\d+)u', TRADE_H).group(1)))
        # true (all) keeps the first runs' families: trade is named, as events and flight are.
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.families(true)'), 63)

    def test_what_the_family_replays_is_left_to_it_by_the_guard(self):
        native_lua = re.findall(r'"(\w+)"', re.search(r'uitrade_lua_names\[\]=\{(.*?)\};', TRADE_H, re.S).group(1))
        native_ffi = re.findall(r'\{"(\w+)","(\w+)"\}', re.search(r'uitrade_defs\[UITRADE_HOOKS\]=\{(.*?)\};', TRADE_H, re.S).group(1))
        lua, _ = uirep.client()
        wrapped = [n for n in lua.eval('X4LAN_UI_TRADE.GLOBALS').values() if n != 'SignalObject']
        self.assertEqual(native_lua, wrapped)
        covered = {(kind, name) for (kind, name), (_, group, cov) in inv.INVENTORY.items() if cov & inv.TRADE}
        self.assertEqual(covered, {('lua', n) for n in native_lua} | {('ffi', n) for n, _ in native_ffi})
        for (kind, name), (_, group, cov) in inv.INVENTORY.items():
            if cov & inv.TRADE:
                self.assertEqual((group, cov), ('trade', inv.TRADE), name)
        rows = {m[0]: int(m[1]) for m in re.findall(r'\{"(\w+)",0x[0-9a-f]+u,UIGUARD_\w+,UIGUARD_G_\w+,(\d+)u,', TABLE)}
        self.assertEqual({n for n, c in rows.items() if c & 1024}, {n for _, n in covered})
        # What stays refused (the report's list) is still in the guard, uncovered.
        for name in ('ClearTradeQueue', 'TransferPlayerInventoryTo', 'TransferInventoryToPlayer', 'CreateTradeRule',
                     'UpdateTradeRule', 'RemoveTradeRule', 'SetPlayerTradeRuleDefault', 'UpdateSupplyOverrides', 'PauseProcessingModule',
                     'PauseProductionModule', 'SetPlayerTradeLoopCargoReservationSetting', 'SetBuildTaskTransferredMoney', 'AddPlayerMoney',
                     'ToggleFactionTradeRestriction', 'MakeRepairPriority'):
            self.assertEqual(rows[name], 0, name)

    def test_the_specs_follow_the_stock_prototypes(self):
        protos = X4RE/'data/ffi_prototypes.csv'
        if not protos.is_file():
            self.skipTest('x4-re with the FFI prototypes not at hand (X4LAN_X4RE)')
        text = protos.read_text(encoding='utf-8')
        letter = {'UniverseID': 'I', 'TradeRuleID': 'u', 'bool': 'b', 'int32_t': 'i', 'float': 'f'}
        for name, spec in re.findall(r'\{"(\w+)","(\w+)"\}', re.search(r'uitrade_defs\[UITRADE_HOOKS\]=\{(.*?)\};', TRADE_H, re.S).group(1)):
            proto = re.search(rf'void {name}\((.*?)\);', text).group(1)
            args = [a.strip().rsplit(' ', 1)[0] for a in proto.split(',')]
            got = ''.join('s' if a == 'const char*' else letter[a] for a in args)
            self.assertEqual(got, spec.replace('z', 's'), name)   # z: a string the menus may pass as NULL (every ware)


class CaptureTests(unittest.TestCase):
    def test_a_trade_is_captured_and_nothing_runs_here(self):
        lua, g = client()
        pushed = captured(lua, g, 2, "env.AddTradeToShipQueue(make_id('9001'),env.ConvertStringTo64Bit('200'),10,true)")
        self.assertEqual(values(g.stock_calls), [])
        self.assertEqual(len(pushed), 1)
        self.assertEqual(pushed[0][1], 0x58)
        self.assertTrue(pushed[0][2].startswith('01'))   # AddTradeToShipQueue, the wire's first global
        lua.execute(f"args,n=X4LAN_UI_REPLAY_CORE.decode_call(('{pushed[0][2]}'):sub(3),S.ctx)")
        self.assertEqual(lua.eval('n'), 4)
        self.assertEqual(lua.eval('args[1].w'), 'energycells')
        self.assertEqual(lua.eval('DEC(args[1].s)'), '300')
        # buy offer kind 2 (we buy from a sell offer), the station's own list, first of one
        self.assertEqual(lua.eval('args[1].c'), 2 + 16*1 + 4096*1)
        self.assertEqual(lua.eval('args[3]'), 10)
        self.assertEqual(lua.eval('args[4]'), True)
        self.assertEqual(g.S.captured, 1)

    def test_the_simulation_the_probe_and_an_apply_reach_the_stock_function(self):
        lua, g = client()
        lua.execute("S=trade_session(2);env.TransferPlayerMoneyTo(5,env.ConvertStringTo64Bit('300'))")
        self.assertEqual(values(g.stock_calls), ['TransferPlayerMoneyTo(5;id:300)@player'])
        self.assertEqual(values(g.pushed), [])

    def test_an_offer_that_is_in_no_list_is_refused_where_it_was_made(self):
        lua, g = client()
        pushed = captured(lua, g, 2, "env.AddTradeToShipQueue(make_id('9999'),env.ConvertStringTo64Bit('200'),10,true)")
        self.assertEqual(pushed, [])
        self.assertTrue(any('UI_REPLAY_REFUSED what=AddTradeToShipQueue why=offer' in line for line in values(g.logs)))

    def test_every_global_and_the_off_switch(self):
        lua, g = client()
        lua.execute('before=env.AddTradeToShipQueue;S=trade_session(1,{mask=7});same=before==env.AddTradeToShipQueue')
        self.assertTrue(g.same)
        lua, g = client()
        pushed = captured(lua, g, 2, '''
          local st=env.ConvertStringTo64Bit('300')
          env.TransferPlayerMoneyTo(100,st);env.TransferMoneyToPlayer(50,st);env.SetContainerWarePriceOverride(st,'ore',true,12.5)
          env.ClearContainerWarePriceOverride(st,'ore',true);env.SetContainerStockLimitOverride(st,'ore',100);env.ClearContainerStockLimitOverride(st,'ore')
          env.SetMinBudget(st,1000);env.SetMaxBudget(st,1500);env.SetVirtualCargoMode(make_id('200'),true,-1)''')
        self.assertEqual([p[2][:2] for p in pushed], ['02', '03', '04', '05', '06', '07', '08', '09', '0b'])
        self.assertEqual(values(g.stock_calls), [])


class ApplyTests(unittest.TestCase):
    def capture_and_apply(self, code, slot=2, coop=False, at_apply=''):
        """Client `slot` captures; both clients apply the event at tick 50: their stock calls and results."""
        lua, g = client(coop)
        pushed = captured(lua, g, slot, code)
        out = []
        for _ in (1, 2):
            other, h = client(coop)
            other.execute(f'S=trade_session({3-slot});{at_apply}')
            due = ';'.join(f'{slot}:{p[1]}:{p[2]}' for p in pushed)
            other.execute(f"DUE={{[50]='{due}'}};S.apply(50)")
            out.append((values(h.stock_calls), values(h.results), [line for line in values(h.logs) if 'UI_REPLAY_APPLIED' in line], h))
        self.assertEqual(out[0][:3], out[1][:3])
        return out[0]

    def test_factions_slot_2_trades_with_its_own_faction_paying(self):
        calls, results, logs, h = self.capture_and_apply(
            "env.AddTradeToShipQueue(make_id('9001'),env.ConvertStringTo64Bit('200'),10,true);"
            "env.SignalObject(C.GetPlayerID(),'docked_player_trade_added',make_id('200'))")
        self.assertEqual(calls, ['AddTradeToShipQueue(id:9001;id:200;10;true)@x4lan_player2', 'SignalObject(id:4242;docked_player_trade_added;id:200)@player'])
        self.assertEqual(results, ['50:1', '50:1'])
        self.assertIn('what=AddTradeToShipQueue scope', logs[0])
        self.assertEqual(values(h.scopes), [2])

    def test_coop_slot_2_pays_from_the_shared_wallet(self):
        calls, results, logs, h = self.capture_and_apply(
            "env.TransferPlayerMoneyTo(1000,env.ConvertStringTo64Bit('300'))", coop=True)
        self.assertEqual(calls, ['TransferPlayerMoneyTo(1000;id:300)@player'])
        self.assertEqual(values(h.scopes), [])
        self.assertIn('what=TransferPlayerMoneyTo ', logs[0] + ' ')

    def test_slot_1_is_the_player_faction_in_both_modes(self):
        for coop in (False, True):
            calls, results, _, h = self.capture_and_apply("env.SetMinBudget(env.ConvertStringTo64Bit('400'),1000)", slot=1, coop=coop)
            self.assertEqual(calls, ['SetMinBudget(id:400;1000)@player'])
            self.assertEqual(values(h.scopes), [])

    def test_an_offer_that_changed_is_refused_on_every_client(self):
        calls, results, logs, _ = self.capture_and_apply(
            "env.AddTradeToShipQueue(make_id('9001'),env.ConvertStringTo64Bit('200'),10,true)",
            at_apply="OFFERS['300'][3]={id='9003',ware='energycells',isselloffer=true}")
        self.assertEqual(calls, [])
        self.assertEqual(results, ['50:0'])
        self.assertIn('AddTradeToShipQueue offer_changed', logs[0])
        # The second of two matching offers resolves to the second again.
        lua, g = client()
        lua.execute("OFFERS['300'][3]={id='9003',ware='energycells',isselloffer=true}")
        pushed = captured(lua, g, 2, "env.AddTradeToShipQueue(make_id('9003'),env.ConvertStringTo64Bit('200'),5,true)")
        lua.execute(f"S2=trade_session(1);stock_calls={{}};DUE={{[51]='2:{pushed[0][1]}:{pushed[0][2]}'}};S2.apply(51)")
        self.assertEqual(values(g.stock_calls), ['AddTradeToShipQueue(id:9003;id:200;5;true)@x4lan_player2'])

    def test_the_authority_rule(self):
        cases = (
            # another slot's ship, both modes
            ("env.TransferPlayerMoneyTo(10,env.ConvertStringTo64Bit('100'))", False, 'slot_ship'),
            ("env.TransferPlayerMoneyTo(10,env.ConvertStringTo64Bit('100'))", True, 'slot_ship'),
            # factions: the player's station is not slot 2's; coop: it is everyone's
            ("env.SetMinBudget(env.ConvertStringTo64Bit('400'),5)", False, 'not_owner'),
            ("env.SetMinBudget(env.ConvertStringTo64Bit('400'),5)", True, None),
            # the own ship takes no trade order, only the trade computer (immediate)
            ("env.AddTradeToShipQueue(make_id('9001'),env.ConvertStringTo64Bit('200'),10,false)", False, 'slot_ship'),
            # a player ship that is no slot ship: coop only
            ("env.AddTradeToShipQueue(make_id('9002'),env.ConvertStringTo64Bit('500'),3,false)", True, None),
            ("env.AddTradeToShipQueue(make_id('9002'),env.ConvertStringTo64Bit('500'),3,false)", False, 'not_owner'),
        )
        for code, coop, why in cases:
            with self.subTest(code=code, coop=coop):
                calls, results, logs, _ = self.capture_and_apply(code, coop=coop)
                if why:
                    self.assertEqual(calls, [])
                    self.assertEqual(results, ['50:0'])
                    self.assertIn(why, logs[0])
                else:
                    self.assertEqual(len(calls), 1)
                    self.assertEqual(results, ['50:1'])

    def test_a_slot_without_a_ship_or_a_faction_runs_nothing(self):
        calls, results, logs, _ = self.capture_and_apply("env.SetMinBudget(env.ConvertStringTo64Bit('300'),5)", at_apply="SLOT_SHIPS={['100']=1}")
        self.assertEqual((calls, results), ([], ['50:0']))
        self.assertIn('no_ship', logs[0])

    def test_a_reference_gone_is_equal_everywhere_a_mismatch_stops(self):
        calls, results, _, h = self.capture_and_apply("env.SetMinBudget(env.ConvertStringTo64Bit('300'),5)", at_apply="KNOWN['300']=nil")
        self.assertEqual((calls, results), ([], ['50:0']))
        self.assertIsNone(h.FAILED)
        calls, results, _, h = self.capture_and_apply("env.SetMinBudget(env.ConvertStringTo64Bit('300'),5)", at_apply="KNOWN['300']='deadbeef'")
        self.assertEqual(h.FAILED, 'ui_ref_mismatch')

    def test_a_scope_that_did_not_close_as_it_opened_is_reported(self):
        calls, results, logs, h = self.capture_and_apply("env.SetMinBudget(env.ConvertStringTo64Bit('300'),5)", at_apply='SCOPE_END=0')
        self.assertEqual(results, ['50:0'])
        self.assertIn('what=scope', logs[0])

    def test_the_ffi_events_run_natively_after_the_same_check(self):
        lua, g = client()
        lua.execute('S=trade_session(1)')
        ref300 = '%016x' % 300 + '55667788'
        ref400 = '%016x' % 400 + '99887766'
        lua.execute(f"DUE={{[50]='2:89:00{ref300}0700000003627579;2:89:00{ref400}00'}};S.apply(50)")
        self.assertEqual(values(g.runs), ['2:00' + ref300 + '0700000003627579'])
        self.assertEqual(values(g.results), ['50:1', '50:0'])
        logs = [line for line in values(g.logs) if 'UI_REPLAY_APPLIED' in line]
        self.assertIn('what=SetContainerTradeRule', logs[0])
        self.assertIn('what=ffi not_owner', logs[1])

    def test_an_unknown_global_stops(self):
        lua, g = client()
        lua.execute("S=trade_session(1);DUE={[50]='2:88:0c00'};S.apply(50)")
        self.assertEqual(g.FAILED, 'ui_trade_op')


class SignalTests(unittest.TestCase):
    def test_the_docked_trade_signal_is_traded_for_every_slot_the_rest_stays_with_the_events_family(self):
        lua, g = client()
        lua.execute("S=trade_session(2,{mask=1024+256});LOCAL=1;"
                    "env.SignalObject(C.GetPlayerID(),'docked_player_trade_added',make_id('200'));env.SignalObject(make_id('100'),'accept');LOCAL=0")
        pushed = values(g.pushed)
        self.assertEqual([p[1] for p in pushed], [0x58])
        self.assertTrue(pushed[0][2].startswith('0a'))   # SignalObject, the trade family's tenth global
        self.assertTrue(any('UI_REPLAY_REFUSED what=SignalObject why=slot' in line for line in values(g.logs)))
        self.assertEqual(values(g.stock_calls), [])


class FrameTests(unittest.TestCase):
    def test_the_fund_reaches_each_slot_faction_that_is_not_the_player_once(self):
        for coop, expect in ((False, ['AddPlayerMoney(5000000)@x4lan_player2']), (True, [])):
            lua, g = client(coop)
            lua.execute('S=trade_session(1,{fund=50000,fleet_tick=10});S.frame(9);S.frame(10);S.frame(11)')
            self.assertEqual(values(g.stock_calls), expect, coop)
            rows = [line for line in values(g.logs) if 'UI_TRADE_FUND' in line]
            self.assertEqual(len(rows), 0 if coop else 1)
            if not coop:
                self.assertIn('tick=10 slot=2 faction=x4lan_player2 credits=50000 scope=1', rows[0])

    def test_the_receipt_and_the_money_rows(self):
        lua, g = client()
        lua.execute("S=trade_session(1);r1=S.frame(119);r2=S.frame(120);MONEY[2]=1000;LOOP['200']=true;r3=S.frame(121)")
        self.assertEqual(g.r1, 'PUIREP_1:x|UITRADE_1:500000,1234500;0;0')
        self.assertEqual(g.r1, g.r2)
        self.assertEqual(g.r3, 'PUIREP_1:x|UITRADE_1:500000,1000;0;1')
        rows = [line for line in values(g.logs) if 'UI_TRADE_MONEY' in line]
        self.assertEqual(rows, ['UI_TRADE_MONEY tick=119 UITRADE_1:500000,1234500;0;0', 'UI_TRADE_MONEY tick=121 UITRADE_1:500000,1000;0;1'])
        self.assertTrue(any(line.startswith('UI_TRADE_STATUS tick=120 ui_trade=live lua=0/0/0/0') for line in values(g.logs)))

    def test_the_trade_view_of_factions_mode(self):
        lua, g = client()
        lua.execute('''S=trade_session(2)
          out=env.GetPlayerMoney();LOCAL=1;mine=env.GetPlayerMoney();list=env.GetTradeShipList();LOCAL=0''')
        self.assertEqual(g.out, 777)                    # outside this client's UI: the stock answer
        self.assertEqual(g.mine, 12345)                 # client 2's UI: x4lan_player2's account, in credits
        self.assertEqual([lua.eval('DEC(list[%d].shipid)' % i) for i in (1, 2)], ['500', '200'])
        self.assertTrue(any('UI_TRADE_VIEW slot=2 faction=x4lan_player2' in line for line in values(g.logs)))
        for slot, coop in ((1, False), (2, True)):
            lua, g = client(coop)
            lua.execute(f'S=trade_session({slot});LOCAL=1;mine=env.GetPlayerMoney();n=#env.GetTradeShipList();LOCAL=0')
            self.assertEqual((g.mine, g.n), (777, 1), (slot, coop))


class ScriptTests(unittest.TestCase):
    def test_the_ops_call_the_stock_entries_as_the_menus_do(self):
        lua, g = client()
        lua.execute('''
          tools={gate=gate,C=C,env=env,own_slot=2,slot_id=function(s) return env.ConvertStringTo64Bit(tostring(s*100)) end,
            slot_lua=function(s) return make_id(tostring(s*100)) end,saved_id=function(s) return env.ConvertStringTo64Bit(s) end,
            saved_lua=function(s) return make_id(s) end}
          plan={{0,'trade_to_ship',1000},{1,'trade_from_ship',500},{2,'trade_to_ship',100,1},{3,'trade_loop',1},{4,'trade_loop','clear'},
                {5,'trade_buy','300','energycells',10,nil,1},{6,'trade_sell','300','ore',3,'500'},{7,'trade_price','300','ore',1,12},{8,'trade_budget','300',1000},
                {9,'trade_buy','300','silicon',1}}
          S=trade_session(2,{script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)})
          gate.ui_replay_local=function() return 1 end
          for t=40,49 do S.frame(t) end
        ''')
        self.assertEqual(values(g.stock_calls), ['SetLoop:200:true', 'RemoveLoop:200'])   # FFI: the native hook captures them
        pushed = [p[2][:2] for p in values(g.pushed)]
        self.assertEqual(pushed, ['02', '03', '02', '01', '0a', '01', '04', '09', '08'])
        rows = [line for line in values(g.logs) if 'UI_REPLAY_SCRIPT' in line]
        self.assertEqual(len(rows), 10)
        self.assertIn('op=trade_buy ok=true result=no_offer', rows[-1])
        with self.assertRaises(Exception):
            lua.execute("X4LAN_UI_TRADE.script_call({1,'trade_nope'},tools)")


PROBE_TRADE = uirep.PROBE_MOCK + r'''
  gate.ui_trade_pins=function() ui_calls[#ui_calls+1]='trade_pins';return TPINS or 1,TPINS and 'pins_ui_trade:288080' or nil end
  gate.ui_replay_local=function() return 0 end
  gate.ui_replay_push=function(op,hex) ui_calls[#ui_calls+1]='push:'..op;return 1 end
  gate.ui_trade_frame=function() return 'UITRADE_1:1,2' end
  gate.ui_trade_status=function() return 'ui_trade=live' end
  gate.ui_trade_money=function() return 0 end
  GetComponentData=function() return 'player' end
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def clients(self, prelude=PROBE_TRADE, **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel', 'trade'])
        cfg.update(extra)
        return [self.stock.StockClient(k, slots_all=True, prelude=prelude, **cfg) for k in (1, 2)]

    def test_the_family_arms_with_its_pins_first_and_joins_the_receipt(self):
        clients = self.clients()
        self.stock.run(clients, 13)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertLess(calls.index('trade_pins'), calls.index('arm:1025'))
            logs = c.values('logs')
            self.assertTrue(any('UI_TRADE_PINS ok=1' in line for line in logs))
            self.assertTrue(any('UI_REPLAY_ARMED' in line and 'families=1025' in line for line in logs))
            self.assertTrue(any('UITRADE_1:1,2' in data for data in c.values('hashes')))

    def test_refusals(self):
        for extra, prelude, why in ((dict(), 'TPINS=0', 'pins_ui_trade:288080'),
                                    (dict(native_gate_ui_trade_fund=0), '', 'native_gate_ui_trade_fund'),
                                    (dict(native_gate_ui_replay=['travel'], native_gate_ui_trade_fund=100), '', 'native_gate_ui_trade_fund')):
            with self.subTest(why=why):
                c = self.clients(prelude=PROBE_TRADE+prelude, **extra)[0]
                self.assertTrue(any(why in line for line in c.errors()), c.values('logs')[-3:])

    def test_without_the_family_nothing_is_asked(self):
        c = self.clients(native_gate_ui_replay=['travel'])[0]
        self.assertNotIn('trade_pins', c.values('ui_calls'))


class OptionTests(unittest.TestCase):
    def test_rules(self):
        failures = uirep.failures
        cfg = uirep.stock_config
        self.assertEqual(failures(cfg(native_gate_ui_replay="{'trade'}")), [])
        self.assertEqual(failures(cfg(native_gate_ui_replay="{'travel','trade'}", native_gate_ui_trade_fund='50000')), [])
        for rule, override in (('native_gate_ui_trade_fund needs native_gate_ui_replay', dict(native_gate_ui_trade_fund='50000')),
                               ('native_gate_ui_trade_fund needs the trade family', dict(native_gate_ui_replay="{'travel'}", native_gate_ui_trade_fund='50000')),
                               ('native_gate_ui_trade_fund needs the trade family', dict(native_gate_ui_replay='true', native_gate_ui_trade_fund='50000')),
                               ('native_gate_ui_trade_fund must be whole credits 1..1000000000', dict(native_gate_ui_replay="{'trade'}", native_gate_ui_trade_fund='0')),
                               ('native_gate_ui_trade_fund must be whole credits 1..1000000000', dict(native_gate_ui_replay="{'trade'}", native_gate_ui_trade_fund='1.5'))):
            self.assertIn(rule, failures(cfg(**override)), override)

    def test_builder(self):
        lines = uirep.BuilderTests().lines('--ui-replay', 'travel,trade')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'travel','trade'}", lines)


if __name__ == '__main__':
    unittest.main()
