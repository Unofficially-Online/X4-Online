"""tools/build_soak_overlay.py guard_config: which UI writes the soak overlay blocks and which it relays."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_soak_overlay  # noqa: E402

CENSUS = {
    'AddPlayerMoney': {'family': 'economy', 'scripts': ['menu_x.lua'], 'kinds': 'q'},
    'SetFormationShape': {'family': 'orders', 'scripts': ['menu_map.lua'], 'kinds': 'Us'},
    'SetLoadout': {'family': 'ships', 'scripts': ['menu_ship.lua'], 'kinds': None},
    'CreateOrder': {'family': 'orders', 'scripts': ['menu_map.lua'], 'kinds': 'Us'},
    'SetMenuOption': {'family': 'presentation', 'scripts': ['menu_options.lua'], 'kinds': 'i'},
    'UnusedWrite': {'family': 'economy', 'scripts': [], 'kinds': 'q'},
}
LUA_GLOBALS = ['TransferPlayerMoneyTo', 'AddTradeToShipQueue', 'GetPlayerMoney', 'NotifyOnSomething', 'SetMinBudget']


class GuardConfig(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        d = Path(self.dir.name)
        self.census = d / 'ui-export-census.json'
        self.census.write_text(json.dumps(CENSUS))
        (d / 'ui-lua-globals.json').write_text(json.dumps(LUA_GLOBALS))

    def tearDown(self):
        self.dir.cleanup()

    def config(self, relay):
        text = build_soak_overlay.guard_config(self.census, relay)
        return {line.split(' = ', 1)[0].removeprefix('X4LAN_CONFIG.'): line.split(' = ', 1)[1]
                for line in text.splitlines()}

    def test_block_lists_game_writes_but_not_presentation_relayed_or_unused(self):
        c = self.config(False)
        self.assertEqual(c['native_gate_ui_exports'], "'block'")
        self.assertEqual(c['native_gate_ui_exports_block'], "{'AddPlayerMoney','SetFormationShape','SetLoadout'}")
        self.assertNotIn('native_gate_ui_relay', c)
        self.assertNotIn('native_gate_ui_lua_relay', c)
        self.assertEqual(c['native_gate_ui_lua_names'], "{'AddTradeToShipQueue','SetMinBudget','TransferPlayerMoneyTo'}")

    def test_relay_carries_exports_with_kinds_and_lua_globals_with_known_kinds(self):
        c = self.config(True)
        self.assertEqual(c['native_gate_ui_exports_block'], "{'SetLoadout'}")
        self.assertEqual(c['native_gate_ui_relay'], "{AddPlayerMoney='q',SetFormationShape='Us'}")
        self.assertEqual(c['native_gate_ui_lua_names'], "{'AddTradeToShipQueue'}")
        self.assertEqual(c['native_gate_ui_lua_relay'], "{SetMinBudget='c',TransferPlayerMoneyTo='-c'}")


if __name__ == '__main__':
    unittest.main()
