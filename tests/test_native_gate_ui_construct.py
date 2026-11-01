"""Station construction for the 'build' family (lockstep_ui_construct.h, native_gate_ui_build_core.lua; default off with it).

Covers what Python can see: the new claims and the guard inventory (the plan commit, cancelling a construction, the delivery
cancel that stays refused) against the generated guard table; the faction-scoped claim list; the stock names the Lua core
wraps (with x4-re at hand: every scope and unblocked entry is a function of its stock menu, the v1 loadout layouts under
LuaJIT's ffi with the stock typedefs, the C mirror's sizes); the Lua core (construction scopes mark their money records,
factions mode refuses the other scopes for slots 2+ and needs the trade family, a hire needs the order families, the order
block opens the deploy entries, a scoped record runs in the slot's faction scope at the apply, the slot's newest plot, the
script ops plan/budget/hire/cancel); the probe passing each slot's faction ID; the registry. The native half (the plan
record, the mirrored commit on two clients, skips, refusals, factions, the ID counter) is tested in C:
src/native/engine/test_lockstep_ui_construct.h."""
import os
from pathlib import Path
import re
import sys
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_build as uib  # noqa: E402
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

ENGINE = ROOT/'src/native/engine'
BUILD_H = (ENGINE/'lockstep_ui_build.h').read_text(encoding='utf-8')
CONSTRUCT_H = (ENGINE/'lockstep_ui_construct.h').read_text(encoding='utf-8')
TABLE_H = (ENGINE/'lockstep_ui_guard_table.h').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
PROBE = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))
FFI_H = X4RE/'data/x4_ffi.h'
STOCK_UI = X4RE/'gamelua/08/ui/addons'
MENU_FILES = {'MapMenu': 'ego_detailmonitor/menu_map.lua', 'InteractMenu': 'ego_interactmenu/menu_interactmenu.lua',
              'StationConfigurationMenu': 'ego_detailmonitor/menu_station_configuration.lua',
              'ShipConfigurationMenu': 'ego_detailmonitor/menu_ship_configuration.lua'}


def lua_client():
    lua, g = uib.lua_client()
    lua.execute(r'''
      trade_scopes={}
      gate.ui_build_add=function(id,hex,scoped) build_added[#build_added+1]=id..':'..tostring(scoped)..':'..hex;return BUILD_ADD or 1,BUILD_WHY end
      gate.ui_trade_scope=function(slot) trade_scopes[#trade_scopes+1]='open:'..slot;return SCOPE_RESULT or 1 end
      gate.ui_trade_scope_end=function() trade_scopes[#trade_scopes+1]='close';return 1 end
      gate.ui_build_plan=function(station,spec) stock_calls[#stock_calls+1]='plan:'..station..':'..spec;return PLAN_RESULT or 1,PLAN_WHY end
      function new_cons(extra)
        extra=extra or {}
        local mask=extra.mask or (4096+8192+4+128)
        R=new_session({mask=mask})
        return X4LAN_UI_BUILD_CORE.session{gate=gate,C=C,env=env,log=log,mask=mask,replay=R,own_slot=extra.slot or 1,factions=extra.factions,
          notify=function(text) notices[#notices+1]=text end,local_mark=extra.local_mark,script=extra.script,menus=function() return MENUS end}
      end
    ''')
    return lua, g


def added(g):
    return [a.split(':')[0] + ':' + a.split(':')[1] for a in uirep.values(g.build_added)]


