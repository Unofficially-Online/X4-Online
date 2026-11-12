"""Opt-in control-bank probe integration against fake Lua/native build boundaries."""
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
KEY = 'native_gate_player_control_bank_probe'
DEPENDENCIES = ('--stock-keyboard-observer', '--serial-pathing')
VALID = ('--player-control-bank-probe', *DEPENDENCIES)


class ControlBankProbeLuaTests(unittest.TestCase):
    def setUp(self):
        fixture = probe_tests.SnapshotProbeTests()
        fixture.setUp()
        self.lua = fixture.lua
        self.lua.execute('''
            X4LAN_CONFIG.native_gate_player_control_bank_probe=true
            X4LAN_CONFIG.native_gate_stock_keyboard_observer=true
            X4LAN_CONFIG.native_gate_serial_pathing=true
            local gate=package.loadlib('gate')()
            gate.platform=function() return 'linux' end
            observer_requests=0;bank_requests=0;bank_reads=0
            gate.stock_keyboard_observer_request=function()
                assert(state.issued==1);observer_requests=observer_requests+1;return 1
            end
            gate.stock_keyboard_observer_status=function() return 'observer' end
            gate.stock_keyboard_observer_drain=function() return nil end
            gate.player_control_bank_probe_request=function()
                assert(state.issued==2);bank_requests=bank_requests+1;return 1
            end
            gate.player_control_bank_probe_status=function()
                bank_reads=bank_reads+1;return 'sample_tick='..state.issued
            end
            state.boot_phase=3;state.issued=1;is_menu=false
        ''')

    def logs(self):
        return list(self.lua.globals().logs.values())

    def test_request_once_at_two_status_through_six_and_stop(self):
        self.lua.execute(SOURCE)
        for tick in range(1, 7):
            self.lua.execute(f'state.issued={tick};update();update()')
        g = self.lua.globals()
        self.assertEqual((g.observer_requests, g.bank_requests), (1, 1))
        self.assertEqual(g.bank_reads, 5)
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update();update()')
        self.assertEqual(g.bank_reads, 6)
        rows = [row for row in self.logs() if 'PLAYER_CONTROL_BANK_PROBE' in row]
        self.assertEqual(len(rows), 6)
        for tick, row in zip(range(2, 7), rows):
            self.assertIn(f'tick={tick} sample_tick={tick}', row)
        self.assertIn('PLAYER_CONTROL_BANK_PROBE_STOP sample_tick=6', rows[-1])
        self.assertFalse(any('ERROR' in row for row in self.logs()))

    def test_default_off_never_requests_reads_or_logs(self):
        self.lua.execute('X4LAN_CONFIG.native_gate_player_control_bank_probe=nil')
        self.lua.execute(SOURCE)
        for tick in range(1, 7):
            self.lua.execute(f'state.issued={tick};update()')
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update()')
        g = self.lua.globals()
        self.assertEqual((g.bank_requests, g.bank_reads), (0, 0))
        self.assertFalse(any('PLAYER_CONTROL_BANK_PROBE' in row for row in self.logs()))

    def test_missing_api_dependencies_or_conflicts_reject_before_request(self):
        changes = (
            'gate.player_control_bank_probe_request=nil', 'gate.player_control_bank_probe_status=nil',
            'X4LAN_CONFIG.native_gate_stock_keyboard_observer=false',
            'X4LAN_CONFIG.native_gate_serial_pathing=false',
            'X4LAN_CONFIG.native_gate_snapshot_sha=nil',
            "gate.platform=function() return 'windows' end",
            'X4LAN_CONFIG.native_gate_stock_keyboard_replay_probe=true',
            'X4LAN_CONFIG.native_gate_player_reference_probe=true',
        )
        for change in changes:
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute("local gate=package.loadlib('gate')();" + change)
                with self.assertRaises(LuaError):
                    self.lua.execute(SOURCE)
                g = self.lua.globals()
                self.assertEqual((g.bank_requests, g.bank_reads, g.observer_requests), (0, 0, 0))

    def test_refused_request_stops_before_status_and_tick_report(self):
        self.lua.execute("package.loadlib('gate')().player_control_bank_probe_request=function() return 0 end")
        self.lua.execute(SOURCE)
        self.lua.execute('update();state.issued=2;update()')
        self.assertEqual(self.lua.globals().bank_reads, 0)
        self.assertEqual(self.lua.globals().reports, 1)
        self.assertTrue(any('control-bank probe request refused' in row for row in self.logs()))


