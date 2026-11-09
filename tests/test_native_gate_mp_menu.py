"""The in-session multiplayer menu (tools/dev/native_gate_probe.lua, native_gate_mp_menu): its block runs against a fake
X4 options menu that draws its pages as X4's gameoptions.lua does (X4's UI has no debug library)."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
PROBE = (ROOT / 'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
START = PROBE.index('-- In-session multiplayer menu')
END = PROBE.index("SetScript('onUpdate',function()", START)
BLOCK = PROBE[START:END]

SETUP = r"""
logs, stops, active = {}, 0, 1
gate = {poll=function() return {active=active, issued=1830} end, stop=function() stops=stops+1; active=0; return true end}
cfg = {slot='p2', native_gate_tick_hz=30, host='127.0.0.1', port=47000}
clients = 2
function log(text) logs[#logs+1]=text end
-- As X4's gameoptions.lua: a local option table, the page function draws a page's rows through the row function, the
-- submenu handler draws any page the table holds.
local config = {optionDefinitions={main={{id='continue'},{id='multiplayer'},{id='line'},{id='settings'},{id='quit'}},
  multiplayer={name='Multiplayer', {id='multiplayer_server'}, {id='lobby'}}}}
local options = {name='OptionsMenu', isStartmenu=false}
drawn = {}
function options.displayOption(ftable, option) drawn[#drawn+1]=option end
function options.displayOptions(p)
  options.currentOption=p; drawn={}
  for _,o in ipairs(config.optionDefinitions[p]) do options.displayOption({}, o) end
end
function options.submenuHandler(p) if config.optionDefinitions[p] then options.displayOptions(p) end end
Menus = {{name='MapMenu'}, options}
menu = options
"""


class MpMenu(unittest.TestCase):
    def run_block(self, setup=SETUP, cfg_extra=''):
        lua = LuaRuntime()
        lua.execute(setup + cfg_extra)
        install, state, line, leave = lua.execute(BLOCK + '\nreturn mp_menu_install, mp_menu, mp_menu_line, mp_menu_leave')
        return lua, install, state, line, leave

    def test_entry_before_settings_page_rows_and_leave(self):
        lua, install, state, line, leave = self.run_block()
        install()
        g = lua.globals()
        self.assertTrue(state.installed)
        g.menu.displayOptions('main')
        ids = [g.drawn[i].id for i in range(1, len(g.drawn) + 1)]
        self.assertEqual(ids, ['continue', 'multiplayer', 'line', 'x4lan_mp', 'settings', 'quit'])
        entry = g.drawn[4]
        self.assertEqual((entry.name, entry.submenu), ('Multiplayer session', 'x4lan_mp'))
        g.menu.submenuHandler(entry.submenu)   # selecting the entry
        self.assertEqual(g.menu.currentOption, 'x4lan_mp')
        rows = [g.drawn[i] for i in range(1, len(g.drawn) + 1)]
        self.assertEqual([r.id for r in rows], ['header'] * 5 + ['line', 'x4lan_mp_leave'])   # the stock rows replaced
        self.assertEqual(rows[0].name(), 'You are player 2 of 2')
        self.assertEqual(line(2), 'Step 1830 (game time 1:01)')
        self.assertTrue(line(3).startswith('In the session'))
        self.assertEqual(line(4), 'Relay 127.0.0.1:47000')
        g.menu.displayOptions(g.menu.currentOption)   # a refresh draws the page again
        self.assertEqual(len(g.drawn), 7)
        g.menu.submenuHandler('multiplayer')   # the stock page itself is unchanged
        self.assertEqual([g.drawn[i].id for i in range(1, len(g.drawn) + 1)], ['multiplayer_server', 'lobby'])
        button = rows[-1]
        self.assertEqual((button.valuetype, button.value), ('button', 'Leave'))
        self.assertTrue(button.selectable())
        button.callback()
        button.callback()   # a second click does nothing
        self.assertEqual(g.stops, 1)
        self.assertTrue(line(3).startswith('You left the session'))
        self.assertFalse(button.selectable())
        logs = [g.logs[i] for i in range(1, len(g.logs) + 1)]
        self.assertTrue(any(t.startswith('MP_MENU_INSTALLED rows=7') for t in logs), logs)
        self.assertTrue(any(t.startswith('MP_MENU_LEAVE stopped=true tick=1830') for t in logs), logs)
        install()   # once only
        g.menu.displayOptions('main')
        self.assertEqual(len(g.drawn), 6)

    def test_waits_for_the_menu_refuses_without_its_functions_and_can_be_off(self):
        lua, install, state, *_ = self.run_block(SETUP.replace("Menus = {{name='MapMenu'}, options}", 'Menus = nil'))
        install()
        self.assertFalse(state.installed)
        lua.execute("Menus = {{name='OptionsMenu'}}")
        install()
        self.assertFalse(state.installed)
        self.assertEqual(lua.globals().logs[1], 'MP_MENU_REFUSED why=no_menu_functions')
        lua, install, state, *_ = self.run_block(cfg_extra='; cfg.native_gate_mp_menu = false')
        install()
        self.assertFalse(state.installed)
        g = lua.globals()
        g.menu.displayOptions('main')
        self.assertEqual(len(g.drawn), 5)


if __name__ == '__main__':
    unittest.main()
