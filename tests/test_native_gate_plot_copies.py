"""Per-player copies of stock plot scripts (tools/dev/native_gate_plot_copies.py), the story MD that begins them, the
play overlay's config and the launcher's compose step; a start's story as a slot's copy and the plot after the Terran cadet
story (24 Nov). Synthetic: plot and start scripts and a DLC patch of our own text in a made-up game data folder; no X4 and
no game file. One test reads the game's own two Terran scripts when X4LAN_GAMEDATA names the launcher's unpacked data."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
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
players = load('native_gate_players_plots_t', DEV / 'native_gate_players.py')
stock = load('session_start_tests_for_copies', ROOT / 'tests/test_native_gate_session_start.py')   # its synthetic starts
try:
    from lxml import etree
except ImportError:
    etree = None
MD_XSD = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re')) / 'gamescripts/resolved/base/libraries/md.xsd'

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


# ---------------------------------------------------------------- a start's story as a slot's copy, and the plot after it

# Own text in the shape of the Terran cadet start script and of the Terran story after it (not the game's files): the root
# cue gated on the start and the start's setup cue, a second root cue of that start, the reads of the other script's actors
# and library, the other script's wait for the cadet's last step, its tests of the game's start.
GS_TERRAN1 = '''<?xml version="1.0" encoding="utf-8"?>
<mdscript name="GS_Terran1" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="md.xsd">
  <cues>
    <cue name="Start" namespace="this" module="x4ep1_gamestart_terran1" version="3">
      <conditions>
        <event_cue_completed cue="md.Setup_Gamestarts.X4ep1_Gamestart_Terran_Shared"/>
      </conditions>
      <actions>
        <set_value name="$MissionCue" exact="this"/>
      </actions>
      <cues>
        <cue name="Setup_Actors">
          <delay exact="1s"/>
          <actions>
            <set_value name="$WingmateActor" exact="md.Story_Yaki.Start.$WingmateActor"/>
            <debug_text text="'player.module is ' + player.module"/>
          </actions>
        </cue>
        <cue name="Ch1_Maintenance">
          <actions>
            <include_actions ref="md.Story_Yaki.Setup_Wingmate"/>
            <create_ship name="$Ship" macro="macro.ship_tst_s_scout_01_a_macro" zone="player.zone"/>
            <set_value name="md.GS_Terran1.Start.$Seen" exact="true"/>
          </actions>
        </cue>
        <cue name="Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission">
          <conditions><event_cue_signalled/></conditions>
          <actions><reward_player money="25000Cr"/></actions>
        </cue>
      </cues>
    </cue>
    <cue name="PatchSVCourtRelations" module="x4ep1_gamestart_terran1">
      <conditions><event_game_loaded/></conditions>
      <actions><set_faction_relation faction="faction.player" otherfaction="faction.court" value="0.1"/></actions>
    </cue>
  </cues>
</mdscript>
'''
STORY_YAKI = '''<?xml version="1.0" encoding="utf-8"?>
<mdscript name="Story_Yaki" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="md.xsd">
  <cues>
    <cue name="Start" namespace="this" mapeditor="false" version="7">
      <conditions>
        <check_any>
          <event_cue_signalled/>
          <check_all>
            <event_cue_signalled cue="md.Setup.Start"/>
            <check_value value="player.galaxy.macro.ismacro.{macro.xu_ep2_universe_macro}"/>
          </check_all>
        </check_any>
      </conditions>
      <actions>
        <set_value name="$RelationThreshold" exact="faction.terran.relation.dock.min"/>
      </actions>
      <cues>
        <cue name="Story_Activation_Manager">
          <cues>
            <cue name="Check_For_Terran_Cadet_Gamestart_Ending">
              <conditions><event_cue_signalled cue="md.GS_Terran1.Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission"/></conditions>
              <actions><signal_cue cue="Activate_Story"/></actions>
            </cue>
            <cue name="Check_For_Friendly_Relations">
              <conditions>
                <event_player_relation_changed faction="faction.terran"/>
                <check_value value="player.module != 'x4ep1_gamestart_terran1'"/>
              </conditions>
              <actions><signal_cue cue="Activate_Story"/></actions>
            </cue>
            <cue name="Activate_Story">
              <conditions><event_cue_signalled/></conditions>
              <actions><signal_cue cue="Offer_Management"/></actions>
            </cue>
          </cues>
        </cue>
        <cue name="Offer_Management">
          <conditions><event_cue_signalled/></conditions>
          <cues>
            <cue name="Mission_Offer_Terran_Cadet_Shortcut" onfail="cancel">
              <conditions>
                <check_value value="player.module == 'x4ep1_gamestart_terran1'"/>
                <check_value value="(@player.sector.owner == faction.terran) and (faction.terran.relationto.{faction.player} ge $RelationThreshold)"/>
              </conditions>
            </cue>
          </cues>
        </cue>
        <library name="Setup_Wingmate">
          <actions>
            <set_value name="md.Story_Yaki.Start.$WingmateShip" exact="null"/>
            <set_value name="$Line" exact="if (player.module == 'x4ep1_gamestart_terran1') then 1 else 2"/>
          </actions>
        </library>
      </cues>
    </cue>
    <cue name="Patch_Userdata" instantiate="true">
      <conditions><event_cue_signalled cue="md.Setup.Start"/></conditions>
      <actions><debug_text text="'patch'"/></actions>
    </cue>
  </cues>
</mdscript>
'''
TERRAN_START = '''<?xml version="1.0" encoding="utf-8"?>
<diff>
 <add sel="/gamestarts">
  <gamestart id="x4ep1_gamestart_terran1" group="1">
   <location galaxy="xu_ep2_universe_macro" zone="zone001_cluster_98_sector001_macro"><position x="1" y="2" z="3"/></location>
   <player macro="character_tst_macro" money="5"><ship macro="ship_tst_s_fighter_01_a_macro"/></player>
  </gamestart>
 </add>
</diff>
'''
CADET = {'extension': 'ego_dlc_terran', 'file': 'gs_terran1.xml', 'script': 'GS_Terran1', 'cue': 'Start'}
T1 = 'x4ep1_gamestart_terran1'


def terran_data(folder):
    """The session start tests' synthetic stock data plus a Terran-cadet-shaped start, its script and the plot after it."""
    libraries, scripts = stock.stock_tree(folder)
    for name, text in ((libraries / 'ego_dlc_terran/libraries/gamestarts.xml', TERRAN_START),
                       (scripts / 'ego_dlc_terran/md/gs_terran1.xml', GS_TERRAN1),
                       (scripts / 'ego_dlc_terran/md/story_yaki.xml', STORY_YAKI)):
        name.parent.mkdir(parents=True, exist_ok=True)
        name.write_text(text, encoding='utf-8')
    return stock.gamestarts.Catalog(libraries, scripts)


