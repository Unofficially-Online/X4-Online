"""UI replay, inventory family (tools/dev/native_gate_ui_inventory_core.lua; lockstep_ui_replay.h UIINV_OP_CLICK, bit 524288).

The Lua half under LuaJIT with a mocked gate and world: two player entities with an inventory each (the mocked slot scope
makes slot k's the player, as the real one does), a recipe, an NPC trader. Covered: a click on a stock button is one event
and nothing runs at the capture; the apply runs it whole in the slot's scope on the player it belongs to, or not at all
(resources gone, the trader's stock gone, no money, wrong arguments); a trader's price in the slot's faction scope; the
refusals before a click runs; the receipt; the scripted ops; the mirrors (bit, op, kinds, file lists, session defaults).
This is a mocked test: it says nothing about X4's own functions, which the live runs check."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402

DEV = ROOT/'tools/dev'
ENGINE = ROOT/'src/native/engine'
CORE = (DEV/'native_gate_ui_inventory_core.lua').read_text(encoding='utf-8')
REPLAY_H = (ENGINE/'lockstep_ui_replay.h').read_text(encoding='utf-8')
INPUTS_H = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
INVENTORY_H = (ENGINE/'lockstep_ui_inventory.h').read_text(encoding='utf-8')

# Players p1 and p2 (slots 1 and 2), trader 300 on station 400. The slot scope sets PLAYER; the faction scope WALLET.
WORLD = r'''
  KNOWN['400']='99887766'
  PLAYER='p1';WALLET='player';entered={};left={};marks={}
  INV={p1={scrap=10,chip=1},p2={scrap=4},['300']={medkit=5,scrap=0}}
  MONEY={player=1000,x4lan_player2=50,station=0}
  RECIPES={gadget={{ware='scrap',amount=2},{ware='chip',amount=1}},bundle={{ware='scrap',amount=1}}}
  FACTION={[1]='player',[2]='player'}
  local function dec(id)
    if type(id)=='cdata' then return (tostring(id):gsub('ULL$','')) end
    return ids[id]
  end
  local function table_of(who)
    local out={}
    for ware,amount in pairs(INV[who] or {}) do if amount>0 then out[ware]={amount=amount,name=ware} end end
    return out
  end
  local function add(who,ware,amount) INV[who]=INV[who] or {};INV[who][ware]=(INV[who][ware] or 0)+amount end
  env.GetPlayerInventory=function() return table_of(PLAYER) end
  PLAYER_IDS={['4301']='p1',['4302']='p2'}
  env.GetInventory=function(entity) local d=dec(entity);return table_of(PLAYER_IDS[d] or d) end
  gate.ui_inventory_player=function(slot) if NO_PLAYER then return nil end;return slot==1 and '4301' or '4302' end
  env.AddInventory=function(entity,ware,amount)
    local who=entity~=nil and (PLAYER_IDS[dec(entity)] or dec(entity)) or PLAYER
    stock_calls[#stock_calls+1]='add:'..who..':'..ware..':'..amount;add(who,ware,amount) end
  env.RemoveInventory=function(entity,ware,amount) stock_calls[#stock_calls+1]='remove:'..PLAYER..':'..ware..':'..amount;add(PLAYER,ware,-amount) end
  env.TransferInventoryToPlayer=function(ware,amount,trader) stock_calls[#stock_calls+1]='take:'..PLAYER..':'..ware..':'..amount;add(dec(trader),ware,-amount);add(PLAYER,ware,amount) end
  env.TransferPlayerInventoryTo=function(ware,amount,trader) stock_calls[#stock_calls+1]='give:'..PLAYER..':'..ware..':'..amount;add(PLAYER,ware,-amount);add(dec(trader),ware,amount) end
  env.TransferPlayerMoneyTo=function(amount,container) stock_calls[#stock_calls+1]='pay:'..WALLET..':'..amount;MONEY[WALLET]=MONEY[WALLET]-amount;MONEY.station=MONEY.station+amount end
  env.TransferMoneyToPlayer=function(amount,container) stock_calls[#stock_calls+1]='paid:'..WALLET..':'..amount;MONEY[WALLET]=MONEY[WALLET]+amount;MONEY.station=MONEY.station-amount end
  env.SetNPCBlackboard=function(entity,name,value) marks[#marks+1]=dec(entity)..':'..name..':'..tostring(value)..'@'..PLAYER end
  env.GetPlayerMoney=function() return MONEY[WALLET] end
  env.GetWareData=function(ware,...)
    local out={}
    for i,key in ipairs({...}) do
      if key=='resources' then out[i]=RECIPES[ware] or {}
      elseif key=='isunbundleammo' then out[i]=ware=='bundle'
      elseif key=='component' then out[i]='ammo_macro'
      else out[i]=false end
    end
    return unpack(out,1,select('#',...))
  end
  env.HasAllResourcesToCraft=function(entity,ware,amount)
    for _,r in ipairs(RECIPES[ware] or {}) do if ((INV[PLAYER] or {})[r.ware] or 0)<r.amount*amount then return false end end
    return true
  end
  cargo_calls={}
  env.AddAmmo=function(ship,component,amount,checkonly,virtual)
    cargo_calls[#cargo_calls+1]='AddAmmo:'..tostring(amount)..':'..tostring(checkonly)..':'..tostring(virtual)..':bypass='..tostring(BYPASS)
    return AMMO_ROOM or amount end
  for _,name in ipairs({'AddCargo','RemoveCargo','RemoveAmmo'}) do env[name]=function(ship,ware,amount,a4,a5)
    cargo_calls[#cargo_calls+1]=name..':'..tostring(amount)..':'..tostring(a4)..':'..tostring(a5)..':bypass='..tostring(BYPASS)
    if GUARD and BYPASS~=1 then return end                       -- the UI guard: a refused Lua function answers nothing
    return math.min(amount,CARGO_ROOM or amount) end end
  env.GetComponentData=function(id,key) if key=='isplayerowned' then return OWN_STATION or dec(id)=='500' end end
  env.GetContextByClass=function(id,class) return 400ULL end
  C.GetPlayerOccupiedShipID=function() return SHIP or 0 end
  C.GetPlayerID=function() return PLAYER=='p2' and 4302ULL or 4301ULL end
  pcall(ffi.cdef,'typedef struct { const char* wareid; int32_t amount; } UIWareAmount;')
  drops={}
  C.DropInventory=function(entity,lockbox,wares,count)
    local out={}
    for i=0,count-1 do local ware,amount=ffi.string(wares[i].wareid),tonumber(wares[i].amount);out[#out+1]=ware..':'..amount;add(PLAYER,ware,-amount) end
    drops[#drops+1]=(tostring(entity):gsub('ULL$',''))..'@'..PLAYER..':'..tostring(lockbox)..':'..table.concat(out,',')
    return DROP_ANSWER~=false end
  gate.ui_replay_local=function() return LOCAL or 0 end
  gate.mpp_scope=function(slot) if MPP_SCOPE==0 then return 0 end;entered[#entered+1]=slot;PLAYER='p'..slot;return 1 end
  gate.mpp_unscope=function(slot) left[#left+1]=slot;PLAYER='p1';return MPP_LEAVE or 1 end
  gate.ui_trade_scope=function(slot)
    local f=FACTION[slot]
    if not f then return -1 end
    if f=='player' then return 0 end
    WALLET=f;return 1
  end
  gate.ui_trade_scope_end=function() WALLET='player';return 1 end
  gate.ui_replay_saved=function(saved) return saved end
  -- the native recorder: while a scope is open the exports below are rows, not calls (EXPORT_LOST: a row did not fit)
  recording=nil;exports={};learned={};licences={}
  gate.ui_inventory_scope=function(on)
    if on==1 then recording={};return 1 end
    local rows=table.concat(recording or {});recording=nil
    if EXPORT_LOST then return nil,'lost' end
    return rows end
  local function export(name,run) C[name]=function(...)
    local out={name}
    for _,v in ipairs({...}) do out[#out+1]=type(v)=='cdata' and (tostring(v):gsub('U?LL$','')) or tostring(v) end
    if recording then recording[#recording+1]=table.concat(out,'\t')..'\n';return end
    exports[#exports+1]=table.concat(out,':')..'@'..WALLET
    if run then run(...) end
  end end
  export('AddPlayerMoney',function(cents) MONEY[WALLET]=MONEY[WALLET]+tonumber(cents)/100 end)
  export('LearnBlueprint',function(ware) learned[#learned+1]=ware end)
  export('StartResearch');export('ClearProductionItems');export('StartTerraformingProject');export('AbortActiveTerraformingProject')
  -- a station's ware reservations: {reserver, ware, amount, buy, deal, supply}; the deal IDs are this client's own (DEAL_BASE)
  pcall(ffi.cdef,[[typedef uint64_t UniverseID;typedef uint64_t TradeID;typedef uint64_t MissionID;
    typedef struct { UniverseID reserverid; const char* ware; uint32_t amount; bool isbuyreservation; double eta; TradeID tradedealid;
      MissionID missionid; bool isvirtual; bool issupply; } WareReservationInfo2;]])
  RESERVATIONS={{100,'ore',40,true,1,false},{200,'ore',40,true,2,false},{200,'energycells',5,false,3,false},{200,'water',9,true,4,true}}
  DEAL_BASE=7000;cancelled={};held={}
  C.GetNumContainerWareReservations2=function(container) return dec(container)=='500' and #RESERVATIONS or 0 end
  C.GetContainerWareReservations2=function(buf,n,container)
    if dec(container)~='500' then return 0 end
    for i,r in ipairs(RESERVATIONS) do
      held[i]=r[2]
      buf[i-1].reserverid=r[1];buf[i-1].ware=held[i];buf[i-1].amount=r[3];buf[i-1].isbuyreservation=r[4];buf[i-1].tradedealid=DEAL_BASE+r[5];buf[i-1].issupply=r[6]
    end
    return #RESERVATIONS end
  C.CancelPlayerInvolvedTradeDeal=function(container,deal,checkonly)
    local id=tonumber(deal)
    if recording then recording[#recording+1]='CancelPlayerInvolvedTradeDeal\t'..dec(container)..'\t'..string.format('%d',id)..'\t'..(checkonly and 1 or 0)..'\n';return true end
    cancelled[#cancelled+1]=dec(container)..':'..string.format('%d',id-DEAL_BASE)
    return CANCEL_ANSWER~=false end
  env.Helper={buttonCancelTrade=function(menu,container,deal) if C.CancelPlayerInvolvedTradeDeal(container,deal,false) then DIRTY=tostring(deal) end end}
  C.CanStartTerraformingProject=function() return CAN_START~=false end
  C.CanAbortActiveTerraformingProject=function() return CAN_ABORT~=false end
  known_items={}
  env.AddKnownItem=function(library,item) known_items[#known_items+1]=library..'/'..item end
  env.AddLicence=function(holder,kind,faction) licences[#licences+1]=holder..':'..kind..':'..faction..'@'..WALLET end
  KNOWN['500']='0a0b0c0d';KNOWN['600']='01020304'
  Menus={
    {name='PlayerInfoMenu',buttonInventoryCraft=function()
       for _,r in ipairs(RECIPES[CRAFT[1]] or {}) do env.RemoveInventory(nil,r.ware,CRAFT[2]*r.amount) end
       if CRAFT[2]>0 then env.AddInventory(nil,CRAFT[1],CRAFT[2],true) end
       return 'crafted' end},
    {name='InventoryTraderMenu',islockbox=false,tradecontainer=400ULL,buttonConfirm=function()
       if TRADE.money>0 then env.TransferPlayerMoneyTo(TRADE.money,400ULL) elseif TRADE.money<0 then env.TransferMoneyToPlayer(-TRADE.money,400ULL) end
       for i=1,#TRADE.wares,2 do
         if TRADE.wares[i+1]>0 then env.TransferInventoryToPlayer(TRADE.wares[i],TRADE.wares[i+1],300ULL)
         else env.TransferPlayerInventoryTo(TRADE.wares[i],-TRADE.wares[i+1],300ULL) end
       end
       env.SetNPCBlackboard(300ULL,'$TradeDone',true) end},
    {name='OtherMenu',buttonInventoryCraft=function() return 'other' end},
    {name='BlueprintOrLicenceTraderMenu',buttonConfirm=function()
       if env.GetPlayerMoney()>=BUY.price then
         for _,l in ipairs(BUY.licences or {}) do env.AddLicence('player',l[1],l[2]) end
         for _,ware in ipairs(BUY.blueprints or {}) do C.LearnBlueprint(ware) end
         if BUY.own then C.AddPlayerMoney(-BUY.price*100) else env.TransferPlayerMoneyTo(BUY.price,400ULL) end
       end end},
    {name='ResearchMenu',buttonStartResearch=function(tech) C.StartResearch(tech,500ULL) end,
       buttonCancelResearch=function(tech) C.ClearProductionItems(500ULL) end},
    {name='StationConfigurationMenu',buttonCancelTrade=function(deal) return C.CancelPlayerInvolvedTradeDeal(500ULL,deal,false) end},
    {name='TerraformingMenu',buttonStartProject=function(project) C.StartTerraformingProject(600ULL,project) end,
       buttonAbortProject=function() C.AbortActiveTerraformingProject(600ULL) end},
  }
  function inv_session(slot,extra)
    extra=extra or {}
    S=X4LAN_UI_REPLAY_CORE.session({gate=gate,C=C,env=env,log=log,mask=extra.mask or 524288,slot_ships=function() return SLOT_SHIPS end,
      own_slot=slot,slots=2,script=extra.script})
    I=X4LAN_UI_INVENTORY_CORE.session({gate=gate,C=C,ffi=ffi,env=env,log=log,replay=S,own_slot=slot,factions=extra.factions,trade=extra.trade,
      mpp=extra.mpp~=false,notify=function(text) notices[#notices+1]=text end,menus=function() return Menus end})
    S.inventory=I
    return S
  end
  notices={}
  function menu(name) for _,m in ipairs(Menus) do if m.name==name then return m end end end
'''


def client(factions=False):
    lua, g = uirep.client()
    lua.execute(CORE)
    lua.execute(WORLD)
    if factions:
        lua.execute("FACTION[2]='x4lan_player2'")
    return lua, g


def values(table):
    return list(table.values()) if table is not None else []


def click(lua, g, slot, code, tick=41, **extra):
    """Client `slot`: its session, one frame (the buttons are wrapped there), then `code` in its UI; the events it pushed."""
    options = ','.join(f'{k}={str(v).lower()}' for k, v in extra.items())
    lua.execute(f'inv_session({slot},{{{options}}});S.frame({tick});LOCAL=1;{code};LOCAL=0')
    return [(int(e[1]), e[2]) for e in values(g.pushed)]


def apply(lua, g, slot, events, tick=50, own=1, **extra):
    """Every client's apply of `events` (slot's), on a fresh session of client `own`: ok rows, the frame's receipt."""
    options = ','.join(f'{k}={str(v).lower()}' for k, v in extra.items())
    due = ';'.join(f'{slot}:{op}:{hex_}' for op, hex_ in events)
    lua.execute(f"inv_session({own},{{{options}}});DUE={{[{tick}]='{due}'}};RECEIPT=S.frame({tick})")
    return g.RECEIPT


