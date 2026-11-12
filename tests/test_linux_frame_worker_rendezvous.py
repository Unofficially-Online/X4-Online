"""Engine-free atomic helper with bounded controlled pthread schedules."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_frame_worker_rendezvous.c'


class FrameWorkerRendezvousTests(unittest.TestCase):
    def test_single_use_fail_closed_rendezvous(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'rendezvous'
            build = subprocess.run(
                ['cc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-pthread',
                 str(SOURCE), '-o', str(executable)],
                cwd=ROOT, capture_output=True, text=True, timeout=30)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], cwd=ROOT, capture_output=True,
                                 text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('7 rendezvous scenario groups passed', run.stdout)
