from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StockViewGateTest(unittest.TestCase):
    def test_prepared_generation_identity_and_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'stock-view-gate'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/native/engine'),
                            str(ROOT / 'src/native/engine/linux/test_gate_player_stock_view_fov.c'),
                            '-pthread', '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
