"""Opt-in correction evidence uses synthetic traces and staged files only."""
import copy
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import test_linux_ui_live_pose_observer_evidence as live_fixture
import test_linux_ui_overlay_runner as overlay_fixture
import test_linux_ui_pose_scope_evidence as scope_fixture
import linux_gate_live as live
from linux_ui_seat_correction_evidence import (
    collect_ui_seat_correction_evidence, expected_delta, expected_output,
    parse_ui_seat_correction_evidence,
)


class SeatCorrectionTests(unittest.TestCase):
    def setUp(self):
        self.fixture = scope_fixture.PoseScopeTests()
        self.fixture.setUp()
        self.overlay = self.fixture.overlay
        self.fork = self.fixture.fixture.fixture.fork
        self.query = self.fixture.fixture.fixture.parse()
        self.legacy = copy.deepcopy(self.fixture.legacy)
        self.scope = self.fixture.parse()
        self.native = list(self.fixture.native)
        self.identity = [0.0]*16
        self.identity[4] = self.identity[9] = self.identity[14] = 1.0
        self.guest = list(self.identity)
        self.guest[0] = 0.25
        for sample, values in zip(self.query['observations'], (self.identity, self.guest)):
            sample['raw'] = ''.join(f'{word:08x}' for word in struct.unpack('<16I', struct.pack('<16f', *values)))
        self.delta = struct.pack('<16f', *expected_delta(
            struct.pack('<16f', *self.identity).hex(), struct.pack('<16f', *self.guest).hex())).hex()
        raw = struct.pack('<16f', *self.identity).hex()
        corrected = struct.pack('<16f', *expected_output(raw, self.delta)).hex()
        open_samples = [sample for sample in self.scope['representatives'] if sample['scope'] == 'open']
        self.sample_rows = []
        for sample in open_samples:
            sample.update(caller=0x1314317, mode=1, move=1, render=0, after=raw, after_valid=1)
            self.sample_rows.append(
                f'X4 UI SEAT_CORRECTION v=1 stage=sample ordinal={sample["ordinal"]} '
                f'last_move={sample["last_move"]} caller=1314317 time={sample["time_bits"]} '
                f'raw={raw} corrected={corrected}\n')
        self.published = ('X4 UI SEAT_CORRECTION v=1 stage=published tick=9 slot=110108 '
                          f'original=900000 scratch=a00000 raw={raw} corrected={corrected}\n')
        self.restored = ('X4 UI SEAT_CORRECTION v=1 stage=published_restore tick=9 '
                         'slot=110108 original=900000 observed=900000\n')
        self.ready = ('X4 UI SEAT_CORRECTION v=1 stage=ready tick=9 target=10000 '
                      f'delta={self.delta} stack_low=700000 stack_high=800000\n')
        frames = self.overlay['frames']
        self.close = ('X4 UI SEAT_CORRECTION v=1 stage=close tick=168 '
                      f'applies={sum(frame["applies"] for frame in frames)} '
                      f'restores={sum(frame["restores"] for frame in frames)} '
                      'calls=2 rows=2 published_restored=1 state=outside\n')
        bind = next(i for i, line in enumerate(self.native) if 'LIVE_POSE v=1 stage=bind' in line)
        self.native[bind+1:bind+1] = [self.ready, self.published]
        during = next(i for i, line in enumerate(self.native) if line.startswith('X4 UI VIEWSTATE v=1 tick=9 stage=during '))
        self.native.insert(during+1, self.restored)
        close = next(i for i, line in enumerate(self.native) if 'POSE_SCOPE v=1 stage=close' in line)
        self.native[close+1:close+1] = self.sample_rows + [self.close]

    def parse(self, native=None, **changes):
        proofs = {'overlay': self.overlay, 'fork': self.fork, 'query': self.query,
                  'legacy': self.legacy, 'scope': self.scope}
        proofs.update(changes)
        return parse_ui_seat_correction_evidence(
            self.native if native is None else native, 2, True, **proofs)

    def mutate(self, old, new):
        return [line.replace(old, new) if 'SEAT_CORRECTION' in line else line for line in self.native]

    def test_positive_calls_bind_stock_samples_delta_and_publication_restoration(self):
        result = self.parse()
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['correction_observed'])
        self.assertTrue(result['sample_coverage_complete'])
        for key in ('readiness_proven', 'safe_resume_proven', 'reader_exclusion_proven'):
            self.assertFalse(result[key])
        for name in ('overlay', 'fork', 'query', 'legacy', 'scope'):
            proof = dict(getattr(self, name), **{'pass': False})
            self.assertFalse(self.parse(**{name: proof})['pass'])

    def test_missing_duplicate_reordered_and_wrong_phase_receipts_reject(self):
        variants = [self.native + [self.ready], self.native + [self.close]]
        for row in (self.ready, self.published, self.restored, *self.sample_rows, self.close):
            variants.append([line for line in self.native if line != row])
        reordered = list(self.native)
        index = reordered.index(self.sample_rows[0])
        reordered[index], reordered[index+1] = reordered[index+1], reordered[index]
        variants.append(reordered)
        without = [line for line in self.native if line not in (self.ready, self.published, self.restored, *self.sample_rows, self.close)]
        variants.extend(([self.ready] + without + self.sample_rows + [self.close],
                         without + [self.ready] + self.sample_rows + [self.close]))
        for native in variants:
            self.assertFalse(self.parse(native)['pass'])

    def test_target_stack_counts_time_and_raw_caller_cannot_be_forged(self):
        for old, new in (('target=10000', 'target=10008'), ('slot=110108', 'slot=110110'),
                         ('observed=900000', 'observed=900010'), ('scratch=a00000', 'scratch=900000'),
                         ('original=900000', 'original=900001'), ('stack_high=800000', 'stack_high=700010'),
                         ('calls=2', 'calls=0'), ('rows=2', 'rows=1'),
                         ('applies=320', 'applies=321'), ('restores=320', 'restores=321'),
                         ('published_restored=1', 'published_restored=0'), ('state=outside', 'state=open'),
                         ('caller=1314317', 'caller=1314407'), ('ordinal=35', 'ordinal=36'),
                         ('time=401ccccccccccccd', 'time=7ff0000000000000'),
                         ('last_move=43', 'last_move=8')):
            with self.subTest(old=old):
                self.assertFalse(self.parse(self.mutate(old, new))['pass'])

    def test_math_rigidity_metadata_and_query_delta_are_independent_checks(self):
        result = self.parse()
        raw = result['samples'][0]['raw']
        corrected = result['samples'][0]['corrected']
        for old, new in (
                ('corrected='+corrected, 'corrected='+raw),
                ('corrected='+corrected, 'corrected='+corrected[:24]+'01000000'+corrected[32:]),
                ('delta='+self.delta, 'delta='+('00'*64)),
                ('raw='+raw, 'raw=0000807f'+raw[8:])):
            with self.subTest(old=old[:20]):
                self.assertFalse(self.parse(self.mutate(old, new))['pass'])
        query = copy.deepcopy(self.query)
        query['observations'][1]['raw'] = query['observations'][0]['raw']
        self.assertFalse(self.parse(query=query)['pass'])

    def test_identity_fast_paths_require_exact_bits_including_signed_zero(self):
        query = copy.deepcopy(self.query)
        query['observations'][1]['raw'] = query['observations'][0]['raw']
        identity = struct.pack('<16f', *self.identity).hex()
        corrected = self.parse()['samples'][0]['corrected']
        native = self.mutate('delta='+self.delta, 'delta='+identity)
        native = [line.replace('corrected='+corrected, 'corrected='+identity) for line in native]
        result = self.parse(native, query=query)
        self.assertTrue(result['pass'], result['errors'])
        changed = '00000080'+identity[8:]
        forged = [line.replace('corrected='+identity, 'corrected='+changed) for line in native]
        self.assertFalse(self.parse(forged, query=query)['pass'])
        tiny = struct.pack('<f', 1e-7).hex()+identity[8:]
        forged = [line.replace('delta='+identity, 'delta='+tiny) for line in native]
        self.assertFalse(self.parse(forged, query=query)['pass'])

    def test_row_vector_rotation_and_metadata_have_independent_known_result(self):
        canonical = [1., 2., 3., -0., 0., 1., 0., 7., -1., 0., 0., 8., 0., 0., 1., 9.]
        guest = [4., 5., 6., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.]
        raw = struct.pack('<16f', *canonical).hex()
        delta = expected_delta(raw, struct.pack('<16f', *guest).hex())
        self.assertEqual(delta, [2., 6., 3., 0., 0., -1., 0., 0., 1., 0., 0., 0., 0., 0., 1., 0.])
        result = expected_output(raw, struct.pack('<16f', *delta).hex())
        for row in range(4):
            self.assertEqual(result[4*row:4*row+3], guest[4*row:4*row+3])
            self.assertEqual(struct.pack('<f', result[4*row+3]), struct.pack('<f', canonical[4*row+3]))

    def test_disabled_host_and_missing_receipts_never_mean_performed(self):
        for slot, enabled in ((1, True), (2, False)):
            result = parse_ui_seat_correction_evidence([], slot, enabled, {}, {}, {}, {}, {})
            self.assertTrue(result['pass'])
            self.assertFalse(result['correction_observed'])
            self.assertFalse(parse_ui_seat_correction_evidence(
                self.native, slot, enabled, {}, {}, {}, {}, {})['pass'])
        self.assertFalse(self.parse([])['pass'])

    def test_partial_failure_retains_raw_even_without_other_accepted_proofs(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            stderr = profile/'stderr.log'
            stderr.write_text(self.ready+self.published+'X4 UI OVERLAY FATAL reason=return\n')
            result = collect_ui_seat_correction_evidence(profile, stderr, 2, True, {}, {}, {}, {}, {})
            self.assertFalse(result['pass'])
            self.assertFalse(result['correction_observed'])
            self.assertEqual(result['raw_native_receipts'], [self.ready, self.published])
            self.assertTrue(any('input unavailable' in error for error in result['errors']))

    def test_raw_file_hashes_bind_every_accepted_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            stderr = profile/'stderr.log'
            paths = {'engine_sha256': profile/'engine-output.log',
                     'native_fd_sha256': profile/'controller-lifecycle.log', 'stderr_sha256': stderr}
            proofs = [copy.deepcopy(proof) for proof in
                      (self.overlay, self.fork, self.query, self.legacy, self.scope)]
            for key, path in paths.items():
                path.write_text(''.join(self.native) if path == stderr else key)
                for proof in proofs:
                    proof[key] = hashlib.sha256(path.read_bytes()).hexdigest()
            result = collect_ui_seat_correction_evidence(profile, stderr, 2, True, *proofs)
            self.assertTrue(result['pass'], result['errors'])
            for index in range(len(proofs)):
                forged = copy.deepcopy(proofs)
                forged[index]['stderr_sha256'] = '0'*64
                self.assertFalse(collect_ui_seat_correction_evidence(profile, stderr, 2, True, *forged)['pass'])


class SeatCorrectionAdmissionTests(unittest.TestCase):
    def options(self):
        return dict(live_fixture.LivePoseAdmissionTests().options(), player_ui_seat_correction=True)

    def test_default_off_strict_option_version_and_guest_environment(self):
        self.assertFalse(live.validate_seat_correction({}))
        self.assertTrue(live.validate_seat_correction(self.options()))
        for value in (None, 0, 1, 'true'):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                live.validate_seat_correction(dict(self.options(), player_ui_seat_correction=value))
        for key in ('player_ui_live_pose_observer', 'player_ui_seat_query_probe', 'player_ui_overlay_probe'):
            with self.subTest(key=key), self.assertRaises(SystemExit):
                live.validate_ui_overlay(dict(self.options(), **{key: False}), 180)
        for version in (None, True, '1', 2):
            with self.subTest(version=version), self.assertRaises(SystemExit):
                live.validate_seat_correction(dict(self.options(), player_ui_seat_correction_evidence_version=version), frozen=True)
        self.assertTrue(live.validate_seat_correction(dict(
            self.options(), player_ui_seat_correction_evidence_version=1), frozen=True))
        self.assertEqual(live.seat_correction_environment(True, 2), {'X4_DEV_UI_SEAT_CORRECTION': '1'})
        self.assertEqual(live.seat_correction_environment(True, 1), {})
        self.assertEqual(live.seat_correction_environment(False, 2), {})

    def test_exact_config_and_source_receipts_required(self):
        config = 'X4LAN_CONFIG.native_gate_linux_ui_seat_correction = true\n'
        live.validate_seat_correction_config(config, True)
        live.validate_seat_correction_config('', False)
        for text, enabled in (('', True), (config, False), (config+config, True), (config.replace('true', '1'), True)):
            with self.subTest(text=text), self.assertRaises(SystemExit):
                live.validate_seat_correction_config(text, enabled)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in live.SEAT_CORRECTION_SOURCES:
                path = root/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            sources = live.seat_correction_sources(root)
            live.freeze_source_receipts(root, root/'bundle', sources)
            self.assertTrue(all(live.sha(source) == live.sha(root/'bundle/source-receipts'/source.relative_to(root)) for source in sources))
            sources[0].unlink()
            with self.assertRaises(SystemExit):
                live.seat_correction_sources(root)

    def test_mocked_bundle_forwards_option_and_freezes_manifest_config_collector(self):
        import json
        original_main = live.main

        def build(arguments):
            enabled = '--player-ui-live-pose-observer' in arguments
            forwarded = arguments + (['--player-ui-seat-correction'] if enabled else [])
            result = original_main(forwarded)
            output = Path(arguments[arguments.index('--out')+1])
            manifest = json.loads((output/'manifest.json').read_text())
            self.assertIs(manifest['player_ui_seat_correction'], enabled)
            self.assertEqual(manifest['player_ui_seat_correction_evidence_version'], 1 if enabled else None)
            live.validate_seat_correction(manifest, frozen=True)
            live.validate_seat_correction_config((output/'extension/x4_lan_lab/ui/config.lua').read_text(), enabled)
            self.assertEqual(manifest['files']['linux_ui_seat_correction_evidence.py'],
                             live.sha(output/'linux_ui_seat_correction_evidence.py'))
            return result

        # Native compilation is mocked by the shared bundle fixture. Source
        # closure itself is checked independently above using staged files.
        with (mock.patch.object(live, 'main', side_effect=build),
              mock.patch.object(live, 'seat_correction_sources', return_value=[])):
            live_fixture.LivePoseBundleTests().test_generated_bundle_forwards_flag_and_freezes_exact_provider_sources()

    def test_lua_requires_exact_bool_and_full_explicit_chain(self):
        fixture = overlay_fixture.UiOverlayLuaTests()
        for value in ('false', 'true', '1', '"true"'):
            owner, lua = fixture.runtime('p2')
            lua.execute('cfg.native_gate_linux_ui_seat_correction='+value)
            if value == 'false':
                owner.create(lua)
            else:
                with self.assertRaises(Exception):
                    owner.create(lua)
        owner, lua = fixture.runtime('p2')
        lua.execute('''
            cfg.native_gate_linux_ui_seat_correction=true
            cfg.native_gate_linux_ui_seat_query_probe=true
            cfg.native_gate_linux_ui_live_pose_observer=true
            cfg.native_gate_linux_ui_overlay_callbacks=160
            cfg.native_gate_stock_keyboard_replay_probe.last_capture=167
        ''')
        owner.create(lua)
