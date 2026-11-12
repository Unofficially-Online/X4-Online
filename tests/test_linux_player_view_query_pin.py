"""Authenticate mock resident query provider without starting X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class ViewQueryPinTest(unittest.TestCase):
    def test_provider_and_original_view(self):
        source = Path(__file__).resolve().parents[1] / 'src/native/engine/linux/test_player_view_query_preload_pin.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'view-query-pin'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
            self.assertIn('view query original pin and provider authentication PASS', result.stdout)
