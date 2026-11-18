"""Action tests (tools/dev/action_tests.py) and the script ops they use (native_gate_ui_replay_core.lua: chain, cargo,
wallet, trade_look).

Synthetic: the Lua runs under LuaJIT with a mocked world, the report reads made-up log rows. What the real game does
with a scenario is shown by a live run of `action_tests.py run`, not here."""
import re
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import action_tests  # noqa: E402
import station_walk_test as rig  # noqa: E402

# A world for the chain: the ship docks at the third dock_state asked, cargo is what CARGO holds, the trade list has
# one sell and one buy offer for ore, AddTradeToShipQueue moves the cargo.
WORLD = r'''
  CARGO={};ASKED=0;DOCKED=false;calls={}
  tools={gate=gate,C={},env={},own_slot=2,dock_range=9000,
    slot_id=function(s) return 'ship'..s end,slot_lua=function(s) return 'ship'..s end,
    saved_id=function(s) return 'id'..s end,saved_lua=function(s) return 'lua'..s end,
    dock_state=function() ASKED=ASKED+1;if ASKED>=3 then DOCKED=true end;return {docked=DOCKED,distance=5000} end,
    assigned_dock=function() return nil end,
    foot_where=function() return 'docked='..tostring(DOCKED) end}
  tools.C.RequestDockAt=function() return false end
  tools.C.UndockPlayerShip=function() DOCKED=false;return ffi.new('char[8]','ok') end
  tools.C.GetPlayerID=function() return 7 end
  tools.env.StartAutoPilot=function(x) calls[#calls+1]='autopilot:'..tostring(x) end
  tools.env.GetComponentData=function(ship,key) calls[#calls+1]='data:'..tostring(ship)..':'..key;return CARGO end
  tools.env.GetPlayerMoney=function() return 123 end
  tools.env.GetTradeList=function(station) return {{id='o1',ware='ore',isselloffer=true,price=50,amount=900},{id='o2',ware='ore',isbuyoffer=true,price=40,amount=10},
    {id='o3',ware='ice',isselloffer=true,price=1,amount=1}} end
  tools.env.AddTradeToShipQueue=function(offer,ship,amount,immediate) calls[#calls+1]='trade:'..offer..':'..tostring(ship)..':'..amount..':'..tostring(immediate)
    CARGO.ore=(CARGO.ore or 0)+(offer=='o1' and amount or -amount);if CARGO.ore==0 then CARGO.ore=nil end end
  tools.env.SignalObject=function(who,what,ship) calls[#calls+1]='signal:'..what..':'..tostring(ship) end
  tools.env.AddUITriggeredEvent=function(screen,control,value) calls[#calls+1]=screen..':'..control..':'..tostring(value.credits) end
  function run(plan,ticks)
    local by=X4LAN_UI_REPLAY_CORE.script(plan,0,tools)
    local rows={}
    for t=0,ticks do
      for _,e in ipairs(by[t] or {}) do
        local ok,result=pcall(e.call)
        if not (e.quiet and ok and result==nil) then rows[#rows+1]=t..' '..tostring(e.op)..' '..tostring(ok)..' '..tostring(result) end
      end
    end
    return rows
  end
'''


def values(table):
    return list(table.values()) if table is not None else []


