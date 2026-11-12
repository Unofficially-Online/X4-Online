"""Default-off disposable child presentation plumbing; no X4 process is launched."""
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
from test_linux_presentation_probe import receipt
import test_linux_view_query_lua as lua_fixture


class PresentationForkTests(unittest.TestCase):
    def options(self):
        return dict(player_presentation_fork_probe=True, player_presentation_probe=True,
                    independent_keyboard_frames=5, independent_keyboard_mode='independent_keyboard',
                    stock_keyboard_observer=True, serial_pathing=True, player_factory_probe=True,
                    stock_keyboard_replay_probe=True, player_lifetime_sha256='fixture',
                    stock_keyboard_replay_last_capture=7)

    def setUp(self):
        self.payload = ('v=1 status=pass parent_unchanged=1 child_reaped=1 lease_held=0 '
            'p1=1 p2=2 room=12 p1_ship=111 p2_ship=222 before=111 overlay=222 restored=111 '
            'live=12 cached=12 refs=10,11,10 marker=2,2 release_attempts=1')
        self.engine = ['[X4LAN] NATIVE_GATE LINUX_PRESENTATION_PROBE tick=7 slot=2\n',
                       '[X4LAN] NATIVE_GATE LINUX_PRESENTATION_FORK_ARMED tick=7 slot=2\n',
                       '[X4LAN] NATIVE_GATE LINUX_PRESENTATION_FORK_RESULT tick=8 slot=2 '+self.payload+'\n']
        self.stderr = ['X4 PRESENTATION FORK RESULT '+self.payload+'\n']
        self.trace = [line.replace('runtime=0 ', 'runtime=111 ') for line in receipt(2)] + [
            'X4LAN LIFECYCLE_DISCARD physics=body stage=before index=0 ship=123 runtime=222 present=1\n']

    def parse(self, engine=None, stderr=None, trace=None):
        return live.parse_presentation_fork_evidence(self.engine if engine is None else engine,
            self.stderr if stderr is None else stderr, self.trace if trace is None else trace, 2)

    def test_admission_bool_dependencies_and_wrong_run_mode(self):
        self.assertFalse(live.validate_presentation_fork({}, 0))
        self.assertTrue(live.validate_network_physics(self.options(), True, 5, 220))
        self.assertRaises(SystemExit,live.validate_presentation_fork,self.options(),4)
        for key,value in [('player_presentation_fork_probe', v) for v in (None,0,1,'true')] + [
                ('player_presentation_probe',False),('player_lifetime_sha256',None),
                ('fork_save',{'tick':7}),('independent_keyboard_frames',4)]:
            with self.subTest(key=key, value=value), self.assertRaises(SystemExit):
                live.validate_network_physics(dict(self.options(), **{key:value}), True, 5, 220)
        self.assertRaises(SystemExit,live.validate_network_physics,self.options(),False,5,220)
        with mock.patch.object(live.subprocess,'run') as run, self.assertRaises(SystemExit):
            live.main(['bundle','--zig','/unused','--snapshot','/unused','--out','/unused','--player-presentation-fork-probe'])
        run.assert_not_called()

    def test_config_default_off_duplicate_and_overlay_bounds(self):
        line='X4LAN_CONFIG.native_gate_linux_presentation_fork_probe = true\n'
        live.validate_presentation_fork_config('',False)
        live.validate_presentation_fork_config(line,True)
        for config,enabled in ((line,False),('',True),(line+line,True),(line.replace('true','1'),True)):
            self.assertRaises(SystemExit,live.validate_presentation_fork_config,config,enabled)
        fixture=keyboard.StockKeyboardReplayOverlayTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        flags=('--stock-keyboard-observer','--serial-pathing','--player-factory-probe',
               '--stock-keyboard-replay-probe','--independent-keyboard-frames','5',
               '--stock-keyboard-replay-last-capture','7')
        self.assertNotIn(line,(fixture.build(*flags)/'ui/config.lua.append').read_text())
        output=fixture.build(*flags,'--player-presentation-probe','--player-presentation-fork-probe')
        self.assertIn(line,(output/'ui/config.lua.append').read_text())
        for extra in (('--player-presentation-fork-probe',),
                      ('--player-presentation-probe','--player-presentation-fork-probe','--fork-save','7')):
            self.assertRaises(SystemExit,fixture.build,*flags,*extra)

    def test_all_sources_required_and_frozen_exactly(self):
        with tempfile.TemporaryDirectory() as directory:
            repo=Path(directory)
            self.assertRaises(SystemExit,live.presentation_fork_sources,repo)
            names=('player_presentation_fork_probe.h','player_presentation_overlay.h',
                   'gate_player_presentation_fork.h','gate_fork_save.h','gate_linux.c')
            for name in names:
                p=repo/'src/native/engine/linux'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(name)
            sources=live.presentation_fork_sources(repo)
            live.freeze_source_receipts(repo,repo/'bundle',sources)
            for source in sources:
                frozen=repo/'bundle/source-receipts'/source.relative_to(repo)
                self.assertEqual(source.read_bytes(),frozen.read_bytes())
                data=source.read_bytes();source.unlink()
                self.assertRaises(SystemExit,live.presentation_fork_sources,repo)
                source.write_bytes(data)

    def test_owned_environment_collection_and_disabled_evidence(self):
        self.assertEqual(live.presentation_fork_environment(False), {})
        self.assertEqual(live.presentation_fork_environment(True), {'X4_DEV_PRESENTATION_FORK':'1'})
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);stderr=profile/'stderr'
            self.assertFalse(live.collect_presentation_fork_evidence(profile,stderr,2,True)['pass'])
            engine=profile/'engine-output.log';engine.write_text(''.join(self.engine))
            stderr.write_text(''.join(self.stderr))
            trace=profile/'controller-lifecycle.log';trace.write_text(''.join(self.trace))
            result=live.collect_presentation_fork_evidence(profile,stderr,2,True)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['native_fd_sha256'],live.sha(trace))
            self.assertFalse(live.collect_presentation_fork_evidence(profile,stderr,2,False)['pass'])
            engine.write_text('unrelated\n');stderr.write_text('unrelated\n')
            self.assertTrue(live.collect_presentation_fork_evidence(profile,stderr,2,False)['pass'])

    def test_disabled_collector_rejects_generic_fork_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory);stderr=profile/'stderr'
            (profile/'engine-output.log').write_text('generic PRESENTATION_FORK unexpected\n')
            stderr.write_text('unrelated\n')
            result=live.collect_presentation_fork_evidence(profile,stderr,2,False)
            self.assertFalse(result['pass'])
            self.assertIn('unexpected presentation fork evidence while disabled',result['errors'])

    def test_missing_run_receipt_refused_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            home=Path(directory);bundle=home/'bundle';bundle.mkdir()
            (bundle/live.MARKER).touch()
            (bundle/'manifest.json').write_text(json.dumps(dict(self.options(),ticks=220,files={})))
            user=mock.Mock(pw_dir=str(home),pw_uid=live.os.geteuid())
            with mock.patch('pwd.getpwnam',return_value=user), mock.patch.object(live.subprocess,'Popen') as launch, mock.patch.object(live.subprocess,'run') as run, self.assertRaisesRegex(SystemExit,'frozen source receipts'):
                live.main(['run','--root',str(bundle),'--user','fixture','--controller-physics-discard','--controller-physics-frames','5'])
            launch.assert_not_called();run.assert_not_called()

    def test_bundle_freezes_flag_config_library_dependencies_and_forwarding(self):
        # C adapter belongs to the other agent; mock only compilation and missing
        # adapter files, exercising the real overlay/config/inventory writers.
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);snapshot=root/'save.gz';snapshot.write_bytes(keyboard.snapshot_bytes())
            original_copy=live.shutil.copy2
            sources=[ROOT/'src/native/engine/linux'/name for name in
                     ('player_presentation_fork_probe.h','player_presentation_overlay.h',
                      'gate_player_presentation_fork.h','gate_fork_save.h','gate_linux.c')]
            for enabled in (False,True):
                out=root/str(enabled);commands=[]
                def copy(source,destination,*args,**kwargs):
                    source=Path(source)
                    if source.name==live.PLAYER_LIFETIME:
                        Path(destination).write_bytes(b'fixture lifetime');return destination
                    if source in sources and not source.is_file():
                        Path(destination).write_bytes(('fixture '+source.name).encode());return destination
                    return original_copy(source,destination,*args,**kwargs)
                def command(argv,**kwargs):
                    commands.append(argv)
                    if 'build_native_gate_linux.py' in str(argv[1]):
                        (out/'.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(argv[1]):
                        with mock.patch.object(sys,'argv',argv[1:]):overlay.main()
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in argv else '',returncode=0)
                flag=['--player-presentation-fork-probe'] if enabled else []
                with mock.patch.object(live.subprocess,'run',side_effect=command), mock.patch.object(live.shutil,'copy2',side_effect=copy), mock.patch.object(live,'presentation_fork_sources',return_value=sources), contextlib.redirect_stdout(io.StringIO()):
                    live.main(['bundle','--zig','/unused','--snapshot',str(snapshot),'--out',str(out),
                               '--stock-keyboard-observer','--serial-pathing','--player-factory-probe',
                               '--stock-keyboard-replay-probe','--player-lifetime-preload',
                               '--player-factory-retain-until','60','--independent-keyboard-frames','5',
                               '--player-presentation-probe',*flag])
                manifest=json.loads((out/'manifest.json').read_text())
                self.assertIs(manifest['player_presentation_fork_probe'],enabled)
                live.validate_presentation_fork_config((out/'extension/x4_lan_lab/ui/config.lua').read_text(),enabled)
                argv=next(c for c in commands if 'build_native_gate_overlay.py' in str(c[1]))
                self.assertEqual('--player-presentation-fork-probe' in argv,enabled)
                if enabled:
                    for source in sources:
                        name='source-receipts/'+source.relative_to(ROOT).as_posix()
                        self.assertEqual(manifest['files'][name],live.sha(out/name))

    def test_exact_child_receipt_binds_existing_identities(self):
        result=self.parse();self.assertTrue(result['pass'],result['errors'])
        self.assertFalse(result['readiness_proven']);self.assertFalse(result['safe_resume_proven'])
        self.assertEqual(result['measurements']['overlay'],222)
        self.assertEqual(result['raw_native_receipts'],self.stderr)

    def test_causal_receipt_missing_duplicate_reordered_and_native_mismatch(self):
        for i in range(3):
            self.assertFalse(self.parse(engine=self.engine[:i]+self.engine[i+1:])['pass'])
        for lines in (self.engine+[self.engine[1]],list(reversed(self.engine)),
                      [self.engine[1],self.engine[0],self.engine[2]],
                      [x.replace('slot=2','slot=1') for x in self.engine],
                      [x.replace('tick=8','tick=9') for x in self.engine],
                      [self.engine[0],self.engine[1],self.engine[2].rstrip('\n')],
                      [self.engine[0],self.engine[1],self.engine[2]+'LINUX_PRESENTATION_FORK partial\n']):
            self.assertFalse(self.parse(engine=lines)['pass'])
        for stderr in ([],self.stderr*2,['X4 PRESENTATION FORK RESULT wrong\n'],[self.stderr[0].rstrip('\n')]):
            self.assertFalse(self.parse(stderr=stderr)['pass'])

    def test_rollback_refcount_child_and_identity_contradictions_refuse(self):
        mutations={'status=pass':'status=fail','parent_unchanged=1':'parent_unchanged=0',
            'child_reaped=1':'child_reaped=0','lease_held=0':'lease_held=1','p1=1 ':'p1=3 ',
            'p2=2 ':'p2=3 ','room=12 ':'room=13 ','p1_ship=111':'p1_ship=112',
            'p2_ship=222':'p2_ship=223','before=111':'before=222','overlay=222':'overlay=111',
            'restored=111':'restored=222','live=12':'live=13','cached=12':'cached=13',
            'refs=10,11,10':'refs=10,11,11','marker=2,2':'marker=2,3',
            'release_attempts=1':'release_attempts=2','p1_ship=111 ':'p1_ship=18446744073709551616 '}
        for old,new in mutations.items():
            payload=self.payload.replace(old,new)
            with self.subTest(mutation=new):
                self.assertFalse(self.parse(engine=self.engine[:2]+[self.engine[2].replace(self.payload,payload)],
                    stderr=['X4 PRESENTATION FORK RESULT '+payload+'\n'])['pass'])
        self.assertFalse(self.parse(trace=[])['pass'])
        self.assertFalse(self.parse(trace=[x.replace('runtime=222 ','runtime=223 ') for x in self.trace])['pass'])

    def test_single_reference_room_refused_even_with_matching_receipts(self):
        payload=self.payload.replace('refs=10,11,10','refs=1,2,1')
        result=self.parse(engine=self.engine[:2]+[self.engine[2].replace(self.payload,payload)],
                          stderr=['X4 PRESENTATION FORK RESULT '+payload+'\n'])
        self.assertFalse(result['pass'])
        self.assertIn('presentation fork reference or marker imbalance',result['errors'])

    def test_untrusted_body_runtime_is_bounded_and_identity_checked(self):
        for runtime in ('-1','not-a-number','9'*5000,'18446744073709551616',''):
            with self.subTest(runtime=runtime[:25]):
                result=self.parse(trace=[line.replace('runtime=222 ', 'runtime='+runtime+' ') for line in self.trace])
                self.assertFalse(result['pass'])
                self.assertTrue(any('runtime' in error for error in result['errors']))
        for suffix in (' role=p1',' slot=1',' role=p2 role=p2',' runtime=222'):
            trace=[line.replace('present=1\n','present=1'+suffix+'\n') for line in self.trace]
            self.assertFalse(self.parse(trace=trace)['pass'])
        trace=[line.replace('present=1\n','present=1 role=p2 slot=2\n') for line in self.trace]
        self.assertTrue(self.parse(trace=trace)['pass'])

    def test_fork_refusal_retained_before_truncated_observer_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);games=[]
            refusal='X4 PRESENTATION FORK ARMREFUSED reason=pins stage=pin\n'
            for slot in (1,2):
                profile=run/f'profile{slot}';profile.mkdir();games.append((None,profile))
                (profile/'engine-output.log').write_text(f'[X4LAN] NATIVE_GATE LINUX_VIEW_QUERY_ARMED tick=7 slot={slot}\n')
                (run/f'game{slot}-stderr.log').write_text('X4 VIEW QUERY PRELOAD ACTIVE version=1\n'+refusal)
                # Client2 also has no lifecycle FD: its refusal must still survive.
                if slot==1:(profile/'controller-lifecycle.log').write_text('X4LAN partial trace')
            result={'pass':False,'error':'original native arm refusal'}
            with self.assertRaisesRegex(ValueError,'invalid UI callback evidence') as raised:
                live.collect_preload_evidence(result,[1,2],games,run,{},view_query_enabled=True,
                    ui_callback_enabled=True,lifetime_preload=True,fork_enabled=True,recurring=True,physics_frames=180)
            self.assertNotIn('invalid presentation fork evidence',str(raised.exception))
            self.assertEqual(result['error'],'original native arm refusal')
            self.assertFalse(result['pass'])
            self.assertEqual(set(result['presentation_fork_evidence']),{'1','2'})
            for evidence in result['presentation_fork_evidence'].values():
                self.assertFalse(evidence['pass'])
                self.assertEqual(evidence['raw_native_receipts'],[refusal])
            self.assertFalse(result['player_view_query_evidence']['1']['pass'])

    def test_malformed_fork_result_still_rejects_without_observer_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);profile=run/'profile';profile.mkdir()
            (profile/'engine-output.log').write_text(''.join(self.engine).replace('child_reaped=1','child_reaped=0'))
            (profile/'controller-lifecycle.log').write_text(''.join(self.trace))
            (run/'game2-stderr.log').write_text(''.join(self.stderr).replace('child_reaped=1','child_reaped=0'))
            result={'pass':False,'error':'original failure'}
            with self.assertRaisesRegex(ValueError,'invalid presentation fork evidence'):
                live.collect_preload_evidence(result,[2],[(None,profile)],run,{},view_query_enabled=False,
                    ui_callback_enabled=False,lifetime_preload=False,fork_enabled=True,recurring=True,physics_frames=180)
            self.assertFalse(result['presentation_fork_evidence']['2']['pass'])
            self.assertIn('child_reaped=0',result['presentation_fork_evidence']['2']['raw_native_receipts'][0])
            self.assertEqual(result['error'],'original failure')
            self.assertFalse(result['pass'])


