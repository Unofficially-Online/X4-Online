"""Atomic VIEW capture and delayed pair validation; no native game claim."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxStockKeyboardViewTest(unittest.TestCase):
    def test_atomic_capture_echo_and_pair_admission(self):
        source = Path(__file__).resolve().parents[1] / "src/native/engine/linux/test_stock_keyboard_view.c"
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "view-queue"
            subprocess.run(
                ["cc", "-UNDEBUG", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(source), "-pthread", "-lm", "-o", str(binary)],
                check=True, timeout=30,
            )
            subprocess.run([str(binary)], check=True, timeout=10)