def amounts(g, who):
    return {k: v for k, v in dict(g.INV[who]).items() if v}


class MirrorTests(unittest.TestCase):
    def test_bit_op_and_kinds(self):
        lua, _ = client()
        k = lua.eval('X4LAN_UI_INVENTORY_CORE')
        self.assertEqual(k.FAMILY, int(re.search(r'UIREP_FAMILY_INVENTORY=(\d+)', INPUTS_H).group(1)))
        self.assertEqual(k.FAMILY, lua.eval('X4LAN_UI_REPLAY_CORE.FAMILY.inventory'))
        self.assertEqual(k.OP, int(re.search(r'#define UIINV_OP_CLICK (0x[0-9a-f]+)u', REPLAY_H).group(1), 16))
        self.assertEqual(len(values(k.KIND_NAME)), int(re.search(r'#define UIINV_KINDS (\d+)u', REPLAY_H).group(1)))
        self.assertEqual(k.KIND.drop, int(re.search(r'#define UIINV_KIND_DROP (\d+)u', INVENTORY_H).group(1)))
        self.assertEqual({name: number for name, number in k.KIND.items()}, {name: i + 1 for i, name in enumerate(values(k.KIND_NAME))})
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'inventory'})"), 524288)

    def test_the_core_file_ships_and_the_family_is_a_session_default(self):
        defaults = uirep.load('native_gate_defaults_inv', 'tools/dev/native_gate_defaults.py')
        self.assertIn('ui/native_gate_ui_inventory_core.lua', uirep.players.UI_REPLAY_FILES)
        self.assertIn('ui/native_gate_ui_inventory_core.lua', defaults.UI_FILES)
        self.assertLess(defaults.UI_FILES.index('ui/native_gate_ui_replay_core.lua'), defaults.UI_FILES.index('ui/native_gate_ui_inventory_core.lua'))
        self.assertIn('inventory', defaults.TABLE['families'])
        self.assertIn('inventory', uirep.players.UI_REPLAY_FAMILIES)
        self.assertIn('inventory', uirep.options.UI_REPLAY_FAMILIES)

    def test_the_native_section_rule(self):
        rule = REPLAY_H[REPLAY_H.index('    case UIINV_OP_CLICK:\n'):]
        rule = rule[:rule.index('return 1;')]
        for part in ('UIREP_FAMILY_INVENTORY', '(slot!=1 && !mpp_armed)', 'data[0]>UIINV_KINDS', '"ui_inventory"'):
            self.assertIn(part, rule)
        push = (ENGINE/'lockstep_ui_replay_lua.h').read_text(encoding='utf-8')
        self.assertIn('op==UIINV_OP_CLICK && (uirep_families&UIREP_FAMILY_INVENTORY) && (pstk_capture_slot==1 || mpp_armed)', push)


