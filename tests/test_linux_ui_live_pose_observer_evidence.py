"""Default-off live pose observation: synthetic evidence and staged files only."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_gate_live as live


class LivePoseAdmissionTests(unittest.TestCase):
    def options(self):
        return {
            'player_ui_live_pose_observer': True,
            'player_ui_seat_query_probe': True,
            'player_ui_overlay_probe': True,
            'player_ui_overlay_callbacks': 160,
            'player_presentation_fork_probe': True,
            'player_presentation_probe': True,
            'player_view_query_observer': True,
            'ui_callback_observer': True,
            'player_lifetime_sha256': 'frozen lifetime fixture',
        }

    def test_requires_explicit_seat_query_and_all_overlay_dependencies(self):
        self.assertFalse(live.validate_live_pose_observer({}))
        self.assertTrue(live.validate_ui_overlay(self.options(), 180))
        for value in (None, 0, 1, 'true'):
            options = dict(self.options(), player_ui_live_pose_observer=value)
            with self.subTest(value=value), self.assertRaises(SystemExit):
                live.validate_live_pose_observer(options)
        for key in ('player_ui_seat_query_probe', 'player_ui_overlay_probe',
                    'player_presentation_fork_probe', 'player_presentation_probe',
                    'player_view_query_observer', 'ui_callback_observer', 'player_lifetime_sha256'):
            options = dict(self.options(), **{key: False})
            with self.subTest(key=key), self.assertRaises(SystemExit):
                live.validate_ui_overlay(options, 180)
        for count in (1, 8, 159, 161, True, '160'):
            options = dict(self.options(), player_ui_overlay_callbacks=count)
            with self.subTest(count=count), self.assertRaises(SystemExit):
                live.validate_live_pose_observer(options)
        with self.assertRaises(SystemExit):
            live.validate_ui_overlay(self.options(), 164)
        with mock.patch.object(live.subprocess, 'run') as run, self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused', '--snapshot', '/unused',
                       '--out', '/unused', '--player-ui-live-pose-observer'])
        run.assert_not_called()

    def test_guest_only_environment_and_exact_frozen_config(self):
        self.assertEqual(live.live_pose_observer_environment(True, 2),
                         {'X4_DEV_UI_LIVE_POSE_OBSERVER': '1'})
        self.assertEqual(live.live_pose_observer_environment(True, 1), {})
        self.assertEqual(live.live_pose_observer_environment(False, 2), {})
        config = 'X4LAN_CONFIG.native_gate_linux_ui_live_pose_observer = true\n'
        live.validate_live_pose_observer_config(config, True)
        live.validate_live_pose_observer_config('', False)
        for text, enabled in (('', True), (config, False), (config + config, True),
                              (config.replace('true', '1'), True)):
            with self.subTest(text=text, enabled=enabled), self.assertRaises(SystemExit):
                live.validate_live_pose_observer_config(text, enabled)

    def test_all_declared_sources_are_required_and_frozen(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in live.LIVE_POSE_SOURCES:
                source = root / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text(name)
            sources = live.live_pose_observer_sources(root)
            live.freeze_source_receipts(root, root / 'bundle', sources)
            for source in sources:
                receipt = root / 'bundle/source-receipts' / source.relative_to(root)
                self.assertEqual(live.sha(source), live.sha(receipt))
            sources[0].unlink()
            with self.assertRaises(SystemExit):
                live.live_pose_observer_sources(root)


class LivePoseBundleTests(unittest.TestCase):
    def test_generated_bundle_forwards_flag_and_freezes_exact_provider_sources(self):
        import contextlib
        import io
        import json
        import build_native_gate_overlay as overlay_builder
        import test_stock_keyboard_replay_overlay as keyboard_fixture

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / 'snapshot.gz'
            snapshot.write_bytes(keyboard_fixture.snapshot_bytes())
            view_sources = [ROOT / name for name in live.VIEW_QUERY_SOURCES]
            observer_sources = [ROOT / name for name in live.LIVE_POSE_SOURCES]
            fixture_sources = set(view_sources + observer_sources)
            original_copy = live.shutil.copy2
            for enabled in (False, True):
                output = root / str(enabled)
                commands = []

                def copy_file(source, destination, *args, **kwargs):
                    source = Path(source)
                    if source.name in (live.PLAYER_LIFETIME, live.PLAYER_VIEW_QUERY):
                        Path(destination).write_bytes(b'fixture preload')
                        return destination
                    if source in fixture_sources and not source.is_file():
                        Path(destination).write_bytes(('fixture source ' + source.name).encode())
                        return destination
                    return original_copy(source, destination, *args, **kwargs)

                def command(argv, **kwargs):
                    commands.append(argv)
                    if 'build_native_gate_linux.py' in str(argv[1]):
                        (output / '.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(argv[1]):
                        with mock.patch.object(sys, 'argv', argv[1:]):
                            overlay_builder.main()
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in argv else '', returncode=0)

                selected = ['--player-ui-live-pose-observer'] if enabled else []
                arguments = [
                    'bundle', '--zig', '/unused', '--snapshot', str(snapshot), '--out', str(output),
                    '--stock-keyboard-observer', '--serial-pathing', '--player-factory-probe',
                    '--stock-keyboard-replay-probe', '--player-lifetime-preload',
                    '--player-factory-retain-until', '60', '--independent-keyboard-frames', '180',
                    '--player-presentation-probe', '--player-presentation-fork-probe',
                    '--player-view-query-observer', '--ui-callback-observer', '--player-ui-overlay-probe',
                    '--player-ui-overlay-callbacks', '160', '--player-ui-seat-query-probe', *selected,
                ]
                with (mock.patch.object(live.subprocess, 'run', side_effect=command),
                      mock.patch.object(live.shutil, 'copy2', side_effect=copy_file),
                      mock.patch.object(live, 'view_query_sources', return_value=view_sources),
                      mock.patch.object(live, 'live_pose_observer_sources', return_value=observer_sources),
                      contextlib.redirect_stdout(io.StringIO())):
                    live.main(arguments)
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertIs(manifest['player_ui_live_pose_observer'], enabled)
                self.assertEqual(manifest['player_ui_live_pose_observer_evidence_version'],
                                 2 if enabled else None)
                live.validate_live_pose_evidence_contract(manifest)
                self.assertEqual(manifest['files']['linux_ui_pose_scope_evidence.py'],
                                 live.sha(output / 'linux_ui_pose_scope_evidence.py'))
                live.validate_live_pose_observer_config(
                    (output / 'extension/x4_lan_lab/ui/config.lua').read_text(), enabled)
                build_argv = next(argv for argv in commands if 'build_native_gate_overlay.py' in str(argv[1]))
                self.assertEqual('--player-ui-live-pose-observer' in build_argv, enabled)
                self.assertEqual(manifest['files']['linux_ui_live_pose_observer_evidence.py'],
                                 live.sha(output / 'linux_ui_live_pose_observer_evidence.py'))
                # Provider headers are compiled into the existing preload even with the feature off.
                expected = view_sources + (observer_sources if enabled else [])
                for source in expected:
                    receipt = 'source-receipts/' + source.relative_to(ROOT).as_posix()
                    self.assertEqual(manifest['files'][receipt], live.sha(output / receipt))


class LivePoseEvidenceTests(unittest.TestCase):
    def setUp(self):
        import test_linux_ui_seat_query_evidence as query_fixtures
        self.fixture = query_fixtures.SeatQueryTests()
        self.fixture.setUp()
        self.native = self.make_native(2)

    def make_native(self, count):
        phase_counts = dict.fromkeys(('move', 'render', 'both', 'other'), 0)
        mode_counts = dict.fromkeys(('mode0', 'mode1', 'mode_other'), 0)
        samples = []
        for index in range(count):
            move, render, mode = ((1, 0, 0), (0, 1, 1), (0, 0, 2))[index % 3]
            phase = 'move' if move else 'render' if render else 'other'
            phase_counts[phase] += 1
            mode_counts[('mode0', 'mode1', 'mode_other')[mode]] += 1
            if index < 32:
                samples.append(
                    f'X4 UI LIVE_POSE v=1 stage=sample index={index} last_move={9+index} '
                    f'caller=100000 time=401ccccccccccccd mode={mode} move={move} render={render} '
                    'before_valid=1 after_valid=1 before=' + '0'*128 + ' after=' + '0'*128 + '\n')
        close = (
            f'X4 UI LIVE_POSE v=1 stage=close tick=168 started={count} returned={count} nested=0 '
            f'rows={min(count, 32)} omitted={max(count-32, 0)} ' +
            ' '.join(f'{key}={value}' for key, value in {**phase_counts, **mode_counts}.items()) +
            ' foreign=unobserved closed=1 lifetime=pointer_only\n')
        rows = ['X4 LIVE POSE PRELOAD ACTIVE version=1\n']
        for line in self.fixture.native:
            if line.startswith('X4 UI OVERLAY FRAME v=3 tick=168 ordinal=160 '):
                rows.extend(samples)
                rows.append(close)
            rows.append(line)
            if line.startswith('X4 UI SEAT_QUERY v=1 tick=9 stage=complete '):
                rows.append('X4 UI LIVE_POSE v=1 stage=bind tick=9 target=10000 lifetime=pointer_only\n')
        return rows

    def parse(self, native=None, slot=2, enabled=True):
        import linux_ui_live_pose_observer_evidence as evidence
        return evidence.parse_ui_live_pose_observer_evidence(
            self.native if native is None else native, slot, enabled,
            self.fixture.overlay, self.fixture.fork, self.fixture.parse())

    def test_zero_observations_and_capped_samples_are_distinct(self):
        empty = self.parse(self.make_native(0))
        self.assertTrue(empty['pass'], empty['errors'])
        self.assertFalse(empty['route_observed'])
        self.assertTrue(empty['sample_coverage_complete'])
        result = self.parse(self.make_native(35))
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['route_observed'])
        self.assertEqual(result['counts']['rows'], 32)
        self.assertEqual(result['counts']['omitted'], 3)
        self.assertFalse(result['sample_coverage_complete'])
        self.assertEqual(result['foreign_calls'], 'unobserved')
        self.assertTrue(result['resident_forwarder'])
        for key in ('readiness_proven', 'pose_installation_proven', 'safe_resume_proven', 'reader_exclusion_proven'):
            self.assertFalse(result[key])

    def test_preload_activation_is_exact_once_guest_only_and_precedes_bind(self):
        activation = 'X4 LIVE POSE PRELOAD ACTIVE version=1\n'
        for native in (self.native[1:], self.native + [activation],
                       self.native[1:] + [activation],
                       [activation.replace('ACTIVE', 'REFUSED')] + self.native[1:]):
            with self.subTest(native=native[:1]):
                self.assertFalse(self.parse(native)['pass'])
        self.assertFalse(self.parse([activation], slot=1)['pass'])
        self.assertFalse(self.parse([activation], enabled=False)['pass'])
        self.assertTrue(self.parse([], slot=1)['pass'])

    def test_native_bounds_counter_accounting_and_output_availability(self):
        for old, new in (
            ('started=2', 'started=3'), ('returned=2', 'returned=1'),
            ('nested=0', 'nested=1'), ('rows=2', 'rows=1'), ('omitted=0', 'omitted=1'),
            ('other=0', 'other=1'), ('mode_other=0', 'mode_other=1'),
            ('closed=1', 'closed=0'), ('foreign=unobserved', 'foreign=0'),
            ('index=0', 'index=1'), ('last_move=9', 'last_move=8'),
            ('last_move=10', 'last_move=169'), ('mode=1', 'mode=256'),
            ('move=1 render=0', 'move=4294967296 render=0'),
            ('target=10000', 'target=10001'), ('caller=100000', 'caller=0'),
        ):
            native = [line.replace(old, new) if 'X4 UI LIVE_POSE' in line else line for line in self.native]
            self.assertNotEqual(native, self.native, old)
            with self.subTest(old=old):
                self.assertFalse(self.parse(native)['pass'])
        unavailable = [line.replace('before_valid=1 after_valid=1', 'before_valid=0 after_valid=0')
                       if 'stage=sample' in line else line for line in self.native]
        self.assertTrue(self.parse(unavailable)['pass'])
        changed = [line.replace('before=' + '0'*128, 'before=' + '01' + '0'*126)
                   if 'stage=sample' in line else line for line in unavailable]
        self.assertFalse(self.parse(changed)['pass'])
        # Time is raw source data; NaN bits do not become an invented finiteness requirement.
        native = [line.replace('time=401ccccccccccccd', 'time=7ff8000000000001') for line in self.native]
        self.assertTrue(self.parse(native)['pass'])

    def test_missing_duplicate_moved_and_reordered_receipts_fail(self):
        live = [line for line in self.native if 'X4 UI LIVE_POSE' in line]
        for line in live:
            self.assertFalse(self.parse([row for row in self.native if row != line])['pass'])
        self.assertFalse(self.parse(self.native + [live[0]])['pass'])
        samples = [line for line in live if 'stage=sample' in line]
        native = self.native.copy()
        first, second = (native.index(line) for line in samples)
        native[first], native[second] = native[second], native[first]
        self.assertFalse(self.parse(native)['pass'])
        stripped = [line for line in self.native if 'X4 UI LIVE_POSE' not in line]
        self.assertFalse(self.parse(live + stripped)['pass'])
        self.assertFalse(self.parse(stripped + live)['pass'])
        capped = self.make_native(35)
        capped = [line.replace('mode0=12', 'mode0=1').replace('mode_other=11', 'mode_other=22')
                  if 'stage=close' in line else line for line in capped]
        self.assertFalse(self.parse(capped)['pass'])

    def test_file_hashes_bind_all_accepted_input_proofs(self):
        import linux_ui_live_pose_observer_evidence as evidence
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            engine = profile / 'engine-output.log'
            stderr = profile / 'stderr'
            trace = profile / 'controller-lifecycle.log'
            engine.write_text(''.join(self.fixture.fixture.engine))
            stderr.write_text(''.join(self.native))
            trace.write_text(''.join(self.fixture.trace))
            hashes = {'engine_sha256': live.sha(engine), 'stderr_sha256': live.sha(stderr),
                      'native_fd_sha256': live.sha(trace)}
            overlay = {**self.fixture.overlay, **hashes}
            fork = {**self.fixture.fork, **hashes}
            query = {**self.fixture.parse(), **hashes}
            result = evidence.collect_ui_live_pose_observer_evidence(profile, stderr, 2, True, overlay, fork, query)
            self.assertTrue(result['pass'], result['errors'])
            for path in (engine, stderr, trace):
                original = path.read_text()
                path.write_text(original + 'later bytes\n')
                result = evidence.collect_ui_live_pose_observer_evidence(profile, stderr, 2, True, overlay, fork, query)
                self.assertFalse(result['pass'])
                path.write_text(original)
