"""Exercise presentation ownership without starting the game."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class PresentationProbeTest(unittest.TestCase):
    def test_reference_cleanup_and_refusal(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_presentation_probe.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'presentation-probe'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
            self.assertIn('presentation probe: ok', result.stdout)
