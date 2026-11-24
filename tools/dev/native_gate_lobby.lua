-- Lobby (x4ls lobby): the session is chosen in X4's main menu, not on a command line.
--
-- x4ls lobby starts this PC's X4 with the play overlay installed but only this file listed in ui.xml (after config.lua and
-- sha256.lua). In the start menu it adds a "Multiplayer" entry to X4's own options menu (before Settings). The page has
-- one of three forms:
--   lobby.netplay (a release's menu extension, or x4ls lobby running netplay): the launcher's state table
--     (docs/NETPLAY.md, "The page and the launcher"): the state's lines, then by phase either Host a game (public or
--     private, 2..8 players, a name), Join a game (a text box for a join code, a long invite code or an address; or the
--     code on the clipboard), the public and LAN games, or the game being made: its code (Copy), the connection, the
--     players, Start for the host, Cancel.
--   lobby.server (a playtest kit with a server): "Join the game", and the start picker of a running game.
--   neither (the LAN lobby of before netplay): "Host a game" and "Join a game (invite code from the clipboard)".
--
-- The game and the launcher talk in the two ways X4's UI Lua allows (it has no io library, and the Win32 calls of
-- LuaJIT's ffi are refused there: "Access to FFI function ... is restricted"):
--   game -> launcher: a log row "[X4LAN] LOBBY REQUEST <text>" (the contract's requests: HOST|<public>|<players>|<name>,
--     JOINCODE|<text>, JOIN, BROWSE, JOINGAME|<id>, JOINLAN|<index>, START, CANCEL, COPY; the LAN and server pages send
--     HOST, JOIN, JOIN<k>, COPY and CANCEL as before); the launcher follows the log. The launcher, not the game, reads
--     and writes the clipboard.
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
local QUIT_DELAY_FRAMES = 90              -- the restart notice stays this long before X4 quits
local INSTALL_TRIES = 3600                -- frames to wait for X4's options menu
local MIN_PLAYERS = 2
local MAX_PLAYERS = 8

-- form: what the player chose or typed on the page; it outlives redraws (X4 draws the page anew for every state).
local form = {public = false, players = MIN_PLAYERS, name = '', code = ''}
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

local function request(text)
  note('REQUEST ' .. text)
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

local GAME_STATE_NAMES = {lobby = 'open', loading = 'loading', running = 'running'}

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
  local ping = tonumber(game.ping_ms)
  if ping then
    parts[#parts + 1] = whole(ping) .. ' ms'
  end
  local why
  if game.compatible == false then
    why = 'another release'
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
  request('HOST|' .. public .. '|' .. form.players .. '|' .. name)
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
for count = MIN_PLAYERS, MAX_PLAYERS do
  local choice = {id = tostring(count), text = tostring(count), icon = '', displayremoveoption = false}
  PLAYER_CHOICES[#PLAYER_CHOICES + 1] = choice
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
    if count and count >= MIN_PLAYERS and count <= MAX_PLAYERS then
      form.players = math.floor(count)
    end
  end
  rows[#rows + 1] = {id = 'x4lan_lobby_players', name = 'Players', valuetype = 'dropdown', value = players_value,
    callback = choose_players, selectable = free, mouseOverText = 'How many players the game has, you included'}
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
  end
  cell.handlers.onTextChanged = function(_, text)
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
        draw_rows(stock_row, ftable, ...)
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
  if PIPE or options.contextMenuMode ~= 'modified' then
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
    lobby.quit_at = polls + QUIT_DELAY_FRAMES
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
  if lobby.quit_at and polls >= lobby.quit_at then   -- the helper starts X4 again for the session once this one is gone
    lobby.quit_at = nil
    note('QUIT for the session')
    if type(QuitGame) == 'function' then
      QuitGame()
    end
    return
  end
  if lobby.stale and not lobby.editing then   -- a redraw held while the player typed
    lobby.stale = false
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
