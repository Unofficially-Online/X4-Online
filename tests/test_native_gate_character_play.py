"""Body animation starts on the character manager (lockstep_character_play.h, native_gate_character_play, stop reason 188;
off in the bare gate, 'sync' in a lockstep session): probe arming, CHARACTER_PLAY_ROW drains written at once, status and
stop rows, refusals, the option checker, the registry and the session default. The native half (the class race on a mock
of slot 17, the synchronous loads, the keyed word, fail-closed paths, the entry swap and the image facts) is tested in C:
src/native/engine/test_lockstep_character_play.h."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_probe as probe  # noqa: E402
import check_gate_options as options  # noqa: E402
import native_gate_defaults as defaults  # noqa: E402

HEADER = (ROOT/'src/native/engine/lockstep_character_play.h').read_text(encoding='utf-8')
GATE = (ROOT/'src/native/engine/lockstep_gate.c').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
PROBE = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')

MOCKS = '''
  local gate=package.loadlib('gate')();order={}
  gate.character_play=function(mode) order[#order+1]='play:'..tostring(mode);return play_native or 1 end
  gate.character_play_status=function() return 'mode=sync installed=1 calls=1 stage=armed failures=0' end
  play_rows={[0]='index=0 tick=1 mode=sync ordinal=0 pair=0 character=0000000500000123 motion=0000000600000456 before=ar loaded=l- '..
    'load_us=900 result=1 caller=2e818130e22bee84 seed=1111111111111111 exit=2222222222222222 draws=1'}
  gate.character_play_row=function(i) local row=play_rows[i];play_rows[i]=nil;return row end
'''


class CharacterPlayProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def run_session(self, mode):
        self.lua.execute("X4LAN_CONFIG.native_gate_character_play='%s'" % mode + MOCKS)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();state.issued=2;update()')
        return list(self.lua.globals().logs.values())

    def test_sync_arms_and_writes_rows_status_and_stop(self):
        lines = self.run_session('sync')
        self.assertEqual(list(self.lua.globals().order.values()), ['play:sync', 'play:sync'])   # again after the UI reload
        self.assertTrue(any(line.endswith('CHARACTER_PLAY_ARMED mode=sync') for line in lines))
        self.assertEqual(sum('CHARACTER_PLAY_ROW index=0 tick=1 mode=sync ordinal=0 pair=0 ' in line for line in lines), 1)
        self.assertTrue(any('CHARACTER_PLAY tick=1 mode=sync installed=1' in line for line in lines))
        self.lua.execute("state.active=0;state.completed=2;setmetatable(state,{__index=function() return 0 end})");self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('CHARACTER_PLAY_STOP mode=sync installed=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_observe_arms_and_rows_are_written_at_once(self):
        lines = self.run_session('observe')
        self.assertEqual(list(self.lua.globals().order.values()), ['play:observe', 'play:observe'])
        self.assertTrue(any(line.endswith('CHARACTER_PLAY_ARMED mode=observe') for line in lines))
        self.assertEqual(sum('CHARACTER_PLAY_ROW index=0 ' in line for line in lines), 1)
        # Rare rows that name the class race: written at once, not held with a status stride's other rows.
        self.assertIn("ticklog.write('CHARACTER_PLAY_ROW '..row)", PROBE)

    def test_refusals(self):
        for config in ("X4LAN_CONFIG.native_gate_character_play='key'", "X4LAN_CONFIG.native_gate_character_play=true",
                       "X4LAN_CONFIG.native_gate_character_play='sync';play_native=0"):
            with self.subTest(config=config):
                self.setUp()
                self.lua.execute(config+MOCKS)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 0)
                self.assertTrue(any('character play refused' in line for line in self.lua.globals().logs.values()), config)


class CharacterPlayOptionTests(unittest.TestCase):
    BASE = 'X4LAN_CONFIG.native_gate_snapshot_sha = "%s"\n' % ('a'*64)

    def failures(self, text):
        return [f['rule'] for f in options.check(text)[1] if 'character_play' in f['rule']]

    def test_modes(self):
        for value in ("'observe'", '"sync"', 'false'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_character_play = %s\n' % value), [])
        self.assertEqual(len(self.failures(self.BASE+"X4LAN_CONFIG.native_gate_character_play = 'key'\n")), 1)
        self.assertEqual(len(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_character_play = true\n')), 1)

    def test_session_default_is_sync(self):
        on = {k: v for k, v, _, _ in defaults.TABLE['on']}
        self.assertEqual(on['native_gate_character_play'], "'sync'")


class MirrorTests(unittest.TestCase):
    def test_stop_reason_registered(self):
        self.assertIn('#define CPLAY_STOP_REASON 188u', HEADER)
        self.assertRegex(REGISTRY, r'\n  188 CPLAY_STOP_REASON +lockstep_character_play\.h')

    def test_gate_checks_owns_and_restores_the_entry(self):
        self.assertIn('if(cplay_installed && !cplay_check()) {finish(CPLAY_STOP_REASON);', GATE)
        self.assertIn('!cplay_owned() ||', GATE)
        self.assertIn('    cplay_restore();', GATE)
        self.assertIn('METHOD(character_play);METHOD(character_play_status);METHOD(character_play_row);', GATE)


if __name__ == '__main__':
    unittest.main()
