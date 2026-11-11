"""Sessions from stock game starts and per-player starts (reports/LOCKSTEP-STARTS-20261101.txt).

Synthetic stock data written by the tests (no Egosoft file is read or copied): the start catalogue and its
classes (a/b/c/refused), the diff reader, templates and the per-mode part rules; the session spec, the generated
hull MD and capture overlay, the capture script's hull step (LuaJIT), registering a synthetic save, the identity
plan kits (Lua core vs Python reference, the real glue on synthetic clients) and the N-player builder's --session.
Offline only: no X4, no lane. The stock data catalogue of the real game runs with X4LAN_GAME_LIBRARIES and
X4LAN_GAME_SCRIPTS set (skipped otherwise).
"""
import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
sys.path.insert(0, str(DEV))
CAPTURE = (DEV / 'native_gate_snapshot.lua').read_text(encoding='utf-8')
CORE = (DEV / 'native_gate_player_identity_core.lua').read_text(encoding='utf-8')
IDENTITY_MD = DEV / 'native_gate_player_identity.xml'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gamestarts = load('native_gate_gamestarts_t', DEV / 'native_gate_gamestarts.py')
session_start = load('native_gate_session_start_t', DEV / 'native_gate_session_start.py')
identity = load('player_identity_reference_starts', DEV / 'native_gate_player_identity.py')
players = load('native_gate_players_builder_starts', DEV / 'native_gate_players.py')
identity_tests = load('identity_tests_for_starts', ROOT / 'tests/test_native_gate_player_identity.py')
try:
    from lxml import etree
except ImportError:
    etree = None
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
MD_XSD = RE / 'gamescripts/resolved/base/libraries/md.xsd'

# ---------------------------------------------------------------- synthetic stock data (own text)

GAMESTARTS = '''<?xml version="1.0" encoding="utf-8"?>
<gamestarts>
 <groups><group group="1"/></groups>
 <gamestart id="alpha_start" group="1">
  <info><item name="n" value="v"/></info>
  <cutscene ref="c"/>
  <location galaxy="xu_ep2_universe_macro" zone="zone001_cluster_90_sector001_macro">
   <position x="10.4" y="-20" z="30.6"/><rotation yaw="200" pitch="3" roll="1"/>
  </location>
  <player macro="character_tst_macro" money="7000">
   <ship macro="ship_tst_s_fighter_01_a_macro">
    <loadout>
     <macros><engine macro="engine_tst_macro" path="../con_engine_01"/><weapon macro="weapon_tst_macro" path="../con_weapon_01" optional="true"/></macros>
     <ammunition><ammunition macro="missile_tst_macro" exact="4"/></ammunition>
     <software><software ware="software_tstmk1"/></software>
     <virtualmacros><thruster macro="thruster_tst_macro"/></virtualmacros>
    </loadout>
   </ship>
   <inventory><ware ware="inv_tst" amount="2"/></inventory>
   <blueprints><ware ware="module_tst_b"/><ware ware="module_tst_a"/></blueprints>
   <research><ware ware="research_tst"/></research>
   <encyclopedia><entry type="warebasket" ref="basket_tst"/></encyclopedia>
   <theme paint="paint_tst"/>
  </player>
  <universe><factions><relations><relation faction="argon" otherfaction="player" relation="0.0032"/></relations></factions></universe>
 </gamestart>
 <gamestart id="beta_start" group="1">
  <location galaxy="xu_ep2_universe_macro" zone="zone002_cluster_91_sector002_macro"><position x="0" y="0" z="0"/></location>
  <player macro="character_tst_macro" money="1000"><ship macro="ship_tst_s_scout_01_a_macro"/></player>
 </gamestart>
 <gamestart id="station_start" group="1">
  <location galaxy="xu_ep2_universe_macro" zone="zone001_cluster_92_sector001_macro" room="room_tst_macro" faction="player" station="station_tst_macro">
   <player disembarked="true"/>
  </location>
  <player macro="character_tst_macro" money="200000"><ship macro="ship_tst_s_scout_01_a_macro"/></player>
 </gamestart>
 <gamestart id="war_start" group="1">
  <location galaxy="xu_ep2_universe_macro" zone="zone001_cluster_93_sector001_macro"/>
  <player macro="character_tst_macro" money="1"><ship macro="ship_tst_s_scout_01_a_macro"/></player>
  <universe><factions><relations><relation faction="argon" otherfaction="xenon" relation="1"/></relations></factions><god enabled="false"/></universe>
 </gamestart>
 <gamestart id="suit_start" group="1">
  <location galaxy="xu_ep2_universe_macro" zone="zone001_cluster_94_sector001_macro"/>
  <player macro="character_tst_macro" money="1"><ship macro="ship_tst_xs_spacesuit_01_a_macro"/></player>
 </gamestart>
 <gamestart id="lesson_start" group="1" tags="tutorial nosave">
  <location galaxy="tutorial_galaxy_macro" zone="zone001_cluster_95_sector001_macro"/>
  <player macro="character_tst_macro" money="1"><ship macro="ship_tst_s_scout_01_a_macro"/></player>
 </gamestart>
 <!-- PLACEHOLDER_TST -->
</gamestarts>
'''
DLC_DIFF = '''<?xml version="1.0" encoding="utf-8"?>
<diff>
 <replace sel="/gamestarts/comment()[. = ' PLACEHOLDER_TST ']">
  <gamestart id="gamma_start" group="1">
   <location galaxy="xu_ep2_universe_macro" zone="zone001_cluster_96_sector001_macro"><position x="5" y="6" z="7"/></location>
   <player macro="character_tst_macro" money="3000"><ship macro="ship_tst_s_scout_01_a_macro"/></player>
  </gamestart>
 </replace>
 <add sel="/gamestarts/gamestart[@id='alpha_start']/universe/factions/relations">
  <relation faction="split" otherfaction="player" relation="-0.032"/>
 </add>
 <replace sel="/gamestarts/gamestart[@id='beta_start']/player/@money">9000</replace>
 <remove sel="/gamestarts/gamestart[@id='beta_start']/player/blueprints" silent="true"/>
</diff>
'''
GOD = '''<god><stations><station id="st_tst" race="teladi" owner="player" type="factory"><quotas><quota galaxy="1" gamestart="station_start"/></quotas></station></stations></god>'''
GS_ALPHA = '''<mdscript name="GS_Alpha"><cues><cue name="Start" module="alpha_start"><conditions><event_cue_signalled cue="md.Setup.GameStart"/></conditions>
<actions/><cues><cue name="Map"><actions>
<set_value name="$M" exact="[macro.cluster_90_sector001_macro, macro.cluster_97_sector001_macro, macro.cluster_90_sector001_macro]"/>
<run_actions ref="md.LIB_Generic.UncoverMap_SectorsAndGates"><param name="InitialMap" value="$M"/><param name="UncoverStationFaction" value="faction.argon"/>
<param name="UncoverShipyardsAndWharfs" value="true"/></run_actions></actions></cue></cues></cue></cues></mdscript>'''
GS_STATION = '''<mdscript name="GS_Station"><cues><cue name="Start" module="station_start"><actions><create_ship name="$s" macro="macro.x_macro" zone="player.zone"/></actions></cue></cues></mdscript>'''
OTHER = '''<mdscript name="Other"><cues><cue name="Story"><conditions><check_value value="player.module == 'war_start'"/></conditions>
<actions><add_inventory entity="player.entity" ware="ware.inv_tst"/></actions></cue>
<cue name="NotFor"><conditions><check_value value="player.module != 'beta_start'"/></conditions><actions><add_inventory entity="player.entity" ware="ware.inv_tst"/></actions></cue></cues></mdscript>'''


def stock_tree(root):
    root = Path(root)
    files = {'libraries/base/libraries/gamestarts.xml': GAMESTARTS, 'libraries/ego_dlc_split/libraries/gamestarts.xml': DLC_DIFF,
             'libraries/base/libraries/god.xml': GOD, 'scripts/base/md/gs_alpha.xml': GS_ALPHA, 'scripts/base/md/gs_station.xml': GS_STATION,
             'scripts/ego_dlc_split/md/other.xml': OTHER, 'libraries/not_stock/libraries/gamestarts.xml': '<diff><remove sel="/gamestarts"/></diff>'}
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding='utf-8')
    return root / 'libraries', root / 'scripts'


def catalog_in(directory):
    return gamestarts.Catalog(*stock_tree(directory))


