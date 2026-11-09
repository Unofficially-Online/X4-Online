"""tools/dev/linux_pair_rtti.py and linux_pair_calls.py: the name parsers and the vote rules (synthetic graphs)."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_pair_calls as calls  # noqa: E402
import linux_pair_rtti as rtti  # noqa: E402
import linux_pair_traces as traces  # noqa: E402


class Names(unittest.TestCase):
    def test_msvc_and_itanium_class_names_agree(self):
        self.assertEqual(rtti.msvc_name('.?AVPlayerController@U@@'), 'U::PlayerController')
        self.assertEqual(rtti.itanium_name('N1U16PlayerControllerE'), 'U::PlayerController')
        self.assertEqual(rtti.msvc_name('.?AVTIntermBinary@glslang@@'), rtti.itanium_name('N7glslang13TIntermBinaryE'))
        self.assertEqual(rtti.itanium_name('4Ship'), 'Ship')
        self.assertIsNone(rtti.msvc_name('.?AV?$vector@H@std@@'))   # templates are left out
        self.assertIsNone(rtti.itanium_name('N1U16PlayerControllerXE'))


class Votes(unittest.TestCase):
    def test_one_gap_needs_two_parents_and_signatures_pair(self):
        # Windows: P1 and P2 call A, X, B; Q calls A, B, C. Linux: the same with a, x, b / a, b, c.
        wg = {1: ([10, 20, 30], [], []), 2: ([10, 20, 30], [], []), 9: ([10, 30, 40], [], []), 10: ([], [], []), 20: ([], [], []),
              30: ([], [], []), 40: ([], [], []), 50: ([10, 30, 40], [], [])}
        lg = {101: ([110, 120, 130], [], []), 102: ([110, 120, 130], [], []), 109: ([110, 130, 140], [], []), 110: ([], [], []),
              120: ([], [], []), 130: ([], [], []), 140: ([], [], [])}
        pairs = {1: 101, 2: 102, 10: 110, 30: 130, 40: 140}
        rev = {v: k for k, v in pairs.items()}
        why = {k: 'anchor' for k in pairs}
        calls.propagate(wg, lg, pairs, rev, why)
        self.assertEqual(pairs.get(20), 120)            # two parents agree
        self.assertEqual(why[20], 'call gaps (2 parents)')
        self.assertNotIn(9, pairs)                       # 9 and 50 share a signature on Windows: not unique
        pairs2 = {1: 101, 10: 110, 30: 130}
        rev2 = {v: k for k, v in pairs2.items()}
        calls.propagate(wg, lg, pairs2, rev2, {k: 'anchor' for k in pairs2})
        self.assertNotIn(20, pairs2)                     # one parent only: not enough


class Traces(unittest.TestCase):
    def test_rows_results_and_event_buckets(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'c1.log'
            log.write_text('\n'.join(['[X4LAN] NATIVE_GATE COVERAGE_ROW index=0 address=d86c80 tick=361',
                                      '[X4LAN] NATIVE_GATE COVERAGE_ROW index=1 address=d95180 tick=2', 'noise',
                                      '[X4LAN] NATIVE_GATE COVERAGE_ROW index=2 address=d9c250 tick=1041']) + '\n')
            res = Path(tmp) / 'r.txt'
            res.write_text('\n'.join(['# header', '166c6f0 361 - input:accelerate', '166d020 300 300 both',
                                      '1d820b0 1041 - input:map']) + '\n')
            w, l = traces.win_first(str(log)), traces.lin_first(str(res))
        self.assertEqual(w, {0xd86c80: 361, 0xd95180: 2, 0xd9c250: 1041})
        self.assertEqual(l, {0x166c6f0: 361, 0x166d020: 300, 0x1d820b0: 1041})
        wb, lb = traces.bucket(w), traces.bucket(l)
        self.assertEqual((wb[360], wb[1040], wb['frame']), ([0xd86c80], [0xd9c250], [0xd95180]))
        self.assertEqual((lb[360], lb[1040], lb['frame']), ([0x166c6f0], [0x1d820b0], [0x166d020]))


if __name__ == '__main__':
    unittest.main()
