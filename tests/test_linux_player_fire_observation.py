"""Engine-free FIRE observation and hash-bound retained-receipt validation."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

import linux_player_fire_observation as observation

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'src/native/engine/linux'


class PlayerFireObservationTests(unittest.TestCase):
    def options(self):
        return dict(player_fire_observation=True, player_startup_phases='v1-t40',
                    player_ui_session_frames=180, independent_keyboard_frames=180)

    def test_default_off_explicit_finite_options_environment_and_closure(self):
        self.assertFalse(observation.enabled({}))
        self.assertTrue(observation.enabled(self.options()))
        for key, value in [('player_fire_observation', 1), ('player_startup_phases', None),
                           ('player_ui_session_frames', 179), ('player_ui_session_frames', 901),
                           ('independent_keyboard_frames', 179)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                observation.enabled(dict(self.options(), **{key: value}))
        environment = {'X4_DEV_FIRE_DIAG': '1', 'OTHER': 'keep'}
        self.assertEqual(observation.environment(environment, False), {'OTHER': 'keep'})
        self.assertEqual(observation.environment(environment, True), environment)
        self.assertEqual(len(observation.source_paths(ROOT)), len(observation.SOURCES))
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
            observation.source_paths(directory)

    def test_group_reader_and_observation_only_native_fixture(self):
        with tempfile.TemporaryDirectory() as directory:
            for source in ('test_player_weapon_groups', 'test_gate_player_fire_observation'):
                binary = Path(directory) / source
                subprocess.run(['cc', '-UNDEBUG', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(NATIVE / (source + '.c')), '-o', str(binary)], check=True, timeout=30)
                result = subprocess.run([str(binary)], check=True, timeout=10, capture_output=True, text=True)
                if source != 'test_gate_player_fire_observation':
                    continue
                receipts = [line for line in result.stderr.splitlines() if line.startswith(observation.PREFIX)]
                self.assertEqual(len(receipts), 4)
                self.assertTrue(all(' v=2 stage=observe ' in line and ' mutation=0 ' in line for line in receipts))
                self.assertIn('firing=1 raw91=7 raw91_present=1 mutation=0 outcome=stock_player_observed', receipts[0])
                self.assertIn('controller=0 vtable=0 firing=0 raw91=0 raw91_present=0 mutation=0 outcome=empty_observed', receipts[1])
                self.assertIn('weapons=0 gun=0 id=0 controller=0 vtable=0 firing=0 raw91=0 raw91_present=0 mutation=0 outcome=no_gun', receipts[2])
                self.assertIn('outcome=incompatible_owner', receipts[3])
                self.assertEqual(result.stderr.count('FIRE_OBSERVATION_TEST refusal=fire_observation_drift'), 6)
                self.assertFalse(any('stage=start' in line or 'stage=release' in line for line in receipts))

    def fixture(self, directory):
        stderr, native = Path(directory) / 'stderr', Path(directory) / 'native'
        rows = [
            'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=40 tail=1 event=enter\n',
            'X4LAN FIRE_DIAG v=2 stage=observe tick=40 worker=4213747709 ship=20000 group=0 ids=1 weapons=1 '
            'gun=30000 id=7b controller=40000 vtable=3b2fc38 firing=1 raw91=7 raw91_present=1 mutation=0 outcome=stock_player_observed\n',
            'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=40 tail=1 event=release\n',
        ]
        native.write_text(observation.FINAL)
        physics = {'pass': True, 'frames': [dict(tick=tick, worker_thread=4213747709, bodies=[{'ship': 0x20000}])
                                           for tick in range(40, 220)]}

        def proof():
            stderr.write_text(''.join(rows))
            physics.update(stderr_sha256=hashlib.sha256(stderr.read_bytes()).hexdigest(),
                           log_sha256=hashlib.sha256(native.read_bytes()).hexdigest())
            return physics
        return stderr, native, rows, proof

    def test_exact_raw_values_hashes_window_and_worker_ship_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr, native, rows, proof = self.fixture(directory)
            result = observation.collect(stderr, native, True, proof(), self.options())
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['raw91'], 7)
            self.assertEqual(result['firing'], 1)
            self.assertEqual(result['raw_receipts'], [rows[1]])
            self.assertFalse(result['fire_replay_supported'])
            self.assertFalse(result['lifetime_exclusion_proven'])
            original = rows[:]
            mutations = [('tick=40 worker=', 'tick=41 worker='), ('worker=4213747709', 'worker=12'),
                         ('ship=20000', 'ship=20008'), ('group=0', 'group=64'), ('ids=1', 'ids=513'),
                         ('weapons=1', 'weapons=17'), ('firing=1', 'firing=2'), ('raw91=7', 'raw91=256'),
                         ('raw91_present=1', 'raw91_present=0'), ('vtable=3b2fc38', 'vtable=123'),
                         ('mutation=0', 'mutation=1'), ('outcome=stock_player_observed', 'outcome=incompatible_owner')]
            for before, after in mutations:
                rows[:] = [line.replace(before, after) for line in original]
                with self.subTest(before=before):
                    result = observation.collect(stderr, native, True, proof(), self.options())
                    self.assertFalse(result['pass'])
                    self.assertEqual(result['raw_receipts'], [rows[1]])
            rows[:] = original
            accepted = proof()
            self.assertFalse(observation.collect(stderr, native, True, dict(accepted, stderr_sha256='0' * 64), self.options())['pass'])
            self.assertFalse(observation.collect(stderr, native, True, dict(accepted, log_sha256='0' * 64), self.options())['pass'])
            self.assertFalse(observation.collect(stderr, native, True, dict(accepted, **{'pass': False}), self.options())['pass'])
            self.assertFalse(observation.collect(stderr, native, True, dict(accepted, frames=accepted['frames'][:-1]), self.options())['pass'])
            self.assertFalse(observation.collect(stderr, native, False, accepted, self.options())['pass'])
            rows[:] = [original[1], original[0], original[2]]
            self.assertFalse(observation.collect(stderr, native, True, proof(), self.options())['pass'])
            rows[:] = original + [original[1]]
            self.assertFalse(observation.collect(stderr, native, True, proof(), self.options())['pass'])
            rows[:] = original
            native.write_text('unaccepted terminal\n')
            self.assertFalse(observation.collect(stderr, native, True, proof(), self.options())['pass'])

    def test_empty_and_no_gun_are_explicit_without_fabricated_controller_data(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr, native, rows, proof = self.fixture(directory)
            rows[1] = rows[1].replace('controller=40000 vtable=3b2fc38 firing=1 raw91=7 raw91_present=1',
                                      'controller=0 vtable=0 firing=1 raw91=0 raw91_present=0')
            rows[1] = rows[1].replace('outcome=stock_player_observed', 'outcome=empty_observed')
            self.assertTrue(observation.collect(stderr, native, True, proof(), self.options())['pass'])
            rows[1] = rows[1].replace('weapons=1 gun=30000 id=7b', 'weapons=0 gun=0 id=0')
            rows[1] = rows[1].replace('firing=1', 'firing=0').replace('outcome=empty_observed', 'outcome=no_gun')
            self.assertTrue(observation.collect(stderr, native, True, proof(), self.options())['pass'])
            rows[1] = rows[1].replace('gun=0', 'gun=30000')
            self.assertFalse(observation.collect(stderr, native, True, proof(), self.options())['pass'])

    def test_disabled_no_native_trace_needed(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr = Path(directory) / 'stderr'
            stderr.write_text('ordinary output\n')
            self.assertTrue(observation.collect(stderr, Path(directory) / 'missing', False, {}, {})['pass'])