class CatalogTests(unittest.TestCase):
    def test_every_start_gets_one_class_with_its_world_parts_named(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            classes = {r['id']: r['class'] for r in c.rows()}
            self.assertEqual(classes, {'alpha_start': 'c', 'beta_start': 'a', 'gamma_start': 'a', 'station_start': 'b', 'war_start': 'b',
                                       'suit_start': 'a', 'lesson_start': '-'})
            self.assertEqual(c.sources, ['base', 'ego_dlc_split'])   # a non-stock extension is not read
            station = gamestarts.classify(c.record('station_start'))[1]
            self.assertIn('player-owned station station_tst_macro (location)', station)
            self.assertTrue(any(r.startswith('god station id=st_tst owner=player type=factory race=teladi') for r in station))
            self.assertTrue(any('MD base/md/gs_station.xml Start (module): create_ship' in r for r in station))
            war = gamestarts.classify(c.record('war_start'))[1]
            self.assertIn('relation argon/xenon 1', war)
            self.assertIn('universe god disabled', war)
            self.assertTrue(any('ego_dlc_split/md/other.xml Story (test): add_inventory' in r for r in war))
            # 'player.module != id' runs for every other start: not a part of beta_start.
            self.assertEqual(gamestarts.classify(c.record('beta_start')), ('a', []))
            self.assertEqual(gamestarts.classify(c.record('alpha_start'))[1], ['rebuilt: map uncover (MD base/md/gs_alpha.xml Start)'])
            self.assertIn('tutorial', gamestarts.classify(c.record('lesson_start'))[1])

    def test_stock_diffs_apply_in_extension_order(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            self.assertEqual(c.record('beta_start')['player']['money'], 9000)
            self.assertEqual(c.record('alpha_start')['player']['reputation'], {'argon': 0.0032, 'split': -0.032})
            self.assertEqual(c.record('gamma_start')['player']['location']['zone'], 'zone001_cluster_96_sector001_macro')
        root = ET.fromstring('<a><b id="1"/><b id="2"><c x="1"/></b></a>')
        for diff, error in (('<diff><replace sel="/a/b">x</replace></diff>', 'matches 2 nodes'),
                            ('<diff><remove sel="/a/b[@id=\'3\']"/></diff>', 'matches nothing'),
                            ('<diff><add sel="/a/b[last()]"/></diff>', 'not supported'),
                            ('<diff><move sel="/a"/></diff>', 'unsupported diff operation'),
                            ('<diff><add sel="a/b"/></diff>', 'not absolute')):
            with self.subTest(diff=diff), self.assertRaises(gamestarts.CatalogError) as caught:
                gamestarts.apply_diff(root, ET.fromstring(diff))
            self.assertIn(error, str(caught.exception))
        gamestarts.apply_diff(root, ET.fromstring('<diff><add sel="/a/b[@id=\'2\']" pos="before"><d/></add>'
                                                  '<replace sel="/a/b[2]/c/@x">5</replace><remove sel="/a/b[1]"/></diff>'))
        self.assertEqual(ET.tostring(root, encoding='unicode'), '<a><d /><b id="2"><c x="5" /></b></a>')

    def test_per_player_and_session_base_refusals_name_the_start_and_why(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            self.assertIsNone(gamestarts.per_player_refusal(c.record('alpha_start')))
            self.assertIn('station_start is class b: player-owned station', gamestarts.per_player_refusal(c.record('station_start')))
            self.assertIn('spacesuit', gamestarts.per_player_refusal(c.record('suit_start')))
            self.assertIn('is class -', gamestarts.per_player_refusal(c.record('lesson_start')))
            self.assertIsNone(gamestarts.session_base_refusal(c.record('war_start')))    # class b: the session's own start
            self.assertIn('ship in open space (on_foot)', gamestarts.session_base_refusal(c.record('station_start')))
            self.assertIn('spacesuit', gamestarts.session_base_refusal(c.record('suit_start')))
            self.assertIn('refused', gamestarts.session_base_refusal(c.record('lesson_start')))
            with self.assertRaises(gamestarts.CatalogError):
                gamestarts.template(c, 'war_start')
            with self.assertRaises(gamestarts.CatalogError):
                c.record('nope')

    def test_template_resolves_values_and_slot_kits_follow_the_mode_rules(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            t = gamestarts.template(c, 'alpha_start')
            self.assertEqual(t['place'], {'zone': 'zone001_cluster_90_sector001_macro', 'sector': 'cluster_90_sector001_macro',
                                          'x': 10, 'y': -20, 'z': 31, 'yaw': -160})
            self.assertEqual(t['ship']['equipment'], [{'kind': 'engine', 'macro': 'engine_tst_macro', 'path': '../con_engine_01'},
                                                      {'kind': 'weapon', 'macro': 'weapon_tst_macro', 'path': '../con_weapon_01'}])
            self.assertEqual((t['money'], t['inventory'], t['blueprints'], t['research']),
                             (7000, {'inv_tst': 2}, ['module_tst_a', 'module_tst_b'], ['research_tst']))
            self.assertEqual(t['known'], {'part': 'map uncover', 'source': 'MD base/md/gs_alpha.xml Start', 'station_faction': 'argon',
                                          'shipyards': True, 'sectors': ['cluster_90_sector001_macro', 'cluster_97_sector001_macro']})
            self.assertEqual(t['presentation'], ['cutscene', 'info', 'player encyclopedia', 'player theme'])
            with self.assertRaises(gamestarts.CatalogError) as caught:
                gamestarts.slot_kit(t, 'factions', 2)
            self.assertIn('parts blueprints, known, research cannot be applied for slot 2 in factions mode', str(caught.exception))
            # Coop takes a start's reputation too (the shared faction only gains: reputation_raise in the identity plan).
            self.assertEqual(gamestarts.slot_kit(t, 'coop', 3)['reputation'], [{'faction': 'argon', 'value': 3200}, {'faction': 'split', 'value': -32000}])
            # The start's inventory goes to the slot's own player in both modes, unless it is left out by name.
            self.assertEqual(gamestarts.slot_kit(t, 'coop', 3, ['reputation'])['inventory'], [{'ware': 'inv_tst', 'amount': 2}])
            self.assertEqual(gamestarts.slot_kit(t, 'factions', 2, ['research', 'blueprints', 'known'])['inventory'], [{'ware': 'inv_tst', 'amount': 2}])
            kit = gamestarts.slot_kit(t, 'factions', 2, ['research', 'blueprints', 'known', 'inventory'])
            self.assertEqual(kit, {'slot': 2, 'gamestart': 'alpha_start', 'money': 7000, 'omitted': ['blueprints', 'inventory', 'known', 'research'],
                                   'reputation': [{'faction': 'argon', 'value': 3200}, {'faction': 'split', 'value': -32000}]})
            kit = gamestarts.slot_kit(t, 'coop', 3, ['inventory', 'reputation'])
            self.assertEqual(kit['blueprints'], ['module_tst_a', 'module_tst_b'])
            self.assertEqual(kit['known'], {'sectors': ['cluster_90_sector001_macro', 'cluster_97_sector001_macro'], 'shipyards': True, 'faction': 'argon'})
            # An applicable part may be left out on purpose (named, never silently).
            self.assertNotIn('blueprints', gamestarts.slot_kit(t, 'coop', 3, ['inventory', 'reputation', 'blueprints']))
            for bad in ((t, 'coop', 1, ['inventory', 'reputation']), (t, 'coop', 2, ['inventory', 'reputation', 'ship'])):
                with self.subTest(bad=bad[2:]), self.assertRaises(gamestarts.CatalogError):
                    gamestarts.slot_kit(*bad)
            beta = gamestarts.slot_kit(gamestarts.template(c, 'beta_start'), 'factions', 2)
            self.assertEqual(beta, {'slot': 2, 'gamestart': 'beta_start', 'money': 9000, 'omitted': []})

    def test_command_line_needs_the_game_data_roots(self):
        env = {k: os.environ.pop(k) for k in ('X4LAN_GAME_LIBRARIES', 'X4LAN_GAME_SCRIPTS') if k in os.environ}
        try:
            with contextlib.redirect_stderr(io.StringIO()) as err, self.assertRaises(SystemExit):
                gamestarts.main(['catalog'])
            self.assertIn('never copied into git', err.getvalue())
            with tempfile.TemporaryDirectory() as d:
                libraries, scripts = stock_tree(d)
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    gamestarts.main(['catalog', '--libraries', str(libraries), '--scripts', str(scripts)])
                self.assertIn('alpha_start', out.getvalue())
                self.assertNotIn('lesson_start', out.getvalue())
        finally:
            os.environ.update(env)

    @unittest.skipUnless(os.environ.get('X4LAN_GAME_LIBRARIES') and os.environ.get('X4LAN_GAME_SCRIPTS'),
                         'set X4LAN_GAME_LIBRARIES and X4LAN_GAME_SCRIPTS to the resolved stock data (read only)')
    def test_stock_game_data_classes(self):
        c = gamestarts.Catalog(os.environ['X4LAN_GAME_LIBRARIES'], os.environ['X4LAN_GAME_SCRIPTS'])
        classes = {r['id']: r['class'] for r in c.rows()}
        self.assertEqual(classes['x4ep1_gamestart_fight'], 'c')
        self.assertEqual(classes['x4ep1_gamestart_discover'], 'c')
        self.assertEqual(classes['x4ep1_gamestart_trade'], 'b')
        self.assertEqual(classes['custom_creative'], 'b')
        self.assertIsNone(gamestarts.session_base_refusal(c.record('custom_creative')))


# ---------------------------------------------------------------- session spec, capture, register

class SpecTests(unittest.TestCase):
    def test_spec_resolves_each_slot_and_offsets_slots_that_share_a_start(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            spec = session_start.build_spec(c, 'war_start', 4, 'coop', {2: 'alpha_start', 3: 'alpha_start', 4: 'beta_start'},
                                            {2: 'inventory,reputation', 3: ['inventory', 'reputation']})
            self.assertEqual((spec['base_start'], spec['base_class']), ('war_start', 'b'))
            self.assertEqual([spec['slots'][k]['hull']['x'] for k in ('2', '3', '4')], [10, 310, 0])
            self.assertEqual([spec['slots'][k]['hull']['seed'] for k in ('2', '3', '4')], [5102, 5103, 5104])
            self.assertEqual(spec['slots']['2']['inventory_not_applied'], {'inv_tst': 2})
            self.assertNotIn('inventory', spec['slots']['2']['kit'])
            kept = session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'alpha_start'}, {2: 'reputation'})
            self.assertEqual(kept['slots']['2']['kit']['inventory'], [{'ware': 'inv_tst', 'amount': 2}])   # applied: the kit carries it
            self.assertEqual(kept['slots']['2']['inventory_not_applied'], {})
            self.assertEqual(spec['slots']['4']['kit'], {'slot': 4, 'gamestart': 'beta_start', 'money': 9000, 'omitted': []})
            same = session_start.build_spec(c, 'beta_start', 3, 'coop', {2: 'beta_start', 3: 'beta_start'})   # the base's own start: beside slot 1
            self.assertEqual([same['slots'][k]['hull']['x'] for k in ('2', '3')], [300, 600])
            refusals = [((c, 'station_start', 2, 'coop', {}), 'session base refused'),
                        ((c, 'war_start', 2, 'coop', {2: 'station_start'}), 'slot 2: start station_start is class b'),
                        ((c, 'war_start', 2, 'coop', {1: 'beta_start'}), 'slot 1 plays the session base start war_start'),
                        ((c, 'war_start', 9, 'coop', {}), '2..8 players'),
                        ((c, 'war_start', 2, 'coop', {}, {2: 'inventory'}), 'slot 2 has no per-player start')]
            for call, text in refusals:
                with self.subTest(text=text), self.assertRaises(session_start.SessionError) as caught:
                    session_start.build_spec(*call)
                self.assertIn(text, str(caught.exception))

    def test_ship_only_takes_a_class_b_starts_ship_place_and_kit_when_the_world_parts_are_omitted_by_name(self):
        # --omit K=world: the player gets the start's ship, place and kit; its world-level parts are listed, not applied.
        delta = ''' <gamestart id="delta_start" group="1">
  <location galaxy="xu_ep2_universe_macro" sector="cluster_98_sector001_macro"><position x="38140" y="40" z="114620"/><rotation yaw="6.4"/></location>
  <player macro="character_tst_macro" money="5000"><ship macro="ship_tst_s_fighter_02_a_macro">
   <loadout><macros><engine macro="engine_tst_macro" path="../con_engine_01"/></macros></loadout></ship></player>
  <universe><god enabled="false"/></universe>
 </gamestart>
 <!-- PLACEHOLDER_TST -->'''
        with tempfile.TemporaryDirectory() as d:
            libraries, scripts = stock_tree(d)
            path = libraries / 'base/libraries/gamestarts.xml'
            path.write_text(path.read_text(encoding='utf-8').replace(' <!-- PLACEHOLDER_TST -->', delta), encoding='utf-8')
            c = gamestarts.Catalog(libraries, scripts)
            rec = c.record('delta_start')
            self.assertEqual(gamestarts.classify(rec)[0], 'b')
            self.assertIn('is class b', gamestarts.per_player_refusal(rec))
            self.assertIsNone(gamestarts.per_player_refusal(rec, world=True))
            self.assertIn('is class -', gamestarts.per_player_refusal(c.record('lesson_start'), world=True))      # refused stays refused
            self.assertIn('is class b', gamestarts.per_player_refusal(c.record('station_start')))                 # not without the word
            self.assertIsNone(gamestarts.per_player_refusal(c.record('station_start'), world=True))               # its ship near its zone
            near = gamestarts.template(c, 'station_start', world=True)
            self.assertEqual(near['place'], {'zone': 'zone001_cluster_92_sector001_macro', 'sector': 'cluster_92_sector001_macro',
                                             'x': 0, 'y': 0, 'z': 0, 'yaw': 0, 'near': True})
            near_spec = session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'station_start'}, {2: 'world'})
            self.assertTrue(near_spec['slots']['2']['hull']['near'])
            near_ship = ET.fromstring(session_start.hulls_md(near_spec).split('-->', 1)[1]).find('.//create_ship')
            self.assertEqual(near_ship.get('zone'), '$zone')
            self.assertIsNone(near_ship.find('position'))
            self.assertEqual(near_ship.find('safepos').attrib, {'value': 'position.[0m, 0m, 0m]', 'min': '3km', 'max': '8km'})
            row = next(r for r in c.rows() if r['id'] == 'delta_start')
            self.assertTrue(row['per_player'] and row['ship_only'] is None)
            tpl = gamestarts.template(c, 'delta_start', world=True)
            self.assertEqual(tpl['place'], {'zone': None, 'sector': 'cluster_98_sector001_macro', 'x': 38140, 'y': 40, 'z': 114620, 'yaw': 6})
            self.assertTrue(tpl['world_omitted'])
            self.assertEqual(gamestarts.template(c, 'alpha_start')['world_omitted'], [])
            with self.assertRaises(session_start.SessionError) as caught:
                session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'delta_start'})
            self.assertIn('is class b', str(caught.exception))
            self.assertIn('add --omit 2=world', str(caught.exception))
            spec = session_start.build_spec(c, 'war_start', 3, 'coop', {2: 'delta_start', 3: 'alpha_start'},
                                            {2: 'world', 3: 'inventory,reputation'})
            slot = spec['slots']['2']
            self.assertEqual((slot['class'], slot['hull']['zone'], slot['hull']['sector'], slot['hull']['x']),
                             ('b', None, 'cluster_98_sector001_macro', 38140))
            self.assertEqual(slot['world_omitted'], tpl['world_omitted'])
            self.assertEqual(slot['kit'], {'slot': 2, 'gamestart': 'delta_start', 'money': 5000, 'omitted': []})   # 'world' is no kit part
            self.assertEqual(spec['slots']['3']['world_omitted'], [])
            md = ET.fromstring(session_start.hulls_md(spec).split('-->', 1)[1])
            actions = md.find('.//actions')
            sector = actions.find('find_sector')
            self.assertEqual(sector.attrib, {'name': '$zone', 'macro': 'macro.cluster_98_sector001_macro'})
            ships = actions.findall('.//create_ship')
            self.assertEqual([s.get('sector') for s in ships], ['$zone', None])     # slot 2 by its sector, slot 3 by its zone
            self.assertEqual([s.get('zone') for s in ships], [None, '$zone'])
            self.assertEqual(ships[0].find('position').attrib, {'x': '38140m', 'y': '40m', 'z': '114620m'})

    def test_hull_md_creates_the_stock_loadout_at_the_start_and_answers_the_capture(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            spec = session_start.build_spec(c, 'war_start', 3, 'factions', {2: 'alpha_start', 3: 'gamma_start'},
                                            {2: 'blueprints,inventory,known,research'})
            text = session_start.hulls_md(spec)
            root = ET.fromstring(text.split('\n', 2)[2])
            self.assertEqual(root.get('name'), 'X4LAN_SessionHulls')
            self.assertEqual([c.get('name') for c in root.iter('cue')], ['Create'])
            ships = list(root.iter('create_ship'))
            self.assertEqual([(s.get('macro'), s.get('zone'), s.get('seed')) for s in ships],
                             [('macro.ship_tst_s_fighter_01_a_macro', '$zone', '5102'), ('macro.ship_tst_s_scout_01_a_macro', '$zone', '5103')])
            self.assertEqual([z.get('macro') for z in root.iter('find_zone')],
                             ['macro.zone001_cluster_90_sector001_macro', 'macro.zone001_cluster_96_sector001_macro'])
            self.assertEqual([(p.get('x'), p.get('y'), p.get('z')) for p in root.iter('position')], [('10m', '-20m', '31m'), ('5m', '6m', '7m')])
            first = next(root.iter('create_loadout'))
            self.assertEqual([(e.tag, e.get('macro'), e.get('path')) for e in first.find('macros')],
                             [('engine', 'engine_tst_macro', '../con_engine_01'), ('weapon', 'weapon_tst_macro', '../con_weapon_01')])
            self.assertEqual(first.find('ammunition/ammunition').attrib, {'macro': 'missile_tst_macro', 'exact': '4'})
            self.assertEqual([o.get('exact') for o in root.iter('owner')], ['faction.player'] * 2)
            self.assertEqual([e.get('param') for e in root.iter('raise_lua_event')], ["if $failed == '' then $ships.count else -1"])
            self.assertIsNone(root.find('.//find_closest_safe_pos'))
            self.assertIsNone(root.find('.//safepos'))
            bad = json.loads(json.dumps(spec))
            bad['slots']['2']['hull']['macro'] = "x' or true or '"
            with self.assertRaises(session_start.SessionError):
                session_start.hulls_md(bad)
            dll = Path(d) / 'fixed.dll'
            dll.write_bytes(b'fixed')
            out = session_start.build_capture(spec, dll, Path(d) / 'cap')
            self.assertEqual((out / 'md/x4lan_session_hulls.xml').read_text(encoding='utf-8'), text)
            self.assertIn("player.module == 'war_start'", (out / 'md/native_gate_battle.xml').read_text(encoding='utf-8'))
            config = (out / 'ui/config.lua.append').read_text()
            self.assertTrue(config.endswith('X4LAN_CONFIG.native_gate_snapshot_gamestart = "war_start"\nX4LAN_CONFIG.native_gate_snapshot_hulls = 2\n'))
            self.assertIn('md/x4lan_session_hulls.xml', json.loads((out / 'x4lane-manifest.json').read_text()))
            command = session_start.capture_command(spec, out, 'collect')
            self.assertIn('--gamestart war_start', command)
            self.assertIn('--expect "NATIVE_SNAPSHOT HULLS_READY count=2"', command)
            self.assertIn('SESSION_HULL_FAILED', command)
            # Factions kits set pool accounts: the new game is made with the pool library (own accounts) of this tree.
            out = Path(d) / 'cap'
            self.assertEqual((out / 'libraries/factions.xml').read_bytes(), (session_start.EXTENSION / 'libraries/factions.xml').read_bytes())
            self.assertEqual(json.loads((out / 'x4lane-manifest.json').read_text())['libraries/factions.xml'],
                             hashlib.sha256((out / 'libraries/factions.xml').read_bytes()).hexdigest())
            # Without per-player starts the capture is the plain one for that start.
            plain = session_start.build_spec(c, 'beta_start', 2, 'coop', {})
            out = session_start.build_capture(plain, dll, Path(d) / 'cap2')
            self.assertFalse((out / 'md/x4lan_session_hulls.xml').exists())
            self.assertFalse((out / 'libraries').exists())
            self.assertNotIn('hulls', (out / 'ui/config.lua.append').read_text())
            coop = session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'alpha_start'}, {2: 'inventory,reputation'})
            self.assertFalse((session_start.build_capture(coop, dll, Path(d) / 'cap3') / 'libraries').exists())

    @unittest.skipUnless(os.environ.get('X4LAN_MD_SCHEMA') == '1' and etree is not None and MD_XSD.is_file(),
                         'set X4LAN_MD_SCHEMA=1 with lxml and the read-only x4-re md.xsd')
    def test_generated_hull_md_and_identity_md_validate_against_game_schema(self):
        schema = etree.XMLSchema(etree.parse(str(MD_XSD)))
        with tempfile.TemporaryDirectory() as d:
            spec = session_start.build_spec(catalog_in(d), 'war_start', 2, 'coop', {2: 'alpha_start'}, {2: 'inventory,reputation'})
            path = Path(d) / 'hulls.xml'
            path.write_text(session_start.hulls_md(spec), encoding='utf-8')
            for md in (path, IDENTITY_MD):
                self.assertTrue(schema.validate(etree.parse(str(md))), schema.error_log)


