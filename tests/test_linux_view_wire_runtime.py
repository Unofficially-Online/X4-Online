"""Finite VIEW route admission; no game or native hooks are started."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class ViewWireRuntimeTest(unittest.TestCase):
    def test_complete_route_and_frame_ownership(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_gate_player_stock_view_wire_runtime.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'view-wire-runtime'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary), '-lm'], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
