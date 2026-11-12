"""Exercise the dormant camera refill dispatch without starting the game."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / "src/native/engine/linux"

class CameraRefillPreloadTest(unittest.TestCase):
    def test_forward_and_close(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            flags = ["-O2", "-Wall", "-Wextra", "-Werror"]
            guard = output / "guard.o"
            fixture = output / "fixture.o"
            subprocess.run(["c++", "-std=c++17", *flags, "-c", str(LINUX / "player_ui_escape_guard.cpp"), "-o", str(guard)], check=True)
            subprocess.run(["cc", "-std=c11", *flags, "-Wno-unused-function", "-c", str(LINUX / "test_player_camera_refill_preload.c"), "-o", str(fixture)], check=True)
            binary = output / "test"
            subprocess.run(["c++", str(fixture), str(guard), "-ldl", "-pthread", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
