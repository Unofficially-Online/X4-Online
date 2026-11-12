"""Presentation diagnostic admission and strict receipt validation, without X4."""
import sys
import subprocess
import shutil
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live
from linux_player_presentation_evidence import validate_presentation_rows
from linux_player_frames_evidence import parse_recurring_physics_evidence
import test_stock_keyboard_replay_overlay as keyboard
from test_linux_player_frames_evidence import valid_frames, worker_rows
from test_linux_player_network_evidence import record
from linux_player_network_evidence import parse_network_physics_evidence


def receipt(slot=2):
    prefix = 'X4LAN PRESENTATION_PROBE v=1 '
    row = ('stage={stage} tick=7 slot={slot} player={player} parent=12 vector=0,0,0 '
           'position=0 bank=0 cache_parent=0 live_called=0 live=0 cached_called=0 '
           'cached=0 omitted=1 refs=0,0,0 marker=0,0\n')
    return [prefix + row.format(stage='p1', slot=slot, player='1'),
            prefix + row.format(stage='p2', slot=slot, player='2'),
            prefix + f'stage=ui_plan tick=7 slot={slot} manager=10000 presenter=20000 presenter_vtable=3ab4600 controller=30000 controller_vtable=3ab4060 flag=0 authenticated=1\n',
            prefix + f'stage=occupied tick=7 slot={slot} runtime=0 observational=1\n',
            prefix + f'stage=result tick=7 slot={slot} success=1 reason=ok\n']


