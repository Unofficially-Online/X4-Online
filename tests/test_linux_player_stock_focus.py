"""Check focus publication without opening a window or running X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PlayerStockFocusTests(unittest.TestCase):
    def test_generation_window_identity_and_main_thread_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "focus-test"
            subprocess.run([
                "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                str(ROOT / "src/native/engine/linux/test_player_stock_focus.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
