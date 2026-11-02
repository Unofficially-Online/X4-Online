import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import triage  # noqa: E402


class Triage(unittest.TestCase):
    def test_window_covers_the_ticks_after_the_last_equal_capture(self):
        self.assertEqual(triage.window(35700), (35401, 35700))
        self.assertEqual(triage.window(150), (1, 150))
        sets = triage.diagnostic_sets(35700)
        self.assertIn('native_gate_trace_window={35401,35700}', sets)
        self.assertIn('native_gate_world_pages_window={35400,35700}', sets)

    def test_rebuild_keeps_the_recorded_arguments_and_replaces_out(self):
        with tempfile.TemporaryDirectory() as tmp:
            overlay = Path(tmp) / 'soak'
            Path(f'{overlay}.build.json').write_text(json.dumps({'argv': ['--snapshot', 'S', '--play-mode']}))
            args = triage.rebuild_args(overlay, Path(tmp) / 'triage-x', 600)
            self.assertEqual(args[:3], ['--snapshot', 'S', '--play-mode'])
            self.assertEqual(args[-2:], ['--out', str(Path(tmp) / 'triage-x')])
            self.assertIn('native_gate_script_rng_rows_window={301,600}', args)
            with self.assertRaises(SystemExit):
                triage.rebuild_args(Path(tmp) / 'unrecorded', Path(tmp) / 'o', 600)

    def test_rows_drop_process_local_fields_and_compare_as_multisets(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / 'a.log', Path(tmp) / 'b.log'
            a.write_text('[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=4 tick=3 owner=1 caller=aa name=x\n'
                         '[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=5 tick=3 owner=2 caller=bb name=y\n'
                         '[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=6 tick=4 owner=1 caller=cc name=x\n')
            b.write_text('[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=9 tick=3 owner=2 caller=dd name=y\n'
                         '[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=8 tick=3 owner=1 caller=ee name=x\n'
                         '[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=7 tick=4 owner=3 caller=ff name=z\n')
            fields = ('index', 'caller')
            ra, rb = triage.rows(a, 'SCRIPT_RNG_ROW', fields), triage.rows(b, 'SCRIPT_RNG_ROW', fields)
            self.assertEqual(ra[3], rb[3])  # order and local fields do not count
            self.assertEqual(triage.first_row_difference(ra, rb), (4, ['tick=4 owner=1 name=x'], ['tick=4 owner=3 name=z']))
            self.assertIsNone(triage.first_row_difference(ra, ra))

    def test_known_local_status_differences_are_not_leads(self):
        self.assertEqual(triage.classify("1 PATH_DETERMINISM [('bank=1', 'bank=0')]"), 'local')
        self.assertEqual(triage.classify("17 WEAPON_FIRE_RNG [('keyed=0', 'keyed=2')]"), 'lead')
        self.assertEqual(triage.classify("1 SCRIPT_RNG [('ai_hits=1', 'ai_hits=2'), ('drawn=3', 'drawn=4')]"), 'lead')


if __name__ == '__main__':
    unittest.main()
