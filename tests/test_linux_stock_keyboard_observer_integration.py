"""Run stock observer gate counterexamples against a synthetic image."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine'
LINUX = ENGINE / 'linux'
MONOCYPHER = ROOT / 'third_party/monocypher-4.0.2/src'


class StockKeyboardObserverIntegrationTests(unittest.TestCase):
    def test_gate_boundaries_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'observer-integration'
            subprocess.run(
                [
                    'cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-mcx16', '-ffp-contract=off',
                    '-fasynchronous-unwind-tables', '-I', str(ENGINE), '-I', str(LINUX),
                    '-I', str(MONOCYPHER), '-I', str(MONOCYPHER / 'optional'),
                    str(LINUX / 'test_stock_keyboard_observer_integration.c'),
                    str(MONOCYPHER / 'monocypher.c'),
                    str(MONOCYPHER / 'optional/monocypher-ed25519.c'),
                    '-ldl', '-lpthread', '-lm', '-o', str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)
