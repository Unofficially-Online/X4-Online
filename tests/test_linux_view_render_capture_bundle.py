import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import linux_view_render_capture_bundle as bundle
import linux_stock_view_fov_observer as stock

class ViewRenderClosureTests(unittest.TestCase):
    def test_inherited_flags_are_cleared(self):
        result = stock.environment({'X4_DEV_RENDER_FOV_OBSERVE': '1', 'X4_DEV_RENDER_FOV_CAPTURE': '1'}, False)
        self.assertNotIn('X4_DEV_RENDER_FOV_OBSERVE', result)
        self.assertNotIn('X4_DEV_RENDER_FOV_CAPTURE', result)

    def test_closure_and_old_delegation(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bundle.previous, 'validate_bundle', return_value=True) as previous:
            root = Path(tmp); files = {}
            for name in bundle.SOURCES:
                path = root / 'source-receipts' / name
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(name.encode())
                files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            for name in bundle.HELPERS:
                path = root / name; path.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
                files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {'view_query_source_version': 10, 'files': files}
            self.assertTrue(bundle.validate_bundle(root, manifest, True))
            self.assertEqual(previous.call_args.args[1]['view_query_source_version'], 9)
            (root / 'source-receipts' / bundle.SOURCES[-1]).write_text('changed')
            with self.assertRaises(ValueError): bundle.validate_bundle(root, manifest, True)
            self.assertTrue(bundle.validate_bundle(root, {'view_query_source_version': 9}, True))
            self.assertEqual(previous.call_args.args[1]['view_query_source_version'], 9)

    def test_bounded_receipts_and_raw_hash(self):
        options = {'player_startup_phases': 'v1-t40', 'player_ui_session_frames': 180,
                   'independent_keyboard_frames': 180, 'player_ui_render_lease': True,
                   'player_cursor_active_flight': True, 'player_view_query_observer': True,
                   'view_render_capture_shadow': True}
        self.assertTrue(bundle.enabled(options))
        self.assertNotIn('X4_DEV_RENDER_FOV_CAPTURE', bundle.environment({}, True, 1))
        self.assertEqual(bundle.environment({}, True, 2)['X4_DEV_RENDER_FOV_CAPTURE'], '1')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'stderr'
            rows = []
            for tick in range(38, 218):
                present = 50 <= tick <= 213
                rows.append(f'X4 VIEW_RENDER_CAPTURE v=1 tick={tick} present={int(present)} bits={int(present)} fov={"3f800000" if present else "00000000"} target=0000000000000000\n')
            log.write_text(''.join(rows))
            proof = {'pass': True, 'stderr_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
            self.assertTrue(bundle.collect(log, proof, options, 2, True)['pass'])
            self.assertFalse(bundle.collect(log, proof, options, 1, True)['pass'])
            self.assertFalse(bundle.collect(log, proof, options, 2, False)['pass'])
            log.write_text(''.join(rows).replace('tick=50 present=1', 'tick=50 present=0'))
            self.assertFalse(bundle.collect(log, proof, options, 2, True)['pass'])
            proof['stderr_sha256'] = hashlib.sha256(log.read_bytes()).hexdigest()
            self.assertFalse(bundle.collect(log, proof, options, 2, True)['pass'])
