"""Configuration and overlay checks for the Linux post observer."""
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))

import check_gate_options
import navmesh_post_config
import test_lockstep_gate_linux as linux_tests


class NavmeshPostConfigTests(unittest.TestCase):
    def test_window_bounds(self):
        navmesh_post_config.validate_window(None)
        navmesh_post_config.validate_window([1, 10], 10)
        for window, ticks in (([0, 1], 10), ([2, 1], 10), ([1, 11], 10),
                              ([1, 9007199254740992], None), ([1, True], None),
                              ([1.0, 2], None), ([1, 2, 3], None)):
            with self.subTest(window=window), self.assertRaises(ValueError):
                navmesh_post_config.validate_window(window, ticks)

    def test_overlay_requires_identities_and_is_off_by_default(self):
        fixture = linux_tests.Overlay()
        fixture.setUp()
        self.addCleanup(fixture.tearDown)
        base = [*fixture.snapshot, '--mode', 'clock', '--world-identities']
        config = (fixture.build(*base) / 'ui/config.lua.append').read_text()
        self.assertNotIn('native_gate_navmesh_post_observe', config)
        config = (fixture.build(*base, '--navmesh-post-observe', '3', '10') /
                  'ui/config.lua.append').read_text()
        self.assertIn('native_gate_navmesh_post_observe = {3,10}', config)
        for bad in (['--navmesh-post-observe', '0', '10'],
                    ['--navmesh-post-observe', '9', '2']):
            with self.assertRaises(SystemExit):
                fixture.build(*base, *bad)
        with self.assertRaises(SystemExit):
            fixture.build(*fixture.snapshot, '--mode', 'clock', '--navmesh-post-observe', '1', '2')

    def test_option_checker_requires_valid_window_and_prerequisites(self):
        prefix = 'X4LAN_CONFIG.native_gate_defaults = false\n'
        identities = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        snapshot = "X4LAN_CONFIG.native_gate_snapshot_sha = '" + 'a' * 64 + "'\n"
        option = 'X4LAN_CONFIG.native_gate_navmesh_post_observe = {1,2}\n'
        def problems(text):
            return [error for error in check_gate_options.check(text)[1]
                    if 'navmesh post observer' in error['rule']]
        self.assertEqual(problems(prefix + identities + snapshot + option), [])
        self.assertTrue(problems(prefix + snapshot + option))
        self.assertTrue(problems(prefix + identities + option))
        self.assertTrue(problems(prefix + identities + snapshot + option.replace('{1,2}', '{2,1}')))
        self.assertTrue(problems(prefix + identities + snapshot + option.replace('{1,2}', 'true')))
