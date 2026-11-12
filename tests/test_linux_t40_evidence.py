"""Synthetic common-startup evidence: Take and pre-apply never mean Update."""
import re
import unittest

import linux_gate_live as live
from linux_player_network_evidence import parse_network_physics_evidence
from linux_ui_session_budget import session_budget
from test_linux_network_long_window import neutral_network, record

P = 'X4LAN LIFECYCLE_DISCARD '


def common_trace(count=180):
    source, _, _ = neutral_network(count + 1)
    blocks, block = [], []
    for row in source[:-1]:
        block.append(row)
        if row.startswith(P + 'physics=frame ') and 'event=end' in row:
            blocks.append(block)
            block = []
    first = blocks[0]
    taken = next(i for i, row in enumerate(first) if row.startswith(P + 'snapshot=taken '))
    canonical = next(i for i, row in enumerate(first) if row.startswith(P + 'snapshot=physics_canonical '))
    take = first[:taken + 1] + [P + 'physics=take_no_update version=1\n'] + first[canonical:]
    take = [re.sub(r'\b(tick|generation)=5\b', r'\g<1>=37', row) for row in take]
    rows = list(take)
    repeated = blocks[1]
    before = next(i for i, row in enumerate(repeated)
                  if row.startswith(P + 'physics=sample stage=before '))
    ready = next(i for i, row in enumerate(repeated) if row.startswith(P + 'physics=ready '))
    for tick in (38, 39):
        pre = ([f'{P}physics=frame tick={tick} event=begin\n'] +
               repeated[before:ready] +
               [f'{P}physics=preapply generation={tick} update=0\n'] +
               repeated[ready + 1:])
        rows.extend(re.sub(r'\btick=6\b', f'tick={tick}', row) for row in pre)
    for old_tick, block in enumerate(blocks[1:], 6):
        tick = old_tick + 34
        for row in block:
            row = re.sub(r'\b(tick|generation)=' + str(old_tick) + r'\b',
                         lambda match: match[1] + '=' + str(tick), row)
            if 'physics=network ' in row:
                row = row.replace(record(old_tick - 2), record(tick - 2))
            rows.append(row)
    rows.append(source[-1])
    stderr = []
    for tick in range(37, count + 40):
        stderr.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n')
        if tick != count + 39:
            stderr.extend([
                f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release\n',
                f'X4LAN PLAYER_CONTROL_RECURRING tick={tick} completed=1 readiness_only=0 '
                'bank_writes=1 context_writes=unverified reference_calls=1 other_threads_excluded=0 tail=1\n'])
    stderr.extend(f'X4LAN KEYBOARD_SLOT1 tick={tick} capture={tick-2} saved=0000000000000011 '
                  f'record={record(tick-2)} held_before=0 held_after=0\n'
                  for tick in range(40, count + 40))
    engine = [f'LINUX_KEYBOARD_CAPTURE tick={tick} record={record(tick)}\n'
              for tick in range(38, count + 38)]
    return rows, stderr, engine


class CommonStartupEvidenceTests(unittest.TestCase):
    def parse(self, rows, stderr, engine):
        return parse_network_physics_evidence(
            rows, stderr, engine, 223, frame_count=180, slot=2,
            session_frames=180, startup='v1-t40')

    def test_separate_take_preapply_and180_real_updates(self):
        rows, stderr, engine = common_trace()
        result = self.parse(rows, stderr, engine)
        self.assertTrue(result['pass'], result['errors'][:8])
        self.assertEqual([frame['tick'] for frame in result['preapply_frames']], [37, 38, 39])
        self.assertTrue(all(frame['update'] is None for frame in result['preapply_frames']))
        self.assertEqual([frame['tick'] for frame in result['frames']], list(range(40, 220)))
        self.assertFalse(parse_network_physics_evidence(
            rows, stderr, engine, 223, frame_count=180, slot=2, session_frames=180)['pass'])

    def test_missing_duplicate_or_update_before_apply_is_refused(self):
        rows, stderr, engine = common_trace()
        marker = P + 'physics=take_no_update version=1\n'
        for changed in ([row for row in rows if row != marker],
                        rows + [marker],
                        [row.replace('generation=38 update=0', 'generation=38 update=1') for row in rows]):
            self.assertFalse(self.parse(changed, stderr, engine)['pass'])

    def test_900_update_boundary_and_fatal_phase_receipt(self):
        rows, stderr, engine = common_trace(900)
        proof = parse_network_physics_evidence(
            rows, stderr, engine, 223, frame_count=900, slot=2,
            session_frames=900, startup='v1-t40')
        self.assertTrue(proof['pass'], proof['errors'][:5])
        self.assertEqual(len(proof['frames']), 900)
        self.assertEqual(proof['frames'][-1]['tick'], 939)
        poisoned = engine + ['X4LAN PLAYER_PHASES_FATAL reason=invalid_config\n']
        self.assertFalse(parse_network_physics_evidence(
            rows, stderr, poisoned, 223, frame_count=900, slot=2,
            session_frames=900, startup='v1-t40')['pass'])

    def test_budget_matches_common_tuple_and_legacy_stays_separate(self):
        budget = session_budget(900, 'v1-t40')
        self.assertEqual((budget['last_capture'], budget['recurring_end'], budget['last'],
                          budget['report_tick']), (937, 939, 933, 934))
        self.assertEqual(live.physics_window_end(
            True, 900, 939, 940, network=True, session_frames=900, startup='v1-t40'), 939)


