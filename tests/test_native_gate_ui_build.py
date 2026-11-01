"""UI replay families 'build' and 'assets' (lockstep_ui_build.h, native_gate_ui_build_core.lua, default off).

Covers the mirrors (family bits, the part op, the Lua records) in C, both Lua cores and the Python tools; the claims against
the guard's inventory and, with x4-re at hand, against the stock prototypes (every argument's letter, the check-only flag,
the hidden struct pointer, the return kind) and the stock struct layouts under LuaJIT's ffi; the Lua core (SetComponentName,
the commit scopes and the money and repair records inside them, the apply of a whole batch through the real replay core,
the receipt, refusal rows and the info line, the script ops); the real probe arming the families on the mocked gate; the
option checker and the overlay builder. The native half (claims through the guard's hook, batches, parts, two clients, slot
rules, references) is tested in C: src/native/engine/test_lockstep_ui_build.h."""
import contextlib
import io
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

DEV = ROOT/'tools/dev'
ENGINE = ROOT/'src/native/engine'
BUILD_CORE = (DEV/'native_gate_ui_build_core.lua').read_text(encoding='utf-8')
BUILD_H = (ENGINE/'lockstep_ui_build.h').read_text(encoding='utf-8')
REPLAY_H = (ENGINE/'lockstep_ui_replay.h').read_text(encoding='utf-8')
INPUTS_H = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))
FFI_H = X4RE/'data/x4_ffi.h'
CLAIM = re.compile(r'\{"(\w+)",UIREP_FAMILY_(BUILD|ASSETS),"(\w+)",UIBUILD_RET_(\w+),UIBUILD_PRED_(\w+),UIBUILD_SLOTS_(\w+),(\d+),(NULL|"\w+")\}')


def claims():
    return [dict(name=m[0], family=m[1].lower(), spec=m[2], ret=m[3], pred=m[4], slots=m[5], arg=int(m[6]), after=m[7].strip('"'))
            for m in CLAIM.findall(BUILD_H)]


def lua_client():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(uirep.CORE)
    lua.execute(BUILD_CORE)
    lua.execute(uirep.HARNESS)
    lua.execute(r'''
      build_added={};flushes=0;lua_done={};notices={}
      gate.ui_replay_local=function() return LOCAL or 1 end
      gate.ui_build_add=function(id,hex) build_added[#build_added+1]=id..':'..hex;return BUILD_ADD or 1,BUILD_WHY end
      gate.ui_build_flush=function() flushes=flushes+1;return 1 end
      gate.ui_build_part=function(slot,hex) local r=PARTS and PARTS[hex];if r=='drop' then return nil,'torn' end;return r end
      gate.ui_build_step=function(slot,tick) return unpack(table.remove(STEPS,1)) end
      gate.ui_build_lua_done=function(slot,ok) lua_done[#lua_done+1]=slot..':'..ok;return 1 end
      gate.ui_build_frame=function(t) return 'UIBUILD_1:'..t..':0:0' end
      gate.ui_build_refusals=function() local r=BUILD_ROWS;BUILD_ROWS=nil;return r end
      gate.ui_build_status=function() return 'ui_build=armed' end
      env.TransferPlayerMoneyTo=function(a,id) stock_calls[#stock_calls+1]='TransferPlayerMoneyTo:'..a..':'..tostring(id) end
      env.TransferMoneyToPlayer=function(a,id,e) stock_calls[#stock_calls+1]='TransferMoneyToPlayer:'..a..':'..tostring(id)..':'..tostring(e) end
      env.SetComponentName=function(id,name) stock_calls[#stock_calls+1]='SetComponentName:'..tostring(ids[id])..'/'..name end
      env.SignalObject=function(id,signal) stock_calls[#stock_calls+1]='SignalObject:'..tostring(id)..'/'..signal end
      env.GetPlayerMoney=function() return MONEY or 10000 end
      MENUS={}
      function new_build(extra)
        R=new_session({mask=(extra and extra.mask) or 4096+8192+4})
        return X4LAN_UI_BUILD_CORE.session{gate=gate,C=C,env=env,log=log,mask=(extra and extra.mask) or 4096+8192+4,replay=R,own_slot=1,
          notify=function(text) notices[#notices+1]=text end,local_mark=extra and extra.local_mark,script=extra and extra.script,fund=extra and extra.fund,
          menus=function() return MENUS end}
      end
    ''')
    return lua, lua.globals()


