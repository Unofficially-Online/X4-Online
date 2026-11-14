"""Pure synthetic SPACE vector tests; no game lookup, hook or activation."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxSpaceContextVectorTest(unittest.TestCase):
    def test_bounded_unique_fresh_lookup_cas_and_ownership(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_space_context_vector.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'space-vector'
            subprocess.run(['cc', '-UNDEBUG', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)
