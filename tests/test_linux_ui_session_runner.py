"""Finite 900 admission, complete synthetic validation, then compact output."""
import copy
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
import linux_gate_live as live
from linux_ui_session_budget import session_budget, resolve_session_options
from linux_ui_session_evidence import compact_validated_result
from linux_player_network_evidence import parse_network_physics_evidence
import test_linux_network_long_window as network_fixture
import test_linux_ui_render_lease_evidence as render_fixture


class SessionAdmissionTests(unittest.TestCase):
    def options(self):
        options = render_fixture.RenderLeaseAdmissionTests().options()
        options.update(player_ui_session_frames=900, player_ui_overlay_probe=False,
                       player_ui_overlay_callbacks=1, independent_keyboard_frames=0,
                       stock_keyboard_replay_probe=True, player_factory_probe=True,
                       stock_keyboard_observer=True, serial_pathing=True)
        return options

    def test_session_is_explicit_and_old_modes_keep_their_limits(self):
        budget = session_budget(900)
        self.assertEqual((budget['last_capture'],budget['recurring_end'],budget['callbacks'],budget['last'],budget['report_tick']),
                         (902,904,890,898,899))
        for count in (0,179,901,18000,True,'900'):
            with self.subTest(count=count), self.assertRaises(ValueError):
                session_budget(count)
        options = resolve_session_options(self.options(),cli=True)
        self.assertEqual(live.validate_independent_keyboard(options,905),900)
        self.assertTrue(live.validate_ui_overlay(options,900))
        self.assertEqual(live.physics_window_end(True,900,904,905,network=True,session_frames=900),904)
        for changes in ({'player_ui_overlay_probe':True},{'player_ui_overlay_callbacks':160},
                        {'player_ui_render_lease':False},{'player_ui_seat_correction':False},
                        {'independent_keyboard_frames':899}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                resolve_session_options(dict(self.options(),**changes),cli=True)
        with self.assertRaises(SystemExit):
            live.physics_window_end(True,900,None,1000,network=True)
        self.assertEqual(live.ui_overlay_environment(True,1,890,900),{'X4_DEV_UI_SESSION_FRAMES':'900'})
        self.assertEqual(live.ui_overlay_environment(True,2,890,900),{'X4_DEV_UI_SESSION_FRAMES':'900'})

    def test_bundle_config_source_freeze_and_no_legacy_activation(self):
        original_main = live.main
        def build(arguments):
            enabled = '--player-ui-seat-correction' in arguments
            if enabled:
                cleaned=[]
                skip=False
                for token in arguments:
                    if skip:
                        skip=False
                        continue
                    if token in ('--independent-keyboard-frames','--player-ui-overlay-callbacks'):
                        skip=True
                        continue
                    if token=='--player-ui-overlay-probe':
                        continue
                    cleaned.append(token)
                arguments=cleaned+['--player-ui-session-frames','900','--ticks','905']
            result=original_main(arguments)
            output=Path(arguments[arguments.index('--out')+1])
            manifest=json.loads((output/'manifest.json').read_text())
            self.assertEqual(manifest['player_ui_session_frames'],900 if enabled else 0)
            if enabled:
                self.assertEqual(manifest['independent_keyboard_frames'],900)
                self.assertEqual(manifest['player_ui_overlay_callbacks'],890)
                self.assertEqual(manifest['stock_keyboard_replay_last_capture'],902)
                config=(output/'extension/x4_lan_lab/ui/config.lua').read_text()
                self.assertIn('native_gate_linux_ui_session_frames = 900',config)
                for name in live.SESSION_SOURCES:
                    receipt='source-receipts/'+name
                    self.assertEqual(manifest['files'][receipt],live.sha(output/receipt))
            return result
        with mock.patch.object(live,'main',side_effect=build):
            render_fixture.RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()


class SessionPhysicsTests(unittest.TestCase):
    def test_complete900_fixture_validates_before_compaction_and_retains_failures(self):
        rows,stderr,engine=network_fixture.neutral_network(900)
        proof=parse_network_physics_evidence(rows,stderr,engine,223,frame_count=900,slot=2,session_frames=900)
        self.assertTrue(proof['pass'],proof['errors'][:4])
        self.assertEqual(len(proof['frames']),900)
        expected_digest=__import__('linux_ui_session_evidence').sequence_digest(proof['frames'])
        result={'physics_discard':{'clients':{'2':proof}}}
        compact_validated_result(result)
        self.assertNotIn('frames',proof)
        self.assertEqual(proof['frame_summary']['sha256'],expected_digest['sha256'])
        self.assertEqual(proof['frame_summary']['count'],900)
        self.assertEqual(proof['slot_streams']['2']['count'],900)
        self.assertEqual(proof['last_frame']['tick'],904)
        self.assertLess(len(json.dumps(result)),200000)
        self.assertFalse(parse_network_physics_evidence(rows,stderr,engine,223,frame_count=900,slot=2)['pass'])
        held=network_fixture.record(902,[(2,1)])
        bad=[line.replace(network_fixture.record(902),held).replace('held_after=0','held_after=1')
             if 'KEYBOARD_SLOT1 tick=904 ' in line else line for line in stderr]
        failed=parse_network_physics_evidence(rows,bad,engine,223,frame_count=900,slot=2,session_frames=900)
        self.assertFalse(failed['pass'])
        self.assertIn('P1 final keys held',failed['errors'])
        compact_validated_result({'physics_discard':{'clients':{'2':failed}}})
        self.assertEqual(len(failed['frames']),900)
        self.assertIn('P1 final keys held',failed['errors'])


class SessionPresentationTests(unittest.TestCase):
    def setUp(self):
        fixture = render_fixture.RenderLeaseEvidenceTests()
        fixture.setUp()
        self.fixture = fixture
        self.query_fixture = fixture.base.fixture.fixture.fixture
        first = next(i for i, row in enumerate(fixture.native)
                     if row.startswith('X4 UI OVERLAY FRAME '))
        final_render = fixture.native.index(fixture.render_rows[-1])
        close = fixture.native.index(fixture.render['close_receipt'])
        deferred = fixture.native[final_render + 1:close]
        self.native = list(fixture.native[:first])
        template = next(row for row in fixture.overlay['raw_native_receipts']
                        if row.startswith('X4 UI OVERLAY FRAME '))
        for ordinal in range(1, 891):
            tick = ordinal + 8
            frame = re.sub(r'\btick=\d+', f'tick={tick}', template)
            frame = re.sub(r'\bordinal=\d+', f'ordinal={ordinal}', frame)
            frame = frame.replace('count=160', 'count=890')
            self.native.append(frame)
            render = fixture.render_rows[0]
            render = re.sub(r'\btick=\d+', f'tick={tick}', render)
            render = re.sub(r'\bordinal=\d+', f'ordinal={ordinal}', render)
            self.native.append(render)
        for row in deferred:
            row = row.replace('tick=168', 'tick=898')
            row = row.replace('opens=480', 'opens=2670')
            row = row.replace('pauses=160', 'pauses=890')
            row = row.replace('closes=320', 'closes=1780')
            row = row.replace('applies=480', 'applies=2670')
            row = row.replace('restores=480', 'restores=2670')
            self.native.append(row)
        self.native.append('X4 UI GAME_RENDER v=1 stage=close tick=898 count=890 restored=1\n')
        summary = 'v=3 status=pass first=9 last=898 count=890'
        self.native.append('X4 UI OVERLAY RESULT ' + summary + '\n')
        self.engine = [row.replace('tick=169', 'tick=899')
                       .replace('last=168 count=160', 'last=898 count=890')
                       for row in self.query_fixture.fixture.engine]
        self.overlay = live.parse_ui_overlay_evidence(
            self.engine, self.native, self.query_fixture.trace, 2, True,
            fixture.fork, callbacks=890, session_frames=900)
        self.assertTrue(self.overlay['pass'], self.overlay['errors'])

    def proofs(self, native):
        from linux_ui_render_lease_evidence import parse_ui_render_lease_evidence
        from linux_ui_live_pose_observer_evidence import parse_ui_live_pose_observer_evidence
        from linux_ui_pose_scope_evidence import parse_ui_pose_scope_evidence
        from linux_ui_seat_correction_evidence import parse_ui_seat_correction_evidence
        fixture = self.fixture
        render = parse_ui_render_lease_evidence(
            native, 2, True, self.overlay, fixture.fork, fixture.query, fixture.callback)
        passive = parse_ui_live_pose_observer_evidence(
            native, 2, True, self.overlay, fixture.fork, fixture.query, render)
        scope = parse_ui_pose_scope_evidence(native, 2, True, passive, self.overlay, render)
        correction = parse_ui_seat_correction_evidence(
            native, 2, True, self.overlay, fixture.fork, fixture.query, passive, scope, render)
        return render, passive, scope, correction

    def test_all890_leases_and_dynamic_final_closures_validate(self):
        render, passive, scope, correction = self.proofs(self.native)
        for proof in (render, passive, scope, correction):
            self.assertTrue(proof['pass'], proof['errors'][:5])
        self.assertEqual(render['lease_count'], 890)
        self.assertEqual(scope['counts']['closes'], 1780)
        self.assertEqual(correction['counts']['applies'], 2670)
        from linux_ui_seat_pose_evidence import parse_ui_seat_pose_evidence
        from linux_ui_seat_query_evidence import parse_ui_seat_query_evidence
        seats = parse_ui_seat_pose_evidence(
            self.native, self.query_fixture.trace, 2, True,
            self.overlay, self.fixture.fork)
        self.assertTrue(seats['pass'], seats['errors'])
        query = parse_ui_seat_query_evidence(
            self.native, 2, True, self.overlay, self.fixture.fork, seats)
        self.assertTrue(query['pass'], query['errors'])
        self.assertFalse(live.parse_ui_overlay_evidence(
            self.engine, self.native, self.query_fixture.trace, 2, True,
            self.fixture.fork, callbacks=890)['pass'])

    def test_missing_or_early_final_render_return_cannot_close_session(self):
        final = next(row for row in self.native
                     if row.startswith('X4 UI GAME_RENDER v=1 stage=frame tick=898 '))
        missing = [row for row in self.native if row != final]
        moved = [row for row in self.native if row != final]
        moved.insert(0, final)
        for rows in (missing, moved):
            with self.subTest(missing=rows is missing):
                self.assertFalse(self.proofs(rows)[0]['pass'])


class SessionResourceAndLuaTests(unittest.TestCase):
    def test_disk_limit_counts_only_owned_files_and_rejects_growth(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'raw.log').write_bytes(b'x' * 11)
            (root / 'alias').symlink_to(root / 'raw.log')
            live.enforce_session_disk_budget(root, 11)
            with self.assertRaises(RuntimeError):
                live.enforce_session_disk_budget(root, 10)
        self.assertFalse(live.parse_view_query_evidence([], 2, 900, True)['pass'])
        self.assertFalse(live.parse_ui_callback_evidence([], [], [], 2, 900, True)['pass'])

    def test_lua_uses_session_summary899_and_refuses_invalid_budget(self):
        import test_linux_ui_overlay_runner as overlay_fixture
        for slot in ('p1', 'p2'):
            fixture, lua = overlay_fixture.UiOverlayLuaTests().runtime(slot)
            lua.execute("""
                cfg.native_gate_linux_ui_session_frames = 900
                cfg.native_gate_linux_ui_overlay_callbacks = 890
                cfg.native_gate_stock_keyboard_replay_probe.last_capture = 902
            """)
            fixture.start(lua)
            for tick in range(2, 900):
                lua.eval(f'probe.frame({tick})')
            results = [row for row in lua.globals().logs.values()
                       if 'UI_OVERLAY_RESULT' in row]
            expected = [] if slot == 'p1' else [
                'LINUX_UI_OVERLAY_RESULT tick=899 slot=2 overlay fixture']
            self.assertEqual(results, expected)
        for value in ('false', '"900"', '179', '901'):
            fixture, lua = overlay_fixture.UiOverlayLuaTests().runtime('p2')
            lua.execute('cfg.native_gate_linux_ui_session_frames = ' + value)
            with self.subTest(value=value), self.assertRaises(Exception):
                fixture.create(lua)
