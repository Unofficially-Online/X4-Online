"""Player settings per slot (lockstep_player_settings.h; native_gate_player_settings_per_slot, default off, stop 164 through the
UI replay; the Lua half rides X4LAN_UI_FACTION in native_gate_ui_replay_core.lua).

Covers the mirrors (the fields, readers and getters against the header's evidence and x4-re's names and prototypes, the pins
against the pinned image when it is at hand, the registry); the Lua half under LuaJIT with a mocked gate in factions mode
(this client's slot faction told to the native half once, the state and status rows, the spawn check on every slot, nothing
of it without the key); the scripted paint, clothing and logo ops through stand-ins of the stock entries; the real probe
arming it after the faction family with its pins first, and its refusals; the option checker. The native half is tested in
C: src/native/engine/test_lockstep_player_settings.h (stand-ins) and on the pinned image."""
import hashlib
import os
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import test_native_gate_ui_faction as fac  # noqa: E402

ENGINE = ROOT/'src/native/engine'
PSS_H = (ENGINE/'lockstep_player_settings.h').read_text(encoding='utf-8')
FACTION_H = (ENGINE/'lockstep_ui_faction.h').read_text(encoding='utf-8')
PRESENT_H = (ENGINE/'lockstep_pilot_present.h').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
X4RE = Path(os.environ.get('X4LAN_X4RE', ROOT.parent/'x4-re'))
FFI_H = X4RE/'data/x4_ffi.h'
SYMBOLS = X4RE/'symbols.csv'
X4_EXE = X4RE/'bin/X4.exe'
values = fac.values


def regions():
    body = re.search(r'pss_regions\[PSS_REGIONS\]=\{(.*?)\};', PSS_H, re.S).group(1)
    return [(int(o, 16), int(s, 16), int(a, 16), n) for o, s, a, n in re.findall(r'\{(0x[0-9a-f]+)u,(0x[0-9a-f]+)u,(0x[0-9a-f]+)u,"(\w+)"\}', body)]


def getters():
    body = re.search(r'pss_args\[PSS_ARGS\]=\{(.*?)\};', PSS_H, re.S).group(1)
    return [(int(r, 16), int(a), int(n), name) for r, a, n, name in re.findall(r'\{(0x[0-9a-f]+)u,(\d),(\d),\{[^}]*\},"(\w+)"\}', body)]


def pins():
    body = re.search(r'pss_pin_table\[\]=\{(.*?)\};', PSS_H, re.S).group(1)
    return [(int(r, 16), int(s, 16), h) for r, s, h in re.findall(r'\{(0x[0-9a-f]+)u,(0x[0-9a-f]+),"([0-9a-f]{64})"\}', body)]


