import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import linux_view_host_capture_bundle as host

ROOT=Path(__file__).resolve().parents[1]
ENGINE=ROOT/'src/native/engine/linux'
class HostViewTests(unittest.TestCase):
    def test_native_fixtures(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ('test_player_stock_view_local_control_proof','test_gate_player_stock_view_host_capture',
                         'test_gate_player_view_host_capture_runtime'):
                with self.subTest(name=name):
                    exe=Path(tmp)/name
                    defines=[] if name=='test_gate_player_view_host_capture_runtime' else ['-D_GNU_SOURCE']
                    subprocess.run(['cc',*defines,'-std=c11','-Wall','-Wextra','-Werror','-pthread',
                                    '-I',str(ENGINE),str(ENGINE/(name+'.c')),'-o',str(exe)],check=True)
                    subprocess.run([str(exe)],check=True,capture_output=True)
                    if name == 'test_gate_player_view_host_capture_runtime':
                        subprocess.run([str(exe), 'replay'], check=True, capture_output=True)

    def test_role_evidence_and_raw_binding(self):
        options={'player_startup_phases':'v1-t40','player_ui_session_frames':180,'independent_keyboard_frames':180,
                 'player_cursor_active_flight':True,'player_view_query_observer':True,
                 'stock_view_fov_observer':True,'view_host_capture_shadow':True}
        self.assertTrue(host.enabled(options))
        self.assertIn('X4_DEV_HOST_VIEW_CAPTURE',host.environment({},True,1))
        self.assertNotIn('X4_DEV_HOST_VIEW_CAPTURE',host.environment({'X4_DEV_HOST_VIEW_CAPTURE':'1'},True,2))
        with tempfile.TemporaryDirectory() as tmp:
            log=Path(tmp)/'stderr'; rows=[]
            for tick in range(38,218):
                present=50<=tick<=213
                rows.append(f'X4 VIEW_HOST_CAPTURE v=1 tick={tick} present={int(present)} bits={int(present)} fov={"3f800000" if present else "00000000"} target=0000000000000000\n')
            log.write_text(''.join(rows));proof={'pass':True,'stderr_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
            self.assertTrue(host.collect(log,proof,options,1,True)['pass'])
            self.assertFalse(host.collect(log,proof,options,2,True)['pass'])
            self.assertFalse(host.collect(log,proof,options,1,False)['pass'])
            log.write_text(''.join(rows).replace('tick=50 present=1','tick=50 present=0'))
            self.assertFalse(host.collect(log,proof,options,1,True)['pass'])

    def test_legacy_version_delegation(self):
        with patch.object(host.previous,'validate_bundle',return_value=True) as old:
            self.assertTrue(host.validate_bundle('/unused',{'view_query_source_version':10},True))
            self.assertEqual(old.call_args.args[1]['view_query_source_version'],10)

    def test_full_source_closure_and_drift(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(host.previous, 'validate_bundle', return_value=True) as old:
            root = Path(tmp)
            files = {}
            for name in host.SOURCES:
                path = root / 'source-receipts' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
                files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            for name in host.HELPERS:
                path = root / name
                path.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
                files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {'view_query_source_version': 11, 'files': files}
            self.assertTrue(host.validate_bundle(root, manifest, True))
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 10)
            (root / 'source-receipts' / host.SOURCES[-1]).write_text('changed')
            with self.assertRaises(ValueError):
                host.validate_bundle(root, manifest, True)

    def test_runner_flag_reaches_bundle(self):
        import linux_gate_live as live
        with patch.object(live, 'bundle') as build:
            live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/snapshot',
                       '--out', '/unused/output', '--view-host-capture-shadow'])
            self.assertTrue(build.call_args.args[0].view_host_capture_shadow)