class CommonPresentationEvidenceTests(unittest.TestCase):
    def setUp(self):
        from test_linux_ui_session_runner import SessionPresentationTests
        fixture = SessionPresentationTests()
        fixture.setUp()
        self.fixture = fixture

        def shifted(row):
            fields = 'tick|last_move|first|last' if 'OVERLAY' in row else 'tick|last_move'
            return re.sub(r'\b(' + fields + r')=([0-9]+)',
                          lambda match: f'{match[1]}={int(match[2]) + 35}', row)
        self.native = [shifted(row) for row in fixture.native]
        self.engine = [shifted(row) for row in fixture.engine]
        self.trace = [shifted(row) for row in fixture.query_fixture.trace]
        self.fork = live.parse_presentation_fork_evidence(
            self.engine, self.native, self.trace, 2, 'v1-t40')
        self.assertTrue(self.fork['pass'], self.fork['errors'])
        self.overlay = live.parse_ui_overlay_evidence(
            self.engine, self.native, self.trace, 2, True, self.fork,
            callbacks=890, session_frames=900, startup='v1-t40')
        self.assertTrue(self.overlay['pass'], self.overlay['errors'])

    def test_full_shifted_render_and_observer_epoch_stays_causally_bound(self):
        from linux_ui_render_lease_evidence import parse_ui_render_lease_evidence
        from linux_ui_live_pose_observer_evidence import parse_ui_live_pose_observer_evidence
        from linux_ui_pose_scope_evidence import parse_ui_pose_scope_evidence
        from linux_ui_seat_correction_evidence import parse_ui_seat_correction_evidence
        from linux_ui_seat_pose_evidence import parse_ui_seat_pose_evidence
        from linux_ui_seat_query_evidence import parse_ui_seat_query_evidence
        fixture = self.fixture.fixture
        # The correction fixture deliberately uses a nonidentity, independently
        # admitted query to exercise delta math; retain that synthetic binding.
        query = dict(fixture.query)
        query['raw_native_receipts'] = [re.sub(r'\btick=9\b', 'tick=44', row)
                                        for row in query['raw_native_receipts']]
        render = parse_ui_render_lease_evidence(
            self.native, 2, True, self.overlay, self.fork, query, fixture.callback)
        passive = parse_ui_live_pose_observer_evidence(
            self.native, 2, True, self.overlay, self.fork, query, render)
        scope = parse_ui_pose_scope_evidence(
            self.native, 2, True, passive, self.overlay, render)
        correction = parse_ui_seat_correction_evidence(
            self.native, 2, True, self.overlay, self.fork, query, passive, scope, render)
        seats = parse_ui_seat_pose_evidence(
            self.native, self.trace, 2, True, self.overlay, self.fork)
        returned = parse_ui_seat_query_evidence(
            self.native, 2, True, self.overlay, self.fork, seats)
        for proof in (render, passive, scope, correction, seats, returned):
            self.assertTrue(proof['pass'], proof['errors'][:5])
        self.assertEqual(self.overlay['callback_tick'], 44)
        self.assertEqual(render['frames'][-1]['tick'], 933)
        self.assertFalse(live.parse_ui_overlay_evidence(
            self.engine, self.native, self.trace, 2, True, self.fork,
            callbacks=890, session_frames=900)['pass'])


