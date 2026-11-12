"""Bounded rolling leaf diagnostics, synthetic only."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine'

@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class RollingLeavesTest(unittest.TestCase):
    def test_real_rolling_hash_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            third = ROOT / 'third_party/monocypher-4.0.2/src'
            binary = Path(tmp) / 'test'
            subprocess.run(['cc','-std=gnu11','-O1','-Wall','-Wextra','-Werror',
                '-Wno-misleading-indentation','-mcx16','-ffp-contract=off','-fasynchronous-unwind-tables',
                '-I',str(ENGINE),'-I',str(ENGINE/'linux'),'-I',str(third),'-I',str(third/'optional'),
                str(ENGINE/'linux/test_gate_world_rolling_leaves.c'),str(third/'monocypher.c'),
                str(third/'optional/monocypher-ed25519.c'),'-ldl','-lpthread','-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_bounds_and_overlay_are_explicit(self):
        import sys
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        import linux_gate_live as live
        import test_lockstep_gate_linux as gate_test
        live.validate_rolling_leaf_ticks([118, 123], 120, 240)
        live.validate_rolling_leaf_ticks(None, None, 240)
        for ticks, slices, end in (([118,118],120,240),([123,118],120,240),([0,2],120,240),
                                   ([1,18001],120,20000),([1,3],None,240),([1,3],120,2),
                                   ([True,3],120,240),([1],120,240)):
            with self.subTest(ticks=ticks), self.assertRaises(SystemExit):
                live.validate_rolling_leaf_ticks(ticks, slices, end)
        fixture = gate_test.Overlay(); fixture.setUp()
        try:
            common = ('--mode','clock','--world-identities','--created-identities','--world-hash-stride','120',
                      '--world-hash-schema','WORLD_IMPORTED_2','--world-log-mode','hashes','--world-rolling-slices','120')
            default = fixture.build(*fixture.snapshot, *common)
            self.assertNotIn('native_gate_world_rolling_leaf_ticks', (default/'ui/config.lua.append').read_text())
            selected = fixture.build(*fixture.snapshot, *common, '--world-rolling-leaf-ticks','118','123')
            self.assertIn('native_gate_world_rolling_leaf_ticks = {118,123}', (selected/'ui/config.lua.append').read_text())
            for pair in (('123','118'),('118','118'),('0','2'),('1','18001')):
                with self.assertRaises(SystemExit):
                    fixture.build(*fixture.snapshot,*common,'--world-rolling-leaf-ticks',*pair)
        finally:
            fixture.tearDown()
