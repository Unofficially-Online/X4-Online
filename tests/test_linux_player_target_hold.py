"""Unused native target/Cockpit reference ledger, compile-time fake image only."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_player_target_hold.c'
SCENARIOS = (
    'unused', 'success', 'nested', 'wrong_boundary', 'pins', 'unsealed', 'duplicate_saved',
    'missing_target', 'wrong_ship', 'wrong_runtime', 'wrong_room', 'room_runtime_zero',
    'wrong_parent', 'wrong_ship_reference', 'wrong_room_reference', 'wrong_ship_dispatch',
    'wrong_room_dispatch', 'wrong_room_predicate', 'zero_count', 'destroyed_marker',
    'room_zero_count', 'room_destroyed_marker', 'room_gated', 'unreadable', 'count_overflow',
    'room_count_overflow', 'initial_identity_change', 'unaligned_room', 'initial_refusal_retry',
    'acquire_target_unreadable', 'acquire_room_unreadable', 'acquire_target_bad_delta',
    'acquire_room_bad_delta', 'acquire_identity_changed', 'acquire_parent_changed',
    'acquire_pin_loss', 'acquire_boundary_loss', 'acquire_worker_change',
    'partial_acquisition', 'wrong_thread', 'foreign_reference_slot', 'live_room_replaced',
    'last_room_reference', 'release_room_unreadable', 'release_target_unreadable',
    'release_room_bad_delta', 'release_target_bad_delta', 'release_parent_changed',
    'release_pin_loss', 'release_boundary_loss', 'release_worker_change', 'release_identity_changed',
    'partial_cleanup', 'last_target_reference',
)


class PlayerTargetHoldTests(unittest.TestCase):
    def test_owned_reference_transitions(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'target-hold'
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-o', str(executable)], cwd=ROOT, check=True)
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], cwd=ROOT,
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
