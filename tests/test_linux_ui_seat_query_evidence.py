"""Canonical parent-worker seat-query evidence, using synthetic files only."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import linux_gate_live as live
import linux_ui_seat_query_evidence as query
import linux_ui_seat_pose_evidence as census
import test_linux_ui_seat_pose_evidence as seat_tests


class SeatQueryTests(unittest.TestCase):
    def setUp(self):
        self.fixture=seat_tests.SeatPoseTests();self.fixture.setUp()
        self.trace=self.fixture.trace
        self.rows=[]
        for role,actor,ship,room in (('p1','110000','160000','130000'),('p2','120000','150000','140000')):
            self.rows.append(f'X4 UI SEAT_QUERY v=1 tick=9 role={role} return=1 actor={actor} ship={ship} room={room} '+
                'position=999 bank=0 rng_before=0000000000000001 rng_after=0000000000000002 rng_restored=0000000000000001 '+
                'contexts_before=13 hash_before='+'a'*64+' contexts_after=14 hash_after='+'b'*64+
                ' finite=1 rigid=1 raw='+self.fixture.raw+' words='+self.fixture.raw+'\n')
        self.rows.append('X4 UI SEAT_QUERY v=1 tick=9 stage=complete calls=2 p1_holds_released=1\n')
        self.native=[]
        for line in self.fixture.native:
            self.native.append(line)
            if line.startswith('X4 UI VIEWSTATE v=1 tick=9 stage=before '):self.native.extend(self.rows)
        self.fork=self.fixture.fork
        self.overlay=live.parse_ui_overlay_evidence(self.fixture.engine,self.native,self.trace,2,True,self.fork,160)
        self.seats=census.parse_ui_seat_pose_evidence(self.native,self.trace,2,True,self.overlay,self.fork)
        self.assertTrue(self.seats['pass'],self.seats['errors'])

    def parse(self,rows=None,slot=2,enabled=True,overlay=None,seats=None):
        return query.parse_ui_seat_query_evidence(self.native if rows is None else rows,slot,enabled,
            self.overlay if overlay is None else overlay,self.fork,self.seats if seats is None else seats)

    def replace(self,old,new):
        return [line.replace(old,new) if 'X4 UI SEAT_QUERY' in line else line for line in self.native]

    def test_one_two_ship_attempt_retains_effect_observations_without_readiness(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors']);self.assertTrue(result['attempt_complete'])
        self.assertEqual([o['role'] for o in result['observations']],['p1','p2'])
        self.assertTrue(all(o['context_changed'] for o in result['observations']))
        for key in ('readiness_proven','safe_resume_proven','read_only_proven','pose_installation_proven'):
            self.assertFalse(result[key])
        self.assertTrue(self.parse(rows=[],slot=1)['pass']);self.assertFalse(self.parse(slot=1)['pass'])
        self.assertFalse(self.parse(enabled=False)['pass'])
        self.assertFalse(self.parse(overlay={**self.overlay,'pass':False})['pass'])
        self.assertFalse(self.parse(seats={**self.seats,'pass':False})['pass'])

    def test_missing_duplicate_reordered_and_moved_receipts(self):
        for target in self.rows:
            self.assertFalse(self.parse([line for line in self.native if line!=target])['pass'])
        self.assertFalse(self.parse(self.native+[self.rows[0]])['pass'])
        index=self.native.index(self.rows[0]);rows=self.native.copy();rows[index],rows[index+1]=rows[index+1],rows[index]
        self.assertFalse(self.parse(rows)['pass'])
        stripped=[line for line in self.native if 'X4 UI SEAT_QUERY' not in line]
        self.assertFalse(self.parse(self.rows+stripped)['pass'])
        self.assertFalse(self.parse(stripped+self.rows)['pass'])

    def test_source_schema_identity_return_rng_and_flags(self):
        for old,new in (('return=1','return=0'),('return=1','return=2'),('tick=9','tick=8'),
                        ('actor=110000','actor=110008'),('ship=160000','ship=160008'),
                        ('room=130000','room=130008'),('bank=0','bank=1'),
                        ('position=999','position=4294967296'),('contexts_after=14','contexts_after=4294967296'),
                        ('rng_restored=0000000000000001','rng_restored=0000000000000002'),
                        ('finite=1','finite=0'),('rigid=1','rigid=0'),
                        ('raw='+self.fixture.raw,'raw=-'),('words='+self.fixture.raw,'words=-'),
                        ('p1_holds_released=1','p1_holds_released=0'),('calls=2','calls=1')):
            with self.subTest(old=old):self.assertFalse(self.parse(self.replace(old,new))['pass'])
        raw='7fc00001'+self.fixture.raw[8:]
        rows=self.replace('raw='+self.fixture.raw+' words='+self.fixture.raw,'raw='+raw+' words='+raw)
        rows=[line.replace('finite=1 rigid=1','finite=0 rigid=0') if 'SEAT_QUERY' in line else line for line in rows]
        self.assertTrue(self.parse(rows)['pass'])
        signed=self.fixture.raw[:24]+'80000000'+self.fixture.raw[32:]
        self.assertTrue(self.parse(self.replace('raw='+self.fixture.raw,'raw='+signed))['pass'])

    def test_hashes_bind_overlay_fork_and_recomputed_census(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);engine=profile/'engine-output.log';stderr=profile/'stderr';trace=profile/'controller-lifecycle.log'
            engine.write_text(''.join(self.fixture.engine));stderr.write_text(''.join(self.native));trace.write_text(''.join(self.trace))
            hashes={'engine_sha256':live.sha(engine),'stderr_sha256':live.sha(stderr),'native_fd_sha256':live.sha(trace)}
            overlay={**self.overlay,**hashes};fork={**self.fork,**hashes}
            result=query.collect_ui_seat_query_evidence(profile,stderr,2,True,overlay,fork)
            self.assertTrue(result['pass'],result['errors'])
            for path in (engine,stderr,trace):
                original=path.read_text();path.write_text(original+'changed bytes\n')
                self.assertFalse(query.collect_ui_seat_query_evidence(profile,stderr,2,True,overlay,fork)['pass']);path.write_text(original)
            trace.unlink();result=query.collect_ui_seat_query_evidence(profile,stderr,2,True,overlay,fork)
            self.assertFalse(result['pass']);self.assertEqual(result['raw_native_receipts'],self.rows)

    def test_admission_config_and_source_freeze_are_explicit(self):
        options={'player_ui_seat_query_probe':True,'player_ui_overlay_probe':True,'player_ui_overlay_callbacks':160}
        self.assertTrue(live.validate_seat_query(options));self.assertFalse(live.validate_seat_query({}))
        for value in (None,0,1,'true'):
            self.assertRaises(SystemExit,live.validate_seat_query,dict(options,player_ui_seat_query_probe=value))
        for changes in ({'player_ui_overlay_probe':False},{'player_ui_overlay_callbacks':8}):
            self.assertRaises(SystemExit,live.validate_seat_query,dict(options,**changes))
        self.assertEqual(live.seat_query_environment(True,2),{'X4_DEV_UI_SEAT_QUERY':'1'})
        self.assertEqual(live.seat_query_environment(True,1),{})
        config='X4LAN_CONFIG.native_gate_linux_ui_seat_query_probe = true\n'
        live.validate_seat_query_config(config,True);live.validate_seat_query_config('',False)
        for text,enabled in (('',True),(config,False),(config+config,True),(config.replace('true','1'),True)):
            self.assertRaises(SystemExit,live.validate_seat_query_config,text,enabled)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in live.SEAT_QUERY_SOURCES:
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(name)
            sources=live.seat_query_sources(root);live.freeze_source_receipts(root,root/'bundle',sources)
            for path in sources:self.assertEqual(live.sha(path),live.sha(root/'bundle/source-receipts'/path.relative_to(root)))
            sources[0].unlink();self.assertRaises(SystemExit,live.seat_query_sources,root)
