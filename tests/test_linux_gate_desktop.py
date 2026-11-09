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
    def test_remote_options_parse_for_run(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--relay-port', '45000', '--relay-token', 'a' * 32,
                       '--local-slots', '2', '--relay-host', '192.0.2.7', '--arm-at', '2000000000'])
        options = run.call_args.args[0]
        self.assertEqual((options.relay_host, options.arm_at, options.local_slots), ('192.0.2.7', 2000000000, '2'))

    def test_remote_session_validation_and_private_config(self):
        self.assertEqual(live.remote_session_options('192.0.2.7', 2000, True, now=1000),
                         ('192.0.2.7', 2000))
        for host, arm, external in [('192.0.2.7', None, False), (None, 2000, False),
                                    ("1.2.3.4'; os.execute('bad') --", None, True),
                                    ('256.2.3.4', None, True), (123, None, True),
                                    (None, 1000, True), (None, True, True), (None, 2000.0, True),
                                    (None, float('nan'), True), (None, float('inf'), True), (None, '2000', True)]:
            with self.subTest(host=host, arm=arm, external=external), self.assertRaises(ValueError):
                live.remote_session_options(host, arm, external, now=1000)
        with mock.patch.object(live.time, 'time', return_value=1000):
            config = live.case_config('slot=@X4LANE_SLOT@', 45000, 'a' * 32, 2, 2,
                                      relay_host='192.0.2.7', arm_at=2000)
        self.assertEqual(config, "slot=p2\nX4LAN_CONFIG.native_gate_relay_host = '192.0.2.7'\n"
                                 'X4LAN_CONFIG.native_gate_arm_at = 2000\n')
        self.assertEqual(live.case_config('slot=@X4LANE_SLOT@', 45000, 'a' * 32, 2, 2), 'slot=p2')
        with mock.patch.object(live.time, 'time', return_value=1000):
            overridden = live.case_config('X4LAN_CONFIG.native_gate_arm_at = 1100\n', 45000, 'a' * 32, 2, 2,
                                          relay_host='192.0.2.7', arm_at=2000)
        self.assertTrue(overridden.rstrip().endswith('X4LAN_CONFIG.native_gate_arm_at = 2000'))
        with self.assertRaises(ValueError):
            live.case_config('', 45000, 'a' * 32, 2, 2, relay_host=123)
        with self.assertRaises(ValueError):
            live.case_config('', 45000, 'a' * 32, 2, 2, arm_at=True)

    def test_remote_options_are_written_only_to_private_case(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle, game, home = root / 'bundle', root / 'game', root / 'home'
            (bundle / 'snapshot').mkdir(parents=True)
            (bundle / 'snapshot' / live.SLOT_SAVE).write_bytes(b'snapshot')
            template = bundle / 'extension/x4_lan_lab/ui/config.lua'
            template.parent.mkdir(parents=True)
            template.write_text('slot=@X4LANE_SLOT@\n')
            (game / 'extensions').mkdir(parents=True)
            home.mkdir()
            with mock.patch.object(live.time, 'time', return_value=1000):
                case, profile, *_ = live.stage_case(bundle, root / 'run', 2, game, home, 45000,
                                                    'a' * 32, 2, relay_host='192.0.2.7', arm_at=2000)
            config = (case / 'extensions/x4_lan_lab/ui/config.lua').read_text()
            self.assertIn("native_gate_relay_host = '192.0.2.7'", config)
            self.assertIn('native_gate_arm_at = 2000', config)
            self.assertEqual(template.read_text(), 'slot=@X4LANE_SLOT@\n')
            self.assertEqual((profile / 'save' / live.SLOT_SAVE).read_bytes(), b'snapshot')

    def test_status_stride_bundle_argument_and_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'snapshot.gz'
            snapshot.write_bytes(gzip.compress(b'<savegame><info><game seed="1"/></info></savegame>'))
            output = root / 'bundle'
            forwarded = []

            def run_command(command, **kwargs):
                if 'build_native_gate_linux.py' in str(command[1]):
                    (output / '.build/x4lan_gate.so').write_bytes(b'fake gate')
                elif 'build_native_gate_overlay.py' in str(command[1]):
                    forwarded.extend(command)
                return mock.Mock(stdout='', returncode=0)

            def compose(overlay, extension, extension_base):
                (extension / 'ui').mkdir(parents=True)
                (extension / 'native').mkdir()
                (extension / 'native/x4lan_gate.so').write_bytes(b'fake gate')
                (extension / 'ui/native_gate_probe.lua').write_text('-- fake')
                (extension / 'ui/sha256.lua').write_text('-- fake')
                (extension / 'ui/config.lua').write_text(
                    '@X4LANE_PORT@ @X4LANE_TOKEN@ @X4LANE_SLOT@ ' + live.sha(snapshot))

            with mock.patch.object(live.subprocess, 'run', side_effect=run_command), \
                    mock.patch.object(live, 'compose_extension_template', side_effect=compose):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                           '--out', str(output), '--status-stride', '17',
                           '--navmesh-post-observe', '1', '10',
                           '--battle', '--serial-pathing', '--nav-rng'])
            position = forwarded.index('--status-stride')
            self.assertEqual(forwarded[position + 1], '17')
            self.assertEqual(json.loads((output / 'manifest.json').read_text())['status_stride'], 17)
            position = forwarded.index('--navmesh-post-observe')
            self.assertEqual(forwarded[position + 1:position + 3], ['1', '10'])
            self.assertEqual(json.loads((output / 'manifest.json').read_text())['navmesh_post_observe'], [1, 10])
            self.assertTrue((output / 'navmesh_post_config.py').is_file())
            self.assertEqual(forwarded.count('--nav-rng'), 1)
            self.assertIn('--snapshot-prebind', forwarded)
            self.assertTrue(json.loads((output / 'manifest.json').read_text())['nav_rng'])

    def test_status_stride_bundle_rejects_invalid_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'bundle'
            for stride in (0, 3601):
                with self.subTest(stride=stride), self.assertRaises(SystemExit):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                               '--out', str(output), '--status-stride', str(stride)])
                self.assertFalse(output.exists())

    def test_linux_core_is_opt_in_and_keeps_explicit_modes(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main(command)
            manual = build.call_args.args[0]
            self.assertFalse(manual.md_timed_rng)
            self.assertIsNone(manual.find_order)
            self.assertFalse(manual.nav_rng)
            live.main([*command, '--preset', 'linux-core', '--find-order', 'observe'])
            preset = build.call_args.args[0]
            self.assertTrue(preset.md_timed_rng)
            self.assertTrue(preset.portable_math)
            self.assertTrue(preset.serial_pathing)
            self.assertEqual(preset.find_order, 'observe')
            self.assertFalse(preset.nav_key)
            self.assertFalse(preset.nav_rng)
            self.assertFalse(preset.build_rng)
            self.assertIsNone(preset.event_rng)
            self.assertFalse(preset.created_rows)
            live.main([*command, '--path-determinism', '3'])
            self.assertTrue(build.call_args.args[0].serial_pathing)
            live.main([*command, '--gravidar'])
            self.assertTrue(build.call_args.args[0].serial_pathing)

    def test_nav_rng_bundle_rejects_missing_fixture_serialization_or_key_conflict(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'bundle'
            for options in (['--nav-rng'], ['--battle', '--nav-rng'],
                            ['--battle', '--serial-pathing', '--nav-rng', '--nav-key']):
                with self.subTest(options=options), mock.patch.object(live.subprocess, 'run') as run:
                    with self.assertRaises(SystemExit):
                        live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                                   '--out', str(output), *options])
                    run.assert_not_called()
                    self.assertFalse(output.exists())

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

    def test_event_rng_options_parse_for_the_bundle(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main([*command, '--preset', 'linux-core', '--event-rng', 'observe',
                       '--event-rng-rows-window', '8400', '8460'])
            options = build.call_args.args[0]
            self.assertEqual(options.event_rng, 'observe')
            self.assertEqual(options.event_rng_rows_window, [8400, 8460])
            self.assertTrue(options.event_order)

    def test_build_rng_is_opt_in(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main([*command, '--preset', 'linux-core', '--build-rng'])
            self.assertTrue(build.call_args.args[0].build_rng)

    def test_economy_load_rng_is_opt_in(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main([*command, '--preset', 'linux-core'])
            self.assertFalse(build.call_args.args[0].economy_load_rng)
            live.main([*command, '--preset', 'linux-core', '--economy-load-rng'])
            self.assertTrue(build.call_args.args[0].economy_load_rng)

    def test_navmesh_post_observer_is_opt_in(self):
        command = ['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/output']
        with mock.patch.object(live, 'bundle') as build:
            live.main([*command, '--preset', 'linux-core'])
            self.assertIsNone(build.call_args.args[0].navmesh_post_observe)
            live.main([*command, '--preset', 'linux-core', '--navmesh-post-observe', '2', '8'])
            self.assertEqual(build.call_args.args[0].navmesh_post_observe, [2, 8])

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
                                 created_rows=rows, created_identities=identities, status_stride=None)
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
