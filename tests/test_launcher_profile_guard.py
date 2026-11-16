"""A launcher profile that a running X4 was started with is not wiped (src/lockstep/launcher/profile.py)."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import profile  # noqa: E402


class InUse(unittest.TestCase):
    def test_a_profile_of_a_running_client_is_refused_and_another_is_not(self):
        with mock.patch.object(profile, 'running_profiles', return_value={profile.PROFILES[1]: 4321}):
            with self.assertRaises(SystemExit) as refused:
                profile.refuse_in_use(profile.PROFILES[1])
            self.assertIn('pid 4321', str(refused.exception))
            self.assertIn('X4LS_FIRST_PROFILE', str(refused.exception))
            profile.refuse_in_use(profile.PROFILES[0])            # nobody has it

    def test_prepare_asks_for_the_real_profiles_only(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(profile, 'running_profiles', return_value={profile.PROFILES[0]: 7}) as asked:
            path = profile.prepare(profile.PROFILES[0], ['x'], root=folder)      # a test's own folder: no question
            self.assertTrue((path / 'config.xml').exists())
            self.assertFalse(asked.called)
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(profile, 'documents', return_value=Path(folder)), \
                mock.patch.object(profile, 'running_profiles', return_value={profile.PROFILES[0]: 7}):
            kept = Path(folder) / str(profile.PROFILES[0]) / 'save'
            kept.mkdir(parents=True)
            (kept / 'keep.txt').write_text('a running client reads this')
            with self.assertRaises(SystemExit):
                profile.prepare(profile.PROFILES[0], ['x'])
            self.assertTrue((kept / 'keep.txt').exists())                         # nothing was wiped
            profile.prepare(profile.PROFILES[1], ['x'])                           # the free one is made
            self.assertTrue((Path(folder) / str(profile.PROFILES[1]) / 'config.xml').exists())

    def test_the_command_lines_it_reads(self):
        rows = ('100|"C:\\x4ls-lanes\\c1\\X4.exe" -personalfolderid 45850 -skipintro\n200|"D:\\Steam\\X4.exe"\nnoise\n'
                '300|X4.exe  -personalfolderid   45857 -logfile x4ls-c1.log\n')
        done = mock.Mock(stdout=rows)
        with mock.patch.object(profile.os, 'name', 'nt'), mock.patch.object(profile.subprocess, 'run', return_value=done):
            self.assertEqual(profile.running_profiles(), {45850: 100, 45857: 300})   # a person's own X4 has no profile of ours
        with mock.patch.object(profile.os, 'name', 'nt'), mock.patch.object(profile.subprocess, 'run', side_effect=OSError):
            self.assertEqual(profile.running_profiles(), {})


if __name__ == '__main__':
    unittest.main()
