-- Lobby (x4ls lobby): the session is chosen in X4's main menu, not on a command line.
--
-- x4ls lobby starts this PC's X4 with the play overlay installed but only this file listed in ui.xml (after config.lua and
-- sha256.lua). In the start menu it adds a "Multiplayer" entry to X4's own options menu (before Settings), whose page has
-- "Host a game" and "Join a game (invite code from the clipboard)"; with a server named in the kit (lobby.server) it has
-- "Join the game" only.
-- The game and the launcher talk in the two ways X4's UI Lua allows (it has no io library, and the Win32 calls of
-- LuaJIT's ffi are refused there: "Access to FFI function ... is restricted"):
--   game -> launcher: a log row "[X4LAN] LOBBY REQUEST <text>" (HOST, JOIN, COPY, CANCEL); the launcher follows the log.
--     The launcher, not the game, reads and writes the clipboard.
--   launcher -> game: lobby/state.lua in the installed extension, a Lua file that returns a table (what the page shows);
--     loadfile reads it again a few times a second.
-- Once every player is there the launcher rewrites ui/config.lua with the session (relay, token, slot, the common load
-- second) and says so in the state. Then this file runs the new config.lua and the overlay's ui files in their ui.xml
-- order, as X4 would have, and the probe takes over: it loads the snapshot at the agreed second.
-- After the load X4 builds a new UI state and runs ui.xml again: config.lua now says lobby.session, and the overlay's
-- files are run at once, with no menu.
-- No stock file is changed; the menu functions are wrapped as the in-session page does (native_gate_probe.lua mp_menu).
--
-- The same page in an X4 that Steam started (lobby.pipe, the menu extension of a kit with a server): that X4 runs the
-- player's own profile, with no launcher behind it, no log file and X4's protected UI mode on. loadfile still opens a
-- named pipe there, so a helper outside the game (lockstep.launcher.steamplay) answers every open with the state, and
-- which pipe was opened is the request (<pipe>-state, <pipe>-join). The session cannot run in the player's own
-- profile: on Join the helper says "restart", this file quits X4, and the helper starts X4 again as x4ls lobby does,
-- in the disposable profile, where it joins by itself. Nothing of the player's profile is read or written.
local cfg=X4LAN_CONFIG
local L=type(cfg)=='table' and cfg.lobby or nil
if type(L)~='table' then return end
local function note(text) DebugError('[X4LAN] LOBBY '..tostring(text)) end
local DIR='./extensions/x4_lan_lab/'
local PIPE=type(L.pipe)=='string' and ('\\\\.\\pipe\\'..L.pipe..'-') or nil
-- A file of the installed extension as a chunk; nil and the reason when it is not there or does not compile.
local function chunk_of(name)
 local ok,chunk,why=pcall(loadfile,DIR..name)
 if ok and chunk then return chunk end
 return nil,tostring(ok and why or chunk)
