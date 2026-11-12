"""Engine-free seat transform math; does not launch the game."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class SeatTransformTest(unittest.TestCase):
    def test_transform_and_invalid_inputs(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_ui_seat_transform.c'
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'seat-transform'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-lm', '-o', str(output)], check=True, timeout=120)
            subprocess.run([str(output)], check=True, timeout=10)
