"""The Multiplayer page's views (tools/dev/native_gate_lobby.lua, docs/MULTIPLAYER-PAGE.md): every phase drawn as X4
draws its own menus, into X4's options frame, and every button sending the request of docs/NETPLAY.md. Under LuaJIT
(lupa) with a stand-in of X4's widget handles that refuses what X4 refuses (x4_widget_fake.py); no X4 runs here. The
rows the page draws when the views cannot be drawn are test_native_gate_lobby_page.py's."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import x4_widget_fake as fake  # noqa: E402

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:   # pragma: no cover
    LuaRuntime = None

ROOT = Path(__file__).resolve().parents[1]
LOBBY = (ROOT / 'tools/dev/native_gate_lobby.lua').read_text(encoding='utf-8')
STATES = json.loads((ROOT / 'tools/dev/page_preview_states.json').read_text(encoding='utf-8'))
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import lobby as launcher_lobby  # noqa: E402

BASE = r"""
logs, scripts, pipes, helper_requests = {}, {}, {}, {}
io, os = nil, nil
function DebugError(text) logs[#logs + 1] = text end
function SetScript(name, fn) scripts[name] = fn end
clock = 100
function getElapsedTime() return clock end
helper_state = "return {stamp = 1, phase = 'idle', lines = {'Helper here.'}}"
local PREFIX = '\\\\.\\pipe\\x4mp-page-'
local buffer
do local real_loadfile = loadfile
 function loadfile(path)
  if path:sub(1, 9) == '\\\\.\\pipe\\' then
   pipes[#pipes + 1] = path
   local name = path:sub(#PREFIX + 1)
   if helper_state == nil then return nil, 'cannot open ' .. path end
   if name == 'tbegin' then buffer = ''
   elseif name:match('^t[0-9a-f]$') and buffer then buffer = buffer .. name:sub(2)
   elseif name == 'tend' and buffer then
    helper_requests[#helper_requests + 1] = buffer:gsub('..', function(h) return string.char(tonumber(h, 16)) end)
    buffer = nil
   end
   return loadstring(helper_state)
  end
  return real_loadfile(path)
 end
end
drawn = {}
config = {optionDefinitions = {main = {{id = 'continue'}, {id = 'line'}, {id = 'settings'}, {id = 'quit'}},
  multiplayer = {name = 'Multiplayer', {id = 'multiplayer_server'}, {id = 'lobby'}}}}
"""
MODULES = r"""
function GetRegisteredModules()
  return {{id = 'x4ep1_gamestart_fight', name = 'The Dedicated Warrior', description = 'A fighter pilot.', image = 'gamestart_fight'},
          {id = 'x4ep1_gamestart_trade', name = 'The Young Trader', description = 'A trader.', image = ''}}
end
"""


def state_of(row, stamp=1):
    """A preview state (tools/dev/page_preview_states.json) as the launcher writes it."""
    fields = {k: v for k, v in row.items() if k not in ('name', 'phase', 'lines', 'hold')}
    text = launcher_lobby.state_text(stamp, row['phase'], row.get('lines') or [], **fields)
    return '(function() ' + text + ' end)()'


def preview(name):
    return next(row for row in STATES if row['name'] == name)


@unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
class Views(unittest.TestCase):
    def start(self, lobby="{netplay = true}", extra=''):
        lua = LuaRuntime()
        lua.execute(BASE + fake.WIDGETS + fake.MENU + extra + '\nX4LAN_CONFIG = {enabled = false, lobby = ' + lobby + '}\n')
        lua.execute(LOBBY)
        g = lua.globals()
        g.scripts.onUpdate()
        return lua, g

    def show(self, lua, g, state):
        lua.execute('X4LAN_LOBBY.state = ' + state)
        g.menu.submenuHandler('x4lan_lobby')
        return fake.Drawn(lua, g)

    def logs(self, g):
        return fake.lua_list(g.logs)

    def requests(self, g):
        prefix = '[X4LAN] LOBBY REQUEST '
        return [t[len(prefix):] for t in self.logs(g) if t.startswith(prefix)]

    def clean(self, g):
        self.assertEqual(fake.lua_list(g.widget_errors), [])
        self.assertFalse([t for t in self.logs(g) if 'ERROR' in t or 'FALLBACK' in t], self.logs(g))
        self.assertEqual(fake.lua_list(g.drawn), [])   # no stock row: the views drew the page

    def test_every_phase_draws_its_view_as_x4_draws_its_menus(self):
        expected = {   # texts each view must have
            'idle': ['Games', 'Public', 'On your network', 'Join with a code', 'Host a game', 'Steam', "Steam's Play starts X4 Online"],
            'browsing-games': ['Friday coop', '1 / 2', 'Other version', 'Full', '2 / 4 · running', 'Living room', '41 ms'],
            'idle-host-starts': ['Start', 'Visibility', 'Players', 'Drop-in slots'],
            'preparing-host': ['Getting ready', 'Make the game', 'Put it online for the others', '35 %'],
            'preparing-join-download': ['Find the game', 'Download the game', 'Prepare the game files', '40 %'],
            'hosting-direct': ['Join code', 'K7QX-3FNA', 'Players', 'Player 1', 'Host · You', 'Ready', 'Getting ready',
                               'Connection', 'Direct connection', '38 ms'],
            'hosting-lan': ['Invite code', 'On your local network'],
            'hosting-firewall-ready': ['Through the X4 Online server', 'Windows Firewall blocks direct connections.'],
            'joining': ['Find the game'],
            'waiting': ['Player 2', 'You'],
            'loading': ['Every player is ready', 'Load the game'],
            'error': ['The X4 Online server does not know this game', 'No game has this code. Check it with the host, or ask for a new one.'],
            'nohelper': ['The multiplayer helper is not running.'],
            'choosing': ['Starts', 'With the host (the session start)', 'Fight (Paranid S fighter)'],
            'restart': ['X4 restarts into the game', 'It joins the game there.'],
        }
        for row in STATES:
            lua, g = self.start()
            with self.subTest(state=row['name']):
                drawn = self.show(lua, g, state_of(row))
                self.clean(g)
                texts = drawn.texts()
                self.assertEqual(texts[0], 'Multiplayer')   # X4's title row, then the page's
                for text in expected.get(row['name'], []):
                    self.assertIn(text, texts + list(drawn.buttons()))
                # technical text is not drawn: the launcher's lines stay in the mouse-over of the status line
                for word in ('UDP', 'TCP', 'Cancel to go back.', 'master server', 'not_found', 'KeyError'):
                    self.assertFalse([t for t in texts if word in t], (word, texts))

    def test_the_start_view_lists_games_in_columns_with_join_per_row(self):
        lua, g = self.start(extra=MODULES)
        drawn = self.show(lua, g, state_of(preview('browsing-games')))
        self.clean(g)
        self.assertEqual(g.frames[1].properties.width, 1220)   # X4's extra wide frame, as its list pages
        head, games, forms = drawn.tables
        self.assertEqual((games.numcolumns, forms.numcolumns), (6, 4))
        self.assertEqual((head.properties.nextTable, games.properties.nextHorizontalTable), (games.index, forms.index))
        joins = [c for c in drawn.cells(2) if c.type == 'button' and c.label == 'Join']
        self.assertEqual(len(joins), 3)   # Friday coop, the running drop-in game, Living room; not the old or the full one
        for button in joins:
            button.handlers.onClick()
        self.assertEqual(self.requests(g), ['JOINGAME|g1', 'JOINGAME|g4', 'JOINLAN|1'])
        self.assertIn('Other version', drawn.texts(2))
        # the start by X4's own name (GetRegisteredModules) when the launcher did not list it; the counts in the labels
        texts = drawn.texts(2)
        self.assertIn('The Dedicated Warrior', texts)
        self.assertNotIn('fight', texts)
        self.assertIn('Public (4)', texts)
        self.assertIn('On your network (1)', texts)
        self.assertFalse([t for t in texts if t in ('4', '1')], texts)
        self.assertIn('GAMES public=4 lan=1 phase=browsing stamp=1', [t.replace('[X4LAN] LOBBY ', '') for t in self.logs(g)])
        drawn.press('Refresh')
        self.assertEqual(self.requests(g)[-1], 'BROWSE')

    def test_join_by_code_and_by_clipboard(self):
        lua, g = self.start()
        drawn = self.show(lua, g, state_of(preview('idle')))
        box = drawn.of_type('editbox')[0]
        self.assertEqual(g.props_of(box.properties).defaultText, 'Join code, invite code or address')
        box.handlers.onEditBoxActivated()
        box.handlers.onTextChanged(None, ' k7qx-3fna ')
        drawn.press('Join')
        box.handlers.onEditBoxDeactivated(None, 'K7QX-3FNA', None, True, False)   # Enter
        box.handlers.onEditBoxDeactivated(None, 'K7QX-3FNA', None, True, True)    # a click elsewhere: nothing
        drawn.press('Use the code on my clipboard')
        self.assertEqual(self.requests(g), ['JOINCODE|k7qx-3fna', 'JOINCODE|K7QX-3FNA', 'JOIN'])

    def test_the_host_form(self):
        lua, g = self.start(extra=MODULES)
        drawn = self.show(lua, g, state_of(preview('idle-host-starts')))
        self.clean(g)
        dropdowns = drawn.of_type('dropdown')
        start, visibility, players, drop_in = dropdowns
        self.assertEqual([o.text for o in fake.lua_list(start.options)], ['Fight', 'Trade', 'Discover'])
        self.assertEqual(g.props_of(start.properties).startOption, 'x4ep1_gamestart_fight')   # the launcher's default
        drawn.press('Host the game')
        self.assertEqual(self.requests(g)[-1], 'HOST|0|2|X4 game|x4ep1_gamestart_fight')
        start.handlers.onDropDownConfirmed(None, 'x4ep1_gamestart_trade')
        visibility.handlers.onDropDownConfirmed(None, 'public')
        players.handlers.onDropDownConfirmed(None, '7')
        drop_in.handlers.onDropDownConfirmed(None, '3')             # 7 + 3 > 8: the players go down (fit_counts)
        name = drawn.of_type('editbox')[1]
        name.handlers.onTextChanged(None, 'Friday|night')
        drawn.press('Host the game')
        self.assertEqual(self.requests(g)[-1], 'HOST|1|5+3|Friday night|x4ep1_gamestart_trade')
        self.assertTrue(g.X4LAN_LOBBY.redraw_next)                   # the counts are drawn again at the next frame
        summary = [t for t in drawn.texts() if t.startswith('Up to')]
        self.assertEqual(summary, ['Up to 8 players: 5 load together, 3 more join while the game runs.'])
        # X4's picture of the chosen start, following the dropdown without a redraw
        icons = [c for c in drawn.of_type('icon') if lua.eval('type')(c.icon) == 'function']
        self.assertEqual(icons[0].icon(), 'gamestart_default')       # The Young Trader has no image of its own
        start.handlers.onDropDownConfirmed(None, 'x4ep1_gamestart_fight')
        self.assertEqual(icons[0].icon(), 'gamestart_fight')

    def test_steam_and_single_player(self):
        lua, g = self.start("{netplay = true, single = true}")
        drawn = self.show(lua, g, state_of(preview('idle')))
        self.clean(g)
        drawn.press('On')
        drawn.press('Single-player')
        self.assertEqual(self.requests(g), ['STEAMPLAY|0', 'SINGLEPLAYER'])
        drawn = self.show(lua, g, state_of(preview('browsing-empty'), 2))
        drawn.press('Off')
        drawn.press('Allow')
        self.assertEqual(self.requests(g)[-2:], ['STEAMPLAY|1', 'FIREWALL'])
        self.assertIn('This takes effect once Steam is closed.', drawn.texts())
        self.assertNotIn('Restart Steam', drawn.buttons())   # only in a Steam-started X4 (a pipe page)

    def test_the_game_view(self):
        lua, g = self.start()
        drawn = self.show(lua, g, state_of(preview('hosting-direct')))
        self.clean(g)
        code = next(c for c in drawn.cells() if c.type == 'text' and c.text == 'K7QX-3FNA')
        self.assertEqual(g.props_of(code.properties).font, 'Zekton bold fixed')
        self.assertFalse(drawn.active('Start the game'))
        drawn.press('Start the game')                                  # inactive: nothing sent
        drawn.press('Copy')
        self.assertEqual(drawn.buttons().keys() & {'Copied'}, {'Copied'})
        g.clock = 103
        self.assertIn('Copy', drawn.buttons())
        drawn.press('Close the game')
        self.assertEqual(self.requests(g), ['COPY', 'CANCEL'])
        colours = {c.text: g.props_of(c.properties).color for c in drawn.cells() if c.type == 'text'}
        self.assertEqual((colours['Ready'].name, colours['Getting ready'].name), ('text_positive', 'text_inprogress'))
        self.assertEqual(colours['Player 1'].name, 'text_player_current')   # you
        # every player ready: Start
        state = dict(preview('hosting-firewall-ready'), game={'name': 'Friday coop', 'players_max': 4, 'open': [4],
                                                              'start': 'x4ep1_gamestart_fight', 'public': True})
        drawn = self.show(lua, g, state_of(state, 2))
        self.clean(g)
        self.assertTrue(drawn.active('Start the game'))
        drawn.press('Start the game')
        drawn.press('Allow')
        self.assertEqual(self.requests(g)[-2:], ['START', 'FIREWALL'])
        texts = drawn.texts()
        self.assertIn('Open', texts)                    # slot 3 of the common load
        self.assertIn('Joins later', texts)             # slot 4, a drop-in slot
        self.assertIn('2 / 4', texts)
        self.assertIn('Friday coop · Public · Start: fight', texts)   # no host_starts here: the start's short id
        # X4's refresh keeps the selected row of its table by its id: the page selects it again
        g.menu.preselectOption = 'x4lan_lobby_actions'
        drawn = self.show(lua, g, state_of(state, 4))
        head = drawn.tables[0]
        self.assertEqual(fake.lua_list(head.rows)[head.selected - 1].rowdata.id, 'x4lan_lobby_actions')
        g.menu.preselectOption = None
        # a joiner: Leave, no Start
        drawn = self.show(lua, g, state_of(preview('waiting'), 3))
        self.assertNotIn('Start the game', drawn.buttons())
        drawn.press('Leave the game')
        self.assertEqual(self.requests(g)[-1], 'CANCEL')
        self.assertIn('Joined Friday coop as player 2. The game loads when every player is ready.', drawn.texts())

    def test_a_start_is_named_as_this_pcs_x4_names_it(self):
        read_text = r"""
function ReadText(page, line) if page == 1021 and line == 7 then return 'Der Kampfpilot' end return 'ReadText' .. page .. '-' .. line end
"""
        lua, g = self.start(extra=MODULES + read_text)
        names = lua.eval('X4LAN_LOBBY.game_start_name')
        lua.execute("X4LAN_LOBBY.state = {stamp = 1, phase = 'idle', lines = {}, host_starts = {{id = 'x4ep1_gamestart_fight', "
                    "page = 1021, line = 7, label = 'Fight'}}}")
        self.assertEqual(names('x4ep1_gamestart_fight'), 'Der Kampfpilot')        # the launcher's list: X4's text
        self.assertEqual(names('x4ep1_gamestart_trade'), 'The Young Trader')      # not listed: X4's own start name
        self.assertEqual(names('x4ep1_gamestart_split2'), 'split2')               # neither: the id's short form
        self.assertIsNone(names('../x'))
        drawn = self.show(lua, g, state_of(dict(preview('hosting-direct'), host_starts=None)))
        self.assertIn('Friday coop · Private · Start: The Dedicated Warrior', drawn.texts())

    def test_the_steam_path_states_and_the_in_session_page_preview(self):
        steam = json.loads((ROOT / 'tools/dev/page_preview_steam.json').read_text(encoding='utf-8'))
        for row in steam:
            lua, g = self.start("{netplay = true, single = true}", extra=MODULES)
            with self.subTest(state=row['name']):
                drawn = self.show(lua, g, state_of(row))
                self.clean(g)
                if row['name'] == 'session-page':
                    for text in ('In the game. You are player 2 of 3.', 'K7QX-3FNA', 'Through the X4 Online server', '82 ms',
                                 '12:34'):
                        self.assertIn(text, drawn.texts())
                    drawn.press('Copy')
                    self.assertEqual(self.requests(g), ['COPY'])
                if row['name'] == 'session-page-leave':
                    self.assertEqual({'Leave', 'Stay'} - set(drawn.buttons()), set())

    def test_a_lan_invite_is_not_drawn_but_copied(self):
        lua, g = self.start()
        drawn = self.show(lua, g, state_of(dict(preview('hosting-lan'), game={'address': '192.168.1.20', 'players_max': 2})))
        self.clean(g)
        self.assertFalse([t for t in drawn.texts() if 'X4MP-' in t])
        self.assertTrue([t for t in drawn.tips() if t.startswith('X4MP-192.168.1.20')])
        self.assertIn('Players on your network can also join with the address 192.168.1.20.', drawn.texts())
        drawn.press('Copy')
        self.assertEqual(self.requests(g), ['COPY'])

    def test_the_progress_view_follows_the_state(self):
        lua, g = self.start()
        drawn = self.show(lua, g, state_of(preview('preparing-join-download')))
        self.clean(g)
        bar = drawn.of_type('statusbar')[0]
        current = g.props_of(bar.properties).current
        self.assertAlmostEqual(current(), 0.4)
        lua.execute("X4LAN_LOBBY.state.progress.done = 0.75")
        self.assertAlmostEqual(current(), 0.75)                     # the bar reads the state every frame
        lua.execute("X4LAN_LOBBY.state.progress = nil")
        self.assertTrue(0 <= current() <= 1)                         # nothing measured: it sweeps
        drawn = self.show(lua, g, state_of(preview('preparing-join-download'), 2))
        ticks = [c for c in drawn.of_type('icon') if c.icon == 'widget_tick_01']
        self.assertEqual(len(ticks), 1)                              # Find the game: done
        drawn.press('Cancel')
        drawn = self.show(lua, g, state_of(preview('loading'), 3))
        drawn.press('Leave')
        self.assertEqual(self.requests(g), ['CANCEL', 'CANCEL'])

    def test_errors_say_what_happened_and_go_back_or_try_again(self):
        lua, g = self.start()
        drawn = self.show(lua, g, state_of(preview('idle')))
        drawn.press('Join')                                          # nothing typed: nothing sent
        box = drawn.of_type('editbox')[0]
        box.handlers.onTextChanged(None, 'ZZZZ-ZZZZ')
        drawn.press('Join')
        drawn = self.show(lua, g, state_of(preview('error'), 2))
        self.clean(g)
        title = next(c for c in drawn.cells() if c.type == 'text' and str(c.text).startswith('The X4 Online server'))
        self.assertEqual(g.props_of(title.properties).color.name, 'text_error')
        drawn.press('Try again')
        drawn.press('Back')
        self.assertEqual(self.requests(g), ['JOINCODE|ZZZZ-ZZZZ', 'JOINCODE|ZZZZ-ZZZZ', 'CANCEL'])
        drawn = self.show(lua, g, "{stamp = 3, phase = 'error', lines = {'Something went wrong in the launcher:', "
                                  "'KeyError: 7', 'Cancel to go back.'}}")
        self.assertIn('KeyError: 7', drawn.texts())                  # an older launcher's line is shown as it came
        self.assertIn('Something went wrong in the launcher', drawn.texts())   # a title without its colon
        # nothing asked before: no Try again
        lua, g = self.start()
        drawn = self.show(lua, g, state_of(preview('error')))
        self.assertNotIn('Try again', drawn.buttons())

    def test_no_helper_and_try_again(self):
        lua, g = self.start("{netplay = true, pipe = 'x4mp-page'}")
        g.helper_state = None
        for _ in range(200):
            g.scripts.onUpdate()
        drawn = self.show(lua, g, 'X4LAN_LOBBY.state')
        self.clean(g)
        self.assertIn('The multiplayer helper is not running.', drawn.texts())
        g.helper_state = "return {stamp = 7, phase = 'idle', lines = {}}"
        drawn.press('Try again')
        self.assertEqual(g.X4LAN_LOBBY.state.phase, 'starting')
        for _ in range(80):
            g.scripts.onUpdate()
        self.assertEqual(g.X4LAN_LOBBY.state.phase, 'idle')

    def test_the_restart_notice_counts_down(self):
        lua, g = self.start("{netplay = true, pipe = 'x4mp-page'}")
        g.helper_state = launcher_lobby.state_text(5, 'restart', ['X4 closes now.'], quit_after=4)
        for _ in range(45):
            g.scripts.onUpdate()
        g.menu.submenuHandler('x4lan_lobby')
        drawn = fake.Drawn(lua, g)
        self.clean(g)
        current = g.props_of(drawn.of_type('statusbar')[0].properties).current
        self.assertAlmostEqual(current(), 1.0)
        g.clock = 102
        self.assertAlmostEqual(current(), 0.5)

    def test_the_start_choice_of_a_drop_in_join(self):
        lua, g = self.start(extra=MODULES)
        state = dict(preview('choosing'))
        state['choices'] = [dict(c, gamestart='x4ep1_gamestart_fight') if c['index'] == 1 else c for c in state['choices']]
        drawn = self.show(lua, g, state_of(state))
        self.clean(g)
        head = drawn.tables[0]
        ids = [r.rowdata.id for r in fake.lua_list(head.rows) if r.rowdata and lua.eval('type')(r.rowdata) == 'table']
        self.assertEqual(ids[:3], ['x4lan_lobby_choice0', 'x4lan_lobby_choice1', 'x4lan_lobby_choice2'])
        starts = [c for c in drawn.cells(1) if c.type == 'button' and c.label == 'Start here']
        starts[2].handlers.onClick()
        starts[0].handlers.onClick()
        drawn.press('Cancel')
        self.assertEqual(self.requests(g), ['JOINSTART|2', 'JOINSTART|0', 'CANCEL'])
        # the selected start beside the list (X4 puts the selected row's data into menu.selectedOption)
        picture = next(c for c in drawn.cells(2) if c.type == 'icon')
        fight = next(r for r in fake.lua_list(head.rows) if r.rowdata and lua.eval('type')(r.rowdata) == 'table'
                     and r.rowdata.id == 'x4lan_lobby_choice1')
        g.menu.selectedOption = fight.rowdata
        self.assertEqual(picture.icon(), 'gamestart_fight')
        lua.execute("X4LAN_LOBBY.state.phase = 'session'")
        starts[1].handlers.onClick()                                  # not choosing any more: nothing sent
        self.assertEqual(len(self.requests(g)), 3)

    def test_a_menu_without_the_frame_gets_the_rows_and_a_failing_view_falls_back(self):
        lua, g = self.start()
        lua.execute('menu.createOptionsFrame = function() menu.optionsFrame = nil return new_frame(800) end')
        lua.execute("X4LAN_LOBBY.state = {stamp = 1, phase = 'idle', lines = {}}")
        g.menu.submenuHandler('x4lan_lobby')
        self.assertIn('x4lan_lobby_host', [r.id for r in fake.lua_list(g.drawn)])   # the rows of before
        lua, g = self.start()
        lua.execute("X4LAN_LOBBY.native.start_view = function() error('a view that fails') end")
        lua.execute("X4LAN_LOBBY.state = {stamp = 1, phase = 'idle', lines = {}}")
        g.menu.submenuHandler('x4lan_lobby')
        self.assertEqual(sum('DRAW_FALLBACK' in t for t in self.logs(g)), 1)
        g.scripts.onUpdate()                                          # the next frame: drawn again, with the rows
        self.assertIn('x4lan_lobby_host', [r.id for r in fake.lua_list(g.drawn)])

    def test_the_views_send_through_the_helpers_pipes(self):
        lua, g = self.start("{netplay = true, pipe = 'x4mp-page'}", extra=MODULES)
        g.helper_state = state_of(preview('idle-host-starts')).replace('(function() ', '').replace(' end)()', '')
        for _ in range(45):
            g.scripts.onUpdate()
        g.menu.submenuHandler('x4lan_lobby')
        drawn = fake.Drawn(lua, g)
        self.clean(g)
        drawn.press('Host the game')
        self.assertEqual(fake.lua_list(g.helper_requests)[-1], 'HOST|0|2|X4 game|x4ep1_gamestart_fight')


PROBE = (ROOT / 'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
MP_BLOCK = PROBE[PROBE.index('-- In-session multiplayer menu'):PROBE.index("SetScript('onUpdate',function()",
                                                                          PROBE.index('-- In-session multiplayer menu'))]
SESSION = r"""
logs, stops, active, requests, files = {}, 0, 1, {}, {}
clock = 100
function getElapsedTime() return clock end
function DebugError(text) requests[#requests + 1] = text end
function log(text) logs[#logs + 1] = text end
gate = {poll = function() return {active = active, issued = 1830} end, stop = function() stops = stops + 1 active = 0 return true end}
clients = 3
function loadfile(path)
  if not files[path] then return nil, 'cannot open ' .. path end
  return loadstring(files[path])
end
drawn = {}
config = {optionDefinitions = {main = {{id = 'continue'}, {id = 'settings'}},
  multiplayer = {name = 'Multiplayer', {id = 'multiplayer_server'}, {id = 'lobby'}}}}
"""


@unittest.skipUnless(LuaRuntime, 'needs lupa (LuaJIT)')
class SessionPage(unittest.TestCase):
    """The probe's in-session page (native_gate_probe.lua mp_menu) through the lobby's X4LAN_MP_UI."""

    def test_the_session_page_as_x4_draws_its_menus(self):
        lua = LuaRuntime()
        lua.execute(SESSION + fake.WIDGETS + fake.MENU + '\nmenu.isStartmenu = false\n')
        lua.execute("X4LAN_CONFIG = {lobby = {files = {}, session = true}}")
        lua.execute(LOBBY)   # the session's ui files: the lobby file first, which defines X4LAN_MP_UI
        g = lua.globals()
        self.assertIsNotNone(g.X4LAN_MP_UI)
        g.files['./extensions/x4_lan_lab/lobby/state.lua'] = launcher_lobby.state_text(
            7, 'session', ['You are player 2 of 3.'], code='K7QX-3FNA', network={'mode': 'relay', 'ping_ms': 81.6},
            players=[{'slot': 1, 'ready': True, 'host': True}, {'slot': 2, 'ready': True}])
        lua.execute("cfg = {slot = 'p2', native_gate_tick_hz = 30, host = '127.0.0.1', port = 47000, lobby = {files = {}, session = true}}")
        install = lua.execute(MP_BLOCK + '\nreturn mp_menu.install')
        install()
        g.menu.submenuHandler('x4lan_mp')
        drawn = fake.Drawn(lua, g)
        self.assertEqual(fake.lua_list(g.widget_errors), [])
        self.assertEqual(fake.lua_list(g.drawn), [])          # no stock rows: the parts drew the page
        texts = drawn.texts()
        for text in ('In the game. You are player 2 of 3.', 'Join code', 'K7QX-3FNA', 'Players', 'Host', 'You',
                     'Connection', 'Through the X4 Online server', '82 ms', 'Playing for', '1:01'):
            self.assertIn(text, texts)
        for word in ('Relay 127.0.0.1', 'Step 1830', 'desyncs.log'):   # mouse-overs now
            self.assertFalse([t for t in texts if word in t])
        self.assertTrue([t for t in drawn.tips() if 'Relay 127.0.0.1:47000' in t])
        drawn.press('Copy')
        self.assertIn('Copied', drawn.buttons())
        drawn.press('Leave the game')                         # asks first
        drawn = fake.Drawn(lua, g)
        self.assertIn('Leave? The others play on without you; this game runs on alone.', drawn.texts())
        drawn.press('Stay')
        drawn = fake.Drawn(lua, g)
        self.assertEqual(g.stops, 0)
        drawn.press('Leave the game')
        fake.Drawn(lua, g).press('Leave')
        self.assertEqual(g.stops, 1)
        requests = fake.lua_list(g.requests)
        self.assertEqual([r for r in requests if r.startswith('[X4LAN] LOBBY REQUEST')],
                         ['[X4LAN] LOBBY REQUEST COPY', '[X4LAN] LOBBY REQUEST LEAVE'])
        drawn = fake.Drawn(lua, g)
        self.assertIn('You left the session: this game runs on alone.', drawn.texts())
        self.assertFalse(drawn.active('Leave the game'))


if __name__ == '__main__':
    unittest.main()
