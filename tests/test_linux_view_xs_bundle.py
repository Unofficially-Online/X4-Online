"""XS inventory remains additive to the frozen component closure."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_view_xs_bundle as xs
from test_linux_view_component_policy_bundle import fixture


class XsBundleTests(unittest.TestCase):
    def test_default_environment_removes_ambient_activation(self):
        import linux_stock_view_fov_observer as observer
        self.assertNotIn('X4_DEV_VIEW_XS', observer.environment({'X4_DEV_VIEW_XS': '1'}, False))

    def test_legacy_and_new_closure(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(xs.previous, 'validate_bundle', return_value='legacy') as old:
            root = Path(directory)
            old_manifest = {'view_query_source_version': 14}
            self.assertEqual(xs.validate_bundle(root, old_manifest, True), 'legacy')
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 14)
            manifest = fixture(root, xs, 15)
            self.assertEqual(xs.validate_bundle(root, manifest, True), 'legacy')
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 14)
            for name in ('source-receipts/src/native/engine/linux/player_view_xs_preload_impl.h',
                         'linux_view_xs_bundle.py'):
                path = root / name
                original = path.read_bytes()
                path.write_bytes(original + b'tamper')
                with self.subTest(name=name), self.assertRaises(ValueError):
                    xs.validate_bundle(root, manifest, True)
                path.write_bytes(original)