class MirrorTests(unittest.TestCase):
    def test_family_bits_ops_and_lua_records_agree(self):
        lua, _ = lua_client()
        enum = dict(re.findall(r'UIREP_FAMILY_(BUILD|ASSETS)=(\d+)', INPUTS_H))
        self.assertEqual({k.lower(): int(v) for k, v in enum.items()}, {'build': 4096, 'assets': 8192})
        families = sum(int(v) for v in re.search(r'UIREP_FAMILIES=([\d|]+)', INPUTS_H).group(1).split('|'))
        self.assertEqual(families & 12288, 12288)   # implemented (bits: docs/LOCKSTEP-REGISTRY.txt)
        for core in ('X4LAN_UI_BUILD_CORE', 'X4LAN_UI_REPLAY_CORE'):
            self.assertEqual(lua.eval(f'{core}.FAMILY.build'), 4096)
            self.assertEqual(lua.eval(f'{core}.FAMILY.assets'), 8192)
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'build','assets'})"), 12288)
        bits = list(lua.eval('X4LAN_UI_REPLAY_CORE.FAMILY').values())   # every family its own bit
        self.assertEqual(len(set(bits)), len(bits))
        self.assertTrue(all(b > 0 and b & (b - 1) == 0 for b in bits))
        self.assertIn('build', uirep.options.UI_REPLAY_FAMILIES)
        self.assertIn('assets', uirep.players.UI_REPLAY_FAMILIES)
        self.assertEqual(uirep.options.UI_REPLAY_FAMILIES, uirep.players.UI_REPLAY_FAMILIES)
        self.assertIn('#define UIBUILD_OP_PART 0x50u', REPLAY_H)
        self.assertEqual(lua.eval('X4LAN_UI_BUILD_CORE.OP_PART'), 0x50)
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.OP_LUA_GLOBAL'), 0x47)   # the replay's own ops end below
        native = re.search(r'uibuild_lua_family\[UIBUILD_LUA_MAX\+1\]=\{0,(.*?)\};', BUILD_H).group(1).split(',')
        self.assertEqual([n.replace('UIREP_FAMILY_', '').lower() for n in native], list(lua.eval('X4LAN_UI_BUILD_CORE.LUA_FAMILY').values()))
        self.assertEqual(int(re.search(r'#define UIBUILD_LUA_MAX (\d+)u', BUILD_H).group(1)), len(list(lua.eval('X4LAN_UI_BUILD_CORE.LUA').values())))
        self.assertIn('ui/native_gate_ui_build_core.lua', uirep.players.UI_REPLAY_FILES)


