"""Seat preflight lifecycle and packaging integration; fake gate, no native game."""
import contextlib
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

from lupa.luajit21 import LuaError
import test_native_gate_probe as probe_tests
import test_lockstep_gate_linux as linux_tests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import build_native_gate_overlay as overlay_builder
import linux_gate_live as live
import native_gate_defaults as defaults

SOURCE = (ROOT / 'tools/dev/native_gate_probe.lua').read_text()
KEY = 'native_gate_player_seat_preflight'
DEPENDENCIES = ('--player-factory-probe', '--stock-keyboard-observer', '--serial-pathing')
VALID = (*DEPENDENCIES, '--player-factory-retain-until', '5', '--player-seat-preflight')


class SeatPreflightLuaTests(unittest.TestCase):
    def setUp(self):
        fixture = probe_tests.SnapshotProbeTests()
        fixture.setUp()
        self.lua = fixture.lua
        self.lua.execute('''
            X4LAN_CONFIG.native_gate_player_seat_preflight=true
            X4LAN_CONFIG.native_gate_player_factory_probe=true
            X4LAN_CONFIG.native_gate_player_factory_retain_until=5
            X4LAN_CONFIG.native_gate_stock_keyboard_observer=true
            X4LAN_CONFIG.native_gate_serial_pathing=true
            local gate=package.loadlib('gate')()
            gate.platform=function() return 'linux' end
            observer_requests=0;factory_requests=0;seat_requests=0;seat_reads=0
            gate.stock_keyboard_observer_request=function()
                assert(state.issued==1);observer_requests=observer_requests+1;return 1
            end
            gate.stock_keyboard_observer_status=function() return 'observer' end
            gate.stock_keyboard_observer_drain=function() return nil end
            gate.factory_probe_request=function(release_tick)
                assert(state.issued==1 and release_tick==5)
                factory_requests=factory_requests+1;return 1
            end
            gate.factory_probe_status=function() return 'factory' end
            gate.player_seat_preflight_request=function()
                assert(state.issued==2);seat_requests=seat_requests+1;return 1
            end
            gate.player_seat_preflight=function()
                seat_reads=seat_reads+1;return 'sample_tick='..state.issued
            end
            state.boot_phase=3;state.issued=1;is_menu=false
        ''')

    def logs(self):
        return list(self.lua.globals().logs.values())

    def test_request_once_at_two_status_at_two_three_four_and_stop(self):
        self.lua.execute(SOURCE)
        for tick in range(1, 5):
            self.lua.execute(f'state.issued={tick};update();update()')
        g = self.lua.globals()
        self.assertEqual((g.observer_requests, g.factory_requests, g.seat_requests), (1, 1, 1))
        self.assertEqual(g.seat_reads, 3)
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update();update()')
        self.assertEqual(g.seat_reads, 4)
        rows = [row for row in self.logs() if 'PLAYER_SEAT_PREFLIGHT' in row]
        self.assertEqual(len(rows), 4)
        for tick, row in zip((2, 3, 4), rows):
            self.assertIn(f'tick={tick} sample_tick={tick}', row)
        self.assertIn('PLAYER_SEAT_PREFLIGHT_STOP sample_tick=4', rows[-1])
        self.assertFalse(any('ERROR' in row for row in self.logs()))

    def test_default_off_never_requests_or_reads(self):
        self.lua.execute('X4LAN_CONFIG.native_gate_player_seat_preflight=nil')
        self.lua.execute(SOURCE)
        for tick in range(1, 5):
            self.lua.execute(f'state.issued={tick};update()')
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update()')
        self.assertEqual(self.lua.globals().seat_requests, 0)
        self.assertEqual(self.lua.globals().seat_reads, 0)
        self.assertFalse(any('PLAYER_SEAT_PREFLIGHT' in row for row in self.logs()))

    def test_missing_methods_or_dependencies_rejected_before_any_request(self):
        changes = (
            'gate.player_seat_preflight_request=nil', 'gate.player_seat_preflight=nil',
            'X4LAN_CONFIG.native_gate_player_factory_probe=false',
            'X4LAN_CONFIG.native_gate_player_factory_retain_until=nil',
            'X4LAN_CONFIG.native_gate_player_factory_retain_until=4',
            'X4LAN_CONFIG.native_gate_stock_keyboard_observer=false',
            'X4LAN_CONFIG.native_gate_serial_pathing=false',
            'X4LAN_CONFIG.native_gate_snapshot_sha=nil',
            "gate.platform=function() return 'windows' end",
        )
        for change in changes:
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute("local gate=package.loadlib('gate')();" + change)
                with self.assertRaises(LuaError):
                    self.lua.execute(SOURCE)
                g = self.lua.globals()
                self.assertEqual((g.seat_requests, g.factory_requests, g.observer_requests), (0, 0, 0))

    def test_refused_request_does_not_read_status_or_report_tick_two(self):
        self.lua.execute("package.loadlib('gate')().player_seat_preflight_request=function() return 0 end")
        self.lua.execute(SOURCE)
        self.lua.execute('update();state.issued=2;update()')
        self.assertEqual(self.lua.globals().seat_reads, 0)
        self.assertEqual(self.lua.globals().reports, 1)
        self.assertTrue(any('player seat preflight request refused' in row for row in self.logs()))


