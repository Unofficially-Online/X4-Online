"""Per-player copies of stock plot scripts (tools/dev/native_gate_plot_copies.py), the story MD that begins them, the
play overlay's config and the launcher's compose step. Synthetic: a plot script and a DLC patch of our own text in a made-up
game data folder; no X4 and no game file."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
sys.path.insert(0, str(DEV))
sys.path.insert(0, str(ROOT / 'src'))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


copies = load('native_gate_plot_copies_t', DEV / 'native_gate_plot_copies.py')
session_start = load('native_gate_session_start_plots_t', DEV / 'native_gate_session_start.py')

PLOT = '''<?xml version="1.0" encoding="utf-8"?>
<mdscript name="X4Ep1_Mentor_Subscriptions" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="md.xsd">
  <cues>
    <cue name="Patch_Userdata" instantiate="true">
      <conditions>
        <event_cue_signalled cue="md.Setup.Start"/>
      </conditions>
      <actions><debug_text text="'patch'"/></actions>
    </cue>
    <cue name="Start" namespace="this" version="25">
      <conditions>
        <event_cue_signalled cue="md.Setup.Start" />
        <check_value value="player.galaxy.macro == macro.xu_ep2_universe_macro"/>
      </conditions>
      <actions>
        <set_value name="$HQPosition" exact="position.[128km, 0m, 198km]"/>
        <set_value name="$story_hq_boso" exact="gamestart.storystate.story_hq_boso"/>
        <set_value name="$Table" exact="table[
                   {1} = 'a',
                   {2} = 'b']"/>
      </actions>
      <cues>
        <cue name="Claim">
          <conditions><event_cue_signalled/></conditions>
          <force name="Create_HQ"/>
          <actions>
            <create_station name="$HQ" owner="faction.player"><safepos value="$HQPosition"/></create_station>
            <set_value name="md.X4Ep1_Mentor_Subscriptions.Start.$Met" exact="true"/>
            <signal_cue cue="md.X4Ep1_Mentor_Subscriptions_Other.Go"/>
            <do_if value="not player.headquarters and $x.owner == faction.player"/>
            <do_if value="faction.playerish and $player.headquartersmore and md.Other.faction.player"/>
            <do_else comment="patchmarker"/>
          </actions>
        </cue>
        <cue name="Debug_Create_HQ_With_Boso"><conditions><event_cue_signalled/></conditions></cue>
        <cue name="Debug_Unlock_Research"><conditions><event_cue_signalled/></conditions></cue>
        <cue name="Phase_SignalLeak"><conditions><event_cue_signalled/></conditions></cue>
      </cues>
    </cue>
  </cues>
</mdscript>
'''
PATCH = '''<?xml version="1.0" encoding="utf-8"?>
<diff>
  <add sel="/mdscript/cues/cue[@name='Start']/cues/cue[@name='Claim']/actions/do_else[@comment='patchmarker']">
    <debug_text text="'from the dlc'"/>
  </add>
</diff>
'''


def game_data(folder, patch=True, plot=PLOT):
    root = Path(folder) / 'gamedata'
    (root / 'base/md').mkdir(parents=True)
    (root / 'base/md/x4ep1_mentor_subscription.xml').write_text(plot, encoding='utf-8')
    if patch:
        (root / 'ego_dlc_pirate/md').mkdir(parents=True)
        (root / 'ego_dlc_pirate/md/x4ep1_mentor_subscription.xml').write_text(PATCH, encoding='utf-8')
    return root


class CopyTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = game_data(self.dir.name)

    def test_a_copy_is_the_patched_script_under_its_own_name_waiting_for_its_own_signal(self):
        e = copies.entry('hq', 2, self.root)
        self.assertEqual((e['copy'], e['target'], [x[0] for x in e['sources']]),
                         ('X4Ep1_Mentor_Subscriptions_P2', 'md/x4lan_plot_hq_p2.xml', ['base', 'ego_dlc_pirate']))
        text = copies.generate([e], self.root)[e['target']]
        self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), e['sha256'])
        made = ET.fromstring(text.encode())
        self.assertEqual(made.get('name'), 'X4Ep1_Mentor_Subscriptions_P2')
        start = [c for c in made.find('cues') if c.get('name') == 'Start'][0]
        self.assertEqual([(c.tag, c.attrib) for c in start.find('conditions')], [('event_cue_signalled', {})])
        self.assertEqual(start.get('version'), '25')
        # its references to itself follow the name; another script whose name only begins the same is not touched
        self.assertIn('md.X4Ep1_Mentor_Subscriptions_P2.Start.$Met', text)
        self.assertIn('md.X4Ep1_Mentor_Subscriptions_Other.Go', text)
        self.assertNotIn('md.X4Ep1_Mentor_Subscriptions.Start', text)
        # the stock extension's patch is in it, the other root cue and the station step as they were
        self.assertIn('from the dlc', text)
        other = [c for c in made.find('cues') if c.get('name') == 'Patch_Userdata'][0]
        self.assertEqual(other.find('conditions/event_cue_signalled').get('cue'), 'md.Setup.Start')
        # ...but it waits until the copy has begun (the stock root cue has run from the game's first frame, a copy's has not)
        self.assertEqual(other.find('conditions/check_value').get('value'), 'Start.state != cuestate.waiting')
        self.assertEqual(len(start.find('conditions')), 1)
        # a debug force name is one per game: the copy has none
        self.assertIn('<force name="Create_HQ"', PLOT)
        self.assertIsNone(made.find('.//force'))
        self.assertEqual(made.find('.//create_station').get('owner'), 'faction.player')
        self.assertIsNotNone(made.find('.//create_station/safepos'))
        self.assertIn('never in git, an overlay or a kit', text)
        self.assertNotEqual(copies.entry('hq', 3, self.root)['sha256'], e['sha256'])

    def test_a_copy_for_a_slot_with_its_own_faction_names_that_faction(self):
        """Factions mode: 'faction.player' is a fixed faction in a script, so the copy names the slot's (the station its plot
        makes is then that faction's), and 'player.headquarters' is that faction's headquarters."""
        plain = copies.entry('hq', 2, self.root)
        own = copies.entry('hq', 2, self.root, 'x4lan_player2')
        self.assertEqual((plain['faction'], own['faction']), (None, 'x4lan_player2'))
        self.assertNotEqual(plain['sha256'], own['sha256'])
        made = ET.fromstring(copies.generate([own], self.root)[own['target']].encode())
        self.assertEqual(made.find('.//create_station').get('owner'), 'faction.x4lan_player2')
        values = [v for e in made.iter() for v in e.attrib.values()]
        self.assertIn('not faction.x4lan_player2.headquarters and $x.owner == faction.x4lan_player2', values)
        self.assertIn('faction.playerish and $player.headquartersmore and md.Other.faction.player', values)   # other names: untouched
        self.assertFalse(any(v == 'faction.player' for v in values))
        for bad in ('player', 'argon', 'x4lan_player'):
            with self.assertRaisesRegex(copies.PlotError, 'copy for faction'):
                copies.entry('hq', 2, self.root, bad)
        with self.assertRaises(copies.PlotError):   # an entry that names another slot's faction
            copies.check_entry(dict(own, faction='x4lan_player3'))

    def test_another_game_another_dlc_set_or_a_changed_entry_is_refused(self):
        e = copies.entry('hq', 2, self.root)
        with tempfile.TemporaryDirectory() as d:   # this PC has no pirate DLC
            with self.assertRaisesRegex(copies.PlotError, "not the session's .*set of DLCs"):
                copies.generate([e], game_data(d, patch=False))
        with tempfile.TemporaryDirectory() as d:   # another game version
            with self.assertRaisesRegex(copies.PlotError, "not the session's"):
                copies.generate([e], game_data(d, plot=PLOT.replace('128km', '129km')))
        with self.assertRaisesRegex(copies.PlotError, 'another launcher version'):
            copies.generate([dict(e, sha256='0' * 64)], self.root)
        for bad in (dict(e, slot=1), dict(e, copy='Other'), dict(e, target='md/setup.xml'), dict(e, id='unknown'), dict(e, sources=[])):
            with self.assertRaises(copies.PlotError):
                copies.check_entry(bad)
        with self.assertRaisesRegex(copies.PlotError, 'unknown plot'):
            copies.entry('nothing', 2, self.root)

    def test_a_script_of_another_shape_is_refused(self):
        for plot, why in ((PLOT.replace('<cue name="Start" namespace="this" version="25">', '<cue name="Start" instantiate="true">'), 'static root cue'),
                          (PLOT.replace('name="X4Ep1_Mentor_Subscriptions" xmlns', 'name="Else" xmlns'), 'not the MD script')):
            with tempfile.TemporaryDirectory() as d:
                with self.assertRaisesRegex(copies.PlotError, why):
                    copies.entry('hq', 2, game_data(d, plot=plot))
        with tempfile.TemporaryDirectory() as d:   # an extension that replaces the file instead of patching it
            root = game_data(d)
            (root / 'ego_dlc_pirate/md/x4ep1_mentor_subscription.xml').write_text(PLOT, encoding='utf-8')
            with self.assertRaisesRegex(copies.PlotError, 'replaces'):
                copies.entry('hq', 2, root)

    def test_copies_are_written_into_a_composed_extension_and_never_over_a_file(self):
        e = copies.entry('hq', 2, self.root)
        target = Path(self.dir.name) / 'ext'
        self.assertEqual(copies.write(copies.generate([e], self.root), target), ['md/x4lan_plot_hq_p2.xml'])
        self.assertTrue((target / 'md/x4lan_plot_hq_p2.xml').is_file())
        with self.assertRaisesRegex(copies.PlotError, 'never replaces'):
            copies.write(copies.generate([e], self.root), target)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = game_data(self.dir.name)
        self.spec = {'mode': 'factions', 'slots': {'2': {'gamestart': 'a'}, '3': {'gamestart': 'b'}}}

    def test_a_slot_gets_its_plot_entries_and_the_overlay_only_the_recipe(self):
        session_start.add_plots(self.spec, {3: ['hq']}, self.root)
        plots = copies.slot_plots(self.spec['slots'])
        self.assertEqual(list(plots), [3])
        files = session_start.story_files({}, plots)
        self.assertEqual(sorted(files), ['md-copies.json', 'md/x4lan_story.xml'])
        recipe = json.loads(files['md-copies.json'])
        self.assertEqual((recipe['format'], [e['copy'] for e in recipe['copies']]), ('X4LAN_MD_COPIES_1', ['X4Ep1_Mentor_Subscriptions_P3']))
        for text in files.values():   # no game text in an overlay
            self.assertNotIn('128km', text)
            self.assertNotIn('create_station', text)
        out = Path(self.dir.name) / 'overlay'
        session_start.write_story_files(out, {}, plots)
        self.assertEqual([e['slot'] for e in copies.read_recipe(out)], [3])
        self.assertEqual(copies.read_recipe(Path(self.dir.name)), [])   # an overlay without copies

    def test_the_story_md_begins_each_copy_under_its_own_code(self):
        session_start.add_plots(self.spec, {2: ['hq'], 3: ['hq']}, self.root)
        story = {2: {'extension': 'base', 'file': 'gs_a.xml', 'script': 'GS_A', 'cue': 'Start'}}
        md = ET.fromstring(session_start.story_md(story, copies.slot_plots(self.spec['slots'])).split('-->', 1)[1])
        branches = {e.get('value'): e for e in md.find('cues/cue/actions') if e.tag == 'do_elseif'}
        self.assertEqual(sorted(branches), ['$slot == 102', '$slot == 103', '$slot == 2'])
        self.assertEqual(branches['$slot == 2'].find('signal_cue_instantly').get('cue'), 'md.GS_A.Start')
        self.assertEqual(branches['$slot == 102'].find('signal_cue_instantly').get('cue'), 'md.X4Ep1_Mentor_Subscriptions_P2.Start')
        self.assertEqual(branches['$slot == 103'].find('signal_cue_instantly').get('cue'), 'md.X4Ep1_Mentor_Subscriptions_P3.Start')
        self.assertEqual((session_start.story_code(2), session_start.story_code(2, 1), session_start.story_code(3, 2)), (2, 102, 203))
        # the diagnostic's signals: each of a plot's own debug cues, in the stock script (slot 1) and in every copy
        debug = {e.get('value'): e.find('signal_cue').get('cue') for e in md.find("cues/cue[@name='PlotDebug']/actions")}
        self.assertEqual(debug, {'event.param2 == 101': 'md.X4Ep1_Mentor_Subscriptions.Debug_Create_HQ',
                                 'event.param2 == 1101': 'md.X4Ep1_Mentor_Subscriptions.Debug_Unlock_Research',
                                 'event.param2 == 102': 'md.X4Ep1_Mentor_Subscriptions_P2.Debug_Create_HQ',
                                 'event.param2 == 1102': 'md.X4Ep1_Mentor_Subscriptions_P2.Debug_Unlock_Research',
                                 'event.param2 == 103': 'md.X4Ep1_Mentor_Subscriptions_P3.Debug_Create_HQ',
                                 'event.param2 == 1103': 'md.X4Ep1_Mentor_Subscriptions_P3.Debug_Unlock_Research',
                                 'event.param2 == 2101': 'md.X4Ep1_Mentor_Subscriptions.SHORTCUT_Claiming_The_HQ',
                                 'event.param2 == 2102': 'md.X4Ep1_Mentor_Subscriptions_P2.SHORTCUT_Claiming_The_HQ',
                                 'event.param2 == 2103': 'md.X4Ep1_Mentor_Subscriptions_P3.SHORTCUT_Claiming_The_HQ'})
        # without copies the MD is what it was
        self.assertEqual(session_start.story_md(story), session_start.story_md(story, {}))

    def test_a_ready_copy_begins_with_the_stock_scripts_own_variable_set(self):
        ready = copies.entry('hq_ready', 2, self.root, 'x4lan_player2')
        plain = copies.entry('hq', 2, self.root, 'x4lan_player2')
        self.assertEqual((ready['target'], ready['copy']), ('md/x4lan_plot_hq_ready_p2.xml', 'X4Ep1_Mentor_Subscriptions_P2'))
        self.assertNotEqual(ready['sha256'], plain['sha256'])
        text = copies.generate([ready], self.root)[ready['target']]
        start = ET.fromstring(text.split('-->', 1)[1]).find("cues/cue[@name='Start']")
        self.assertEqual(start.find("actions/set_value[@name='$story_hq_boso']").get('exact'), 'true')
        self.assertEqual(start.find("actions/set_value[@name='$HQPosition']").get('exact'), 'position.[128km, 0m, 198km]')
        text = copies.generate([plain], self.root)[plain['target']]
        self.assertIn('exact="gamestart.storystate.story_hq_boso"', text)
        # a script whose root cue does not set the variable is refused
        with tempfile.TemporaryDirectory() as d:
            other = game_data(d, plot=PLOT.replace('$story_hq_boso', '$story_other'))
            with self.assertRaisesRegex(copies.PlotError, r'does not set \$story_hq_boso once'):
                copies.entry('hq_ready', 2, other)
            copies.entry('hq', 2, other)
        # one script, one copy per slot
        self.spec['slots']['2']['plots'] = [plain, ready]
        with self.assertRaisesRegex(copies.PlotError, 'one script twice'):
            copies.slot_plots(self.spec['slots'])

    def test_player_1_begins_with_a_plots_result_through_the_stock_scripts_own_cues(self):
        coop = {'mode': 'coop', 'slots': {'2': {}}}
        session_start.add_plots(coop, {1: ['hq_ready']}, self.root)   # in coop too: the headquarters the players share
        first = copies.first_plots(coop)
        self.assertEqual(first, [{'id': 'hq_ready', 'script': 'X4Ep1_Mentor_Subscriptions', 'instantly': ['Debug_Create_HQ_With_Boso'],
                                  'signal': ['Debug_Unlock_Research'], 'cancel': ['Phase_SignalLeak']}])
        self.assertNotIn('plots', coop['slots']['2'])
        files = session_start.story_files({}, {}, first)
        self.assertEqual(sorted(files), ['md/x4lan_story.xml'])   # no copy, no recipe
        md = ET.fromstring(files['md/x4lan_story.xml'].split('-->', 1)[1])
        ready = md.find("cues/cue[@name='PlotReady']")
        self.assertEqual(ready.find('conditions/event_object_signalled').get('param'), "'x4lan_plot_ready'")
        branch = ready.find("actions/do_elseif[@value='event.param2 == 1']")
        self.assertEqual(branch.find('do_if').get('value'), 'player.headquarters')   # a faction that has one keeps it
        steps = [(e.tag, e.get('cue')) for e in branch.find('do_else') if e.tag != 'debug_text']
        self.assertEqual(steps, [('signal_cue_instantly', 'md.X4Ep1_Mentor_Subscriptions.Debug_Create_HQ_With_Boso'),
                                 ('signal_cue', 'md.X4Ep1_Mentor_Subscriptions.Debug_Unlock_Research'),
                                 ('cancel_cue', 'md.X4Ep1_Mentor_Subscriptions.Phase_SignalLeak')])
        self.assertEqual(ready.find('actions')[1].get('value'), 'md.$X4LAN_PlotReady.indexof.{event.param2}')   # once per game
        # the stock script's debug cues stay reachable for a diagnosis
        debug = {e.get('value'): e.find('signal_cue').get('cue') for e in md.find("cues/cue[@name='PlotDebug']/actions")}
        self.assertEqual(debug, {'event.param2 == 101': 'md.X4Ep1_Mentor_Subscriptions.Debug_Create_HQ',
                                 'event.param2 == 1101': 'md.X4Ep1_Mentor_Subscriptions.Debug_Unlock_Research',
                                 'event.param2 == 2101': 'md.X4Ep1_Mentor_Subscriptions.SHORTCUT_Claiming_The_HQ'})
        # with a copy for another slot both are in one session
        session_start.add_plots(self.spec, {1: ['hq_ready'], 3: ['hq_ready']}, self.root)
        self.assertEqual([e['id'] for e in self.spec['first_plots']], ['hq_ready'])
        self.assertEqual(self.spec['slots']['3']['plots'][0]['id'], 'hq_ready')
        # refusals: a plot without a form for player 1, a script without the cues, a changed entry
        with self.assertRaisesRegex(session_start.SessionError, 'no form for player 1'):
            session_start.add_plots(self.spec, {1: ['hq']}, self.root)
        with self.assertRaisesRegex(session_start.SessionError, 'two plots of one script'):
            session_start.add_plots(self.spec, {1: ['hq_ready', 'hq_ready']}, self.root)
        with tempfile.TemporaryDirectory() as d:
            other = game_data(d, plot=PLOT.replace('Debug_Unlock_Research', 'Debug_Other'))
            with self.assertRaisesRegex(session_start.SessionError, r"with the cues \['Debug_Unlock_Research'\]"):
                session_start.add_plots({'mode': 'coop', 'slots': {}}, {1: ['hq_ready']}, other)
        with self.assertRaisesRegex(copies.PlotError, 'not a first plot entry'):
            copies.first_plots({'first_plots': [dict(first[0], signal=['Start'])]})
        self.assertEqual(copies.first_plots({}), [])
        self.assertEqual(session_start.story_files({}, {}, []), {})

    def test_refusals(self):
        with self.assertRaisesRegex(session_start.SessionError, 'is for factions mode'):   # coop: one faction, one headquarters
            session_start.add_plots({'mode': 'coop', 'slots': {'2': {}}}, {2: ['hq']}, self.root)
        session_start.add_plots(self.spec, {3: ['hq']}, self.root)
        self.assertEqual(self.spec['slots']['3']['plots'][0]['faction'], 'x4lan_player3')
        with self.assertRaisesRegex(session_start.SessionError, 'slot 4 has no per-player start'):
            session_start.add_plots(self.spec, {4: ['hq']}, self.root)
        with self.assertRaisesRegex(session_start.SessionError, 'named twice'):
            session_start.add_plots(self.spec, {2: ['hq', 'hq']}, self.root)
        with self.assertRaisesRegex(session_start.SessionError, 'unknown plot'):
            session_start.add_plots(self.spec, {2: ['other']}, self.root)
        self.spec['slots']['2']['plots'] = [dict(copies.entry('hq', 3, self.root))]   # an entry of another slot
        with self.assertRaisesRegex(copies.PlotError, 'another slot'):
            copies.slot_plots(self.spec['slots'])


class ComposeTests(unittest.TestCase):
    def test_the_launcher_makes_the_copies_from_this_pcs_game_data(self):
        from lockstep.launcher import extension, gamedata
        with tempfile.TemporaryDirectory() as d:
            root = game_data(d)
            spec = {'mode': 'factions', 'slots': {'2': {'gamestart': 'a'}}}
            session_start.add_plots(spec, {2: ['hq']}, root)
            overlay, target = Path(d) / 'overlay', Path(d) / 'ext'
            session_start.write_story_files(overlay, {}, copies.slot_plots(spec['slots']))
            real, gamedata.unpack = gamedata.unpack, lambda game, cache, log=print: root
            try:
                self.assertEqual(extension.plot_copies(overlay, target, Path(d) / 'view'), ['md/x4lan_plot_hq_p2.xml'])
                self.assertIn('X4Ep1_Mentor_Subscriptions_P2', (target / 'md/x4lan_plot_hq_p2.xml').read_text(encoding='utf-8'))
                self.assertEqual(extension.plot_copies(Path(d), Path(d) / 'ext2', Path(d) / 'view'), [])   # no recipe: nothing
                (root / 'base/md/x4ep1_mentor_subscription.xml').write_text(PLOT.replace('198km', '199km'), encoding='utf-8')
                with self.assertRaisesRegex(SystemExit, "plot copies: .*not the session's"):
                    extension.plot_copies(overlay, Path(d) / 'ext3', Path(d) / 'view')
            finally:
                gamedata.unpack = real


if __name__ == '__main__':
    unittest.main()
