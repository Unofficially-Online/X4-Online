"""Offline replay-probe plumbing checks; fake gate and Lua module, no X4 calls."""
import contextlib
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
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

KEY = 'native_gate_stock_keyboard_replay_probe'
MODULE = 'native_gate_linux_keyboard.lua'
MODULE_BYTES = b'X4LAN_LINUX_KEYBOARD = {} -- fixture only\n'
NEUTRAL_SHIP = '0000000000000100'


def snapshot_bytes():
    references = ''.join(f'<value type="component" value="[0x{saved:x}]"/>' for saved in range(0x100, 0x10c))
    ships = ''.join(
        f'<component id="[0x{saved:x}]" class="ship_s" '
        f'macro="ship_{"arg" if saved < 0x106 else "xen"}_s_fighter_01_a_macro" '
        f'owner="{"argon" if saved < 0x106 else "xenon"}"><movement class="navigation"/></component>'
        for saved in range(0x100, 0x10c)
    )
    return gzip.compress((
        '<savegame><info><game seed="1"/></info>'
        '<md><script name="X4LAN_NativeGate"><cue name="Spawn" state="complete">'
        '<vars><value name="$ships" type="list" value="42"/></vars></cue></script></md>'
        f'<script><refs type="list"><ref id="42">{references}</ref></refs></script>'
        f'<universe>{ships}</universe></savegame>'
    ).encode())


