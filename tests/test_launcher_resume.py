"""x4ls resume (src/lockstep/launcher/resume.py): the one-player copy of a session's save, the resumed spec and the play
overlay's arguments. Synthetic: saves written here in X4's line shape; the prepare step itself needs X4."""
import gzip
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import resume  # noqa: E402

SAVE = '''<?xml version="1.0" encoding="UTF-8"?>
<savegame>
<universe>
<component class="ship_s" macro="ship_ter_s_fighter_01_a_macro" id="[0x3ce11]">
<connections>
<connection connection="cockpit">
<component class="cockpit" macro="cockpit_gen_virtual_01_macro" id="[0x3ce18]">
<connections>
<connection connection="room">
<component class="player" macro="character_player_discover_macro" connection="room" code="JOR-849" owner="player" id="[0x160311]">
<listeners>
<listener listener="[0x3ce11]" event="killed"/>
</listeners>
<connections>
<connection connection="inventory">
<component class="inventory" macro="inventory_macro" id="[0x160312]"/>
<component class="scene" macro="scene_macro" id="[0x160313]">
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
<component class="ship_s" macro="ship_arg_s_scout_01_a_macro" id="[0x69534]">
<component class="player" macro="character_player_discover_macro" connection="room" name="{1021,402}" code="EEV-621" owner="player" id="[0x69539]">
<component class="inventory" macro="inventory_macro" id="[0x6953a]"/>
</component>
</component>
</universe>
<md>
<value name="$ApproachObject" type="component" value="[0x160311]"/>
</md>
</savegame>
'''


def write(path, text):
    with gzip.open(path, 'wb') as f:
        f.write(text.encode())
    return path


def read(path):
    with gzip.open(path, 'rb') as f:
        return f.read().decode()


class SinglePlayerTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.save = write(Path(self.dir.name) / 'c1-t72000.xml.gz', SAVE)
        self.out = Path(self.dir.name) / 'one.xml.gz'

    def test_the_other_slots_player_goes_with_everything_inside_it(self):
        self.assertEqual([(saved, named) for saved, named, _ in resume.players(self.save)],
                         [('0000000000160311', False), ('0000000000069539', True)])
        kept, removed = resume.single_player(self.save, self.out)
        self.assertEqual((kept, removed), ('0000000000069539', ['0000000000160311']))
        text = read(self.out)
        for gone in ('[0x160311]"', '[0x160312]', '[0x160313]', 'JOR-849'):
            self.assertNotIn(gone, text.split('<md>')[0])
        for stays in ('id="[0x3ce18]"', 'id="[0x69539]"', 'id="[0x6953a]"', '$ApproachObject'):   # its room, X4's own player, the rest
            self.assertIn(stays, text)
        self.assertEqual(SAVE.count('\n') - text.count('\n'), 12)   # the twelve lines of that one component
        self.assertEqual([saved for saved, _, _ in resume.players(self.out)], ['0000000000069539'])

    def test_the_kept_player_can_be_named_and_must_exist(self):
        kept, removed = resume.single_player(self.save, self.out, keep='160311')
        self.assertEqual((kept, removed), ('0000000000160311', ['0000000000069539']))
        self.assertIn('JOR-849', read(self.out))
        with self.assertRaisesRegex(SystemExit, 'no player object'):
            resume.single_player(self.save, self.out, keep='abc')

    def test_a_save_with_several_named_players_asks(self):
        save = write(Path(self.dir.name) / 'two.xml.gz', SAVE.replace('code="JOR-849"', 'name="x" code="JOR-849"'))
        with self.assertRaisesRegex(SystemExit, 'say which is X4.s own with --keep-player'):
            resume.single_player(save, self.out)
        none = write(Path(self.dir.name) / 'none.xml.gz', '<savegame>\n</savegame>\n')
        with self.assertRaisesRegex(SystemExit, 'no player object'):
            resume.single_player(none, self.out)

    def test_a_save_with_one_player_is_copied_whole(self):
        one = write(Path(self.dir.name) / 'single.xml.gz', SAVE.replace(' name="{1021,402}"', ''))
        resume.single_player(one, self.out, keep='69539')
        again = Path(self.dir.name) / 'again.xml.gz'
        kept, removed = resume.single_player(self.out, again)   # one unnamed player: it is X4's own
        self.assertEqual((kept, removed), ('0000000000069539', []))
        self.assertEqual(read(again), read(self.out))


class RecipeTests(unittest.TestCase):
    RECIPE = {'players': 2, 'mode': 'coop', 'minutes': 180.0, 'lua_block': 'Pause,Unpause,SaveGame', 'full_logs': False,
              'mixed_platform': False, 'open': [], 'join_mode': 'report', 'save_every': 10.0,
              'set': ['native_gate_digest_parts={1,400000}']}

    def test_the_resumed_spec_keeps_the_starts_and_leaves_the_stories(self):
        spec = {'base_start': 'a', 'players': 2, 'mode': 'coop',
                'slots': {'2': {'gamestart': 'b', 'hull': {'macro': 'm'}, 'kit': {'money': 1}, 'story': {'script': 'GS_B', 'cue': 'Start'}}}}
        out = resume.resume_spec(spec)
        self.assertIsNone(out['slots']['2']['story'])
        self.assertEqual((out['slots']['2']['hull'], out['slots']['2']['kit'], out['base_start']), ({'macro': 'm'}, {'money': 1}, 'a'))
        self.assertEqual(spec['slots']['2']['story']['script'], 'GS_B')   # the first session's spec is not touched
        self.assertEqual(resume.resume_spec(spec, story=True), spec)

    def test_the_overlay_is_built_from_the_first_sessions_recipe(self):
        cmd = resume.overlay_command(self.RECIPE, 's.xml.gz', 'session.json', 'gate.dll', 'out')
        self.assertEqual(cmd[cmd.index('--minutes') + 1], '180.0')
        self.assertEqual(cmd[cmd.index('--save-every') + 1], '10.0')
        self.assertEqual(cmd[cmd.index('--session') + 1], 'session.json')
        self.assertEqual([cmd[i + 1] for i, a in enumerate(cmd) if a == '--set'], ['native_gate_digest_parts={1,400000}'])
        self.assertNotIn('--open', cmd)
        cmd = resume.overlay_command(self.RECIPE, 's.xml.gz', 'session.json', 'gate.dll', 'out', minutes=30, sets=['native_gate_x=1'],
                                     save_every=2)
        self.assertEqual((cmd[cmd.index('--minutes') + 1], cmd[cmd.index('--save-every') + 1]), ('30', '2'))
        self.assertEqual([cmd[i + 1] for i, a in enumerate(cmd) if a == '--set'], ['native_gate_digest_parts={1,400000}', 'native_gate_x=1'])
        old = dict(self.RECIPE)
        del old['save_every']   # an overlay from before periodic saves
        cmd = resume.overlay_command(old, 's.xml.gz', 'session.json', 'gate.dll', 'out')
        self.assertEqual(cmd[cmd.index('--save-every') + 1], '0')


if __name__ == '__main__':
    unittest.main()
