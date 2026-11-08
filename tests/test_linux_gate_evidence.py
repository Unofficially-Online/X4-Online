"""Synthetic log tests; these do not exercise X4 or the native gate."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'dev'))
from linux_gate_evidence import validate_evidence


def capture(tick, digest='a', count=12):
    state = f'WORLD_IMPORTED_2:{tick}:{count}:' + ':'.join([digest * 64] * 4)
    return (f'[X4LAN] NATIVE_GATE WORLD_HASH tick={tick} objects={count} present={count} '
            f'changed=0 unknown_class=0 unknown_motion=0 unknown_references=0 state={state}')


def log(slot, ticks=120, stride=60):
    captures = [capture(t) for t in sorted({1, *range(stride, ticks + 1, stride)})]
    return captures + [f'NATIVE_GATE STOP slot=p{slot} completed={ticks} reason=1 restored=1',
                       'NATIVE_GATE WORLD_IDENTITY_STOP calls=12 count=12 late=0 restored=1 failures=0',
                       f'NATIVE_GATE WORLD_HASH_STOP scans={len(captures)} failures=0']


class EvidenceTest(unittest.TestCase):
    def check(self, one, two=None, ticks=120, stride=60):
        return validate_evidence({1: one, 2: log(2) if two is None else two}, ticks, stride)

    def test_complete_and_single_external_client(self):
        self.assertTrue(self.check(log(1))['evidence_ok'])
        self.assertTrue(validate_evidence({2: log(2)}, 120, 60)['evidence_ok'])
        self.assertTrue(validate_evidence({1: log(1, 1, 1)}, 1, 1)['evidence_ok'])

    def test_missing_later_capture_was_accepted_by_intersection(self):
        one, two = log(1), log(2)
        one.remove(capture(120))
        # The former verdict compared only the non-empty intersection, {1, 60}.
        result = self.check(one, two)
        self.assertEqual(result['world_hash_ticks'], 2)
        self.assertIsNone(result['world_hash_first_difference'])
        self.assertFalse(result['evidence_ok'])
        self.assertIn('missing WORLD_HASH ticks [120]', ' '.join(result['evidence_errors']))

    def test_problem_identity_and_footer_failures(self):
        for added, removed in [
            ('NATIVE_GATE WORLD_CAPTURE_PROBLEM stage=dock_controller count=1', None),
            ('NATIVE_GATE WORLD_IDENTITY_STOP calls=12 count=12 late=0 restored=1 failures=1', 'WORLD_IDENTITY_STOP'),
            (None, 'WORLD_IDENTITY_STOP'), (None, 'WORLD_HASH_STOP'),
            ('NATIVE_GATE WORLD_HASH_STOP scans=3 failures=1', 'WORLD_HASH_STOP'),
            ('NATIVE_GATE STOP slot=p1 completed=119 reason=1 restored=1', 'NATIVE_GATE STOP '),
        ]:
            with self.subTest(added=added, removed=removed):
                rows = [x for x in log(1) if not removed or removed not in x]
                if added:
                    rows.append(added)
                self.assertFalse(self.check(rows)['evidence_ok'])

    def test_duplicate_conflict_malformed_and_differences(self):
        for extra in [capture(60), capture(60, 'b'), capture(61), capture(120).replace('WORLD_IMPORTED_2:120:', 'WORLD_IMPORTED_2:119:'),
                      'NATIVE_GATE WORLD_HASH tick=90 invalid']:
            with self.subTest(extra=extra):
                self.assertFalse(self.check(log(1) + [extra])['evidence_ok'])
        peer = [capture(60, 'b') if x == capture(60) else x for x in log(2)]
        result = self.check(log(1), peer)
        self.assertEqual(result['world_hash_first_difference'], 60)
        self.assertFalse(result['evidence_ok'])

    def test_empty_capture_fails_and_coverage_limits_are_reported(self):
        rows = [capture(1, count=0) if x == capture(1) else x for x in log(1)]
        self.assertFalse(self.check(rows)['evidence_ok'])
        rows = [x.replace('unknown_motion=0', 'unknown_motion=2') for x in log(1)]
        result = self.check(rows)
        self.assertTrue(result['evidence_ok'])
        self.assertEqual(result['world_hash_coverage'][0][0]['unknown_motion'], 2)

    def test_fatal_messages_after_successful_footers(self):
        for fatal in ('NATIVE_GATE REFUSED build', 'NATIVE_GATE ERROR code=7',
                      'NATIVE_GATE ERROR DEGRADEDLY unexpected',
                      'NATIVE_GATE ERROR DEFAULT_SKIPPED_BAD unexpected',
                      'NATIVE_GATE ERROR'):
            with self.subTest(fatal=fatal):
                result = self.check(log(1) + [fatal])
                self.assertFalse(result['evidence_ok'])
                self.assertIn('fatal gate message', ' '.join(result['evidence_errors']))
        for allowed in ('NATIVE_GATE ERROR DEGRADED diagnostic',
                        'NATIVE_GATE ERROR DEFAULT_SKIPPED diagnostic'):
            with self.subTest(allowed=allowed):
                self.assertTrue(self.check(log(1) + [allowed])['evidence_ok'])

    def test_missing_and_duplicate_stop_and_wrong_slot(self):
        rows = log(1)
        for changed in ([x for x in rows if 'NATIVE_GATE STOP ' not in x], rows + [rows[-3]],
                        [x.replace('slot=p1', 'slot=p2') for x in rows]):
            self.assertFalse(self.check(changed)['evidence_ok'])


if __name__ == '__main__':
    unittest.main()
