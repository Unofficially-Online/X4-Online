"""Adversarial synthetic rolling logs; no game or native capture is run."""
import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'dev'))
from linux_gate_evidence import validate_evidence


def capture(tick, slices=3, imported=7, created=0, digest='a', **overrides):
    total = imported + created
    cycle = 1 + (tick - 1) // slices
    index = (tick - 1) % slices
    row = {'tick': tick, 'cycle': cycle, 'slice': index, 'slices': slices,
           'begin': total * index // slices, 'end': total * (index + 1) // slices,
           'objects': total, 'created': created}
    row.update(overrides)
    state = ':'.join(str(row[key]) for key in ('tick', 'cycle', 'slice', 'slices', 'begin', 'end', 'objects', 'created'))
    counts = 'present=0 unknown_class=0 unknown_motion=0 unknown_references=0'
    root = hashlib.sha256(b'').hexdigest() if row['begin'] == row['end'] else digest * 64
    return ('NATIVE_GATE WORLD_ROLLING ' + ' '.join(f'{key}={value}' for key, value in row.items()) +
            f' {counts} state=WORLD_ROLLING_1:{state}:' + ':'.join([root] * 4))


def log(slot, ticks=7, slices=3):
    rows = [capture(tick, slices, created=1 if tick > slices else 0) for tick in range(1, ticks + 1)]
    return rows + [f'NATIVE_GATE STOP slot=p{slot} completed={ticks} reason=1 restored=1',
                   'NATIVE_GATE WORLD_IDENTITY_STOP calls=7 count=7 late=0 restored=1 failures=0',
                   f'NATIVE_GATE WORLD_ROLLING_STOP scans={ticks} cycles={ticks // slices} failures=0']


