"""Adversarial native/Lua camera evidence checks; no game or X11."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from linux_player_camera_evidence import collect_camera_evidence, parse_camera_evidence
import linux_gate_live as live
from linux_player_network_evidence import parse_network_physics_evidence
from test_linux_player_network_evidence import network_fixture

BANK = 'ab' * 32
CONTEXT1, CONTEXT2 = 'cd' * 32, 'ef' * 32
BEFORE = ('X4LAN CAMERA stage=before tick=6 pins=1 saved=0000000000175310 target=1234 '
          f'runtime=442530 current=442691 bank={BANK} contexts=13/7 context_hash={CONTEXT1} '
          'rng=9275c381a57c8fe6 ids=1368534/1355691/669654\n')
AFTER = ('X4LAN CAMERA stage=after tick=6 possible=1 set=1 current=442530 match=1 '
         f'bank={BANK} bank_changed=0 contexts=15/7 context_hash={CONTEXT2} context_changed=1 '
         'rng=9275c381a57c8fe7 restored=9275c381a57c8fe6 ids=1374308/1361465/669635\n')
RESULT = 'X4LAN CAMERA stage=result success=1 reason=matched\n'
MARKER = '[=ERROR=] 7.15 [X4LAN] NATIVE_GATE LINUX_EXTERNAL_CAMERA tick=6 slot=2 target=0000000000175310\n'


def logs():
    return {'1': {'lifecycle': [], 'engine': [], 'stdout': [], 'stderr': []},
            '2': {'lifecycle': [BEFORE, AFTER, RESULT], 'engine': [MARKER], 'stdout': [], 'stderr': []}}


class CameraEvidenceTest(unittest.TestCase):
    def test_success_reports_changed_state_not_visual_proof(self):
        result = parse_camera_evidence(logs())
        self.assertTrue(result['pass'], result['errors'])
        self.assertFalse(result['visual_proven'])
        self.assertEqual(result['deltas'], {'nodes': 2, 'inserts': 5774, 'live': 5774, 'free': -19})
        self.assertNotEqual(result['before']['context'], result['after']['context'])
        case = logs()
        case['2']['stdout'] = ['[X4LAN] NATIVE_GATE LINUX_EXTERNAL_CAMERA tick=6 slot=2 target=0000000000175310\n']
        self.assertTrue(parse_camera_evidence(case)['pass'])
        case['2']['stdout'] *= 2
        self.assertFalse(parse_camera_evidence(case)['pass'])

    def test_refuse_bad_or_foreign_native_and_marker(self):
        changes = [
            ('lifecycle', [BEFORE, RESULT, AFTER]),
            ('lifecycle', [BEFORE, AFTER]),
            ('lifecycle', [BEFORE, AFTER, RESULT, BEFORE]),
            ('lifecycle', [BEFORE.replace('saved=0000000000175310', 'saved=0000000000175311'), AFTER, RESULT]),
            ('lifecycle', [BEFORE.replace('current=442691', 'current=442530'), AFTER, RESULT]),
            ('lifecycle', [BEFORE.replace('target=1234', 'target=0'), AFTER, RESULT]),
            ('lifecycle', [BEFORE, AFTER.replace('current=442530', 'current=442531'), RESULT]),
            ('lifecycle', [BEFORE, AFTER.replace('possible=1', 'possible=0'), RESULT]),
            ('lifecycle', [BEFORE, AFTER.replace('restored=9275c381a57c8fe6', 'restored=9275c381a57c8fe7'), RESULT]),
            ('lifecycle', [BEFORE, AFTER.replace('bank_changed=0', 'bank_changed=1'), RESULT]),
            ('lifecycle', [BEFORE, AFTER.replace('context_changed=1', 'context_changed=0'), RESULT]),
            ('lifecycle', [BEFORE, AFTER.replace('ids=1374308', 'ids=18446744073709551616'), RESULT]),
            ('lifecycle', [BEFORE, AFTER, RESULT.replace('success=1', 'success=0')]),
            ('engine', []), ('engine', [MARKER, MARKER]),
            ('engine', [MARKER.replace('slot=2', 'slot=1')]),
            ('engine', [MARKER.replace('target=0000000000175310', 'target=0000000000175311')]),
        ]
        for source, replacement in changes:
            case = logs(); case['2'][source] = replacement
            with self.subTest(replacement=replacement): self.assertFalse(parse_camera_evidence(case)['pass'])
        for source, row in (('engine', MARKER), ('lifecycle', BEFORE), ('stderr', RESULT)):
            case = logs(); case['1'][source].append(row)
            self.assertFalse(parse_camera_evidence(case)['pass'])

    def test_generic_frame_parser_filters_only_explicit_camera_mode(self):
        rows, stderr, engine, _, _ = network_fixture()
        index = next(i for i, row in enumerate(rows) if 'physics=frame tick=6 event=begin' in row)
        rows[index:index] = [BEFORE, AFTER, RESULT]
        kw = dict(frame_count=3, slot=2)
        self.assertFalse(parse_network_physics_evidence(rows, stderr, engine, 223, **kw)['pass'])
        parsed = parse_network_physics_evidence(rows, stderr, engine, 223, allow_camera=True, **kw)
        self.assertFalse(parsed['pass'])  # Old, unreceipted evidence cannot adopt a new baseline.
        changed = [row.replace('contexts_nodes=13', 'contexts_nodes=15')
                   if 'physics_update_after' in row else row for row in rows]
        self.assertFalse(parse_network_physics_evidence(changed, stderr, engine, 223,
                         allow_camera=True, **kw)['pass'])

    def test_runner_propagates_camera_failure(self):
        with mock.patch.object(live, 'collect_camera_evidence', return_value={'pass': False, 'errors': ['bad camera']}):
            physics = {'pass': True}
            live.add_camera_evidence(physics, Path('/fixture'), {})
            self.assertFalse(physics['pass'])
            self.assertEqual(physics['camera_probe_result']['errors'], ['bad camera'])
        with mock.patch.object(live, 'collect_camera_evidence', return_value={'pass': True, 'errors': []}):
            physics = {'pass': False}
            live.add_camera_evidence(physics, Path('/fixture'), {})
            self.assertFalse(physics['pass'])

    def test_file_collection_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); profiles = {}
            for slot in ('1', '2'):
                profiles[slot] = root / ('profile' + slot); profiles[slot].mkdir()
                for source, filename in (('lifecycle','controller-lifecycle.log'), ('engine','engine-output.log')):
                    (profiles[slot] / filename).write_text(''.join(logs()[slot][source]))
                for name in ('stdout','stderr'): (root / f'game{slot}-{name}.log').write_text('')
            self.assertTrue(collect_camera_evidence(root, profiles)['pass'])
            (root/'game1-stderr.log').unlink()
            self.assertFalse(collect_camera_evidence(root, profiles)['pass'])

class CameraContextProofTest(unittest.TestCase):
    @staticmethod
    def payload(context, requester, priority=5, enabled=0, tail=b'\0\0\0'):
        import struct
        return struct.pack('<iiiB3s', context, requester, priority, enabled, tail)

    def versioned(self):
        import hashlib
        b = [self.payload(context, 6, context, 1) for context in (8, 15, 16)]
        a = [b[0], self.payload(13, 20, tail=b'\xaa\xbb\xcc'),
             self.payload(14, 20, tail=b'\x02\x03\x04'), b[1], b[2]]
        digest = hashlib.sha256(b''.join(b)).hexdigest()
        before = BEFORE.replace('contexts=13/7', 'contexts=3/7').replace(CONTEXT1, digest)
        after = AFTER.replace('contexts=15/7', 'contexts=5/7').replace(CONTEXT2, digest)
        def rows(stage, payloads):
            return [f'X4LAN CAMERA_CONTEXT v=1 stage={stage} index={index} count={len(payloads)} '
                    f'payload={payload.hex()}\n' for index, payload in enumerate(payloads)]
        case = logs()
        case['2']['lifecycle'] = [before, *rows('before', b), after, *rows('after', a), RESULT]
        return case

    def test_complete_exact_insertion_and_legacy_unavailable(self):
        case = self.versioned()
        result = parse_camera_evidence(case)
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['context_proof']['pass'])
        self.assertEqual(len(result['context_proof']['before_payloads']), 3)
        self.assertFalse(parse_camera_evidence(logs())['context_proof']['available'])

    def test_wrong_missing_duplicate_reordered_and_changed_entries(self):
        case = self.versioned()
        rows = case['2']['lifecycle']
        changes = [rows[:2] + rows[3:], rows + [rows[-2]], rows[:1] + [rows[2], rows[1]] + rows[3:],
                   [row.replace('index=0 count=3', 'index=1 count=3') for row in rows],
                   [row.replace('count=5', 'count=4') for row in rows],
                   [row.replace('stage=after index=1', 'stage=before index=1') for row in rows],
                   [row.replace('0d000000140000000500000000aabbcc', '0d000000140000000600000000aabbcc') for row in rows],
                   [row.replace('0e000000140000000500000000020304', '0e000000150000000500000000020304') for row in rows],
                   [row.replace('08000000060000000800000001000000', '08000000060000000900000001000000') if 'stage=after index=0' in row else row for row in rows],
                   [row.replace('v=1 stage=after', 'v=2 stage=after') for row in rows]]
        for changed in changes:
            with self.subTest(changed=changed):
                self.assertNotEqual(changed, rows)
                altered = deepcopy(case); altered['2']['lifecycle'] = changed
                self.assertFalse(parse_camera_evidence(altered)['pass'])

class CameraContextV2ProofTest(unittest.TestCase):
    @staticmethod
    def payload(context, requester, priority=0, enabled=1, tail=b'\0\0\0'):
        import struct
        return struct.pack('<iiiB3s', context, requester, priority, enabled, tail)

    def versioned(self):
        import hashlib
        old = [self.payload(i, 6, tail=bytes((i, 0xaa, 0xbb)))
               for i in (0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 15, 16)]
        new = [row for row in old if row[:4] != (10).to_bytes(4, 'little')]
        new += [self.payload(11, 6, tail=b'\x7f\x81\xff'), self.payload(13, 20, 5, 0, b'\x08\x09\x0a'),
                self.payload(14, 20, 5, 0, b'\x09\x08\x07')]
        import struct
        new.sort(key=lambda row: struct.unpack_from('<ii', row))
        digest = hashlib.sha256(b''.join(row for row in old if int.from_bytes(row[:4], 'little') in (8,15,16))).hexdigest()
        before = BEFORE.replace(CONTEXT1, digest)
        after = AFTER.replace(CONTEXT2, digest)
        def rows(stage, payloads):
            return [f'X4LAN CAMERA_CONTEXT v=2 stage={stage} index={i} count={len(payloads)} payload={row.hex()}\n'
                    for i, row in enumerate(payloads)]
        case = logs(); case['2']['lifecycle'] = [before, *rows('before', old), after, *rows('after', new), RESULT]
        return case

    def test_exact_four_edit_contract_preserves_opaque_tails(self):
        case = self.versioned(); result = parse_camera_evidence(case)
        self.assertTrue(result['pass'], result['errors'])
        proof = result['context_proof']
        self.assertTrue(proof['pass']); self.assertEqual(proof['version'], 2)
        self.assertEqual(proof['opaque_tails']['removed'], {'10/6': '0aaabb'})
        self.assertEqual(proof['opaque_tails']['added']['11/6'], '7f81ff')
        self.assertEqual(proof['opaque_tails']['added']['13/20'], '08090a')

    def test_refuse_mixed_versions_extra_edits_or_wrong_semantics(self):
        case = self.versioned(); rows = case['2']['lifecycle']
        changes = [
            [row.replace('v=2 stage=after index=0', 'v=1 stage=after index=0') for row in rows],
            [row.replace('0a0000000600000000000000010aaabb', '0a0000000600000001000000010aaabb') for row in rows],
            [row.replace('0b0000000600000000000000017f81ff', '0b0000000600000000000000007f81ff') for row in rows],
            [row.replace('0d00000014000000050000000008090a', '0d00000014000000060000000008090a') for row in rows],
            [row.replace('0e000000140000000500000000090807', '0e000000150000000500000000090807') for row in rows],
            [row.replace('0700000006000000000000000107aabb', '0700000006000000000000000107aabc') if 'stage=after' in row else row for row in rows],
            rows[:-2] + rows[-1:], rows + [rows[-2]],
        ]
        for changed in changes:
            with self.subTest(changed=changed):
                self.assertNotEqual(changed, rows)
                altered = deepcopy(case); altered['2']['lifecycle'] = changed
                self.assertFalse(parse_camera_evidence(altered)['pass'])

    def anchored_fixture(self):
        import hashlib
        rows, stderr, engine, _, _ = network_fixture()
        camera = self.versioned()['2']['lifecycle']
        camera = [r.replace('target=1234', 'target=aaa').replace('runtime=442530', 'runtime=123')
                  .replace('current=442530', 'current=123') for r in camera]
        payloads = [bytes.fromhex(r.split('payload=')[1].strip()) for r in camera if 'stage=before index=' in r]
        selected = b''.join(p for p in payloads if int.from_bytes(p[:4], 'little') in (8,15,16)).hex()
        after = [bytes.fromhex(r.split('payload=')[1].strip()) for r in camera if 'stage=after index=' in r]
        digest = hashlib.sha256(b''.join(after)).hexdigest()
        receipt = f'X4LAN CONTEXT_ANCHOR v=1 tick=6 owner=100 manager=110 target=aaa controller=aa digest={digest}\n'
        out, active, tick = [], False, 0
        for row in rows:
            if 'physics=frame tick=' in row and 'event=begin' in row:
                tick = int(row.split('tick=')[1].split()[0])
            if 'contexts_payloads=' in row:
                row = row.replace('contexts_payloads=' + '00' * 48, 'contexts_payloads=' + selected)
                if active:
                    row = row.replace('contexts_nodes=13', 'contexts_nodes=15').rstrip('\n') + f' context_anchor_v=1 manager=110 full_hash={digest}\n'
            out.append(row)
            if tick == 6 and 'physics=upkeep stage=leave' in row:
                out.extend(camera); out.append(receipt); active = True
        return out, stderr, engine, digest

    def test_receipt_required_and_full_anchor_checked(self):
        rows, stderr, engine, digest = self.anchored_fixture()
        kw = dict(frame_count=3, slot=2, allow_camera=True)
        result = parse_network_physics_evidence(rows, stderr, engine, 223, **kw)
        self.assertTrue(result['pass'], result['errors'])
        receipt = next(i for i, r in enumerate(rows) if 'CONTEXT_ANCHOR v=' in r)
        after = next(i for i, r in enumerate(rows) if 'snapshot=physics_end' in r and 'full_hash=' in r)
        cases = [rows[:receipt] + rows[receipt+1:], rows[:receipt] + [rows[receipt]] + rows[receipt:],
                 rows[:receipt] + rows[receipt+1:after+1] + [rows[receipt]] + rows[after+1:],
                 [r.replace('tick=6 owner=', 'tick=7 owner=') for r in rows],
                 [r.replace('manager=110', 'manager=111', 1) if 'CONTEXT_ANCHOR' in r else r for r in rows],
                 [r.replace('full_hash='+digest, 'full_hash='+'0'*64, 1) if i == after else r for i,r in enumerate(rows)],
                 [r.replace('0b0000000600000000000000017f81ff', '0b0000000600000000000000007f81ff') for r in rows],
                 [r.replace('target=aaa controller=aa', 'target=aab controller=aa') for r in rows]]
        for bad in cases:
            with self.subTest(case=cases.index(bad)):
                self.assertFalse(parse_network_physics_evidence(bad, stderr, engine, 223, **kw)['pass'])
        self.assertFalse(parse_network_physics_evidence(rows, stderr, engine, 223, frame_count=3, slot=1, allow_camera=True)['pass'])
