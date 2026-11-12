"""Default-off GameRender lease admission and synthetic evidence only."""
import unittest

import test_linux_ui_seat_correction_evidence as correction_fixture
import linux_gate_live as live


class RenderLeaseAdmissionTests(unittest.TestCase):
    def options(self):
        return dict(correction_fixture.SeatCorrectionAdmissionTests().options(),
                    player_ui_render_lease=True)

    def test_explicit_full_chain_and_bounded_window_are_required(self):
        self.assertFalse(live.validate_render_lease({}))
        self.assertTrue(live.validate_render_lease(self.options()))
        self.assertTrue(live.validate_ui_overlay(self.options(), 180))
        for value in (None, 0, 1, 'true'):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                live.validate_render_lease(dict(self.options(), player_ui_render_lease=value))
        for key in ('player_ui_seat_correction', 'player_ui_live_pose_observer',
                    'player_ui_seat_query_probe', 'player_ui_overlay_probe',
                    'player_presentation_fork_probe', 'player_view_query_observer',
                    'ui_callback_observer', 'player_lifetime_sha256'):
            with self.subTest(key=key), self.assertRaises(SystemExit):
                live.validate_ui_overlay(dict(self.options(), **{key: False}), 180)
        with self.assertRaises(SystemExit):
            live.validate_ui_overlay(self.options(), 164)

    def test_mocked_bundle_forwarding_config_helper_and_source_inventory(self):
        import json
        from pathlib import Path
        from unittest import mock
        original_main = live.main

        def build(arguments):
            enabled = '--player-ui-seat-correction' in arguments
            result = original_main(arguments + (['--player-ui-render-lease'] if enabled else []))
            output = Path(arguments[arguments.index('--out')+1])
            manifest = json.loads((output/'manifest.json').read_text())
            self.assertIs(manifest['player_ui_render_lease'], enabled)
            self.assertEqual(manifest['player_ui_render_lease_evidence_version'], 1 if enabled else None)
            live.validate_render_lease(manifest, frozen=True)
            live.validate_render_lease_config((output/'extension/x4_lan_lab/ui/config.lua').read_text(), enabled)
            helper = 'linux_ui_render_lease_evidence.py'
            self.assertEqual(manifest['files'][helper], live.sha(output/helper))
            if enabled:
                for source in live.RENDER_LEASE_SOURCES:
                    receipt = 'source-receipts/'+source
                    self.assertEqual(manifest['files'][receipt], live.sha(output/receipt))
            return result

        with mock.patch.object(live, 'main', side_effect=build):
            correction_fixture.SeatCorrectionAdmissionTests().test_mocked_bundle_forwards_option_and_freezes_manifest_config_collector()

    def test_frozen_version_exact_config_and_guest_only_environment(self):
        options = dict(self.options(), player_ui_seat_correction_evidence_version=1)
        for version in (None, True, '1', 2):
            with self.subTest(version=version), self.assertRaises(SystemExit):
                live.validate_render_lease(dict(options, player_ui_render_lease_evidence_version=version), frozen=True)
        self.assertTrue(live.validate_render_lease(dict(
            options, player_ui_render_lease_evidence_version=1), frozen=True))
        config = 'X4LAN_CONFIG.native_gate_linux_ui_render_lease = true\n'
        live.validate_render_lease_config(config, True)
        live.validate_render_lease_config('', False)
        for text, enabled in (('', True), (config, False), (config+config, True),
                              (config.replace('true', '1'), True)):
            with self.subTest(text=text), self.assertRaises(SystemExit):
                live.validate_render_lease_config(text, enabled)
        self.assertEqual(live.render_lease_environment(True, 2), {'X4_DEV_UI_RENDER_LEASE': '1'})
        self.assertEqual(live.render_lease_environment(True, 1), {})
        self.assertEqual(live.render_lease_environment(False, 2), {})


