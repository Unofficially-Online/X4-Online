"""Run the engine-free native P2 network adapter fixture."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine'


@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class PlayerPhysicsNetworkTest(unittest.TestCase):
    def test_network_scope_and_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'network-test'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ENGINE), '-I', str(ENGINE / 'linux'),
                            str(ENGINE / 'linux/test_player_physics_network.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