class CraftTests(unittest.TestCase):
    def test_a_craft_click_is_one_event_and_nothing_runs(self):
        lua, g = client()
        events = click(lua, g, 2, "CRAFT={'gadget',2};RESULT=menu('PlayerInfoMenu').buttonInventoryCraft()")
        self.assertEqual(g.RESULT, 'crafted')                       # the stock function ran to its end
        self.assertEqual([op for op, _ in events], [0x90])
        self.assertTrue(events[0][1].startswith('01'))
        self.assertEqual(values(g.stock_calls), [])                 # no inventory call reached X4
        self.assertEqual(amounts(g, 'p1'), {'scrap': 10, 'chip': 1})
        self.assertEqual(amounts(g, 'p2'), {'scrap': 4})
        self.assertTrue(any('UI_INVENTORY_CAPTURED kind=craft what=craft gadget x2' in row for row in values(g.logs)))
        self.assertTrue(lua.eval('env.AddInventory==env.X4LAN_UI_INVENTORY_STOCK.AddInventory'))   # the globals are back

    def test_the_mode_click_of_the_same_button_is_no_event(self):
        lua, g = client()
        self.assertEqual(click(lua, g, 1, "CRAFT={'gadget',0};menu('PlayerInfoMenu').buttonInventoryCraft()"), [])

    def test_the_apply_crafts_on_the_slots_own_player(self):
        lua, g = client()
        events = click(lua, g, 1, "CRAFT={'gadget',3};menu('PlayerInfoMenu').buttonInventoryCraft()")
        for own in (1, 2):                                          # each client runs the same calls
            lua.execute("INV.p1={scrap=10,chip=3};INV.p2={scrap=10,chip=3};stock_calls={};entered={};left={}")
            receipt = apply(lua, g, 1, events, own=own)
            self.assertEqual(values(g.stock_calls), ['remove:p1:scrap:6', 'remove:p1:chip:3', 'add:p1:gadget:3'])
            self.assertEqual(values(g.entered), [])                 # slot 1: the canonical player
            self.assertEqual(amounts(g, 'p1'), {'scrap': 4, 'gadget': 3})
            self.assertEqual(amounts(g, 'p2'), {'scrap': 10, 'chip': 3})
            self.assertIn('|UIINV_1:1:craft:ran:gadget=3', receipt)

    def test_slot_2_crafts_from_its_own_inventory_in_its_scope(self):
        lua, g = client()
        lua.execute("INV.p2={scrap=5,chip=2}")
        events = click(lua, g, 2, "CRAFT={'gadget',2};menu('PlayerInfoMenu').buttonInventoryCraft()")
        for own in (1, 2):
            lua.execute("INV.p1={scrap=10,chip=1};INV.p2={scrap=5,chip=2};stock_calls={};entered={};left={}")
            receipt = apply(lua, g, 2, events, own=own)
            self.assertEqual(values(g.stock_calls), ['remove:p2:scrap:4', 'remove:p2:chip:2', 'add:p2:gadget:2'])
            self.assertEqual((values(g.entered), values(g.left)), ([2], [2]))
            self.assertEqual(g.PLAYER, 'p1')
            self.assertEqual(amounts(g, 'p1'), {'scrap': 10, 'chip': 1})
            self.assertEqual(amounts(g, 'p2'), {'scrap': 1, 'gadget': 2})
            self.assertIn('|UIINV_1:2:craft:ran:gadget=2', receipt)
        self.assertIsNone(g.FAILED)

    def test_resources_gone_by_the_apply_tick_nothing_runs(self):
        lua, g = client()
        lua.execute("INV.p2={scrap=5,chip=2}")
        events = click(lua, g, 2, "CRAFT={'gadget',2};menu('PlayerInfoMenu').buttonInventoryCraft()")
        lua.execute("INV.p2={scrap=3,chip=2}")
        receipt = apply(lua, g, 2, events)
        self.assertEqual(values(g.stock_calls), [])
        self.assertEqual(amounts(g, 'p2'), {'scrap': 3, 'chip': 2})
        self.assertIn('|UIINV_1:2:craft:skipped:resources', receipt)
        self.assertEqual(values(g.results), ['50:0'])

    def test_a_ware_without_a_recipe_and_bad_arguments(self):
        lua, g = client()
        lua.execute('''K=X4LAN_UI_REPLAY_CORE
          function ev(...) return '01'..assert(K.encode_call({...},select('#',...),{})) end
          E={ev('medkit',1),ev('gadget',0),ev('gadget',1.5),ev('gadget',1,2),ev(5,1),ev('gadget',1e9)}''')
        for hex_, why in zip(values(g.E), ('no_recipe', 'arguments', 'arguments', 'arguments', 'arguments', 'arguments')):
            lua.execute('stock_calls={}')
            receipt = apply(lua, g, 1, [(0x90, hex_)])
            self.assertIn('craft:skipped:' + why, receipt)
            self.assertEqual(values(g.stock_calls), [])
        lua.execute('FAILED=nil')
        apply(lua, g, 1, [(0x90, '1f' + values(g.E)[0][2:])])
        self.assertEqual(g.FAILED, 'ui_inventory_op')               # a kind nobody sends

    def test_bundled_ammunition_needs_room_in_the_ship(self):
        lua, g = client()
        events = click(lua, g, 1, "CRAFT={'bundle',2};menu('PlayerInfoMenu').buttonInventoryCraft()")
        lua.execute('SHIP=77ULL;AMMO_ROOM=1;stock_calls={}')
        self.assertIn('craft:skipped:ammo_space', apply(lua, g, 1, events))
        self.assertEqual(values(g.stock_calls), [])
        lua.execute('AMMO_ROOM=nil')
        self.assertIn('craft:ran:bundle=2', apply(lua, g, 1, events))

    def test_a_scope_that_cannot_be_entered_or_left_stops_the_replay(self):
        lua, g = client()
        lua.execute("INV.p2={scrap=5,chip=2}")
        events = click(lua, g, 2, "CRAFT={'gadget',1};menu('PlayerInfoMenu').buttonInventoryCraft()")
        lua.execute('MPP_SCOPE=0;stock_calls={}')
        apply(lua, g, 2, events)
        self.assertEqual((g.FAILED, values(g.stock_calls)), ('inventory_scope', []))
        lua.execute('MPP_SCOPE=nil;MPP_LEAVE=0;FAILED=nil')
        apply(lua, g, 2, events)
        self.assertEqual(g.FAILED, 'inventory_leave')

    def test_slot_2_without_its_scope_is_refused_before_the_click_runs(self):
        lua, g = client()
        events = click(lua, g, 2, "CRAFT={'gadget',1};RESULT=menu('PlayerInfoMenu').buttonInventoryCraft()", mpp=False)
        self.assertEqual(events, [])
        self.assertIsNone(g.RESULT)
        self.assertEqual(values(g.stock_calls), [])
        self.assertTrue(any('needs_missions_per_player' in row for row in values(g.logs)))
        self.assertEqual(len(values(g.notices)), 1)

    def test_outside_a_local_phase_the_button_is_the_stock_one(self):
        lua, g = client()
        lua.execute("inv_session(1);S.frame(41);CRAFT={'gadget',1};menu('PlayerInfoMenu').buttonInventoryCraft()")
        self.assertEqual(values(g.pushed), [])
        self.assertEqual(values(g.stock_calls), ['remove:p1:scrap:2', 'remove:p1:chip:1', 'add:p1:gadget:1'])

    def test_only_the_listed_buttons_are_wrapped_once(self):
        lua, g = client()
        lua.execute("other=menu('OtherMenu').buttonInventoryCraft;inv_session(1);S.frame(41);first=menu('PlayerInfoMenu').buttonInventoryCraft;S.frame(42)")
        self.assertTrue(lua.eval("menu('OtherMenu').buttonInventoryCraft==other"))
        self.assertTrue(lua.eval("menu('PlayerInfoMenu').buttonInventoryCraft==first"))
        self.assertEqual(sum('UI_INVENTORY_SCOPES menu=' in row for row in values(g.logs)), 7)


