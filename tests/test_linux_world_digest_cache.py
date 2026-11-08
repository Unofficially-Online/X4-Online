"""Native cache checks plus independent Python hashlib comparisons."""
import ctypes
import hashlib
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which(os.environ.get('CC', 'cc')),
                     'native C compiler on Linux required')
class WorldDigestCacheTests(unittest.TestCase):
    def test_native_and_hashlib(self):
        source = REPO / 'tests/test_linux_world_digest_cache.c'
        include = REPO / 'src/native/engine/linux'
        compiler = os.environ.get('CC', 'cc')
        flags = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(include)]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'cache-test'
            library_path = Path(directory) / 'cache-test.so'
            subprocess.run([compiler, *flags, str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True, capture_output=True)
            subprocess.run([compiler, *flags, '-DCACHE_TEST_LIBRARY', '-shared', '-fPIC',
                            str(source), '-o', str(library_path)], check=True)
            library = ctypes.CDLL(str(library_path))
            library.cache_test_hash.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
            library.cache_test_init()
            try:
                rng = random.Random(20261108)
                for length in list(range(130)) + [rng.randrange(0, 512) for _ in range(300)]:
                    data = rng.randbytes(length)
                    source_buffer = ctypes.create_string_buffer(data)
                    output = (ctypes.c_ubyte * 32)()
                    for _ in range(2):
                        library.cache_test_hash(source_buffer, length, output)
                        self.assertEqual(bytes(output), hashlib.sha256(data).digest())
            finally:
                library.cache_test_release()


if __name__ == '__main__':
    unittest.main()
