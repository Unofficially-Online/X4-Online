"""Readiness-only boundary probe plumbing; mocked Lua/build/run, never native X4.

These tests exercise option policy and reporting. They do not prove the native
main/worker barrier, exclusion of other writers, or timeout222 execution.
"""
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
KEY = 'native_gate_player_control_boundary_probe'
OPTION = '--player-control-boundary-probe'
DEPENDENCIES = ('--stock-keyboard-observer', '--serial-pathing')
VALID = (OPTION, *DEPENDENCIES)
CONFLICTS = (
    ('--player-control-bank-probe',),
    ('--player-factory-probe',),
    ('--player-reference-probe',),
    ('--stock-keyboard-replay-probe',),
    ('--player-factory-probe', '--player-factory-retain-until', '5', '--player-seat-preflight'),
)
CONFLICT_KEYS = (
    'native_gate_player_control_bank_probe',
    'native_gate_player_factory_probe',
    'native_gate_player_reference_probe',
    'native_gate_stock_keyboard_replay_probe',
    'native_gate_player_seat_preflight',
)
MANIFEST_CONFLICT_KEYS = tuple(key.removeprefix('native_gate_') for key in CONFLICT_KEYS)


class ControlBoundaryProbeLuaTests(unittest.TestCase):
    def setUp(self):
        fixture = probe_tests.SnapshotProbeTests()
        fixture.setUp()
        self.lua = fixture.lua
        self.lua.execute('''
            X4LAN_CONFIG.native_gate_player_control_boundary_probe=true
            X4LAN_CONFIG.native_gate_stock_keyboard_observer=true
            X4LAN_CONFIG.native_gate_serial_pathing=true
            local gate=package.loadlib('gate')()
            gate.platform=function() return 'linux' end
            observer_requests=0;boundary_requests=0;boundary_reads=0
            gate.stock_keyboard_observer_request=function()
                assert(state.issued==1)
                observer_requests=observer_requests+1
                return 1
            end
            gate.stock_keyboard_observer_status=function() return 'observer' end
            gate.stock_keyboard_observer_drain=function() return nil end
            gate.player_control_boundary_request=function()
                assert(state.issued==2)
                boundary_requests=boundary_requests+1
                return 1
            end
            gate.player_control_boundary_status=function()
                boundary_reads=boundary_reads+1
                return 'status_tick='..state.issued..' readiness_only=1'
            end
            local function forbidden() error('unexpected mutation probe request') end
            gate.player_control_bank_probe_request=forbidden
            gate.factory_probe_request=forbidden
            gate.player_reference_probe_request=forbidden
            gate.player_seat_preflight_request=forbidden
            state.boot_phase=3;state.issued=1;is_menu=false
        ''')

    def logs(self):
        return list(self.lua.globals().logs.values())

    def test_request_once_at_two_status_from_two_through_six_and_stop(self):
        self.lua.execute(SOURCE)
        for tick in range(1, 7):
            self.lua.execute(f'state.issued={tick};update();update()')
        values = self.lua.globals()
        self.assertEqual((values.observer_requests, values.boundary_requests), (1, 1))
        self.assertEqual(values.boundary_reads, 5)
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update();update()')
        self.assertEqual(values.boundary_reads, 6)
        rows = [row for row in self.logs() if 'PLAYER_CONTROL_BOUNDARY' in row]
        self.assertEqual(len(rows), 6)
        for tick, row in zip(range(2, 7), rows):
            self.assertIn(f'tick={tick} status_tick={tick} readiness_only=1', row)
        self.assertIn('PLAYER_CONTROL_BOUNDARY_STOP status_tick=6 readiness_only=1', rows[-1])
        self.assertFalse(any('ERROR' in row for row in self.logs()))

    def test_default_off_never_requests_reads_or_logs(self):
        self.lua.execute('X4LAN_CONFIG.native_gate_player_control_boundary_probe=nil')
        self.lua.execute(SOURCE)
        for tick in range(1, 7):
            self.lua.execute(f'state.issued={tick};update()')
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update()')
        values = self.lua.globals()
        self.assertEqual((values.boundary_requests, values.boundary_reads), (0, 0))
        self.assertFalse(any('PLAYER_CONTROL_BOUNDARY' in row for row in self.logs()))

    def test_missing_methods_dependencies_or_conflicts_reject_before_any_request(self):
        changes = (
            'gate.player_control_boundary_request=nil',
            'gate.player_control_boundary_status=nil',
            'X4LAN_CONFIG.native_gate_stock_keyboard_observer=false',
            'X4LAN_CONFIG.native_gate_serial_pathing=false',
            'X4LAN_CONFIG.native_gate_snapshot_sha=nil',
            "gate.platform=function() return 'windows' end",
            *(f'X4LAN_CONFIG.{key}=true' for key in CONFLICT_KEYS),
        )
        for change in changes:
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute("local gate=package.loadlib('gate')();" + change)
                with self.assertRaises(LuaError):
                    self.lua.execute(SOURCE)
                values = self.lua.globals()
                self.assertEqual((values.observer_requests, values.boundary_requests,
                                  values.boundary_reads), (0, 0, 0))

    def test_refused_request_stops_before_status_or_tick_two_publication(self):
        self.lua.execute('''
            package.loadlib('gate')().player_control_boundary_request=function()
                assert(state.issued==2)
                boundary_requests=boundary_requests+1
                return 0
            end
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute('update();state.issued=2;update()')
        values = self.lua.globals()
        self.assertEqual((values.boundary_requests, values.boundary_reads), (1, 0))
        self.assertEqual(values.reports, 1)
        self.assertTrue(any('control-boundary request refused' in row for row in self.logs()))

    def test_census_can_coexist_without_another_mutation_probe(self):
        self.lua.execute('''
            X4LAN_CONFIG.native_gate_player_control_bank_census=true
            package.loadlib('gate')().player_control_bank_census_request=function()
                assert(state.issued==2)
                return 1
            end
            package.loadlib('gate')().player_control_bank_census=function() return 'census' end
        ''')
        self.lua.execute(SOURCE)
        for tick in range(1, 7):
            self.lua.execute(f'state.issued={tick};update()')
        self.assertEqual(self.lua.globals().boundary_requests, 1)
        self.assertEqual(self.lua.globals().boundary_reads, 5)
        self.assertFalse(any('ERROR' in row for row in self.logs()))

    def test_unavailable_status_stops_before_tick_two_publication(self):
        self.lua.execute('''
            package.loadlib('gate')().player_control_boundary_status=function()
                boundary_reads=boundary_reads+1
                return nil
            end
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute('update();state.issued=2;update()')
        values = self.lua.globals()
        self.assertEqual((values.boundary_requests, values.boundary_reads), (1, 1))
        self.assertEqual(values.reports, 1)
        self.assertTrue(any('ERROR' in row for row in self.logs()))


class ControlBoundaryProbePackagingTests(unittest.TestCase):
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
                for conflict_key in CONFLICT_KEYS:
                    self.assertNotIn(conflict_key, config)
                manifest = json.loads((output / 'x4lane-manifest.json').read_text())
                for relative in ('ui/config.lua.append', 'ui/native_gate_probe.lua', 'native/x4lan_gate.so'):
                    self.assertEqual(live.sha(output / relative), manifest[relative])

    def test_overlay_refuses_missing_dependencies_conflicts_windows_and_no_snapshot(self):
        invalid = [(OPTION,), (OPTION, '--stock-keyboard-observer'),
                   (OPTION, '--serial-pathing'), *(VALID + extra for extra in CONFLICTS)]
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

    def test_bundle_refuses_invalid_selection_before_any_build(self):
        invalid = [(OPTION,), (OPTION, '--stock-keyboard-observer'),
                   (OPTION, '--serial-pathing'), (*VALID, '--ticks', '5'),
                   *(VALID + extra for extra in CONFLICTS)]
        for options in invalid:
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'bundle'
                with mock.patch.object(live.subprocess, 'run') as command, \
                        self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save.gz',
                               '--out', str(output), '--ticks', '6', *options])
                command.assert_not_called()
                self.assertFalse(output.exists())

    def test_bundle_preserves_option_config_manifest_files_and_optional_census(self):
        selections = ((), VALID, (*VALID, '--player-control-bank-census'))
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            snapshot = temporary / 'save.gz'
            snapshot.write_bytes(gzip.compress(b'<savegame><info><game seed="1"/></info></savegame>'))
            for selection_index, selection in enumerate(selections):
                with self.subTest(selection=selection):
                    output = temporary / str(selection_index)
                    forwarded = []

                    def command(arguments, **kwargs):
                        if 'build_native_gate_linux.py' in str(arguments[1]):
                            (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                        elif 'build_native_gate_overlay.py' in str(arguments[1]):
                            forwarded.extend(arguments)
                            with mock.patch.object(sys, 'argv', arguments[1:]):
                                overlay_builder.main()
                        text = 'fixture-commit\n' if 'rev-parse' in arguments else ''
                        return mock.Mock(stdout=text, returncode=0)

                    with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                            contextlib.redirect_stdout(io.StringIO()):
                        live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                                   '--out', str(output), '--ticks', '6', *selection])
                    enabled = bool(selection)
                    self.assertEqual(OPTION in forwarded, enabled)
                    manifest = json.loads((output / 'manifest.json').read_text())
                    self.assertEqual(manifest['player_control_boundary_probe'], enabled)
                    self.assertEqual(manifest['player_control_bank_census'], selection_index == 2)
                    for key in MANIFEST_CONFLICT_KEYS:
                        self.assertFalse(manifest.get(key, False), key)
                    self.assertEqual(manifest['ticks'], 6)
                    self.assertEqual(manifest['snapshot_sha256'], live.sha(snapshot))
                    for relative, digest in manifest['files'].items():
                        self.assertEqual(live.sha(output / relative), digest, relative)
                    config = defaults.parse((output / 'extension/x4_lan_lab/ui/config.lua').read_text())
                    self.assertEqual(config.get(KEY), 'true' if enabled else None)
                    self.assertEqual(config.get('native_gate_player_control_bank_census'),
                                     'true' if selection_index == 2 else None)

    def run_manifest(self, manifest, override, expected_exception):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            bundle = home / 'bundle'
            bundle.mkdir()
            (bundle / live.MARKER).touch()
            config = bundle / 'extension/x4_lan_lab/ui/config.lua'
            config.parent.mkdir(parents=True)
            config.write_text('X4LAN_CONFIG = {}\n')
            manifest = dict(manifest, files={**manifest.get('files', {}),
                                             'extension/x4_lan_lab/ui/config.lua': live.sha(config)})
            (bundle / 'manifest.json').write_text(json.dumps(manifest))
            user = mock.Mock(pw_dir=str(home), pw_uid=live.os.geteuid())
            arguments = ['run', '--root', str(bundle), '--user', 'fixture-user']
            if override is not None:
                arguments.extend(('--ticks', str(override)))
            with mock.patch('pwd.getpwnam', return_value=user), \
                    mock.patch.object(live, 'snapshot_seed', side_effect=RuntimeError('snapshot boundary')) as snapshot, \
                    mock.patch.object(live.subprocess, 'run') as command, \
                    mock.patch.object(live.subprocess, 'Popen') as launch, \
                    self.assertRaises(expected_exception):
                live.main(arguments)
            command.assert_not_called()
            launch.assert_not_called()
            return snapshot.call_count

    def test_effective_ticks_reject_before_snapshot_or_process_operations(self):
        for manifest_ticks, override in ((5, None), (60, 5)):
            with self.subTest(ticks=manifest_ticks, override=override):
                manifest = {'player_control_boundary_probe': True, 'ticks': manifest_ticks}
                self.assertEqual(self.run_manifest(manifest, override, SystemExit), 0)

    def test_effective_six_tick_minimum_continues_to_snapshot_validation(self):
        for manifest_ticks, override in ((6, None), (60, 6), (5, 6)):
            with self.subTest(ticks=manifest_ticks, override=override):
                manifest = {'player_control_boundary_probe': True, 'stock_keyboard_observer': True,
                            'serial_pathing': True, 'ticks': manifest_ticks, 'files': {}}
                self.assertEqual(self.run_manifest(manifest, override, RuntimeError), 1)

    def test_effective_run_rejects_conflicts_and_missing_dependencies_before_processes(self):
        base = {'player_control_boundary_probe': True, 'stock_keyboard_observer': True,
                'serial_pathing': True, 'ticks': 6, 'files': {}}
        changes = ({'stock_keyboard_observer': False}, {'serial_pathing': False},
                   *({key: True} for key in MANIFEST_CONFLICT_KEYS))
        for change in changes:
            with self.subTest(change=change):
                self.assertEqual(self.run_manifest({**base, **change}, None, SystemExit), 0)

    def test_session_defaults_leave_probe_off_and_preserve_explicit_choice(self):
        session = {'native_gate_test': 'true', 'slot': "'p1'", 'port': '1234', 'token': "'fixture'"}
        values, report = defaults.apply(session)
        self.assertNotIn(KEY, values)
        self.assertNotIn(KEY, report['applied'])
        explicit, _ = defaults.apply({**session, KEY: 'true'})
        self.assertEqual(explicit[KEY], 'true')
        self.assertIn(KEY, dict(defaults.load()['excluded']))


if __name__ == '__main__':
    unittest.main()
