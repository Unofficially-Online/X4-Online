import linux_semaphore_observer_bundle as latest_bundle
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import linux_stock_view_fov_observer as observer
import linux_player_cursor_bundle as cursor
import linux_input_observation_bundle as trace_bundle
import linux_ui_camera_refill_bundle as camera


class StockViewObserverTests(unittest.TestCase):
    def options(self):
        return dict(stock_view_fov_observer=True, player_startup_phases='v1-t40',
                    player_ui_session_frames=180, independent_keyboard_frames=180, player_view_query_observer=True)

    def test_explicit_options_and_environment_never_enable_rewrite(self):
        self.assertFalse(observer.enabled({}))
        self.assertTrue(observer.enabled(self.options()))
        for key, value in [('stock_view_fov_observer', 1), ('player_startup_phases', None),
                           ('player_ui_session_frames', 901), ('player_view_query_observer', False)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                observer.enabled(dict(self.options(), **{key: value}))
        env = {'X4_DEV_STOCK_VIEW_FOV': '1', 'X4_DEV_STOCK_VIEW_FOV_OBSERVE': 'bad', 'OTHER': 'keep'}
        self.assertEqual(observer.environment(env, False), {'OTHER': 'keep'})
        self.assertEqual(observer.environment(env, True), {'OTHER': 'keep', 'X4_DEV_STOCK_VIEW_FOV_OBSERVE': '1'})

    def test_disabled_normal_run_needs_no_lifecycle_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr = Path(directory) / 'stderr'
            stderr.write_text('ordinary output\n')
            missing = Path(directory) / 'no-native-trace'
            self.assertTrue(observer.collect(stderr, missing, False, {})['pass'])
            self.assertFalse(observer.collect(stderr, missing, True, {})['pass'])

    def test_rewrite_activation_requires_explicit_mode_and_same_raw_physics(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr, trace = Path(directory) / 'stderr', Path(directory) / 'trace'
            trace.write_text('accepted native lifecycle bytes\n')
            def proof(rows):
                stderr.write_text(''.join(rows))
                return {'pass': True, 'stderr_sha256': hashlib.sha256(stderr.read_bytes()).hexdigest(),
                        'log_sha256': hashlib.sha256(trace.read_bytes()).hexdigest()}
            accepted = proof([observer.REWRITE_ACTIVE])
            self.assertFalse(observer.collect(stderr, trace, False, accepted)['pass'])
            self.assertFalse(observer.collect(stderr, trace, True, accepted)['pass'])
            corrected = observer.collect(stderr, trace, False, accepted, rewrite=True)
            self.assertTrue(corrected['pass'], corrected['errors'])
            self.assertTrue(corrected['rewrite_mode_admitted'])
            self.assertFalse(corrected['performed'])
            self.assertFalse(corrected['rewrite_effects_proved'])
            for changed in ({'pass': False}, {'stderr_sha256': '0' * 64}, {'log_sha256': '0' * 64}):
                with self.subTest(proof=changed):
                    self.assertFalse(observer.collect(stderr, trace, False,
                        dict(accepted, **changed), rewrite=True)['pass'])
            self.assertFalse(observer.collect(stderr, trace, True, accepted, rewrite=True)['pass'])
            self.assertFalse(observer.collect(stderr, trace, False, accepted, rewrite=1)['pass'])
            for rows in ([], [observer.ACTIVE], [observer.REWRITE_ACTIVE] * 2,
                         [observer.REWRITE_ACTIVE.rstrip('\n')],
                         [observer.REWRITE_ACTIVE.replace('rewrite', 'unknown')],
                         [observer.REWRITE_ACTIVE, observer.ACTIVE],
                         [observer.REWRITE_ACTIVE, observer.PREFIX + 'unexpected\n']):
                with self.subTest(rows=rows):
                    self.assertFalse(observer.collect(stderr, trace, False, proof(rows), rewrite=True)['pass'])
            accepted = proof([observer.REWRITE_ACTIVE])
            trace.unlink()
            self.assertFalse(observer.collect(stderr, trace, False, accepted, rewrite=True)['pass'])

    def test_real_bundle_path_freezes_observer_default_and_activation(self):
        import linux_gate_live as live
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main

        def build(arguments):
            active = '--player-ui-seat-correction' in arguments
            if active:
                cleaned = []
                skip = False
                for token in arguments:
                    if skip:
                        skip = False
                        continue
                    if token in ('--independent-keyboard-frames', '--player-ui-overlay-callbacks'):
                        skip = True
                        continue
                    if token != '--player-ui-overlay-probe':
                        cleaned.append(token)
                arguments = cleaned + ['--player-ui-session-frames', '180', '--ticks', '220',
                                       '--player-startup-phases', 'v1-t40', '--stock-view-fov-observer']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertIs(manifest['stock_view_fov_observer'], active)
            self.assertEqual(manifest['view_query_source_version'], latest_bundle.SOURCE_VERSION)
            self.assertFalse(latest_bundle.validate_bundle(output, manifest, True))
            for source in observer.SOURCES:
                receipt = 'source-receipts/' + source
                self.assertEqual(manifest['files'][receipt], live.sha(output / receipt))
            return result

        with mock.patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()

    def fixture(self, directory):
        stderr, trace = Path(directory) / 'stderr', Path(directory) / 'trace'
        rows = [observer.ACTIVE, 'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=219 tail=1 event=enter\n']
        rows += [f'{observer.PREFIX}v=1 site={site} slot={slot} owner_outside=0 owner_parked=1 owner_scoped=2 foreign_lower=0\n'
                 for site in range(3) for slot in range(1, 4)]
        rows += [f'{observer.PREFIX}v=1 stage=close tick=219 coverage=bounded foreign=lower_bound unpatched=0\n']
        trace.write_text('X4LAN LIFECYCLE_DISCARD result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n')
        def proof():
            stderr.write_text(''.join(rows))
            return dict(frames=[{'tick': 219}],
                        stderr_sha256=hashlib.sha256(stderr.read_bytes()).hexdigest(),
                        log_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(), **{'pass': True})
        return stderr, trace, rows, proof

    def test_exact_nine_rows_zero_valid_and_no_rewrite_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr, trace, rows, proof = self.fixture(directory)
            result = observer.collect(stderr, trace, True, proof())
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(len(result['counts']), 9)
            self.assertFalse(result['stock_values_rewritten'])
            rows[:] = [row.replace('owner_parked=1 owner_scoped=2', 'owner_parked=0 owner_scoped=0') for row in rows]
            self.assertTrue(observer.collect(stderr, trace, True, proof())['pass'])

    def test_missing_duplicate_reorder_bad_count_activation_and_tail_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr, trace, rows, proof = self.fixture(directory)
            original = list(rows)
            variants = [original[1:], original + [observer.ACTIVE], original[:2] + original[3:],
                        original[:2] + [original[3], original[2]] + original[4:],
                        [x.replace('owner_parked=1', 'owner_parked=18446744073709551616') for x in original],
                        [x.replace('tick=219', 'tick=218') if 'stage=close' in x else x for x in original],
                        [x.rstrip('\n') if 'site=0 slot=1' in x else x for x in original]]
            for variant in variants:
                rows[:] = variant
                self.assertFalse(observer.collect(stderr, trace, True, proof())['pass'])
            rows[:] = original
            accepted = proof()
            self.assertFalse(observer.collect(stderr, trace, False, accepted)['pass'])
            self.assertFalse(observer.collect(stderr, trace, True, dict(accepted, log_sha256='0'*64))['pass'])
            self.assertFalse(observer.collect(stderr, trace, True, dict(accepted, **{'pass': False}))['pass'])

    def test_postphysics_collection_uses_validated_proofs_and_retains_other_failure(self):
        import linux_gate_live as live
        first, second = {'pass': True, 'frames': [{'tick': 219}]}, {'pass': True, 'frames': [{'tick': 219}]}
        result = {'pass': False, 'postprocessing_error': 'earlier observation failed',
                  'physics_discard': {'pass': True, 'clients': {'1': first, '2': second}}}
        games = [(None, Path('/profile1')), (None, Path('/profile2'))]
        with mock.patch.object(observer, 'collect', return_value={'pass': True}) as collect:
            live.collect_stock_view_observations(result, [1, 2], games, Path('/run'), False, rewrite=True)
            self.assertIs(collect.call_args_list[0].args[-1], first)
            self.assertIs(collect.call_args_list[1].args[-1], second)
            self.assertTrue(all(call.kwargs == {'rewrite': True} for call in collect.call_args_list))
        self.assertEqual(result['postprocessing_error'], 'earlier observation failed')
        self.assertFalse(result['pass'])
        partial = {'pass': True, 'physics_discard': {'pass': True, 'clients': {'1': first}}}
        with mock.patch.object(observer, 'collect', return_value={'pass': True}):
            live.collect_stock_view_observations(partial, [1], games[:1], Path('/run'), False, rewrite=True)
        self.assertFalse(partial['pass'])
        self.assertFalse(partial['physics_discard']['pass'])
        with mock.patch.object(observer, 'collect', return_value={'pass': False}):
            live.collect_stock_view_observations(result, [1, 2], games, Path('/run'), False)
        self.assertFalse(result['physics_discard']['pass'])

    def test_version_four_freezes_all_headers_even_off_preserves_v3(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = {'view_query_source_version': 3}
            with mock.patch.object(camera, 'validate_bundle', return_value=False) as previous:
                self.assertFalse(observer.validate_bundle(root, manifest, True))
                previous.assert_called_once()
                current = dict(manifest, view_query_source_version=4, files={})
                with self.assertRaises(ValueError):
                    observer.validate_bundle(root, current, True)
                for name in observer.SOURCES:
                    path = root / 'source-receipts' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(name)
                    current['files']['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertFalse(observer.validate_bundle(root, current, True))
                for name in observer.SOURCES:
                    bad = dict(current, files=dict(current['files']))
                    bad['files'].pop('source-receipts/' + name)
                    with self.subTest(name=name), self.assertRaises(ValueError):
                        observer.validate_bundle(root, bad, True)
