"""Read-only P2 seat preflight fixture; no native process or mutation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_player_seat_preflight.c'
SCENARIOS = (
    'off', 'request', 'request_wrong_worker', 'request_wrong_tick', 'request_owner_mismatch', 'observe',
    'context_missing', 'context_invalid', 'context_unreadable', 'owner_change_before_collect',
    'inflight', 'singleton_uncertain',
    'room_plain', 'room_gated', 'room_unknown', 'room_cycle',
    'cockpit', 'cockpit_gated', 'live_cockpit_navigation', 'controller_navigation',
    'navigation_unexpected_next', 'navigation_unexpected_predicate',
    'controller_unknown', 'controller_player', 'animation_handles', 'context_disabled',
    'large_post_index', 'p2_attached', 'pins', 'joined_boundary', 'lost_hold',
    'post_short', 'post_index_outside', 'post_empty', 'post_capacity_before_end',
    'post_begin_after_end', 'post_stride', 'post_unaligned', 'post_null_partial',
    'post_unreadable', 'post_header_unreadable', 'post_record_change', 'post_vector_change',
    'uncertain_hold', 'release_pending', 'wrong_phase', 'missed_tick',
    'target_missing', 'target_duplicate', 'target_wrong_vtable', 'target_wrong_runtime',
    'target_change', 'canonical_change', 'p2_field_change', 'p2_unreadable', 'vector_invalid',
)


class PlayerSeatPreflightTests(unittest.TestCase):
    def test_read_only_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'seat-preflight'
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-o', str(executable)], cwd=ROOT, check=True)
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], cwd=ROOT,
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
