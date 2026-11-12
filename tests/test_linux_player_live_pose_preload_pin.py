"""Authenticate a synthetic resident live-pose provider without launching X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class LivePoseProviderPinTest(unittest.TestCase):
    def test_authentication_and_tampering(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_live_pose_preload_pin.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'live-pose-pin'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            self.assertIn('live pose provider pin PASS', result.stdout)
