"""Build and run focused synthetic Linux physics helpers, never X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine/linux'


class PlayerPhysicsNativeFixtureTests(unittest.TestCase):
    def run_fixture(self, source, expected):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'fixture'
            build = subprocess.run(
                ['cc', '-UNDEBUG', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                 str(ENGINE / source), '-lm', '-ldl', '-o', str(executable)],
                cwd=ROOT, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(expected, result.stdout)

    def test_presentation_method(self):
        self.run_fixture('test_gate_player_presentation_method.c',
                         'presentation native adapter admission and ownership PASS')

    def test_pause_receipt(self):
        self.run_fixture('test_player_recurring_pause.c',
                         'pause receipt uses nonblocking mutex and reports unavailable PASS')

    def test_context_policy_adapter(self):
        self.run_fixture('test_player_context_policy_adapter.c',
                         'context policy adapter snapshots, rebase and scope checks PASS')

    def test_recurring_control_boundary(self):
        self.run_fixture('test_gate_player_control_recurring.c', 'recurring boundary adapter: ok')

    def test_scripted_physics_replay(self):
        self.run_fixture('test_gate_player_physics_replay.c', 'player physics replay scripted scopes and refusal: ok')

    def test_checked_body_observations(self):
        self.run_fixture('test_player_body_observation.c', 'target Jolt bodies: snapshots, missing data and link rechecks PASS')

    def test_context_failure_diagnostics(self):
        self.run_fixture('test_player_context_diagnostic.c',
                         'failure context diagnostics retain bounded complete and partial payloads PASS')

    def test_fatal_callback_diagnostics(self):
        self.run_fixture('test_player_callback_diagnostic.c',
                         'callback diagnostics preserve raw arguments and fatal refusal PASS')

    def test_controller_routes(self):
        self.run_fixture('test_player_controller_routes.c', 'synthetic P2 routing ABI, snapshots, canonical and restore PASS')

    def test_native_motion_adapters(self):
        self.run_fixture('test_player_controller_native_motion.c', 'ELF prologue branch adapters SysV ABI PASS')

    def test_route_comparison(self):
        self.run_fixture('test_player_route_compare.c', 'synthetic getter comparison and output canaries PASS')

    def test_getter_preload_pin(self):
        self.run_fixture('test_player_getter_preload_pin.c', 'four-window transform getter original-view pin PASS')
