import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StockViewStateTest(unittest.TestCase):
    def test_canonical_two_slot_staging(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'view-state'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/native/engine'),
                            str(ROOT / 'src/native/engine/linux/test_player_stock_view_state.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
