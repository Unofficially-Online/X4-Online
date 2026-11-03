"""UI guard (lockstep_ui_guard.h, native_gate_ui_guard, default off) and the events family of the UI replay (P3, slot 1).

Covers the inventory against the generated native table (and, with the pinned X4.exe of x4-re at hand, a fresh generation),
the group lists in their four mirrors, the Lua core (the events wrappers: slot 1 captures, every client runs the stock
function at the apply, slots 2+ refused, the probe's and the simulation's own calls untouched; the guard's log rows and
info line; the scripted triggers), the real probe arming the guard on the mocked gate, and the option checker and the
overlay builder. The native half (hooks, refusal values, local phases, pass, pins) is tested in C:
src/native/engine/test_lockstep_ui_guard.h."""
import contextlib
import io
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

TABLE = (ROOT/'src/native/engine/lockstep_ui_guard_table.h').read_text(encoding='utf-8')
GUARD_H = (ROOT/'src/native/engine/lockstep_ui_guard.h').read_text(encoding='utf-8')
REPLAY_H = (ROOT/'src/native/engine/lockstep_ui_replay.h').read_text(encoding='utf-8')
ROW = re.compile(r'\{"(\w+)",0x([0-9a-f]+)u,UIGUARD_(FFI|LUA),UIGUARD_G_(\w+),(\d+)u,UIGUARD_RET_(\w+),(\d+),(\d+),(-?\d+),(\d+),')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))


def table_rows():
    return [dict(name=m[0], rva=int(m[1], 16), kind=m[2].lower(), group=m[3].lower(), covered=int(m[4]), ret=m[5], checkonly=int(m[8]))
            for m in ROW.findall(TABLE)]


