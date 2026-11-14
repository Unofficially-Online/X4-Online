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
    'target_duplicate_different_runtime', 'target_count_invalid', 'target_population_mismatch',
    'target_count_zero', 'target_count_negative', 'target_restore_started', 'cache_restore_started',
    'target_generation_during_find',
    'target_zero_runtime', 'cache_fresh_object', 'cache_generation', 'cache_unsealed',
    'cache_replaced_table', 'cache_count', 'cache_failures', 'cache_restored',
    'cache_entry_drift', 'cache_saved_drift', 'cache_exhausted',
    'target_change', 'canonical_change', 'p2_field_change', 'p2_unreadable', 'vector_invalid',
)


class PlayerSeatPreflightTests(unittest.TestCase):
    def test_read_only_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'seat-preflight'
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-pthread', '-o', str(executable)], cwd=ROOT, check=True)
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], cwd=ROOT,
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_import_sealing_and_generation(self):
        """Exercise the production import hooks/seal, independently of fake registry state."""
        engine = ROOT / 'src/native/engine'
        third = ROOT / 'third_party/monocypher-4.0.2/src'
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'world-seal.c'
            source.write_text(
                '#define main original_gate_fixture_main\n'
                '#include "test_gate_linux.c"\n'
                '#undef main\n'
                'int main(void) {\n'
                '  synth_image(); test_world_hook();\n'
                '  pthread_mutex_lock(&lxw_insert_lock);\n'
                '  world_identity_seal_generation = UINT64_MAX;\n'
                '  assert(!world_identity_advance_locked());\n'
                '  assert(world_identity_seal_generation_exhausted);\n'
                '  assert(!world_identity_seal_generation && !world_identity_seal_admitted);\n'
                '  assert(!world_identity_advance_locked());\n'
                '  pthread_mutex_unlock(&lxw_insert_lock);\n'
                '  return 0;\n'
                '}\n')
            executable = Path(directory) / 'world-seal'
            subprocess.run([
                'cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                '-Wno-misleading-indentation', '-mcx16', '-ffp-contract=off',
                '-fasynchronous-unwind-tables', '-I', str(engine), '-I', str(engine / 'linux'),
                '-I', str(third), '-I', str(third / 'optional'), str(source),
                str(third / 'monocypher.c'), str(third / 'optional/monocypher-ed25519.c'),
                '-ldl', '-pthread', '-lm', '-o', str(executable)], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)
