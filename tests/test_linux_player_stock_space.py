"""Check SPACE replay ordering and the local/simulation ownership boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PlayerStockSpaceTests(unittest.TestCase):
    def test_ordered_handoff_and_local_state_separation(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "space-test"
            subprocess.run([
                "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                str(ROOT / "src/native/engine/linux/test_player_stock_space.c"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True, timeout=10)