class CommonStartupBundleTests(unittest.TestCase):
    def test_explicit_phase_config_source_freeze_and_capture_budget(self):
        import json
        from pathlib import Path
        from unittest import mock
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main

        def build(arguments):
            enabled = '--player-ui-seat-correction' in arguments
            if enabled:
                cleaned, skip = [], False
                for token in arguments:
                    if skip:
                        skip = False
                        continue
                    if token in ('--independent-keyboard-frames', '--player-ui-overlay-callbacks'):
                        skip = True
                        continue
                    if token != '--player-ui-overlay-probe':
                        cleaned.append(token)
                arguments = cleaned + ['--player-ui-session-frames', '900',
                                       '--player-startup-phases', 'v1-t40', '--ticks', '940']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            if enabled:
                self.assertEqual(manifest['player_startup_phases'], 'v1-t40')
                self.assertEqual(manifest['stock_keyboard_replay_last_capture'], 937)
                config = (output / 'extension/x4_lan_lab/ui/config.lua').read_text()
                self.assertIn("native_gate_linux_startup_phases = 'v1-t40'", config)
                live.validate_session_config(config, 900, 'v1-t40')
                self.assertIn('linux_player_startup_phases.py', manifest['files'])
                for source in live.SESSION_SOURCES:
                    self.assertIn('source-receipts/' + source, manifest['files'])
            return result

        with mock.patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()

    def test_lua_request_and_result_use_the_same_explicit_phase_version(self):
        from test_linux_ui_overlay_runner import UiOverlayLuaTests
        for slot in ('p1', 'p2'):
            fixture, lua = UiOverlayLuaTests().runtime(slot)
            lua.execute("""
                cfg.native_gate_linux_startup_phases = 'v1-t40'
                cfg.native_gate_linux_ui_session_frames = 900
                cfg.native_gate_linux_ui_overlay_callbacks = 890
                cfg.native_gate_stock_keyboard_replay_probe.last_capture = 937
                gate.stock_keyboard_replay_request = function(...)
                    request_args = {...}
                    return 1
                end
            """)
            fixture.start(lua)
            self.assertEqual(lua.globals().request_args[8], 1)
            for tick in range(2, 935):
                lua.eval(f'probe.frame({tick})')
            results = [row for row in lua.globals().logs.values()
                       if 'UI_OVERLAY_RESULT' in row]
            expected = [] if slot == 'p1' else [
                'LINUX_UI_OVERLAY_RESULT tick=934 slot=2 overlay fixture']
            self.assertEqual(results, expected)
            self.assertTrue(any('first=38 last=937 delay=2' in row
                                for row in lua.globals().logs.values()))
        for value in ('false', 'true', '"v2"'):
            fixture, lua = UiOverlayLuaTests().runtime('p2')
            lua.execute('cfg.native_gate_linux_startup_phases = ' + value)
            with self.subTest(value=value), self.assertRaises(Exception):
                fixture.create(lua)


class CommonSupplementalEvidenceTests(unittest.TestCase):
    def test_context_policy_init_binds_take37_and_all_later_snapshots(self):
        import hashlib
        from test_linux_player_context_evidence import fixture
        rows, stderr, engine = common_trace()
        legacy, _, _ = fixture(transitions=False)
        receipt = next(row for row in legacy if 'CONTEXT_POLICY v=1 event=init' in row)
        receipt = receipt.replace('tick=5 ', 'tick=37 ')
        payloads = bytes.fromhex(receipt.split('payloads=')[1].strip())
        selected = b''.join(payloads[i:i+16] for i in range(0, len(payloads), 16)
                            if int.from_bytes(payloads[i:i+4], 'little') in (8, 15, 16)).hex()
        suffix = (' context_policy_v=1 manager=110 epoch=1 full_hash=' +
                  hashlib.sha256(payloads).hexdigest() + '\n')
        transformed, active = [], False
        for row in rows:
            if row.startswith(P + 'snapshot=physics_canonical '):
                transformed.append(receipt)
                active = True
            if row.startswith(P + 'snapshot='):
                row = re.sub(r'contexts_payloads=[0-9a-f]{96}', 'contexts_payloads=' + selected, row)
                if active:
                    row = row.rstrip('\n') + suffix
            transformed.append(row)
        proof = CommonStartupEvidenceTests().parse(transformed, stderr, engine)
        self.assertTrue(proof['pass'], proof['errors'][:6])
        corrupted = [row.replace('event=init tick=37', 'event=init tick=40') for row in transformed]
        self.assertFalse(CommonStartupEvidenceTests().parse(corrupted, stderr, engine)['pass'])

    def test_callback_and_view_query_close_at_actual_common_end(self):
        from test_linux_ui_callback_evidence import UiCallbackEvidenceTest
        from test_linux_view_query_bundle import ViewQueryBundleTests
        fixture = UiCallbackEvidenceTest()
        fixture.setUp()
        shift = lambda row: re.sub(r'\btick=([0-9]+)',
                                   lambda match: f'tick={int(match[1]) + 35}', row)
        proof = live.parse_ui_callback_evidence(
            [shift(row) for row in fixture.engine], fixture.stderr,
            [shift(row) for row in fixture.presentation], 2, 180, 180, 'v1-t40')
        self.assertTrue(proof['pass'], proof['errors'])
        rows = ViewQueryBundleTests().evidence(frames=180, slot=2)
        rows = [shift(row) for row in rows]
        view = live.parse_view_query_evidence(rows, 2, 180, 180, 'v1-t40')
        self.assertTrue(view['pass'], view['errors'])
        self.assertFalse(live.parse_view_query_evidence(rows, 2, 180, 180)['pass'])
        with self.assertRaises(SystemExit):
            live.validate_session_config(
                "X4LAN_CONFIG.native_gate_linux_ui_session_frames = 180\n", 180, 'v1-t40')


class CommonEnvironmentTests(unittest.TestCase):
    def test_both_owned_peers_receive_exact_phase_and_session_environment(self):
        for slot in (1, 2):
            self.assertEqual(live.ui_overlay_environment(True, slot, 890, 900, 'v1-t40'), {
                'X4_DEV_UI_SESSION_FRAMES': '900', 'X4_DEV_PLAYER_PHASES': 'v1-t40'})
        self.assertEqual(live.ui_overlay_environment(True, 1, 160), {})
        with self.assertRaises(ValueError):
            live.ui_overlay_environment(True, 2, 160, 900, 'v1-t40')
