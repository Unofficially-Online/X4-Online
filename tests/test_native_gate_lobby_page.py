"""The main-menu Multiplayer page (tools/dev/native_gate_lobby.lua) for netplay: the rows it draws for each phase of the
state table (docs/NETPLAY.md, "The page and the launcher"), the requests its buttons and edit boxes send (log rows, and
pipe names in a Steam-started X4), the public and LAN lists, and that it works with X4's stock options menu and with a
replacement of it as kuertee's UI Extensions ships, in protected UI mode, and without breaking the menu when something
fails. Under LuaJIT (lupa) with fake menus; no X4 runs here."""
from pathlib import Path
import unittest

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:   # pragma: no cover
    LuaRuntime = None

ROOT = Path(__file__).resolve().parents[1]
LOBBY = (ROOT / 'tools/dev/native_gate_lobby.lua').read_text(encoding='utf-8')
PIPE_PREFIX = '\\\\.\\pipe\\x4mp-page-'   # as a Python string: \\.\pipe\x4mp-page-

# X4's UI Lua as the page sees it: no io, no os, no ffi; DebugError rows; SetScript; named pipes behind loadfile. A fake
# helper answers every open of \\.\pipe\x4mp-page-* with helper_state (tend with te_state when set), or fails as LuaJIT's
# loadfile does when no helper runs (helper_state nil) or every instance of a pipe is busy (busy[name] opens fail first).
# It collects the spelled requests: tbegin clears its buffer, t0..tf add a digit, tend decodes the buffer into helper_requests.
BASE = r"""
logs, scripts, pipes, helper_requests, busy = {}, {}, {}, {}, {}
io, os = nil, nil
do local real = require
 function require(name) if name == 'ffi' then error('the page must not need ffi') end return real(name) end
end
function DebugError(text) logs[#logs + 1] = text end
function SetScript(name, fn) scripts[name] = fn end
helper_state = "return {stamp = 1, phase = 'idle', lines = {'Helper here.'}}"
local PREFIX = '\\\\.\\pipe\\x4mp-page-'
local buffer
local function helper_saw(name)
 if name == 'tbegin' then
  buffer = ''
 elseif name:match('^t[0-9a-f]$') and buffer then
  buffer = buffer .. name:sub(2)
 elseif name == 'tend' and buffer then
  helper_requests[#helper_requests + 1] = buffer:gsub('..', function(h) return string.char(tonumber(h, 16)) end)
  buffer = nil
  if te_state then return te_state end
 end
 return helper_state
end
do local real_loadfile = loadfile
 function loadfile(path)
  if path:sub(1, 9) == '\\\\.\\pipe\\' then
   pipes[#pipes + 1] = path
   local name = path:sub(#PREFIX + 1)
   if helper_state == nil then return nil, 'cannot open ' .. path end
   if (busy[name] or 0) > 0 then
    busy[name] = busy[name] - 1
    return nil, 'cannot open ' .. path .. ': All pipe instances are busy'
   end
   return loadstring(helper_saw(name))
  end
  return real_loadfile(path)
 end
end
drawn = {}
-- Cells and tables as X4's Helper makes them, as far as the page's edit box rows use them.
local function new_cell()
 local cell = {properties = {}, handlers = {}}
 function cell:setColSpan(n) self.colspan = n return self end
 function cell:createText(text, properties) self.kind = 'text' self.text = text return self end
 function cell:createEditBox(properties) self.kind = 'editbox' self.box = properties return self end
 function cell:setText(text, properties) self.boxtext = text return self end
 return cell
end
function new_table()
 local ftable = {}
 function ftable:addRow(rowdata, properties)
  local row = {index = #drawn + 1}
  for i = 1, 5 do row[i] = new_cell() end
  drawn[#drawn + 1] = {id = rowdata.id, edit = row[3], label = row[2]}
  return row
 end
 function ftable:setSelectedRow(index) self.selected = index end
 return ftable
end
config = {optionDefinitions = {main = {{id = 'continue'}, {id = 'multiplayer'}, {id = 'line'}, {id = 'settings'}, {id = 'quit'}},
  multiplayer = {name = 'Multiplayer', {id = 'multiplayer_server'}, {id = 'lobby'}}}}
"""

