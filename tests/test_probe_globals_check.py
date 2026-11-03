import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import probe_globals_check  # noqa: E402


class ProbeGlobalsCheck(unittest.TestCase):
    def run_check(self, source, known=()):
        with tempfile.TemporaryDirectory() as tmp:
            lua = Path(tmp) / 'probe.lua'
            lua.write_text(source)
            globals_file = Path(tmp) / 'globals.json'
            globals_file.write_text(json.dumps(list(known)))
            return probe_globals_check.main([str(lua), '--globals', str(globals_file)])

    def test_a_read_before_the_local_declaration_is_flagged(self):
        # f compiles 'later' as a global read: nil at run time (the night of 3 November: ticklog, K_ZERO16).
        source = 'local function f() return later.x end\nlocal later={x=1}\nreturn f\n'
        self.assertEqual(self.run_check(source), 1)

    def test_set_globals_known_globals_and_locals_pass(self):
        source = 'COUNT=1\nlocal t={}\nlocal function f() return COUNT+t[1]+GetPlayerMoney()+string.len("a") end\nreturn f\n'
        self.assertEqual(self.run_check(source, known=['GetPlayerMoney']), 0)
        self.assertEqual(self.run_check(source), 1)  # GetPlayerMoney unknown without the census

    def test_debug_is_allowed_only_as_an_optional_read(self):
        self.assertEqual(self.run_check('local ok=type(debug)=="table"\nreturn ok\n'), 0)


if __name__ == '__main__':
    unittest.main()
