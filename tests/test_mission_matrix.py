"""tools/dev/mission_matrix.py: the plans it writes, the rows it reads, the verdicts (no game involved)."""
import contextlib
import importlib.util
import io
import re
import tempfile
import unittest
import xml.dom.minidom
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mission_matrix', ROOT / 'tools/dev/mission_matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


def row(name, slot, value):
    return "[Scripts] 12.5 *** Context:md.X4LAN_UiReplay.MtEnd: [X4LAN] MT_RESULT type=%s slot=%d player=0x1 value=%s took=5 time=12.5 text=null\n" % (name, slot, value)


class Plans(unittest.TestCase):
    def test_every_client_holds_every_offer_and_only_its_own_accepts(self):
        plans, end = matrix.plans(['flyto', 'collect'], 2)
        offers = [e for e in plans[0] if e[1] == 'mt_offer']
        self.assertEqual(offers, [e for e in plans[1] if e[1] == 'mt_offer'])       # the probe's own call: the same tick everywhere
        self.assertEqual([(e[2], e[3]) for e in offers], [('flyto', 1), ('flyto', 2), ('collect', 1), ('collect', 2)])
        self.assertEqual([e for e in plans[0] if e[1] == 'mt_accept'], [(120, 'mt_accept', 'flyto', 1), (1320, 'mt_accept', 'collect', 1)])
        self.assertEqual([e for e in plans[1] if e[1] == 'mt_accept'], [(720, 'mt_accept', 'flyto', 2), (1920, 'mt_accept', 'collect', 2)])
        self.assertEqual(end, matrix.START + 4 * matrix.SPACING + matrix.SPACING)
        # one test at a time: the tests of two players never overlap (a shared standing would answer for both)
        ticks = [e[0] for e in offers]
        self.assertTrue(all(b - a == matrix.SPACING for a, b in zip(ticks, ticks[1:])))

    def test_the_script_is_a_lua_table_per_client(self):
        text, _ = matrix.script(['target'], 3)
        self.assertEqual(text, "{{{60,'mt_offer','target',1},{120,'mt_accept','target',1},{660,'mt_offer','target',2},{1260,'mt_offer','target',3}},"
                               "{{60,'mt_offer','target',1},{660,'mt_offer','target',2},{720,'mt_accept','target',2},{1260,'mt_offer','target',3}},"
                               "{{60,'mt_offer','target',1},{660,'mt_offer','target',2},{1260,'mt_offer','target',3},{1320,'mt_accept','target',3}}}")

    def test_a_type_with_ops_of_its_own(self):
        plans, _ = matrix.plans(['deploy'], 2)
        # the stock for the ship is the probe's own call (every client), the launch is the player's own
        self.assertIn((180, 'launch_stock', 1, 'eq_arg_satellite_01_macro', 1), plans[0])
        self.assertIn((180, 'launch_stock', 1, 'eq_arg_satellite_01_macro', 1), plans[1])
        self.assertIn((330, 'launch', 'satellite', 'eq_arg_satellite_01_macro'), plans[0])
        self.assertNotIn((330, 'launch', 'satellite', 'eq_arg_satellite_01_macro'), plans[1])
        self.assertIn((930, 'launch', 'satellite', 'eq_arg_satellite_01_macro'), plans[1])
        self.assertIn((780, 'launch_stock', 2, 'eq_arg_satellite_01_macro', 1), plans[0])

    def test_every_type_has_its_md_cue_and_the_ops_exist(self):
        md = xml.dom.minidom.parse(str(ROOT / 'tools/dev/native_gate_ui_replay.xml'))
        checks = {c.getAttribute('value') for c in md.getElementsByTagName('check_value')}
        refs = {c.getAttribute('ref') for c in md.getElementsByTagName('cue') if c.getAttribute('ref')}
        for name, (library, _, _) in matrix.TYPES.items():
            self.assertIn("$Type == '%s'" % name, checks)
            self.assertIn('md.' + library, refs)
        core = (ROOT / 'tools/dev/native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
        self.assertIn("op=='mt_offer'", core)
        self.assertIn("op=='mt_accept'", core)
        self.assertTrue(re.search(r"K\.CANONICAL=\{[^}]*mt_offer=true", core))      # the offer is the probe's own call


class Table(unittest.TestCase):
    def verdicts(self, client1, client2, types=('flyto', 'destroy'), run=None):
        with tempfile.TemporaryDirectory() as folder:
            logs = []
            for i, rows in enumerate((client1, client2)):
                path = Path(folder) / ('c%d.log' % i)
                path.write_text(''.join(rows), encoding='latin-1')
                logs.append(path)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                failed = matrix.table(logs, list(types), 2, run)
            return failed, out.getvalue()

    def test_player_two_must_match_player_one(self):
        good = [row('flyto', 1, '1'), row('flyto', 2, '1'), row('destroy', 1, '-1'), row('destroy', 2, '-1')]
        failed, text = self.verdicts(good, good)
        self.assertEqual(failed, 0, text)
        self.assertIn('2 of 2 tests passed', text)
        bad = [row('flyto', 1, '1'), row('flyto', 2, 'timeout'), row('destroy', 1, '-1'), row('destroy', 2, '-1')]
        failed, text = self.verdicts(bad, bad)
        self.assertEqual(failed, 1, text)
        self.assertIn('FAIL: player 2', text)

    def test_a_wrong_reference_blames_the_test(self):
        rows = [row('flyto', 1, 'timeout'), row('flyto', 2, 'timeout'), row('destroy', 1, '-1'), row('destroy', 2, '-1')]
        failed, text = self.verdicts(rows, rows)
        self.assertEqual(failed, 1, text)
        self.assertIn('REFERENCE FAILED', text)

    def test_a_missing_row_and_clients_that_differ_fail(self):
        one = [row('flyto', 1, '1'), row('flyto', 2, '1'), row('destroy', 1, '-1')]
        failed, text = self.verdicts(one, one)
        self.assertEqual(failed, 1, text)
        self.assertIn('no row', text)
        other = [row('flyto', 1, '1'), row('flyto', 2, '1'), row('destroy', 1, '-1'), row('destroy', 2, '-1')]
        failed, text = self.verdicts(other, one)
        self.assertGreaterEqual(failed, 1, text)
        self.assertIn('different rows', text)

    def test_the_first_row_of_a_test_counts(self):
        rows = [row('flyto', 1, '1'), row('flyto', 1, 'timeout'), row('flyto', 2, '1'), row('destroy', 1, '-1'), row('destroy', 2, '-1')]
        self.assertEqual(self.verdicts(rows, rows)[0], 0)

    def test_a_split_or_an_error_row_of_the_run_fails(self):
        rows = [row('flyto', 1, '1'), row('flyto', 2, '1'), row('destroy', 1, '-1'), row('destroy', 2, '-1')]
        with tempfile.TemporaryDirectory() as run:
            (Path(run) / 'desyncs.log').write_text('16:00:00 NATIVE_GATE STALL tick=5\n', encoding='utf-8')
            self.assertEqual(self.verdicts(rows, rows, run=run)[0], 0)             # a stall is no split
            (Path(run) / 'desyncs.log').write_text('16:00:00 NATIVE_GATE DESYNC tick=5 slots=2 first=5 mismatches=1\n', encoding='utf-8')
            failed, text = self.verdicts(rows, rows, run=run)
            self.assertEqual(failed, 1, text)
            self.assertIn('the clients split', text)
            (Path(run) / 'desyncs.log').write_text('', encoding='utf-8')
            (Path(run) / 'errors.log').write_text('client1 [=ERROR=] something\n', encoding='utf-8')
            self.assertEqual(self.verdicts(rows, rows, run=run)[0], 1)


if __name__ == '__main__':
    unittest.main()
