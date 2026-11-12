"""Camera refill source-mode receipts only; no game or rendered-view claim."""
import copy
import hashlib
from pathlib import Path
import re
import tempfile
import unittest

import linux_ui_camera_refill_evidence as camera
from linux_ui_render_lease_evidence import parse_ui_render_lease_evidence
from linux_ui_live_pose_observer_evidence import parse_ui_live_pose_observer_evidence
from linux_ui_pose_scope_evidence import parse_ui_pose_scope_evidence
from linux_ui_seat_correction_evidence import parse_ui_seat_correction_evidence
import test_linux_ui_render_lease_evidence as fixtures


def add_camera_returns(native, first, last):
    """Add complete stock-return records and their documented lease counters."""
    camera_count = last - first
    render_close = next(row for row in native if row.startswith('X4 UI GAME_RENDER v=1 stage=close '))
    rows = [f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={first} tail=1 event=enter\n']
    camera_rows = []
    for row in native:
        if row.startswith('X4 UI OVERLAY FRAME '):
            tick = int(re.search(r'tick=(\d+)', row)[1])
            if tick > first:
                rows.extend([
                    f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick-1} tail=1 event=release\n',
                    f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n'])
        if row == render_close:
            continue
        if row.startswith('X4 UI POSE_SCOPE v=1 stage=close '):
            for key in ('opens', 'closes'):
                row = re.sub(r'\b' + key + r'=(\d+)',
                             lambda match: key + '=' + str(int(match[1]) + camera_count), row)
        if row.startswith('X4 UI SEAT_CORRECTION v=1 stage=close '):
            for key in ('applies', 'restores'):
                row = re.sub(r'\b' + key + r'=(\d+)',
                             lambda match: key + '=' + str(int(match[1]) + camera_count), row)
        if row.startswith('X4 UI OVERLAY RESULT '):
            rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={last} tail=1 event=release\n')
        rows.append(row)
        if row.startswith('X4 UI GAME_RENDER v=1 stage=frame '):
            tick = int(re.search(r'tick=(\d+)', row)[1])
            if tick == last:
                rows.append(render_close)
            if tick > first:
                ordinal = tick - first
                camera_bank, actor_bank = ordinal % 2, (ordinal + 1) % 2
                output = 0x50000 + 0x20 + camera_bank * 0x2c0
                slot = 0x110000 + 0x108 + actor_bank * 0x80
                original = 0x400000 + actor_bank * 0x100
                sample = (f'X4 UI CAMERA_REFILL v=1 stage=frame tick={tick} ordinal={ordinal} '
                          f'camera=50000 output={output:x} camera_bank={camera_bank} actor_bank={actor_bank} '
                          f'slot={slot:x} original={original:x} observed={original:x} restored=1\n')
                rows.append(sample)
                camera_rows.append(sample)
        if row.startswith('X4 UI SEAT_CORRECTION v=1 stage=close '):
            rows.append(f'X4 UI CAMERA_REFILL v=1 stage=close tick={last} count={camera_count} restored=1\n')
    return rows, camera_rows


class CameraRefillEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.RenderLeaseEvidenceTests()
        self.fixture.setUp()
        fixture = self.fixture
        self.native, self.camera_rows = add_camera_returns(fixture.native, 9, 168)
        self.render = parse_ui_render_lease_evidence(
            self.native, 2, True, fixture.overlay, fixture.fork, fixture.query, fixture.callback, True)
        self.assertTrue(self.render['pass'], self.render['errors'])

    def parse(self, rows=None, slot=2, enabled=True):
        fixture = self.fixture
        return camera.parse_camera_refill_evidence(
            self.native if rows is None else rows, slot, enabled,
            fixture.overlay, fixture.fork, fixture.query, self.render)

    def test_all159_returns_and_extra_scope_pairs_close_without_visual_claim(self):
        fixture = self.fixture
        passive = parse_ui_live_pose_observer_evidence(
            self.native, 2, True, fixture.overlay, fixture.fork, fixture.query, self.render)
        scope = parse_ui_pose_scope_evidence(
            self.native, 2, True, passive, fixture.overlay, self.render)
        correction = parse_ui_seat_correction_evidence(
            self.native, 2, True, fixture.overlay, fixture.fork, fixture.query, passive, scope, self.render)
        proof = self.parse()
        for item in (passive, scope, correction, proof):
            self.assertTrue(item['pass'], item['errors'][:5])
        self.assertEqual(scope['counts']['closes'], 479)
        self.assertEqual(correction['counts']['applies'], 639)
        self.assertEqual(proof['refill_count'], 159)
        self.assertTrue(proof['camera_identity_correlated'])
        self.assertFalse(proof['camera_output_restored'] or proof['camera_pose_bytes_verified'])
        self.assertFalse(proof['rendered_view_proven'] or proof['safe_resume_proven'])
        self.assertFalse(parse_ui_render_lease_evidence(
            self.native, 2, True, fixture.overlay, fixture.fork, fixture.query, fixture.callback)['pass'])

    def test_missing_duplicate_reordered_partial_and_disabled_receipts_fail(self):
        first = self.camera_rows[0]
        variants = [self.native + [first], [row for row in self.native if row != first],
                    self.native[:-3], [first] + [row for row in self.native if row != first]]
        for rows in variants:
            self.assertFalse(self.parse(rows)['pass'])
        partial = self.parse(self.camera_rows[:1])
        self.assertEqual(partial['raw_native_receipts'], self.camera_rows[:1])
        self.assertFalse(partial['pass'])
        self.assertFalse(self.parse(slot=1)['pass'])
        self.assertFalse(self.parse(enabled=False)['pass'])
        self.assertTrue(self.parse(rows=[], slot=1)['pass'])

    def test_bank_addresses_restored_pointer_camera_identity_and_worker_boundary(self):
        first = self.camera_rows[0]
        for old, new in (('ordinal=1 ', 'ordinal=2 '), ('camera_bank=1', 'camera_bank=0'),
                         ('actor_bank=0', 'actor_bank=1'), ('observed=400000', 'observed=400010'),
                         ('restored=1', 'restored=0'), ('tick=10 ', 'tick=11 ')):
            rows = [row.replace(old, new) if row == first else row for row in self.native]
            with self.subTest(old=old):
                self.assertFalse(self.parse(rows)['pass'])
        # A fully self-consistent different camera still disagrees with the
        # independently retained first-UI view-state candidates.
        rows = [row.replace('camera=50000', 'camera=60000').replace('output=502e0', 'output=602e0')
                .replace('output=50020', 'output=60020') if 'X4 UI CAMERA_REFILL' in row else row
                for row in self.native]
        self.assertFalse(self.parse(rows)['pass'])

    def test_collector_binds_all_three_input_hashes_and_retains_raw_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            stderr = profile / 'stderr.log'
            stderr.write_text(''.join(self.native))
            (profile / 'engine-output.log').write_text('engine')
            (profile / 'controller-lifecycle.log').write_text('trace')
            proofs = copy.deepcopy([self.fixture.overlay, self.fixture.fork, self.fixture.query, self.render])
            for key, path in (('stderr_sha256', stderr),
                              ('engine_sha256', profile / 'engine-output.log'),
                              ('native_fd_sha256', profile / 'controller-lifecycle.log')):
                for proof in proofs:
                    proof[key] = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertTrue(camera.collect_camera_refill_evidence(
                profile, stderr, 2, True, *proofs)['pass'])
            proofs[0]['native_fd_sha256'] = '0' * 64
            result = camera.collect_camera_refill_evidence(profile, stderr, 2, True, *proofs)
            self.assertFalse(result['pass'])
            self.assertEqual(len(result['raw_native_receipts']), 160)


class CameraRefillCommonSessionTests(unittest.TestCase):
    def test_common900_has889_refills_and_exact_added_scope_counters(self):
        from test_linux_t40_evidence import CommonPresentationEvidenceTests
        source = CommonPresentationEvidenceTests()
        source.setUp()
        underlying = source.fixture.fixture
        query = copy.deepcopy(underlying.query)
        query['raw_native_receipts'] = [row.replace('tick=9 ', 'tick=44 ')
                                        for row in query['raw_native_receipts']]
        rows, _ = add_camera_returns(source.native, 44, 933)
        render = parse_ui_render_lease_evidence(
            rows, 2, True, source.overlay, source.fork, query, underlying.callback, True)
        passive = parse_ui_live_pose_observer_evidence(
            rows, 2, True, source.overlay, source.fork, query, render)
        scope = parse_ui_pose_scope_evidence(rows, 2, True, passive, source.overlay, render)
        correction = parse_ui_seat_correction_evidence(
            rows, 2, True, source.overlay, source.fork, query, passive, scope, render)
        proof = camera.parse_camera_refill_evidence(
            rows, 2, True, source.overlay, source.fork, query, render)
        for result in (render, passive, scope, correction, proof):
            self.assertTrue(result['pass'], result['errors'][:5])
        self.assertEqual(proof['refill_count'], 889)
        self.assertEqual(proof['frames'][0]['tick'], 45)
        self.assertEqual(proof['frames'][-1]['tick'], 933)
        self.assertEqual(scope['counts']['closes'], 2669)
        self.assertEqual(correction['counts']['applies'], 3559)
