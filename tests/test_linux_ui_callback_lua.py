"""Callback observer close/read order without X4."""
import unittest
import test_linux_view_query_lua as view_query_tests


class UiCallbackLuaTest(unittest.TestCase):
    def runtime(self):
        fixture, lua = view_query_tests.ViewQueryKeyboardLuaTests().runtime()
        lua.execute('''
            cfg.native_gate_linux_view_query_observer=true
            cfg.native_gate_linux_presentation_probe=true
            cfg.native_gate_linux_ui_callback_observer=true
            gate.player_presentation_probe=function() return 1 end
            close_calls=0; result_calls=0
            gate.player_ui_callback_close=function() close_calls=close_calls+1; return 1 end
            gate.player_ui_callback_result=function() result_calls=result_calls+1; return 'v=1 fixture' end
        ''')
        return fixture, lua

    def test_close_precedes_existing_finish(self):
        fixture, lua = self.runtime()
        fixture.start(lua)
        for tick in range(2, 21):
            lua.eval(f'probe.frame({tick})')
        self.assertEqual(lua.globals().close_calls, 1)
        self.assertEqual(lua.globals().result_calls, 1)
        self.assertEqual(lua.globals().finish_calls, 1)
        rows = list(lua.globals().logs.values())
        self.assertIn('LINUX_UI_CALLBACK_CLOSE tick=19 slot=1', rows)
        self.assertIn('LINUX_UI_CALLBACK_RESULT tick=20 slot=1 v=1 fixture', rows)
        self.assertLess(rows.index('LINUX_UI_CALLBACK_CLOSE tick=19 slot=1'),
                        rows.index('LINUX_UI_CALLBACK_RESULT tick=20 slot=1 v=1 fixture'))

    def test_invalid_dependencies_and_refusal(self):
        for mutation in ('cfg.native_gate_linux_ui_callback_observer=1',
                         'cfg.native_gate_linux_view_query_observer=false',
                         'cfg.native_gate_linux_presentation_probe=false',
                         'gate.player_ui_callback_close=nil', 'gate.player_ui_callback_result=nil'):
            fixture, lua = self.runtime()
            lua.execute(mutation)
            with self.assertRaises(Exception):
                fixture.create(lua)
        fixture, lua = self.runtime()
        lua.execute('gate.player_ui_callback_close=function() return 0 end')
        fixture.start(lua)
        lua.eval('probe.frame(7)')
        with self.assertRaisesRegex(Exception, 'close refused'):
            lua.eval('probe.frame(19)')