class MirrorTests(unittest.TestCase):
    def test_the_fields_a_slot_owns(self):
        rows = regions()
        self.assertEqual([n for *_, n in rows], ['themes', 'loadout_level', 'waits', 'illegal_wares', 'loop_reserve'])
        self.assertEqual([(o, s) for o, s, _, _ in rows], [(0x6bb8, 0x20), (0x8a6a8, 0x04), (0x8a6b4, 0x02), (0x8a6d0, 0x30), (0x8a720, 0x01)])
        # Not the global loadout quantity (+0x8a6ac) or quality (+0x8a6b0): X4's own heap bytes (test below).
        for field in (0x8a6ac, 0x8a6b0):
            self.assertFalse(any(o <= field < o + s for o, s, _, _ in rows), hex(field))
        # Each region's place in a slot's block: inside the block, apart from the others; the vector 16-aligned.
        block = int(re.search(r'#define PSS_BLOCK (0x[0-9a-f]+)u', PSS_H).group(1), 16)
        spans = sorted((a, a + s) for _, s, a, _ in rows)
        for (a, b), (c, _) in zip(spans, spans[1:]):
            self.assertLessEqual(b, c)
        self.assertLessEqual(spans[-1][1], block)
        self.assertEqual(rows[3][2] % 16, 0)
        # The faction family's own offsets of the four simple settings fall inside them.
        for name in ('UIFAC_E_SHIPS_WAIT', 'UIFAC_E_TAXI_WAITS', 'UIFAC_E_LOOP_RESERVE', 'UIFAC_E_LOADOUT'):
            at = int(re.search(rf'#define {name} (0x[0-9a-f]+)u', FACTION_H).group(1), 16)
            self.assertTrue(any(o <= at < o + s for o, s, _, _ in rows), name)

    def test_the_family_and_the_presentation_call_it(self):
        self.assertIn('#include "lockstep_player_settings.h"', FACTION_H)
        self.assertIn('pss_apply_enter(slot)', FACTION_H)
        self.assertIn('pss_view_enter(pstk_capture_slot)', FACTION_H)
        self.assertIn('if(!pss_pass(tick))', FACTION_H)
        self.assertIn('if(pres_settings_hook) pres_settings_hook(1);', PRESENT_H)
        self.assertIn('if(pres_settings_hook) pres_settings_hook(0);', PRESENT_H)
        self.assertNotRegex(PSS_H, r'#define \w*STOP\w* ')   # no stop reason of its own: 164 through the replay

    def test_the_registry(self):
        self.assertIn('native_gate_player_settings_per_slot', REGISTRY)
        self.assertIn('lockstep_player_settings.h', REGISTRY)
        for rva in ('0x6d83b0', '0x6bf530', '0x8fa710'):
            self.assertIn(rva, REGISTRY)

    @unittest.skipUnless(X4_EXE.is_file(), 'the pinned X4.exe is not at hand')
    def test_loadout_quantity_and_quality_are_no_slot_s(self):
        # Census f3set: slot 2's table took X4's player's +0x8a6ac/+0x8a6b0 as client 2's heap had them ("a3b25632"; client
        # 1's zero), ;P2 differed from tick 1. X4's code names the two fields only in the keyword accessors (0x1548c0/d0,
        # 0xb61560/90), the getters and the setters: its player's constructor, load and save never touch them, so they are
        # each process's own bytes and no slot takes them.
        import pefile
        pe = pefile.PE(str(X4_EXE), fast_load=True)
        text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
        data, base = text.get_data(), text.VirtualAddress

        def sites(disp):
            pattern, out = disp.to_bytes(4, 'little'), []
            at = data.find(pattern)
            while at >= 0:
                out.append(base + at)
                at = data.find(pattern, at + 1)
            return out
        self.assertEqual(sites(0x8a6ac), [0x1548c4, 0x1d7937, 0x1dcf97, 0xb61564])
        self.assertEqual(sites(0x8a6b0), [0x1548d4, 0x1d7967, 0x1dcfc7, 0xb61594])
        image = pe.get_memory_mapped_image()
        # The constructor writes the level and both waits (and nothing at +0x8a6ac..+0x8a6b3); the load reads the level.
        self.assertEqual(bytes(image[0x6faffe:0x6fb00d]), bytes.fromhex('899ea8a60800' '66c786b4a608000101'))
        self.assertEqual(bytes(image[0x6fdb79:0x6fdb83]), bytes.fromhex('f3410f118424a8a60800'))

    @unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
    def test_the_getters_take_the_faction_id_where_the_stub_rewrites_it(self):
        text = FFI_H.read_text(encoding='utf-8', errors='replace')
        rows = getters()
        self.assertEqual(len(rows), 4)
        for rva, arg, prologue, name in rows:
            m = re.search(r'^\S[^\n(]*\b' + name + r'\((.*?)\);', text, re.M)
            self.assertIsNotNone(m, name)
            params = [p.strip() for p in m.group(1).split(',')]
            self.assertEqual(params[arg], 'const char* factionid', name)
            self.assertIn(prologue, (5, 6, 7), name)

    @unittest.skipUnless(SYMBOLS.is_file(), 'x4-re symbols.csv not at hand')
    def test_the_getters_and_setters_are_the_exports_of_their_names(self):
        names = {}
        for line in SYMBOLS.read_text(encoding='utf-8', errors='replace').splitlines()[1:]:
            parts = line.split(',')
            names.setdefault(int(parts[0], 16), parts[2])
        for rva, _, _, name in getters():
            self.assertEqual(names.get(rva), name)
        for rva, name in ((0x1b9810, 'SetPlayerPaintTheme'), (0x1b9280, 'SetPlayerClothingTheme'), (0x1b95c0, 'SetPlayerIllegalWare'),
                          (0x1b96d0, 'SetPlayerLogo'), (0x1a48c0, 'GetPlayerPaintTheme'), (0x1a4200, 'GetPlayerClothingTheme'),
                          (0x18bda0, 'GetCurrentPlayerLogo'), (0x1dcf70, 'SetPlayerGlobalLoadoutQuantity'), (0x1d7910, 'GetPlayerGlobalLoadoutQuantity')):
            self.assertEqual(names.get(rva), name)
            self.assertTrue(any(r == rva for r, _, _ in pins()), name)

    @unittest.skipUnless(X4_EXE.is_file(), 'the pinned X4.exe is not at hand')
    def test_the_pins_hold_on_the_pinned_image(self):
        import pefile
        pe = pefile.PE(str(X4_EXE), fast_load=True)
        image = pe.get_memory_mapped_image()
        rows = pins()
        self.assertEqual(len(rows), 21)
        for rva, size, digest in rows:
            self.assertEqual(hashlib.sha256(bytes(image[rva:rva + size])).hexdigest(), digest, hex(rva))
        # The entries the stubs replace begin with the bytes the header expects.
        self.assertEqual(bytes(image[0x6d83b0:0x6d83b5]), bytes([0x48, 0x83, 0xec, 0x28, 0x48]))
        for rva in (0x6bf530, 0x8fa710):
            self.assertEqual(bytes(image[rva:rva + 5]), bytes([0x48, 0x89, 0x5c, 0x24, 0x08]))
        body = re.search(r'pss_args\[PSS_ARGS\]=\{(.*?)\};', PSS_H, re.S).group(1)
        for rva, n, data in re.findall(r'\{(0x[0-9a-f]+)u,\d,(\d),\{([^}]*)\}', body):
            expect = bytes(int(b, 16) for b in data.split(','))
            self.assertEqual(len(expect), int(n))
            self.assertEqual(bytes(image[int(rva, 16):int(rva, 16) + len(expect)]), expect, rva)


