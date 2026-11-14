"""Engine-free SPACE queue admission and delayed two-slot semantic checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxStockKeyboardSpaceTest(unittest.TestCase):
    def test_context_edges_inactive_cursor_and_complete_pair_validation(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_stock_keyboard_space.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'space-queue'
            subprocess.run(['cc', '-UNDEBUG', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-pthread', '-lm', '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)
