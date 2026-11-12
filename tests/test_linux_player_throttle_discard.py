"""Synthetic tests for the default-off native P2 throttle diagnostic."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine'


@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class PlayerThrottleDiscardTests(unittest.TestCase):
    def test_scripted_sequence_and_network_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('test_player_throttle_discard', 'test_player_physics_network'):
                with self.subTest(fixture=name):
                    executable = Path(directory) / name
                    subprocess.run([
                        'cc', '-UNDEBUG', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Wno-clobbered', '-Wno-unused-function', '-I', str(ENGINE),
                        '-I', str(ENGINE / 'linux'), str(ENGINE / 'linux' / (name + '.c')),
                        '-o', str(executable),
                    ], check=True, timeout=30)
                    subprocess.run([str(executable)], check=True, timeout=30)