# A session of the faction family with the key: client 2 is x4lan_player2's (factions mode, fac.WORLD).
PSS_WORLD = r'''
  pss_calls={};STATE_TEXT='slot=1 wait=1/1 slot=2 wait=0/1';spawns={};bypass={}
  gate.player_settings_faction=function(id) pss_calls[#pss_calls+1]='faction:'..id;return 1 end
  gate.player_settings_state=function() return STATE_TEXT end
  gate.player_settings_status=function() return 'player_settings=live' end
  gate.player_settings_themes=function(slot) return slot==2 and 'painttheme_two' or 'painttheme_one','cloth' end
  gate.ui_replay_bypass=function(on) bypass[#bypass+1]=on end
  env.ConvertStringTo64Bit=function(s) return tonumber(s) end
  env.ConvertIDTo64Bit=function(id) return id end
  local gcd=env.GetComponentData
  env.GetComponentData=function(id,key)
    if key=='sectorid' then return 729 end
    if type(id)=='cdata' then return end   -- as X4's: nothing for the C call's uint64 (census f3set)
    if type(id)=='number' and id>=9000 then return key=='owner' and SPAWN_OWNER[id] or nil end
    return gcd(id,key)
  end
  SPAWN_OWNER={}
  PC={GetObjectPositionInSector=function(id) return {x=1,y=2,z=3} end,
    SpawnObjectAtPos2=function(macro,sector,at,owner) local id=9000+#spawns+1;spawns[#spawns+1]=macro..'/'..sector..'/'..at.z..'/'..owner;SPAWN_OWNER[id]=owner;return ffi.new('uint64_t',id) end,
    GetInstalledPaintMod=function(id,mod) mod.Ware=SPAWN_OWNER[tonumber(id)]=='x4lan_player2' and 'paintmod_two' or 'paintmod_one';return true end,
    GetPaintThemeMod=function(theme,faction,mod) mod.Ware=theme=='painttheme_two' and 'paintmod_two' or 'paintmod_one';return true end}
  function pss_session(slot,spawn)
    return X4LAN_UI_REPLAY_CORE.session({gate=gate,C=PC,env=env,log=log,mask=1+32768,slot_ships=function() return SLOT_SHIPS end,
      own_slot=slot,slots=2,notify=function(text) notes[#notes+1]=text end,pss=true,pss_spawn=spawn})
  end
'''


