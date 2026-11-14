from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine/linux'


class StockViewComponentTest(unittest.TestCase):
    def test_provider_forwarding_and_collision(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'component-provider'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-no-pie',
                            str(ENGINE / 'player_stock_view_component_bridge.S'),
                            str(ENGINE / 'test_player_stock_view_component_preload.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_provider_pin_rejects_foreign_mutations(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'component-pin'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-no-pie',
                            str(ENGINE / 'test_player_stock_view_component_preload_pin.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
