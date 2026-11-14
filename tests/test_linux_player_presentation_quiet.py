"""Deterministic clock and real mutexes around the actual quiet adapter."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class PresentationQuietTests(unittest.TestCase):
    def test_bounded_retry_and_fail_closed_cleanup(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_gate_player_presentation_quiet.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'quiet'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(source), '-o', str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)
