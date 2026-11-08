"""Exercise the Linux observer's explicit configuration and bounds."""
import unittest

import check_gate_options
import economy_watch_config
import test_lockstep_gate_linux as linux_tests

SAVED = '0000000000036fe9'


class EconomyWatchConfigurationTests(unittest.TestCase):
    def test_bounds_and_required_pair(self):
        economy_watch_config.validate_configuration(SAVED, [5, 36004], 36004)
        for saved, window, ticks in ((SAVED, None, None), ('0' * 16, [1, 2], None),
                                     (SAVED, [1, 36001], None), (SAVED, [2, 1], None),
                                     (SAVED, [1, 3], 2)):
            with self.subTest(saved=saved, window=window), self.assertRaises(ValueError):
                economy_watch_config.validate_configuration(saved, window, ticks)

    def test_checker_rejects_incomplete_configuration(self):
        _, errors = check_gate_options.check_effective(
            "X4LAN_CONFIG.native_gate_economy_watch='" + SAVED + "'")
        self.assertTrue(any('economy watch' in error['rule'] for error in errors))

    def test_overlay_explicit_selection_and_default_off(self):
        fixture = linux_tests.Overlay()
        fixture.setUp()
        self.addCleanup(fixture.tearDown)
        base = [*fixture.snapshot, '--mode', 'clock', '--world-identities']
        output = fixture.build(*base, '--economy-watch', SAVED,
                               '--economy-watch-window', '2', '3')
        config = (output / 'ui/config.lua.append').read_text()
        self.assertIn("native_gate_economy_watch = '" + SAVED + "'", config)
        self.assertIn('native_gate_economy_watch_window = {2,3}', config)
        with self.assertRaises(SystemExit):
            fixture.build(*base, '--economy-watch', SAVED)
        config = (fixture.build(*base) / 'ui/config.lua.append').read_text()
        self.assertNotIn('native_gate_economy_watch', config)