class ChainTests(unittest.TestCase):
    def client(self):
        lua, g = uirep.client()
        lua.execute('ffi=require("ffi")')
        lua.execute(WORLD)
        return lua, g

    def test_steps_run_one_after_the_other_each_when_the_last_has_ended(self):
        lua, g = self.client()
        lua.execute('''rows=run({{5,'chain',400,{
          {'dock','until','^docked','dock_station','aa',300},
          {'rest','wait',3},
          {'look','op','trade_look','aa','ore'},
          {'buy','op','trade_buy','aa','ore',20,2,1},
          {'bought','poll','^amount=20 ',2,40,'cargo','ore'},
          {'undock','op','undock'},
          {'away','poll','docked=false',5,50,'foot_where'},
          {'sell','op','trade_sell','aa','ore',20,2,1},
          {'sold','poll','^amount=0 ',2,40,'cargo','ore'}}}},420)''')
        rows = values(g.rows)
        text = '\n'.join(rows)
        self.assertIn('5 chain true steps=9', rows[0])
        self.assertIn('step=dock n=1 at=1 ok=true autopilot', text)
        self.assertIn('step=dock n=1 at=4 ok=true docked after=3', text)          # the third dock_state: docked
        self.assertIn('step=look n=3 at=9 ok=true offers=2 sell=50/900 buy=40/10 money=123', text)
        self.assertIn('step=bought n=5 at=11 ok=true amount=20 ware=ore wares=1 ore=20 money=123', text)
        self.assertIn('step=sold n=9 ', text)
        self.assertIn('amount=0 ware=ore wares=0 money=123', text)
        self.assertTrue(rows[-1].endswith('chain_end ok=true steps=9/9'), rows[-1])
        calls = values(g.calls)
        self.assertIn('trade:o1:ship2:20:true', calls)                               # the station's sell offer, immediate
        self.assertIn('trade:o2:ship2:20:true', calls)
        self.assertEqual(calls.count('signal:docked_player_trade_added:ship2'), 2)
        self.assertLess(calls.index('trade:o1:ship2:20:true'), calls.index('trade:o2:ship2:20:true'))
        # ...and the report's reader takes the rows apart
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp)/'c.log'
            log.write_text('\n'.join('[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%s op=%s ok=%s result=%s scope=0' % tuple(r.split(' ', 3)) for r in rows))
            c = action_tests.parse(log)
        self.assertEqual(c['end'], {'ok': True, 'done': 9, 'of': 9, 'why': None, 'tick': c['end']['tick']})
        self.assertEqual(action_tests.last_answer(c, 'dock', 'docked')['text'], 'docked after=3 asked=0')
        self.assertEqual(rig.field(action_tests.last_answer(c, 'bought')['text'], 'amount'), '20')

    def test_a_step_that_never_answers_ends_the_chain_and_says_which(self):
        lua, g = self.client()
        lua.execute('''rows=run({{0,'chain',100,{{'a','op','cargo','ore'},{'never','poll','^amount=5 ',2,10,'cargo','ore'},{'b','op','cargo'}}}},120)''')
        rows = values(g.rows)
        self.assertTrue(rows[-1].endswith('chain_end ok=false steps=1/3 why=never:never'), rows[-1])
        self.assertFalse(any('step=b ' in r for r in rows))
        # ...unless the step is a `try`: then the miss is a row and the chain goes on
        lua.execute('''rows=run({{0,'chain',100,{{'never','try','^amount=5 ',2,10,'cargo','ore'},{'b','op','cargo'}}}},120)''')
        text = '\n'.join(values(g.rows))
        self.assertIn('step=never n=1 at=11 ok=true gave_up', text)
        self.assertIn('step=b n=2 ', text)
        self.assertTrue(values(g.rows)[-1].endswith('chain_end ok=true steps=2/2'), values(g.rows)[-1])
        lua.execute('''rows=run({{0,'chain',6,{{'long','wait',50},{'b','op','cargo'}}}},120)''')
        self.assertTrue(values(g.rows)[-1].endswith('chain_end ok=false steps=0/2 why=long:limit'), values(g.rows)[-1])
        lua.execute('''tools.env.GetComponentData=function() error('boom') end
          rows=run({{0,'chain',20,{{'bad','op','cargo'},{'b','op','cargo'}}}},30)''')
        self.assertIn('step=bad n=1 at=1 ok=false', values(g.rows)[-1])
        self.assertTrue(values(g.rows)[-1].endswith('chain_end ok=false steps=0/2 why=bad:error'), values(g.rows)[-1])

    def test_the_menu_ops_press_the_stock_handlers_and_meet_the_block(self):
        lua, g = self.client()
        lua.execute('''
          pressed={}
          MAP={name='MapMenu',mode='tradecontext',contextMenuMode='trade',contextMenuData={component=300,currentShip=200,immediate=true,orders={}}}
          MAP.orderAmountHelper=function(sell,buy,value) if value<0 then MAP.contextMenuData.orders[sell]=value else MAP.contextMenuData.orders[buy]=value end end
          MAP.buttonConfirmTrade=function() pressed[#pressed+1]='confirm' end
          DOCK={name='DockedMenu',shown=true,buttonTrade=function(exchange) pressed[#pressed+1]='trade:'..tostring(exchange) end,
            buttonUndock=function() return 0 end}          -- as the order block's refusal answers
          tools.menu=function(name) return ({MapMenu=MAP,DockedMenu=DOCK})[name] end
          tools.menu_entry=function(menu,name) return name=='buttonConfirmTrade' and 'blocked' or 'open why=shared' end
          tools.env.ConvertStringToLuaID=function(s) return 'lua'..s end
          rows=run({{0,'menu_entry','MapMenu','buttonConfirmTrade'},{1,'menu_look','DockedMenu'},{2,'menu_call','DockedMenu','buttonTrade',false},
            {3,'menu_look','MapMenu'},{4,'trade_menu_amount','ore',20},{5,'menu_look','MapMenu'},{6,'menu_call','MapMenu','buttonConfirmTrade'},
            {7,'trade_menu_amount','ore',-5},{8,'menu_look','MapMenu'},{9,'menu_call','DockedMenu','buttonUndock'},{10,'menu_call','NoMenu','x'},
            {11,'menu_call','MapMenu','nothing'},{12,'trade_menu_amount','ice',-1}},13)''')
        rows = values(g.rows)
        self.assertEqual(rows[0], '0 menu_entry true blocked entry=MapMenu.buttonConfirmTrade')
        self.assertEqual(rows[1], '1 menu_look true shown=true mode=nil context=nil')
        self.assertEqual(rows[2], '2 menu_call true called')
        self.assertEqual(rows[3], '3 menu_look true shown=false mode=tradecontext context=trade ship=200 immediate=true amounts=-')
        self.assertEqual(rows[5], '5 menu_look true shown=false mode=tradecontext context=trade ship=200 immediate=true amounts=-20')
        self.assertEqual(rows[6], '6 menu_call true called')
        self.assertEqual(rows[8], '8 menu_look true shown=false mode=tradecontext context=trade ship=200 immediate=true amounts=-20,5')
        self.assertEqual(rows[9], '9 menu_call true blocked answer=0')
        self.assertEqual(rows[10:], ['10 menu_call true no_menu', '11 menu_call true no_function', '12 trade_menu_amount true no_buy_offer'])
        self.assertEqual(values(g.pressed), ['trade:false', 'confirm'])
        # the trade menu: by the dock menu's button while that menu is on screen, else opened as the button opens it
        lua.execute('''opened={}
          tools.env.OpenMenu=function(name,param) opened[#opened+1]=name..':'..tostring(param[6])..':'..tostring(param[7][1]) end
          rows=run({{0,'trade_menu_open','aa'}},1);DOCK.shown=false;more=run({{0,'trade_menu_open','aa'}},1)''')
        self.assertEqual(values(g.rows), ['0 trade_menu_open true via=dock_menu'])
        self.assertEqual(values(g.more), ['0 trade_menu_open true via=open_menu dock_menu=false'])
        self.assertEqual(values(g.opened), ['MapMenu:tradecontext:luaaa'])
        self.assertEqual(values(g.pressed)[-1], 'trade:false')
        self.assertEqual(lua.eval("MAP.contextMenuData.orders.o1"), -20)          # the station's sell offer: the ship buys
        self.assertEqual(lua.eval("MAP.contextMenuData.orders.o2"), 5)

    def test_the_ship_ops_make_the_crew_and_order_calls_of_the_menus(self):
        lua, g = self.client()
        lua.execute('''
          ffi.cdef('typedef uint64_t NPCSeed; typedef uint64_t UniverseID; typedef struct { UniverseID entity; UniverseID personcontrollable; NPCSeed personseed; } GenericActor;')
          made={}
          PEOPLE={{seed=11ULL,role='service'},{seed=12ULL,role='marine'}}
          tools.ship_id=function(ref) return 500ULL end
          tools.ship_people=function(ref,role) local out={};for _,x in ipairs(PEOPLE) do if not role or x.role==role then out[#out+1]=x end end;return out end
          tools.ship_look=function(ref) return 'state=ok ship='..tostring(ref) end
          tools.C.RemovePerson=function(ship,seed) made[#made+1]='remove:'..tostring(ship)..':'..tostring(seed) end
          tools.C.AssignHiredActor=function(actor,ship,post,role,check) made[#made+1]='assign:'..tostring(actor.personseed)..':'..tostring(ship)..':'..post..':'..tostring(check);return ffi.new('char[4]','') end
          tools.C.CreateOrder=function(id,def,default) made[#made+1]='order:'..tostring(id)..':'..def..':'..tostring(default);return 3 end
          tools.C.EnableOrder=function(id,idx) made[#made+1]='enable:'..idx end
          tools.C.ResetOrderLoop=function(id) made[#made+1]='resetloop' end
          tools.C.EnablePlannedDefaultOrder=function(id,check) made[#made+1]='planned:'..tostring(check);return true end
          tools.C.GetObjectPositionInSector=function(id) return {x=100,y=0,z=-50} end
          tools.env.SetOrderParam=function(ship,idx,param,_,value) made[#made+1]='param:'..tostring(idx)..':'..param..':'..tostring(value) end
          tools.env.CreateOrder=function(ship,def,params) made[#made+1]='lua:'..tostring(ship)..':'..def;return 4 end
          tools.env.GetComponentData=function(id,key) return key=='assignedpilot' and 'pilot77' or nil end
          tools.env.ConvertIDTo64Bit=function(x) return 77ULL end
          tools.env.ConvertStringTo64Bit=function(s) return s end
          tools.env.ConvertStringToLuaID=function(s) return 'lua'..s end
          tools.env.SignalObject=function(who,what) made[#made+1]='signal:'..tostring(who)..':'..what end
          rows=run({{0,'ship_look','aa'},{1,'crew_fire','aa','marine'},{2,'crew_promote','aa'},{3,'captain_fire','aa'},{4,'explore','aa','s1'},
            {5,'q_patrol','aa','s1'},{6,'behaviour','aa','Patrol','s1'},{7,'move_by','aa','s1',1000,0,500},{8,'ship_arrived','aa',2000},
            {9,'crew_fire','aa','pilot'},{10,'behaviour_confirm','aa'}},11)''')
        rows = values(g.rows)
        self.assertEqual(rows[0], '0 ship_look true state=ok ship=aa')
        self.assertEqual(rows[1], '1 crew_fire true fired=12 role=marine')
        self.assertEqual(rows[2], '2 crew_promote true promoted=11 role=service answer=-')
        self.assertEqual(rows[3], '3 captain_fire true dismissed=77')
        self.assertEqual(rows[5], '5 q_patrol true 3')
        self.assertEqual(rows[6], '6 behaviour true 3')
        self.assertEqual(rows[10], '10 behaviour_confirm true true')
        self.assertEqual(rows[7], '7 move_by true order=4 from=100/0/-50 to=1100/0/450')
        self.assertEqual(rows[8], '8 ship_arrived true near=true distance=1118')
        self.assertEqual(rows[9], '9 crew_fire true nobody')
        made = values(g.made)
        for call in ('remove:500ULL:12ULL', 'assign:11ULL:500ULL:aipilot:false', 'signal:77ULL:npc__control_dismissed', 'lua:luaaa:Explore',
                     'order:idaa:Patrol:false', 'param:3:1:luas1', 'enable:3', 'order:idaa:Patrol:true', 'param:planneddefault:1:luas1', 'resetloop',
                     'planned:false', 'lua:luaaa:MoveWait'):
            self.assertIn(call, made)

    def test_a_fixture_is_no_step_and_the_wallet_is_a_fixture(self):
        lua, g = self.client()
        with self.assertRaises(Exception):
            lua.execute('''run({{0,'chain',10,{{'kit','op','dock_kit',1}}}},5)''')
        with self.assertRaises(Exception):
            lua.execute('''run({{0,'chain',10,{{'x','sometimes','cargo'}}}},5)''')
        lua.execute('''rows=run({{3,'wallet',50000}},5)''')
        self.assertEqual(values(g.calls)[-1], 'X4LAN_UiReplay:Wallet:50000')
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL.wallet'))
        cue = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
        self.assertIn('control="\'Wallet\'"', cue)
        self.assertIn('<reward_player money="(event.param3.$credits)Cr"/>', cue)


