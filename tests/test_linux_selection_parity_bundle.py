"""Selection observation admission, frozen propagation and raw proof binding."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import linux_selection_parity_bundle as parity


class SelectionParityBundleTests(unittest.TestCase):
    def options(self):
        return dict(player_selection_parity=True, view_host_capture_shadow=True,
                    player_startup_phases='v1-t40', player_ui_session_frames=180,
                    independent_keyboard_frames=180, player_cursor_active_flight=True,
                    player_view_query_observer=True, stock_view_fov_observer=True)

    def test_admission_and_ambient_environment(self):
        self.assertFalse(parity.enabled({}))
        options = self.options()
        self.assertTrue(parity.enabled(options))
        for change in ({'player_selection_parity': 1}, {'view_host_capture_shadow': False},
                       {'player_startup_phases': None}, {'independent_keyboard_frames': 179},
                       {'player_ui_session_frames': 901}, {'player_cursor_active_flight': False}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                parity.enabled(dict(options, **change))
        env = {'X4_DEV_SELECTION_PARITY': 'ambient', 'KEEP': 'yes'}
        self.assertEqual(parity.environment(env, False, 1, {}), {'KEEP': 'yes'})
        self.assertEqual(parity.environment(env, True, 2, options), {'KEEP': 'yes'})
        self.assertEqual(parity.environment(env, True, 1, options),
                         {'KEEP': 'yes', 'X4_DEV_SELECTION_PARITY': '1'})
        guest = dict(options, view_host_capture_shadow=False, view_render_capture_shadow=True,
                     player_ui_render_lease=True)
        self.assertEqual(parity.environment(env, True, 1, guest), {'KEEP': 'yes'})
        self.assertEqual(parity.environment(env, True, 2, guest)['X4_DEV_SELECTION_PARITY'], '1')
        with self.assertRaises(ValueError):
            parity.environment(env, True, 1, {})

    def test_frozen_closure_and_old_version_delegation(self):
        with patch.object(parity.previous, 'validate_bundle', return_value='previous') as old:
            self.assertEqual(parity.validate_bundle('/unused', {'view_query_source_version': 12}, False), 'previous')
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 12)
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                files = {}
                for name in parity.SOURCES:
                    path = root / 'source-receipts' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(name)
                    files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
                for name in parity.HELPERS:
                    path = root / name
                    path.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
                    files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
                manifest = dict(self.options(), view_query_source_version=13, files=files,
                                player_selection_parity_evidence_version=1)
                self.assertEqual(parity.validate_bundle(root, manifest, True), 'previous')
                self.assertEqual(old.call_args.args[1]['view_query_source_version'], 12)
                for change in ({'view_query_source_version': 12}, {'player_selection_parity_evidence_version': None},
                               {'player_selection_parity_evidence_version': True}):
                    with self.assertRaises(ValueError):
                        parity.validate_bundle(root, dict(manifest, **change), True)
                with self.assertRaises(ValueError):
                    parity.validate_bundle(root, manifest, False)
                for path in (root / 'linux_selection_parity_bundle.py',
                             root / 'source-receipts/src/native/engine/linux/player_selection_parity.h'):
                    original = path.read_bytes()
                    path.write_bytes(original + b'tamper')
                    with self.subTest(path=path), self.assertRaises(ValueError):
                        parity.validate_bundle(root, manifest, True)
                    path.write_bytes(original)
                self.assertEqual(parity.validate_bundle(root, dict(manifest, player_selection_parity=False), True), 'previous')

    def test_complete_raw_receipts_and_physics_binding(self):
        options = self.options()
        budget = parity.budget_from_options(options)
        rows = []
        for tick in range(budget['first'] + 6, budget['last'] + 1):
            rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter')
            rows.append(f'X4 SELECTION_PARITY v=1 stage=identity tick={tick} slot=1 tid=123 '
                        'local=aaa controller=ccc p1=aaa p1_runtime=0000000000000001 p2=bbb p2_runtime=0000000000000002')
            for source in ('p1_player', 'p2_player', 'local_ui'):
                rows.append(f'X4 SELECTION_PARITY v=1 tick={tick} slot=1 source={source} '
                            'status=absent at=eee raw=0000000000000000 object=0 runtime=0000000000000000 '
                            'mapping=none saved=0000000000000000 portable=0000000000000000')
            rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release')
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'stderr'
            text = '\n'.join(rows) + '\n'
            log.write_text(text)
            proof = {'pass': True, 'stderr_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
            result = parity.collect(log, proof, options, 1, True)
            self.assertTrue(result['pass'], result['errors'])
            self.assertFalse(result['selection_equivalence_proved'])
            self.assertFalse(parity.collect(log, None, options, 1, True)['pass'])
            self.assertFalse(parity.collect(log, proof, options, 2, True)['pass'])
            self.assertFalse(parity.collect(log, proof, options, 1, False)['pass'])
            variants = ('\n'.join(rows[:-2]) + '\n', text.replace('source=p2_player', 'source=p1_player', 1),
                        text.replace('local=aaa', 'local=bbb', 1), text.replace('event=release', 'event=other', 1))
            for changed in variants:
                log.write_text(changed)
                fresh = {'pass': True, 'stderr_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
                self.assertFalse(parity.collect(log, fresh, options, 1, True)['pass'])
            log.write_text(text + 'unrelated appended raw byte\n')
            self.assertFalse(parity.collect(log, proof, options, 1, True)['pass'])

    def test_mocked_runner_build_default_and_enabled(self):
        import linux_gate_live as live
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main
        seen = []

        def build(arguments):
            active = '--player-ui-seat-correction' in arguments
            if active:
                converted = []
                skip = False
                for token in arguments:
                    if skip:
                        skip = False
                    elif token in ('--independent-keyboard-frames', '--player-ui-overlay-callbacks', '--ticks'):
                        skip = True
                    elif token != '--player-ui-overlay-probe':
                        converted.append(token)
                arguments = converted + ['--player-ui-session-frames', '180', '--ticks', '220',
                    '--player-startup-phases', 'v1-t40', '--player-cursor-active-flight',
                    '--stock-view-fov-observer', '--view-host-capture-shadow', '--player-selection-parity']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['view_query_source_version'], 18)
            self.assertIs(manifest['player_selection_parity'], active)
            self.assertEqual(manifest['player_selection_parity_evidence_version'], 1 if active else None)
            import linux_semaphore_observer_bundle as component
            component.validate_bundle(output, manifest, True)
            for name in parity.SOURCES:
                self.assertEqual(manifest['files']['source-receipts/' + name],
                                 live.sha(output / 'source-receipts' / name))
            seen.append(active)
            return result

        with patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()
        self.assertEqual(set(seen), {False, True})
