-- In-place feasibility prototype (tools/dev/inplace_proto/proto.py), stand-in for the release's menu extension.
-- Runs in X4's protected UI mode (no package, io, os): it reads commands with loadfile from cmd.lua beside it (the
-- driver writes the file) and answers with log rows "[X4INPLACE] ...". Nothing here writes a file.
local DIR = './extensions/x4inplace_proto/'
local function note(text)
  DebugError('[X4INPLACE] ' .. tostring(text))
end

local ffi_ok, ffi = pcall(require, 'ffi')
local C = ffi_ok and ffi.C or nil
if ffi_ok then
  pcall(ffi.cdef, 'const char* GetSaveFolderPath(void); bool IsStartmenu(void); bool IsSaveListLoadingComplete(void); void ReloadSaveList(void);')
end

local function folder()
  if not C then
    return '?'
  end
  local ok, path = pcall(function() return ffi.string(C.GetSaveFolderPath()) end)
  return ok and path or ('error ' .. tostring(path))
end

local function startmenu()
  if not C then
    return '?'
  end
  local ok, value = pcall(function() return C.IsStartmenu() end)
  return ok and tostring(value) or 'error'
end

note('LUA_READY package=' .. type(package) .. ' io=' .. type(io) .. ' os=' .. type(os) .. ' debug=' .. type(debug)
  .. ' ffi=' .. tostring(ffi_ok) .. ' loadfile=' .. type(loadfile) .. ' startmenu=' .. startmenu()
  .. ' savefolder=' .. folder())

pcall(RegisterEvent, 'x4inplace.md', function(_, param) note('MD_EVENT ' .. tostring(param)) end)

local last = nil
local frames = 0
local waiting_list = nil

local function ware()
  local ok, name = pcall(GetWareData, 'x4inplace_proto_ware', 'name')
  return 'ok=' .. tostring(ok) .. ' name=' .. tostring(name)
end

local function run(c)
  local op = c.op
  if op == 'info' then
    note('RES ' .. c.id .. ' info package=' .. type(package) .. ' startmenu=' .. startmenu() .. ' savefolder=' .. folder()
      .. ' ware=' .. ware())
  elseif op == 'reload' then
    C.ReloadSaveList()
    waiting_list = c.id
  elseif op == 'load' then
    note('RES ' .. c.id .. ' loading ' .. tostring(c.arg) .. ' from ' .. folder())
    LoadGame(c.arg)
  elseif op == 'save' then
    local ok, err = pcall(SaveGame, c.arg, 'inplace proto')
    note('RES ' .. c.id .. ' save ' .. tostring(c.arg) .. ' ok=' .. tostring(ok) .. ' ' .. tostring(err) .. ' folder=' .. folder())
  elseif op == 'autosave' then
    local before = GetAutosaveOption and GetAutosaveOption()
    local ok, err = pcall(SetAutosaveOption)
    note('RES ' .. c.id .. ' autosave before=' .. tostring(before) .. ' after=' .. tostring(GetAutosaveOption and GetAutosaveOption())
      .. ' ok=' .. tostring(ok) .. ' ' .. tostring(err))
  elseif op == 'uisafe' then
    -- stock option function: sets the in-memory value and saves config.xml, which goes where the folder points now
    local before = GetUISafeModeOption and GetUISafeModeOption()
    local ok, err = pcall(SetUISafeModeOption, false)
    note('RES ' .. c.id .. ' uisafe before=' .. tostring(before) .. ' after=' .. tostring(GetUISafeModeOption and GetUISafeModeOption())
      .. ' ok=' .. tostring(ok) .. ' ' .. tostring(err))
  elseif op == 'gate' then
    if type(package) ~= 'table' then
      note('RES ' .. c.id .. ' gate package=' .. type(package))
      return
    end
    local open, why = package.loadlib(DIR .. c.arg, 'luaopen_x4lan_gate')
    if not open then
      note('RES ' .. c.id .. ' gate loadlib failed ' .. tostring(why))
      return
    end
    local ok, gate = pcall(open)
    local methods = 0
    if ok and type(gate) == 'table' then
      for _ in pairs(gate) do methods = methods + 1 end
    end
    local slots = ok and type(gate) == 'table' and gate.pilot_slots_max and gate.pilot_slots_max()
    note('RES ' .. c.id .. ' gate loadlib ok open=' .. tostring(ok) .. ' type=' .. type(gate) .. ' methods=' .. methods
      .. ' pilot_slots_max=' .. tostring(slots))
  elseif op == 'ware' then
    note('RES ' .. c.id .. ' ware ' .. ware())
  elseif op == 'quit' then
    note('RES ' .. c.id .. ' quit')
    QuitGame()
  else
    note('RES ' .. c.id .. ' noop')
  end
end

SetScript('onUpdate', function()
  frames = frames + 1
  if waiting_list and C.IsSaveListLoadingComplete() then
    note('RES ' .. waiting_list .. ' reloaded savefolder=' .. folder())
    waiting_list = nil
  end
  if frames % 20 ~= 0 then
    return
  end
  local ok, chunk = pcall(loadfile, DIR .. 'cmd.lua')
  if not ok or not chunk then
    return
  end
  local ran, c = pcall(chunk)
  if not ran or type(c) ~= 'table' or c.id == nil or c.id == last then
    return
  end
  last = c.id
  if c.op == 'noop' then
    return
  end
  note('CMD ' .. c.id .. ' ' .. tostring(c.op))
  local done, err = pcall(run, c)
  if not done then
    note('RES ' .. c.id .. ' error ' .. tostring(err))
  end
end)
