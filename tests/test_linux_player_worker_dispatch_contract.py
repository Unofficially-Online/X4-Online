"""Engine-free dispatch ordering; this does not admit the native adapter."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class WorkerDispatchContractTest(unittest.TestCase):
    def test_dispatch_and_forwarding(self):
        root = Path(__file__).resolve().parents[1]
        source = root / "src/native/engine/linux/test_player_worker_dispatch_contract.c"
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "dispatch-contract"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)