# X4's stock gameoptions.lua, as far as the page uses it: the row function records the option it draws.
STOCK = BASE + r"""
menu = {name = 'OptionsMenu', isStartmenu = true, refreshes = 0}
function menu.displayOption(ftable, option, numCols) drawn[#drawn + 1] = option end
function menu.displayOptions(p)
 menu.currentOption = p
 drawn = {}
 local ftable = new_table()
 for _, o in ipairs(config.optionDefinitions[p]) do menu.displayOption(ftable, o) end
end
function menu.submenuHandler(p) if config.optionDefinitions[p] then menu.displayOptions(p) end end
function menu.refresh() menu.refreshes = menu.refreshes + 1 menu.submenuHandler(menu.currentOption) end
Menus = {{name = 'MapMenu'}, menu}
"""

# kuertee's UI Extensions replace gameoptions.lua: another menu table with the same functions, callbacks that may change
# a page's options before it is drawn, a submenu handler that asks its callbacks first, registered once its file ran.
KUERTEE = BASE + r"""
menu = {name = 'OptionsMenu', isStartmenu = true, refreshes = 0, uix_callbacks = {}}
function menu.registerCallback(name, fn)
 menu.uix_callbacks[name] = menu.uix_callbacks[name] or {}
 table.insert(menu.uix_callbacks[name], fn)
end
function menu.displayOption(ftable, option, numCols)
 if numCols == nil then numCols = 5 end
 drawn[#drawn + 1] = option
end
function menu.displayOptions(optionParameter)
 menu.currentOption = optionParameter
 drawn = {}
 local options = config.optionDefinitions[optionParameter]
 for _, callback in pairs(menu.uix_callbacks['displayOptions_modifyOptions'] or {}) do options = callback(options, config) end
 local ftable = new_table()
 for _, option in ipairs(options) do menu.displayOption(ftable, option) end
end
pre_pages = {}
function menu.submenuHandler(optionParameter)
 for _, callback in pairs(menu.uix_callbacks['submenuHandler_preDisplayOptions'] or {}) do callback(optionParameter) end
 for _, callback in pairs(menu.uix_callbacks['submenuHandler_customPage'] or {}) do
  if callback(optionParameter, config) then return end
 end
 if config.optionDefinitions[optionParameter] then menu.displayOptions(optionParameter) end
end
function menu.refresh() menu.refreshes = menu.refreshes + 1 menu.submenuHandler(menu.currentOption) end
-- another mod adds a row to the stock multiplayer page through UI Extensions
menu.registerCallback('displayOptions_modifyOptions', function(options, config)
 if options ~= config.optionDefinitions.multiplayer then return options end
 local copy = {name = options.name}
 for i, option in ipairs(options) do copy[i] = option end
 copy[#copy + 1] = {id = 'other_mod_row'}
 return copy
end)
menu.registerCallback('submenuHandler_preDisplayOptions', function(p) pre_pages[#pre_pages + 1] = p end)
late_menu = menu
Menus = {{name = 'MapMenu'}}
"""

START_ROWS = ['header', 'x4lan_lobby_public', 'x4lan_lobby_players', 'x4lan_lobby_name', 'x4lan_lobby_host',
              'header', 'x4lan_lobby_code', 'x4lan_lobby_joincode', 'x4lan_lobby_join',
              'header', 'x4lan_lobby_browse', 'x4lan_lobby_game_none', 'header', 'x4lan_lobby_lan_none']
LINES = ['header'] * 6 + ['line']
HOSTING = ("{stamp = 5, phase = 'hosting', lines = {'Hosting'}, code = 'K7QX-3FNA', public = true, host = true, "
           "can_start = false, network = {mode = 'direct', mapping = 'upnp', public_ip_known = true, ping_ms = 38.4}, "
           "players = {{slot = 1, ready = true, host = true}, {slot = 2, ready = false}}}")


@unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
class Page(unittest.TestCase):
    def start(self, lobby='{netplay = true}', menu=STOCK, extra=''):
        lua = LuaRuntime()
        lua.execute(menu + extra + '\nX4LAN_CONFIG = {enabled = false, lobby = ' + lobby + '}\n')
        lua.execute(LOBBY)
        g = lua.globals()
        g.scripts.onUpdate()   # installs the entry
        return lua, g

    def frames(self, g, n):
        for _ in range(n):
            g.scripts.onUpdate()

    def state(self, lua, text):
        lua.execute('X4LAN_LOBBY.state = ' + text)

    def open(self, g):
        g.menu.submenuHandler('x4lan_lobby')
        return self.rows(g)

    def rows(self, g):
        return [g.drawn[i] for i in range(1, len(g.drawn) + 1)]

    def ids(self, g):
        return [r.id for r in self.rows(g)]

    def row(self, g, row_id):
        return next(r for r in self.rows(g) if r.id == row_id)

    def logs(self, g):
        return [g.logs[i] for i in range(1, len(g.logs) + 1)]

    def requests(self, g):
        prefix = '[X4LAN] LOBBY REQUEST '
        return [t[len(prefix):] for t in self.logs(g) if t.startswith(prefix)]

    def test_each_phase_draws_its_rows(self):
        lua, g = self.start()
        phases = {
            "{stamp = 1, phase = 'idle', lines = {'2 players'}}": LINES + START_ROWS,
            "{stamp = 1, phase = 'browsing', lines = {}}": LINES + START_ROWS,
            "{stamp = 1, phase = 'error', lines = {'The server is not reachable.'}}": LINES + START_ROWS + ['x4lan_lobby_cancel'],
            "{stamp = 1, phase = 'nohelper', lines = {}}": LINES + START_ROWS,
            "{stamp = 1, phase = 'preparing', lines = {}, progress = {what = 'session', done = 0.25}}":
                LINES + ['x4lan_lobby_progress', 'x4lan_lobby_cancel'],
            HOSTING: LINES + ['x4lan_lobby_copy', 'x4lan_lobby_network', 'header', 'x4lan_lobby_player1',
                              'x4lan_lobby_player2', 'x4lan_lobby_startgame', 'x4lan_lobby_cancel'],
            "{stamp = 1, phase = 'joining', lines = {}, progress = {what = 'download', done = '0.4'}}":
                LINES + ['x4lan_lobby_progress', 'x4lan_lobby_cancel'],
            "{stamp = 1, phase = 'waiting', lines = {}, code = 'K7QX-3FNA', players = {{slot = 1, host = true}, {slot = 2}}}":
                LINES + ['x4lan_lobby_copy', 'header', 'x4lan_lobby_player1', 'x4lan_lobby_player2', 'x4lan_lobby_cancel'],
            "{stamp = 1, phase = 'loading', lines = {'Loading at 12:00:30'}, network = {mode = 'relay', ping_ms = 80}}":
                LINES + ['x4lan_lobby_network', 'x4lan_lobby_cancel'],
            "{stamp = 1, phase = 'session', lines = {'You are player 2 of 2.'}}": LINES,
            "{stamp = 1, phase = 'restart', lines = {'X4 closes now.'}}": LINES,
        }
        for text, expected in phases.items():
            with self.subTest(state=text):
                self.state(lua, text)
                self.open(g)
                self.assertEqual(self.ids(g), expected)
        self.state(lua, HOSTING)
        rows = self.open(g)
        names = {r.id: r.name for r in rows if r.id != 'header' or isinstance(r.name, str)}
        self.assertEqual(names['x4lan_lobby_copy'], 'Join code: K7QX-3FNA')
        self.assertEqual(names['x4lan_lobby_network'], 'Connection: direct (UPnP), ping 38 ms')
        self.assertEqual((names['x4lan_lobby_player1'], names['x4lan_lobby_player2']),
                         ('Player 1 (host): ready', 'Player 2: not ready yet'))
        self.assertEqual(names['header'], 'Players')
        start = self.row(g, 'x4lan_lobby_startgame')
        self.assertFalse(start.selectable())
        start.callback()                                       # not yet: nothing is sent
        self.assertNotIn('START', self.requests(g))
        lua.execute('X4LAN_LOBBY.state.can_start = true')
        self.assertTrue(start.selectable())
        start.callback()
        self.row(g, 'x4lan_lobby_copy').callback()
        self.row(g, 'x4lan_lobby_cancel').callback()
        self.assertEqual(self.requests(g)[-3:], ['START', 'COPY', 'CANCEL'])
        self.state(lua, "{stamp = 1, phase = 'joining', lines = {}, progress = {what = 'download', done = '0.4'}}")
        self.assertEqual(self.open(g)[7].name, 'Downloading the session: 40%')
        self.state(lua, "{stamp = 1, phase = 'preparing', lines = {}, progress = {what = 'overlay'}}")
        self.assertEqual(self.open(g)[7].name, 'Preparing the game files...')
        self.state(lua, "{stamp = 1, phase = 'loading', lines = {}, network = {mode = 'lan', mapping = 'none'}}")
        self.assertEqual(self.open(g)[7].name, 'Connection: local network')
        # before the launcher's first state, and without a helper, nothing can be chosen
        for text in ("{stamp = 0, phase = 'starting', lines = {'Starting...'}}", "{stamp = 0, phase = 'nohelper', lines = {}}"):
            self.state(lua, text)
            rows = self.open(g)
            buttons = [r for r in rows if r.valuetype in ('button', 'dropdown')]
            self.assertTrue(buttons)
            self.assertFalse(any(r.selectable() for r in buttons), text)
            self.assertFalse(any(r.edit.box.active for r in rows if r.edit), text)

    def test_the_three_forms_of_the_page(self):
        idle = "{stamp = 1, phase = 'idle', lines = {}}"
        forms = {
            '{}': LINES + ['x4lan_lobby_host', 'x4lan_lobby_join', 'x4lan_lobby_copy', 'x4lan_lobby_cancel'],   # the LAN lobby
            '{server = true}': LINES + ['x4lan_lobby_join', 'x4lan_lobby_cancel'],                              # a kit's server
            '{netplay = true}': LINES + START_ROWS,
            "{pipe = 'x4online', netplay = true}": LINES + START_ROWS,                                         # a release
            '{netplay = true, server = true}': LINES + START_ROWS,
        }
        for config, expected in forms.items():
            with self.subTest(lobby=config):
                lua, g = self.start(config)
                self.state(lua, idle)
                self.open(g)
                self.assertEqual(self.ids(g), expected)
        # the LAN lobby's buttons send the old requests
        lua, g = self.start('{}')
        self.state(lua, idle)
        self.open(g)
        self.row(g, 'x4lan_lobby_host').callback()
        self.row(g, 'x4lan_lobby_join').callback()
        self.assertEqual(self.requests(g), ['HOST', 'JOIN'])
        self.frames(g, 40)
        self.assertNotIn('BROWSE', self.requests(g))           # only the netplay page asks for the lists
        g.menu.submenuHandler('main')
        self.assertEqual(self.row(g, 'x4lan_lobby').name, 'Multiplayer')

    def test_edit_boxes_send_the_code_and_the_host_choices(self):
        lua, g = self.start("{pipe = 'x4mp-page', netplay = true}")
        self.frames(g, 40)                                     # the helper's idle state
        self.open(g)
        code = self.row(g, 'x4lan_lobby_code')
        name = self.row(g, 'x4lan_lobby_name')
        self.assertEqual((code.label.text, code.edit.kind, code.edit.box.maxChars), ('Code or address', 'editbox', 100))
        self.assertEqual((name.label.text, name.edit.box.maxChars, name.edit.box.defaultText), ('Game name', 40, 'X4 game'))
        self.assertTrue(code.edit.box.active)
        # typing: a new state while the box is active does not redraw the page under the player's fingers
        code.edit.handlers.onEditBoxActivated()
        code.edit.handlers.onTextChanged(None, ' k7qx-3fna ')
        refreshes = g.menu.refreshes
        g.helper_state = "return {stamp = 2, phase = 'idle', lines = {'new'}}"
        self.frames(g, 40)
        self.assertEqual(g.menu.refreshes, refreshes)
        self.assertTrue(g.X4LAN_LOBBY.stale)
        code.edit.handlers.onEditBoxDeactivated(None, ' k7qx-3fna ', True, True)   # Enter
        self.assertEqual(self.requests(g)[-1], 'JOINCODE|k7qx-3fna')
        self.frames(g, 1)
        self.assertEqual(g.menu.refreshes, refreshes + 1)   # the held redraw, now
        self.assertEqual(self.row(g, 'x4lan_lobby_code').edit.boxtext, ' k7qx-3fna ')   # the typed text stays
        self.row(g, 'x4lan_lobby_joincode').callback()
        self.row(g, 'x4lan_lobby_join').callback()
        self.assertEqual(self.requests(g)[-2:], ['JOINCODE|k7qx-3fna', 'JOIN'])
        # leaving the box without Enter keeps the text and sends nothing: Escape, or a click elsewhere in the table
        # (X4 reports that click as confirmed, with wastableclick)
        before = len(self.requests(g))
        code = self.row(g, 'x4lan_lobby_code')
        code.edit.handlers.onEditBoxDeactivated(None, 'K7QX-0000', True, False)
        code.edit.handlers.onEditBoxDeactivated(None, 'X4MP-10.0.0.5-47000-' + 'ab' * 16, True, True, True)
        self.assertEqual(len(self.requests(g)), before)
        self.row(g, 'x4lan_lobby_joincode').callback()
        self.assertEqual(self.requests(g)[-1], 'JOINCODE|X4MP-10.0.0.5-47000-' + 'ab' * 16)
        # host: private, 2 players, the default name; then public, 5 players, a typed name
        self.row(g, 'x4lan_lobby_host').callback()
        self.assertEqual(self.requests(g)[-1], 'HOST|0|2|X4 game')
        visibility = self.row(g, 'x4lan_lobby_public')
        self.assertEqual(visibility.value(), 'Private')
        visibility.callback()
        self.assertEqual(visibility.value(), 'Public')
        players = self.row(g, 'x4lan_lobby_players')
        choices, current = players.value()
        self.assertEqual(([choices[i].id for i in range(1, len(choices) + 1)], current), (['2', '3', '4', '5', '6', '7', '8'], '2'))
        players.callback('x4lan_lobby_players', '5')
        players.callback()                                     # the row selected without a choice: unchanged
        players.callback('x4lan_lobby_players', '9')           # out of range: unchanged
        self.assertEqual(players.value()[1], '5')
        name = self.row(g, 'x4lan_lobby_name')
        name.edit.handlers.onEditBoxDeactivated(None, ' Coop | run\n', True, True)
        self.row(g, 'x4lan_lobby_host').callback()
        self.assertEqual(self.requests(g)[-1], 'HOST|1|5|Coop   run')   # no '|' or line break in a field
        name.edit.handlers.onTextChanged(None, 'A' * 39 + '\u00e9')         # 41 bytes: the last character does not fit
        self.row(g, 'x4lan_lobby_host').callback()
        self.assertEqual(self.requests(g)[-1], 'HOST|1|5|' + 'A' * 39)
        # nothing starts outside the start phases
        self.state(lua, HOSTING)
        before = len(self.requests(g))
        for row_id in ('x4lan_lobby_host', 'x4lan_lobby_joincode', 'x4lan_lobby_join', 'x4lan_lobby_browse'):
            next(r for r in self.rows(g) if r.id == row_id).callback()
        self.assertEqual(len(self.requests(g)), before)
        self.assertFalse(any('ERROR' in t for t in self.logs(g)), self.logs(g))

    def test_public_and_lan_games(self):
        lua, g = self.start()
        self.state(lua, "{stamp = 1, phase = 'idle', lines = {}, games = {"
                        "{id = 'g1', name = 'Alpha', players = 2, max = 4, state = 'lobby', compatible = true, ping_ms = 40},"
                        "{id = 'g2', name = 'Beta', players = 1, max = 2, state = 'lobby', compatible = false},"
                        "{id = 'g3', name = 'Gamma', players = 4, max = 4, state = 'running', compatible = true}},"
                        "lan = {{index = 1, name = 'Den', players = 1, max = 2, compatible = true}}}")
        self.open(g)
        ids = self.ids(g)
        games = ids[ids.index('x4lan_lobby_browse') + 1:]
        self.assertEqual(games, ['x4lan_lobby_game_g1', 'x4lan_lobby_game_g2', 'x4lan_lobby_game_g3', 'header', 'x4lan_lobby_lan_1'])
        alpha, beta, gamma = (self.row(g, 'x4lan_lobby_game_' + k) for k in ('g1', 'g2', 'g3'))
        den = self.row(g, 'x4lan_lobby_lan_1')
        self.assertEqual([alpha.name, beta.name, gamma.name, den.name],
                         ['Alpha, 2/4, open, 40 ms', 'Beta, 1/2, open (another release)', 'Gamma, 4/4, running (full)', 'Den, 1/2'])
        self.assertEqual([r.selectable() for r in (alpha, beta, gamma, den)], [True, False, False, True])
        for r in (alpha, beta, gamma, den):
            r.callback()
        self.assertEqual(self.requests(g)[-2:], ['JOINGAME|g1', 'JOINLAN|1'])
        # empty lists, and a long list that is cut
        self.state(lua, "{stamp = 2, phase = 'idle', lines = {}, games = {}, lan = {}}")
        self.open(g)
        self.assertEqual((self.row(g, 'x4lan_lobby_game_none').name, self.row(g, 'x4lan_lobby_lan_none').name),
                         ('No public games right now.', 'No games found on this network.'))
        many = ', '.join(f"{{id = 'g{k}', name = 'Game {k}', players = 1, max = 2}}" for k in range(35))
        self.state(lua, "{stamp = 3, phase = 'browsing', lines = {}, games = {" + many + "}}")
        self.open(g)
        ids = self.ids(g)
        self.assertEqual(sum(i.startswith('x4lan_lobby_game_g') for i in ids), 30)
        self.assertEqual(self.row(g, 'x4lan_lobby_game_more').name, 'and 5 more')
        self.assertEqual(self.row(g, 'x4lan_lobby_lan_none').name, 'Looking for games...')
        self.row(g, 'x4lan_lobby_browse').callback()
        self.assertEqual(self.requests(g)[-1], 'BROWSE')

    def pipes(self, g, start=0):
        return [g.pipes[i][len(PIPE_PREFIX):] for i in range(start + 1, len(g.pipes) + 1)]

    def helper_requests(self, g):
        return [g.helper_requests[i] for i in range(1, len(g.helper_requests) + 1)]

    def spell(self, text):
        return ['tbegin'] + ['t' + digit for digit in text.encode('utf-8').hex()] + ['tend']

    def test_spelled_requests_in_protected_mode(self):
        lua, g = self.start("{pipe = 'x4mp-page', netplay = true}", extra='\npackage = nil\n')
        self.frames(g, 40)
        self.assertEqual(self.pipes(g)[-1], 'state')
        self.assertEqual(self.open(g)[0].name(), 'Helper here.')
        self.frames(g, 40)
        self.assertEqual(self.helper_requests(g), ['BROWSE'])   # the lists, once, when the open page shows them
        lobby = g.X4LAN_LOBBY
        # one request: begin, one open per hex digit of its bytes, end; nothing else
        count = len(g.pipes)
        lobby.request('START')
        self.assertEqual(self.pipes(g, count), ['tbegin', 't5', 't3', 't5', 't4', 't4', 't1', 't5', 't2', 't5', 't4', 'tend'])
        # a typed name with a character outside ASCII: its UTF-8 bytes (c3 a9), digit by digit
        self.row(g, 'x4lan_lobby_name').edit.handlers.onTextChanged(None, 'Caf\u00e9')
        count = len(g.pipes)
        self.row(g, 'x4lan_lobby_host').callback()
        self.assertEqual(self.pipes(g, count), self.spell('HOST|0|2|Caf\u00e9'))
        self.assertEqual(self.pipes(g, count)[-5:], ['tc', 't3', 'ta', 't9', 'tend'])
        self.assertEqual(self.helper_requests(g)[-1], 'HOST|0|2|Caf\u00e9')
        # Join (clipboard) and the start picker's JOIN<k> keep their pipe names; every other request is spelled
        count = len(g.pipes)
        self.row(g, 'x4lan_lobby_join').callback()
        lobby.request('JOIN3')
        self.assertEqual(self.pipes(g, count), ['join', 'join3'])
        count = len(g.pipes)
        self.row(g, 'x4lan_lobby_code').edit.handlers.onEditBoxDeactivated(None, 'K7QX-3FNA', True, True)
        digits = self.pipes(g, count)[1:-1]
        self.assertTrue('tb' in digits and 'te' in digits)    # K is 4b, N is 4e: digits, never the begin or end marks
        self.assertEqual(self.helper_requests(g)[-1], 'JOINCODE|K7QX-3FNA')
        for text in ('JOINGAME|g1', 'JOINLAN|2', 'CANCEL', 'COPY', 'LEAVE'):
            lobby.request(text)
        self.assertEqual(self.helper_requests(g)[-6:], ['JOINCODE|K7QX-3FNA', 'JOINGAME|g1', 'JOINLAN|2', 'CANCEL', 'COPY', 'LEAVE'])
        # a busy pipe is opened again; the helper still gets the request whole
        g.busy.t3 = 2
        count = len(g.pipes)
        lobby.request('START')
        self.assertEqual(self.helper_requests(g)[-1], 'START')
        self.assertEqual(self.pipes(g, count).count('t3'), 3)
        # at most 200 bytes
        count = len(g.pipes)
        lobby.request('JOINCODE|' + 'x' * 191)
        self.assertEqual((len(g.pipes) - count, self.helper_requests(g)[-1]), (402, 'JOINCODE|' + 'x' * 191))
        count = len(g.pipes)
        lobby.request('JOINCODE|' + 'x' * 192)
        self.assertEqual(len(g.pipes), count)
        self.assertTrue(any(t.startswith('[X4LAN] LOBBY REQUEST_UNSENT longer than 200 bytes') for t in self.logs(g)))
        # the end's answer is the state now: the page shows it at the next frame
        refreshes = g.menu.refreshes
        g.te_state = "return {stamp = 9, phase = 'hosting', lines = {'Hosting'}, code = 'K7QX-3FNA', host = true}"
        self.row(g, 'x4lan_lobby_host').callback()
        self.frames(g, 1)
        self.assertEqual((g.X4LAN_LOBBY.state.stamp, g.menu.refreshes), (9, refreshes + 1))
        self.assertIn('x4lan_lobby_copy', self.ids(g))
        # the helper goes away: a request stops at its begin, and the page says so
        g.helper_state = None
        count = len(g.pipes)
        lobby.request('START')
        self.assertEqual(self.pipes(g, count), ['tbegin'] * 100)
        self.assertTrue(any(t == '[X4LAN] LOBBY REQUEST_UNSENT no helper answers' for t in self.logs(g)))
        self.frames(g, 40 * 3)
        self.assertEqual(self.rows(g)[0].name(), 'The multiplayer helper is not running.')
        self.assertEqual(self.rows(g)[1].name(), 'Start the multiplayer helper from the Windows Start menu, then open this page again.')

    def test_server_pages_keep_their_pipe_names(self):
        lua, g = self.start("{pipe = 'x4mp-page', server = true}", extra='\npackage = nil\n')
        for text in ('JOIN', 'JOIN4', 'CANCEL'):
            g.X4LAN_LOBBY.request(text)
        self.assertEqual(self.pipes(g), ['join', 'join4', 'cancel'])
        self.assertEqual(self.helper_requests(g), [])

    def test_a_replacement_menu_as_kuertees_ui_extensions(self):
        lua, g = self.start(menu=KUERTEE)
        self.frames(g, 5)
        self.assertFalse(g.X4LAN_LOBBY.installed)                 # the replacement registers its menu later
        lua.execute('table.insert(Menus, late_menu)')
        self.frames(g, 1)
        self.assertTrue(g.X4LAN_LOBBY.installed)
        g.menu.submenuHandler('main')
        self.assertEqual(self.ids(g), ['continue', 'multiplayer', 'line', 'x4lan_lobby', 'settings', 'quit'])
        self.state(lua, "{stamp = 1, phase = 'idle', lines = {'ready'}}")
        self.open(g)
        self.assertEqual(self.ids(g), LINES + START_ROWS)          # the other mod's row is not on this page
        self.row(g, 'x4lan_lobby_code').edit.handlers.onEditBoxDeactivated(None, 'K7QX3FNA', True, True)
        self.assertEqual(self.requests(g)[-1], 'JOINCODE|K7QX3FNA')
        g.menu.refresh()
        self.assertEqual(self.ids(g), LINES + START_ROWS)
        g.menu.submenuHandler('multiplayer')                       # the stock page and the other mod's row are left alone
        self.assertEqual(self.ids(g), ['multiplayer_server', 'lobby', 'other_mod_row'])
        self.assertIn('multiplayer', list(g.pre_pages.values()))
        self.assertFalse(any('ERROR' in t for t in self.logs(g)), self.logs(g))

    def test_a_menu_without_its_functions_is_left_as_it_is(self):
        lua, g = self.start(extra='\nmenu.displayOptions = nil\nstock_row, stock_handler = menu.displayOption, menu.submenuHandler\n')
        self.frames(g, 5)
        refusals = [t for t in self.logs(g) if 'MENU_REFUSED' in t]
        self.assertEqual(refusals, ['[X4LAN] LOBBY MENU_REFUSED why=no_menu_functions'])
        self.assertFalse(g.X4LAN_LOBBY.installed)
        self.assertTrue(lua.eval('menu.displayOption == stock_row and menu.submenuHandler == stock_handler'))

    def test_failures_while_drawing_stay_out_of_the_menu(self):
        # the menu's row function fails on dropdowns: that row is left out, the rest is drawn
        lua, g = self.start(extra="""
local row = menu.displayOption
function menu.displayOption(ftable, option, numCols)
 if option.valuetype == 'dropdown' then error('no dropdowns here') end
 return row(ftable, option, numCols)
end
""")
        self.state(lua, "{stamp = 1, phase = 'idle', lines = {}}")
        self.open(g)
        self.assertEqual(self.ids(g), LINES + [i for i in START_ROWS if i != 'x4lan_lobby_players'])
        self.assertTrue(any('DRAW_ERROR x4lan_lobby_players' in t for t in self.logs(g)))
        # a state that breaks the rows: the lines are drawn
        lua.execute("X4LAN_LOBBY.state = setmetatable({phase = 'idle'}, {__index = function(_, key) error('bad state ' .. key) end})")
        self.open(g)
        self.assertEqual(self.ids(g), ['header'] * 6)
        self.assertTrue(any(t.startswith('[X4LAN] LOBBY DRAW_ERROR rows') for t in self.logs(g)))
        # a table that cannot make edit boxes: their plain rows instead, said once
        lua, g = self.start(extra='\nfunction new_table() return {} end\n')
        self.state(lua, "{stamp = 1, phase = 'idle', lines = {}}")
        self.open(g)
        self.open(g)
        self.assertEqual(self.ids(g), LINES + START_ROWS)
        self.assertEqual(self.row(g, 'x4lan_lobby_code').name, 'Typing is not available here: copy the code, then use the clipboard row')
        self.assertEqual(sum('EDIT_UNAVAILABLE' in t for t in self.logs(g)), 1)
        # a button whose action fails does not raise into X4's menu
        lua.execute("X4LAN_LOBBY.form.players = nil")
        self.row(g, 'x4lan_lobby_host').callback()
        self.assertTrue(any('ACTION_ERROR x4lan_lobby_host' in t for t in self.logs(g)))


if __name__ == '__main__':
    unittest.main()
