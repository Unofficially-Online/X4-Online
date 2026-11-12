"""Synthetic integration test; never starts the game."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class UiOverlayProbeTest(unittest.TestCase):
    def test_apply_pause_resume_restore_and_interlock(self):
        root = Path(__file__).resolve().parents[1]
        engine = root / 'src/native/engine'
        crypto = root / 'third_party/monocypher-4.0.2/src'
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'ui-overlay-probe'
            subprocess.run(['cc', '-std=gnu11', '-O1', '-mcx16', '-ffp-contract=off',
                '-I', str(engine), '-I', str(engine / 'linux'), '-I', str(crypto), '-I', str(crypto / 'optional'),
                str(engine / 'linux/test_gate_ui_overlay_probe.c'), str(crypto / 'monocypher.c'),
                str(crypto / 'optional/monocypher-ed25519.c'), '-ldl', '-lpthread', '-lm', '-o', str(output)],
                check=True, timeout=120)
            subprocess.run([str(output)], check=True, timeout=10)