class TradeTests(unittest.TestCase):
    BUY = "TRADE={money=120,wares={'medkit',2,'scrap',-3}};menu('InventoryTraderMenu').buttonConfirm()"

    def test_a_trader_click_is_one_event_and_nothing_runs(self):
        lua, g = client()
        events = click(lua, g, 2, self.BUY)
        self.assertEqual([op for op, _ in events], [0x90])
        self.assertTrue(events[0][1].startswith('02'))
        self.assertEqual((values(g.stock_calls), values(g.marks)), ([], []))
        self.assertEqual(dict(g.MONEY), {'player': 1000, 'x4lan_player2': 50, 'station': 0})
        self.assertLessEqual(len(events[0][1])/2, 200)

    def test_coop_the_shared_wallet_pays_and_the_wares_are_the_slots(self):
        lua, g = client()
        events = click(lua, g, 2, self.BUY)
        for own in (1, 2):
            lua.execute("INV.p1={scrap=10};INV.p2={scrap=4};INV['300']={medkit=5};MONEY={player=1000,x4lan_player2=50,station=0};stock_calls={};marks={};entered={}")
            receipt = apply(lua, g, 2, events, own=own, trade=True)
            self.assertEqual(values(g.stock_calls), ['pay:player:120', 'take:p2:medkit:2', 'give:p2:scrap:3'])
            self.assertEqual(values(g.marks), ['300:$TradeDone:true@p2'])
            self.assertEqual(amounts(g, 'p2'), {'scrap': 1, 'medkit': 2})
            self.assertEqual(amounts(g, 'p1'), {'scrap': 10})
            self.assertEqual(amounts(g, '300'), {'medkit': 3, 'scrap': 3})
            self.assertEqual(dict(g.MONEY)['player'], 880)
            self.assertEqual(values(g.entered), [2, 2])             # the check, then the wares; the money between them
            self.assertIn('|UIINV_1:2:trade:ran:medkit=2,scrap=1', receipt)

    def test_factions_the_slots_own_account_pays(self):
        lua, g = client(factions=True)
        events = click(lua, g, 2, "TRADE={money=40,wares={'medkit',1}};menu('InventoryTraderMenu').buttonConfirm()", factions=True, trade=True)
        receipt = apply(lua, g, 2, events, factions=True, trade=True)
        self.assertEqual(values(g.stock_calls), ['pay:x4lan_player2:40', 'take:p2:medkit:1'])
        self.assertEqual((dict(g.MONEY)['x4lan_player2'], dict(g.MONEY)['player'], g.WALLET), (10, 1000, 'player'))
        self.assertIn('trade:ran:medkit=1', receipt)

    def test_selling_pays_the_player(self):
        lua, g = client()
        events = click(lua, g, 1, "TRADE={money=-30,wares={'scrap',-5}};menu('InventoryTraderMenu').buttonConfirm()")
        apply(lua, g, 1, events, trade=True)
        self.assertEqual(values(g.stock_calls), ['paid:player:30', 'give:p1:scrap:5'])
        self.assertEqual(dict(g.MONEY)['player'], 1030)

    def test_nothing_runs_when_the_wares_or_the_money_are_gone(self):
        lua, g = client()
        events = click(lua, g, 2, self.BUY)
        for change, why in (("INV['300'].medkit=1", 'trader_stock'), ("INV.p2.scrap=2", 'player_stock'), ("MONEY.player=100", 'money')):
            lua.execute("INV.p2={scrap=4};INV['300']={medkit=5};MONEY={player=1000,x4lan_player2=50,station=0};stock_calls={};marks={};" + change)
            receipt = apply(lua, g, 2, events, trade=True)
            self.assertIn('trade:skipped:' + why, receipt)
            self.assertEqual((values(g.stock_calls), values(g.marks)), ([], []))

    def test_what_is_not_shared_yet_is_refused_before_the_click_runs(self):
        lua, g = client()
        self.assertEqual(click(lua, g, 2, self.BUY, factions=True), [])        # factions mode without the trade family
        self.assertTrue(any('why=needs_trade_family' in row for row in values(g.logs)))
        self.assertEqual(values(g.stock_calls), [])
        self.assertEqual(len(click(lua, g, 1, self.BUY, factions=True)), 1)    # slot 1 is `player`: its wallet as it is

    def test_a_push_the_gate_refuses_is_a_refused_click(self):
        lua, g = client()
        lua.execute("PUSH_RESULT=0")
        self.assertEqual(len(click(lua, g, 1, self.BUY)), 1)                   # the mocked gate saw the push and refused it
        self.assertTrue(any('UI_INVENTORY_REFUSED' in row and 'why=native' in row for row in values(g.logs)))
        self.assertEqual(values(g.stock_calls), [])

    MANY = "TRADE={money=-70,wares={'w01',-1,'w02',-1,'w03',-1,'w04',-1,'w05',-1,'w06',-1,'w07',-1,'w08',-1,'w09',-1,'w10',-1,'w11',-1,'w12',-1}};menu('InventoryTraderMenu').buttonConfirm()"
    STOCK = "INV.p2={};for i=1,12 do INV.p2[string.format('w%02d',i)]=1 end;INV['300']={};MONEY={player=1000,x4lan_player2=50,station=500};stock_calls={};marks={}"

    def test_a_trade_of_many_wares_goes_as_parts_and_runs_whole(self):
        lua, g = client()
        lua.execute(self.STOCK)
        events = click(lua, g, 2, self.MANY)
        self.assertEqual([hex_[:2] for _, hex_ in events], ['09', '09', '02'])  # 5 + 2 wares ahead, 5 with the trade
        self.assertTrue(all(len(hex_)/2 <= 200 for _, hex_ in events))
        self.assertEqual(values(g.stock_calls), [])
        for own in (1, 2):
            lua.execute(self.STOCK)
            receipt = apply(lua, g, 2, events, own=own, trade=True)
            calls = values(g.stock_calls)
            self.assertEqual(calls[0], 'paid:player:70')
            self.assertEqual(sorted(calls[1:]), sorted('give:p2:w%02d:1' % i for i in range(1, 13)))
            self.assertEqual(amounts(g, 'p2'), {})
            self.assertIn('2:trade_part:ran:staged=5;2:trade_part:ran:staged=7;2:trade:ran:', receipt)
            self.assertIsNone(g.FAILED)

    def test_nothing_of_a_large_trade_runs_when_one_ware_is_gone(self):
        lua, g = client()
        lua.execute(self.STOCK)
        events = click(lua, g, 2, self.MANY)
        lua.execute(self.STOCK + ";INV.p2.w02=0")                              # one of the wares that went ahead
        receipt = apply(lua, g, 2, events, trade=True)
        self.assertIn('2:trade:skipped:player_stock', receipt)
        self.assertEqual((values(g.stock_calls), dict(g.MONEY)['player']), ([], 1000))

    def test_a_trade_without_all_its_parts_stops_the_replay(self):
        lua, g = client()
        lua.execute(self.STOCK)
        events = click(lua, g, 2, self.MANY)
        lua.execute(self.STOCK)
        apply(lua, g, 2, events[1:], trade=True)                               # a client that missed the first part
        self.assertEqual((g.FAILED, values(g.stock_calls)), ('inventory_trade_parts', []))
        # an unfinished click's parts do not leak into the next trade: its first part empties them
        lua.execute(self.STOCK + ";FAILED=nil")
        apply(lua, g, 2, events[:1] + events, trade=True)
        self.assertIsNone(g.FAILED)
        self.assertEqual(len(values(g.stock_calls)), 13)

    def test_too_many_wares_in_one_click_are_refused(self):
        lua, g = client()
        wares = ','.join("'w%03d',-1" % i for i in range(61))
        self.assertEqual(click(lua, g, 1, "TRADE={money=0,wares={%s}};menu('InventoryTraderMenu').buttonConfirm()" % wares), [])
        self.assertTrue(any('why=too_many_wares' in row for row in values(g.logs)))


