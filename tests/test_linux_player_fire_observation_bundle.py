"""Source version 9 and runner contracts, without native processes."""
import ast
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_gate_live as live
import linux_input_observation_bundle as previous
import linux_view_ancestry_bundle as ancestry
import linux_ui_camera_refill_bundle as camera
import linux_stock_view_fov_observer as fov
import linux_player_cursor_bundle as cursor
import linux_view_component_trace_bundle as component
import linux_player_fire_observation as observation
import linux_player_fire_observation_bundle as bundle


class FireBundleTests(unittest.TestCase):
    def freeze(self, root):
        inventory = {}
        # Earlier source generations are frozen separately by the runner too.
        sources = tuple(dict.fromkeys(ancestry.EXTRA_SOURCES + camera.SOURCES +
            fov.SOURCES + cursor.SOURCES + component.SOURCES + bundle.SOURCES))
        for name in sources:
            target = root / 'source-receipts' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(name.encode())
            inventory['source-receipts/' + name] = live.sha(target)
        helpers = tuple(dict.fromkeys(camera.HELPERS + component.HELPERS + bundle.HELPERS +
            ('linux_view_ancestry_bundle.py', 'linux_view_ancestry_evidence.py',
             'linux_stock_view_fov_observer.py', 'linux_player_cursor_bundle.py')))
        for name in helpers:
            target = root / name
            target.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
            inventory[name] = live.sha(target)
        return dict(view_query_source_version=9, files=inventory,
                    player_fire_observation=True, player_fire_observation_evidence_version=2,
                    player_startup_phases='v1-t40', player_ui_session_frames=180,
                    independent_keyboard_frames=180)

    def test_inventory_and_runtime_tamper(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            manifest = self.freeze(root)
            self.assertFalse(bundle.validate_bundle(root, manifest, True))
            for name in bundle.SOURCES:
                with self.subTest(source=name):
                    target = root / 'source-receipts' / name
                    original = target.read_bytes()
                    target.write_bytes(original + b'changed')
                    with self.assertRaises(ValueError):
                        bundle.validate_bundle(root, manifest, True)
                    target.write_bytes(original)
            for name in bundle.HELPERS:
                with self.subTest(helper=name):
                    target = root / name
                    original = target.read_bytes()
                    target.write_bytes(original + b'changed')
                    # A runtime-only hash update cannot bypass its source receipt.
                    modified = dict(manifest, files=dict(manifest['files'], **{name: live.sha(target)}))
                    with self.assertRaises(ValueError):
                        bundle.validate_bundle(root, modified, True)
                    target.write_bytes(original)
            for change in ({'view_query_source_version': 8},
                           {'player_fire_observation_evidence_version': 1},
                           {'player_fire_observation_evidence_version': True},
                           {'independent_keyboard_frames': 179},
                           {'player_ui_session_frames': 901},
                           {'player_startup_phases': None}):
                with self.subTest(change=change), self.assertRaises(ValueError):
                    bundle.validate_bundle(root, dict(manifest, **change), True)
            with self.assertRaises(ValueError):
                bundle.validate_bundle(root, manifest, False)

    def test_old_generations_delegate_without_changing_manifest(self):
        for version in range(2, 9):
            manifest = {'view_query_source_version': version}
            with self.subTest(version=version), patch.object(previous, 'validate_bundle', return_value='legacy') as delegate:
                self.assertEqual(bundle.validate_bundle(Path('/unused'), manifest, False), 'legacy')
                delegate.assert_called_once_with(Path('/unused'), manifest, False)
                self.assertEqual(manifest, {'view_query_source_version': version})
        # Existing full generation 2..8 inventory tests still exercise real validators.

    def test_collector_preserves_failed_raw_and_runs_before_compaction(self):
        tree = ast.parse(Path(live.__file__).read_text())
        run = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'run')
        calls = {ast.unparse(node.func): node.lineno for node in ast.walk(run) if isinstance(node, ast.Call)}
        self.assertLess(calls['collect_network_client'], calls['collect_fire_observations'])
        self.assertLess(calls['collect_fire_observations'], calls['compact_validated_result'])
        result = {'pass': True, 'physics_discard': {'pass': True, 'clients': {'1': {'frames': [40]}}}}
        proof = {'pass': False, 'raw_receipts': ['original raw receipt\n']}
        with patch.object(observation, 'collect', return_value=proof) as collect:
            live.collect_fire_observations(result, [1], [(None, Path('/private'))], Path('/run'), True, {})
        self.assertEqual(result['player_fire_observation_evidence']['1'], proof)
        self.assertFalse(result['pass'])
        self.assertFalse(result['physics_discard']['pass'])
        self.assertEqual(collect.call_args.args[1], Path('/private/controller-lifecycle.log'))
        self.assertEqual(collect.call_args.args[3], {'frames': [40]})

    def test_runner_environment_and_disabled_receipt_refusal(self):
        tree = ast.parse(Path(live.__file__).read_text())
        run = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'run')
        calls = [node for node in ast.walk(run) if isinstance(node, ast.Call) and
                 ast.unparse(node.func) == 'fire_observation.environment']
        self.assertEqual(len(calls), 1)
        self.assertEqual([ast.unparse(argument) for argument in calls[0].args],
                         ['env', 'fire_observation_enabled'])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'game1-stderr.log').write_text('X4LAN FIRE_DIAG unexpected retained row\n')
            result = {'pass': True, 'physics_discard': {'pass': True}}
            live.collect_fire_observations(result, [1], [(None, root)], root, False, {})
            proof = result['player_fire_observation_evidence']['1']
            self.assertFalse(proof['pass'])
            self.assertEqual(proof['raw_receipts'], ['X4LAN FIRE_DIAG unexpected retained row\n'])
            self.assertFalse(result['physics_discard']['pass'])

    def test_mocked_bundle_default_and_enabled(self):
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main
        seen = []

        def build(arguments):
            active = '--player-ui-seat-correction' in arguments
            if active:
                session_arguments = []
                skip = False
                for token in arguments:
                    if skip:
                        skip = False
                        continue
                    if token in ('--independent-keyboard-frames', '--player-ui-overlay-callbacks', '--ticks'):
                        skip = True
                    elif token != '--player-ui-overlay-probe':
                        session_arguments.append(token)
                arguments = session_arguments + ['--player-ui-session-frames', '180', '--ticks', '220',
                                                 '--player-startup-phases', 'v1-t40',
                                                 '--player-fire-observation']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['view_query_source_version'], live.semaphore_bundle.SOURCE_VERSION)
            self.assertEqual(manifest['player_fire_observation'], active)
            self.assertEqual(manifest['player_fire_observation_evidence_version'], 2 if active else None)
            self.assertEqual(live.semaphore_bundle.validate_bundle(output, manifest, True), False)
            for name in bundle.SOURCES:
                self.assertEqual(manifest['files']['source-receipts/' + name], live.sha(output / 'source-receipts' / name))
            for name in bundle.HELPERS:
                self.assertEqual(manifest['files'][name], live.sha(output / name))
            seen.append(active)
            return result

        with patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()
        self.assertEqual(set(seen), {False, True})


if __name__ == '__main__':
    unittest.main()
