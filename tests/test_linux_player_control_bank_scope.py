"""Exercise the unused native bank adapter with a fake mapped image."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_gate_player_control_bank_scope.c'


class PlayerControlBankScopeTests(unittest.TestCase):
    def test_fake_native_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'bank-scope'
            build = subprocess.run(
                ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 str(SOURCE), '-o', str(executable)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('6 bank adapter scenario groups passed', run.stdout)