class ReadTests(unittest.TestCase):
    """Client k's menus read slot k's own inventory (the presentation scope keeps slot 1's player entity)."""
    def test_slot_2s_menus_read_its_own_inventory_and_the_apply_reads_the_stock_function(self):
        lua, g = client()
        lua.execute("INV.p1={scrap=10,chip=1};INV.p2={scrap=4};inv_session(2);S.frame(41)")
        lua.execute("LOCAL=1;A=env.GetPlayerInventory().scrap.amount;B=env.HasAllResourcesToCraft(nil,'gadget',1);C1=env.HasAllResourcesToCraft(nil,'bundle',4);LOCAL=0")
        self.assertEqual((g.A, g.B, g.C1), (4, False, True))          # slot 1 could craft the gadget, slot 2 cannot: its own answer
        lua.execute("D=env.GetPlayerInventory().scrap.amount;E=env.HasAllResourcesToCraft(nil,'gadget',1)")
        self.assertEqual((g.D, g.E), (10, True))                      # not a person's UI (the probe, an apply): X4's own answer
        self.assertEqual(lua.eval('I.reads'), 3)

    def test_slot_1_keeps_the_stock_readers(self):
        lua, _ = client()
        lua.execute("inv_session(2);inv_session(1)")                  # a later session of the same Lua state as slot 1
        self.assertTrue(lua.eval('env.GetPlayerInventory==env.X4LAN_UI_INVENTORY_STOCK.GetPlayerInventory'))
        self.assertTrue(lua.eval('env.HasAllResourcesToCraft==env.X4LAN_UI_INVENTORY_STOCK.HasAllResourcesToCraft'))

    def test_a_read_x4_refuses_falls_back_to_its_own_answer(self):
        lua, g = client()
        lua.execute("inv_session(2);local real=env.X4LAN_UI_INVENTORY_STOCK.GetInventory;"
                    "env.X4LAN_UI_INVENTORY_STOCK.GetInventory=function() error('no such entity') end;"
                    "LOCAL=1;A=env.GetPlayerInventory().scrap.amount;B=env.HasAllResourcesToCraft(nil,'gadget',1);LOCAL=0;"
                    "env.X4LAN_UI_INVENTORY_STOCK.GetInventory=real")
        self.assertEqual((g.A, g.B, lua.eval('I.read_failures')), (10, True, 2))

    def test_without_the_slots_player_the_stock_answer_stands(self):
        lua, g = client()
        lua.execute("NO_PLAYER=true;inv_session(2);LOCAL=1;A=env.GetPlayerInventory().scrap.amount;LOCAL=0")
        self.assertEqual(g.A, 10)

    def test_the_craft_click_of_slot_2_is_judged_on_its_own_inventory(self):
        lua, g = client()
        lua.execute('''INV.p1={scrap=0};INV.p2={scrap=5,chip=2}
          menu('PlayerInfoMenu').buttonInventoryCraft=function()
            if env.HasAllResourcesToCraft(nil,CRAFT[1],CRAFT[2]) then
              for _,r in ipairs(RECIPES[CRAFT[1]]) do env.RemoveInventory(nil,r.ware,CRAFT[2]*r.amount) end
              env.AddInventory(nil,CRAFT[1],CRAFT[2],true)
            end end''')
        events = click(lua, g, 2, "CRAFT={'gadget',2};menu('PlayerInfoMenu').buttonInventoryCraft()")
        self.assertEqual(len(events), 1)                              # slot 1 has nothing; slot 2's own resources allow it
        self.assertIn('craft:ran:gadget=2', apply(lua, g, 2, events, own=1))


class DealTests(unittest.TestCase):
    """A station's ware reservation cancelled: the deal's ID is each client's own, the event names what both see."""
    def test_the_cancel_button_is_one_event_that_names_the_reservation(self):
        lua, g = client()
        events = click(lua, g, 2, "env.Helper.buttonCancelTrade(nil,500ULL,7002ULL)")
        self.assertEqual([(op, hex_[:2]) for op, hex_ in events], [(0x90, '0b')])
        self.assertEqual((values(g.cancelled), g.DIRTY), ([], '7002ULL'))          # nothing ran; the menu was answered true
        self.assertTrue(any('UI_INVENTORY_CAPTURED kind=cancel_deal what=cancel deal ore x40' in row for row in values(g.logs)))
        # every client finds the reservation under its own ID: the second ore row (ship 200), not the first (ship 100)
        for own, base in ((1, 7000), (2, 91000)):
            lua.execute(f"DEAL_BASE={base};cancelled={{}}")
            receipt = apply(lua, g, 2, events, own=own)
            self.assertEqual(values(g.cancelled), ['500:2'])
            self.assertIn('|UIINV_1:2:cancel_deal:ran:ore x40', receipt)

    def test_the_station_configurations_own_button_too(self):
        lua, g = client()
        events = click(lua, g, 1, "menu('StationConfigurationMenu').buttonCancelTrade(7003ULL)")
        self.assertEqual(len(events), 1)
        apply(lua, g, 1, events)
        self.assertEqual(values(g.cancelled), ['500:3'])

    def test_a_reservation_that_is_gone_a_foreign_station_and_x4s_refusal(self):
        lua, g = client()
        events = click(lua, g, 1, "env.Helper.buttonCancelTrade(nil,500ULL,7002ULL)")
        lua.execute("SAVED=RESERVATIONS;RESERVATIONS={RESERVATIONS[1]}")
        self.assertIn('cancel_deal:skipped:deal_gone', apply(lua, g, 1, events))
        lua.execute("RESERVATIONS=SAVED;CANCEL_ANSWER=false")
        self.assertIn('cancel_deal:skipped:x4_refused:ore x40', apply(lua, g, 1, events))
        lua.execute("CANCEL_ANSWER=nil;cancelled={};pushed={};logs={}")
        self.assertEqual(click(lua, g, 1, "env.Helper.buttonCancelTrade(nil,500ULL,7999ULL)"), [])   # a deal this client does not list
        self.assertTrue(any('why=deal_gone' in row for row in values(g.logs)))
        lua.execute('''K=X4LAN_UI_REPLAY_CORE;ctx={ref=function(u) return gate.ui_replay_ref((tostring(u):gsub('ULL$',''))) end}
          E='0b'..assert(K.encode_call({300ULL,200ULL,'ore',40,true},5,ctx))''')
        self.assertIn('cancel_deal:skipped:not_owner', apply(lua, g, 1, [(0x90, g.E)]))
        self.assertEqual(values(g.cancelled), [])

    def test_a_supply_reservation_is_not_offered_and_factions_mode_keeps_it_for_slot_1(self):
        lua, g = client()
        self.assertEqual(click(lua, g, 1, "env.Helper.buttonCancelTrade(nil,500ULL,7004ULL)"), [])   # the supply row
        lua2, g2 = client(factions=True)
        self.assertEqual(click(lua2, g2, 2, "env.Helper.buttonCancelTrade(nil,500ULL,7002ULL)", factions=True, trade=True), [])
        self.assertTrue(any('Helper.buttonCancelTrade why=factions' in row for row in values(g2.logs)))


class SliderTests(unittest.TestCase):
    """The trade menu's sliders: virtual cargo and ammunition calls answer at once on the client that asks."""
    def test_a_virtual_call_runs_here_with_the_guard_bypassed_and_answers(self):
        lua, g = client()
        lua.execute("GUARD=true;CARGO_ROOM=7;inv_session(2);LOCAL=1;"
                    "A=env.AddCargo(4302ULL,'ore',10,true);B=env.RemoveCargo(4302ULL,'ore',3,true);"
                    "C1=env.AddAmmo(4302ULL,'missile_macro',2,false,true);D=env.RemoveAmmo(4302ULL,'missile_macro',1,false,true);LOCAL=0")
        self.assertEqual((g.A, g.B, g.C1, g.D), (7, 3, 2, 1))
        self.assertEqual(values(g.cargo_calls), ['AddCargo:10:true:nil:bypass=1', 'RemoveCargo:3:true:nil:bypass=1',
                                                 'AddAmmo:2:false:true:bypass=1', 'RemoveAmmo:1:false:true:bypass=1'])
        self.assertEqual((g.BYPASS, values(g.pushed), lua.eval('I.virtual')), (0, [], 4))   # the bypass is closed again; no event

    def test_a_real_cargo_change_of_a_menu_stays_the_guards(self):
        lua, g = client()
        lua.execute("GUARD=true;inv_session(1);LOCAL=1;A=env.AddCargo(4301ULL,'ore',10);B=env.RemoveCargo(4301ULL,'ore',3,false);"
                    "C1=env.RemoveAmmo(4301ULL,'m',1,false,false);LOCAL=0")
        self.assertEqual((g.A, g.B, g.C1), (None, None, None))         # refused: nothing answered, as before
        self.assertTrue(all(call.endswith('bypass=nil') or call.endswith('bypass=0') for call in values(g.cargo_calls)))

    def test_the_craft_buttons_room_check_answers(self):
        lua, g = client()
        lua.execute("inv_session(1);LOCAL=1;A=env.AddAmmo(4301ULL,'missile_macro',3,true);LOCAL=0")
        self.assertEqual((g.A, values(g.cargo_calls)), (3, ['AddAmmo:3:true:nil:bypass=1']))

    def test_outside_a_persons_ui_nothing_changes(self):
        lua, g = client()
        lua.execute("inv_session(1);A=env.AddCargo(4301ULL,'ore',10,true)")
        self.assertEqual((g.A, lua.eval('I.virtual')), (10, 0))
        self.assertFalse(values(g.cargo_calls)[0].endswith('bypass=1'))

    def test_an_error_in_x4s_function_closes_the_bypass(self):
        lua, g = client()
        lua.execute("inv_session(1);env.X4LAN_UI_INVENTORY_STOCK.AddCargo=function() error('boom') end;LOCAL=1;OK=pcall(env.AddCargo,1,'ore',1,true);LOCAL=0")
        self.assertEqual((g.OK, g.BYPASS), (False, 0))


