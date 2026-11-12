"""Strict worker-tail readiness stream; fixtures never launch a native game."""
import io
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live


def rows(end=30):
    lines = []
    for tick in range(5, end + 1):
        lines.extend(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event={event}\n'
                     for event in ('enter', 'release'))
        lines.append(f'X4LAN PLAYER_CONTROL_RECURRING tick={tick} completed=1 readiness_only=1 '
                     'bank_writes=0 context_writes=0 reference_calls=0 other_threads_excluded=0 tail=1\n')
    return lines + ['X4LAN PLAYER_CONTROL_RECURRING tail=1 hook_restored=1\n']


class RecurringTailTests(unittest.TestCase):
    def test_complete_window_then_single_restoration(self):
        for end in (5, 30, 1000):
            evidence = live.recurring_tail_readiness_evidence(rows(end), end)
            self.assertTrue(evidence['pass'], evidence['errors'])
            self.assertEqual(evidence['ticks'], list(range(5, end + 1)))
            self.assertEqual(len(evidence['events']), 3 * (end - 4) + 1)
            self.assertTrue(evidence['tail'] and evidence['hook_restored'])
            self.assertTrue(evidence['readiness_only'])
            self.assertFalse(evidence['other_threads_excluded'])

    def test_missing_duplicate_and_cross_tick_or_phase_reordering(self):
        valid = rows(6)
        cases = [valid[:-1], valid[1:], valid[:1] + valid[2:], valid[:2] + valid[3:],
                 [valid[-1]] + valid[:-1], valid + [valid[-1]], valid[:1] + valid,
                 [valid[1], valid[0]] + valid[2:], valid[:2] + [valid[3], valid[2]] + valid[4:],
                 valid[:3] + [valid[-1]] + valid[3:-1]]
        for bad in cases:
            self.assertFalse(live.recurring_tail_readiness_evidence(bad, 6)['pass'])

    def test_exact_raw_flags_ticks_and_final_record(self):
        valid = rows(5)
        for before, after in [('tail=1', 'tail=0'), ('tick=5', 'tick=05'), ('tick=5', 'tick=6'),
                              ('event=enter', 'event=release'), ('event=release', 'event=other'),
                              ('completed=1', 'completed=0'), ('bank_writes=0', 'bank_writes=1'),
                              ('context_writes=0', 'context_writes=1'), ('reference_calls=0', 'reference_calls=1'),
                              ('other_threads_excluded=0', 'other_threads_excluded=1'),
                              ('hook_restored=1', 'hook_restored=0'), ('\n', ''), ('\n', ' extra=1\n')]:
            with self.subTest(before=before, after=after):
                self.assertFalse(live.recurring_tail_readiness_evidence(
                    [line.replace(before, after) for line in valid], 5)['pass'])
        self.assertFalse(live.recurring_tail_readiness_evidence(['prefix ' + valid[0], *valid[1:]], 5)['pass'])

    def test_original_parser_stays_strict_and_fatal_overrides_complete_window(self):
        self.assertFalse(live.recurring_readiness_evidence(rows(5), 5)['pass'])
        old = [rows(5)[2].replace(' tail=1', '')]
        self.assertTrue(live.recurring_readiness_evidence(old, 5)['pass'])
        self.assertFalse(live.recurring_tail_readiness_evidence(old, 5)['pass'])
        for fatal in ('X4LAN PLAYER_CONTROL_RECURRING_FATAL exit=222 reason=bad state=1 failure=1\n',
                      'NATIVE_GATE ERROR bad\n'):
            self.assertFalse(live.recurring_tail_readiness_evidence(rows(5) + [fatal], 5)['pass'])

    def test_tail_flag_end_and_mutual_exclusion(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-recurring-tail-readiness',
                       '--controller-recurring-end', '30'])
            self.assertTrue(run.call_args.args[0].controller_recurring_tail_readiness)
            self.assertFalse(run.call_args.args[0].controller_recurring_readiness)
            self.assertEqual(run.call_args.args[0].controller_recurring_end, 30)
        for other in ('--controller-recurring-readiness', '--controller-lifecycle-discard',
                      '--controller-lifecycle-isolated-discard', '--controller-lifecycle-update-discard',
                      '--controller-lifecycle-routes-discard', '--controller-lifecycle-input-discard'):
            with mock.patch.object(live, 'run') as run, \
                    mock.patch.object(sys, 'stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                live.main(['run', '--root', '/unused', '--controller-recurring-tail-readiness', other])
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
