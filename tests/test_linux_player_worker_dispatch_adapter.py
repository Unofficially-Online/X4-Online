"""Threaded synthetic resident handoff; no native game claim."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class WorkerDispatchAdapterTest(unittest.TestCase):
    def test_threaded_handoff(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'adapter'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(root / 'src/native/engine/linux/test_player_worker_dispatch_adapter.c'),
                            '-o', str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_provider_registration_during_wait(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'midwait'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(root / 'src/native/engine/linux/test_player_semaphore_dispatch_midwait.c'),
                            '-o', str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_strict_network_dispatch_receipts(self):
        import re
        from linux_player_frames_evidence import PREFIX
        from test_linux_semaphore_checkpoint_evidence import SemaphoreCheckpointTests
        fixture = SemaphoreCheckpointTests()
        fixture.setUpClass()
        base = fixture.inserted()
        rows = []
        first = True
        for row in base:
            match = re.fullmatch(re.escape(PREFIX) + r'physics=frame tick=([0-9]+) event=begin\n', row)
            if match:
                if first:
                    rows.append('X4LAN WORKER_DISPATCH bootstrap=1 parked=1 count=0 maximum=1\n')
                    first = False
                tick = match[1]
                rows.append(f'X4LAN WORKER_DISPATCH tick={tick} ticket={tick} consumed=1 post_returned=1\n')
            rows.append(row)
        self.assertFalse(first, 'fixture frame prefix changed')
        parsed = fixture.parse(rows, allow_semaphore_checkpoint=True, allow_worker_dispatch=True)
        self.assertTrue(parsed['pass'], parsed['errors'][:8])
        self.assertFalse(fixture.parse(rows, allow_semaphore_checkpoint=True)['pass'])
        self.assertFalse(fixture.parse(rows, allow_semaphore_checkpoint=True, allow_worker_dispatch=1)['pass'])
        for mutate in ('ticket=40', 'bootstrap=1'):
            broken = [row.replace(mutate, mutate + '0') for row in rows]
            self.assertFalse(fixture.parse(broken, allow_semaphore_checkpoint=True, allow_worker_dispatch=True)['pass'])
        dispatch = next(row for row in rows if 'WORKER_DISPATCH tick=40 ' in row)
        broken = [dispatch] + rows
        self.assertFalse(fixture.parse(broken, allow_semaphore_checkpoint=True, allow_worker_dispatch=True)['pass'])
