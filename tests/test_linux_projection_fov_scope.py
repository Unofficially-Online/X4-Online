"""Exercise the pinned projection dispatcher without launching the game."""
from pathlib import Path
import subprocess
import tempfile
import unittest

LINUX = Path(__file__).resolve().parents[1] / "src/native/engine/linux"


class ProjectionFovScopeTest(unittest.TestCase):
    def test_original_once_and_guarded_lifecycle(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "projection_scope"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", str(LINUX / "test_player_projection_fov_scope_preload.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


class ProjectionScopeBundleTest(unittest.TestCase):
    def test_dependencies_environment_and_raw_receipts(self):
        import hashlib
        import linux_view_projection_scope_bundle as scope
        options = {'player_ui_projection_fov_scope': True, 'view_render_capture_shadow': True,
                   'player_ui_camera_refill': True, 'player_ui_session_frames': 180,
                   'player_startup_phases': 'v1-t40'}
        self.assertTrue(scope.enabled(options))
        with self.assertRaises(ValueError):
            scope.enabled(dict(options, player_ui_camera_refill=False))
        self.assertEqual(scope.environment({}, True, 2)['X4_DEV_UI_PROJECTION_FOV_SCOPE'], '1')
        self.assertNotIn('X4_DEV_UI_PROJECTION_FOV_SCOPE', scope.environment(
            {'X4_DEV_UI_PROJECTION_FOV_SCOPE': '1'}, True, 1))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stderr'
            budget = scope.budget_from_options(options)
            first, last = budget['first'] + 1, budget['last']
            rows = []
            for ordinal, tick in enumerate(range(first, last + 1), 1):
                rows.extend((f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n',
                             f'X4 UI CAMERA_REFILL v=1 stage=frame tick={tick} test\n',
                             f'X4 UI PROJECTION_FOV v=1 stage=frame tick={tick} ordinal={ordinal} '
                             'camera=abcd bank=0 restored=1\n',
                             f'X4 UI OVERLAY FRAME v=3 tick={tick} test\n'))
                if tick == last:
                    rows.extend((
                        f'X4 UI GAME_RENDER v=1 stage=frame tick={last} test\n',
                        f'X4 UI GAME_RENDER v=1 stage=close tick={last} count={budget["callbacks"]} restored=1\n',
                        f'X4 UI PROJECTION_FOV v=1 stage=close tick={last} count={last-first+1} restored=1\n',
                        f'X4 UI LIVE_POSE v=1 stage=close tick={last} test\n',
                        f'X4 UI POSE_SCOPE v=1 stage=close tick={last} test\n',
                        f'X4 UI SEAT_CORRECTION v=1 stage=close tick={last} test\n',
                        f'X4 UI CAMERA_REFILL v=1 stage=close tick={last} count={last-first+1} restored=1\n'))
                rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release\n')
            rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={budget["recurring_end"]} tail=1 event=enter\n')
            path.write_text(''.join(rows))
            proof = {'pass': True, 'stderr_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                     'returncode': 223, 'frame_count': 180, 'frames': [{'tick': budget['recurring_end']}]}
            self.assertTrue(scope.collect(path, proof, options, 2, True)['physics_bound'])
            self.assertTrue(scope.collect_order(path, options, 2)['pass'])
            self.assertFalse(scope.collect_order(path, options, 2)['physics_bound'])
            self.assertFalse(scope.collect(path, None, options, 2, True)['pass'])
            self.assertFalse(scope.collect(path, proof, options, 1, True)['pass'])
            self.assertFalse(scope.collect(path, proof, options, 2, False)['pass'])
            close = next(row for row in rows if 'PROJECTION_FOV v=1 stage=close' in row)
            for before in ('X4 UI OVERLAY FRAME', 'X4 UI GAME_RENDER v=1 stage=frame',
                           'X4 UI GAME_RENDER v=1 stage=close', 'X4 UI CAMERA_REFILL v=1 stage=close',
                           'X4LAN PLAYER_CONTROL_RECURRING_WORKER'):
                modified = [row for row in rows if row != close]
                # Last occurrence targets the final frame/terminal, not startup.
                target = max(i for i, row in enumerate(modified) if row.startswith(before))
                modified.insert(target, close)
                with self.subTest(before=before):
                    path.write_text(''.join(modified))
                    fresh = dict(proof, stderr_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                    self.assertFalse(scope.collect(path, fresh, options, 2, True)['pass'])
                    self.assertFalse(scope.collect_order(path, options, 2)['pass'])
            for modified in ([row for row in rows if row != close] + [close], rows + [close]):
                path.write_text(''.join(modified))
                fresh = dict(proof, stderr_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertFalse(scope.collect(path, fresh, options, 2, True)['pass'])
            path.write_text(''.join(rows))
            for change in ({'returncode': 0}, {'frame_count': 179}, {'frames': []},
                           {'frames': [{'tick': last}]}):
                with self.subTest(physics=change):
                    self.assertFalse(scope.collect(path, dict(proof, **change), options, 2, True)['pass'])
            self.assertTrue(scope.collect_order(path, options, 2)['pass'])
            path.write_text(''.join(rows).replace('ordinal=2 ', 'ordinal=3 ', 1))
            self.assertFalse(scope.collect(path, proof, options, 2, True)['pass'])

    def test_version_delegation(self):
        from unittest.mock import patch
        import linux_view_projection_scope_bundle as scope
        with patch.object(scope.previous, 'validate_bundle', return_value=True) as previous:
            self.assertTrue(scope.validate_bundle('/unused', {'view_query_source_version': 11}, True))
            self.assertEqual(previous.call_args.args[1]['view_query_source_version'], 11)

    def test_source_closure_detects_missing_and_changed_files(self):
        import hashlib
        from unittest.mock import patch
        import linux_view_projection_scope_bundle as scope
        with tempfile.TemporaryDirectory() as directory, patch.object(scope.previous, 'validate_bundle', return_value=True):
            root = Path(directory)
            files = {}
            for name in scope.SOURCES:
                path = root / 'source-receipts' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
                files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            for name in scope.HELPERS:
                path = root / name
                path.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
                files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {'view_query_source_version': 12, 'files': files}
            self.assertTrue(scope.validate_bundle(root, manifest, True))
            (root / scope.HELPERS[-1]).unlink()
            with self.assertRaises(ValueError):
                scope.validate_bundle(root, manifest, True)

    def test_runner_flag(self):
        from unittest.mock import patch
        import linux_gate_live as live
        with patch.object(live, 'bundle') as bundle:
            live.main(['bundle', '--zig', '/unused', '--snapshot', '/unused', '--out', '/unused',
                       '--player-ui-projection-fov-scope'])
            self.assertTrue(bundle.call_args.args[0].player_ui_projection_fov_scope)
