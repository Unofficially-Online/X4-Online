"""Engine-free strong-reference transaction regressions."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/native/engine/linux'


class PlayerZoneLedgerTests(unittest.TestCase):
    def test_ledger(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'ledger'
            build = subprocess.run(
                ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 str(LINUX / 'test_player_zone_ledger.c'), '-o', str(executable)],
                capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('6 scenario groups passed', run.stdout)
