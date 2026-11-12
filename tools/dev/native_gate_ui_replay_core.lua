-- UI replay, Lua half (config native_gate_ui_replay, default off; src/native/engine/lockstep_ui_replay.h,
-- reports/LOCKSTEP-UI-REPLAY-20261030.txt). Stock menus call a few world-changing functions as Lua C globals, the
-- native hooks cannot see those: here they are wrapped by name (the menus look them up at each call), encoded as
-- events of this client's slot and handed to the gate's queue, so they keep their order with the native events.
-- At the apply tick every client's probe frame runs them through the stock function, canonical, with the same
-- arguments. Nothing runs locally before that. Pure Lua apart from the double's bytes; the engine is injected.
X4LAN_UI_REPLAY_CORE={}
local K=X4LAN_UI_REPLAY_CORE
local ffi=require('ffi')
K.FAMILY={travel=1,target=2,orders=4,autopilot=8,dock=16,weapons=32,missiles=64,queue=128,events=256,flight=512,
 trade=1024,missions=2048,build=4096,assets=8192,conversation=16384,faction=32768,inventory=524288}   -- docs/LOCKSTEP-REGISTRY.txt; trade: X4LAN_UI_TRADE below (lockstep_ui_trade.h);
 -- build, assets: native_gate_ui_build_core.lua (lockstep_ui_build.h); missions, conversation: lockstep_ui_missions.h;
 -- faction: X4LAN_UI_FACTION below (lockstep_ui_faction.h); inventory: native_gate_ui_inventory_core.lua
K.ALL=63   -- true: the families of the first runs; missiles (pilot fire, target), queue, events and flight are listed by name
K.OP_ORDERS_CLEAR,K.OP_ORDERS_CLEAR2,K.OP_LUA_CALL=0x41,0x42,0x43
K.OP_QUEUE,K.OP_QUEUE_PARAM,K.OP_QUEUE_LIST=0x44,0x45,0x46   -- the queue editor (lockstep_ui_replay.h)
K.OP_LUA_GLOBAL=0x47   -- the events family (P3, slot 1): a Lua C global by its number in K.GLOBALS, then its arguments
K.GLOBALS={'AddUITriggeredEvent','SignalObject'}   -- the order is the wire's (lockstep_ui_replay.h UIREP_LUA_GLOBALS)
-- AddUITriggeredEvent calls that are UI-only, one per client, by {screen, control}: they run where they were made (no MD cue
-- of the base game or a DLC listens for the screen). Mirrors: tools/dev/ui_guard_inventory.py UI_ONLY_EVENTS,
-- lockstep_ui_guard.h uiguard_ui_only. TopLevelMenu '': Helper's show of the HUD's top level menu (every client at the
-- first game plan change after a load, after an undock, and whenever a menu closes in flight).
K.UI_ONLY={{'TopLevelMenu',''}}
-- Missions and conversations of slot 1 (lockstep_ui_missions.h, reports/LOCKSTEP-UI-MISSIONS-20261101.txt): the guard's hook
-- captures the mission exports natively (op 0x48, applied by gate.ui_missions_run); the conversation globals are wrapped
-- here (op 0x49: the number in this list, then the codec; the native side numbers them the same, uimis_conv_names).
K.OP_MISSION,K.OP_CONVERSATION=0x48,0x49
K.CONVERSATION={'StartConversationFromMenu','StartSubConversationFromMenu','ProceedFromMenu','ReturnFromMenu','CancelConversation',
 'UnsuspendConversation','SelectDialogOption','SelectBackOption'}
