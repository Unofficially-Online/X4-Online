"""The v14 source closure preserves historical v12/v13 bundle validation."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_view_component_policy_bundle as component
import linux_selection_parity_bundle as parity
import linux_view_projection_scope_bundle as projection
import linux_view_host_capture_bundle as host


def fixture(root, module, version):
    inventory = {}
    for name in module.SOURCES:
        path = root / 'source-receipts' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(name.encode())
        inventory['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in module.HELPERS:
        path = root / name
        path.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
        inventory[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return {'view_query_source_version': version, 'files': inventory}


class ComponentClosureTest(unittest.TestCase):
    def test_historical_versions_delegate_without_new_sources(self):
        with patch.object(host, 'validate_bundle', return_value='legacy'):
            for version, module in ((12, projection), (13, parity)):
                with self.subTest(version=version), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    manifest = fixture(root, module, version)
                    if version == 12:
                        manifest.update(player_ui_projection_fov_scope=True, view_render_capture_shadow=True,
                                        player_ui_camera_refill=True, player_ui_session_frames=180,
                                        player_startup_phases='v1-t40')
                    else:
                        manifest.update(player_selection_parity=True, player_selection_parity_evidence_version=1,
                                        view_host_capture_shadow=True, player_startup_phases='v1-t40',
                                        player_ui_session_frames=180, independent_keyboard_frames=180,
                                        player_cursor_active_flight=True, player_view_query_observer=True,
                                        stock_view_fov_observer=True)
                    self.assertFalse((root / 'source-receipts/src/native/engine/linux/player_stock_view_component_policy.h').exists())
                    self.assertEqual(component.validate_bundle(root, manifest, True), 'legacy')

    def test_v14_chain_and_tampering(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(host, 'validate_bundle', return_value='legacy') as old:
            root = Path(directory)
            manifest = fixture(root, component, 14)
            manifest.update(player_selection_parity=True, player_selection_parity_evidence_version=1,
                            view_host_capture_shadow=True, player_startup_phases='v1-t40',
                            player_ui_session_frames=180, independent_keyboard_frames=180,
                            player_cursor_active_flight=True, player_view_query_observer=True,
                            stock_view_fov_observer=True)
            self.assertEqual(component.validate_bundle(root, manifest, True), 'legacy')
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 11)
            for name in ('source-receipts/src/native/engine/linux/player_stock_view_component_policy.h',
                         'linux_view_component_policy_bundle.py'):
                path = root / name
                original = path.read_bytes()
                path.write_bytes(original + b'tamper')
                with self.subTest(name=name), self.assertRaises(ValueError):
                    component.validate_bundle(root, manifest, True)
                path.write_bytes(original)
            (root / 'source-receipts/src/native/engine/linux/gate_player_stock_view_component_policy.h').unlink()
            with self.assertRaises(ValueError):
                component.validate_bundle(root, manifest, True)
