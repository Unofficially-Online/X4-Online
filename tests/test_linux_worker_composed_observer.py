"""Bounded composed observer, source receipt and default-off behavior."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ComposedObserverTest(unittest.TestCase):
    def test_fixture_and_pinned_closure(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            fixture = directory / 'fixture'
            subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-mcx16', '-I', str(ROOT / 'src/native/engine'), '-I', str(ROOT / 'tools/dev'),
                            str(ROOT / 'src/native/engine/linux/test_linux_worker_composed_observer.c'),
                            '-o', str(fixture), '-pthread'], check=True)
            subprocess.run([str(fixture)], check=True)
            gate = directory / 'gate.so'; gate.write_bytes(b'gate')
            bundle = directory / 'bundle'
            subprocess.run(['python3', str(ROOT / 'tools/dev/build_linux_worker_composed_observer.py'),
                            '--out', str(bundle), '--gate', str(gate)], check=True, capture_output=True)
            manifest = json.loads((bundle / 'manifest.json').read_text())
            self.assertEqual(manifest['gate_sha256'], hashlib.sha256(b'gate').hexdigest())
            for name, expected in manifest['sources'].items():
                self.assertEqual(hashlib.sha256((bundle / 'source-receipts' / name).read_bytes()).hexdigest(), expected)
            source = directory / 'dummy.c'; source.write_text('int main(void) { return 0; }\n')
            binary = directory / 'dummy'; subprocess.run(['cc', str(source), '-o', str(binary)], check=True)
            env = dict(os.environ, LD_PRELOAD=str(bundle / 'worker_wait_observer.so'))
            env.pop('X4_DEV_WORKER_WAIT_OBSERVE', None)
            self.assertEqual(subprocess.run([str(binary)], env=env, capture_output=True, check=True).stderr, b'')
            env['X4_DEV_WORKER_WAIT_OBSERVE'] = '1'
            refused = subprocess.run([str(binary)], env=env, capture_output=True)
            self.assertEqual(refused.returncode, 224)

if __name__ == '__main__':
    unittest.main()