def client():
    lua, g = fac.client()
    lua.execute("ffi=require('ffi');pcall(ffi.cdef,'typedef struct { const char* Name; const char* RawName; const char* Ware; uint32_t Quality; uint32_t Amount; } UIPaintMod;'); "
                "pcall(ffi.cdef,'typedef struct { float x; float y; float z; float yaw; float pitch; float roll; } UIPosRot;')")
    lua.execute(PSS_WORLD)
    return lua, g


class LuaTests(unittest.TestCase):
    def test_the_slot_faction_the_rows_and_the_receipt(self):
        lua, g = client()
        lua.execute("S=pss_session(2);r=S.frame(119);S.frame(120);STATE_TEXT='slot=1 wait=1/1 slot=2 wait=1/1';S.frame(121)")
        self.assertEqual(values(g.pss_calls), ['faction:x4lan_player2'])   # once, then only when it changes
        states = [line for line in values(g.logs) if 'PLAYER_SETTINGS_STATE' in line]
        self.assertEqual(states, ['PLAYER_SETTINGS_STATE tick=119 slot=1 wait=1/1 slot=2 wait=0/1', 'PLAYER_SETTINGS_STATE tick=121 slot=1 wait=1/1 slot=2 wait=1/1'])
        self.assertIn('PLAYER_SETTINGS_STATUS tick=120 player_settings=live', values(g.logs))
        self.assertTrue(g.r.endswith('|UIFAC_1:b0.0,f0.0,t0.0;0.0.0,0.0.0;w111.00000000;h00'))   # the native receipt carries ;P
        self.assertEqual(values(g.spawns), [])
        # Client 1 (`player`) tells its own faction too: the getters' rewrite stays off natively (no UI scope opens for slot 1).
        lua.execute("pss_calls={};S1=pss_session(1);S1.frame(10)")
        self.assertEqual(values(g.pss_calls), ['faction:player'])

    def test_the_spawn_check_on_every_client(self):
        lua, g = client()
        lua.execute("S=pss_session(1,{50,'ship_test_macro'});for t=48,53 do S.frame(t) end")
        self.assertEqual(values(g.spawns), ['ship_test_macro/729/2503/player', 'ship_test_macro/729/3003/x4lan_player2'])   # once, every slot
        self.assertEqual(values(g.bypass), [1, 0, 1, 0])
        rows = [line for line in values(g.logs) if 'PLAYER_SETTINGS_SPAWN' in line]
        self.assertEqual(rows, ['PLAYER_SETTINGS_SPAWN tick=50 slot=1 faction=player ship=9001 owner=player paint=paintmod_one theme=painttheme_one expected=paintmod_one',
                                'PLAYER_SETTINGS_SPAWN tick=50 slot=2 faction=x4lan_player2 ship=9002 owner=x4lan_player2 paint=paintmod_two theme=painttheme_two expected=paintmod_two'])
        # A spawn that fails is a row with the error, the bypass closed.
        lua.execute("spawns={};bypass={};PC.SpawnObjectAtPos2=function() error('no macro') end;S2=pss_session(1,{60,'nope'});S2.frame(60)")
        self.assertEqual(values(g.bypass), [1, 0, 1, 0])
        self.assertTrue(any('slot=2 faction=x4lan_player2 error=' in line and 'no macro' in line for line in values(g.logs)))

    def test_without_the_key_nothing_of_it(self):
        lua, g = client()
        lua.execute("S=faction_session(2);S.frame(120)")
        self.assertEqual(values(g.pss_calls), [])
        self.assertFalse(any('PLAYER_SETTINGS' in line for line in values(g.logs)))


