"""Actual probe lifecycle with a stand-in gate; no native engine calls."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaError
import test_native_gate_probe as probe_tests

SOURCE = (Path(__file__).resolve().parents[1] / 'tools/dev/native_gate_probe.lua').read_text()


class PlayerControlBankCensusLuaTests(unittest.TestCase):
    def setUp(self):
        fixture = probe_tests.SnapshotProbeTests()
        fixture.setUp()
        self.lua = fixture.lua
        self.lua.execute('''
            X4LAN_CONFIG.native_gate_player_control_bank_census=true
            X4LAN_CONFIG.native_gate_stock_keyboard_observer=true
            X4LAN_CONFIG.native_gate_serial_pathing=true
            local gate=package.loadlib('gate')()
            gate.platform=function() return 'linux' end
            observer_requests=0;census_requests=0;census_reads=0
            gate.stock_keyboard_observer_request=function()
                assert(state.issued==1)
                observer_requests=observer_requests+1
                return 1
            end
            gate.stock_keyboard_observer_status=function() return 'observer' end
            gate.stock_keyboard_observer_drain=function() return nil end
            gate.player_control_bank_census_request=function()
                assert(state.issued==2)
                census_requests=census_requests+1
                return 1
            end
            gate.player_control_bank_census=function()
                census_reads=census_reads+1
                return 'sample_tick='..state.issued
            end
            state.boot_phase=3;state.issued=1;is_menu=false
        ''')

    def logs(self):
        return list(self.lua.globals().logs.values())

    def test_request_at_two_then_status_every_tick_and_stop(self):
        self.lua.execute(SOURCE)
        self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().observer_requests, 1)
        self.assertEqual(self.lua.globals().census_requests, 0)
        self.lua.execute('state.issued=2;update();update();state.issued=3;update()')
        self.assertEqual(self.lua.globals().census_requests, 1)
        self.assertEqual(self.lua.globals().census_reads, 2)
        self.lua.execute(probe_tests.STOP_FIELDS + 'state.active=0;state.reason=1;update()')
        self.assertEqual(self.lua.globals().census_reads, 3)
        rows = [row for row in self.logs() if 'PLAYER_CONTROL_BANK_CENSUS' in row]
        self.assertEqual(len(rows), 3)
        self.assertIn('tick=2 sample_tick=2', rows[0])
        self.assertIn('tick=3 sample_tick=3', rows[1])
        self.assertIn('PLAYER_CONTROL_BANK_CENSUS_STOP sample_tick=3', rows[2])
        self.assertFalse(any('ERROR' in row for row in self.logs()))

    def test_missing_methods_and_dependencies_refuse_before_request(self):
        cases = (
            "gate.player_control_bank_census_request=nil",
            "gate.player_control_bank_census=nil",
            "gate.platform=function() return 'windows' end",
            "X4LAN_CONFIG.native_gate_stock_keyboard_observer=false",
            "X4LAN_CONFIG.native_gate_serial_pathing=false",
            "X4LAN_CONFIG.native_gate_snapshot_sha=nil",
            "X4LAN_CONFIG.native_gate_stock_keyboard_replay_probe=true",
        )
        for change in cases:
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute("local gate=package.loadlib('gate')();" + change)
                with self.assertRaises(LuaError):
                    self.lua.execute(SOURCE)
                self.assertEqual(self.lua.globals().census_requests, 0)
                self.assertEqual(self.lua.globals().observer_requests, 0)

    def test_default_off_never_calls_census(self):
        self.lua.execute('X4LAN_CONFIG.native_gate_player_control_bank_census=nil')
        self.lua.execute(SOURCE)
        self.lua.execute('update();state.issued=2;update();state.issued=3;update()')
        self.assertEqual(self.lua.globals().census_requests, 0)
        self.assertEqual(self.lua.globals().census_reads, 0)
        self.assertFalse(any('PLAYER_CONTROL_BANK_CENSUS' in row for row in self.logs()))

    def test_refused_request_stops_tick_before_status_or_report(self):
        self.lua.execute("local gate=package.loadlib('gate')();gate.player_control_bank_census_request=function() return 0 end")
        self.lua.execute(SOURCE)
        self.lua.execute('update();state.issued=2;update()')
        self.assertEqual(self.lua.globals().census_reads, 0)
        self.assertEqual(self.lua.globals().reports, 1)
        self.assertTrue(any('player control-bank census request refused' in row for row in self.logs()))