end
-- The overlay's ui files, in their ui.xml order, as X4's loader would have run them.
local function run_files()
 for _,name in ipairs(L.files or {}) do
  local chunk,why=chunk_of(name)
  if not chunk then note('ERROR missing '..name..' '..why);return false end
  local ok,err=pcall(chunk)
  if not ok then note('ERROR '..name..' '..tostring(err));return false end
 end
 note('SESSION_FILES run='..#(L.files or {}))
 return true
end
if L.session then run_files();return end   -- after the load: the session is on, no menu

local lobby={state={phase='starting',lines={'Starting...'}},installed=false,tries=0,loaded=false}
X4LAN_LOBBY=lobby   -- tests reach it
local function request(text)
 note('REQUEST '..text)
 if PIPE then pcall(loadfile,PIPE..text:lower()) end   -- the helper sees which pipe was opened
end
lobby.request=request
local function line(k)
 local lines=lobby.state.lines
 return type(lines)=='table' and lines[k] and tostring(lines[k]) or ''
end
local function phase() return lobby.state.phase or 'idle' end
local function start_session()
 if lobby.loaded then return end
 lobby.loaded=true
 local chunk,why=chunk_of('ui/config.lua')
 if not chunk then note('ERROR config '..why);return end
 local ok,err=pcall(chunk)
 if not ok or type(X4LAN_CONFIG)~='table' or type(X4LAN_CONFIG.lobby)~='table' or not X4LAN_CONFIG.lobby.session then
  note('ERROR config has no session '..tostring(err));return
 end
 cfg=X4LAN_CONFIG;L=cfg.lobby
 note('SESSION slot='..tostring(cfg.slot)..' clients='..tostring(cfg.native_gate_clients)..' arm_at='..tostring(cfg.native_gate_arm_at))
 run_files()
end
-- The page's rows, made again for every draw: with a server whose game runs (state.game=='running', from the helper)
-- there is one row per open start, and choosing one joins the running game in that slot.
local function page_rows()
 local rows={}
 for k=1,6 do rows[#rows+1]={id='header',name=function() return line(k) end} end
 rows[#rows+1]={id='line'}
 local starts=L.server and lobby.state.game=='running' and type(lobby.state.starts)=='table' and lobby.state.starts or nil
 if starts then
  for _,start in ipairs(starts) do
   local slot,label=tonumber(start.slot),tostring(start.label or start.start or '')
   if slot then
    rows[#rows+1]={id='x4lan_lobby_start'..slot,name='Join as '..label,value=start.taken and 'Taken' or 'Join',valuetype='button',
     mouseOverText='Join the running game with this start: X4 loads the game as it is now',
     callback=function() request('JOIN'..slot) end,
     selectable=function() return phase()=='idle' and not start.taken and lobby.state.joinable~=false end}
   end
  end
 elseif L.server then   -- the kit names a server (x4ls serve): nobody hosts, there is no code, Join is the one choice
  rows[#rows+1]={id='x4lan_lobby_join',name=lobby.state.join_label or 'Join the game',value='Join',valuetype='button',
   mouseOverText='Join the server; the game loads when every player is here',
   callback=function() request('JOIN') end,selectable=function() return phase()=='idle' end}
 else
  rows[#rows+1]={id='x4lan_lobby_host',name='Host a game',value='Host',valuetype='button',
   mouseOverText='Start a session on this PC and get an invite code for the other players',
   callback=function() request('HOST') end,selectable=function() return phase()=='idle' end}
  rows[#rows+1]={id='x4lan_lobby_join',name='Join a game (invite code from the clipboard)',value='Join',valuetype='button',
   mouseOverText='Copy the invite code the host sent you, then press this',
   callback=function() request('JOIN') end,selectable=function() return phase()=='idle' end}
  rows[#rows+1]={id='x4lan_lobby_copy',name='Copy the invite code again',value='Copy',valuetype='button',
   callback=function() request('COPY') end,
   selectable=function() return phase()=='hosting' and lobby.state.invite~=nil end}
 end
 rows[#rows+1]={id='x4lan_lobby_cancel',name='Cancel',value='Cancel',valuetype='button',
  callback=function() request('CANCEL') end,
  selectable=function() local p=phase() return p=='hosting' or p=='joined' or p=='joining' end}
 return rows
end
local function install(options)
 local rows=page_rows()
 local entry={id='x4lan_lobby',name='Multiplayer',submenu='x4lan_lobby',mouseOverText='Host or join a lockstep session'}
 local row,draw,handle=options.displayOption,options.displayOptions,options.submenuHandler
 options.displayOption=function(ftable,option,...)
  if lobby.drawing then   -- the stock page's rows: ours instead, once
   if not lobby.drawn then lobby.drawn=true;for _,r in ipairs(page_rows()) do row(ftable,r,...) end end
   return
  end
  if options.currentOption=='main' and options.isStartmenu and type(option)=='table' and option.id=='settings' then row(ftable,entry,...) end
  return row(ftable,option,...)
 end
 options.displayOptions=function(parameter,...)
  if parameter~='x4lan_lobby' then return draw(parameter,...) end
  lobby.drawing,lobby.drawn=true,false
  local ok,why=pcall(draw,'multiplayer',...)
  lobby.drawing=false
  options.currentOption='x4lan_lobby'   -- a refresh draws this page again
  if not ok then note('DRAW_ERROR '..tostring(why)) end
 end
 options.submenuHandler=function(parameter,...)
  if parameter=='x4lan_lobby' then return options.displayOptions('x4lan_lobby') end
  return handle(parameter,...)
 end
 lobby.options=options
 lobby.installed=true
 note('MENU_INSTALLED rows='..#rows)
end
local function try_install()
 if lobby.installed or lobby.tries>=3600 then return end
 lobby.tries=lobby.tries+1
 if type(Menus)~='table' then return end
 local options
 for _,m in ipairs(Menus) do if type(m)=='table' and m.name=='OptionsMenu' then options=m end end
 if not options then return end
 lobby.tries=3600   -- one attempt once the menu is there
 if type(options.displayOption)~='function' or type(options.displayOptions)~='function' or type(options.submenuHandler)~='function' then
  note('MENU_REFUSED why=no_menu_functions');return
 end
 install(options)
end
local polls,read_failed,misses=0,false,0
local function state_chunk()
 if not PIPE then return chunk_of('lobby/state.lua') end
 local ok,chunk,why=pcall(loadfile,PIPE..'state')
 if ok and chunk then return chunk end
 return nil,tostring(ok and why or chunk)
end
local function update()
 if lobby.loaded then return end
 pcall(try_install)
 polls=polls+1
 if lobby.quit_at and polls>=lobby.quit_at then   -- the helper starts X4 again for the session once this one is gone
  lobby.quit_at=nil
  note('QUIT for the session')
  if type(QuitGame)=='function' then QuitGame() end
  return
 end
 if polls%(PIPE and 40 or 20)~=0 then return end   -- a few times a second
 local chunk,why=state_chunk()
 local ok,s=false,nil
 if chunk then ok,s=pcall(chunk) end
 if not chunk or not ok or type(s)~='table' then   -- the launcher may be replacing the file right now: the next poll reads it
  if not read_failed then read_failed=true;note('STATE_UNREAD '..tostring(why or s)) end
  misses=misses+1
  if PIPE and misses==3 and lobby.state.phase~='nohelper' then   -- nobody answers the pipe: say what to do
   lobby.state={phase='nohelper',lines={'The multiplayer helper is not running.',
    'Start "X4 Multiplayer Playtest" from the Windows Start menu, then open this page again.'}}
   local o=lobby.options
   if o and o.currentOption=='x4lan_lobby' and type(o.refresh)=='function' then pcall(o.refresh) end
  end
  return
 end
 misses=0
 if s.stamp==lobby.state.stamp then return end
 local changed=s.phase~=lobby.state.phase
 lobby.state=s
 if changed then note('PHASE '..tostring(s.phase)) end
 if s.phase=='session' and not PIPE then start_session();return end
 if s.phase=='restart' and PIPE and not lobby.quit_at then lobby.quit_at=polls+90 end   -- the page shows why, then X4 closes
 -- lobby.show (x4ls lobby --show, a test switch): the page opens by itself once, as if Multiplayer had been chosen
 if L.show and not lobby.shown and lobby.installed and s.phase~='starting' then
  lobby.shown=true
  local opener=lobby.options.openSubmenu or lobby.options.submenuHandler
  local shown,err=pcall(opener,'x4lan_lobby','x4lan_lobby')
  note('SHOW '..tostring(shown)..' '..tostring(err))
 end
 -- lobby.auto (a test switch of the Steam menu page): Join is chosen by itself once, as if it had been clicked
 if L.auto=='join' and not lobby.autoed and s.phase=='idle' then lobby.autoed=true;request('JOIN') end
 local o=lobby.options
 if o and o.currentOption=='x4lan_lobby' and type(o.refresh)=='function' then pcall(o.refresh) end
end
lobby.update=update
SetScript('onUpdate',update)
note('READY files='..#(L.files or {}))
