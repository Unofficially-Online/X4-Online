"""Production C transaction failures and actual Lua snapshot routing.

The C fixture simulates Win32 APIs; it executes no game instructions. The
separate native-image C test verifies the pinned PE when the Windows suite runs.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import test_native_gate_probe as fixture
import sys

ROOT = Path(__file__).resolve().parents[1]


class MassJacobiOrderTransactionTests(unittest.TestCase):
    def test_production_transaction_with_fault_injection(self):
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'C compiler required for transaction tests')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'transaction'
            subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-pthread',
                            '-I', str(ROOT / 'src/native/engine'),
                            str(ROOT / 'tests/fixtures/mass_jacobi_order_harness.c'), '-o', str(binary)],
                           check=True, capture_output=True, text=True, timeout=30)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('production transaction failure/lifecycle tests PASS', result.stdout)


class MassJacobiOrderProbeTests(unittest.TestCase):
    def runtime(self, platform='windows', extra=''):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.globals().platform_name = platform
        lua.execute('''
          X4LAN_CONFIG.native_gate_mass_jacobi_order=true
          local gate=package.loadlib('gate')()
          calls={};arm_calls=0;installed=false
          gate.platform=function() return platform_name end
          gate.mass_jacobi_order=function()
            calls[#calls+1]='arm';arm_calls=arm_calls+1
            if refuse_arm then return 0 end
            installed=true;return 1
          end
          gate.mass_jacobi_order_status=function()
            return 'installed='..(installed and '1' or '0')..' restored=0 stage='..(installed and 'armed' or 'pins')
          end
          local load=LoadGame
          LoadGame=function(slot)
            calls[#calls+1]='load'
            assert(platform_name=='linux' or installed,'loaded before native arm')
            return load(slot)
          end
        ''')
        lua.execute(extra)
        return lua

    def boot(self, lua):
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')

    def errors(self, lua):
        return [line for line in lua.globals().logs.values() if 'NATIVE_GATE ERROR ' in line]

    def test_windows_arms_before_load_and_logs_at_stop(self):
        lua = self.runtime()
        self.boot(lua)
        self.assertEqual(list(lua.globals().calls.values()), ['arm', 'load'])
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(self.errors(lua), [])
        lua.execute('state.active=0;state.reason=1;'+fixture.STOP_FIELDS+'update()')
        lines = list(lua.globals().logs.values())
        self.assertTrue(any('MASS_JACOBI_ORDER_ARMED installed=1' in line for line in lines))
        self.assertTrue(any('MASS_JACOBI_ORDER_STOP installed=1' in line for line in lines))

    def test_loading_ui_reload_repeats_native_idempotent_arm(self):
        lua = self.runtime()
        self.boot(lua)
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')
        self.assertEqual(lua.globals().arm_calls, 2)
        self.assertTrue(lua.globals().installed)
        self.assertEqual(self.errors(lua), [])

    def test_linux_explicitly_reports_native_order_without_windows_api(self):
        lua = self.runtime('linux', '''
          local gate=package.loadlib('gate')()
          gate.mass_jacobi_order=function() error('Windows patch called on Linux') end
          gate.mass_jacobi_order_status=function() error('Windows status called on Linux') end
        ''')
        self.boot(lua)
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(lua.globals().arm_calls, 0)
        self.assertEqual(self.errors(lua), [])
        self.assertTrue(any('MASS_JACOBI_ORDER_NOOP linux: native theta order is (0.5*h)/a' in line
                            for line in lua.globals().logs.values()))
        lua.execute('state.active=0;state.reason=1;'+fixture.STOP_FIELDS+'update()')
        self.assertEqual(self.errors(lua), [])

    def test_absent_api_or_refusal_prevents_load(self):
        for extra in ('package.loadlib("gate")().mass_jacobi_order=nil',
                      'package.loadlib("gate")().mass_jacobi_order_status=nil', 'refuse_arm=true'):
            with self.subTest(extra=extra):
                lua = self.runtime(extra=extra)
                self.boot(lua)
                self.assertEqual(lua.globals().loads, 0)
                self.assertTrue(any('mass Jacobi order refused' in line for line in self.errors(lua)))

    def test_boolean_validation_before_boot(self):
        for value in ('0', '1', '"true"', '{}'):
            with self.subTest(value=value):
                lua = self.runtime(extra=f'X4LAN_CONFIG.native_gate_mass_jacobi_order={value}')
                with self.assertRaisesRegex(fixture.LuaError, 'must be a boolean'):
                    lua.execute(fixture.SOURCE)
                self.assertEqual(lua.globals().boots, 0)
                self.assertEqual(lua.globals().loads, 0)

    def test_missing_snapshot_refuses_before_boot(self):
        lua = self.runtime(extra='X4LAN_CONFIG.native_gate_snapshot_sha=nil')
        with self.assertRaisesRegex(fixture.LuaError, 'requires the snapshot boot'):
            lua.execute(fixture.SOURCE)
        self.assertEqual(lua.globals().boots, 0)
        self.assertEqual(lua.globals().arm_calls, 0)
        self.assertEqual(lua.globals().loads, 0)

    def test_default_and_explicit_false_require_no_api(self):
        for value in ('nil', 'false'):
            with self.subTest(value=value):
                lua = self.runtime(extra=f'''
                  X4LAN_CONFIG.native_gate_mass_jacobi_order={value}
                  package.loadlib('gate')().mass_jacobi_order=nil
                  package.loadlib('gate')().mass_jacobi_order_status=nil
                  LoadGame=function() loads=loads+1 end
                ''')
                self.boot(lua)
                self.assertEqual(lua.globals().loads, 1)
                self.assertEqual(lua.globals().arm_calls, 0)
                self.assertEqual(self.errors(lua), [])


class MassJacobiOrderOverlayTests(unittest.TestCase):
    def build(self, *extra, linux=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            gate = root / 'gate'
            gate.write_bytes(b'synthetic gate')
            snapshot = root / 'save.xml.gz'
            snapshot.write_bytes(b'synthetic snapshot')
            output = root / 'overlay'
            command = [sys.executable, str(ROOT / 'tools/dev/build_native_gate_overlay.py'),
                       '--gate-so' if linux else '--gate-dll', str(gate),
                       '--snapshot', str(snapshot), '--mode', 'clock', '--out', str(output), *extra]
            subprocess.run(command, check=True, capture_output=True, text=True)
            return (output / 'ui/config.lua.append').read_text()

    def test_option_default_off_and_explicit_on_both_platforms(self):
        self.assertNotIn('native_gate_mass_jacobi_order', self.build())
        for linux in (False, True):
            with self.subTest(linux=linux):
                config = self.build('--mass-jacobi-order', linux=linux)
                self.assertIn('native_gate_mass_jacobi_order = true', config)


if __name__ == '__main__':
    unittest.main()
