"""Engine-free checks for the isolated Linux Player constructor transaction."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/native/engine/linux/test_player_factory_probe.c"


class PlayerFactoryProbeTests(unittest.TestCase):
    def test_transaction_faults(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "player-factory-test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(SOURCE), "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