K.MISSIONS_NOTICE_TICKS=90   -- the info line of a slot 2+ refusal at most this often
-- Hiring a person who stands on a station (md/conversations.xml DefaultComm 'g_hireworksomewhere' opens the map in mode
-- 'hire'; reports/LOCKSTEP-ON-FOOT-20261101.txt): MapMenu.buttonHire pays the fee with TransferPlayerMoneyTo to the NPC's
-- station (the trade family refuses that: not the slot's container) and answers the conversation with ProceedFromMenu
-- ('g_hire', {target, role, ispost}); MD's assign_hired_actor follows. With the conversation family that click is a hire
-- scope: slot 1's fee is this family's event (op 0x49, number K.HIRE_MONEY = lockstep_ui_missions.h UIMIS_C_HIRE_MONEY) in
-- call order with the ProceedFromMenu, both run on every client at the apply; slots 2+: the whole click is refused (no fee
-- without the hire). The fee is the player faction's money: the shared wallet in coop, slot 1's own (player) in factions.
K.HIRE_MONEY=16
K.HIRE_SCOPES={MapMenu={'buttonHire'}}
-- The UI guard (lockstep_ui_guard.h, native_gate_ui_guard): its groups in the native order (bit 1 << index-1).
K.GUARD_GROUPS={'flight','orders','fleet','trade','faction','assets','build','conflict','missions','events','conversation','session','cheat','editor'}
K.GUARD_NOTICE_TICKS=90   -- the info line of a refusal at most this often
K.EVENT_MAX=200
K.REF_HEX=24
-- cue_luaid, cue_id: an MD cue (a mission or offer ID), not a component (lockstep_ui_missions.h: ID and module check).
local TAG={['nil']=0,['false']=1,['true']=2,number=3,string=4,luaid=5,id=6,table=7,cue_luaid=8,cue_id=9}
local DEPTH_MAX,ITEMS_MAX,STRING_MAX=4,32,64

-- true = every family; a list names them. Returns the mask, or nil and the reason.
function K.families(value)
 if value==true then return K.ALL end
 if type(value)~='table' or #value==0 then return nil,'native_gate_ui_replay must be true or a list of travel, target, orders, autopilot, dock, weapons, missiles, queue, events, flight, trade, missions, build, assets, conversation, faction, inventory' end
 local mask=0
 for _,name in ipairs(value) do
  local bit=K.FAMILY[name]
  if not bit then return nil,'unknown family '..tostring(name) end
  if math.floor(mask/bit)%2==1 then return nil,'family listed twice '..name end
  mask=mask+bit
 end
 return mask
end

-- What a row shows of an events call: AddUITriggeredEvent's screen/control, SignalObject's signal; letters, digits and _ . -
-- as they are, anything else '?', 20 characters each (the native guard's rows use the same text).
local function arg_text(v)
 if v==nil then return '' end
 return (tostring(v):sub(1,20):gsub('[^%w_%.%-]','?'))
end
function K.args_text(name,...)
 local a,b=...
 if name=='AddUITriggeredEvent' then return arg_text(a)..'/'..arg_text(b) end
 if name=='SignalObject' then return arg_text(b) end
 return '-'
end
-- A UI-only AddUITriggeredEvent (K.UI_ONLY): exact screen and control.
function K.ui_only(name,screen,control)
 if name~='AddUITriggeredEvent' then return false end
 for _,e in ipairs(K.UI_ONLY) do if screen==e[1] and control==e[2] then return true end end
 return false
end

-- native_gate_ui_guard: 'all' (or true) or a list of groups. Returns the mask, or nil and the reason.
function K.guard_groups(value)
 if value=='all' or value==true then return 2^#K.GUARD_GROUPS-1 end
 local why='native_gate_ui_guard must be all or a list of '..table.concat(K.GUARD_GROUPS,', ')
 if type(value)~='table' or #value==0 then return nil,why end
 local mask,seen=0,{}
 for _,name in ipairs(value) do
  local bit
  for i,g in ipairs(K.GUARD_GROUPS) do if g==name then bit=2^(i-1) end end
  if not bit or seen[name] then return nil,why end
  seen[name]=true;mask=mask+bit
 end
 return mask
end

local function byte_hex(b) return string.format('%02x',b) end
local function double_hex(v)
 local d=ffi.new('double[1]',v);local p=ffi.cast('const uint8_t*',d);local out={}
 for i=0,7 do out[#out+1]=byte_hex(p[i]) end
 return table.concat(out)
end
local function hex_double(h)
 local d=ffi.new('double[1]');local p=ffi.cast('uint8_t*',d)
 for i=0,7 do p[i]=tonumber(h:sub(2*i+1,2*i+2),16) end
 return tonumber(d[0])
end
-- Canonical order: numbers ascending, then strings ascending, so every client gets one encoding of a table.
local function sorted_keys(t)
 local numbers,strings={},{}
 for k in pairs(t) do
  if type(k)=='number' then numbers[#numbers+1]=k
  elseif type(k)=='string' then strings[#strings+1]=k
  else return nil end
 end
 table.sort(numbers);table.sort(strings)
 for _,k in ipairs(strings) do numbers[#numbers+1]=k end
 return numbers
end
local encode_value
function encode_value(v,ctx,depth)
 local kind=type(v)
 if kind=='nil' then return byte_hex(TAG['nil']) end
 if kind=='boolean' then return byte_hex(v and TAG['true'] or TAG['false']) end
 if kind=='number' then
  if v~=v or v==math.huge or v==-math.huge then return nil,'number' end
  return byte_hex(TAG.number)..double_hex(v)
 end
 if kind=='string' then
  if #v>STRING_MAX then return nil,'string' end
  local out={byte_hex(TAG.string),byte_hex(#v)}
  for i=1,#v do out[#out+1]=byte_hex(v:byte(i)) end
  return table.concat(out)
 end
 if kind=='userdata' or kind=='cdata' then
  local ref=ctx.ref(v)
  if ref then return byte_hex(kind=='userdata' and TAG.luaid or TAG.id)..ref end
  local cue=ctx.cue_ref and ctx.cue_ref(v)
  if cue then return byte_hex(kind=='userdata' and TAG.cue_luaid or TAG.cue_id)..cue end
  return nil,'unmapped'
 end
 if kind=='table' then
  if depth>=DEPTH_MAX then return nil,'depth' end
  local keys=sorted_keys(v)
  if not keys or #keys>ITEMS_MAX then return nil,'table' end
  local out={byte_hex(TAG.table),byte_hex(#keys)}
  for _,k in ipairs(keys) do
   local a,why=encode_value(k,ctx,depth+1)
   if not a then return nil,why end
   local b,why2=encode_value(v[k],ctx,depth+1)
   if not b then return nil,why2 end
   out[#out+1]=a;out[#out+1]=b
  end
  return table.concat(out)
 end
 return nil,'type'
end
-- (args, n) -> event hex (count, values), or nil and the reason.
function K.encode_call(args,n,ctx)
 if n>ITEMS_MAX then return nil,'count' end
 local out={byte_hex(n)}
 for i=1,n do
  local h,why=encode_value(args[i],ctx,0)
  if not h then return nil,why end
  out[#out+1]=h
 end
 local hex=table.concat(out)
 if #hex/2>K.EVENT_MAX then return nil,'too_long' end
 return hex
end
local decode_value
function decode_value(h,at,ctx,depth)
 if at+1>#h then return nil,nil,'truncated' end
 local tag=tonumber(h:sub(at,at+1),16)
 at=at+2
 if tag==TAG['nil'] then return nil,at end
 if tag==TAG['false'] then return false,at end
 if tag==TAG['true'] then return true,at end
 if tag==TAG.number then
  if at+15>#h then return nil,nil,'truncated' end
  return hex_double(h:sub(at,at+15)),at+16
 end
 if tag==TAG.string then
  if at+1>#h then return nil,nil,'truncated' end
  local n=tonumber(h:sub(at,at+1),16);at=at+2
  if at+2*n-1>#h then return nil,nil,'truncated' end
  local out={}
  for i=0,n-1 do out[#out+1]=string.char(tonumber(h:sub(at+2*i,at+2*i+1),16)) end
  return table.concat(out),at+2*n
 end
 if tag==TAG.luaid or tag==TAG.id then
  if at+K.REF_HEX-1>#h then return nil,nil,'truncated' end
  local value,why=ctx.resolve(h:sub(at,at+K.REF_HEX-1),tag==TAG.luaid and 'luaid' or 'id')
  if value==nil then return nil,nil,why or 'unresolved' end
  return value,at+K.REF_HEX
 end
 if tag==TAG.cue_luaid or tag==TAG.cue_id then
  if not ctx.resolve_cue or at+K.REF_HEX-1>#h then return nil,nil,'truncated' end
  local value,why=ctx.resolve_cue(h:sub(at,at+K.REF_HEX-1),tag==TAG.cue_luaid and 'luaid' or 'id')
  if value==nil then return nil,nil,why or 'unresolved' end
  return value,at+K.REF_HEX
 end
 if tag==TAG.table then
  if depth>=DEPTH_MAX or at+1>#h then return nil,nil,'truncated' end
  local n=tonumber(h:sub(at,at+1),16);at=at+2
  local t={}
  for _=1,n do
   local key,value,why
   key,at,why=decode_value(h,at,ctx,depth+1)
   if not at then return nil,nil,why end
   if key==nil then return nil,nil,'key' end
   value,at,why=decode_value(h,at,ctx,depth+1)
   if not at then return nil,nil,why end
   t[key]=value
  end
  return t,at
 end
 return nil,nil,'tag'
end
-- Event hex -> args, n; or nil and the reason.
function K.decode_call(hex,ctx)
 if type(hex)~='string' or #hex<2 or #hex%2~=0 or hex:find('[^0-9a-f]') then return nil,'encoding' end
 local n=tonumber(hex:sub(1,2),16)
 if n>ITEMS_MAX then return nil,'count' end
 local args,at={},3
 for i=1,n do
  local value,why
  value,at,why=decode_value(hex,at,ctx,0)
  if not at then return nil,why end
  args[i]=value
 end
 if at~=#hex+1 then return nil,'trailing' end
 return args,n
end

-- The entries that stay unblocked per family (native_gate_pilot_block_orders refuses every other one).
K.UNBLOCKED={
 travel={'InteractMenu.buttonTravelMode','hotkey.INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE'},
 orders={'InteractMenu.orderAttack','InteractMenu.orderMoveWait','InteractMenu.orderDock','InteractMenu.orderFollow',
  'MapMenu.orderMoveWait','MapMenu.orderAttack','MapMenu.orderFollow'},
 autopilot={'hotkey.INPUT_ACTION_ADDON_DETAILMONITOR_A_SHIFT'},
 -- The map's order rows and the interact menu's queue entries whose calls are all captured (not the default order's
 -- confirm: ResetOrderLoop is not; not ProceedWithOrders).
 queue={'MapMenu.buttonNewOrder','MapMenu.buttonOrderUp','MapMenu.buttonOrderDown','MapMenu.removeOrder','MapMenu.buttonSetOrderParam',
  'MapMenu.setOrderParamFromMode','MapMenu.slidercellSetOrderParam','MapMenu.buttonStartOrders','MapMenu.buttonDockToTrade',
  'InteractMenu.buttonRemoveOrder','InteractMenu.setOrderImmediate'},
}
function K.unblocked(mask)
 local out={}
 for name,bit in pairs(K.FAMILY) do
  if math.floor(mask/bit)%2==1 then for _,entry in ipairs(K.UNBLOCKED[name] or {}) do out[entry]=true end end
 end
 return out
end

-- p: gate, C (ffi.C), env (the Lua globals), log, mask, slot_ships() -> {decimal=slot}, script (by absolute tick).
function K.session(p)
 local gate,C,env,log,mask=p.gate,p.C,p.env,p.log,p.mask
 local S={replaying=false,captured=0,refused=0,applied=0,ui_only=0,unblocked=K.unblocked(mask),ops={},extras={}}   -- ops, extras: other cores' parts
 local orders=math.floor(mask/K.FAMILY.orders)%2==1
 local queue=math.floor(mask/K.FAMILY.queue)%2==1
 local events=math.floor(mask/K.FAMILY.events)%2==1
 local missions=math.floor(mask/K.FAMILY.missions)%2==1
 local conversation=math.floor(mask/K.FAMILY.conversation)%2==1
 local function decimal(id)
  local kind=type(id)
  if kind=='number' then return string.format('%.0f',id) end
  if kind~='cdata' then id=env.ConvertIDTo64Bit(id) end
  local text=(tostring(id):gsub('ULL$',''))
  assert(text:match('^%d+$'),'invalid runtime component ID')
  return text
 end
 S.decimal=decimal
 local ctx={
  ref=function(id) return gate.ui_replay_ref(decimal(id)) end,
  resolve=function(ref,kind)
   local runtime,why=gate.ui_replay_resolve(ref)
   if not runtime then return nil,why end
   return kind=='luaid' and env.ConvertStringToLuaID(runtime) or env.ConvertStringTo64Bit(runtime)
  end}
 -- Mission and offer IDs are MD cues: with the missions or conversation family they travel as cue references (the
 -- events family's SignalObject 'accept' carries the offer's ID). Without them such a value stays unmapped (refused).
 if (missions or conversation) and gate.ui_missions_cue_ref then
  ctx.cue_ref=function(id)
   local ok,text=pcall(decimal,id)
   return ok and gate.ui_missions_cue_ref(text) or nil
  end
  ctx.resolve_cue=function(ref,kind)
   local id,why=gate.ui_missions_cue_resolve(ref)
   if not id then return nil,why end
   return kind=='luaid' and env.ConvertStringToLuaID(id) or env.ConvertStringTo64Bit(id)
  end
 end
 S.ctx=ctx
 local function refuse(what,why,args)
  S.refused=S.refused+1
  log('UI_REPLAY_REFUSED what='..what..' why='..tostring(why)..(args and ' args='..args or ''))
 end
 -- Stock functions, once per Lua state (a re-arm finds the wrappers in the globals).
 local stock=env.X4LAN_UI_REPLAY_STOCK
 if not stock then
  stock={CreateOrder=env.CreateOrder,SetOrderParam=env.SetOrderParam,RemoveOrderListParam=env.RemoveOrderListParam}
  for _,name in ipairs(K.GLOBALS) do stock[name]=env[name] end
  for _,name in ipairs(K.CONVERSATION) do stock[name]=env[name] end
  env.X4LAN_UI_REPLAY_STOCK=stock
 end
 S.stock=stock
 -- Missions per player: this client's mission menus list its own slot's missions (lockstep_mission_owners.h 'lists'). The
 -- stock menus walk GetNumMissions() and GetMissionDetails(i): both answer the own slot's and nobody's (a client's own
 -- guidance, a mission no accept made), by index among those. Presentation only: nothing here reaches the simulation.
 if p.mpp and env.GetNumMissions and env.GetMissionDetails and gate.mpp_owner then
  if not stock.GetNumMissions then stock.GetNumMissions,stock.GetMissionDetails=env.GetNumMissions,env.GetMissionDetails end
  local map={}
  local function own(id)
   local ok,text=pcall(decimal,id)
   if not ok then return true end
   local owner=gate.mpp_owner(text)
   return owner==0 or owner==p.own_slot
  end
  local function refresh()
   map={}
   for i=1,stock.GetNumMissions() do if own((stock.GetMissionDetails(i))) then map[#map+1]=i end end
   return #map
  end
  env.GetNumMissions=function() return refresh() end
  env.GetMissionDetails=function(i)
   if not map[i] then refresh() end
   if map[i] then return stock.GetMissionDetails(map[i]) end
  end
  S.own_missions=refresh
 end
 local function is_slot_ship(id)
  local ok,text=pcall(decimal,id)
  return ok and p.slot_ships()[text]~=nil
 end
 if orders then
  env.CreateOrder=function(...)
   if S.replaying then return stock.CreateOrder(...) end
   local args,n={...},select('#',...)
   if n<1 then return refuse('CreateOrder','arguments') or 0 end
   if is_slot_ship(args[1]) then return refuse('CreateOrder','slot_ship') or 0 end
   local hex,why=K.encode_call(args,n,ctx)
   if not hex then return refuse('CreateOrder',why) or 0 end
   if gate.ui_replay_push(K.OP_LUA_CALL,hex)==1 then S.captured=S.captured+1 end   -- a native refusal counts and logs itself
   return 0
  end
 end
 -- Order parameters of queued orders (the queue editor): captured with the queue family, with the count of this slot's
 -- creates applied so far (the index they mean is mapped at apply), refused without it.
 if orders or queue then
  local function param(name,op)
   return function(...)
    if S.replaying then return stock[name](...) end
    if not queue then return refuse(name,'not_replayed_yet') end
    local n=select('#',...)
    if n<3 then return refuse(name,'arguments') end
    if is_slot_ship((...)) then return refuse(name,'slot_ship') end
    local hex,why=K.encode_call({gate.ui_replay_queue_seen(),...},n+1,ctx)
    if not hex then return refuse(name,why) end
    if gate.ui_replay_push(op,hex)==1 then S.captured=S.captured+1 end
   end
  end
  env.SetOrderParam=param('SetOrderParam',K.OP_QUEUE_PARAM)
  env.RemoveOrderListParam=param('RemoveOrderListParam',K.OP_QUEUE_LIST)
 end
 -- The events family (P3): AddUITriggeredEvent and SignalObject reach MD, whose player.entity is slot 1's player. Slot 1's
 -- client captures its menus' calls and every client runs them at the apply tick (canonical: slot 1's, the others
 -- dropped); slots 2+ are refused until their MD scope exists. The probe's own calls, the simulation's (no local phase)
 -- and the apply run the stock function at once, and so does a UI-only event (K.UI_ONLY) on the client that made it.
 -- Every row names the call's screen/control or signal (K.args_text).
 if events then
  for id,name in ipairs(K.GLOBALS) do
   if stock[name] then
    env[name]=function(...)
     if S.replaying or gate.ui_replay_local()~=1 then return stock[name](...) end
     local args=K.args_text(name,...)
     if K.ui_only(name,...) then
      S.ui_only=S.ui_only+1
      log('UI_REPLAY_LOCAL what='..name..' args='..args..' why=ui_only')
      return stock[name](...)
     end
     if p.own_slot~=1 and not p.mpp then return refuse(name,'slot',args) end   -- missions per player: every slot's own
     local hex,why=K.encode_call({...},select('#',...),ctx)
     if not hex then return refuse(name,why,args) end
     if gate.ui_replay_push(K.OP_LUA_GLOBAL,byte_hex(id)..hex)==1 then S.captured=S.captured+1
     else log('UI_REPLAY_REFUSED what='..name..' why=native args='..args) end   -- the native row (UI_REPLAY_REFUSED_NATIVE) says why
    end
   end
  end
 end
 -- The conversation family (slot 1): the menus' conversation calls are captured like the events family's and run on
 -- every client at the apply tick; the four without component arguments reach the guard's hook as well when a script
 -- holds the C function itself (captured there, the same event). Slots 2+: refused, the person is told at most every
 -- K.MISSIONS_NOTICE_TICKS ticks (MD acts on slot 1's player; per player conversations are the report's model (c)).
 local notice_slot
 local function slot_refuse(name,args)
  refuse(name,'slot',args)
  local tick=S.tick or 0
  if p.notify and (not notice_slot or tick-notice_slot>=K.MISSIONS_NOTICE_TICKS) then
   notice_slot=tick
   pcall(p.notify,'Multiplayer: '..name..' works for player 1 only for now - nothing happened.')
  end
 end
 S.slot_refuse=slot_refuse
 if conversation then
  for id,name in ipairs(K.CONVERSATION) do
   assert(stock[name],'ui replay refused: conversation family without the Lua global '..name)
   env[name]=function(...)
    if S.replaying or gate.ui_replay_local()~=1 then return stock[name](...) end
    if p.own_slot~=1 then return slot_refuse(name) end
    local hex,why=K.encode_call({...},select('#',...),ctx)
    if not hex then return refuse(name,why) end
    if gate.ui_missions_push(K.OP_CONVERSATION,byte_hex(id)..hex)==1 then
     S.captured=S.captured+1
     log('UI_MISSIONS_CAPTURED tick='..tostring(S.tick)..' what='..name..' via=lua')
    else log('UI_REPLAY_REFUSED what='..name..' why=native') end
   end
  end
  -- The hire scope (K.HIRE_SCOPES): the fee joins the conversation's events; slots 2+ refused as a whole.
  if stock.TransferPlayerMoneyTo==nil then stock.TransferPlayerMoneyTo=env.TransferPlayerMoneyTo end
  S.hires=0
  function S.hire_scope(name,fn,...)
   if S.replaying or S.hire or gate.ui_replay_local()~=1 then return fn(...) end
   if p.own_slot~=1 then return slot_refuse(name) end
   S.hire=name;S.hires=S.hires+1
   local saved=env.TransferPlayerMoneyTo
   env.TransferPlayerMoneyTo=function(...)
    if S.replaying or gate.ui_replay_local()~=1 then return saved(...) end
    local hex,why=K.encode_call({...},select('#',...),ctx)
    if not hex then return refuse('TransferPlayerMoneyTo',why) end
    if gate.ui_missions_push(K.OP_CONVERSATION,byte_hex(K.HIRE_MONEY)..hex)==1 then
     S.captured=S.captured+1
     log('UI_MISSIONS_CAPTURED tick='..tostring(S.tick)..' what=TransferPlayerMoneyTo via=hire')
    else log('UI_REPLAY_REFUSED what=TransferPlayerMoneyTo why=native') end
   end
   local results={n=0}
   local function keep(ok,...) results={n=select('#',...)+1,ok,...} end
   keep(pcall(fn,...))
   env.TransferPlayerMoneyTo=saved;S.hire=nil
   if not results[1] then error(results[2],0) end
   return unpack(results,2,results.n)
  end
  -- The stock menus' hire buttons, wrapped once per menu table (the handlers look them up when they run).
  function S.wrap_hire_menus()
   for _,menu in ipairs(p.menus and p.menus() or {}) do
    local names=type(menu)=='table' and K.HIRE_SCOPES[menu.name]
    for _,fname in ipairs(names or {}) do
     local orig=menu[fname]
     if type(orig)=='function' and not menu['x4lan_hire_'..fname] then
      menu['x4lan_hire_'..fname]=orig
      local scope=menu.name..'.'..fname
      menu[fname]=function(...) return S.hire_scope(scope,orig,...) end
      log('UI_MISSIONS_HIRE_SCOPE menu='..menu.name..' function='..fname)
     end
    end
   end
  end
 end
 -- The trade family (X4LAN_UI_TRADE below): its wrappers go on top of the ones above (SignalObject: the docked trade's only).
 if math.floor(mask/K.FAMILY.trade)%2==1 then S.trade=X4LAN_UI_TRADE.attach(S,p,refuse) end
 -- The faction family (X4LAN_UI_FACTION below): captured natively in the guard's hook; its op and receipt join S.ops/S.extras.
 if math.floor(mask/K.FAMILY.faction)%2==1 then S.faction=X4LAN_UI_FACTION.attach(S,p) end
 -- One due event; ok, what happened. A reference that is gone is the same on every client, a refusal too.
 local function run(slot,op,hex,tick)
  if S.ops[op] then return S.ops[op](slot,hex,tick) end   -- another core's op (native_gate_ui_build_core.lua: 0x50)
  if S.trade and (op==X4LAN_UI_TRADE.OP_LUA or op==X4LAN_UI_TRADE.OP_FFI) then return S.trade.run(slot,op,hex) end
  if op==K.OP_LUA_CALL then
   local args,n=K.decode_call(hex,ctx)
   if not args then
    if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
    return false,n
   end
   if is_slot_ship(args[1]) then return false,'slot_ship' end
   local ok,result=pcall(stock.CreateOrder,unpack(args,1,n))
   return ok,ok and 'order='..tostring(args[2]) or tostring(result)
  end
  if op==K.OP_ORDERS_CLEAR or op==K.OP_ORDERS_CLEAR2 then
   local runtime,why=gate.ui_replay_resolve(hex:sub(1,K.REF_HEX))
   if not runtime then return false,why end
   if p.slot_ships()[runtime] then return false,'slot_ship' end
   local id=env.ConvertStringTo64Bit(runtime)
   if op==K.OP_ORDERS_CLEAR then return pcall(C.RemoveAllOrders,id),'clear' end
   local flags=tonumber(hex:sub(K.REF_HEX+1,K.REF_HEX+2),16)
   return pcall(C.RemoveAllOrders2,id,flags%2==1,flags>=2),'clear2'
  end
  -- The queue editor's FFI calls run natively (index mapping, X4's answer); its Lua parameters here, mapped the same way.
  if op==K.OP_QUEUE then
   local ok,what=gate.ui_replay_queue_run(slot,hex)
   return ok==1,what
  end
  if op==K.OP_LUA_GLOBAL then
   local name=K.GLOBALS[tonumber(hex:sub(1,2),16) or 0]
   if (slot~=1 and not p.mpp) or not name or not stock[name] then gate.ui_replay_fail('ui_lua_op');return false,'op' end
   local args,n=K.decode_call(hex:sub(3),ctx)
   if not args then
    if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
    return false,n
   end
   -- Missions per player (lockstep_mission_owners.h): slot k's call runs in slot k's scope on every client, and its accept
   -- (SignalObject(actor, 'accept', offer), the stock menus' form) claims the offer's cue tree for slot k first.
   local claimed=''
   if p.mpp and name=='SignalObject' and args[2]=='accept' and args[3]~=nil then   -- slot 1's too: its menus' lists
    local okd,text=pcall(decimal,args[3])
    claimed=' claim='..tostring(okd and gate.mpp_claim(slot,text)==1)
   end
   if slot~=1 and gate.mpp_scope(slot)~=1 then gate.ui_replay_fail('mpp_scope');return false,'mpp_scope' end
   local ok,result=pcall(stock[name],unpack(args,1,n))
   if slot~=1 and gate.mpp_unscope(slot)~=1 then gate.ui_replay_fail('mpp_leave') end
   return ok,ok and name..'('..tostring(args[1])..','..tostring(args[2])..')'..claimed or tostring(result)
  end
  -- Missions (op 0x48): the export, natively, with its cue resolved there first (gone: nothing, the same everywhere).
  if op==K.OP_MISSION then
   if slot~=1 and not p.mpp then gate.ui_replay_fail('ui_lua_op');return false,'op' end   -- slot k's: in its scope, natively
   local ok,what=gate.ui_missions_run(slot,hex)
   return ok==1,what
  end
  -- Conversations (op 0x49): the stock global with the captured arguments; a conversation start through MD (below).
  if op==K.OP_CONVERSATION then
   local number=tonumber(hex:sub(1,2),16) or 0
   local name=K.CONVERSATION[number] or (number==K.HIRE_MONEY and 'TransferPlayerMoneyTo' or nil)   -- the hire scope's fee
   if slot~=1 or not name or not stock[name] then gate.ui_replay_fail('ui_lua_op');return false,'op' end
   local args,n=K.decode_call(hex:sub(3),ctx)
   if not args then
    if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
    return false,n
   end
   -- X4 starts a menu conversation only as the result of a closing menu (Helper.closeMenuForNewConversation): a bare call
   -- from the replay's frame was ignored (m1d, m1e). So every client raises X4LAN_UiConversation/Start here and our MD
   -- (native_gate_ui_replay.xml ConversationStart) starts the same conversation at the same tick: start_conversation with the
   -- captured actor, name and parameter (the names MD knows; another is refused there, the same on every client).
   if name=='StartConversationFromMenu' and stock.AddUITriggeredEvent then
    -- MD takes a component from a Lua ID only: a 64-bit actor (the captured call's own form) is converted here (m1f).
    local actor,form=args[2],type(args[2])
    if form=='cdata' or form=='number' then
     local okd,text=pcall(decimal,actor)
     actor=okd and env.ConvertStringToLuaID(text) or actor
    end
    local okt,text=pcall(decimal,actor)
    local ok,result=pcall(stock.AddUITriggeredEvent,'X4LAN_UiConversation','Start',{conversation=args[1],actor=actor,convparam=args[3]})
    return ok,ok and 'md:'..name..'('..tostring(args[1])..','..form..':'..(okt and text or '?')..')' or tostring(result)
   end
   local ok,result=pcall(stock[name],unpack(args,1,n))
   return ok,ok and name..'('..tostring(args[1])..')' or tostring(result)
  end
  if op==K.OP_QUEUE_PARAM or op==K.OP_QUEUE_LIST then
   local args,n=K.decode_call(hex,ctx)
   if not args then
    if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
    return false,n
   end
   if type(args[1])~='number' or n<4 then gate.ui_replay_fail('ui_lua_decode');return false,'decode' end
   local call={}
   for i=2,n do call[i-1]=args[i] end
   if is_slot_ship(call[1]) then return false,'slot_ship' end
   call[2]=gate.ui_replay_queue_index(slot,decimal(call[1]),call[2],args[1])
   local name=op==K.OP_QUEUE_PARAM and 'SetOrderParam' or 'RemoveOrderListParam'
   local ok,result=pcall(stock[name],unpack(call,1,n-1))
   return ok,ok and name..'@'..tostring(call[2]) or tostring(result)
  end
  gate.ui_replay_fail('ui_lua_op')
  return false,'op'
 end
 -- Generic mission offers per slot (lockstep_mission_owners.h 'offers'; MD native_gate_ui_replay.xml SlotOffers): every
 -- client, at the same ticks (stateless: a joiner computes the same ticks), signals the galaxy inside slot k's scope with the
 -- generation open, so X4's own selection runs for slot k's sector and player and what it makes is slot k's.
 -- p.offers={first=tick,every=ticks,stagger=ticks,galaxy=function -> Lua ID}.
 function S.slot_offers(tick)
  local o=p.offers
  if not (p.mpp and o and gate.mpp_generate) or tick<o.first then return end
  for k=2,p.slots do
   if (tick-o.first-(k-2)*o.stagger)%o.every==0 and tick>=o.first+(k-2)*o.stagger then
    local done,before='no_scope',gate.ui_missions_state and gate.ui_missions_state() or '-'
    if gate.mpp_scope(k)==1 then
     S.replaying=true;gate.ui_replay_bypass(1)
     local ok,why=pcall(function()
      assert(gate.mpp_generate(k)==1,'generate refused')
      stock.SignalObject(o.galaxy(),'x4lan_slot_offers',k)
     end)
     gate.mpp_generate(0)
     gate.ui_replay_bypass(0);S.replaying=false
     if gate.mpp_unscope(k)~=1 then gate.ui_replay_fail('mpp_leave') end
     done=ok and 'signalled' or 'error:'..tostring(why)
    end
    log('MPP_OFFERS tick='..tick..' slot='..k..' '..done..' before=['..tostring(before)..'] after=['..
     tostring(gate.ui_missions_state and gate.ui_missions_state() or '-')..'] '..tostring(gate.mpp_status()))
   end
  end
 end
 -- A start's story for slot k (native_gate_story = {[k]='<Script>.<Cue>'}; MD x4lan_story.xml Begin, generated per
 -- session): every client, the same tick, names the start script's static cue to the gate and signals the galaxy inside
 -- slot k's scope with the generation open; the MD signals that cue at once, the gate claims it for slot k at its first
 -- frame (gate.mpp_story) and everything below it and every root instance it makes is slot k's.
 function S.slot_story(tick,k,why)
  local galaxy=p.galaxy or (p.offers and p.offers.galaxy)
  local path=type(p.story)=='table' and p.story[k] or nil
  local done,before='no_scope',gate.ui_missions_state and gate.ui_missions_state() or '-'
  if type(path)~='string' then done='no_story'
  elseif not (p.mpp and galaxy and gate.mpp_generate) then done='no_missions_per_player'
  elseif not gate.mpp_story then done='gate_without_story'
  elseif gate.mpp_scope(k)==1 then
   S.replaying=true;gate.ui_replay_bypass(1)
   local ok,why_not=pcall(function()
    assert(gate.mpp_generate(k)==1,'generate refused')
    assert(gate.mpp_story(k,path)==1,'story refused')
    stock.SignalObject(galaxy(),'x4lan_story',k)
   end)
   local claimed=gate.mpp_story(0)==1
   gate.mpp_generate(0)
   gate.ui_replay_bypass(0);S.replaying=false
   if gate.mpp_unscope(k)~=1 then gate.ui_replay_fail('mpp_leave') end
   done=not ok and 'error:'..tostring(why_not) or claimed and 'signalled' or 'not_claimed'
  end
  log('MPP_STORY tick='..tick..' slot='..k..' why='..tostring(why)..' '..done..' before=['..tostring(before)..'] after=['..
   tostring(gate.ui_missions_state and gate.ui_missions_state() or '-')..'] '..tostring(gate.mpp_status and gate.mpp_status() or '-'))
  return done
 end
 -- Every client, the probe's frame of the tick, the events in slot order and call order.
 function S.apply(tick)
  S.slot_offers(tick)
  local list=gate.ui_replay_lua(tick)
  if not list then return end
  S.replaying=true;gate.ui_replay_bypass(1)
  for slot,op,hex in list:gmatch('(%d+):(%d+):(%x*)') do
   local ok,what=run(tonumber(slot),tonumber(op),hex,tick)
   gate.ui_replay_result(tick,ok and 1 or 0)
   S.applied=S.applied+1
   log(string.format('UI_REPLAY_APPLIED tick=%d slot=%s op=%s result=%s what=%s',tick,slot,op,ok and 'applied' or 'refused',tostring(what)))
  end
  gate.ui_replay_bypass(0);S.replaying=false
 end
 -- The input section (every client stages every slot's; the proposal is this client's own).
 function S.section()
  return {
   apply=function(_,inputs)
    local staged=0
    for slot=1,#inputs do
     local payload=inputs[slot]
     if payload and payload~='' then
      local ok,why=gate.ui_replay_stage(slot,payload)
      assert(ok==1,'ui replay section refused slot='..slot..' why='..tostring(why))
      staged=staged+1
     end
    end
    return 'ui='..staged
   end,
   proposal=function(tick,budget) return gate.ui_replay_take(tick,math.floor((budget or 472)/2)) end}
 end
 -- Scripted use of the stock entries, for runs without people: they go through the same capture.
 local script=p.script or {}
 local function run_script(tick)
  for _,e in ipairs(script[tick] or {}) do
   -- As a person's click: the UI half runs on this client alone, so what it allocates is not the probe's. A canonical
   -- op (K.CANONICAL: a test fixture in every client's plan at the same tick) stays the probe's own call.
   if p.local_mark and not e.canonical then p.local_mark(0) end
   -- A scoped op runs where a person's click runs: in the presentation scope, X4's player as this client's slot
   -- (p.present(true) answers whether it opened it; the probe's frame itself is canonical).
   local scoped=e.scoped and p.present and p.present(true) or false
   local ok,result=pcall(e.call,S)
   if scoped then p.present(false) end
   if p.local_mark and not e.canonical then p.local_mark(1) end
   log(string.format('UI_REPLAY_SCRIPT tick=%d op=%s ok=%s result=%s%s',tick,tostring(e.op),tostring(ok),tostring(result),
     e.scoped and (scoped and ' scope=1' or ' scope=0') or ''))
  end
 end
 -- Test protection (native_gate_ui_replay_min_hull): every slot ship keeps at least this hull percentage, set by MD
 -- on every client at the same tick (the fleet's tick), so the missile run's hits do not end the player's ship.
 local protected=false
 local function protect(tick)
  if protected or not p.min_hull or tick<(p.fleet_tick or 0) then return end
  protected=true
  for slot=1,p.slots or 0 do
   env.AddUITriggeredEvent('X4LAN_UiReplay','MinHull',{ship=p.slot_lua(slot),hull=p.min_hull})
  end
  log('UI_REPLAY_MIN_HULL tick='..tick..' slots='..tostring(p.slots or 0)..' hull='..p.min_hull)
 end
 -- The test fleet: `player` ships, as the stock UI needs them, from the same tick on every client; their queues
 -- join the receipt, so an order that reached one client only shows at once.
 local fleet=p.fleet or {}
 local fleet_owned=false
 local function fleet_rows(tick)
  if #fleet==0 then return '' end
  if not fleet_owned and tick>=(p.fleet_tick or 0) then
   fleet_owned=true
   for _,saved in ipairs(fleet) do
    env.AddUITriggeredEvent('X4LAN_UiReplay','Own',{ship=p.saved_lua(saved)})
   end
  end
  local rows={}
  for _,saved in ipairs(fleet) do rows[#rows+1]=saved..':'..tostring(p.queue_of(saved)) end
  local text=table.concat(rows,',')
  if tick%30==0 then log('UI_REPLAY_FLEET tick='..tick..' '..text) end
  return '|UIFLEET_1:'..text
 end
 -- The UI guard (p.guard: armed): what it refused since the last frame, one row each, and an info line for the person at
 -- most every K.GUARD_NOTICE_TICKS ticks (p.notify: a stock UI call, nothing of the world). The native rows are
 -- tick:name:group:verdict:args (verdict refused, or ui_only for a UI-only AddUITriggeredEvent it let run; args '-' or
 -- the events call's screen/control or signal).
 local notice_at
 local function guard_frame(tick)
  if not p.guard then return end
  local rows=gate.ui_guard_refusals()
  if rows then
   local last
   for at,name,group,verdict,args in rows:gmatch('(%d+):([%w_]+):(%a+):([%a_]+):([^,]*)') do
    local named=args~='-' and ' args='..args or ''
    if verdict=='ui_only' then log('UI_GUARD_PASSED tick='..at..' op='..name..' group='..group..named..' why=ui_only')
    else
     log('UI_GUARD_REFUSED tick='..at..' op='..name..' group='..group..named)
     if not K.GUARD_QUIET[name] then last=name end
    end
   end
   if last and p.notify and (not notice_at or tick-notice_at>=K.GUARD_NOTICE_TICKS) then
    notice_at=tick
    pcall(p.notify,'Multiplayer: '..last..' is not shared between the players yet - nothing happened.')
   end
  end
  if tick%120==0 then log('UI_GUARD_STATUS tick='..tick..' '..tostring(gate.ui_guard_status())) end
 end
 -- Missions and conversations: what the guard's hook captured (one row each), the managers as they change, the status,
 -- and the receipt's part (offers; the conversation manager's state): what took on one client only differs at once.
 local function missions_frame(tick)
  if not (missions or conversation) or not gate.ui_missions_frame then return '' end
  local rows=gate.ui_missions_rows()
  if rows then
   for at,op,name,id in rows:gmatch('(%d+):(%d+):([%w_]+):(%d+)') do
    log('UI_MISSIONS_CAPTURED tick='..at..' what='..name..' id='..id..' op='..op..' via=native')
   end
  end
  local state=gate.ui_missions_state()
  if state then
   if p.active_mission then local ok,active=pcall(p.active_mission);state=state..' active='..(ok and tostring(active) or '?') end
   if state~=S.missions_state then S.missions_state=state;log('UI_MISSIONS_STATE tick='..tick..' '..state) end
  end
  if tick%120==0 then log('UI_MISSIONS_STATUS tick='..tick..' '..tostring(gate.ui_missions_status())) end
  local part=gate.ui_missions_frame()
  return part and '|'..part or ''
 end
 -- The module's part of the probe's frame; the receipt joins the pilot receipt.
 function S.frame(tick)
  S.tick=tick
  if S.wrap_hire_menus then S.wrap_hire_menus() end   -- the conversation family's hire scope, once each menu exists
  S.apply(tick)
  protect(tick)
  run_script(tick)
  guard_frame(tick)
  local refusals=gate.ui_replay_refusals()
  if refusals then log('UI_REPLAY_REFUSED_NATIVE tick='..tick..' '..refusals) end
  if tick%120==0 then log('UI_REPLAY_STATUS tick='..tick..' '..tostring(gate.ui_replay_status())) end
  local receipt=assert(gate.ui_replay_frame(),'ui replay receipt unavailable')
  -- Every slot's activity and soft target as they change: what the receipt hashes, readable.
  local slots=receipt:match('|(.*)$')
  if slots and slots~=S.slots then S.slots=slots;log('UI_REPLAY_SLOTS tick='..tick..' '..slots) end
  local extra=''
  for _,part in ipairs(S.extras) do extra=extra..part(tick) end   -- another core's receipt
  return receipt..fleet_rows(tick)..missions_frame(tick)..(S.trade and S.trade.frame(tick) or '')..extra
 end
 return S
end

-- Ops that run in the presentation scope, as a person's click on this client does (U6c): X4's soft target refuses the
-- ship the player controls (ui/core/messageconfigurationgraph.xml: untargetableconditions is_playercontrolled), and in
-- the probe's canonical frame that is slot 1's ship on every client, so slot k could not target it. The other ops keep
-- the canonical frame their live runs (U5, U7) passed with. Guidance is set and ended where the map's click runs too.
K.SCOPED={target_slot=true,target_none=true,guidance_slot=true,guidance_end=true,inv_read=true,inv_drop=true}
-- A canonical op is the probe's own call, not a person's click: a test fixture that every client's plan holds at the same
-- tick (MD runs it on every client alike, nothing is captured).
K.CANONICAL={mis_fixture=true,gm_limit=true,slot_kill=true,slot_airlock=true,launch_stock=true,inv_seed=true,inv_look=true,inv_join_state=true,inv_join_give=true}
-- Refused entries the stock HUD calls by itself, not on a person's action: logged, no info line. AddKnownItem: the
-- encyclopedia entry the HUD adds for whatever comes into view (45 refusals and their info lines in a playtest of 9 Nov).
K.GUARD_QUIET={AddKnownItem=true}
K.FIXTURE_OFFER,K.FIXTURE_MISSION,K.FIXTURE_TEXTURE='X4LAN test offer','X4LAN test mission','x4lan_rt'   -- native_gate_ui_replay.xml
-- What the scripted mission ops need of the stock UI's queries (C: ffi.C with the probe's declarations; env: the globals):
-- an offer by its name (the briefing's and the map's lists), with its client, and a mission by its name.
function K.mission_tools(C,env)
 local T={}
 function T.find_offer(name)
  for _,bbs in ipairs({true,false}) do
   local n=tonumber(C.GetNumCurrentMissionOffers(bbs)) or 0
   if n>0 then
    local list=ffi.new('uint64_t[?]',n)
    n=tonumber(C.GetCurrentMissionOffers(list,n,bbs))
    for i=0,n-1 do
     local id=(tostring(list[i]):gsub('ULL$',''))
     local details={env.GetMissionOfferDetails(env.ConvertStringToLuaID(id))}
     if details[1]==name then T.actor=details[22];return id,details[22] end
    end
   end
  end
  error('ui replay script: no mission offer named '..name)
 end
 function T.find_mission(name)
  for i=1,tonumber(env.GetNumMissions()) or 0 do
   local id,mission=env.GetMissionDetails(i)
   if mission==name then return env.ConvertStringTo64Bit(tostring(id)) end
  end
  error('ui replay script: no mission named '..name)
 end
 -- The briefing's subject: the fixture mission once accepted, its offer before.
 function T.subject(which)
  if which=='offer' then return env.ConvertStringTo64Bit((T.find_offer(K.FIXTURE_OFFER))) end
  return T.find_mission(K.FIXTURE_MISSION)
 end
 return T
end
-- Named script actions for runs without people; arguments are saved IDs or slots, resolved when they run.
-- plan: list of {tick, op, ...}, ticks relative to the pilot window start.
function K.script(plan,start,tools)
 local by={}
 for _,e in ipairs(plan or {}) do
  local at,op=start+e[1],e[2]
  local call
  if op=='travel' then call=function() tools.C.StartPlayerActivity('travel');return 'ffi' end
  elseif op=='travel_stop' then call=function() tools.C.StopPlayerActivity('travel');return 'ffi' end
  elseif op=='travel_key' then call=function() return tools.gate.pilot_stock_inject('action',0x12f,0) end
  elseif op=='target_slot' then call=function() return tools.C.SetSofttarget(tools.slot_id(e[3]),'') end
  elseif op=='target_none' then call=function() return tools.env.RemoveSofttarget() end
  elseif op=='match' then call=function() return tools.gate.pilot_stock_inject('state',0x5b,e[3]) end
  elseif op=='input' then call=function() return tools.gate.pilot_stock_inject(e[3],e[4],e[5]) end
  -- The flight families: autopilot by Lua (no guidance needed) and by the FFI toggle, dock, undock, weapon groups.
  elseif op=='ap_to' then call=function() return tools.env.StartAutoPilot(tools.slot_lua(e[3])) end
  -- The autopilot to any saved object, as the map's "Autopilot" on a station (e.g. to get within docking range first).
  elseif op=='ap_obj' then call=function() return tools.env.StartAutoPilot(tools.saved_lua(e[3])) end
  elseif op=='ap_stop' then call=function() return tools.env.StopAutoPilot() end
  elseif op=='ap_toggle' then call=function() return tools.C.ToggleAutoPilot(false) end
  elseif op=='quickdock' then call=function() return tools.C.QuickDock() end
  elseif op=='dock_at' then call=function() return tools.C.RequestDockAt(tools.saved_id(e[3]),false) end
  elseif op=='undock' then call=function() return ffi.string(tools.C.UndockPlayerShip(false)) end
  elseif op=='wgroup' then call=function() return tools.C.SetDefensibleActiveWeaponGroup(tools.slot_id(tools.own_slot),e[3]==1,e[4]) end
  elseif op=='wkey' then call=function() return tools.gate.pilot_stock_inject('action',e[3],0) end
  -- Guidance as the map's and the interact menu's "Set guidance" / "End guidance": the client's own, never captured
  -- (lockstep_guidance_local.h keeps what it runs in the simulation on local IDs with native_gate_guidance_local).
  elseif op=='guidance_slot' then call=function() tools.C.SetGuidance(tools.slot_id(e[3]),ffi.new('UIPosRot'));return 'ffi' end
  elseif op=='guidance_end' then call=function() tools.C.EndGuidance();return 'ffi' end
  -- Missiles: the secondary trigger held (1) or released (0), carried by this client's record.
  elseif op=='missile' then call=function() return tools.gate.ui_replay_missile_hold(e[3]) end
  -- The queue editor as the map's rows call it: a create, its parameter and the enable in one Lua call, a move, a removal.
  elseif op=='q_move' then call=function()
    local id=tools.saved_id(e[3]);local idx=tools.C.CreateOrder(id,'MoveWait',false)
    if idx>0 then tools.env.SetOrderParam(tools.saved_lua(e[3]),idx,1,nil,{tools.saved_lua(e[4]),{e[5],e[6],e[7]}});tools.C.EnableOrder(id,idx) end
    return idx end
  -- {tick,'q_dock',ship,station}: DockAndWait at the station, as the map's "Dock at" (create + destination + enable); the
  -- ship's own pilot docks it (a player-flown ship once its autopilot follows the queue: ap_toggle).
  elseif op=='q_dock' then call=function()
    local id=tools.saved_id(e[3]);local idx=tools.C.CreateOrder(id,'DockAndWait',false)
    if idx>0 then tools.env.SetOrderParam(tools.saved_lua(e[3]),idx,1,nil,tools.saved_lua(e[4]));tools.C.EnableOrder(id,idx) end
    return idx end
  elseif op=='q_wait' then call=function()
    local id=tools.saved_id(e[3]);local idx=tools.C.CreateOrder(id,'Wait',false)
    if idx>0 then tools.C.EnableOrder(id,idx) end
    return idx end
  elseif op=='q_adjust' then call=function() return tools.C.AdjustOrder(tools.saved_id(e[3]),e[4],e[5],true,false,false) end
  elseif op=='q_remove' then call=function() return tools.C.RemoveOrder(tools.saved_id(e[3]),e[4],false,false) end
  elseif op=='clear' then call=function() return tools.C.RemoveAllOrders2(tools.saved_id(e[3]),false,false) end
  elseif op=='attack' then call=function() return tools.env.CreateOrder(tools.saved_lua(e[3]),'Attack',{primarytarget=tools.saved_lua(e[4])},false,false,false,nil,nil,nil,true) end
  elseif op=='move' then call=function() return tools.env.CreateOrder(tools.saved_lua(e[3]),'MoveWait',{destination={tools.saved_lua(e[4]),{e[5],e[6],e[7]}}},false,false,false,nil,nil,nil,true) end
  elseif op=='dock' then call=function() return tools.env.CreateOrder(tools.saved_lua(e[3]),'DockAndWait',{destination=tools.saved_lua(e[4])},false,false,false) end
  -- The UI guard and the events family (reports/LOCKSTEP-UI-GUARD-20261101.txt): stock entries as a menu calls them. With the
  -- guard each is refused in the local phase (nothing runs, a UI_GUARD_REFUSED row); with events slot 1's MD calls replay.
  elseif op=='ui_event' then call=function() tools.env.AddUITriggeredEvent('X4LAN_UiGuard',e[3],e[4]);return 'lua' end
  elseif op=='signal' then call=function() tools.env.SignalObject(tools.slot_lua(e[3]),e[4]);return 'lua' end
  -- money and rename answer before->after: with the guard both sides are equal on the client that tried.
  elseif op=='money' then call=function() local before=tools.env.GetPlayerMoney();tools.C.AddPlayerMoney(e[3]);return tostring(before)..'->'..tostring(tools.env.GetPlayerMoney()) end
  elseif op=='rename' then call=function()
    local ship=tools.slot_lua(e[3]);local before=tools.env.GetComponentData(ship,'name');tools.env.SetComponentName(ship,e[4])
    return tostring(before)..'->'..tostring(tools.env.GetComponentData(ship,'name')) end
  elseif op=='flight_assist' then call=function() tools.C.ToggleFlightAssist();return 'ffi' end
  -- An AddUITriggeredEvent as any menu makes it: screen, control, value (a UI-only one runs here, K.UI_ONLY).
  elseif op=='ui_raw' then call=function() tools.env.AddUITriggeredEvent(e[3],e[4],e[5]);return 'lua' end
  -- The flight family (lockstep_ui_replay.h): the HUD's turret and drone buttons (crosshair handling.lua), 1-based.
  elseif op=='turret_mode' then call=function() tools.C.CycleTurretMode(e[3]);return 'ffi' end
  elseif op=='turret_missile' then call=function() tools.C.CycleTurretMissile(e[3]);return 'ffi' end
  elseif op=='drones' then call=function() tools.C.ToggleDrones(e[3]);return 'ffi' end
  elseif op=='drone_mode' then call=function() tools.C.CycleNextDroneMode(e[3]);return 'ffi' end
  -- Deployables as the ship interact menu launches them: kind (satellite, navbeacon, probe, mine, lasertower), macro name,
  -- from this client's own slot ship.
  elseif op=='launch' then call=function()
    local fn=({satellite='LaunchSatellite',navbeacon='LaunchNavBeacon',probe='LaunchResourceProbe',mine='LaunchMine',lasertower='LaunchLaserTower'})[e[3]]
    if not fn then error('launch kind '..tostring(e[3])) end
    tools.C[fn](tools.slot_id(tools.own_slot),e[4]);return 'ffi' end
  -- A ship's settings as the docked menu and the map set them, on this client's own slot ship; cargo dropped as the map does.
  elseif op=='turrets_mode' then call=function() tools.C.SetAllTurretModes(tools.slot_id(tools.own_slot),e[3]);return 'ffi' end
  elseif op=='turrets_armed' then call=function() tools.C.SetAllTurretsArmed(tools.slot_id(tools.own_slot),e[3] and true or false);return 'ffi' end
  elseif op=='drones_armed' then call=function() tools.C.SetAllDronesArmed(tools.slot_id(tools.own_slot),e[3] and true or false);return 'ffi' end
  elseif op=='drop_cargo' then call=function() return tostring(tools.C.DropCargo(tools.slot_id(tools.own_slot),e[3],e[4])) end
  -- Test only (MD Stock, canonical: in every client's plan at the same tick): a slot's ship gets deployables to launch.
  elseif op=='launch_stock' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Stock',{ship=tools.slot_lua(e[3]),macro=e[4],amount=e[5] or 1});return 'lua' end
  elseif op=='comm' then call=function() return tools.env.StartConversationFromMenu('default',tools.slot_lua(e[3])) end
  -- Missions and conversations (lockstep_ui_missions.h). mis_fixture: the test offer (MD native_gate_ui_replay.xml), canonical;
  -- the rest as the briefing menu, the map's mission list and the dialog call the stock entries.
  elseif op=='mis_fixture' then call=function() tools.env.AddUITriggeredEvent('X4LAN_UiMissions','Fixture',{location=tools.slot_lua(e[3] or 1)});return 'lua' end
  -- Test only: the generic missions' limit per sector (MD GmLimit), canonical like mis_fixture.
  elseif op=='gm_limit' then call=function() tools.env.AddUITriggeredEvent('X4LAN_UiMissions','GmLimit',e[3]);return 'lua' end
  -- Test only: slot e[3]'s ship loses its hull (native_gate_pilot_respawn.xml Kill), in every present client's plan at the same tick.
  -- Test only: walking slot e[3] leaves its ship through the airlock (lockstep_pilot_death.h pvehk_ask), in every present
  -- client's plan at the same tick; a person's click on the console goes through lockstep_ui_compslot.h instead.
  -- Test only: this client asks X4 for a reload of its UI, as a window resize or a UI scale change does (the stock Lua
  -- call ScheduleReloadUI); with native_gate_ui_reload_hold the session goes on and X4 reloads after it.
  elseif op=='ui_reload' then call=function() tools.env.ScheduleReloadUI();return 'asked' end
  elseif op=='slot_airlock' then call=function() return tostring(tools.gate.pilot_death_out(e[3])) end
  elseif op=='slot_kill' then call=function() tools.env.AddUITriggeredEvent('X4LAN_PilotRespawn','Kill',{slotname=tostring(e[3]),object=tools.slot_lua(e[3])});return 'lua' end
  elseif op=='offer_accept' or op=='offer_decline' then call=function()
    local offer,actor=tools.missions.find_offer(K.FIXTURE_OFFER)
    tools.env.SignalObject(actor,op=='offer_accept' and 'accept' or 'decline',tools.env.ConvertStringToLuaID(offer));return offer end
  elseif op=='mis_active' then call=function() return tools.C.SetActiveMission(tools.missions.find_mission(K.FIXTURE_MISSION)) end
  elseif op=='mis_none' then call=function() return tools.C.SetActiveMission(0) end
  elseif op=='mis_abort' then call=function() tools.C.AbortMission(tools.missions.find_mission(K.FIXTURE_MISSION));return 'ffi' end
  elseif op=='briefing' then call=function() tools.C.StartBriefing(tools.missions.subject(e[3]),K.FIXTURE_TEXTURE,false);return 'ffi' end
  elseif op=='briefing_stop' then call=function() tools.C.StopBriefing(tools.missions.subject(e[3]),false);return 'ffi' end
  elseif op=='briefing_button' then call=function() tools.C.MissionBriefingInteractButtonClicked(tools.missions.subject(e[3]));return 'ffi' end
  -- conv_start: our conversation X4LAN_Talk with the fixture's client ('fixture', found with its offer) or a saved object.
  elseif op=='conv_start' then call=function()
    local actor=e[3]=='fixture' and (tools.missions.actor or select(2,tools.missions.find_offer(K.FIXTURE_OFFER))) or tools.saved_lua(e[3])
    tools.env.StartConversationFromMenu(e[4] or 'X4LAN_Talk',actor);return 'lua' end
  elseif op=='dialog' then call=function() tools.env.SelectDialogOption(e[3]);return 'lua' end
  -- The C function itself, as a script that held it before the arming (the core UI's dialog menu): the guard's hook.
  elseif op=='dialog_native' then call=function() tools.env.X4LAN_UI_REPLAY_STOCK.SelectDialogOption(e[3]);return 'native' end
  elseif op=='conv_cancel' then call=function() tools.env.CancelConversation();return 'lua' end
  -- Hiring a person who stands on a station (the conversation family's hire scope, reports/LOCKSTEP-ON-FOOT-20261101.txt):
  -- hire_talk talks to the first person of the saved station e[3] with a hiring fee (DefaultComm offers 'Hire' then);
  -- hire_confirm confirms the map's hire menu (MD opened it on 'g_hireworksomewhere') for slot e[3]'s ship or a saved
  -- object, role or post e[4] (e[5] true: a post), as its Hire button does.
  -- where: a saved object's position in its sector (read only, nothing captured), e.g. a station to start a ship next to.
  elseif op=='where' then call=function()
    local pos=tools.C.GetObjectPositionInSector(tools.saved_id(e[3]))
    return string.format('%.0f,%.0f,%.0f/%.0f',pos.x,pos.y,pos.z,math.deg(pos.yaw)) end
  elseif op=='hire_talk' then call=function()
    for _,npc in ipairs(tools.env.GetNPCs(tools.saved_id(e[3])) or {}) do
     local fee=tools.env.GetNPCBlackboard(npc,'$HiringFee')
     if type(fee)=='number' and fee>0 then tools.env.StartConversationFromMenu('default',npc);return tostring(npc)..' fee='..fee end
    end
    error('ui replay script: nobody to hire on '..tostring(e[3])) end
  elseif op=='hire_confirm' then call=function()
    for _,menu in ipairs(tools.env.Menus or {}) do
     if type(menu)=='table' and menu.name=='MapMenu' and menu.mode=='hire' then
      local target=type(e[3])=='number' and tools.slot_id(e[3]) or tools.saved_id(e[3])
      menu.contextMenuData=menu.contextMenuData or {}
      menu.contextMenuData.hireObject,menu.contextMenuData.hireRole,menu.contextMenuData.hireIsPost=target,e[4],e[5]==true
      -- The hire context (createHireContext) sets the container's discounts before its button exists (h1h): read here
      -- the same way, read only on this client.
      if not menu.hiringdiscounts and type(menu.modeparam)=='table' then
       local env=tools.env
       local discounts=env.GetComponentData(env.GetContextByClass(env.ConvertIDTo64Bit(menu.modeparam[2]),'container',true),'hiringdiscounts') or {}
       discounts.totalfactor=1
       for _,entry in ipairs(discounts) do discounts.totalfactor=discounts.totalfactor-entry.amount/100 end
       menu.hiringdiscounts=discounts
      end
      menu.buttonHire()
      return 'hire '..tostring(e[4])
     end
    end
    error('ui replay script: no hire menu open') end
  -- A player's own inventory (each slot's player entity holds one; slot k's is reached in slot k's scope).
  -- {tick,'inv_seed',slot,ware,amount}: test fixture, canonical: that slot's player gets the wares inside the slot's scope,
  -- the same on every client; answers the player entity it reached and the amount before->after.
  -- {tick,'inv_look',slot,ware}: canonical, read only: the slot's amount as its scope reads it.
  -- {tick,'inv_read',ware}: this client's own amount as its menus read it (the presentation scope).
  elseif op=='inv_seed' or op=='inv_look' then call=function()
    local k=e[3]
    if k~=1 and tools.gate.mpp_scope(k)~=1 then error('ui replay script: no scope for slot '..tostring(k)) end
    local function amount() local item=(tools.env.GetPlayerInventory() or {})[e[4]];return item and item.amount or 0 end
    local ok,result=pcall(function()
      local _,who=pcall(function() return tostring(tools.C.GetPlayerID()) end)
      local before=amount()
      if op=='inv_seed' then tools.env.AddInventory(nil,e[4],e[5]) end
      return 'slot='..tostring(k)..' player='..tostring(who)..' '..tostring(e[4])..'='..tostring(before)..(op=='inv_seed' and '->'..tostring(amount()) or '') end)
    if k~=1 and tools.gate.mpp_unscope(k)~=1 then error('ui replay script: the scope of slot '..tostring(k)..' did not close') end
    if not ok then error(result) end
    return result end
  elseif op=='inv_read' then call=function()
    local _,who=pcall(function() return tostring(tools.C.GetPlayerID()) end)
    local item=(tools.env.GetPlayerInventory() or {})[e[3]]
    return 'player='..tostring(who)..' '..tostring(e[3])..'='..tostring(item and item.amount or 0) end
  elseif X4LAN_UI_TRADE and X4LAN_UI_TRADE.SCRIPT_OPS[op] then call=X4LAN_UI_TRADE.script_call(e,tools)   -- the trade family's ops
  elseif X4LAN_UI_FACTION and X4LAN_UI_FACTION.SCRIPT_OPS[op] then call=X4LAN_UI_FACTION.script_call(e,tools)   -- the faction family's ops
  elseif X4LAN_UI_INVENTORY_CORE and X4LAN_UI_INVENTORY_CORE.SCRIPT_OPS[op] then call=X4LAN_UI_INVENTORY_CORE.script_call(e,tools)   -- the inventory family's
  else error('unknown ui replay script op '..tostring(op)) end
  by[at]=by[at] or {};by[at][#by[at]+1]={op=op,call=call,scoped=K.SCOPED[op] or nil,canonical=K.CANONICAL[op] or nil}
 end
 return by
end

-- ---------------------------------------------------------------------------------------------------------------------
-- The trade family (native_gate_ui_replay lists 'trade', bit 1024; default off; lockstep_ui_trade.h,
-- reports/LOCKSTEP-UI-TRADE-20261101.txt). The trade menus' world changes become events of this client's slot, for EVERY
-- slot: every client runs them at the apply tick, in slot order and call order, through the stock function, after an
-- authority check (what the call changes belongs to the slot's faction and is no other slot's ship), inside the slot's
-- faction scope (gate.ui_trade_scope: g_PlayerFaction = the owner of the slot's ship for the length of the call). X4 keeps
-- the player's money on the player faction, so coop (every slot `player`) pays from the one shared wallet and factions mode
-- from x4lan_playerk's own account. Lua C globals are wrapped here (op 0x58); the FFI setters of station trade settings
-- are hooked natively (op 0x59) and run through gate.ui_trade_run after the same check.
X4LAN_UI_TRADE={}
local T=X4LAN_UI_TRADE
T.OP_LUA,T.OP_FFI=0x58,0x59
-- The wire's numbering (lockstep_ui_trade.h UITRADE_LUA_GLOBALS).
T.GLOBALS={'AddTradeToShipQueue','TransferPlayerMoneyTo','TransferMoneyToPlayer','SetContainerWarePriceOverride','ClearContainerWarePriceOverride',
 'SetContainerStockLimitOverride','ClearContainerStockLimitOverride','SetMinBudget','SetMaxBudget','SignalObject','SetVirtualCargoMode'}
-- The argument naming what a call changes (the ship that trades; the container whose account takes or gives, whose prices,
-- stock limits or budget change; the ship or container the trade menu keeps its virtual cargo for while open, a per-container
-- state at +0x710 the menu sets on open and clears on close: replayed, every client keeps the same): it must belong to the
-- slot's faction.
T.SUBJECT={AddTradeToShipQueue=2,TransferPlayerMoneyTo=2,TransferMoneyToPlayer=2,SetContainerWarePriceOverride=1,ClearContainerWarePriceOverride=1,
 SetContainerStockLimitOverride=1,ClearContainerStockLimitOverride=1,SetMinBudget=1,SetMaxBudget=1,SignalObject=3,SetVirtualCargoMode=1}
-- SignalObject is the events family's; trade takes the docked trade's signal only: md/orders.xml PlayerTradeOrderAdded creates
-- the trade computer on event.param2 (the ship) and reads nothing of the player, so it means the same for every slot.
T.SIGNALS={docked_player_trade_added=true}
T.BUY,T.SELL,T.MISSION,T.EXCHANGE=1,2,4,8
T.STATUS_TICKS=120
T.SCRIPT_OPS={trade_to_ship=true,trade_from_ship=true,trade_loop=true,trade_buy=true,trade_sell=true,trade_price=true,trade_budget=true}
local function trade_hex(b) return string.format('%02x',b) end
-- A component or offer ID as decimal text, from a number, a 64-bit cdata, a Lua ID or decimal text; nil when it is none.
function T.id_text(env,v)
 local kind=type(v)
 if kind=='number' then return string.format('%.0f',v) end
 if kind=='cdata' then return (tostring(v):gsub('ULL$','')) end
 if kind=='string' then return v:match('^%d+$') end   -- the gate's decimal runtime IDs
 if v==nil or kind=='table' or kind=='boolean' then return nil end
 local ok,c=pcall(env.ConvertIDTo64Bit,v)
 if not ok or c==nil then return nil end
 return (tostring(c):gsub('ULL$',''))
end
local function trade_items(list)
 local out={}
 if type(list)~='table' then return out end
 for i=1,#list do out[i]=list[i] end
 return out
end
-- The offers of a list that match ware, kind and mission flag, in the engine's list order.
local function trade_matches(list,ware,kind,mission)
 local out={}
 for _,td in ipairs(trade_items(list)) do
  local k=td.isbuyoffer and T.BUY or td.isselloffer and T.SELL or 0
  if td.ware==ware and k==kind and (td.ismissionoffer and true or false)==mission then out[#out+1]=td end
 end
 return out
end
-- A trade offer is no component (report 7c): its portable reference is the station (a component reference), the ware, buy
-- or sell, the mission flag, the list it is in (the station's own, or the ware exchange between the trading ship and the
-- station), its place among the matching offers of that list and how many there are: {s=station, w=ware, c=code}. Nil and
-- why when the offer is not in either list.
function T.offer_ref(env,offer,ship)
 local ok,td=pcall(env.GetTradeData,offer)
 if not ok or type(td)~='table' or td.station==nil then return nil,'offer' end
 local want=T.id_text(env,offer)
 if not want then return nil,'offer' end
 local ship_text=T.id_text(env,ship)
 for source=0,1 do
  local list
  if source==0 then list=env.GetTradeList(td.station)
  elseif ship_text and env.GetWareExchangeTradeList then list=env.GetWareExchangeTradeList(env.ConvertStringToLuaID(ship_text),td.station) end
  for _,entry in ipairs(trade_items(list)) do
   if T.id_text(env,entry.id)==want then
    local kind=entry.isbuyoffer and T.BUY or entry.isselloffer and T.SELL
    if not kind or type(entry.ware)~='string' then return nil,'offer' end
    local mission=entry.ismissionoffer and true or false
    local same,at=trade_matches(list,entry.ware,kind,mission),nil
    for i,m in ipairs(same) do if T.id_text(env,m.id)==want then at=i end end
    if not at or #same>255 then return nil,'offer' end
    return {s=td.station,w=entry.ware,c=kind+(mission and T.MISSION or 0)+source*T.EXCHANGE+16*at+4096*#same}
   end
  end
 end
 return nil,'offer_unlisted'
end
-- At the apply, every client: the offer the reference names now (its ID as the list gives it), or nil and why. A list that
-- no longer holds the same number of matching offers refuses (offer_changed), the same on every client if the economy agrees.
function T.resolve_offer(env,ref,ship)
 if type(ref)~='table' or ref.s==nil or type(ref.w)~='string' or type(ref.c)~='number' then return nil,'offer' end
 local c=ref.c
 local kind,mission,source,at,count=c%4,math.floor(c/4)%2==1,math.floor(c/8)%2,math.floor(c/16)%256,math.floor(c/4096)
 local list
 if source==0 then list=env.GetTradeList(ref.s)
 elseif T.id_text(env,ship) and env.GetWareExchangeTradeList then list=env.GetWareExchangeTradeList(env.ConvertStringToLuaID(T.id_text(env,ship)),ref.s) end
 local same=trade_matches(list,ref.w,kind,mission)
 if #same~=count or not same[at] then return nil,'offer_changed' end
 return same[at].id
end
-- The family's part of a session. p: the session's parameters (gate, C, env, log, mask, slot_ships, own_slot, slots, fleet_tick,
-- trade_fund); refuse(what, why). Returns run(slot, op, hex) and frame(tick) for the core, the counts and authority().
function T.attach(S,p,refuse)
 local gate,C,env,log=p.gate,p.C,p.env,p.log
 local K=X4LAN_UI_REPLAY_CORE
 local R={captured=0,applied=0,skipped=0,refused=0,view=0}
 local stock=S.stock
 for _,name in ipairs(T.GLOBALS) do if stock[name]==nil then stock[name]=env[name] end end
 for _,name in ipairs({'GetPlayerMoney','GetTradeShipList'}) do if stock[name]==nil then stock[name]=env[name] end end
 for _,line in ipairs({'UniverseID GetPlayerID(void);','void AddPlayerMoney(int64_t);','bool HasShipTradeLoopCargoReservationOverride(UniverseID);',
   'void SetShipTradeLoopCargoReservationOverride(UniverseID,bool);','void RemoveShipTradeLoopCargoReservationOverride(UniverseID);'}) do pcall(ffi.cdef,line) end
 local function slot_ship(slot) for d,s in pairs(p.slot_ships()) do if s==slot then return d end end end
 local function faction_of(decimal)
  if not decimal then return nil end
  local ok,owner=pcall(env.GetComponentData,env.ConvertStringTo64Bit(decimal),'owner')
  return ok and owner or nil
 end
 R.slot_ship,R.faction_of=slot_ship,faction_of
 -- nil when slot `slot` may change `subject` with `name`; else why (the same on every client: the world is the same).
 function R.authority(slot,subject,name,args)
  local id=T.id_text(env,subject)
  if not id then return 'subject' end
  local other=p.slot_ships()[id]
  if other and other~=slot then return 'slot_ship' end
  local mine=slot_ship(slot)
  if not mine then return 'no_ship' end
  local faction=faction_of(mine)
  if not faction or faction_of(id)~=faction then return 'not_owner' end
  -- The slot's own ship trades through the trade computer only (docked, immediate): a person flies it, it takes no trade order.
  if id==mine and name=='AddTradeToShipQueue' and args[4]~=true then return 'slot_ship' end
 end
 -- Capture: this client's UI (a local phase, not the probe's frame, not an apply); nothing runs here, the menus do not use
 -- the answers. The SignalObject wrapper sits on the events family's (or the stock function) and takes the trade signal only.
 local events=math.floor(p.mask/K.FAMILY.events)%2==1
 local signal_prev=events and env.SignalObject or stock.SignalObject
 local function capture(id,name,args,n)
  local hex,why=K.encode_call(args,n,S.ctx)
  if not hex then R.refused=R.refused+1;return refuse(name,why) end
  if gate.ui_replay_push(T.OP_LUA,trade_hex(id)..hex)==1 then S.captured=S.captured+1;R.captured=R.captured+1
  else R.refused=R.refused+1;log('UI_REPLAY_REFUSED what='..name..' why=native') end   -- UI_REPLAY_REFUSED_NATIVE says why
 end
 for id,name in ipairs(T.GLOBALS) do
  if stock[name] then
   local prev=name=='SignalObject' and signal_prev or stock[name]
   env[name]=function(...)
    if S.replaying or gate.ui_replay_local()~=1 then return prev(...) end
    local args,n={...},select('#',...)
    if name=='SignalObject' then
     if not T.SIGNALS[args[2]] then return prev(...) end
     args[1]=nil   -- the player entity: the apply names X4's own
    elseif name=='AddTradeToShipQueue' then
     local ref,why=T.offer_ref(env,args[1],args[2])
     if not ref then R.refused=R.refused+1;return refuse(name,why) end
     args[1]=ref
    end
    capture(id,name,args,n)
   end
  end
 end
 -- The trade view (factions mode, this client's UI only, presentation): the stock menus show and list the `player` faction's.
 -- When this client's slot belongs to another faction, its money (GetPlayerMoney) is its own faction's and its own ship is in
 -- the trade menu's ship list (GetTradeShipList), so the person can trade with it through the stock menu. Reads only.
 local own=p.own_slot
 local view_logged
 local function view()
  if not own or gate.ui_replay_local()~=1 then return nil end
  local ship=slot_ship(own)
  local faction=faction_of(ship)
  if not faction or faction=='player' then return nil end
  if not view_logged then view_logged=true;log('UI_TRADE_VIEW slot='..own..' faction='..tostring(faction)) end
  return ship
 end
 if stock.GetPlayerMoney then
  env.GetPlayerMoney=function(...)
   if view() then local cents=gate.ui_trade_money(own);if cents then R.view=R.view+1;return cents/100 end end
   return stock.GetPlayerMoney(...)
  end
 end
 if stock.GetTradeShipList then
  env.GetTradeShipList=function(...)
   local list=stock.GetTradeShipList(...)
   local ship=view()
   if ship and type(list)=='table' then
    for _,entry in ipairs(trade_items(list)) do if T.id_text(env,entry.shipid)==ship then return list end end
    list[#list+1]={shipid=env.ConvertStringToLuaID(ship)};R.view=R.view+1
   end
   return list
  end
 end
 -- One due event (the probe's frame, every client): ok, what happened.
 function R.run(slot,op,hex)
  if op==T.OP_FFI then
   local subject,gone=gate.ui_trade_subject(hex)
   if not subject then R.skipped=R.skipped+1;return false,gone end   -- a mismatch or a decode error stops natively
   local no=R.authority(slot,subject,'ffi',{})
   if no then R.skipped=R.skipped+1;return false,'ffi '..no end
   local ok,what=gate.ui_trade_run(slot,hex)
   if ok==1 then R.applied=R.applied+1 else R.skipped=R.skipped+1 end
   return ok==1,what
  end
  local name=T.GLOBALS[tonumber(hex:sub(1,2),16) or 0]
  if not name or not stock[name] then gate.ui_replay_fail('ui_trade_op');return false,'op' end
  local args,n=K.decode_call(hex:sub(3),S.ctx)
  if not args then
   if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
   R.skipped=R.skipped+1;return false,name..' '..tostring(n)
  end
  local no=R.authority(slot,args[T.SUBJECT[name]],name,args)
  if no then R.skipped=R.skipped+1;return false,name..' '..no end
  if name=='AddTradeToShipQueue' then
   local offer,why=T.resolve_offer(env,args[1],args[2])
   if not offer then R.skipped=R.skipped+1;return false,name..' '..why end
   args[1]=offer
  elseif name=='SignalObject' then
   if not T.SIGNALS[args[2]] then gate.ui_replay_fail('ui_trade_op');return false,'signal' end
   args[1]=env.ConvertStringTo64Bit(T.id_text(env,C.GetPlayerID()))
  end
  -- The slot's faction pays and owns for the length of the call (no scope for the signal: MD runs later, as slot 1's does).
  local scope=0
  if name~='SignalObject' then
   scope=gate.ui_trade_scope(slot)
   if scope<0 then R.skipped=R.skipped+1;return false,name..' no_faction' end
  end
  local ok,result=pcall(stock[name],unpack(args,1,n))
  if scope==1 and gate.ui_trade_scope_end()~=1 then return false,'scope' end
  if ok then R.applied=R.applied+1 else R.skipped=R.skipped+1 end
  return ok,ok and name..(scope==1 and ' scope' or '') or tostring(result)
 end
 -- Test fixture (native_gate_ui_trade_fund, credits): every slot faction that is not `player` gets this much once, at the
 -- fleet tick, on every client (the stock AddPlayerMoney in that slot's faction scope), so factions mode has money to spend.
 local funded=false
 -- Every slot faction that is not `player` (the plan's pool factions, factions mode) gets its own account once, before any of
 -- its money moves: X4 gives only the player faction one, the others resolve to its dummy account (money made from nothing,
 -- money vanishing; live t2d). The same tick on every client (the fleet tick), outside any scope.
 local accounted=false
 local function accounts(tick)
  if accounted or tick<(p.fleet_tick or 0) or not gate.ui_trade_account then return end
  accounted=true
  for slot=1,p.slots or 0 do
   local faction=faction_of(slot_ship(slot))
   if faction and faction~='player' then
    local r,why=gate.ui_trade_account(slot)
    log(string.format('UI_TRADE_ACCOUNT tick=%d slot=%d faction=%s made=%s',tick,slot,tostring(faction),tostring(r or why)))
   end
  end
 end
 local function fund(tick)
  if funded or not p.trade_fund or tick<(p.fleet_tick or 0) then return end
  funded=true
  for slot=1,p.slots or 0 do
   local faction=faction_of(slot_ship(slot))
   if faction and faction~='player' then
    gate.ui_replay_bypass(1)
    local scope=gate.ui_trade_scope(slot)
    if scope==1 then pcall(C.AddPlayerMoney,p.trade_fund*100);gate.ui_trade_scope_end() end
    gate.ui_replay_bypass(0)
    log(string.format('UI_TRADE_FUND tick=%d slot=%d faction=%s credits=%d scope=%d',tick,slot,tostring(faction),p.trade_fund,scope))
   end
  end
 end
 -- The receipt's part: each slot faction's balance (native) and each slot ship's trade loop override. No container money:
 -- a container without its own account answers through X4's dummy account, whose balance getter draws from the thread's
 -- RNG (0x96e0a0), and the receipt must not draw.
 local last
 function R.frame(tick)
  accounts(tick)
  fund(tick)
  local rows={tostring(gate.ui_trade_frame() or 'UITRADE_1:-')}
  for slot=1,p.slots or 0 do
   local ship=slot_ship(slot)
   local reserve='x'
   if ship then
    local ok,r=pcall(C.HasShipTradeLoopCargoReservationOverride,env.ConvertStringTo64Bit(ship))
    if ok then reserve=r and '1' or '0' end
   end
   rows[#rows+1]=reserve
  end
  local text=table.concat(rows,';')
  if text~=last then last=text;log('UI_TRADE_MONEY tick='..tick..' '..text) end
  if tick%T.STATUS_TICKS==0 then
   log('UI_TRADE_STATUS tick='..tick..' '..tostring(gate.ui_trade_status())..' lua='..R.captured..'/'..R.applied..'/'..R.skipped..'/'..R.refused..' view='..R.view)
  end
  return '|'..text
 end
 return R
end
-- Scripted use for runs without people (native_gate_ui_replay_script): the stock entries as the menus call them, captured as
-- a person's click is. A ship argument is a slot number or a saved ID; without one, this client's own slot ship.
function T.script_call(e,tools)
 local op=e[2]
 local function ship64(v) v=v==nil and tools.own_slot or v;if type(v)=='number' then return tools.slot_id(v) end;return tools.saved_id(v) end
 local function shiplua(v) v=v==nil and tools.own_slot or v;if type(v)=='number' then return tools.slot_lua(v) end;return tools.saved_lua(v) end
 if op=='trade_to_ship' then return function() tools.env.TransferPlayerMoneyTo(e[3],ship64(e[4]));return 'lua' end end
 if op=='trade_from_ship' then return function() tools.env.TransferMoneyToPlayer(e[3],ship64(e[4]));return 'lua' end end
 if op=='trade_loop' then return function()
   if e[3]=='clear' then tools.C.RemoveShipTradeLoopCargoReservationOverride(ship64(e[4]))
   else tools.C.SetShipTradeLoopCargoReservationOverride(ship64(e[4]),e[3]==1) end
   return 'ffi' end end
 -- {tick,'trade_buy'|'trade_sell',station,ware,amount[,ship[,immediate]]}: the station's sell offer (we buy) or buy offer (we
 -- sell); immediate as the docked trade computer, then the menu's signal follows.
 if op=='trade_buy' or op=='trade_sell' then return function()
   local want=op=='trade_buy' and 'isselloffer' or 'isbuyoffer'
   local offer
   for _,td in ipairs(trade_items(tools.env.GetTradeList(tools.saved_lua(e[3])))) do
    if td.ware==e[4] and td[want] and not td.ismissionoffer then offer=td.id;break end
   end
   if offer==nil then return 'no_offer' end
   local immediate=e[7]==true or e[7]==1
   tools.env.AddTradeToShipQueue(offer,ship64(e[6]),e[5],immediate)
   if immediate then tools.env.SignalObject(tools.C.GetPlayerID(),'docked_player_trade_added',shiplua(e[6])) end
   return 'lua' end end
 -- {tick,'trade_price',station,ware,1 buy|0 sell,price}; {tick,'trade_budget',station,credits}: as the station menus call them.
 if op=='trade_price' then return function() tools.env.SetContainerWarePriceOverride(tools.saved_id(e[3]),e[4],e[5]==1,e[6]);return 'lua' end end
 if op=='trade_budget' then return function()
   tools.env.SetMaxBudget(tools.saved_id(e[3]),e[4]*3/2);tools.env.SetMinBudget(tools.saved_id(e[3]),e[4]);return 'lua' end end
 error('unknown trade script op '..tostring(op))
end

-- ---------------------------------------------------------------------------------------------------------------------
-- The faction family (native_gate_ui_replay lists 'faction', bit 32768; default off; needs native_gate_ui_guard;
-- lockstep_ui_faction.h, reports/LOCKSTEP-UI-FACTION-20261101.txt). The player faction's settings for every slot: blacklists,
-- fight rules, trade rules (create, update, remove, the faction's defaults, a controllable's own), default responses to
-- signals (the faction's and a controllable's), the faction's build method, weapon mode and name, the player's own settings
-- (ships and taxi wait, global loadout level, trade loop cargo reservation, illegal wares, logo, paint and clothing theme),
-- supply overrides, module pause and the resupply level. The native half claims the stock FFI entries at the guard's hooks
-- (op 0x5a: one record per call, in parts) and answers a create with a token the later records of the slot may name; this
-- half runs a whole record at its apply (every client, slot and call order): the subject's authority (the slot faction's,
-- no other slot's ship), then gate.ui_faction_run with the slot faction's ID, which runs the stock function in the slot's
-- faction scope (coop: `player` everywhere, the shared faction; factions mode: x4lan_playerk, "player" arguments become it).
X4LAN_UI_FACTION={}
local F=X4LAN_UI_FACTION
F.OP=0x5a
F.STATUS_TICKS=120
F.NOTICE_TICKS=90
-- The claimed entries in the native order (lockstep_ui_faction.h uifac_claims): the mirror the tests compare.
F.CLAIMS={'CreateBlacklist2','UpdateBlacklist2','RemoveBlacklist','SetPlayerBlacklistDefault','SetControllableBlacklist',
 'CreateFightRule','UpdateFightRule','RemoveFightRule','SetPlayerFightRuleDefault','SetControllableFightRule',
 'CreateTradeRule','UpdateTradeRule','RemoveTradeRule','SetPlayerTradeRuleDefault',
 'SetDefaultResponseToSignalForFaction2','ResetDefaultResponseToSignalForFaction','SetDefaultResponseToSignalForControllable','ResetResponseToSignalForControllable',
 'SetFactionBuildMethod','SetFactionDefaultWeaponMode','SetPlayerFactionName',
 'SetPlayerShipsWaitForPlayer','SetPlayerTaxiWaitsForPlayer','SetPlayerGlobalLoadoutLevel','SetPlayerTradeLoopCargoReservationSetting','SetPlayerIllegalWare',
 'SetPlayerLogo','SetPlayerPaintTheme','SetPlayerClothingTheme',
 'UpdateSupplyOverrides','PauseProcessingModule','PauseProductionModule','SetDefensibleLoadoutLevel'}
-- The stock struct layouts the native half reads and builds (size, offsets); a difference refuses the arming, a type no
-- menu declared yet is only reported.
F.LAYOUTS={
 {'BlacklistInfo2',72,{id=0,type=8,name=16,usemacrowhitelist=24,nummacros=28,macros=32,usefactionwhitelist=40,numfactions=44,factions=48,relation=56,hazardous=64}},
 {'FightRuleInfo',32,{id=0,name=8,numfactions=16,factions=24}},
 {'UIFightRuleSetting',24,{factionid=0,civiliansetting=8,militarysetting=16}},
 {'TradeRuleInfo',40,{id=0,name=8,numfactions=16,factions=24,iswhitelist=32}},
 {'UILogo',24,{file=0,icon=8,ispersonal=16}},
 {'SupplyOverride',16,{macro=0,amount=8}},
}
function F.check_layouts(ffi_)
 local missing={}
 for _,entry in ipairs(F.LAYOUTS) do
  local name,size,fields=entry[1],entry[2],entry[3]
  if pcall(ffi_.typeof,name) then
   if ffi_.sizeof(name)~=size then return nil,name..' size '..tostring(ffi_.sizeof(name)) end
   for field,offset in pairs(fields) do
    local ok,at=pcall(ffi_.offsetof,name,field)
    if not ok or at~=offset then return nil,name..'.'..field..' '..tostring(at) end
   end
  else missing[#missing+1]=name end
 end
 table.sort(missing)
 return true,missing
end
-- The faction view (factions mode, this client's own UI only): the stock functions that show the player faction's settings
-- run with X4's player faction = this client's slot faction (gate.ui_faction_view: the native half opens it only in a local
-- phase of this client's UI, when its slot's faction is not the player faction), so the player information menu lists and
-- edits the slot's own rules, defaults, name and money, and the map's rule lists offer them. Nothing of the world changes
-- in them (every world-changing entry is captured or refused there). Coop and slot 1: the view is never needed (0).
F.VIEW_MENUS={PlayerInfoMenu={'onShowMenu','refreshInfoFrame','onUpdate'}}
F.VIEW_HELPER={'getBlackLists','getFightRules','updateTradeRules'}
function F.pack(...) return {n=select('#',...),...} end   -- LuaJIT without the 5.2 extensions has no table.pack
-- Player settings per slot (native_gate_player_settings_per_slot, lockstep_player_settings.h; default off): factions mode,
-- every slot k has its own player settings (ships and taxi wait, loadout level, cargo reservation, illegal wares, logo, paint
-- and clothing theme). The native half holds them and makes the readers answer the owner's; this part tells it this
-- client's slot faction (the menus' "player" in the getters by faction ID), logs its state and status, and runs the spawn
-- check (native_gate_player_settings_spawn = {tick, macro}): at that tick, on every client in the probe's frame, a ship of
-- `macro` for every slot faction next to the slot's ship, its installed paint mod logged against the slot's theme.
function F.settings(p,R,log)
 local gate,env=p.gate,p.env
 local last_id,last_state,spawned
 local function paint_of(C,id)
  local mod=ffi.new('UIPaintMod')
  local ok,has=pcall(C.GetInstalledPaintMod,id,mod)
  return ok and has and mod.Ware~=nil and ffi.string(mod.Ware) or '-'
 end
 local function expected(C,theme,faction)
  if not theme or theme=='' then return '-' end
  local mod=ffi.new('UIPaintMod')
  local ok,has=pcall(C.GetPaintThemeMod,theme,faction,mod)
  return ok and has and mod.Ware~=nil and ffi.string(mod.Ware) or '-'
 end
 local function spawn(tick)
  local plan=p.pss_spawn
  if spawned or not plan or tick<plan[1] then return end
  spawned=true
  local C=p.C
  for slot=1,p.slots or 0 do
   local ship=R.slot_ship(slot)
   local faction=R.faction_of(ship)
   local row=string.format('PLAYER_SETTINGS_SPAWN tick=%d slot=%d faction=%s',tick,slot,tostring(faction))
   if ship and faction and C then
    local ok,result=pcall(function()
     local id=env.ConvertStringTo64Bit(ship)
     local sector=env.ConvertIDTo64Bit(env.GetComponentData(id,'sectorid'))
     local pos=C.GetObjectPositionInSector(id)
     local at=ffi.new('UIPosRot',{x=pos.x,y=pos.y,z=pos.z+2000+500*slot})
     gate.ui_replay_bypass(1)
     local made=C.SpawnObjectAtPos2(plan[2],sector,at,faction)
     gate.ui_replay_bypass(0)
     local made_id=(tostring(made):gsub('ULL$',''))
     -- GetComponentData takes a converted ID, not the C call's uint64 (it answered nothing for that: census f3set)
     local owner=env.GetComponentData(env.ConvertStringTo64Bit(made_id),'owner')
     local theme=gate.player_settings_themes(slot)
     return string.format(' ship=%s owner=%s paint=%s theme=%s expected=%s',made_id,tostring(owner),paint_of(C,made),
       theme~='' and theme or '-',expected(C,theme,faction))
    end)
    if not ok then pcall(gate.ui_replay_bypass,0) end
    row=row..(ok and result or ' error='..tostring(result))
   end
   log(row)
  end
 end
 return function(tick)
  local id=R.faction_of(R.slot_ship(p.own_slot))
  if id and id~=last_id then last_id=id;gate.player_settings_faction(id) end
  spawn(tick)
  local state=gate.player_settings_state()
  if state and state~=last_state then last_state=state;log('PLAYER_SETTINGS_STATE tick='..tick..' '..state) end
  if tick%F.STATUS_TICKS==0 then log('PLAYER_SETTINGS_STATUS tick='..tick..' '..tostring(gate.player_settings_status())) end
 end
end
-- The family's part of a session. p: gate, env, log, slot_ships(), slots, notify or nil, C or nil (the state row's responses),
-- menus() or nil (the stock menu tables, for the view), pss / pss_spawn (player settings per slot) or nil.
function F.attach(S,p)
 local gate,env,log=p.gate,p.env,p.log
 local R={applied=0,skipped=0,parts=0,dropped=0,refused=0,views=0}
 -- A stock function inside the view (an error inside still closes it).
 local function viewed(fn)
  return function(...)
   local opened=gate.ui_faction_view(1)
   local r=F.pack(pcall(fn,...))
   if opened==1 then R.views=R.views+1;gate.ui_faction_view(0) end
   if not r[1] then error(r[2],0) end
   return unpack(r,2,r.n)
  end
 end
 R.viewed=viewed
 local function wrap_view()
  local H=env.Helper
  if type(H)=='table' and not H.x4lan_faction_view then
   H.x4lan_faction_view=true
   local count=0
   for _,name in ipairs(F.VIEW_HELPER) do if type(H[name])=='function' then H[name]=viewed(H[name]);count=count+1 end end
   log('UI_FACTION_VIEW_WRAPPED table=Helper functions='..count)
  end
  for _,menu in ipairs(p.menus and p.menus() or {}) do
   local names=type(menu)=='table' and F.VIEW_MENUS[menu.name]
   if names and not menu.x4lan_faction_view then
    menu.x4lan_faction_view=true
    local count=0
    for _,name in ipairs(names) do if type(menu[name])=='function' then menu[name]=viewed(menu[name]);count=count+1 end end
    log('UI_FACTION_VIEW_WRAPPED table='..menu.name..' functions='..count)
   end
  end
 end
 local function slot_ship(slot) for d,s in pairs(p.slot_ships()) do if s==slot then return d end end end
 local function faction_of(decimal)
  if not decimal then return nil end
  local ok,owner=pcall(env.GetComponentData,env.ConvertStringTo64Bit(decimal),'owner')
  return ok and type(owner)=='string' and owner or nil
 end
 R.slot_ship,R.faction_of=slot_ship,faction_of
 -- nil when slot `slot` may change `subject` (a decimal runtime ID, '' none, 'gone': the native run skips it); else why. The
 -- same on every client: the world is.
 function R.authority(slot,subject)
  local mine=slot_ship(slot)
  if not mine then return 'no_ship' end
  if subject=='' or subject=='gone' then return nil end
  local other=p.slot_ships()[subject]
  if other and other~=slot then return 'slot_ship' end
  local faction=faction_of(mine)
  if not faction or faction_of(subject)~=faction then return 'not_owner' end
 end
 -- One part at its apply; the record runs when whole.
 S.ops[F.OP]=function(slot,hex,tick)
  local state,name,subject=gate.ui_faction_part(slot,hex)
  if state==nil then
   R.dropped=R.dropped+1
   log('UI_FACTION_DROPPED tick='..tostring(tick)..' slot='..slot..' why='..tostring(name))
   return false,'dropped '..tostring(name)
  end
  if state==0 then R.parts=R.parts+1;return true,'part' end
  local no=R.authority(slot,subject)
  local ok,what=gate.ui_faction_run(slot,faction_of(slot_ship(slot)) or '',no)
  if ok==1 then R.applied=R.applied+1 else R.skipped=R.skipped+1 end
  return ok==1,what
 end
 -- The first signal's default response of each slot faction (a query, rows only): what a response change looks like.
 local signal
 local function responses()
  local C=p.C
  if not C then return '' end
  if signal==nil then
   signal=false
   pcall(function()
    local n=C.GetNumAllSignals()
    if n>0 then local all=ffi.new('SignalInfo[?]',n);n=C.GetAllSignals(all,n);if n>0 then signal=ffi.string(all[0].id) end end
   end)
  end
  if not signal then return '' end
  local parts={}
  for slot=1,p.slots or 0 do
   local faction=faction_of(slot_ship(slot))
   local text='x'
   if faction then
    local ok,r=pcall(function() return ffi.string(C.GetDefaultResponseToSignalForFaction2(signal,faction,'')) end)
    text=ok and r or '?'
   end
   parts[#parts+1]=text
  end
  return ' response='..signal..':'..table.concat(parts,',')
 end
 -- The probe's frame: this client's capture refusals (a row each, the info line), the state as it changes, the status, the
 -- receipt's part (joins the pilot receipt).
 local notice_at,last_state
 S.extras[#S.extras+1]=function(tick)
  wrap_view()
  local rows=gate.ui_faction_refusals()
  if rows then
   local last
   for at,what,why in rows:gmatch('(%d+):([%w_]+):([%w_]+)') do
    R.refused=R.refused+1;last=what
    log('UI_FACTION_REFUSED tick='..at..' op='..what..' why='..why)
   end
   if last and p.notify and (not notice_at or tick-notice_at>=F.NOTICE_TICKS) then
    notice_at=tick
    pcall(p.notify,'Multiplayer: '..last..' is not shared for your slot yet - nothing happened.')
   end
  end
  if R.settings then R.settings(tick) end
  local state=tostring(gate.ui_faction_state())..responses()
  if state~=last_state then last_state=state;log('UI_FACTION_STATE tick='..tick..' '..state) end
  if tick%F.STATUS_TICKS==0 then
   log('UI_FACTION_STATUS tick='..tick..' '..tostring(gate.ui_faction_status())..' lua='..R.applied..'/'..R.skipped..'/'..R.parts..'/'..R.dropped..'/'..R.refused..' view='..R.views)
  end
  return '|'..assert(gate.ui_faction_frame(),'ui faction receipt unavailable')
 end
 if p.pss then R.settings=F.settings(p,R,log) end
 return R
end
-- Scripted use for runs without people (native_gate_ui_replay_script): the stock entries as the player information menu and
-- the map's behaviour panel call them, captured as a person's click is. A rule op remembers the ID (a token) the create
-- answered, as the menu keeps it; the later ops of the same kind name it.
F.SCRIPT_OPS={fac_blacklist=true,fac_blacklist_name=true,fac_blacklist_ship=true,fac_blacklist_remove=true,fac_fightrule=true,fac_fightrule_ship=true,
 fac_fightrule_remove=true,fac_traderule=true,fac_traderule_remove=true,fac_response=true,fac_ship_response=true,fac_ship_response_reset=true,
 fac_setting=true}
F.last={}
-- {signal index, response index} -> signal ID, response ID (the stock lists, 1-based), or an error.
function F.signal_response(C,si,ri)
 local n=C.GetNumAllSignals()
 local all=ffi.new('SignalInfo[?]',math.max(n,1));n=C.GetAllSignals(all,n)
 assert(si>=1 and si<=n,'ui faction script: no signal '..tostring(si))
 local signal=ffi.string(all[si-1].id)
 local m=C.GetNumAllResponsesToSignal(signal)
 local responses=ffi.new('ResponseInfo[?]',math.max(m,1));m=C.GetAllResponsesToSignal(responses,m,signal)
 assert(ri>=1 and ri<=m,'ui faction script: no response '..tostring(ri)..' to '..signal)
 return signal,ffi.string(responses[ri-1].id)
end
function F.script_call(e,tools)
 local op,C=e[2],tools.C
 local function own() return tools.slot_id(tools.own_slot) end
 local function rule(kind) return assert(F.last[kind],'ui faction script: no '..kind..' created yet') end
 -- {t,'fac_blacklist',name[,type[,civilian default]]}: CreateBlacklist2, then the default as the confirm button sets it;
 -- {t,'fac_blacklist_name',name[,type]}: UpdateBlacklist2 of the last one created.
 if op=='fac_blacklist' or op=='fac_blacklist_name' then return function()
   local kind=e[4] or 'sectortravel'
   local info=ffi.new('BlacklistInfo2')
   info.type=kind;info.name=e[3];info.relation=''
   if op=='fac_blacklist_name' then info.id=rule('blacklist');C.UpdateBlacklist2(info);return 'ffi' end
   local id=C.CreateBlacklist2(info)
   F.last.blacklist=id
   if id~=0 and e[5] then C.SetPlayerBlacklistDefault(id,kind,'civilian',true) end
   return string.format('%d',id) end end
 -- {t,'fac_blacklist_ship'[,type]}: the map's behaviour panel, this client's own ship.
 if op=='fac_blacklist_ship' then return function() C.SetControllableBlacklist(own(),rule('blacklist'),e[3] or 'sectortravel',true);return 'ffi' end end
 if op=='fac_blacklist_remove' then return function() C.RemoveBlacklist(rule('blacklist'));return 'ffi' end end
 -- {t,'fac_fightrule',name[,faction[,attack default]]}: one faction's setting, both settings 'default' (empty strings).
 if op=='fac_fightrule' then return function()
   local settings=ffi.new('UIFightRuleSetting[1]')
   settings[0].factionid=e[4] or 'player';settings[0].civiliansetting='';settings[0].militarysetting=''
   local info=ffi.new('FightRuleInfo')
   info.name=e[3];info.numfactions=1;info.factions=settings
   local id=C.CreateFightRule(info)
   F.last.fightrule=id
   if id~=0 and e[5] then C.SetPlayerFightRuleDefault(id,'attack',true) end
   return string.format('%d',id) end end
 if op=='fac_fightrule_ship' then return function() C.SetControllableFightRule(own(),rule('fightrule'),e[3] or 'attack',true);return 'ffi' end end
 if op=='fac_fightrule_remove' then return function() C.RemoveFightRule(rule('fightrule'));return 'ffi' end end
 -- {t,'fac_traderule',name[,{factions}[,whitelist[,trade default]]]}
 if op=='fac_traderule' then return function()
   local list=e[4] or {'player'}
   local factions=ffi.new('const char*[?]',#list)
   for i,f in ipairs(list) do factions[i-1]=f end
   local info=ffi.new('TradeRuleInfo')
   info.name=e[3];info.numfactions=#list;info.factions=factions;info.iswhitelist=e[5]~=false
   local id=C.CreateTradeRule(info)
   F.last.traderule=id
   if id~=0 and e[6] then C.SetPlayerTradeRuleDefault(id,'buy',true);C.SetPlayerTradeRuleDefault(id,'sell',true) end
   return string.format('%d',id) end end
 if op=='fac_traderule_remove' then return function() C.RemoveTradeRule(rule('traderule'));return 'ffi' end end
 -- {t,'fac_response',signal index,response index}: the faction's default (player information, global standing orders).
 if op=='fac_response' then return function()
   local signal,response=F.signal_response(C,e[3] or 1,e[4] or 1)
   C.SetDefaultResponseToSignalForFaction2(response,false,signal,'player','')
   return signal..'/'..response end end
 -- {t,'fac_ship_response',signal index,response index} / {t,'fac_ship_response_reset',signal index}: this client's own ship.
 if op=='fac_ship_response' then return function()
   local signal,response=F.signal_response(C,e[3] or 1,e[4] or 1)
   C.SetDefaultResponseToSignalForControllable(response,false,signal,own())
   return signal..'/'..response end end
 if op=='fac_ship_response_reset' then return function()
   local signal=F.signal_response(C,e[3] or 1,1)
   C.ResetResponseToSignalForControllable(signal,own())
   return signal end end
 -- {t,'fac_setting',what,value[,flag]}: the player information menu's faction and player settings, the own ship's resupply.
 if op=='fac_setting' then return function()
   local what,v=e[3],e[4]
   if what=='ships_wait' then C.SetPlayerShipsWaitForPlayer(v==true or v==1)
   elseif what=='taxi_wait' then C.SetPlayerTaxiWaitsForPlayer(v==true or v==1)
   elseif what=='loop_reserve' then C.SetPlayerTradeLoopCargoReservationSetting(v==true or v==1)
   elseif what=='loadout' then C.SetPlayerGlobalLoadoutLevel(v)
   elseif what=='name' then C.SetPlayerFactionName(v)
   elseif what=='weapon_mode' then C.SetFactionDefaultWeaponMode('player',v)
   elseif what=='build_method' then C.SetFactionBuildMethod('player',v)
   elseif what=='illegal' then C.SetPlayerIllegalWare(v,e[5]==true or e[5]==1)
   elseif what=='resupply' then C.SetDefensibleLoadoutLevel(own(),v)
   elseif what=='paint' or what=='clothing' then
    -- a theme by its position in the stock list (1-based) or by ID, as the player information menu offers them
    local id=v
    if type(v)=='number' then
     local paint=what=='paint'
     local n=paint and C.GetNumAvailablePaintThemes() or C.GetNumAvailableClothingThemes()
     local buf=ffi.new(paint and 'UIPaintTheme[?]' or 'UIClothingTheme[?]',math.max(n,1))
     n=paint and C.GetAvailablePaintThemes(buf,n) or C.GetAvailableClothingThemes(buf,n)
     assert(v>=1 and v<=n,'ui faction script: no '..what..' theme '..tostring(v))
     id=ffi.string(buf[v-1].ID)
    end
    if what=='paint' then C.SetPlayerPaintTheme(id) else C.SetPlayerClothingTheme(id) end
    return id
   elseif what=='logo' then
    -- a logo by its position in the stock list (standard and custom, 1-based)
    local n=C.GetNumPlayerLogos(true,true)
    local buf=ffi.new('UILogo[?]',math.max(n,1))
    n=C.GetPlayerLogos(buf,n,true,true)
    assert(v>=1 and v<=n,'ui faction script: no logo '..tostring(v))
    C.SetPlayerLogo(buf[v-1])
    return buf[v-1].file~=nil and ffi.string(buf[v-1].file) or '-'
   else error('ui faction script: unknown setting '..tostring(what)) end
   return 'ffi' end end
 error('unknown faction script op '..tostring(op))
end
