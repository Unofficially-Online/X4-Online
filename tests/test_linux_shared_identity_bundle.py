"""Shared identity bundle preparation and immutable receipts; no native launch."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest

import linux_shared_identity_bundle as shared
import linux_gate_live as live
from test_linux_shared_identity_overlay import SharedIdentityOverlayTests
from test_linux_ui_session_runner import SessionAdmissionTests
from linux_ui_session_budget import resolve_session_options

ROOT = Path(__file__).resolve().parents[1]


class SharedIdentityBundleTests(unittest.TestCase):
    def options(self):
        options = SessionAdmissionTests().options()
        options.update(player_startup_phases='v1-t40', shared_identity_t40=True,
                       shared_identity_p1='00000000000ad43a')
        return resolve_session_options(options)

    def bundle(self, directory):
        root = Path(directory) / 'bundle'
        base = SharedIdentityOverlayTests().base(directory)
        selected, metadata = shared.prepare_overlay(
            ROOT, base, Path(directory) / 'identity-overlay', self.options())
        extension = root / 'extension/x4_lan_lab'
        live.compose_extension_template(selected, extension)
        for source in shared.sources(ROOT):
            target = root / 'source-receipts' / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        for name in shared.BUNDLE_HELPERS:
            shutil.copy2(ROOT / 'tools/dev' / name, root / name)
        manifest = dict(self.options(), **metadata)
        self.seal(root, manifest)
        return root, manifest, base

    def seal(self, root, manifest):
        manifest['files'] = {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in root.rglob('*') if path.is_file()}

    def test_prepares_a_new_overlay_and_authenticates_all_composed_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, base = self.bundle(directory)
            self.assertTrue(shared.validate_bundle(root, manifest))
            self.assertFalse((base / 'shared-identity-profile.json').exists())
            self.assertEqual(manifest['shared_identity_plan_sha256'],
                             'bb588d3e9d290c2339220cbae3b122127427a85d513447a598750e979457fbcd')
            for source in shared.SOURCES:
                self.assertIn('source-receipts/' + source, manifest['files'])

    def test_explicit_mode_strict_types_and_matching_finite_tuple(self):
        self.assertFalse(shared.validate_options({}))
        for changes in ({'shared_identity_t40': 1}, {'shared_identity_t40': False},
                        {'shared_identity_p1': shared.TARGET}, {'player_startup_phases': None},
                        {'player_ui_session_frames': 0}, {'independent_keyboard_frames': 899},
                        {'player_ui_render_lease': False}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                shared.validate_options(dict(self.options(), **changes))

    def test_missing_source_tampered_runtime_and_resealed_wrong_configuration_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, _ = self.bundle(directory)
            missing = copy.deepcopy(manifest)
            missing['files'].pop('source-receipts/' + shared.SOURCES[0])
            with self.assertRaises(ValueError):
                shared.validate_bundle(root, missing)
            adapter = root / 'extension/x4_lan_lab/ui/native_gate_player_identity.lua'
            original = adapter.read_bytes()
            adapter.write_bytes(original + b'-- changed\n')
            self.seal(root, manifest)
            with self.assertRaises(ValueError):
                shared.validate_bundle(root, manifest)
            adapter.write_bytes(original)
            config = root / 'extension/x4_lan_lab/ui/config.lua'
            config.write_text(config.read_text().replace('native_gate_shared_identity_t40 =',
                                                        'native_gate_shared_identity_t40 = false --'))
            self.seal(root, manifest)
            with self.assertRaises(ValueError):
                shared.validate_bundle(root, manifest)

    def test_disabled_preparation_leaves_base_untouched_and_creates_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            base = SharedIdentityOverlayTests().base(directory)
            output = Path(directory) / 'unused'
            selected, metadata = shared.prepare_overlay(ROOT, base, output, {})
            self.assertEqual(selected, base)
            self.assertEqual(metadata, {'shared_identity_t40': False})
            self.assertFalse(output.exists())


class SharedIdentityRunnerTests(unittest.TestCase):
    def test_cli_rejects_identity_without_finite_t40_before_any_build(self):
        from unittest import mock
        with mock.patch.object(live.subprocess, 'run') as command, self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused', '--snapshot', '/unused',
                       '--out', '/unused', '--shared-identity-t40',
                       '--shared-identity-p1', '00000000000ad43a'])
        command.assert_not_called()

    def test_real_mocked_bundle_prepares_before_composition_and_validates_freeze(self):
        from unittest import mock
        from test_linux_t40_evidence import CommonStartupBundleTests
        import gzip
        import test_stock_keyboard_replay_overlay as keyboard_fixture
        original_snapshot = keyboard_fixture.snapshot_bytes

        def pinned_snapshot():
            xml = gzip.decompress(original_snapshot())
            return gzip.compress(xml.replace(b'[0x100]', b'[0x175310]'))
        original_main = live.main
        original_prepare = shared.prepare_overlay
        original_compose = live.compose_extension_template
        events = []

        def prepare(repo, base, output, options):
            selected, metadata = original_prepare(repo, base, output, options)
            if metadata['shared_identity_t40']:
                events.append('identity')
                self.assertTrue((selected / 'shared-identity-profile.json').is_file())
            return selected, metadata

        def compose(overlay, target, extension_base=None):
            if events:
                events.append('compose')
                self.assertEqual(Path(overlay).name, 'shared-identity-overlay')
            return original_compose(overlay, target, extension_base)

        def build(arguments):
            enabled = '--player-ui-seat-correction' in arguments
            if enabled:
                arguments = arguments + ['--shared-identity-t40', '--shared-identity-p1',
                                         '00000000000ad43a', '--battle']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertIs(manifest['shared_identity_t40'], enabled)
            self.assertEqual(shared.validate_bundle(output, manifest), enabled)
            if enabled:
                self.assertEqual(events, ['identity', 'compose'])
                for source in shared.SOURCES:
                    name = 'source-receipts/' + source
                    self.assertEqual(manifest['files'][name], live.sha(output / name))
                for helper in shared.BUNDLE_HELPERS:
                    self.assertEqual(manifest['files'][helper], live.sha(output / helper))
                # Runtime validation rejects a fresh, internally hashed plan
                # when it differs from the frozen selected ships.
                plan = output / 'extension/x4_lan_lab/player-identity-plan.json'
                changed = json.loads(plan.read_text())
                changed['slots'][0]['ship'] = '0000000000000001'
                plan.write_text(json.dumps(changed))
                manifest['files'][plan.relative_to(output).as_posix()] = live.sha(plan)
                with self.assertRaises(ValueError):
                    shared.validate_bundle(output, manifest)
            return result

        with (mock.patch.object(live, 'main', side_effect=build),
              mock.patch.object(shared, 'prepare_overlay', side_effect=prepare),
              mock.patch.object(live, 'compose_extension_template', side_effect=compose),
              mock.patch.object(keyboard_fixture, 'snapshot_bytes', side_effect=pinned_snapshot)):
            CommonStartupBundleTests().test_explicit_phase_config_source_freeze_and_capture_budget()