STOCK_UI = ROOT.parent/'x4-re/gamelua/08/ui/addons'
MENU_FILES = {'InteractMenu': 'ego_interactmenu/menu_interactmenu.lua', 'MapMenu': 'ego_detailmonitor/menu_map.lua'}


class OpenEntryTests(unittest.TestCase):
    """The stock menu entries a session leaves open to a person (K.UNBLOCKED): read against the stock menus."""

    @unittest.skipUnless(STOCK_UI.is_dir(), 'x4-re stock UI scripts not at hand')
    def test_every_open_order_entry_makes_only_calls_the_replay_captures(self):
        lua, _ = uirep.client()
        entries = [e for family in lua.eval('X4LAN_UI_REPLAY_CORE.UNBLOCKED').values() for e in family.values()]
        # what an order entry may call: reads, and the calls the orders family captures
        allowed = {'IsOrderSelectableFor', 'IsComponentClass', 'GetWareCapacity', 'RemoveAllOrders2', 'CreateOrder'}
        seen = 0
        for entry in entries:
            menu, name = entry.split('.')
            if menu not in MENU_FILES:
                continue          # a hotkey
            text = (STOCK_UI/MENU_FILES[menu]).read_text(encoding='utf-8', errors='replace')
            body = re.search(r'\nfunction menu\.' + name + r'\((.*?)\)\n(.*?)\nend\n', text, re.S)
            self.assertIsNotNone(body, entry)
            if not name.startswith('order'):
                continue          # the queue editor's rows and the trade Confirm: their own families' tests
            calls = set(re.findall(r'\bC\.(\w+)\(', body.group(2))) | set(re.findall(r'(?<![\w.:])(CreateOrder\d?|SetOrderParam|AddTradeToShipQueue|SignalObject)\(', body.group(2)))
            self.assertLessEqual(calls, allowed, entry)
            seen += 1
        self.assertGreaterEqual(seen, 28)
        # the two functions named order... that order nothing are never blocked (the probe's ORDER_LOCAL)
        probe = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
        self.assertIn("local ORDER_LOCAL={MapMenu={orderAmountHelper=true},InteractMenu={orderIconText=true}}", probe)
        for menu, name in (('MapMenu', 'orderAmountHelper'), ('InteractMenu', 'orderIconText')):
            text = (STOCK_UI/MENU_FILES[menu]).read_text(encoding='utf-8', errors='replace')
            body = re.search(r'\nfunction menu\.' + name + r'\((.*?)\)\n(.*?)\nend\n', text, re.S).group(2)
            self.assertEqual(re.findall(r'\bC\.(\w+)\(', body), [], name)          # no engine call at all



