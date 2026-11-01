-- x4ls autostart (snapshot capture only): from X4's start menu, start one new game.
-- The UI reloads when the new game begins; the start-menu check keeps this from repeating.
local gamestart = X4LAN_CONFIG and X4LAN_CONFIG.x4ls_autostart
if type(gamestart) ~= 'string' or not gamestart:match('^[A-Za-z0-9_]+$') then return end
local ffi = require('ffi')
pcall(ffi.cdef, 'const char* GetGameStartName(void);')
local current = ffi.string(ffi.C.GetGameStartName())
if current ~= 'startmenu' then return end
DebugError('[X4LAN] X4LS_AUTOSTART new game ' .. gamestart)
Helper.addDelayedOneTimeCallbackOnUpdate(function() NewGame(gamestart) end, true, getElapsedTime() + 3)