class TableTests(unittest.TestCase):
    def test_the_table_is_the_inventory(self):
        rows = table_rows()
        self.assertEqual(len(rows), int(re.search(r'#define UIGUARD_ENTRIES (\d+)u', TABLE).group(1)))
        self.assertEqual({(r['kind'], r['name']) for r in rows}, set(inv.INVENTORY))
        for r in rows:
            kind, group, covered = inv.INVENTORY[(r['kind'], r['name'])]
            self.assertEqual((r['group'], r['covered']), (group, covered), r['name'])
        self.assertEqual(len({r['rva'] for r in rows}), len(rows))

    def test_the_groups_agree_in_every_mirror(self):
        names = re.search(r'uiguard_group_names\[UIGUARD_GROUPS\]=\{(.*?)\};', GUARD_H, re.S).group(1)
        self.assertEqual(tuple(re.findall(r'"(\w+)"', names)), inv.GROUPS)
        enum = re.search(r'enum \{UIGUARD_G_FLIGHT=0,(.*?)UIGUARD_GROUPS\};', GUARD_H, re.S).group(0)
        self.assertEqual(tuple(n.lower() for n in re.findall(r'UIGUARD_G_(\w+)', enum)), inv.GROUPS)
        lua, _ = uirep.client()
        self.assertEqual(tuple(lua.eval('X4LAN_UI_REPLAY_CORE.GUARD_GROUPS').values()), inv.GROUPS)
        self.assertEqual(uirep.options.UI_GUARD_GROUPS, inv.GROUPS)
        self.assertEqual(uirep.players.UI_GUARD_GROUPS, inv.GROUPS)

    def test_the_p3_and_economy_entry_points_of_the_report_are_in(self):
        names = {name for _, name in inv.INVENTORY}
        for name in ('StartConversationFromMenu', 'StartSubConversationFromMenu', 'SelectDialogOption', 'AddUITriggeredEvent', 'SignalObject',
                     'SignalObjectWithNPCSeed', 'SignalObjectWithNPCSeedAndMissionID', 'SetActiveMission', 'AbortMission', 'AddTradeToShipQueue',
                     'TransferPlayerMoneyTo', 'TransferMoneyToPlayer', 'SetContainerTradeRule', 'SetContainerWareIsBuyable', 'CreateTradeRule',
                     'AddBuildTask6', 'PerformCrewExchange2', 'ClaimShip', 'SellPlayerShip', 'CreateBoardingOperation', 'StartResearch'):
            self.assertIn(name, names)
        # The client's own presentation stays out: guidance (native_gate_guidance_local), cameras, the info line, map display.
        for name in ('SetGuidance', 'EndGuidance', 'ShowInfoLine', 'SetPlayerCameraCockpitView', 'SetMapRenderTradeOffers', 'OpenMenu'):
            self.assertNotIn(name, names)
        query = re.compile(r'^(Get|Is|Has|Can|Are|Check|Count|Does|Was|Find)[A-Z]')
        self.assertFalse([n for n in names if query.match(n) and n != 'GetUp'])   # GetUp: the docked menu's "Get up", an action

    def test_what_the_replay_hooks_itself_is_covered_by_its_family(self):
        hooks = re.findall(r'\{"(\w+)",0x([0-9a-f]+)u,\d+,(UIREP_\w+|0),', REPLAY_H)
        family = {'UIREP_FAMILY_ORDERS': inv.ORDERS, 'UIREP_QUEUE_HOOK': inv.ORDERS | inv.QUEUE, 'UIREP_FAMILY_QUEUE': inv.QUEUE,
                  'UIREP_FAMILY_DOCK': inv.DOCK, 'UIREP_FAMILY_WEAPONS': inv.WEAPONS, 'UIREP_FAMILY_TRAVEL': inv.TRAVEL,
                  'UIREP_FAMILY_AUTOPILOT': inv.AUTOPILOT, 'UIREP_FAMILY_FLIGHT': inv.FLIGHT, '0': 0}
        by_rva = {r['rva']: r for r in table_rows()}
        shared = 0
        for name, rva, fam in hooks:
            row = by_rva.get(int(rva, 16))
            if row:
                shared += 1
                self.assertEqual(row['covered'] & family[fam], family[fam], name)
                self.assertNotEqual(family[fam], 0, name)
        self.assertGreaterEqual(shared, 15)
        # The flight family: below ToggleFlightAssist (its export starts RIP-relative), at the HUD's button exports.
        flight = {name: int(rva, 16) for name, rva, fam in hooks if fam == 'UIREP_FAMILY_FLIGHT'}
        self.assertEqual(flight, {'FlightAssistToggle': 0x710d50, 'CycleTurretMode': 0xb01ff0, 'CycleTurretMissile': 0xb01ed0,
                                  'ToggleDrones': 0xb0b390, 'CycleNextDroneMode': 0xb01e70})
        covered = {name for (kind, name), (_, _, cov) in inv.INVENTORY.items() if cov & inv.FLIGHT}
        self.assertEqual(covered, {'ToggleFlightAssist', 'CycleTurretMode', 'CycleTurretMissile', 'ToggleDrones', 'CycleNextDroneMode'})

    def test_the_ui_only_events_agree_in_every_mirror(self):
        native = re.search(r'uiguard_ui_only\[\]=\{(.*?)\};', GUARD_H, re.S).group(1)
        self.assertEqual(tuple(re.findall(r'\{"(\w*)","(\w*)"\}', native)), inv.UI_ONLY_EVENTS)
        lua, _ = uirep.client()
        self.assertEqual(tuple(tuple(e.values()) for e in lua.eval('X4LAN_UI_REPLAY_CORE.UI_ONLY').values()), inv.UI_ONLY_EVENTS)

    def test_no_md_cue_listens_for_a_ui_only_event(self):
        # The extracted MD of the base game and the DLCs (x4-re): no script names the screen of a UI-only event; the unfiltered
        # listeners check event.param for other screens (scenario_tutorials' debug cue logs every event: tutorial start only).
        md = X4RE/'gamescripts/resolved'
        files = sorted(md.glob('*/md/*.xml')) if md.is_dir() else []
        if not files:
            self.skipTest('x4-re with the extracted MD not at hand (X4LAN_X4RE)')
        for screen, _ in inv.UI_ONLY_EVENTS:
            hits = [f.name for f in files if f"'{screen}'" in f.read_text(encoding='utf-8', errors='replace')]
            self.assertEqual(hits, [], screen)

    def test_a_fresh_generation_on_the_pinned_image_matches(self):
        image, symbols, protos = X4RE/'bin/X4.exe', X4RE/'symbols.csv', X4RE/'data/ffi_prototypes.csv'
        if not (image.is_file() and symbols.is_file() and protos.is_file()):
            self.skipTest('x4-re with the pinned X4.exe not at hand (X4LAN_X4RE)')
        try:
            import capstone, pefile  # noqa: F401
        except ImportError:
            self.skipTest('capstone/pefile not installed')
        out = subprocess.run([sys.executable, str(ROOT/'tools/dev/gen_ui_guard_table.py'), '--image', str(image), '--symbols', str(symbols),
                              '--prototypes', str(protos), '--check'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)


EVENTS = r'''
  gate.ui_replay_local=function() return LOCAL or 0 end
  env.AddUITriggeredEvent=function(screen,control,value) stock_calls[#stock_calls+1]='AddUITriggeredEvent:'..screen..'/'..tostring(control)..'/'..
    (type(value)=='userdata' and 'id:'..tostring(ids[value]) or tostring(value)) end
  env.SignalObject=function(object,param) stock_calls[#stock_calls+1]='SignalObject:'..tostring(ids[object])..'/'..tostring(param) end
  X4LAN_UI_REPLAY_STOCK=nil
'''


class EventsTests(unittest.TestCase):
    def session(self, slot, **extra):
        lua, g = uirep.client()
        lua.execute(EVENTS)
        lua.execute(f"S=X4LAN_UI_REPLAY_CORE.session({{gate=gate,C=C,env=env,log=log,mask=256,slot_ships=function() return SLOT_SHIPS end,own_slot={slot}}})")
        return lua, g

    def test_slot_1_captures_and_every_client_runs_the_same_stock_call_at_the_apply(self):
        calls = []
        for client in (1, 2):
            lua, g = self.session(1)
            lua.execute("LOCAL=1;env.AddUITriggeredEvent('MapMenu','selection_ship',make_id('100'));env.SignalObject(make_id('200'),'x4lan')")
            self.assertEqual(uirep.values(g.stock_calls), [])          # nothing ran at the capture
            pushed = uirep.values(g.pushed)
            self.assertEqual([p[1] for p in pushed], [0x47, 0x47])
            self.assertTrue(pushed[0][2].startswith('01') and pushed[1][2].startswith('02'))
            lua.execute(f"LOCAL=1;DUE={{[50]='1:71:{pushed[0][2]};1:71:{pushed[1][2]}'}};S.apply(50)")
            calls.append(uirep.values(g.stock_calls))
            self.assertEqual(uirep.values(g.results), ['50:1', '50:1'])
            self.assertTrue(any('UI_REPLAY_APPLIED tick=50 slot=1 op=71 result=applied what=AddUITriggeredEvent(MapMenu,selection_ship)' in l
                                for l in uirep.values(g.logs)))
        self.assertEqual(calls[0], calls[1])
        self.assertEqual(calls[0], ['AddUITriggeredEvent:MapMenu/selection_ship/id:100', 'SignalObject:200/x4lan'])

    def test_slots_2_and_up_are_refused_and_run_nothing(self):
        lua, g = self.session(2)
        lua.execute("LOCAL=1;env.AddUITriggeredEvent('MapMenu','x');env.SignalObject(make_id('100'),'y')")
        self.assertEqual(uirep.values(g.stock_calls), [])
        self.assertEqual(uirep.values(g.pushed), [])
        logs = uirep.values(g.logs)
        self.assertTrue(any('UI_REPLAY_REFUSED what=AddUITriggeredEvent why=slot' in l for l in logs))
        self.assertTrue(any('UI_REPLAY_REFUSED what=SignalObject why=slot' in l for l in logs))
        # A section of slot 2 holding one is not applied (the gate refuses it natively; the Lua side stops too).
        lua.execute("DUE={[50]='2:71:0101'};S.apply(50)")
        self.assertEqual(g.FAILED, 'ui_lua_op')

    def test_the_probe_the_simulation_and_the_apply_run_the_stock_function_at_once(self):
        lua, g = self.session(2)
        lua.execute("LOCAL=0;env.AddUITriggeredEvent('X4LAN_UiReplay','MinHull',3)")   # the probe's frame or no local phase
        lua.execute("LOCAL=1;S.replaying=true;env.AddUITriggeredEvent('X4LAN_UiReplay','Own',4);S.replaying=false")
        self.assertEqual(uirep.values(g.stock_calls), ['AddUITriggeredEvent:X4LAN_UiReplay/MinHull/3', 'AddUITriggeredEvent:X4LAN_UiReplay/Own/4'])
        self.assertEqual(uirep.values(g.pushed), [])

    def test_what_does_not_encode_is_refused(self):
        lua, g = self.session(1)
        lua.execute("LOCAL=1;env.AddUITriggeredEvent('MapMenu','message_read',make_id('999'))")   # not a component: no reference
        self.assertEqual(uirep.values(g.pushed), [])
        self.assertEqual(uirep.values(g.stock_calls), [])
        self.assertTrue(any('UI_REPLAY_REFUSED what=AddUITriggeredEvent why=unmapped' in l for l in uirep.values(g.logs)))

    def test_a_ui_only_event_runs_where_it_was_made_on_every_slot(self):
        for slot in (1, 2):
            lua, g = self.session(slot)
            lua.execute("LOCAL=1;env.AddUITriggeredEvent('TopLevelMenu','');env.AddUITriggeredEvent('TopLevelMenu','menu_close')")
            self.assertEqual(uirep.values(g.stock_calls), ['AddUITriggeredEvent:TopLevelMenu//nil'])
            logs = uirep.values(g.logs)
            self.assertIn('UI_REPLAY_LOCAL what=AddUITriggeredEvent args=TopLevelMenu/ why=ui_only', logs)
            if slot == 1:   # the other control is a world event: captured as before
                self.assertEqual([p[1] for p in uirep.values(g.pushed)], [0x47])
            else:
                self.assertIn('UI_REPLAY_REFUSED what=AddUITriggeredEvent why=slot args=TopLevelMenu/menu_close', logs)
                self.assertEqual(uirep.values(g.pushed), [])

    def test_rows_name_the_screen_control_and_signal(self):
        lua, g = self.session(2)
        lua.execute("LOCAL=1;env.AddUITriggeredEvent('MapMenu','selection:ship,1');env.SignalObject(make_id('100'),'accept')")
        logs = uirep.values(g.logs)
        self.assertIn('UI_REPLAY_REFUSED what=AddUITriggeredEvent why=slot args=MapMenu/selection?ship?1', logs)
        self.assertIn('UI_REPLAY_REFUSED what=SignalObject why=slot args=accept', logs)
        # Slot 1: a capture the gate refuses (before the window, live g2 at tick 1) names it too; the native row says why.
        lua, g = self.session(1)
        lua.execute("PUSH_RESULT=0;LOCAL=1;env.AddUITriggeredEvent('DockedMenu','')")
        self.assertIn('UI_REPLAY_REFUSED what=AddUITriggeredEvent why=native args=DockedMenu/', uirep.values(g.logs))
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.args_text('AddUITriggeredEvent','A very long screen name here','x')"),
                         'A?very?long?screen?n/x')
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.args_text('CreateOrder',1,2)"), '-')

    def test_off_the_globals_stay_stock(self):
        lua, g = uirep.client()
        lua.execute(EVENTS + "before=env.AddUITriggeredEvent;S=new_session({mask=7});same=before==env.AddUITriggeredEvent")
        self.assertTrue(g.same)

    def test_families_and_groups(self):
        lua, _ = uirep.client()
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'events'})"), 256)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'flight','events'})"), 512+256)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.guard_groups('all')"), 2**14-1)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.guard_groups(true)"), 2**14-1)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.guard_groups({'trade','missions'})"), 8+256)
        for text in ("{}", "{'trade','trade'}", "{'seta'}", "'some'", "false"):
            mask, why = lua.eval(f'X4LAN_UI_REPLAY_CORE.guard_groups({text})')
            self.assertIsNone(mask, text)
            self.assertIn('native_gate_ui_guard must be all or a list of flight', why)


