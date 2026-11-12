"""Exact policy payloads and causal scope boundaries; no engine or native launch."""
import hashlib
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from linux_player_context_evidence import KNOWN_ADDITION, verify_context_policy
from linux_player_network_evidence import parse_network_physics_evidence
import test_linux_player_camera_evidence as camera_fixture
from test_linux_player_network_evidence import network_fixture

P = 'X4LAN LIFECYCLE_DISCARD '


def receipt(event, tick, boundary, epoch, base, values):
    payloads = b''.join(values).hex()
    digest = hashlib.sha256(bytes.fromhex(payloads)).hexdigest()
    return (f'X4LAN CONTEXT_POLICY v=1 event={event} tick={tick} boundary={boundary} epoch={epoch} '
            f'owner=100 manager=110 target=aaa controller=aa base_nodes={base} nodes={len(values)} '
            f'digest={digest} payloads={payloads}\n')


def fixture(camera=False, transitions=True):
    maker = camera_fixture.CameraContextV2ProofTest()
    proof = maker.versioned()['2']['lifecycle']
    old = [bytes.fromhex(row.split('payload=')[1]) for row in proof if 'stage=before index=' in row]
    new = [bytes.fromhex(row.split('payload=')[1]) for row in proof if 'stage=after index=' in row]
    if camera:
        rows, stderr, engine, _ = maker.anchored_fixture()
    else:
        rows, stderr, engine, _, _ = network_fixture()
    selected = b''.join(p for p in old if int.from_bytes(p[:4], 'little') in (8,15,16)).hex()
    out, active, epoch, values, tick = [], False, 0, old, 0
    for line in rows:
        if 'physics=frame tick=' in line and 'event=begin' in line:
            tick = int(line.split('tick=')[1].split()[0])
        if line.startswith(P + 'snapshot=physics_canonical '):
            out.append(receipt('init', 5, 'ready', 1, 13, old))
            active, epoch = True, 1
        if not camera and transitions and tick == 7 and line.startswith(P + 'snapshot=physics_update_before '):
            epoch += 1; values = old
            out.append(receipt('remove', 7, 'begin', epoch, 13, values))
        if 'contexts_payloads=' in line:
            line = re.sub(r'contexts_payloads=[0-9a-f]{96}', 'contexts_payloads=' + selected, line)
            line = re.sub(r' context_anchor_v=.*\n', '\n', line)
            if active:
                line = re.sub(r'contexts_nodes=\d+', f'contexts_nodes={len(values)}', line)
                digest = hashlib.sha256(b''.join(values)).hexdigest()
                line = line.rstrip('\n') + f' context_policy_v=1 manager=110 epoch={epoch} full_hash={digest}\n'
        out.append(line)
        if camera and line.startswith('X4LAN CONTEXT_ANCHOR '):
            epoch += 1; values = new
            out.append(receipt('camera', 6, 'camera', epoch, 15, values))
        if camera and transitions and tick == 7 and line.startswith(P + 'physics=upkeep stage=leave '):
            epoch += 1; values = new + [KNOWN_ADDITION]
            out.append(receipt('add', 7, 'end', epoch, 15, values))
        if not camera and transitions and tick == 6 and line.startswith(P + 'physics=upkeep stage=leave '):
            epoch += 1; values = old + [KNOWN_ADDITION]
            out.append(receipt('add', 6, 'end', epoch, 13, values))
    return out, stderr, engine


