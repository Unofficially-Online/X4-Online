"""Disposable Linux external-camera option plumbing, without launching X4."""
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live
import test_stock_keyboard_replay_overlay as keyboard


class ExternalCameraBundleTest(unittest.TestCase):
    def options(self, frames=3):
        return dict(player_external_camera_probe=True, independent_keyboard_frames=frames,
                    independent_keyboard_mode='independent_keyboard', clients=2,
                    stock_keyboard_replay_probe=True, player_factory_probe=True,
                    stock_keyboard_observer=True, serial_pathing=True,
                    player_lifetime_sha256='pinned', stock_keyboard_replay_last_capture=frames+2)

    def test_default_off_and_strict_manifest_requirements(self):
        self.assertFalse(live.validate_external_camera({}, 0))
        self.assertTrue(live.validate_network_physics(self.options(), True, 3, 180))
        for value in (None, 0, 1, 'true'):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                live.validate_network_physics(dict(self.options(), player_external_camera_probe=value), True, 3, 180)
        for frames in (0, 2):
            with self.subTest(frames=frames), self.assertRaises(SystemExit):
                live.validate_network_physics(self.options(frames), True, frames, 180)
        for dependency in ('stock_keyboard_replay_probe', 'player_factory_probe',
                           'stock_keyboard_observer', 'serial_pathing', 'player_lifetime_sha256'):
            with self.subTest(dependency=dependency), self.assertRaises(SystemExit):
                live.validate_network_physics(dict(self.options(), **{dependency: False}), True, 3, 180)
        with self.assertRaises(SystemExit):
            live.validate_network_physics(dict(self.options(), clients=3), True, 3, 180)
        with self.assertRaises(SystemExit):
            live.validate_network_physics(self.options(), False, 3, 180)

    def test_frozen_config_refuses_mismatch_or_duplicate(self):
        line = 'X4LAN_CONFIG.native_gate_linux_external_camera_probe = true\n'
        live.validate_external_camera_config(line, True)
        live.validate_external_camera_config('', False)
        live.validate_external_camera_config(line.replace('true', 'false'), False)
        for config, enabled in (('', True), (line, False), (line + line, True),
                                (line.replace('true', '1'), True)):
            with self.subTest(config=config, enabled=enabled), self.assertRaises(SystemExit):
                live.validate_external_camera_config(config, enabled)

    def test_bundle_rejects_missing_window_before_build(self):
        for frames in ('0', '2'):
            with self.subTest(frames=frames), mock.patch.object(live.subprocess, 'run') as run, self.assertRaises(SystemExit):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                           '--out', '/unused/out', '--player-external-camera-probe',
                           '--independent-keyboard-frames', frames])
            run.assert_not_called()

    def test_overlay_config_is_opt_in_and_needs_independent_window(self):
        fixture = keyboard.StockKeyboardReplayOverlayTests()
        fixture.setUp()
        try:
            common = ('--stock-keyboard-observer', '--serial-pathing', '--player-factory-probe',
                      '--stock-keyboard-replay-probe', '--independent-keyboard-frames', '3',
                      '--stock-keyboard-replay-last-capture', '5')
            flag = 'X4LAN_CONFIG.native_gate_linux_external_camera_probe = true\n'
            self.assertNotIn(flag, (fixture.build(*common) / 'ui/config.lua.append').read_text())
            output = fixture.build(*common, '--player-external-camera-probe')
            self.assertIn(flag, (output / 'ui/config.lua.append').read_text())
            for options in (('--player-external-camera-probe',),
                            (*common[:5], '2', '--stock-keyboard-replay-last-capture', '4',
                             '--player-external-camera-probe')):
                with self.assertRaises(SystemExit): fixture.build(*options)
        finally:
            fixture.doCleanups()
