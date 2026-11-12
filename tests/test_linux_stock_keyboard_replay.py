"""Exercise the engine-free canonical keyboard replay coordinator."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_stock_keyboard_replay.c'
INDEPENDENT_SOURCE = ROOT / 'src/native/engine/linux/test_stock_keyboard_replay_independent.c'


class StockKeyboardReplayTests(unittest.TestCase):
    def test_independent_slots(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'stock-keyboard-independent'
            subprocess.run(
                ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                 str(INDEPENDENT_SOURCE), '-pthread', '-lm', '-o', str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_replay_phases_and_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'stock-keyboard-replay'
            subprocess.run(
                ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                 str(SOURCE), '-pthread', '-lm', '-o', str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)
