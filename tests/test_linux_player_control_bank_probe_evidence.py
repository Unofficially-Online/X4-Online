"""Synthetic full-stream evidence checks; no native game is launched."""
import sys
from pathlib import Path
import unittest
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from linux_gate_evidence import validate_evidence
import linux_gate_live as live
from test_linux_gate_evidence import log

SUCCESS = dict(requested='1', attempted='1', captured_tick='5', complete='1', clean='1',
               canonical_only='1', native_update_calls='0', canonical_bytes_restored='1',
               input_tree_unchanged='1', context_count='13', zone_count_before='3', zone_count_after='3',
               temporary_requested_space='0', temporary_identity_reason='fixture',
               actual_requested_space='0', actual_identity_reason='fixture', cleanup_complete='1', reason='none')
FATAL = ('X4LAN NATIVE_GATE ERROR PLAYER_CONTROL_BANK_PROBE_FATAL exit=221 captured_tick=5 '
         'reason=cleanup_failed complete=0 cleanup_complete=0 bank_phase=3 pending_bytes=1 '
         'zone_phase=2 zone_pending=1 effect_pending=1 slot_owned=1 canonical_owned=1')


def status(tick=None, **changes):
    values = {**SUCCESS, **changes}
    event = 'PLAYER_CONTROL_BANK_PROBE_STOP' if tick is None else f'PLAYER_CONTROL_BANK_PROBE tick={tick}'
    payload = ' '.join(f'{key}={value}' for key, value in values.items() if value is not None)
    return f'[X4LAN] NATIVE_GATE {event} {payload}'


def client(slot=1):
    return log(slot, ticks=8, stride=1) + [
        status(2, attempted='0', captured_tick='0', complete='0'),
        status(3, attempted='0', captured_tick='0', complete='0'),
        status(4, attempted='0', captured_tick='0', complete='0'),
        status(5), status(6), status(7), status(8), status()]


def check(one=None, two=None, enabled=True):
    return validate_evidence({1: client(1) if one is None else one,
                              2: client(2) if two is None else two},
                             8, 1, player_control_bank_probe=enabled)


