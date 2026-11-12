"""Offline mixed-platform preset checks; never compile or start a native client."""
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_gate_live as live


class MixedPlatformPreset(unittest.TestCase):
    command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
               '--out', '/unused/output', '--mixed-platform']

    def test_current_shared_preset_reaches_bundle_with_typed_parity_options(self):
        # Exercise the actual CLI -> apply_bundle_preset -> dynamically imported
        # current MIXED_PLATFORM_SETS path. No hand-written replacement preset.
        with mock.patch.object(live, 'bundle') as bundle:
            live.main(self.command)
        args = bundle.call_args.args[0]
        for name in ('ik_wrap', 'ik_step', 'nav_rng', 'nav_initial_refresh',
                     'attention_rng', 'attention_initial_refresh', 'shape_rng',
                     'shape_initial_refresh', 'radar_rng', 'radar_initial_refresh',
                     'steer_roll', 'avoid_angle', 'pursuit_speed'):
            self.assertIs(getattr(args, name), True, name)
        self.assertEqual(args.body1_mass, 'fallback')
        self.assertEqual(args.shape_refresh, 'view')
        self.assertEqual(args.fp_redirect, 'redirect')
        self.assertEqual(args.fp_redirect_solver, 'redirect')
        self.assertEqual(args.fp_redirect_drones, 'redirect')
        self.assertIs(type(args.fp_redirect_ticks), int)
        self.assertEqual(args.fp_redirect_ticks, 3)

    def test_shared_preset_keeps_matching_explicit_values_and_refuses_conflicts(self):
        with mock.patch.object(live, 'bundle') as bundle:
            live.main([*self.command, '--ik-wrap', '--pursuit-speed',
                       '--fp-redirect', 'redirect', '--body1-mass', 'fallback'])
            self.assertIs(bundle.call_args.args[0].pursuit_speed, True)
            bundle.reset_mock()
            with self.assertRaisesRegex(SystemExit, 'native_gate_fp_redirect'):
                live.main([*self.command, '--fp-redirect', 'shadow'])
            bundle.assert_not_called()

    def test_supported_lua_literals_have_exact_python_types(self):
        for literal, expected in [('true', True), ('false', False), ('  true  ', True),
                                  ('3', 3), ('-4', -4), ('0', 0),
                                  ("'redirect'", 'redirect'), ("'fallback'", 'fallback')]:
            with self.subTest(literal=literal):
                result = live.mixed_platform_literal(literal)
                self.assertIs(type(result), type(expected))
                self.assertEqual(result, expected)

    def test_expressions_and_unsupported_literals_are_refused(self):
        for literal in ('nil', 'True', '1.0', '1e3', '{}', '1+2', '01', '+3',
                        'true or false', "'view' .. 'x'", "'view", '"view"',
                        "'view'; os.execute('bad')", "'escaped\\n'", "'line\nbreak'"):
            with self.subTest(literal=literal), self.assertRaisesRegex(SystemExit, '--mixed-platform'):
                live.mixed_platform_literal(literal)


if __name__ == '__main__':
    unittest.main()
