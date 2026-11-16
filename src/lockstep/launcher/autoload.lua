-- x4ls autoload (x4ls resume's prepare step only): from X4's start menu, load the staged save_009 once.
-- The UI reloads when the game is loaded; the start-menu check keeps this from repeating.
if not (X4LAN_CONFIG and X4LAN_CONFIG.x4ls_autoload == true) then return end
local ffi = require('ffi')
pcall(ffi.cdef, 'const char* GetGameStartName(void);')
if ffi.string(ffi.C.GetGameStartName()) ~= 'startmenu' then return end
DebugError('[X4LAN] X4LS_AUTOLOAD save_009')
Helper.addDelayedOneTimeCallbackOnUpdate(function() LoadGame('save_009') end, true, getElapsedTime() + 3)