class KnownTests(unittest.TestCase):
    """The encyclopedia: what one player has seen is an event once and known to all."""
    def test_a_new_entry_is_one_event_once_and_nothing_runs_here(self):
        lua, g = client()
        events = click(lua, g, 2, "env.AddKnownItem('wares','energycells');env.AddKnownItem('wares','energycells');env.AddKnownItem('shiptypes_s','ship_x_macro')")
        self.assertEqual([hex_[:2] for _, hex_ in events], ['0a', '0a'])
        self.assertEqual((values(g.known_items), values(g.notices)), ([], []))
        for own in (1, 2):
            lua.execute('known_items={}')
            receipt = apply(lua, g, 2, events, own=own)
            self.assertEqual(values(g.known_items), ['wares/energycells', 'shiptypes_s/ship_x_macro'])
            self.assertNotIn('UIINV_1', receipt)                     # no receipt row and no log row per entry
        self.assertFalse(any('UI_INVENTORY_APPLIED' in row for row in values(g.logs)))

    def test_outside_a_persons_ui_the_stock_function_runs(self):
        lua, g = client()
        lua.execute("inv_session(1);env.AddKnownItem('wares','ore')")
        self.assertEqual((values(g.known_items), values(g.pushed)), (['wares/ore'], []))

    def test_a_refused_push_is_quiet_and_tried_again(self):
        lua, g = client()
        lua.execute("PUSH_RESULT=0")
        click(lua, g, 1, "env.AddKnownItem('wares','ore')")
        self.assertEqual((values(g.notices), lua.eval('I.known')), ([], 0))
        lua.execute("PUSH_RESULT=1;pushed={};LOCAL=1;env.AddKnownItem('wares','ore');env.AddKnownItem('wares','ore');LOCAL=0")
        self.assertEqual((len(values(g.pushed)), lua.eval('I.known')), (1, 1))

    def test_odd_calls_and_a_slot_without_its_scope_send_nothing(self):
        lua, g = client()
        self.assertEqual(click(lua, g, 1, "env.AddKnownItem('wares');env.AddKnownItem(1,2);env.AddKnownItem('a','b','c')"), [])
        self.assertEqual(click(lua, g, 2, "env.AddKnownItem('wares','ore')", mpp=False), [])
        self.assertEqual(values(g.notices), [])
        lua.execute("E='0a'..assert(X4LAN_UI_REPLAY_CORE.encode_call({'wares',5},2,{}))")
        self.assertIn('known:skipped:arguments', apply(lua, g, 1, [(0x90, g.E)]))


class JoinTests(unittest.TestCase):
    """The gate's own player entities are in no save: their inventories ride the join package."""
    def test_the_savers_state_lists_slots_2_up_sorted_and_only_what_they_hold(self):
        lua, g = client()
        lua.execute("INV.p1={scrap=10};INV.p2={scrap=4,chip=2,gone=0};inv_session(1);T=I.join_state(2);INV.p2={};E=I.join_state(2)")
        self.assertEqual((g.T, g.E), ('2:chip*2,scrap*4', ''))
        self.assertEqual(values(g.stock_calls), [])                    # read only

    def test_the_joiner_gives_its_new_entities_what_the_session_holds(self):
        lua, g = client()
        lua.execute("INV.p2={};inv_session(3);I.join_restore('2:chip*2,scrap*4')")
        self.assertEqual(amounts(g, 'p2'), {'chip': 2, 'scrap': 4})
        self.assertEqual(sorted(values(g.stock_calls)), ['add:p2:chip:2', 'add:p2:scrap:4'])
        self.assertTrue(any('UI_INVENTORY_JOIN_GIVEN tick=0 slot=2 wares=2 given=2' in row for row in values(g.logs)))
        lua.execute("stock_calls={};I.join_give();S.frame(41)")         # once
        self.assertEqual(values(g.stock_calls), [])

    def test_a_player_entity_that_is_not_there_yet_gets_it_before_the_first_event(self):
        lua, g = client()
        lua.execute("INV.p2={scrap=5,chip=2}")
        events = click(lua, g, 2, "CRAFT={'gadget',1};menu('PlayerInfoMenu').buttonInventoryCraft()")
        lua.execute("INV.p2={};NO_PLAYER=true;stock_calls={};inv_session(3);I.join_restore('2:chip*2,scrap*5')")
        self.assertEqual(values(g.stock_calls), [])                    # the joiner has no entity of slot 2 yet
        lua.execute("NO_PLAYER=nil;DUE={[50]='2:144:%s'};RECEIPT=S.frame(50)" % events[0][1])
        self.assertEqual(values(g.stock_calls)[:2] if values(g.stock_calls)[0].startswith('add:p2:chip') else values(g.stock_calls)[:2][::-1],
                         ['add:p2:chip:2', 'add:p2:scrap:5'])
        self.assertIn('2:craft:ran:gadget=1', g.RECEIPT)                # the craft found the caught-up inventory
        self.assertEqual(amounts(g, 'p2'), {'scrap': 3, 'chip': 1, 'gadget': 1})

    def test_an_empty_state_and_what_the_entity_already_holds(self):
        lua, g = client()
        lua.execute("inv_session(3);I.join_restore('');INV.p2={scrap=1};I.join_restore('2:scrap*4')")
        self.assertEqual((amounts(g, 'p2'), values(g.stock_calls)), ({'scrap': 4}, ['add:p2:scrap:3']))

    def test_a_failing_catch_up_is_a_row_and_does_not_break_the_frame(self):
        lua, g = client()
        lua.execute("inv_session(3);env.X4LAN_UI_INVENTORY_STOCK.AddInventory=function() error('refused entity') end;"
                    "OK=pcall(I.join_restore,'2:chip*2');S.frame(41)")
        self.assertTrue(g.OK)
        self.assertTrue(any('UI_INVENTORY_JOIN_GIVEN' in row and 'error=' in row for row in values(g.logs)))

    def test_the_test_ops_show_the_state_and_run_the_catch_up_on_every_client(self):
        lua, g = client()
        lua.execute('''INV.p2={scrap=4};tools={gate=gate,C=C,env=env,own_slot=1}
          plan=X4LAN_UI_REPLAY_CORE.script({{5,'inv_join_state',2},{6,'inv_join_give','2:chip*2,scrap*6'},{7,'inv_join_state',2}},40,tools)
          inv_session(1,{script=plan});S.frame(45);S.frame(46);S.frame(47)''')
        rows = [row for row in values(g.logs) if 'UI_REPLAY_SCRIPT' in row]
        self.assertIn('state=2:scrap*4', rows[0])
        self.assertIn('state=2:chip*2,scrap*6', rows[2])
        self.assertEqual(values(g.pushed), [])
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL.inv_join_give and X4LAN_UI_REPLAY_CORE.CANONICAL.inv_join_state'))

    def test_the_probe_registers_the_join_state(self):
        probe = (DEV/'native_gate_probe.lua').read_text(encoding='utf-8')
        self.assertIn("X4LAN_JOIN_CORE.provide('inventory',function() return S.inventory.join_state(#cfg.native_gate_pilot_slots) end,", probe)


