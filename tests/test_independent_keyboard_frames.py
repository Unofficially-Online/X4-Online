"""Explicit independent keyboard experiment plumbing; no native process."""
from copy import deepcopy
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
import test_stock_keyboard_replay_overlay as keyboard


class IndependentKeyboardFramesTests(unittest.TestCase):
    def options(self, frames=30):
        return dict(independent_keyboard_frames=frames, clients=2,
                    stock_keyboard_replay_probe=True, player_factory_probe=True,
                    stock_keyboard_observer=True, serial_pathing=True, player_lifetime_preload=True,
                    player_factory_retain_until=60)

    def test_bundle_requires_every_explicit_dependency_and_drain(self):
        good = self.options()
        self.assertEqual(live.validate_independent_keyboard(good, 61), 30)
        for field in ('stock_keyboard_replay_probe', 'player_factory_probe', 'stock_keyboard_observer',
                      'serial_pathing', 'player_lifetime_preload'):
            with self.subTest(field=field), self.assertRaises(SystemExit):
                live.validate_independent_keyboard(dict(good, **{field: False}), 61)
        for frames in (-1, 1, 601, True):
            self.assertRaises(SystemExit, live.validate_independent_keyboard, self.options(frames), 240)
        self.assertRaises(SystemExit, live.validate_independent_keyboard, dict(good, clients=3), 61)
        self.assertRaises(SystemExit, live.validate_independent_keyboard, good, 34)
        for field in ('player_control_bank_probe', 'player_control_boundary_probe',
                      'player_control_bank_census', 'player_reference_probe', 'player_seat_preflight'):
            self.assertRaises(SystemExit, live.validate_independent_keyboard, dict(good, **{field: True}), 61)

    def test_missing_bundle_flags_refuse_before_any_build(self):
        with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                       '--out', '/unused/out', '--independent-keyboard-frames', '30'])
        command.assert_not_called()

    def test_default_isolation_and_narrow_lifecycle_exception(self):
        self.assertEqual(live.validate_independent_keyboard({}, 180), 0)
        options = self.options()
        live.validate_lifecycle_discard(options, 180)
        self.assertRaises(SystemExit, live.validate_lifecycle_discard,
                          dict(options, independent_keyboard_frames=0), 180)
        self.assertRaises(SystemExit, live.validate_lifecycle_discard,
                          dict(options, serial_pathing=False), 180)

    def test_run_requires_same_manifest_window_and_explicit_physics(self):
        manifest = dict(self.options(), independent_keyboard_mode='independent_keyboard',
                        stock_keyboard_replay_last_capture=32)
        self.assertTrue(live.validate_network_physics(manifest, True, 30, 180))
        for physics, frames in ((False,30),(True,1),(True,29),(True,31)):
            self.assertRaises(SystemExit, live.validate_network_physics, manifest, physics, frames, 180)
        for key,value in (('stock_keyboard_replay_last_capture',31),('independent_keyboard_mode',None)):
            self.assertRaises(SystemExit, live.validate_network_physics, dict(manifest, **{key:value}), True,30,180)
        self.assertFalse(live.validate_network_physics({},True,30,180))
        self.assertRaises(SystemExit, live.validate_network_physics,
                          {'independent_keyboard_mode':'independent_keyboard'},True,30,180)

    def test_exact_native_environment_mode(self):
        self.assertEqual(live.physics_environment_mode(30,True),'network-frames-discard')
        self.assertEqual(live.physics_environment_mode(30),'bounded-frames-discard')
        self.assertEqual(live.physics_environment_mode(1),'single-frame-discard')

    def peers(self):
        records = {'1': ['0300', '0301'], '2': ['0302', '0303']}
        return {str(slot): {'pass': True, 'errors': [], 'slot': slot,
                           'slot_records': deepcopy(records), 'validation_pending': False}
                for slot in (1, 2)}

    def test_network_run_refuses_missing_bundled_collector_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'bundle'
            root.mkdir()
            (root / live.MARKER).touch()
            manifest = dict(self.options(2), ticks=180, files={},
                            player_lifetime_sha256='fixture',
                            independent_keyboard_mode='independent_keyboard',
                            stock_keyboard_replay_last_capture=4)
            (root / 'manifest.json').write_text(json.dumps(manifest))
            args = SimpleNamespace(user='fixture', root=root, ticks=None,
                                   controller_physics_discard=True, controller_physics_frames=2)
            user = SimpleNamespace(pw_dir=directory, pw_uid=os.geteuid())
            with mock.patch('pwd.getpwnam', return_value=user), \
                    mock.patch.object(live.subprocess, 'Popen') as launch, \
                    self.assertRaisesRegex(SystemExit, 'bundled strict network collector'):
                live.run(args)
            launch.assert_not_called()

    def test_network_dispatch_uses_strict_collector_and_slot(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr = Path(directory) / 'stderr'
            stderr.write_text('worker receipt\n')
            arguments = ('trace', 'retained', 'engine', 'stdout', 223)
            with mock.patch.object(live, 'collect_network_physics_evidence',
                                   return_value=self.peers()['2']) as network, \
                    mock.patch.object(live, 'collect_physics_evidence') as single, \
                    mock.patch.object(live, 'collect_recurring_physics_evidence') as scripted:
                result = live.collect_network_client(arguments, stderr, 2, 2)
            network.assert_called_once_with(*arguments, frame_count=2, stderr=stderr, slot=2)
            single.assert_not_called()
            scripted.assert_not_called()
            self.assertTrue(result['pass'])
            self.assertFalse(result['validation_pending'])
            self.assertEqual(result['stderr_sha256'], live.sha(stderr))

    def test_peer_records_require_both_clients_both_slots_and_exact_order(self):
        peers = self.peers()
        self.assertEqual(live.network_peer_errors(peers, 2), [])
        changed = deepcopy(peers)
        changed['2']['slot_records']['1'].reverse()
        self.assertTrue(live.network_peer_errors(changed, 2))
        changed = deepcopy(peers)
        changed['2']['slot_records']['2'][0] = 'ffff'
        self.assertTrue(live.network_peer_errors(changed, 2))
        self.assertTrue(live.network_peer_errors({'1': peers['1']}, 2))
        for slot_records in ({'1': ['0300', '0301']}, {'1': [], '2': []},
                             {'1': ['0300'], '2': ['0302', '0303']}):
            changed = deepcopy(peers)
            changed['2']['slot_records'] = slot_records
            self.assertTrue(live.network_peer_errors(changed, 2))
        for key, value in (('pass', False), ('validation_pending', True),
                           ('errors', ['fatal']), ('slot', 1)):
            changed = deepcopy(peers)
            changed['2'][key] = value
            self.assertTrue(live.network_peer_errors(changed, 2))

    def test_missing_native_and_fatal_network_evidence_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            engine, output, stderr = [root / name for name in ('engine', 'output', 'stderr')]
            engine.write_text('NATIVE_GATE ERROR invalid\n')
            output.write_text('')
            stderr.write_text('')
            result = live.collect_network_client(
                (root / 'missing', root / 'retained', engine, output, 223), stderr, 2, 1)
            self.assertFalse(result['pass'])
            self.assertTrue(any('unavailable' in error for error in result['errors']))
            self.assertTrue(any('NATIVE_GATE ERROR' in error for error in result['errors']))
            peers = self.peers()
            peers['1'] = result
            self.assertTrue(live.network_peer_errors(peers, 2))

    def test_overlay_explicit_window_and_default_replay_unchanged(self):
        fixture=keyboard.StockKeyboardReplayOverlayTests()
        fixture.setUp()
        try:
            common=('--stock-keyboard-observer','--serial-pathing','--player-factory-probe',
                    '--stock-keyboard-replay-probe','--independent-keyboard-frames','30',
                    '--stock-keyboard-replay-last-capture','32')
            output=fixture.build(*common)
            config=(output/'ui/config.lua.append').read_text()
            self.assertIn('last_capture=32',config)
            self.assertIn('independent_players=true, delay=2, lookahead=0',config)
            self.assertIn('X4LAN_CONFIG.native_gate_lookahead = 0\n',config)
            self.assertIn('native_gate_clients = 2',config)
            self.assertEqual(32+2,4+30)
            with self.assertRaises(SystemExit):fixture.build(*common[:-1],'33')
            with self.assertRaises(SystemExit):fixture.build('--independent-keyboard-frames','30')
            ordinary=fixture.build('--stock-keyboard-observer','--stock-keyboard-replay-probe')
            config=(ordinary/'ui/config.lua.append').read_text()
            self.assertNotIn('independent_players',config)
            self.assertNotIn('native_gate_lookahead = 0',config)
        finally:fixture.doCleanups()


if __name__ == '__main__':unittest.main()