@unittest.skipUnless(FFI_H.is_file(), 'x4-re data/x4_ffi.h not at hand')
class ScriptTests(unittest.TestCase):
    def test_paint_clothing_and_logo_as_the_menu_offers_them(self):
        lua, g = client()
        lua.globals().CDEF = fac.stock_typedefs({'UIPaintTheme', 'UIClothingTheme', 'UILogo'})
        lua.execute(r'''
          ffi.cdef(CDEF)
          calls={}
          local function rec(text) calls[#calls+1]=text end
          FC={GetNumAvailablePaintThemes=function() return 2 end,
            GetAvailablePaintThemes=function(buf,n) buf[0].ID='painttheme_01';buf[1].ID='painttheme_02';return 2 end,
            GetNumAvailableClothingThemes=function() return 1 end,
            GetAvailableClothingThemes=function(buf,n) buf[0].ID='clothingtheme_01';return 1 end,
            GetNumPlayerLogos=function(a,b) rec('logos:'..tostring(a)..'/'..tostring(b));return 3 end,
            GetPlayerLogos=function(buf,n,a,b) for i=0,2 do buf[i].file='logo_'..i;buf[i].icon='icon_'..i;buf[i].ispersonal=i==2 end;return 3 end,
            SetPlayerPaintTheme=function(id) rec('Paint:'..id) end,
            SetPlayerClothingTheme=function(id) rec('Clothing:'..id) end,
            SetPlayerLogo=function(logo) rec('Logo:'..ffi.string(logo.file)..'/'..ffi.string(logo.icon)..'/'..tostring(logo.ispersonal)) end,
            SetPlayerTaxiWaitsForPlayer=function(v) rec('Taxi:'..tostring(v)) end,
            SetPlayerTradeLoopCargoReservationSetting=function(v) rec('Reserve:'..tostring(v)) end}
          tools={gate=gate,C=FC,env=env,own_slot=2,slot_id=function(s) return s*100 end}
          plan={{0,'fac_setting','paint',2},{1,'fac_setting','clothing',1},{2,'fac_setting','logo',3},{3,'fac_setting','paint','painttheme_x'},
                {4,'fac_setting','taxi_wait',false},{5,'fac_setting','loop_reserve',true}}
          S=faction_session(2,{script=X4LAN_UI_REPLAY_CORE.script(plan,40,tools)})
          for t=40,45 do S.frame(t) end
        ''')
        self.assertEqual(values(g.calls), ['Paint:painttheme_02', 'Clothing:clothingtheme_01', 'logos:true/true', 'Logo:logo_2/icon_2/true',
                                           'Paint:painttheme_x', 'Taxi:false', 'Reserve:true'])
        rows = [line for line in values(g.logs) if 'UI_REPLAY_SCRIPT' in line]
        self.assertIn('op=fac_setting ok=true result=painttheme_02', rows[0])
        self.assertIn('op=fac_setting ok=true result=logo_2', rows[2])
        lua.execute("f=X4LAN_UI_FACTION.script_call({1,'fac_setting','paint',3},tools);ok,why=pcall(f)")
        self.assertFalse(g.ok)
        self.assertIn('no paint theme 3', g.why)