CAPTURE_HARNESS = '''
  X4LAN_CONFIG={enabled=false,test_profile='45841',native_gate_snapshot='capture',native_gate_snapshot_gamestart='war_start'}
  logs={};raised={};callbacks={};now=7;saved=nil;paused=false
  package.loadlib=function(path) assert(path:find('fixedtick'));return function() return {start=function() return 1 end} end end
  local ffi={cdef=function() end,string=function(s) return s end,
    C={GetSaveFolderPath=function() return 'fake/45841/save' end,IsPlayerValid=function() return true end,
      IsGamePaused=function() return paused end,GetCurrentGameTime=function() return now end,
      IsSaveListLoadingComplete=function() return true end,ReloadSaveList=function() end,
      IsFullscreenCutsceneActive=function() return false end,GetGameStartName=function() return 'war_start' end}}
  require=function(name) assert(name=='ffi');return ffi end
  DebugError=function(s) logs[#logs+1]=s end
  RegisterEvent=function(name,fn) callbacks[name]=fn end
  SetScript=function(name,fn) update=fn end
  GetSaveList=function() return {} end
  IsSavingPossible=function() return true end
  Pause=function() paused=true end
  SaveGame=function(slot) saved=slot end
  AddUITriggeredEvent=function(screen,control,param) raised[#raised+1]=screen..'/'..control..(param and param.count and ('#'..param.count) or '') end
'''


