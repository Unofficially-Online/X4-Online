"""Camera identity, one-shot admission and native-call refusal fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine'


@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class PlayerCameraProbeTests(unittest.TestCase):
    def test_camera_contracts(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('test_player_camera_probe', 'test_gate_player_camera_probe',
                         'test_gate_player_camera_method'):
                with self.subTest(fixture=name):
                    binary = Path(directory) / name
                    subprocess.run([
                        'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ENGINE), '-I', str(ENGINE / 'linux'),
                        str(ENGINE / 'linux' / (name + '.c')), '-o', str(binary),
                    ], check=True)
                    subprocess.run([str(binary)], check=True)

    def test_camera_id_route_and_thread_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            third_party = ROOT / 'third_party/monocypher-4.0.2/src'
            binary = Path(directory) / 'camera-id-scope'
            subprocess.run([
                'cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                '-Wno-misleading-indentation', '-mcx16', '-ffp-contract=off',
                '-fasynchronous-unwind-tables', '-I', str(ENGINE), '-I', str(ENGINE / 'linux'),
                '-I', str(third_party), '-I', str(third_party / 'optional'),
                str(ENGINE / 'linux/test_gate_camera_id_scope.c'), str(third_party / 'monocypher.c'),
                str(third_party / 'optional/monocypher-ed25519.c'), '-ldl', '-lpthread', '-lm', '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
