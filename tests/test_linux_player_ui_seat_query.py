"""Synthetic seat-query ordering and classification; no game is launched."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class SeatQueryTest(unittest.TestCase):
    def test_order_failure_and_output(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_ui_seat_query.c'
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'seat-query'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-lm', '-o', str(output)], check=True, timeout=120)
            subprocess.run([str(output)], check=True, timeout=10)
