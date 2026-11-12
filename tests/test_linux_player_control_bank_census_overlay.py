"""Read-only control-bank census option plumbing with a stand-in gate; no X4 or compiler."""
import contextlib
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import build_native_gate_overlay as overlay_builder
import linux_gate_live as live
import native_gate_defaults as defaults
import test_lockstep_gate_linux as linux_tests

KEY = 'native_gate_player_control_bank_census'
REPLAY_KEY = 'native_gate_stock_keyboard_replay_probe'


class LinuxPlayerControlBankCensusOverlayTests(unittest.TestCase):
    def setUp(self):
        self.fixture = linux_tests.Overlay()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)

    def build(self, *options):
        return self.fixture.build(*self.fixture.snapshot, '--mode', 'clock', *options)

    def test_overlay_explicit_selection_writes_config_and_hashes_it(self):
        output = self.build('--player-control-bank-census', '--stock-keyboard-observer', '--serial-pathing')
        config = (output / 'ui/config.lua.append').read_text()
        self.assertEqual(defaults.parse(config)[KEY], 'true')
        self.assertEqual(defaults.parse(config)['native_gate_stock_keyboard_observer'], 'true')
        self.assertEqual(defaults.parse(config)['native_gate_serial_pathing'], 'true')
        self.assertNotIn(REPLAY_KEY, defaults.parse(config))
        self.assertNotIn('native_gate_snapshot_fixture', defaults.parse(config))
        manifest = json.loads((output / 'x4lane-manifest.json').read_text())
        for relative in ('ui/config.lua.append', 'ui/native_gate_probe.lua', 'native/x4lan_gate.so'):
            self.assertEqual(live.sha(output / relative), manifest[relative])

    def test_default_and_observer_alone_do_not_request_control_bank_census(self):
        for options in ((), ('--stock-keyboard-observer', '--serial-pathing')):
            with self.subTest(options=options):
                output = self.build(*options)
                self.assertNotIn(KEY, defaults.parse((output / 'ui/config.lua.append').read_text()))

    def test_overlay_requires_all_dependencies_and_excludes_keyboard_replay(self):
        for options in (
            ('--player-control-bank-census',),
            ('--player-control-bank-census', '--stock-keyboard-observer'),
            ('--player-control-bank-census', '--serial-pathing'),
            ('--player-control-bank-census', '--stock-keyboard-observer', '--serial-pathing',
             '--stock-keyboard-replay-probe'),
        ):
            with self.subTest(options=options), self.assertRaises(SystemExit):
                self.build(*options)
        with self.assertRaises(SystemExit):
            self.fixture.build('--mode', 'clock', '--player-control-bank-census',
                               '--stock-keyboard-observer', '--serial-pathing')
        output = Path(self.fixture.tmp.name) / 'windows-output'
        arguments = ['overlay', '--gate-dll', str(Path(self.fixture.tmp.name) / 'gate.dll'),
                     *self.fixture.snapshot, '--out', str(output), '--player-control-bank-census',
                     '--stock-keyboard-observer', '--serial-pathing']
        with mock.patch.object(sys, 'argv', arguments), contextlib.redirect_stderr(io.StringIO()), \
                self.assertRaises(SystemExit):
            overlay_builder.main()
        self.assertFalse(output.exists())

    def test_bundle_refuses_invalid_selection_before_compilation_or_output(self):
        for options in (
            (), ('--stock-keyboard-observer',), ('--serial-pathing',),
            ('--stock-keyboard-observer', '--serial-pathing', '--ticks', '3'),
            ('--stock-keyboard-observer', '--serial-pathing', '--stock-keyboard-replay-probe'),
        ):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'bundle'
                with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                               '--out', str(output), '--player-control-bank-census', *options])
                command.assert_not_called()
                self.assertFalse(output.exists())

    def test_bundle_preserves_selection_config_and_complete_file_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'
            snapshot.write_bytes(gzip.compress(b'<savegame><info><game seed="1"/></info></savegame>'))
            output = root / 'bundle'
            forwarded = []

            def command(arguments, **kwargs):
                if 'build_native_gate_linux.py' in str(arguments[1]):
                    (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                elif 'build_native_gate_overlay.py' in str(arguments[1]):
                    forwarded.extend(arguments)
                    with mock.patch.object(sys, 'argv', arguments[1:]):
                        overlay_builder.main()
                result = 'fixture-commit\n' if 'rev-parse' in arguments else ''
                if 'status' in arguments:
                    result = ' M tools/dev/native_gate_probe.lua\n'
                return mock.Mock(stdout=result, returncode=0)

            for enabled in (False, True):
                with self.subTest(enabled=enabled):
                    output = root / ('enabled-bundle' if enabled else 'default-bundle')
                    forwarded.clear()
                    selection = ['--player-control-bank-census', '--stock-keyboard-observer', '--serial-pathing'] if enabled else []
                    with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                            contextlib.redirect_stdout(io.StringIO()):
                        live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                                   '--out', str(output), '--ticks', '4', *selection])
                    self.assertEqual('--player-control-bank-census' in forwarded, enabled)
                    manifest = json.loads((output / 'manifest.json').read_text())
                    self.assertEqual(manifest['player_control_bank_census'], enabled)
                    self.assertEqual(manifest['stock_keyboard_observer'], enabled)
                    self.assertEqual(manifest['serial_pathing'], enabled)
                    self.assertFalse(manifest['stock_keyboard_replay_probe'])
                    self.assertEqual(manifest['commit'], 'fixture-commit')
                    self.assertTrue(manifest['source_dirty'])
                    self.assertEqual(manifest['ticks'], 4)
                    self.assertEqual(manifest['snapshot_sha256'], live.sha(snapshot))
                    self.assertEqual(manifest['gate_so_sha256'], live.sha(output / 'extension/x4_lan_lab/native/x4lan_gate.so'))
                    for relative, digest in manifest['files'].items():
                        self.assertEqual(live.sha(output / relative), digest, relative)
                    config = defaults.parse((output / 'extension/x4_lan_lab/ui/config.lua').read_text())
                    self.assertEqual(config.get(KEY), 'true' if enabled else None)
                    self.assertNotIn(REPLAY_KEY, config)
                    self.assertFalse((output / 'extension/x4_lan_lab/ui/native_gate_linux_keyboard.lua').exists())

    def test_control_bank_census_remains_excluded_from_session_defaults(self):
        self.assertIn(KEY, defaults.EXCLUDED_KEYS)
        self.assertNotIn(KEY, defaults.ON_KEYS)
        session = {'native_gate_test': 'true', 'slot': "'p1'", 'port': '1234', 'token': "'test'"}
        values, report = defaults.apply(session)
        self.assertNotIn(KEY, values)
        self.assertNotIn(KEY, report['applied'])
        explicit, _ = defaults.apply({**session, KEY: 'true'})
        self.assertEqual(explicit[KEY], 'true')

    def test_census_can_accompany_retained_factory_reference(self):
        output = self.build('--player-control-bank-census', '--stock-keyboard-observer',
                            '--serial-pathing', '--player-factory-probe',
                            '--player-factory-retain-until', '4')
        config = defaults.parse((output / 'ui/config.lua.append').read_text())
        self.assertEqual(config[KEY], 'true')
        self.assertEqual(config['native_gate_player_factory_probe'], 'true')
        self.assertEqual(config['native_gate_player_factory_retain_until'], '4')

    def test_effective_run_tick_limits_refuse_before_host_checks_or_launch(self):
        cases = (
            ({'player_control_bank_census': True, 'ticks': 3}, None),
            ({'player_control_bank_census': True, 'ticks': 60}, 3),
            ({'player_factory_probe': True, 'player_factory_retain_until': 4, 'ticks': 4}, None),
            ({'player_factory_probe': True, 'player_factory_retain_until': 4, 'ticks': 60}, 4),
        )
        for manifest, override in cases:
            with self.subTest(manifest=manifest, override=override), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                bundle = home / 'bundle'
                bundle.mkdir()
                (bundle / live.MARKER).touch()
                (bundle / 'manifest.json').write_text(json.dumps(manifest))
                user = mock.Mock(pw_dir=str(home), pw_uid=live.os.geteuid())
                arguments = ['run', '--root', str(bundle), '--user', 'fixture-user']
                if override is not None:
                    arguments.extend(('--ticks', str(override)))
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live, 'snapshot_seed') as snapshot, \
                        mock.patch.object(live.subprocess, 'run') as command, \
                        self.assertRaises(SystemExit):
                    live.main(arguments)
                snapshot.assert_not_called()
                command.assert_not_called()

    def test_effective_run_valid_bounds_continue_to_snapshot_validation(self):
        cases = (
            ({'player_control_bank_census': True, 'ticks': 4}, None),
            ({'player_control_bank_census': True, 'ticks': 60}, 4),
            ({'player_factory_probe': True, 'player_factory_retain_until': 4, 'ticks': 5}, None),
            ({'player_factory_probe': True, 'player_factory_retain_until': 4, 'ticks': 60}, 5),
        )
        for manifest, override in cases:
            with self.subTest(manifest=manifest, override=override), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                bundle = home / 'bundle'
                bundle.mkdir()
                (bundle / live.MARKER).touch()
                config = bundle / 'extension/x4_lan_lab/ui/config.lua'
                config.parent.mkdir(parents=True)
                config.write_text('X4LAN_CONFIG = {}\n')
                (bundle / 'manifest.json').write_text(json.dumps({**manifest, 'files': {
                        'extension/x4_lan_lab/ui/config.lua': live.sha(config)}}))
                user = mock.Mock(pw_dir=str(home), pw_uid=live.os.geteuid())
                arguments = ['run', '--root', str(bundle), '--user', 'fixture-user']
                if override is not None:
                    arguments.extend(('--ticks', str(override)))
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live, 'snapshot_seed', side_effect=RuntimeError('snapshot check reached')) as snapshot, \
                        mock.patch.object(live.subprocess, 'run') as command, \
                        self.assertRaisesRegex(RuntimeError, 'snapshot check reached'):
                    live.main(arguments)
                snapshot.assert_called_once()
                command.assert_not_called()
