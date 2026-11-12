"""Offline ownership and cleanup checks; no game process is launched."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

DEV = Path(__file__).resolve().parents[1] / 'tools/dev'
sys.path.insert(0, str(DEV))
spec = importlib.util.spec_from_file_location('linux_gate_live_cleanup_test', DEV / 'linux_gate_live.py')
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


@unittest.skipUnless(sys.platform == 'linux', 'Linux-only process isolation')
class CleanupTests(unittest.TestCase):
    def test_lock_refuses_existing_file_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / 'lock'
            target.write_text('existing')
            with self.assertRaises(FileExistsError):
                live.create_lock_target(target, os.getuid(), os.getgid())
            self.assertEqual(target.read_text(), 'existing')
            target.unlink()
            target.symlink_to(root / 'missing')
            with self.assertRaises(FileExistsError):
                live.create_lock_target(target, os.getuid(), os.getgid())
            self.assertTrue(target.is_symlink())

    def test_lock_releases_only_original_inode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'lock'
            fd, stat = live.create_lock_target(path, os.getuid(), os.getgid())
            path.unlink()
            path.write_text('replacement')
            with self.assertRaisesRegex(RuntimeError, 'replaced'):
                live.release_lock_target(path, fd, stat)
            self.assertEqual(path.read_text(), 'replacement')
            path.unlink()
            fd, stat = live.create_lock_target(path, os.getuid(), os.getgid())
            live.release_lock_target(path, fd, stat)
            self.assertFalse(path.exists())

    def test_reused_or_reaped_leader_never_signalled(self):
        proc = mock.Mock(pid=12345)
        proc.poll.return_value = None
        with mock.patch.object(live, 'start_ticks', return_value=11), \
             mock.patch.object(live.os, 'killpg') as kill:
            live.stop_owned_process(proc, 10)
        kill.assert_not_called()
        proc.poll.return_value = 0
        with mock.patch.object(live.os, 'killpg') as kill:
            live.stop_owned_process(proc, 10)
        kill.assert_not_called()

    def test_kill_rechecks_identity_after_timeout(self):
        proc = mock.Mock(pid=12345)
        proc.poll.return_value = None
        proc.wait.side_effect = subprocess.TimeoutExpired('fake', 15)
        with mock.patch.object(live, 'start_ticks', side_effect=[10, 11]), \
             mock.patch.object(live.os, 'killpg') as kill:
            live.stop_owned_process(proc, 10)
        kill.assert_called_once_with(12345, live.signal.SIGTERM)

    def test_survivors_are_reported_after_bounded_wait(self):
        with mock.patch.object(live, 'owned_group_members', return_value=[12346]), \
             mock.patch.object(live.time, 'monotonic', side_effect=[0, 6]):
            self.assertEqual(live.wait_owned_groups({12345: 10}), {12345: [12346]})

    def test_gate_log_includes_rows_written_during_teardown(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'engine-output.log'
            retained = root / 'retained.log'
            extracted = root / 'game1.log'
            script = """
import signal
import sys
import time

log_path = sys.argv[1]
def finish(*unused):
    with open(log_path, 'a') as output:
        output.write('[=ERROR=] 2.0 [X4LAN] NATIVE_GATE FINAL restored=1\\n')
    raise SystemExit(0)
signal.signal(signal.SIGTERM, finish)
with open(log_path, 'w') as output:
    output.write('[=ERROR=] 1.0 [X4LAN] NATIVE_GATE STOP completed=600\\n')
print('ready', flush=True)
while True:
    time.sleep(1)
"""
            process = subprocess.Popen(
                [sys.executable, '-c', script, str(source)],
                stdout=subprocess.PIPE,
                text=True,
                start_new_session=True,
            )
            try:
                self.assertEqual(process.stdout.readline().strip(), 'ready')
                original_before_stop = source.read_text()
                with self.assertRaisesRegex(RuntimeError, 'writers may still run'):
                    live.retain_gate_log(source, retained, extracted, writers_closed=False)
                self.assertFalse(extracted.exists())
                started = live.start_ticks(process.pid)
                live.stop_owned_process(process, started)
                self.assertEqual(live.wait_owned_groups({process.pid: started}), {})
                self.assertNotEqual(source.read_text(), original_before_stop)
                live.retain_gate_log(source, retained, extracted, writers_closed=True)
                self.assertEqual(retained.read_text(), source.read_text())
                self.assertEqual(extracted.read_text(),
                                 '[X4LAN] NATIVE_GATE STOP completed=600\n'
                                 '[X4LAN] NATIVE_GATE FINAL restored=1\n')
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stdout.close()

    def test_missing_engine_log_refuses_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(RuntimeError, 'engine log missing'):
                live.retain_gate_log(root / 'missing', root / 'raw', root / 'game1.log', True)
            self.assertFalse((root / 'game1.log').exists())

    def test_settled_groups_do_not_fail(self):
        with mock.patch.object(live, 'owned_group_members', side_effect=[[12346], []]), \
             mock.patch.object(live.time, 'sleep'):
            self.assertEqual(live.wait_owned_groups({12345: 10}), {})


if __name__ == '__main__':
    unittest.main()
