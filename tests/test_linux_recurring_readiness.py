"""Recurring readiness protocol checks; no game or native process is launched."""
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live


def rows(end=30):
    return [f'X4LAN PLAYER_CONTROL_RECURRING tick={tick} completed=1 readiness_only=1 '
            'bank_writes=0 context_writes=0 reference_calls=0 other_threads_excluded=0\n'
            for tick in range(5, end + 1)]


class RecurringReadinessTests(unittest.TestCase):
    def test_complete_window_and_narrow_claim(self):
        for end in (5, 30, 1000):
            evidence = live.recurring_readiness_evidence(rows(end), end)
            self.assertTrue(evidence['pass'], evidence['errors'])
            self.assertEqual(evidence['ticks'], list(range(5, end + 1)))
            self.assertTrue(evidence['readiness_only'])
            self.assertFalse(evidence['other_threads_excluded'])

    def test_missing_duplicate_reordered_or_outside_window(self):
        valid = rows(8)
        for bad in (valid[:-1], valid + [valid[-1]], list(reversed(valid)),
                    [valid[0].replace('tick=5', 'tick=4')] + valid[1:]):
            self.assertFalse(live.recurring_readiness_evidence(bad, 8)['pass'])

    def test_strict_fields_and_complete_raw_lines(self):
        valid = rows(5)
        for before, after in [('completed=1', 'completed=0'), ('readiness_only=1', 'readiness_only=0'),
                              ('bank_writes=0', 'bank_writes=1'), ('context_writes=0', 'context_writes=1'),
                              ('reference_calls=0', 'reference_calls=1'),
                              ('other_threads_excluded=0', 'other_threads_excluded=1'),
                              ('tick=5', 'tick=05'), ('\n', ' extra=1\n'), ('\n', '')]:
            self.assertFalse(live.recurring_readiness_evidence(
                [valid[0].replace(before, after)], 5)['pass'], (before, after))
        self.assertFalse(live.recurring_readiness_evidence(['noise ' + valid[0]], 5)['pass'])
        self.assertTrue(live.recurring_readiness_evidence(['ordinary stderr chatter\n', *valid], 5)['pass'])

    def test_fatal_records_independent_of_completed_window(self):
        for fatal in ('X4LAN PLAYER_CONTROL_RECURRING_FATAL exit=222 reason=bad state=2 failure=1\n',
                      'prefix PLAYER_CONTROL_RECURRING_FATAL malformed\n', 'NATIVE_GATE ERROR bad\n'):
            self.assertFalse(live.recurring_readiness_evidence(rows(5) + [fatal], 5)['pass'])
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'stderr'
            log.write_text('X4LAN PLAYER_CONTROL_RECURRING_FATAL exit=222 reason=bad state=2 failure=1\n')
            watch = live.LogWatch(log)
            watch.poll()
            self.assertIn('RECURRING_FATAL', watch.failure)

    def test_admission_and_effective_ticks(self):
        manifest = dict(stock_keyboard_observer=True, serial_pathing=True)
        live.validate_recurring_readiness(manifest, 31, 30)
        for end in (4, 1001, True, None):
            self.assertRaises(SystemExit, live.validate_recurring_readiness, manifest, 2000, end)
        self.assertRaises(SystemExit, live.validate_recurring_readiness, manifest, 30, 30)
        for key, value in (('stock_keyboard_observer', False), ('serial_pathing', False),
                           ('player_control_boundary_probe', True), ('player_control_bank_probe', True)):
            self.assertRaises(SystemExit, live.validate_recurring_readiness, manifest | {key: value}, 31, 30)

    def test_run_flag_end_and_mutual_exclusion(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-recurring-readiness',
                       '--controller-recurring-end', '30'])
            self.assertTrue(run.call_args.args[0].controller_recurring_readiness)
            self.assertEqual(run.call_args.args[0].controller_recurring_end, 30)
        for flag in ('--controller-lifecycle-discard', '--controller-lifecycle-isolated-discard',
                     '--controller-lifecycle-update-discard', '--controller-lifecycle-routes-discard',
                     '--controller-lifecycle-input-discard'):
            with mock.patch.object(live, 'run') as run, \
                    mock.patch.object(sys, 'stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                live.main(['run', '--root', '/unused', '--controller-recurring-readiness', flag])
            run.assert_not_called()

    def test_run_refuses_tick_override_and_end_without_optin_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'bundle'
            root.mkdir()
            (root / live.MARKER).touch()
            (root / 'manifest.json').write_text(json.dumps(
                dict(ticks=100, stock_keyboard_observer=True, serial_pathing=True)))
            user = SimpleNamespace(pw_dir=directory, pw_uid=os.geteuid())
            for enabled, ticks in ((True, 30), (False, 100)):
                args = SimpleNamespace(user='fixture', root=root, ticks=ticks,
                                       controller_recurring_readiness=enabled, controller_recurring_end=30)
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live.subprocess, 'Popen') as launch, self.assertRaises(SystemExit):
                    live.run(args)
                launch.assert_not_called()


if __name__ == '__main__':
    unittest.main()