class StockKeyboardReplayOverlayTests(unittest.TestCase):
    def setUp(self):
        self.fixture = linux_tests.Overlay()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.copy_file = shutil.copy2

    def copy_with_module_fixture(self, source, destination, *args, **kwargs):
        if Path(source).name == MODULE:
            Path(destination).write_bytes(MODULE_BYTES)
            return destination
        return self.copy_file(source, destination, *args, **kwargs)

    def build(self, *options):
        root = Path(self.fixture.tmp.name)
        snapshot = root / 'save.xml.gz'
        snapshot.write_bytes(snapshot_bytes())
        gate = root / 'gate.so'
        gate.write_bytes(b'linux gate')
        output = root / f'out{len(list(root.iterdir()))}'
        arguments = ['overlay', '--gate-so', str(gate), '--snapshot', str(snapshot),
                     '--out', str(output), '--mode', 'clock', *options]
        with mock.patch.object(overlay_builder.shutil, 'copy2', side_effect=self.copy_with_module_fixture), \
                mock.patch.object(sys, 'argv', arguments), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            overlay_builder.main()
        return output

    def test_explicit_probe_ships_module_before_probe_and_pins_two_wire3_clients(self):
        output = self.build('--stock-keyboard-observer', '--stock-keyboard-replay-probe')
        config = (output / 'ui/config.lua.append').read_text()
        self.assertIn(f"X4LAN_CONFIG.{KEY} = {{last_capture=480, neutral_ship='{NEUTRAL_SHIP}'}}\n", config)
        self.assertNotIn('native_gate_snapshot_fixture', config)
        self.assertNotIn('native_gate_snapshot_prebind', config)
        self.assertIn('X4LAN_CONFIG.native_gate_stock_keyboard_observer = true\n', config)
        self.assertIn('X4LAN_CONFIG.native_gate_clients = 2\n', config)
        self.assertNotIn('@X4LANE_CLIENTS@', config)
        files = (output / 'ui.xml.files').read_text().splitlines()
        self.assertLess(files.index(f'ui/{MODULE}'), files.index('ui/native_gate_probe.lua'))
        self.assertIn('ui/native_gate_inputs_core.lua', files)
        self.assertEqual((output / 'ui' / MODULE).read_bytes(), MODULE_BYTES)
        manifest = json.loads((output / 'x4lane-manifest.json').read_text())
        for relative in ('ui/config.lua.append', f'ui/{MODULE}', 'native/x4lan_gate.so'):
            self.assertEqual(manifest[relative], hashlib.sha256((output / relative).read_bytes()).hexdigest())

    def test_default_and_observer_alone_leave_probe_and_module_absent(self):
        for options in ((), ('--stock-keyboard-observer',)):
            with self.subTest(options=options):
                output = self.build(*options)
                self.assertNotIn(KEY, (output / 'ui/config.lua.append').read_text())
                self.assertFalse((output / 'ui' / MODULE).exists())
                self.assertNotIn(f'ui/{MODULE}', (output / 'ui.xml.files').read_text())

    def test_probe_rejects_missing_dependencies_and_other_player_experiments(self):
        with self.assertRaises(SystemExit):
            self.build('--stock-keyboard-replay-probe')
        with self.assertRaises(SystemExit):
            self.fixture.build('--mode', 'clock', '--stock-keyboard-observer', '--stock-keyboard-replay-probe')
        for options in (('--player-probe', 'agreed-throttle'), ('--player-orders', '/unused/plan'),
                        ('--command-test', 'coop'), ('--docking-state',)):
            with self.subTest(options=options), self.assertRaises(SystemExit):
                self.build('--stock-keyboard-observer', '--stock-keyboard-replay-probe', *options)
        arguments = ['overlay', '--gate-dll', '/unused/gate.dll', *self.fixture.snapshot,
                     '--out', '/unused/output', '--stock-keyboard-observer', '--stock-keyboard-replay-probe']
        with mock.patch.object(sys, 'argv', arguments), contextlib.redirect_stderr(io.StringIO()), \
                self.assertRaises(SystemExit):
            overlay_builder.main()

    def test_missing_module_fails_instead_of_shipping_an_incomplete_overlay(self):
        def copy_missing_module(source, destination, *args, **kwargs):
            if Path(source).name == MODULE:
                raise FileNotFoundError('missing keyboard adapter')
            return self.copy_file(source, destination, *args, **kwargs)
        with mock.patch.object(overlay_builder.shutil, 'copy2', side_effect=copy_missing_module), \
                mock.patch.object(overlay_builder, 'snapshot_fixture', return_value=[NEUTRAL_SHIP]), \
                self.assertRaises(FileNotFoundError):
            self.fixture.build(*self.fixture.snapshot, '--mode', 'clock', '--stock-keyboard-observer',
                               '--stock-keyboard-replay-probe')

    def test_bundle_rejects_unsupported_selection_before_building(self):
        for options in ([], ['--stock-keyboard-observer', '--clients', '3'],
                        ['--stock-keyboard-observer', '--ticks', '4']):
            with self.subTest(options=options), mock.patch.object(live.subprocess, 'run') as command, \
                    self.assertRaises(SystemExit):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                           '--out', '/unused/output', '--stock-keyboard-replay-probe', *options])
            command.assert_not_called()

    def test_bundle_builds_actual_overlay_and_retains_config_and_file_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'
            snapshot.write_bytes(snapshot_bytes())
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
                    mock.patch.object(overlay_builder.shutil, 'copy2', side_effect=self.copy_with_module_fixture), \
                    contextlib.redirect_stdout(io.StringIO()):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot), '--out', str(output),
                           '--stock-keyboard-observer', '--stock-keyboard-replay-probe', '--ticks', '120'])
            self.assertIn('--stock-keyboard-replay-probe', forwarded)
            self.assertIn('--session-clients', forwarded)
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertTrue(manifest['stock_keyboard_replay_probe'])
            self.assertTrue(manifest['stock_keyboard_observer'])
            self.assertEqual(manifest['clients'], 2)
            self.assertEqual(manifest['commit'], 'fixture-commit')
            self.assertEqual(manifest['snapshot_sha256'], live.sha(snapshot))
            for relative, digest in manifest['files'].items():
                self.assertEqual(live.sha(output / relative), digest, relative)
            extension = output / 'extension/x4_lan_lab'
            config = (extension / 'ui/config.lua').read_text()
            self.assertIn(f"X4LAN_CONFIG.{KEY} = {{last_capture=118, neutral_ship='{NEUTRAL_SHIP}'}}", config)
            self.assertEqual(manifest['stock_keyboard_replay_last_capture'], 118)
            self.assertEqual(forwarded[forwarded.index('--stock-keyboard-replay-last-capture') + 1], '118')
            self.assertIn('X4LAN_CONFIG.native_gate_clients = 2', config)
            self.assertEqual((extension / 'ui' / MODULE).read_bytes(), MODULE_BYTES)
            output = root / 'default-bundle'
            forwarded.clear()
            with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                    mock.patch.object(overlay_builder.shutil, 'copy2', side_effect=self.copy_with_module_fixture), \
                    contextlib.redirect_stdout(io.StringIO()):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot), '--out', str(output)])
            default_manifest = json.loads((output / 'manifest.json').read_text())
            self.assertFalse(default_manifest['stock_keyboard_replay_probe'])
            self.assertFalse(default_manifest['stock_keyboard_observer'])
            self.assertIsNone(default_manifest['stock_keyboard_replay_last_capture'])
            self.assertNotIn('--stock-keyboard-replay-probe', forwarded)
            self.assertNotIn('--session-clients', forwarded)
            self.assertNotIn(f'extension/x4_lan_lab/ui/{MODULE}', default_manifest['files'])

    def test_capture_bound_is_explicit_and_requires_a_valid_saved_fixture(self):
        output = self.build('--stock-keyboard-observer', '--stock-keyboard-replay-probe',
                            '--stock-keyboard-replay-last-capture', '3')
        config = (output / 'ui/config.lua.append').read_text()
        self.assertIn(f"last_capture=3, neutral_ship='{NEUTRAL_SHIP}'", config)
        with self.assertRaises(SystemExit):
            self.build('--stock-keyboard-observer', '--stock-keyboard-replay-probe',
                       '--stock-keyboard-replay-last-capture', '2')
        with self.assertRaises(SystemExit):
            self.build('--stock-keyboard-replay-last-capture', '3')
        invalid = gzip.compress(b'<savegame/>')
        with mock.patch(__name__ + '.snapshot_bytes', return_value=invalid), self.assertRaises(SystemExit):
            self.build('--stock-keyboard-observer', '--stock-keyboard-replay-probe')

    def test_defaults_classify_probe_as_excluded_and_never_enable_it(self):
        self.assertIn(KEY, defaults.EXCLUDED_KEYS)
        self.assertNotIn(KEY, defaults.ON_KEYS)
        session = {'native_gate_test': 'true', 'slot': "'p1'", 'port': '1234', 'token': "'test'"}
        values, report = defaults.apply(session)
        self.assertNotIn(KEY, values)
        self.assertNotIn(KEY, report['applied'])
        explicit = f"{{last_capture=480, neutral_ship='{NEUTRAL_SHIP}'}}"
        values, _ = defaults.apply({**session, KEY: explicit})
        self.assertEqual(values[KEY], explicit)
