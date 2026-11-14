"""Synthetic forwarding and default-off checks, with no X4 launch."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class SemaphoreObserverPreloadTest(unittest.TestCase):
    def test_forwarding_and_default_off(self):
        engine = Path(__file__).resolve().parents[1] / "src/native/engine/linux"
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libx4mp_semaphore_observer.so"
            fixture = Path(directory) / "semaphore-observer-fixture"
            flags = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror"]
            subprocess.run([*flags, "-fPIC", "-shared", str(engine / "player_semaphore_observer_preload.c"),
                            "-o", str(library)], check=True, timeout=30)
            subprocess.run([*flags, str(engine / "test_player_semaphore_observer_preload.c"),
                            "-o", str(fixture)], check=True, timeout=30)
            subprocess.run([str(fixture)], check=True, timeout=10)
            env = dict(os.environ, LD_PRELOAD=str(library))
            env.pop("X4MP_SEMAPHORE_OBSERVER", None)
            subprocess.run(["/usr/bin/true"], env=env, check=True, timeout=10)
            read_fd, write_fd = os.pipe()
            try:
                env.update(X4MP_SEMAPHORE_OBSERVER="1", X4MP_SEMAPHORE_OBSERVER_FD=str(write_fd))
                result = subprocess.run(["/usr/bin/true"], env=env, pass_fds=(write_fd,),
                                        capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 223)
                self.assertIn(b"SEMAPHORE OBSERVER REFUSED", result.stderr)
            finally:
                os.close(read_fd)
                os.close(write_fd)
