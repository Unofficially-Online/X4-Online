-- Lobby (x4ls lobby): the session is chosen in X4's main menu, not on a command line.
--
-- x4ls lobby starts this PC's X4 with the play overlay installed but only this file listed in ui.xml (after config.lua and
-- sha256.lua). In the start menu it adds a "Multiplayer" entry to X4's own options menu (before Settings). The page has
-- one of three forms:
--   lobby.netplay (a release's menu extension, or x4ls lobby running netplay): the launcher's state table
--     (docs/NETPLAY.md, "The page and the launcher"): the state's lines, then by phase either Host a game (public or
--     private, 2..8 players, the start every player starts in, a name), Join a game (a text box for a join code, a long
--     invite code or an address; or the code on the clipboard), the public and LAN games, or the game being made: its
--     code (Copy), the connection, the players, Start for the host, Cancel.
--   lobby.server (a playtest kit with a server): "Join the game", and the start picker of a running game.
--   neither (the LAN lobby of before netplay): "Host a game" and "Join a game (invite code from the clipboard)".
-- The netplay page is drawn as X4 draws its own menus (docs/MULTIPLAYER-PAGE.md): one view per phase (start, progress,
-- game, error, restart, choosing) with X4's fonts, colours, icons, a games table with Join per row, a status bar, the
-- join code big; the launcher's lines are the status line's mouse-over. The rows below stay for every request and for an
-- options menu the views cannot draw into (draw_rows; the server and LAN pages always use them).
--
-- The game and the launcher talk in the two ways X4's UI Lua allows (it has no io library, and the Win32 calls of
-- LuaJIT's ffi are refused there: "Access to FFI function ... is restricted"):
--   game -> launcher: a log row "[X4LAN] LOBBY REQUEST <text>" (the contract's requests:
--     HOST|<public>|<players>|<name>[|<start>], JOINCODE|<text>, JOIN, BROWSE, JOINGAME|<id>, JOINLAN|<index>, START,
--     CANCEL, COPY; the LAN and server pages send HOST, JOIN, JOIN<k>, COPY and CANCEL as before); the launcher follows
--     the log. The launcher, not the game, reads and writes the clipboard.
--   launcher -> game: lobby/state.lua in the installed extension, a Lua file that returns a table (what the page shows);
--     loadfile reads it again a few times a second.
-- Once every player is there the launcher rewrites ui/config.lua with the session (relay, token, slot, the common load
-- second) and says so in the state. Then this file runs the new config.lua and the overlay's ui files in their ui.xml
-- order, as X4 would have, and the probe takes over: it loads the snapshot at the agreed second.
-- After the load X4 builds a new UI state and runs ui.xml again: config.lua now says lobby.session, and the overlay's
-- files are run at once, with no menu.
--
-- No stock file is changed. The menu functions are wrapped as the in-session page does (native_gate_probe.lua mp_menu),
-- through the menu table only (menu.displayOption, menu.displayOptions, menu.submenuHandler and the stock "multiplayer"
-- page definition), so X4's stock options menu and replacements that keep these (kuertee's UI Extensions) both work.
-- A missing function is one MENU_REFUSED row and the menu stays as it is; every row is drawn under pcall.
--
-- The same page in an X4 that Steam started (lobby.pipe, the menu extension of a kit): that X4 runs the player's own
-- profile, with no launcher behind it, no log file and X4's protected UI mode on (no package library). loadfile still
-- opens a named pipe there, so a helper outside the game (lockstep.launcher.steamplay) answers every open with the state,
-- and which pipe was opened is the request: <pipe>-state, the old requests by their old names (<pipe>-join, -join<k>;
-- the server and LAN pages also -host, -copy, -cancel), and every netplay request spelled through fixed names, as
-- typed text cannot be a pipe name the helper made beforehand: <pipe>-tbegin, one <pipe>-t0 .. -tf per digit of the
-- request's UTF-8 bytes in lowercase hex, <pipe>-tend (at most 200 bytes, all in one frame). The session cannot run
-- in the player's own profile: on Join the helper says "restart", this file quits X4, and the helper starts X4 again as
-- x4ls lobby does, in the disposable profile, where it joins by itself. Nothing of the player's profile is read or written.
local cfg = X4LAN_CONFIG
local lobby_config = type(cfg) == 'table' and cfg.lobby or nil
if type(lobby_config) ~= 'table' then
  return
end

local function note(text)
  DebugError('[X4LAN] LOBBY ' .. tostring(text))
end

local DIR = './extensions/x4_lan_lab/'
local PIPE = type(lobby_config.pipe) == 'string' and ('\\\\.\\pipe\\' .. lobby_config.pipe .. '-') or nil

-- A file of the installed extension as a chunk; nil and the reason when it is not there or does not compile.
local function chunk_of(name)
  local ok, chunk, why = pcall(loadfile, DIR .. name)
  if ok and chunk then
    return chunk
  end
  return nil, tostring(ok and why or chunk)
end

-- The overlay's ui files, in their ui.xml order, as X4's loader would have run them.
local function run_files()
  for _, name in ipairs(lobby_config.files or {}) do
    local chunk, why = chunk_of(name)
    if not chunk then
      note('ERROR missing ' .. name .. ' ' .. why)
      return false
    end
    local ok, err = pcall(chunk)
    if not ok then
      note('ERROR ' .. name .. ' ' .. tostring(err))
      return false
    end
  end
  note('SESSION_FILES run=' .. #(lobby_config.files or {}))
  return true
end

-- X4LAN_MP_UI: the parts the Multiplayer pages are drawn with, in the style of X4's own menus (docs/MULTIPLAYER-PAGE.md,
-- "How the page is drawn"): text styles, X4's colours by name, section headers, separators, buttons, icons, dropdowns,
-- status bars. They call X4's widget functions (Helper's table, row and cell handles) on tables X4 made or the page added
-- to X4's frame, and only while a page is drawn. Defined before the session branch, so that a session the lobby made has
-- them: the probe's in-session page draws with them there (UI.session_page).
local UI = {}
X4LAN_MP_UI = UI
UI.FONT = 'Zekton outlined'           -- the options menu's text
UI.FONT_BOLD = 'Zekton bold outlined'
UI.FONT_CODE = 'Zekton bold fixed'    -- the join code: one width per character, so 8 and B, 5 and S read apart
UI.ROW = 19                           -- a row of text, a button, a dropdown, an edit box (1080p pixels, X4 scales)
UI.SECTION = 22                       -- a section header
UI.INFO = 16                          -- a line of small text, an icon
UI.CODE = 40                          -- the join code's row
UI.BAR = 12                           -- a status bar

