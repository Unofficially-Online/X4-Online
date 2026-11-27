"""The starts a hosted game may have (src/lockstep/launcher/hoststarts.py, the Host form's Start): which starts of a
catalog can be hosted (the session base and every slot's own start in the ship-only form) and which give every player
its own story, the names from the game's own text (gamedata.texts, read straight from the catalogs), the table loaded in
its own thread, netkit.start_story. Synthetic catalogs; the stock list of X4 9.00 with X4LAN_GAMEDATA (the unpacked
game data of this PC, read only). No X4 runs here."""
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tools/dev'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import gamedata, hoststarts, netkit  # noqa: E402
import native_gate_gamestarts as gamestarts  # noqa: E402
import netplay_fakes as fakes  # noqa: E402
import test_native_gate_session_start as stock  # noqa: E402
from test_launcher_newsession import pack  # noqa: E402

GAMEDATA = os.environ.get('X4LAN_GAMEDATA')
TEXT = '''<?xml version="1.0" encoding="utf-8"?>
<language id="44">
 <page id="1021" title="Gamestarts">
  <t id="400">The Untested Explorer</t>
  <t id="600">{20005, 5009}(Fires of Defeat)</t>
  <t id="900">Terran Cadet</t>
  <t id="901">\\(Not a note\\) kept (a writer's note)</t>
 </page>
 <page id="20005"><t id="5009">Fires of Defeat</t></page>
</language>
'''


class TextTests(unittest.TestCase):
    def test_the_games_text_from_its_catalogs(self):
        with tempfile.TemporaryDirectory() as d:
            game = Path(d)
            pack(game / '01.cat', [('libraries/gamestarts.xml', '<gamestarts/>'), ('t/0001-l044.xml', '<language/>')])
            pack(game / '09.cat', [('t/0001-l049.xml', '<language id="49"/>'), ('t/0001-l044.xml', TEXT)])
            self.assertEqual(gamedata.read_file(game, 'T\\0001-L044.xml'), TEXT.encode())   # the later catalog wins
            self.assertIsNone(gamedata.read_file(game, 't/0001-l007.xml'))
            names = gamedata.texts(game, [(1021, 400), (1021, 600), (1021, 901), (1021, 999)])
            self.assertEqual(names, {(1021, 400): 'The Untested Explorer', (1021, 600): 'Fires of Defeat',
                                     (1021, 901): '(Not a note) kept'})   # notes out, references resolved, no line 999
            self.assertEqual(gamedata.texts(game, [(1021, 400)], language=7), {})   # no such language file
        table = {(1, 1): '{1,2}', (1, 2): '{1,1}'}
        self.assertEqual(gamedata.resolve_text(table, 1, 1), '')   # a loop ends at the depth limit

    def test_names_and_ids(self):
        self.assertEqual(hoststarts.name_ref('{1021,900}'), (1021, 900))
        self.assertEqual(hoststarts.name_ref(' { 1021 , 900 } '), (1021, 900))
        self.assertIsNone(hoststarts.name_ref('Terran Cadet'))
        self.assertEqual(hoststarts.short('x4ep1_gamestart_terran1'), 'terran1')
        self.assertEqual(hoststarts.short('custom_creative'), 'custom_creative')
        rows = [{'id': 'x4ep1_gamestart_terran1'}, {'id': 'beta_start'}]
        self.assertEqual([hoststarts.full(name, rows) for name in ('terran1', 'X4EP1_GAMESTART_TERRAN1', 'beta_start', 'fight')],
                         ['x4ep1_gamestart_terran1', 'x4ep1_gamestart_terran1', 'beta_start', None])
        self.assertEqual(hoststarts.find(rows, 'beta_start'), {'id': 'beta_start'})
        self.assertIsNone(hoststarts.find(None, 'beta_start'))