class ExportTests(unittest.TestCase):
    """The stock buttons whose changes are FFI exports: the native half records them while the click's scope is open."""
    def test_a_trader_on_the_players_own_station_takes_the_price_from_the_account(self):
        lua, g = client()
        lua.execute('''menu('InventoryTraderMenu').buttonConfirm=function()
            C.AddPlayerMoney(-120*100)
            env.TransferInventoryToPlayer('medkit',2,300ULL);env.SetNPCBlackboard(300ULL,'$TradeDone',true) end''')
        events = click(lua, g, 2, "menu('InventoryTraderMenu').buttonConfirm()")
        self.assertEqual(([op for op, _ in events], values(g.exports), values(g.stock_calls)), ([0x90], [], []))
        apply(lua, g, 2, events, trade=True)
        self.assertEqual(values(g.exports), ['AddPlayerMoney:-12000@player'])
        self.assertEqual(values(g.stock_calls), ['take:p2:medkit:2'])
        self.assertEqual(dict(g.MONEY)['player'], 880)

    def test_blueprints_are_one_event_checked_and_run_whole(self):
        lua, g = client()
        events = click(lua, g, 2, "BUY={price=300,blueprints={'ship_a','module_b'}};menu('BlueprintOrLicenceTraderMenu').buttonConfirm()")
        self.assertEqual([(op, hex_[:2]) for op, hex_ in events], [(0x90, '04')])
        self.assertEqual((values(g.learned), values(g.stock_calls), dict(g.MONEY)['player']), ([], [], 1000))
        for own in (1, 2):
            lua.execute("learned={};stock_calls={};MONEY.player=1000;MONEY.station=0")
            receipt = apply(lua, g, 2, events, own=own)
            self.assertEqual((values(g.learned), values(g.stock_calls)), (['ship_a', 'module_b'], ['pay:player:300']))
            self.assertIn('|UIINV_1:2:purchase:ran:blueprints=2 money=300', receipt)
            self.assertEqual(values(g.entered), [])                  # the player faction's library: no slot scope
        lua.execute("learned={};stock_calls={};MONEY.player=299")
        self.assertIn('purchase:skipped:money', apply(lua, g, 2, events))
        self.assertEqual((values(g.learned), values(g.stock_calls)), ([], []))

    def test_licences_and_the_own_station_price(self):
        lua, g = client()
        events = click(lua, g, 1, "BUY={price=50,own=true,licences={{'police','argon'},{'capitalship','argon'}}};menu('BlueprintOrLicenceTraderMenu').buttonConfirm()")
        self.assertEqual(len(events), 1)
        receipt = apply(lua, g, 1, events)
        self.assertEqual(values(g.licences), ['player:police:argon@player', 'player:capitalship:argon@player'])
        self.assertEqual((values(g.exports), dict(g.MONEY)['player']), (['AddPlayerMoney:-5000@player'], 950))
        self.assertIn('purchase:ran:licences=2 money=50', receipt)

    def test_a_click_without_the_money_is_no_event(self):
        lua, g = client()
        self.assertEqual(click(lua, g, 1, "BUY={price=5000,blueprints={'ship_a'}};menu('BlueprintOrLicenceTraderMenu').buttonConfirm()"), [])

    def test_research_and_terraforming_are_the_recorded_export(self):
        lua, g = client()
        cases = (("menu('ResearchMenu').buttonStartResearch('research_x')", '05', 'StartResearch:research_x:500@player', 'research:ran:research_x'),
                 ("menu('ResearchMenu').buttonCancelResearch('research_x')", '06', 'ClearProductionItems:500@player', 'research_cancel:ran:cancelled'),
                 ("menu('TerraformingMenu').buttonStartProject('project_y')", '07', 'StartTerraformingProject:600:project_y@player', 'terraform:ran:project_y'),
                 ("menu('TerraformingMenu').buttonAbortProject()", '08', 'AbortActiveTerraformingProject:600@player', 'terraform_abort:ran:aborted'))
        for code, kind, call, row in cases:
            lua.execute('pushed={};exports={}')
            events = click(lua, g, 2, code)
            self.assertEqual(([(op, hex_[:2]) for op, hex_ in events], values(g.exports)), ([(0x90, kind)], []), code)
            receipt = apply(lua, g, 2, events)
            self.assertEqual(values(g.exports), [call])
            self.assertIn('|UIINV_1:2:' + row, receipt)

    def test_the_checks_at_the_apply(self):
        lua, g = client()
        events = click(lua, g, 1, "menu('TerraformingMenu').buttonStartProject('project_y')")
        lua.execute('CAN_START=false;exports={}')
        self.assertIn('terraform:skipped:cannot_start', apply(lua, g, 1, events))
        lua.execute('pushed={};CAN_START=nil')
        events = click(lua, g, 1, "menu('TerraformingMenu').buttonAbortProject()")
        lua.execute('CAN_ABORT=false')
        self.assertIn('terraform_abort:skipped:cannot_abort', apply(lua, g, 1, events))
        # a research module that is not the player's
        lua.execute('''pushed={};K=X4LAN_UI_REPLAY_CORE
          ctx={ref=function(u) return gate.ui_replay_ref((tostring(u):gsub('ULL$',''))) end}
          E='05'..assert(K.encode_call({'research_x',300ULL},2,ctx))''')
        self.assertIn('research:skipped:not_owner', apply(lua, g, 1, [(0x90, g.E)]))
        self.assertEqual(values(g.exports), [])

    def test_factions_mode_keeps_them_for_slot_1(self):
        lua, g = client(factions=True)
        self.assertEqual(click(lua, g, 2, "menu('ResearchMenu').buttonStartResearch('research_x')", factions=True, trade=True), [])
        self.assertTrue(any('ResearchMenu.buttonStartResearch why=factions' in row for row in values(g.logs)))
        events = click(lua, g, 1, "menu('ResearchMenu').buttonStartResearch('research_x')", factions=True, trade=True)
        self.assertEqual(len(events), 1)
        self.assertIn('research:skipped:factions', apply(lua, g, 2, events, factions=True, trade=True))   # sent as slot 2's anyway
        self.assertIn('research:ran:research_x', apply(lua, g, 1, events, factions=True, trade=True))

    def test_a_record_the_native_half_lost_refuses_the_click(self):
        lua, g = client()
        lua.execute('EXPORT_LOST=true')
        self.assertEqual(click(lua, g, 1, "menu('ResearchMenu').buttonStartResearch('research_x')"), [])
        self.assertTrue(any('why=exports_lost' in row for row in values(g.logs)))

    def test_the_recorded_exports_mirror_the_native_table(self):
        lua, _ = client()
        native = dict(re.findall(r'\{"(\w+)","(\w+)"\}', INVENTORY_H[INVENTORY_H.index('uiinv_calls[UIINV_CALLS]={'):INVENTORY_H.index('#define UIINV_RECORD_MAX')]))
        self.assertEqual(dict(lua.eval('X4LAN_UI_INVENTORY_CORE.FFI').items()), native)
        self.assertEqual(len(native), int(re.search(r'#define UIINV_CALLS (\d+)u', INVENTORY_H).group(1)))
        probe = (DEV/'native_gate_probe.lua').read_text(encoding='utf-8')
        for name in native:
            self.assertRegex(probe, r"'(void|bool) %s\(" % name)


class DropTests(unittest.TestCase):
    """Kind 3 is captured natively (the guard's hook); here its bytes as the native half writes them, and the apply."""
    @staticmethod
    def event(lua, lockbox, *wares):
        """The native half's event: kind 3, then the codec's count, lockbox (nil or a string) and ware, amount pairs."""
        args = ('nil' if lockbox is None else repr(lockbox),) + tuple(repr(w) for w in wares)
        return lua.eval(f"'03'..assert(X4LAN_UI_REPLAY_CORE.encode_call({{{','.join(args)}}},{len(args)},{{}}))")

    def test_the_native_bytes_are_the_codecs(self):
        lua, _ = client()
        hex_ = self.event(lua, None, 'scrap', 2)
        number = hex_[-16:]
        self.assertEqual(hex_, '03' + '03' + '00' + '0405' + 'scrap'.encode().hex() + '03' + number)
        self.assertEqual(number, '0000000000000040')                 # the double 2.0 as the C half writes it
        for tag, name in ((0, 'NIL'), (3, 'NUMBER'), (4, 'STRING')):
            self.assertIn(f'#define UIINV_TAG_{name} {tag}u', INVENTORY_H)
        self.assertIn('#define UIINV_WARE_MAX 64u', INVENTORY_H)

    def test_the_apply_drops_from_the_slots_own_inventory(self):
        lua, g = client()
        lua.execute("INV.p2={scrap=4,chip=2}")
        for own in (1, 2):
            lua.execute("INV.p1={scrap=10};INV.p2={scrap=4,chip=2};drops={};entered={}")
            receipt = apply(lua, g, 2, [(0x90, self.event(lua, 'lockbox_macro', 'scrap', 3, 'chip', 2))], own=own)
            self.assertEqual(values(g.drops), ['4302@p2:lockbox_macro:scrap:3,chip:2'])
            self.assertEqual((amounts(g, 'p1'), amounts(g, 'p2')), ({'scrap': 10}, {'scrap': 1}))
            self.assertIn('|UIINV_1:2:drop:ran:scrap=1,chip=0', receipt)
        lua.execute("drops={}")
        apply(lua, g, 1, [(0x90, self.event(lua, None, 'scrap', 1))])
        self.assertEqual(values(g.drops), ['4301@p1:nil:scrap:1'])

    def test_more_than_the_slot_has_and_bad_arguments_drop_nothing(self):
        lua, g = client()
        cases = ((self.event(lua, None, 'scrap', 5), 'player_stock'), (self.event(lua, None, 'scrap', 2, 'scrap', 3), 'player_stock'),
                 (self.event(lua, None, 'scrap', 0), 'arguments'), (self.event(lua, None, 'scrap'), 'arguments'),
                 (self.event(lua, 7, 'scrap', 1), 'arguments'), (self.event(lua, None, 'scrap', 1.5), 'arguments'))
        for hex_, why in cases:
            lua.execute("INV.p2={scrap=4};drops={}")
            self.assertIn('drop:skipped:' + why, apply(lua, g, 2, [(0x90, hex_)]))
            self.assertEqual(values(g.drops), [])

    def test_x4s_own_refusal_is_the_same_row_everywhere(self):
        lua, g = client()
        lua.execute("DROP_ANSWER=false")
        self.assertIn('|UIINV_1:1:drop:skipped:x4_refused:', apply(lua, g, 1, [(0x90, self.event(lua, None, 'scrap', 1))]))

    def test_the_scripted_drop_calls_the_export_as_the_menu_does(self):
        lua, g = client()
        lua.execute('''tools={gate=gate,C=C,ffi=ffi,env=env,own_slot=1}
          plan=X4LAN_UI_REPLAY_CORE.script({{5,'inv_drop',false,{'scrap',2}}},40,tools)
          inv_session(1,{script=plan});S.frame(45)''')
        self.assertEqual(values(g.drops), ['4301@p1:nil:scrap:2'])    # the mocked export stands where the guard's hook captures
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.SCOPED.inv_drop'))