class RollingEvidenceTest(unittest.TestCase):
    def check(self, first, second=None, ticks=7, slices=3):
        return validate_evidence({1: first, 2: log(2, ticks, slices) if second is None else second},
                                 ticks, 60, rolling_slices=slices)

    def test_success_partial_complete_and_empty_slices(self):
        result = self.check(log(1))
        self.assertTrue(result['evidence_ok'], result['evidence_errors'])
        self.assertEqual(result['completecycles'], 2)
        self.assertEqual(result['partialcycle'], {'cycle': 3, 'sampled_slices': 1, 'total_slices': 3})
        self.assertEqual(result['world_hash_ticks'], 7)
        self.assertEqual(result['world_hash_coverage'][0][0]['begin'], 0)
        self.assertIsNone(self.check(log(1, 6), ticks=6)['partialcycle'])
        small = [capture(t, slices=5, imported=1) for t in range(1, 6)]
        small += ['NATIVE_GATE STOP slot=p1 completed=5 reason=1 restored=1',
                  'NATIVE_GATE WORLD_IDENTITY_STOP calls=1 count=1 late=0 restored=1 failures=0',
                  'NATIVE_GATE WORLD_ROLLING_STOP scans=5 cycles=1 failures=0']
        self.assertTrue(validate_evidence({1: small}, 5, 1, rolling_slices=5)['evidence_ok'])

    def test_matching_wrong_empty_slice_roots_fail(self):
        rows = [capture(t, slices=5, imported=1) for t in range(1, 6)]
        empty_hash = hashlib.sha256(b'').hexdigest()
        rows[0] = rows[0].replace(empty_hash, 'a' * 64)
        footer = ['NATIVE_GATE STOP slot=p{} completed=5 reason=1 restored=1',
                  'NATIVE_GATE WORLD_IDENTITY_STOP calls=1 count=1 late=0 restored=1 failures=0',
                  'NATIVE_GATE WORLD_ROLLING_STOP scans=5 cycles=1 failures=0']
        logs = {slot: rows + [line.format(slot) for line in footer] for slot in (1, 2)}
        result = validate_evidence(logs, 5, 1, rolling_slices=5)
        self.assertFalse(result['evidence_ok'])
        self.assertIsNone(result['world_hash_first_difference'])
        self.assertIn('invalid empty WORLD_ROLLING roots', ' '.join(result['evidence_errors']))

    def test_missing_duplicate_order_and_late_capture(self):
        rows = log(1)
        variants = [rows[:2] + rows[3:], rows[:2] + [rows[1]] + rows[2:],
                    [rows[1], rows[0], *rows[2:]], rows[1:] + [rows[0]]]
        for changed in variants:
            with self.subTest(changed=changed):
                self.assertFalse(self.check(changed)['evidence_ok'])

    def test_cycles_count_only_contiguous_valid_samples(self):
        result = self.check([], [])
        self.assertFalse(result['evidence_ok'])
        self.assertEqual(result['requestedcycles'], 2)
        self.assertEqual(result['completecycles'], 0)
        self.assertIsNone(result['partialcycle'])
        rows = log(1)
        missing = rows[:4] + rows[5:]
        result = self.check(missing)
        self.assertFalse(result['evidence_ok'])
        self.assertEqual(result['observed_contiguous_ticks'], 4)
        self.assertEqual(result['completecycles'], 1)
        self.assertEqual(result['partialcycle']['sampled_slices'], 1)
        missing_prefix = rows[1:]
        result = self.check(missing_prefix)
        self.assertEqual(result['observed_contiguous_ticks'], 0)
        self.assertEqual(result['completecycles'], 0)
        peer = log(2)
        peer[3] = capture(4, created=1, digest='b')
        result = self.check(rows, peer)
        self.assertFalse(result['evidence_ok'])
        self.assertEqual(result['completecycles'], 2)

    def test_metadata_schedule_bounds_and_coverage(self):
        base = log(1)
        changes = [capture(2, cycle=3), capture(2, slice=2), capture(2, slices=4),
                   capture(2, begin=0), capture(2, objects=0), capture(2, created=8),
                   capture(2).replace('WORLD_ROLLING_1:2:', 'WORLD_ROLLING_1:3:'),
                   capture(2).replace('present=0', 'present=99'),
                   capture(2).replace('unknown_references=0', 'unknown_references=1'),
                   capture(2).replace('unknown_motion=0', 'unknown_motion=1'),
                   capture(2).replace('unknown_class=0', 'unknown_class=1'),
                   'NATIVE_GATE WORLD_ROLLING tick=2 bad']
        for replacement in changes:
            with self.subTest(replacement=replacement):
                rows = base.copy()
                rows[1] = replacement
                self.assertFalse(self.check(rows)['evidence_ok'])

    def test_cohort_stability_and_growth(self):
        rows = log(1)
        for tick, replacement in [(2, capture(2, created=1)), (4, capture(4, imported=8)),
                                  (7, capture(7, created=0))]:
            with self.subTest(tick=tick):
                changed = rows.copy()
                changed[tick - 1] = replacement
                self.assertFalse(self.check(changed)['evidence_ok'])

    def test_footer_failures_fatals_and_other_mode(self):
        rows = log(1)
        changes = [rows[:-1], rows + [rows[-1]],
                   rows[:-1] + [rows[-1].replace('cycles=2', 'cycles=3')],
                   rows[:-1] + [rows[-1].replace('failures=0', 'failures=1')],
                   rows[:-2] + rows[-1:], rows[:-3] + rows[-2:],
                   rows + ['NATIVE_GATE ERROR bad'],
                   rows + ['NATIVE_GATE WORLD_CAPTURE_PROBLEM stage=bad'],
                   rows + ['NATIVE_GATE WORLD_HASH tick=1 bad'],
                   [*rows[:7], rows[-2], rows[-3], rows[-1]],
                   rows[:-2] + [rows[-2].replace('count=7', 'count=6')] + rows[-1:],
                   [rows[-1], *rows[:-1]]]
        for changed in changes:
            with self.subTest(changed=changed):
                self.assertFalse(self.check(changed)['evidence_ok'])

    def test_peer_difference_and_invalid_configuration(self):
        peer = log(2)
        peer[3] = capture(4, created=1, digest='b')
        result = self.check(log(1), peer)
        self.assertFalse(result['evidence_ok'])
        self.assertEqual(result['world_hash_first_difference'], 4)
        for invalid in (0, -1, True, 1.5, 18001):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    validate_evidence({1: log(1)}, 7, 60, rolling_slices=invalid)


if __name__ == '__main__':
    unittest.main()
