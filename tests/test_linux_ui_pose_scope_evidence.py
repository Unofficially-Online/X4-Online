"""Supplemental scope receipts identify existing calls, never extra probes."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest

import test_linux_ui_live_pose_observer_evidence as fixtures
import linux_gate_live as live
from linux_ui_pose_scope_evidence import (
    collect_ui_pose_scope_evidence, parse_ui_pose_scope_evidence,
)


class PoseScopeTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.LivePoseEvidenceTests()
        self.fixture.setUp()
        self.native = self.fixture.make_native(40)
        self.legacy = self.fixture.parse(self.native)
        self.assertTrue(self.legacy['pass'], self.legacy['errors'])
        self.overlay = self.fixture.fixture.overlay
        self.rows = []
        for scope, ordinals in (('outside', (1, 2)), ('open', (35, 36)),
                                ('paused', (37, 38)), ('changing', (39, 40))):
            for ordinal in ordinals:
                move, render, mode = ((1, 0, 0), (0, 1, 1), (0, 0, 2))[(ordinal - 1) % 3]
                self.rows.append(
                    f'X4 UI POSE_SCOPE v=1 stage=sample scope={scope} ordinal={ordinal} '
                    f'last_move={ordinal+8} caller=100000 time=401ccccccccccccd mode={mode} '
                    f'move={move} render={render} before_valid=1 after_valid=1 '
                    'before=' + '0'*128 + ' after=' + '0'*128 + '\n')
        frames = self.overlay['frames']
        self.rows.append(
            'X4 UI POSE_SCOPE v=1 stage=close tick=168 outside=34 open=2 paused=2 changing=2 '
            f'opens={sum(frame["applies"] for frame in frames)} '
            f'pauses={sum(frame["updates"] for frame in frames)} closes=160 state=outside\n')
        self.native = self.insert(self.native, self.rows)

    def insert(self, native, rows):
        index = next(i for i, line in enumerate(native) if line.startswith('X4 UI LIVE_POSE v=1 stage=close')) + 1
        return native[:index] + rows + native[index:]

    def parse(self, native=None, legacy=None, overlay=None, slot=2, enabled=True):
        return parse_ui_pose_scope_evidence(
            self.native if native is None else native, slot, enabled,
            self.legacy if legacy is None else legacy,
            self.overlay if overlay is None else overlay)

    def test_four_scopes_represent_existing_calls_and_bind_transitions(self):
        result = self.parse()
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual(result['overlapping_legacy_samples'], 2)
        self.assertEqual(result['additional_unique_samples'], 6)
        self.assertFalse(result['scope_coverage_complete'])
        self.assertFalse(result['readiness_proven'])
        self.assertFalse(result['safe_resume_proven'])
        self.assertFalse(result['reader_exclusion_proven'])
        overlay = copy.deepcopy(self.overlay)
        overlay['frames'][0]['updates'] += 1
        self.assertFalse(self.parse(overlay=overlay)['pass'])

    def test_missing_duplicate_reordered_and_transaction_scope_are_strict(self):
        variants = [
            self.native + [self.rows[-1]],
            [line for line in self.native if line != self.rows[0]],
            [line.replace('changing=2', 'changing=1') for line in self.native],
            [line.replace('state=outside', 'state=open') for line in self.native],
            [line.replace('scope=changing', 'scope=unknown') for line in self.native],
            [line.replace('closes=160', 'closes=159') for line in self.native],
            [line.replace('ordinal=35 ', 'ordinal=1 ') for line in self.native],
            [line.replace('outside=34 open=2', 'outside=2 open=34') for line in self.native],
        ]
        reordered = list(self.native)
        first = reordered.index(self.rows[0])
        reordered[first], reordered[first+2] = reordered[first+2], reordered[first]
        variants.append(reordered)
        for native in variants:
            with self.subTest(receipts=native[-1:]):
                self.assertFalse(self.parse(native)['pass'])

    def test_overlap_bytes_bounds_and_causal_placement_cannot_be_forged(self):
        variants = [
            [line.replace('before_valid=1', 'before_valid=0') if line == self.rows[0] else line for line in self.native],
            [line.replace('ordinal=40 ', 'ordinal=41 ') for line in self.native],
            [line.replace('mode=0 ', 'mode=256 ') if line == self.rows[0] else line for line in self.native],
            [line.replace('last_move=43 ', 'last_move=8 ') for line in self.native],
            [line.replace('last_move=43 ', 'last_move=9 ') for line in self.native],
        ]
        without = [line for line in self.native if line not in self.rows]
        variants.extend((self.rows + without, without + self.rows))
        for native in variants:
            with self.subTest(native=native[:1]):
                self.assertFalse(self.parse(native)['pass'])

    def test_zero_calls_and_disabled_host_remain_explicit(self):
        native = self.fixture.make_native(0)
        legacy = self.fixture.parse(native)
        terminal = self.rows[-1].replace('outside=34 open=2 paused=2 changing=2',
                                         'outside=0 open=0 paused=0 changing=0')
        result = self.parse(self.insert(native, [terminal]), legacy=legacy)
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['scope_coverage_complete'])
        self.assertTrue(result['all_call_bytes_retained'])
        self.assertTrue(self.parse([], slot=1)['pass'])
        self.assertFalse(self.parse(slot=1)['pass'])
        self.assertFalse(self.parse(enabled=False)['pass'])

    def test_unavailable_zero_filled_bytes_preserve_only_ordinal_coverage(self):
        native = self.fixture.make_native(1)
        native = [line.replace('before_valid=1 after_valid=1',
                               'before_valid=0 after_valid=0') for line in native]
        legacy = self.fixture.parse(native)
        self.assertTrue(legacy['pass'], legacy['errors'])
        sample = self.rows[0].replace('before_valid=1 after_valid=1',
                                      'before_valid=0 after_valid=0')
        terminal = self.rows[-1].replace('outside=34 open=2 paused=2 changing=2',
                                         'outside=1 open=0 paused=0 changing=0')
        result = self.parse(self.insert(native, [sample, terminal]), legacy=legacy)
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['all_call_ordinals_retained'])
        self.assertFalse(result['all_call_bytes_retained'])
        self.assertTrue(result['scope_coverage_complete'])

    def test_collector_binds_all_three_raw_inputs_to_both_proofs(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            stderr = profile / 'stderr.log'
            paths = {'engine_sha256': profile/'engine-output.log',
                     'native_fd_sha256': profile/'controller-lifecycle.log',
                     'stderr_sha256': stderr}
            for key, path in paths.items():
                path.write_text(''.join(self.native) if path == stderr else key)
            legacy = copy.deepcopy(self.legacy)
            overlay = copy.deepcopy(self.overlay)
            for key, path in paths.items():
                for proof in (legacy, overlay):
                    proof[key] = hashlib.sha256(path.read_bytes()).hexdigest()
            result = collect_ui_pose_scope_evidence(profile, stderr, 2, True, legacy, overlay)
            self.assertTrue(result['pass'], result['errors'])
            for key in paths:
                forged = dict(legacy, **{key: '0'*64})
                self.assertFalse(collect_ui_pose_scope_evidence(
                    profile, stderr, 2, True, forged, overlay)['pass'])


class PoseScopeContractTests(unittest.TestCase):
    def test_new_frozen_contract_requires_exact_integer_two(self):
        options = fixtures.LivePoseAdmissionTests().options()
        for version in (None, True, 1, '2', 3):
            with self.subTest(version=version), self.assertRaises(SystemExit):
                live.validate_live_pose_evidence_contract(dict(
                    options, player_ui_live_pose_observer_evidence_version=version))
        self.assertEqual(live.validate_live_pose_evidence_contract(dict(
            options, player_ui_live_pose_observer_evidence_version=2)), 2)
        self.assertIsNone(live.validate_live_pose_evidence_contract({}))
        with self.assertRaises(SystemExit):
            live.validate_live_pose_evidence_contract({'player_ui_live_pose_observer_evidence_version': 2})
        self.assertIn('tools/dev/linux_ui_pose_scope_evidence.py', live.LIVE_POSE_SOURCES)
