import subprocess
import tempfile
import unittest
from pathlib import Path


class PlayerSpaceReplayTests(unittest.TestCase):
    def test_owner_registry_capture_order_and_cf0_timing(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_gate_player_space_replay.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'space-replay'
            subprocess.run(['cc', '-std=c11', '-UNDEBUG', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
