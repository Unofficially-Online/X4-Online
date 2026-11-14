import linux_semaphore_observer_bundle as latest_bundle
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import linux_player_cursor_bundle as cursor
import linux_input_observation_bundle as trace_bundle
from linux_player_network_evidence import decode_record


class CursorBundleTests(unittest.TestCase):
    def options(self):
        return dict(player_cursor_active_flight=True, player_startup_phases='v1-t40',
                    player_ui_session_frames=180, independent_keyboard_frames=180,
                    player_view_query_observer=True)

    def record(self, tick, bits='00000000'):
        data = bytearray(43)
        data[0:4] = bytes((3, 3)) + tick.to_bytes(2, 'little')
        data[4:8] = int(bits, 16).to_bytes(4, 'little')
        return data.hex()

    def test_explicit_admission_and_environment(self):
        self.assertFalse(cursor.enabled({}))
        self.assertTrue(cursor.enabled(self.options()))
        for key, value in [('player_cursor_active_flight', 1), ('player_startup_phases', None),
                           ('player_view_query_observer', False), ('independent_keyboard_frames', 179)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                cursor.enabled(dict(self.options(), **{key: value}))
        env = dict(X4_DEV_PLAYER_CURSOR='bad', X4_DEV_PLAYER_FOCUS='bad', OTHER='keep')
        self.assertEqual(cursor.environment(env, False), {'OTHER': 'keep'})
        self.assertEqual(cursor.environment(env, True), dict(OTHER='keep', X4_DEV_PLAYER_CURSOR='1', X4_DEV_PLAYER_FOCUS='1'))

    def test_network_cursor_decode_requires_explicit_flag_and_rejects_other_features(self):
        record = self.record(38, '3f800000')
        self.assertEqual(decode_record(record, 40, 0, allow_cursor=True), 0)
        with self.assertRaises(ValueError):
            decode_record(record, 40, 0)
        for offset, value in [(1, 0), (1, 2), (1, 7), (1, 11), (1, 19), (16, 1), (28, 1), (29, 1), (30, 1), (34, 1)]:
            data = bytearray.fromhex(record)
            data[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                decode_record(data.hex(), 40, 0, allow_cursor=True)
        for bits in ('7fc00000', '7f800000', '41880000'):
            with self.assertRaises(ValueError):
                decode_record(self.record(38, bits), 40, 0, allow_cursor=True)
        data = bytearray.fromhex(self.record(38))
        data[1] = 1
        self.assertEqual(decode_record(data.hex(), 40, 0, allow_cursor=True), 0)
        data[7] = 128
        with self.assertRaises(ValueError):
            decode_record(data.hex(), 40, 0, allow_cursor=True)
        data = bytearray.fromhex(record)
        data[42] = 1
        data.extend((8 | (3 << 14)).to_bytes(2, 'little') + bytes(4))
        with self.assertRaises(ValueError):
            decode_record(data.hex(), 40, 0, allow_cursor=True)

    def fixture(self, directory):
        stderr, trace = Path(directory) / 'stderr', Path(directory) / 'trace'
        wire = [self.record(tick) for tick in range(38, 218)]
        physics = {'pass': True, 'slot': 1, 'frames': [dict(tick=tick, worker_thread=4213747709) for tick in range(40, 220)],
                   'preapply_frames': [dict(tick=tick, worker_thread=4213747709, snapshots={'taken': {'controller': '20000'}}) for tick in range(37, 40)],
                   'slot_records': {'1': wire[:], '2': wire[:]}}
        lines = [cursor.ACTIVE, 'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=38 tail=1 event=enter\n', cursor.PREFIX + 'stage=targets tick=38 owner=123 worker=4213747709 canonical=10000 guest=20000 focused=1 flight=1\n']
        for tick in range(38, 220):
            if tick != 38:
                lines += [f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n']
            if tick <= 217:
                lines += [cursor.PREFIX + f'stage=capture tick={tick} focused=1 seen={1 if tick < 40 else 3} device=00000000,00000000,00000000 slot1=00000000,00000000,00000000 slot2=00000000,00000000,00000000\n']
            if tick < 219:
                lines += [f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release\n']
        lines += [cursor.PREFIX + 'stage=close tick=219 captured=180 slot1=182 slot2=180 complete=1\n']
        trace.write_text('X4LAN LIFECYCLE_DISCARD result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n')
        def proof():
            stderr.write_text(''.join(lines))
            physics.update(stderr_sha256=hashlib.sha256(stderr.read_bytes()).hexdigest(), log_sha256=hashlib.sha256(trace.read_bytes()).hexdigest())
            return physics
        return stderr, trace, lines, proof

    def test_hash_bound_tick_pointer_focus_and_replay_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr, trace, lines, proof = self.fixture(directory)
            result = cursor.collect(stderr, trace, True, proof(), self.options())
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(len(result['captures']), 180)
            original = lines[:]
            for before, after in [('guest=20000', 'guest=10000'), ('worker=4213747709', 'worker=4213747708'),
                                  ('tick=38 owner=', 'tick=39 owner='), ('tick=38 focused=1 seen=1', 'tick=38 focused=1 seen=3'),
                                  ('device=00000000,', 'device=3f800000,'), ('slot2=00000000,', 'slot2=3f800000,'),
                                  ('slot1=182', 'slot1=181'), ('flight=1', 'flight=0'), ('focused=1 flight=1', 'focused=0 flight=1')]:
                lines[:] = [line.replace(before, after) for line in original]
                with self.subTest(before=before):
                    self.assertFalse(cursor.collect(stderr, trace, True, proof(), self.options())['pass'])
            lines[:] = original
            accepted = proof()
            self.assertFalse(cursor.collect(stderr, trace, True, dict(accepted, stderr_sha256='0'*64), self.options())['pass'])
            self.assertFalse(cursor.collect(stderr, trace, True, dict(accepted, **{'pass': False}), self.options())['pass'])
            self.assertFalse(cursor.collect(stderr, trace, False, accepted, self.options())['pass'])

    def test_real_bundle_forwards_explicit_cursor_and_seals_sources(self):
        import json
        import linux_gate_live as live
        from test_linux_stock_view_fov_observer import StockViewObserverTests
        original_main = live.main

        def build(arguments):
            active = '--player-startup-phases' in arguments
            arguments = arguments + (['--player-cursor-active-flight'] if active else [])
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertIs(manifest['player_cursor_active_flight'], active)
            self.assertFalse(latest_bundle.validate_bundle(output, manifest, True))
            for source in cursor.SOURCES:
                key = 'source-receipts/' + source
                self.assertEqual(manifest['files'][key], live.sha(output / key))
            return result

        with mock.patch.object(live, 'main', side_effect=build):
            StockViewObserverTests().test_real_bundle_path_freezes_observer_default_and_activation()

    def test_runner_collection_forwards_proofs_and_never_masks_failures(self):
        import linux_gate_live as live
        first, second = {'pass': True}, {'pass': True}
        result = {'pass': False, 'physics_discard': {'pass': True, 'clients': {'1': first, '2': second}}}
        with mock.patch.object(cursor, 'collect', return_value={'pass': True}) as collect:
            live.collect_cursor_observations(result, [1, 2], [(None, Path('/p1')), (None, Path('/p2'))], Path('/run'), True, self.options())
            self.assertIs(collect.call_args_list[0].args[-2], first)
            self.assertIs(collect.call_args_list[1].args[-2], second)
        self.assertFalse(result['pass'])
        with mock.patch.object(cursor, 'collect', return_value={'pass': False}):
            live.collect_cursor_observations(result, [1, 2], [(None, Path('/p1')), (None, Path('/p2'))], Path('/run'), True, self.options())
        self.assertFalse(result['physics_discard']['pass'])

    def test_source_generation_five_off_still_sealed_and_v4_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(cursor.previous, 'validate_bundle', return_value=False) as previous:
                self.assertFalse(cursor.validate_bundle(root, {'view_query_source_version': 4}, True))
                previous.assert_called_once()
                manifest = {'view_query_source_version': 5, 'files': {}}
                with self.assertRaises(ValueError):
                    cursor.validate_bundle(root, manifest, True)
                for name in cursor.SOURCES:
                    path = root / 'source-receipts' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(name)
                    manifest['files']['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertFalse(cursor.validate_bundle(root, manifest, True))
                for name in cursor.SOURCES:
                    invalid = dict(manifest, files=dict(manifest['files']))
                    invalid['files'].pop('source-receipts/' + name)
                    with self.subTest(name=name), self.assertRaises(ValueError):
                        cursor.validate_bundle(root, invalid, True)
