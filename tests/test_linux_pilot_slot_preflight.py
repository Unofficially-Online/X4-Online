"""Exercise the inert Linux pilot descriptor preflight with fake objects."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_pilot_slot_preflight.c'


class LinuxPilotSlotPreflightTests(unittest.TestCase):
    def test_stable_and_changing_descriptors(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'pilot-slot-preflight'
            subprocess.run(
                ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', str(SOURCE), '-o', str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)