class ClaimTests(unittest.TestCase):
    def test_every_claim_is_an_inventoried_ffi_entry_no_other_family_covers(self):
        rows = claims()
        self.assertEqual(len(rows), int(len(re.findall(r'^\s*\{"\w+",UIREP_FAMILY_', BUILD_H, re.M))))
        self.assertEqual(len(rows), 31)   # 29 + the station plan commit and CancelConstruction (test_native_gate_ui_construct.py)
        for c in rows:
            entry = inv.INVENTORY.get(('ffi', c['name']))
            self.assertIsNotNone(entry, c['name'])
            self.assertIn(entry[1], ('build', 'assets', 'trade'), c['name'])
            self.assertEqual(entry[2], 0, c['name'])
        names = {c['name'] for c in rows}
        # Stay refused (report: the new NPC is read at once; the build method is the trade family's; a delivery's trade deal ID).
        for name in ('CreateNPCFromPerson', 'SetContainerBuildMethod', 'CancelPlayerInvolvedTradeDeal'):
            self.assertNotIn(name, names)
        self.assertEqual({c['name'] for c in rows if c['slots'].startswith('INVENTORY')},
                         {n for n in names if n.startswith(('Install', 'Dismantle'))})

    @unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
    def test_every_spec_matches_the_stock_prototype(self):
        text = FFI_H.read_text(encoding='utf-8', errors='replace')
        letters = {'UniverseID': 'IiBQ', 'BuildTaskID': 'tu', 'NPCSeed': 'u', 'int64_t': 'u', 'bool': 'bk', 'const char*': 'sz',
                   'UIPosRot': 'P', 'Coord3D': 'C', 'UILoadout2': 'L', 'CrewTransferInfo2': 'T', 'AddBuildTask6Container*': 'X',
                   'GenericActor': 'G', 'NPCSeed*': 'N', 'CrewTransferContainer*': 'R', 'uint32_t': 'n'}
        returns = {'void': 'VOID', 'bool': 'BOOL', 'BuildTaskID': 'U64', 'UniverseID': 'U64', 'const char*': 'REASON',
                   'UICrewExchangeResult': 'STRUCT_REASON'}
        for c in claims():
            m = re.search(r'^(\w[\w\s\*]*?)\s*\b' + c['name'] + r'\((.*?)\);', text, re.M)
            self.assertIsNotNone(m, c['name'])
            ret = m.group(1).replace(' *', '*').strip()
            self.assertEqual(returns[ret], c['ret'], c['name'])
            params = [p.strip() for p in m.group(2).split(',')]
            spec = c['spec']
            if c['ret'] == 'STRUCT_REASON':
                self.assertEqual(spec[0], 'h', c['name'])   # the hidden struct pointer is argument 0
                spec = spec[1:]
            self.assertEqual(len(spec), len(params), c['name'])
            for i, (letter, param) in enumerate(zip(spec, params)):
                ptype, pname = param.rsplit(' ', 1)
                ptype = (ptype + '*' * pname.count('*')).replace(' *', '*').strip()
                pname = pname.strip('*')
                if ptype == 'float':
                    self.assertEqual(letter, 'f' if i + (c['ret'] == 'STRUCT_REASON') < 4 else 'F', c['name'])
                    continue
                self.assertIn(letter, letters[ptype], f"{c['name']} {param}")
                self.assertEqual(letter == 'k', pname == 'checkonly', f"{c['name']} {param}")
                if letter == 'n':
                    self.assertIn(spec[i-1], 'NR', c['name'])
            if c['pred'] == 'CHECK':
                self.assertIn('k', spec, c['name'])

    @unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
    def test_the_layouts_match_the_stock_cdefs_under_luajit(self):
        text = FFI_H.read_text(encoding='utf-8', errors='replace')
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(BUILD_CORE)
        names = [list(t.values())[0] for t in lua.eval('X4LAN_UI_BUILD_CORE.LAYOUTS').values()]
        typedefs = []
        for line in text.splitlines():
            m = re.match(r'typedef (?:struct \{.*\}|[\w ]+) (\w+);$', line.strip())
            if m and (m.group(1) in names or m.group(1) in ('UniverseID', 'NPCSeed', 'BlacklistID', 'FightRuleID', 'BuildTaskID')):
                typedefs.append(line.strip())
        cdef = '\n'.join(typedefs)   # read from x4-re at test time only; nothing of it is kept here
        lua.globals().CDEF = cdef
        ok, missing = lua.eval('(function() local ffi=require("ffi");ffi.cdef(CDEF);return X4LAN_UI_BUILD_CORE.check_layouts(ffi) end)()')
        self.assertTrue(ok, missing)
        self.assertEqual(list(missing.values()), [])
        # A layout that differs refuses (another Lua state: a type of our own under a stock name).
        other = LuaRuntime(unpack_returned_tuples=True)
        other.execute(BUILD_CORE)
        ok, why = other.eval('(function() local ffi=require("ffi");ffi.cdef("typedef struct {float x; double y; float z;} Coord3D;");'
                             'return X4LAN_UI_BUILD_CORE.check_layouts(ffi) end)()')
        self.assertIsNone(ok)
        self.assertIn('Coord3D', why)


