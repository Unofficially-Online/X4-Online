"""Synthetic readiness receipts; success does not establish full writer exclusion."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live
from linux_gate_evidence import validate_evidence
from test_linux_gate_evidence import log
from test_linux_player_control_bank_probe_evidence import client as bank_client

FIXED = dict(requested='1', failure='0', readiness_only='1', bank_writes='0',
             context_writes='0', reference_calls='0', other_threads_excluded='0')
COMPLETE = dict(captured_tick='5', main_parked='1', worker_entered='1', worker_released='1',
                main_return_allowed='1', complete='1', state='126')
PENDING = dict(captured_tick='0', main_parked='0', worker_entered='0', worker_released='0',
               main_return_allowed='0', complete='0', state='0')
FATAL = ('X4LAN NATIVE_GATE ERROR PLAYER_CONTROL_BOUNDARY_FATAL exit=222 '
         'reason=main_timeout state=190 failure=9')


def status(tick=None, ack=True, **changes):
    values = {**FIXED, **(PENDING if tick is not None and tick < 5 else COMPLETE)}
    if tick == 5 and not ack:
        values.update(main_return_allowed='0', complete='0', state='62')
    values.update(changes)
    event = 'PLAYER_CONTROL_BOUNDARY_STOP' if tick is None else f'PLAYER_CONTROL_BOUNDARY tick={tick}'
    payload = ' '.join(f'{name}={value}' for name, value in values.items() if value is not None)
    return f'[X4LAN] NATIVE_GATE {event} {payload}'


def client(slot=1, ack=True):
    return log(slot, 8, 1) + [status(tick, ack=ack) for tick in range(2, 9)] + [status()]


def check(one=None, two=None, enabled=True):
    return validate_evidence({1: client() if one is None else one,
                              2: client(2) if two is None else two},
                             8, 1, player_control_boundary_probe=enabled)


class ControlBoundaryEvidenceTests(unittest.TestCase):
    def test_tick_five_ack_race_and_cached_statuses_are_valid(self):
        for ack in (True, False):
            with self.subTest(ack=ack):
                result = check(client(ack=ack))
                self.assertTrue(result['evidence_ok'], result['evidence_errors'])
                self.assertEqual(result['player_control_boundary_probe'][1], {**FIXED, **COMPLETE})
                self.assertIn('readiness only', result['player_control_boundary_scope'])
                self.assertIn('no claim', result['player_control_boundary_scope'])

    def test_default_off_preserves_nonprobe_behavior(self):
        result = check(log(1, 8, 1), log(2, 8, 1), enabled=False)
        self.assertTrue(result['evidence_ok'])
        self.assertNotIn('player_control_boundary_probe', result)

    def test_each_client_requires_each_event_and_stop(self):
        for removed in [status(tick) for tick in range(2, 9)] + [status()]:
            with self.subTest(removed=removed):
                self.assertFalse(check([row for row in client() if row != removed])['evidence_ok'])
        self.assertFalse(check(two=log(2, 8, 1))['evidence_ok'])

    def test_pending_request_at_tick_two_must_be_uncaptured(self):
        for changes in ({'requested': '0'}, {'captured_tick': '5'}, {'complete': '1'}, {'state': '62'}):
            with self.subTest(changes=changes):
                rows = [status(2, **changes) if row == status(2) else row for row in client()]
                self.assertFalse(check(rows)['evidence_ok'])

    def test_early_success_is_rejected_at_every_precapture_tick(self):
        for tick in (2, 3, 4):
            rows = [status(tick, **COMPLETE) if row == status(tick) else row for row in client()]
            self.assertFalse(check(rows)['evidence_ok'])

    def test_inconsistent_tick_five_ack_flags_are_rejected(self):
        for changes in ({'state': '62'}, {'complete': '0'}, {'main_return_allowed': '0'},
                        {'captured_tick': '0'}, {'state': '30'}, {'worker_released': '0'}):
            rows = [status(5, **changes) if row == status(5) else row for row in client()]
            self.assertFalse(check(rows)['evidence_ok'])

    def test_post_five_and_stop_require_fully_completed_rendezvous(self):
        for tick in (6, 7, 8, None):
            for field, value in COMPLETE.items():
                with self.subTest(tick=tick, field=field):
                    changed = '4' if field == 'captured_tick' else '0'
                    rows = [status(tick, **{field: changed}) if row == status(tick) else row for row in client()]
                    self.assertFalse(check(rows)['evidence_ok'])

    def test_every_required_field_is_present(self):
        for field in {**FIXED, **COMPLETE}:
            rows = [status(6, **{field: None}) if row == status(6) else row for row in client()]
            self.assertFalse(check(rows)['evidence_ok'], field)

    def test_failure_write_or_full_exclusion_claim_invalidates_readiness(self):
        failures = dict(failure='1', readiness_only='0', bank_writes='1', context_writes='1',
                        reference_calls='1', other_threads_excluded='1')
        for field, value in failures.items():
            with self.subTest(field=field):
                rows = [status(6, **{field: value}) if row == status(6) else row for row in client()]
                self.assertFalse(check(rows)['evidence_ok'])

    def test_duplicate_event_or_stop_is_rejected_even_if_identical(self):
        for extra in (status(2), status(5, ack=False), status(6), status()):
            rows = client()
            rows.insert(-1, extra)
            self.assertFalse(check(rows)['evidence_ok'])

    def test_unexpected_ticks_order_and_after_stop_are_rejected(self):
        for extra in (status(1), status(9), status(6).replace('tick=6', 'tick=invalid', 1)):
            rows = client()
            rows.insert(-1, extra)
            self.assertFalse(check(rows)['evidence_ok'])
        self.assertFalse(check(client() + [status(6)])['evidence_ok'])
        rows = client()
        index = rows.index(status(5))
        rows[index:index+2] = [status(6), status(5)]
        self.assertFalse(check(rows)['evidence_ok'])

    def test_malformed_duplicate_fields_and_conflicting_stable_fields_fail(self):
        for replacement in (status(6) + ' failure=0', status(6) + ' malformed',
                            status(6) + ' diagnostic=changed'):
            rows = [replacement if row == status(6) else row for row in client()]
            self.assertFalse(check(rows)['evidence_ok'])

    def test_late_raw_fatal_222_cannot_be_overwritten_by_readiness_success(self):
        result = check(client() + [FATAL])
        self.assertFalse(result['evidence_ok'])
        self.assertTrue(any('PLAYER_CONTROL_BOUNDARY_FATAL' in e for e in result['evidence_errors']))

    def test_combined_bank_and_boundary_observers_consume_single_pass(self):
        logs = {}
        for slot in (1, 2):
            # Each independent probe has its own STOP marker and status timeline.
            rows = bank_client(slot) + [status(tick, ack=False) for tick in range(2, 9)] + [status()]
            logs[slot] = (line for line in rows)
        result = validate_evidence(logs, 8, 1, player_control_bank_probe=True,
                                   player_control_boundary_probe=True)
        self.assertTrue(result['evidence_ok'], result['evidence_errors'])
        self.assertIn('player_control_bank_probe', result)
        self.assertIn('player_control_boundary_probe', result)

    def test_single_client_valid_and_short_requested_run_fails(self):
        result = validate_evidence({2: iter(client(2, ack=False))}, 8, 1,
                                   player_control_boundary_probe=True)
        self.assertTrue(result['evidence_ok'], result['evidence_errors'])
        self.assertFalse(validate_evidence({1: log(1, 5, 1) + [status(2), status(3), status(4), status(5), status()]},
                                           5, 1, player_control_boundary_probe=True)['evidence_ok'])


class CollectedBoundaryEvidenceTests(unittest.TestCase):
    def collect(self, rows, stdout='', enabled=True):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            case, profile, run = root / 'case', root / 'profile', root / 'run'
            ui = case / 'extensions/x4_lan_lab/ui'
            ui.mkdir(parents=True)
            (ui / 'config.lua').write_text('fixture config\n')
            profile.mkdir()
            run.mkdir()
            (profile / 'engine-output.log').write_text('\n'.join(rows) + '\n')
            if stdout is not None:
                (run / 'game1-stdout.log').write_text(stdout)
            return live.collect_run_evidence(run, [1], [(case, profile)], 8,
                {'stride': 1, 'player_control_boundary_probe': enabled}, True)

    def test_collector_requires_readiness_receipts_only_when_enabled(self):
        self.assertTrue(self.collect(client(ack=False))['evidence_ok'])
        self.assertFalse(self.collect(log(1, 8, 1))['evidence_ok'])
        self.assertTrue(self.collect(log(1, 8, 1), stdout=None, enabled=False)['evidence_ok'])

    def test_enabled_collector_requires_complete_process_output(self):
        with self.assertRaisesRegex(RuntimeError, 'control-boundary process output missing'):
            self.collect(client(), stdout=None)

    def test_late_synchronous_fatal_222_from_process_output_is_retained(self):
        result = self.collect(client(), stdout=FATAL + '\n')
        self.assertFalse(result['evidence_ok'])
        self.assertTrue(any('PLAYER_CONTROL_BOUNDARY_FATAL' in e for e in result['evidence_errors']))

    def test_ordinary_duplicate_stdout_receipts_do_not_become_second_capture(self):
        self.assertTrue(self.collect(client(), stdout='\n'.join(client()))['evidence_ok'])


if __name__ == '__main__':
    unittest.main()