# A start script of the stock shape: one root cue Start, gated on the start (module=) and on another cue's signal.
GS_STATION_STORY = '''<mdscript name="GS_Station"><cues><cue name="Start" module="station_start" namespace="this">
<conditions><event_cue_completed cue="md.Setup_Gamestarts.Station_Shared"/></conditions>
<actions><create_ship name="$s" macro="macro.x_macro" zone="player.zone"/></actions></cue></cues></mdscript>'''


def story_catalog(directory, extension='base'):
    libraries, scripts = stock_tree(directory)
    (scripts / 'base/md/gs_station.xml').unlink()
    (scripts / extension / 'md').mkdir(parents=True, exist_ok=True)
    (scripts / extension / 'md/gs_station.xml').write_text(GS_STATION_STORY, encoding='utf-8')
    return gamestarts.Catalog(libraries, scripts)


class StoryTests(unittest.TestCase):
    """A start's story for the player who took that start (slots 2..N): the catalogue's entry, the spec, the generated
    patch and MD. Synthetic data, text only: that X4 applies the patch and runs the instance in the slot's scope is a
    matter of a live run."""

    def test_the_catalogue_names_a_start_script_of_the_stock_shape_only(self):
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            self.assertIsNone(c.story('station_start'))   # its Start cue has no signal to replace
            self.assertIsNone(c.story('beta_start'))      # no script
            self.assertIsNone(gamestarts.template(c, 'station_start', world=True)['story'])
        with tempfile.TemporaryDirectory() as d:
            c = story_catalog(d)
            story = {'extension': 'base', 'file': 'gs_station.xml', 'script': 'GS_Station', 'cue': 'Start'}
            self.assertEqual(c.story('station_start'), story)
            self.assertEqual(gamestarts.template(c, 'station_start', world=True)['story'], story)
            self.assertIsNone(gamestarts.template(c, 'alpha_start')['story'])   # class c: its script is rebuilt, not run

    def test_the_spec_gives_the_story_to_one_slot_and_omit_leaves_it_out(self):
        with tempfile.TemporaryDirectory() as d:
            c = story_catalog(d, 'ego_dlc_split')
            story = {'extension': 'ego_dlc_split', 'file': 'gs_station.xml', 'script': 'GS_Station', 'cue': 'Start'}
            both = ({2: 'station_start', 3: 'station_start'}, {2: 'world', 3: 'world'})
            spec = session_start.build_spec(c, 'war_start', 3, 'coop', *both, story=[2])
            self.assertEqual(spec['slots']['2']['story'], story)
            self.assertIsNone(spec['slots']['3']['story'])
            self.assertEqual(session_start.story_slots(spec['slots']), {2: story})
            # Not asked for and not a start whose story a live run has shown (gamestarts.STORY_DEFAULT): the ship only.
            quiet = session_start.build_spec(c, 'war_start', 3, 'coop', *both)
            self.assertEqual(session_start.story_slots(quiet['slots']), {})
            shown_in = session_start.gamestarts   # the module the spec reads (this file loads its own copy)
            old = shown_in.STORY_DEFAULT
            shown_in.STORY_DEFAULT = frozenset({'station_start'})
            try:   # ...a shown start has it by default: the first slot with it, and --omit K=story leaves it out
                shown = session_start.build_spec(c, 'war_start', 3, 'coop', *both)
                self.assertEqual(session_start.story_slots(shown['slots']), {2: story})
                off = session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'station_start'}, {2: 'world,story'})
                self.assertEqual(session_start.story_slots(off['slots']), {})
            finally:
                shown_in.STORY_DEFAULT = old
            for args, text in ((((c, 'war_start', 3, 'coop', *both), dict(story=[3])), 'another player has the story of station_start'),
                               (((c, 'war_start', 2, 'coop', {2: 'station_start'}, {2: 'world,story'}), dict(story=[2])), 'left out by --omit'),
                               (((c, 'war_start', 2, 'coop', {2: 'beta_start'}), dict(story=[2])), 'has no story script'),
                               (((c, 'war_start', 2, 'coop', {}), dict(story=[2])), 'slot 2 has no per-player start')):
                with self.subTest(text=text), self.assertRaises(session_start.SessionError) as caught:
                    session_start.build_spec(*args[0], **args[1])
                self.assertIn(text, str(caught.exception))
            self.assertEqual(session_start.story_slots(None), {})
            # An older session.json has no story field: no story.
            self.assertEqual(session_start.story_slots({'2': {'gamestart': 'station_start'}}), {})
            for bad in ({'extension': 'not_stock', **{k: story[k] for k in ('file', 'script', 'cue')}}, dict(story, file='../x.xml'),
                        dict(story, script="A' or '"), dict(story, cue=''), dict(story, more=1), 'GS_Station'):
                with self.subTest(bad=bad), self.assertRaises(session_start.SessionError):
                    session_start.story_slots({'2': {'story': bad}})
            with self.assertRaises(session_start.SessionError):
                session_start.story_slots({'2': {'story': story}, '3': {'story': story}})

    def test_a_hull_without_a_pilot_by_name(self):
        # A stock start's own ship has no NPC pilot; a session hull has one by default (it holds the Wait order). --unpiloted K
        # makes slot K's hull without: no pilot and no order in the generated MD.
        with tempfile.TemporaryDirectory() as d:
            c = catalog_in(d)
            args = (c, 'war_start', 3, 'coop', {2: 'alpha_start', 3: 'alpha_start'}, {2: 'inventory,reputation', 3: 'inventory,reputation'})
            spec = session_start.build_spec(*args, unpiloted=[3])
            self.assertNotIn('pilot', spec['slots']['2']['hull'])
            self.assertIs(spec['slots']['3']['hull']['pilot'], False)
            md = ET.fromstring(session_start.hulls_md(spec).split('-->', 1)[1])
            ships = md.findall('.//create_ship')
            self.assertEqual([s.find('pilot') is not None for s in ships], [True, False])
            self.assertEqual(len(md.findall(".//create_order")), 1)
            self.assertEqual(len(md.findall(".//append_to_list[@name='$ships']")), 2)   # both hulls are still counted
            with self.assertRaises(session_start.SessionError) as caught:
                session_start.build_spec(c, 'war_start', 2, 'coop', {}, unpiloted=[2])
            self.assertIn('slot 2 has no per-player start', str(caught.exception))

    def test_the_patch_and_the_begin_script(self):
        base = {'extension': 'base', 'file': 'gs_station.xml', 'script': 'GS_Station', 'cue': 'Start'}
        dlc = {'extension': 'ego_dlc_terran', 'file': 'gs_terran1.xml', 'script': 'GS_Terran1', 'cue': 'Start'}
        files = session_start.story_files({2: dlc, 4: base})
        # A patch of another extension's file lies under extensions/<that extension>/ of ours; a base file at its own path.
        self.assertEqual(sorted(files), ['extensions/ego_dlc_terran/md/gs_terran1.xml', 'md/gs_station.xml', 'md/x4lan_story.xml'])
        self.assertEqual(session_start.story_files({}), {})
        diff = ET.fromstring(files['md/gs_station.xml'].split('-->', 1)[1])
        sel = "/mdscript/cues/cue[@name='Start']"
        # No module, its own signal, nothing else: the cue stays the static cue X4 wrote (an instance of it loses the
        # signals to cues of branches that have not started: the first live run).
        self.assertEqual([(e.tag, e.get('sel'), e.get('type')) for e in diff],
                         [('remove', sel + '/@module', None), ('replace', sel + '/conditions', None)])
        self.assertNotIn('instantiate', files['md/gs_station.xml'])
        self.assertEqual([c.tag for c in diff[1].find('conditions')], ['event_cue_signalled'])
        self.assertEqual(diff[1].find('conditions')[0].attrib, {})   # its own signal, nobody else's
        md = ET.fromstring(files['md/x4lan_story.xml'].split('-->', 1)[1])
        cue = md.find('cues/cue')
        self.assertEqual((md.get('name'), cue.get('name'), cue.get('instantiate')), ('X4LAN_Story', 'Begin', 'true'))
        self.assertEqual(cue.find('conditions/event_object_signalled').attrib, {'object': 'player.galaxy', 'param': "'x4lan_story'"})
        branches = {e.get('value'): e for e in cue.find('actions') if e.tag == 'do_elseif'}
        self.assertEqual(sorted(branches), ['$slot == 2', '$slot == 4'])
        self.assertEqual(branches['$slot == 2'].find('signal_cue_instantly').get('cue'), 'md.GS_Terran1.Start')
        self.assertEqual(branches['$slot == 4'].find('signal_cue_instantly').get('cue'), 'md.GS_Station.Start')
        # Once per slot: the list is asked before any branch and filled in the branch.
        tags = [e.tag for e in cue.find('actions')]
        self.assertEqual(tags, ['set_value', 'do_if', 'do_if', 'do_elseif', 'do_elseif', 'do_else'])
        self.assertEqual(cue.find('actions')[2].get('value'), 'md.$X4LAN_StoryBegun.indexof.{$slot}')
        self.assertEqual(branches['$slot == 2'][0].attrib, {'name': 'md.$X4LAN_StoryBegun', 'exact': '$slot'})

    def test_the_capture_overlay_and_the_session_carry_the_story(self):
        with tempfile.TemporaryDirectory() as d:
            c = story_catalog(d, 'ego_dlc_split')
            spec = session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'station_start'}, {2: 'world'}, story=[2])
            dll = Path(d) / 'fixed.dll'
            dll.write_bytes(b'fixed')
            out = session_start.build_capture(spec, dll, Path(d) / 'cap')
            manifest = json.loads((out / 'x4lane-manifest.json').read_text())
            for name, text in session_start.story_files(session_start.story_slots(spec['slots'])).items():
                self.assertEqual((out / name).read_text(encoding='utf-8'), text)
                self.assertEqual(manifest[name], hashlib.sha256((out / name).read_bytes()).hexdigest())
            self.assertTrue((out / 'extensions/ego_dlc_split/md/gs_station.xml').is_file())
            with self.assertRaises(session_start.SessionError):   # never over a file of the overlay
                session_start.write_story_files(out, session_start.story_slots(spec['slots']))
            plain = session_start.build_spec(c, 'war_start', 2, 'coop', {2: 'station_start'}, {2: 'world'})
            out = session_start.build_capture(plain, dll, Path(d) / 'cap2')
            self.assertFalse((out / 'md/x4lan_story.xml').exists())
            self.assertFalse((out / 'extensions').exists())


