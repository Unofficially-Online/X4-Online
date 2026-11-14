"""Combined observation closure and runner contracts; no native process."""
import ast
import hashlib
import json
import shutil
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_gate_live as live
import linux_input_observation_bundle as combined
import linux_semaphore_observer_bundle as latest
import linux_player_space_bundle as space
import linux_cursor_input_trace as mouse
import linux_view_precompare_bundle as pre
import linux_view_ancestry_bundle as ancestry
import linux_ui_camera_refill_bundle as camera
import linux_stock_view_fov_observer as fov
import linux_player_cursor_bundle as cursor
import linux_view_component_trace_bundle as component


class InputObservationTests(unittest.TestCase):
    def test_frozen_chain_two_through_eight_and_missing_receipts(self):
        groups = {2: ancestry.EXTRA_SOURCES, 3: camera.SOURCES, 4: fov.SOURCES,
                  5: cursor.SOURCES, 6: component.SOURCES, 7: space.SOURCES, 8: combined.SOURCES}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            files = {}
            for version, sources in groups.items():
                for name in sources:
                    path = root / 'source-receipts' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(name.encode())
                    files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
                if version >= 6:
                    helpers = component.HELPERS if version == 6 else space.HELPERS if version == 7 else combined.HELPERS
                    for name in helpers:
                        target = root / name
                        target.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
                        files[name] = hashlib.sha256(target.read_bytes()).hexdigest()
                manifest = {'view_query_source_version': version, 'files': dict(files),
                            'view_ancestry_observer': False, 'view_component_trace': False}
                self.assertFalse(combined.validate_bundle(root, manifest, True), version)
                self.assertFalse(latest.validate_bundle(root, manifest, True), version)
            manifest.update(player_view_query_observer=True, view_ancestry_observer=True,
                            view_component_trace=True, view_component_trace_evidence_version=1,
                            view_precompare_trace=True, view_precompare_trace_evidence_version=1,
                            independent_keyboard_frames=180)
            self.assertTrue(combined.validate_bundle(root, manifest, True))
            self.assertTrue(latest.validate_bundle(root, manifest, True))
            for name in combined.SOURCES:
                broken = dict(manifest, files=dict(manifest['files']))
                broken['files'].pop('source-receipts/' + name)
                with self.subTest(source=name), self.assertRaises(ValueError):
                    combined.validate_bundle(root, broken, True)
            for name in combined.HELPERS:
                broken = dict(manifest, files=dict(manifest['files']))
                broken['files'].pop(name)
                with self.subTest(helper=name), self.assertRaises(ValueError):
                    combined.validate_bundle(root, broken, True)
            (root / combined.HELPERS[0]).write_text('changed')
            with self.assertRaises(ValueError):
                combined.validate_bundle(root, manifest, True)

    def test_environment_default_off_and_dependencies(self):
        inherited = {mouse.ENVIRONMENT: '1', pre.ENVIRONMENT: '1', 'KEEP': 'yes'}
        clean = pre.environment(mouse.environment(inherited, False), False)
        self.assertEqual(clean, {'KEEP': 'yes'})
        active = pre.environment(mouse.environment(inherited, True), True)
        self.assertEqual(active, inherited)
        for args in ((False, True, True), (True, False, True), (True, True, False)):
            with self.assertRaises(ValueError):
                pre.enabled({'view_precompare_trace': True}, *args)
        with self.assertRaises(ValueError):
            mouse.enabled({'cursor_input_trace': True})

    def test_runner_precompare_end_uses_session_budget(self):
        tree = ast.parse(Path(live.__file__).read_text())
        run = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'run')
        calls = [n for n in ast.walk(run) if isinstance(n, ast.Call) and
                 isinstance(n.func, ast.Attribute) and isinstance(n.func.value, ast.Name) and
                 n.func.value.id == 'precompare_trace' and n.func.attr == 'collect']
        self.assertEqual(len(calls), 1)
        self.assertEqual(ast.unparse(calls[0].args[-2]), 'end_tick')
        assigned = [n for n in ast.walk(run) if isinstance(n, ast.Assign) and
                    any(isinstance(t, ast.Name) and t.id == 'end_tick' for t in n.targets)]
        self.assertEqual(len(assigned), 1)
        self.assertEqual(ast.unparse(assigned[0].value), "session['recurring_end'] if session else 0")

    def test_real_mocked_bundle_opt_in_precompare(self):
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main
        seen = []
        def build(arguments):
            active = '--player-ui-seat-correction' in arguments
            if active:
                arguments = arguments + ['--view-ancestry-observer', '--view-component-trace',
                                         '--view-precompare-trace']
            if active:
                isolated = []
                skip = False
                for token in arguments:
                    if skip:
                        skip = False
                        continue
                    if token in ('--independent-keyboard-frames', '--player-ui-overlay-callbacks', '--ticks'):
                        skip = True
                        continue
                    if token == '--player-ui-overlay-probe':
                        continue
                    isolated.append(token)
                out_index = isolated.index('--out') + 1
                isolated += ['--player-ui-session-frames', '180', '--ticks', '220',
                             '--player-startup-phases', 'v1-t40', '--player-cursor-active-flight',
                             '--cursor-input-trace']
                original_main(isolated)
                mouse_root = Path(isolated[out_index])
                mouse_manifest = json.loads((mouse_root / 'manifest.json').read_text())
                self.assertTrue(mouse_manifest['cursor_input_trace'])
                self.assertTrue(mouse_manifest['view_precompare_trace'])
                self.assertEqual(mouse_manifest['cursor_input_trace_evidence_version'], 1)
                self.assertEqual(mouse_manifest['view_query_source_version'], latest.SOURCE_VERSION)
                self.assertTrue(latest.validate_bundle(mouse_root, mouse_manifest, True))
                shutil.rmtree(mouse_root)
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['view_precompare_trace'], active)
            self.assertEqual(manifest['view_precompare_trace_evidence_version'], 1 if active else None)
            self.assertEqual(manifest['view_query_source_version'], latest.SOURCE_VERSION)
            self.assertEqual(latest.validate_bundle(output, manifest, True), active)
            for name in latest.SOURCES:
                receipt = 'source-receipts/' + name
                self.assertEqual(manifest['files'][receipt], live.sha(output / receipt))
            for name in latest.HELPERS:
                self.assertEqual(manifest['files'][name], live.sha(output / name))
            seen.append(active)
            return result
        with patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()
        self.assertEqual(set(seen), {False, True})


if __name__ == '__main__':
    unittest.main()