class ControlBankProbePackagingTests(unittest.TestCase):
    def setUp(self):
        self.fixture = linux_tests.Overlay()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)

    def build(self, *options):
        return self.fixture.build(*self.fixture.snapshot, '--mode', 'clock', *options)

    def test_overlay_explicit_selection_and_default_are_hash_bound(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                output = self.build(*(VALID if enabled else DEPENDENCIES))
                config = defaults.parse((output / 'ui/config.lua.append').read_text())
                self.assertEqual(config.get(KEY), 'true' if enabled else None)
                manifest = json.loads((output / 'x4lane-manifest.json').read_text())
                for relative in ('ui/config.lua.append', 'ui/native_gate_probe.lua', 'native/x4lan_gate.so'):
                    self.assertEqual(live.sha(output / relative), manifest[relative])

    def test_overlay_rejects_missing_dependencies_conflicts_and_nonlinux_snapshot(self):
        invalid = [('--player-control-bank-probe',),
                   ('--player-control-bank-probe', '--stock-keyboard-observer'),
                   ('--player-control-bank-probe', '--serial-pathing'),
                   (*VALID, '--stock-keyboard-replay-probe'), (*VALID, '--player-reference-probe')]
        with contextlib.redirect_stderr(io.StringIO()):
            for options in invalid:
                with self.subTest(options=options), self.assertRaises(SystemExit):
                    self.build(*options)
            with self.assertRaises(SystemExit):
                self.fixture.build('--mode', 'clock', *VALID)
            output = Path(self.fixture.tmp.name) / 'windows'
            arguments = ['overlay', '--gate-dll', str(Path(self.fixture.tmp.name) / 'gate.dll'),
                         *self.fixture.snapshot, '--out', str(output), *VALID]
            with mock.patch.object(sys, 'argv', arguments), self.assertRaises(SystemExit):
                overlay_builder.main()
            self.assertFalse(output.exists())

    def test_bundle_refuses_invalid_selection_before_build(self):
        invalid = [('--player-control-bank-probe',),
                   ('--player-control-bank-probe', '--stock-keyboard-observer'),
                   ('--player-control-bank-probe', '--serial-pathing'),
                   (*VALID, '--ticks', '5'), (*VALID, '--stock-keyboard-replay-probe'),
                   (*VALID, '--player-reference-probe')]
        for options in invalid:
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'bundle'
                with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                               '--out', str(output), '--ticks', '6', *options])
                command.assert_not_called()
                self.assertFalse(output.exists())

    def test_bundle_preserves_config_manifest_provenance_and_optional_diagnostics(self):
        selections = ((), VALID, (*VALID, '--player-control-bank-census', '--player-factory-probe',
                                  '--player-factory-retain-until', '5', '--player-seat-preflight'))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'
            snapshot.write_bytes(gzip.compress(b'<savegame><info><game seed="1"/></info></savegame>'))
            for index, selection in enumerate(selections):
                with self.subTest(selection=selection):
                    output = root / str(index)
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
                                   '--out', str(output), '--ticks', '6', *selection])
                    enabled = bool(selection)
                    self.assertEqual('--player-control-bank-probe' in forwarded, enabled)
                    manifest = json.loads((output / 'manifest.json').read_text())
                    self.assertEqual(manifest['player_control_bank_probe'], enabled)
                    self.assertFalse(manifest['stock_keyboard_replay_probe'])
                    self.assertEqual(manifest['ticks'], 6)
                    self.assertEqual(manifest['snapshot_sha256'], live.sha(snapshot))
                    for relative, digest in manifest['files'].items():
                        self.assertEqual(live.sha(output / relative), digest, relative)
                    config = defaults.parse((output / 'extension/x4_lan_lab/ui/config.lua').read_text())
                    self.assertEqual(config.get(KEY), 'true' if enabled else None)
                    if index == 2:
                        for key in ('native_gate_player_control_bank_census', 'native_gate_player_factory_probe',
                                    'native_gate_player_seat_preflight'):
                            self.assertEqual(config[key], 'true')

    def test_effective_ticks_reject_before_snapshot_port_or_process_operations(self):
        for manifest_ticks, override in ((5, None), (60, 5)):
            with self.subTest(ticks=manifest_ticks, override=override), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                bundle = home / 'bundle'
                bundle.mkdir()
                (bundle / live.MARKER).touch()
                (bundle / 'manifest.json').write_text(json.dumps({
                    'player_control_bank_probe': True, 'ticks': manifest_ticks}))
                user = mock.Mock(pw_dir=str(home), pw_uid=live.os.geteuid())
                arguments = ['run', '--root', str(bundle), '--user', 'fixture-user']
                if override is not None:
                    arguments.extend(('--ticks', str(override)))
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live, 'snapshot_seed') as snapshot, \
                        mock.patch.object(live.subprocess, 'run') as command, \
                        mock.patch.object(live.subprocess, 'Popen') as launch, self.assertRaises(SystemExit):
                    live.main(arguments)
                snapshot.assert_not_called()
                command.assert_not_called()
                launch.assert_not_called()

    def test_effective_six_tick_boundary_continues_to_snapshot_validation(self):
        for manifest_ticks, override in ((6, None), (60, 6)):
            with self.subTest(ticks=manifest_ticks, override=override), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                bundle = home / 'bundle'
                bundle.mkdir()
                (bundle / live.MARKER).touch()
                config = bundle / 'extension/x4_lan_lab/ui/config.lua'
                config.parent.mkdir(parents=True)
                config.write_text('X4LAN_CONFIG = {}\n')
                (bundle / 'manifest.json').write_text(json.dumps({
                    'player_control_bank_probe': True, 'stock_keyboard_observer': True,
                    'serial_pathing': True, 'ticks': manifest_ticks, 'files': {
                        'extension/x4_lan_lab/ui/config.lua': live.sha(config)}}))
                user = mock.Mock(pw_dir=str(home), pw_uid=live.os.geteuid())
                arguments = ['run', '--root', str(bundle), '--user', 'fixture-user']
                if override is not None:
                    arguments.extend(('--ticks', str(override)))
                with mock.patch('pwd.getpwnam', return_value=user), \
                        mock.patch.object(live, 'snapshot_seed', side_effect=RuntimeError('snapshot boundary')) as snapshot, \
                        mock.patch.object(live.subprocess, 'Popen') as launch, \
                        self.assertRaisesRegex(RuntimeError, 'snapshot boundary'):
                    live.main(arguments)
                snapshot.assert_called_once()
                launch.assert_not_called()

    def test_session_defaults_leave_probe_off_and_preserve_explicit_selection(self):
        session = {'native_gate_test': 'true', 'slot': "'p1'", 'port': '1234', 'token': "'fixture'"}
        values, report = defaults.apply(session)
        self.assertNotIn(KEY, values)
        self.assertNotIn(KEY, report['applied'])
        explicit, _ = defaults.apply({**session, KEY: 'true'})
        self.assertEqual(explicit[KEY], 'true')


if __name__ == '__main__':
    unittest.main()
