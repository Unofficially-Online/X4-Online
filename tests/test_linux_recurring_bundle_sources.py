"""Freeze diagnostic sources for readiness and disposable physics bundles."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import linux_gate_live as live
import build_native_gate_overlay as overlay
from test_stock_keyboard_replay_overlay import snapshot_bytes


class RecurringBundleSourcesTests(unittest.TestCase):
    def test_requires_both_headers_and_freezes_exact_bytes(self):
        sources=live.recurring_diagnostic_sources(ROOT)
        self.assertEqual([p.name for p in sources],
                         ['gate_player_recurring_receipt.h','gate_player_recurring_pause.h'])
        with tempfile.TemporaryDirectory() as directory:
            repo=Path(directory)
            for source in sources:
                path=repo/source.relative_to(ROOT);path.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(source,path)
            self.assertEqual(len(live.recurring_diagnostic_sources(repo)),2)
            for source in sources:
                path=repo/source.relative_to(ROOT);saved=path.read_bytes();path.unlink()
                self.assertRaises(SystemExit,live.recurring_diagnostic_sources,repo)
                path.write_bytes(saved)
            live.freeze_source_receipts(repo,repo/'bundle',live.recurring_diagnostic_sources(repo))
            for source in sources:
                self.assertEqual((repo/'bundle/source-receipts'/source.relative_to(ROOT)).read_bytes(),source.read_bytes())

    def test_plain_readiness_and_independent_physics_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);snapshot=root/'save.gz';snapshot.write_bytes(snapshot_bytes())
            original_copy=live.shutil.copy2
            for physics in (False,True):
                output=root/str(physics)
                def copy(source,destination,*args,**kwargs):
                    if Path(source).name==live.PLAYER_LIFETIME:
                        Path(destination).write_bytes(b'fixture preload');return destination
                    return original_copy(source,destination,*args,**kwargs)
                def command(arguments,**kwargs):
                    if 'build_native_gate_linux.py' in str(arguments[1]):
                        (output/'.build/x4lan_gate.so').write_bytes(b'fixture gate')
                    elif 'build_native_gate_overlay.py' in str(arguments[1]):
                        with mock.patch.object(sys,'argv',arguments[1:]):overlay.main()
                    return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '',returncode=0)
                flags=[] if not physics else ['--player-factory-probe','--stock-keyboard-replay-probe',
                    '--player-lifetime-preload','--player-factory-retain-until','60','--independent-keyboard-frames','3']
                with mock.patch.object(live.subprocess,'run',side_effect=command), \
                        mock.patch.object(live.shutil,'copy2',side_effect=copy),contextlib.redirect_stdout(io.StringIO()):
                    live.main(['bundle','--zig','/unused','--snapshot',str(snapshot),'--out',str(output),
                               '--stock-keyboard-observer','--serial-pathing',*flags])
                manifest=json.loads((output/'manifest.json').read_text())
                for source in live.recurring_diagnostic_sources(ROOT):
                    relative='source-receipts/'+source.relative_to(ROOT).as_posix()
                    self.assertEqual((output/relative).read_bytes(),source.read_bytes())
                    self.assertEqual(live.sha(output/relative),manifest['files'][relative])

if __name__=='__main__':unittest.main()
