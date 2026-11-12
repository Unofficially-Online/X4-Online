"""Player factory option plumbing with a stand-in gate; no X4 or compiler."""
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

KEY = 'native_gate_player_factory_probe'
REPLAY_KEY = 'native_gate_stock_keyboard_replay_probe'
RETAIN_KEY = 'native_gate_player_factory_retain_until'


class LinuxPlayerFactoryOverlayTests(unittest.TestCase):
    def setUp(self):
        self.fixture = linux_tests.Overlay()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)

    def build(self, *options):
        return self.fixture.build(*self.fixture.snapshot, '--mode', 'clock', *options)

    def test_overlay_explicit_selection_writes_config_and_hashes_it(self):
        output = self.build('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing')
        config = (output / 'ui/config.lua.append').read_text()
        self.assertEqual(defaults.parse(config)[KEY], 'true')
        self.assertEqual(defaults.parse(config)['native_gate_stock_keyboard_observer'], 'true')
        self.assertEqual(defaults.parse(config)['native_gate_serial_pathing'], 'true')
        self.assertIn('native_gate_snapshot_sha', defaults.parse(config))
        self.assertNotIn(REPLAY_KEY, defaults.parse(config))
        self.assertNotIn('native_gate_snapshot_fixture', defaults.parse(config))
        manifest = json.loads((output / 'x4lane-manifest.json').read_text())
        for relative in ('ui/config.lua.append', 'ui/native_gate_probe.lua', 'native/x4lan_gate.so'):
            self.assertEqual(live.sha(output / relative), manifest[relative])

    def test_default_and_observer_alone_do_not_request_factory_diagnostic(self):
        for options in ((), ('--stock-keyboard-observer', '--serial-pathing')):
            with self.subTest(options=options):
                output = self.build(*options)
                self.assertNotIn(KEY, defaults.parse((output / 'ui/config.lua.append').read_text()))

    def test_overlay_requires_all_dependencies_and_excludes_keyboard_replay(self):
        for options in (
            ('--player-factory-probe',),
            ('--player-factory-probe', '--stock-keyboard-observer'),
            ('--player-factory-probe', '--serial-pathing'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--stock-keyboard-replay-probe'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--player-reference-probe'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--player-probe', 'observe'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--command-test', 'pvp'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--player-orders', '/unused/orders.json'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--docking-state'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--command-replay', '/unused/barrier.json'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--docking-shape-rng'),
            ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing',
             '--docking-shape-refresh'),
        ):
            with self.subTest(options=options), self.assertRaises(SystemExit):
                self.build(*options)
        with self.assertRaises(SystemExit):
            self.fixture.build('--mode', 'clock', '--player-factory-probe',
                               '--stock-keyboard-observer', '--serial-pathing')
        output = Path(self.fixture.tmp.name) / 'windows-output'
        arguments = ['overlay', '--gate-dll', str(Path(self.fixture.tmp.name) / 'gate.dll'),
                     *self.fixture.snapshot, '--out', str(output), '--player-factory-probe',
                     '--stock-keyboard-observer', '--serial-pathing']
        with mock.patch.object(sys, 'argv', arguments), contextlib.redirect_stderr(io.StringIO()), \
                self.assertRaises(SystemExit):
            overlay_builder.main()
        self.assertFalse(output.exists())

    def test_bundle_refuses_invalid_selection_before_compilation_or_output(self):
        for options in (
            (), ('--stock-keyboard-observer',), ('--serial-pathing',),
            ('--stock-keyboard-observer', '--serial-pathing', '--ticks', '2'),
            ('--stock-keyboard-observer', '--serial-pathing', '--stock-keyboard-replay-probe'),
            ('--stock-keyboard-observer', '--serial-pathing', '--player-reference-probe'),
        ):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'bundle'
                with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                               '--out', str(output), '--player-factory-probe', *options])
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
                    selection = ['--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing'] if enabled else []
                    with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                            contextlib.redirect_stdout(io.StringIO()):
                        live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                                   '--out', str(output), '--ticks', '3', *selection])
                    self.assertEqual('--player-factory-probe' in forwarded, enabled)
                    manifest = json.loads((output / 'manifest.json').read_text())
                    self.assertEqual(manifest['player_factory_probe'], enabled)
                    self.assertEqual(manifest['stock_keyboard_observer'], enabled)
                    self.assertEqual(manifest['serial_pathing'], enabled)
                    self.assertFalse(manifest['stock_keyboard_replay_probe'])
                    self.assertEqual(manifest['commit'], 'fixture-commit')
                    self.assertTrue(manifest['source_dirty'])
                    self.assertEqual(manifest['ticks'], 3)
                    self.assertEqual(manifest['snapshot_sha256'], live.sha(snapshot))
                    self.assertEqual(manifest['gate_so_sha256'], live.sha(output / 'extension/x4_lan_lab/native/x4lan_gate.so'))
                    for relative, digest in manifest['files'].items():
                        self.assertEqual(live.sha(output / relative), digest, relative)
                    config = defaults.parse((output / 'extension/x4_lan_lab/ui/config.lua').read_text())
                    self.assertEqual(config.get(KEY), 'true' if enabled else None)
                    self.assertNotIn(REPLAY_KEY, config)
                    self.assertFalse((output / 'extension/x4_lan_lab/ui/native_gate_linux_keyboard.lua').exists())

    def test_overlay_retention_requires_probe_and_valid_integer(self):
        valid = ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing')
        output = self.build(*valid, '--player-factory-retain-until', '4')
        config = defaults.parse((output / 'ui/config.lua.append').read_text())
        self.assertEqual(config[RETAIN_KEY], '4')
        for extra in (('3',), ('0',), ('36001',), ('4.5',)):
            with self.subTest(value=extra[0]), self.assertRaises(SystemExit):
                self.build(*valid, '--player-factory-retain-until', *extra)
        with self.assertRaises(SystemExit):
            self.build('--stock-keyboard-observer', '--serial-pathing',
                       '--player-factory-retain-until', '4')
        default = self.build(*valid)
        self.assertNotIn(RETAIN_KEY, defaults.parse((default / 'ui/config.lua.append').read_text()))

    def test_bundle_retention_refuses_before_build(self):
        valid = ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing')
        for selection in (
            ('--player-factory-retain-until', '4'),
            (*valid, '--ticks', '5', '--player-factory-retain-until', '3'),
            (*valid, '--ticks', '5', '--player-factory-retain-until', '5'),
            (*valid, '--ticks', '5', '--player-factory-retain-until', '6'),
            (*valid, '--ticks', '40000', '--player-factory-retain-until', '36001'),
            (*valid, '--ticks', '5', '--player-factory-retain-until', '4.5'),
        ):
            with self.subTest(selection=selection), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'bundle'
                with mock.patch.object(live.subprocess, 'run') as command, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                               '--out', str(output), *selection])
                command.assert_not_called()
                self.assertFalse(output.exists())

    def test_run_override_must_not_end_before_release(self):
        live.validate_factory_retention(4, True, 5)
        for ticks in (3, 4):
            with self.subTest(ticks=ticks), self.assertRaises(SystemExit):
                live.validate_factory_retention(4, True, ticks)
        with self.assertRaises(SystemExit):
            live.validate_factory_retention(4, False, 5)

    def test_bundle_forwards_retention_and_manifest(self):
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
                return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '', returncode=0)

            with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                    contextlib.redirect_stdout(io.StringIO()):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                           '--out', str(output), '--ticks', '5', '--player-factory-probe',
                           '--stock-keyboard-observer', '--serial-pathing',
                           '--player-factory-retain-until', '4'])
            index = forwarded.index('--player-factory-retain-until')
            self.assertEqual(forwarded[index + 1], '4')
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['player_factory_retain_until'], 4)
            config = defaults.parse((output / 'extension/x4_lan_lab/ui/config.lua').read_text())
            self.assertEqual(config[RETAIN_KEY], '4')
            for relative, digest in manifest['files'].items():
                self.assertEqual(live.sha(output / relative), digest, relative)

    def test_factory_probe_remains_excluded_from_session_defaults(self):
        self.assertIn(RETAIN_KEY, defaults.EXCLUDED_KEYS)
        self.assertNotIn(RETAIN_KEY, defaults.ON_KEYS)
        self.assertIn(KEY, defaults.EXCLUDED_KEYS)
        self.assertNotIn(KEY, defaults.ON_KEYS)
        session = {'native_gate_test': 'true', 'slot': "'p1'", 'port': '1234', 'token': "'test'"}
        values, report = defaults.apply(session)
        self.assertNotIn(KEY, values)
        self.assertNotIn(KEY, report['applied'])
        explicit, _ = defaults.apply({**session, KEY: 'true'})
        self.assertEqual(explicit[KEY], 'true')
