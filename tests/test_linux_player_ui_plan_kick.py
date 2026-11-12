from pathlib import Path
import subprocess
import tempfile
import unittest


class PlayerUIPlanKickTest(unittest.TestCase):
    def test_sequence_and_failure_restore_rng(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_ui_plan_kick.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'ui-plan-kick-test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
