"""Additive provenance checks; no native engine or activation changes."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_view_performance_bundle as performance
from test_linux_view_component_policy_bundle import fixture


class PerformanceBundleTests(unittest.TestCase):
    def test_old_versions_delegate_without_rewriting_acceptance(self):
        for version in (None, 12, 13, 14, 15, 16):
            manifest = {'view_query_source_version': version, 'player_view_wire': version == 16}
            with self.subTest(version=version), patch.object(
                    performance.previous, 'validate_bundle', return_value='old') as old:
                self.assertEqual(performance.validate_bundle('/unused', manifest, True), 'old')
                self.assertIs(old.call_args.args[1], manifest)
                self.assertIs(old.call_args.args[2], True)

    def test_new_files_and_copied_helper_are_hash_bound(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(
                performance.previous, 'validate_bundle', return_value='wire-policy') as old:
            root = Path(directory)
            manifest = fixture(root, performance, 17)
            self.assertEqual(performance.validate_bundle(root, manifest, True), 'wire-policy')
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 16)
            self.assertEqual(manifest['view_query_source_version'], 17)
            for name in performance.EXTRA_SOURCES:
                path = root / 'source-receipts' / name
                original = path.read_bytes()
                for mutation in ('changed', 'missing'):
                    with self.subTest(source=name, mutation=mutation):
                        if mutation == 'changed':
                            path.write_bytes(original + b'tamper')
                        else:
                            path.unlink()
                        old.reset_mock()
                        with self.assertRaises(ValueError):
                            performance.validate_bundle(root, manifest, True)
                        old.assert_not_called()
                        path.write_bytes(original)
            helper = root / 'linux_view_performance_bundle.py'
            helper.write_bytes(helper.read_bytes() + b'tamper')
            with self.assertRaises(ValueError):
                performance.validate_bundle(root, manifest, True)

    def test_real_source_paths_and_complete_v16_policy_delegation(self):
        import linux_view_host_capture_bundle as host
        from test_linux_player_view_wire_bundle import ViewWireBundleTests
        repo = Path(__file__).resolve().parents[1]
        self.assertEqual(len(performance.source_paths(repo)), len(performance.SOURCES))
        for active in (False, True):
            with self.subTest(active=active), tempfile.TemporaryDirectory() as directory, patch.object(
                    host, 'validate_bundle', return_value='historical-policy'):
                root = Path(directory)
                manifest = fixture(root, performance, 17)
                if active:
                    manifest.update(ViewWireBundleTests().options(),
                                    player_view_wire_evidence_version=1,
                                    player_selection_parity_evidence_version=1)
                self.assertEqual(performance.validate_bundle(root, manifest, True), 'historical-policy')
                if active:
                    # v17 may not bypass the unchanged v16 wire admission checks.
                    with self.assertRaises(ValueError):
                        performance.validate_bundle(root, dict(manifest, player_cursor_active_flight=False), True)
                    with self.assertRaises(ValueError):
                        performance.validate_bundle(root, manifest, False)