class CaptureScriptTests(unittest.TestCase):
    def capture(self, config=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(CAPTURE_HARNESS)
        lua.execute(config)
        lua.execute(CAPTURE)
        lua.execute('''update()
          callbacks['X4LAN_NativeGate.Ready'](nil,12);now=10;update()
          callbacks['X4LAN_NativeGate.Attention'](nil,12);update()''')
        return lua

    def test_hulls_are_requested_after_the_fixture_check_and_the_save_waits_for_them(self):
        lua = self.capture('X4LAN_CONFIG.native_gate_snapshot_hulls=2')
        g = lua.globals()
        self.assertEqual(list(g.raised.values()), ['X4LAN_NativeGate/Spawn', 'X4LAN_NativeGate/InspectAttention', 'X4LAN_SessionHulls/Create#2'])
        lua.execute('update();update()')
        self.assertIsNone(g.saved)   # no answer yet: no save
        lua.execute("callbacks['X4LAN_SessionHulls.Ready'](nil,2);update();update();update()")
        self.assertEqual(g.saved, 'save_009')
        logs = '\n'.join(g.logs.values())
        self.assertIn('NATIVE_SNAPSHOT HULLS_REQUEST count=2', logs)
        self.assertIn('NATIVE_SNAPSHOT HULLS_READY count=2', logs)
        self.assertLess(logs.index('HULLS_READY'), logs.index('SAVE_REQUEST'))

    def test_the_save_waits_while_x4_allows_none(self):
        # The Terran cadet start: its wingmate speaks from second 6 and X4 allows no save until the line is over.
        lua = self.capture()
        g = lua.globals()
        lua.execute('IsSavingPossible=function() return false end;update();update();now=20;update()')
        self.assertIsNone(g.saved)
        self.assertEqual(sum('NATIVE_SNAPSHOT SAVE_WAIT' in line for line in g.logs.values()), 1)
        lua.execute('IsSavingPossible=function() return true end;update()')
        self.assertEqual(g.saved, 'save_009')
        # ...and not for ever: 180 s of game time, then the capture stops loudly.
        lua = self.capture()
        lua.execute('IsSavingPossible=function() return false end;update();update();now=191;update()')
        self.assertIsNone(lua.globals().saved)
        self.assertTrue(any('ERROR' in line and 'native saving unavailable' in line for line in lua.globals().logs.values()))

    def test_a_failed_or_missing_hull_stops_the_capture_loudly(self):
        lua = self.capture('X4LAN_CONFIG.native_gate_snapshot_hulls=2')
        lua.execute("update();callbacks['X4LAN_SessionHulls.Ready'](nil,-1);update();update()")
        self.assertIsNone(lua.globals().saved)
        self.assertTrue(any('ERROR' in line and 'session hulls incomplete: -1 of 2' in line for line in lua.globals().logs.values()))
        # Without hulls the capture saves as before.
        lua = self.capture()
        lua.execute('update();update()')
        self.assertEqual(lua.globals().saved, 'save_009')
        self.assertEqual(sum('SessionHulls' in r for r in lua.globals().raised.values()), 0)
        for value in ('0', '16', '1.5', "'2'"):
            with self.subTest(value=value), self.assertRaises(LuaError):
                self.capture('X4LAN_CONFIG.native_gate_snapshot_hulls=' + value)


FIXTURE = [f'{0xa000 + i:016x}' for i in range(1, 13)]


def session_save(directory, start='war_start', hulls=(('ship_tst_s_fighter_01_a_macro', 'zone001_cluster_90_sector001_macro', 'cluster_90_sector001_macro'),),
                 owner='player'):
    ships = ''.join(f'<component id="[0x{0xa000 + i:x}]" class="ship_s" owner="{"argon" if i <= 6 else "xenon"}" '
                    f'macro="{"ship_arg_s_fighter_01_a_macro" if i <= 6 else "ship_xen_s_fighter_01_a_macro"}">'
                    '<movement class="navigation"/></component>' for i in range(1, 13))
    player = ('<component class="ship_s" id="[0xae79c]" macro="ship_tst_s_scout_01_a_macro"><component class="cockpit" id="[0xae79f]">'
              '<component class="player" id="[0x11e12]"/></component></component>')
    far = ''.join(f'<component class="sector" macro="{sector}" id="[0x{0xc10 + i:x}]"><component class="zone" macro="{zone}" id="[0x{0xd10 + i:x}]">'
                  f'<component class="ship_s" id="[0x{0xb000 + i:x}]" macro="{macro}" owner="{owner}"/></component></component>'
                  for i, (macro, zone, sector) in enumerate(hulls))
    fixture = ''.join(f'<value type="component" value="[0x{0xa000 + i:x}]"/>' for i in range(1, 13))
    hull_refs = ''.join(f'<value type="component" value="[0x{0xb000 + i:x}]"/>' for i in range(len(hulls)))
    xml = (f'<savegame><info><game id="X4" version="900" build="611726" time="7.1" start="{start}" seed="276944566"/></info>'
           '<universe><component class="cluster" id="[0x5]"><component class="sector" macro="cluster_93_sector001_macro" id="[0xc01]">'
           f'<component class="zone" macro="tempzone" id="[0xd01]">{ships}{player}</component></component>{far}</component></universe>'
           f'<script><refs type="list"><ref id="77">{fixture}</ref><ref id="78">{hull_refs}</ref></refs></script>'
           '<md><script name="X4LAN_NativeGate"><cue name="Spawn" state="complete"><vars><value name="$ships" type="list" value="77"/>'
           '</vars></cue></script><script name="X4LAN_SessionHulls"><cue name="Create" state="complete"><vars>'
           '<value name="$ships" type="list" value="78"/><value name="$failed" type="string" value=""/></vars></cue></script></md></savegame>')
    path = Path(directory) / 'save_009.xml.gz'
    with gzip.open(path, 'wt') as stream:
        stream.write(xml)
    return path


BASE_CONFIG = identity_tests.BASE_CONFIG


def make_base(root, sha):
    base = Path(root) / 'base'
    for sub in ('ui', 'native', 'md'):
        (base / sub).mkdir(parents=True)
    (base / 'ui/config.lua.append').write_text(BASE_CONFIG.replace('a' * 64, sha))
    (base / 'ui/native_gate_probe.lua').write_text('-- old probe\n')
    (base / 'native/x4lan_gate_64.dll').write_bytes(b'old dll')
    (base / 'md/native_gate_battle.xml').write_text('<mdscript/>\n')
    (base / 'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
    return base


class RegisterTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.catalog = catalog_in(self.root / 'stock')
        self.spec = session_start.build_spec(self.catalog, 'war_start', 2, 'coop', {2: 'alpha_start'}, {2: 'inventory,reputation'})

    def tearDown(self):
        self.dir.cleanup()

    def register(self, **kw):
        save_dir = self.root / f'save{len(list(self.root.glob("save*")))}'
        save_dir.mkdir()
        return session_start.register(self.spec, session_save(save_dir, **kw))

    def test_register_reads_start_fixture_and_hulls_and_hashes_the_save(self):
        s = self.register()
        self.assertEqual((s['base_start'], s['player_ship'], s['player_sector'], s['fixture']),
                         ('war_start', '00000000000ae79c', 'cluster_93_sector001_macro', FIXTURE))
        self.assertEqual(s['hulls'], {'2': '000000000000b000'})
        self.assertEqual(s['snapshot']['sha256'], hashlib.sha256(Path(s['snapshot']['path']).read_bytes()).hexdigest())
        self.assertEqual(s['slots']['2']['kit']['gamestart'], 'alpha_start')
        cases = [(dict(start='beta_start'), 'not the session base start war_start'),
                 (dict(hulls=()), 'session hulls, the spec 1'),
                 (dict(hulls=(('ship_other_macro', 'zone001_cluster_90_sector001_macro', 'cluster_90_sector001_macro'),)), "'macro'"),
                 (dict(hulls=(('ship_tst_s_fighter_01_a_macro', 'tempzone', 'cluster_90_sector001_macro'),)), "'zone'"),
                 (dict(owner='argon'), "'owner'")]
        for kw, text in cases:
            with self.subTest(text=text), self.assertRaises(session_start.SessionError) as caught:
                self.register(**kw)
            self.assertIn(text, str(caught.exception))

    def write_session(self):
        session = self.register()
        path = self.root / 'session.json'
        path.write_text(json.dumps(session))
        return path, session

    def test_kits_are_made_again_without_a_capture(self):
        """A session registered with the inventory left out gets it: the same save, the same hulls, a new kit."""
        path, session = self.write_session()
        self.assertEqual(session['slots']['2']['kit']['omitted'], ['inventory', 'reputation'])
        again = session_start.rekit(session_start.session_of(path), self.catalog, ['inventory'])
        kit = again['slots']['2']['kit']
        self.assertEqual((kit['inventory'], kit['omitted'], kit['money']), ([{'ware': 'inv_tst', 'amount': 2}], ['reputation'], 7000))
        self.assertEqual((again['snapshot'], again['hulls']), (session['snapshot'], session['hulls']))
        self.assertNotIn('inventory', session['slots']['2']['kit'])            # the given session is not changed
        plan = identity.build_plan(2, 'coop', ships=['00000000000ae79c', '000000000000b000'], occupied_ship='00000000000ae79c', kits=[kit])
        self.assertTrue(identity.canonical(identity.validate(plan)).endswith(':omit=reputation:inv=inv_tst/2'))
        self.assertEqual(session_start.rekit(again, self.catalog, ['inventory']), again)   # again: nothing more to do
        with self.assertRaises(session_start.SessionError) as caught:
            session_start.rekit(session, self.catalog, ['ship'])
        self.assertIn('unknown parts', str(caught.exception))
        # Reputation applies in coop now: a session made with it left out and a kit made with it are different kits.
        with self.assertRaises(session_start.SessionError) as caught:
            session_start.rekit(session, self.catalog, ['reputation'])
        self.assertIn('another kit than the session holds', str(caught.exception))
        other = json.loads(json.dumps(session))
        other['slots']['2']['kit']['money'] = 1
        with self.assertRaises(session_start.SessionError) as caught:
            session_start.rekit(other, self.catalog, ['inventory'])
        self.assertIn('another kit than the session holds', str(caught.exception))
        out = self.root / 'again.json'
        self.assertEqual(session_start.main(['kits', '--libraries', str(self.catalog.libraries), '--scripts', str(self.catalog.scripts), '--session', str(path),
                                             '--out', str(out)]), 0)
        self.assertEqual(json.loads(out.read_text())['slots']['2']['kit']['inventory'], [{'ware': 'inv_tst', 'amount': 2}])

    def test_rebase_and_players_overlay_take_the_session_save_and_its_kits(self):
        path, session = self.write_session()
        sha = session['snapshot']['sha256']
        base = make_base(self.root / 'b', 'a' * 64)
        rebased = session_start.rebase(session_start.session_of(path), base, self.root / 'rebased')
        self.assertIn(f'native_gate_snapshot_sha = "{sha}"', (rebased / 'ui/config.lua.append').read_text())
        args = players.defaults(players.build_parser().parse_args(['overlay', '--session', str(path), '--base', str(rebased),
                                                                  '--out', str(self.root / 'o'), '--probe', str(DEV / 'native_gate_probe.lua'),
                                                                  '--players', '2', '--mode', 'coop', '--source', 'none']))
        out, plan = players.build_overlay(args)
        self.assertEqual([s['ship'] for s in plan['slots']], ['00000000000ae79c', '000000000000b000'])
        self.assertEqual(plan['kits'], [session['slots']['2']['kit']])
        self.assertEqual(json.loads((out / 'session.json').read_text())['snapshot']['sha256'], sha)
        self.assertIn('kits=2:alpha_start:money=7000', identity.canonical(identity.validate(plan)))
        self.assertFalse((out / 'libraries').exists())   # coop: the money is the player's own account
        # The unrebased base boots another save: refused.
        args = players.defaults(players.build_parser().parse_args(['overlay', '--session', str(path), '--base', str(base),
                                                                  '--out', str(self.root / 'o2'), '--probe', str(DEV / 'native_gate_probe.lua'),
                                                                  '--players', '2', '--mode', 'coop', '--source', 'none']))
        with self.assertRaises(ValueError) as caught:
            players.build_overlay(args)
        self.assertIn('rebase it', str(caught.exception))
        changed = dict(session, snapshot=dict(session['snapshot'], sha256='0' * 64))
        with self.assertRaises(session_start.SessionError):
            session_start.rebase(changed, base, self.root / 'r2')

    def test_factions_kits_ship_the_pool_library_with_own_accounts(self):
        # The wallet step sets x4lan_player<k>'s OWN account; a frozen base predates the pool accounts, so the overlay
        # carries this tree's library (16 pool factions, each <account amount="0" />).
        self.spec = session_start.build_spec(self.catalog, 'war_start', 2, 'factions', {2: 'alpha_start'},
                                             {2: 'blueprints,inventory,known,research'})
        path, session = self.write_session()
        rebased = session_start.rebase(session_start.session_of(path), make_base(self.root / 'b', 'a' * 64), self.root / 'rebased')
        args = players.defaults(players.build_parser().parse_args(['overlay', '--session', str(path), '--base', str(rebased),
                                                                  '--out', str(self.root / 'o'), '--probe', str(DEV / 'native_gate_probe.lua'),
                                                                  '--players', '2', '--mode', 'factions', '--source', 'none']))
        out, plan = players.build_overlay(args)
        library = out / 'libraries/factions.xml'
        self.assertEqual(library.read_bytes(), (session_start.EXTENSION / 'libraries/factions.xml').read_bytes())
        pool = [f for f in ET.fromstring(library.read_text(encoding='utf-8')).iter('faction') if f.get('id', '').startswith('x4lan_player')]
        self.assertEqual([[a.get('amount') for a in f.findall('account')] for f in pool], [['0']] * 16)
        self.assertIn('libraries/factions.xml', json.loads((out / 'x4lane-manifest.json').read_text()))
        steps = identity.steps(identity.validate(plan))
        self.assertIn('step=wallet slot=2 wallet=x4lan_player2 op=set money=7000', ' '.join(s['head'] for s in steps))
        self.assertTrue(any(n.startswith('wallet: libraries/factions.xml') for n in players.session_notes(args, plan)))
        self.assertTrue(players.wallet_library(plan))
        self.assertFalse(players.wallet_library(dict(plan, kits=[])))
        self.assertFalse(players.wallet_library(dict(plan, mode='coop')))

    def test_builder_session_defaults_and_refusals(self):
        path, session = self.write_session()

        def args(*extra):
            return players.defaults(players.build_parser().parse_args(['plan', '--session', str(path), *extra]))
        a = args('--players', '2', '--mode', 'coop', '--source', 'stock', '--stock-script', 'demo')
        lines, plan, _ = players.config_block(a)
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_slots = {'00000000000ae79c','000000000000b000'}", lines)
        self.assertEqual(plan['kits'][0]['slot'], 2)
        self.assertNotIn('starts', plan)   # the hull is already at its start in the save
        self.assertIn('--session', players.echo_args(a))
        self.assertTrue(any('session: base start war_start' in n for n in players.session_notes(a, plan)))
        # Two sectors: slot 1's (the save's player sector) and the hull's start sector.
        self.assertEqual(players.slot_sectors(a, a.ships, [], {}), {1: 'cluster_93_sector001_macro', 2: 'cluster_90_sector001_macro'})
        for extra, text in ((('--players', '2', '--mode', 'factions', '--source', 'none'), 'not 2 in factions'),
                            (('--players', '3', '--mode', 'coop', '--source', 'none'), '2 players in coop mode, not 3'),
                            (('--players', '2', '--mode', 'coop', '--source', 'script'), 'needs --source stock or none'),
                            (('--players', '2', '--mode', 'coop', '--source', 'none', '--ships', '00000000000ae79c,0000000000176ad5'), 'session hulls'),
                            (('--players', '2', '--mode', 'coop', '--source', 'none', '--occupied-ship', '0000000000176ad5'), 'occupied ship')):
            with self.subTest(text=text), self.assertRaises(ValueError) as caught:
                args(*extra)
            self.assertIn(text, str(caught.exception))
        out = io.StringIO()
        with contextlib.redirect_stderr(out), self.assertRaises(SystemExit):
            players.main(['plan', '--session', str(path), '--players', '3', '--mode', 'coop'])
        self.assertIn('2 players in coop mode', out.getvalue())


# ---------------------------------------------------------------- identity plan kits

KIT_F = {'slot': 2, 'gamestart': 'alpha_start', 'money': 7000, 'reputation': [{'faction': 'argon', 'value': 3200}, {'faction': 'split', 'value': -32000}],
         'omitted': ['blueprints', 'inventory', 'known', 'research']}
KIT_INVENTORY = [{'ware': 'inv_tst', 'amount': 2}, {'ware': 'weapon_tst_laser', 'amount': 1}]   # a start's wares for the slot's own player
KIT_C = {'slot': 2, 'gamestart': 'alpha_start', 'money': 7000, 'blueprints': ['module_tst_a', 'module_tst_b'], 'research': ['research_tst'],
         'known': {'sectors': ['cluster_90_sector001_macro', 'cluster_97_sector001_macro'], 'faction': 'argon', 'shipyards': True},
         'omitted': ['inventory', 'reputation']}


def lua_core():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(CORE)
    return lua, lua.globals().X4LAN_PLAYER_IDENTITY_CORE


def lua_validate(lua, K, plan):
    result = K.validate(lua.eval(identity.lua_literal(plan)))
    return result if isinstance(result, tuple) else (result, None)


class KitPlanTests(unittest.TestCase):
    def plans(self):
        return [identity.build_plan(2, 'factions', kits=[KIT_F]), identity.build_plan(3, 'factions', kits=[dict(KIT_F, slot=3), dict(KIT_F, slot=2, reputation=[])]),
                identity.build_plan(2, 'coop', kits=[KIT_C]), identity.build_plan(3, 'coop', kits=[KIT_C, dict(KIT_C, slot=3, known={'sectors': ['cluster_01_sector001_macro'], 'shipyards': False})]),
                identity.build_plan(2, 'coop', kits=[{'slot': 2, 'gamestart': 'beta_start', 'money': 0}]),
                identity.build_plan(2, 'coop', kits=[dict(KIT_C, omitted=['reputation'], inventory=KIT_INVENTORY)]),
                identity.build_plan(2, 'factions', kits=[dict(KIT_F, omitted=['blueprints', 'known', 'research'], inventory=KIT_INVENTORY)]),
                identity.build_plan(2, 'coop', kits=[dict(KIT_C, omitted=['inventory'], reputation=KIT_F['reputation'])])]   # coop takes a start's reputation

    def test_lua_and_python_agree_on_kit_text_digest_and_steps(self):
        lua, K = lua_core()
        for plan in self.plans():
            P = identity.validate(plan)
            with self.subTest(plan=identity.canonical(P)[-80:]):
                LP, why = lua_validate(lua, K, plan)
                self.assertIsNone(why)
                self.assertEqual(K.canonical(LP), identity.canonical(P))
                steps = K.steps(LP, identity.digest(P))
                self.assertEqual([steps[i].head for i in range(1, len(steps) + 1)], [s['head'] for s in identity.steps(P)])
        # A kit's inventory is part of the text (and so of the digest); a kit without one keeps its earlier text.
        with_inventory = identity.canonical(identity.validate(self.plans()[5]))
        self.assertTrue(with_inventory.endswith(':omit=reputation:inv=inv_tst/2+weapon_tst_laser/1'), with_inventory[-80:])
        self.assertNotIn(':inv=', identity.canonical(identity.validate(self.plans()[2])))
        self.assertEqual(identity.validate(self.plans()[6])['kits'][0]['inventory'], KIT_INVENTORY)
        # A plan without kits keeps its earlier text and digest.
        self.assertNotIn('kits', identity.canonical(identity.validate(identity.build_plan(2, 'coop'))))

    def test_coop_takes_a_starts_reputation_as_a_raise_of_the_shared_faction(self):
        P = identity.validate(identity.build_plan(2, 'coop', kits=[dict(KIT_C, omitted=['inventory'], reputation=KIT_F['reputation'])]))
        heads = [s['head'] for s in identity.steps(P) if s['step'].startswith('reputation')]
        self.assertEqual(['step=reputation_raise ' in h for h in heads], [True, True], heads)   # never the factions mode's set
        self.assertTrue(heads[0].endswith('faction=player other=argon value=3200') and heads[1].endswith('faction=player other=split value=-32000'), heads)
        self.assertIn(':rep=argon/3200+split/-32000:', identity.canonical(P))
        factions = identity.validate(identity.build_plan(2, 'factions', kits=[KIT_F]))
        self.assertEqual({s['step'] for s in identity.steps(factions) if s['step'].startswith('reputation')}, {'reputation'})
        # the MD half: its own branch, the shared faction, both directions, only upward
        md = (ROOT/'tools/dev/native_gate_player_identity.xml').read_text(encoding='utf-8')
        branch = md[md.index("$a.$step == 'reputation_raise'"):md.index("$a.$step == 'reputation'\"")]
        self.assertIn('exact="faction.player"', branch)
        self.assertEqual(branch.count('<set_faction_relation'), 2)
        self.assertEqual(branch.count('lt $a.$value - 0.000001'), 4)      # each direction guarded, and the readback of both

    def test_kit_steps_follow_the_relations_in_slot_order(self):
        P = identity.validate(identity.build_plan(2, 'factions', kits=[KIT_F]))
        self.assertEqual([s['step'] for s in identity.steps(P)],
                         ['begin', 'faction', 'ship', 'ship', 'relation', 'kit', 'wallet', 'reputation', 'reputation', 'end'])
        heads = [s['head'] for s in identity.steps(P)]
        self.assertIn('tick=20 ordinal=6 step=kit slot=2 gamestart=alpha_start faction=x4lan_player2 omitted=blueprints+inventory+known+research', heads)
        self.assertIn('tick=20 ordinal=7 step=wallet slot=2 wallet=x4lan_player2 op=set money=7000', heads)
        self.assertIn('tick=20 ordinal=9 step=reputation slot=2 faction=x4lan_player2 other=split value=-32000', heads)
        P = identity.validate(identity.build_plan(2, 'coop', kits=[KIT_C]))
        self.assertEqual([s['step'] for s in identity.steps(P)],
                         ['begin', 'ship', 'ship', 'kit', 'wallet', 'blueprint', 'blueprint', 'research', 'known_sector', 'known_sector', 'known', 'end'])
        self.assertIn('step=wallet slot=2 wallet=player op=add money=7000', ' '.join(s['head'] for s in identity.steps(P)))
        self.assertTrue(identity.canonical(P).endswith('|kits=2:alpha_start:money=7000:rep=-:bp=module_tst_a+module_tst_b:rs=research_tst'
                                                       ':known=argon/1/cluster_90_sector001_macro+cluster_97_sector001_macro:omit=inventory+reputation'))

    def test_invalid_kits_refuse_with_the_same_reason_in_lua_and_python(self):
        lua, K = lua_core()
        f, c = identity.build_plan(3, 'factions'), identity.build_plan(2, 'coop')

        def kit(plan, **fields):
            out = json.loads(json.dumps(plan))
            row = dict(KIT_F if plan['mode'] == 'factions' else KIT_C)
            row.update(fields)
            for key in [k for k, v in fields.items() if v is None]:
                del row[key]
            out['kits'] = [row]
            return out

        def two(plan, a, b):
            out = json.loads(json.dumps(plan))
            out['kits'] = [a, b]
            return out
        bad = [(dict(f, kits={'slot': 2}), 'kits'), (kit(f, colour=1), 'key_unknown'), (kit(f, slot=1), 'kit_slot'), (kit(f, slot=4), 'kit_slot'),
               (two(f, KIT_F, KIT_F), 'kit_duplicate'), (kit(identity.build_plan(3, 'factions', occupied_ship='0000000000176ad5'), slot=2), 'kit_player_slot'),
               (kit(f, gamestart='Bad_Start'), 'kit_gamestart'), (kit(f, gamestart=None), 'kit_gamestart'), (kit(f, money=-1), 'kit_money'),
               (kit(f, money=10 ** 12 + 1), 'kit_money'), (kit(f, money=True), 'kit_money'),
               (kit(f, reputation={'faction': 'argon'}), 'kit_reputation'),
               (kit(f, reputation=[{'faction': 'player', 'value': 1}]), 'kit_faction'), (kit(f, reputation=[{'faction': 'x4lan_player3', 'value': 1}]), 'kit_faction'),
               (kit(f, reputation=[{'faction': 'argon', 'value': 1000001}]), 'kit_value'),
               (kit(f, reputation=[{'faction': 'split', 'value': 1}, {'faction': 'argon', 'value': 1}]), 'kit_order'),
               (kit(f, reputation=[{'faction': 'argon', 'value': 1, 'x': 2}]), 'key_unknown'),
               (kit(f, blueprints=['a'], omitted=['inventory']), 'kit_library_mode'), (kit(c, blueprints=['b', 'a']), 'kit_order'),
               (kit(c, research=['A']), 'kit_ware'), (kit(c, blueprints=['w'] * 65), 'kit_wares'),
               (kit(f, known={'sectors': ['cluster_01_sector001_macro'], 'shipyards': True}, omitted=['inventory']), 'kit_known_mode'),
               (kit(c, known={'sectors': [], 'shipyards': True}), 'kit_known'), (kit(c, known={'sectors': ['x'], 'shipyards': True}), 'kit_known'),
               (kit(c, known={'sectors': ['cluster_01_sector001_macro'] * 2, 'shipyards': True}), 'kit_known'),
               (kit(c, known={'sectors': ['cluster_01_sector001_macro'], 'shipyards': 1}), 'kit_known'),
               (kit(c, known={'sectors': ['cluster_01_sector001_macro'], 'shipyards': True, 'faction': 'player'}), 'kit_faction'),
               (kit(c, known={'sectors': ['cluster_01_sector001_macro'], 'shipyards': True, 'pos': 1}), 'key_unknown'),
               (kit(c, omitted=['reputation', 'inventory']), 'kit_omitted'), (kit(c, omitted=['ship']), 'kit_omitted'),
               (kit(c, omitted=['blueprints', 'inventory', 'reputation']), 'kit_omitted_conflict'),
               (kit(c, inventory=KIT_INVENTORY), 'kit_omitted_conflict'),                        # given and named as left out
               (kit(c, omitted=['reputation'], inventory={'ware': 'inv_tst'}), 'kit_inventory'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_tst'}]), 'kit_amount'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_tst', 'amount': 1, 'x': 1}]), 'key_unknown'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'Inv', 'amount': 1}]), 'kit_ware'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_tst', 'amount': 0}]), 'kit_amount'),
               (kit(c, omitted=['reputation'], inventory=[{'amount': 2}]), 'kit_ware'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_tst', 'amount': 1000001}]), 'kit_amount'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_b', 'amount': 1}, {'ware': 'inv_a', 'amount': 1}]), 'kit_order'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_a', 'amount': 1}, {'ware': 'inv_a', 'amount': 1}]), 'kit_order'),
               (kit(c, omitted=['reputation'], inventory=[{'ware': 'inv_%02d' % i, 'amount': 1} for i in range(65)]), 'kit_inventory')]
        for plan, reason in bad:
            with self.subTest(reason=reason):
                with self.assertRaises(identity.Refused) as caught:
                    identity.validate(plan)
                self.assertEqual(str(caught.exception), reason)
                self.assertEqual(lua_validate(lua, K, plan)[1], reason)

    def test_wallet_money_reaches_md_in_two_exact_parts(self):
        # Live l2a: the old step read a pool account back through (money / 1ct)i, a 32-bit integer. The start's whole
        # credits now travel as mega (millions) and units (the rest), each below 2^24 (exact even as a 32-bit float);
        # MD rebuilds the money value as (mega)i * 1000000Cr + (units)i * 1Cr. Lua and Python split alike.
        lua, K = lua_core()
        for money in (0, 1, 7000, 999_999, 1_000_000, 16_777_216, 123_456_789_012, identity.MONEY_MAX - 1, identity.MONEY_MAX):
            for mode, kit in (('factions', KIT_F), ('coop', KIT_C)):
                with self.subTest(money=money, mode=mode):
                    plan = identity.build_plan(2, mode, kits=[dict(kit, money=money)])
                    P = identity.validate(plan)
                    py = [s for s in identity.steps(P) if s['step'] == 'wallet'][0]
                    LP, why = lua_validate(lua, K, plan)
                    self.assertIsNone(why)
                    steps = K.steps(LP, identity.digest(P))
                    lw = [steps[i] for i in range(1, len(steps) + 1) if steps[i].step == 'wallet'][0]
                    self.assertEqual((lw.mega, lw.units), (py['mega'], py['units']))
                    self.assertEqual(py['mega'] * 1_000_000 + py['units'], money)
                    self.assertTrue(0 <= py['mega'] <= 1_000_000 and 0 <= py['units'] < 1_000_000 < 2 ** 24)
                    self.assertIsNone(getattr(lw, 'cents', None))
                    self.assertTrue(py['head'].endswith(f'money={money}'))   # the head (and the plan text) keep whole credits


