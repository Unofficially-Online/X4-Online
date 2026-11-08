"""x4ls starts / new-session, offline: the game data unpacked from catalog pairs (gamedata.py) gives the start catalog the
same rows as the extracted layout, a later catalog and an extension's subst catalog win, unsafe paths are refused, the
cache is reused while the catalogs are unchanged; new-session refuses an unusable start before anything is captured."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tools/dev'))
from lockstep.launcher import gamedata, newsession  # noqa: E402
import native_gate_gamestarts as gamestarts  # noqa: E402

spec = importlib.util.spec_from_file_location('session_start_tests', ROOT / 'tests/test_native_gate_session_start.py')
stock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stock)


def pack(cat, files):
    """A catalog pair: the index lines (path size time md5) and the bytes in that order."""
    cat = Path(cat)
    cat.parent.mkdir(parents=True, exist_ok=True)
    with open(cat, 'w', encoding='utf-8', newline='\n') as index, open(cat.with_suffix('.dat'), 'wb') as dat:
        for path, text in files:
            data = text.encode('utf-8')
            index.write(f'{path} {len(data)} 1700000000 {"0" * 32}\n')
            dat.write(data)


def fake_game(root):
    """The synthetic stock data of the session start tests, packed the way X4 ships it."""
    root = Path(root)
    pack(root / '01.cat', [('libraries/gamestarts.xml', stock.GAMESTARTS), ('ui/addons/x.lua', 'return 1'),
                           ('md/gs_alpha.xml', '<mdscript name="Stale"/>')])
    pack(root / '02.cat', [('libraries/god.xml', stock.GOD), ('md/gs_alpha.xml', stock.GS_ALPHA), ('md/gs_station.xml', stock.GS_STATION)])
    pack(root / '02_sig.cat', [('libraries/god.xml', 'signature, not data')])
    pack(root / 'extensions/ego_dlc_split/ext_01.cat', [('libraries/gamestarts.xml', stock.DLC_DIFF), ('md/other.xml', stock.OTHER)])
    pack(root / 'extensions/not_stock/ext_01.cat', [('libraries/gamestarts.xml', '<diff><remove sel="/gamestarts"/></diff>')])
    return root


class GameDataTests(unittest.TestCase):
    def test_unpacked_catalog_matches_the_extracted_layout(self):
        with tempfile.TemporaryDirectory() as d:
            game, cache = fake_game(Path(d) / 'game'), Path(d) / 'cache'
            lines = []
            self.assertEqual(gamedata.unpack(game, cache, lines.append), cache)
            self.assertEqual(len(lines), 1)
            self.assertEqual((cache / 'base/md/gs_alpha.xml').read_text(encoding='utf-8'), stock.GS_ALPHA)   # the later catalog wins
            self.assertFalse((cache / 'base/ui').exists())                     # only libraries/ and md/
            self.assertFalse((cache / 'not_stock').exists())                   # only stock extensions
            self.assertEqual((cache / 'base/libraries/god.xml').read_text(encoding='utf-8'), stock.GOD)   # no _sig catalog
            unpacked = gamestarts.Catalog(cache, cache).rows()
            expected = stock.catalog_in(Path(d) / 'layout').rows()
            self.assertEqual([(r['id'], r['class'], bool(r['base']), bool(r['per_player'])) for r in unpacked],
                             [(r['id'], r['class'], bool(r['base']), bool(r['per_player'])) for r in expected])
            # Unchanged catalogs: the cache is used as it is.
            (cache / 'base/md/marker.xml').write_text('kept')
            gamedata.unpack(game, cache, lines.append)
            self.assertTrue((cache / 'base/md/marker.xml').exists())
            self.assertEqual(len(lines), 1)
            # An extension's subst catalog replaces a base file; a changed catalog unpacks again.
            pack(game / 'extensions/ego_dlc_split/subst_01.cat', [('md/gs_station.xml', '<mdscript name="Subst"/>')])
            gamedata.unpack(game, cache, lines.append)
            self.assertFalse((cache / 'base/md/marker.xml').exists())
            self.assertEqual((cache / 'base/md/gs_station.xml').read_text(encoding='utf-8'), '<mdscript name="Subst"/>')

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(SystemExit, 'no X4 catalogs'):
                gamedata.unpack(Path(d), Path(d) / 'cache', lambda *_: None)
            pack(Path(d) / '01.cat', [('md/../../evil.xml', '<x/>')])
            with self.assertRaisesRegex(ValueError, 'unsafe catalog path'):
                gamedata.unpack(Path(d), Path(d) / 'cache', lambda *_: None)
            (Path(d) / '01.cat').write_text('not a catalog line\n')
            with self.assertRaisesRegex(ValueError, 'not a catalog line'):
                gamedata.unpack(Path(d), Path(d) / 'cache2', lambda *_: None)


class NewSessionTests(unittest.TestCase):
    def test_starts_and_refusals_before_any_capture(self):
        with tempfile.TemporaryDirectory() as d:
            game, cache = fake_game(Path(d) / 'game'), Path(d) / 'cache'
            lines = []
            rows = newsession.starts(game, cache, log=lines.append)
            self.assertIn('war_start', [r['id'] for r in rows])
            self.assertTrue(any(l.startswith('war_start') and 'session-base=yes' in l for l in lines))
            self.assertTrue(any(l.startswith('beta_start') and 'player-start=yes' in l for l in lines))
            with mock.patch('lockstep.launcher.session.capture_snapshot') as capture:
                for kwargs, text in ((dict(base_start='station_start'), 'session base refused'),
                                     (dict(base_start='war_start', starts={2: 'station_start'}), 'class b'),
                                     (dict(base_start='war_start', players=9), 'players')):
                    o = newsession.NewSessionOptions(**{'players': 2, 'mode': 'coop', 'out': Path(d) / 'out', 'gate_dll': Path('x.dll'),
                                                        'game': game, 'cache': cache, **kwargs})
                    with self.subTest(text=text), self.assertRaisesRegex(SystemExit, text):
                        newsession.new_session(o, log=lambda *_: None)
                    self.assertFalse((Path(d) / 'out').exists())
                capture.assert_not_called()
                # A good spec writes spec.json and the capture overlay, then captures; a failed capture registers nothing.
                capture.return_value = 'failed'
                o = newsession.NewSessionOptions(base_start='war_start', players=2, mode='coop', out=Path(d) / 'out',
                                                 gate_dll=Path('x.dll'), starts={2: 'beta_start'}, game=game, cache=cache)
                with self.assertRaisesRegex(SystemExit, 'ended failed; nothing registered'):
                    newsession.new_session(o, log=lambda *_: None)
                self.assertTrue((Path(d) / 'out/spec.json').is_file())
                self.assertTrue((Path(d) / 'out/capture-overlay/md/x4lan_session_hulls.xml').is_file())
                self.assertFalse((Path(d) / 'out/session.json').exists())
                self.assertEqual(capture.call_args.kwargs['overlay'], Path(d) / 'out/capture-overlay')


if __name__ == '__main__':
    unittest.main()