def copy_tree(entry, root):
    return ET.fromstring(copies.generate([entry], root)[entry['target']].split('-->', 1)[1])


def values(tree):
    return [v for e in tree.iter() for v in e.attrib.values()]


class StoryCopyTests(unittest.TestCase):
    """Several players on one start, each with that start's story, and the plot after the Terran cadet's story for the
    player who played it (24 Nov playtest: player 2 finished the cadet story, the Terran story after it never offered its
    first mission). Synthetic scripts and starts (own text); that X4 runs the copies as each player is a live run's."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.catalog = terran_data(self.dir.name)
        self.root = self.catalog.scripts

    def test_the_cadet_story_and_the_plot_after_it_are_one_set_of_copies_per_slot(self):
        self.assertEqual(copies.follow_on(T1), ['yaki'])
        self.assertEqual(copies.follow_on('x4ep1_gamestart_discover'), [])
        for k in (2, 3, 8):
            with self.subTest(slot=k):
                story, yaki = copies.story_entries(CADET, T1, k, self.root, None, ['yaki'])
                self.assertEqual((story['id'], story['copy'], story['target'], story['module'], story['links']),
                                 ('story', f'GS_Terran1_P{k}', f'md/x4lan_plot_story_p{k}.xml', T1, {'Story_Yaki': f'Story_Yaki_P{k}'}))
                self.assertEqual((yaki['id'], yaki['copy'], yaki['target'], yaki['module'], yaki['links']),
                                 ('yaki', f'Story_Yaki_P{k}', f'md/x4lan_plot_yaki_p{k}.xml', T1, {'GS_Terran1': f'GS_Terran1_P{k}'}))
                cadet = copy_tree(story, self.root)
                self.assertEqual(cadet.get('name'), f'GS_Terran1_P{k}')
                start = cadet.find("cues/cue[@name='Start']")
                # no module (the session's base start may be another), its own signal only, its start's other root cue gone
                self.assertNotIn('module', start.attrib)
                self.assertEqual([(c.tag, c.attrib) for c in start.find('conditions')], [('event_cue_signalled', {})])
                self.assertEqual([c.get('name') for c in cadet.find('cues')], ['Start'])
                # it reads its own slot's Terran story, and itself under its own name
                found = values(cadet)
                self.assertIn(f'md.Story_Yaki_P{k}.Start.$WingmateActor', found)
                self.assertIn(f'md.Story_Yaki_P{k}.Setup_Wingmate', found)
                self.assertIn(f'md.GS_Terran1_P{k}.Start.$Seen', found)
                self.assertFalse(any('md.Story_Yaki.' in v or 'md.GS_Terran1.' in v for v in found))
                # the game's start is the slot's start, but a name inside a text stays text
                self.assertIn(f"'player.module is ' + '{T1}'", found)
                plot = copy_tree(yaki, self.root)
                self.assertEqual(plot.get('name'), f'Story_Yaki_P{k}')
                start = plot.find("cues/cue[@name='Start']")
                self.assertEqual([(c.tag, c.attrib) for c in start.find('conditions')], [('event_cue_signalled', {})])
                found = values(plot)
                self.assertIn(f'md.GS_Terran1_P{k}.Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission', found)
                self.assertIn(f'md.Story_Yaki_P{k}.Start.$WingmateShip', found)
                self.assertIn(f"'{T1}' == '{T1}'", found)                  # the cadet's shortcut to the first offer
                self.assertIn(f"'{T1}' != '{T1}'", found)                  # "only if the player is NOT the cadet"
                self.assertIn(f"if ('{T1}' == '{T1}') then 1 else 2", found)
                self.assertFalse(any('player.module' in v for v in found))
                self.assertFalse(any('md.Story_Yaki.' in v or 'md.GS_Terran1.' in v for v in found))
                self.assertIn('faction.terran.relationto.{faction.player} ge $RelationThreshold', ' '.join(found))   # coop: one faction
                other = plot.find("cues/cue[@name='Patch_Userdata']")
                self.assertEqual(other.find('conditions/check_value').get('value'), 'Start.state != cuestate.waiting')
        # another slot's set names that slot's scripts only
        two, three = (copies.story_entries(CADET, T1, k, self.root, None, ['yaki']) for k in (2, 3))
        self.assertNotEqual([e['sha256'] for e in two], [e['sha256'] for e in three])
        self.assertFalse(any('_P2' in v for v in values(copy_tree(three[1], self.root))))
        # factions mode: the slot's own faction where the scripts name the player faction
        story, yaki = copies.story_entries(CADET, T1, 3, self.root, 'x4lan_player3', ['yaki'])
        self.assertIn('faction.terran.relationto.{faction.x4lan_player3} ge $RelationThreshold', ' '.join(values(copy_tree(yaki, self.root))))
        # without the plot after it: the story alone, linking nothing
        [alone] = copies.story_entries(CADET, T1, 3, self.root)
        self.assertNotIn('links', alone)
        self.assertIn('md.Story_Yaki.Setup_Wingmate', values(copy_tree(alone, self.root)))

    def test_refusals(self):
        story, yaki = copies.story_entries(CADET, T1, 3, self.root, None, ['yaki'])
        with self.assertRaisesRegex(copies.PlotError, 'copied with that story'):   # the plot after a story never comes alone
            copies.entry('yaki', 3, self.root)
        with self.assertRaisesRegex(copies.PlotError, 'no plot that continues the story of x4ep1_gamestart_discover'):
            copies.story_entries(dict(CADET), 'x4ep1_gamestart_discover', 3, self.root, None, ['yaki'])
        with self.assertRaisesRegex(copies.PlotError, 'not a start script'):
            copies.story_entries(dict(CADET, file='story_yaki.xml'), T1, 3, self.root)
        for bad in (dict(yaki, module='x4ep1_gamestart_discover'), dict(yaki, module=None), dict(story, module=None),
                    dict(story, links={'Story_Yaki': 'Story_Yaki_P2'}), dict(story, links={'GS_Terran1': 'GS_Terran1_P3'}),
                    dict(story, file='../gs_terran1.xml'), dict(story, extension='not_stock'), dict(story, module="a' or '")):
            with self.subTest(bad={k: bad[k] for k in ('id', 'module', 'file', 'extension') if k in bad}), \
                    self.assertRaises(copies.PlotError):
                copies.check_entry(bad)
        with self.assertRaisesRegex(copies.PlotError, 'another launcher version'):
            copies.generate([dict(yaki, links={})], self.root)
        # a slot's set must be whole, one story per slot, and never both the claimed stock script and a copy
        with self.assertRaisesRegex(copies.PlotError, 'links a script the slot has no copy of'):
            copies.slot_plots({'3': {'plots': [yaki]}})
        with self.assertRaisesRegex(copies.PlotError, 'both as the stock script and as a copy'):
            copies.slot_plots({'3': {'story': CADET, 'plots': [story, yaki]}})
        self.assertEqual(list(copies.slot_plots({'3': {'plots': [story, yaki]}})), [3])

    def spec(self, base, players, starts, story, omit=None, mode='coop'):
        omit = {**{k: 'world' for k in starts}, **(omit or {})}
        return session_start.build_spec(self.catalog, base, players, mode, starts, omit, story=story)

    def test_players_2_and_3_on_the_cadet_start_each_play_their_own_story_and_the_plot_after_it(self):
        spec = self.spec('war_start', 3, {2: T1, 3: T1}, [2, 3])
        for k in ('2', '3'):
            slot = spec['slots'][k]
            self.assertIsNone(slot['story'])   # the stock script stays nobody's: a copy with the plot after it is the slot's
            self.assertEqual([(e['id'], e['copy']) for e in slot['plots']], [('story', f'GS_Terran1_P{k}'), ('yaki', f'Story_Yaki_P{k}')])
        self.assertEqual([spec['slots'][k]['hull']['x'] for k in '23'], [1, 301])   # side by side at the start's place
        self.assertEqual(session_start.story_slots(spec['slots']), {})
        # --omit K=yaki: that slot plays the story alone, and the first such slot claims the stock script
        alone = self.spec('war_start', 3, {2: T1, 3: T1}, [2, 3], {3: 'world,yaki'})
        self.assertEqual([e['copy'] for e in alone['slots']['2']['plots']], ['GS_Terran1_P2', 'Story_Yaki_P2'])
        self.assertEqual(session_start.story_slots(alone['slots']), {3: CADET})
        self.assertNotIn('plots', alone['slots']['3'])
        # factions mode: each slot's copies name its own faction
        own = self.spec('war_start', 3, {2: T1, 3: T1}, [2, 3], mode='factions')
        self.assertEqual({e['faction'] for k in '23' for e in own['slots'][k]['plots']} , {'x4lan_player2', 'x4lan_player3'})
        self.assertEqual([e['faction'] for e in own['slots']['3']['plots']], ['x4lan_player3', 'x4lan_player3'])

    def test_three_players_on_the_session_start_each_with_its_story(self):
        """Slot 1 plays the base start's story by X4's own module rule (the stock scripts); slots 2 and 3 on the same
        start play their copies (before 24 Nov the second player with a start's story was refused)."""
        spec = self.spec(T1, 3, {2: T1, 3: T1}, [2, 3])
        self.assertEqual(spec['base_start'], T1)
        self.assertEqual([spec['slots'][k]['hull']['x'] for k in '23'], [301, 601])   # beside slot 1's own ship
        for k in '23':
            self.assertEqual([e['copy'] for e in spec['slots'][k]['plots']], [f'GS_Terran1_P{k}', f'Story_Yaki_P{k}'])
        # x4ls new-session --base-start <cadet> --players 3 --story 2,3: the slots without --start take the base start's
        # ship (newsession.own_starts) and so its story too
        from lockstep.launcher import newsession
        o = newsession.NewSessionOptions(base_start=T1, players=3, mode='coop', out=Path(self.dir.name) / 'out',
                                         gate_dll=Path('x.dll'), story=(2, 3))
        starts, omit = newsession.own_starts(o, self.catalog, log=lambda *_: None)
        self.assertEqual((starts, omit), ({2: T1, 3: T1}, {2: 'world', 3: 'world'}))
        self.assertEqual(session_start.build_spec(self.catalog, T1, 3, 'coop', starts, omit, story=o.story), spec)
        # a start without a plot after its story: still a copy for every slot after the first player on it
        plain = session_start.build_spec(stock.story_catalog(Path(self.dir.name) / 'plain'), 'war_start', 4, 'coop',
                                         {2: 'station_start', 3: 'station_start', 4: 'station_start'}, {k: 'world' for k in (2, 3, 4)},
                                         story=[2, 3, 4])
        self.assertEqual(session_start.story_slots(plain['slots']), {2: plain['slots']['2']['story']})
        self.assertEqual([[e['copy'] for e in plain['slots'][k].get('plots', [])] for k in '234'],
                         [[], ['GS_Station_P3'], ['GS_Station_P4']])

    def test_the_session_files_begin_each_copy_in_its_slot_and_carry_no_game_text(self):
        spec = self.spec(T1, 3, {2: T1, 3: T1}, [2, 3])
        plots = copies.slot_plots(spec['slots'])
        files = session_start.story_files(session_start.story_slots(spec['slots']), plots)
        self.assertEqual(sorted(files), ['md-copies.json', 'md/x4lan_story.xml'])   # no patch: no slot claims a stock script
        for text in files.values():
            self.assertNotIn('Setup_Wingmate', text.replace('.DEBUG_Setup_Wingmate', ''))   # a debug cue's name only, a reference
            self.assertNotIn('create_ship', text)
            self.assertNotIn('relationto', text)
        recipe = json.loads(files['md-copies.json'])
        self.assertEqual([(e['slot'], e['copy']) for e in recipe['copies']],
                         [(2, 'GS_Terran1_P2'), (2, 'Story_Yaki_P2'), (3, 'GS_Terran1_P3'), (3, 'Story_Yaki_P3')])
        md = ET.fromstring(files['md/x4lan_story.xml'].split('-->', 1)[1])
        branches = {e.get('value'): e.find('signal_cue_instantly').get('cue') for e in md.find('cues/cue/actions') if e.tag == 'do_elseif'}
        self.assertEqual(branches, {'$slot == 102': 'md.GS_Terran1_P2.Start', '$slot == 202': 'md.Story_Yaki_P2.Start',
                                    '$slot == 103': 'md.GS_Terran1_P3.Start', '$slot == 203': 'md.Story_Yaki_P3.Start'})
        # the diagnostic: the plot's own activation step, in the stock script (player 1's) and in each slot's copy
        debug = {e.get('value'): e.find('signal_cue').get('cue') for e in md.find("cues/cue[@name='PlotDebug']/actions")}
        self.assertEqual(debug, {'event.param2 == 101': 'md.Story_Yaki.Activate_Story', 'event.param2 == 202': 'md.Story_Yaki_P2.Activate_Story',
                                 'event.param2 == 203': 'md.Story_Yaki_P3.Activate_Story',
                                 # its second debug cue, 1000 higher: the wingmate's own setup (the wingmate's tests)
                                 'event.param2 == 1101': 'md.Story_Yaki.DEBUG_Setup_Wingmate',
                                 'event.param2 == 1202': 'md.Story_Yaki_P2.DEBUG_Setup_Wingmate',
                                 'event.param2 == 1203': 'md.Story_Yaki_P3.DEBUG_Setup_Wingmate'})
        # the play overlay's lines: each slot begins its copies (no stock script: false), both stock scripts are player 1's
        lines, stories, listed, _ = players.story_config({'slots': spec['slots']})
        self.assertEqual(lines, ["X4LAN_CONFIG.native_gate_story = {[2]={false,'GS_Terran1_P2.Start','Story_Yaki_P2.Start'},"
                                 "[3]={false,'GS_Terran1_P3.Start','Story_Yaki_P3.Start'}}",
                                 "X4LAN_CONFIG.native_gate_plot_first = {'GS_Terran1','Story_Yaki'}"])
        # a slot that claims the stock cadet script keeps it as its own: it is not player 1's then
        alone = self.spec('war_start', 3, {2: T1, 3: T1}, [2, 3], {3: 'world,yaki'})
        lines, _, _, _ = players.story_config({'slots': alone['slots']})
        self.assertEqual(lines, ["X4LAN_CONFIG.native_gate_story = {[2]={false,'GS_Terran1_P2.Start','Story_Yaki_P2.Start'},"
                                 "[3]='GS_Terran1.Start'}", "X4LAN_CONFIG.native_gate_plot_first = {'Story_Yaki'}"])
        self.assertEqual(players.story_config(None), ([], {}, {}, []))

    def test_a_follow_on_plot_is_not_asked_for_by_plot_and_hq_comes_after_the_story(self):
        spec = self.spec('war_start', 3, {2: T1, 3: T1}, [2, 3], mode='factions')
        with self.assertRaisesRegex(session_start.SessionError, 'continues the story of x4ep1_gamestart_terran1 and comes with it'):
            session_start.add_plots(spec, {3: ['yaki']}, self.root)
        root = game_data(Path(self.dir.name) / 'hq')   # the synthetic headquarters plot of this file
        session_start.add_plots(spec, {3: ['hq']}, root)
        self.assertEqual([e['id'] for e in spec['slots']['3']['plots']], ['story', 'yaki', 'hq'])
        self.assertEqual([e['id'] for e in spec['slots']['2']['plots']], ['story', 'yaki'])

    def test_the_launcher_makes_a_slots_copies_on_each_pc(self):
        from lockstep.launcher import extension, gamedata
        spec = self.spec(T1, 3, {2: T1, 3: T1}, [3])
        overlay, target = Path(self.dir.name) / 'overlay', Path(self.dir.name) / 'ext'
        session_start.write_story_files(overlay, session_start.story_slots(spec['slots']), copies.slot_plots(spec['slots']))
        real, gamedata.unpack = gamedata.unpack, lambda game, cache, log=print: self.root
        try:
            self.assertEqual(extension.plot_copies(overlay, target, Path(self.dir.name) / 'view'),
                             ['md/x4lan_plot_story_p3.xml', 'md/x4lan_plot_yaki_p3.xml'])
        finally:
            gamedata.unpack = real
        self.assertIn('md.Story_Yaki_P3.Setup_Wingmate', (target / 'md/x4lan_plot_story_p3.xml').read_text(encoding='utf-8'))

    @unittest.skipUnless(os.environ.get('X4LAN_GAMEDATA'), "set X4LAN_GAMEDATA to the launcher's unpacked stock data (read only)")
    def test_the_stock_cadet_story_and_story_yaki(self):
        """The game's own GS_Terran1 and Story_Yaki (read from this PC's unpacked data, nothing written to the tree)."""
        root = Path(os.environ['X4LAN_GAMEDATA'])
        catalog = stock.gamestarts.Catalog(root, root)
        self.assertEqual(catalog.story(T1), CADET)
        for k in (2, 3):
            story, yaki = copies.story_entries(CADET, T1, k, root, None, copies.follow_on(T1))
            files = copies.generate([story, yaki], root)
            cadet, plot = (ET.fromstring(files[e['target']].split('-->', 1)[1]) for e in (story, yaki))
            found = values(cadet) + values(plot)
            self.assertFalse(any(re.search(r'md\.(Story_Yaki|GS_Terran1)\.', v) for v in found))
            self.assertFalse(any('player.module' in v for v in found))
            self.assertFalse(any(re.search(rf'(GS_Terran1|Story_Yaki)_P{5 - k}', v) for v in found))   # nothing of the other slot's set
            self.assertNotIn('module', cadet.find("cues/cue[@name='Start']").attrib)
            self.assertEqual([c.get('name') for c in cadet.find('cues')], ['Start'])   # PatchSVCourtRelations left out
            self.assertEqual(cadet.findall(".//include_actions")[0].get('ref'), f'md.Story_Yaki_P{k}.Setup_Wingmate')
            ending = plot.find(".//cue[@name='Check_For_Terran_Cadet_Gamestart_Ending']/conditions/event_cue_signalled")
            self.assertEqual(ending.get('cue'), f'md.GS_Terran1_P{k}.Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission')
            shortcut = plot.find(".//cue[@name='Mission_Offer_Terran_Cadet_Shortcut']/conditions/check_value")
            self.assertEqual(shortcut.get('value'), f"'{T1}' == '{T1}'")
            friendly = plot.find(".//cue[@name='Check_For_Friendly_Relations']/conditions")
            self.assertIn(f"'{T1}' != '{T1}'", [c.get('value') for c in friendly])
            self.assertIsNotNone(plot.find(".//cue[@name='Activate_Story']"))   # the diagnostic's step
            # the wingmate's tests' step: the copy's own wingmate setup, under its root cue (its frames are the slot's), and
            # the setup protects the ship of the player whose frame runs it (player.ship: the slot's, in the slot's scope)
            self.assertIsNotNone(plot.find("cues/cue[@name='Start']/cues/cue[@name='DEBUG_Setup_Wingmate']"))
            setup = plot.find("cues/cue[@name='Start']/cues/library[@name='Setup_Wingmate']")
            protect = [o for o in setup.iter('create_order') if o.get('id') == "'ProtectShip'"]
            self.assertEqual([[(p.get('name'), p.get('value')) for p in o.iter('param')] for o in protect], [[('target', '$CurrentPlayerShip')]])
            self.assertIn({'name': '$CurrentPlayerShip', 'exact': 'player.ship'}, [s.attrib for s in setup.iter('set_value')])
            self.assertIsNotNone(cadet.find(".//cue[@name='Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission']"))
            if os.environ.get('X4LAN_MD_SCHEMA') == '1' and etree is not None and MD_XSD.is_file():
                schema = getattr(type(self), 'schema', None) or etree.XMLSchema(etree.parse(str(MD_XSD)))
                type(self).schema = schema
                for name, text in files.items():
                    self.assertTrue(schema.validate(etree.fromstring(text.encode('utf-8'))), (name, schema.error_log))


if __name__ == '__main__':
    unittest.main()