class PresentationProbeTest(unittest.TestCase):
    def test_admission_and_config(self):
        self.assertFalse(live.validate_presentation_probe({}, 0))
        self.assertTrue(live.validate_presentation_probe({'player_presentation_probe': True}, 4))
        for value in (None, 0, 1, 'true'):
            with self.assertRaises(SystemExit):
                live.validate_presentation_probe({'player_presentation_probe': value}, 4)
        with self.assertRaises(SystemExit):
            live.validate_presentation_probe({'player_presentation_probe': True}, 3)
        line = 'X4LAN_CONFIG.native_gate_linux_presentation_probe = true\n'
        live.validate_presentation_config(line, True)
        live.validate_presentation_config('', False)
        for config, enabled in (('', True), (line, False), (line + line, True)):
            with self.assertRaises(SystemExit):
                live.validate_presentation_config(config, enabled)

    def test_overlay_is_opt_in_and_requires_four_frames(self):
        fixture = keyboard.StockKeyboardReplayOverlayTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        common = ('--stock-keyboard-observer', '--serial-pathing', '--player-factory-probe',
                  '--stock-keyboard-replay-probe', '--independent-keyboard-frames', '4',
                  '--stock-keyboard-replay-last-capture', '6')
        flag = 'X4LAN_CONFIG.native_gate_linux_presentation_probe = true\n'
        self.assertNotIn(flag, (fixture.build(*common) / 'ui/config.lua.append').read_text())
        self.assertIn(flag, (fixture.build(*common, '--player-presentation-probe') / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            fixture.build('--player-presentation-probe')

    def test_receipt_exactness_and_getter_invariants(self):
        rows = receipt()
        self.assertTrue(validate_presentation_rows(rows, 2)['pass'])
        for changed in (rows[:-1], rows[:2] + rows[3:], rows[:3] + rows[2:],
                        rows + rows[-1:], list(reversed(rows)),
                        [row.replace('slot=2', 'slot=1') for row in rows],
                        [row.replace('observational=1', 'observational=0') for row in rows],
                        [row.replace('omitted=1', 'omitted=0') for row in rows],
                        [row.replace('success=1', 'success=0') for row in rows],
                        [row.replace('presenter_vtable=3ab4600', 'presenter_vtable=3ab4608') for row in rows],
                        [row.replace('controller_vtable=3ab4060', 'controller_vtable=0') for row in rows],
                        [row.replace('flag=0 authenticated=1', 'flag=2 authenticated=1') for row in rows],
                        [row.replace('manager=10000', 'manager=0') for row in rows],
                        [rows[0], 'unknown\n', *rows[1:]],
                        [rows[0].replace('player=1', 'player=' + '1' * 10000), *rows[1:]]):
            self.assertFalse(validate_presentation_rows(changed, 2)['pass'])
        called = rows[0].replace('cache_parent=0 live_called=0 live=0 cached_called=0 cached=0 omitted=1 refs=0,0,0 marker=0,0',
                                 'cache_parent=12 live_called=1 live=12 cached_called=1 cached=12 omitted=0 refs=3,4,3 marker=2,2')
        self.assertTrue(validate_presentation_rows([called, *rows[1:]], 2)['pass'])
        self.assertFalse(validate_presentation_rows([called.replace('marker=2,2', 'marker=2,1'), *rows[1:]], 2)['pass'])
        self.assertFalse(validate_presentation_rows([called.replace('refs=3,4,3', 'refs=3,3,3'), *rows[1:]], 2)['pass'])

    def test_disabled_and_short_runs_do_not_accept_receipts(self):
        for slot in (None, 2):
            result = parse_recurring_physics_evidence(receipt(), [], 223, frame_count=3,
                                                       input_mode='network', presentation_slot=slot)
            self.assertFalse(result['pass'])

    def test_complete_network_trace_and_missing_or_bad_receipts(self):
        rows = []
        tick = 0
        for line in valid_frames(4):
            if 'physics=frame tick=' in line and 'event=begin' in line:
                tick = int(line.split('tick=')[1].split()[0])
            wire = record(tick - 2) if tick else ''
            if 'input=press ' in line or ('physics=input ' in line and ' state=1 ' in line):
                for phase in ('before', 'applied'):
                    rows.append(f'X4LAN LIFECYCLE_DISCARD physics=network tick={tick} slot=2 stage={phase} '
                                f'held_before=0 held_after=0 digital=0 record={wire}\n')
            elif 'input=release ' in line or ('physics=input ' in line and ' state=0 ' in line):
                rows.append(f'X4LAN LIFECYCLE_DISCARD physics=network tick={tick} slot=2 stage=updated '
                            f'held_before=0 held_after=0 digital=0 record={wire}\n')
            else:
                rows.append(line.replace('input=state2 worker=', 'input=record worker='))
                if tick == 7 and line.startswith('X4LAN LIFECYCLE_DISCARD physics=upkeep stage=leave '):
                    rows.extend(receipt())
        stderr = worker_rows(4) + [
            f'X4LAN KEYBOARD_SLOT1 tick={tick} capture={tick-2} saved=0000000000000011 '
            f'record={record(tick-2)} held_before=0 held_after=0\n' for tick in range(5, 9)]
        engine = [f'LINUX_KEYBOARD_CAPTURE tick={tick-2} record={record(tick-2)}\n' for tick in range(5, 9)]
        engine.append('LINUX_PRESENTATION_PROBE tick=7 slot=2\n')
        def parse(trace, engine_lines, enabled=True):
            return parse_network_physics_evidence(trace, stderr, engine_lines, 223,
                       frame_count=4, slot=2, allow_presentation=enabled)
        result = parse(rows, engine)
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['presentation']['pass'])
        self.assertFalse(parse(rows, engine, False)['pass'])
        self.assertFalse(parse([line for line in rows if 'PRESENTATION_PROBE' not in line], engine, False)['pass'])
        self.assertFalse(parse(rows, engine[:-1])['pass'])
        self.assertFalse(parse(rows + receipt(), engine)['pass'])
        self.assertFalse(parse([line.replace('observational=1', 'observational=0') for line in rows], engine)['pass'])

    @unittest.skipUnless(shutil.which('luajit'), 'luajit unavailable')
    def test_lua_both_clients_once_and_failed_result(self):
        source = Path(__file__).resolve().parents[1] / 'tools/dev/native_gate_linux_keyboard.lua'
        script = r"""
        dofile(arg[1])
        for slot = 1, 2 do
            local calls = 0
            local gate = {
                platform = function() return 'linux' end,
                saved_identity = function() return '0000000000000011' end,
                stock_keyboard_replay_request = function() return 1 end,
                stock_keyboard_replay_status = function() return 'ok' end,
                player_presentation_probe = function() calls = calls + 1; return 1 end,
            }
            local ffi = { C = {
                GetPlayerOccupiedShipID = function() return 11 end,
                IsComponentOperational = function() return true end,
                GetObjectPositionInSector = function()
                    return {x=0,y=0,z=0,yaw=0,pitch=0,roll=0}
                end,
            }}
            local cfg = {native_gate_stock_keyboard_observer=true, native_gate_clients=2,
                native_gate_linux_presentation_probe=true, slot='p'..slot,
                native_gate_stock_keyboard_replay_probe={independent_players=true,
                    last_capture=6, neutral_ship='0000000000000022'}}
            local probe = X4LAN_LINUX_KEYBOARD.create(gate, cfg, function() end, ffi, function() end)
            for tick=1,8 do probe.frame(tick) end
            assert(calls == 1)
            assert(not pcall(probe.frame, 7))
            assert(calls == 1)
            gate.player_presentation_probe = function() calls = calls + 1; return 0 end
            local failing = X4LAN_LINUX_KEYBOARD.create(gate, cfg, function() end, ffi, function() end)
            failing.frame(1)
            assert(not pcall(failing.frame, 7))
            assert(not pcall(failing.frame, 7))
            assert(calls == 2)
        end
        """
        subprocess.run(['luajit', '-', str(source)], input=script, text=True, check=True)

    def test_bundle_short_window_rejected_before_build(self):
        with mock.patch.object(live.subprocess, 'run') as run, self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused/zig', '--snapshot', '/unused/save', '--out', '/unused/out',
                       '--player-presentation-probe', '--independent-keyboard-frames', '3'])
        run.assert_not_called()