class KitGlueTests(unittest.TestCase):
    def test_clients_raise_identical_kit_steps_with_md_values_and_compare(self):
        for mode, kit in (('factions', KIT_F), ('coop', KIT_C)):
            with self.subTest(mode=mode):
                plan = identity.build_plan(2, mode, kits=[kit])
                clients = [identity_tests.Client(k, plan, clients=2) for k in (1, 2)]
                states = identity_tests.run(clients, 31)
                self.assertTrue(all(len(s) == 1 for s in states), [s for s in states if len(s) != 1])
                heads = [s['head'] for s in identity.steps(identity.validate(plan))]
                for client in clients:
                    events = list(client.lua.globals().events.values())
                    self.assertEqual([e.head for e in events], heads)
                    wallet = [e for e in events if e.step == 'wallet'][0]
                    self.assertEqual((wallet.faction, wallet.op, wallet.mega, wallet.units),
                                     ('x4lan_player2', 'set', 0, 7000) if mode == 'factions' else ('player', 'add', 0, 7000))
                    if mode == 'factions':
                        self.assertEqual([(e.faction, e.other, e.value) for e in events if e.step == 'reputation'],
                                         [('x4lan_player2', 'argon', 0.0032), ('x4lan_player2', 'split', -0.032)])
                    else:
                        self.assertEqual([e.ware for e in events if e.step in ('blueprint', 'research')], ['module_tst_a', 'module_tst_b', 'research_tst'])
                        self.assertEqual([e.knownsector for e in events if e.step == 'known_sector'],
                                         ['cluster_90_sector001_macro', 'cluster_97_sector001_macro'])
                        known = [e for e in events if e.step == 'known'][0]
                        self.assertEqual((known.count, known.kfaction, known.hasfaction, known.shipyards), (2, 'argon', 1, 1))
                    self.assertEqual([e.gamestart for e in events if e.step == 'kit'], ['alpha_start'])
                    self.assertIn('PLAYER_IDENTITY READY tick=30', client.log())
                ok, details = identity.compare([c.log() for c in clients], plan)
                self.assertTrue(ok, details)

    def test_a_refused_kit_step_stops_the_session(self):
        plan = identity.build_plan(2, 'coop', kits=[KIT_C])
        refused = [s['ordinal'] for s in identity.steps(identity.validate(plan)) if s['step'] == 'known'][0]
        client = identity_tests.Client(1, plan, clients=2)
        with self.assertRaises(LuaError) as caught:
            identity_tests.run([client], 31, refuse=refused)
        self.assertIn(f'refused:{refused}', str(caught.exception))
        # A pool faction without its own account (live l2a) refuses the wallet step loudly: the session stops at the fold.
        lua, K = lua_core()
        plan = identity.build_plan(2, 'factions', kits=[KIT_F])
        LP, _ = lua_validate(lua, K, plan)
        steps = K.steps(LP, identity.digest(identity.validate(plan)))
        rows = [f'{i}|applied|-' for i in range(1, len(steps) + 1)]
        wallet = [i for i in range(1, len(steps) + 1) if steps[i].step == 'wallet'][0]
        rows[wallet - 1] = f'{wallet}|refused_wallet_account|account=none'
        self.assertEqual(K.results(steps, lua.table_from(rows))[1], f'refused:{wallet}:refused_wallet_account')