class RenderLeaseEvidenceTests(unittest.TestCase):
    def setUp(self):
        import re
        import linux_ui_render_lease_evidence as evidence
        import linux_ui_live_pose_observer_evidence as passive
        import linux_ui_pose_scope_evidence as scopes
        import linux_ui_seat_correction_evidence as corrections

        self.base = correction_fixture.SeatCorrectionTests()
        self.base.setUp()
        self.callback = {'pass': True, 'owner': 1234}
        self.overlay, self.fork, self.query = self.base.overlay, self.base.fork, self.base.query
        self.render_rows = []
        deferred = []
        self.native = []
        for line in self.base.native:
            if line.startswith('X4 UI LIVE_POSE v=1 stage=close '):
                for key, value in {'move': 16, 'render': 12, 'other': 12,
                                   'mode0': 14, 'mode1': 14, 'mode_other': 12}.items():
                    line = re.sub(r'\b'+key+r'=\d+', key+'='+str(value), line)
            if line.startswith('X4 UI POSE_SCOPE v=1 stage=sample scope=open '):
                sample = next(sample for sample in self.base.scope['representatives']
                              if sample['scope'] == 'open' and f'ordinal={sample["ordinal"]} ' in line)
                line = (f'X4 UI POSE_SCOPE v=1 stage=sample scope=open ordinal={sample["ordinal"]} '
                        f'last_move={sample["last_move"]} caller=1314317 time={sample["time_bits"]} '
                        f'mode=1 move=1 render=0 before_valid=1 after_valid=1 before={sample["before"]} after={sample["after"]}\n')
            if line.startswith('X4 UI POSE_SCOPE v=1 stage=close '):
                line = line.replace('opens=320', 'opens=480').replace('closes=160', 'closes=320')
            if line.startswith('X4 UI SEAT_CORRECTION v=1 stage=close '):
                line = line.replace('applies=320', 'applies=480').replace('restores=320', 'restores=480')
            if line.startswith(('X4 UI LIVE_POSE v=1 stage=sample ', 'X4 UI LIVE_POSE v=1 stage=close ',
                                'X4 UI POSE_SCOPE ', 'X4 UI SEAT_CORRECTION v=1 stage=sample ',
                                'X4 UI SEAT_CORRECTION v=1 stage=close ')):
                deferred.append(line)
                continue
            self.native.append(line)
            if line.startswith('X4 UI OVERLAY FRAME '):
                tick = int(re.search(r'tick=(\d+)', line)[1])
                ordinal = tick-8
                bank = ordinal % 2
                slot = 0x110000+0x108+bank*0x80
                original = 0x300000+bank*0x100
                row = (f'X4 UI GAME_RENDER v=1 stage=frame tick={tick} ordinal={ordinal} tid=1234 '
                       f'object=200000 p1=110000 p2=120000 bank={bank} slot={slot:x} '
                       f'original={original:x} observed={original:x} restored=1\n')
                self.native.append(row)
                self.render_rows.append(row)
                if tick == 168:
                    self.native.extend(deferred)
                    self.native.append('X4 UI GAME_RENDER v=1 stage=close tick=168 count=160 restored=1\n')
        self.render = evidence.parse_ui_render_lease_evidence(
            self.native, 2, True, self.overlay, self.fork, self.query, self.callback)
        self.assertTrue(self.render['pass'], self.render['errors'])
        self.passive = passive.parse_ui_live_pose_observer_evidence(
            self.native, 2, True, self.overlay, self.fork, self.query, self.render)
        self.assertTrue(self.passive['pass'], self.passive['errors'])
        self.scope = scopes.parse_ui_pose_scope_evidence(
            self.native, 2, True, self.passive, self.overlay, self.render)
        self.assertTrue(self.scope['pass'], self.scope['errors'])
        self.correction = corrections.parse_ui_seat_correction_evidence(
            self.native, 2, True, self.overlay, self.fork, self.query, self.passive, self.scope, self.render)
        self.assertTrue(self.correction['pass'], self.correction['errors'])

    def parse(self, rows=None, **changes):
        from linux_ui_render_lease_evidence import parse_ui_render_lease_evidence
        proofs = {'overlay': self.overlay, 'fork': self.fork, 'query': self.query, 'callback': self.callback}
        proofs.update(changes)
        return parse_ui_render_lease_evidence(self.native if rows is None else rows, 2, True, **proofs)

    def test_render_mode_closes_after_actual_return_with_exact_added_counts(self):
        self.assertEqual(self.render['lease_count'], 160)
        self.assertEqual(self.scope['counts']['closes'], 320)
        self.assertEqual(self.scope['counts']['opens'], 480)
        self.assertEqual(self.correction['counts']['applies'], 480)
        for key in ('rendered_view_proven','readiness_proven','safe_resume_proven','reader_exclusion_proven'):
            self.assertFalse(self.render[key])
        for name in ('overlay', 'fork', 'query', 'callback'):
            self.assertFalse(self.parse(**{name: dict(getattr(self,name), **{'pass':False})})['pass'])

    def test_missing_duplicate_reordered_or_pre_ui_return_is_rejected(self):
        first = self.render_rows[0]
        variants = [[line for line in self.native if line != first], self.native+[first],
                    [line for line in self.native if line != self.render['close_receipt']], [first]+[line for line in self.native if line != first]]
        swapped = list(self.native)
        left, right = swapped.index(self.render_rows[0]), swapped.index(self.render_rows[1])
        swapped[left], swapped[right] = swapped[right], swapped[left]
        variants.append(swapped)
        for rows in variants:
            self.assertFalse(self.parse(rows)['pass'])

    def test_owner_actor_bank_object_and_actual_restoration_are_bound(self):
        for old,new in (('tid=1234','tid=1235'),('object=200000','object=200008'),
                        ('p1=110000','p1=110008'),('p2=120000','p2=120008'),
                        ('slot=110188','slot=110108'),('observed=300100','observed=300110'),
                        ('restored=1','restored=0'),('ordinal=1 ','ordinal=2 '),
                        ('tick=9 ','tick=8 '),('original=300100','original=300101')):
            rows = [line.replace(old,new) if line == self.render_rows[0] else line for line in self.native]
            with self.subTest(old=old):
                self.assertFalse(self.parse(rows)['pass'])

    def test_render_correction_retains_source_allowed_ui_depths_without_attribution(self):
        import re
        import linux_ui_live_pose_observer_evidence as passive
        import linux_ui_pose_scope_evidence as scopes
        import linux_ui_seat_correction_evidence as corrections
        for depth in (0, 1):
            native = []
            for line in self.native:
                if line.startswith('X4 UI POSE_SCOPE v=1 stage=sample scope=open ordinal=35 '):
                    line = line.replace('move=1 render=0', f'move=0 render={depth}')
                if line.startswith('X4 UI LIVE_POSE v=1 stage=close '):
                    line = re.sub(r'\bmove=16', 'move=15', line)
                    line = re.sub(r'\b'+('render' if depth else 'other')+'=12',
                                  ('render' if depth else 'other')+'=13', line)
                native.append(line)
            legacy = passive.parse_ui_live_pose_observer_evidence(
                native, 2, True, self.overlay, self.fork, self.query, self.render)
            self.assertTrue(legacy['pass'], legacy['errors'])
            scope = scopes.parse_ui_pose_scope_evidence(native, 2, True, legacy, self.overlay, self.render)
            self.assertTrue(scope['pass'], scope['errors'])
            result = corrections.parse_ui_seat_correction_evidence(
                native, 2, True, self.overlay, self.fork, self.query, legacy, scope, self.render)
            self.assertTrue(result['pass'], result['errors'])

    def test_wrong_final_observer_boundary_and_old_mode_are_rejected(self):
        import linux_ui_live_pose_observer_evidence as passive
        import linux_ui_pose_scope_evidence as scopes
        import linux_ui_seat_correction_evidence as corrections
        self.assertFalse(passive.parse_ui_live_pose_observer_evidence(
            self.native,2,True,self.overlay,self.fork,self.query)['pass'])
        self.assertFalse(scopes.parse_ui_pose_scope_evidence(
            self.native,2,True,self.passive,self.overlay)['pass'])
        self.assertFalse(corrections.parse_ui_seat_correction_evidence(
            self.native,2,True,self.overlay,self.fork,self.query,self.passive,self.scope)['pass'])
        close = self.render['close_receipt']
        rows = [line for line in self.native if line != close]
        index = rows.index(self.render_rows[-1])+1
        rows.insert(index, close)
        self.assertFalse(self.parse(rows)['pass'])

    def test_disabled_host_and_partial_failure_preserve_raw(self):
        from linux_ui_render_lease_evidence import parse_ui_render_lease_evidence
        for slot,enabled in ((1,True),(2,False)):
            self.assertTrue(parse_ui_render_lease_evidence([],slot,enabled,{},{},{},{})['pass'])
            self.assertFalse(parse_ui_render_lease_evidence(self.native,slot,enabled,{},{},{},{})['pass'])
        partial = self.parse(self.render_rows[:1])
        self.assertFalse(partial['pass'])
        self.assertEqual(partial['raw_native_receipts'], self.render_rows[:1])

    def test_collection_requires_shared_hashes_for_all_four_proofs(self):
        import copy
        import hashlib
        import tempfile
        from pathlib import Path
        from linux_ui_render_lease_evidence import collect_ui_render_lease_evidence
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory)
            stderr=profile/'stderr.log'
            paths={'engine_sha256':profile/'engine-output.log',
                   'native_fd_sha256':profile/'controller-lifecycle.log','stderr_sha256':stderr}
            proofs=copy.deepcopy([self.overlay,self.fork,self.query,self.callback])
            for key,path in paths.items():
                path.write_text(''.join(self.native) if path==stderr else key)
                for proof in proofs:
                    proof[key]=hashlib.sha256(path.read_bytes()).hexdigest()
            result=collect_ui_render_lease_evidence(profile,stderr,2,True,*proofs)
            self.assertTrue(result['pass'],result['errors'])
            for index in range(4):
                changed=copy.deepcopy(proofs)
                changed[index]['stderr_sha256']='0'*64
                self.assertFalse(collect_ui_render_lease_evidence(profile,stderr,2,True,*changed)['pass'])
