"""Dormant native wire capture bridge, with exact role/epoch and refusal checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ENGINE = Path(__file__).resolve().parents[1] / 'src/native/engine/linux'


class ViewWireHandoffTests(unittest.TestCase):
    def test_pure_policy_and_native_sealed_role(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('test_player_stock_view_wire_handoff', 'test_gate_player_stock_view_wire_handoff'):
                with self.subTest(fixture=name):
                    output = Path(directory) / name
                    subprocess.run(['cc', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                                    str(ENGINE / (name + '.c')), '-o', str(output), '-pthread', '-lm'], check=True)
                    subprocess.run([str(output)], check=True, timeout=10)
