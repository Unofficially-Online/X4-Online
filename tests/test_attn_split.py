"""attn_split.py (the per-player attention measurement driver) and attn_split_census.py on synthetic inputs; no X4."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import attn_split as a        # noqa: E402
import attn_split_census as ac  # noqa: E402


def row(kind, text, stamp='1.00'):
    return '[=ERROR=] %s [X4LAN] NATIVE_GATE %s %s' % (stamp, kind, text)


class PlanTest(unittest.TestCase):
    def test_slot2_is_moved_first_and_both_turn_and_fire(self):
        end = 10800
        p1, p2 = a.plan(1, end), a.plan(2, end)
        self.assertEqual(p2[0], [60, 'goto_obj', a.TARGET, a.SPOT])
        self.assertFalse(any(e[1] == 'goto_obj' for e in p1))
        # slot 1 throttles, slot 2 does not (it would fly into the station 12 km ahead)
        self.assertTrue(any(e[1:4] == ['input', 'state', a.ACCEL] for e in p1))
        self.assertFalse(any(e[1:4] == ['input', 'state', a.ACCEL] for e in p2))
        for p in (p1, p2):
            self.assertTrue(any(e[1] == 'aim_fire' and e[2] == 1 for e in p))
            self.assertIn([90, 'every', 300, end, [['place']]], p)
            self.assertTrue(all(e[0] < end for e in p))

    def test_variants_add_their_keys_after_the_shared_ones(self):
        for name in a.VARIANTS:
            sets = a.sets_for(name, 6)
            self.assertIn('native_gate_world_hash_stride=%d' % a.FINE.get(name, a.STRIDE), sets)
            self.assertIn('native_gate_world_hash_spread=false', sets)
            self.assertEqual(sets[len(sets) - len(a.VARIANTS[name]):], a.VARIANTS[name])
        self.assertIn('native_gate_hybrid={owners={1,2},tick=%d,poses=4}' % a.SPLIT_TICK, a.sets_for('hyb12', 6))
        self.assertIn('native_gate_view_points_mask=1', a.sets_for('mask2', 6))


class ReadTest(unittest.TestCase):
    def write(self, folder, lines):
        path = Path(folder) / 'attn-rows.txt'
        path.write_text('\n'.join(lines) + '\n', encoding='latin-1')
        return path

    def test_sim_profile_leaves_out_captures_and_splits_at_the_split(self):
        with tempfile.TemporaryDirectory() as d:
            lines = []
            for t in range(600, 2000):
                sim = 30000 if t < a.SPLIT_TICK + 300 else 20000
                if t % a.STRIDE == 0:
                    sim = 900000          # a capture tick: left out
                lines.append(row('FRAME_TIMING', 'tick=%d sim_us=%d pre_us=1000' % (t, sim)))
            out = a.sim_profile(self.write(d, lines))
            self.assertEqual(out['before']['median_us'], 31000)
            self.assertEqual(out['after']['median_us'], 21000)
            self.assertLess(out['after']['p90_us'], 900000)

    def test_digest_split_and_status_diff_name_the_first_tick(self):
        with tempfile.TemporaryDirectory() as d:
            logs = {}
            for k in (1, 2):
                folder = Path(d) / ('c%d' % k)
                folder.mkdir()
                lines = []
                for t in range(1, 11):
                    rid = 'aa' if (k == 1 or t < 5) else 'bb'
                    lines.append(row('DIGEST_PARTS', 'tick=%d pilot=11 rid=%s world=22 hash=33' % (t, rid)))
                    lines.append(row('RUNTIME_IDS', 'tick=%d next=%d mode=set wait_us=%d' % (t, 100 + t + (k == 2 and t >= 7), 5 * k)))
                logs[k] = self.write(folder, lines)
            parts, span = a.digest_split(logs)
            self.assertEqual(span, (1, 10))
            self.assertEqual(parts['rid'], [5, 6, 10])
            self.assertEqual(parts['pilot'], [None, 0, 10])
            diff = a.status_diff(logs)
            self.assertEqual(diff, {'RUNTIME_IDS.next': [7, 4, 10]})   # wait_us is a timing field: left out


class CensusTest(unittest.TestCase):
    def test_objects_sorted_into_the_players_sectors_and_the_rest(self):
        save = ('<savegame><universe><component class="galaxy" macro="g" id="[0x1]">'
                '<component class="sector" macro="sec_a" id="[0x10]"><component class="ship_s" macro="s" id="[0x11]"/></component>'
                '<component class="sector" macro="sec_b" id="[0x20]"><component class="station" macro="st" id="[0x21]"/></component>'
                '<component class="sector" macro="sec_c" id="[0x30]"><component class="ship_m" macro="m" id="[0x31]"/></component>'
                '</component></universe></savegame>')
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'save.xml.gz'
            with gzip.open(path, 'wt', encoding='utf-8') as f:
                f.write(save)
            sectors = ac.save_sectors(path)
            self.assertEqual(sectors['0000000000000011'], 'sec_a')
            self.assertEqual(sectors['0000000000000031'], 'sec_c')
            self.assertIsNone(sectors['0000000000000001'])
            run = Path(d) / 'run'
            h = ' '.join(['00' * 8] * 4)
            created = '8000000000000001'       # a created object below the station in sec_b
            for k in (1, 2):
                (run / ('client%d' % k)).mkdir(parents=True)
                for i, tick in enumerate((900, 1800)):
                    state = 'WORLD_IMPORTED_2:%d:5:a:b:c:%s' % (tick, 'd' if tick == 900 or k == 1 else 'e')
                    items = []
                    if tick == 1800:
                        items.append('0000000000000011 0000abcd 00000007 0000000000000010 ' + (h if k == 1 else h.replace('00', '11', 1)))
                        items.append('0000000000000031 0000abcd 00000007 0000000000000030 ' + (h if k == 1 else h.replace('00', '22', 1)))
                        if k == 2:
                            items.append(created + ' 0000abcd 00000007 0000000000000021 ' + h)
                    head = 'WORLD_RECORD_1 tick=%d objects=5 changed=%d rows=%d state=%s' % (tick, len(items), len(items), state)
                    (run / ('client%d' % k) / ('x4lan-census-%d.txt' % i)).write_text('\n'.join([head] + items) + '\n', encoding='ascii')
            out = ac.classify(run, sectors, {'sec_a': 'S1', 'sec_b': 'S2'})
            first, second = out['captures']
            self.assertTrue(first['equal'])
            self.assertEqual(second['by_region'], {'S1': 1, 'outside': 1, 'S2': 1})
            self.assertEqual(out['footprint'], {'S1': 1, 'outside': 1, 'S2': 1})
            self.assertEqual([e[1] for e in out['first_outside']], ['0000000000000031'])
            json.dumps(out)


if __name__ == '__main__':
    unittest.main()
