"""Before a client start the launcher frees X4's instance mutex in other sessions' clients (session.release_beside)."""
import contextlib
import io
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import session  # noqa: E402


class ReleaseBeside(unittest.TestCase):
    def test_other_sessions_clients_are_freed_and_reported(self):
        # pid 10: another session's client that holds the mutex; 11: one that does not; 12: this session's own;
        # 13: an X4 that is not a launcher client (never touched); 14: a client that ended since the listing.
        listed = [(10, True), (11, True), (12, True), (13, False), (14, True)]
        asked, lines = [], []

        def release(pid):
            asked.append(pid)
            if pid == 14:
                raise OSError('gone')
            return {10: 2, 11: 0}[pid]

        with mock.patch.object(session, 'other_clients', return_value=listed), \
                mock.patch.object(session.instance, 'release_instance_mutex', side_effect=release):
            session.release_beside({12}, lines.append)
        self.assertEqual(asked, [10, 11, 14])
        self.assertEqual(lines, ['beside X4 pid 10: released 2 instance mutex handle(s)'])

    def test_it_reports_without_a_log_of_its_own(self):
        out = io.StringIO()
        with mock.patch.object(session, 'other_clients', return_value=[(10, True)]),                 mock.patch.object(session.instance, 'release_instance_mutex', return_value=1),                 contextlib.redirect_stdout(out):
            session.release_beside(set())
        self.assertEqual(out.getvalue().strip(), 'beside X4 pid 10: released 1 instance mutex handle(s)')


if __name__ == '__main__':
    unittest.main()
