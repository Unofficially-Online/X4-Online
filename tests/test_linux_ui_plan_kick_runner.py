"""Plan-kick parser/admission fixtures only; no native process is started."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import linux_gate_live as live
import linux_ui_plan_kick_evidence as kick
from linux_player_presentation_evidence import validate_presentation_rows
from test_linux_ui_overlay_runner import UiOverlay160Tests


class PlanKickTests(unittest.TestCase):
    def setUp(self):
        self.overlay=UiOverlay160Tests();self.overlay.setUp()
        self.trace=self.overlay.base.fixture.trace
        identity=validate_presentation_rows(self.trace,2)['ui_plan']
        self.rows=[]
        for stage,first,plan,rng in (('before',0,'cockpit',1),('opposite',1,'firstperson',2),
                                      ('restored',0,'cockpit',3)):
            self.rows.append(f'X4 UI PLAN KICK v=1 stage={stage} tick=9 '+
                ' '.join(f'{key}={identity[key]:x}' for key in ('manager','presenter','controller'))+
                f' selector=1 first={first} plan_len={len(plan)} plan={plan.encode().hex()} rng={rng:016x} '+
                'contexts=13 context_hash='+'a'*64+' camera_candidate=10000 camera_mode=0 external_target=20000 target_id=333\n')
        self.rows.append('X4 UI PLAN KICK v=1 stage=result tick=9 success=1 '+
            'rng_before=0000000000000001 rng_after=0000000000000003 rng_restored=0000000000000001 '+
            'context_changed=0 camera_changed=0\n')
        view_body=' '.join(f'{key}={identity[key]:x}' for key in ('manager','presenter','controller'))+\
            ' universe=40000 selector=1 first=0 plan_len=7 plan=636f636b706974 '+\
            'camera_candidate=50000 camera_mode=0 external_target=60000 target_id=333\n'
        self.during='X4 UI VIEWSTATE v=1 tick=9 stage=during status=observed '+view_body
        self.after='X4 UI VIEWSTATE v=1 tick=9 stage=after status=observed '+view_body
        self.native=self.overlay.native[:1]+[self.during]+self.rows+[self.after]+self.overlay.native[1:]
        self.proof=self.overlay.parse(native=self.native)
        self.assertTrue(self.proof['pass'],self.proof['errors'])

    def parse(self,rows=None,proof=None,enabled=True,slot=2):
        return kick.parse_plan_kick_evidence(self.native if rows is None else rows,self.trace,slot,enabled,
                                             self.proof if proof is None else proof)

    def test_exact_sequence_and_disabled_host_are_distinct(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors']);self.assertTrue(result['performed'])
        self.assertFalse(result['readiness_proven']);self.assertFalse(result['context_changed'])
        host=self.parse(rows=[],slot=1);self.assertTrue(host['pass']);self.assertFalse(host['performed'])
        self.assertFalse(self.parse(slot=1)['pass']);self.assertFalse(self.parse(enabled=False)['pass'])
        self.assertFalse(self.parse(proof=dict(self.proof,callback_count=8))['pass'])
        self.assertFalse(self.parse(proof={**self.proof,'pass':False})['pass'])

    def test_missing_duplicate_reordered_unavailable_and_semantic_corruption(self):
        for i in range(2,6):self.assertFalse(self.parse(rows=self.native[:i]+self.native[i+1:])['pass'])
        for rows in (self.native+[self.rows[0]],self.native[1:5]+self.native[:1]+self.native[5:],
                     self.native[:1]+[self.rows[1],self.rows[0],*self.rows[2:]]+self.native[5:],
                     self.native[:1]+self.native[5:]+self.rows):
            self.assertFalse(self.parse(rows=rows)['pass'])
        for index,old,new in ((1,'stage=before','stage=unavailable'),(1,'tick=9','tick=10'),
                (1,'first=0','first=1'),(2,'plan_len=11','plan_len=7'),
                (2,'6669727374706572736f6e','636f636b706974'),(3,'controller=30000','controller=30008'),
                (1,'target_id=333','target_id=18446744073709551616'),
                (4,'rng_restored=0000000000000001','rng_restored=0000000000000002'),
                (4,'rng_after=0000000000000003','rng_after=0000000000000002'),
                (4,'context_changed=0','context_changed=1'),(4,'camera_changed=0','camera_changed=1'),
                (4,'success=1','success=0')):
            rows=self.native.copy();index+=1;rows[index]=rows[index].replace(old,new)
            self.assertNotEqual(rows[index],self.native[index],(old,new))
            with self.subTest(old=old):self.assertFalse(self.parse(rows=rows)['pass'])

    def test_unsigned32_fields_and_viewstate_causal_bracket(self):
        for field in ('contexts','camera_mode'):
            rows=self.native.copy()
            old='contexts=13' if field=='contexts' else 'camera_mode=0'
            rows[2]=rows[2].replace(old,field+'=4294967296')
            result=self.parse(rows);self.assertFalse(result['pass'])
            self.assertTrue(any('unsigned32' in e for e in result['errors']))
        for rows in (self.native[:1]+self.rows+[self.during,self.after]+self.overlay.native[1:],
                     self.native[:1]+[self.during,self.after]+self.rows+self.overlay.native[1:],
                     [line for line in self.native if line!=self.during],
                     [line for line in self.native if line!=self.after],
                     self.native+[self.during],self.native+[self.after]):
            with self.subTest(rows=rows[:3]):self.assertFalse(self.parse(rows)['pass'])

    def test_full_viewstate_schema_identities_and_observation_bounds(self):
        for original in (self.during,self.after):
            for old,new in (('manager=10000','manager=10008'),('presenter=20000','presenter=20008'),
                            ('controller=30000','controller=30008'),('universe=40000 ',''),
                            ('plan_len=7','plan_len=6'),('plan=636f636b706974','plan=zz'),
                            ('selector=1','selector=4294967296'),('camera_mode=0','camera_mode=4294967296'),
                            ('target_id=333','target_id=18446744073709551616'),
                            ('first=0','first=2'),('selector=1','selector=1 selector=1')):
                changed=original.replace(old,new);self.assertNotEqual(changed,original)
                rows=[changed if row==original else row for row in self.native]
                with self.subTest(old=old,stage=original[:60]):self.assertFalse(self.parse(rows)['pass'])
            abbreviated=original.split('status=observed ',1)[0]+'status=observed fixture\n'
            self.assertFalse(self.parse([abbreviated if row==original else row for row in self.native])['pass'])
        # A valid after observation may differ; only the emitted kick rows establish its delta flags.
        after=self.after.replace('camera_mode=0','camera_mode=1').replace('target_id=333','target_id=334')
        result=self.parse([after if row==self.after else row for row in self.native])
        self.assertTrue(result['pass'],result['errors'])
        self.assertEqual(result['viewstates']['after']['target_id'],334)

    def test_truthful_changed_context_and_camera_are_observations(self):
        rows=self.native.copy();rows[4]=rows[4].replace('contexts=13','contexts=14').replace('target_id=333','target_id=334')
        rows[5]=rows[5].replace('context_changed=0 camera_changed=0','context_changed=1 camera_changed=1')
        result=self.parse(rows);self.assertTrue(result['pass'],result['errors'])
        self.assertTrue(result['context_changed']);self.assertTrue(result['camera_changed'])
        self.assertFalse(result['safe_resume_proven'])

    def test_fresh_file_proofs_and_stale_stream_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);engine=profile/'engine-output.log';stderr=profile/'stderr'
            trace=profile/'controller-lifecycle.log'
            engine.write_text(''.join(self.overlay.engine));stderr.write_text(''.join(self.native));trace.write_text(''.join(self.trace))
            proof=dict(self.proof,engine_sha256=live.sha(engine),stderr_sha256=live.sha(stderr),native_fd_sha256=live.sha(trace))
            self.assertTrue(kick.collect_plan_kick_evidence(profile,stderr,2,True,proof)['pass'])
            for path in (engine,stderr,trace):
                original=path.read_text();path.write_text(original+'later bytes\n')
                self.assertFalse(kick.collect_plan_kick_evidence(profile,stderr,2,True,proof)['pass']);path.write_text(original)
            trace.unlink();result=kick.collect_plan_kick_evidence(profile,stderr,2,True,proof)
            self.assertFalse(result['pass']);self.assertEqual(result['raw_native_receipts'],self.rows)

    def test_boolean_dependencies_config_environment_and_source_receipts(self):
        options=dict(self.overlay.base.options(),player_ui_overlay_callbacks=160,player_ui_plan_kick_probe=True)
        self.assertTrue(live.validate_ui_overlay(options,165))
        for value in (None,0,1,'true'):
            self.assertRaises(SystemExit,live.validate_plan_kick,dict(options,player_ui_plan_kick_probe=value))
        for changes in ({'player_ui_overlay_probe':False},{'player_ui_overlay_callbacks':8}):
            self.assertRaises(SystemExit,live.validate_plan_kick,dict(options,**changes))
        self.assertEqual(live.plan_kick_environment(True,2),{'X4_DEV_UI_PLAN_KICK':'1'})
        self.assertEqual(live.plan_kick_environment(True,1),{})
        config='X4LAN_CONFIG.native_gate_linux_ui_plan_kick_probe = true\n'
        live.validate_plan_kick_config(config,True);live.validate_plan_kick_config('',False)
        for text,enabled in ((config,False),('',True),(config+config,True),(config.replace('true','1'),True)):
            self.assertRaises(SystemExit,live.validate_plan_kick_config,text,enabled)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in live.PLAN_KICK_SOURCES:
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(name)
            sources=live.plan_kick_sources(root);live.freeze_source_receipts(root,root/'bundle',sources)
            for path in sources:
                self.assertEqual(live.sha(path),live.sha(root/'bundle/source-receipts'/path.relative_to(root)))
            sources[0].unlink();self.assertRaises(SystemExit,live.plan_kick_sources,root)