class SeatPreflightPackagingTests(unittest.TestCase):
    def setUp(self):
        self.fixture = linux_tests.Overlay()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)

    def test_overlay_selection_and_default_are_hash_bound(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                options = VALID if enabled else DEPENDENCIES
                output = self.fixture.build(*self.fixture.snapshot, '--mode', 'clock', *options)
                config = defaults.parse((output / 'ui/config.lua.append').read_text())
                self.assertEqual(config.get(KEY), 'true' if enabled else None)
                if enabled:
                    self.assertEqual(config['native_gate_player_factory_retain_until'], '5')
                manifest = json.loads((output / 'x4lane-manifest.json').read_text())
                for relative in ('ui/config.lua.append', 'ui/native_gate_probe.lua', 'native/x4lan_gate.so'):
                    self.assertEqual(live.sha(output / relative), manifest[relative])

    def test_overlay_and_bundle_require_dependencies_and_release_after_tick_four(self):
        invalid = [tuple(option for option in DEPENDENCIES if option != missing)
                   + ('--player-factory-retain-until', '5') for missing in DEPENDENCIES]
        invalid += [DEPENDENCIES, (*DEPENDENCIES, '--player-factory-retain-until', '4')]
        for options in invalid:
            with self.subTest(options=options), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    self.fixture.build(*self.fixture.snapshot, '--mode', 'clock',
                                       '--player-seat-preflight', *options)
                with tempfile.TemporaryDirectory() as directory:
                    output = Path(directory) / 'bundle'
                    with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
                        live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                                   '--out', str(output), '--ticks', '6', '--player-seat-preflight', *options])
                    command.assert_not_called()
                    self.assertFalse(output.exists())

    def test_bundle_preserves_selected_config_and_complete_file_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'
            snapshot.write_bytes(gzip.compress(b'<savegame><info><game seed="1"/></info></savegame>'))
            for enabled in (False, True):
                with self.subTest(enabled=enabled):
                    output = root / ('enabled' if enabled else 'default')
                    forwarded = []
                    def command(arguments, **kwargs):
                        if 'build_native_gate_linux.py' in str(arguments[1]):
                            (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                        elif 'build_native_gate_overlay.py' in str(arguments[1]):
                            forwarded.extend(arguments)
                            with mock.patch.object(sys, 'argv', arguments[1:]):
                                overlay_builder.main()
                        result = 'fixture-commit\n' if 'rev-parse' in arguments else ''
                        return mock.Mock(stdout=result, returncode=0)
                    with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                            contextlib.redirect_stdout(io.StringIO()):
                        live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                                   '--out', str(output), '--ticks', '6', *(VALID if enabled else ())])
                    self.assertEqual('--player-seat-preflight' in forwarded, enabled)
                    manifest = json.loads((output / 'manifest.json').read_text())
                    self.assertEqual(manifest['player_seat_preflight'], enabled)
                    self.assertEqual(manifest['player_factory_probe'], enabled)
                    self.assertEqual(manifest['stock_keyboard_observer'], enabled)
                    self.assertEqual(manifest['serial_pathing'], enabled)
                    self.assertEqual(manifest['player_factory_retain_until'], 5 if enabled else None)
                    self.assertEqual(manifest['ticks'], 6)
                    self.assertEqual(manifest['snapshot_sha256'], live.sha(snapshot))
                    for relative, digest in manifest['files'].items():
                        self.assertEqual(live.sha(output / relative), digest, relative)
                    config = defaults.parse((output / 'extension/x4_lan_lab/ui/config.lua').read_text())
                    self.assertEqual(config.get(KEY), 'true' if enabled else None)
                    self.assertNotIn('native_gate_stock_keyboard_replay_probe', config)

    def test_bundle_release_must_precede_stop_before_compilation(self):
        for ticks in (4, 5):
            with self.subTest(ticks=ticks), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'bundle'
                with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                               '--out', str(output), '--ticks', str(ticks), *VALID])
                command.assert_not_called()
                self.assertFalse(output.exists())

    def test_effective_run_ticks_cannot_truncate_owned_reference_release(self):
        for manifest_ticks, override in ((5, None), (60, 4), (60, 5), (6, 5)):
            with self.subTest(ticks=manifest_ticks, override=override), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                bundle = home / 'bundle'
                bundle.mkdir()
                (bundle / live.MARKER).touch()
                manifest = dict(player_seat_preflight=True, player_factory_probe=True,
                                player_factory_retain_until=5, stock_keyboard_observer=True,
                                serial_pathing=True, ticks=manifest_ticks)
                (bundle / 'manifest.json').write_text(json.dumps(manifest))
                user = mock.Mock(pw_dir=str(home), pw_uid=live.os.geteuid())
                arguments = ['run', '--root', str(bundle), '--user', 'fixture-user']
                if override is not None:
                    arguments.extend(('--ticks', str(override)))
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live, 'snapshot_seed') as snapshot, \
                        mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
                    live.main(arguments)
                snapshot.assert_not_called()
                command.assert_not_called()


if __name__ == '__main__':
    unittest.main()