class PresentationForkLuaTests(unittest.TestCase):
    def runtime(self):
        fixture,lua=lua_fixture.ViewQueryKeyboardLuaTests().runtime()
        lua.execute('''
            cfg.native_gate_linux_presentation_probe=true
            cfg.native_gate_linux_presentation_fork_probe=true
            calls={}
            gate.player_presentation_probe=function() table.insert(calls,'presentation'); return 1 end
            gate.player_presentation_fork_arm=function() table.insert(calls,'arm'); return 1 end
            gate.player_presentation_fork_result=function() table.insert(calls,'result'); return 'v=1 child fixture' end
        ''')
        return fixture,lua

    def test_order_default_off_and_native_refusals(self):
        fixture,lua=self.runtime();fixture.start(lua)
        for tick in range(2,9):lua.eval(f'probe.frame({tick})')
        self.assertEqual(list(lua.globals().calls.values()),['presentation','arm','result'])
        logs=list(lua.globals().logs.values())
        self.assertIn('LINUX_PRESENTATION_FORK_ARMED tick=7 slot=1',logs)
        self.assertIn('LINUX_PRESENTATION_FORK_RESULT tick=8 slot=1 v=1 child fixture',logs)
        fixture,lua=self.runtime();lua.execute('cfg.native_gate_linux_presentation_fork_probe=nil');fixture.start(lua)
        lua.eval('probe.frame(7)');lua.eval('probe.frame(8)')
        self.assertEqual(list(lua.globals().calls.values()),['presentation'])
        for method,ret,tick in [('arm','0',7),('result','nil',8)]:
            fixture,lua=self.runtime();lua.execute(f'gate.player_presentation_fork_{method}=function() return {ret} end');fixture.start(lua)
            if tick==8:lua.eval('probe.frame(7)')
            with self.assertRaisesRegex(Exception,'fork '+method+' refused'):lua.eval(f'probe.frame({tick})')

    def test_bad_lua_dependencies_do_not_activate(self):
        for mutation in ('cfg.native_gate_linux_presentation_fork_probe=1',
                         'cfg.native_gate_linux_presentation_probe=false','cfg.native_gate_fork_save={tick=7}',
                         'gate.player_presentation_fork_arm=nil','gate.player_presentation_fork_result=nil',
                         'cfg.native_gate_stock_keyboard_replay_probe.last_capture=6'):
            fixture,lua=self.runtime();lua.execute(mutation)
            with self.subTest(mutation=mutation),self.assertRaises(Exception):fixture.create(lua)


if __name__=='__main__':unittest.main()
