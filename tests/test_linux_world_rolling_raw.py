"""Explicit rolling raw-leaf configuration and pinned bundles; never launches X4."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import build_native_gate_overlay as overlay
import linux_gate_live as live
from test_stock_keyboard_replay_overlay import snapshot_bytes


class RollingRawTests(unittest.TestCase):
    ids = ['00000000000ad454', '0000000000011f87']
    common = ['--world-identities', '--created-identities', '--world-hash-stride', '120',
              '--world-hash-schema', 'WORLD_IMPORTED_2', '--world-log-mode', 'hashes',
              '--world-rolling-slices', '120', '--world-rolling-leaf-ticks', '118', '123']

    def test_strict_bounds_and_missing_dependency(self):
        live.validate_rolling_raw_ids(None, None)
        live.validate_rolling_raw_ids(self.ids, [118, 123])
        live.validate_rolling_raw_ids([f'{i:016x}' for i in range(1, 9)], [118, 123])
        for ids, ticks in ((self.ids, None), ([], [1, 2]), (['1'], [1, 2]),
                           (['0000000000000000'], [1, 2]), (['8000000000000000'], [1, 2]),
                           (['000000000000000A'], [1, 2]), (['000000000000000g'], [1, 2]),
                           (self.ids * 2, [1, 2]), ([f'{i:016x}' for i in range(1, 10)], [1, 2])):
            with self.subTest(ids=ids), self.assertRaises(SystemExit):
                live.validate_rolling_raw_ids(ids, ticks)
        for options in (['--world-rolling-raw-ids', *self.ids],
                        [*self.common, '--world-rolling-raw-ids', *self.ids, *self.ids]):
            with mock.patch.object(live.subprocess, 'run') as command, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save',
                           '--out', '/unused/out', *options])
            command.assert_not_called()
            with mock.patch.object(sys, 'argv', ['overlay', '--out', '/unused/out', *options]), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                overlay.main()

    def build(self, output, snapshot, ids, corrupt=False):
        forwarded = []
        def command(arguments, **kwargs):
            if 'build_native_gate_linux.py' in str(arguments[1]):
                (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
            elif 'build_native_gate_overlay.py' in str(arguments[1]):
                forwarded.extend(arguments)
                with mock.patch.object(sys, 'argv', arguments[1:]):
                    overlay.main()
                if corrupt:
                    # Discover the actual overlay destination from its argv.
                    config = Path(arguments[arguments.index('--out') + 1]) / 'ui/config.lua.append'
                    config.write_text(config.read_text().replace(self.ids[0], '0000000000000001'))
                    receipt = config.parent.parent / 'x4lane-manifest.json'
                    hashes = json.loads(receipt.read_text()); hashes['ui/config.lua.append'] = live.sha(config)
                    receipt.write_text(json.dumps(hashes))
            return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '', returncode=0)
        with mock.patch.object(live.subprocess, 'run', side_effect=command), \
                contextlib.redirect_stdout(io.StringIO()):
            live.main(['bundle', '--zig', '/unused/zig', '--snapshot', str(snapshot), '--out', str(output),
                       '--created-identities', '--world-log-mode', 'hashes', '--world-rolling-slices', '120',
                       '--world-rolling-leaf-ticks', '118', '123', *([] if ids is None else ['--world-rolling-raw-ids', *ids])])
        return forwarded

    def test_real_overlay_forward_config_manifest_and_source_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); snapshot = root / 'save.gz'; snapshot.write_bytes(snapshot_bytes())
            for ids in (None, self.ids):
                output = root / ('default' if ids is None else 'selected')
                forwarded = self.build(output, snapshot, ids)
                config = (output / 'extension/x4_lan_lab/ui/config.lua').read_text()
                manifest = json.loads((output / 'manifest.json').read_text())
                if ids is None:
                    self.assertNotIn('--world-rolling-raw-ids', forwarded)
                    self.assertNotIn('native_gate_world_rolling_raw_ids', config)
                    self.assertIsNone(manifest['world_rolling_raw_ids'])
                else:
                    offset = forwarded.index('--world-rolling-raw-ids')
                    self.assertEqual(forwarded[offset + 1:offset + 3], ids)
                    self.assertEqual(manifest['world_rolling_raw_ids'], ids)
                    self.assertIn('native_gate_world_rolling_raw_ids = {"' + '","'.join(ids) + '"}', config)
                for name in ('src/native/engine/linux/gate_world.h', 'src/native/engine/linux/gate_linux.c',
                             'tools/dev/native_gate_probe.lua', 'tools/dev/build_native_gate_overlay.py',
                             'tools/dev/linux_gate_live.py'):
                    relative = 'source-receipts/' + name
                    self.assertEqual((output / relative).read_bytes(), (ROOT / name).read_bytes())
                    self.assertEqual(live.sha(output / relative), manifest['files'][relative])
                for relative, digest in manifest['files'].items():
                    self.assertEqual(live.sha(output / relative), digest)

    def test_bundle_refuses_overlay_config_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); snapshot = root / 'save.gz'; snapshot.write_bytes(snapshot_bytes())
            with self.assertRaisesRegex(SystemExit, 'rolling raw config'):
                self.build(root / 'output', snapshot, self.ids, corrupt=True)


if __name__ == '__main__':
    unittest.main()