def rows_of(steps, slot):
    """Log rows of a finished trade chain."""
    out, n = [], 0
    for name, text in steps:
        n += 1
        out.append('[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=chain ok=true result=step=%s n=%d at=%d ok=true %s scope=1' % (100 + n * 10 + slot, name, n, n * 10, text))
    out.append('[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=chain ok=true result=chain_end ok=true steps=19/19 scope=1' % (400 + slot))
    return out


class ReportTests(unittest.TestCase):
    STEPS = [('dock_buy', 'docked after=1900 asked=1'), ('at_buy', 'player=1 station=500 docked=true'),
             ('entry_trade', 'open why=shared entry=MapMenu.buttonConfirmTrade'), ('offer_buy', 'offers=1 sell=19.78/22280 money=60000'),
             ('cargo_before', 'amount=0 ware=foodrations wares=0 money=60000'), ('buy', 'lua'),
             ('bought', 'amount=20 ware=foodrations wares=1 foodrations=20 money=59604.4'), ('undock', 'ok'), ('undocked', 'player=1 station=0 docked=false'),
             ('dock_sell', 'docked after=800 asked=1'), ('at_sell', 'player=1 station=600 docked=true'), ('offer_sell', 'offers=1 buy=18.69/2562 money=59208.8'),
             ('sell', 'lua'), ('sold', 'amount=0 ware=foodrations wares=0 money=59582.6')]
    MONEY = ['UI_TRADE_MONEY tick=1 UITRADE_1:1000000,n;0;0', 'UI_TRADE_MONEY tick=21 UITRADE_1:1000000,1000000;0;0', 'UI_TRADE_MONEY tick=45 UITRADE_1:6000000,6000000;0;0',
             'UI_TRADE_MONEY tick=2100 UITRADE_1:5960440,5960440;0;0', 'UI_TRADE_MONEY tick=2130 UITRADE_1:5920880,5920880;0;0',
             'UI_TRADE_MONEY tick=3300 UITRADE_1:5958260,5958260;0;0', 'UI_TRADE_MONEY tick=3330 UITRADE_1:5995640,5995640;0;0']
    APPLIED = ['UI_REPLAY_APPLIED tick=%d slot=%d op=88 result=applied what=AddTradeToShipQueue' % x for x in ((2100, 1), (2130, 2), (3300, 1), (3330, 2))]

    def logs(self, tmp, change=None):
        paths = []
        for k in (1, 2):
            lines = rows_of(self.STEPS, k) + ['[X4LAN] NATIVE_GATE ' + x for x in self.MONEY + self.APPLIED]
            lines += ['[X4LAN] NATIVE_GATE DIGEST_PARTS tick=%d hash=%d world=aa records=bb inputs=cc rid=dd' % (t, t) for t in range(1, 6)]
            # the watch: both players' holds read on this client at the same ticks (not in the presentation scope)
            lines += ['[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=cargo ok=true result=amount=%d ware=foodrations wares=0 money=60000' % (t, n)
                      for t in (340, 640) for n in (0, 20)]
            if change:
                lines = change(k, lines)
            path = Path(tmp)/('c%d.log' % k)
            path.write_text('\n'.join(lines), encoding='utf-8')
            paths.append(path)
        return paths

    def args(self):
        t = action_tests.Trade
        return SimpleNamespace(buy_station=t.BUY, sell_station=t.SELL, ware=t.WARE, amount=t.AMOUNT, credits=t.CREDITS, by='calls')

    def failed(self, change=None):
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as tmp:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                n = action_tests.report(action_tests.Trade, self.logs(tmp, change), None, self.args())
            return n, out.getvalue()

    def test_a_clean_trade_run_passes_every_check(self):
        n, text = self.failed()
        self.assertEqual(n, 0, text)
        self.assertIn('the fixture paid 50000 Cr at tick 45; then -395.60 Cr at tick 2100, -395.60 Cr at tick 2130, +373.80 Cr at tick 3300, +373.80 Cr at tick 3330', text)
        self.assertIn('the 4 trade calls ran on both clients at the same ticks', text)

    def test_what_went_wrong_is_named(self):
        def unsold(k, lines):
            return [x for x in lines if 'step=sold ' not in x] if k == 2 else lines
        n, text = self.failed(unsold)
        self.assertEqual(n, 1, text)
        self.assertIn('sold      FAIL player 2', text)

        def account(k, lines):
            return [x.replace('5958260,5958260', '5958261,5958261') for x in lines] if k == 2 else lines
        n, text = self.failed(account)
        self.assertEqual(n, 1, text)
        self.assertIn('account   FAIL', text)

        def blocked(k, lines):          # the calls trade, but a person's click on Confirm would be refused
            return [x.replace('ok=true open why=shared entry=', 'ok=true blocked entry=') for x in lines]
        n, text = self.failed(blocked)
        self.assertEqual(n, 2, text)
        import contextlib
        import io
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            self.assertEqual(action_tests.main(['plan', 'trade']), 0)
        self.assertIn("'trade_menu_open'", printed.getvalue())          # the default is the menu-driven run
        self.assertIn('offered   FAIL player 1: the trade menu', text)
        self.assertIn('REFUSED to a person in this session (blocked entry=MapMenu.buttonConfirmTrade)', text)

        def refused(k, lines):          # what a client was refused is named (a note: the checks say what it cost)
            return lines + ['[X4LAN] NATIVE_GATE UI_REPLAY_REFUSED what=MapMenu.buttonHire why=slot'] * 2 if k == 2 else lines
        n, text = self.failed(refused)
        self.assertEqual(n, 0, text)
        self.assertIn("refused   note player 2's client was refused: MapMenu.buttonHire (slot) x2", text)

        def reading(k, lines):
            return [x.replace('tick=640 op=cargo ok=true result=amount=20', 'tick=640 op=cargo ok=true result=amount=19') for x in lines] if k == 2 else lines
        n, text = self.failed(reading)
        self.assertEqual(n, 1, text)
        self.assertIn('readings  FAIL 2 readings differ, the first at tick 640', text)

        def world(k, lines):
            return [x.replace('tick=4 hash=4 world=aa', 'tick=4 hash=4 world=ab') for x in lines] if k == 2 else lines
        n, text = self.failed(world)
        self.assertEqual(n, 1, text)
        self.assertIn('in sync   FAIL world differs at 1 ticks from tick 4', text)

        def stopped(k, lines):
            return lines[:5] + ['[X4LAN] NATIVE_GATE STOP reason=3'] if k == 1 else lines
        n, text = self.failed(stopped)
        self.assertGreaterEqual(n, 3, text)
        self.assertIn('running   FAIL', text)
        self.assertIn('chain     FAIL player 1: no end row', text)

    def test_the_plan_is_a_chain_per_player_with_the_fixtures_first(self):
        plan = action_tests.Trade.plan(self.args())
        self.assertEqual(len(plan), 2)
        for k, client in enumerate(plan, 1):
            self.assertEqual(client[:3], [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', 50000)])
            tick, op, limit, steps = client[3]
            self.assertEqual((tick, op), (60 + 30 * (k - 1), 'chain'))
            names = [s[0] for s in steps]
            self.assertEqual(len(names), len(set(names)))
            self.assertIn(('buy', 'op', 'trade_buy', action_tests.Trade.BUY, 'foodrations', 20, k, 1), steps)
            self.assertIn(('sell', 'op', 'trade_sell', action_tests.Trade.SELL, 'foodrations', 20, k, 1), steps)
        by_menu = self.args()
        by_menu.by = 'menu'
        steps = action_tests.Trade.plan(by_menu)[1][3][3]          # player 2's chain: the stock menus' own handlers
        for step in (('open_buy', 'op', 'trade_menu_open', action_tests.Trade.BUY), ('amount_buy', 'op', 'trade_menu_amount', 'foodrations', 20),
                     ('buy', 'op', 'menu_call', 'MapMenu', 'buttonConfirmTrade'), ('amount_sell', 'op', 'trade_menu_amount', 'foodrations', -20),
                     ('sell', 'op', 'menu_call', 'MapMenu', 'buttonConfirmTrade')):
            self.assertIn(step, steps)
        self.assertFalse(any(s[2] in ('trade_buy', 'trade_sell') for s in steps if len(s) > 2))
        full = action_tests.full_plan(action_tests.Trade, self.args())
        for client in full:          # the watch: the same readings at the same ticks in both plans
            self.assertEqual([x for x in client if x[1] == 'cargo'][:2], [(300, 'cargo', 'foodrations', 1), (300, 'cargo', 'foodrations', 2)])
        self.assertEqual([x for x in full[0] if x[1] == 'cargo'], [x for x in full[1] if x[1] == 'cargo'])
        lua, _ = uirep.client()
        self.assertEqual(lua.eval('#' + rig.lua(full)), 2)          # the plan's text is Lua


if __name__ == '__main__':
    unittest.main()