class CoreTests(unittest.TestCase):
    def test_a_rename_is_a_record_of_the_batch_and_runs_nowhere_before_the_apply(self):
        lua, g = lua_client()
        lua.execute("B=new_build();env.SetComponentName(make_id('100'),'Probe One')")
        added = uirep.values(g.build_added)
        self.assertEqual(len(added), 1)
        self.assertTrue(added[0].startswith('3:'))
        self.assertEqual(uirep.values(g.stock_calls), [])
        lua.execute("LOCAL=0;env.SetComponentName(make_id('100'),'Sim');LOCAL=nil")
        self.assertEqual(uirep.values(g.stock_calls), ['SetComponentName:100/Sim'])
        lua.execute("BUILD_ADD=0;BUILD_WHY='factions';env.SetComponentName(make_id('100'),'Two');BUILD_ADD=nil")
        self.assertTrue(any('UI_BUILD_REFUSED tick=- op=SetComponentName why=factions' in line for line in uirep.values(g.logs)))
        self.assertEqual(len(uirep.values(g.notices)), 1)

    def test_a_commit_scope_wraps_the_stock_menus_and_restores_the_globals(self):
        lua, g = lua_client()
        lua.execute(r'''
          B=new_build()
          shipyard=make_id('200')
          -- the stock menus call their globals; in the harness that table is env
          menu={name='ShipConfigurationMenu'}
          function menu.buttonConfirm(a)
            env.TransferPlayerMoneyTo(500,shipyard)
            env.SignalObject(shipyard,'repairs_initiate',make_id('100'),{100,{}})
            return 'r1',a
          end
          function menu.buttonInstallMod() env.TransferPlayerMoneyTo(1,shipyard);error('boom') end
          MENUS={menu,{name='MapMenu',buttonSellShips=function() env.TransferMoneyToPlayer(70,shipyard,'sellship') end},{name='OtherMenu',buttonConfirm=function() end}}
          before_money=env.TransferPlayerMoneyTo
          B.frame(40);B.frame(41)
        ''')
        logs = uirep.values(g.logs)
        self.assertEqual(sum('UI_BUILD_SCOPES menu=ShipConfigurationMenu functions=2' in line for line in logs), 1)   # once
        self.assertTrue(any('UI_BUILD_SCOPES menu=MapMenu functions=1' in line for line in logs))
        self.assertFalse(any('OtherMenu' in line for line in logs))
        flushes = g.flushes
        lua.execute("r1,r2=menu.buttonConfirm('x');MENUS[2].buttonSellShips()")
        self.assertEqual((g.r1, g.r2), ('r1', 'x'))
        self.assertEqual(g.flushes, flushes + 2)
        self.assertEqual([a.split(':')[0] for a in uirep.values(g.build_added)], ['1', '4', '2'])
        self.assertEqual(uirep.values(g.stock_calls), [])
        self.assertTrue(lua.eval('env.TransferPlayerMoneyTo==before_money'))
        ok, err = lua.eval('pcall(menu.buttonInstallMod)')
        self.assertFalse(ok)
        self.assertIn('boom', err)
        self.assertTrue(lua.eval('env.TransferPlayerMoneyTo==before_money'))
        self.assertIsNone(lua.eval('B.scope'))
        # Outside a local phase (simulation, the probe) a scope is no scope: the menu's calls go on as they are.
        lua.execute("LOCAL=0;menu.buttonConfirm('y');LOCAL=nil")
        self.assertEqual(len(uirep.values(g.stock_calls)), 2)

    def test_scoped_records_in_call_order_with_the_menus_globals(self):
        lua, g = lua_client()
        lua.execute(r'''
          B=new_build()
          shipyard=make_id('200')
          -- the stock menus call the globals; here env is that table
          local fn=function()
            env.TransferPlayerMoneyTo(500,shipyard)
            env.SignalObject(shipyard,'repairs_initiate',make_id('100'))
            env.SignalObject(shipyard,'other')
            env.SetComponentName(make_id('100'),'Renamed')
          end
          B.with_scope('test.confirm',fn)
          env.TransferPlayerMoneyTo(9,shipyard)   -- outside a scope: left to the next handler
        ''')
        added = [a.split(':')[0] for a in uirep.values(g.build_added)]
        self.assertEqual(added, ['1', '4', '3'])
        shipyard = lua.eval('tostring(shipyard)')
        self.assertEqual(uirep.values(g.stock_calls), [f'SignalObject:{shipyard}/other', f'TransferPlayerMoneyTo:9:{shipyard}'])

    def test_a_whole_batch_runs_its_lua_records_through_the_stock_functions_at_the_apply(self):
        lua, g = lua_client()
        lua.execute(r'''
          B=new_build()
          hex=assert(X4LAN_UI_REPLAY_CORE.encode_call({500,make_id('200')},2,R.ctx))
          PARTS={aa=0,bb=1,cc='drop'}
          STEPS={{'ran','AddBuildTask6=16962'},{'lua',1,hex},{'ran','SetBuildTaskTransferredMoney'},{'done','batch seq=0 records=3'}}
          DUE={[50]='1:80:aa;1:80:bb;2:80:cc'}
          R.apply(50)
        ''')
        calls = uirep.values(g.stock_calls)
        self.assertEqual(len(calls), 1)
        self.assertTrue(calls[0].startswith('TransferPlayerMoneyTo:500:'))
        self.assertEqual(uirep.values(g.lua_done), ['1:1'])
        logs = uirep.values(g.logs)
        self.assertTrue(any('UI_REPLAY_APPLIED tick=50 slot=1 op=80 result=applied what=part' in line for line in logs))
        self.assertTrue(any('UI_REPLAY_APPLIED tick=50 slot=1 op=80 result=applied what=batch seq=0 records=3' in line for line in logs))
        self.assertTrue(any('UI_BUILD_APPLIED tick=50 slot=1 what=AddBuildTask6=16962' in line for line in logs))
        self.assertTrue(any('UI_BUILD_APPLIED tick=50 slot=1 what=TransferPlayerMoneyTo(500,' in line for line in logs))
        self.assertTrue(any('UI_BUILD_DROPPED tick=50 slot=2 why=torn' in line for line in logs))
        self.assertTrue(any('UI_REPLAY_APPLIED tick=50 slot=2 op=80 result=refused' in line for line in logs))
        self.assertEqual(g.BYPASS, 0)
        self.assertFalse(lua.eval('B.applying'))

    def test_the_receipt_joins_the_replays_and_carries_the_wallet(self):
        lua, g = lua_client()
        lua.execute("B=new_build();MONEY=12345;receipt=R.frame(60)")
        self.assertTrue(g.receipt.endswith('|UIBUILD_1:60:0:0:M12345'), g.receipt)
        lua.execute("BUILD_ROWS='61:InstallWeaponMod:inventory,62:batch:queue_full';R.frame(62);BUILD_ROWS='63:RemoveBuildStorage:factions';R.frame(63)")
        logs = uirep.values(g.logs)
        self.assertTrue(any('UI_BUILD_REFUSED tick=61 op=InstallWeaponMod why=inventory' in line for line in logs))
        self.assertTrue(any('UI_BUILD_REFUSED tick=62 op=batch why=queue_full' in line for line in logs))
        self.assertTrue(any('UI_BUILD_REFUSED tick=63 op=RemoveBuildStorage why=factions' in line for line in logs))
        self.assertEqual(uirep.values(g.notices), ['Multiplayer: InstallWeaponMod is not shared for your slot yet - nothing happened.'])
        lua.execute("R.frame(120)")
        self.assertTrue(any('UI_BUILD_STATUS tick=120 ui_build=armed' in line for line in uirep.values(g.logs)))

    def test_the_script_ops_call_the_stock_entries_in_a_commit_scope(self):
        lua, g = lua_client()
        lua.execute(r'''
          ffi=require('ffi')
          C.AddBuildTask6=function(station,def,macro,loadout,price,crew,immediate,name,info)
            stock_calls[#stock_calls+1]='AddBuildTask6:'..tostring(station)..'/'..macro..'/'..price..'/'..name;return BT6 or 0x58ULL end
          C.SetBuildTaskTransferredMoney=function(id,v) stock_calls[#stock_calls+1]='SetBuildTaskTransferredMoney:'..tostring(id)..'/'..v end
          C.ReserveBuildPlot=function(sector,faction,set,pos,x,y,z) stock_calls[#stock_calls+1]='ReserveBuildPlot:'..faction..'/'..set..'/'..x;return 0 end
          C.DismantleShipMod=function(id) stock_calls[#stock_calls+1]='DismantleShipMod:'..tostring(id) end
          C.AddPlayerMoney=function(n) stock_calls[#stock_calls+1]='AddPlayerMoney:'..n end
          tools={gate=gate,C=C,env=env,ffi={new=function(t,v) return {type=t,v=v} end,string=function(s) return s end},
            saved_id=function(s) return 'station:'..s end,slot_id=function(s) return 'ship:'..s end,slot_lua=function(s) return make_id(tostring(100*s)) end,
            loadout=function(station,macro) return 'loadout' end}
          plan={{0,'buy','000000000001b5af','ship_arg_s_scout_01_a_macro',90000,'Bought'},{1,'rename',2,'Two'},{2,'plot','0000000000000729',0,0,-150000,4},
            {3,'dismantle',1}}
          marks={}
          B=new_build({script=X4LAN_UI_BUILD_CORE.script(plan,40,tools),fund={tick=36,amount=2000000},local_mark=function(on) marks[#marks+1]=on end})
          for t=36,44 do R.frame(t) end
        ''')
        self.assertEqual(uirep.values(g.stock_calls), ['AddPlayerMoney:2000000', 'AddBuildTask6:station:000000000001b5af/ship_arg_s_scout_01_a_macro/90000/Bought',
                                                       'SetBuildTaskTransferredMoney:88ULL/90000', 'ReserveBuildPlot:player/factory/4000',
                                                       'DismantleShipMod:ship:1'])
        added = [a.split(':')[0] for a in uirep.values(g.build_added)]
        self.assertEqual(added, ['1', '3'])                    # the money inside the buy's scope, then the rename
        self.assertEqual(uirep.values(g.marks), [0, 1] * 4)
        logs = uirep.values(g.logs)
        self.assertTrue(any('UI_BUILD_FUND tick=36 amount=2000000 money=10000' in line for line in logs))
        self.assertTrue(any('UI_BUILD_SCRIPT tick=40 op=buy ok=true result=88ULL' in line for line in logs))
        with self.assertRaises(Exception):
            lua.eval("X4LAN_UI_BUILD_CORE.script({{0,'sell'}},40,{})")


