"""Canonical bank roundtrip against a fake image and request tree."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_gate_player_bank_roundtrip.c'


class PlayerBankRoundtripTests(unittest.TestCase):
    def test_roundtrip_and_pending_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'roundtrip'
            build = subprocess.run(
                ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 str(SOURCE), '-o', str(executable)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('4 canonical roundtrip scenario groups passed', run.stdout)
            fatal = subprocess.run([str(executable), '--fatal'], cwd=ROOT,
                                   capture_output=True, text=True)
            self.assertEqual(fatal.returncode, 221, fatal.stdout + fatal.stderr)
            self.assertEqual(fatal.stdout, '')
            self.assertIn('PLAYER_CONTROL_BANK_PROBE_FATAL exit=221 captured_tick=5', fatal.stderr)
            self.assertIn('reason=unknown_zone_effect complete=0 cleanup_complete=0', fatal.stderr)
            self.assertIn('pending_bytes=1', fatal.stderr)
            self.assertIn('effect_pending=1 slot_owned=1', fatal.stderr)
