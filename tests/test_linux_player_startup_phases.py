"""The optional cross-platform phase contract is engine-free and fail-closed."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class PlayerStartupPhaseTests(unittest.TestCase):
    def test_common_environment_is_sealed_before_use(self):
        root = Path(__file__).resolve().parents[1]
        include = root / 'src/native/engine/linux'
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'admission.c'
            executable = Path(directory) / 'admission'
            source.write_text('#include "gate_player_startup_phases.h"\n'
                              'int main(void) { return player_startup()->version != 1; }\n')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-I', str(include), str(source), '-o', str(executable)], check=True)
            env = os.environ.copy()
            env.update(X4_DEV_PLAYER_PHASES='v1-t40', X4_DEV_UI_SESSION_FRAMES='900',
                       X4_DEV_PLAYER_PHYSICS='network-frames-discard',
                       X4LAN_PLAYER_CONTROL_RECURRING='1', X4LAN_PLAYER_CONTROL_RECURRING_TAIL='1',
                       X4LAN_PLAYER_CONTROL_RECURRING_END='939')
            env.pop('X4_DEV_P2_THROTTLE', None)
            self.assertEqual(subprocess.run([str(executable)], env=env, capture_output=True).returncode, 0)
            for name, value in [('X4LAN_PLAYER_CONTROL_RECURRING_END', '904'),
                                ('X4_DEV_UI_SESSION_FRAMES', 'invalid'),
                                ('X4_DEV_P2_THROTTLE', 'state13-discard'),
                                ('X4_DEV_PLAYER_PHYSICS', 'single-frame-discard')]:
                invalid = env.copy()
                invalid[name] = value
                result = subprocess.run([str(executable)], env=invalid, capture_output=True)
                self.assertEqual(result.returncode, 222, name)
                self.assertIn(b'PLAYER_PHASES_FATAL', result.stderr)

    def test_legacy_and_common_ordering(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_startup_phases.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'startup-phases'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)