class KitMdTests(unittest.TestCase):
    def test_md_applies_every_kit_step_with_a_readback(self):
        text = IDENTITY_MD.read_text(encoding='utf-8')
        root = ET.fromstring(text)
        for step in ('kit', 'wallet', 'reputation', 'blueprint', 'research', 'known_sector', 'known'):
            self.assertIn(f"$a.$step == '{step}'", text)
        for result in ('refused_wallet_invalid', 'refused_wallet_account', 'refused_wallet_range', 'refused_wallet_readback',
                       'refused_relation_readback', 'refused_blueprint_readback',
                       'refused_research_readback', 'refused_known_invalid', 'refused_known_readback', 'refused_ware_invalid'):
            self.assertIn(f"'{result}'", text)
        self.assertEqual([r.get('ref') for r in root.iter('run_actions')], ['md.LIB_Generic.UncoverMap_SectorsAndGates'])
        # Factions: money moves through the player's wallet into slot k's account and back out; nothing else pays. Each
        # transfer reads back what moved (its result).
        self.assertEqual([(t.get('from'), t.get('to'), t.get('result')) for t in root.iter('transfer_money')],
                         [('faction.player', '$f', '$moved'), ('$f', 'faction.player', '$moved')])
        self.assertEqual([r.get('money') for r in root.iter('reward_player')], ['$want', '$delta', '-$moved'])

    def test_md_wallet_uses_only_an_own_account_bounded_and_in_money_units(self):
        # Live l2a: x4lan_player2 had no own account, so .money answered X4's dummy account (a large random sum) and the
        # (money / 1ct)i readback printed -1638158336. The step now checks the account kind first and never converts
        # money through the 32-bit (..)i.
        text = IDENTITY_MD.read_text(encoding='utf-8')
        root = ET.fromstring(text)
        step = [e for e in root.iter('do_elseif') if e.get('value') == "$a.$step == 'wallet'"]
        self.assertEqual(len(step), 1)
        block = ET.tostring(step[0], encoding='unicode')
        self.assertNotRegex(text, r'1(ct|Cr)\)i')
        self.assertNotIn('$a.$cents', text)
        self.assertIn('($a.$mega)i * 1000000Cr + ($a.$units)i * 1Cr', block)
        # Order of the branches: invalid, then the account kind (before any read of the slot account), then add / set.
        branches = [e for e in step[0] if e.tag in ('do_if', 'do_elseif', 'do_else')]
        self.assertEqual([(b.tag, b.get('value')) for b in branches[-4:]],
                         [('do_if', branches[-4].get('value')), ('do_elseif', 'not $f.hasownaccount'), ('do_elseif', "$a.$op == 'add'"), ('do_else', None)])
        self.assertIn('$want gt $cap', branches[-4].get('value'))
        self.assertNotIn('$f.money', ET.tostring(branches[-4], encoding='unicode') + branches[-4].get('value'))
        self.assertEqual([s.get('exact') for s in branches[-3].iter('set_value') if s.get('name') == '$result'], ["'refused_wallet_account'"])
        before = block[:block.index('not $f.hasownaccount')]
        self.assertNotIn('$f.money', before)
        self.assertIn('<set_value name="$cap" exact="1000000Cr * 1000000" />', block)   # 1e12 Cr, no literal past 32 bits
        # set: the slot account must be in 0..cap before anything moves; readbacks compare money values.
        set_branch = ET.tostring(branches[-1], encoding='unicode')
        self.assertLess(set_branch.index('$was gt $cap'), set_branch.index('<reward_player'))
        self.assertLess(set_branch.index('$was gt $cap'), set_branch.index('<transfer_money'))
        self.assertIn('$f.money != $want or player.money != $before or $moved != (if $delta lt 0Cr then -$delta else $delta)', set_branch)
        self.assertIn('$f.money != $before + $want', ET.tostring(branches[-2], encoding='unicode'))
        for detail in [s.get('exact') for s in step[0].iter('set_value') if s.get('name') == '$detail']:
            self.assertTrue(detail == "'account=none'" or detail.startswith("'account=own "), detail)
            self.assertNotRegex(detail, r'\)i\b')
        self.assertEqual([a.get('wares') for a in root.iter('add_blueprints')], ['$w'])
        self.assertEqual([a.get('ware') for a in root.iter('add_research')], ['$w'])
        self.assertEqual(text.count('<debug_text '), 1)   # still one APPLIED line per step
        self.assertNotIn('add_inventory', text)            # P_k is not reachable from MD: inventory is never applied here


if __name__ == '__main__':
    unittest.main()
