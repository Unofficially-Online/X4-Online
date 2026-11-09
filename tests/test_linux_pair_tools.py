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


def vt(win, lin, wv=0x2000000, lv=0x3a00000, offset=0):
    return {'offset': offset, 'win_vtable': '%x' % wv, 'lin_vtable': '%x' % lv,
            'win_slots': ['%x' % a for a in win], 'lin_slots': ['%x' % a for a in lin]}


class DestructorSlots(unittest.TestCase):
    def test_interface_destructor_last_primary_first_and_folded_siblings(self):
        import linux_pair_data as data
        # an interface at offset 48 (Engine's): slot 1 is each class's own, slot 2 shared, slot 3 the destructor (GCC:
        # two); the shared slot between them decides it (with the own slot next to the destructor both would fit)
        iface = {f'C{k}': [vt([0x10, 0x20 + k, 0x11, 0x30 + k], [0x110, 0x120 + k, 0x111, 0x130 + k, 0x140 + k],
                              0x2100000 + 0x100 * k, 0x3b00000 + 0x100 * k, offset=48)] for k in range(3)}
        # primary vtables: the destructor first; C0 and C1 share a folded Windows override in slot 2 (MSVC's ICF)
        prim = {'P0': [vt([0x50, 0x60, 0x70], [0x150, 0x151, 0x160, 0x170])],
                'P1': [vt([0x51, 0x60, 0x70], [0x152, 0x153, 0x160, 0x171])],
                'P2': [vt([0x52, 0x60, 0x72], [0x154, 0x155, 0x160, 0x172])]}
        d = data.dtor_slots({**iface, **prim})
        self.assertEqual(d[('C0', 48)], (3, 3))
        self.assertEqual(d[('P0', 0)], (0, 0))
        self.assertEqual([data.lin_slot(i, (3, 3)) for i in range(4)], [0, 1, 2, 3])
        self.assertEqual([data.lin_slot(i, (0, 0)) for i in range(3)], [0, 2, 3])
        self.assertEqual([data.lin_slot(i, (1, 3)) for i in range(5)], [0, 1, None, None, 5])
        # an address in slot 2 of the interface stays slot 2 (the old +1 rule gave slot 3, the destructor)
        placed = data.place_vtables(iface, [0x2100000 + 16])
        self.assertEqual(placed[0x2100000 + 16]['placed'], 0x3b00000 + 16)

    def test_slot_counts_far_apart_place_only_the_vtable_itself(self):
        import linux_pair_data as data
        rt = {'Big': [vt(list(range(0x100, 0x110)), list(range(0x200, 0x218)))]}   # 16 Windows, 24 Linux slots
        placed = data.place_vtables(rt, [0x2000000, 0x2000000 + 8 * 9])
        self.assertEqual(placed[0x2000000]['placed'], 0x3a00000)
        self.assertNotIn(0x2000000 + 8 * 9, placed)


class BlockKey(unittest.TestCase):
    def test_mapped_block_fields_place_with_a_clear_lead_only(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev/linux_people'))
        import block_key
        delta = 0xE42280
        base = 0x2f4b720
        blocks = [(base, base + 0x100, delta, 8)]
        g = [base + 0x10 * i for i in range(6)]
        wg = {0x100000: ([], [], [(g[0], 'mov'), (g[1], 'mov'), (g[2], 'movss')]),   # the open function
              0x100200: ([], [], [(g[3], 'mov'), (g[4], 'mov')]),
              0x100400: ([], [], [(g[5], 'mov')])}
        lg = {0x1600000: ([], [], [(g[0] + delta, 'mov'), (g[1] + delta, 'mov'), (g[2] + delta, 'movss')]),
              0x1600200: ([], [], [(g[0] + delta, 'mov'), (g[3] + delta, 'mov')]),
              0x1600400: ([], [], [(g[3] + delta, 'mov'), (g[4] + delta, 'mov')]),
              0x1600500: ([], [], [(g[3] + delta, 'mov'), (g[4] + delta, 'mov')]),
              0x1600600: ([], [], [])}
        key = block_key.Key(wg, lg, {}, blocks)
        got, mapped, rows = key.place(0x100000)
        self.assertEqual((got, len(mapped)), (0x1600000, 3))
        self.assertEqual(rows[0][:2], (1.0, 3))
        # two Linux functions with the same fields: no lead, no placement
        self.assertIsNone(key.place(0x100200)[0])
        # one field is not enough
        self.assertIsNone(key.place(0x100400)[0])


if __name__ == '__main__':
    unittest.main()
