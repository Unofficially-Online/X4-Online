import unittest

import test_linux_view_query_lua as view_query_fixture


class UIOverlayLuaTests(unittest.TestCase):
    def runtime(self):
        lua = view_query_fixture.ViewQueryFrameBracketLuaTests().runtime()
        lua.execute('''
            cfg.native_gate_linux_ui_overlay_probe=true
            mp_menu={installed=false}
            function mp_menu_install() push('prelude') end
            gate.player_ui_overlay_abort=function() push('abort'); error('overlay aborted') end
        ''')
        return lua

    def test_full_handler_is_inside_pause(self):
        lua = self.runtime()
        lua.execute('on_update()')
        self.assertEqual(list(lua.globals().calls.values()),
            ['pause', 'prelude', 'hud0', 'mark1', 'phase1', 'frame',
             'phase0', 'mark0', 'hud1', 'resume'])

    def test_frame_failure_aborts_before_resume(self):
        lua = self.runtime()
        lua.execute("frame_error=true; local ok=pcall(on_update); assert(not ok)")
        calls = list(lua.globals().calls.values())
        self.assertEqual(calls[-1], 'abort')
        self.assertNotIn('resume', calls)
        self.assertNotIn('stop', calls)

    def test_done_early_return_restores(self):
        lua = self.runtime()
        lua.execute('done=true; diagnostic=true; on_update()')
        self.assertEqual(list(lua.globals().calls.values()), ['pause', 'prelude', 'resume'])


if __name__ == '__main__':
    unittest.main()
