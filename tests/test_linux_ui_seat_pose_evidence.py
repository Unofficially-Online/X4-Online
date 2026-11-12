"""Synthetic seat observations only; never starts X4 or opens a display."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import linux_gate_live as live
import linux_ui_seat_pose_evidence as seat
import test_linux_ui_plan_kick_runner as kick_tests


def raw_words(values):
    return ''.join(f'{struct.unpack("<I",struct.pack("<f",value))[0]:08x}' for value in values)


class SeatPoseTests(unittest.TestCase):
    def setUp(self):
        self.fixture=kick_tests.PlanKickTests();self.fixture.setUp()
        body=('X4LAN LIFECYCLE_DISCARD physics=body stage=before index=0 '
              'ship=150000 vtable=234 parent=140000 runtime=222 present=1 '
              'record=30000 xbody=40000 scene=50000 system=60000 interface=70000 manager=80000 '
              'array=90000 body=a0000 motion=b0000 count=1 id=0 motion_present=1 '
              'position=0,0,0 rotation=0,0,0,1 linear=0,0,0 angular=0,0,0\n')
        self.trace=[]
        for line in self.fixture.trace[:-1]:
            if 'stage=p1 ' in line:line=line.replace('player=1 ','player=110000 ').replace('parent=12 ','parent=130000 ')
            if 'stage=p2 ' in line:line=line.replace('player=2 ','player=120000 ').replace('parent=12 ','parent=140000 ')
            self.trace.append(line)
        self.trace.append(body)
        translate=lambda line:line.replace('p1=1 ','p1=110000 ').replace('p2=2 ','p2=120000 ').replace('room=12 ','room=140000 ').replace('live=12 ','live=140000 ').replace('cached=12 ','cached=140000 ')
        self.engine=[translate(line) for line in self.fixture.overlay.engine]
        base_native=[translate(line) for line in self.fixture.native]
        base_native=base_native[:1]+[self.fixture.during.replace('stage=during','stage=before')]+base_native[1:]
        self.matrix=[1,2,3,0, 1,0,0,0, 0,1,0,0, 0,0,1,0]
        self.raw=raw_words(self.matrix)
        self.seats=[]
        self.native=[]
        for line in base_native:
            if line.startswith('X4 UI VIEWSTATE '):
                stage=line.split('stage=',1)[1].split()[0]
                for role in ('p1','p2'):
                    actor,ship,controller,vtable,candidate=('110000','160000','10000','30000','50000') if role=='p1' else ('120000','150000','20000','40000','60000')
                    room='130000' if role=='p1' else '140000'
                    parent='140000' if stage=='during' and role=='p1' else room
                    position=1 if stage=='during' and role=='p1' else 0
                    row=(f'X4 UI SEAT_POSE v=1 tick=9 stage={stage} role={role} status=observed '
                         f'actor={actor} ship={ship} room={room} parent={parent} controller={controller} vtable={vtable} '
                         f'bank=0 view=1 position={position} candidate={candidate} finite=1 rigid=1 words={self.raw} raw={self.raw}\n')
                    self.seats.append(row);self.native.append(row)
            self.native.append(line)
        original=self.fixture.overlay.base.fork
        self.fork={**original,'measurements':{**original['measurements'],'p1':0x110000,'p2':0x120000,'room':0x140000},
                   'raw_engine_receipts':self.engine[1:3],'raw_native_receipts':base_native[:1]}
        self.overlay=live.parse_ui_overlay_evidence(self.engine,self.native,self.trace,2,True,self.fork,160)
        self.assertTrue(self.overlay['pass'],self.overlay['errors'])

    def parse(self,rows=None,enabled=True,slot=2,overlay=None,fork=None):
        return seat.parse_ui_seat_pose_evidence(self.native if rows is None else rows,self.trace,slot,enabled,
            self.overlay if overlay is None else overlay,self.fork if fork is None else fork)

    def replace(self,old,new):
        return [line.replace(old,new) if 'X4 UI SEAT_POSE' in line else line for line in self.native]

    def test_complete_order_bindings_and_nonclaims(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors'])
        self.assertTrue(result['observation_complete']);self.assertTrue(result['pose_observation_complete'])
        self.assertEqual(result['observed_samples'],6)
        self.assertFalse(result['readiness_proven']);self.assertFalse(result['renderer_use_proven'])
        self.assertFalse(result['safe_resume_proven'])
        self.assertTrue(self.parse(rows=[],slot=1)['pass'])
        self.assertFalse(self.parse(slot=1)['pass']);self.assertFalse(self.parse(enabled=False)['pass'])
        self.assertFalse(self.parse(overlay={**self.overlay,'pass':False})['pass'])
        self.assertFalse(self.parse(fork={})['pass'])

    def test_missing_unavailable_null_are_explicit_not_pose_observed(self):
        unavailable='X4 UI SEAT_POSE v=1 tick=9 stage=before role=p1 status=unavailable\n'
        rows=[unavailable if line==self.seats[0] else line for line in self.native]
        result=self.parse(rows);self.assertTrue(result['pass'],result['errors'])
        self.assertFalse(result['observation_complete']);self.assertEqual(result['observed_samples'],5)
        all_missing=[line for line in self.native if 'X4 UI SEAT_POSE' not in line]
        result=self.parse(all_missing);self.assertFalse(result['pass']);self.assertEqual(result['coverage'],'missing')
        rows=self.replace(f'candidate=50000 finite=1 rigid=1 words={self.raw} raw={self.raw}',
                          'candidate=0 finite=-1 rigid=-1 words=- raw=-')
        result=self.parse(rows);self.assertTrue(result['pass'],result['errors'])
        self.assertTrue(result['observation_complete']);self.assertFalse(result['pose_observation_complete'])
        self.assertEqual(result['observed_poses'],3)
        self.assertFalse(self.parse(self.replace('candidate=50000','candidate=0'))['pass'])

    def test_causal_boundary_duplicates_and_schema_adversaries(self):
        rows=self.native.copy();i=rows.index(self.seats[0]);rows[i],rows[i+1]=rows[i+1],rows[i]
        self.assertFalse(self.parse(rows)['pass'])
        for rows in (self.native+[self.seats[0]],self.native[1:]+self.native[:1],
                     [line for line in self.native if line!=self.seats[2]]):
            self.assertFalse(self.parse(rows)['pass'])
        # Pair moved after its VIEWSTATE must reject even though stage order stays intact.
        rows=self.native.copy();i=rows.index(self.seats[0]);pair=rows[i:i+2];del rows[i:i+2];rows[i+1:i+1]=pair
        self.assertFalse(self.parse(rows)['pass'])
        for old,new in (('view=1','view=0'),('position=0','position=4294967296'),
                        ('bank=0','bank=1'),('actor=110000 ','actor=110008 '),('room=140000','room=140008'),
                        ('ship=150000','ship=150008'),('candidate=50000','candidate=50001'),
                        ('controller=10000','controller=0'),('vtable=30000','vtable=0'),
                        ('finite=1','finite=0'),('rigid=1','rigid=0'),
                        ('words='+self.raw,'words=-'),('raw='+self.raw,'raw=-'),
                        ('raw='+self.raw,'raw='+self.raw.upper().replace('3F','3f',1))):
            rows=self.replace(old,new)
            if rows==self.native:continue
            with self.subTest(old=old):self.assertFalse(self.parse(rows)['pass'])
        changed=self.seats[2].replace('ship=160000','ship=160008')
        self.assertFalse(self.parse([changed if line==self.seats[2] else line for line in self.native])['pass'])

    def test_native_user_pointer_bounds(self):
        for old,new in (('ship=160000','ship=1'),('parent=130000','parent=1'),
                        ('controller=10000','controller=1'),('vtable=30000','vtable=1'),
                        ('parent=130000','parent=8000000000000000')):
            result=self.parse(self.replace(old,new))
            self.assertFalse(result['pass'])
            self.assertTrue(any('native user-address bounds' in e for e in result['errors']))
        self.assertFalse(self.parse(overlay={**self.overlay,'callback_count':True})['pass'])

    def test_raw_float32_normalization_finite_and_native_rigid_tolerance(self):
        raw=self.raw[:24]+'80000000'+self.raw[32:]
        rows=self.replace('raw='+self.raw,'raw='+raw)
        self.assertTrue(self.parse(rows)['pass'])
        self.assertFalse(self.parse(self.replace('words='+self.raw,'words='+raw))['pass'])
        for bits,finite,rigid in (('7fc00001',0,0),('7f800000',0,0),('ff800000',0,0)):
            changed=bits+self.raw[8:]
            rows=self.replace('words='+self.raw+' raw='+self.raw,'words='+changed+' raw='+changed)
            rows=[line.replace('finite=1 rigid=1',f'finite={finite} rigid={rigid}') if 'SEAT_POSE' in line else line for line in rows]
            self.assertTrue(self.parse(rows)['pass'])
        for z,expected_rigid in ((1.0001,1),(1.001,0),(-1,0)):
            values=self.matrix.copy();values[14]=z;raw=raw_words(values)
            rows=self.replace('words='+self.raw+' raw='+self.raw,'words='+raw+' raw='+raw)
            rows=[line.replace('rigid=1',f'rigid={expected_rigid}') if 'SEAT_POSE' in line else line for line in rows]
            result=self.parse(rows);self.assertTrue(result['pass'],result['errors'])

    def test_stream_hashes_bind_both_overlay_and_fork_proofs(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);engine=profile/'engine-output.log';stderr=profile/'stderr';trace=profile/'controller-lifecycle.log'
            engine.write_text(''.join(self.engine));stderr.write_text(''.join(self.native));trace.write_text(''.join(self.trace))
            hashes={'engine_sha256':live.sha(engine),'stderr_sha256':live.sha(stderr),'native_fd_sha256':live.sha(trace)}
            overlay={**self.overlay,**hashes};fork={**self.fork,**hashes}
            result=seat.collect_ui_seat_pose_evidence(profile,stderr,2,True,overlay,fork)
            self.assertTrue(result['pass'],result['errors'])
            for path in (engine,stderr,trace):
                original=path.read_text();path.write_text(original+'changed bytes\n')
                self.assertFalse(seat.collect_ui_seat_pose_evidence(profile,stderr,2,True,overlay,fork)['pass'])
                path.write_text(original)
            self.assertFalse(seat.collect_ui_seat_pose_evidence(profile,stderr,2,True,overlay,self.fork)['pass'])
            trace.unlink();result=seat.collect_ui_seat_pose_evidence(profile,stderr,2,True,overlay,fork)
            self.assertFalse(result['pass']);self.assertEqual(result['raw_native_receipts'],self.seats)
