"""Raw observer-evidence counterexamples; no native run or normalized fixtures."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
SPEC = importlib.util.spec_from_file_location('stock_keyboard_evidence', ROOT / 'tools/dev/linux_stock_keyboard_evidence.py')
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


def status(tick, stopped=False, **changes):
    fields = {'mode': 'observe_only', 'requested': 1, 'installed_tick': 0 if tick == 1 else 2,
              'input_tick': 0 if tick == 1 else tick, 'active': int(tick > 1 and not stopped),
              'owned': 32767 if tick > 1 and not stopped else 0, 'uncertain': 0,
              'pending_readback': 0, 'permission_pending': 0, 'depth': 0, 'overflow': 0,
              'queued': 0, 'thread_failure': 0, 'failed': 0, 'cleaned': int(stopped),
              'problem': 'restored' if stopped else 'off' if tick == 1 else 'observing_only',
              'failure_reason': 'none'}
    fields.update(changes)
    tag = 'STOCK_KEYBOARD_STOP' if stopped else f'STOCK_KEYBOARD_OBSERVER tick={tick}'
    return '[X4LAN] NATIVE_GATE ' + tag + ' ' + ' '.join(f'{name}={value}' for name, value in fields.items())


def event(tick=2, thread=91, caller='1051041', kind=1, identity=8, state=1):
    return (f'[X4LAN] NATIVE_GATE STOCK_KEYBOARD_EVENT tick={tick} thread={thread} '
            f'caller={caller} kind={kind} id={identity} state={state}')


def raw_log(slot, ticks=4):
    rows = [status(tick) for tick in range(1, ticks + 1)]
    # These raw limits are deliberately retained in the passing fixture.
    rows.insert(1, '[X4LAN] NATIVE_GATE ERROR DEGRADED status=FIND_ORDER unmapped=14 (+14) tick=1')
    rows.insert(2, '[X4LAN] NATIVE_GATE ERROR DEGRADED status=CREATED_IDENTITIES macro_unknown=1 (+1) tick=1')
    rows += [f'[X4LAN] NATIVE_GATE STOP slot=p{slot} completed={ticks} reason=1 restored=1 wait_game_delta=0 max_dt_error=0',
             status(ticks, stopped=True)]
    return '\n'.join(rows) + '\n'


def receipt(ticks=4):
    identity = {'pid': 90, 'start': 20, 'uid': 1000}
    return {'pass': True, 'ticks': ticks, 'local_slots': [1, 2], 'armed': [True, True],
            'owned_processes_remaining': {}, 'steam_unchanged': True, 'x4_left': [],
            'evidence_ok': True, 'evidence_errors': [], 'steam_before': identity,
            'steam_after': dict(identity),
            'stops': [{'slot': slot, 'completed': ticks, 'reason': 1, 'restored': 1} for slot in (1, 2)]}


def manifest(events):
    return {'schema': 1, 'module_base': '0x400000', 'events': {'1': events, '2': []}}


class StockKeyboardEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.logs = {slot: raw_log(slot) for slot in (1, 2)}
        self.result = receipt()

    def validate(self, expected=None):
        return evidence.validate_logs(self.logs, self.result, 4, expected)

    def test_idle_keeps_degraded_limits(self):
        report = self.validate()
        self.assertTrue(report['accepted'], report['errors'])
        self.assertEqual(len(report['raw_nonfatal_limits']), 4)
        self.assertIn('macro_unknown=1', report['raw_nonfatal_limits'][1]['raw'])
        self.assertEqual(report['mode'], 'idle_observation')
        self.assertIn('no input replay', report['claim_limit'])

    def test_absent_log_or_slot(self):
        for logs in ({1: self.logs[1]}, {1: '', 2: self.logs[2]}, {True: self.logs[1], 2: self.logs[2]}):
            with self.subTest(logs=list(logs)):
                self.assertFalse(evidence.validate_logs(logs, self.result, 4)['accepted'])

    def test_missing_duplicate_or_reordered_ticks(self):
        original = self.logs[1]
        for changed in (original.replace(status(2) + '\n', ''),
                        original.replace(status(2), status(2) + '\n' + status(2)),
                        original.replace(status(2), status(3)).replace(status(4), status(2)),
                        original + status(5) + '\n'):
            with self.subTest(changed=changed):
                self.logs[1] = changed
                self.assertFalse(self.validate()['accepted'])
        self.logs[1] = original

    def test_every_required_status_number_and_text(self):
        original = self.logs[1]
        for name in evidence.STATUS_NUMBERS:
            desired = 0 if name in {'requested', 'installed_tick', 'input_tick', 'active', 'owned'} else 1
            self.logs[1] = original.replace(status(2), status(2, **{name: desired}))
            with self.subTest(name=name):
                self.assertFalse(self.validate()['accepted'])
        for name in evidence.STATUS_TEXT:
            self.logs[1] = original.replace(status(2), status(2, **{name: 'wrong'}))
            with self.subTest(name=name):
                self.assertFalse(self.validate()['accepted'])

    def test_tick1_must_be_requested_and_uninstalled(self):
        original = self.logs[1]
        for changes in ({'requested': 0}, {'installed_tick': 1}, {'active': 1}, {'input_tick': 1}):
            self.logs[1] = original.replace(status(1), status(1, **changes))
            self.assertFalse(self.validate()['accepted'])

    def test_wrong_native_input_phase_is_not_normalized(self):
        self.logs[1] = self.logs[1].replace(status(3), status(3, input_tick=2))
        report = self.validate()
        self.assertFalse(report['accepted'])
        self.assertTrue(any('input_tick=2, expected 3' in error for error in report['errors']))

    def test_missing_duplicate_unknown_and_noncanonical_fields(self):
        original = self.logs[1]
        for row in (status(2).replace(' owned=32767', ''), status(2) + ' active=1',
                    status(2) + ' extra=0', status(2).replace('active=1', 'active=01'),
                    status(2).replace('active=1', 'active=true'),
                    status(2).replace('[X4LAN] NATIVE_GATE ', ''),
                    status(2).replace('STOCK_KEYBOARD_OBSERVER', 'STOCK_KEYBOARD_UNKNOWN')):
            self.logs[1] = original.replace(status(2), row)
            with self.subTest(row=row):
                self.assertFalse(self.validate()['accepted'])

    def test_observer_stop_proofs(self):
        original = self.logs[1]
        stop = status(4, stopped=True)
        for changed in ('', stop + '\n' + stop, status(4, stopped=True, owned=1),
                        status(4, stopped=True, cleaned=0), status(4, stopped=True, permission_pending=1),
                        status(4, stopped=True, input_tick=3), status(4, stopped=True, queued=1)):
            self.logs[1] = original.replace(stop, changed)
            self.assertFalse(self.validate()['accepted'])

    def test_generic_stop_and_cleanup_order(self):
        original = self.logs[1]
        gate = '[X4LAN] NATIVE_GATE STOP slot=p1 completed=4 reason=1 restored=1 wait_game_delta=0 max_dt_error=0'
        for changed in (original.replace(gate, ''), original.replace(gate, gate + '\n' + gate),
                        original.replace('STOP slot=p1', 'STOP slot=p2'),
                        original.replace('completed=4 reason=1', 'completed=3 reason=1'),
                        original.replace('reason=1 restored=1', 'reason=3 restored=1'),
                        original.replace('reason=1 restored=1', 'reason=1 restored=0'),
                        original.replace(gate + '\n' + status(4, stopped=True), status(4, stopped=True) + '\n' + gate)):
            self.logs[1] = changed
            self.assertFalse(self.validate()['accepted'])

    def test_fatal_errors_anywhere_and_nonfatal_limits(self):
        for row in ('[X4LAN] NATIVE_GATE ERROR observer failure', '[X4LAN] NATIVE_GATE REFUSED diagnostic'):
            self.logs[1] = raw_log(1) + row
            self.assertFalse(self.validate()['accepted'])
        self.logs[1] = raw_log(1) + '[X4LAN] NATIVE_GATE ERROR DEFAULT_SKIPPED optional=off\n'
        report = self.validate()
        self.assertTrue(report['accepted'], report['errors'])
        self.assertEqual(len(report['raw_nonfatal_limits']), 5)

    def test_receipt_cannot_replace_raw_proof(self):
        self.logs[1] = ''
        self.assertTrue(self.result['pass'])
        self.assertFalse(self.validate()['accepted'])

    def test_receipt_errors_absent_cleanup_and_boolean_aliases(self):
        for name, value in (('pass', False), ('ticks', True), ('local_slots', [True, 2]),
                            ('local_slots', 1), ('armed', [1, 1]), ('owned_processes_remaining', {'5': [5]}),
                            ('x4_left', [9]), ('steam_unchanged', False), ('evidence_errors', ['bad']),
                            ('cleanup_errors', ['leftover']), ('error', 'failed'),
                            ('postprocessing_error', 'failed'), ('stops', []), ('steam_after', {'pid': 1})):
            self.result = receipt()
            self.result[name] = value
            with self.subTest(name=name, value=value):
                self.assertFalse(self.validate()['accepted'])
        for name in ('owned_processes_remaining', 'x4_left', 'armed', 'steam_before', 'evidence_ok'):
            self.result = receipt()
            del self.result[name]
            self.assertFalse(self.validate()['accepted'])

    def test_nonzero_requires_explicit_exact_manifest(self):
        self.logs[1] = self.logs[1].replace(status(2), event() + '\n' + status(2))
        self.assertFalse(self.validate()['accepted'])
        expected = manifest([{'tick': 2, 'kind': 1, 'id': 8, 'state': 1, 'caller_rva': '0xc51041'}])
        report = self.validate(expected)
        self.assertTrue(report['accepted'], report['errors'])
        self.assertEqual(report['mode'], 'specified_event_observation')
        for name, value in (('tick', 3), ('kind', 0), ('id', 9), ('state', 0), ('caller_rva', '0xc51042')):
            changed = copy.deepcopy(expected)
            changed['events']['1'][0][name] = value
            self.assertFalse(self.validate(changed)['accepted'])

    def test_event_order_duplicates_and_thread(self):
        events = [event(state=1), event(state=0)]
        expected = manifest([{'tick': 2, 'kind': 1, 'id': 8, 'state': state, 'caller_rva': '0xc51041'} for state in (1, 0)])
        original = self.logs[1]
        self.logs[1] = original.replace(status(2), '\n'.join(events + [status(2)]))
        self.assertTrue(self.validate(expected)['accepted'])
        for rows in (events[::-1], events + [events[0]], [event(thread=0), events[1]],
                     [events[0], event(thread=92, state=0)], [event(caller='0x1051041'), events[1]],
                     [event(caller='400000'), events[1]], [event(identity=511), events[1]]):
            self.logs[1] = original.replace(status(2), '\n'.join(rows + [status(2)]))
            self.assertFalse(self.validate(expected)['accepted'])
        self.logs[1] = original.replace(status(2), status(2) + '\n' + event())
        self.assertFalse(self.validate(expected)['accepted'])

    def test_manifest_incomplete_schema_and_types(self):
        for expected in ({}, {'schema': 1, 'module_base': '0x400000', 'events': {'1': []}},
                         {'schema': True, 'module_base': '0x400000', 'events': {'1': [], '2': []}},
                         {'schema': 1, 'module_base': '0x0', 'events': {'1': [], '2': []}},
                         manifest([{'tick': True, 'kind': 1, 'id': 8, 'state': 1, 'caller_rva': '0xc51041'}])):
            self.assertFalse(self.validate(expected)['accepted'])

    def test_strict_json(self):
        for text in ('{"ticks":4,"ticks":4}', '{"pass":NaN}'):
            with self.assertRaises(ValueError):
                evidence.parse_json(text)

    def test_cli_reads_raw_bytes_and_rejects_missing_log(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for slot in (1, 2):
                (directory / f'game{slot}.log').write_text(self.logs[slot])
            result_path = directory / 'run-result.json'
            result_path.write_text(json.dumps(self.result))
            command = [sys.executable, str(ROOT / 'tools/dev/linux_stock_keyboard_evidence.py'), '--ticks', '4',
                       '--log1', str(directory / 'game1.log'), '--log2', str(directory / 'game2.log'),
                       '--result', str(result_path)]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads(completed.stdout)
            self.assertEqual(len(report['source_sha256']), 3)
            (directory / 'game2.log').unlink()
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 1)
            self.assertFalse(json.loads(completed.stdout)['accepted'])


if __name__ == '__main__':
    unittest.main()
