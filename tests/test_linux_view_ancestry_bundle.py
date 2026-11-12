import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_view_ancestry_bundle as bundle


class BundleTest(unittest.TestCase):
    def test_strict_options_and_legacy(self):
        with self.assertRaises(ValueError):
            bundle.validate_options({'view_ancestry_observer': 1}, True)
        with self.assertRaises(ValueError):
            bundle.validate_bundle(Path('.'), {'view_ancestry_observer': True,
                'player_view_query_observer': True, 'independent_keyboard_frames': 180}, True)
        self.assertFalse(bundle.validate_bundle(Path('.'), {}, False))
        with self.assertRaises(ValueError):
            bundle.validate_options({'view_ancestry_observer': True,
                'player_view_query_observer': True, 'independent_keyboard_frames': 180}, False)

    def test_versioned_frozen_sources_even_when_sampling_disabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = {}
            for name in bundle.EXTRA_SOURCES:
                key = 'source-receipts/' + name
                path = root / key
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
                files[key] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {'view_query_source_version': 2, 'player_view_query_observer': False,
                        'view_ancestry_observer': False, 'files': files}
            self.assertFalse(bundle.validate_bundle(root, manifest, True))
            manifest.update(view_ancestry_observer=True, player_view_query_observer=True,
                            independent_keyboard_frames=180)
            self.assertTrue(bundle.validate_bundle(root, manifest, True))
            (root / ('source-receipts/' + bundle.EXTRA_SOURCES[0])).write_text('changed')
            with self.assertRaises(ValueError):
                bundle.validate_bundle(root, manifest, True)
            manifest['view_query_source_version'] = 3
            with self.assertRaises(ValueError):
                bundle.validate_bundle(root, manifest, True)

    def test_environment_does_not_inherit_unowned_optin(self):
        env = {'X4_DEV_VIEW_ANCESTRY': '1', 'OTHER': 'ok'}
        self.assertEqual(bundle.environment(env, False), {'OTHER': 'ok'})
        self.assertEqual(bundle.environment(env, True)['X4_DEV_VIEW_ANCESTRY'], '1')
        self.assertEqual(env['X4_DEV_VIEW_ANCESTRY'], '1')


if __name__ == '__main__':
    unittest.main()
