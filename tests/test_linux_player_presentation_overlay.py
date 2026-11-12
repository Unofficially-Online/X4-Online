"""Engine-free presentation overlay transaction tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class PresentationOverlayTest(unittest.TestCase):
    def test_transactions(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_presentation_overlay.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'presentation-overlay'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
            self.assertIn('presentation overlay transactions and refusal PASS', result.stdout)
