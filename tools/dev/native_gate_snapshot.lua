-- Owned save_009 in a disposable lane only; the runner archives/removes that slot.
local cfg=X4LAN_CONFIG
if not cfg or cfg.native_gate_snapshot~='capture' then return end
local ffi=require('ffi');local C=ffi.C
ffi.cdef[[bool IsPlayerValid(void);bool IsGamePaused(void);double GetCurrentGameTime(void);
bool IsSaveListLoadingComplete(void);void ReloadSaveList(void);const char* GetSaveFolderPath(void);
bool IsFullscreenCutsceneActive(void);const char* GetGameStartName(void);]]
local path=ffi.string(C.GetSaveFolderPath()):gsub('\\','/'):gsub('/+$','')
assert(cfg.test_profile=='45841' and path:sub(-#'/45841/save')=='/45841/save','snapshot outside disposable profile')
assert(cfg.enabled==false,'snapshot requires disabled multiplayer gameplay')
-- Optional: other player starts (tools/dev/native_gate_scenarios.py capture-overlay).
local gamestart=cfg.native_gate_snapshot_gamestart
assert(gamestart==nil or (type(gamestart)=='string' and gamestart:match('^[A-Za-z0-9_]+$')),'invalid snapshot gamestart')
-- native_gate_snapshot_fixed=false captures at the normal frame rate (no fixed-tick DLL): the
-- snapshot is a save file every client loads byte for byte, so the capture clock does not matter.
-- Optional: hulls of per-player starts (tools/dev/native_gate_session_start.py). The overlay's generated MD
-- X4LAN_SessionHulls creates them after the fixture check and before the save, so they get saved IDs.
local hulls=cfg.native_gate_snapshot_hulls
assert(hulls==nil or (type(hulls)=='number' and hulls%1==0 and hulls>=1 and hulls<=15),'invalid snapshot hull count')
local fixed=cfg.native_gate_snapshot_fixed~=false and assert(package.loadlib('./extensions/x4_lan_lab/native/x4lan_fixedtick_64.dll','luaopen_x4lan_fixedtick'))() or nil
local owner,spawned,ready,refreshed,saving,done,inspecting,nearby
local hulls_raised,hulls_count,save_wait
local function log(s) DebugError('[X4LAN] NATIVE_SNAPSHOT '..s) end
RegisterEvent('X4LAN_SessionHulls.Ready',function(_,count) hulls_count=tonumber(count) or -1 end)
RegisterEvent('X4LAN_NativeGate.Ready',function(_,count)
 assert(tonumber(count)==12,'snapshot fixture incomplete');ready=C.GetCurrentGameTime()
 log('ACTORS_READY time='..string.format('%.17g',ready))
end)
RegisterEvent('gameSaved',function(_,success)
 if saving and not done then
  assert(success==true or success==1,'native snapshot save failed');done=true
  log('SAVED slot=save_009 time='..string.format('%.17g',C.GetCurrentGameTime()))
 end
end)
RegisterEvent('X4LAN_NativeGate.Attention',function(_,count) nearby=tonumber(count) end)
local function frame()
 if done then return end
 if fixed and not owner then owner=assert(fixed.start('fixed60'));log('FIXED60 armed') end
 if not C.IsPlayerValid() then return end
 -- Story starts may open with a fullscreen intro; spawn and inspect only outside it.
 if C.IsFullscreenCutsceneActive() then return end
 local time=C.GetCurrentGameTime()
 if not spawned and time>=5 then
  local started=ffi.string(C.GetGameStartName())
  assert(gamestart==nil or started==gamestart,'snapshot gamestart mismatch '..started)
  log('GAMESTART id='..started..' time='..string.format('%.17g',time))
  spawned=true;AddUITriggeredEvent('X4LAN_NativeGate','Spawn',{});return
 end
 if not ready or time<ready+2 or saving then return end
 if not inspecting then inspecting=true;AddUITriggeredEvent('X4LAN_NativeGate','InspectAttention',{});return end
 if nearby==nil then return end
 assert(nearby==12,'snapshot requires all 12 actors at nearby attention')
 if hulls then
  assert(hulls_raised or hulls_count==nil,'session hulls answered before the request')
  if not hulls_raised then
   hulls_raised=true;log('HULLS_REQUEST count='..hulls)
   AddUITriggeredEvent('X4LAN_SessionHulls','Create',{count=hulls});return
  end
  if hulls_count==nil then return end
  assert(hulls_count==hulls,'session hulls incomplete: '..tostring(hulls_count)..' of '..hulls)
  if not refreshed then log('HULLS_READY count='..hulls) end
 end
 if not refreshed then C.ReloadSaveList();refreshed=true;return end
 if not C.IsSaveListLoadingComplete() then return end
 for _,entry in pairs(GetSaveList({save_009=true}) or {}) do assert(entry.filename~='save_009','snapshot slot occupied') end
 -- A story start can open with talk during which X4 refuses a save (the Terran cadet's wingmate speaks from second 6):
 -- wait for it to end, at most 180 s of game time.
 if not IsSavingPossible(false) then
  if not save_wait then save_wait=time;log('SAVE_WAIT time='..string.format('%.17g',time)) end
  assert(time<save_wait+180,'native saving unavailable')
  return
 end
 if not C.IsGamePaused() then Pause() end
 saving=true;log('SAVE_REQUEST time='..string.format('%.17g',time))
 SaveGame('save_009','Native lockstep disposable starting fixture')
end
SetScript('onUpdate',function()
 local ok,why=pcall(frame)
 if not ok then done=true;log('ERROR '..tostring(why)) end
end)
