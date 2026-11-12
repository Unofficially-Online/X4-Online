"""Engine-free full-context policy; no hooks or game required."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class PlayerContextPolicyTest(unittest.TestCase):
    def test_policy(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_context_policy.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'context-policy'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            self.assertIn('player context policy: ok', result.stdout)
