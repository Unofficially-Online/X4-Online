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
-- The lobby's side: DebugError rows are the requests; loadfile serves files[path] (the state file or a helper's pipe).
requests, files, opened, refreshes = {}, {}, {}, 0
function DebugError(text) requests[#requests+1]=text end
function loadfile(path)
  opened[#opened+1]=path
  if not files[path] then return nil, 'cannot open '..path end
  return loadstring(files[path])
end
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
function options.refresh() refreshes=refreshes+1; options.submenuHandler(options.currentOption) end
Menus = {{name='MapMenu'}, options}
menu = options
"""
STATE_FILE = './extensions/x4_lan_lab/lobby/state.lua'
STATE = ("return {stamp = 7, phase = 'session', lines = {'You are player 2 of 2.'}, code = 'K7QX-3FNA', "
         "network = {mode = 'direct', mapping = 'upnp', public_ip_known = true, ping_ms = 38}, "
         "players = {{slot = 1, ready = true, host = true}, {slot = 2, ready = true}}}")


class MpMenu(unittest.TestCase):
    def run_block(self, setup=SETUP, cfg_extra=''):
        lua = LuaRuntime()
        lua.execute(setup + cfg_extra)
        install, state, line, leave, tick = lua.execute(
            BLOCK + '\nreturn mp_menu.install, mp_menu, mp_menu.line, mp_menu.leave, mp_menu.tick')
        self.tick = tick
        return lua, install, state, line, leave

    def rows(self, g):
        return [g.drawn[i] for i in range(1, len(g.drawn) + 1)]

    def requests(self, g):
        return [g.requests[i] for i in range(1, len(g.requests) + 1)]

    def opened(self, g):
        return [g.opened[i] for i in range(1, len(g.opened) + 1)]

    def ticks(self, n):
        for _ in range(n):
            self.tick()

    def test_lobby_state_players_code_network_refresh_and_leave(self):
        lua, install, state, line, leave = self.run_block(cfg_extra="; cfg.lobby = {files = {}, session = true}")
        g = lua.globals()
        g.files[STATE_FILE] = STATE
        install()
        g.menu.submenuHandler('x4lan_mp')
        rows = self.rows(g)
        self.assertEqual([r.id for r in rows], ['header'] * 5 + ['x4lan_mp_network', 'x4lan_mp_copy', 'header',
                                                                 'x4lan_mp_player1', 'x4lan_mp_player2', 'line', 'x4lan_mp_leave'])
        self.assertEqual(rows[0].name(), 'You are player 2 of 2')     # the lines of before stay
        self.assertEqual([r.name for r in rows[5:10]], ['Connection: direct (UPnP), ping 38 ms', 'Join code: K7QX-3FNA',
                                                        'Players', 'Player 1 (host)', 'Player 2 (you)'])
        rows[6].callback()
        self.assertEqual(self.requests(g)[-1], '[X4LAN] LOBBY REQUEST COPY')
        # while the page is open the state is read every few seconds; the page is drawn again when it changed
        reads = self.opened(g).count(STATE_FILE)
        self.ticks(149)
        self.assertEqual(self.opened(g).count(STATE_FILE), reads)
        self.ticks(1)
        self.assertEqual((self.opened(g).count(STATE_FILE), g.refreshes), (reads + 1, 0))   # the same stamp: not drawn again
        g.files[STATE_FILE] = "return {stamp = 8, phase = 'session', code = 'K7QX-3FNA', network = {mode = 'relay', ping_ms = 81.6}, " \
                              "players = {{slot = 1, host = true}, {slot = 2}, {slot = 3}}}"
        self.ticks(150)
        self.assertEqual(g.refreshes, 1)
        rows = self.rows(g)
        self.assertEqual(rows[5].name, "Connection: through the server's relay, ping 82 ms")
        self.assertEqual([r.id for r in rows].count('x4lan_mp_player3'), 1)
        g.files[STATE_FILE] = 'return {stamp = 9, pha'             # being replaced: the last state stays
        self.ticks(150)
        self.assertEqual((g.refreshes, self.rows(g)[5].name), (1, "Connection: through the server's relay, ping 82 ms"))
        # a closed page reads nothing
        g.menu.submenuHandler('main')
        reads = self.opened(g).count(STATE_FILE)
        self.ticks(300)
        self.assertEqual(self.opened(g).count(STATE_FILE), reads)
        # Leave: the gate stops and the launcher hears LEAVE
        leave()
        self.assertEqual(g.stops, 1)
        self.assertEqual(self.requests(g)[-1], '[X4LAN] LOBBY REQUEST LEAVE')
        leave()
        self.assertEqual(self.requests(g).count('[X4LAN] LOBBY REQUEST LEAVE'), 1)

    def test_a_steam_started_session_asks_the_helpers_pipes(self):
        lua, install, state, line, leave = self.run_block(cfg_extra="; cfg.lobby = {pipe = 'x4mp-t'}")
        g = lua.globals()
        pipe = '\\\\.\\pipe\\x4mp-t-'
        for name in ['state', 'tbegin', 'tend'] + ['t' + digit for digit in '0123456789abcdef']:
            g.files[pipe + name] = STATE   # the helper answers every open with the state
        install()
        g.menu.submenuHandler('x4lan_mp')
        rows = self.rows(g)
        self.assertEqual(rows[6].name, 'Join code: K7QX-3FNA')
        rows[6].callback()
        leave()

        def spelled(text):   # begin, one open per hex digit of the request's bytes, end
            return [pipe + 'tbegin'] + [pipe + 't' + digit for digit in text.encode().hex()] + [pipe + 'tend']
        self.assertEqual(self.opened(g), [pipe + 'state'] + spelled('COPY') + spelled('LEAVE'))
        # no helper: a request stops at its begin (tried a few times at once)
        g.files[pipe + 'tbegin'] = None
        count = len(self.opened(g))
        rows[6].callback()
        self.assertEqual(self.opened(g)[count:], [pipe + 'tbegin'] * 100)

    def test_a_row_that_fails_is_left_out(self):
        failing = SETUP.replace('function options.displayOption(ftable, option) drawn[#drawn+1]=option end',
                                "function options.displayOption(ftable, option)\n"
                                "  if option.id == 'x4lan_mp_copy' then error('no buttons today') end\n"
                                "  drawn[#drawn+1]=option\nend")
        self.assertNotEqual(failing, SETUP)
        lua, install, *_ = self.run_block(failing, cfg_extra="; cfg.lobby = {files = {}, session = true}")
        g = lua.globals()
        g.files[STATE_FILE] = STATE
        install()
        g.menu.submenuHandler('x4lan_mp')
        self.assertNotIn('x4lan_mp_copy', [r.id for r in self.rows(g)])
        self.assertIn('x4lan_mp_leave', [r.id for r in self.rows(g)])
        logs = [g.logs[i] for i in range(1, len(g.logs) + 1)]
        self.assertTrue(any(t.startswith('MP_MENU_DRAW_ERROR x4lan_mp_copy') for t in logs), logs)

    def test_the_onupdate_handler_runs_the_menu_tick(self):
        handler = PROBE[END:PROBE.index('\nend)\n', END)]
        self.assertIn('pcall(mp_menu.tick)', handler)
        self.assertLess(handler.index('if not mp_menu then'), handler.index('pcall(mp_menu.tick)'))

    def test_the_whole_probe_compiles(self):
        """LuaJIT allows 200 local variables per function; the probe's main function is close to it."""
        compiles, why = LuaRuntime(unpack_returned_tuples=True).eval(
            'function(source) local chunk, why = loadstring(source, "=native_gate_probe.lua") return chunk ~= nil, why end')(PROBE)
        self.assertTrue(compiles, why)

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
