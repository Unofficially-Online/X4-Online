"""UI replay, faction family (lockstep_ui_faction.h, X4LAN_UI_FACTION in native_gate_ui_replay_core.lua; default off, stop 164).

Covers the mirrors (family bit, op, the claimed entries against the guard inventory and the generated table, every spec and
return against the stock prototypes, the struct layouts under LuaJIT with the stock typedefs, the registry); the Lua half
under LuaJIT with a mocked gate in both session modes (coop: every slot `player`; factions: slot 2 is x4lan_player2): a whole
record runs after the subject's authority with the slot faction's ID, parts and dropped records, the rows, the info line,
the state and the receipt; the scripted ops call the stock entries as the player information menu and the map do (a create's
token named by the later ops); the real probe arming the family after the guard with its pins first and the receipt joining
the digest; the option checker and the builder. The native half is tested in C: src/native/engine/test_lockstep_ui_faction.h."""
import os
from pathlib import Path
import re
import sys
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

ENGINE = ROOT/'src/native/engine'
FACTION_H = (ENGINE/'lockstep_ui_faction.h').read_text(encoding='utf-8')
REPLAY_H = (ENGINE/'lockstep_ui_replay.h').read_text(encoding='utf-8')
INPUTS_H = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
TABLE = (ENGINE/'lockstep_ui_guard_table.h').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))
FFI_H = X4RE/'data/x4_ffi.h'
BIT = 32768


def claims():
    body = re.search(r'uifac_claims\[\]=\{(.*?)\n\};', FACTION_H, re.S).group(1)
    rows = re.findall(r'\{"(\w+)","(\w+)",UIFAC_RET_(\w+),UIFAC_PRED_(\w+),UIFAC_K_(\w+),(\d),(-?\d+)\}', body)
    return [dict(name=n, spec=s, ret=r, pred=p, kind=k, entity=int(e), subject=int(j)) for n, s, r, p, k, e, j in rows]


def stock_typedefs(names):
    """The stock typedefs of `names` from x4-re (read at test time only; nothing of it is kept here)."""
    text = FFI_H.read_text(encoding='utf-8', errors='replace')
    out = []
    for line in text.splitlines():
        m = re.match(r'typedef (?:struct \{.*\}|[\w ]+) (\w+);$', line.strip())
        if m and m.group(1) in names:
            out.append(line.strip())
    return '\n'.join(out)


