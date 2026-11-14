"""Native component policy and original-return preservation, without X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ComponentPolicyTest(unittest.TestCase):
    def test_default_environment_strips_component_policy(self):
        import linux_stock_view_fov_observer as observer
        self.assertNotIn('X4_DEV_STOCK_VIEW_COMPONENT_POLICY', observer.environment(
            {'X4_DEV_STOCK_VIEW_COMPONENT_POLICY': '1'}, False))

    def test_attribution_generation_and_stock_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            for fixture in ('test_player_stock_view_component_policy', 'test_gate_player_stock_view_component_policy'):
                with self.subTest(fixture=fixture):
                    binary = Path(directory) / fixture
                    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT / 'src/native/engine'),
                                    str(ROOT / ('src/native/engine/linux/' + fixture + '.c')),
                                    '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True, timeout=10)
