"""Exercise the engine-free published pose pointer lease."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class PublishedPoseTest(unittest.TestCase):
    def test_pointer_transaction(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_ui_published_pose.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'published-pose'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