-- An X4 colour by its mapping name (libraries/colors.xml); nil outside X4 or for a name this X4 does not have (rawget:
-- X4's own lookup logs every unknown name).
function UI.color(name)
  if type(name) ~= 'string' or type(Color) ~= 'table' then
    return nil
  end
  return rawget(Color, name)
end

-- Seconds of X4's clock (getElapsedTime); 0 without it.
function UI.now()
  if type(getElapsedTime) == 'function' then
    local ok, seconds = pcall(getElapsedTime)
    if ok and type(seconds) == 'number' then
      return seconds
    end
  end
  return 0
end

local STYLES = {
  section = {font = UI.FONT_BOLD, fontsize = 10, x = 5, y = 2, minRowHeight = UI.SECTION, titleColor = 'row_title'},
  column = {font = UI.FONT_BOLD, fontsize = 9, x = 5, y = 2, color = 'text_lowlight'},
  label = {font = UI.FONT, fontsize = 10, x = 5, y = 2},
  strong = {font = UI.FONT_BOLD, fontsize = 10, x = 5, y = 2},
  inactive = {font = UI.FONT, fontsize = 10, x = 5, y = 2, color = 'text_inactive'},
  status = {font = UI.FONT, fontsize = 9, x = 5, y = 2, minRowHeight = UI.INFO, color = 'text_lowlight'},
  info = {font = UI.FONT, fontsize = 9, x = 5, y = 2, wordwrap = true, minRowHeight = UI.INFO, color = 'text_lowlight'},
  body = {font = UI.FONT, fontsize = 10, x = 5, y = 2, wordwrap = true},
  warning = {font = UI.FONT, fontsize = 10, x = 5, y = 2, wordwrap = true, color = 'text_warning'},
  title = {font = UI.FONT_BOLD, fontsize = 12, x = 5, y = 3, minRowHeight = UI.SECTION},
  code = {font = UI.FONT_CODE, fontsize = 20, x = 10, y = 6, minRowHeight = UI.CODE},
}
UI.STYLES = STYLES

-- Text properties for X4's createText: a style with its colour names as X4's colours, and extra properties over it
-- (a colour there may be a name too).
function UI.style(name, extra)
  local out = {}
  for key, value in pairs(STYLES[name] or STYLES.label) do
    out[key] = value
  end
  for key, value in pairs(extra or {}) do
    out[key] = value
  end
  for _, key in ipairs({'color', 'titleColor', 'cellBGColor'}) do
    if type(out[key]) == 'string' then
      out[key] = UI.color(out[key])
    end
  end
  return out
end

-- The cell of `row` at column `col`, spanning `span` columns (1 without it). A cell under the colspan of a cell to its
-- left (X4's options table gives column 3 a default colspan of 3) shrinks that one first, so it ends before `col`.
function UI.cell(row, col, span)
  local cell = row[col]
  if cell.colspan == 0 then
    for left = col - 1, 1, -1 do
      local owner = row[left]
      if (owner.colspan or 0) > 0 then
        owner:setColSpan(col - left)
        break
      end
    end
  end
  span = span or 1
  if cell.colspan ~= span then
    cell:setColSpan(span)
  end
  return cell
end

local function tip_of(cell, tip)
  if type(tip) == 'string' and tip ~= '' then
    cell.properties.mouseOverText = tip
  end
end

-- A text cell (text may be a function: X4 reads it again every frame).
function UI.text(row, col, span, text, style, extra, tip)
  local cell = UI.cell(row, col, span)
  cell:createText(text, UI.style(style, extra))
  tip_of(cell, tip)
  return cell
end

-- A button with centred text (a function too) in an X4 colour (by name, optional); its action never raises into X4's
-- menu (on_error gets the reason).
function UI.button(row, col, span, text, action, active, tip, on_error, text_color)
  local cell = UI.cell(row, col, span)
  local properties = {active = active ~= false}
  if type(tip) == 'string' and tip ~= '' then
    properties.mouseOverText = tip
  end
  cell:createButton(properties):setText(text, {halign = 'center', fontsize = 10, color = UI.color(text_color)})
  cell.handlers.onClick = function()
    local ok, why = pcall(action)
    if not ok and on_error then
      on_error(why)
    end
  end
  return cell
end

-- An icon of the small text's size, in an X4 colour (by name).
function UI.icon(row, col, icon, color_name, tip)
  local cell = UI.cell(row, col, 1)
  local size = type(Helper) == 'table' and type(Helper.scaleY) == 'function' and Helper.scaleY(UI.INFO) or UI.INFO
  local properties = {width = size, height = size, scaling = false, color = UI.color(color_name)}
  if type(tip) == 'string' and tip ~= '' then
    properties.mouseOverText = tip
  end
  cell:createIcon(icon, properties)
  return cell
end

-- A dropdown of X4's choices ({id, text, icon, displayremoveoption}); on_choose gets the chosen id.
function UI.dropdown(row, col, span, choices, current, on_choose, active, tip)
  local cell = UI.cell(row, col, span)
  local properties = {active = active ~= false, startOption = current, textOverride = ''}
  if type(tip) == 'string' and tip ~= '' then
    properties.mouseOverText = tip
  end
  cell:createDropDown(choices, properties):setTextProperties({halign = 'left'})
  cell.handlers.onDropDownConfirmed = function(_, id)
    pcall(on_choose, id)
  end
  return cell
end

-- A status bar of a fraction 0..1 read every frame from value(); nil from it: the bar sweeps (nothing is measured).
function UI.bar(row, col, span, value)
  local cell = UI.cell(row, col, span)
  local function fraction()
    local ok, done = pcall(value)
    done = ok and tonumber(done) or nil
    if not done then
      return (UI.now() % 1.6) / 1.6
    end
    return math.max(0, math.min(1, done))
  end
  cell:createStatusBar({current = fraction, start = fraction, max = 1, height = UI.BAR,
    cellBGColor = UI.color('row_background_unselectable'), valueColor = UI.color('statusbar_value_default'),
    markerColor = UI.color('statusbar_marker_hidden')})
  return cell
end

-- A 1 px separator over the table's columns from `col`.
function UI.line(ftable, cols, col)
  col = col or 1
  local row = ftable:addRow(false, {})
  local border = type(Helper) == 'table' and Helper.borderSize or 3
  UI.cell(row, col, cols - col + 1):createText(' ', {fontsize = 1, height = border, cellBGColor = UI.color('row_separator')})
  return row
end

-- A section header over the columns from `col`; right: a short text in its last `right_span` columns (1 without it),
-- or a function(row, col) that fills the last column (a button).
function UI.section(ftable, cols, title, right, col, right_span)
  col = col or 1
  right_span = right and (right_span or 1) or 0
  -- X4 takes a button only in a selectable row (row data other than nil or false)
  local row = ftable:addRow(type(right) == 'function' and true or false, {})
  UI.text(row, col, cols - col + 1 - right_span, title, 'section')
  if type(right) == 'function' then
    right(row, cols)
  elseif right then
    UI.text(row, cols - right_span + 1, right_span, right, 'section', {halign = 'right'})
  end
  return row
end

-- The connection of state.network in plain words, and its details for the mouse-over (NETPLAY.md: mode, mapping, ping).
local MAPPING_WORDS = {upnp = 'Your router opened the ports by itself (UPnP).', natpmp = 'Your router opened the ports by itself (NAT-PMP).',
  pcp = 'Your router opened the ports by itself (PCP).', manual = 'A port forwarded on your router reaches this PC.'}
function UI.connection(network)
  if type(network) ~= 'table' then
    return nil
  end
  local text, tip
  if network.mode == 'direct' then
    text, tip = 'Direct connection', MAPPING_WORDS[network.mapping] or 'The other players reach this PC directly.'
  elseif network.mode == 'relay' and tonumber(network.drop_in) then
    text = 'Through the X4 Online server (drop-in players join there)'
    tip = 'Drop-in games run through the server, which keeps the game for players who join later.'
  elseif network.mode == 'relay' then
    text = 'Through the X4 Online server'
    tip = 'Other players cannot reach this PC directly, so the game runs through the server. It works everywhere and adds a little delay.'
  elseif network.mode == 'lan' then
    text, tip = 'On your local network', 'The players reach each other on this network; no server is needed.'
  else
    text = 'Being set up...'
  end
  local ping = tonumber(network.ping_ms)
  return text, tip, ping and (math.floor(ping + 0.5) .. ' ms') or nil, ping
end

-- An X4 colour name for a ping: quick, normal, slow.
function UI.ping_color(ping)
  ping = tonumber(ping)
  if not ping then
    return nil
  end
  if ping < 80 then
    return 'text_positive'
  end
  return ping >= 160 and 'text_warning' or nil
end

-- The in-session "Multiplayer session" page (native_gate_probe.lua's mp_menu) into the table X4 made for it: the status
-- line, the join code with Copy, the players, the connection, the playing time, Leave with its question. info: slot,
-- clients, active() (the gate runs), left, minutes (the game time as text, a function), relay (its address, for the
-- mouse-over), lobby (the state table), copy(), copied() (Copy pressed in the last 2 s), leave(), confirming (Leave was
-- pressed once), confirm(bool).
function UI.session_page(ftable, info)
  local cols = 5
  local status = function()
    if info.left then
      return 'You left the session: this game runs on alone.'
    end
    if not info.active() then
      return 'The session has ended.'
    end
    return 'In the game. You are player ' .. tostring(info.slot) .. ' of ' .. tostring(info.clients) .. '.'
  end
  local row = ftable:addRow(false, {fixed = true})
  UI.text(row, 2, cols - 1, status, 'status', nil, 'The game shares every step with the other players. Desyncs are '
    .. 'not shown here; the host logs them.')
  local state = type(info.lobby) == 'table' and info.lobby or {}
  local code = type(state.code) == 'string' and state.code ~= '' and state.code or nil
  if code or state.invite then
    UI.section(ftable, cols, 'Join code', nil, 2)
    row = ftable:addRow({id = 'x4lan_mp_copy', callback = info.copy}, {})
    if code and #code <= 12 then
      UI.text(row, 2, 1, code, 'code')
    else
      UI.text(row, 2, 1, 'Invite code', 'strong', nil, code or state.invite)
    end
    local copy = UI.button(row, 3, 3, function()
      return (info.copied and info.copied()) and 'Copied' or 'Copy'
    end, info.copy, true, 'Put the code on the clipboard to send it to another player')
    if code and #code <= 12 then   -- in the middle of the big code's row
      copy.properties.y = math.floor((UI.CODE - UI.ROW) / 2)
    end
  end
  if type(state.players) == 'table' and #state.players > 0 then
    UI.section(ftable, cols, 'Players', nil, 2)
    for index, player in ipairs(state.players) do
      if type(player) == 'table' then
        local slot = tonumber(player.slot) or index
        local you = slot == info.slot
        row = ftable:addRow({id = 'x4lan_mp_player' .. slot}, {interactive = false})
        UI.icon(row, 1, 'widget_tick_01', 'icon_positive')
        UI.text(row, 2, 1, 'Player ' .. slot, you and 'strong' or 'label', you and {color = 'text_player_current'} or nil)
        local role = player.host and (you and 'Host · You' or 'Host') or (you and 'You' or '')
        UI.text(row, 3, 1, role, 'label', {color = 'text_lowlight'})
        UI.text(row, 4, 2, 'In the game', 'label', {color = 'text_positive'})
      end
    end
  end
  local connection, details, ping, ping_ms = UI.connection(state.network)
  UI.section(ftable, cols, 'Connection', nil, 2)
  row = ftable:addRow(false, {interactive = false})
  UI.text(row, 2, 1, connection or 'Relay', 'label', nil, (details and (details .. ' ') or '') .. tostring(info.relay or ''))
  UI.text(row, 3, 3, ping or '', 'label', {halign = 'right', color = UI.ping_color(ping_ms)})
  row = ftable:addRow(false, {interactive = false})
  UI.text(row, 2, 1, 'Playing for', 'label', {color = 'text_lowlight'})
  UI.text(row, 3, 3, info.minutes, 'label', {halign = 'right'}, nil)
  UI.line(ftable, cols, 2)
  local can_leave = not info.left and info.active()
  if info.confirming then
    row = ftable:addRow(false, {})
    UI.text(row, 2, 4, 'Leave? The others play on without you; this game runs on alone.', 'warning')
    row = ftable:addRow({id = 'x4lan_mp_leave_confirm'}, {})
    UI.button(row, 3, 1, 'Leave', function() info.leave() end, can_leave, 'Stop taking part now')
    UI.button(row, 4, 2, 'Stay', function() info.confirm(false) end, true)
  else
    row = ftable:addRow({id = 'x4lan_mp_leave'}, {})
    UI.button(row, 3, 3, 'Leave the game', function() info.confirm(true) end, can_leave,
      'Stop taking part: the others play on without you, this game runs on alone')
  end
end

if lobby_config.session then   -- after the load: the session is on, no menu
  run_files()
  return
end

local PAGE = 'x4lan_lobby'                -- the page's id in the options menu
local STATE_LINES = 6                     -- the state table's header lines
local DEFAULT_GAME_NAME = 'X4 game'
local MAX_NAME_BYTES = 40                 -- as the LAN announcement's name
local MAX_CODE_BYTES = 100                -- a join code, a long invite code or an address
local MAX_LISTED_GAMES = 30               -- rows of one games list
local POLL_FRAMES = 20                    -- the state is read every POLL_FRAMES frames ...
local PIPE_POLL_FRAMES = 40               -- ... and through a pipe every PIPE_POLL_FRAMES frames
local QUIT_DELAY_S = 1.5                  -- the restart notice stays this long before X4 quits (else state.quit_after)
local EDIT_HOLD_S = 2.5                   -- a redraw waits for typing in an edit box at most this long after a key
local INSTALL_TRIES = 3600                -- frames to wait for X4's options menu
local MIN_PLAYERS = 2
local MAX_PLAYERS = 8
local MAX_DROP_IN = MAX_PLAYERS - 1      -- drop-in slots: players who join while the game runs (the host may start alone)
local MAX_START_BYTES = 64                -- a gamestart id (HOST's last field)
local MAX_STARTS_SHOWN = 20               -- the Start dropdown's options at most

-- form: what the player chose or typed on the page; it outlives redraws (X4 draws the page anew for every state).
-- form.start: the start chosen in the Start dropdown (a gamestart id of state.host_starts); nil: the state's default.
local form = {public = false, players = MIN_PLAYERS, open = 0, name = '', code = '', start = nil}
local lobby = {
  state = {phase = 'starting', lines = {'Starting...'}},
  installed = false,
  tries = 0,
  loaded = false,
  form = form,
}
X4LAN_LOBBY = lobby   -- tests reach it

-- Requests --------------------------------------------------------------------------------------------------------------

local MAX_REQUEST_BYTES = 200   -- a spelled request: at most 400 opens of a digit pipe
local OPEN_TRIES = 100          -- a busy pipe (all its instances serving) is opened again at once
-- The marks around a spelled request's digits (-t0 .. -tf). Not -tb and -te: those are the digits b and e.
local SPELL_BEGIN = 'tbegin'
local SPELL_END = 'tend'

local function hex_byte(character)
  return string.format('%02x', character:byte())
end

-- A request spelled through the fixed pipes (docs/NETPLAY.md): every netplay request but Join (clipboard) and the start
-- picker's JOIN<k>, which keep the pipe names the helper knew before (as every request of the server and LAN pages).
local function spelled(text)
  if not lobby_config.netplay then
    return false
  end
  return text ~= 'JOIN' and not text:match('^JOIN%d+$')
end

-- One open of <pipe>-<name>: the helper's answer as a chunk, or nil. An open that did not reach the helper (loadfile
-- says "cannot open": no helper, or every instance of that pipe busy) is tried again; any other failure is an open the
-- helper saw, so it is not repeated.
local function open_pipe(name)
  for _ = 1, OPEN_TRIES do
    local ok, chunk, why = pcall(loadfile, PIPE .. name)
    if ok and chunk then
      return chunk
    end
    local message = tostring(ok and why or chunk)
    if not message:find('cannot open', 1, true) then
      return nil
    end
  end
  return nil
end

-- The helper's answer to the end of a spelled request: the state as it is now, applied at the next frame.
local function keep_answer(chunk)
  local ok, state = pcall(chunk)
  if ok and type(state) == 'table' then
    lobby.answer = state
  end
end

-- <pipe>-tbegin (the helper clears its buffer), one open of <pipe>-t0 .. -tf per digit of the request's UTF-8 bytes in
-- lowercase hex, <pipe>-tend (the helper runs the request). The opens run in order (loadfile waits for each answer);
-- only the end's answer is used.
local function send_spelled(text)
  if #text > MAX_REQUEST_BYTES then
    note('REQUEST_UNSENT longer than ' .. MAX_REQUEST_BYTES .. ' bytes')
    return
  end
  if not open_pipe(SPELL_BEGIN) then
    note('REQUEST_UNSENT no helper answers')
    return
  end
  local hex = text:gsub('.', hex_byte)
  for digit in hex:gmatch('.') do
    if not open_pipe('t' .. digit) then
      note('REQUEST_UNSENT the helper stopped answering')
      return
    end
  end
  local answer = open_pipe(SPELL_END)
  if answer then
    keep_answer(answer)
  end
end

-- What the page asks to host or join (Try again sends it once more), and when Copy was pressed (Copied for a moment).
local RETRIED = {HOST = true, JOIN = true, JOINCODE = true, JOINGAME = true, JOINLAN = true}

local function request(text)
  note('REQUEST ' .. text)
  local kind = text:match('^[A-Z]+')
  if RETRIED[kind] then
    lobby.last_request = text
  elseif kind == 'COPY' then
    lobby.copied_at = lobby.now and lobby.now() or 0
  end
  if not PIPE then
    return
  end
  if spelled(text) then
    send_spelled(text)
  else
    open_pipe(text:lower())   -- the helper sees which pipe was opened
  end
end
lobby.request = request

local function is_continuation_byte(byte)
  return byte ~= nil and byte >= 0x80 and byte < 0xC0
end

-- Typed text as one field of a request: no control characters, no '|', trimmed, at most max_bytes bytes of whole
-- UTF-8 characters (a request is one log row, and a spelled request has at most MAX_REQUEST_BYTES bytes).
local function field(text, max_bytes)
  local clean = tostring(text or ''):gsub('[%c|]', ' ')
  clean = clean:gsub('^%s+', '')
  clean = clean:gsub('%s+$', '')
  if #clean <= max_bytes then
    return clean
  end
  local cut = max_bytes
  while cut > 0 and is_continuation_byte(clean:byte(cut + 1)) do
    cut = cut - 1
  end
  local short = clean:sub(1, cut):gsub('%s+$', '')
  return short
end
lobby.field = field

-- State ------------------------------------------------------------------------------------------------------------------

local function line(index)
  local lines = lobby.state.lines
  if type(lines) == 'table' and lines[index] then
    return tostring(lines[index])
  end
  return ''
end

local function phase()
  return lobby.state.phase or 'idle'
end

-- Phases in which the page may start something (host, join, browse); the error phase says why and lets the player retry.
local function free()
  local current = phase()
  return current == 'idle' or current == 'browsing' or current == 'error'
end

-- Phases with something to cancel (joined: the launcher's waiting phase before netplay).
local CANCELS = {
  choosing = true,
  preparing = true,
  hosting = true,
  joining = true,
  joined = true,
  waiting = true,
  loading = true,
  error = true,
}

local function cancellable()
  return CANCELS[phase()] == true
end

local function start_session()
  if lobby.loaded then
    return
  end
  lobby.loaded = true
  local chunk, why = chunk_of('ui/config.lua')
  if not chunk then
    note('ERROR config ' .. why)
    return
  end
  local ok, err = pcall(chunk)
  local config = X4LAN_CONFIG
  if not ok or type(config) ~= 'table' or type(config.lobby) ~= 'table' or not config.lobby.session then
    note('ERROR config has no session ' .. tostring(err))
    return
  end
  cfg = config
  lobby_config = config.lobby
  note('SESSION slot=' .. tostring(cfg.slot) .. ' clients=' .. tostring(cfg.native_gate_clients)
    .. ' arm_at=' .. tostring(cfg.native_gate_arm_at))
  run_files()
end

local function page_open()
  local options = lobby.options
  return options ~= nil and options.currentOption == PAGE
end

-- The open page drawn again (a new state); held while an edit box is active, so typing is not cut off.
local function redraw()
  local options = lobby.options
  if not page_open() or type(options.refresh) ~= 'function' then
    return
  end
  if lobby.editing then
    lobby.stale = true
    return
  end
  pcall(options.refresh)
end

-- Seconds since the page ran first (X4's getElapsedTime; frames at 60 per second without it).
local frames_run = 0
local function now()
  if type(getElapsedTime) == 'function' then
    local ok, seconds = pcall(getElapsedTime)
    if ok and type(seconds) == 'number' then
      return seconds
    end
  end
  return frames_run / 60
end
lobby.now = now

-- Lines made from the state ------------------------------------------------------------------------------------------------

local MAPPING_NAMES = {upnp = 'UPnP', natpmp = 'NAT-PMP', pcp = 'PCP', manual = 'forwarded port'}

local function whole(number)
  return math.floor(number + 0.5)
end

-- state.network as one line: 'Connection: direct (UPnP), ping 38 ms'.
local function network_line(network)
  if type(network) ~= 'table' then
    return nil
  end
  local how
  if network.mode == 'direct' then
    local mapping = MAPPING_NAMES[network.mapping]
    how = mapping and ('direct (' .. mapping .. ')') or 'direct'
  elseif network.mode == 'relay' then
    how = "through the server's relay"
  elseif network.mode == 'lan' then
    how = 'local network'
  end
  local ping = tonumber(network.ping_ms)
  if not how and not ping then
    return nil
  end
  local text = 'Connection: ' .. (how or 'being set up')
  if ping then
    text = text .. ', ping ' .. whole(ping) .. ' ms'
  end
  return text
end
lobby.network_line = network_line

local PROGRESS_NAMES = {
  download = 'Downloading the session',
  overlay = 'Preparing the game files',
  session = 'Making the session',
}

-- state.progress as one line: 'Downloading the session: 40%'.
local function progress_line(progress)
  if type(progress) ~= 'table' then
    return nil
  end
  local what = PROGRESS_NAMES[progress.what] or 'Working'
  local done = tonumber(progress.done)
  if not done then
    return what .. '...'
  end
  local percent = math.floor(math.max(0, math.min(1, done)) * 100)
  return what .. ': ' .. percent .. '%'
end

-- The starts a game hosted here may have (state.host_starts, every phase: {id, page, line, label, story, default}).
-- A row without a gamestart id is left out.
local function host_starts()
  local list = lobby.state.host_starts
  local out = {}
  if type(list) ~= 'table' then
    return out
  end
  for _, entry in ipairs(list) do
    local id = type(entry) == 'table' and type(entry.id) == 'string' and entry.id or ''
    if #out < MAX_STARTS_SHOWN and #id <= MAX_START_BYTES and id:match('^[a-z0-9_]+$') then
      out[#out + 1] = entry
    end
  end
  return out
end
lobby.host_starts = host_starts

local PREFIX = 'x4ep1_gamestart_'

-- A start's name: X4's own text in the player's language (ReadText of its name reference), else the launcher's label
-- (the game's English text), else its id.
local function start_name(entry)
  local page, line_id = tonumber(entry.page), tonumber(entry.line)
  if page and line_id and type(ReadText) == 'function' then
    local ok, text = pcall(ReadText, page, line_id)
    if ok and type(text) == 'string' and text ~= '' and not text:match('^ReadText') then
      return field(text, MAX_NAME_BYTES)
    end
  end
  local label = field(entry.label, MAX_NAME_BYTES)
  if label ~= '' then
    return label
  end
  local id = tostring(entry.id or '')
  return id:sub(1, #PREFIX) == PREFIX and id:sub(#PREFIX + 1) or id
end
lobby.start_name = start_name

-- The start the Host form has now: the chosen one while the list has it, else the list's default, else its first.
local function chosen_start()
  local starts = host_starts()
  local default
  for _, entry in ipairs(starts) do
    if entry.id == form.start then
      return entry
    end
    if entry.default == true and not default then
      default = entry
    end
  end
  return default or starts[1]
end

-- The game's starts as X4 lists them (GetRegisteredModules: name, description and image in the player's language), once.
local modules
local function start_module(id)
  if type(id) ~= 'string' then
    return nil
  end
  if modules == nil then
    modules = {}
    local ok, list = false, nil
    if type(GetRegisteredModules) == 'function' then
      ok, list = pcall(GetRegisteredModules)
    end
    for _, module in ipairs(ok and type(list) == 'table' and list or {}) do
      if type(module) == 'table' and type(module.id) == 'string' then
        local image = type(module.image) == 'string' and module.image ~= '' and module.image or 'gamestart_default'
        modules[module.id] = {name = module.name, description = module.description, image = image}
      end
    end
  end
  return modules[id]
end

-- The name of a listed game's start (game.start: its gamestart id, all the master's row carries): this PC's name of it
-- when the launcher listed it (host_starts: X4's text of the start in the player's language, else its label), else X4's
-- own name of that start (GetRegisteredModules), else the id's short form.
local function game_start_name(id)
  if type(id) ~= 'string' or not id:match('^[a-z0-9_]+$') or #id > MAX_START_BYTES then
    return nil
  end
  for _, entry in ipairs(host_starts()) do
    if entry.id == id then
      return start_name(entry)
    end
  end
  local module = start_module(id)
  if module and type(module.name) == 'string' and module.name ~= '' then
    return field(module.name, MAX_NAME_BYTES)
  end
  return start_name({id = id})
end
lobby.game_start_name = game_start_name

local GAME_STATE_NAMES = {lobby = 'open', loading = 'loading', running = 'running', started = 'running'}

-- A public or LAN game as one line, and whether it can be joined (if not, the line ends with the reason).
local function game_line(game)
  local name = field(game.name, MAX_NAME_BYTES)
  local parts = {name ~= '' and name or DEFAULT_GAME_NAME}
  local players = tonumber(game.players)
  local max = tonumber(game.max)
  if players then
    parts[#parts + 1] = players .. '/' .. (max or '?')
  end
  if game.state then
    parts[#parts + 1] = GAME_STATE_NAMES[game.state] or field(game.state, 20)
  end
  local start = game_start_name(game.start)
  if start then
    parts[#parts + 1] = start
  end
  local ping = tonumber(game.ping_ms)
  if ping then
    parts[#parts + 1] = whole(ping) .. ' ms'
  end
  -- a running game of the master takes players only in its drop-in slots (game.open: the free ones)
  local running = game.state == 'started' or game.state == 'running'
  local free_slots = tonumber(game.open)
  if running and free_slots and free_slots > 0 then
    parts[#parts + 1] = whole(free_slots) .. ' drop-in'
  end
  local why
  if game.compatible == false then
    why = 'another release'
  elseif running and free_slots ~= nil then
    if free_slots < 1 then
      why = 'no drop-in slot free'
    end
  elseif players and max and players >= max then
    why = 'full'
  end
  local text = table.concat(parts, ', ')
  if why then
    return text .. ' (' .. why .. ')', false
  end
  return text, true
end

-- Rows -------------------------------------------------------------------------------------------------------------------

-- A button row; its callback never raises into X4's menu.
local function button(id, name, value, action, selectable, tip)
  local function callback()
    local ok, why = pcall(action)
    if not ok then
      note('ACTION_ERROR ' .. id .. ' ' .. tostring(why))
    end
  end
  return {id = id, name = name, value = value, valuetype = 'button', mouseOverText = tip, selectable = selectable,
    callback = callback}
end

-- A row of text that cannot be selected (X4 draws it in the normal text colour, over the whole row).
local function info(id, name)
  return {id = id, name = name, interactive = false}
end

local function header(name)
  return {id = 'header', name = name}
end

-- One button row per game of state.games (kind 'game') or state.lan (kind 'lan'); the number drawn, nil without a list.
local function add_games(rows, list, kind)
  if type(list) ~= 'table' then
    return nil
  end
  local shown = 0
  for index, game in ipairs(list) do
    if type(game) == 'table' and shown < MAX_LISTED_GAMES then
      local text, joinable = game_line(game)
      local key
      local join_request
      if kind == 'lan' then
        key = tonumber(game.index) or index
        join_request = 'JOINLAN|' .. key
      else
        key = field(game.id, 64)
        if key ~= '' then
          join_request = 'JOINGAME|' .. key
        end
      end
      joinable = joinable and join_request ~= nil
      local function can_join()
        return free() and joinable
      end
      local function join()
        if can_join() then
          request(join_request)
        end
      end
      local tip = joinable and 'Join this game' or 'This game cannot be joined from here'
      rows[#rows + 1] = button('x4lan_lobby_' .. kind .. '_' .. key, text, 'Join', join, can_join, tip)
      shown = shown + 1
    end
  end
  if #list > shown then
    rows[#rows + 1] = info('x4lan_lobby_' .. kind .. '_more', 'and ' .. (#list - shown) .. ' more')
  end
  return shown
end

local function host()
  if not free() then
    return
  end
  local name = field(form.name, MAX_NAME_BYTES)
  if name == '' then
    name = DEFAULT_GAME_NAME
  end
  local public = form.public and 1 or 0
  local players = form.players .. ''   -- a broken form fails here (the action's error row), not as a bad request
  if form.open > 0 then
    players = players .. '+' .. form.open   -- HOST|<public>|<players>+<drop-in slots>|<name>
  end
  local start = chosen_start()   -- HOST|...|<name>|<start> once the launcher listed the starts; without: its default
  request('HOST|' .. public .. '|' .. players .. '|' .. name .. (start and ('|' .. start.id) or ''))
end

local function join_typed()
  local text = field(form.code, MAX_CODE_BYTES)
  if text ~= '' and free() then
    request('JOINCODE|' .. text)
  end
end
lobby.host = host
lobby.join_typed = join_typed

-- The players dropdown's options, as X4's dropdowns take them.
local PLAYER_CHOICES = {}
for count = 1, MAX_PLAYERS do
  local text = tostring(count)
  if count == 1 then
    text = '1 (you; the others drop in)'
  end
  local choice = {id = tostring(count), text = text, icon = '', displayremoveoption = false}
  PLAYER_CHOICES[#PLAYER_CHOICES + 1] = choice
end
local DROP_IN_CHOICES = {}
for count = 0, MAX_DROP_IN do
  local text = count == 0 and 'none' or tostring(count)
  DROP_IN_CHOICES[#DROP_IN_CHOICES + 1] = {id = tostring(count), text = text, icon = '', displayremoveoption = false}
end

-- The players at the start and the drop-in slots, kept valid together (parse_host's rule): together at most
-- MAX_PLAYERS, and a game without drop-in slots has at least MIN_PLAYERS players.
local function fit_counts(changed)
  if form.players + form.open > MAX_PLAYERS then
    if changed == 'open' then
      form.players = MAX_PLAYERS - form.open
    else
      form.open = MAX_PLAYERS - form.players
    end
  end
  if form.open == 0 and form.players < MIN_PLAYERS then
    if changed == 'open' then
      form.players = MIN_PLAYERS
    else
      form.open = 1
    end
  end
end
lobby.fit_counts = fit_counts

-- The Start dropdown (only once the launcher listed the starts): every player of the game starts in that start.
local STORY_TIP = 'Every player starts in this start\'s own ship, at its place, with its money, and plays its story as their own'
local SHIP_TIP = 'Every player starts in this start\'s own ship, at its place, with its money'
local function start_row(rows)
  local starts = host_starts()
  if #starts == 0 then
    return
  end
  local function start_value()
    local choices = {}
    for _, entry in ipairs(starts) do
      choices[#choices + 1] = {id = entry.id, text = start_name(entry), icon = '', displayremoveoption = false,
        mouseovertext = entry.story == true and STORY_TIP or SHIP_TIP}
    end
    local current = chosen_start()
    return choices, current and current.id or starts[1].id
  end
  local function choose_start(_, choice)
    for _, entry in ipairs(host_starts()) do
      if entry.id == choice then
        form.start = choice
      end
    end
  end
  local current = chosen_start()
  rows[#rows + 1] = {id = 'x4lan_lobby_start', name = 'Start', valuetype = 'dropdown', value = start_value,
    callback = choose_start, selectable = free,
    mouseOverText = (current and current.story == true) and STORY_TIP or SHIP_TIP}
end

local function host_rows(rows)
  rows[#rows + 1] = header('Host a game')
  local function visibility()
    return form.public and 'Public' or 'Private'
  end
  local function toggle()
    form.public = not form.public
  end
  rows[#rows + 1] = button('x4lan_lobby_public', 'Visibility', visibility, toggle, free,
    'Public: listed on the server for everyone. Private: joined with the code only')
  local function players_value()
    return PLAYER_CHOICES, tostring(form.players)
  end
  -- X4 calls this with the dropdown's choice; selecting the row itself calls it without one.
  local function choose_players(_, choice)
    local count = tonumber(choice)
    if count and count >= 1 and count <= MAX_PLAYERS then
      form.players = math.floor(count)
      fit_counts('players')
    end
  end
  rows[#rows + 1] = {id = 'x4lan_lobby_players', name = 'Players', valuetype = 'dropdown', value = players_value,
    callback = choose_players, selectable = free, mouseOverText = 'How many players load the game together, you included'}
  local function drop_in_value()
    return DROP_IN_CHOICES, tostring(form.open)
  end
  local function choose_drop_in(_, choice)
    local count = tonumber(choice)
    if count and count >= 0 and count <= MAX_DROP_IN then
      form.open = math.floor(count)
      fit_counts('open')
    end
  end
  rows[#rows + 1] = {id = 'x4lan_lobby_dropin', name = 'Drop-in slots', valuetype = 'dropdown', value = drop_in_value,
    callback = choose_drop_in, selectable = free,
    mouseOverText = 'More players who join while the game runs (it plays on the master server\'s relay)'}
  start_row(rows)
  rows[#rows + 1] = {
    id = 'x4lan_lobby_name',
    name = 'Game name',
    mouseOverText = 'Shown in the list of games; empty: "' .. DEFAULT_GAME_NAME .. '"',
    edit = {
      hint = DEFAULT_GAME_NAME,
      max = MAX_NAME_BYTES,
      get = function()
        return form.name
      end,
      set = function(text)
        form.name = text
      end,
    },
    fallback = info('x4lan_lobby_name', 'Game name: ' .. DEFAULT_GAME_NAME),
  }
  rows[#rows + 1] = button('x4lan_lobby_host', 'Host the game', 'Host', host, free,
    'Make the session on this PC and get a join code for the other players')
end

local function join_rows(rows)
  rows[#rows + 1] = header('Join a game')
  rows[#rows + 1] = {
    id = 'x4lan_lobby_code',
    name = 'Code or address',
    mouseOverText = 'A join code (K7QX-3FNA), an invite code (X4MP-...) or an address (10.0.0.5:47001); Enter joins',
    edit = {
      hint = 'Join code, invite code or address',
      max = MAX_CODE_BYTES,
      get = function()
        return form.code
      end,
      set = function(text)
        form.code = text
      end,
      confirm = join_typed,
    },
    fallback = info('x4lan_lobby_code', 'Typing is not available here: copy the code, then use the clipboard row'),
  }
  rows[#rows + 1] = button('x4lan_lobby_joincode', 'Join with this code or address', 'Join', join_typed, free,
    'Join the game of the code typed above')
  local function join_clipboard()
    if free() then
      request('JOIN')
    end
  end
  rows[#rows + 1] = button('x4lan_lobby_join', 'Join with the code on the clipboard', 'Join', join_clipboard, free,
    'Copy the code the host sent you, then press this')
end

local function list_rows(rows)
  local state = lobby.state
  local waiting_text = phase() == 'browsing' and 'Looking for games...' or 'Press Refresh to list the games.'
  rows[#rows + 1] = header('Public games')
  local function browse()
    if free() then
      request('BROWSE')
    end
  end
  rows[#rows + 1] = button('x4lan_lobby_browse', 'Look for games again', 'Refresh', browse, free,
    'Ask the server for its public games and look for games on this network')
  local public = add_games(rows, state.games, 'game')
  local lan_count = type(state.lan) == 'table' and #state.lan or -1
  local drawn_lists = tostring(public or -1) .. '/' .. lan_count .. '/' .. tostring(state.stamp)
  if drawn_lists ~= lobby.lists_noted then   -- the next report says what the page showed (the launcher logs what it gave)
    lobby.lists_noted = drawn_lists
    note('GAMES public=' .. tostring(public or 'none') .. ' lan=' .. (lan_count >= 0 and lan_count or 'none')
      .. ' phase=' .. tostring(phase()) .. ' stamp=' .. tostring(state.stamp))
  end
  if public == nil then
    rows[#rows + 1] = info('x4lan_lobby_game_none', waiting_text)
  elseif #state.games == 0 then
    rows[#rows + 1] = info('x4lan_lobby_game_none', 'No public games right now.')
  end
  rows[#rows + 1] = header('LAN games')
  local lan = add_games(rows, state.lan, 'lan')
  if lan == nil then
    rows[#rows + 1] = info('x4lan_lobby_lan_none', waiting_text)
  elseif #state.lan == 0 then
    rows[#rows + 1] = info('x4lan_lobby_lan_none', 'No games found on this network.')
  end
  -- Windows Firewall has no rule for the launcher's Python (state.firewall, from the launcher): a LAN scan hears
  -- nothing and other players cannot reach a game hosted here; one click asks Windows for the rule.
  if state.firewall == 'blocked' then
    local function allow()
      request('FIREWALL')
    end
    rows[#rows + 1] = button('x4lan_lobby_lan_firewall', 'Windows Firewall may hide LAN games', 'Allow', allow, free,
      'Ask Windows to let X4 Online hear LAN games and take direct connections (Windows asks for permission)')
  end
end

-- The game being made or joined: its code, the connection, the players, Start (the host).
local function game_rows(rows)
  local state = lobby.state
  local code = type(state.code) == 'string' and state.code ~= '' and state.code or nil
  if code or state.invite then
    local name = code and ('Join code: ' .. field(code, MAX_NAME_BYTES)) or 'Copy the invite code again'
    local function copy()
      request('COPY')
    end
    local function has_code()
      return lobby.state.code ~= nil or lobby.state.invite ~= nil
    end
    rows[#rows + 1] = button('x4lan_lobby_copy', name, 'Copy', copy, has_code,
      'Put the code on the clipboard to send it to the other players')
  end
  local network = network_line(state.network)
  if network then
    rows[#rows + 1] = info('x4lan_lobby_network', network)
  end
  -- The router opened the ports but a firewall on this PC blocked them (state.network.firewall == 'blocked'): one
  -- click asks Windows for a firewall rule for the launcher's own Python (Windows shows its permission prompt).
  if type(state.network) == 'table' and state.network.firewall == 'blocked' then
    local function allow()
      request('FIREWALL')
    end
    rows[#rows + 1] = button('x4lan_lobby_firewall', 'Allow direct connections (Windows asks first)', 'Allow', allow,
      nil, 'Let other players connect to this PC directly instead of through the server\'s relay')
  end
  if type(state.players) == 'table' and #state.players > 0 then
    rows[#rows + 1] = header('Players')
    for index, player in ipairs(state.players) do
      if type(player) == 'table' then
        local slot = tonumber(player.slot) or index
        local name = 'Player ' .. slot .. (player.host and ' (host)' or '')
        local ready = player.ready and 'ready' or 'not ready yet'
        rows[#rows + 1] = info('x4lan_lobby_player' .. slot, name .. ': ' .. ready)
      end
    end
  end
  if state.host then
    local function can_start()
      return lobby.state.can_start == true
    end
    local function start()
      if can_start() then
        request('START')
      end
    end
    rows[#rows + 1] = button('x4lan_lobby_startgame', 'Start the game', 'Start', start, can_start,
      'Load the game now with the players who are ready')
  end
end

-- With a server whose game runs (state.game=='running', from the helper) there is one row per open start, and
-- choosing one joins the running game in that slot.
local function start_picker_rows(rows, starts)
  for _, start in ipairs(starts) do
    local slot = tonumber(start.slot)
    local label = tostring(start.label or start.start or '')
    if slot then
      local function can_join()
        return phase() == 'idle' and not start.taken and lobby.state.joinable ~= false
      end
      rows[#rows + 1] = {
        id = 'x4lan_lobby_start' .. slot,
        name = 'Join as ' .. label,
        value = start.taken and 'Taken' or 'Join',
        valuetype = 'button',
        mouseOverText = 'Join the running game with this start: X4 loads the game as it is now',
        callback = function()
          request('JOIN' .. slot)
        end,
        selectable = can_join,
      }
    end
  end
end

-- The kit names a server (x4ls serve): nobody hosts, there is no code, Join is the one choice.
local function server_join_row(rows)
  rows[#rows + 1] = {
    id = 'x4lan_lobby_join',
    name = lobby.state.join_label or 'Join the game',
    value = 'Join',
    valuetype = 'button',
    mouseOverText = 'Join the server; the game loads when every player is here',
    callback = function()
      request('JOIN')
    end,
    selectable = function()
      return phase() == 'idle'
    end,
  }
end

-- The LAN lobby of before netplay (neither lobby.netplay nor lobby.server): the invite code goes through the clipboard.
local function lan_rows(rows)
  local function idle()
    return phase() == 'idle'
  end
  rows[#rows + 1] = {
    id = 'x4lan_lobby_host',
    name = 'Host a game',
    value = 'Host',
    valuetype = 'button',
    mouseOverText = 'Start a session on this PC and get an invite code for the other players',
    callback = function()
      request('HOST')
    end,
    selectable = idle,
  }
  rows[#rows + 1] = {
    id = 'x4lan_lobby_join',
    name = 'Join a game (invite code from the clipboard)',
    value = 'Join',
    valuetype = 'button',
    mouseOverText = 'Copy the invite code the host sent you, then press this',
    callback = function()
      request('JOIN')
    end,
    selectable = idle,
  }
  rows[#rows + 1] = {
    id = 'x4lan_lobby_copy',
    name = 'Copy the invite code again',
    value = 'Copy',
    valuetype = 'button',
    callback = function()
      request('COPY')
    end,
    selectable = function()
      return phase() == 'hosting' and lobby.state.invite ~= nil
    end,
  }
end

-- The phases the server and LAN pages could cancel before netplay.
local OLD_CANCELS = {hosting = true, joined = true, joining = true}

local function cancel_row(can_cancel)
  return {
    id = 'x4lan_lobby_cancel',
    name = 'Cancel',
    value = 'Cancel',
    valuetype = 'button',
    callback = function()
      request('CANCEL')
    end,
    selectable = can_cancel,
  }
end

local function old_cancellable()
  return OLD_CANCELS[phase()] == true
end

local function line_rows()
  local rows = {}
  for index = 1, STATE_LINES do
    rows[#rows + 1] = {id = 'header', name = function()
      return line(index)
    end}
  end
  return rows
end

-- How this PC's Steam starts X4 (state.steam_play from the launcher: on, pending, steam_running; steamlib.py) and, in an
-- X4 that Steam's Play started through X4 Online (lobby.single), Single-player: X4 starts again as Steam starts it, in
-- the player's own profile.
local function steam_rows(rows)
  local steam = type(lobby.state.steam_play) == 'table' and lobby.state.steam_play or nil
  if not steam and not lobby_config.single then
    return
  end
  rows[#rows + 1] = header('Steam')
  if lobby_config.single then
    local function single()
      if free() then
        request('SINGLEPLAYER')
      end
    end
    rows[#rows + 1] = button('x4lan_lobby_single', 'Play single-player (X4 starts again in your own profile)',
      'Single-player', single, free, 'X4 starts again as Steam starts it, with your own profile, saves and settings')
  end
  if steam then
    local on = steam.on == true
    local function toggle()
      if free() then
        request('STEAMPLAY|' .. (on and '0' or '1'))
      end
    end
    rows[#rows + 1] = button('x4lan_lobby_steamplay', "Steam's Play starts X4 Online", on and 'On' or 'Off', toggle, free,
      'On: Play in Steam opens X4 here, in its multiplayer profile (Single-player is one click away). Off: Steam starts X4 as before')
    if steam.pending == true then
      rows[#rows + 1] = info('x4lan_lobby_steampending', 'This takes effect once Steam is closed.')
      if PIPE and steam.steam_running == true then
        local function restart()
          if free() then
            request('STEAMRESTART')
          end
        end
        rows[#rows + 1] = button('x4lan_lobby_steamrestart', 'Restart Steam now (X4 closes)', 'Restart', restart, free,
          'Steam closes and starts again with this setting')
      end
    end
  end
end
lobby.steam_rows = steam_rows

-- A drop-in game whose session offers starts chosen at join (phase 'choosing', state.choices = {{index, label,
-- gamestart}, ...}, state.slot): one row per start, JOINSTART|<index> (0: the slot's own start, beside the host). The
-- launcher makes the ship when the player joins. (The rows of the join-starts branch, kept for its tests and launcher.)
local CHOICES_SHOWN_MAX = 40
local function choosing()
  return phase() == 'choosing'
end

-- The state's choices that can be sent: an index 0..255 that is a whole number, at most CHOICES_SHOWN_MAX of them.
local function choices()
  local list = type(lobby.state.choices) == 'table' and lobby.state.choices or {}
  local out = {}
  for _, choice in ipairs(list) do
    local index = type(choice) == 'table' and tonumber(choice.index) or nil
    if index and index >= 0 and index <= 255 and index == math.floor(index) and #out < CHOICES_SHOWN_MAX then
      out[#out + 1] = {index = math.floor(index), label = field(tostring(choice.label or ''), MAX_NAME_BYTES),
        gamestart = type(choice.gamestart) == 'string' and choice.gamestart:match('^[a-z0-9_]+$') and choice.gamestart or nil}
    end
  end
  return out, #list
end
lobby.choices = choices

local function pick_start(index)
  if choosing() then
    request('JOINSTART|' .. string.format('%d', index))
  end
end
lobby.pick_start = pick_start

local CHOICE_TIP_HOST = "Start beside the host, in the session start's own ship"
local CHOICE_TIP = "Start with this start's ship, place and money; it is made in the running game when you join"

local function choice_rows(rows)
  local list, total = choices()
  for _, choice in ipairs(list) do
    local index = choice.index
    rows[#rows + 1] = button('x4lan_lobby_choice' .. string.format('%d', index), choice.label, 'Start here',
      function() pick_start(index) end, choosing, index == 0 and CHOICE_TIP_HOST or CHOICE_TIP)
  end
  if total > #list then
    rows[#rows + 1] = info('x4lan_lobby_choice_more', 'and ' .. (total - #list) .. ' more')
  end
end

-- The netplay page (lobby.netplay): by phase the start rows (host, join, the lists) or the game being made.
local function netplay_rows(rows)
  local current = phase()
  local progress = progress_line(lobby.state.progress)
  if progress then
    rows[#rows + 1] = info('x4lan_lobby_progress', progress)
  end
  if free() or current == 'starting' or current == 'nohelper' then
    host_rows(rows)
    join_rows(rows)
    list_rows(rows)
    steam_rows(rows)
  elseif current == 'choosing' then
    choice_rows(rows)
  elseif current ~= 'session' and current ~= 'restart' then
    game_rows(rows)
  end
  if cancellable() then
    rows[#rows + 1] = cancel_row(cancellable)
  end
end

-- The page's rows, made again for every draw: the netplay page (lobby.netplay: a release, or x4ls lobby running
-- netplay), a kit's server page (lobby.server: Join the game, the start picker) or the LAN lobby of before.
local function page_rows()
  local rows = line_rows()
  rows[#rows + 1] = {id = 'line'}
  local state = lobby.state
  if lobby_config.netplay then
    netplay_rows(rows)
    return rows
  end
  if not lobby_config.server then
    lan_rows(rows)
  elseif state.game == 'running' and type(state.starts) == 'table' then
    start_picker_rows(rows, state.starts)
  else
    server_join_row(rows)
  end
  rows[#rows + 1] = cancel_row(old_cancellable)
  return rows
end
lobby.page_rows = page_rows

-- Drawing ----------------------------------------------------------------------------------------------------------------

local TEXT_PROPERTIES = {font = 'Zekton outlined', fontsize = 10, x = 5, y = 2}   -- the options menu's standard text
local EDIT_TEXT_PROPERTIES = {fontsize = 10, x = 5}
local EDIT_BOX_HEIGHT = 19                                                         -- the menu's standard text height
local DEFAULT_COLUMNS = 5                                                          -- the options page's table

-- An edit box row (X4's options menu has no row type for one): the label in column 2 and the box from column 3 on, as
-- the menu's own value rows. row.edit: hint (shown while empty), max (characters), get/set (the form), confirm (Enter).
local function edit_row(ftable, row, columns)
  columns = columns or DEFAULT_COLUMNS
  local edit = row.edit
  local drawn = ftable:addRow({id = row.id}, {})
  drawn[2]:setColSpan(columns - 4):createText(row.name, TEXT_PROPERTIES)
  if row.mouseOverText then
    drawn[2].properties.mouseOverText = row.mouseOverText
  end
  local cell = drawn[columns - 2]
  local box = cell:createEditBox({height = EDIT_BOX_HEIGHT, description = row.name, defaultText = edit.hint,
    maxChars = edit.max, active = free()})
  box:setText(tostring(edit.get() or ''), EDIT_TEXT_PROPERTIES)
  cell.handlers.onEditBoxActivated = function()
    lobby.editing = true
    lobby.typed_at = now()
  end
  cell.handlers.onTextChanged = function(_, text)
    lobby.typed_at = now()
    if type(text) == 'string' then
      edit.set(text)
    end
  end
  -- X4 confirms a box on Enter and also when the player clicks elsewhere in a table (by_click): only Enter acts.
  cell.handlers.onEditBoxDeactivated = function(_, text, _, confirmed, by_click)
    lobby.editing = false
    if type(text) == 'string' then
      edit.set(text)
    end
    if confirmed and not by_click and edit.confirm then
      local ok, why = pcall(edit.confirm)
      if not ok then
        note('ACTION_ERROR ' .. row.id .. ' ' .. tostring(why))
      end
    end
  end
  local options = lobby.options
  if options and options.preselectOption == row.id then   -- the selection X4 keeps over a refresh
    pcall(ftable.setSelectedRow, ftable, drawn.index)
  end
end

-- One row through the menu's own row function, an edit box row by edit_row (its fallback row if that fails).
local function draw_row(stock_row, ftable, row, ...)
  if not row.edit then
    return pcall(stock_row, ftable, row, ...)
  end
  local ok, why = pcall(edit_row, ftable, row, ...)
  if ok then
    return true
  end
  if not lobby.edit_refused then
    lobby.edit_refused = true
    note('EDIT_UNAVAILABLE ' .. tostring(why))
  end
  return pcall(stock_row, ftable, row.fallback, ...)
end

-- The page's rows in place of the stock page's; a row that fails is left out (one row in the log).
local function draw_rows(stock_row, ftable, ...)
  local ok, rows = pcall(page_rows)
  if not ok then
    note('DRAW_ERROR rows ' .. tostring(rows))
    rows = line_rows()
  end
  for _, row in ipairs(rows) do
    local drawn, why = draw_row(stock_row, ftable, row, ...)
    if not drawn then
      note('DRAW_ERROR ' .. tostring(row.id) .. ' ' .. tostring(why))
    end
  end
end

-- The page as X4 draws its own menus (docs/MULTIPLAYER-PAGE.md, "The views, phase by phase") --------------------------
-- X4 draws the frame, the title row with its back arrow and the 5-column table (the head, X4's menu.optionTable) through
-- the stock "multiplayer" page; the views add the status line and their rows to the head, and the start and choosing
-- views add tables of their own to X4's frame beside and below it (the games, the forms, the chosen start). Every action
-- is one of the requests the rows above send (NETPLAY.md). Without what they need (another options menu), or once a view
-- failed, the page draws the rows above through the menu's row function instead (draw_rows).
local native = {}
lobby.native = native

native.start_module = start_module

local function action_error(id)
  return function(why)
    note('ACTION_ERROR ' .. id .. ' ' .. tostring(why))
  end
end

-- The state's lines as the mouse-over of the status line (they are not drawn: NETPLAY.md's lines stay for older pages).
local function lines_tip()
  local lines = type(lobby.state.lines) == 'table' and lobby.state.lines or {}
  local out = {}
  for _, text in ipairs(lines) do
    if tostring(text) ~= '' then
      out[#out + 1] = tostring(text)
    end
  end
  return table.concat(out, '\n')
end

-- Text that ends in dots that move (something is under way).
local function moving(text)
  return function()
    return text .. string.rep('.', 1 + math.floor(now() * 2) % 3)
  end
end

local function count(n, one, many)
  return n .. ' ' .. (n == 1 and one or many)
end

-- A row of X4's head table with row data {id = ...}: selected again when X4's refresh kept it (menu.preselectOption),
-- as X4's own option rows are, so the keyboard's place survives a new state.
local function head_row(head, rowdata, properties)
  local row = head:addRow(rowdata, properties or {})
  local options = lobby.options
  if type(rowdata) == 'table' and options and options.preselectOption ~= nil and options.preselectOption == rowdata.id then
    pcall(head.setSelectedRow, head, row.index)
  end
  return row
end

-- The status line under X4's title: a short plain text (a function too), the launcher's lines as its mouse-over, and
-- Single-player at its right in an X4 that Steam's Play started through X4 Online (lobby.single).
local SINGLE_TIP = 'X4 starts again as Steam starts it, with your own profile, saves and settings'
local function status_row(head, text, color_name)
  local single = lobby_config.single and (free() or phase() == 'starting' or phase() == 'nohelper')
  local row = head_row(head, single and {id = 'x4lan_lobby_status'} or false, {fixed = true})
  UI.text(row, 2, single and 2 or 4, text, 'status', color_name and {color = color_name} or nil, lines_tip())
  if single then
    UI.button(row, 4, 2, 'Single-player', function()
      if free() then
        request('SINGLEPLAYER')
      end
    end, free(), SINGLE_TIP, action_error('x4lan_lobby_single'))
  end
  return row
end

-- Where the body starts: right under the head as it is now.
local function body_y(head)
  return head.properties.y + head:getVisibleHeight() + Helper.borderSize
end

-- A table of the page's own in X4's frame, with the option menu's button, dropdown and edit box sizes.
local function body_table(frame, cols, x, y, width, height, tab_order)
  local ftable = frame:addTable(cols, {tabOrder = tab_order, x = x, y = y, width = width, maxVisibleHeight = height})
  for _, kind in ipairs({'button', 'dropdown'}) do
    ftable:setDefaultCellProperties(kind, {height = UI.ROW})
    ftable:setDefaultComplexCellProperties(kind, 'text', {x = 5, fontsize = 10})
  end
  return ftable
end

-- The frame as wide as X4's list pages (Load, Extensions), for the views with tables side by side.
local function widen(options, frame)
  local wide = tonumber(options.widthExtraWide)
  if wide and tonumber(frame.properties.width) and frame.properties.width < wide then
    frame.properties.width = wide
  end
end

-- An edit box in a cell, as edit_row's: typing holds a redraw back, Enter confirms (a click elsewhere does not).
function native.edit(cell, id, spec)
  local box = cell:createEditBox({height = UI.ROW, description = spec.name, defaultText = spec.hint, maxChars = spec.max,
    active = free()})
  box:setText(tostring(spec.get() or ''), EDIT_TEXT_PROPERTIES)
  if spec.tip then
    cell.properties.mouseOverText = spec.tip
  end
  cell.handlers.onEditBoxActivated = function()
    lobby.editing = true
    lobby.typed_at = now()
  end
  cell.handlers.onTextChanged = function(_, text)
    lobby.typed_at = now()
    if type(text) == 'string' then
      spec.set(text)
    end
  end
  cell.handlers.onEditBoxDeactivated = function(_, text, _, confirmed, by_click)
    lobby.editing = false
    if type(text) == 'string' then
      spec.set(text)
    end
    if confirmed and not by_click and spec.confirm then
      local ok, why = pcall(spec.confirm)
      if not ok then
        note('ACTION_ERROR ' .. id .. ' ' .. tostring(why))
      end
    end
  end
end


-- A server's reason code (a bare word such as not_found) as a sentence, where one is known.
local REASONS = {
  not_found = 'No game has this code. Check it with the host, or ask for a new one.',
  full = 'The game is full.',
  started = 'The game has started and takes no more players.',
  closed = 'The host closed the game.',
}

local function plain(text)
  text = tostring(text or '')
  text = text:gsub('[Tt]he master server', function(match)
    return match:sub(1, 1) .. 'he X4 Online server'
  end)
  return REASONS[text] or text
end
native.plain = plain

-- The start view ------------------------------------------------------------------------------------------------------
-- idle, browsing, starting: the games on the left (X4's list style: columns, groups, Join per row), joining by code,
-- hosting and Steam on the right.

local OTHER_RELEASE_TIP = 'Made with another release of X4 Online: both players need the same release.'

-- What a listed game shows: its key and request, its columns, and why it cannot be joined (the rows' rules above).
local function game_facts(game, kind, index)
  local facts = {}
  local name = field(game.name, MAX_NAME_BYTES)
  facts.name = name ~= '' and name or DEFAULT_GAME_NAME
  local players, max = tonumber(game.players), tonumber(game.max)
  local running = game.state == 'started' or game.state == 'running'
  local free_slots = tonumber(game.open)
  facts.players = players and (whole(players) .. ' / ' .. (max and whole(max) or '?')) or ''
  if running then
    facts.players = facts.players .. ' · running'
  end
  facts.start = game_start_name(game.start) or ''
  facts.ping = tonumber(game.ping_ms)
  if kind == 'lan' then
    facts.key = tonumber(game.index) or index
    facts.request = 'JOINLAN|' .. facts.key
  else
    facts.key = field(game.id, 64)
    facts.request = facts.key ~= '' and ('JOINGAME|' .. facts.key) or nil
  end
  if game.compatible == false then
    facts.why, facts.tip, facts.warn = 'Other version', OTHER_RELEASE_TIP, true
  elseif running and free_slots ~= nil and free_slots < 1 then
    facts.why, facts.tip = 'No free slot', 'The game runs and every drop-in slot is taken.'
  elseif not running and players and max and players >= max then
    facts.why, facts.tip = 'Full', 'Every player of this game is here.'
  elseif not facts.request then
    facts.why, facts.tip = '-', 'This game cannot be joined from here.'
  elseif running and free_slots and free_slots > 0 then
    facts.tip = 'A running game: you join it in one of its ' .. count(whole(free_slots), 'free drop-in slot', 'free drop-in slots') .. '.'
  end
  return facts
end
native.game_facts = game_facts

local function game_row(list_table, game, kind, index)
  local facts = game_facts(game, kind, index)
  local row = list_table:addRow({id = 'x4lan_lobby_' .. kind .. '_' .. facts.key}, {})
  if facts.warn then
    UI.icon(row, 1, 'workshop_error', 'icon_warning', facts.tip)
  end
  local joinable = facts.why == nil
  UI.text(row, 2, 1, facts.name, joinable and 'label' or 'inactive', nil, facts.tip)
  UI.text(row, 3, 1, facts.players, joinable and 'label' or 'inactive')
  UI.text(row, 4, 1, facts.start, 'label', {color = 'text_lowlight'})
  UI.text(row, 5, 1, facts.ping and (whole(facts.ping) .. ' ms') or '', 'label',
    {halign = 'right', color = UI.ping_color(facts.ping)})
  if joinable then
    UI.button(row, 6, 1, 'Join', function()
      if free() then
        request(facts.request)
      end
    end, free(), 'Join this game', action_error('x4lan_lobby_' .. kind .. '_' .. facts.key))
  else
    UI.text(row, 6, 1, facts.why, facts.warn and 'label' or 'inactive', {halign = 'center',
      color = facts.warn and 'text_warning' or nil}, facts.tip)
  end
end

-- One group of the games table (Public, On your network): its name and count, its games, or why there are none.
local function game_group(list_table, title, list, kind, empty)
  local row = list_table:addRow(false, {})
  UI.text(row, 2, 5, type(list) == 'table' and (title .. ' (' .. #list .. ')') or title, 'column')
  if type(list) ~= 'table' then
    row = list_table:addRow(false, {})
    UI.text(row, 2, 5, phase() == 'browsing' and moving('Looking for games') or 'Refresh to list the games.', 'inactive')
    return nil
  end
  if #list == 0 then
    row = list_table:addRow(false, {})
    UI.text(row, 2, 5, empty, 'inactive')
    return 0
  end
  local shown = 0
  for index, game in ipairs(list) do
    if type(game) == 'table' and shown < MAX_LISTED_GAMES then
      game_row(list_table, game, kind, index)
      shown = shown + 1
    end
  end
  if #list > shown then
    row = list_table:addRow(false, {})
    UI.text(row, 2, 5, 'and ' .. (#list - shown) .. ' more', 'inactive')
  end
  return shown
end

local function browse()
  if free() then
    request('BROWSE')
  end
end

local function games_table(list_table)
  local state = lobby.state
  UI.section(list_table, 6, 'Games', function(row, col)
    UI.button(row, col, 1, 'Refresh', browse, free(), 'Look for games again: the public games of the X4 Online server '
      .. 'and the games on your network', action_error('x4lan_lobby_browse'))
  end, 2)
  local row = list_table:addRow(false, {})
  UI.text(row, 2, 1, 'Name', 'column')
  UI.text(row, 3, 1, 'Players', 'column')
  UI.text(row, 4, 1, 'Start', 'column')
  UI.text(row, 5, 1, 'Ping', 'column', {halign = 'right'})
  local public = game_group(list_table, 'Public', state.games, 'game', 'Nobody is hosting a public game right now.')
  UI.line(list_table, 6, 2)
  local lan = game_group(list_table, 'On your network', state.lan, 'lan', 'No games found on your network.')
  -- what the page showed (as list_rows: the launcher logs what it gave)
  local lan_count = type(state.lan) == 'table' and #state.lan or -1
  local drawn_lists = tostring(public or -1) .. '/' .. lan_count .. '/' .. tostring(state.stamp)
  if drawn_lists ~= lobby.lists_noted then
    lobby.lists_noted = drawn_lists
    note('GAMES public=' .. tostring(public or 'none') .. ' lan=' .. (lan_count >= 0 and lan_count or 'none')
      .. ' phase=' .. tostring(phase()) .. ' stamp=' .. tostring(state.stamp))
  end
  if state.firewall == 'blocked' then
    row = list_table:addRow({id = 'x4lan_lobby_lan_firewall'}, {})
    UI.icon(row, 1, 'workshop_error', 'icon_warning')
    UI.text(row, 2, 4, 'Windows Firewall may hide games on your network.', 'warning')
    UI.button(row, 6, 1, 'Allow', function()
      request('FIREWALL')
    end, free(), 'Ask Windows to let X4 Online hear games on your network and take direct connections (Windows asks first)',
      action_error('x4lan_lobby_lan_firewall'))
  end
end

local function join_clipboard()
  if free() then
    request('JOIN')
  end
end

local VISIBILITY_CHOICES = {
  {id = 'private', text = 'Private', icon = '', displayremoveoption = false},
  {id = 'public', text = 'Public', icon = '', displayremoveoption = false},
}
local VISIBILITY_TIP = 'Private: only players with the code can join. Public: listed here for everyone.'

-- The form's player counts in plain words.
local function counts_text()
  local open = form.open
  local total = form.players + open
  if open == 0 then
    return count(form.players, 'player', 'players') .. ' load the game together.'
  end
  return 'Up to ' .. total .. ' players: ' .. (form.players == 1 and 'you start alone' or (form.players .. ' load together'))
    .. ', ' .. count(open, 'more joins', 'more join') .. ' while the game runs.'
end
native.counts_text = counts_text

local function start_choices()
  local choices_list = {}
  for _, entry in ipairs(host_starts()) do
    choices_list[#choices_list + 1] = {id = entry.id, text = start_name(entry), icon = '', displayremoveoption = false,
      mouseovertext = entry.story == true and STORY_TIP or SHIP_TIP}
  end
  local current = chosen_start()
  return choices_list, current and current.id or nil
end

local function forms_table(form_table, width)
  local cols = 4
  local state = lobby.state
  UI.section(form_table, cols, 'Join with a code')
  local row = form_table:addRow({id = 'x4lan_lobby_code'}, {})
  native.edit(UI.cell(row, 1, cols), 'x4lan_lobby_code', {name = 'Code or address', hint = 'Join code, invite code or address',
    max = MAX_CODE_BYTES, get = function() return form.code end, set = function(text) form.code = text end,
    confirm = join_typed, tip = 'A join code (K7QX-3FNA), an invite code (X4MP-...) or an address (10.0.0.5:47001); Enter joins'})
  row = form_table:addRow({id = 'x4lan_lobby_joinrow'}, {})
  UI.button(row, 1, 3, 'Use the code on my clipboard', join_clipboard, free(), 'Copy the code the host sent you, then press this',
    action_error('x4lan_lobby_join'))
  UI.button(row, 4, 1, 'Join', join_typed, free(), 'Join the game of the code typed above', action_error('x4lan_lobby_joincode'))
  form_table:addEmptyRow(UI.SECTION / 2)
  UI.section(form_table, cols, 'Host a game')
  row = form_table:addRow({id = 'x4lan_lobby_name'}, {})
  UI.text(row, 1, 1, 'Game name', free() and 'label' or 'inactive')
  native.edit(UI.cell(row, 2, 3), 'x4lan_lobby_name', {name = 'Game name', hint = DEFAULT_GAME_NAME, max = MAX_NAME_BYTES,
    get = function() return form.name end, set = function(text) form.name = text end,
    tip = 'Shown in the list of games; empty: "' .. DEFAULT_GAME_NAME .. '"'})
  local starts_list, current = start_choices()
  if #starts_list > 0 then
    row = form_table:addRow({id = 'x4lan_lobby_start'}, {})
    UI.text(row, 1, 1, 'Start', free() and 'label' or 'inactive')
    UI.dropdown(row, 2, 3, starts_list, current, function(id)
      for _, entry in ipairs(host_starts()) do
        if entry.id == id then
          form.start = id
        end
      end
    end, free(), 'The start every player of the game starts in')
    local chosen = function()
      local entry = chosen_start()
      return entry, entry and start_module(entry.id)
    end
    local _, module = chosen()
    if module then   -- X4's picture of the start and what it means for the players, following the dropdown
      row = form_table:addRow(false, {})
      local image_width = math.floor(width * 0.5) - Helper.borderSize
      UI.cell(row, 1, 2):createIcon(function()
        local _, shown = chosen()
        return shown and shown.image or 'gamestart_default'
      end, {width = image_width, height = math.floor(image_width * 9 / 16), scaling = false})
      UI.text(row, 3, 2, function()
        local entry = chosen()
        return entry and ((entry.story == true and STORY_TIP or SHIP_TIP) .. '.') or ''
      end, 'info')
    end
  end
  row = form_table:addRow({id = 'x4lan_lobby_public'}, {})
  UI.text(row, 1, 1, 'Visibility', free() and 'label' or 'inactive', nil, VISIBILITY_TIP)
  UI.dropdown(row, 2, 3, VISIBILITY_CHOICES, form.public and 'public' or 'private', function(id)
    form.public = id == 'public'
  end, free(), VISIBILITY_TIP)
  row = form_table:addRow({id = 'x4lan_lobby_players'}, {})
  UI.text(row, 1, 1, 'Players', free() and 'label' or 'inactive', nil, 'How many players load the game together, you included')
  UI.dropdown(row, 2, 1, PLAYER_CHOICES, tostring(form.players), function(id)
    local value = tonumber(id)
    if value and value >= 1 and value <= MAX_PLAYERS then
      form.players = math.floor(value)
      fit_counts('players')
      lobby.redraw_next = true   -- the drop-in slots may have changed with it
    end
  end, free())
  UI.text(row, 3, 1, 'Drop-in slots', free() and 'label' or 'inactive', nil,
    'More players who join while the game runs (it plays on the X4 Online server)')
  UI.dropdown(row, 4, 1, DROP_IN_CHOICES, tostring(form.open), function(id)
    local value = tonumber(id)
    if value and value >= 0 and value <= MAX_DROP_IN then
      form.open = math.floor(value)
      fit_counts('open')
      lobby.redraw_next = true
    end
  end, free())
  row = form_table:addRow(false, {})
  UI.text(row, 1, cols, counts_text, 'info')
  row = form_table:addRow({id = 'x4lan_lobby_host'}, {})
  UI.button(row, 3, 2, 'Host the game', host, free(), 'Make the game and get a join code for the other players',
    action_error('x4lan_lobby_host'))
  local steam = type(state.steam_play) == 'table' and state.steam_play or nil
  if steam then
    form_table:addEmptyRow(UI.SECTION / 2)
    UI.section(form_table, cols, 'Steam')
    local on = steam.on == true
    row = form_table:addRow({id = 'x4lan_lobby_steamplay'}, {})
    UI.text(row, 1, 3, "Steam's Play starts X4 Online", free() and 'label' or 'inactive', nil,
      'On: Play in Steam opens X4 here, in its multiplayer profile (Single-player is one click away). Off: Steam starts X4 as before')
    UI.button(row, 4, 1, on and 'On' or 'Off', function()
      if free() then
        request('STEAMPLAY|' .. (on and '0' or '1'))
      end
    end, free(), nil, action_error('x4lan_lobby_steamplay'), on and 'text_positive' or nil)
    if steam.pending == true then
      row = form_table:addRow(false, {})
      UI.text(row, 1, cols, 'This takes effect once Steam is closed.', 'info')
      if PIPE and steam.steam_running == true then
        row = form_table:addRow({id = 'x4lan_lobby_steamrestart'}, {})
        UI.button(row, 3, 2, 'Restart Steam', function()
          if free() then
            request('STEAMRESTART')
          end
        end, free(), 'Steam closes and starts again with this setting (X4 closes)', action_error('x4lan_lobby_steamrestart'))
      end
    end
  end
end

local function start_status()
  local state = lobby.state
  if phase() == 'starting' then
    return moving('Connecting to the multiplayer helper')
  end
  local games = type(state.games) == 'table' and #state.games or nil
  local lan = type(state.lan) == 'table' and #state.lan or nil
  if games or lan then
    if (games or 0) == 0 and (lan or 0) == 0 then
      return 'Nobody is hosting right now. Host a game, or join one with a code.'
    end
    return count(games or 0, 'public game', 'public games') .. ', ' .. (lan or 0) .. ' on your network.'
  end
  if phase() == 'browsing' then
    return moving('Looking for games')
  end
  return 'Host a game, or join one with a code or from the list.'
end

function native.start_view(options, frame, head)
  status_row(head, start_status())
  local geometry = options.table
  widen(options, frame)
  local y = body_y(head)
  local height = geometry.y + geometry.height - y
  local left_width = geometry.width
  local list_table = body_table(frame, 6, geometry.x, y, left_width, height, 2)
  list_table:setColWidth(1, geometry.arrowColumnWidth or Helper.scaleY(UI.ROW), false)
  list_table:setColWidthPercent(2, 34)
  list_table:setColWidthPercent(3, 16)
  list_table:setColWidthPercent(4, 21)
  list_table:setColWidthPercent(5, 10)
  games_table(list_table)
  local right_x = geometry.x + left_width + Helper.borderSize
  local right_width = (tonumber(geometry.widthExtraWide) or math.floor(left_width * 1.6)) - left_width - Helper.borderSize
  local form_table = body_table(frame, 4, right_x, y, right_width, height, 3)
  form_table:setColWidthPercent(1, 26)
  form_table:setColWidthPercent(2, 19)
  form_table:setColWidthPercent(3, 30)
  forms_table(form_table, right_width)
  head.properties.nextTable = list_table.index
  list_table.properties.prevTable = head.index
  list_table.properties.nextHorizontalTable = form_table.index
  form_table.properties.prevHorizontalTable = list_table.index
  form_table.properties.prevTable = head.index
  lobby.body = {list = list_table, forms = form_table}
end

-- The progress view ---------------------------------------------------------------------------------------------------
-- preparing, joining, loading, session: the steps, the current one's bar, the button that stops it.

local HOST_STEPS = {{key = 'session', text = 'Make the game'}, {key = 'online', text = 'Put it online for the others'}}
local JOIN_STEPS = {{key = 'lookup', text = 'Find the game'}, {key = 'download', text = 'Download the game'},
  {key = 'overlay', text = 'Prepare the game files'}, {key = 'x4', text = 'Start X4 with the game'}}

-- Whether this page hosts (the request it sent, or what the state says) and which step runs now.
local function progress_steps()
  local state = lobby.state
  local what = type(state.progress) == 'table' and state.progress.what or nil
  local current = phase()
  if current == 'joining' or what == 'download' or what == 'overlay' or what == 'lookup' then
    lobby.joining_flow = true
  end
  local joins = lobby.joining_flow or (lobby.last_request and not lobby.last_request:match('^HOST')) or false
  if current == 'loading' or current == 'session' then
    return {{text = 'Every player is ready', mark = 'done'}, {text = current == 'session' and 'Start the game' or 'Load the game',
      mark = 'now'}}
  end
  local steps = joins and JOIN_STEPS or HOST_STEPS
  local now_key
  if joins then
    now_key = current == 'joining' and 'lookup' or (what == 'download' and 'download') or (what == 'overlay' and 'overlay') or 'x4'
  else
    now_key = what == 'session' and 'session' or 'online'
  end
  local out, seen = {}, false
  for _, step in ipairs(steps) do
    local mark = seen and 'next' or (step.key == now_key and 'now' or 'done')
    if step.key == now_key then
      seen = true
    end
    out[#out + 1] = {text = step.text, mark = mark}
  end
  return out
end
native.progress_steps = progress_steps

local function progress_done()
  local progress = lobby.state.progress
  return type(progress) == 'table' and tonumber(progress.done) or nil
end

local PROGRESS_STATUS = {preparing = 'Getting the game ready', joining = 'Joining the game', loading = 'Loading the game',
  session = 'Starting the game'}

local function cancel_text()
  local current = phase()
  if current == 'loading' or current == 'session' then
    return 'Leave'
  end
  return 'Cancel'
end

function native.progress_view(options, frame, head)
  local current = phase()
  local first = line(1)
  local status = current == 'loading' and first ~= '' and first or moving(PROGRESS_STATUS[current] or 'Working')
  status_row(head, status)
  UI.section(head, 5, 'Getting ready', nil, 2)
  for _, step in ipairs(progress_steps()) do
    local row = head:addRow(false, {})
    if step.mark == 'done' then
      UI.icon(row, 1, 'widget_tick_01', 'icon_positive')
    end
    UI.text(row, 2, 1, step.text, step.mark == 'now' and 'strong' or (step.mark == 'next' and 'inactive' or 'label'))
    if step.mark == 'now' then
      UI.text(row, 3, 3, function()
        local done = progress_done()
        return done and (math.floor(math.max(0, math.min(1, done)) * 100) .. ' %') or ''
      end, 'label', {halign = 'right'})
      row = head:addRow(false, {})
      UI.bar(row, 2, 4, progress_done)
    end
  end
  if current ~= 'loading' and first ~= '' then   -- the launcher's own line: what it does now, how long it takes
    local row = head:addRow(false, {})
    UI.text(row, 2, 4, plain(first), 'info')
  end
  UI.line(head, 5, 2)
  if cancellable() then
    local row = head_row(head, {id = 'x4lan_lobby_cancel'}, {})
    UI.button(row, 4, 2, cancel_text(), function()
      request('CANCEL')
    end, true, current == 'loading' and 'Leave the game: the others load it without you' or 'Stop and go back',
      action_error('x4lan_lobby_cancel'))
  end
end

-- The game view -------------------------------------------------------------------------------------------------------
-- hosting, waiting: the join code big with Copy, the players with ready marks, the connection, Start.

local function copy_code()
  request('COPY')
end

local function copied()
  return lobby.copied_at ~= nil and now() - lobby.copied_at < 2
end

local function game_status()
  local state = lobby.state
  local players = type(state.players) == 'table' and state.players or {}
  local ready = 0
  for _, player in ipairs(players) do
    if type(player) == 'table' and player.ready then
      ready = ready + 1
    end
  end
  local game = type(state.game) == 'table' and state.game or {}
  local max = tonumber(game.players_max)
  local open = type(game.open) == 'table' and #game.open or 0
  local wanted = max and (max - open) or #players
  if state.host then
    if state.can_start then
      return 'Every player is ready: the game loads in a moment.'
    end
    return 'Waiting for players: ' .. ready .. ' of ' .. wanted .. ' ready.'
  end
  local slot = tonumber(state.slot)
  local name = type(game.name) == 'string' and field(game.name, MAX_NAME_BYTES) or ''
  return 'Joined' .. (name ~= '' and (' ' .. name) or '') .. (slot and (' as player ' .. whole(slot)) or '')
    .. '. The game loads when every player is ready.'
end
native.game_status = game_status

local function player_rows(head)
  local state = lobby.state
  local players = type(state.players) == 'table' and state.players or {}
  local game = type(state.game) == 'table' and state.game or {}
  local max = tonumber(game.players_max)
  local open = {}
  for _, slot in ipairs(type(game.open) == 'table' and game.open or {}) do
    open[tonumber(slot) or -1] = true
  end
  local own = state.host and 1 or tonumber(state.slot)
  local here = {}
  UI.section(head, 5, 'Players', (#players) .. (max and (' / ' .. whole(max)) or ''), 2, 2)
  for index, player in ipairs(players) do
    if type(player) == 'table' then
      local slot = tonumber(player.slot) or index
      here[slot] = true
      local you = slot == own
      local row = head:addRow({id = 'x4lan_lobby_player' .. slot}, {interactive = false})
      if player.ready then
        UI.icon(row, 1, 'widget_tick_01', 'icon_positive')
      else
        UI.icon(row, 1, 'order_wait', 'text_inprogress')
      end
      UI.text(row, 2, 1, 'Player ' .. slot, you and 'strong' or 'label', you and {color = 'text_player_current'} or nil)
      local role = player.host and (you and 'Host · You' or 'Host') or (you and 'You' or (open[slot] and 'Drop-in' or ''))
      UI.text(row, 3, 1, role, 'label', {color = 'text_lowlight'})
      UI.text(row, 4, 2, player.ready and 'Ready' or 'Getting ready', 'label',
        {color = player.ready and 'text_positive' or 'text_inprogress'})
    end
  end
  for slot = 1, max or 0 do   -- the slots nobody has taken yet
    if not here[slot] then
      local row = head:addRow({id = 'x4lan_lobby_player' .. slot}, {interactive = false})
      UI.text(row, 2, 1, 'Player ' .. slot, 'inactive')
      UI.text(row, 3, 1, open[slot] and 'Drop-in slot' or '', 'inactive')
      UI.text(row, 4, 2, open[slot] and 'Joins later' or 'Open', 'inactive')
    end
  end
end

function native.game_view(options, frame, head)
  local state = lobby.state
  status_row(head, game_status())
  local code = type(state.code) == 'string' and state.code ~= '' and state.code or nil
  local invite = type(state.invite) == 'string' and state.invite ~= '' and state.invite or nil
  if code or invite then
    UI.section(head, 5, invite and 'Invite code' or 'Join code', nil, 2)
    local row = head_row(head, {id = 'x4lan_lobby_copy', callback = copy_code}, {})
    if code and not invite and #code <= 16 then
      UI.text(row, 2, 1, field(code, MAX_NAME_BYTES), 'code')
    else
      UI.text(row, 2, 1, 'Invite code', 'strong', nil, invite or code)
    end
    local copy = UI.button(row, 3, 3, function()
      return copied() and 'Copied' or 'Copy'
    end, copy_code, true, 'Put the code on the clipboard to send it to the other players', action_error('x4lan_lobby_copy'))
    if code and not invite and #code <= 16 then   -- in the middle of the big code's row
      copy.properties.y = math.floor((UI.CODE - UI.ROW) / 2)
    end
    row = head:addRow(false, {})
    UI.text(row, 2, 4, state.host and ((invite and 'The invite code' or 'The code') .. ' is on your clipboard. Send it to the other players.')
      or 'Others can join this game with this code.', 'info')
    local game = type(state.game) == 'table' and state.game or {}
    if state.host and type(game.address) == 'string' and game.address:match('^[%d%.]+$') then
      row = head:addRow(false, {})
      UI.text(row, 2, 4, 'Players on your network can also join with the address ' .. game.address .. '.', 'info')
    end
  end
  if type(state.players) == 'table' and #state.players > 0 then
    player_rows(head)
  end
  local connection, details, ping, ping_ms = UI.connection(state.network)
  if connection then
    UI.section(head, 5, 'Connection', nil, 2)
    local row = head:addRow(false, {interactive = false})
    UI.text(row, 2, 1, connection, 'label', nil, details)
    UI.text(row, 3, 3, ping or '', 'label', {halign = 'right', color = UI.ping_color(ping_ms)})
    if type(state.network) == 'table' and state.network.firewall == 'blocked' then
      row = head_row(head, {id = 'x4lan_lobby_firewall'}, {})
      UI.icon(row, 1, 'workshop_error', 'icon_warning')
      UI.text(row, 2, 2, 'Windows Firewall blocks direct connections.', 'warning')
      UI.button(row, 4, 2, 'Allow', function()
        request('FIREWALL')
      end, true, 'Let other players connect to this PC directly instead of through the server (Windows asks first)',
        action_error('x4lan_lobby_firewall'))
    end
  end
  local game = type(state.game) == 'table' and state.game or nil
  if game then
    local parts = {}
    local name = field(game.name, MAX_NAME_BYTES)
    if name ~= '' then
      parts[#parts + 1] = name
    end
    if game.public ~= nil or state.public ~= nil then
      parts[#parts + 1] = (game.public == true or (game.public == nil and state.public == true)) and 'Public' or 'Private'
    end
    local start = game_start_name(game.start)
    if start then
      parts[#parts + 1] = 'Start: ' .. start
    end
    if #parts > 0 then
      UI.section(head, 5, 'Game', nil, 2)
      local row = head:addRow(false, {interactive = false})
      UI.text(row, 2, 4, table.concat(parts, ' · '), 'label')
    end
  end
  UI.line(head, 5, 2)
  local row = head_row(head, {id = 'x4lan_lobby_actions'}, {})
  if cancellable() then
    UI.button(row, state.host and 3 or 4, state.host and 1 or 2, state.host and 'Close the game' or 'Leave the game', function()
      request('CANCEL')
    end, true, state.host and 'The game is deleted; players who joined are told so' or 'Leave this game and go back',
      action_error('x4lan_lobby_cancel'))
  end
  if state.host then
    UI.button(row, 4, 2, 'Start the game', function()
      if lobby.state.can_start == true then
        request('START')
      end
    end, state.can_start == true, state.can_start == true and 'Load the game now with the players who are ready'
      or 'Starts when every player is ready', action_error('x4lan_lobby_startgame'))
  end
end

-- The error view ------------------------------------------------------------------------------------------------------
-- error, nohelper: what happened and what to do, in X4's warning colours, and no form under it.


local function error_lines()
  local out = {}
  for _, text in ipairs(type(lobby.state.lines) == 'table' and lobby.state.lines or {}) do
    text = tostring(text)
    if text ~= '' and text ~= 'Cancel to go back.' then
      out[#out + 1] = plain(text)
    end
  end
  return out
end

local function try_again()
  if lobby.last_request and free() then
    request(lobby.last_request)
  end
end

function native.error_view(options, frame, head)
  local helperless = phase() == 'nohelper'
  local lines = error_lines()
  local title = (lines[1] or 'Something went wrong.'):gsub(':$', '')
  local row = head:addRow(false, {fixed = true})
  UI.text(row, 2, 4, '', 'status')   -- the status line's place: the notice says it all
  row = head:addRow(false, {})
  UI.icon(row, 1, 'workshop_error', helperless and 'icon_warning' or 'icon_error')
  UI.text(row, 2, 4, title, 'title', {color = helperless and 'text_warning' or 'text_error'})
  for index = 2, #lines do
    row = head:addRow(false, {})
    UI.text(row, 2, 4, lines[index], 'body')
  end
  UI.line(head, 5, 2)
  row = head_row(head, {id = 'x4lan_lobby_actions'}, {})
  if helperless then
    UI.button(row, 4, 2, 'Try again', function()
      if lobby.reset_misses then
        lobby.reset_misses()
      end
    end, true, 'Look for the multiplayer helper again', action_error('x4lan_lobby_retry'))
    return
  end
  UI.button(row, 3, 1, 'Back', function()
    request('CANCEL')
  end, true, 'Back to the Multiplayer page', action_error('x4lan_lobby_cancel'))
  if lobby.last_request then
    UI.button(row, 4, 2, 'Try again', try_again, true, 'Send the same host or join request once more',
      action_error('x4lan_lobby_retry'))
  end
end

-- The restart notice --------------------------------------------------------------------------------------------------

local function quit_left()
  if not lobby.quit_at or not lobby.quit_total then
    return nil
  end
  return math.max(0, lobby.quit_at - now()) / lobby.quit_total
end

function native.restart_view(options, frame, head)
  local row = head:addRow(false, {fixed = true})
  UI.text(row, 2, 4, '', 'status')
  row = head:addRow(false, {})
  UI.text(row, 2, 4, 'X4 restarts into the game', 'title')
  for _, text in ipairs(error_lines()) do
    row = head:addRow(false, {})
    UI.text(row, 2, 4, text, 'body')
  end
  row = head:addRow(false, {})
  UI.text(row, 2, 4, 'Your own profile and saves are not touched.', 'info')
  row = head:addRow(false, {})
  UI.bar(row, 2, 4, quit_left)
end

-- The start choice of a drop-in join ----------------------------------------------------------------------------------
-- choosing (state.choices, state.slot): the starts in X4's own list (the head, so the arrow keys move through it as on
-- X4's New game page), the selected one's picture and text beside it (function cells reading menu.selectedOption).

function native.choosing_view(options, frame, head)
  local slot = tonumber(lobby.state.slot)
  status_row(head, 'A running game: choose where you start.' .. (slot and (' You join as player ' .. whole(slot) .. '.') or ''))
  local list, total = choices()
  local list_y = body_y(head)
  UI.section(head, 5, 'Starts', nil, 2)
  local any_module = false
  for _, choice in ipairs(list) do
    local id = 'x4lan_lobby_choice' .. string.format('%d', choice.index)
    local row = head_row(head, {id = id, choice = choice.index, gamestart = choice.gamestart, label = choice.label})
    if choice.index == 0 then
      UI.icon(row, 1, 'menu_recommended', nil, CHOICE_TIP_HOST)
    end
    UI.text(row, 2, 1, choice.label, 'label', nil, choice.index == 0 and CHOICE_TIP_HOST or CHOICE_TIP)
    UI.button(row, 3, 3, 'Start here', function()
      pick_start(choice.index)
    end, choosing(), choice.index == 0 and CHOICE_TIP_HOST or CHOICE_TIP, action_error(id))
    any_module = any_module or start_module(choice.gamestart) ~= nil
  end
  if total > #list then
    local row = head:addRow(false, {})
    UI.text(row, 2, 4, 'and ' .. (total - #list) .. ' more', 'inactive')
  end
  UI.line(head, 5, 2)
  local row = head_row(head, {id = 'x4lan_lobby_cancel'}, {})
  UI.button(row, 4, 2, 'Cancel', function()
    request('CANCEL')
  end, true, 'Do not join: back to the Multiplayer page', action_error('x4lan_lobby_cancel'))
  if not any_module then
    return
  end
  -- the selected start's picture and text (the row X4's menu has selected: options.selectedOption)
  widen(options, frame)
  local geometry = options.table
  local right_x = geometry.x + geometry.width + Helper.borderSize
  local right_width = (tonumber(geometry.widthExtraWide) or math.floor(geometry.width * 1.6)) - geometry.width - Helper.borderSize
  local info_table = body_table(frame, 1, right_x, list_y, right_width, geometry.y + geometry.height - list_y, 0)
  local function selected()
    local data = options.selectedOption
    local start = type(data) == 'table' and data.gamestart and start_module(data.gamestart) or nil
    return data, start
  end
  row = info_table:addRow(false, {bgColor = UI.color('optionsmenu_cell_background')})
  row[1]:createIcon(function()
    local _, start = selected()
    return start and start.image or 'gamestart_default'
  end, {width = right_width, height = math.floor(right_width * 9 / 16), scaling = false})
  row = info_table:addRow(false, {bgColor = UI.color('optionsmenu_cell_background')})
  UI.text(row, 1, 1, function()
    local data, start = selected()
    return start and tostring(start.name or '') or (type(data) == 'table' and tostring(data.label or '') or '')
  end, 'strong')
  row = info_table:addRow(false, {bgColor = UI.color('optionsmenu_cell_background')})
  UI.text(row, 1, 1, function()
    local data, start = selected()
    local text = start and tostring(start.description or '') or ''
    if type(data) == 'table' and data.choice == 0 then
      return CHOICE_TIP_HOST .. '.'
    end
    return (text ~= '' and (text .. '\n\n') or '') .. 'Your ship is made in the running game when you join; the others play on.'
  end, 'info')
end

-- The in-session page in the start menu (x4ls lobby --page-preview, phase preview_session, no launcher sends it): the
-- probe's page as UI.session_page draws it in a session, from the state (slot, clients, confirming), to see it in X4.
function native.preview_session_view(options, frame, head)
  local state = lobby.state
  UI.session_page(head, {slot = tonumber(state.slot) or 2, clients = tonumber(state.clients) or 2, left = state.left == true,
    lobby = state, confirming = state.confirming == true, relay = 'Relay 127.0.0.1:47000',
    active = function() return state.left ~= true end, minutes = function() return '12:34' end,
    copy = function() request('COPY') end, copied = function() return false end,
    confirm = function() end, leave = function() end})
end

local VIEWS = {idle = 'start', browsing = 'start', starting = 'start', preparing = 'progress', joining = 'progress',
  loading = 'progress', session = 'progress', hosting = 'game', joined = 'game', waiting = 'game', error = 'error',
  nohelper = 'error', restart = 'restart', choosing = 'choosing', preview_session = 'preview_session'}
native.VIEWS = VIEWS

-- Whether the views can be drawn here: X4's frame of the page with its table (the head), Helper, the menu's geometry.
local function native_ready(options, head)
  local frame = options.optionsFrame
  local geometry = options.table
  return lobby_config.netplay == true and not lobby.native_off and type(frame) == 'table'
    and type(frame.addTable) == 'function' and type(frame.properties) == 'table' and type(head) == 'table'
    and type(head.addRow) == 'function' and type(head.properties) == 'table' and type(Helper) == 'table'
    and type(Helper.scaleY) == 'function' and type(geometry) == 'table' and tonumber(geometry.x) ~= nil
    and tonumber(geometry.width) ~= nil and tonumber(geometry.height) ~= nil
end

-- The view of the phase into X4's page; false when the rows are drawn instead. A view that fails is not tried again:
-- the page is drawn again at the next frame, with the rows (one DRAW_FALLBACK row in the log).
local function draw_native(options, head)
  if not native_ready(options, head) then
    return false
  end
  lobby.body = nil
  local view = native[(VIEWS[phase()] or 'start') .. '_view']
  local ok, why = pcall(view, options, options.optionsFrame, head)
  if not ok then
    lobby.native_off = true
    lobby.redraw_next = true
    note('DRAW_FALLBACK ' .. tostring(why))
  end
  return true
end
lobby.draw_native = draw_native

local function install(options)
  local rows = page_rows()
  -- lobby.title: the entry's name (a playtest kit's menu says so, so a PC with it and an X4 Online install shows two
  -- rows that can be told apart)
  local title = type(lobby_config.title) == 'string' and lobby_config.title ~= '' and lobby_config.title or 'Multiplayer'
  local entry = {id = PAGE, name = title, submenu = PAGE, mouseOverText = 'Host or join a multiplayer game'}
  local stock_row = options.displayOption
  local stock_draw = options.displayOptions
  local stock_handle = options.submenuHandler
  options.displayOption = function(ftable, option, ...)
    if lobby.drawing then   -- the stock page's rows: ours instead, once
      if not lobby.drawn then
        lobby.drawn = true
        if not draw_native(options, ftable) then
          draw_rows(stock_row, ftable, ...)
        end
      end
      return
    end
    local main = options.currentOption == 'main' and options.isStartmenu
    if main and type(option) == 'table' and option.id == 'settings' then
      pcall(stock_row, ftable, entry, ...)
    end
    return stock_row(ftable, option, ...)
  end
  options.displayOptions = function(parameter, ...)
    if parameter ~= PAGE then
      return stock_draw(parameter, ...)
    end
    lobby.drawing = true
    lobby.drawn = false
    lobby.editing = false   -- a new page has no active edit box
    lobby.stale = false
    local ok, why = pcall(stock_draw, 'multiplayer', ...)
    lobby.drawing = false
    options.currentOption = PAGE   -- a refresh draws this page again
    if not ok then
      note('DRAW_ERROR ' .. tostring(why))
    elseif not lobby.drawn then
      note('DRAW_EMPTY the menu drew no row of the multiplayer page')
    end
    note('PAGE phase=' .. tostring(lobby.state.phase))   -- the page was drawn (opened, or refreshed for a new state)
  end
  options.submenuHandler = function(parameter, ...)
    if parameter == PAGE then
      return options.displayOptions(PAGE)
    end
    return stock_handle(parameter, ...)
  end
  lobby.options = options
  lobby.installed = true
  note('MENU_INSTALLED rows=' .. #rows)
end

-- X4's "Modified game detected" notice at the start menu: in the launcher's X4 this is the disposable multiplayer
-- profile, where it says nothing new and costs a click at every start. In a Steam-started X4 (PIPE) it is the player's
-- own game and the notice is theirs to read: left alone.
local function close_modified_notice(options)
  if (PIPE and not lobby_config.preview) or options.contextMenuMode ~= 'modified' then
    return
  end
  if type(__CORE_DETAILMONITOR_USERQUESTION) == 'table' then
    __CORE_DETAILMONITOR_USERQUESTION['modified'] = true
  end
  local closed = type(options.closeContextMenu) == 'function' and pcall(options.closeContextMenu)
  options.contextMenuMode = nil
  note('MODIFIED_NOTICE closed=' .. tostring(closed))
end

local function find_options_menu()
  if type(Menus) ~= 'table' then
    return nil
  end
  local found
  for _, menu in ipairs(Menus) do
    if type(menu) == 'table' and menu.name == 'OptionsMenu' then
      found = menu
    end
  end
  return found
end

local function try_install()
  if lobby.installed or lobby.tries >= INSTALL_TRIES then
    return
  end
  lobby.tries = lobby.tries + 1
  local options = find_options_menu()
  if not options then
    return
  end
  lobby.tries = INSTALL_TRIES   -- one attempt once the menu is there
  local functions = type(options.displayOption) == 'function' and type(options.displayOptions) == 'function'
    and type(options.submenuHandler) == 'function'
  if not functions then
    note('MENU_REFUSED why=no_menu_functions')
    return
  end
  install(options)
  close_modified_notice(options)
end

-- Polling ----------------------------------------------------------------------------------------------------------------

local polls = 0
local read_failed = false
local misses = 0
local NOHELPER_MISSES = 3   -- unanswered pipe reads before the page says the helper is missing

local function state_chunk()
  if not PIPE then
    return chunk_of('lobby/state.lua')
  end
  local ok, chunk, why = pcall(loadfile, PIPE .. 'state')
  if ok and chunk then
    return chunk
  end
  return nil, tostring(ok and why or chunk)
end

local function read_state()
  local chunk, why = state_chunk()
  if not chunk then
    return nil, why
  end
  local ok, state = pcall(chunk)
  if not ok or type(state) ~= 'table' then
    return nil, state
  end
  return state
end

-- Try again on the nohelper notice: the helper is looked for again (NOHELPER_MISSES more reads before the notice).
lobby.reset_misses = function()
  misses = 0
  lobby.state = {phase = 'starting', lines = {'Starting...'}}
  redraw()
end

-- The state could not be read: the launcher may be replacing the file right now, and the next poll reads it.
local function state_missed(why)
  if not read_failed then
    read_failed = true
    note('STATE_UNREAD ' .. tostring(why))
  end
  misses = misses + 1
  if PIPE and misses == NOHELPER_MISSES and lobby.state.phase ~= 'nohelper' then   -- nobody answers the pipe
    local what = lobby_config.netplay and 'the multiplayer helper' or '"X4 Multiplayer Playtest"'
    lobby.state = {phase = 'nohelper', lines = {'The multiplayer helper is not running.',
      'Start ' .. what .. ' from the Windows Start menu, then open this page again.'}}
    redraw()
  end
end

-- The public list once, when the open page first shows the start rows without games (Refresh asks again).
local function first_browse()
  local state = lobby.state
  if not lobby_config.netplay or lobby.browsed or state.phase ~= 'idle' or state.games ~= nil or not page_open() then
    return
  end
  lobby.browsed = true
  request('BROWSE')
end

-- lobby.show (x4ls lobby --show, a test switch): the page opens by itself once, as if Multiplayer had been chosen.
local function show_once(state)
  if not lobby_config.show or lobby.shown or not lobby.installed or state.phase == 'starting' then
    return
  end
  lobby.shown = true
  local opener = lobby.options.openSubmenu or lobby.options.submenuHandler
  local shown, err = pcall(opener, PAGE, PAGE)
  note('SHOW ' .. tostring(shown) .. ' ' .. tostring(err))
end

-- lobby.auto (a test switch of the Steam menu page): 'join' chooses Join once, as if it had been clicked; 'start' the
-- first free start of the running game.
local function auto_once(state)
  if lobby.autoed or state.phase ~= 'idle' then
    return
  end
  if lobby_config.auto == 'join' then
    lobby.autoed = true
    request('JOIN')
    return
  end
  if lobby_config.auto ~= 'start' or state.game ~= 'running' or not state.joinable or type(state.starts) ~= 'table' then
    return
  end
  for _, start in ipairs(state.starts) do
    local slot = tonumber(start.slot)
    if not start.taken and slot then
      lobby.autoed = true
      request('JOIN' .. slot)
      return
    end
  end
end

local function apply_state(state)
  local changed = state.phase ~= lobby.state.phase
  lobby.state = state
  if changed then
    note('PHASE ' .. tostring(state.phase))
  end
  if state.phase == 'session' and not PIPE then
    start_session()
    return
  end
  if state.phase == 'restart' and PIPE and not lobby.quit_at then   -- the page shows why, then X4 closes
    local after = tonumber(state.quit_after) or QUIT_DELAY_S
    lobby.quit_total = math.max(1, math.min(30, after))
    lobby.quit_at = now() + lobby.quit_total
    note('RESTART notice for ' .. after .. ' s, then X4 quits')
  end
  if state.phase == 'idle' or state.phase == 'browsing' then   -- the next progress is a new host or join
    lobby.joining_flow = nil
  end
  show_once(state)
  auto_once(state)
  redraw()
end

local function update()
  if lobby.loaded then
    return
  end
  pcall(try_install)
  polls = polls + 1
  frames_run = frames_run + 1
  if lobby.quit_at and now() >= lobby.quit_at then   -- the helper starts X4 again for the session once this one is gone
    lobby.quit_at = nil
    note('QUIT for the session')
    if type(QuitGame) == 'function' then
      QuitGame()
    end
    return
  end
  -- A redraw held while the player typed, now; or, while the box stays active, once no key came for EDIT_HOLD_S (a
  -- box X4 keeps active must not hold a new list or a new phase back: the public games of 25 Nov never showed while
  -- the code box was active). The typed text stays (the form keeps it).
  if lobby.stale and (not lobby.editing or now() - (lobby.typed_at or 0) >= EDIT_HOLD_S) then
    lobby.stale = false
    lobby.editing = false
    redraw()
  end
  if lobby.redraw_next then   -- the form changed what the page shows (the player counts), or a view failed: drawn again
    lobby.redraw_next = false
    redraw()
  end
  local answer = lobby.answer   -- the helper's state at the end of a spelled request
  if answer then
    lobby.answer = nil
    if answer.stamp ~= lobby.state.stamp then
      apply_state(answer)
      if lobby.loaded then
        return
      end
    end
  end
  if polls % (PIPE and PIPE_POLL_FRAMES or POLL_FRAMES) ~= 0 then   -- a few times a second
    return
  end
  local state, why = read_state()
  if not state then
    state_missed(why)
    return
  end
  misses = 0
  if state.stamp ~= lobby.state.stamp then
    apply_state(state)
    if lobby.loaded then
      return
    end
  end
  pcall(first_browse)
end
lobby.update = update
SetScript('onUpdate', update)
note('READY files=' .. #(lobby_config.files or {}))