class ContextPolicyEvidenceTests(unittest.TestCase):
    def parse(self, rows, stderr, engine, camera=False):
        return parse_network_physics_evidence(rows, stderr, engine, 223, frame_count=3,
                                               slot=2, allow_camera=camera)

    def test_exact_init_add_remove_and_camera_rebase(self):
        for camera in (False, True):
            rows, stderr, engine = fixture(camera)
            result = self.parse(rows, stderr, engine, camera)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual([r['event'] for r in result['context_policy']['receipts']],
                             ['init','camera','add'] if camera else ['init','add','remove'])
            for frame in result['frames']:
                for snapshot in frame['snapshots'].values():
                    expected = snapshot['context_policy']
                    if expected:
                        self.assertEqual(snapshot['nodes'], str(expected['nodes']))
                        self.assertEqual(snapshot['policy_epoch'], str(expected['epoch']))
            self.assertFalse(result['restored'] or result['resumed'] or result['movement_proven'])

    def test_missing_duplicate_reordered_and_unframed_receipts(self):
        rows, stderr, engine = fixture()
        indices = [i for i,row in enumerate(rows) if row.startswith('X4LAN CONTEXT_POLICY ')]
        for index in indices:
            with self.subTest(index=index):
                self.assertFalse(self.parse(rows[:index] + rows[index+1:], stderr, engine)['pass'])
                self.assertFalse(self.parse(rows[:index] + [rows[index]] + rows[index:], stderr, engine)['pass'])
                self.assertFalse(self.parse(rows[:index+1] + rows[index+2:], stderr, engine)['pass'])
        add = indices[1]
        cases = [[rows[add]] + rows[:add] + rows[add+1:], rows[:add] + rows[add+1:] + [rows[add]],
                 rows[:add-1] + [rows[add],rows[add-1]] + rows[add+1:]]
        for bad in cases:
            self.assertFalse(self.parse(bad,stderr,engine)['pass'])
        # A receipt moved inside native upkeep is not a legal end boundary.
        bad=rows.copy();row=bad.pop(add);inside=next(i for i,r in enumerate(bad) if 'snapshot=physics_upkeep_after' in r and 'epoch=1' in r and i>add-5)
        bad.insert(inside,row);self.assertFalse(self.parse(bad,stderr,engine)['pass'])

    def test_digest_identity_epoch_boundary_and_known_payload_are_exact(self):
        rows,stderr,engine=fixture()
        changes=[('owner=100','owner=101'),('manager=110','manager=111'),('target=aaa','target=aab'),
                 ('controller=aa','controller=ab'),('epoch=2','epoch=4'),('boundary=end','boundary=ready'),
                 ('base_nodes=13','base_nodes=15'),('nodes=14','nodes=15'),
                 ('3b000000080000000000000001000000','3b000000080000000000000001000001')]
        for old,new in changes:
            bad=[row.replace(old,new,1) if 'event=add ' in row else row for row in rows]
            with self.subTest(change=old):self.assertFalse(self.parse(bad,stderr,engine)['pass'])
        for mutate in (lambda r:re.sub('digest=[0-9a-f]{64}','digest='+'0'*64,r),
                       lambda r:r.replace('event=add','event=remove'),
                       lambda r:r.replace('owner=100','owner=10000000000000000'),
                       lambda r:r[:-1], lambda r:'prefix '+r, lambda r:r.rstrip('\n')+' extra=1\n'):
            bad=[mutate(row) if 'event=add ' in row else row for row in rows]
            self.assertFalse(self.parse(bad,stderr,engine)['pass'])

    def test_known_addition_tail_refuses_even_with_recomputed_digest(self):
        rows,stderr,engine=fixture()
        added=next(row for row in rows if 'event=add ' in row)
        old_digest=re.search('digest=([0-9a-f]{64})',added)[1]
        changed=added.replace(KNOWN_ADDITION.hex(),KNOWN_ADDITION.hex()[:-2]+'01')
        payloads=changed.split('payloads=')[1].strip()
        new_digest=hashlib.sha256(bytes.fromhex(payloads)).hexdigest()
        bad=[(changed if row==added else row).replace(old_digest,new_digest) for row in rows]
        result=self.parse(bad,stderr,engine)
        self.assertFalse(result['pass'])
        self.assertTrue(any('unexpected base or added payload mutation' in error for error in result['errors']))

    def test_every_active_snapshot_and_scope_epoch_are_bound(self):
        rows,stderr,engine=fixture()
        indices=[i for i,row in enumerate(rows) if 'context_policy_v=1' in row]
        for index in (indices[0],indices[-1]):
            for change in (lambda r:r.split(' context_policy_v=')[0]+'\n',
                           lambda r:r.replace('manager=110','manager=111'),
                           lambda r:re.sub('full_hash=[0-9a-f]{64}','full_hash='+'0'*64,r),
                           lambda r:r.replace('contexts_nodes=13','contexts_nodes=14')):
                bad=rows.copy();bad[index]=change(bad[index]);self.assertFalse(self.parse(bad,stderr,engine)['pass'])
        index=next(i for i,row in enumerate(rows) if 'snapshot=physics_update_after ' in row)
        bad=rows.copy();bad[index]=bad[index].replace('epoch=1','epoch=2')
        self.assertFalse(self.parse(bad,stderr,engine)['pass'])
        bad=[row.replace('context_policy_v=1','context_anchor_v=1') for row in rows]
        self.assertFalse(self.parse(bad,stderr,engine)['pass'])

    def test_camera_full_before_binding_not_only_selected_targets(self):
        rows,stderr,engine=fixture(True)
        # Change a retained non-target opaque byte in both camera censuses and
        # recompute the after digest everywhere. The initial full13 proof must win.
        key='0100000006000000000000000101aabb';changed=key[:-2]+'bc'
        self.assertTrue(any(key in row for row in rows))
        bad=[row.replace(key,changed) if 'CAMERA_CONTEXT ' in row or 'event=camera ' in row else row for row in rows]
        after=[bytes.fromhex(row.split('payload=')[1]) for row in bad if 'CAMERA_CONTEXT ' in row and 'stage=after ' in row]
        old_digest=hashlib.sha256(b''.join(bytes.fromhex(row.split('payload=')[1]) for row in rows if 'CAMERA_CONTEXT ' in row and 'stage=after ' in row)).hexdigest()
        new_digest=hashlib.sha256(b''.join(after)).hexdigest()
        bad=[row.replace(old_digest,new_digest) for row in bad]
        result=self.parse(bad,stderr,engine,True)
        self.assertFalse(result['pass']);self.assertTrue(any('full before/after proof' in e for e in result['errors']))

    def test_legacy_context_change_still_fails_without_policy(self):
        rows,stderr,engine,_,_=network_fixture()
        bad=[row.replace('contexts_nodes=13','contexts_nodes=14') if 'snapshot=physics_end ' in row else row for row in rows]
        self.assertFalse(self.parse(bad,stderr,engine)['pass'])
        malformed=verify_context_policy(['X4LAN CONTEXT_POLICY malformed\n'])
        self.assertTrue(malformed['enabled']);self.assertTrue(malformed['errors'])

if __name__=='__main__':unittest.main()