PROBE_PSS = fac.PROBE_FACTION + r'''
  gate.player_settings_pins=function() ui_calls[#ui_calls+1]='pss_pins';return PPINS or 1,PPINS and 'pins_player_settings:6d83b0' or nil end
  gate.player_settings=function() ui_calls[#ui_calls+1]='pss';if PARM then return 0,PARM end;return 1,7 end
  gate.player_settings_faction=function(id) ui_calls[#ui_calls+1]='pss_faction:'..id;return 1 end
  gate.player_settings_state=function() return 'slot=1 wait=1/1 slot=2 wait=1/1' end
  gate.player_settings_status=function() return 'player_settings=live' end
  gate.player_settings_themes=function() return '','' end
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_native_gate_pilot_stock as stock
        cls.stock = stock

    def client(self, slot=1, prelude='', **extra):
        cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['travel', 'faction'], native_gate_ui_guard='all',
                   native_gate_player_settings_per_slot=True)
        cfg.update(extra)
        return self.stock.StockClient(slot, slots_all=True, prelude=PROBE_PSS + prelude, **cfg)

    def test_it_arms_after_the_faction_family_with_its_pins_first(self):
        clients = [self.client(k) for k in (1, 2)]
        self.stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('ui_calls')
            self.assertLess(calls.index('pss_pins'), calls.index(f'arm:{fac.BIT+1}'))
            self.assertLess(calls.index('faction_pins'), calls.index('pss_pins'))
            self.assertLess(calls.index('faction'), calls.index('pss'))
            logs = c.values('logs')
            self.assertTrue(any('PLAYER_SETTINGS_PINS ok=1' in line for line in logs))
            self.assertTrue(any(f'PLAYER_SETTINGS_ARMED slot={c.slot} hooks=7 spawn=-' in line for line in logs), logs[-5:])
            self.assertTrue(any('PLAYER_SETTINGS_STATE' in line for line in logs), logs[-5:])

    def test_refusals(self):
        for extra, prelude, why in ((dict(), 'PPINS=0', 'player settings refused: pins_player_settings:6d83b0'),
                                    (dict(), "PARM='needs_ui_faction'", 'player settings refused: needs_ui_faction'),
                                    (dict(native_gate_player_settings_per_slot='yes'), '', 'native_gate_player_settings_per_slot is true or nil'),
                                    (dict(native_gate_player_settings_spawn=[0, 'm']), '', 'native_gate_player_settings_spawn is {tick, macro}'),
                                    (dict(native_gate_player_settings_per_slot=None, native_gate_player_settings_spawn=[50, 'm']), '',
                                     'native_gate_player_settings_spawn is {tick, macro}')):
            with self.subTest(why=why):
                c = self.client(prelude=prelude, **extra)
                self.assertTrue(any(why in line for line in c.errors()), c.values('logs')[-3:])

    def test_without_the_key_nothing_is_asked(self):
        clients = [self.client(k, native_gate_player_settings_per_slot=None) for k in (1, 2)]
        self.stock.run(clients, 12)
        c = clients[0]
        self.assertEqual(c.errors(), [])
        self.assertFalse({'pss_pins', 'pss'} & set(c.values('ui_calls')))
        self.assertFalse(any(call.startswith('pss_faction') for call in c.values('ui_calls')))


class OptionTests(unittest.TestCase):
    def failures(self, **override):
        text = uirep.stock_config(**override)
        return [f['rule'] for f in uirep.options.player_settings_rules(text, set())]

    def test_rules(self):
        self.assertEqual(self.failures(), [])
        on = dict(native_gate_ui_replay="{'travel','faction'}", native_gate_ui_guard="'all'", native_gate_player_settings_per_slot='true')
        self.assertEqual(self.failures(**on), [])
        self.assertEqual(self.failures(**on, native_gate_player_settings_spawn="{300,'ship_arg_s_fighter_01_a_macro'}"), [])
        for rule, override in (('native_gate_player_settings_per_slot needs native_gate_ui_replay faction',
                                dict(native_gate_ui_replay="{'travel'}", native_gate_player_settings_per_slot='true')),
                               ('native_gate_player_settings_per_slot needs native_gate_ui_replay faction',
                                dict(native_gate_ui_replay='true', native_gate_player_settings_per_slot='true')),
                               ('native_gate_player_settings_per_slot must be true', dict(on, native_gate_player_settings_per_slot="'on'")),
                               ('native_gate_player_settings_spawn needs native_gate_player_settings_per_slot',
                                dict(native_gate_player_settings_spawn="{300,'m'}")),
                               ("native_gate_player_settings_spawn must be {tick, 'macro'}", dict(on, native_gate_player_settings_spawn="{0,'m'}")),
                               ("native_gate_player_settings_spawn must be {tick, 'macro'}", dict(on, native_gate_player_settings_spawn="{'m',300}"))):
            self.assertIn(rule, self.failures(**override), override)
        # Reached from the checker's entry point.
        rules = [f['rule'] for f in uirep.options.probe_rules(uirep.stock_config(native_gate_player_settings_per_slot='true'), set())]
        self.assertIn('native_gate_player_settings_per_slot needs native_gate_ui_replay faction', rules)


if __name__ == '__main__':
    unittest.main()
