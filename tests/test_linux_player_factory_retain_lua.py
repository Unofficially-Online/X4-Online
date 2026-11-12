"""Exercise the actual retained-factory Lua guard and request expression."""
from pathlib import Path
import unittest

try:
    from lupa.luajit21 import LuaError, LuaRuntime
except ImportError:
    LuaRuntime = None
    LuaError = Exception


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "tools/dev/native_gate_probe.lua").read_text()
GUARD = SOURCE.split("if cfg.native_gate_player_factory_retain_until ~= nil then", 1)[1]
GUARD = "if cfg.native_gate_player_factory_retain_until ~= nil then" + GUARD.split(
    "\nif cfg.native_gate_player_factory_probe then", 1
)[0]
REQUEST = next(
    line.strip() for line in SOURCE.splitlines()
    if "assert(gate.factory_probe_request(" in line
)


@unittest.skipIf(LuaRuntime is None, "LuaJIT Lupa unavailable")
class FactoryRetentionLuaTests(unittest.TestCase):
    def runtime(self, **values):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().cfg = lua.table_from(values)
        lua.execute("recorded = nil; gate = {factory_probe_request = function(t) recorded=t; return 1 end}")
        return lua

    def test_request_defaults_to_zero_and_forwards_explicit_tick(self):
        for retain, expected in ((None, 0), (4, 4), (36000, 36000)):
            with self.subTest(retain=retain):
                lua = self.runtime(native_gate_player_factory_probe=True,
                                   native_gate_player_factory_retain_until=retain)
                lua.execute(GUARD)
                lua.execute(REQUEST)
                self.assertEqual(lua.globals().recorded, expected)

    def test_guard_rejects_invalid_or_unrequested_retention(self):
        cases = (
            {"native_gate_player_factory_retain_until": 4},
            {"native_gate_player_factory_probe": True, "native_gate_player_factory_retain_until": 3},
            {"native_gate_player_factory_probe": True, "native_gate_player_factory_retain_until": 4.5},
            {"native_gate_player_factory_probe": True, "native_gate_player_factory_retain_until": 36001},
            {"native_gate_player_factory_probe": True, "native_gate_player_factory_retain_until": "4"},
        )
        for config in cases:
            with self.subTest(config=config):
                lua = self.runtime(**config)
                with self.assertRaises(LuaError):
                    lua.execute(GUARD)
                self.assertIsNone(lua.globals().recorded)

    def test_guard_accepts_valid_release_tick(self):
        lua = self.runtime(native_gate_player_factory_probe=True,
                           native_gate_player_factory_retain_until=4)
        lua.execute(GUARD)
        lua.execute(REQUEST)
        self.assertEqual(lua.globals().recorded, 4)


if __name__ == "__main__":
    unittest.main()
