"""Default-off disposable P2 throttle admission; fixture builds, never X4."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import build_native_gate_overlay as overlay
import linux_gate_live as live
from test_stock_keyboard_replay_overlay import snapshot_bytes
from test_linux_player_network_evidence import network_fixture, throttle_fixture
from linux_player_network_evidence import parse_network_physics_evidence


class ThrottleProbeBundleTests(unittest.TestCase):
    def options(self):
        return dict(player_throttle_probe=True, independent_keyboard_frames=2,
                    independent_keyboard_mode='independent_keyboard', clients=2,
                    stock_keyboard_replay_probe=True, player_factory_probe=True,
                    stock_keyboard_observer=True, serial_pathing=True,
                    player_lifetime_sha256='pinned', stock_keyboard_replay_last_capture=4)

    def test_default_and_exact_manifest_environment_admission(self):
        self.assertEqual(live.throttle_probe_environment({}, False), {})
        manifest = self.options()
        self.assertTrue(live.validate_network_physics(manifest, True, 2, 180))
        self.assertEqual(live.throttle_probe_environment(manifest, True),
                         {'X4_DEV_P2_THROTTLE': 'state13-discard'})
        self.assertRaises(SystemExit, live.throttle_probe_environment, manifest, False)
        self.assertRaises(SystemExit, live.validate_network_physics, manifest, False, 2, 180)
        self.assertRaises(SystemExit, live.validate_network_physics, manifest, True, 3, 180)
        for value in (None, 0, 1, 'true'):
            self.assertRaises(SystemExit, live.throttle_probe_environment,
                              dict(manifest, player_throttle_probe=value), True)
        self.assertRaises(SystemExit, live.validate_network_physics,
                          dict(manifest, player_external_camera_probe=True, independent_keyboard_frames=3,
                               stock_keyboard_replay_last_capture=5), True, 3, 180)
        self.assertRaises(SystemExit, live.validate_network_physics,
                          dict(manifest, independent_keyboard_frames=0), True, 2, 180)

    def test_warm_manifest_requires_frozen_schedule_and_exclusive_mode(self):
        manifest = dict(self.options(), player_throttle_probe=False, player_throttle_warm_probe=True,
                        independent_keyboard_frames=3, stock_keyboard_replay_last_capture=5,
                        player_throttle_start_tick=6, player_throttle_environment='state13-warm-discard')
        self.assertTrue(live.validate_network_physics(manifest, True, 3, 180))
        self.assertEqual(live.throttle_probe_environment(manifest, True),
                         {'X4_DEV_P2_THROTTLE': 'state13-warm-discard'})
        for changed in (dict(manifest, player_throttle_probe=True),
                        dict(manifest, player_external_camera_probe=True),
                        dict(manifest, independent_keyboard_frames=2),
                        dict(manifest, player_throttle_start_tick=5),
                        dict(manifest, player_throttle_environment='state13-discard'),
                        {key: value for key, value in manifest.items() if key != 'player_throttle_start_tick'}):
            with self.subTest(changed=changed), self.assertRaises(SystemExit):
                live.throttle_probe_environment(changed, True)

    def test_bundle_rejects_without_independent_window_before_build(self):
        with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                       '--out', '/unused/out', '--player-throttle-probe'])
        command.assert_not_called()

    def test_header_is_required_and_frozen_as_real_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            self.assertRaises(SystemExit, live.throttle_probe_sources, repo)
            source = repo / 'src/native/engine/linux/gate_player_throttle_discard.h'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'fixture throttle header\n')
            sources = live.throttle_probe_sources(repo)
            self.assertEqual(sources, [source])
            live.freeze_source_receipts(repo, repo / 'bundle', sources)
            receipt = repo / 'bundle/source-receipts' / source.relative_to(repo)
            self.assertEqual(receipt.read_bytes(), source.read_bytes())
            self.assertEqual(live.sha(receipt), live.sha(source))

    def test_run_refuses_missing_frozen_header_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'bundle'
            root.mkdir()
            (root / live.MARKER).touch()
            config = root / 'extension/x4_lan_lab/ui/config.lua'; config.parent.mkdir(parents=True); config.write_text('')
            manifest = dict(self.options(), ticks=180, files={})
            (root / 'manifest.json').write_text(json.dumps(manifest))
            args = SimpleNamespace(user='fixture', root=root, ticks=None,
                                   controller_physics_discard=True, controller_physics_frames=2)
            import os
            with mock.patch('pwd.getpwnam', return_value=SimpleNamespace(pw_dir=directory, pw_uid=os.geteuid())), \
                    mock.patch.object(live.subprocess, 'Popen') as launch, \
                    self.assertRaisesRegex(SystemExit, 'frozen native header'):
                live.run(args)
            launch.assert_not_called()

    def test_throttle_rows_require_explicit_parser_admission(self):
        rows, stderr, engine, _, _ = network_fixture()
        marker = ('X4LAN LIFECYCLE_DISCARD throttle=before tick=5 ship=aa controller=bb '
                  'collection=cc members=0 keys=00 command=0 lower=0 upper=1 original=0\n')
        rows.insert(2, marker)
        result = parse_network_physics_evidence(rows, stderr, engine, 223, frame_count=3, slot=2)
        self.assertFalse(result['pass'])
        self.assertTrue(any('throttle=' in error for error in result['errors']))

    def test_runner_collects_both_peers_and_checks_throttle_semantics(self):
        rows, stderr_rows, _, p1, p2 = throttle_fixture()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            peers = {}
            for slot, records in ((1, p1), (2, p2)):
                peer = root / str(slot)
                peer.mkdir()
                (peer / 'trace').write_text(''.join(rows))
                (peer / 'stderr').write_text(''.join(stderr_rows))
                (peer / 'output').write_text('')
                (peer / 'engine').write_text(''.join(
                    f'LINUX_KEYBOARD_CAPTURE tick={index + 3} record={record}\n'
                    for index, record in enumerate(records)))
                arguments = (peer / 'trace', peer / 'retained', peer / 'engine', peer / 'output', 223)
                peers[str(slot)] = live.collect_network_client(
                    arguments, peer / 'stderr', 3, slot, throttle=True)
                self.assertTrue(peers[str(slot)]['pass'], peers[str(slot)]['errors'])
            self.assertEqual(live.network_peer_errors(peers, 3, throttle=True), [])
            peers['2']['throttle']['stages'][2]['command'] += 0.5
            self.assertTrue(live.network_peer_errors(peers, 3, throttle=True))
            with mock.patch.object(live, 'compare_throttle_peers', None):
                self.assertTrue(live.network_peer_errors(peers, 3, throttle=True))

    def test_runner_collects_warm_evidence_for_both_peers(self):
        rows, stderr_rows, _, p1, p2 = throttle_fixture(start=6)
        with tempfile.TemporaryDirectory() as directory:
            peers = {}
            for slot, records in ((1, p1), (2, p2)):
                peer = Path(directory) / str(slot)
                peer.mkdir()
                (peer / 'trace').write_text(''.join(rows))
                (peer / 'stderr').write_text(''.join(stderr_rows))
                (peer / 'output').write_text('')
                (peer / 'engine').write_text(''.join(
                    f'LINUX_KEYBOARD_CAPTURE tick={index + 3} record={record}\n'
                    for index, record in enumerate(records)))
                arguments = (peer / 'trace', peer / 'retained', peer / 'engine', peer / 'output', 223)
                peers[str(slot)] = live.collect_network_client(
                    arguments, peer / 'stderr', 3, slot, throttle=True, throttle_start_tick=6)
                self.assertTrue(peers[str(slot)]['pass'], peers[str(slot)]['errors'])
            self.assertEqual(live.network_peer_errors(peers, 3, throttle=True), [])
            peers['2']['throttle']['stages'][0]['factors']['values'][0] += 1
            self.assertTrue(live.network_peer_errors(peers, 3, throttle=True))

    def test_fixture_bundle_records_flag_and_pins_source_without_overlay_enablement(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            snapshot = base / 'save.gz'
            snapshot.write_bytes(snapshot_bytes())
            source = ROOT / 'src/native/engine/linux/gate_player_throttle_discard.h'
            original_copy = shutil.copy2
            for mode in ('off', 'original', 'warm'):
                enabled = mode != 'off'
                output = base / mode
                def copy(source_path, destination, *args, **kwargs):
                    if Path(source_path) == source:
                        Path(destination).write_bytes(b'fixture throttle header\n')
                        return destination
                    if Path(source_path).name == live.PLAYER_LIFETIME:
                        Path(destination).write_bytes(b'fixture preload')
                        return destination
                    return original_copy(source_path, destination, *args, **kwargs)
                def command(arguments, **kwargs):
                    if 'build_native_gate_linux.py' in str(arguments[1]):
                        (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(arguments[1]):
                        self.assertNotIn('--player-throttle-probe', arguments)
                        with mock.patch.object(sys, 'argv', arguments[1:]):
                            overlay.main()
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '', returncode=0)
                with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                        mock.patch.object(live.shutil, 'copy2', side_effect=copy), \
                        mock.patch.object(live, 'throttle_probe_sources', return_value=[source]), \
                        contextlib.redirect_stdout(io.StringIO()):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot), '--out', str(output),
                               '--stock-keyboard-replay-probe', '--player-factory-probe', '--stock-keyboard-observer',
                               '--serial-pathing', '--player-lifetime-preload', '--player-factory-retain-until', '60',
                               '--independent-keyboard-frames', '3' if mode == 'warm' else '2',
                               *(['--player-throttle-warm-probe'] if mode == 'warm' else
                                 ['--player-throttle-probe'] if enabled else [])])
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertIs(manifest['player_throttle_probe'], mode == 'original')
                self.assertIs(manifest['player_throttle_warm_probe'], mode == 'warm')
                self.assertEqual(manifest['player_throttle_start_tick'], 6 if mode == 'warm' else 5 if enabled else None)
                self.assertEqual(manifest['player_throttle_environment'], 'state13-warm-discard' if mode == 'warm' else 'state13-discard' if enabled else None)
                if enabled:
                    relative = 'source-receipts/' + source.relative_to(ROOT).as_posix()
                    self.assertEqual(live.sha(output / relative), manifest['files'][relative])
                    self.assertEqual((output / relative).read_bytes(), b'fixture throttle header\n')


if __name__ == '__main__':
    unittest.main()