class ClaimTests(unittest.TestCase):
    def test_the_construction_claims_are_inventoried_build_entries_of_the_table(self):
        names = {c['name']: c for c in uib.claims()}
        plan, cancel = names['SetConstructionSequenceFromConstructionMap'], names['CancelConstruction']
        self.assertEqual((plan['spec'], plan['ret'], plan['pred'], plan['family']), ('IQ', 'VOID', 'VOID', 'build'))
        self.assertEqual((cancel['spec'], cancel['ret'], cancel['pred'], cancel['family']), ('Iu', 'BOOL', 'TRUE', 'build'))
        for name in ('SetConstructionSequenceFromConstructionMap', 'CancelConstruction', 'CancelPlayerInvolvedTradeDeal'):
            self.assertEqual(inv.INVENTORY[('ffi', name)], ('ffi', 'build', 0), name)
            self.assertRegex(TABLE_H, r'\{"' + name + r'",0x[0-9a-f]+u,UIGUARD_FFI,UIGUARD_G_BUILD,0u,')
        self.assertNotIn('CancelPlayerInvolvedTradeDeal', names)   # stays refused: the trade deal ID's numbering
        self.assertEqual(int(re.search(r'#define UIGUARD_ENTRIES (\d+)u', TABLE_H).group(1)), len(inv.INVENTORY))

    def test_the_faction_scoped_claims_are_the_station_construction(self):
        scoped = re.search(r'uibuild_scoped_names\[\]=\{(.*?)\};', BUILD_H, re.S).group(1)
        scoped = re.findall(r'"(\w+)"|(UIBUILD_PLAN_CLAIM)', scoped)
        scoped = {'SetConstructionSequenceFromConstructionMap' if b else a for a, b in scoped}
        self.assertEqual(scoped, {'SetConstructionSequenceFromConstructionMap', 'CancelConstruction', 'SetStationModuleLoadoutLevel',
                                  'ReserveBuildPlot', 'ExtendBuildPlot', 'PayBuildPlotSize', 'RemoveBuildPlot2', 'RemoveBuildStorage'})
        claims = {c['name']: c for c in uib.claims()}
        self.assertTrue(scoped <= set(claims))
        self.assertTrue(all(claims[n]['family'] == 'build' and claims[n]['slots'] == 'ANY' for n in scoped))

    def test_the_pins_cover_every_function_the_commit_calls_or_mirrors(self):
        pins = set(int(m, 16) for m in re.findall(r'\{(?:0x([0-9a-f]+)u|UICONS_\w+),0x[0-9a-f]+,"[0-9a-f]{64}"\}', CONSTRUCT_H) if m)
        named = dict(re.findall(r'#define (UICONS_\w+) 0x([0-9a-f]+)u', CONSTRUCT_H))
        for name in ('UICONS_BUILD_TASK', 'UICONS_UPGRADE_EMPTY', 'UICONS_UPGRADE_INIT', 'UICONS_UPGRADE_FREE', 'UICONS_UPGRADE_FROM',
                     'UICONS_MACRO_LOOKUP', 'UICONS_HOLOMAP_OF', 'UICONS_BUILD_CONTEXT', 'UICONS_ALLOCATE', 'UICONS_PLANNED_FN',
                     'UICONS_LOADOUT', 'UICONS_LOADOUT_COUNTS'):
            self.assertIn('{' + name + ',', CONSTRUCT_H, name)
            pins.add(int(named[name], 16))
        # the stock commit itself, the copy, the entry, the count, the macro database's user, the reads, the import's lookup
        for rva in (0x1dc030, 0xd57a50, 0xd57880, 0xd57fb0, 0x22c580, 0x1c8cc3, 0xe652cf, 0x1c3ef0):
            self.assertIn(rva, pins, hex(rva))


