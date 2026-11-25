"""The HUD weapon panel of every player as X4 shows player 1's (native_gate_hud_weapons; src/native/engine/
lockstep_hud_weapons.h).

Static checks: the session default and what it needs, the probe's arming and status rows, the gate's methods, tick check
and restore, the eight call sites of the two active tests in the five UI readers, the registry's entries, the scoped look
op of the UI replay script. With the read-only stock colours (X4LAN_GAMEDATA/base/libraries/colors.xml) and, when the
x4-re copy of the stock UI is there, crosshair handling.lua: the panel's active colours are the blue ones, the inactive ones
grey, and the panel picks them by GetWeaponDetails3's active flag. The C half's behaviour (slots 2, 3 and 8 presented, the
sites by CAS, the trampolines, the pinned image) is the gate's own test (build_native_gate.py --test --native-image).
Nothing here runs X4.
"""
import os
from pathlib import Path
import re
import sys
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
ENGINE = ROOT / 'src/native/engine'
sys.path.insert(0, str(DEV))
import native_gate_defaults as defaults  # noqa: E402

GAMEDATA = os.environ.get('X4LAN_GAMEDATA')
COLORS = Path(GAMEDATA) / 'base/libraries/colors.xml' if GAMEDATA else None
SKIP_COLORS = 'needs the stock libraries/colors.xml read only: X4LAN_GAMEDATA/base/libraries'
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
PANEL = RE / 'gamelua/08/ui/core/lua/crosshair handling.lua'
SKIP_PANEL = ('needs the stock ui/core/lua/crosshair handling.lua read only: the x4-re copy (X4_RE; X4LAN_GAMEDATA holds no '
              'ui files)')
SITES = {0xb0607f: 'HUDW_PRIMARY', 0xb06c9f: 'HUDW_SECONDARY', 0xb07fd9: 'HUDW_SECONDARY', 0xb07fe0: 'HUDW_PRIMARY',
         0xb08479: 'HUDW_SECONDARY', 0xb08480: 'HUDW_PRIMARY', 0xb08949: 'HUDW_SECONDARY', 0xb08950: 'HUDW_PRIMARY'}