GUARD = r'''
  notices={}
  gate.ui_guard_refusals=function() local r=GUARD_ROWS;GUARD_ROWS=nil;return r end
  gate.ui_guard_status=function() return 'ui_guard=live refused=2' end
'''


class GuardLogTests(unittest.TestCase):
    def test_refusals_are_logged_and_shown_at_most_every_90_ticks(self):
        lua, g = uirep.client()
        lua.execute(GUARD + '''
          S=X4LAN_UI_REPLAY_CORE.session({gate=gate,C=C,env=env,log=log,mask=1,slot_ships=function() return {} end,guard=16383,
            notify=function(text) notices[#notices+1]=text end})
          GUARD_ROWS='60:AddPlayerMoney:trade:refused:-,60:SetComponentName:assets:refused:-';S.frame(60)
          GUARD_ROWS='100:StartConversationFromMenu:conversation:refused:-';S.frame(100)
          S.frame(120)
          GUARD_ROWS='160:AbortMission:missions:refused:-';S.frame(160)
          GUARD_ROWS='250:AddUITriggeredEvent:events:ui_only:TopLevelMenu/,250:AddUITriggeredEvent:events:refused:MapMenu/,'..
            '251:SignalObject:events:refused:accept';S.frame(251)
          GUARD_ROWS='300:AddUITriggeredEvent:events:ui_only:TopLevelMenu/';S.frame(400)
        ''')
        rows = [l for l in uirep.values(g.logs) if l.startswith('UI_GUARD')]
        self.assertEqual(rows, ['UI_GUARD_REFUSED tick=60 op=AddPlayerMoney group=trade', 'UI_GUARD_REFUSED tick=60 op=SetComponentName group=assets',
                                'UI_GUARD_REFUSED tick=100 op=StartConversationFromMenu group=conversation',
                                'UI_GUARD_STATUS tick=120 ui_guard=live refused=2', 'UI_GUARD_REFUSED tick=160 op=AbortMission group=missions',
                                'UI_GUARD_PASSED tick=250 op=AddUITriggeredEvent group=events args=TopLevelMenu/ why=ui_only',
                                'UI_GUARD_REFUSED tick=250 op=AddUITriggeredEvent group=events args=MapMenu/',
                                'UI_GUARD_REFUSED tick=251 op=SignalObject group=events args=accept',
                                'UI_GUARD_PASSED tick=300 op=AddUITriggeredEvent group=events args=TopLevelMenu/ why=ui_only'])
        # The info line: refusals only, at most every 90 ticks (the last refusal of a frame); a UI-only event shows nothing.
        self.assertEqual(uirep.values(g.notices), ['Multiplayer: SetComponentName is not shared between the players yet - nothing happened.',
                                                   'Multiplayer: AbortMission is not shared between the players yet - nothing happened.',
                                                   'Multiplayer: SignalObject is not shared between the players yet - nothing happened.'])

    def test_without_the_guard_nothing_is_asked(self):
        lua, g = uirep.client()
        lua.execute("gate.ui_guard_refusals=function() error('asked') end;S=new_session();S.frame(120)")
        self.assertFalse([l for l in uirep.values(g.logs) if l.startswith('UI_GUARD')])

    def test_the_scripted_triggers_call_the_stock_entries_as_a_menu_does(self):
        lua, g = uirep.client()
        lua.execute(EVENTS + '''
          C.AddPlayerMoney=function(n) stock_calls[#stock_calls+1]='AddPlayerMoney:'..n end
          C.ToggleFlightAssist=function() stock_calls[#stock_calls+1]='ToggleFlightAssist' end
          env.GetPlayerMoney=function() return 5000 end
          env.SetComponentName=function(id,name) stock_calls[#stock_calls+1]='SetComponentName:'..tostring(ids[id])..'/'..name end
          env.GetComponentData=function(id,key) return 'Old' end
          env.StartConversationFromMenu=function(c,actor) stock_calls[#stock_calls+1]='StartConversationFromMenu:'..c..'/'..tostring(ids[actor]) end
          tools={gate=gate,C=C,env=env,own_slot=1,slot_lua=function(s) return make_id(tostring(1000+s)) end}
          C.CycleTurretMode=function(n) stock_calls[#stock_calls+1]='CycleTurretMode:'..n end
          C.CycleTurretMissile=function(n) stock_calls[#stock_calls+1]='CycleTurretMissile:'..n end
          C.ToggleDrones=function(n) stock_calls[#stock_calls+1]='ToggleDrones:'..n end
          C.CycleNextDroneMode=function(n) stock_calls[#stock_calls+1]='CycleNextDroneMode:'..n end
          plan={{0,'money',1000},{1,'rename',1,'Guarded'},{2,'ui_event','Probe',7},{3,'signal',2,'x4lan'},{4,'flight_assist'},{5,'comm',2},
            {6,'ui_raw','TopLevelMenu',''},{7,'turret_mode',2},{8,'turret_missile',1},{9,'drones',3},{10,'drone_mode',1}}
          marks={}
          S=new_session({mask=1,script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools),local_mark=function(on) marks[#marks+1]=on end})
          for t=40,51 do S.frame(t) end
        ''')
        self.assertEqual(uirep.values(g.stock_calls), ['AddPlayerMoney:1000', 'SetComponentName:1001/Guarded', 'AddUITriggeredEvent:X4LAN_UiGuard/Probe/7',
                                                       'SignalObject:1002/x4lan', 'ToggleFlightAssist', 'StartConversationFromMenu:default/1002',
                                                       'AddUITriggeredEvent:TopLevelMenu//nil', 'CycleTurretMode:2', 'CycleTurretMissile:1',
                                                       'ToggleDrones:3', 'CycleNextDroneMode:1'])
        self.assertEqual(uirep.values(g.marks), [0, 1] * 11)    # each as a person's click: outside the probe's frame
        logs = uirep.values(g.logs)
        self.assertTrue(any('UI_REPLAY_SCRIPT tick=40 op=money ok=true result=5000->5000' in l for l in logs))
        self.assertTrue(any('UI_REPLAY_SCRIPT tick=41 op=rename ok=true result=Old->Old' in l for l in logs))


