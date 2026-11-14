"""Verify exact reconstruction of observer-owned semaphore entry detours."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
import unittest


class SemaphoreOriginalViewTest(unittest.TestCase):
    def test_owned_pages_and_full_original_range(self):
        source = Path(__file__).resolve().parents[1] / "src/native/engine/linux/test_player_semaphore_original_view.c"
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "original-view"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_actual_gate_adapter_with_pinned_provider(self):
        engine = Path(__file__).resolve().parents[1] / "src/native/engine/linux"
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "libx4mp_semaphore_observer.so"
            binary = Path(directory) / "gate-pin"
            flags = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror"]
            subprocess.run([*flags, "-fPIC", "-shared", str(engine / "test_semaphore_pin_provider.c"),
                            "-o", str(provider)], check=True, timeout=30)
            subprocess.run([*flags, str(engine / "test_gate_player_semaphore_original_view.c"),
                            "-o", str(binary), "-ldl"], check=True, timeout=30)
            data = provider.read_bytes()
            env = dict(os.environ, X4MP_SEMAPHORE_OBSERVER_SHA256=hashlib.sha256(data).hexdigest(),
                       X4MP_SEMAPHORE_OBSERVER_SIZE=str(len(data)))
            subprocess.run([str(binary), str(provider)], env=env, check=True, timeout=10)
            subprocess.run([*flags, "-fPIC", "-fvisibility=hidden", "-shared",
                            str(engine / "player_semaphore_observer_preload.c"), "-o", str(provider)],
                           check=True, timeout=30)
            data = provider.read_bytes()
            env.update(X4MP_SEMAPHORE_OBSERVER_SHA256=hashlib.sha256(data).hexdigest(),
                       X4MP_SEMAPHORE_OBSERVER_SIZE=str(len(data)))
            env.pop("X4MP_SEMAPHORE_OBSERVER", None)
            subprocess.run([str(binary), str(provider), "provider-only"], env=env, check=True, timeout=10)