class WiringTests(unittest.TestCase):
    def test_a_session_default_that_needs_the_lease(self):
        entry = [e for e in defaults.TABLE['on'] if e[0] == 'native_gate_hud_weapons']
        self.assertEqual(len(entry), 1)
        self.assertEqual(entry[0][1], 'true')
        self.assertEqual(entry[0][2], ('native_gate_pilot_fire',))
        order = [e[0] for e in defaults.TABLE['on']]
        self.assertLess(order.index('native_gate_pilot_fire'), order.index('native_gate_hud_weapons'))
        options = (DEV / 'check_gate_options.py').read_text(encoding='utf-8')
        self.assertIn("'native_gate_hud_weapons': (('true',), (), None),", options)

    def test_the_probe_arms_it_and_logs_its_status(self):
        probe = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
        self.assertIn("gate.hud_weapons()==1", probe)
        self.assertIn("log('HUD_WEAPONS_ARMED')", probe)
        self.assertIn("ticklog.status('HUD_WEAPONS_STATUS',tick,gate.hud_weapons_status)", probe)
        self.assertEqual(probe.count("log('HUD_WEAPONS_STOP '"), 2)
        self.assertLess(probe.index("log('TURRET_ASSIST_ARMED')"), probe.index("log('HUD_WEAPONS_ARMED')"))

    def test_the_gate_installs_checks_and_restores_the_sites(self):
        gate = (ENGINE / 'lockstep_gate.c').read_text(encoding='utf-8')
        for method in ('hud_weapons', 'hud_weapons_status'):
            self.assertIn('METHOD(%s);' % method, gate)
        self.assertIn('#include "lockstep_hud_weapons.h"', gate)
        self.assertIn('if(hudw_installed && !hudw_check()) {finish(HUDW_STOP_REASON);', gate)
        self.assertIn('    hudw_uninstall();\n', gate)
        self.assertIn('if(hudw_installed) stats.restored=0;', gate)
        module = (ENGINE / 'lockstep_hud_weapons.h').read_text(encoding='utf-8')
        self.assertIn('#define HUDW_STOP_REASON 248u', module)
        self.assertIn('#define HUDW_PRIMARY 0x81d530u', module)
        self.assertIn('#define HUDW_SECONDARY 0x81d590u', module)
        sites = {int(rva, 16): target for rva, target in re.findall(r'\{(0x[0-9a-f]+)u,(HUDW_PRIMARY|HUDW_SECONDARY),"Get\w+"\}', module)}
        self.assertEqual(sites, SITES)
        self.assertIn('#define HUDW_SITES 8u', module)
        tests = (ENGINE / 'test_lockstep_gate.c').read_text(encoding='utf-8')
        self.assertIn('#include "test_lockstep_hud_weapons.h"', tests)
        self.assertIn('test_hud_weapons_image(argv[1]);', tests)
        self.assertIn('    test_hud_weapons();\n', tests)

    def test_the_registry_names_the_sites_and_the_stop(self):
        registry = (ROOT / 'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
        self.assertRegex(registry, r'248 HUDW_STOP_REASON +lockstep_hud_weapons\.h')
        for rva in SITES:
            self.assertIn('0x%x' % rva, registry)
        free = re.search(r'\n  free: (1\d\d[^\n]+)', registry).group(1)   # the module stop reasons' free list
        self.assertTrue(free.endswith(' 247, 249-255.'), free)

    def test_the_look_op_runs_in_the_presentation_scope(self):
        core = (DEV / 'native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
        self.assertIn("op=='hud_weapons'", core)
        scoped = re.search(r'K\.SCOPED=\{([^}]*)\}', core).group(1)
        self.assertIn('hud_weapons=true', scoped)   # read where the HUD reads: X4's player as this client's slot
        self.assertIn('WeaponDetails4 GetWeaponDetails3(size_t weaponnum, bool issecondary);', core)


class StockPanelTests(unittest.TestCase):
    @unittest.skipUnless(COLORS and COLORS.is_file(), SKIP_COLORS)
    def test_active_is_blue_and_inactive_grey(self):
        mapping = {m.get('id'): m.get('ref') for m in ET.parse(str(COLORS)).getroot().iter('mapping')}
        for active, inactive in (('crosshair_weapon', 'crosshair_weapon_inactive'),
                                 ('crosshair_clipreload_ready', 'crosshair_clipreload_ready_inactive'),
                                 ('crosshair_heat_normal', 'crosshair_heat_normal_inactive')):
            self.assertTrue(mapping[active].startswith('azure'), (active, mapping[active]))
        self.assertEqual(mapping['crosshair_weapon_inactive'], 'grey_64')
        self.assertEqual(mapping['crosshair_clipreload_ready_inactive'], 'grey_64')
        self.assertEqual(mapping['crosshair_weapon_disabled'], 'grey_128')   # the stance block: not this report's grey

    @unittest.skipUnless(PANEL.is_file(), SKIP_PANEL)
    def test_the_panel_colours_by_the_active_flag(self):
        lua = PANEL.read_text(encoding='utf-8', errors='replace')
        update = lua[lua.index('function updateWeaponPanel()'):]
        update = update[:update.index('\nend\n')]
        self.assertIn('C.GetWeaponDetails3(i, false)', update)
        self.assertIn('C.GetWeaponDetails3(i, true)', update)
        self.assertIn('setWeaponColor(weaponEntry.icon, weaponDetails.active,', update)
        self.assertIn('setBarState(weaponEntry.bar, weaponDetails.mode, weaponDetails.active,', update)
        self.assertIn('setReloadState(weaponEntry.reload, weaponEntry.reloadbar, weaponDetails,', update)
        colour = lua[lua.index('function setWeaponColor('):]
        colour = colour[:colour.index('\nend\n')]
        self.assertIn('if isActive then', colour)


if __name__ == '__main__':
    unittest.main()
