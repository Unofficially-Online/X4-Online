"""Compare both native SHA implementations with Python's hashlib, without X4."""
import hashlib
import os
import platform
from pathlib import Path
import random
import shutil
import sys
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which(os.environ.get("CC", "cc")),
                     "native C compiler on Linux required")
class Sha256Differential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.driver = Path(cls.directory.name) / 'sha256-driver'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(REPO / 'src/native/engine'), str(REPO / 'tests/sha256_accel_driver.c'),
                        '-o', str(cls.driver)], check=True)
        cls.hardware = subprocess.check_output([str(cls.driver), 'available'], text=True).strip() == '1'
        cls.fallback_driver = Path(cls.directory.name) / 'sha256-fallback'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-DLOCKSTEP_SHA256_DISABLE_HARDWARE', '-I', str(REPO / 'src/native/engine'),
                        str(REPO / 'tests/sha256_accel_driver.c'), '-o', str(cls.fallback_driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_vectors_streams_and_unaligned_inputs(self):
        rng = random.Random(20261108)
        lengths = [0, 1, 55, 56, 63, 64, 65, 127, 128, 129, 1024, 4097]
        lengths += [rng.randrange(0, 10000) for _ in range(25)]
        for length in lengths:
            data = rng.randbytes(length)
            expected = hashlib.sha256(data).hexdigest()
            for chunk, offset in ((0, 0), (1, 1), (17, 3), (63, 7), (64, 0), (137, 1)):
                for mode in (('scalar', 'hardware', 'auto') if self.hardware else ('scalar', 'auto')):
                    with self.subTest(length=length, chunk=chunk, offset=offset, mode=mode):
                        result = subprocess.check_output([str(self.driver), mode, str(chunk), str(offset)], input=data)
                        self.assertEqual(result.decode().strip(), expected)

    def test_simulated_runtime_feature_denial(self):
        if platform.machine() not in ('x86_64', 'amd64'):
            self.skipTest('x86-64 CPUID test')
        driver = Path(self.directory.name) / 'sha256-mocked-cpuid'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-pthread', '-DLOCKSTEP_SHA256_TEST_CPUID', '-I', str(REPO / 'src/native/engine'),
                        str(REPO / 'tests/sha256_accel_driver.c'), '-o', str(driver)], check=True)
        cases = [(6, 1 << 9, 1 << 29, 0), (7, 0, 1 << 29, 0),
                 (7, 1 << 9, 0, 0), (7, 1 << 9, 1 << 29, 1)]
        for maximum, leaf1, leaf7, expected in cases:
            output = subprocess.check_output([str(driver), 'cpuid', str(maximum), str(leaf1), str(leaf7)])
            fields = output.decode().split()
            self.assertEqual(int(fields[0]), expected)
            if not expected:
                self.assertEqual(fields[1:], [hashlib.sha256(b'abc').hexdigest()])
            else:
                self.assertEqual(len(fields), 1)

    def test_concurrent_first_context_initialization(self):
        for _ in range(4):
            subprocess.run([str(self.driver), 'threads'], check=True)

    def test_long_reference_input(self):
        data = bytes(range(256)) * 4301
        expected = hashlib.sha256(data).hexdigest()
        for mode in (('scalar', 'hardware', 'auto') if self.hardware else ('scalar', 'auto')):
            output = subprocess.check_output([str(self.driver), mode, '4051', '3'], input=data)
            self.assertEqual(output.decode().strip(), expected)

    def test_arbitrary_initial_chain_compression(self):
        if not self.hardware:
            self.skipTest('SHA-NI unavailable')
        rng = random.Random(77123)
        for _ in range(100):
            data = rng.randbytes(96)
            scalar = subprocess.check_output([str(self.driver), 'compress', 'scalar'], input=data)
            accelerated = subprocess.check_output([str(self.driver), 'compress', 'hardware'], input=data)
            self.assertEqual(accelerated, scalar)

    def test_build_without_hardware_uses_scalar(self):
        self.assertEqual(subprocess.check_output([str(self.fallback_driver), 'available']).strip(), b'0')
        data = b'fallback' * 123
        result = subprocess.check_output([str(self.fallback_driver), 'auto', '17', '1'], input=data)
        self.assertEqual(result.decode().strip(), hashlib.sha256(data).hexdigest())
        refused = subprocess.run([str(self.fallback_driver), 'hardware', '17', '1'], input=data)
        self.assertEqual(refused.returncode, 77)

    def test_known_vectors(self):
        for data in (b'', b'abc', b'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'):
            result = subprocess.check_output([str(self.driver), 'auto', '7', '1'], input=data).decode().strip()
            self.assertEqual(result, hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    unittest.main()