class ScriptTests(unittest.TestCase):
    def test_the_scripted_ops_capture_as_the_buttons_do(self):
        lua, g = client()
        lua.execute('''tools={gate=gate,C=C,env=env,own_slot=2,saved_id=function(s) return 300ULL end}
          plan=X4LAN_UI_REPLAY_CORE.script({{5,'inv_craft','gadget',2},{6,'inv_trade','trader',120,{'medkit',2,'scrap',-3}}},40,tools)
          inv_session(2,{script=plan});LOCAL=1;S.frame(45);S.frame(46);LOCAL=0''')
        events = [(int(e[1]), e[2]) for e in values(g.pushed)]
        self.assertEqual([(op, hex_[:2]) for op, hex_ in events], [(0x90, '01'), (0x90, '02')])
        self.assertEqual(values(g.stock_calls), [])
        lua.execute("INV.p2={scrap=8,chip=2}")
        receipt = apply(lua, g, 2, events, trade=True)
        self.assertIn('2:craft:ran:gadget=2;2:trade:ran:medkit=2,scrap=1', receipt)

    def test_the_test_fixture_ops_reach_each_players_own_inventory(self):
        lua, g = client()
        lua.execute('''tools={gate=gate,C=C,env=env,own_slot=1}
          C.GetPlayerID=function() return 4242ULL end
          plan=X4LAN_UI_REPLAY_CORE.script({{5,'inv_seed',2,'gadget',3},{6,'inv_look',1,'gadget'},{6,'inv_look',2,'gadget'}},40,tools)
          inv_session(1,{script=plan});S.frame(45);S.frame(46)''')
        rows = [row for row in values(g.logs) if 'UI_REPLAY_SCRIPT' in row]
        self.assertIn('gadget=0->3', rows[0])
        self.assertIn('slot=1', rows[1])
        self.assertIn('gadget=0', rows[1])
        self.assertIn('gadget=3', rows[2])
        self.assertEqual(amounts(g, 'p2'), {'scrap': 4, 'gadget': 3})
        self.assertEqual(values(g.pushed), [])                      # canonical: every client's plan runs it, nothing is captured


class KitInventoryTests(unittest.TestCase):
    """A start's inventory for the player who took the start (the identity plan's kit.inventory): the probe's step."""
    PROBE = (DEV/'native_gate_probe.lua').read_text(encoding='utf-8')

    def client(self):
        from lupa.luajit21 import LuaRuntime
        lua = LuaRuntime(unpack_returned_tuples=True)
        block = self.PROBE[self.PROBE.index('function pilot_view.kit_inventory(tick,slot,why)'):
                           self.PROBE.index("-- A start's story for the player who took that start")]
        lua.execute('''
          pilot_view={};logs={};loud={};PLAYER='p1';INV={p1={},p2={},p3={}};scopes={}
          function log(text) logs[#logs+1]=text end
          ticklog={loud=function(kind,text,notice) loud[#loud+1]=kind..' '..text..' / '..tostring(notice) end}
          gate={mpp_scope=function(k) if NO_SCOPE then return 0 end;scopes[#scopes+1]='in'..k;PLAYER='p'..k;return 1 end,
                mpp_unscope=function(k) scopes[#scopes+1]='out'..k;PLAYER='p1';return 1 end}
          AddInventory=function(entity,ware,amount) assert(entity==nil);INV[PLAYER][ware]=(INV[PLAYER][ware] or 0)+amount end
          GetPlayerInventory=function() local out={};for w,n in pairs(INV[PLAYER]) do out[w]={amount=n} end;return out end
          cfg={native_gate_player_identity_plan={mode='coop',kits={
            {slot=2,gamestart='a',money=5,omitted={'reputation'},inventory={{ware='inv_tst',amount=2},{ware='weapon_tst_laser',amount=1}}},
            {slot=3,gamestart='b',money=5,omitted={'inventory'}}}}}
        ''' + block)
        return lua, lua.globals()

    def test_the_wares_go_to_the_slots_own_player_inside_its_scope(self):
        lua, g = self.client()
        lua.execute("pilot_view.kit_inventory(48,2,'window')")
        self.assertEqual(dict(g.INV.p2), {'inv_tst': 2, 'weapon_tst_laser': 1})
        self.assertEqual((dict(g.INV.p1), values(g.scopes), g.PLAYER), ({}, ['in2', 'out2'], 'p1'))
        self.assertEqual(values(g.logs), ['KIT_INVENTORY tick=48 slot=2 why=window wares=2 applied=1 now=inv_tst=2,weapon_tst_laser=1'])

    def test_a_kit_without_inventory_and_a_slot_without_a_kit_do_nothing(self):
        lua, g = self.client()
        lua.execute("pilot_view.kit_inventory(48,3,'window');pilot_view.kit_inventory(48,4,'window');cfg.native_gate_player_identity_plan=nil;pilot_view.kit_inventory(48,2,'window')")
        self.assertEqual((values(g.logs), values(g.loud), values(g.scopes)), ([], [], []))

    def test_without_the_slots_scope_nothing_is_added_and_the_row_is_loud(self):
        lua, g = self.client()
        lua.execute("NO_SCOPE=true;pilot_view.kit_inventory(660,2,'handover')")
        self.assertEqual(dict(g.INV.p2), {})
        self.assertEqual(len(values(g.loud)), 1)
        self.assertIn('KIT_INVENTORY_REFUSED KIT_INVENTORY tick=660 slot=2 why=handover wares=2 refused=no_scope', values(g.loud)[0])

    def test_when_the_step_runs(self):
        self.assertIn("if type(w)=='table' and tick==w[1]+8 then", self.PROBE)                       # slots there from the start
        self.assertIn("for k=2,PILOT_SLOTS_MAX do if not open[k] then pilot_view.kit_inventory(tick,k,'window') end end", self.PROBE)
        self.assertIn('kit_inventory_at[h+12]=k', self.PROBE)                                            # a slot that joins
        self.assertIn("pilot_view.kit_inventory(tick,k,'handover')", self.PROBE)


PROBE_GUARD = uirep.PROBE_MOCK + r"""
  gate.ui_guard_pins=function() return 1 end
  gate.ui_guard=function(mask) ui_calls[#ui_calls+1]='guard:'..mask;return 1,280 end
  gate.ui_guard_refusals=function() return nil end
  gate.ui_guard_status=function() return 'ui_guard=live' end
  gate.runtime_ids_probe=function(on) end
  gate.ui_replay_push=function(op,hex) return 1 end
  gate.ui_replay_local=function() return 0 end
  gate.ui_replay_saved=function() return nil end
  gate.ui_inventory=function() ui_calls[#ui_calls+1]='inventory';if IARM then return 0,IARM end;return 1 end
  gate.ui_inventory_status=function() return 'ui_inventory=armed' end
  gate.ui_inventory_scope=function(on) return on==1 and 1 or '' end
"""
PROBE_INVENTORY = PROBE_GUARD + CORE


class ProbeTests(unittest.TestCase):
    """The real probe on the stock tests' mocked gate: the family arms with its core, and its frames run."""
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def clients(self, families, prelude=None, **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=families, native_gate_ui_guard='all')
        cfg.update(extra)
        return [self.stock.StockClient(k, slots_all=True, prelude=PROBE_INVENTORY if prelude is None else prelude, **cfg) for k in (1, 2)]

    def test_the_probe_arms_the_family_and_runs_its_frames(self):
        clients = self.clients(['travel', 'inventory'])
        self.stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            self.assertIn('arm:524289', c.values('ui_calls'))
            self.assertIn('inventory', c.values('ui_calls'))
            self.assertTrue(any(f'UI_INVENTORY_ARMED slot={c.slot} factions=false mpp=false' in line for line in c.values('logs')))

    def test_without_the_family_nothing_of_it_arms(self):
        for c in self.clients(['travel']):
            self.assertEqual(c.errors(), [])
            self.assertFalse(any('UI_INVENTORY' in line for line in c.values('logs')))

    def test_the_family_without_its_core_file_refuses(self):
        c = self.clients(['travel', 'inventory'], prelude=PROBE_GUARD)[0]
        self.assertTrue(any('ui inventory refused: core missing' in e for e in c.errors()), c.errors())

    def test_the_family_needs_the_guard_and_its_claim(self):
        c = self.clients(['travel', 'inventory'], native_gate_ui_guard=None)[0]
        self.assertTrue(any('ui inventory refused: needs native_gate_ui_guard' in e for e in c.errors()), c.errors())
        c = self.clients(['travel', 'inventory'], prelude=PROBE_INVENTORY + "IARM='ui_inventory_claim_taken'")[0]
        self.assertTrue(any('ui inventory refused: ui_inventory_claim_taken' in e for e in c.errors()), c.errors())


if __name__ == '__main__':
    unittest.main()
