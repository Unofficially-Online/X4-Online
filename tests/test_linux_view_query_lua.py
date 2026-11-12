"""Exercise Linux view-query Lua integration without starting X4."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime
import test_linux_keyboard_probe_lua as keyboard

ROOT = Path(__file__).resolve().parents[1]


class ViewQueryKeyboardLuaTests(unittest.TestCase):
    def runtime(self, slot='p1'):
        fixture = keyboard.LinuxKeyboardProbeLuaTests()
        lua = fixture.runtime(slot)
        lua.execute('''
            cfg.native_gate_stock_keyboard_replay_probe.independent_players=true
            observe_calls, finish_calls = 0, 0
            observe_result, finish_result = 1, 'v=1 finished=1'
            gate.player_view_query_observe=function()
                observe_calls=observe_calls+1
                return observe_result
            end
            gate.player_view_query_phase=function() return 1 end
            gate.player_view_query_finish=function()
                finish_calls=finish_calls+1
                return finish_result
            end
        ''')
        return fixture, lua

    def test_default_off_and_both_slots_arm_once_and_finish_at_end(self):
        for slot in ('p1', 'p2'):
            for enabled in (None, False, True):
                with self.subTest(slot=slot, enabled=enabled):
                    fixture, lua = self.runtime(slot)
                    lua.globals().cfg.native_gate_linux_view_query_observer = enabled
                    fixture.start(lua)
                    for tick in range(2, 21):
                        lua.eval(f'probe.frame({tick})')
                    self.assertEqual(lua.globals().observe_calls, int(enabled is True))
                    self.assertEqual(lua.globals().finish_calls, int(enabled is True))
                    rows = list(lua.globals().logs.values())
                    if enabled is True:
                        self.assertIn(f'LINUX_VIEW_QUERY_ARMED tick=7 slot={slot[-1]}', rows)
                        self.assertIn(f'LINUX_VIEW_QUERY_RESULT tick=20 slot={slot[-1]} v=1 finished=1', rows)
                        with self.assertRaisesRegex(Exception, 'cannot be retried'):
                            lua.eval('probe.frame(7)')
                        with self.assertRaisesRegex(Exception, 'completion out of order'):
                            lua.eval('probe.frame(20)')
                        self.assertEqual(lua.globals().observe_calls, 1)
                        self.assertEqual(lua.globals().finish_calls, 1)
                    else:
                        self.assertFalse(any('LINUX_VIEW_QUERY_' in row for row in rows))

    def test_invalid_configuration_fails_before_arming(self):
        for mutation in (
            'cfg.native_gate_stock_keyboard_replay_probe.independent_players=false',
            'cfg.native_gate_stock_keyboard_replay_probe.last_capture=5',
            'gate.player_view_query_observe=nil',
            'gate.player_view_query_phase=nil',
            'gate.player_view_query_finish=nil',
            'cfg.native_gate_linux_view_query_observer=1',
            'cfg.native_gate_linux_view_query_observer="true"',
        ):
            with self.subTest(mutation=mutation):
                fixture, lua = self.runtime()
                lua.execute('cfg.native_gate_linux_view_query_observer=true')
                lua.execute(mutation)
                with self.assertRaisesRegex(Exception, 'view query observer'):
                    fixture.create(lua)
                self.assertEqual(lua.globals().observe_calls, 0)
                self.assertEqual(lua.globals().finish_calls, 0)

    def test_arm_and_finish_fail_closed(self):
        fixture, lua = self.runtime()
        lua.execute('cfg.native_gate_linux_view_query_observer=true')
        fixture.start(lua)
        with self.assertRaisesRegex(Exception, 'completion out of order'):
            lua.eval('probe.frame(20)')
        self.assertEqual(lua.globals().finish_calls, 0)

        fixture, lua = self.runtime()
        lua.execute('cfg.native_gate_linux_view_query_observer=true; observe_result=0')
        fixture.start(lua)
        with self.assertRaisesRegex(Exception, 'view query observer refused'):
            lua.eval('probe.frame(7)')
        with self.assertRaisesRegex(Exception, 'cannot be retried'):
            lua.eval('probe.frame(7)')
        self.assertEqual(lua.globals().observe_calls, 1)
        self.assertEqual(lua.globals().finish_calls, 0)

        fixture, lua = self.runtime()
        lua.execute('cfg.native_gate_linux_view_query_observer=true; finish_result=nil')
        fixture.start(lua)
        lua.eval('probe.frame(7)')
        with self.assertRaisesRegex(Exception, 'report unavailable'):
            lua.eval('probe.frame(20)')
        with self.assertRaisesRegex(Exception, 'completion out of order'):
            lua.eval('probe.frame(20)')
        self.assertEqual(lua.globals().finish_calls, 1)


class ViewQueryFrameBracketLuaTests(unittest.TestCase):
    def runtime(self):
        source = (ROOT / 'tools/dev/native_gate_probe.lua').read_text()
        start = source.index("SetScript('onUpdate',function()")
        end = source.index('\nend)\nend)', start) + len('\nend)')
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
            cfg={native_gate_linux_view_query_observer=true, native_gate_runtime_ids_local=true}
            calls, logs = {}, {}
            function push(value) calls[#calls+1]=value end
            function SetScript(name, fn) assert(name=='onUpdate'); on_update=fn end
            function frame() push('frame'); if frame_error then error('frame failure') end end
            function log(text) logs[#logs+1]=text end
            ticklog={flush=function() push('flush') end,
                notice=function() push('notice') end,
                try=function(fn, ...) return fn(...) end}
            pilot_view={own_predict_hud=true}
            native_owned=true
            gate={
                player_view_query_phase=function(value)
                    push('phase'..value)
                    if value==1 and entry_throws then error('entry exception') end
                    if value==0 and exit_throws then error('exit exception') end
                    if value==1 and entry_fails then return 0 end
                    if value==0 and exit_fails then return 0 end
                    return 1
                end,
                runtime_ids_probe=function(value) push('mark'..value) end,
                own_predict_hud=function(value) push('hud'..value) end,
                pilot_present_pause=function() push('pause');return 1 end,
                pilot_present_resume=function() push('resume') end,
                stop=function() push('stop');return 1 end,
                poll=function() return {active=false,restored=false,issued=7} end,
            }
        ''')
        lua.execute(source[start:end])
        return lua

    def test_normal_scope_order(self):
        lua = self.runtime()
        lua.execute('on_update()')
        self.assertEqual(list(lua.globals().calls.values()),
                         ['pause', 'hud0', 'mark1', 'phase1', 'frame',
                          'phase0', 'mark0', 'hud1', 'resume'])
        self.assertIsNone(lua.globals().done)

    def test_disabled_bracket_does_not_call_observer(self):
        lua = self.runtime()
        lua.execute('cfg.native_gate_linux_view_query_observer=false; on_update()')
        calls = list(lua.globals().calls.values())
        self.assertIn('frame', calls)
        self.assertNotIn('phase1', calls)
        self.assertNotIn('phase0', calls)

    def test_entry_exit_and_frame_failures_use_cleanup_and_error_path(self):
        for mutation, reason, frame_called in (
            ('entry_fails=true', 'view query probe entry refused', False),
            ('entry_throws=true', 'entry exception', False),
            ('exit_fails=true', 'view query probe exit refused', True),
            ('exit_throws=true', 'exit exception', True),
            ('frame_error=true', 'frame failure', True),
        ):
            with self.subTest(mutation=mutation):
                lua = self.runtime()
                lua.execute(mutation)
                lua.execute('on_update()')
                calls = list(lua.globals().calls.values())
                self.assertEqual('frame' in calls, frame_called)
                self.assertEqual(calls[-7:], ['phase0', 'mark0', 'hud1', 'resume', 'flush', 'notice', 'stop'])
                self.assertTrue(lua.globals().done)
                self.assertTrue(any('ERROR ' in row and reason in row for row in lua.globals().logs.values()))
