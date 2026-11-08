"""Desktop setup checks. These tests never launch X4 or alter a game profile."""
import hashlib
import gzip
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_gate_live as live


@unittest.skipUnless(sys.platform == "linux", "Linux desktop isolation is Linux-only")
class DesktopSetup(unittest.TestCase):
    def test_linux_core_is_opt_in_and_keeps_explicit_modes(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main(command)
            manual = build.call_args.args[0]
            self.assertFalse(manual.md_timed_rng)
            self.assertIsNone(manual.find_order)
            live.main([*command, '--preset', 'linux-core', '--find-order', 'observe'])
            preset = build.call_args.args[0]
            self.assertTrue(preset.md_timed_rng)
            self.assertTrue(preset.portable_math)
            self.assertTrue(preset.serial_pathing)
            self.assertEqual(preset.find_order, 'observe')
            self.assertFalse(preset.nav_key)
            self.assertFalse(preset.created_rows)
            live.main([*command, '--path-determinism', '3'])
            self.assertTrue(build.call_args.args[0].serial_pathing)
            live.main([*command, '--gravidar'])
            self.assertTrue(build.call_args.args[0].serial_pathing)

    def test_ordered_batches_replace_only_the_preset_snapshot_strategy(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main([*command, '--preset', 'linux-core'])
            self.assertTrue(build.call_args.args[0].travel_snapshot)
            self.assertFalse(build.call_args.args[0].travel_determinism)
            live.main([*command, '--preset', 'linux-core', '--travel-determinism'])
            options = build.call_args.args[0]
            self.assertTrue(options.travel_determinism)
            self.assertFalse(options.travel_snapshot)
            self.assertTrue(options.tempzone_order)
            with self.assertRaises(SystemExit):
                live.main([*command, '--travel-determinism', '--travel-snapshot'])

    def test_reload_seed_comes_from_snapshot_and_invalid_headers_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            save = Path(directory) / 'save.xml.gz'
            for seed in (0, 2222222, 0xffffffff):
                save.write_bytes(gzip.compress(f'<savegame><info><game seed="{seed}"/></info></savegame>'.encode()))
                self.assertEqual(live.snapshot_seed(save), seed)
            header = b'<savegame><info><game seed="2222222"/><description>'
            split_character = header + b'x' * (8191 - len(header)) + '\u00e9'.encode()
            save.write_bytes(gzip.compress(split_character + b'</description></info></savegame>'))
            self.assertEqual(live.snapshot_seed(save), 2222222)
            for seed in ('', '-1', '4294967296', 'invalid'):
                save.write_bytes(gzip.compress(f'<savegame><info><game seed="{seed}"/></info></savegame>'.encode()))
                with self.assertRaises(ValueError):
                    live.snapshot_seed(save)

    def test_invalid_bundle_options_do_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'bundle'
            for ticks, stride, rows, identities in ((0, 60, False, False), (600, 0, False, False),
                                                   (600, 60, True, False)):
                args = mock.Mock(out=output, ticks=ticks, stride=stride,
                                 created_rows=rows, created_identities=identities)
                with self.assertRaises(SystemExit):
                    live.bundle(args)
                self.assertFalse(output.exists())

    def test_current_user_needs_no_runuser(self):
        import pwd

        username = pwd.getpwuid(os.geteuid()).pw_name
        self.assertEqual(live.user_command(['bwrap', '--version'], username), ['bwrap', '--version'])

    def test_unprivileged_user_cannot_select_another_account(self):
        with mock.patch.object(live.os, 'geteuid', return_value=12345), mock.patch(
            'pwd.getpwuid', return_value=mock.Mock(pw_name='desktop-user')
        ):
            with self.assertRaisesRegex(SystemExit, 'cannot run as'):
                live.user_command(['bwrap'], 'another-user')

    def test_lavapipe_selection_avoids_the_32_bit_driver(self):
        with tempfile.TemporaryDirectory() as directory:
            drivers = Path(directory)
            (drivers / 'lvp_icd.i686.json').touch()
            self.assertIsNone(live.lavapipe_icd(drivers))
            (drivers / 'lvp_icd.x86_64.json').touch()
            self.assertEqual(live.lavapipe_icd(drivers), drivers / 'lvp_icd.x86_64.json')

    def test_desktop_display_requires_local_authentication(self):
        with tempfile.TemporaryDirectory() as directory:
            authorization = Path(directory) / 'auth'
            authorization.touch()
            with mock.patch.dict(os.environ, {'XAUTHORITY': str(authorization)}):
                self.assertEqual(live.desktop_display(':0'), (':0', authorization))
                with self.assertRaisesRegex(SystemExit, 'local X11'):
                    live.desktop_display('remote.example:0')
            with mock.patch.dict(os.environ, {'XAUTHORITY': str(authorization / 'missing')}):
                with self.assertRaisesRegex(SystemExit, 'XAUTHORITY'):
                    live.desktop_display(':0')

    def test_modern_extension_loads_sha256_before_the_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            overlay = Path(directory) / 'overlay'
            (overlay / 'ui').mkdir(parents=True)
            (overlay / 'ui/probe.lua').write_text('assert(X4LAN_SHA256)\n')
            (overlay / 'ui.xml.files').write_text('ui/probe.lua\n')
            (overlay / 'ui/config.lua.append').write_text('X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\n')
            manifest = {
                path.relative_to(overlay).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in overlay.rglob('*') if path.is_file()
            }
            (overlay / 'x4lane-manifest.json').write_text(json.dumps(manifest))
            target = Path(directory) / 'extension'
            live.compose_extension_template(overlay, target)
            registration = ET.parse(target / 'ui.xml')
            self.assertEqual([row.get('name') for row in registration.findall('.//file')],
                             ['ui/config.lua', 'ui/sha256.lua', 'ui/probe.lua'])
            self.assertIn('@X4LANE_SLOT@', (target / 'ui/config.lua').read_text())
            self.assertEqual(ET.parse(target / 'content.xml').getroot().get('save'), 'false')
            (overlay / 'ui/probe.lua').write_text('unverified change\n')
            with self.assertRaisesRegex(SystemExit, 'differs from its manifest'):
                live.compose_extension_template(overlay, Path(directory) / 'bad-extension')


if __name__ == '__main__':
    unittest.main()