# The world both clients share: slot 1's ship 100 (`player`), slot 2's ship 200, station 300 (slot 2's faction), station 400
# (`player`). OWNERS['200'] and OWNERS['300'] say the mode: 'x4lan_player2' factions, 'player' coop.
WORLD = r'''
  SLOT_SHIPS={['100']=1,['200']=2}
  OWNERS={['100']='player',['200']='x4lan_player2',['300']='x4lan_player2',['400']='player'}
  env.GetComponentData=function(id,key)
    local d=type(id)=='cdata' and (tostring(id):gsub('ULL$','')) or tostring(id)
    if key=='owner' then return OWNERS[d] end
  end
  runs={};notes={};PARTS={}
  gate.ui_faction_part=function(slot,hex) local r=PARTS[hex] or {1,'SetFactionDefaultWeaponMode',''};return r[1],r[2],r[3] end
  gate.ui_faction_run=function(slot,faction,refused) runs[#runs+1]=slot..':'..faction..':'..tostring(refused);return refused and 0 or 1,refused and ('X skipped='..refused) or 'X' end
  gate.ui_faction_refusals=function() local r=FROWS;FROWS=nil;return r end
  gate.ui_faction_state=function() return STATE or 'UIFAC_1:b0.0,f0.0,t0.0;0.0.0,0.0.0;w111.00000000' end
  gate.ui_faction_status=function() return 'ui_faction=armed' end
  gate.ui_faction_frame=function() return (STATE or 'UIFAC_1:b0.0,f0.0,t0.0;0.0.0,0.0.0;w111.00000000')..';h00' end
  function faction_session(slot,extra)
    extra=extra or {}
    return X4LAN_UI_REPLAY_CORE.session({gate=gate,C=extra.C,env=env,log=log,mask=extra.mask or (1+32768),slot_ships=function() return SLOT_SHIPS end,
      own_slot=slot,slots=2,script=extra.script,notify=function(text) notes[#notes+1]=text end})
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


class MirrorTests(unittest.TestCase):
    def test_the_family_and_its_op_agree_everywhere(self):
        lua, _ = uirep.client()
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.FAMILY.faction'), BIT)
        self.assertIn(f'UIREP_FAMILY_FACTION={BIT}', INPUTS_H)
        families = sum(int(v) for v in re.search(r'UIREP_FAMILIES=([0-9|]+)', INPUTS_H).group(1).split('|'))
        self.assertEqual(families & BIT, BIT)
        self.assertLess(BIT, 1 << 16)   # the guard table's 'covered' field is 16 bits wide
        self.assertIn('faction', uirep.options.UI_REPLAY_FAMILIES)
        self.assertIn('faction', uirep.players.UI_REPLAY_FAMILIES)
        op = int(re.search(r'#define UIFAC_OP_PART (0x[0-9a-f]+)u', REPLAY_H).group(1), 16)
        self.assertEqual(lua.eval('X4LAN_UI_FACTION.OP'), op)
        self.assertEqual(op, 0x5a)
        self.assertRegex(REGISTRY, r'\n  32768 +faction ')
        self.assertIn('0x5a', REGISTRY)
        self.assertEqual(lua.eval('X4LAN_UI_REPLAY_CORE.families(true)'), 63)   # true does not include it
        self.assertEqual(lua.eval("X4LAN_UI_REPLAY_CORE.families({'faction'})"), BIT)

    def test_the_claims_are_inventoried_entries_no_family_covers(self):
        rows = claims()
        self.assertEqual(len(rows), 33)
        lua, _ = uirep.client()
        self.assertEqual(list(lua.eval('X4LAN_UI_FACTION.CLAIMS').values()), [c['name'] for c in rows])
        table = {m[0]: (m[1], int(m[2])) for m in re.findall(r'\{"(\w+)",0x[0-9a-f]+u,UIGUARD_FFI,UIGUARD_G_(\w+),(\d+)u,', TABLE)}
        for c in rows:
            entry = inv.INVENTORY.get(('ffi', c['name']))
            self.assertIsNotNone(entry, c['name'])
            self.assertIn(entry[1], ('faction', 'fleet', 'trade'), c['name'])
            self.assertEqual(entry[2], 0, c['name'])            # no family hooks below it: the claim is the only capture
            self.assertEqual(table[c['name']], (entry[1].upper(), 0), c['name'])
            if 'I' in c['spec']:
                self.assertEqual(c['spec'].count('I'), 1, c['name'])
                self.assertEqual(c['subject'], c['spec'].index('I'), c['name'])   # the one component is the subject
            else:
                self.assertEqual(c['subject'], -1, c['name'])
            self.assertEqual(c['pred'] == 'TOKEN', c['name'].startswith('Create'), c['name'])
            self.assertEqual(c['kind'] != 'NONE', 'r' in c['spec'] or c['spec'][0] in 'BGT', c['name'])
        names = {c['name'] for c in rows}
        # The whole faction group is the family's; the trade group's rules, overrides and module pause; the fleet group's
        # per-controllable rules, responses and resupply.
        self.assertEqual({n for (k, n), v in inv.INVENTORY.items() if v[1] == 'faction'} - names, set())
        for name in ('CreateTradeRule', 'UpdateTradeRule', 'RemoveTradeRule', 'SetPlayerTradeRuleDefault', 'UpdateSupplyOverrides',
                     'PauseProcessingModule', 'PauseProductionModule', 'SetPlayerTradeLoopCargoReservationSetting', 'SetControllableBlacklist',
                     'SetControllableFightRule', 'SetDefaultResponseToSignalForControllable', 'ResetResponseToSignalForControllable',
                     'SetDefensibleLoadoutLevel'):
            self.assertIn(name, names)
        # What other families own or what stays refused is not claimed here.
        for name in ('SetContainerTradeRule', 'AddPlayerMoney', 'SetBuildTaskTransferredMoney', 'TransferPlayerInventoryTo', 'SetFleetName',
                     'SetShipStance', 'SetEntityToPost'):
            self.assertNotIn(name, names)
        build = (ENGINE/'lockstep_ui_build.h').read_text(encoding='utf-8')
        missions = (ENGINE/'lockstep_ui_missions.h').read_text(encoding='utf-8')
        for name in names:
            self.assertNotIn(f'"{name}"', build)
            self.assertNotIn(f'"{name}"', missions)

    def test_the_entity_settings_are_the_player_entity_s(self):
        entity = {c['name'] for c in claims() if c['entity']}
        self.assertEqual(entity, {'SetPlayerShipsWaitForPlayer', 'SetPlayerTaxiWaitsForPlayer', 'SetPlayerGlobalLoadoutLevel',
                                  'SetPlayerTradeLoopCargoReservationSetting', 'SetPlayerIllegalWare', 'SetPlayerLogo', 'SetPlayerPaintTheme',
                                  'SetPlayerClothingTheme'})

    @unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
    def test_every_spec_and_return_matches_the_stock_prototype(self):
        text = FFI_H.read_text(encoding='utf-8', errors='replace')
        letters = {'UniverseID': 'I', 'BlacklistID': 'r', 'FightRuleID': 'r', 'TradeRuleID': 'r', 'const char*': 'sF', 'bool': 'b', 'float': 'f',
                   'BlacklistInfo2': 'B', 'FightRuleInfo': 'G', 'TradeRuleInfo': 'T', 'UILogo': 'L', 'SupplyOverride*': 'O', 'uint32_t': 'n'}
        returns = {'void': 'VOID', 'bool': 'BOOL', 'BlacklistID': 'ID', 'FightRuleID': 'ID', 'TradeRuleID': 'ID'}
        kinds = {'BlacklistID': 'BLACKLIST', 'FightRuleID': 'FIGHT', 'TradeRuleID': 'TRADE', 'BlacklistInfo2': 'BLACKLIST',
                 'FightRuleInfo': 'FIGHT', 'TradeRuleInfo': 'TRADE'}
        for c in claims():
            m = re.search(r'^(\w[\w\s\*]*?)\s*\b' + c['name'] + r'\((.*?)\);', text, re.M)
            self.assertIsNotNone(m, c['name'])
            self.assertEqual(returns[m.group(1).strip()], c['ret'], c['name'])
            params = [p.strip() for p in m.group(2).split(',')]
            self.assertEqual(len(params), len(c['spec']), c['name'])
            for letter, param in zip(c['spec'], params):
                ptype, pname = param.rsplit(' ', 1)
                ptype = (ptype + '*' * pname.count('*')).replace(' *', '*').strip()
                pname = pname.strip('*')
                self.assertIn(letter, letters[ptype], f"{c['name']} {param}")
                self.assertEqual(letter == 'F', pname == 'factionid', f"{c['name']} {param}")
                if ptype in kinds:
                    self.assertEqual(kinds[ptype], c['kind'], c['name'])
                if letter == 'f':
                    self.assertLess(c['spec'].index('f'), 2, c['name'])   # xmm0 or xmm1: the run's two float shapes

    @unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
    def test_the_layouts_match_the_stock_cdefs_under_luajit(self):
        lua, _ = uirep.client()
        names = [list(t.values())[0] for t in lua.eval('X4LAN_UI_FACTION.LAYOUTS').values()]
        lua.globals().CDEF = stock_typedefs(set(names) | {'FightRuleID'})
        ok, missing = lua.eval('(function() local ffi=require("ffi");ffi.cdef(CDEF);return X4LAN_UI_FACTION.check_layouts(ffi) end)()')
        self.assertTrue(ok, missing)
        self.assertEqual(list(missing.values()), [])
        other = LuaRuntime(unpack_returned_tuples=True)
        other.execute(uirep.CORE)
        ok, why = other.eval('(function() local ffi=require("ffi");ffi.cdef("typedef struct {int32_t amount; const char* macro;} SupplyOverride;");'
                             'return X4LAN_UI_FACTION.check_layouts(ffi) end)()')
        self.assertIsNone(ok)
        self.assertIn('SupplyOverride', why)
        # A type no menu declared yet is reported only.
        fresh = LuaRuntime(unpack_returned_tuples=True)
        fresh.execute(uirep.CORE)
        ok, missing = fresh.eval('X4LAN_UI_FACTION.check_layouts(require("ffi"))')
        self.assertTrue(ok)
        self.assertEqual(sorted(missing.values()), sorted(names))

    def test_the_native_layouts_and_managers(self):
        # The C mirrors carry the stock layouts as static asserts; the managers' globals are the three the creates use.
        self.assertIn('{0,0x6cf1850u,0x6cf1878u,0x6cf17d8u}', FACTION_H)
        self.assertIn('sizeof(uifac_blacklist)==72', FACTION_H)
        self.assertIn('sizeof(uifac_traderule)==40', FACTION_H)


class ApplyTests(unittest.TestCase):
    def apply(self, coop, slot, subject, extra=''):
        lua, g = client(coop)
        lua.execute(f"S=faction_session(1);PARTS['aa']={{1,'SetControllableBlacklist','{subject}'}};{extra}DUE={{[50]='{slot}:90:aa'}};S.apply(50)")
        return values(g.runs), values(g.results), [line for line in values(g.logs) if 'UI_REPLAY_APPLIED' in line]

    def test_a_whole_record_runs_with_the_slot_faction_s_id(self):
        for coop, faction in ((False, 'x4lan_player2'), (True, 'player')):
            runs, results, logs = self.apply(coop, 2, '300')
            self.assertEqual(runs, ['2:' + faction + ':nil'], coop)
            self.assertEqual(results, ['50:1'])
            self.assertIn('slot=2 op=90 result=applied what=X', logs[0])
        runs, _, _ = self.apply(False, 1, '400')
        self.assertEqual(runs, ['1:player:nil'])

    def test_the_authority_rule(self):
        cases = ((False, 2, '400', 'not_owner'),   # factions: the player's station is not slot 2's
                 (True, 2, '400', 'nil'),          # coop: it is everyone's
                 (False, 2, '100', 'slot_ship'),   # another slot's ship, both modes
                 (True, 2, '100', 'slot_ship'),
                 (False, 2, '200', 'nil'),         # the own ship
                 (False, 2, '', 'nil'),            # a faction-wide setting names no component
                 (False, 2, 'gone', 'nil'))        # the native run skips it as gone, the same everywhere
        for coop, slot, subject, why in cases:
            with self.subTest(coop=coop, subject=subject):
                runs, results, logs = self.apply(coop, slot, subject)
                self.assertEqual(runs, [f'{slot}:' + ('player' if coop or slot == 1 else 'x4lan_player2') + ':' + why])
                self.assertEqual(results, ['50:' + ('1' if why == 'nil' else '0')])
        runs, results, logs = self.apply(False, 2, '300', "SLOT_SHIPS={['100']=1};")
        self.assertEqual(runs, ['2::no_ship'])   # no ship: no faction either; the native run skips it

    def test_parts_and_a_dropped_record(self):
        lua, g = client()
        lua.execute("S=faction_session(1);PARTS['b0']={0};PARTS['b1']={nil,'torn'};DUE={[50]='2:90:b0;2:90:b1'};S.apply(50)")
        self.assertEqual(values(g.runs), [])
        self.assertEqual(values(g.results), ['50:1', '50:0'])
        logs = values(g.logs)
        self.assertTrue(any('what=part' in line for line in logs))
        self.assertTrue(any('UI_FACTION_DROPPED tick=50 slot=2 why=torn' in line for line in logs))

    def test_the_rows_the_info_line_and_the_receipt(self):
        lua, g = client()
        lua.execute("S=faction_session(2);FROWS='101:SetPlayerShipsWaitForPlayer:player_entity,101:SetPlayerLogo:player_entity';r1=S.frame(119)")
        logs = values(g.logs)
        self.assertIn('UI_FACTION_REFUSED tick=101 op=SetPlayerShipsWaitForPlayer why=player_entity', logs)
        self.assertEqual(values(g.notes), ['Multiplayer: SetPlayerLogo is not shared for your slot yet - nothing happened.'])
        self.assertTrue(g.r1.endswith('|UIFAC_1:b0.0,f0.0,t0.0;0.0.0,0.0.0;w111.00000000;h00'))
        lua.execute("FROWS='150:SetPlayerLogo:player_entity';S.frame(150)")
        self.assertEqual(len(values(g.notes)), 1)   # the info line at most every 90 ticks
        lua.execute("FROWS='239:SetPlayerPaintTheme:player_entity';STATE='UIFAC_1:b1.1,f0.0,t0.0;0.0.0,1.0.0;w111.00000000';S.frame(240)")
        self.assertEqual(len(values(g.notes)), 2)
        states = [line for line in values(g.logs) if 'UI_FACTION_STATE' in line]
        self.assertEqual(len(states), 2)
        self.assertIn('UI_FACTION_STATE tick=240 UIFAC_1:b1.1,f0.0,t0.0;0.0.0,1.0.0;w111.00000000', states[1])
        self.assertTrue(any(line.startswith('UI_FACTION_STATUS tick=240 ui_faction=armed lua=0/0/0/0/4') for line in values(g.logs)))

    def test_without_the_family_nothing_of_it(self):
        lua, g = client()
        lua.execute('S=faction_session(1,{mask=1});has=S.ops[0x5a]~=nil;r=S.frame(10)')
        self.assertFalse(g.has)
        self.assertNotIn('UIFAC_1', g.r)


class ViewTests(unittest.TestCase):
    """The faction view: the player information menu's display functions and Helper's rule lists run inside it."""
    PRELUDE = r'''
      VIEW_DEPTH=0;view_calls={};seen={}
      gate.ui_faction_view=function(on)
        view_calls[#view_calls+1]=on
        if on==1 then if VIEW_DEPTH>0 then return 2 end;if VIEW_RESULT then return VIEW_RESULT end;VIEW_DEPTH=1;return 1 end
        VIEW_DEPTH=0;return 1
      end
      env.Helper={getBlackLists=function() seen[#seen+1]='bl:'..VIEW_DEPTH;return {1,2},nil,3 end,
        getFightRules=function() error('stock failure') end,updateTradeRules=function() seen[#seen+1]='tr:'..VIEW_DEPTH end,other=function() return 'x' end}
      INFO={name='PlayerInfoMenu',refreshInfoFrame=function() seen[#seen+1]='refresh:'..VIEW_DEPTH;env.Helper.getBlackLists() end,
        onShowMenu=function() seen[#seen+1]='show:'..VIEW_DEPTH end,onUpdate=function() end,buttonBlacklistConfirm=function() seen[#seen+1]='confirm:'..VIEW_DEPTH end}
      MENUS={INFO,{name='MapMenu',refreshInfoFrame=function() end}}
      function view_session(slot)
        return X4LAN_UI_REPLAY_CORE.session({gate=gate,env=env,log=log,mask=1+32768,slot_ships=function() return SLOT_SHIPS end,own_slot=slot,slots=2,
          menus=function() return MENUS end})
      end
    '''

    def test_the_display_functions_run_inside_the_view(self):
        lua, g = client()
        lua.execute(self.PRELUDE + "S=view_session(2);S.frame(10);S.frame(11)")
        logs = values(g.logs)
        self.assertIn('UI_FACTION_VIEW_WRAPPED table=Helper functions=3', logs)
        self.assertIn('UI_FACTION_VIEW_WRAPPED table=PlayerInfoMenu functions=3', logs)
        self.assertEqual(sum('UI_FACTION_VIEW_WRAPPED' in line for line in logs), 2)   # once each, not again at 11
        lua.execute("a,b,c=env.Helper.getBlackLists();INFO.refreshInfoFrame();INFO.onShowMenu();INFO.buttonBlacklistConfirm();x=env.Helper.other()")
        self.assertEqual(values(g.seen), ['bl:1', 'refresh:1', 'bl:1', 'show:1', 'confirm:0'])   # a click handler is not inside it
        self.assertEqual(values(g.view_calls), [1, 0, 1, 1, 0, 1, 0])                          # the nested call does not close it
        self.assertEqual((list(g.a.values()), g.b, g.c, g.x), ([1, 2], None, 3, 'x'))
        lua.execute("ok,why=pcall(env.Helper.getFightRules)")
        self.assertFalse(g.ok)
        self.assertIn('stock failure', g.why)
        self.assertEqual(g.VIEW_DEPTH, 0)                                                          # closed on the error too
        # A second session (a rerun) does not wrap again; a view the native half does not open is not closed.
        lua.execute("S2=view_session(2);S2.frame(12);view_calls={};VIEW_RESULT=0;env.Helper.updateTradeRules()")
        self.assertEqual(values(g.view_calls), [1])
        self.assertEqual(sum('UI_FACTION_VIEW_WRAPPED' in line for line in values(g.logs)), 2)


@unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
class ScriptTests(unittest.TestCase):
    """The scripted ops through stand-ins of the stock entries (ffi.C stand-in), the struct contents as a menu fills them."""
    def test_the_ops_call_the_stock_entries_as_the_menus_do(self):
        lua, g = client()
        lua.globals().CDEF = stock_typedefs({'BlacklistInfo2', 'FightRuleInfo', 'UIFightRuleSetting', 'TradeRuleInfo', 'SignalInfo', 'ResponseInfo',
                                             'FightRuleID', 'BlacklistID', 'TradeRuleID'})
        lua.execute(r'''
          ffi.cdef(CDEF)
          calls={}
          local function s(p) return p~=nil and ffi.string(p) or 'nil' end
          local function rec(text) calls[#calls+1]=text end
          FC={GetNumAllSignals=function() return 2 end,
            GetAllSignals=function(buf,n) buf[0].id='attacked';buf[1].id='scanned';return 2 end,
            GetNumAllResponsesToSignal=function(sig) return 2 end,
            GetAllResponsesToSignal=function(buf,n,sig) buf[0].id='ignore';buf[1].id='flee';return 2 end,
            CreateBlacklist2=function(i) rec('CreateBlacklist2:'..s(i.type)..'/'..s(i.name)..'/'..s(i.relation)..'/'..i.id);return 0x7f020001 end,
            UpdateBlacklist2=function(i) rec('UpdateBlacklist2:'..i.id..'/'..s(i.name)) end,
            SetPlayerBlacklistDefault=function(id,t,g,v) rec('SetPlayerBlacklistDefault:'..id..'/'..t..'/'..g..'/'..tostring(v)) end,
            SetControllableBlacklist=function(c,id,t,v) rec('SetControllableBlacklist:'..tostring(c)..'/'..id..'/'..t..'/'..tostring(v)) end,
            RemoveBlacklist=function(id) rec('RemoveBlacklist:'..id) end,
            CreateFightRule=function(i) rec('CreateFightRule:'..s(i.name)..'/'..i.numfactions..'/'..s(i.factions[0].factionid)..'/'..s(i.factions[0].civiliansetting));return 0x7f020002 end,
            SetPlayerFightRuleDefault=function(id,t,v) rec('SetPlayerFightRuleDefault:'..id..'/'..t) end,
            SetControllableFightRule=function(c,id,t,v) rec('SetControllableFightRule:'..id..'/'..t) end,
            RemoveFightRule=function(id) rec('RemoveFightRule:'..id) end,
            CreateTradeRule=function(i) rec('CreateTradeRule:'..s(i.name)..'/'..i.numfactions..'/'..s(i.factions[0])..'/'..s(i.factions[1])..'/'..tostring(i.iswhitelist));return 0x7f020003 end,
            SetPlayerTradeRuleDefault=function(id,t,v) rec('SetPlayerTradeRuleDefault:'..id..'/'..t) end,
            RemoveTradeRule=function(id) rec('RemoveTradeRule:'..id) end,
            SetDefaultResponseToSignalForFaction2=function(r,a,sig,f,p) rec('Faction2:'..r..'/'..tostring(a)..'/'..sig..'/'..f..'/'..p);return true end,
            SetDefaultResponseToSignalForControllable=function(r,a,sig,c) rec('Controllable:'..r..'/'..sig..'/'..tostring(c));return true end,
            ResetResponseToSignalForControllable=function(sig,c) rec('Reset:'..sig..'/'..tostring(c));return true end,
            SetPlayerShipsWaitForPlayer=function(v) rec('ShipsWait:'..tostring(v)) end,
            SetPlayerGlobalLoadoutLevel=function(v) rec('Loadout:'..v) end,
            SetFactionDefaultWeaponMode=function(f,m) rec('WeaponMode:'..f..'/'..m) end,
            SetPlayerIllegalWare=function(w,v) rec('Illegal:'..w..'/'..tostring(v)) end,
            SetDefensibleLoadoutLevel=function(c,v) rec('Resupply:'..tostring(c)..'/'..v) end}
          tools={gate=gate,C=FC,env=env,own_slot=2,slot_id=function(s) return s*100 end}
          X4LAN_UI_FACTION.last={}
          plan={{0,'fac_blacklist','Keep out','sectortravel',true},{1,'fac_blacklist_name','Keep out 2'},{2,'fac_blacklist_ship'},
                {3,'fac_fightrule','Defend','argon',true},{4,'fac_fightrule_ship'},{5,'fac_traderule','Own only',{'player','argon'},true,true},
                {6,'fac_response',1,2},{7,'fac_ship_response',2,1},{8,'fac_ship_response_reset',1},
                {9,'fac_setting','ships_wait',false},{10,'fac_setting','loadout',0.5},{11,'fac_setting','weapon_mode','attack'},
                {12,'fac_setting','illegal','spacefuel',1},{13,'fac_setting','resupply',0.25},
                {14,'fac_blacklist_remove'},{15,'fac_fightrule_remove'},{16,'fac_traderule_remove'}}
          S=faction_session(2,{script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)})
          for t=40,56 do S.frame(t) end
        ''')
        token = str(0x7f020001)
        self.assertEqual(values(g.calls), [
            'CreateBlacklist2:sectortravel/Keep out//0', f'SetPlayerBlacklistDefault:{token}/sectortravel/civilian/true',
            f'UpdateBlacklist2:{token}/Keep out 2', f'SetControllableBlacklist:200/{token}/sectortravel/true',
            'CreateFightRule:Defend/1/argon/', f'SetPlayerFightRuleDefault:{0x7f020002}/attack', f'SetControllableFightRule:{0x7f020002}/attack',
            'CreateTradeRule:Own only/2/player/argon/true', f'SetPlayerTradeRuleDefault:{0x7f020003}/buy', f'SetPlayerTradeRuleDefault:{0x7f020003}/sell',
            'Faction2:flee/false/attacked/player/', 'Controllable:ignore/scanned/200', 'Reset:attacked/200',
            'ShipsWait:false', 'Loadout:0.5', 'WeaponMode:player/attack', 'Illegal:spacefuel/true', 'Resupply:200/0.25',
            f'RemoveBlacklist:{token}', f'RemoveFightRule:{0x7f020002}', f'RemoveTradeRule:{0x7f020003}'])
        rows = [line for line in values(g.logs) if 'UI_REPLAY_SCRIPT' in line]
        self.assertEqual(len(rows), 17)
        self.assertIn(f'op=fac_blacklist ok=true result={token}', rows[0])
        self.assertIn('op=fac_response ok=true result=attacked/flee', rows[6])
        with self.assertRaises(Exception):
            lua.execute("X4LAN_UI_FACTION.script_call({1,'fac_nope'},tools)")
        lua.execute("X4LAN_UI_FACTION.last={};f=X4LAN_UI_FACTION.script_call({1,'fac_blacklist_ship'},tools);ok,why=pcall(f)")
        self.assertFalse(g.ok)
        self.assertIn('no blacklist created yet', g.why)


PROBE_FACTION = uirep.PROBE_MOCK + r'''
  gate.ui_guard_pins=function() return 1 end
  gate.ui_guard=function(mask) ui_calls[#ui_calls+1]='guard:'..mask;return 1,283 end
  gate.ui_guard_refusals=function() return nil end
  gate.ui_guard_status=function() return 'ui_guard=live' end
  gate.runtime_ids_probe=function(on) ui_calls[#ui_calls+1]='mark:'..on end
  gate.ui_replay_push=function(op,hex) return 1 end
  gate.ui_replay_local=function() return 0 end
  gate.ui_replay_saved=function() return nil end
  gate.ui_faction_pins=function() ui_calls[#ui_calls+1]='faction_pins';return FPINS or 1,FPINS and 'pins_ui_faction:371370' or nil end
  gate.ui_faction=function() ui_calls[#ui_calls+1]='faction';if FARM then return 0,FARM end;return 1,33 end
  gate.ui_faction_refusals=function() return nil end
  gate.ui_faction_state=function() return 'UIFAC_1:b1.1,f0.0,t0.0;1.0.0,0.0.0;w111.00000000' end
  gate.ui_faction_status=function() return 'ui_faction=armed' end
  gate.ui_faction_frame=function() return 'UIFAC_1:b1.1,f0.0,t0.0;1.0.0,0.0.0;w111.00000000;h00' end
  GetComponentData=function() return 'player' end
  local f=require('ffi')
  f.sizeof=function(name) return FLAYOUT_SIZE[name] end
  f.offsetof=function(name,field) return FLAYOUT_OFFSET[name..'.'..field] end
  FLAYOUT_SIZE={};FLAYOUT_OFFSET={}
  for _,e in ipairs(X4LAN_UI_FACTION.LAYOUTS) do
    FLAYOUT_SIZE[e[1]]=e[2]
    for field,at in pairs(e[3]) do FLAYOUT_OFFSET[e[1]..'.'..field]=at end
  end
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def client(self, slot=1, prelude='', **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel', 'faction'], native_gate_ui_guard='all')
        cfg.update(extra)
        return self.stock.StockClient(slot, slots_all=True, prelude=PROBE_FACTION + prelude, **cfg)

    def test_the_family_arms_after_the_guard_with_its_pins_first_and_joins_the_receipt(self):
        clients = [self.client(k) for k in (1, 2)]
        self.stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertLess(calls.index('faction_pins'), calls.index(f'arm:{BIT+1}'))
            self.assertLess(calls.index('guard:16383'), calls.index('faction'))
            logs = c.values('logs')
            self.assertTrue(any('UI_FACTION_PINS ok=1' in line for line in logs))
            self.assertTrue(any(f'UI_FACTION_ARMED slot={c.slot} families={BIT+1} claims=33' in line for line in logs), logs[-5:])
            self.assertTrue(any('UIFAC_1:b1.1,f0.0,t0.0;1.0.0,0.0.0;w111.00000000;h00' in data for data in c.values('hashes')))

    def test_refusals(self):
        for extra, prelude, why in ((dict(native_gate_ui_guard=None), '', 'ui faction refused: needs native_gate_ui_guard'),
                                    (dict(), "FARM='claim_taken:CreateBlacklist2'", 'ui faction refused: claim_taken:CreateBlacklist2'),
                                    (dict(), 'FPINS=0', 'ui faction refused: pins_ui_faction:371370'),
                                    (dict(), 'FLAYOUT_SIZE.TradeRuleInfo=48', 'ui faction refused: stock layout TradeRuleInfo size 48')):
            with self.subTest(why=why):
                c = self.client(prelude=prelude, **extra)
                self.assertTrue(any(why in line for line in c.errors()), c.values('logs')[-3:])

    def test_without_the_family_nothing_is_asked(self):
        clients = [self.client(k, native_gate_ui_replay=['travel']) for k in (1, 2)]
        self.stock.run(clients, 12)
        c = clients[0]
        self.assertEqual(c.errors(), [])
        self.assertFalse({'faction_pins', 'faction'} & set(c.values('ui_calls')))
        self.assertFalse(any('UIFAC_1' in data for data in c.values('hashes')))


class OptionTests(unittest.TestCase):
    def test_rules(self):
        failures, config = uirep.failures, uirep.stock_config
        self.assertEqual(failures(config(native_gate_ui_replay="{'travel','faction'}", native_gate_ui_guard="'all'")), [])
        self.assertEqual(failures(config(native_gate_ui_replay="{'faction','trade','build','assets'}", native_gate_ui_guard="{'faction'}")), [])
        self.assertIn('native_gate_ui_replay faction needs native_gate_ui_guard', failures(config(native_gate_ui_replay="{'faction'}")))

    def test_builder(self):
        lines = uirep.BuilderTests().lines('--ui-replay', 'travel,faction', '--ui-guard', 'all')
        self.assertIn("X4LAN_CONFIG.native_gate_ui_replay = {'travel','faction'}", lines)
        with self.assertRaises(ValueError):
            uirep.BuilderTests().lines('--ui-replay', 'faction')


if __name__ == '__main__':
    unittest.main()
