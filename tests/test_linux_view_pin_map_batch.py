"""Mapping snapshots remain per-validation and reject permission drift."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxViewPinMapBatchTest(unittest.TestCase):
    def test_mapping_drift(self):
        root = Path(__file__).resolve().parents[1]
        source = root / "src/native/engine/linux/test_player_view_pin_map_batch.c"
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "map-batch"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
