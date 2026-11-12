"""Caller table sampling config and artifact pinning; no compiler or game."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import build_native_gate_overlay as overlay
import linux_gate_live as live
import test_lockstep_gate_linux as linux_fixture
from test_stock_keyboard_replay_overlay import snapshot_bytes


class RuntimeIdCallerStrideTests(unittest.TestCase):
    def test_bundle_rejects_invalid_stride_and_missing_dependencies_before_build(self):
        for options in (['--runtime-id-callers', '--runtime-ids', '--runtime-id-caller-stride', '-1'],
                        ['--runtime-id-callers', '--runtime-ids', '--runtime-id-caller-stride', '0'],
                        ['--runtime-id-callers', '--runtime-ids', '--runtime-id-caller-stride', '3601'],
                        ['--runtime-id-callers', '--runtime-ids', '--runtime-id-caller-stride', '1.5'],
                        ['--runtime-ids', '--runtime-id-caller-stride', '1'],
                        ['--runtime-id-callers', '--runtime-id-caller-stride', '1']):
            with self.subTest(options=options), mock.patch.object(live.subprocess, 'run') as command, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                           '--out', '/unused/output', *options])
            command.assert_not_called()

    def test_overlay_rejects_invalid_stride_or_missing_callers_before_output(self):
        for options in (['--runtime-ids', '--runtime-id-caller-stride', '1'],
                        ['--runtime-id-callers', '--runtime-id-caller-stride', '1'],
                        ['--runtime-ids', '--runtime-id-callers', '--runtime-id-caller-stride', '0'],
                        ['--runtime-ids', '--runtime-id-callers', '--runtime-id-caller-stride', '3601']):
            with mock.patch.object(sys, 'argv', ['overlay', '--out', '/unused/output', *options]), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                overlay.main()

    def test_overlay_config_is_optional_and_hashed(self):
        fixture = linux_fixture.Overlay()
        fixture.setUp()
        try:
            for stride in (None, 1, 3600):
                options = ['--mode', 'clock', '--world-identities', '--runtime-ids', '--runtime-id-callers']
                if stride is not None:
                    options += ['--runtime-id-caller-stride', str(stride)]
                output = fixture.build(*fixture.snapshot, *options)
                config = output / 'ui/config.lua.append'
                if stride is None:
                    self.assertNotIn('native_gate_runtime_ids_caller_stride', config.read_text())
                else:
                    self.assertIn(f'native_gate_runtime_ids_caller_stride = {stride}\n', config.read_text())
                manifest = json.loads((output / 'x4lane-manifest.json').read_text())
                self.assertEqual(live.sha(config), manifest['ui/config.lua.append'])
        finally:
            fixture.tearDown()

    def test_bundle_forwards_and_freezes_only_explicit_stride(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'
            snapshot.write_bytes(snapshot_bytes())
            for stride in (None, 1, 3600):
                output = root / f'bundle-{stride}'
                forwarded = []
                def command(arguments, **kwargs):
                    if 'build_native_gate_linux.py' in str(arguments[1]):
                        (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(arguments[1]):
                        forwarded.extend(arguments)
                        with mock.patch.object(sys, 'argv', arguments[1:]):
                            overlay.main()
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '', returncode=0)
                options = [] if stride is None else ['--runtime-id-caller-stride', str(stride)]
                with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                        contextlib.redirect_stdout(io.StringIO()):
                    live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot),
                               '--out', str(output), '--runtime-ids', '--runtime-id-callers', *options])
                manifest = json.loads((output / 'manifest.json').read_text())
                if stride is None:
                    self.assertNotIn('--runtime-id-caller-stride', forwarded)
                    self.assertNotIn('runtime_id_caller_stride', manifest)
                else:
                    self.assertEqual(forwarded[forwarded.index('--runtime-id-caller-stride') + 1], str(stride))
                    self.assertEqual(manifest['runtime_id_caller_stride'], stride)
                    config = (output / 'extension/x4_lan_lab/ui/config.lua').read_text()
                    self.assertIn(f'native_gate_runtime_ids_caller_stride = {stride}\n', config)
                for relative, digest in manifest['files'].items():
                    self.assertEqual(live.sha(output / relative), digest, relative)


if __name__ == '__main__':
    unittest.main()
