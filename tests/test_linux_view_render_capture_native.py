"""Engine-free native renderer VIEW shadow contracts."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine/linux'

class ViewRenderNativeTests(unittest.TestCase):
    def test_native_contracts(self):
        fixtures = ('test_player_view_render_fov_observer', 'test_player_view_render_fov_observer_pin',
                    'test_player_view_render_fov_sample', 'test_player_stock_view_mailbox',
                    'test_player_stock_view_capture_handoff', 'test_gate_player_view_render_capture')
        with tempfile.TemporaryDirectory() as tmp:
            for name in fixtures:
                with self.subTest(fixture=name):
                    output = Path(tmp) / name
                    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                                    '-I', str(ENGINE), str(ENGINE / (name + '.c')), '-o', str(output)], check=True)
                    subprocess.run([str(output)], check=True, capture_output=True)
