"""Exercise the diagnostic observer against synthetic pointer trees, never X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_stock_keyboard_observer.c'


class LinuxStockKeyboardObserverTests(unittest.TestCase):
    def test_observer_scenarios(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'stock-observer'
            subprocess.run(
                ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', str(SOURCE), '-o', str(binary)],
                check=True,
            )
            for scenario in (
                'normal', 'duplicate', 'rollback', 'unknown', 'topology',
                'foreign', 'owner', 'overflow', 'readback', 'pins',
                'unselected_growth', 'unselected_remove', 'unselected_rebalance',
                'selected_remove', 'selected_replace', 'selected_duplicate',
                'invalid_unselected_parent', 'invalid_unselected_order',
                'invalid_unselected_cycle', 'invalid_unselected_read',
                'invalid_unselected_limit', 'unselected_wrapper',
                'install_growth', 'rollback_growth', 'restore_growth_readback',
                'consumer_default', 'consumer_consumed', 'consumer_passthrough',
                'consumer_stopped', 'consumer_failed_cleanup',
            ):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), scenario], check=True)
