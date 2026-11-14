"""Default-off camera mode and v2/v3 frozen-source dispatch; no native launch."""
import linux_semaphore_observer_bundle as latest_bundle
import copy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

import linux_gate_live as live
import linux_ui_camera_refill_bundle as bundle
import linux_stock_view_fov_observer as stock_view
import linux_player_cursor_bundle as cursor
import linux_input_observation_bundle as trace_bundle
import linux_view_ancestry_bundle as ancestry
from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests

ROOT = Path(__file__).resolve().parents[1]


class CameraRefillBundleTests(unittest.TestCase):
    def options(self):
        return dict(RenderLeaseAdmissionTests().options(), player_ui_camera_refill=True)

    def test_explicit_full_chain_exact_config_and_guest_only_environment(self):
        self.assertFalse(bundle.validate_options({}))
        self.assertTrue(bundle.validate_options(self.options()))
        for value in (1, None, 'true'):
            with self.assertRaises(ValueError):
                bundle.validate_options(dict(self.options(), player_ui_camera_refill=value))
        for key in ('player_ui_render_lease', 'player_ui_seat_correction',
                    'player_ui_live_pose_observer', 'player_ui_seat_query_probe',
                    'player_view_query_observer', 'player_ui_overlay_probe'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                bundle.validate_options(dict(self.options(), **{key: False}))
        env = {'X4_DEV_UI_CAMERA_REFILL': 'unowned', 'OTHER': 'keep'}
        self.assertEqual(bundle.environment(env, True, 1), {'OTHER': 'keep'})
        self.assertEqual(bundle.environment(env, False, 2), {'OTHER': 'keep'})
        self.assertEqual(bundle.environment(env, True, 2), {'OTHER': 'keep', 'X4_DEV_UI_CAMERA_REFILL': '1'})
        config = 'X4LAN_CONFIG.native_gate_linux_ui_camera_refill = true\n'
        bundle.validate_config(config, True)
        bundle.validate_config('', False)
        for text, enabled in (('', True), (config, False), (config + config, True),
                              (config.replace('true', '1'), True)):
            with self.assertRaises(ValueError):
                bundle.validate_config(text, enabled)

    def test_v2_is_preserved_v3_requires_new_headers_even_when_disabled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {}
            for name in ancestry.EXTRA_SOURCES:
                path = root / 'source-receipts' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
                files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            legacy = {'view_query_source_version': 2, 'view_ancestry_observer': False, 'files': files}
            self.assertFalse(bundle.validate_bundle(root, legacy, True))
            current = dict(legacy, view_query_source_version=3)
            with self.assertRaises(ValueError):
                bundle.validate_bundle(root, current, True)
            for name in bundle.SOURCES:
                path = root / 'source-receipts' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
                files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertFalse(bundle.validate_bundle(root, current, True))
            for name in (source for source in bundle.SOURCES if source.endswith('.h')):
                broken = copy.deepcopy(current)
                broken['files'].pop('source-receipts/' + name)
                with self.subTest(name=name), self.assertRaises(ValueError):
                    bundle.validate_bundle(root, broken, True)
            unsupported = dict(current, view_query_source_version=True)
            with self.assertRaises(ValueError):
                bundle.validate_bundle(root, unsupported, True)

    def test_real_mocked_bundle_forwards_freezes_and_preserves_default(self):
        original_main = live.main

        def build(arguments):
            enabled = '--player-ui-seat-correction' in arguments
            if enabled:
                arguments = arguments + ['--player-ui-camera-refill']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertIs(manifest['player_ui_camera_refill'], enabled)
            self.assertEqual(manifest['view_query_source_version'], latest_bundle.SOURCE_VERSION)
            config = (output / 'extension/x4_lan_lab/ui/config.lua').read_text()
            bundle.validate_config(config, enabled)
            self.assertFalse(latest_bundle.validate_bundle(output, manifest, True))
            for source in bundle.SOURCES:
                receipt = 'source-receipts/' + source
                self.assertEqual(manifest['files'][receipt], live.sha(output / receipt))
            for helper in bundle.HELPERS:
                self.assertEqual(manifest['files'][helper], live.sha(output / helper))
            if enabled:
                invalid = dict(manifest, player_ui_camera_refill_evidence_version=True)
                with self.assertRaises(ValueError):
                    latest_bundle.validate_bundle(output, invalid, True)
            return result

        with mock.patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()
