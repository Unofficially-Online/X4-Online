"""Offline Proton adapter checks; never start Steam, Proton or X4."""
import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import proton_gate_live as runner


class ProtonGateLive(unittest.TestCase):
    def test_prefix_environment_matches_absolute_path_with_trailing_slash(self):
        prefix = Path('/private/run/compat/pfx')
        self.assertTrue(runner.prefix_env_matches(b'HOME=/private\0WINEPREFIX=/private/run/compat/pfx/\0', prefix))
        self.assertTrue(runner.prefix_env_matches(b'WINEPREFIX=/private/run/compat/pfx\0', prefix))
        self.assertFalse(runner.prefix_env_matches(b'WINEPREFIX=/private/run/compat/pfx-other/\0', prefix))
        self.assertFalse(runner.prefix_env_matches(b'WINEPREFIX=private/run/compat/pfx/\0', prefix))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('windows', 'data/extensions/ego_dlc_test', 'proton/files/share/default_pfx',
                     'overlay/ui', 'overlay/native', 'steam/legacycompat', '.local'):
            (self.root / name).mkdir(parents=True)
        (self.root / 'windows/X4.exe').write_bytes(b'windows')
        (self.root / 'windows/steam_api64.dll').write_bytes(b'steam')
        (self.root / 'data/01.cat').write_bytes(b'catalog')
        (self.root / 'data/01.dat').write_bytes(b'data')
        (self.root / 'proton/proton').write_text('proton')
        (self.root / 'steam/legacycompat/steamclient64.dll').write_bytes(b'steam')
        (self.root / 'authority').write_text('auth')
        self.snapshot = self.root / 'save.gz'
        self.snapshot.write_bytes(gzip.compress(b'<game seed="42"/>'))
        overlay = self.root / 'overlay'
        (overlay / 'native/x4lan_gate_64.dll').write_bytes(b'gate')
        (overlay / 'ui.xml.files').write_text('')
        (overlay / 'ui/config.lua.append').write_text(
            'X4LAN_CONFIG.native_gate_snapshot_sha = "' + runner.sha256(self.snapshot) + '"\n')
        self.write_manifest()
        self.args = SimpleNamespace(windows=self.root / 'windows', data=self.root / 'data',
                                    proton=self.root / 'proton', overlay=overlay, snapshot=self.snapshot,
                                    steam=self.root / 'steam', out=self.root / '.local/run',
                                    xauthority=self.root / 'authority', display=':0', relay_host='192.0.2.10',
                                    relay_port=47000, relay_token='a' * 32, slot=2, clients=2,
                                    arm_at=2000, startup=70, timeout=600, width=1280, height=720,
                                    expected_ticks=10, fast=True)
        self.root_patch = mock.patch.object(runner, 'ROOT', self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.verify = mock.patch.object(runner.game, 'verify_exe')
        self.verify.start()
        self.addCleanup(self.verify.stop)
        self.bwrap = mock.patch.object(runner.shutil, 'which', return_value='/usr/bin/bwrap')
        self.bwrap.start()
        self.addCleanup(self.bwrap.stop)

    def write_manifest(self):
        overlay = self.root / 'overlay'
        files = {p.relative_to(overlay).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in overlay.rglob('*') if p.is_file() and p.name != 'x4lane-manifest.json'}
        (overlay / 'x4lane-manifest.json').write_text(json.dumps(files))

    def test_preflight_validates_inputs_without_creating_output(self):
        info = runner.validate(self.args, now=1000)
        self.assertEqual((info['seed'], info['relay_host'], info['arm_at']), (42, '192.0.2.10', 2000))
        self.assertNotIn('relay_token', info)
        self.assertFalse(self.args.out.exists())
        runner.game.verify_exe.assert_called_once_with(self.args.windows)

    def test_rejects_bad_session_and_changed_overlay_or_snapshot(self):
        for field, value in [('relay_host', "1.2.3.4';bad"), ('arm_at', 1050), ('clients', 1),
                             ('relay_token', 'bad')]:
            old = getattr(self.args, field)
            setattr(self.args, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                runner.validate(self.args, now=1000)
            setattr(self.args, field, old)
        self.snapshot.write_bytes(gzip.compress(b'<game seed="43"/>'))
        with self.assertRaisesRegex(ValueError, 'pin this snapshot'):
            runner.validate(self.args, now=1000)
        self.assertFalse(self.args.out.exists())

    def test_game_view_prefers_windows_and_filters_extensions(self):
        (self.root / 'data/X4.exe').write_bytes(b'decoy')
        (self.root / 'data/extensions/untrusted_mod').mkdir()
        run = self.root / '.local/run'
        run.mkdir()
        view = runner.make_view(run, self.args.windows, self.args.data)
        self.assertEqual((view / 'X4.exe').read_bytes(), b'windows')
        self.assertEqual([p.name for p in (view / 'extensions').iterdir()], ['ego_dlc_test'])

    def test_namespace_only_grants_private_run_writes(self):
        run = self.root / '.local/run'
        command = runner.namespace(run, {'HOME': str(run / 'home')}, ['proton', 'run', 'X4.exe'])
        self.assertEqual(command[:4], ['bwrap', '--ro-bind', '/', '/'])
        self.assertIn(['--bind', str(run), str(run)], [command[i:i + 3] for i in range(len(command) - 2)])
        self.assertEqual(command[-4:], ['--', 'proton', 'run', 'X4.exe'])

    def test_run_stages_session_in_private_profile_and_records_no_token(self):
        info = runner.validate(self.args, now=1000)
        calls = []

        def fake_copy(command, **kwargs):
            self.assertEqual(command[:4], ['cp', '-a', '--reflink=auto', '--'])
            runner.shutil.copytree(command[4], command[5])

        def fake_popen(command, **kwargs):
            calls.append(command)
            documents = self.args.out / 'compat/pfx/drive_c/users/steamuser/Documents'
            documents.mkdir(parents=True, exist_ok=True)
            if len(calls) == 2:
                logfile = documents / 'Egosoft/X4/45851/x4ls-c2.log'
                logfile.write_text('NATIVE_GATE ARMED\nNATIVE_GATE STOP slot=p2 completed=10 reason=1 restored=1\n')
            return mock.Mock(pid=1234 + len(calls), wait=mock.Mock(return_value=0))

        with mock.patch.object(runner.subprocess, 'run', side_effect=fake_copy), \
                mock.patch.object(runner.subprocess, 'Popen', side_effect=fake_popen), \
                mock.patch.object(runner, 'start_ticks', return_value=1), \
                mock.patch.object(runner, 'cleanup_prefix', return_value={'cleanup_errors': [],
                                  'owned_groups_remaining': {}, 'prefix_processes_remaining': []}), \
                mock.patch.object(runner.time, 'time', return_value=1000):
            result = runner.run(self.args, info)
        self.assertEqual(result['status'], 'stopped')
        self.assertTrue(result['gate_armed'])
        self.assertEqual(result['gate_stop'], (2, 10, 1, 1))
        self.assertNotIn(self.args.relay_token, (self.args.out / 'result.json').read_text())
        config = (self.args.out / 'game/extensions/x4_lan_lab/ui/config.lua').read_text()
        self.assertIn("X4LAN_CONFIG.native_gate_relay_host = '192.0.2.10'", config)
        self.assertIn('X4LAN_CONFIG.native_gate_arm_at = 2000', config)
        self.assertEqual((self.args.out / 'compat/pfx/drive_c/users/steamuser/Documents/Egosoft/X4/45851/save/save_009.xml.gz').read_bytes(),
                         self.snapshot.read_bytes())
        config_xml = self.args.out / 'compat/pfx/drive_c/users/steamuser/Documents/Egosoft/X4/45851/config.xml'
        self.assertIn('<chromaticaberration>false</chromaticaberration>', config_xml.read_text())
        self.assertEqual(len(calls), 2)

    def test_monitor_stops_for_gate_without_waiting_for_game_exit(self):
        logfile = self.root / 'game.log'
        logfile.write_text('[X4LAN] NATIVE_GATE ARMED\n[X4LAN] NATIVE_GATE STOP slot=p2 completed=10 reason=1 restored=1\n')
        proc = mock.Mock()
        proc.poll.return_value = None
        result = runner.monitor_game(proc, logfile, 1, 2, 10)
        self.assertEqual(result['status'], 'stopped')
        self.assertEqual(result['gate_stop'], (2, 10, 1, 1))
        self.assertEqual(runner.monitor_game(proc, logfile, 1, 2, 11)['status'], 'failed')
        proc.wait.assert_not_called()

    def test_cleanup_targets_private_wineserver_even_after_parent_exit(self):
        run = self.root / '.local/run'
        (run / 'compat/pfx').mkdir(parents=True)
        process = mock.Mock(pid=123)
        with mock.patch.object(runner.subprocess, 'run', return_value=mock.Mock(returncode=0)) as call, \
                mock.patch.object(runner, 'stop_owned_process') as stop, \
                mock.patch.object(runner, 'stop_prefix_process') as detached, \
                mock.patch.object(runner, 'wait_owned_groups', return_value={}), \
                mock.patch.object(runner, 'prefix_processes', return_value=[456]):
            result = runner.cleanup_prefix(run, {'HOME': str(run / 'home')}, [(process, 7)])
        self.assertEqual([item.args[0][-1] for item in call.call_args_list], ['-k', '-w'])
        self.assertIn('WINEPREFIX', call.call_args_list[0].args[0])
        stop.assert_called_once_with(process, 7)
        detached.assert_called_once_with(456, run / 'compat/pfx')
        self.assertEqual(result['prefix_processes_remaining'], [456])
        self.assertTrue(result['cleanup_errors'])

    def test_detached_prefix_process_uses_authenticated_pidfd(self):
        prefix = Path('/private/pfx')
        with mock.patch.object(runner, 'start_ticks', return_value=123), \
                mock.patch.object(runner.os, 'pidfd_open', return_value=88), \
                mock.patch.object(runner, 'prefix_process_matches', side_effect=[True, True]) as matches, \
                mock.patch.object(runner, 'wait_pidfd', side_effect=[False, True]), \
                mock.patch.object(runner.signal, 'pidfd_send_signal') as send, \
                mock.patch.object(runner.os, 'close') as close:
            runner.stop_prefix_process(55, prefix)
        self.assertEqual(matches.call_count, 2)
        self.assertEqual(send.call_args_list, [mock.call(88, runner.signal.SIGTERM), mock.call(88, runner.signal.SIGKILL)])
        close.assert_called_once_with(88)

    def test_detached_process_is_not_signalled_when_identity_changes(self):
        with mock.patch.object(runner, 'start_ticks', return_value=123), \
                mock.patch.object(runner.os, 'pidfd_open', return_value=88), \
                mock.patch.object(runner, 'prefix_process_matches', return_value=False), \
                mock.patch.object(runner.signal, 'pidfd_send_signal') as send, \
                mock.patch.object(runner.os, 'close'):
            runner.stop_prefix_process(55, Path('/private/pfx'))
        send.assert_not_called()

    def test_monitor_timeout_and_unclean_stop_are_failures(self):
        logfile = self.root / 'game.log'
        proc = mock.Mock()
        proc.poll.return_value = None
        with mock.patch.object(runner.time, 'monotonic', side_effect=[0, 2]):
            self.assertIn('timeout', runner.monitor_game(proc, logfile, 1, 2, 10)['error'])
        logfile.write_text('NATIVE_GATE ARMED\nNATIVE_GATE STOP slot=p2 completed=10 reason=1 restored=0\n')
        self.assertEqual(runner.monitor_game(proc, logfile, 1, 2, 10)['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