PROBE_BUILD = r'''
  gate.ui_guard_pins=function() return 1 end
  gate.ui_guard=function(mask) ui_calls[#ui_calls+1]='guard:'..mask;return 1,280 end
  gate.ui_guard_refusals=function() return nil end
  gate.ui_guard_status=function() return 'ui_guard=live' end
  gate.runtime_ids_probe=function(on) ui_calls[#ui_calls+1]='mark:'..on end
  gate.ui_replay_push=function(op,hex) return 1 end
  gate.ui_replay_local=function() return 1 end
  gate.ui_replay_saved=function() return nil end
  gate.ui_build=function(factions) ui_calls[#ui_calls+1]='build:'..factions;if BARM then return 0,BARM end;return 1 end
  gate.ui_build_flush=function() return 0 end
  gate.ui_build_refusals=function() return nil end
  gate.ui_build_status=function() return 'ui_build=armed' end
  gate.ui_build_frame=function(t) return 'UIBUILD_1:'..t..':0:0' end
  GetPlayerMoney=function() return 10000 end
  local f=require('ffi')
  f.sizeof=function(name) return LAYOUT_SIZE and LAYOUT_SIZE[name] end
  f.offsetof=function(name,field) return LAYOUT_OFFSET and LAYOUT_OFFSET[name..'.'..field] end
  LAYOUT_SIZE={};LAYOUT_OFFSET={}
  for _,e in ipairs(X4LAN_UI_BUILD_CORE.LAYOUTS) do
    LAYOUT_SIZE[e[1]]=e[2]
    for field,at in pairs(e[3]) do LAYOUT_OFFSET[e[1]..'.'..field]=at end
  end
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def client(self, slot=1, prelude='', **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel', 'build', 'assets'],
                   native_gate_ui_guard='all', native_gate_player_identity_plan={'mode': 'coop'})
        cfg.update(extra)
        return self.stock.StockClient(slot, slots_all=True, prelude=uirep.PROBE_MOCK + BUILD_CORE + PROBE_BUILD + prelude, **cfg)

    def test_the_families_arm_after_the_guard_and_join_the_receipt(self):
        clients = [self.client(k) for k in (1, 2)]
        self.stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertIn('arm:12289', calls)
            self.assertLess(calls.index('guard:16383'), calls.index('build:0'))
            logs = c.values('logs')
            self.assertTrue(any(f'UI_BUILD_ARMED slot={c.slot} families=12289 coop=true missing= script=0' in line for line in logs), logs[-5:])
            self.assertTrue(any('UIBUILD_1:12:0:0:M10000' in data for data in c.values('hashes')))

    def test_factions_mode_arms_slot_1_only_and_the_refusals(self):
        c = self.client(native_gate_player_identity_plan={'mode': 'factions'})
        self.assertIn('build:1', c.values('ui_calls'))
        c = self.client(native_gate_player_identity_plan=None)
        self.assertIn('build:1', c.values('ui_calls'))        # no plan: not known to be coop
        for extra, prelude, why in ((dict(native_gate_ui_guard=None), '', 'ui build refused: needs native_gate_ui_guard'),
                                    (dict(), "BARM='claim:AddBuildTask6'", 'ui build refused: claim:AddBuildTask6'),
                                    (dict(), "LAYOUT_SIZE.Coord3D=16", 'ui build refused: stock layout Coord3D size 16'),
                                    (dict(native_gate_ui_build_fund=-5), '', 'native_gate_ui_build_fund is whole credits'),
                                    (dict(native_gate_ui_build_script='x'), '', 'invalid native_gate_ui_build_script')):
            with self.subTest(why=why):
                c = self.client(prelude=prelude, **extra)
                self.assertTrue(any(why in line for line in c.errors()), c.values('logs')[-3:])

    def test_without_the_families_nothing_arms(self):
        clients = [self.client(k, native_gate_ui_replay=['travel']) for k in (1, 2)]
        self.stock.run(clients, 12)
        c = clients[0]
        self.assertEqual(c.errors(), [])
        self.assertFalse([call for call in c.values('ui_calls') if call.startswith('build:')])
        self.assertFalse(any('UIBUILD_1' in data for data in c.values('hashes')))


class OptionTests(unittest.TestCase):
    def test_rules(self):
        failures, config = uirep.failures, uirep.stock_config
        self.assertEqual(failures(config(native_gate_ui_replay="{'travel','build','assets'}", native_gate_ui_guard="'all'",
                                         native_gate_ui_build_fund='2000000', native_gate_ui_build_script='{{},{}}')), [])
        for rule, override in (
                ('native_gate_ui_replay build/assets need native_gate_ui_guard', dict(native_gate_ui_replay="{'build'}")),
                ('native_gate_ui_replay build/assets need native_gate_ui_guard', dict(native_gate_ui_replay="{'assets'}")),
                ('native_gate_ui_build_script needs native_gate_ui_replay build or assets', dict(native_gate_ui_replay="{'travel'}", native_gate_ui_build_script='{}')),
                ('native_gate_ui_build_fund needs native_gate_ui_replay build or assets', dict(native_gate_ui_build_fund='5')),
                ('native_gate_ui_build_fund must be whole credits 1..1e9', dict(native_gate_ui_replay="{'build'}", native_gate_ui_guard="'all'", native_gate_ui_build_fund='0')),
                ('native_gate_ui_build_fund must be whole credits 1..1e9', dict(native_gate_ui_replay="{'build'}", native_gate_ui_guard="'all'", native_gate_ui_build_fund='1.5'))):
            self.assertIn(rule, failures(config(**override)), override)


class BuilderTests(unittest.TestCase):
    def test_the_families_need_the_guard_and_the_overlay_brings_the_core(self):
        lines = builder_lines
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'travel','build','assets'}", lines('--ui-replay', 'travel,build,assets', '--ui-guard'))
        with self.assertRaises(ValueError):
            lines('--ui-replay', 'build')
        with tempfile.TemporaryDirectory() as d:
            out = uirep.BuilderTests.overlay(uirep.BuilderTests(), Path(d), 'b', '', '--ui-replay', 'travel,build', '--ui-guard')
            files = (out/'ui.xml.files').read_text().split()
            self.assertLess(files.index('ui/native_gate_ui_build_core.lua'), files.index('ui/native_gate_probe.lua'))
            self.assertEqual((out/'ui/native_gate_ui_build_core.lua').read_text(encoding='utf-8'), BUILD_CORE)


def builder_lines(*extra):
    players = uirep.players
    args = players.defaults(players.build_parser().parse_args(['config', '--players', '2', '--mode', 'coop', '--source', 'stock', '--present', *extra]))
    return players.config_block(args)[0]


if __name__ == '__main__':
    unittest.main()
