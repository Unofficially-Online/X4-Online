"""Engine-free candidate seat-pose checks; no game is started."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class SeatPoseTest(unittest.TestCase):
    def test_bounded_candidate_reads(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_ui_seat_pose.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'seat-pose-test'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-lm', '-o', str(executable)], check=True, timeout=120)
            subprocess.run([str(executable)], check=True, timeout=10)
