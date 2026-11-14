"""Default-off client2 one-callback overlay plumbing; fixture files/Lua only."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_gate_live as live
import build_native_gate_overlay as overlay
import test_stock_keyboard_replay_overlay as keyboard
import test_linux_presentation_fork as fork_tests


class UiOverlayTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fork_tests.PresentationForkTests();self.fixture.setUp()
        self.fork=self.fixture.parse()
        self.payload=('v=2 status=pass tick=9 updates=1 applies=2 restores=2 before=111 overlay=222 paused=111 resumed=222 restored=111 '
                      'p1=1 p2=2 room=12 object=20008 canonical_restored=1')
        self.engine=self.fixture.engine+['[X4LAN] NATIVE_GATE LINUX_UI_OVERLAY_ARMED tick=8 slot=2\n',
            '[X4LAN] NATIVE_GATE LINUX_UI_OVERLAY_RESULT tick=10 slot=2 '+self.payload+'\n']
        self.stderr=self.fixture.stderr+['X4 UI OVERLAY RESULT '+self.payload+'\n']

    def options(self):
        return dict(self.fixture.options(),player_ui_overlay_probe=True,player_view_query_observer=True,
                    ui_callback_observer=True,independent_keyboard_frames=6,stock_keyboard_replay_last_capture=8)

    def parse(self,engine=None,stderr=None,fork=None,slot=2,enabled=True):
        return live.parse_ui_overlay_evidence(self.engine if engine is None else engine,
            self.stderr if stderr is None else stderr,self.fixture.trace,slot,enabled,self.fork if fork is None else fork)

    def test_missing_bundle_dependencies_and_run_receipts_refuse_before_launch(self):
        with mock.patch.object(live.subprocess,'run') as run,self.assertRaises(SystemExit):
            live.main(['bundle','--zig','/unused','--snapshot','/unused','--out','/unused','--player-ui-overlay-probe'])
        run.assert_not_called()
        with tempfile.TemporaryDirectory() as directory:
            home=Path(directory);bundle=home/'bundle';bundle.mkdir();(bundle/live.MARKER).touch()
            live.freeze_source_receipts(ROOT,bundle,live.view_query_sources(ROOT))
            config=bundle/'extension/x4_lan_lab/ui/config.lua';config.parent.mkdir(parents=True);config.write_text('')
            library=bundle/'player_view_query'/live.PLAYER_VIEW_QUERY;library.parent.mkdir();library.write_bytes(b'fixture preload')
            files={path.relative_to(bundle).as_posix():live.sha(path) for path in bundle.rglob('*') if path.is_file() and path.name!=live.MARKER}
            (bundle/'manifest.json').write_text(json.dumps(dict(self.options(),ticks=220,files=files,
                player_view_query_sha256=live.sha(library),player_view_query_size=library.stat().st_size)))
            user=mock.Mock(pw_dir=str(home),pw_uid=live.os.geteuid())
            with mock.patch('pwd.getpwnam',return_value=user),mock.patch.object(live.subprocess,'Popen') as launch,self.assertRaisesRegex(SystemExit,'UI overlay requires its frozen source receipts'):
                live.main(['run','--root',str(bundle),'--user','fixture','--controller-physics-discard','--controller-physics-frames','6'])
            launch.assert_not_called()

    def test_exact_result_and_client1_isolation(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors'])
        self.assertEqual(result['callback_tick'],9)
        self.assertFalse(result['readiness_proven']);self.assertFalse(result['exclusion_proven'])
        self.assertFalse(result['safe_resume_proven'])
        self.assertEqual(live.ui_overlay_environment(True,2),{'X4_DEV_UI_OVERLAY':'1'})
        self.assertEqual(live.ui_overlay_environment(True,1),{})
        self.assertEqual(live.ui_overlay_environment(False,2),{})
        self.assertTrue(self.parse(engine=[],stderr=[],slot=1)['pass'])
        self.assertFalse(self.parse(slot=1)['pass'])
        self.assertFalse(self.parse(enabled=False)['pass'])
        self.assertFalse(self.parse(engine=['generic UI_OVERLAY unexpected\n'],stderr=[],enabled=False)['pass'])

    def test_v2_bounded_updates_and_exact_counter_relations(self):
        def parse_payload(payload):
            return self.parse(engine=self.engine[:-1]+[self.engine[-1].replace(self.payload,payload)],
                stderr=self.fixture.stderr+['X4 UI OVERLAY RESULT '+payload+'\n'])
        for count in range(1,9):
            payload=self.payload.replace('updates=1 applies=2 restores=2',
                f'updates={count} applies={count+1} restores={count+1}')
            result=parse_payload(payload)
            self.assertTrue(result['pass'],result['errors'])
            self.assertEqual(result['measurements']['updates'],count)
        for old,new in (('v=2','v=1'),('updates=1','updates=0'),('updates=1','updates=9'),
                        ('updates=1','updates=01'),('updates=1','updates=-1'),
                        ('updates=1','updates=x'),('updates=1 ','updates=1 updates=1 '),
                        ('updates=1 ',''),('applies=2','applies=1'),('restores=2','restores=3'),
                        ('applies=2 restores=2','applies=3 restores=3')):
            with self.subTest(old=old,new=new):
                self.assertFalse(parse_payload(self.payload.replace(old,new))['pass'])
        self.assertFalse(self.parse(stderr=self.fixture.stderr+[
            'X4 UI OVERLAY RESULT '+self.payload.replace('updates=1','updates=2')+'\n'])['pass'])

    def test_native_fork_receipt_must_precede_ui_even_with_fresh_hashes(self):
        for callbacks in (1,8):
            eight=UiOverlayEightTests();eight.setUp()
            engine=self.engine if callbacks==1 else eight.engine
            native=self.stderr if callbacks==1 else eight.native
            reordered=native[1:]+native[:1]
            for rows in (reordered,native+native[:1],native[1:]):
                with tempfile.TemporaryDirectory() as directory:
                    profile=Path(directory);engine_path=profile/'engine-output.log'
                    stderr=profile/'stderr';trace=profile/'controller-lifecycle.log'
                    engine_path.write_text(''.join(engine));stderr.write_text(''.join(rows))
                    trace.write_text(''.join(self.fixture.trace))
                    proof=dict(self.fork,engine_sha256=live.sha(engine_path),stderr_sha256=live.sha(stderr),
                               native_fd_sha256=live.sha(trace))
                    result=live.collect_ui_overlay_evidence(profile,stderr,2,True,proof,callbacks)
                    self.assertFalse(result['pass'])
                    self.assertTrue(any('native child fork result' in error for error in result['errors']))

    def test_strict_bool_all_dependencies_and_end_window(self):
        self.assertFalse(live.validate_ui_overlay({},0))
        self.assertTrue(live.validate_network_physics(self.options(),True,6,220))
        for value in (None,0,1,'true'):
            self.assertRaises(SystemExit,live.validate_ui_overlay,dict(self.options(),player_ui_overlay_probe=value),6)
        for key in ('player_presentation_fork_probe','player_presentation_probe','player_view_query_observer',
                    'ui_callback_observer','player_lifetime_sha256'):
            self.assertRaises(SystemExit,live.validate_ui_overlay,dict(self.options(),**{key:False}),6)
        self.assertRaises(SystemExit,live.validate_ui_overlay,self.options(),5)
        self.assertRaises(SystemExit,live.validate_network_physics,self.options(),False,6,220)
        self.assertRaises(SystemExit,live.validate_network_physics,self.options(),True,6,10)
        self.assertRaises(SystemExit,live.validate_network_physics,dict(self.options(),fork_save='7'),True,6,220)
        self.assertFalse(self.parse(enabled=1)['pass'])

    def test_order_missing_duplicate_wrong_phase_and_native_fatal(self):
        for i in range(3,5):self.assertFalse(self.parse(engine=self.engine[:i]+self.engine[i+1:])['pass'])
        for engine in (self.engine+[self.engine[-1]],self.engine[:-2]+list(reversed(self.engine[-2:])),
                       [self.engine[-2]]+self.engine[:-2]+[self.engine[-1]],
                       [x.replace('tick=10','tick=11') for x in self.engine],
                       [x.replace('LINUX_UI_OVERLAY_ARMED tick=8','LINUX_UI_OVERLAY_ARMED tick=9') for x in self.engine],
                       self.engine[:-1]+[self.engine[-1].rstrip('\n')]):
            self.assertFalse(self.parse(engine=engine)['pass'])
        for stderr in ([],self.stderr+[self.stderr[-1]],['X4 UI OVERLAY FATAL reason=escape\n']):
            result=self.parse(stderr=stderr);self.assertFalse(result['pass'])
            self.assertEqual(result['raw_native_receipts'],[x for x in stderr if 'X4 UI OVERLAY' in x])

    def test_fork_identity_and_restoration_contradictions(self):
        for old,new in [('status=pass','status=fail'),('tick=9','tick=8'),('before=111','before=222'),
            *{
            'paused=111':'paused=222','restored=111':'restored=222','overlay=222':'overlay=111',
            'resumed=222':'resumed=111','p1=1 ':'p1=3 ','p2=2 ':'p2=3 ','room=12':'room=13',
            'object=20008':'object=20010','canonical_restored=1':'canonical_restored=0',
            'before=111':'before=18446744073709551616'}.items()]:
            payload=self.payload.replace(old,new)
            with self.subTest(new=new):
                self.assertFalse(self.parse(engine=self.engine[:-1]+[self.engine[-1].replace(self.payload,payload)],
                    stderr=self.fixture.stderr+['X4 UI OVERLAY RESULT '+payload+'\n'])['pass'])
        self.assertFalse(self.parse(fork={})['pass'])
        self.assertFalse(self.parse(fork={**self.fork,'pass':False})['pass'])
        self.assertFalse(self.parse(fork=dict(self.fork,raw_engine_receipts=[]))['pass'])

    def test_frozen_sources_and_config(self):
        line='X4LAN_CONFIG.native_gate_linux_ui_overlay_probe = true\n'
        live.validate_ui_overlay_config('',False);live.validate_ui_overlay_config(line,True)
        for config,enabled in ((line,False),('',True),(line+line,True),(line.replace('true','1'),True)):
            self.assertRaises(SystemExit,live.validate_ui_overlay_config,config,enabled)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in live.UI_OVERLAY_SOURCES:
                source=root/name;source.parent.mkdir(parents=True,exist_ok=True);source.write_text(name)
            sources=live.ui_overlay_sources(root);live.freeze_source_receipts(root,root/'bundle',sources)
            for source in sources:
                data=source.read_bytes();source.unlink();self.assertRaises(SystemExit,live.ui_overlay_sources,root)
                source.write_bytes(data)
                self.assertEqual(live.sha(root/'bundle/source-receipts'/source.relative_to(root)),live.sha(source))

    def test_collection_hash_binding_and_missing_trace_preserves_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);engine=profile/'engine-output.log';stderr=profile/'stderr'
            engine.write_text(''.join(self.engine));stderr.write_text(''.join(self.stderr))
            (profile/'controller-lifecycle.log').write_text(''.join(self.fixture.trace))
            proof=dict(self.fork,engine_sha256=live.sha(engine),stderr_sha256=live.sha(stderr),
                       native_fd_sha256=live.sha(profile/'controller-lifecycle.log'))
            result=live.collect_ui_overlay_evidence(profile,stderr,2,True,proof)
            self.assertTrue(result['pass'],result['errors'])
            self.assertFalse(live.collect_ui_overlay_evidence(profile,stderr,2,True,self.fork)['pass'])
            (profile/'controller-lifecycle.log').unlink();stderr.write_text('X4 UI OVERLAY FATAL reason=escape\n')
            result=live.collect_ui_overlay_evidence(profile,stderr,2,True,proof)
            self.assertFalse(result['pass']);self.assertEqual(result['raw_native_receipts'],['X4 UI OVERLAY FATAL reason=escape\n'])

    def test_changed_lifecycle_or_stderr_cannot_reuse_accepted_fork_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);engine=profile/'engine-output.log';stderr=profile/'stderr'
            trace=profile/'controller-lifecycle.log'
            engine.write_text(''.join(self.engine));stderr.write_text(''.join(self.stderr))
            trace.write_text(''.join(self.fixture.trace))
            proof=dict(self.fork,engine_sha256=live.sha(engine),stderr_sha256=live.sha(stderr),
                       native_fd_sha256=live.sha(trace))
            self.assertTrue(live.collect_ui_overlay_evidence(profile,stderr,2,True,proof)['pass'])
            for path,modified,label in (
                (trace,trace.read_text().replace('runtime=222 ','runtime=223 '),'lifecycle'),
                (stderr,stderr.read_text()+'unrelated later stderr bytes\n','stderr')):
                original=path.read_text();path.write_text(modified)
                result=live.collect_ui_overlay_evidence(profile,stderr,2,True,proof)
                self.assertFalse(result['pass'])
                self.assertTrue(any(f'{label} bytes' in error for error in result['errors']))
                path.write_text(original)
            for key in ('native_fd_sha256','stderr_sha256'):
                missing={k:v for k,v in proof.items() if k!=key}
                self.assertFalse(live.collect_ui_overlay_evidence(profile,stderr,2,True,missing)['pass'])
            self.assertTrue(live.collect_ui_overlay_evidence(profile,stderr,2,True,proof)['pass'])

    def test_overlay_generation_is_opt_in_and_requires_complete_selection(self):
        fixture=keyboard.StockKeyboardReplayOverlayTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        common=('--stock-keyboard-observer','--serial-pathing','--player-factory-probe','--stock-keyboard-replay-probe',
                '--independent-keyboard-frames','6','--stock-keyboard-replay-last-capture','8',
                '--player-presentation-probe','--player-presentation-fork-probe',
                '--player-view-query-observer','--ui-callback-observer')
        flag='X4LAN_CONFIG.native_gate_linux_ui_overlay_probe = true\n'
        self.assertNotIn(flag,(fixture.build(*common)/'ui/config.lua.append').read_text())
        self.assertIn(flag,(fixture.build(*common,'--player-ui-overlay-probe')/'ui/config.lua.append').read_text())
        self.assertRaises(SystemExit,fixture.build,*common[:-1],'--player-ui-overlay-probe')

    def test_mocked_bundle_freezes_flag_and_all_guard_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);snapshot=root/'save.gz';snapshot.write_bytes(keyboard.snapshot_bytes())
            original_copy=live.shutil.copy2;overlay_sources=[ROOT/name for name in live.UI_OVERLAY_SOURCES]
            plan_sources=[ROOT/name for name in live.PLAN_KICK_SOURCES]
            query_sources=[ROOT/name for name in live.SEAT_QUERY_SOURCES]
            for enabled,callbacks,plan,query in ((False,1,False,False),(True,1,False,False),(True,8,False,False),(True,160,False,False),(True,160,True,False),(True,160,True,True)):
                sources=overlay_sources+(plan_sources if plan else [])+(query_sources if query else [])
                out=root/f'{enabled}-{callbacks}-{plan}-{query}';commands=[]
                def copy(source,destination,*args,**kwargs):
                    source=Path(source)
                    if source.name in (live.PLAYER_LIFETIME,live.PLAYER_VIEW_QUERY):
                        Path(destination).write_bytes(b'fixture preload');return destination
                    if source in sources and not source.is_file():
                        Path(destination).write_bytes(('fixture '+source.name).encode());return destination
                    return original_copy(source,destination,*args,**kwargs)
                def command(argv,**kwargs):
                    commands.append(argv)
                    if 'build_native_gate_linux.py' in str(argv[1]):(out/'.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(argv[1]):
                        with mock.patch.object(sys,'argv',argv[1:]):overlay.main()
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in argv else '',returncode=0)
                flag=['--player-ui-overlay-probe'] if enabled else []
                if plan:
                    flag += ['--player-ui-plan-kick-probe']
                if query:
                    flag += ['--player-ui-seat-query-probe']
                if callbacks != 1:
                    flag += ['--player-ui-overlay-callbacks',str(callbacks),
                             '--independent-keyboard-frames',str(callbacks+5)]
                with mock.patch.object(live.subprocess,'run',side_effect=command),mock.patch.object(live.shutil,'copy2',side_effect=copy),mock.patch.object(live,'ui_overlay_sources',return_value=overlay_sources),mock.patch.object(live,'plan_kick_sources',return_value=plan_sources),mock.patch.object(live,'seat_query_sources',return_value=query_sources),contextlib.redirect_stdout(io.StringIO()):
                    live.main(['bundle','--zig','/unused','--snapshot',str(snapshot),'--out',str(out),
                        '--stock-keyboard-observer','--serial-pathing','--player-factory-probe','--stock-keyboard-replay-probe',
                        '--player-lifetime-preload','--player-factory-retain-until','60','--independent-keyboard-frames','6',
                        '--player-presentation-probe','--player-presentation-fork-probe',
                        '--player-view-query-observer','--ui-callback-observer',*flag])
                manifest=json.loads((out/'manifest.json').read_text())
                self.assertIs(manifest['player_ui_overlay_probe'],enabled)
                live.validate_ui_overlay_config((out/'extension/x4_lan_lab/ui/config.lua').read_text(),enabled,callbacks)
                self.assertEqual(manifest['player_ui_overlay_callbacks'],callbacks)
                self.assertIs(manifest['player_ui_plan_kick_probe'],plan)
                self.assertIs(manifest['player_ui_seat_query_probe'],query)
                live.validate_seat_query_config((out/'extension/x4_lan_lab/ui/config.lua').read_text(),query)
                live.validate_plan_kick_config((out/'extension/x4_lan_lab/ui/config.lua').read_text(),plan)
                argv=next(c for c in commands if 'build_native_gate_overlay.py' in str(c[1]))
                self.assertEqual('--player-ui-overlay-probe' in argv,enabled)
                self.assertEqual('--player-ui-overlay-callbacks' in argv,callbacks != 1)
                self.assertEqual('--player-ui-plan-kick-probe' in argv,plan)
                self.assertEqual('--player-ui-seat-query-probe' in argv,query)
                self.assertEqual(manifest['files']['linux_ui_seat_query_evidence.py'],live.sha(out/'linux_ui_seat_query_evidence.py'))
                self.assertEqual(manifest['files']['linux_ui_plan_kick_evidence.py'],live.sha(out/'linux_ui_plan_kick_evidence.py'))
                if enabled:
                    for source in sources:
                        name='source-receipts/'+source.relative_to(ROOT).as_posix()
                        self.assertEqual(manifest['files'][name],live.sha(out/name))


class UiOverlayEightTests(unittest.TestCase):
    def setUp(self):
        self.fixture=UiOverlayTests();self.fixture.setUp()
        self.summary='v=3 status=pass first=9 last=16 count=8'
        self.engine=self.fixture.engine[:-1]+[
            '[X4LAN] NATIVE_GATE LINUX_UI_OVERLAY_RESULT tick=17 slot=2 '+self.summary+'\n']
        body=self.fixture.payload.split('updates=',1)[1]
        self.frames=[f'X4 UI OVERLAY FRAME v=3 tick={8+i} ordinal={i} count=8 updates='+body+'\n'
                     for i in range(1,9)]
        self.native=self.fixture.fixture.stderr+self.frames+['X4 UI OVERLAY RESULT '+self.summary+'\n']

    def parse(self,native=None,engine=None,callbacks=8):
        return live.parse_ui_overlay_evidence(self.engine if engine is None else engine,
            self.native if native is None else native,self.fixture.fixture.trace,2,True,self.fixture.fork,callbacks)

    def test_all_frames_and_summary_bind_without_promoting_individual_rows(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors'])
        self.assertEqual(len(result['frames']),8);self.assertEqual(result['callback_count'],8)
        self.assertFalse(result['readiness_proven'])
        for i in range(len(self.native)):
            with self.subTest(missing=i):self.assertFalse(self.parse(native=self.native[:i]+self.native[i+1:])['pass'])
        self.assertFalse(self.parse(callbacks=1)['pass'])
        self.assertFalse(self.parse(engine=self.fixture.engine)['pass'])

    def test_order_duplicate_counter_identity_and_mode_corruption(self):
        mutations=[self.native+[self.native[0]],list(reversed(self.native)),
                   [self.native[1],self.native[0],*self.native[2:]]]
        for old,new in (('tick=9','tick=10'),('ordinal=1','ordinal=2'),('count=8','count=7'),
                        ('updates=1','updates=0'),('updates=1','updates=9'),('applies=2','applies=3'),
                        ('restores=2','restores=1'),('p1=1 ','p1=3 '),('room=12','room=13'),
                        ('object=20008','object=20010'),('restored=111','restored=222'),
                        ('v=3','v=2'),('canonical_restored=1','canonical_restored=0')):
            mutations.append([self.native[0],self.native[1].replace(old,new),*self.native[2:]])
        mutations.extend([self.native[:-1]+[self.native[-1].replace('last=16','last=15')],
                          self.native[:-1]+[self.native[-1].rstrip('\n')]])
        for native in mutations:
            with self.subTest(native=native[0]):self.assertFalse(self.parse(native=native)['pass'])

    def test_eight_mode_keeps_same_stream_hash_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);engine=profile/'engine-output.log';stderr=profile/'stderr'
            trace=profile/'controller-lifecycle.log'
            engine.write_text(''.join(self.engine));stderr.write_text(''.join(self.native))
            trace.write_text(''.join(self.fixture.fixture.trace))
            proof=dict(self.fixture.fork,engine_sha256=live.sha(engine),stderr_sha256=live.sha(stderr),
                       native_fd_sha256=live.sha(trace))
            self.assertTrue(live.collect_ui_overlay_evidence(profile,stderr,2,True,proof,8)['pass'])
            for path in (engine,stderr,trace):
                original=path.read_text();path.write_text(original+'unrelated bytes\n')
                self.assertFalse(live.collect_ui_overlay_evidence(profile,stderr,2,True,proof,8)['pass'])
                path.write_text(original)

    def test_explicit_admission_config_and_guest_environment(self):
        options=dict(self.fixture.options(),player_ui_overlay_callbacks=8)
        self.assertTrue(live.validate_ui_overlay(options,13))
        for value in (0,2,7,9,True,'8',None):
            self.assertRaises(SystemExit,live.validate_ui_overlay,dict(options,player_ui_overlay_callbacks=value),13)
        self.assertRaises(SystemExit,live.validate_ui_overlay,options,12)
        self.assertRaises(SystemExit,live.validate_ui_overlay,dict(options,player_ui_overlay_probe=False),13)
        self.assertEqual(live.ui_overlay_environment(True,2,8),{'X4_DEV_UI_OVERLAY_EIGHT':'1'})
        self.assertEqual(live.ui_overlay_environment(True,1,8),{})
        config='X4LAN_CONFIG.native_gate_linux_ui_overlay_probe = true\n'
        count='X4LAN_CONFIG.native_gate_linux_ui_overlay_callbacks = 8\n'
        live.validate_ui_overlay_config(config+count,True,8)
        for text,enabled,mode in ((config,True,8),(config+count,True,1),
                                  (config+count+count,True,8),(count,False,8)):
            self.assertRaises(SystemExit,live.validate_ui_overlay_config,text,enabled,mode)

    def test_overlay_builder_requires_mode_and_freezes_count(self):
        fixture=keyboard.StockKeyboardReplayOverlayTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        flags=('--stock-keyboard-observer','--serial-pathing','--player-factory-probe','--stock-keyboard-replay-probe',
               '--independent-keyboard-frames','13','--stock-keyboard-replay-last-capture','15',
               '--player-presentation-probe','--player-presentation-fork-probe','--player-view-query-observer',
               '--ui-callback-observer')
        self.assertRaises(SystemExit,fixture.build,*flags,'--player-ui-overlay-callbacks','8')
        output=fixture.build(*flags,'--player-ui-overlay-probe','--player-ui-overlay-callbacks','8')
        self.assertIn('X4LAN_CONFIG.native_gate_linux_ui_overlay_callbacks = 8\n',
                      (output/'ui/config.lua.append').read_text())


class UiOverlay160Tests(unittest.TestCase):
    def setUp(self):
        self.base=UiOverlayTests();self.base.setUp()
        self.summary='v=3 status=pass first=9 last=168 count=160'
        self.engine=self.base.engine[:-1]+[
            '[X4LAN] NATIVE_GATE LINUX_UI_OVERLAY_RESULT tick=169 slot=2 '+self.summary+'\n']
        body=self.base.payload.split('updates=',1)[1]
        self.native=self.base.fixture.stderr+[
            f'X4 UI OVERLAY FRAME v=3 tick={8+i} ordinal={i} count=160 updates='+body+'\n'
            for i in range(1,161)]+['X4 UI OVERLAY RESULT '+self.summary+'\n']

    def parse(self,native=None,engine=None,callbacks=160):
        return live.parse_ui_overlay_evidence(self.engine if engine is None else engine,
            self.native if native is None else native,self.base.fixture.trace,2,True,self.base.fork,callbacks)

    def test_complete160_identity_counter_and_stream_order(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors'])
        self.assertEqual(len(result['frames']),160)
        self.assertEqual(result['callback_count'],160)
        for i in (0,1,80,160,161):
            with self.subTest(missing=i):self.assertFalse(self.parse(native=self.native[:i]+self.native[i+1:])['pass'])
        for old,new in (('count=160','count=8'),('tick=88','tick=87'),('ordinal=80','ordinal=81'),
                        ('updates=1','updates=9'),('applies=2','applies=3'),('p2=2 ','p2=3 '),
                        ('object=20008','object=20010'),('canonical_restored=1','canonical_restored=0')):
            rows=self.native.copy();rows[80]=rows[80].replace(old,new)
            with self.subTest(old=old):self.assertFalse(self.parse(native=rows)['pass'])
        for rows in (self.native[1:]+self.native[:1],
                     self.native[:80]+[self.native[81],self.native[80]]+self.native[82:],
                     self.native+[self.native[80]],
                     self.native[:-1]+[self.native[-1].replace('last=168','last=167')]):
            self.assertFalse(self.parse(native=rows)['pass'])
        self.assertFalse(self.parse(callbacks=8)['pass'])
        self.assertFalse(self.parse(engine=self.base.engine)['pass'])

    def test160_admission_config_and_guest_environment(self):
        options=dict(self.base.options(),player_ui_overlay_callbacks=160)
        self.assertTrue(live.validate_ui_overlay(options,165))
        self.assertRaises(SystemExit,live.validate_ui_overlay,options,164)
        self.assertRaises(SystemExit,live.validate_ui_overlay,dict(options,player_ui_overlay_probe=False),165)
        for value in (159,161,True,'160'):
            self.assertRaises(SystemExit,live.validate_ui_overlay,dict(options,player_ui_overlay_callbacks=value),165)
        self.assertEqual(live.ui_overlay_environment(True,2,160),{'X4_DEV_UI_OVERLAY_160':'1'})
        self.assertEqual(live.ui_overlay_environment(True,1,160),{})
        config='X4LAN_CONFIG.native_gate_linux_ui_overlay_probe = true\n'
        count='X4LAN_CONFIG.native_gate_linux_ui_overlay_callbacks = 160\n'
        live.validate_ui_overlay_config(config+count,True,160)
        for text,mode in ((config,160),(config+count,8),(config+count+count,160)):
            self.assertRaises(SystemExit,live.validate_ui_overlay_config,text,True,mode)


class UiOverlayLuaTests(unittest.TestCase):
    def runtime(self,slot):
        fixture,lua=fork_tests.lua_fixture.ViewQueryKeyboardLuaTests().runtime(slot)
        lua.execute('''
            cfg.native_gate_linux_presentation_probe=true
            cfg.native_gate_linux_presentation_fork_probe=true
            cfg.native_gate_linux_view_query_observer=true
            cfg.native_gate_linux_ui_callback_observer=true
            cfg.native_gate_linux_ui_overlay_probe=true
            calls={}
            gate.player_presentation_probe=function() return 1 end
            gate.player_presentation_fork_arm=function() return 1 end
            gate.player_presentation_fork_result=function() table.insert(calls,'fork8'); return 'child fixture' end
            gate.player_ui_callback_close=function() return 1 end
            gate.player_ui_callback_result=function() return 'callback fixture' end
            gate.player_ui_overlay_arm=function() table.insert(calls,'arm8'); return 1 end
            gate.player_ui_overlay_result=function() table.insert(calls,'result10'); return 'overlay fixture' end
        ''')
        return fixture,lua

    def test160_mode_reads_only_at169(self):
        fixture,lua=self.runtime('p2')
        lua.execute('cfg.native_gate_linux_ui_overlay_callbacks=160; cfg.native_gate_stock_keyboard_replay_probe.last_capture=166')
        with self.assertRaises(Exception):fixture.create(lua)
        for slot in ('p1','p2'):
            fixture,lua=self.runtime(slot)
            lua.execute('cfg.native_gate_linux_ui_overlay_callbacks=160; cfg.native_gate_stock_keyboard_replay_probe.last_capture=167')
            fixture.start(lua)
            for tick in range(2,170):lua.eval(f'probe.frame({tick})')
            results=[r for r in lua.globals().logs.values() if 'UI_OVERLAY_RESULT' in r]
            self.assertEqual(results,[] if slot=='p1' else ['LINUX_UI_OVERLAY_RESULT tick=169 slot=2 overlay fixture'])

    def test_callback_mode_is_nil_or_exact_numeric_choice(self):
        for value in ('false','true','"8"','0','2','9'):
            fixture,lua=self.runtime('p2')
            lua.execute('cfg.native_gate_linux_ui_overlay_callbacks='+value)
            with self.subTest(value=value),self.assertRaises(Exception):fixture.create(lua)

    def test_eight_mode_reads_only_at17_and_rejects_short_window(self):
        fixture,lua=self.runtime('p2')
        lua.execute('cfg.native_gate_linux_ui_overlay_callbacks=8; cfg.native_gate_stock_keyboard_replay_probe.last_capture=8')
        with self.assertRaises(Exception):fixture.create(lua)
        for slot in ('p1','p2'):
            fixture,lua=self.runtime(slot)
            lua.execute('cfg.native_gate_linux_ui_overlay_callbacks=8; cfg.native_gate_stock_keyboard_replay_probe.last_capture=15')
            fixture.start(lua)
            for tick in range(2,18):lua.eval(f'probe.frame({tick})')
            calls=list(lua.globals().calls.values())
            self.assertEqual(calls,['fork8'] if slot=='p1' else ['fork8','arm8','result10'])
            rows=list(lua.globals().logs.values())
            self.assertEqual([r for r in rows if 'UI_OVERLAY_RESULT' in r],
                [] if slot=='p1' else ['LINUX_UI_OVERLAY_RESULT tick=17 slot=2 overlay fixture'])

    def test_only_client2_arms_after_fork_and_reads_at10(self):
        for slot in ('p1','p2'):
            fixture,lua=self.runtime(slot);fixture.start(lua)
            for tick in range(2,11):lua.eval(f'probe.frame({tick})')
            self.assertEqual(list(lua.globals().calls.values()),['fork8'] if slot=='p1' else ['fork8','arm8','result10'])
            rows=list(lua.globals().logs.values());overlay=[row for row in rows if 'UI_OVERLAY' in row]
            self.assertEqual(overlay,[] if slot=='p1' else ['LINUX_UI_OVERLAY_ARMED tick=8 slot=2','LINUX_UI_OVERLAY_RESULT tick=10 slot=2 overlay fixture'])

    def test_lua_dependency_bool_missing_methods_and_refusals(self):
        for mutation in ('cfg.native_gate_linux_ui_overlay_probe=1','cfg.native_gate_linux_presentation_fork_probe=false',
                         'cfg.native_gate_linux_ui_callback_observer=false','cfg.native_gate_linux_view_query_observer=false',
                         'gate.player_ui_overlay_arm=nil','gate.player_ui_overlay_result=nil',
                         'cfg.native_gate_stock_keyboard_replay_probe.last_capture=7'):
            fixture,lua=self.runtime('p2');lua.execute(mutation)
            with self.subTest(mutation=mutation),self.assertRaises(Exception):fixture.create(lua)
        for method,value,tick in (('arm','0',8),('result','nil',10)):
            fixture,lua=self.runtime('p2');lua.execute(f'gate.player_ui_overlay_{method}=function() return {value} end');fixture.start(lua)
            for before in range(2,tick):lua.eval(f'probe.frame({before})')
            with self.assertRaisesRegex(Exception,'UI overlay '+method+' refused'):lua.eval(f'probe.frame({tick})')


if __name__=='__main__':unittest.main()
