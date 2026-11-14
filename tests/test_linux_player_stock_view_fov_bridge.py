import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine/linux'


class StockViewFovBridgeTest(unittest.TestCase):
    def test_displaced_mode_reads_and_forwarding(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'mode-bridge'
            subprocess.run(['cc', '-DPLAYER_STOCK_VIEW_BRIDGE_TEST', '-Wall', '-Wextra', '-Werror',
                            str(ENGINE / 'player_stock_view_fov_bridge.S'),
                            str(ENGINE / 'player_stock_view_ship_bridge.S'),
                            str(ENGINE / 'test_player_stock_view_fov_bridge.S'),
                            str(ENGINE / 'test_player_stock_view_fov_bridge.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_mid_function_stub_frame_registered(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'stub-frame'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(ENGINE / 'test_player_stock_view_fov_cfi.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_provider_dispatch_and_patch_ownership(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'provider'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(ENGINE / 'player_stock_view_fov_bridge.S'),
                            str(ENGINE / 'test_player_stock_view_fov_preload.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_provider_pin_rejects_foreign_mutations(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'provider-pin'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-no-pie',
                            str(ENGINE / 'test_player_stock_view_fov_preload_pin.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_install_restores_owned_prior_patch_on_collision(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'provider-install'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-no-pie',
                            str(ENGINE / 'player_stock_view_fov_bridge.S'),
                            str(ENGINE / 'test_player_stock_view_fov_install.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_ship_provider_pin_rejects_foreign_mutations(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'ship-provider-pin'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-no-pie',
                            str(ENGINE / 'test_player_stock_view_ship_preload_pin.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_ship_install_and_foreign_collision(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'ship-install'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-no-pie',
                            str(ENGINE / 'player_stock_view_ship_bridge.S'),
                            str(ENGINE / 'test_player_stock_view_ship_install.c'),
                            '-ldl', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
