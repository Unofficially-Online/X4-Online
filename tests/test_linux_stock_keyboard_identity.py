"""Engine-free checks for the read-only canonical keyboard identity sample."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/native/engine/linux'


class StockKeyboardIdentityTests(unittest.TestCase):
    def test_read_failures_identity_context_and_lua_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'identity'
            subprocess.run(
                ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 str(LINUX / 'test_stock_keyboard_identity.c'), '-lm', '-o', str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)