class TableTests(unittest.TestCase):
    def test_a_start_is_hosted_when_it_can_be_the_base_and_every_slots_own(self):
        with tempfile.TemporaryDirectory() as d:
            cat = stock.catalog_in(d)
            cat.starts['beta_start'].set('name', '{1021,77}')
            rows, refused = hoststarts.table(cat, {(1021, 77): 'Beta Start'})
            self.assertEqual([row['id'] for row in rows], ['alpha_start', 'beta_start', 'war_start', 'gamma_start'])
            self.assertEqual(rows[1], {'id': 'beta_start', 'label': 'Beta Start', 'story': False, 'page': 1021, 'line': 77})
            self.assertEqual(rows[0], {'id': 'alpha_start', 'label': 'alpha_start', 'story': False})   # no name: its id
            self.assertEqual(sorted(refused), ['station_start', 'suit_start'])   # on foot; a spacesuit (the tutorial: out)
            self.assertIn('on_foot', refused['station_start'])
            # factions mode: a start whose map knowledge cannot be applied per player faction is not offered
            rows, refused = hoststarts.table(cat, {}, mode='factions')
            self.assertNotIn('alpha_start', [row['id'] for row in rows])
            self.assertIn('known', refused['alpha_start'])

    @unittest.skipUnless(GAMEDATA, 'needs X4LAN_GAMEDATA (the unpacked game data of X4 9.00, read only)')
    def test_the_stock_starts_that_can_be_hosted(self):
        cat = gamestarts.Catalog(GAMEDATA, GAMEDATA)
        rows, refused = hoststarts.table(cat, {(1021, 400): 'The Untested Explorer', (1021, 900): 'Terran Cadet'})
        self.assertEqual([(row['id'], row['story']) for row in rows],
                         [('x4ep1_gamestart_discover', False), ('x4ep1_gamestart_terran1', True),
                          ('x4ep1_gamestart_split2', True), ('x4ep1_gamestart_fight', False)])
        self.assertEqual((rows[0]['label'], rows[0]['page'], rows[0]['line']), ('The Untested Explorer', 1021, 400))
        self.assertEqual((rows[1]['label'], rows[3]['label']), ('Terran Cadet', 'fight'))
        # the others, and why: the session's capture needs the player in a ship in open space, and so do the slots
        for start, why in (('x4ep1_gamestart_trade', 'on_foot'), ('x4ep1_gamestart_intro', 'docked'),
                           ('x4ep1_gamestart_terran2', 'on_foot'), ('x4ep1_gamestart_split1', 'spacesuit'),
                           ('x4ep1_gamestart_boron2', 'on_foot'), ('x4ep1_gamestart_hyperion', 'does not know'),
                           ('custom_creative', 'location without a zone')):
            self.assertIn(why, refused[start], start)
        self.assertNotIn('x4ep1_gamestart_tutorial1', refused)   # tutorials are not starts at all here


class StartsTests(unittest.TestCase):
    def test_the_table_is_read_in_its_own_thread(self):
        release = threading.Event()
        seen = []

        def loader():
            release.wait(5)
            return fakes.STARTS
        starts = hoststarts.Starts(loader, log=lambda text: None).start(seen.append)
        self.assertIsNone(starts.wait(0.05))   # still reading: the page draws without a Start meanwhile
        release.set()
        self.assertEqual([row['id'] for row in starts.wait(5)], ['x4ep1_gamestart_discover', 'x4ep1_gamestart_terran1'])
        starts.thread.join(5)
        self.assertEqual(seen, [fakes.STARTS[0]])
        self.assertIn('x4ep1_gamestart_trade', starts.refused)

    def test_no_game_data_no_table(self):
        logs = []

        def fails():
            raise SystemExit('C:/X4: no X4 catalogs (NN.cat with NN.dat)')
        starts = hoststarts.Starts(fails, log=logs.append).start(lambda rows: self.fail('no rows'))
        self.assertIsNone(starts.wait(5))
        self.assertIn('no X4 catalogs', starts.error)
        self.assertTrue(any('host starts: not read' in line for line in logs))
        self.assertIsNone(hoststarts.Starts(lambda: None, log=lambda text: None).start().wait(5))   # a backend without one

    def test_start_story(self):
        def load(game, cache, log=print):
            return fakes.STARTS
        self.assertTrue(netkit.start_story('x4ep1_gamestart_terran1', load=load))
        self.assertFalse(netkit.start_story('x4ep1_gamestart_discover', load=load))
        with self.assertRaises(netkit.KitError) as refused:
            netkit.start_story('x4ep1_gamestart_trade', load=load)
        self.assertIn('on_foot', str(refused.exception))
        with self.assertRaises(netkit.KitError) as refused:
            netkit.start_story('x4ep1_gamestart_boron1', load=load)
        self.assertIn('no such start', str(refused.exception))


if __name__ == '__main__':
    unittest.main()
