"""Frame-window admission and collector dispatch only; no native launch."""
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live


class PhysicsFramesRunnerTests(unittest.TestCase):
    def test_default_one_frame_and_optin_bounds(self):
        self.assertIsNone(live.physics_window_end(False, 1, None, 180))
        self.assertEqual(live.physics_window_end(True, 1, None, 180), 5)
        self.assertEqual(live.physics_window_end(True, 1, 5, 180), 5)
        for count in (2, 30, 180):
            self.assertEqual(live.physics_window_end(True, count, None, count + 5), count + 4)
        for count in (0, -1, 181, True, 1.5, None):
            self.assertRaises(SystemExit, live.physics_window_end, True, count, None, 1000)
        self.assertRaises(SystemExit, live.physics_window_end, False, 2, None, 180)

    def test_end_agreement_and_room_for_next_main_frame(self):
        for count in (1, 2, 180):
            end = 4 + count
            self.assertRaises(SystemExit, live.physics_window_end, True, count, end - 1, 1000)
            self.assertRaises(SystemExit, live.physics_window_end, True, count, end + 1, 1000)
            self.assertRaises(SystemExit, live.physics_window_end, True, count, end, end)
            self.assertEqual(live.physics_window_end(True, count, end, end + 1), end)
        # Existing retention=60 remains valid: repeated native mode owns cleanup.
        live.validate_factory_retention(60, True, 185)
        self.assertEqual(live.physics_window_end(True, 180, None, 185), 184)

    def test_cli_default_and_explicit_frame_count(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused'])
            self.assertEqual(run.call_args.args[0].controller_physics_frames, 1)
            live.main(['run', '--root', '/unused', '--controller-physics-discard',
                       '--controller-physics-frames', '30'])
            self.assertEqual(run.call_args.args[0].controller_physics_frames, 30)

    def test_single_frame_keeps_exact_stderr_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr = Path(directory) / 'stderr'
            line = 'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=5 tail=1 event=enter\n'
            arguments = (Path('trace'), Path('retained'), Path('engine'), Path('stdout'), 223)
            stderr.write_text(line)
            with mock.patch.object(live, 'collect_physics_evidence', return_value={'pass': True, 'errors': []}) as single, \
                    mock.patch.object(live, 'collect_recurring_physics_evidence') as repeated:
                evidence = live.collect_physics_client(arguments, stderr, 1)
                self.assertTrue(evidence['pass'])
                self.assertEqual(evidence['frame_count'], 1)
                single.assert_called_once_with(*arguments)
                repeated.assert_not_called()
            stderr.write_text(line + line.replace('event=enter', 'event=release'))
            with mock.patch.object(live, 'collect_physics_evidence', return_value={'pass': True, 'errors': []}):
                self.assertFalse(live.collect_physics_client(arguments, stderr, 1)['pass'])

    def test_multiple_frames_delegate_stderr_and_requested_count(self):
        arguments = ('trace', 'retained', 'engine', 'output', 223)
        stderr = Path('/not-opened-by-single-frame-guard')
        with mock.patch.object(live, 'collect_physics_evidence') as single, \
                mock.patch.object(live, 'collect_recurring_physics_evidence',
                                  return_value={'pass': True, 'errors': []}) as repeated:
            result = live.collect_physics_client(arguments, stderr, 30)
            self.assertTrue(result['pass'])
            self.assertEqual(result['frame_count'], 30)
            repeated.assert_called_once_with(*arguments, frame_count=30, stderr=stderr)
            single.assert_not_called()
        with mock.patch.object(live, 'collect_recurring_physics_evidence', None):
            self.assertRaises(RuntimeError, live.collect_physics_client, arguments, stderr, 2)

    def test_missing_frozen_collector_and_lifetime_refuse_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'bundle'
            root.mkdir()
            (root / live.MARKER).touch()
            config = root / 'extension/x4_lan_lab/ui/config.lua'
            config.parent.mkdir(parents=True)
            config.write_text('X4LAN_CONFIG = {}\n')
            manifest = {'ticks': 180, 'files': {}, 'player_lifetime_sha256': 'recorded'}
            (root / 'manifest.json').write_text(json.dumps(manifest))
            args = SimpleNamespace(user='fixture', root=root, ticks=None, controller_physics_discard=True,
                                   controller_physics_frames=2)
            user = SimpleNamespace(pw_dir=directory, pw_uid=os.geteuid())
            for collector in (None, mock.Mock()):
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live, 'collect_recurring_physics_evidence', collector), \
                        mock.patch.object(live.subprocess, 'Popen') as launch, \
                        self.assertRaisesRegex(SystemExit, 'bundled linux_player_frames'):
                    live.run(args)
                launch.assert_not_called()
            manifest.pop('player_lifetime_sha256')
            (root / 'manifest.json').write_text(json.dumps(manifest))
            with mock.patch('pwd.getpwnam', return_value=user), \
                    mock.patch.object(live.subprocess, 'Popen') as launch, \
                    self.assertRaisesRegex(SystemExit, 'lifetime preload'):
                live.run(args)
            launch.assert_not_called()


if __name__ == '__main__':
    unittest.main()
