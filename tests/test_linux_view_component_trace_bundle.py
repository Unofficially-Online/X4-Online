"""Versioned closure preserves old inventories and freezes every new input."""
import linux_semaphore_observer_bundle as latest_bundle
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_view_ancestry_bundle as ancestry
import linux_ui_camera_refill_bundle as camera
import linux_stock_view_fov_observer as fov
import linux_player_cursor_bundle as cursor
import linux_view_component_trace_bundle as trace
import linux_input_observation_bundle as observation


class ComponentTraceBundleTests(unittest.TestCase):
    def test_real_mocked_bundle_freezes_opt_in_and_sources(self):
        import linux_gate_live as live
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main

        def build(arguments):
            active = '--player-ui-seat-correction' in arguments
            arguments = arguments + (['--view-ancestry-observer', '--view-component-trace'] if active else [])
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['view_component_trace'], active)
            self.assertEqual(manifest['view_component_trace_evidence_version'], 1 if active else None)
            self.assertEqual(manifest['view_query_source_version'], latest_bundle.SOURCE_VERSION)
            self.assertEqual(latest_bundle.validate_bundle(output, manifest, True), active)
            for source in trace.SOURCES:
                receipt = 'source-receipts/' + source
                self.assertEqual(manifest['files'][receipt], live.sha(output / receipt))
            return result

        with patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()

    def test_old_versions_and_v6_closure(self):
        groups = {2: ancestry.EXTRA_SOURCES, 3: camera.SOURCES, 4: fov.SOURCES,
                  5: cursor.SOURCES, 6: trace.SOURCES}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {}
            for version in range(2, 7):
                for name in groups[version]:
                    path = root / 'source-receipts' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(name)
                    files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
                if version == 6:
                    for name in trace.HELPERS:
                        source = root / 'source-receipts/tools/dev' / name
                        target = root / name
                        target.write_bytes(source.read_bytes())
                        files[name] = hashlib.sha256(target.read_bytes()).hexdigest()
                manifest = {'view_query_source_version': version, 'view_ancestry_observer': False,
                            'view_component_trace': False, 'files': dict(files)}
                self.assertFalse(trace.validate_bundle(root, manifest, True))
                if version < 6:
                    with self.assertRaises(ValueError):
                        trace.validate_bundle(root, dict(manifest, view_component_trace=True), True)
            manifest.update(view_ancestry_observer=True, view_component_trace=True,
                            view_component_trace_evidence_version=1, player_view_query_observer=True,
                            independent_keyboard_frames=180)
            self.assertTrue(trace.validate_bundle(root, manifest, True))
            for name in trace.SOURCES:
                broken = dict(manifest, files=dict(manifest['files']))
                broken['files'].pop('source-receipts/' + name)
                with self.subTest(source=name), self.assertRaises(ValueError):
                    trace.validate_bundle(root, broken, True)
            with self.assertRaises(ValueError):
                trace.validate_bundle(root, dict(manifest, view_component_trace_evidence_version=True), True)
            for name in ('gate_player_space_native.h', 'player_stock_space.h', 'player_space_context_vector.h'):
                self.assertIn('src/native/engine/linux/' + name, trace.SOURCES)
            (root / trace.HELPERS[0]).write_text('tampered helper')
            with self.assertRaises(ValueError):
                trace.validate_bundle(root, manifest, True)

    def test_camera_three_delegates_to_exact_ancestry_two(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {}
            for name in camera.SOURCES:
                path = root / 'source-receipts' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
                files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {'view_query_source_version': 3, 'files': files}
            with patch.object(camera.ancestry, 'SOURCE_VERSION', 99), \
                 patch.object(camera.ancestry, 'validate_bundle', return_value=False) as call:
                self.assertFalse(camera.validate_bundle(root, manifest, True))
                self.assertEqual(call.call_args.args[1]['view_query_source_version'], 2)

    def test_environment_does_not_inherit(self):
        env = {'X4_DEV_VIEW_COMPONENT_TRACE': '1', 'OTHER': 'ok'}
        self.assertEqual(trace.environment(env, False), {'OTHER': 'ok'})
        self.assertEqual(trace.environment(env, True), env)
        with self.assertRaises(ValueError):
            trace.enabled({'view_component_trace': True}, False)
