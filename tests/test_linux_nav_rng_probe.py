"""Production Lua navigation RNG integration under a fake native gate.

This verifies platform routing and lifecycle, not native RNG implementation.
"""
import unittest

import test_native_gate_probe as fixture

BASE_OPTIONS = (1 << 46) + 3  # World identities, serial pathing, frame RNG.


class FixtureNavRngProbeTests(unittest.TestCase):
    def runtime(self, platform='linux', extra=''):
        harness = fixture.SnapshotProbeTests()
        harness.setUp()
        lua = harness.lua
        lua.globals().platform_name = platform
        lua.execute('''
          X4LAN_CONFIG.native_gate_serial_pathing=true
          X4LAN_CONFIG.native_gate_seed_rng=true
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_nav_rng=true
          X4LAN_CONFIG.native_gate_nav_cadence='far'
          X4LAN_CONFIG.native_gate_snapshot_fixture={}
          for i=1,12 do
            X4LAN_CONFIG.native_gate_snapshot_fixture[i]=string.format('%016x',i)
          end
          calls={};boot_options={};nav_rng_calls=0;nav_rng_installs=0
          cadence_calls=0;manifest_calls=0;stops=0
          local gate=package.loadlib('gate')()
          gate.platform=function() return platform_name end
          gate.boot=function(slot,port,token,timeout,hash,options)
            boots=boots+1;boot_options[#boot_options+1]=options
            calls[#calls+1]='boot'
            if platform_name=='linux' and reject_unsupported and
              (math.floor(options/65536)%2==1 or math.floor(options/262144)%2==1) then
              return 105
            end
            return 0
          end
          gate.nav_cadence=function(mode)
            assert(mode=='far');cadence_calls=cadence_calls+1
            cadence_armed=true;calls[#calls+1]='cadence';return 1
          end
          gate.nav_cadence_status=function() return 'armed=1 mode=far' end
          gate.nav_rng_fixture_status=function() return 'armed=1 keyed=12 failures=0 scope=fixture' end
          gate.nav_rng_fixture=function()
            nav_rng_calls=nav_rng_calls+1;calls[#calls+1]='fixture_rng'
            assert(cadence_armed,'fixture RNG called before cadence')
            if refuse_rng then return 0 end
            if not nav_rng_armed then nav_rng_installs=nav_rng_installs+1;nav_rng_armed=true end
            return 1
          end
          gate.snapshot_fixture=function(manifest)
            manifest_calls=manifest_calls+1;calls[#calls+1]='manifest'
            assert(#manifest==203)
            return refuse_manifest and 0 or 1
          end
          local request=gate.load_requested
          gate.load_requested=function()
            calls[#calls+1]='load_requested';return request()
          end
          local load=LoadGame
          LoadGame=function(slot) calls[#calls+1]='load';load(slot) end
          local stop=gate.stop
          gate.stop=function() stops=stops+1;return stop() end
        ''')
        lua.execute(extra)
        return lua

    def run_boot(self, lua):
        lua.execute(fixture.SOURCE)
        lua.execute('update();update();update()')

    def errors(self, lua):
        return [line for line in lua.globals().logs.values()
                if 'NATIVE_GATE ERROR ' in line]

    def assert_error(self, lua, reason):
        self.assertTrue(any(reason in line for line in self.errors(lua)), self.errors(lua))
        self.assertEqual(lua.globals().loads, 0)

    def test_windows_keeps_boot_bit_and_never_calls_linux_api(self):
        lua = self.runtime('windows', '''
          package.loadlib('gate')().nav_rng_fixture=function()
            error('Windows called Linux fixture RNG API')
          end
        ''')
        self.run_boot(lua)
        self.assertEqual(list(lua.globals().boot_options.values()), [BASE_OPTIONS + 131072])
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(lua.globals().nav_rng_installs, 0)
        self.assertNotIn('fixture_rng', list(lua.globals().calls.values()))
        self.assertEqual(self.errors(lua), [])

    def test_linux_strips_only_nav_rng_and_arms_after_cadence_before_load(self):
        lua = self.runtime()
        self.run_boot(lua)
        g = lua.globals()
        self.assertEqual(list(g.boot_options.values()), [BASE_OPTIONS])
        self.assertEqual(list(g.calls.values()),
                         ['boot', 'cadence', 'fixture_rng', 'manifest', 'load_requested', 'load'])
        self.assertEqual((g.nav_rng_calls, g.nav_rng_installs, g.loads), (1, 1, 1))
        self.assertEqual(self.errors(lua), [])

    def test_missing_linux_api_refuses_before_boot(self):
        for name in ('nav_rng_fixture', 'nav_rng_fixture_status'):
            with self.subTest(api=name):
                lua = self.runtime(extra=f"package.loadlib('gate')().{name}=nil")
                self.run_boot(lua)
                self.assert_error(lua, 'Linux fixture navigation RNG is unavailable')
                self.assertEqual(lua.globals().boots, 0)
                self.assertEqual(lua.globals().nav_rng_calls, 0)

    def test_refused_linux_api_stops_before_load(self):
        lua = self.runtime(extra='refuse_rng=true')
        self.run_boot(lua)
        self.assert_error(lua, 'Linux fixture navigation RNG refused')
        g = lua.globals()
        self.assertEqual((g.boots, g.cadence_calls, g.nav_rng_calls, g.nav_rng_installs), (1, 1, 1, 0))
        self.assertEqual(g.stops, 1)
        self.assertEqual(g.state.active, 0)

    def test_invalid_manifest_and_nav_key_conflict_prevent_loading(self):
        cases = (
            'X4LAN_CONFIG.native_gate_snapshot_fixture=nil',
            'X4LAN_CONFIG.native_gate_snapshot_fixture[12]=nil',
            "X4LAN_CONFIG.native_gate_snapshot_fixture[13]='000000000000000d'",
            'X4LAN_CONFIG.native_gate_nav_key=true',
        )
        for extra in cases:
            with self.subTest(extra=extra):
                lua = self.runtime(extra=extra)
                self.run_boot(lua)
                self.assert_error(lua, 'Linux fixture navigation RNG is unavailable')
                self.assertEqual(lua.globals().boots, 0)
                self.assertEqual(lua.globals().nav_rng_calls, 0)
        lua = self.runtime(extra='refuse_manifest=true')
        self.run_boot(lua)
        self.assert_error(lua, 'snapshot fixture manifest refused')
        self.assertEqual(lua.globals().manifest_calls, 1)
        self.assertEqual(lua.globals().stops, 1)

    def test_disabled_flag_requires_neither_api_nor_manifest(self):
        for value in ('nil', 'false'):
            with self.subTest(value=value):
                lua = self.runtime(extra=f'''
                  X4LAN_CONFIG.native_gate_nav_rng={value}
                  X4LAN_CONFIG.native_gate_snapshot_fixture=nil
                  package.loadlib('gate')().nav_rng_fixture=nil
                ''')
                self.run_boot(lua)
                self.assertEqual(list(lua.globals().boot_options.values()), [BASE_OPTIONS])
                self.assertEqual(lua.globals().nav_rng_calls, 0)
                self.assertEqual(lua.globals().loads, 1)
                self.assertEqual(self.errors(lua), [])

    def test_ui_reload_repeats_idempotent_native_arm_without_loading_again(self):
        lua = self.runtime()
        self.run_boot(lua)
        lua.execute('state.boot_phase=3;is_menu=false')
        lua.execute(fixture.SOURCE)
        lua.execute('update();update()')
        g = lua.globals()
        self.assertEqual(list(g.boot_options.values()), [BASE_OPTIONS, BASE_OPTIONS])
        self.assertEqual((g.nav_rng_calls, g.nav_rng_installs, g.loads), (2, 1, 1))
        self.assertEqual(g.cadence_calls, 2)
        self.assertEqual(self.errors(lua), [])

    def test_other_unsupported_nav_bits_reach_linux_gate_and_refuse(self):
        for flag, bit in (('native_gate_nav_diag', 65536),
                          ('native_gate_nav_initial_refresh', 262144)):
            with self.subTest(flag=flag):
                lua = self.runtime(extra=f'reject_unsupported=true;X4LAN_CONFIG.{flag}=true')
                self.run_boot(lua)
                self.assertEqual(list(lua.globals().boot_options.values()), [BASE_OPTIONS + bit])
                self.assert_error(lua, 'code=105')
                self.assertEqual(lua.globals().nav_rng_calls, 0)
                self.assertEqual(lua.globals().cadence_calls, 0)

    def test_nav_rng_flag_requires_boolean_before_boot(self):
        for value in ("'true'", '1', '0', '{}'):
            with self.subTest(value=value):
                lua = self.runtime(extra=f'X4LAN_CONFIG.native_gate_nav_rng={value}')
                with self.assertRaisesRegex(fixture.LuaError, 'native_gate_nav_rng must be a boolean'):
                    lua.execute(fixture.SOURCE)
                self.assertEqual(lua.globals().boots, 0)
                self.assertEqual(lua.globals().loads, 0)
                self.assertEqual(lua.globals().nav_rng_calls, 0)


if __name__ == '__main__':
    unittest.main()