class ControlBankProbeEvidenceTests(unittest.TestCase):
    def test_cached_success_may_repeat_at_different_event_ticks_and_stop(self):
        result = check()
        self.assertTrue(result['evidence_ok'], result['evidence_errors'])
        self.assertEqual(result['player_control_bank_probe_errors'], [])
        self.assertEqual(result['player_control_bank_probe'][1], SUCCESS)

    def test_default_off_preserves_general_behavior(self):
        self.assertTrue(validate_evidence({1: log(1, 8, 1)}, 8, 1)['evidence_ok'])
        self.assertTrue(check(log(1, 8, 1), log(2, 8, 1), enabled=False)['evidence_ok'])
        self.assertNotIn('player_control_bank_probe', check(enabled=False))

    def test_enabled_requires_evidence_from_every_client(self):
        for one, two in ((log(1, 8, 1), client(2)), (client(1), log(2, 8, 1))):
            self.assertFalse(check(one, two)['evidence_ok'])

    def test_missing_event_or_stop_is_failure(self):
        for rows in ([line for line in client() if 'PLAYER_CONTROL_BANK_PROBE tick=' not in line],
                     [line for line in client() if 'PLAYER_CONTROL_BANK_PROBE_STOP' not in line]):
            self.assertFalse(check(rows)['evidence_ok'])

    def test_each_restoration_or_canonical_contract_field_is_required(self):
        for field in SUCCESS:
            if field in ('context_count', 'temporary_identity_reason', 'actual_requested_space', 'actual_identity_reason'):
                continue
            with self.subTest(field=field):
                rows = [line.replace(status(6), status(6, **{field: None})) for line in client()]
                self.assertFalse(check(rows)['evidence_ok'])

    def test_each_required_success_value_rejects_failure(self):
        failures = dict(requested='0', attempted='0', captured_tick='4', complete='0', clean='0',
                        canonical_only='0', native_update_calls='1', canonical_bytes_restored='0',
                        input_tree_unchanged='0', temporary_requested_space='2', cleanup_complete='0', reason='failed')
        for field, value in failures.items():
            with self.subTest(field=field):
                rows = [status(6, **{field: value}) if line == status(6) else line for line in client()]
                self.assertFalse(check(rows)['evidence_ok'])

    def test_zone_counts_must_be_positive_numeric_and_unchanged(self):
        for before, after in (('0', '0'), ('3', '4'), ('-1', '-1'), ('unknown', 'unknown')):
            with self.subTest(before=before, after=after):
                rows = [status(6, zone_count_before=before, zone_count_after=after)
                        if line == status(6) else line for line in client()]
                self.assertFalse(check(rows)['evidence_ok'])

    def test_duplicate_event_tick_or_stop_rejected_even_when_identical(self):
        for extra in (status(6), status(6, context_count='14'), status()):
            with self.subTest(extra=extra):
                rows = client()
                rows.insert(-1, extra)
                self.assertFalse(check(rows)['evidence_ok'])

    def test_cached_payload_changes_and_peer_divergence_rejected(self):
        changed = [status(6, context_count='14') if line == status(6) else line for line in client()]
        self.assertFalse(check(changed)['evidence_ok'])
        peer = [line.replace('context_count=13', 'context_count=14') for line in client(2)]
        self.assertFalse(check(two=peer)['evidence_ok'])

    def test_early_success_or_capture_never_qualifies(self):
        for tick in (2, 3, 4):
            pending = status(tick, attempted='0', captured_tick='0', complete='0')
            rows = [status(tick) if line == pending else line for line in client()]
            self.assertFalse(check(rows)['evidence_ok'])

    def test_stop_must_repeat_the_same_successful_capture(self):
        for replacement in (status(context_count='14'), status(clean='0'), status(captured_tick='6')):
            self.assertFalse(check(client()[:-1] + [replacement])['evidence_ok'])

    def test_conflicting_duplicate_field_malformed_and_event_after_stop_rejected(self):
        for row in (status(6) + ' clean=0', status(6) + ' garbage', status(9),
                    status(6).replace('tick=6', 'tick=invalid', 1)):
            rows = client()
            rows.insert(-1, row)
            self.assertFalse(check(rows)['evidence_ok'])
        self.assertFalse(check(client() + [status(6)])['evidence_ok'])

    def test_fatal_gate_error_after_success_cannot_be_overwritten_by_probe(self):
        for fatal in ('[X4LAN] NATIVE_GATE ERROR cleanup failed', FATAL):
            with self.subTest(fatal=fatal):
                result = check(client() + [fatal])
                self.assertFalse(result['evidence_ok'])
                self.assertTrue(any('fatal gate message' in error for error in result['evidence_errors']))

    def test_generators_are_consumed_once_and_single_local_slot_is_valid(self):
        rows = (line for line in client())
        result = validate_evidence({1: rows}, 8, 1, player_control_bank_probe=True)
        self.assertTrue(result['evidence_ok'], result['evidence_errors'])


class CollectedControlBankEvidenceTests(unittest.TestCase):
    def collect(self, rows, enabled=True, stdout=''):
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
                                             {'stride': 1, 'player_control_bank_probe': enabled}, True)

    def test_manifest_opt_in_requires_actual_probe_receipts(self):
        self.assertTrue(self.collect(client())['evidence_ok'])
        self.assertFalse(self.collect(log(1, 8, 1))['evidence_ok'])
        self.assertTrue(self.collect(log(1, 8, 1), enabled=False)['evidence_ok'])

    def test_complete_engine_stream_keeps_late_fatal_failure(self):
        result = self.collect(client() + [FATAL])
        self.assertFalse(result['evidence_ok'])
        self.assertTrue(any('PLAYER_CONTROL_BANK_PROBE_FATAL' in e for e in result['evidence_errors']))

    def test_synchronous_fatal_in_separate_process_output_is_not_lost(self):
        result = self.collect(client(), stdout=FATAL + '\n')
        self.assertFalse(result['evidence_ok'])
        self.assertTrue(any('PLAYER_CONTROL_BANK_PROBE_FATAL' in e for e in result['evidence_errors']))

    def test_repeated_ordinary_process_output_is_not_a_duplicate_capture(self):
        self.assertTrue(self.collect(client(), stdout='\n'.join(client()))['evidence_ok'])

    def test_enabled_probe_requires_retained_process_output(self):
        with self.assertRaisesRegex(RuntimeError, 'control-bank process output missing'):
            self.collect(client(), stdout=None)

    def test_nonprobe_keeps_legacy_missing_process_output_compatibility(self):
        self.assertTrue(self.collect(log(1, 8, 1), enabled=False, stdout=None)['evidence_ok'])


if __name__ == '__main__':
    unittest.main()
