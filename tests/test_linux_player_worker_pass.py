"""Engine-free pass provenance policy; no hooks or game required."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class WorkerPassTest(unittest.TestCase):
    def test_policy(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_worker_pass.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'worker-pass'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            self.assertIn('worker pass policy: ok', result.stdout)