PROBE_GUARD = uirep.PROBE_MOCK + r'''
  gate.ui_guard_pins=function() ui_calls[#ui_calls+1]='guard_pins';return GPINS or 1,GPINS and 'pins_ui_guard:1b5940' or nil end
  gate.ui_guard=function(mask) ui_calls[#ui_calls+1]='guard:'..mask;if GARM then return 0,GARM end;return 1,280 end
  gate.ui_guard_refusals=function() local r=GUARD_ROWS;GUARD_ROWS=nil;return r end
  gate.ui_guard_status=function() return 'ui_guard=live' end
  gate.runtime_ids_probe=function(on) ui_calls[#ui_calls+1]='mark:'..on end
  gate.ui_replay_push=function(op,hex) ui_calls[#ui_calls+1]='push:'..op;return 1 end
  gate.ui_replay_local=function() return 1 end
  gate.ui_replay_saved=function() return nil end
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def clients(self, prelude=PROBE_GUARD, **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel', 'target'], native_gate_ui_guard='all')
        cfg.update(extra)
        return [self.stock.StockClient(k, slots_all=True, prelude=prelude, **cfg) for k in (1, 2)]

    def test_the_guard_arms_after_the_replay_and_its_pins_before_any_patch(self):
        clients = self.clients()
        self.stock.run(clients, 13)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertLess(calls.index('guard_pins'), calls.index('arm:3'))
            self.assertEqual(calls[calls.index('arm:3')+1], 'guard:16383')
            logs = c.values('logs')
            self.assertTrue(any('UI_GUARD_PINS ok=1' in line for line in logs))
            self.assertTrue(any(f'UI_GUARD_ARMED slot={c.slot} groups=16383 entries=280' in line for line in logs))
            self.assertIn('mark:1', calls)                    # the probe's own frame is marked: its calls pass the guard

    def test_a_refusal_reaches_the_log_and_the_info_line(self):
        prelude = PROBE_GUARD + '''
          infolines={}
          require('ffi').C.ShowInfoLine=function(text,timeout) infolines[#infolines+1]=text..'/'..timeout end
        '''
        clients = self.clients(prelude=prelude)
        clients[1].lua.execute("GUARD_ROWS='9:AddTradeToShipQueue:trade:refused:-'")
        self.stock.run(clients, 12)
        self.assertEqual(clients[1].errors(), [], clients[1].values('logs')[-3:])
        self.assertTrue(any('UI_GUARD_REFUSED tick=9 op=AddTradeToShipQueue group=trade' in line for line in clients[1].values('logs')))
        self.assertEqual(clients[1].values('infolines'), ['Multiplayer: AddTradeToShipQueue is not shared between the players yet - nothing happened./4'])
        self.assertEqual(clients[0].values('infolines'), [])

    def test_the_scripted_triggers_run_as_a_persons_click(self):
        prelude = PROBE_GUARD + '''
          require('ffi').C.AddPlayerMoney=function(n) ui_calls[#ui_calls+1]='money:'..n end
          GetPlayerMoney=function() return 7 end
        '''
        clients = self.clients(prelude=prelude, native_gate_ui_replay_script=[[[7, 'money', 1000]], []])
        self.stock.run(clients, 13)
        calls = clients[0].values('ui_calls')
        at = calls.index('money:1000')
        self.assertEqual(calls[at-1:at+2], ['mark:0', 'money:1000', 'mark:1'])

    def test_refusals(self):
        for extra, prelude, why in ((dict(native_gate_ui_guard='some'), '', 'native_gate_ui_guard must be all or a list of'),
                                    (dict(native_gate_ui_guard=['trade', 'nope']), '', 'native_gate_ui_guard must be all or a list of'),
                                    (dict(native_gate_ui_replay=None), '', 'ui guard refused: needs native_gate_ui_replay'),
                                    (dict(), 'GPINS=0', 'pins_ui_guard:1b5940'),
                                    (dict(), "GARM='needs_ui_replay'", 'ui guard refused: needs_ui_replay')):
            with self.subTest(why=why):
                c = self.stock.StockClient(1, slots_all=True, prelude=PROBE_GUARD+prelude, **dict(
                    dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel'], native_gate_ui_guard='all'), **extra))
                self.assertTrue(any(why in line for line in c.errors()), c.values('logs')[-3:])

    def test_off_nothing_changes(self):
        c = self.stock.StockClient(1, slots_all=True, prelude=PROBE_GUARD, native_gate_pilot_present=True, native_gate_runtime_ids=True,
                                   native_gate_ui_replay=['travel'])
        self.stock.run([c], 12)
        self.assertFalse([call for call in c.values('ui_calls') if call.startswith('guard')])
        self.assertFalse(any('UI_GUARD' in line for line in c.values('logs')))


class OptionTests(unittest.TestCase):
    def test_rules(self):
        failures = uirep.failures
        config = uirep.stock_config
        self.assertEqual(failures(config(native_gate_ui_replay="{'travel','events'}", native_gate_ui_guard="'all'")), [])
        self.assertEqual(failures(config(native_gate_ui_replay='true', native_gate_ui_guard="{'trade','conversation'}")), [])
        for rule, override in (('native_gate_ui_guard needs native_gate_ui_replay', dict(native_gate_ui_guard="'all'")),
                               ('native_gate_ui_guard must be all or a list of flight', dict(native_gate_ui_replay='true', native_gate_ui_guard="'some'")),
                               ('native_gate_ui_guard must be all or a list of flight', dict(native_gate_ui_replay='true', native_gate_ui_guard="{'trade','trade'}")),
                               ('native_gate_ui_guard must be all or a list of flight', dict(native_gate_ui_replay='true', native_gate_ui_guard="{'seta'}"))):
            self.assertTrue(any(f.startswith(rule) for f in failures(config(**override))), override)


class BuilderTests(unittest.TestCase):
    ARGS = ['--players', '2', '--mode', 'coop', '--source', 'stock', '--present']

    def lines(self, *extra):
        players = uirep.players
        args = players.defaults(players.build_parser().parse_args(['config', *self.ARGS, *extra]))
        return players.config_block(args)[0]

    def test_guard_block(self):
        self.assertFalse(any('ui_guard' in line for line in self.lines('--ui-replay')))
        self.assertIn("X4LAN_CONFIG.native_gate_ui_guard = 'all'", self.lines('--ui-replay', '--ui-guard'))
        self.assertIn("X4LAN_CONFIG.native_gate_ui_guard = {'trade','missions'}", self.lines('--ui-replay', '--ui-guard', 'trade,missions'))
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'travel','events'}", self.lines('--ui-replay', 'travel,events'))
        for extra in (['--ui-guard'], ['--ui-replay', '--ui-guard', 'seta'], ['--ui-replay', '--ui-guard', 'trade,trade']):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.lines(*extra)
        text = '\n'.join(self.lines('--ui-replay', 'travel,events', '--ui-guard')).replace('@X4LANE_CLIENTS@', '2')
        text += '\nX4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(uirep.failures(text), [])

    def test_the_overlay_brings_the_md_rows(self):
        with tempfile.TemporaryDirectory() as d:
            out = uirep.BuilderTests.overlay(uirep.BuilderTests(), Path(d), 'g', '', '--ui-replay', 'travel', '--ui-guard')
            self.assertTrue((out/'md/native_gate_ui_replay.xml').is_file())
            self.assertIn("native_gate_ui_guard = 'all'", (out/'ui/config.lua.append').read_text())
            out = uirep.BuilderTests.overlay(uirep.BuilderTests(), Path(d), 'e', '', '--ui-replay', 'travel,events')
            self.assertTrue((out/'md/native_gate_ui_replay.xml').is_file())
        md = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
        self.assertIn("<event_ui_triggered screen=\"'X4LAN_UiGuard'\"/>", md)
        self.assertIn('UI_GUARD_MD_EVENT control=%s value=%s time=%s', md)


if __name__ == '__main__':
    unittest.main()
