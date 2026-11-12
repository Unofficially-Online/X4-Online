"""Engine-free strict camera context anchor checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/native/engine/linux'

@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class CameraContextAnchorTest(unittest.TestCase):
    def test_transition_and_exact_anchor(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'camera-context-anchor'
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-I',str(LINUX),
                            str(LINUX/'test_player_camera_context_anchor.c'),'-o',str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
