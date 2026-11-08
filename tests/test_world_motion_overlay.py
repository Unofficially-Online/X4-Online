"""Builder/config guards use temporary stand-ins; no compiler or game is run."""
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import test_lockstep_gate_linux as linux_tests
from test_world_motion_evidence import SAVED
from world_motion_evidence import configured_watch
import linux_gate_live as live
import check_gate_options


class MotionOverlayTests(unittest.TestCase):
    def test_overlay_accepts_independent_watch_with_rolling_and_checks_bounds(self):
        fixture = linux_tests.Overlay()
        fixture.setUp()
        self.addCleanup(fixture.tearDown)
        base = [*fixture.snapshot, '--mode', 'clock', '--world-identities']
        selected = ['--world-motion-watch', SAVED, '--world-motion-window', '8000', '12000']
        out = fixture.build(*base, *selected, '--world-hash-stride', '120', '--world-hash-schema', 'WORLD_IMPORTED_2',
                            '--world-log-mode', 'hashes', '--world-rolling-slices', '120', '--created-identities')
        config = (out / 'ui/config.lua.append').read_text()
        self.assertEqual(configured_watch(config), [[SAVED], [8000, 12000]])
        for options in (['--world-motion-watch', SAVED],
                        ['--world-motion-watch', SAVED, '--world-motion-window', '1', '18001']):
            with self.assertRaises(SystemExit):
                fixture.build(*base, *options)
        bare = (fixture.build(*base) / 'ui/config.lua.append').read_text()
        self.assertNotIn('native_gate_world_motion_', bare)

    def test_bundle_forwards_and_retains_explicit_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'
            snapshot.write_bytes(gzip.compress(b'<savegame><info><game seed="1"/></info></savegame>'))
            output, forwarded = root / 'bundle', []

            def command(arguments, **kwargs):
                if 'build_native_gate_linux.py' in str(arguments[1]):
                    (output / '.build/x4lan_gate.so').write_bytes(b'fake gate')
                elif 'build_native_gate_overlay.py' in str(arguments[1]):
                    forwarded.extend(arguments)
                return mock.Mock(stdout='', returncode=0)

            def compose(overlay, extension, extension_base):
                (extension / 'ui').mkdir(parents=True)
                (extension / 'native').mkdir()
                (extension / 'native/x4lan_gate.so').write_bytes(b'fake gate')
                (extension / 'ui/native_gate_probe.lua').write_text('-- fake')
                (extension / 'ui/sha256.lua').write_text('-- fake')
                (extension / 'ui/config.lua').write_text('@X4LANE_PORT@ @X4LANE_TOKEN@ @X4LANE_SLOT@ ' + live.sha(snapshot))

            with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                    mock.patch.object(live, 'compose_extension_template', side_effect=compose):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot), '--out', str(output),
                           '--ticks', '12000', '--world-motion-watch', SAVED, '--world-motion-window', '8000', '12000'])
            self.assertIn('--world-motion-watch', forwarded)
            self.assertEqual(forwarded[forwarded.index('--world-motion-window') + 1:][:2], ['8000', '12000'])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['world_motion_watch'], [SAVED])
            self.assertEqual(manifest['world_motion_window'], [8000, 12000])
            self.assertIn('world_motion_evidence.py', manifest['files'])
            self.assertIn('native_world_evidence.py', manifest['files'])

    def test_config_guard_rejects_missing_window(self):
        _, errors = check_gate_options.check_effective(f"X4LAN_CONFIG.native_gate_world_motion_watch={{'{SAVED}'}}")
        self.assertTrue(any('world motion' in error['rule'] for error in errors))


if __name__ == '__main__':
    unittest.main()