class LayoutTests(unittest.TestCase):
    def test_the_v1_loadout_mirror_agrees_with_the_lua_layouts(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(uib.BUILD_CORE)
        layouts = {list(t.values())[0]: list(t.values())[1] for t in lua.eval('X4LAN_UI_BUILD_CORE.LAYOUTS').values()}
        self.assertEqual((layouts['UILoadout'], layouts['UILoadoutMacroData'], layouts['UILoadoutGroupData'], layouts['UILoadoutCounts']), (160, 32, 32, 36))
        self.assertIn('sizeof(uicons_macro1)==32 && sizeof(uicons_group1)==32 && sizeof(uicons_loadout1)==160', CONSTRUCT_H)
        self.assertIn('__builtin_offsetof(uicons_loadout1,thruster)==144', CONSTRUCT_H)
        self.assertIn('sizeof(uicons_counts1)==36', CONSTRUCT_H)

    @unittest.skipUnless(STOCK_UI.is_dir(), 'x4-re stock UI scripts not at hand')
    def test_every_wrapped_or_unblocked_entry_is_a_stock_menu_function(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(uib.BUILD_CORE)
        entries = []
        for menu, names in lua.eval('X4LAN_UI_BUILD_CORE.SCOPES').items():
            entries += [f'{menu}.{n}' for n in names.values()]
        entries += [e for e in lua.eval('X4LAN_UI_BUILD_CORE.CONSTRUCTION').keys() if not e.startswith('script.')]
        entries += list(lua.eval('X4LAN_UI_BUILD_CORE.UNBLOCK').keys())
        entries += [e for e in lua.eval('X4LAN_UI_BUILD_CORE.HIRE').keys() if not e.startswith('script.')]
        for entry in sorted(set(entries)):
            menu, name = entry.split('.')
            text = (STOCK_UI/MENU_FILES[menu]).read_text(encoding='utf-8', errors='replace')
            self.assertRegex(text, r'\bfunction menu\.' + name + r'\(', entry)
        # the plan commit's call site, read at test time only
        text = (STOCK_UI/MENU_FILES['StationConfigurationMenu']).read_text(encoding='utf-8', errors='replace')
        body = re.search(r'function menu\.buttonConfirm\(\)(.*?)\nend', text, re.S).group(1)
        self.assertIn('C.SetConstructionSequenceFromConstructionMap(menu.container, menu.holomap)', body)

    @unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
    def test_the_v1_layouts_match_the_stock_cdefs_under_luajit(self):
        text = FFI_H.read_text(encoding='utf-8', errors='replace')
        typedefs = [line.strip() for line in text.splitlines()
                    if re.match(r'typedef struct \{.*\} (UILoadout|UILoadoutMacroData|UILoadoutGroupData|UILoadoutAmmoData|UILoadoutSoftwareData|'
                                r'UILoadoutVirtualMacroData|UILoadoutCounts);$', line.strip())]
        self.assertEqual(len(typedefs), 7)
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(uib.BUILD_CORE)
        lua.globals().CDEF = '\n'.join(typedefs)
        ok, missing = lua.eval('(function() local ffi=require("ffi");ffi.cdef(CDEF);return X4LAN_UI_BUILD_CORE.check_layouts(ffi) end)()')
        self.assertTrue(ok, missing)
        self.assertNotIn('UILoadout', list(missing.values()))
        # the stock prototypes of what the native half calls with these
        self.assertRegex(text, r'void GetConstructionMapItemLoadout2\(UILoadout\* result, UniverseID holomapid, size_t itemidx, UniverseID defensibleid, UniverseID moduleid\);')
        self.assertRegex(text, r'void SetConstructionSequenceFromConstructionMap\(UniverseID containerid, UniverseID holomapid\);')
        self.assertRegex(text, r'bool CancelConstruction\(UniverseID containerid, BuildTaskID id\);')


class CoreTests(unittest.TestCase):
    def test_construction_scopes_mark_their_money_and_the_plan_confirm_is_a_scope(self):
        lua, g = lua_client()
        lua.execute(r'''
          B=new_cons()
          storage=make_id('300')
          station={name='StationConfigurationMenu',buttonConfirmMoney=function() env.TransferPlayerMoneyTo(5000,storage) end,
            buttonConfirm=function() return 'confirmed' end}
          ship={name='ShipConfigurationMenu',buttonConfirm=function() env.TransferPlayerMoneyTo(9,storage) end}
          MENUS={station,ship}
          B.frame(40)
          station.buttonConfirmMoney();r=station.buttonConfirm();ship.buttonConfirm()
        ''')
        self.assertEqual(added(g), ['1:1', '1:0'])            # the budget is scoped, the ship purchase is not
        self.assertEqual(g.r, 'confirmed')
        self.assertTrue(any('UI_BUILD_SCOPES menu=StationConfigurationMenu functions=2' in line for line in uirep.values(g.logs)))

    def test_factions_mode_slot_2_runs_only_construction_scopes_and_needs_the_trade_family(self):
        lua, g = lua_client()
        lua.execute(r'''
          storage=make_id('300')
          function scopes(B)
            local ran={}
            B.with_scope('StationConfigurationMenu.buttonConfirmMoney',function() ran[#ran+1]='budget';env.TransferPlayerMoneyTo(5,storage) end)
            B.with_scope('ShipConfigurationMenu.buttonConfirm',function() ran[#ran+1]='ship' end)
            return table.concat(ran,',')
          end
          B=new_cons({factions=true,slot=2});without=scopes(B)
          B=new_cons({factions=true,slot=2,mask=4096+8192+4+128+1024});with=scopes(B)
          B=new_cons({factions=true,slot=1});slot1=scopes(B)
        ''')
        self.assertEqual((g.without, g['with'], g.slot1), ('', 'budget', 'budget,ship'))
        logs = uirep.values(g.logs)
        self.assertTrue(any('op=StationConfigurationMenu.buttonConfirmMoney why=needs_trade' in line for line in logs))
        self.assertTrue(any('op=ShipConfigurationMenu.buttonConfirm why=factions' in line for line in logs))
        self.assertTrue(any('ShipConfigurationMenu.buttonConfirm is not shared for your slot yet' in t for t in uirep.values(g.notices)))
        self.assertEqual(added(g), ['1:1', '1:1'])

    def test_a_hire_needs_the_order_families_and_opens_the_deploy_entries(self):
        lua, g = lua_client()
        lua.execute(r'''
          builder=make_id('200')
          function hire(B) local n=0;B.with_scope('MapMenu.selectCV',function() n=n+1;env.TransferPlayerMoneyTo(12000,builder) end);return n end
          B=new_cons({mask=4096+8192+4});alone=hire(B);open1=R.unblocked['MapMenu.orderDeployToStation'];open2=R.unblocked['InteractMenu.orderDeployToStation']
          B=new_cons({mask=4096+8192+4+128});both=hire(B);open3=R.unblocked['MapMenu.orderDeployToStation']
          B=new_cons({mask=4+128});none=R.unblocked['MapMenu.orderDeployToStation']
        ''')
        self.assertEqual((g.alone, g.both), (0, 1))
        self.assertEqual((g.open1, g.open2, g.open3, g.none), (None, True, True, None))
        self.assertTrue(any('op=MapMenu.selectCV why=needs_queue' in line for line in uirep.values(g.logs)))
        self.assertEqual(added(g), ['1:1'])

    def test_a_scoped_record_runs_in_the_slots_faction_scope_and_the_newest_plot_is_kept(self):
        lua, g = lua_client()
        lua.execute(r'''
          B=new_cons({factions=true,slot=2,mask=4096+8192+4+128+1024})
          hex=assert(X4LAN_UI_REPLAY_CORE.encode_call({500,make_id('300')},2,R.ctx))
          PARTS={aa=1,bb=1}
          STEPS={{'ran','ReserveBuildPlot=28675'},{'lua',1,hex,1},{'ran','SetConstructionSequenceFromConstructionMap task=1 plan=00000000000000ab'},
            {'done','batch seq=0 records=3'},{'lua',1,hex,1},{'done','batch seq=1 records=1'}}
          DUE={[50]='2:80:aa',[51]='2:80:bb'}
          R.apply(50);SCOPE_RESULT=-1;R.apply(51)
        ''')
        self.assertEqual(uirep.values(g.trade_scopes), ['open:2', 'close', 'open:2'])
        self.assertEqual(uirep.values(g.lua_done), ['2:1', '2:0'])
        self.assertEqual(len(uirep.values(g.stock_calls)), 1)
        self.assertEqual(lua.eval('B.plots[2]'), '28675')
        logs = uirep.values(g.logs)
        self.assertTrue(any('UI_BUILD_APPLIED tick=50 slot=2 what=TransferPlayerMoneyTo(500,' in line and line.endswith(' scope') for line in logs), logs)
        self.assertTrue(any('UI_BUILD_APPLIED tick=51 slot=2 what=TransferPlayerMoneyTo scope result=error' in line for line in logs), logs)
        self.assertTrue(any('what=SetConstructionSequenceFromConstructionMap task=1 plan=00000000000000ab' in line for line in logs))

    def test_the_script_ops_plan_budget_hire_cancel(self):
        lua, g = lua_client()
        lua.execute(r'''
          C.GetBuilderHiringFee=function() return 12000LL end
          C.CreateDeployToStationOrder=function(id) stock_calls[#stock_calls+1]='Deploy:'..tostring(id);return 2 end
          C.EnableOrder=function(id,idx) stock_calls[#stock_calls+1]='Enable:'..tostring(id)..'/'..idx end
          C.GetNumBuildTasks=function(station,module,progress,upgrade) return progress and 0 or 1 end
          C.GetBuildTasks=function(buf,n,station,module,progress,upgrade) buf[0].id=0x77;return 1 end
          C.CancelConstruction=function(station,id) stock_calls[#stock_calls+1]='Cancel:'..tostring(station)..'/'..tostring(id);return true end
          env.GetComponentData=function(id,key)
            if key=='buildstorage' then return '301' end   -- tostring of X4's Lua ID is its decimal
            if key=='isplayerowned' then return false end
          end
          env.TransferPlayerMoneyTo=function(a,id) stock_calls[#stock_calls+1]='Transfer:'..tostring(a)..':'..tostring(id) end
          env.SetOrderParam=function(id,idx,n,v,station) stock_calls[#stock_calls+1]='Param:'..idx..'/'..tostring(ids[station]) end
          gate.ui_replay_queue_seen=function() return 0 end
          KNOWN['777']='0badf00d';KNOWN['301']='0badf00e'
          tools={gate=gate,C=C,env=env,own_slot=1,ffi={new=function(t,n) return {[0]={id=0}} end,string=function(s) return s end},
            saved_id=function(s) return tonumber(s,16)+0ULL end,saved_lua=function(s) return make_id(tostring(tonumber(s,16))) end}
          plan={{0,'plan','plot','prod_gen_refinedmetals_macro,-,-,-,0,0,0'},{1,'budget','plot',250000},{2,'hire','00000000000000c8','plot'},{3,'cancel','plot'},
            {4,'plan','000000000000012c','x,-,-,-,0,0,0'}}
          B=new_cons({mask=4096+8192+4+128,script=X4LAN_UI_BUILD_CORE.script(plan,40,tools),local_mark=function(on) end})
          B.plots[1]='777'
          for t=40,44 do R.frame(t) end
        ''')
        calls = uirep.values(g.stock_calls)
        # the plan through gate.ui_build_plan; the hire's order calls go to the order families (here: the harness's stock
        # RemoveAllOrders, the queue family's SetOrderParam capture); the cancel through the claimed entry
        self.assertEqual(calls, ['plan:777:prod_gen_refinedmetals_macro,-,-,-,0,0,0', 'RemoveAllOrders:200ULL', 'Deploy:200ULL', 'Enable:200ULL/2',
                                 'Cancel:777ULL/119', 'plan:300:x,-,-,-,0,0,0'])
        self.assertEqual([list(t.values())[0] for t in uirep.values(g.pushed)], [0x45])
        self.assertEqual(added(g), ['1:1', '1:1'])   # the budget and the fee: construction scopes
        logs = uirep.values(g.logs)
        for op, result in (('plan', 'plan'), ('budget', 'budget'), ('hire', 'order=2'), ('cancel', 'cancel=true')):
            self.assertTrue(any(f'UI_BUILD_SCRIPT tick=4' in line and f'op={op} ok=true result={result}' in line for line in logs), (op, logs[-8:]))
        with self.assertRaises(Exception):
            lua.eval("X4LAN_UI_BUILD_CORE.script({{0,'demolish'}},40,{})")


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def client(self, slot=1, prelude='', **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel', 'build', 'assets'],
                   native_gate_ui_guard='all', native_gate_player_identity_plan={'mode': 'coop'})
        cfg.update(extra)
        build = uib.PROBE_BUILD.replace("gate.ui_build=function(factions) ui_calls[#ui_calls+1]='build:'..factions;",
                                        "gate.ui_build=function(factions,ids) ui_calls[#ui_calls+1]='build:'..factions..':'..tostring(ids);")
        return self.stock.StockClient(slot, slots_all=True, prelude=uirep.PROBE_MOCK + uib.BUILD_CORE + build + prelude, **cfg)

    def test_the_probe_passes_each_slots_faction_id(self):
        c = self.client()
        self.assertIn('build:0:player,player', c.values('ui_calls'))
        c = self.client(native_gate_player_identity_plan={'mode': 'factions', 'slots': [{'faction': 'player'}, {'faction': 'x4lan_player2'}]})
        self.assertIn('build:1:player,x4lan_player2', c.values('ui_calls'))


class RegistryTests(unittest.TestCase):
    def test_the_registry_names_the_new_claims_and_no_new_numbers(self):
        self.assertIn('31 FFI entries (uibuild_claims', REGISTRY)
        self.assertIn('SetConstructionSequenceFromConstructionMap', REGISTRY)
        self.assertIn('CancelConstruction', REGISTRY)
        # no new family bit and no new op: the plan travels in the build family's parts (0x50)
        self.assertNotIn('UIREP_FAMILY_CONSTRUCT', BUILD_H + CONSTRUCT_H)
        self.assertNotRegex(CONSTRUCT_H, r'#define \w*_OP\w* 0x')
        self.assertIn('ui_build_plan', PROBE + (ROOT/'tools/dev/native_gate_ui_build_core.lua').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
