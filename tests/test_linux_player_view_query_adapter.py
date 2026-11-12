"""Build the engine-free view-query gate fixture without launching X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class ViewQueryAdapterTest(unittest.TestCase):
    def test_boundary_and_report(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_gate_player_view_query.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'view-query-adapter'
            subprocess.run(['cc', '-UNDEBUG', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(executable)], check=True, timeout=30)
            result = subprocess.run([str(executable)], capture_output=True, text=True,
                                    check=True, timeout=30)
            self.assertIn('view-query adapter PASS', result.stdout)
            self.assertEqual(result.stderr.count('stage=observe_admission failed_bit=0x200 '), 1)
            self.assertIn('thread=42 ui_thread=42 worker_thread=42 worker_tls=1 input_tick=7 input_depth=0 wrapper_depth=0',
                          result.stderr)
            for stage in ('observe_library_env', 'observe_provider', 'observe_original_pin',
                          'observe_configure', 'observe_phase', 'finish_callback', 'finish_row_count'):
                self.assertIn('stage=' + stage + ' ', result.stderr)
