"""Default-off observer plumbing; synthetic files only, never launches X4."""
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
import linux_gate_live as live
import build_player_view_query_preload as builder
import build_native_gate_overlay as overlay
from test_stock_keyboard_replay_overlay import snapshot_bytes


class ViewQueryBundleTests(unittest.TestCase):
    def options(self):
        return dict(player_view_query_observer=True, independent_keyboard_frames=4,
                    independent_keyboard_mode='independent_keyboard', clients=2,
                    stock_keyboard_replay_probe=True, player_factory_probe=True,
                    stock_keyboard_observer=True, serial_pathing=True,
                    player_lifetime_sha256='pinned', stock_keyboard_replay_last_capture=6)

    def fixture(self, root):
        manifest = self.options()
        sources = []
        for name in live.VIEW_QUERY_SOURCES:
            source = root / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(('fixture ' + name).encode())
            sources.append(source)
        self.assertEqual(live.view_query_sources(root), sources)
        live.freeze_source_receipts(root, root, sources)
        lib = root / 'player_view_query' / live.PLAYER_VIEW_QUERY
        lib.parent.mkdir()
        lib.write_bytes(b'fixture SO')
        files = {p.relative_to(root).as_posix(): live.sha(p)
                 for p in [lib, *(root / 'source-receipts' / s for s in live.VIEW_QUERY_SOURCES)]}
        manifest.update(files=files, player_view_query_sha256=live.sha(lib), player_view_query_size=lib.stat().st_size)
        return manifest

    def test_default_off_and_dependencies_before_build(self):
        self.assertFalse(live.validate_view_query_bundle(Path('/unused'), {}, False))
        self.assertTrue(live.validate_network_physics(self.options(), True, 4, 180))
        for key, value in [('player_view_query_observer', v) for v in (None, 0, 1, 'true')] + [
                ('independent_keyboard_frames', 3), ('player_lifetime_sha256', None),
                ('stock_keyboard_observer', False), ('clients', 1)]:
            with self.subTest(key=key, value=value), self.assertRaises(SystemExit):
                live.validate_network_physics(dict(self.options(), **{key: value}), True, 4, 180)
        with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused', '--snapshot', '/unused', '--out', '/unused',
                       '--player-view-query-observer'])
        command.assert_not_called()

    def test_inventory_hash_size_and_wrong_mode_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            m = self.fixture(root)
            self.assertTrue(live.validate_view_query_bundle(root, m, True))
            cases = [dict(m, player_view_query_sha256='a'*64), dict(m, player_view_query_size=True),
                     dict(m, player_view_query_size=m['player_view_query_size']+1),
                     dict(m, player_view_query_observer=False), dict(m, files=[])]
            for name in m['files']:
                files = dict(m['files']); del files[name]
                cases.append(dict(m, files=files))
            for changed in cases:
                with self.subTest(changed=changed), self.assertRaises(SystemExit):
                    live.validate_view_query_bundle(root, changed, True)
            self.assertRaises(SystemExit, live.validate_view_query_bundle, root, m, False)
            for name in m['files']:
                receipt = root / name
                saved = receipt.read_bytes(); receipt.write_bytes(b'corrupt')
                self.assertRaises(SystemExit, live.validate_view_query_bundle, root, m, True)
                receipt.write_bytes(saved)
            lib = root / 'player_view_query' / live.PLAYER_VIEW_QUERY
            lib.write_bytes(b'changed SO')
            self.assertRaises(SystemExit, live.validate_view_query_bundle, root, m, True)
            lib.unlink()
            self.assertRaises(SystemExit, live.validate_view_query_bundle, root, m, True)

    def test_disabled_allows_only_authentic_inert_source_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            name = 'source-receipts/' + live.VIEW_QUERY_SOURCES[2]
            receipt = root / name
            receipt.parent.mkdir(parents=True)
            receipt.write_bytes(b'inert source')
            manifest = {'files': {name: live.sha(receipt)}}
            self.assertFalse(live.validate_view_query_bundle(root, manifest, True))
            for spoof in ('source-receipts/../player_view_query_preload.so',
                          'source-receipts/other/player_view_query_preload.c',
                          'player_view_query/player_view_query_preload.so'):
                with self.subTest(spoof=spoof), self.assertRaises(SystemExit):
                    live.validate_view_query_bundle(root, {'files': {spoof: 'a' * 64}}, True)
            receipt.write_bytes(b'tampered')
            with self.assertRaises(SystemExit):
                live.validate_view_query_bundle(root, manifest, True)
            receipt.unlink()
            receipt.symlink_to('/etc/passwd')
            with self.assertRaises(SystemExit):
                live.validate_view_query_bundle(root, manifest, True)
            lib = root / 'player_view_query' / live.PLAYER_VIEW_QUERY
            lib.parent.mkdir()
            lib.write_bytes(b'unexpected binary')
            with self.assertRaises(SystemExit):
                live.validate_view_query_bundle(root, {'files': {}}, True)
            with self.assertRaises(SystemExit):
                live.validate_view_query_bundle(root, {'player_view_query_observer': True}, True)

    def test_source_receipts_require_every_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            for name in live.VIEW_QUERY_SOURCES:
                source = root / name
                data = source.read_bytes(); source.unlink()
                self.assertRaises(SystemExit, live.view_query_sources, root)
                source.write_bytes(data)

    def test_owned_environment_keeps_existing_preload_order(self):
        previous = {'LD_PRELOAD': '/case/lifetime.so /case/math.so', 'HOME': '/case/home'}
        result = live.view_query_environment(previous, Path('/owned'), 'a'*64, 123)
        self.assertEqual(result['LD_PRELOAD'], '/case/lifetime.so /case/math.so /owned/' + live.PLAYER_VIEW_QUERY)
        self.assertEqual(result['X4MP_VIEW_QUERY_OBSERVER'], '1')
        self.assertEqual(result['X4MP_VIEW_QUERY_PRELOAD_SHA256'], 'a'*64)
        self.assertEqual(result['X4MP_VIEW_QUERY_PRELOAD_SIZE'], '123')
        self.assertNotIn('X4MP_VIEW_QUERY_OBSERVER', previous)
        self.assertEqual(previous['LD_PRELOAD'], '/case/lifetime.so /case/math.so')

    def test_config_and_activation_are_exact_not_environment_proof(self):
        line = 'X4LAN_CONFIG.native_gate_linux_view_query_observer = true\n'
        live.validate_view_query_config(line, True)
        live.validate_view_query_config('', False)
        for config, enabled in [('', True), (line, False), (line+line, True),
                                (line.replace('true', '1'), True)]:
            self.assertRaises(SystemExit, live.validate_view_query_config, config, enabled)
        active = 'X4 VIEW QUERY PRELOAD ACTIVE version=1'
        self.assertTrue(live.validate_view_query_receipts([active]))
        for receipts in ([], [active, active], [active+' trailing'],
                         ['X4 VIEW QUERY PRELOAD REFUSED'], [active, 'X4 VIEW QUERY PRELOAD REFUSED']):
            self.assertFalse(live.validate_view_query_receipts(receipts))

    def test_mocked_bundle_forwards_option_freezes_library_and_reserves_config(self):
        # Root owns the overlay/C implementation. Simulate its newly agreed flag/config
        # while exercising the real bundle composition, inventory and manifest writer.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'save.gz'; snapshot.write_bytes(snapshot_bytes())
            original_copy = live.shutil.copy2
            sources = [ROOT / name for name in live.VIEW_QUERY_SOURCES]
            for enabled in (False, True):
                output = root / str(enabled)
                commands = []
                def copy(source, destination, *args, **kwargs):
                    source = Path(source)
                    if source.name in (live.PLAYER_LIFETIME, live.PLAYER_VIEW_QUERY):
                        Path(destination).write_bytes(b'fixture preload'); return destination
                    if source in sources and not source.is_file():
                        Path(destination).write_bytes(('fixture ' + source.name).encode()); return destination
                    return original_copy(source, destination, *args, **kwargs)
                def command(arguments, **kwargs):
                    commands.append(arguments)
                    if 'build_native_gate_linux.py' in str(arguments[1]):
                        (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(arguments[1]):
                        forwarded = '--player-view-query-observer' in arguments
                        ordinary = [a for a in arguments if a != '--player-view-query-observer']
                        with mock.patch.object(sys, 'argv', ordinary[1:]): overlay.main()
                        if forwarded:
                            # Locate the output requested by the real runner.
                            config = Path(arguments[arguments.index('--out')+1]) / 'ui/config.lua.append'
                            config.write_text(config.read_text() + '\nX4LAN_CONFIG.native_gate_linux_view_query_observer = true\n')
                            manifest_path = config.parents[1] / 'x4lane-manifest.json'
                            hashes = json.loads(manifest_path.read_text())
                            hashes['ui/config.lua.append'] = live.sha(config)
                            manifest_path.write_text(json.dumps(hashes))
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '', returncode=0)
                flags = ['--player-view-query-observer'] if enabled else []
                with mock.patch.object(live.subprocess, 'run', side_effect=command), mock.patch.object(live.shutil, 'copy2', side_effect=copy), mock.patch.object(live, 'view_query_sources', return_value=sources), contextlib.redirect_stdout(io.StringIO()):
                    live.main(['bundle', '--zig', '/unused', '--snapshot', str(snapshot), '--out', str(output),
                               '--stock-keyboard-observer', '--serial-pathing', '--player-factory-probe',
                               '--stock-keyboard-replay-probe', '--player-lifetime-preload',
                               '--player-factory-retain-until', '60', '--independent-keyboard-frames', '4', *flags])
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertIs(manifest['player_view_query_observer'], enabled)
                self.assertEqual(live.validate_view_query_bundle(output, manifest, True), enabled)
                if not enabled:
                    # Exercise the run preflight up to the next bundle validator;
                    # no subprocess or native game may be started by this test.
                    user = mock.Mock(pw_dir=str(root), pw_uid=live.os.geteuid())
                    with mock.patch('pwd.getpwnam', return_value=user), \
                            mock.patch.object(live.subprocess, 'Popen') as launch, \
                            mock.patch.object(live.input_observation_bundle, 'validate_bundle',
                                              side_effect=RuntimeError('next validator reached')) as next_validator, \
                            self.assertRaisesRegex(RuntimeError, 'next validator reached'):
                        live.main(['run', '--root', str(output), '--user', 'fixture',
                                   '--controller-physics-discard', '--controller-physics-frames', '4'])
                    next_validator.assert_called_once()
                    launch.assert_not_called()
                overlay_command = next(c for c in commands if 'build_native_gate_overlay.py' in str(c[1]))
                self.assertEqual('--player-view-query-observer' in overlay_command, enabled)
                self.assertEqual(any('build_player_view_query_preload.py' in str(c[1]) for c in commands), enabled)
                live.validate_view_query_config((output / 'extension/x4_lan_lab/ui/config.lua').read_text(), enabled)

    def evidence(self, data='', ui=0, probe=0, overflow=0, slot=1, frames=4):
        rows = len(data.split(';')) if data else 0
        return [f'[X4LAN] NATIVE_GATE LINUX_VIEW_QUERY_ARMED tick=7 slot={slot}\n',
                f'[X4LAN] NATIVE_GATE LINUX_VIEW_QUERY_RESULT tick={4+frames} slot={slot} '
                f'v=1 finished=1 ui_calls={ui} worker_calls=0 other_calls=0 probe_calls={probe} '
                f'overflow={overflow} rows={rows} data={data}\n']

    def test_empty_observation_is_valid_but_never_readiness(self):
        result = live.parse_view_query_evidence(self.evidence(ui=3, probe=3), 1, 4)
        self.assertTrue(result['pass'])
        self.assertEqual(result['rows'], [])
        self.assertTrue(result['observation_complete'])
        self.assertFalse(result['readiness_proven'])
        self.assertEqual(result['row_scope'], 'outside_marked_frame')
        # Counts without corresponding rows remain visible as a diagnostic gap.
        result = live.parse_view_query_evidence(self.evidence(ui=3), 1, 4)
        self.assertTrue(result['pass'])
        self.assertEqual(result['unrepresented_ui_calls'], 3)
        self.assertFalse(result['observation_complete'])

    def test_all_rows_retained_including_unknown_outside_image(self):
        data = 'a44e47/42/7/8/2/1/0;7f1234567890/0/7/7/1/0/0'
        result = live.parse_view_query_evidence(self.evidence(data, ui=4, probe=1), 1, 4)
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual(len(result['rows']), 2)
        self.assertEqual(result['rows'][1]['caller'], '7f1234567890')
        self.assertEqual(result['rows'][1]['in_image'], 0)
        self.assertTrue(result['observation_complete'])
        self.assertFalse(result['readiness_proven'])

    def test_missing_duplicate_misordered_wrong_slot_and_truncated_markers(self):
        lines = self.evidence()
        cases = [[], lines[:1], lines[1:], lines+lines[:1], list(reversed(lines)),
                 [lines[0], lines[1].replace('slot=1', 'slot=2')],
                 [lines[0].replace('tick=7', 'tick=6'), lines[1]],
                 [lines[0], lines[1].replace('tick=8', 'tick=9')],
                 [lines[0], lines[1].replace('finished=1', 'finished=0')],
                 [lines[0], lines[1].replace('v=1', 'v=2')],
                 [lines[0], lines[1].rstrip('\n')],
                 [lines[0], lines[1]+'LINUX_VIEW_QUERY partial\n']]
        for case in cases:
            with self.subTest(case=case):
                self.assertFalse(live.parse_view_query_evidence(case, 1, 4)['pass'])

    def test_bad_row_range_count_identity_order_and_probe_refuse(self):
        good = 'a44e47/42/7/8/2/1/0'
        bad = ['a44e47/42/6/8/2/1/0', 'a44e47/42/7/9/2/1/0',
               'a44e47/42/8/7/2/1/0', 'a44e47/42/7/8/0/1/0',
               'a44e47/42/7/8/2/0/0', 'a44e47/42/7/8/2/1/1',
               '0/42/7/8/2/0/0', 'a44e47/18446744073709551616/7/8/2/1/0',
               good+';'+good, 'a44e47/42/8/8/1/1/0;7f1234567890/42/7/8/1/0/0']
        for data in bad:
            with self.subTest(data=data):
                self.assertFalse(live.parse_view_query_evidence(self.evidence(data, ui=10), 1, 4)['pass'])
        for lines in (self.evidence(good, ui=1), self.evidence(ui=0, probe=1),
                      self.evidence(overflow=1, ui=1),
                      self.evidence(ui=18446744073709551616),
                      [self.evidence()[0], self.evidence()[1].replace('rows=0', 'rows=1')]):
            self.assertFalse(live.parse_view_query_evidence(lines, 1, 4)['pass'])

    def test_overflow_explicitly_incomplete_all_64_rows_preserved(self):
        data = ';'.join(f'{0x700000+i:x}/0/7/8/1/1/0' for i in range(64))
        result = live.parse_view_query_evidence(self.evidence(data, ui=66, overflow=2), 1, 4)
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual(len(result['rows']), 64)
        self.assertEqual(result['overflow'], 2)
        self.assertFalse(result['observation_complete'])
        self.assertFalse(result['readiness_proven'])
        self.assertTrue(any('omitted' in line for line in result['limitations']))
        self.assertFalse(live.parse_view_query_evidence(self.evidence(data+';710000/0/7/8/1/1/0', ui=65), 1, 4)['pass'])

    def test_collection_preserves_receipts_hash_and_rejects_missing_or_oversized(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine.log'
            self.assertFalse(live.collect_view_query_evidence(engine, 1, 4)['pass'])
            engine.write_text('unrelated engine row\n'+''.join(self.evidence()))
            result = live.collect_view_query_evidence(engine, 1, 4)
            self.assertTrue(result['pass'])
            self.assertEqual(result['raw_markers'], self.evidence())
            self.assertEqual(result['engine_sha256'], live.sha(engine))
            for text in (''.join(self.evidence())+'LINUX_VIEW_QUERY extra\n',
                         'LINUX_VIEW_QUERY '+('x'*16640)+'\n',
                         ''.join(self.evidence()).rstrip('\n')):
                engine.write_text(text)
                self.assertFalse(live.collect_view_query_evidence(engine, 1, 4)['pass'])

    def test_builder_uses_dynamic_registry_link_and_private_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'private/lib.so'
            with mock.patch.object(builder, 'OUTPUT', output), mock.patch.object(sys, 'argv', ['build', '--zig', '/fixture/zig']), mock.patch.object(builder.subprocess, 'run') as run, contextlib.redirect_stdout(io.StringIO()):
                builder.main()
            argv = run.call_args.args[0]
            self.assertEqual(argv[:4], ['/fixture/zig', 'cc', '-target', 'x86_64-linux-gnu.2.17'])
            self.assertIn('-ldl', argv)
            self.assertNotIn('-lgcc_s', argv)
            self.assertEqual(argv[argv.index('-o')+1], str(output))
            self.assertEqual(builder.OUTPUT.name, live.PLAYER_VIEW_QUERY)


if __name__ == '__main__':
    unittest.main()
