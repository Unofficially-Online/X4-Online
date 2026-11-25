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
-- Signals of ours (on the galaxy) whose MD cue acts as the signalling slot's player: a slot 2..N's replayed one runs in
-- its scope with the generation open (gate.mpp_generate), so the instance is that slot's. x4lan_ai_dock: MD AiDock.
K.SLOT_SIGNALS={x4lan_ai_dock=true}
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
-- A fleet ship's question answered (the key or the click on its notification: RaisePlayerInteractionEvent): number
-- K.ANSWER = UIMIS_C_ANSWER with the notification's sequence number, captured natively (lockstep_md_media.h); every
-- client raises the event at the apply (gate.md_media_answer). With conversation owners (p.conv_slots) any player answers:
-- the answer takes the conversation for its slot first, as a start does, so the conversation the ship's script starts
-- next is that player's (its dialog on that player's screen); a conversation already running refuses it, the same on
-- every client ('busy'). Without them slot 1's only.
K.ANSWER=17
K.HIRE_SCOPES={MapMenu={'buttonHire'}}
-- The UI guard (lockstep_ui_guard.h, native_gate_ui_guard): its groups in the native order (bit 1 << index-1).
K.GUARD_GROUPS={'flight','orders','fleet','trade','faction','assets','build','conflict','missions','events','conversation','session','cheat','editor'}
K.GUARD_NOTICE_TICKS=90   -- the info line of a refusal at most this often
K.EVENT_MAX=200
-- One event carries 160 bytes (lockstep_ui_replay.h UIREP_EVENT_FIT). An order whose parameters need more travels in
-- parts: the interact menu's "Deploy" on a place (DeployObjectAtPosition: the place, the macro, the amount) is 223
-- bytes and was refused as too_long, so a fleet ship could not be sent to drop a satellite or a probe (found by
-- tools/dev/action_tests.py mining, 19 Nov). A part begins with a byte no argument count can be: PART_FIRST and a
-- chunk, PART_MORE and a chunk, ..., PART_LAST and the rest; the receiver joins a slot's parts in the order they arrive.
K.EVENT_FIT,K.PART_MORE,K.PART_LAST,K.PART_FIRST,K.PART_DATA,K.PARTS_MAX=160,0xf0,0xf1,0xf2,159,4
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
-- The events of a call that is longer than one: PART_FIRST..chunk, PART_MORE..chunk, ..., PART_LAST..chunk.
function K.split_call(hex)
 local out,size={},K.PART_DATA*2
 for at=1,#hex,size do out[#out+1]=byte_hex(at+size>#hex and K.PART_LAST or at==1 and K.PART_FIRST or K.PART_MORE)..hex:sub(at,at+size-1) end
 return out
end
-- A slot's event of a call at its apply: the whole call's hex; nil,'part' while more follows; nil and a reason for a
-- part without its beginning (the sender's queue was full on the way) or too many parts: nothing runs, the same on
-- every client. `parts` holds what each slot has sent so far; a whole call or a new first part drops an unfinished one.
function K.join_call(parts,slot,hex)
 local head=tonumber(hex:sub(1,2),16)
 if head~=K.PART_FIRST and head~=K.PART_MORE and head~=K.PART_LAST then parts[slot]=nil;return hex end
 if head==K.PART_FIRST then parts[slot]=hex:sub(3);return nil,'part' end
 if not parts[slot] then return nil,'orphan' end
 local so_far=parts[slot]..hex:sub(3)
 if #so_far/2>K.PART_DATA*K.PARTS_MAX then parts[slot]=nil;return nil,'parts' end
 if head==K.PART_MORE then parts[slot]=so_far;return nil,'part' end
 parts[slot]=nil
 return so_far
end
-- (args, n[, limit]) -> event hex (count, values), or nil and the reason.
function K.encode_call(args,n,ctx,limit)
 if n>ITEMS_MAX then return nil,'count' end
 local out={byte_hex(n)}
 for i=1,n do
  local h,why=encode_value(args[i],ctx,0)
  if not h then return nil,why end
  out[#out+1]=h
 end
 local hex=table.concat(out)
 if #hex/2>(limit or K.EVENT_MAX) then return nil,'too_long' end
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
  'MapMenu.orderMoveWait','MapMenu.orderAttack','MapMenu.orderFollow',
  -- The other order entries that make the same calls and no other (a selectable check, RemoveAllOrders2, the Lua
  -- CreateOrder: all captured; an order whose parameters the capture cannot carry is refused whole, as before).
  -- Opened 17 Nov; of these only Explore has run live (tools/dev/action_tests.py orders).
  'InteractMenu.orderAttackInRange','InteractMenu.orderAttackSurfaceElements','InteractMenu.orderCollect','InteractMenu.orderCollectDeployable',
  'InteractMenu.orderCollectLockbox','InteractMenu.orderCollectRadius','InteractMenu.orderDeployAtPosition','InteractMenu.orderDepositInventoryAtHQ',
  'InteractMenu.orderExplore','InteractMenu.orderExploreUpdate','InteractMenu.orderGetSupplies','InteractMenu.orderMining',
  'InteractMenu.orderPlayerDockToTrade','InteractMenu.orderProtect','InteractMenu.orderRescueInRange','InteractMenu.orderRescueShip',
  'InteractMenu.orderSalvageCollect','InteractMenu.orderSalvageCrush','InteractMenu.orderSalvageInRadius','InteractMenu.orderTacticalAttack',
  'MapMenu.orderWithdrawFromCombat','MapMenu.orderAttackMultiple'},
 autopilot={'hotkey.INPUT_ACTION_ADDON_DETAILMONITOR_A_SHIFT'},
 -- The map's order rows and the interact menu's queue entries whose calls are all captured (not ProceedWithOrders).
 -- The behaviour page's confirm (buttonDefaultOrderConfirm: ResetOrderLoop, EnablePlannedDefaultOrder; for a loop
 -- RemoveAllOrders, a default Wait, SetOrderLoop) is one of them since ResetOrderLoop is captured (17 Nov).
 queue={'MapMenu.buttonNewOrder','MapMenu.buttonOrderUp','MapMenu.buttonOrderDown','MapMenu.removeOrder','MapMenu.buttonSetOrderParam',
  'MapMenu.setOrderParamFromMode','MapMenu.slidercellSetOrderParam','MapMenu.buttonStartOrders','MapMenu.buttonDockToTrade',
  'MapMenu.buttonDefaultOrderConfirm',
  -- The trade menu's Confirm in its loop mode (a ship on repeat orders: a SingleBuy or SingleSell order at a station,
  -- CreateOrder, four SetOrderParam, EnableOrder; all the queue family's). Blocked until 19 Nov: a person's click did
  -- nothing (found by tools/dev/action_tests.py tradeloop).
  'MapMenu.buttonConfirmTradeLoop',
  'InteractMenu.buttonRemoveOrder','InteractMenu.setOrderImmediate'},
 -- The trade menu's "Confirm" (the docked trade with the own ship, a trade order for a fleet ship): every call it makes
 -- is the trade family's (AddTradeToShipQueue, the docked trade's signal). Until 17 Nov it stayed blocked: a person's
 -- click on it did nothing in a session, while the scripted trade_buy / trade_sell (the same calls, no menu) traded.
 trade={'MapMenu.buttonConfirmTrade'},
}
function K.unblocked(mask)
 local out={}
 for name,bit in pairs(K.FAMILY) do
  if math.floor(mask/bit)%2==1 then
   for _,entry in ipairs(K.UNBLOCKED[name] or {}) do out[entry]=true end
  end
 end
 return out
end

-- Ship Interactions (DockedMenu) on a client of slot 2..N: the probe arms it (pilot_view.arm_ui_replay).
-- X4 opens DockedMenu by itself from three places (menu_docked.lua's first game plan change and its checkActivation after
-- a conversation or a cinematic camera, the menu helper's closeMenu after another menu closed), each asking first "is
-- the player's ship docked, or does the player sit at a post that is not the pilot's". When such a handler runs outside
-- this client's own view of the player, "the player" is player 1: when player 1 docked, the menu opened on every other
-- client too, as that client's Ship Interactions, held its flight controls and stopped its ship (X4's stop in menus; two
-- scripted clients, 16 Nov). So an automatic open is asked again inside this client's presentation scope (p.own) and
-- left out when the answer is no.
-- A person opens the same menu without that question: the HUD's top level bar ("Ship Interactions", in the HUD's own
-- top level menu and in the tabs of the map, the player information and the other top level menus; their tab scroll keys)
-- goes Helper.scrollTopLevelInternal -> Helper.closeMenuAndOpenNewMenu -> OpenMenu('DockedMenu',{0,0},...), and Back from
-- a menu opened out of it (its "Ship Information" map) goes Helper.closeMenu(menu,'back') -> OpenMenu(its return record).
-- Until 24 Nov the automatic opens' question was asked of these too; in flight its answer is no, so player 2's click on
-- Ship Interactions closed the bar or the map and opened nothing (live games rk7 and rk8: UI_DOCKED_MENU kept=N right
-- after each click, scope=false). Now the two Helper routes mark their call as a person's navigation and the open
-- passes as in stock (row UI_DOCKED_MENU ... person=N via=tab|back). The Enter key (INPUT_ACTION_OPEN_COCKPIT_MENU,
-- action 0x13d) opens the menu natively (X4's 0x8c7a70 -> MenuHandler::OpenMenu 0x8c82f0), not through OpenMenu.
-- What the menu then does for slot k (flight assist, modes, deployables, autopilot, dock, Get Up, weapons) goes through
-- the stock calls the replay families and the guard already take, as player 1's does.
-- p: present(on) (the probe's: true opens the scope from the probe's canonical frame and answers whether it did, false
-- closes it again), own() (the question, in whatever scope is open; may raise), log(text). Once per Lua state (a re-arm
-- finds the wrappers in env.X4LAN_OPEN_MENU_STOCK); returns the state {kept, passed, person, helper} or nil, why.
function K.docked_menu(env,p)
 if type(env.OpenMenu)~='function' then return nil,'no_open_menu' end
 if env.X4LAN_OPEN_MENU_STOCK then return nil,'armed' end
 local D={kept=0,passed=0,person=0,helper=false}
 local stock_open=env.OpenMenu
 env.X4LAN_OPEN_MENU_STOCK=stock_open
 local nav,via   -- the menu a person's navigation is opening right now, and its route
 local function counts() return 'kept='..D.kept..' passed='..D.passed..' person='..D.person end
 env.OpenMenu=function(name,...)
  if name~='DockedMenu' then return stock_open(name,...) end
  if nav=='DockedMenu' then
   D.person=D.person+1
   p.log('UI_DOCKED_MENU '..counts()..' via='..tostring(via))
   return stock_open(name,...)
  end
  local opened=p.present(true)
  local ok,own=pcall(p.own)
  if opened then p.present(false) end
  if ok and not own then
   D.kept=D.kept+1
   p.log('UI_DOCKED_MENU '..counts()..' scope='..tostring(opened))
   return
  end
  D.passed=D.passed+1
  return stock_open(name,...)
 end
 local H=env.Helper
 if type(H)=='table' and type(H.closeMenuAndOpenNewMenu)=='function' and type(H.closeMenu)=='function' then
  local stock={closeMenuAndOpenNewMenu=H.closeMenuAndOpenNewMenu,closeMenu=H.closeMenu}
  env.X4LAN_DOCKED_NAV_STOCK=stock
  -- the mark holds for the stock call only, also when it raises (the error goes on unchanged)
  local function finish(before,before_via,ok,...)
   nav,via=before,before_via
   if not ok then error((...),0) end
   return ...
  end
  local function marked(name,route,f,...)
   local before,before_via=nav,via
   nav,via=name,route
   return finish(before,before_via,pcall(f,...))
  end
  H.closeMenuAndOpenNewMenu=function(menu,newname,...)
   return marked(newname,'tab',stock.closeMenuAndOpenNewMenu,menu,newname,...)
  end
  -- Back with a return record (no conversation menu: that one returns to its conversation); without one it only closes
  H.closeMenu=function(menu,due,...)
   if due=='back' and type(menu)=='table' and not menu.conversationMenu and type(menu.param2)=='table' and type(menu.param2[1])=='string' then
    return marked(menu.param2[1],'back',stock.closeMenu,menu,due,...)
   end
   return stock.closeMenu(menu,due,...)
  end
  D.helper=true
 end
 return D
end

-- p: gate, C (ffi.C), env (the Lua globals), log, mask, slot_ships() -> {decimal=slot}, script (by absolute tick).
function K.session(p)
 local gate,C,env,log,mask=p.gate,p.C,p.env,p.log,p.mask
 local S={replaying=false,captured=0,refused=0,applied=0,ui_only=0,unblocked=K.unblocked(mask),ops={},extras={},parts={}}   -- ops, extras: other cores' parts
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
   local hex,why=K.encode_call(args,n,ctx,K.PART_DATA*K.PARTS_MAX)
   if not hex then return refuse('CreateOrder',why) or 0 end
   if #hex/2<=K.EVENT_FIT then
    if gate.ui_replay_push(K.OP_LUA_CALL,hex)==1 then S.captured=S.captured+1 end   -- a native refusal counts and logs itself
    return 0
   end
   local sent=0
   for _,part in ipairs(K.split_call(hex)) do
    if gate.ui_replay_push(K.OP_LUA_CALL,part)~=1 then break end   -- the queue is full: the receiver drops what it has
    sent=sent+1
   end
   if sent>0 then S.captured=S.captured+1 end
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
   pcall(p.notify,p.conv_slots and 'Multiplayer: another player is in a conversation - nothing happened.' or
    'Multiplayer: '..name..' works for player 1 only for now - nothing happened.')
  end
 end
 S.slot_refuse=slot_refuse
 -- A conversation's owner slot (lockstep_conversation_owner.h, p.conv_slots): any slot may start one; a choice, a proceed,
 -- a return or a cancel is its owner's alone. Without it: slot 1 only, as before.
 local function conversation_refused(name)
  if not p.conv_slots then return p.own_slot~=1 end
  return name~='StartConversationFromMenu' and gate.conversation_owner_may(p.own_slot)~=1
 end
 if conversation then
  for id,name in ipairs(K.CONVERSATION) do
   assert(stock[name],'ui replay refused: conversation family without the Lua global '..name)
   env[name]=function(...)
    if S.replaying or gate.ui_replay_local()~=1 then return stock[name](...) end
    if conversation_refused(name) then return slot_refuse(name) end
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
   if conversation_refused(name) then return slot_refuse(name) end
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
 local function run_one(slot,op,hex,tick)
  if S.ops[op] then return S.ops[op](slot,hex,tick) end   -- another core's op (native_gate_ui_build_core.lua: 0x50)
  if S.trade and (op==X4LAN_UI_TRADE.OP_LUA or op==X4LAN_UI_TRADE.OP_FFI) then return S.trade.run(slot,op,hex) end
  if op==K.OP_LUA_CALL then
   local whole,part=K.join_call(S.parts,slot,hex)
   if not whole then return part=='part',part end   -- a part: nothing runs yet; too many parts: refused, on every client
   local args,n=K.decode_call(whole,ctx)
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
   -- A signal of ours that acts as its slot's player (K.SLOT_SIGNALS): the generation open as well, so the cue instance
   -- it makes is slot k's and its frames run in slot k's scope (as ConversationStartSlot's signal below).
   local made=slot~=1 and name=='SignalObject' and K.SLOT_SIGNALS[args[2]] and gate.mpp_generate and gate.mpp_generate(slot)==1
   local ok,result=pcall(stock[name],unpack(args,1,n))
   if made then gate.mpp_generate(0) end
   if slot~=1 and gate.mpp_unscope(slot)~=1 then gate.ui_replay_fail('mpp_leave') end
   return ok,ok and name..'('..tostring(args[1])..','..tostring(args[2])..')'..claimed..(made and ' generated' or '') or tostring(result)
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
   if number==K.ANSWER then
    if (slot~=1 and not p.conv_slots) or not gate.md_media_answer then gate.ui_replay_fail('ui_lua_op');return false,'op' end
    local args=K.decode_call(hex:sub(3),ctx)
    if not args or type(args[1])~='number' then gate.ui_replay_fail('ui_lua_decode');return false,'decode' end
    if p.conv_slots and gate.conversation_owner_take(slot)~=1 then return false,'busy' end
    local raised=gate.md_media_answer(args[1])==1
    return raised,raised and 'answer('..args[1]..')' or 'gone'   -- gone: the question was over, the same on every client
   end
   local name=K.CONVERSATION[number] or (number==K.HIRE_MONEY and 'TransferPlayerMoneyTo' or nil)   -- the hire scope's fee
   if (slot~=1 and not p.conv_slots) or not name or not stock[name] then gate.ui_replay_fail('ui_lua_op');return false,'op' end
   local args,n=K.decode_call(hex:sub(3),ctx)
   if not args then
    if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
    return false,n
   end
   -- X4 starts a menu conversation only as the result of a closing menu (Helper.closeMenuForNewConversation): a bare call
   -- from the replay's frame was ignored (m1d, m1e). So every client raises X4LAN_UiConversation/Start here and our MD
   -- (native_gate_ui_replay.xml ConversationStart) starts the same conversation at the same tick: start_conversation with the
   -- captured actor, name and parameter (the names MD knows; another is refused there, the same on every client).
   -- The owner (p.conv_slots): a start takes the conversation for its slot when the manager is idle and free, the same on
   -- every client; everything else is the owner's alone.
   if p.conv_slots then
    if name=='StartConversationFromMenu' then
     if gate.conversation_owner_take(slot)~=1 then return false,'busy' end
    elseif gate.conversation_owner_may(slot)~=1 then return false,'not_owner' end
   end
   if name=='StartConversationFromMenu' and stock.AddUITriggeredEvent then
    -- MD takes a component from a Lua ID only: a 64-bit actor (the captured call's own form) is converted here (m1f).
    local actor,form=args[2],type(args[2])
    if form=='cdata' or form=='number' then
     local okd,text=pcall(decimal,actor)
     actor=okd and env.ConvertStringToLuaID(text) or actor
    end
    local okt,text=pcall(decimal,actor)
    -- Slot k's start: the galaxy is signalled inside slot k's scope with the generation open, so our MD cue
    -- ConversationStartSlot runs start_conversation as slot k's and its instance is slot k's (the names MD knows there:
    -- default and X4LAN_Talk, without a parameter; another is refused, the same on every client).
    if slot~=1 then
     local galaxy=p.galaxy or (p.offers and p.offers.galaxy)
     if not galaxy or (args[1]~='default' and args[1]~='X4LAN_Talk') or args[3]~=nil then return false,'slot_conversation:'..tostring(args[1]) end
     if gate.mpp_scope(slot)~=1 then gate.ui_replay_fail('mpp_scope');return false,'mpp_scope' end
     gate.mpp_generate(slot)
     local ok,result=pcall(stock.SignalObject,galaxy(),'x4lan_conversation:'..args[1],actor)
     gate.mpp_generate(0)
     if gate.mpp_unscope(slot)~=1 then gate.ui_replay_fail('mpp_leave') end
     return ok,ok and 'md:'..name..'('..tostring(args[1])..','..form..':'..(okt and text or '?')..') slot='..slot or tostring(result)
    end
    local ok,result=pcall(stock.AddUITriggeredEvent,'X4LAN_UiConversation','Start',{conversation=args[1],actor=actor,convparam=args[3]})
    return ok,ok and 'md:'..name..'('..tostring(args[1])..','..form..':'..(okt and text or '?')..')' or tostring(result)
   end
   if slot~=1 and gate.mpp_scope(slot)~=1 then gate.ui_replay_fail('mpp_scope');return false,'mpp_scope' end   -- the owner's call in its scope
   local ok,result=pcall(stock[name],unpack(args,1,n))
   if slot~=1 and gate.mpp_unscope(slot)~=1 then gate.ui_replay_fail('mpp_leave') end
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
   -- The order a parameter belongs to is an index of the queue (mapped to where X4 put that create) or one of X4's
   -- words for the orders outside the queue ('default', 'planneddefault': the behaviour page's order). A word stays
   -- as it is: mapped like an index it became 0, the parameter went nowhere and the planned behaviour's confirm
   -- failed for want of it (17 Nov, action_tests.py orders: Patrol as the default order).
   if type(call[2])=='number' then call[2]=gate.ui_replay_queue_index(slot,decimal(call[1]),call[2],args[1]) end
   local name=op==K.OP_QUEUE_PARAM and 'SetOrderParam' or 'RemoveOrderListParam'
   local ok,result=pcall(stock[name],unpack(call,1,n-1))
   return ok,ok and name..'@'..tostring(call[2]) or tostring(result)
  end
  gate.ui_replay_fail('ui_lua_op')
  return false,'op'
 end
 -- An order is given as its player's faction (factions mode, slots 2..N): X4 takes an order for a ship of its player
 -- faction only, and at the apply that is player 1's on every client. Run fa3 (19 Nov): player 2 made its freighter its
 -- station's trader in the stock menu, every call replayed on both clients, CreateOrder3 answered 0 and nothing was
 -- assigned. The order, queue and clear ops of a slot run inside that slot's faction scope (the trade family's:
 -- g_PlayerFaction is the slot's faction for the length of the call); in coop and for slot 1 there is none to open.
 -- X4's own test then also keeps a player from ordering another player's ships about.
 local ORDER_OPS={[K.OP_LUA_CALL]=true,[K.OP_ORDERS_CLEAR]=true,[K.OP_ORDERS_CLEAR2]=true,[K.OP_QUEUE]=true,[K.OP_QUEUE_PARAM]=true,[K.OP_QUEUE_LIST]=true}
 local function run(slot,op,hex,tick)
  if not ORDER_OPS[op] or not gate.ui_trade_scope then return run_one(slot,op,hex,tick) end
  local opened=gate.ui_trade_scope(slot)
  local ok,what=run_one(slot,op,hex,tick)
  if opened==1 then
   if gate.ui_trade_scope_end()~=1 then return false,'scope' end
   what=tostring(what)..' scope'
  end
  return ok,what
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
 -- Players see each other on foot (native_gate_foot_avatars, on by default; MD native_gate_ui_replay.xml Avatar): every
 -- client, at the same ticks, signals the galaxy once per slot: slot 1 in the probe's own frame, slot k inside slot k's
 -- scope with the generation open, so the cue instance the signal makes is slot k's and all its later frames see slot
 -- k's player. That instance keeps a stand-in figure where its player walks. p.avatars={first=tick,stagger=ticks,
 -- galaxy=function -> Lua ID}. Stateless: the instance is in the world a joiner loads.
 function S.slot_avatars(tick)
  local a=p.avatars
  if not (a and a.galaxy) or tick<a.first then return end
  for k=1,p.slots do
   if tick==a.first+(k-1)*a.stagger then
    local done='no_scope'
    if k==1 then
     S.replaying=true;gate.ui_replay_bypass(1)
     local ok,why=pcall(function() stock.SignalObject(a.galaxy(),'x4lan_avatar',1) end)
     gate.ui_replay_bypass(0);S.replaying=false
     done=ok and 'signalled' or 'error:'..tostring(why)
    elseif not (p.mpp and gate.mpp_generate) then done='no_missions_per_player'
    elseif gate.mpp_scope(k)==1 then
     S.replaying=true;gate.ui_replay_bypass(1)
     local ok,why=pcall(function()
      assert(gate.mpp_generate(k)==1,'generate refused')
      stock.SignalObject(a.galaxy(),'x4lan_avatar',k)
     end)
     gate.mpp_generate(0)
     gate.ui_replay_bypass(0);S.replaying=false
     if gate.mpp_unscope(k)~=1 then gate.ui_replay_fail('mpp_leave') end
     done=ok and 'signalled' or 'error:'..tostring(why)
    end
    log('FOOT_AVATAR tick='..tick..' slot='..k..' '..done)
   end
  end
  -- On foot or not, per slot: the gate's answer, told to the figures' cues at its changes (the same tick everywhere).
  if gate.pilot_foot_walking and tick>a.first+p.slots*a.stagger then
   a.walks=a.walks or {}
   for k=1,p.slots do
    local walks=gate.pilot_foot_walking(k)==1
    if walks~=(a.walks[k] or false) then
     a.walks[k]=walks
     S.replaying=true;gate.ui_replay_bypass(1)
     local ok,why=pcall(function() stock.SignalObject(a.galaxy(),'x4lan_avatar_walk',k*10+(walks and 1 or 0)) end)
     gate.ui_replay_bypass(0);S.replaying=false
     log('FOOT_AVATAR_WALK tick='..tick..' slot='..k..' walks='..tostring(walks)..' '..(ok and 'signalled' or 'error:'..tostring(why)))
     -- This client's own player got up: the dock menu X4 opened for it closes, as X4's own get-up closes it
     -- (the stock menu listens for the player's get-up event, which only X4's own player's stand-up raises).
     if walks and k==p.own_slot and k>=2 and a.close_dock_menu then pcall(a.close_dock_menu) end
    end
   end
  end
 end
 -- A start's story for slot k (native_gate_story = {[k]='<Script>.<Cue>'}; MD x4lan_story.xml Begin, generated per
 -- session): every client, the same tick, names the start script's static cue to the gate and signals the galaxy inside
 -- slot k's scope with the generation open; the MD signals that cue at once, the gate claims it for slot k at its first
 -- frame (gate.mpp_story) and everything below it and every root instance it makes is slot k's.
 -- Diagnostic (native_gate_plot_debug): the generated story MD's PlotDebug cue signals a plot's own debug cue; every
 -- client, the same tick, outside any slot's scope (a copy's cue is the owning slot's by its root cue).
 local function plot_signal(tick,signal,row,code)
  local galaxy=p.galaxy or (p.offers and p.offers.galaxy)
  local done='no_galaxy'
  if galaxy then
   S.replaying=true;gate.ui_replay_bypass(1)
   local ok,why_not=pcall(function() stock.SignalObject(galaxy(),signal,code) end)
   gate.ui_replay_bypass(0);S.replaying=false
   done=ok and 'signalled' or 'error:'..tostring(why_not)
  end
  log(row..' tick='..tick..' code='..tostring(code)..' '..done)
  return done
 end
 function S.plot_debug(tick,code) return plot_signal(tick,'x4lan_plot_debug','PLOT_DEBUG',code) end
 -- Player 1's plot begun with its result (native_gate_plot_ready; the story MD's PlotReady): the same way, once.
 function S.plot_ready(tick,code) return plot_signal(tick,'x4lan_plot_ready','PLOT_READY',code) end
 -- Milestones (the probe's native_gate_milestones and native_gate_continue; MD x4lan_milestones.xml): a signal of ours on
 -- the galaxy with any value (a cue as a Lua ID: X4 gives MD the cue it names), every client the same tick, outside every
 -- slot's scope, X4's own SignalObject (not the capture's). The caller writes the rows (one snapshot is many signals).
 -- A call of the probe's frame as a person's click (a person's quick save, native_gate_milestones): this client's slot's
 -- event, captured and run on every client at the apply tick like a menu's. Refused when the call would not be captured
 -- (it would run on this client alone): (ok, result) of the call, or false and why.
 function S.click(fn)
  if not p.local_mark then return false,'no_local_mark' end
  p.local_mark(0)
  local scoped=p.present and p.present(true) or false   -- where a person's click runs (the console's and scripted ops' form)
  local ok,result
  if gate.ui_replay_local()==1 then ok,result=pcall(fn) else ok,result=false,'not_local' end
  if scoped then p.present(false) end
  p.local_mark(1)
  return ok,result
 end
 function S.galaxy_signal(signal,value)
  local galaxy=p.galaxy or (p.offers and p.offers.galaxy)
  if not galaxy then return 'no_galaxy' end
  S.replaying=true;gate.ui_replay_bypass(1)
  local ok,why_not=pcall(function() stock.SignalObject(galaxy(),signal,value) end)
  gate.ui_replay_bypass(0);S.replaying=false
  return ok and 'signalled' or 'error:'..tostring(why_not)
 end
 -- A start's blueprints and research for a slot with a faction of its own (factions mode; the gate's library per faction,
 -- lockstep_research_owner.h): every client, the same tick, inside slot k's scope, where the universe's player is slot k's
 -- with its own library, X4's own LearnBlueprint and AddResearch add to it (as an add_blueprints of its own mission would).
 -- kit = {blueprints={ware, ...}, research={ware, ...}}. The row names what the slot's library holds afterwards.
 function S.slot_library(tick,k,why,kit)
  local blueprints,research=kit.blueprints or {},kit.research or {}
  local done='no_scope'
  if not (p.mpp and gate.mpp_scope and gate.research_list and p.library) then done='no_library_per_faction'
  elseif gate.mpp_scope(k)==1 then
   S.replaying=true;gate.ui_replay_bypass(1)
   local ok,why_not=pcall(function()
    for _,ware in ipairs(blueprints) do C.LearnBlueprint(ware) end
    for _,ware in ipairs(research) do C.AddResearch(ware) end
   end)
   gate.ui_replay_bypass(0);S.replaying=false
   if gate.mpp_unscope(k)~=1 then gate.ui_replay_fail('mpp_leave') end
   done=ok and 'given' or 'error:'..tostring(why_not)
  end
  local has,count=nil,nil
  if gate.research_list then has,count=gate.research_list(k) end
  log('KIT_LIBRARY tick='..tick..' slot='..k..' why='..tostring(why)..' blueprints='..#blueprints..' research='..#research..' '..done..
   ' now=blueprints:'..tostring(count)..' research:'..tostring(has and select(2,has:gsub('[^,]+','')) or nil))
  return done
 end
 -- A slot's own copies of stock plots (tools/dev/native_gate_plot_copies.py) begin the same way in the same scope: the
 -- slot's entry is then a list {<story path or false>, '<Copy>.<Cue>', ...}; the i-th copy's signal carries k+100*i.
 function S.slot_story(tick,k,why)
  local galaxy=p.galaxy or (p.offers and p.offers.galaxy)
  local entry=type(p.story)=='table' and p.story[k] or nil
  local list=type(entry)=='table' and entry or {entry}
  local path=nil
  for i=1,8 do if type(list[i])=='string' then path=list[i];break end end
  local done,before='no_scope',gate.ui_missions_state and gate.ui_missions_state() or '-'
  if type(path)~='string' then done='no_story'
  elseif not (p.mpp and galaxy and gate.mpp_generate) then done='no_missions_per_player'
  elseif not gate.mpp_story then done='gate_without_story'
  elseif gate.mpp_scope(k)==1 then
   S.replaying=true;gate.ui_replay_bypass(1)
   local claimed,asked=true,0
   local ok,why_not=pcall(function()
    assert(gate.mpp_generate(k)==1,'generate refused')
    for i=1,8 do
     local one=list[i]
     if type(one)=='string' then   -- one cue per ask: named, signalled, claimed at its first frame
      asked=asked+1
      assert(gate.mpp_story(k,one)==1,'story refused '..one)
      stock.SignalObject(galaxy(),'x4lan_story',k+100*(i-1))
      if gate.mpp_story(0)~=1 then claimed=false;log('MPP_STORY tick='..tick..' slot='..k..' not_claimed='..one) end
     end
    end
   end)
   if not ok then gate.mpp_story(0);claimed=false end
   gate.mpp_generate(0)
   gate.ui_replay_bypass(0);S.replaying=false
   if gate.mpp_unscope(k)~=1 then gate.ui_replay_fail('mpp_leave') end
   done=(not ok and 'error:'..tostring(why_not) or claimed and 'signalled' or 'not_claimed')..' cues='..asked
  end
  log('MPP_STORY tick='..tick..' slot='..k..' why='..tostring(why)..' '..done..' before=['..tostring(before)..'] after=['..
   tostring(gate.ui_missions_state and gate.ui_missions_state() or '-')..'] '..tostring(gate.mpp_status and gate.mpp_status() or '-'))
  return done
 end
 -- The stock scripts' lists of "the player's" objects hold every player's (native_gate_player_groups; the MD patch
 -- native_gate_player_groups.xml of md.Setup): every client, at the same ticks (stateless: a joiner computes the same
 -- ticks), names the player entities of the slots that have one, in slot order; the script sets its groups again when
 -- the list changed. p.groups={first=tick,every=ticks,player=function(slot) -> Lua ID or nil}.
 function S.player_groups(tick)
  local g=p.groups
  if not g or tick<g.first or (tick-g.first)%g.every~=0 then return end
  local list={}
  for k=1,p.slots do
   local id=g.player(k)
   if id then list[#list+1]=id end
  end
  if #list==0 then return end
  S.replaying=true;gate.ui_replay_bypass(1)
  local ok,why=pcall(env.AddUITriggeredEvent,'X4LAN_PlayerGroups','Players',list)
  gate.ui_replay_bypass(0);S.replaying=false
  if tick==g.first or not ok then log('PLAYER_GROUPS tick='..tick..' players='..#list..' '..(ok and 'named' or 'error:'..tostring(why))) end
 end
 -- Every client, the probe's frame of the tick, the events in slot order and call order.
 function S.apply(tick)
  S.player_groups(tick)
  S.slot_offers(tick)
  S.slot_avatars(tick)
  local list=gate.ui_replay_lua(tick)
  if not list then return end
  S.replaying=true;gate.ui_replay_bypass(1)
  for slot,op,hex in list:gmatch('(%d+):(%d+):(%x*)') do
   local ok,what=run(tonumber(slot),tonumber(op),hex,tick)
   gate.ui_replay_result(tick,ok and 1 or 0)
   S.applied=S.applied+1
   -- what changed the game for a menu that is open (not a UI notice, not a 'known' mark): the build core redraws the station menu
   if ok and not tostring(what):find('^AddUITriggeredEvent') and not tostring(what):find('^known ') then S.applied_world=(S.applied_world or 0)+1 end
   -- a station setting of this client's own player landed (an offer, a price, a limit, the account, a trade rule):
   -- the build core draws the open station overview's node again
   if ok and tonumber(slot)==(p.own_slot or 1) and K.station_call(what) then S.applied_station=(S.applied_station or 0)+1 end
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
   if not (e.quiet and ok and result==nil) then log(string.format('UI_REPLAY_SCRIPT tick=%d op=%s ok=%s result=%s%s',tick,tostring(e.op),tostring(ok),tostring(result),
     e.scoped and (scoped and ' scope=1' or ' scope=0') or '')) end
  end
 end
 -- A slot 2..N's own undock end (lockstep_undock_screen.h): X4 raises the UI event playerUndocked only for its own
 -- player's ship, slot 1's, so on a client of slot 2..N the HUD's top level menu never came back after an undock
 -- (menu_toplevel.lua opens it on that event) and the menu helper stayed "undocking". The client raises it itself, as a
 -- person's UI does: in its presentation scope, at the tick its own ship is no longer docked. Row UI_REPLAY_UNDOCKED.
 local undock_was
 local function undock_watch(tick)
  if not p.undocked_event or not p.own_docked or (p.own_slot or 1)<2 or not p.present then return end
  if not p.present(true) then return end
  if p.local_mark then p.local_mark(0) end
  local ok,docked=pcall(p.own_docked)
  local raised,why
  if ok and docked==false and undock_was==true then raised,why=pcall(p.undocked_event) end
  if p.local_mark then p.local_mark(1) end
  p.present(false)
  if raised~=nil then log('UI_REPLAY_UNDOCKED tick='..tick..' slot='..tostring(p.own_slot)..' '..(raised and 'raised' or 'error:'..tostring(why))) end
  if ok and docked~=nil then undock_was=docked end
 end
 S.undock_watch=undock_watch
 -- Test console (native_gate_ui_replay_console, default off): p.console() loads a Lua file of this client that the
 -- person at the test rig rewrites while the session runs: `return {n=NUMBER, run=function(tools,S) ... end}`. A
 -- number above the last one seen runs once, where a scripted op runs: as a person's click (not the probe's own
 -- frame), in the presentation scope. One client only, so it may read anything but must change the game only through
 -- the stock entries, which are captured and replayed like a click's. Row UI_REPLAY_CONSOLE.
 local console_seen=0
 local function run_console(tick)
  if not p.console or tick%3~=0 then return end
  local loaded,cmd=pcall(p.console)
  if not loaded or type(cmd)~='table' or type(cmd.n)~='number' or cmd.n<=console_seen or type(cmd.run)~='function' then return end
  console_seen=cmd.n
  if p.local_mark and not cmd.canonical then p.local_mark(0) end
  local scoped=not cmd.unscoped and p.present and p.present(true) or false
  local ok,result=pcall(cmd.run,p.console_tools,S)
  if scoped then p.present(false) end
  if p.local_mark and not cmd.canonical then p.local_mark(1) end
  log(string.format('UI_REPLAY_CONSOLE tick=%d n=%d ok=%s result=%s',tick,cmd.n,tostring(ok),tostring(result)))
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
  run_console(tick)
  undock_watch(tick)
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
K.SCOPED={place=true,suit_to=true,dock_named=true,panel_find=true,hack_yes=true,inv_total=true,trader_pick=true,trader_look=true,itemtrader_buy=true,foot_where=true,transport_list=true,transport_to=true,transport_named=true,target_slot=true,target_none=true,target_home=true,dock_home=true,suit_home=true,guidance_slot=true,guidance_end=true,guidance_set=true,guidance_look=true,inv_read=true,inv_drop=true,inv_research_seen=true,inv_research_hqs=true,toplevel_tab=true,menu_back=true,escort_colour=true,escort_select=true,escort_map=true}
-- The stock calls of the station overview and the station menu's trade settings, by what their names hold.
function K.station_call(what)
 local name=tostring(what):match('^[%w_]+') or ''
 for _,part in ipairs({'Container','Budget','TradeWare','Supply','TransferMoney','TransferPlayerMoney','Production','Processing'}) do
  if name:find(part,1,true) then return true end
 end
 return false
end
-- A canonical op is the probe's own call, not a person's click: a test fixture that every client's plan holds at the same
-- tick (MD runs it on every client alike, nothing is captured).
K.CANONICAL={panel_look=true,refit_why=true,dock_approach=true,dock_warp=true,station_blueprints=true,mis_fixture=true,mt_offer=true,gm_limit=true,slot_kill=true,slot_airlock=true,launch_stock=true,hold_stock=true,hull_set=true,marines_stock=true,abandon=true,dock_kit=true,wallet=true,probe_far=true,groups_look=true,escort_look=true,escort_front=true,inv_seed=true,inv_look=true,inv_join_state=true,inv_join_give=true,transporter_find=true}
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
 -- Every offer this client's menus list, in the lists' order: {id, name, type, faction, actor}.
 function T.offers()
  local out={}
  for _,bbs in ipairs({true,false}) do
   local n=tonumber(C.GetNumCurrentMissionOffers(bbs)) or 0
   if n>0 then
    local list=ffi.new('uint64_t[?]',n)
    n=tonumber(C.GetCurrentMissionOffers(list,n,bbs))
    for i=0,n-1 do
     local id=(tostring(list[i]):gsub('ULL$',''))
     local d={env.GetMissionOfferDetails(env.ConvertStringToLuaID(id))}
     out[#out+1]={id=id,name=tostring(d[1]),kind=tostring(d[5])..'/'..tostring(d[6]),faction=tostring(d[8]),actor=d[22]}
    end
   end
  end
  return out
 end
 -- Every mission this client's menus list: {id, name}.
 function T.missions_now()
  local out={}
  for i=1,tonumber(env.GetNumMissions()) or 0 do
   local id,name=env.GetMissionDetails(i)
   out[#out+1]={id=tostring(id),name=tostring(name)}
  end
  return out
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
-- The flight home of a scripted spacesuit (the op suit_home). X4 takes a suit in when it reaches the ship it asked to
-- dock at, so after the request the suit has to fly there as a person flies it: the flight keys as states of this
-- client's own record (strafe 6..9, throttle 13/14), chosen every tick from where the ship is in the suit's frame
-- (tools.suit_delta: right, up, ahead in metres; nil once the player is no longer in a suit). The throttle is a set
-- point: its key ticks are counted so it comes back to zero. All keys are released at the arrival or the limit.
function K.suit_fly(tools,limit,find)   -- find(): another ship (its 64-bit ID) to fly to and ask to dock at, at the start; else home
 local F={throttle=0,held={},done=false}
 local function set(id,on)
  on=on and 1 or 0
  if (F.held[id] or 0)~=on then F.held[id]=on;tools.gate.pilot_stock_inject('state',id,on) end
 end
 function F.start()
  if find then F.target=find();if not F.target then F.done=true;return 'no_ship' end end
  local home=F.target or tools.home_id()
  return tostring(home)..'='..tostring(tools.C.RequestDockAt(home,false))
 end
 function F.step(i)
  if F.done then return nil end
  local d=tools.suit_delta(F.target)
  if not d or i>=limit then
   for _,id in ipairs({6,7,8,9,13,14}) do set(id,false) end
   F.done=true;return (d and 'limit' or 'home')..' after='..i
  end
  set(7,d[1]>1);set(6,d[1]<-1);set(8,d[2]>1);set(9,d[2]<-1)
  local want=d[3]>3 and 10 or d[3]<-3 and -10 or 0
  local up,down=F.throttle<want,F.throttle>want
  set(13,up);set(14,down);F.throttle=F.throttle+(up and 1 or down and -1 or 0)
  if i%60==0 then return string.format('d=%.1f/%.1f/%.1f throttle=%d',d[1],d[2],d[3],F.throttle) end
  return nil
 end
 return F
end
-- Docking at a station by script (the op dock_station): the autopilot to the station as the map's "Autopilot" starts
-- it, then the station menu's "Request Docking" (RequestDockAt) every second once the ship is within X4's range for a
-- request, until it is granted, then the autopilot to the dock the station assigned. At the dock X4 docks the ship
-- itself when it carries the docking computer (the test fixture dock_kit installs X4's own ware); the step reports
-- the docking when the ship says it is docked. tools.assigned_dock(): that dock as a Lua ID, or nil.
-- tools.dock_state(saved): {docked, distance in metres or nil in another sector}.
-- `detour` (false or 0: none): when an approach brought the ship no nearer to its dock (X4's autopilot to a dock flies
-- to a free place first; for the second ship at the wharf AVK-777 that place was 3.5 km short of the dock and the
-- approach ended there at once whenever it was started again, 17-19 Nov), the autopilot flies to the ship docked
-- nearest to that dock (tools.dock_neighbour: a person targets a ship and presses Shift+A) and the approach starts
-- again from beside it; at most three detours. (A course plotted to a place, SetGuidance, does not serve: a player
-- 2..N's guidance makes no guidance mission, run q1 of 19 Nov, so Shift+A has nothing to fly to.)
-- `hop` (false or 0: none): when an approach within a kilometre of the dock brought the ship no nearer, the ship is
-- flown by its flight keys, as a person flies it: straight up to K.HOP_HEIGHT above the dock (in the dock's frame),
-- across to K.HOP_OUT beyond it, then the approach again; at most two hops. The second pad of the wharf AVK-777 is on
-- the outer face of its module: from the station's side X4's autopilot to that dock, and to a ship docked beside it,
-- runs the ship into the module's wall 200 m up and stops (picture and dock-frame rows of runs 20261120-004252 and
-- -005618); the first ship's approach crosses the module from above and lands from 56 m over the pad.
-- A table for `hop`: the route's points instead, in the dock's frame ('here': the ship's own value at the hop's start;
-- {'approach',dx,dy,dz}: that far from where X4's landing check wants the ship, the answer of the op dock_approach, which
-- the hop waits for). hold=true in it: where the route ends the ship stays and waits for X4's landing (no dock key, no
-- approach again); {hold=true,after=N} without points: from step N the autopilot is stopped and never started again
-- (for the canonical fixture dock_warp at that time). X4 lands a player's ship when it is within the docking
-- computer's reach of its pad (U::Player's update; libraries/parameters.xml: an S dock with the Mk2 computer, 50 m along
-- each of the pad's axes); in a game whose stations' navigation octrees never showed (an open or dropped slot before
-- 25 Nov, lockstep_nav_octree.h) the autopilot to the dock ended 230-470 m in front of it, for player 1 as for player 2.
-- tools.dock_frames(): the ship's and the station centre's places in the dock's frame, the dock's and the station
-- centre's places in the ship's frame, and (when a detour chose one) the other ship's place in both, for a check.
K.DETOUR_NEAR=120
K.HOP_HEIGHT,K.HOP_OUT,K.HOP_NEAR,K.HOP_LIMIT=500,150,40,3600
K.HOP_TPS,K.HOP_GAIN,K.HOP_SPEED,K.HOP_THROTTLE=30,0.5,40,40
K.HOP_LAST_NEAR,K.SETTLE_KEY,K.SETTLE_END=12,90,240
-- A vector of one frame in another frame, from two vectors known in both (the triad method; both frames are X4's, of
-- the same handedness, and differ by a rotation): a1,b1 in frame 1 are a2,b2 in frame 2; v in frame 1 -> frame 2.
function K.frame_map(a1,b1,a2,b2,v)
 local function dot(u,w) return u[1]*w[1]+u[2]*w[2]+u[3]*w[3] end
 local function cross(u,w) return {u[2]*w[3]-u[3]*w[2],u[3]*w[1]-u[1]*w[3],u[1]*w[2]-u[2]*w[1]} end
 local function unit(u) local n=math.sqrt(dot(u,u));return {u[1]/n,u[2]/n,u[3]/n} end
 local function triad(a,b) local t1,t2=unit(a),unit(cross(a,b));return {t1,t2,cross(t1,t2)} end
 local t1,t2=triad(a1,b1),triad(a2,b2)
 local out={0,0,0}
 for i=1,3 do
  local k=dot(v,t1[i])
  for j=1,3 do out[j]=out[j]+k*t2[i][j] end
 end
 return out
end
function K.dock_fly(tools,saved,limit,via,detour,hop)
 local F={stage='fly',done=false,asked=0,detours=0,hops=0,held={},throttle=0}
 local function gap_value()
  local ok,value=pcall(function() return tools.dock_gap and tools.dock_gap() end)
  return ok and value or nil
 end
 -- ' dock_gap=N rel=X/Y/Z centre=X/Y/Z': the ship's distance to its assigned dock, when it has one (tools.dock_gap),
 -- and the ship's and the station centre's places in the dock's frame (tools.dock_rel)
 local function gap()
  local value=gap_value()
  if not value then return '' end
  local ok,rel=pcall(function() return tools.dock_rel and tools.dock_rel() end)
  return ' dock_gap='..math.floor(value)..((ok and rel) and ' rel='..rel or '')
 end
 -- The hop's flight keys (as K.suit_fly: strafe 6 left, 7 right, 8 up, 9 down; throttle 13 up, 14 down, counted)
 local function key(id,on)
  on=on and 1 or 0
  if (F.held[id] or 0)~=on then F.held[id]=on;tools.gate.pilot_stock_inject('state',id,on) end
 end
 local function release() for _,id in ipairs({6,7,8,9,13,14}) do key(id,false) end end
 -- A point of the dock's frame seen from the ship: {right, up, ahead}, the ship's place in the dock's frame, and the
 -- check's error in metres (the other ship mapped from the dock's frame against its place read in the ship's frame).
 -- With the tick `i`: also the ship's velocity in its own frame (m/s, from its place one tick before).
 local function hop_delta(h,i)
  local ok,p,c,ds,cs,nd,ns=pcall(tools.dock_frames)
  if not ok or not p then return nil end
  local a,b={-p[1],-p[2],-p[3]},{c[1]-p[1],c[2]-p[2],c[3]-p[3]}
  local err,v
  if nd and ns then
   local m=K.frame_map(a,b,ds,cs,{nd[1]-p[1],nd[2]-p[2],nd[3]-p[3]})
   err=math.sqrt((m[1]-ns[1])^2+(m[2]-ns[2])^2+(m[3]-ns[3])^2)
  end
  if i then
   local q=F.hop_prev
   if q and F.hop_prev_i==i-1 then v=K.frame_map(a,b,ds,cs,{(p[1]-q[1])*K.HOP_TPS,(p[2]-q[2])*K.HOP_TPS,(p[3]-q[3])*K.HOP_TPS}) end
   F.hop_prev,F.hop_prev_i=p,i
  end
  local function at(k,x) return x=='here' and p[k] or x end
  return K.frame_map(a,b,ds,cs,{at(1,h[1])-p[1],at(2,h[2])-p[2],at(3,h[3])-p[3]}),p,err,c,v
 end
 function F.start()
  tools.env.StartAutoPilot(tools.saved_lua(saved));return 'autopilot'
 end
 function F.step(i)
  if F.done then return nil end
  local d=tools.dock_state(saved)
  if d.docked then F.done=true;F.docked_at=i;return 'docked after='..i..' asked='..F.asked end
  if i>=limit then F.done=true;release();return 'limit stage='..F.stage..' distance='..tostring(d.distance and math.floor(d.distance))..' asked='..F.asked..gap() end
  -- {hold=true,after=N} for `hop`: from step N the autopilot is stopped and never started again (a test that puts
  -- the ship at its dock by a canonical fixture at that time, the op dock_warp, and waits for X4's landing)
  if type(hop)=='table' and hop.hold and hop.after and i>=hop.after and F.stage~='hold' then
   release()
   if tools.env.StopAutoPilot then tools.env.StopAutoPilot() end
   F.stage,F.hold_at='hold',i
   return 'hold after='..i..gap()
  end
  if F.stage=='fly' and d.distance and d.distance<(tools.dock_range or 9000) and i%30==0 then
   F.asked=F.asked+1
   if tools.C.RequestDockAt(tools.saved_id(saved),false) then F.stage='asked';F.granted=i;return 'asked after='..i..' distance='..math.floor(d.distance)..' asked='..F.asked end
  end
  -- The call's own answer is this client's capture (the request itself runs at its apply tick on every client): it is
  -- granted when the station has assigned a dock; none within two seconds: ask again.
  if F.stage=='asked' then
   if tools.assigned_dock() then F.stage='granted';F.granted=i;return 'granted after='..i..' distance='..tostring(d.distance and math.floor(d.distance))..' asked='..F.asked end
   if i>F.granted+60 then F.stage='fly' end
  end
  -- The approach: the autopilot to the dock the station assigned (X4's autopilot flies to a dock's approach position),
  -- started again every 20 s while the ship is not docked (an autopilot that arrived has ended).
  -- A detour under way: the approach again when the ship is beside the other ship (K.DETOUR_NEAR), when ten seconds
  -- brought it no nearer, or after 90 s. Beside means beside: the second pad of the wharf AVK-777 is on the outer face
  -- of its module, and a detour that ended 394 m off the other ship left the ship on the inner side, where the
  -- approach ran it into the module's wall and stopped (picture and dock-frame rows of run 20261120-004252).
  -- A hop under way: to each point in turn (within K.HOP_NEAR), then the throttle back to zero and the approach again;
  -- given up after K.HOP_LIMIT ticks or when the frames cannot be read.
  if F.stage=='hop' then
   local h,p,_,_,v=hop_delta(F.hop_points[F.hop_n],i)
   if not h or i-F.hop_at>=K.HOP_LIMIT then
    release()
    F.stage='unwind'
    return 'hop_stop n='..F.hops..' why='..(h and 'limit' or 'no_frames')..' after='..(i-F.hop_at)..gap()
   end
   local route=type(hop)=='table'
   if math.sqrt(h[1]^2+h[2]^2+h[3]^2)<((route and F.hop_n==#F.hop_points) and K.HOP_LAST_NEAR or K.HOP_NEAR) then
    F.hop_n=F.hop_n+1
    if F.hop_n>#F.hop_points then
     release()
     F.stage='unwind'
     return 'hop_end n='..F.hops..' after='..(i-F.hop_at)..gap()
    end
    return 'hop_point n='..F.hops..' point='..F.hop_n..' after='..(i-F.hop_at)..gap()
   end
   -- each axis wants a speed towards the point that shrinks with the distance (K.HOP_GAIN per second, at most
   -- K.HOP_SPEED); a key is held while the ship is slower than that and its opposite while faster: holding a key
   -- until the point is reached swung the ship 200 m past it in circles (run 20261120-011239)
   local function want(x) return math.max(-K.HOP_SPEED,math.min(K.HOP_SPEED,x*K.HOP_GAIN)) end
   local w={want(h[1]),want(h[2]),want(h[3])}
   v=v or {0,0,0}
   key(7,v[1]<w[1]-3);key(6,v[1]>w[1]+3);key(8,v[2]<w[2]-3);key(9,v[2]>w[2]+3)
   local up=v[3]<w[3]-3 and F.throttle<K.HOP_THROTTLE
   local down=v[3]>w[3]+3 and F.throttle>-K.HOP_THROTTLE
   key(13,up);key(14,down);F.throttle=F.throttle+(up and 1 or down and -1 or 0)
   if (i-F.hop_at)%60==0 then
    return string.format('stage=hop point=%d d=%.0f/%.0f/%.0f v=%.0f/%.0f/%.0f throttle=%d',F.hop_n,h[1],h[2],h[3],v[1],v[2],v[3],F.throttle)..gap()
   end
   return nil
  end
  -- ...the throttle back to zero (its key ticks counted), then the approach again from there
  if F.stage=='unwind' then
   if F.throttle==0 then
    release()
    -- after a route: the ship stands where the first ship's approach ended (X4's docking takes a ship within range of
    -- the dock's approach point); three seconds, then the dock key, five seconds, then the approach as before
    -- a route with hold=true: the ship stays where the route ended (a person who flew to the docking location and
    -- waits for X4's landing); the approach is not started again
    if type(hop)=='table' and hop.hold then F.stage,F.hold_at='hold',i;return 'hold'..gap() end
    if type(hop)=='table' then F.stage,F.settle_at='settle',i;return nil end
    F.stage='granted';F.granted=i-4;F.approach_gap=nil;return nil
   end
   local up,down=F.throttle<0,F.throttle>0     -- (held for this tick, let go at the next: every count is a tick held)
   key(13,up);key(14,down);F.throttle=F.throttle+(up and 1 or down and -1 or 0)
   return nil
  end
  if F.stage=='hold' then
   if (i-F.hold_at)%60==0 then return 'stage=hold after='..(i-F.hold_at)..gap() end
   return nil
  end
  if F.stage=='settle' then
   if i-F.settle_at==K.SETTLE_KEY then
    local ok,answer=pcall(function() return tools.C.QuickDock() end)
    return 'quickdock='..(ok and tostring(answer) or 'error')..' dock='..tostring(tools.assigned_dock())..gap()
   end
   if i-F.settle_at>=K.SETTLE_END then
    F.stage='granted';F.granted=i-4;F.approach_gap=nil
    return 'settle_end dock='..tostring(tools.assigned_dock())..gap()
   end
   return nil
  end
  if F.stage=='detour' then
   local left=tools.detour_left and tools.detour_left()
   local still=false
   if left and (i-F.detour_at)%300==0 then
    still=F.detour_last~=nil and left>F.detour_last-20
    F.detour_last=left
   end
   if (left and left<K.DETOUR_NEAR) or still or i-F.detour_at>=2700 then
    F.detour_last=nil
    F.stage='granted';F.granted=i-4;F.approach_gap=nil
    return 'detour_end n='..F.detours..' left='..tostring(left and math.floor(left))..' after='..(i-F.detour_at)..gap()
   end
   if i%150==0 then return 'stage=detour left='..tostring(left and math.floor(left))..gap() end
   return nil
  end
  if F.stage=='granted' and (i-F.granted)%600==5 then
   local dock=tools.assigned_dock()
   if not dock then   -- the station took the dock back (the ship never docked there): ask again
    F.stage='fly';F.last=nil
    return 'approach dock=nil distance='..tostring(d.distance and math.floor(d.distance))
   end
   local g=gap_value()
   -- Within a kilometre and no nearer: the hop over whatever stands between (up, across to the dock's far side)
   if hop and hop~=0 and (type(hop)~='table' or #hop>0) and F.hops<(type(hop)=='table' and 1 or 2) and g and g<=1000 and F.approach_gap and g>F.approach_gap-20 and tools.dock_frames then
    local seen,p,err,c=hop_delta({0,0,0})
    -- a route that starts from X4's landing pose waits until that pose is known
    local wait=false
    if seen and type(hop)=='table' and not (tools.dock_approach and tools.dock_approach[tonumber(tools.own_slot)]) then
     for _,q in ipairs(hop) do if q[1]=='approach' then wait=true end end
    end
    if wait then
     F.approach_gap=g;tools.env.StartAutoPilot(dock)
     return 'hop_wait why=no_approach'..gap()
    end
    if seen then
     F.hops=F.hops+1
     if tools.env.StopAutoPilot then tools.env.StopAutoPilot() end
     -- the far side: away from the station's centre, in the dock's plane
     local n=math.sqrt(c[1]^2+c[3]^2)
     local ox,oz=(n>0 and -c[1]/n or 0)+0,(n>0 and -c[3]/n or -1)+0     -- (+0: no "-0" in the row)
     F.stage,F.hop_at,F.hop_n,F.hop_prev='hop',i,1,nil
     -- a route given with the op (points of the dock's frame, 'here' for the ship's own value when the hop begins),
     -- or up and across to the far side
     if type(hop)=='table' then
      F.hop_points={}
      -- {'approach',dx,dy,dz}: that far from where X4's landing check wants the ship (tools.dock_approach, the op
      -- dock_approach, read beforehand)
      local a=tools.dock_approach and tools.dock_approach[tonumber(tools.own_slot)]
      for k,q in ipairs(hop) do
       if q[1]=='approach' then
        F.hop_points[k]=a and {a.pos[1]+(q[2] or 0),a.pos[2]+(q[3] or 0),a.pos[3]+(q[4] or 0)} or nil
       else
        F.hop_points[k]={q[1]=='here' and p[1] or q[1],q[2]=='here' and p[2] or q[2],q[3]=='here' and p[3] or q[3]}
       end
      end
     else
      F.hop_points={{p[1],K.HOP_HEIGHT,p[3]},{ox*K.HOP_OUT,K.HOP_HEIGHT,oz*K.HOP_OUT}}
     end
     local last=F.hop_points[#F.hop_points]
     return string.format('hop n=%d check=%s dock_seen=%.0f/%.0f/%.0f points=%d to=%.0f/%.0f/%.0f',F.hops,err and string.format('%.1f',err) or 'none',
      seen[1],seen[2],seen[3],#F.hop_points,last[1],last[2],last[3])..gap()
    end
   end
   -- (Within a kilometre and no nearer, the dock key does not land the ship: at 294 m off the second dock of AVK-777
   -- QuickDock gave the grant back, run q3 of 19 Nov; the first ship's approach ends 74 m off and its docking
   -- computer lands it.)
   if detour and detour~=0 and F.detours<3 and g and g>1000 and F.approach_gap and g>F.approach_gap-200 and tools.dock_neighbour then
    F.detours=F.detours+1
    local ok,via,info=pcall(tools.dock_neighbour)
    if ok and via then
     tools.env.StartAutoPilot(via)
     F.stage,F.detour_at='detour',i
     return 'detour n='..F.detours..' '..tostring(info)..' distance='..tostring(d.distance and math.floor(d.distance))..gap()
    end
    F.approach_gap=g   -- nobody docked to fly to: the approach as before
    tools.env.StartAutoPilot(dock)
    return 'approach dock='..tostring(dock)..' detour_failed='..tostring(ok and info or via)..gap()
   end
   F.approach_gap=g
   tools.env.StartAutoPilot(dock)
   return 'approach dock='..tostring(dock)..' distance='..tostring(d.distance and math.floor(d.distance))..gap()
  end
  -- An autopilot that ended short of the request's range (it stops where it loses its way: seen 16 km out): started
  -- again whenever ten seconds brought the ship no closer.
  if F.stage=='fly' and d.distance and i%300==0 then
   local was=F.last;F.last=d.distance
   if was and d.distance>was-500 then
    -- X4's autopilot to a station ends at a free place around it, which can be far outside the request's range (15 km,
    -- 16 Nov, with another ship already there). `via`: a slot whose ship is docked there: the autopilot to that ship
    -- ends beside the dock.
    F.again=(F.again or 0)+1
    if via and F.again%2==1 then tools.env.StartAutoPilot(tools.slot_lua(via));return 'autopilot via slot '..via..' again='..F.again..' distance='..math.floor(d.distance) end
    tools.env.StartAutoPilot(tools.saved_lua(saved))
    return 'autopilot again='..F.again..' distance='..math.floor(d.distance)
   end
  end
  if i%150==0 then return 'stage='..F.stage..' distance='..tostring(d.distance and math.floor(d.distance))..' asked='..F.asked..gap() end
  return nil
 end
 return F
end
-- Docking by X4's own AI, a test and debug tool (the op ai_dock_station): a player's own ship docked the way a fleet
-- ship docks when it is ordered to. X4's autopilot is not made to dock from end to end: aiscripts/move.autopilot.xml
-- flies to a dock's approach point and leaves the rest to the docking computer, which held the second ship of a run
-- 156 m over the wharf AVK-777's second pad (20 Nov). A fleet ship's Dock order runs aiscripts/order.dock.xml: the
-- dock's entry path, its dock path, the landing. X4 has an MD action that runs that same script for the player's own
-- ship, dock_player_ship_at (MD::DockPlayerShipAtAction 0x11337b0: the station is asked for a dock, 0x7d03e0; granted at
-- once, the ship's steering goes to the AI, 0xd9c920 sets the autopilot word, and the ship computer starts order.dock,
-- 0x731560); no stock script uses it. Here: the autopilot to the station as dock_station starts it; within the
-- request's range the autopilot is stopped and, K.AI_DOCK_SETTLE ticks later, the galaxy is signalled
-- 'x4lan_ai_dock' with the station: the signal travels as the slot's script calls do (the events family) and runs in
-- the slot's scope with the generation open (K.SLOT_SIGNALS), so our MD cue AiDock (native_gate_ui_replay.xml) runs
-- dock_player_ship_at as that slot's player, on every client at the same tick. The action asks once: a station whose
-- docks are all taken grants nothing and nothing flies, so with no dock assigned after K.AI_DOCK_AGAIN ticks it is
-- asked again. Done when the ship says it is docked ('docked after=N asked=K by=ai').
K.AI_DOCK_SETTLE,K.AI_DOCK_AGAIN,K.AI_DOCK_RANGE=15,600,9000
function K.ai_dock_signal(tools,saved)
 tools.env.SignalObject(tools.galaxy_lua(),'x4lan_ai_dock',tools.saved_lua(saved))
end
function K.ai_dock_fly(tools,saved,limit)
 local F={stage='fly',done=false,asked=0}
 local function gap()
  local ok,value=pcall(function() return tools.dock_gap and tools.dock_gap() end)
  return (ok and value) and ' dock_gap='..math.floor(value) or ''
 end
 function F.start()
  tools.env.StartAutoPilot(tools.saved_lua(saved));return 'autopilot'
 end
 function F.step(i)
  if F.done then return nil end
  local d=tools.dock_state(saved)
  local distance=tostring(d.distance and math.floor(d.distance))
  if d.docked then F.done=true;F.docked_at=i;return 'docked after='..i..' asked='..F.asked..' by=ai' end
  if i>=limit then F.done=true;return 'limit stage='..F.stage..' distance='..distance..' asked='..F.asked..' dock='..tostring(tools.assigned_dock())..gap() end
  if F.stage=='fly' then
   if d.distance and d.distance<(tools.dock_range or K.AI_DOCK_RANGE) and i%30==0 then
    tools.env.StopAutoPilot()
    F.stage,F.at='stopped',i
    return 'autopilot_stop distance='..distance
   end
   -- an autopilot that ended out of the request's range: started again whenever ten seconds brought the ship no closer
   if d.distance and i%300==0 then
    local was=F.last;F.last=d.distance
    if was and d.distance>was-500 then tools.env.StartAutoPilot(tools.saved_lua(saved));return 'autopilot again distance='..distance end
   end
   return nil
  end
  if F.stage=='stopped' then
   if i-F.at<K.AI_DOCK_SETTLE then return nil end
   F.asked=F.asked+1
   K.ai_dock_signal(tools,saved)
   F.stage,F.at='ai',i
   return 'ai_dock asked='..F.asked..' distance='..distance
  end
  -- the AI flies: a row every ten seconds; no dock assigned after K.AI_DOCK_AGAIN ticks: asked again
  local dock=tools.assigned_dock()
  if not dock and i-F.at>=K.AI_DOCK_AGAIN then
   F.stage,F.at='stopped',i-K.AI_DOCK_SETTLE
   return 'ai_dock_again dock=nil distance='..distance
  end
  if (i-F.at)%300==0 then return 'stage=ai dock='..tostring(dock)..' distance='..distance..' asked='..F.asked..gap() end
  return nil
 end
 return F
end
-- A walking player walks up to another player's figure: the op foot_meet, for runs without people. It reads both
-- figures' places in the station (tools.foot_pose) and steers by what its own walking does: forward for a few ticks,
-- the heading is the way its figure moved; when the other is not ahead it stops, holds a turn key for the ticks the
-- angle takes (K.FOOT_TURN: X4's key turn, 0.2333 rad a tick, seen live 16 Nov) and walks on. Which key turns which
-- way in the station's frame it learns from the first turn. Done when the other is `distance` away or nearer.
-- Keys: FP_MOVE_FORWARD 26, FP_TURN_LEFT 28, FP_TURN_RIGHT 29.
K.FOOT_TURN=0.2333
-- Within K.FOOT_NEAR of the place the walk ends in measured steps: turned to face it, then as many ticks of walking as
-- the distance takes at the speed the walk measured, then the heading read again from that step. Ten ticks of walking
-- at a time overshot a place 1.4 m off and circled it for minutes (room tests, 21 Nov 20:08).
K.FOOT_NEAR=2.5
-- `slot` may also be a function that answers the place to walk to ({x,y,z} in the station's frame, nil: not known yet).
function K.foot_walk(tools,slot,distance)
 local F={held={},sign=1,phase='walk',ticks=0,turns=0}
 local target=type(slot)=='function' and slot or function() return tools.foot_pose(slot) end
 local function key(id,on)
  on=on and true or false
  if (F.held[id] or false)~=on then F.held[id]=on;tools.gate.pilot_stock_inject('state',id,on and 1 or 0) end
 end
 local function release() key(26,false);key(28,false);key(29,false) end
 local function wrap(a) return (a+math.pi)%(2*math.pi)-math.pi end
 function F.start() return 'towards '..(type(slot)=='function' and 'a place' or 'slot '..tostring(slot))..' to '..tostring(distance)..' m' end
 function F.step(i)
  if F.done then return nil end
  local me,other=tools.foot_pose(tools.own_slot),target()
  if not me or not other then release();F.phase='walk';F.ticks=0;F.mark=nil;return i%150==0 and 'waits for both figures' or nil end
  local dx,dz=other.x-me.x,other.z-me.z
  local d=math.sqrt(dx*dx+dz*dz)
  if d<=distance and not other.crumb then
   F.done=true;release()
   return string.format('there after=%d distance=%.1f heading=%s bearing=%.2f turns=%d sign=%d me=%.1f/%.1f/%.1f other=%.1f/%.1f/%.1f',
    i,d,F.heading and string.format('%.2f',F.heading) or '-',math.atan2(dx,dz),F.turns,F.sign,me.x,me.y,me.z,other.x,other.y,other.z)
  end
  F.ticks=F.ticks+1
  -- near the place: a measured step (K.FOOT_NEAR), with the heading and the speed the walk has measured
  -- (also for a trail's crumbs: without it the walk circled each crumb 1.3 m off, run of 21 Nov 20:53)
  if F.phase=='walk' and d<K.FOOT_NEAR and F.heading and F.speed and F.speed>0.005 then
   local err=wrap(math.atan2(dx,dz)-F.heading)
   if math.abs(err)>0.15 then
    key(26,false)
    F.need=math.max(1,math.floor(math.abs(err)/K.FOOT_TURN+0.5));F.turns=F.turns+1
    local right=err*F.sign>0
    key(29,right);key(28,not right);F.phase='turn';F.ticks=0
    F.heading=wrap(F.heading+(err>0 and 1 or -1)*F.need*K.FOOT_TURN);F.near=true
   else
    key(26,true);F.phase='step';F.ticks=0;F.from={x=me.x,z=me.z}
    F.need=math.max(1,math.floor(math.max(d-distance*0.5,0.1)/F.speed+0.5))
   end
   return i%60==0 and string.format('distance=%.1f phase=near heading=%.2f speed=%.3f',d,F.heading,F.speed) or nil
  end
  if F.phase=='step' then
   if F.ticks>=F.need then key(26,false);F.phase='after';F.ticks=0 end
  elseif F.phase=='after' then           -- the step's way out of the records and the body's last slide
   if F.ticks>=8 then
    local vx,vz=me.x-F.from.x,me.z-F.from.z
    -- a step that moved the player: its heading; one that did not (something in the way): the walk's own way round
    -- (ten ticks of walking and its quarter turns), until a walk has measured the heading again
    if vx*vx+vz*vz>=0.0225 then F.heading=math.atan2(vx,vz) else F.heading=nil;F.stuck=(F.stuck or 0)+1 end
    F.phase='walk';F.ticks=0;F.mark=nil
   end
  elseif F.phase=='turn' then
   if F.ticks>F.need then
    key(28,false);key(29,false);F.phase='settle';F.ticks=0
   end
  elseif F.phase=='settle' then          -- the keys' way through the records: a few ticks until the turn has ended
   if F.ticks>=6 then F.phase='walk';F.ticks=0;F.mark=nil end
  else
   key(26,true)
   if not F.mark then F.mark={x=me.x,z=me.z};F.ticks=0 end
   if F.ticks>=10 then
    local vx,vz=me.x-F.mark.x,me.z-F.mark.z
    if vx*vx+vz*vz>=0.25 then
     local heading=math.atan2(vx,vz)
     F.speed=math.sqrt(vx*vx+vz*vz)/math.max(F.ticks,1)
     if F.turned and F.heading and math.abs(F.turned)>=0.4 then   -- which way the last turn went
      local went=wrap(heading-F.heading)
      if went*F.turned<0 and math.abs(went)>0.3 then F.sign=-F.sign end
     end
     F.heading=heading;F.turned=nil;F.stuck=0
     local err=wrap(math.atan2(dx,dz)-heading)
     if math.abs(err)>0.2 then
      key(26,false)
      F.need=math.max(1,math.floor(math.abs(err)/K.FOOT_TURN+0.5));F.turned=err;F.turns=F.turns+1
      local right=err*F.sign>0
      key(29,right);key(28,not right);F.phase='turn';F.ticks=0
     end
    else
     -- Walking and not moving: something is in the way. A quarter turn (one way, then the other), a few steps, and the
     -- steering takes over again from wherever that leads.
     F.stuck=(F.stuck or 0)+1
     if F.stuck%2==0 then
      key(26,false)
      F.dodge=not F.dodge
      F.need=math.floor(1.5708/K.FOOT_TURN+0.5);F.turned=nil;F.heading=nil;F.turns=F.turns+1
      key(29,F.dodge);key(28,not F.dodge);F.phase='turn';F.ticks=0
     end
    end
    F.mark={x=me.x,z=me.z};if F.phase=='walk' then F.ticks=0 end
   end
  end
  return i%60==0 and string.format('distance=%.1f phase=%s heading=%s turns=%d stuck=%d',d,F.phase,F.heading and string.format('%.2f',F.heading) or '-',F.turns,F.stuck or 0) or nil
 end
 F.release=release
 return F
end
-- A walking player follows its own figure's trail to a place the figure walks to by X4's NPC walk (the op walk_guided,
-- after avatar_guide): the figure's places are taken every K.TRAIL_EVERY ticks once it has moved K.TRAIL_STEP metres,
-- the player walks to each in turn (K.foot_walk; one within K.TRAIL_NEAR is passed: farther, the player cut the trail's
-- corners into the walkway's railing, 21 Nov 20:18), and when the figure has stood
-- still for K.TRAIL_STILL ticks and the trail is walked, to `final()` itself (the transporter's own point), done within
-- `distance`. A straight walk ran into the docking bay's wall 25 m short (room tests, 21 Nov).
K.TRAIL_EVERY,K.TRAIL_STEP,K.TRAIL_NEAR,K.TRAIL_STILL=10,0.7,1.0,90
-- Once the figure has stood still that long it is let go (`release`: it follows its player again) and the trail's
-- last K.TRAIL_FIGURE metres are dropped: the figure's own body stood there, beside the transporter, and the player
-- stopped a metre short of it (run of 21 Nov 20:25).
-- A transporter used as a person uses it (core op transporter_ui): the player stands on its pad, looks at its button
-- until X4's crosshair names the transporter's action, clicks, and travels with X4's TransporterMenu.
-- The button: a station's transporter booth (interiors_arg_airlock_01_body, a dock bay's connection27 holds its
-- part_main) is used by a part tagged trigger_interact ui on its side wall (ConnectionFordetail_l_button): in the
-- booth's frame 1.21 m along +x from part_main, 0.09 m along -z, 0.02 m up; its NPC stand place
-- (con_npc_stand_001, where the guide sent the figure) lies 0.1 m along -z from part_main, so the two give the booth's
-- facing: the button is 1.21 m to the left of the way in (+x is -z turned a quarter). Neither the pad nor the doors
-- carry an action.
-- The look: the player's facing (the gate's transform of its player, tools.foot_facing) turned to the button by the
-- turn keys (K.FOOT_TURN a tick, 13 degrees), then finer by the FP_YAW axis (K.UI_YAW_RATE a tick at 1.0) with the
-- facing read again, then the view tilted by the FP_PITCH axis (above 0 looks down, K.UI_PITCH_RATE a tick) to
-- K.UI_ABOVE above the button's angle and down one tick at a time from there with the crosshair read after each
-- (K.UI_SWEEP ticks), and with no hit a few degrees to either side and the sweep again. Both axes are walking inputs of
-- the slot (recorded, replayed on every client); the calibration run of 21 Nov 22:44 (rooms --calibrate) found
-- MOUSELOOK yaw and pitch moving nothing on foot, recorded or local, and every local look refused while walking.
-- The click is TARGETMOUSE_SELECT (state 0x82: a left click on foot).
-- The menu's row for the room is chosen with the table's own SelectRow and "Travel to" is the button's own handler.
K.UI_SETTLE,K.UI_FP_YAW,K.UI_FP_PITCH=6,0xd,0xe
-- An axis is pressed one tick at a time (set, then 0 the next tick, K.UI_PULSE_GAP ticks between): held longer, X4's view
-- speeds up (13 ticks of FP_PITCH at 1.0 looked straight down, run of 21 Nov 22:52). One pulse turns about
-- K.UI_YAW_RATE or tilts about K.UI_PITCH_RATE (the calibration's pictures: six pulses, five degrees).
K.UI_PULSE_GAP,K.UI_YAW_RATE,K.UI_PITCH_RATE=2,0.016,0.014
-- The sides: the button's place from the booth's data was 25 degrees right of the panel seen (run of 21 Nov 22:58:
-- the panel beside the booth's door frame, at the crosshair's height; run of 23:04: aimed at -0.36 and -0.48), so the
-- sweep begins there and then goes to either side.
K.UI_ABOVE,K.UI_SWEEP,K.UI_SIDES,K.UI_EYE,K.UI_FINES=0.16,40,{-0.36,-0.48,-0.24,-0.6,-0.12,0,0.12,0.24},0.0,40
K.BOOTH_BUTTON_SIDE,K.BOOTH_BUTTON_IN,K.BOOTH_BUTTON_UP=1.21,0.09,0.02
-- Where a booth's button is, from its transporter (t) and its stand place (w), both in the station's frame; nil when
-- the two give no facing.
function K.booth_button(t,w)
 if not t or not w then return nil end
 local ux,uz=w.x-t.x,w.z-t.z
 local n=math.sqrt(ux*ux+uz*uz)
 if n<0.02 then return nil end
 ux,uz=ux/n,uz/n
 return {x=t.x-uz*K.BOOTH_BUTTON_SIDE+ux*K.BOOTH_BUTTON_IN,y=t.y+K.BOOTH_BUTTON_UP,z=t.z+ux*K.BOOTH_BUTTON_SIDE+uz*K.BOOTH_BUTTON_IN}
end
K.UI_MOUSELOOK_YAW,K.UI_MOUSELOOK_PITCH,K.UI_SELECT=7,8,0x82
-- mode 'aim': the look alone (ends at 'aimed ...' or 'aim_failed ...'); 'use': the click and the menu alone (the
-- crosshair already on the button); else both.
function K.transporter_ui(tools,room,mode)
 local U={phase=mode=='use' and 'click' or 'look',ticks=0,turns=0,pitches=0,held={},seen={},rows=nil,errors=0}
 pcall(ffi.cdef,'typedef struct { uint32_t messageID; bool obstructed; } CrosshairMessage;')
 pcall(ffi.cdef,'CrosshairMessage GetCurrentCrosshairMessage();')
 pcall(ffi.cdef,'typedef struct { uint64_t poiID; UniverseID componentID; const char* messageType; const char* connectionName; bool isAssociative; bool isMissionTarget; bool isPriorityMissionTarget; bool showIndicator; bool hasAdditionalOffset; } MessageDetails3;')
 pcall(ffi.cdef,'MessageDetails3 GetMessageDetails3(const uint32_t messageid);')
 pcall(ffi.cdef,'typedef struct { UniverseID component; const char* connection; } UIComponentSlot;')
 pcall(ffi.cdef,'const char* GetTransporterLocationName(UIComponentSlot target);')
 local function key(id,on)
  on=on and true or false
  if (U.held[id] or false)~=on then U.held[id]=on;tools.gate.pilot_stock_inject('state',id,on and 1 or 0) end
 end
 local function release() key(28,false);key(29,false) end
 local function crosshair()
  local ok,m=pcall(function() return tools.C.GetCurrentCrosshairMessage() end)
  if not ok then U.errors=U.errors+1;U.error=U.error or tostring(m);return nil end
  if not m or m.messageID==0 then return nil end
  local id=m.messageID
  local okn,name,possible=pcall(tools.env.GetActionInfo,id)
  local okd,d=pcall(function() return tools.C.GetMessageDetails3(id) end)
  local r={id=id,name=okn and tostring(name) or '?',possible=okn and tostring(possible) or '?',
   comp=okd and tostring(d.componentID):gsub('ULL$','') or '?',conn=okd and d.connectionName~=nil and ffi.string(d.connectionName) or '?',
   kind=okd and d.messageType~=nil and ffi.string(d.messageType) or '?'}
  r.text=string.format('id=%d name=%s possible=%s comp=%s conn=%s type=%s obstructed=%s',r.id,r.name,r.possible,r.comp,r.conn,r.kind,tostring(m.obstructed))
  return r
 end
 local function wanted(r)
  local n,c=r.name:lower(),r.conn:lower()
  return n:find('transport') or c:find('transport') or c:find('button')
 end
 local function menu()
  for _,m in ipairs(tools.env.Menus or {}) do
   if type(m)=='table' and m.name=='TransporterMenu' and m.shown then return m end
  end
 end
 local function rows(m)
  local out={}
  local map=m.rowDataMap and m.defaulttable and m.rowDataMap[m.defaulttable] or {}
  for r,data in pairs(map) do
   if type(data)=='table' and data.target then
    local ok,name=pcall(function() return ffi.string(tools.C.GetTransporterLocationName(data.target)) end)
    out[#out+1]={row=r,data=data,name=ok and name or '?'}
   end
  end
  table.sort(out,function(a,b) return a.row<b.row end)
  return out
 end
 function U.start() return 'uses the transporter to '..tostring(room) end
 function U.release() release() end
 function U.step(i)
  if U.done then return nil end
  U.ticks=U.ticks+1
  if U.phase=='look' then
   local function wrap(x) return (x+math.pi)%(2*math.pi)-math.pi end
   -- an axis pressed one tick at a time, n times, then the view's settling
   if U.pulse then
    local P=U.pulse
    if P.on then
     tools.gate.pilot_stock_inject('range',P.axis,0);P.on=false;U.ticks=0
     return nil
    end
    if U.ticks<K.UI_PULSE_GAP then return nil end
    if P.left>0 then P.left=P.left-1;tools.gate.pilot_stock_inject('range',P.axis,P.value);P.on=true;U.ticks=0;return nil end
    U.pulse=nil;U.ticks=0;U.wait=K.UI_SETTLE;return nil
   end
   if U.wait and U.ticks<U.wait then return nil end
   U.wait=nil
   if U.turning then key(28,false);key(29,false);U.turning=nil;U.ticks=0;U.wait=K.UI_SETTLE;return nil end
   local function pulse(axis,value,n)
    U.pulse={axis=axis,value=value,left=math.max(1,math.floor(n+0.5)),on=false};U.ticks=K.UI_PULSE_GAP
   end
   if not U.button then
    U.button=K.booth_button(tools.transporter_guided and tools.transporter_guided() or nil,tools.guide_place and tools.guide_place[tonumber(tools.own_slot)] or nil)
    if not U.button then U.done=true;return 'aim_failed no booth button (transporter or stand place unknown)' end
    U.stage='turn';U.side=1;U.tilted=0
   end
   local yaw,me
   if tools.foot_facing then yaw,me=tools.foot_facing() end
   if not yaw or not me then U.done=true;return 'aim_failed no facing (a gate without the facing)' end
   local dx,dz=U.button.x-me.x,U.button.z-me.z
   local flat=math.sqrt(dx*dx+dz*dz)
   local err=wrap(math.atan2(dx,dz)+K.UI_SIDES[U.side]-yaw)
   if U.stage=='turn' then
    -- the keys, as near as one tick allows
    if math.abs(err)>K.FOOT_TURN*0.5 and U.turns<8 then
     local n=math.max(1,math.floor(math.abs(err)/K.FOOT_TURN+0.5))
     U.turns=U.turns+1
     if U.last_err and math.abs(err)>math.abs(U.last_err)+0.05 then U.sign=-(U.sign or 1) end
     U.last_err=err
     local right=err*(U.sign or 1)>0
     key(29,right);key(28,not right);U.turning=true;U.ticks=0;U.wait=n
     return nil
    end
    U.stage='fine';U.fines=0
    return nil
   elseif U.stage=='fine' then
    -- the yaw axis, with the facing read after each try (its rate a pulse measured from the try before)
    if math.abs(err)>0.012 and U.fines<K.UI_FINES then
     if U.fine_from then
      local turned=wrap(yaw-U.fine_from.yaw)
      if math.abs(turned)>0.002 then U.yaw_rate=math.abs(turned)/U.fine_from.n end
     end
     local rate=U.yaw_rate or K.UI_YAW_RATE
     local n=math.min(8,math.max(1,math.floor(math.abs(err)/rate+0.5)))
     U.fines=U.fines+1;U.fine_from={yaw=yaw,n=n}
     pulse(K.UI_FP_YAW,err>0 and 1 or -1,n)
     return nil
    end
    -- then down to a little above the button's angle (the eye at the player's place, K.UI_EYE above it)
    U.want_pitch=math.atan2(me.y+K.UI_EYE-U.button.y,flat)
    U.stage='sweep';U.swept=0
    local pre=math.max(0,U.want_pitch-K.UI_ABOVE)/K.UI_PITCH_RATE
    if pre>=1 then pulse(K.UI_FP_PITCH,1,pre);U.tilted=math.floor(pre+0.5) end
    return string.format('faces the button at %.1f/%.1f/%.1f distance=%.2f down=%.2f error=%.3f turns=%d fines=%d yaw_rate=%s side=%.2f',
     U.button.x,U.button.y,U.button.z,flat,U.want_pitch,err,U.turns,U.fines,U.yaw_rate and string.format('%.4f',U.yaw_rate) or '-',K.UI_SIDES[U.side])
   end
   -- the sweep: read, then one tick further down
   local r=crosshair()
   local row
   if r and not U.seen[r.text] then U.seen[r.text]=true;row=string.format('sees down=%d side=%d %s',U.tilted,U.side,r.text) end
   if r and wanted(r) then
    U.phase='click';U.ticks=0;U.hit=r
    if mode=='aim' then U.done=true end
    return string.format('aimed down=%d side=%d %s',U.tilted,U.side,r.text)
   end
   if U.swept>=K.UI_SWEEP then
    -- back up, and the next side
    if U.tilted>0 then pulse(K.UI_FP_PITCH,-1,U.tilted) end
    U.tilted=0;U.side=U.side+1
    if not K.UI_SIDES[U.side] then U.done=true;return 'aim_failed sides='..#K.UI_SIDES..' seen='..(function() local n=0 for _ in pairs(U.seen) do n=n+1 end return n end)()..' reads_failed='..U.errors..(U.error and ' error='..U.error or '') end
    U.stage='turn';U.turns=0;U.last_err=nil;U.fine_from=nil
    return row or 'swept '..K.UI_SWEEP..' ticks down, nothing: the next side '..K.UI_SIDES[U.side]
   end
   U.swept=U.swept+1;U.tilted=U.tilted+1
   pulse(K.UI_FP_PITCH,1,1)
   return row
  elseif U.phase=='click' then
   -- the click as this client's own input (a presentation id), else as a recorded one (a replay id: X4's handler of
   -- the click, 71c640, then runs it on every client in the click's slot)
   if U.ticks==1 then
    U.how=tools.gate.pilot_stock_local('state',K.UI_SELECT,1)==1 and 'local' or tools.gate.pilot_stock_inject('state',K.UI_SELECT,1)==1 and 'record' or 'refused'
    return 'click '..U.how
   end
   if U.ticks==3 then
    if U.how=='local' then tools.gate.pilot_stock_local('state',K.UI_SELECT,0) elseif U.how=='record' then tools.gate.pilot_stock_inject('state',K.UI_SELECT,0) end
    return nil
   end
   local m=menu()
   if m then U.phase='menu';U.ticks=0;U.menu=m;return 'menu open from='..tostring(m.transportercomponent)..'/'..tostring(m.transporterconnection) end
   if U.ticks>=240 then U.done=true;return 'no menu after the click '..(U.hit and U.hit.text or '') end
   return nil
  elseif U.phase=='menu' then
   if U.ticks<20 then return nil end
   local m=U.menu
   if U.ticks==20 then
    local list=rows(m);U.rows=list
    local names={}
    for _,x in ipairs(list) do names[#names+1]=x.row..':'..x.name..(x.data.issubtarget and '(sub)' or '')..(x.data.hassubentries and '(+)' or '') end
    for _,x in ipairs(list) do if x.name==room then U.want=x end end
    if not U.want then
     -- a room inside a category that is closed: the category's own "+" (buttonExtendCategory), then the rows again
     for _,x in ipairs(list) do
      if x.data.hassubentries and not U.extended and type(m.buttonExtendCategory)=='function' then
       U.extended=true;pcall(m.buttonExtendCategory,tostring(x.data.target.component));U.ticks=0
       return 'rows '..table.concat(names,' ')..' -> opens '..x.name
      end
     end
     U.done=true;return 'no row '..tostring(room)..': '..table.concat(names,' ')
    end
    local ok=pcall(tools.env.SelectRow,m.defaulttable,U.want.row)
    return 'selects row='..U.want.row..' '..U.want.name..' ok='..tostring(ok)..' rows '..table.concat(names,' ')
   end
   if U.ticks==50 then
    local sel=m.currentselection and m.currentselection.target
    if not (sel and U.want and tostring(sel.component)==tostring(U.want.data.target.component) and ffi.string(sel.connection)==ffi.string(U.want.data.target.connection)) then
     -- the selection did not reach the menu's row handler: the handler as the table calls it
     Helper.currentTableRow[m.defaulttable]=U.want.row
     pcall(m.onRowChanged,U.want.row,U.want.data)
     return 'selection by the row handler'
    end
    return nil
   end
   if U.ticks==80 then
    local ok,why=pcall(m.buttonGoTo)
    U.done=true
    return 'there pressed travel ok='..tostring(ok)..(ok and '' or ' '..tostring(why))..' to='..U.want.name
   end
   return nil
  end
 end
 return U
end
-- The figure counts as arrived only when it stands still within K.TRAIL_ARRIVED of the place the walk ends (or has stood
-- still K.TRAIL_GIVE_UP ticks): it also stands still on its way, at a door that opens before it (run of 21 Nov 20:45
-- let it go at a door, 7 m short, and the door closed between the player and the booth).
K.TRAIL_FIGURE,K.TRAIL_ARRIVED,K.TRAIL_GIVE_UP=2.0,3.0,900
function K.foot_follow(tools,figure,final,distance,release)
 local T={crumbs={},still=0,tick=0}
 local function arrived()
  if T.still<K.TRAIL_STILL then return false end
  if T.still>=K.TRAIL_GIVE_UP then return true end
  local goal,at=final(),T.last
  return goal and at and math.sqrt((goal.x-at.x)^2+(goal.z-at.z)^2)<=K.TRAIL_ARRIVED or false
 end
 local function target()
  local me=tools.foot_pose(tools.own_slot)
  if not T.released and arrived() then
   T.released=true
   if release then pcall(release) end
   local at=T.last
   while at and T.crumbs[#T.crumbs] and math.sqrt((T.crumbs[#T.crumbs].x-at.x)^2+(T.crumbs[#T.crumbs].z-at.z)^2)<=K.TRAIL_FIGURE do table.remove(T.crumbs) end
  end
  while me and T.crumbs[1] and math.sqrt((T.crumbs[1].x-me.x)^2+(T.crumbs[1].z-me.z)^2)<=K.TRAIL_NEAR do table.remove(T.crumbs,1) end
  if T.crumbs[1] then return T.crumbs[1] end
  if T.released then return final() end
  return nil
 end
 local walk=K.foot_walk(tools,target,distance)
 local step=walk.step
 function walk.step(i)
  T.tick=T.tick+1
  -- (none once the figure is let go: it walks back to its player then, and its way back is no trail)
  if T.tick%K.TRAIL_EVERY==0 and not T.released then
   local f=figure()
   if f then
    local last=T.crumbs[#T.crumbs] or T.last
    if not last or math.sqrt((f.x-last.x)^2+(f.z-last.z)^2)>=K.TRAIL_STEP then
     T.crumbs[#T.crumbs+1]={x=f.x,y=f.y,z=f.z,crumb=true};T.last=T.crumbs[#T.crumbs];T.still=0
    else T.still=T.still+K.TRAIL_EVERY end
   end
  end
  local out=step(i)
  if out and i%60==0 then out=out..' crumbs='..#T.crumbs..' still='..T.still end
  return out
 end
 return walk
end
-- A chain of script ops (the op chain): steps one after the other, each begun when the one before it has ended, for
-- runs whose steps wait for the game (fly and dock, then buy, then undock, then dock elsewhere...). A step is
--   {NAME,'wait',TICKS}
--   {NAME,'op',OP,...}                        the op, once (an op that runs over many ticks: to its last tick)
--   {NAME,'until',PATTERN,OP,...}             a many-tick op (dock_station, foot_meet) to its first answer that holds
--                                             PATTERN (a Lua pattern); none by its last tick ends the chain
--   {NAME,'poll',PATTERN,EVERY,TICKS,OP,...}  a reading op every EVERY ticks until its answer holds PATTERN; none within
--                                             TICKS ends the chain
--   {NAME,'try',PATTERN,EVERY,TICKS,OP,...}   the same, but none within TICKS is a row 'gave_up' and the chain goes on
--                                             (a check of something the later steps do not need)
-- Every answer is a row part 'step=NAME n=K at=I ok=true|false ANSWER' (I: ticks since the chain's start); the end is
-- 'chain_end ok=true steps=N/N' or 'chain_end ok=false steps=K/N why=NAME:error|never|limit'. A chain's ops run in the
-- presentation scope, where a person's click runs; a canonical op (a fixture of every client's plan) cannot be a step.
function K.chain(tools,steps,limit)
 local C={at=1,since=0,done=false}
 local compiled={}
 local function compile(step,from)
  local e={0}
  for i=from,#step do e[#e+1]=step[i] end
  if K.CANONICAL[e[2]] then error('ui replay script: a chain holds no canonical op ('..tostring(e[2])..')') end
  local by=K.script({e},0,tools)
  local last=0
  for t in pairs(by) do if t>last then last=t end end
  return by,last
 end
 for n,step in ipairs(steps or {}) do
  local kind,c=step[2],nil
  if kind=='wait' then c={ticks=step[3]}
  elseif kind=='op' then c={};c.by,c.last=compile(step,3)
  elseif kind=='until' then c={pattern=step[3]};c.by,c.last=compile(step,4)
  elseif kind=='poll' or kind=='try' then c={pattern=step[3],every=math.max(1,step[4]),ticks=step[5],polls=true,goes_on=kind=='try'};c.by=compile(step,6)
  else error('ui replay script: unknown chain step kind '..tostring(kind)) end
  c.kind,c.name=kind,tostring(step[1])
  compiled[n]=c
 end
 function C.step(i)
  if C.done then return nil end
  local out={}
  local function stop(why)
   C.done=true
   out[#out+1]='chain_end ok='..(why and 'false' or 'true')..' steps='..(why and C.at-1 or #compiled)..'/'..#compiled..(why and ' why='..why or '')
  end
  local c=compiled[C.at]
  if not c then stop()
  else
   local j,finished,failed=C.since,false,nil
   C.since=j+1
   if c.kind=='wait' then finished=j>=c.ticks
   else
    local due
    if c.polls then due=j%c.every==0 and c.by[0] or nil else due=c.by[j] end
    for _,x in ipairs(due or {}) do
     local ok,value=pcall(x.call)
     local hit=c.pattern and ok and value~=nil and tostring(value):find(c.pattern) and true or false
     if not ok then failed='error' end
     if hit then finished=true end
     -- a poll's misses: every tenth is a row, so a wait that never ends shows what it saw
     if not (x.quiet and ok and value==nil) and (not c.polls or hit or not ok or j%(c.every*10)==0) then
      out[#out+1]=string.format('step=%s n=%d at=%d ok=%s %s',c.name,C.at,i,tostring(ok),tostring(value))
     end
    end
    if c.kind=='op' then finished=j>=c.last
    elseif c.kind=='until' and not finished and j>=c.last then failed='never'
    elseif c.polls and not finished and j>=c.ticks then
     if c.goes_on then finished=true;out[#out+1]=string.format('step=%s n=%d at=%d ok=true gave_up',c.name,C.at,i) else failed='never' end
    end
   end
   if failed then stop(c.name..':'..failed)
   elseif finished then
    C.at,C.since=C.at+1,0
    if C.at>#compiled then stop() end
   elseif i>=limit then stop(c.name..':limit') end
  end
  return #out>0 and table.concat(out,' | ') or nil
 end
 return C
end
-- Named script actions for runs without people; arguments are saved IDs or slots, resolved when they run.
-- plan: list of {tick, op, ...}, ticks relative to the pilot window start.
-- The station fixtures' module blueprints (the MD cue StationKit names the same wares for the player faction).
K.STATION_BLUEPRINTS={'module_arg_conn_base_01','module_arg_conn_cross_01','module_arg_stor_container_s_01','module_arg_dock_m_01',
 'module_gen_prod_energycells_01','module_arg_hab_s_01','module_arg_pier_l_01','module_arg_def_claim_01'}
-- One slot ship per player faction, in slot order: slot 1's alone in coop, every slot's in factions mode.
function K.faction_ships(tools)
 local seen,ships={},{}
 for k=1,tools.slots or 1 do
  local ok,ship=pcall(tools.slot_lua,k)
  local owner=ok and tools.env.GetComponentData(ship,'owner') or nil
  if owner and not seen[owner] then seen[owner]=true;ships[#ships+1]=ship end
 end
 return ships
end
function K.script(plan,start,tools)
 local by={}
 for _,e in ipairs(plan or {}) do
  local at,op=start+e[1],e[2]
  local call
  if op=='travel' then call=function() tools.C.StartPlayerActivity('travel');return 'ffi' end
  elseif op=='travel_stop' then call=function() tools.C.StopPlayerActivity('travel');return 'ffi' end
  elseif op=='travel_key' then call=function() return tools.gate.pilot_stock_inject('action',0x12f,0) end
  -- This client's scan mode key ({tick,'scan_key'}: on, the next one off), as travel_key.
  elseif op=='scan_key' then call=function() return tools.gate.pilot_stock_inject('action',0x130,0) end
  -- This client's player starts the deep scan of its target ({tick,'scan_start'}), through the call the UI's scan
  -- action ends in (the action's own key needs the UI to have offered it: a person's hands).
  elseif op=='scan_start' then call=function() return tostring(tools.gate.scan_start())..' '..tostring(tools.gate.scan_diag and tools.gate.scan_diag()) end
  -- Read-only: what X4's can-scan test finds for this client's player and its target ({tick,'scan_diag'}).
  elseif op=='scan_diag' then call=function() return tostring(tools.gate.scan_diag()) end
  elseif op=='scan_trace' then call=function() return tostring(tools.gate.scan_trace()) end
  -- This client's scan mode on or off ({tick,'scan_mode',1|0}): the call X4's own mode switch ends in.
  elseif op=='scan_mode' then call=function()
    if e[3]==1 or e[3]==true then tools.C.StartPlayerActivity('scan') else tools.C.StopPlayerActivity('scan') end
    return 'ffi' end
  elseif op=='target_slot' then call=function() return tools.C.SetSofttarget(tools.slot_id(e[3]),'') end
  elseif op=='target_none' then call=function() return tools.env.RemoveSofttarget() end
  elseif op=='match' then call=function() return tools.gate.pilot_stock_inject('state',0x5b,e[3]) end
  elseif op=='input' then call=function() return tools.gate.pilot_stock_inject(e[3],e[4],e[5]) end
  -- {tick,'input_local',KIND,ID,VALUE}: the same as this client's own key or mouse (pstk_local_press: X4's handler on
  -- this client, then the record as a person's input is captured), for presentation ids such as the camera's look.
  elseif op=='input_local' then call=function() return tools.gate.pilot_stock_local(e[3],e[4],e[5]) end
  -- This client's guns without a person: {tick,'aim_fire',1} aims with the aim assist at its player's target and holds
  -- the primary trigger, {tick,'aim_fire',0} lets go (gate.pilot_aim_script). A trigger injected as an input state
  -- (state 24) is a key press: it counts only in the window that has the focus (two windows on one PC), so a plan
  -- cannot rely on it. {tick,'aim_fire',1,X,Y,Z}: a manual aim at that point of the ship's frame instead (metres right, up,
  -- ahead), as a person's crosshair on it: a window without the focus captures no aim (lockstep_pilot_aim.h), and a gun at
  -- rest shoots along its mount, past a small thing close by (a suit's repair laser at a claim signal 4 m ahead, live c2c).
  elseif op=='aim_fire' then call=function()
    local on=e[3]==1 or e[3]==true
    if on and e[6] then return tools.gate.pilot_aim_script(1,e[4],e[5],e[6],1) end
    return tools.gate.pilot_aim_script(on and 2 or 0,0,0,1000,on and 1 or 0) end
  -- The flight families: autopilot by Lua (no guidance needed) and by the FFI toggle, dock, undock, weapon groups.
  elseif op=='ap_to' then call=function() return tools.env.StartAutoPilot(tools.slot_lua(e[3])) end
  -- The autopilot to any saved object, as the map's "Autopilot" on a station (e.g. to get within docking range first).
  elseif op=='ap_obj' then call=function() return tools.env.StartAutoPilot(tools.saved_lua(e[3])) end
  elseif op=='ap_stop' then call=function() return tools.env.StopAutoPilot() end
  elseif op=='ap_toggle' then call=function() return tools.C.ToggleAutoPilot(false) end
  -- Plot course (the map's menu.plotCourse: SetGuidance with no offset) to a saved object for this client's own
  -- player, {tick,'guidance_set',SAVED} (Remove course: guidance_end below). A guidance is its client's
  -- own: the calls stay local (players 2..N: native_gate_player_guidance.xml). Read only, {tick,'guidance_look'
  -- [,SAVED]}: the target of this client's guidance (none), the distance to SAVED, whether its autopilot flies.
  elseif op=='guidance_set' then call=function()
    pcall(ffi.cdef,'typedef struct { float x; float y; float z; float yaw; float pitch; float roll; } UIPosRot;')
    pcall(ffi.cdef,'void SetGuidance(UniverseID componentid, UIPosRot offset);')
    tools.C.SetGuidance(tools.saved_id(e[3]),ffi.new('UIPosRot',0));return 'set' end
  elseif op=='guidance_look' then call=function()
    pcall(ffi.cdef,'bool IsAutoPilotActive(void);')
    local target=tools.env.GetActiveGuidanceMissionComponent and tools.env.GetActiveGuidanceMissionComponent()
    local text='guidance='..(target and tostring(target)~='0' and (tostring(tools.env.GetComponentData(target,'idcode') or '?')..'/'..
      tostring(ConvertStringTo64Bit(tostring(target)))) or 'none')
    if e[3] then local d=tools.dock_state(e[3]);text=text..' distance='..tostring(d.distance and math.floor(d.distance)) end
    return text..' autopilot='..tostring(tools.C.IsAutoPilotActive()) end
  elseif op=='quickdock' then call=function() return tools.C.QuickDock() end
  elseif op=='dock_at' then call=function() return tools.C.RequestDockAt(tools.saved_id(e[3]),false) end
  -- The spacesuit, out and home, through the calls the stock menus make (each is captured as a person's click is:
  -- lockstep_ui_compslot.h 'terminal', lockstep_ui_replay.h's dock family), for this client's own player, slot 1 too:
  --  {tick,'suit_out'}: the transporter menu's "Use Spacesuit" while the player walks in its ship
  --    (TransportPlayerToTarget with a zone, the menu's spacesuit target); the ship is noted as the way home.
  --  {tick,'dock_home'}: the ship menu's "Request Docking" on the ship left (menu.buttonDockRequest's RequestDockAt).
  --  {tick,'target_home'}: that ship as the soft target, where a click runs; {tick,'input','action',0xaf,0} is the
  --    dock key then.
  --  {tick,'suit_home'[,limit]}: dock_home, then the suit flies to the ship by its flight keys until X4 has taken the
  --    player in, at most `limit` ticks (900): K.suit_fly.
  elseif op=='suit_out' then call=function()
    local target,zone=tools.suit_target();tools.C.TransportPlayerToTarget(target);return 'zone='..tostring(zone) end
  elseif op=='dock_home' then call=function() local home=tools.home_id();return tostring(home)..'='..tostring(tools.C.RequestDockAt(home,false)) end
  elseif op=='target_home' then call=function() return tools.C.SetSofttarget(tools.home_id(),'') end
  -- {tick,'suit_to',NAME,OWNER[,limit]}: as suit_home, to the ship of that name and owner (a claimed ship: 'player'), or
  -- of that saved ID (16 hex digits; OWNER then unused),
  -- looked up at the op's tick. Live (24 Nov, c1 and c2: player 1 as much as player 2) the flight keys took a suit 47 m
  -- in front of a corvette AWAY from it at 1.7 m/s: the request is what counts here, suit_dock stands in for the flight.
  elseif op=='suit_to' then
    local fly=K.suit_fly(tools,e[5] or 900,function()
     if type(e[3])=='string' and e[3]:match('^%x+$') and #e[3]==16 then return tonumber(tools.gate.ui_replay_saved(e[3])) end   -- a saved ID
     for _,object in ipairs(tools.env.GetContainedObjectsByOwner(e[4] or 'player') or {}) do
      if tools.env.GetComponentData(object,'name')==e[3] then return tools.env.ConvertIDTo64Bit(object) end
     end
    end)
    call=fly.start
    for i=1,e[5] or 900 do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op='suit_fly',call=function() return fly.step(i) end,quiet=true}
    end
  elseif op=='suit_home' then
    local fly=K.suit_fly(tools,e[3] or 900)
    call=fly.start
    for i=1,e[3] or 900 do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op='suit_fly',call=function() return fly.step(i) end,quiet=true}
    end
  -- Docking at a station (K.dock_fly): {tick,'dock_station',SAVED[,limit]} for this client's own ship; its steps run in
  -- the presentation scope, where a person's request runs (the check's answer is about this client's own ship there).
  -- {tick,'dock_kit',SLOT}: test fixture, canonical: X4's docking computer on that slot's ship (MD DockKit).
  -- A fifth entry is a plan of its own whose ticks count from the docking ({ticks after it, op, ...}; within `limit`):
  -- what the player does once the ship is docked, whenever that is. A sixth: a slot whose (docked) ship the approach
  -- flies to when the autopilot to the station ends out of the request's range. A seventh: K.dock_fly's detour, an
  -- eighth its hop (both default on; false or 0: none).
  elseif op=='dock_station' then
    local fly=K.dock_fly(tools,e[3],e[4] or 3000,e[6],e[7]==nil and true or e[7],e[8]==nil and true or e[8])
    local after=e[5] and K.script(e[5],0,tools) or {}
    call=fly.start
    for i=1,e[4] or 3000 do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op='dock_station',quiet=true,scoped=true,call=function()
      local result=fly.step(i)
      local due=fly.docked_at and after[i-fly.docked_at]
      if not due then return result end
      local parts={result}
      for _,x in ipairs(due) do
       local ok,value=pcall(x.call)
       if not (x.quiet and ok and value==nil) then
        parts[#parts+1]='docked+'..(i-fly.docked_at)..' '..tostring(x.op)..' ok='..tostring(ok)..' '..tostring(value)
       end
      end
      return table.concat(parts,' | ')
     end}
    end
  -- Docking by X4's own AI (K.ai_dock_fly, a test and debug tool): {tick,'ai_dock_station',SAVED[,limit]} for this
  -- client's own ship, as dock_station (a chain step 'until' '^docked'); the ship computer flies the fleet ships' dock
  -- order. {tick,'ai_dock',SAVED}: the AI dock alone, once (the ship must be within the request's range; the
  -- autopilot stopped first: ap_stop).
  elseif op=='ai_dock_station' then
    local fly=K.ai_dock_fly(tools,e[3],e[4] or 3000)
    call=fly.start
    for i=1,e[4] or 3000 do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op='ai_dock_station',quiet=true,scoped=true,call=function() return fly.step(i) end}
    end
  elseif op=='ai_dock' then call=function() K.ai_dock_signal(tools,e[3]);return 'signalled' end
  elseif op=='dock_kit' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','DockKit',{ship=tools.slot_lua(e[3])});return 'lua' end
  -- {tick,'every',STRIDE,LAST,{{OP,...},...}}: each of those ops at tick, tick+STRIDE, ... up to LAST (a plan's
  -- regular readings in one entry); the ops are any of this list's, each logged under its own name.
  elseif op=='every' then
    local expanded={}
    for t=e[1],e[4],math.max(1,e[3]) do
     for _,inner in ipairs(e[5] or {}) do
      local entry={t}
      for i=1,#inner do entry[#entry+1]=inner[i] end
      expanded[#expanded+1]=entry
     end
    end
    for tick,entries in pairs(K.script(expanded,start,tools)) do
     by[tick]=by[tick] or {}
     for _,entry in ipairs(entries) do by[tick][#by[tick]+1]=entry end
    end
    call=function() return 'readings='..#expanded end
  -- {tick,'chain',LIMIT,{STEP,...}}: K.chain, one step action a tick for at most LIMIT ticks.
  elseif op=='chain' then
    local chain=K.chain(tools,e[4],e[3])
    call=function() return 'steps='..#(e[4] or {}) end
    for i=1,e[3] do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op='chain',quiet=true,scoped=true,call=function() return chain.step(i) end}
    end
  -- Test fixture, canonical ({tick,'wallet',CREDITS} in every client's plan at the same tick): that much money for the
  -- player's account, paid by MD on every client (cue Wallet), so a scripted run can buy.
  elseif op=='wallet' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Wallet',{credits=e[3]});return 'lua' end
  -- Test fixtures of the station tests, canonical (in every client's plan at the same tick), by MD on every client:
  --  {tick,'station_kit',SLOT[,BUILDER]}: the blueprints of a few station modules for the player faction
  --    and, with BUILDER true, a construction vessel of the player's near that slot's ship (cue StationKit).
  --  {tick,'station_wares',AMOUNT}: that much building material in every player station's build storage (StationWares).
  --  {tick,'station_look'}: read only: the player faction's stations as the map lists them (tools.station_look).
  --  In factions mode the fixtures are the faction's whose slot is named; station_wares and builder_warp go to every
  --  player faction (one event per faction, K.faction_ships). A slot's own faction's blueprints:
  --  {tick,'station_blueprints',SLOT}, canonical (every client's plan): the same wares by X4's LearnBlueprint inside
  --  that slot's scope (S.slot_library), where the library is that faction's.
  elseif op=='station_kit' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','StationKit',{ship=tools.slot_lua(e[3]),builder=e[4] and 1 or 0});return 'lua' end
  elseif op=='station_blueprints' then call=function(S)
    return S.slot_library(at,e[3],'station_blueprints',{blueprints=K.STATION_BLUEPRINTS}) end
  elseif op=='station_wares' then call=function()
    for _,ship in ipairs(K.faction_ships(tools)) do tools.env.AddUITriggeredEvent('X4LAN_UiReplay','StationWares',{amount=e[3],ship=ship}) end
    return 'lua' end
  --  {tick,'builder_warp'}: every construction vessel of the player's that is on its way to a player station is put
  --    3 km from it (cue BuilderWarp).
  elseif op=='builder_warp' then call=function()
    for _,ship in ipairs(K.faction_ships(tools)) do tools.env.AddUITriggeredEvent('X4LAN_UiReplay','BuilderWarp',{ship=ship}) end
    return 'lua' end
  elseif op=='station_look' then call=function() return tools.station_look() end
  --  {tick,'station_ready',SLOT[,EAST]}: a standing, producing station of the player faction with a manager and an
  --    account below that slot's ship (EAST metres aside), an Argon construction vessel to hire and a freighter of
  --    the player's near it (cue StationReady).
  elseif op=='station_ready' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','StationReady',{ship=tools.slot_lua(e[3]),east=e[4] or 0});return 'lua' end
  -- Read only: what a player's ship carries: {tick,'cargo'[,WARE[,SLOT]]} (this client's own without a slot). The row
  -- begins with that ware's amount ('amount=N ware=WARE', 0 when the ship has none), then every ware, then the account.
  elseif op=='cargo' then call=function()
    local ship=type(e[4])=='string' and tools.saved_lua(e[4]) or tools.slot_lua(e[4] or tools.own_slot)   -- a saved ID: a fleet ship
    local cargo=tools.env.GetComponentData(ship,'cargo')
    if type(cargo)~='table' then cargo={} end
    local names,parts={},{}
    for ware in pairs(cargo) do names[#names+1]=tostring(ware) end
    table.sort(names)
    for _,ware in ipairs(names) do parts[#parts+1]=ware..'='..tostring(cargo[ware]) end
    return (e[3] and 'amount='..tostring(cargo[e[3]] or 0)..' ware='..tostring(e[3])..' ' or '')..'wares='..#names..' '..table.concat(parts,' ')..
     (#parts>0 and ' ' or '')..'money='..tostring(tools.env.GetPlayerMoney()) end
  -- The transporter (menu_transporter.lua's calls) for this client's own player: {tick,'transport_list'} logs the
  -- targets X4 offers from where the player is; {tick,'transport_to',N} is the menu's "Travel to" on target N of that
  -- list (TransportPlayerToTarget); {tick,'transport_named',TEXT}: the first target whose name or class holds TEXT.
  -- Test only: {tick,'avatar_hold',true|false}: this client's player's stand-in figure stops following it / follows
  -- again (MD AvatarHold; the signal travels as any script call of the slot does).
  elseif op=='avatar_hold' then call=function()
    tools.env.SignalObject(tools.galaxy_lua(),'x4lan_avatar_hold',tools.own_slot*10+(e[3] and 1 or 0));return 'lua' end
  elseif op=='transport_list' then call=function() return tools.transport_list() end
  elseif op=='transport_to' then call=function() return tools.transport_to(e[3],nil) end
  elseif op=='transport_named' then call=function() return tools.transport_to(nil,e[3]) end
  -- {tick,'foot_meet',SLOT[,metres[,limit]]}: this client's walking player walks towards that slot's figure until it
  -- is `metres` away (K.foot_walk; 3 m, 900 ticks).
  elseif op=='foot_meet' then
    local limit=e[5] or 900
    local walk=K.foot_walk(tools,e[3],e[4] or 3)
    call=walk.start
    for i=1,limit do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op=op,quiet=true,call=function()
      if i==limit then walk.release();return 'ends' end
      return walk.step(i) end}
    end
  -- The room tests. Test only, read only, canonical (in every client's plan at the same ticks): {tick,'transporter_find',
  -- SLOT}: MD finds the transporters around that slot's walking player (TransporterFind) and tells every UI.
  -- {tick,'transporter_look'}: this client's own player's nearest one ('points=N nearest=D at=X/Y/Z', none: points=0).
  -- {tick,'walk_transporter'[,metres[,limit]]}: this client's walking player walks to its nearest transporter, chosen
  -- when the walk begins (K.foot_walk to a place; 0.6 m, 1200 ticks).
  elseif op=='transporter_find' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','TransporterFind',{slot=e[3]});return 'lua' end
  -- Read only: {tick,'figure_look'}: every slot's stand-in figure's place in its station (the same on every client).
  elseif op=='figure_look' then call=function()
    local parts={}
    for k=1,tools.slots or 2 do
     local f=tools.figure_pose and tools.figure_pose(k)
     parts[#parts+1]=k..'='..(f and string.format('%.1f/%.1f/%.1f',f.x,f.y,f.z) or 'none')
    end
    return table.concat(parts,' ') end
  elseif op=='transporter_look' then call=function()
    local t,d,n=tools.transporter_near()
    return 'points='..tostring(n)..(t and string.format(' nearest=%.1f at=%.1f/%.1f/%.1f slot=%s',d,t.x,t.y,t.z,tostring(t.name)) or '') end
  -- {tick,'avatar_guide',true|false}: this client's player's figure walks ahead to the nearest player transporter (MD
  -- AvatarGuide; the signal travels as the slot's script calls do), or follows its player again.
  -- {tick,'walk_guided'[,metres[,limit]]}: this client's walking player follows that figure's trail to the transporter
  -- (K.foot_follow; 0.5 m, 2400 ticks).
  elseif op=='avatar_guide' then call=function()
    tools.env.SignalObject(tools.galaxy_lua(),'x4lan_avatar_guide',tools.own_slot*10+(e[3] and 1 or 0));return 'lua' end
  elseif op=='walk_guided' then
    local limit,pick=e[4] or 2400,nil
    local walk=K.foot_follow(tools,function() return tools.figure_pose(tools.own_slot) end,function()
      if not pick then pick=tools.transporter_guided and tools.transporter_guided() or nil end
      return pick end,e[3] or 0.5,
      function() tools.env.SignalObject(tools.galaxy_lua(),'x4lan_avatar_guide',tools.own_slot*10) end)
    call=walk.start
    for i=1,limit do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op=op,quiet=true,call=function()
      if i==limit then walk.release();return 'ends' end
      return walk.step(i) end}
    end
  -- {tick,'walk_point',X,Z[,metres[,limit]]}: this client's walking player walks to a place of its station's frame
  -- (a known transporter pad of a test station; 0.5 m, 1800 ticks).
  elseif op=='walk_point' then
    local limit=e[6] or 1800
    local walk=K.foot_walk(tools,function() return {x=e[3],y=0,z=e[4]} end,e[5] or 0.5)
    call=walk.start
    for i=1,limit do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op=op,quiet=true,call=function()
      if i==limit then walk.release();return 'ends' end
      return walk.step(i) end}
    end
  -- {tick,'transporter_ui',ROOM[,limit]}: this client's player on a transporter's pad travels to ROOM as a person does
  -- (K.transporter_ui: look round until the crosshair names the transporter, click, X4's TransporterMenu, its row,
  -- "Travel to"); 'there pressed travel ...' when the button was pressed (2400 ticks).
  -- {tick,'transporter_aim'[,limit]} and {tick,'transporter_use',ROOM[,limit]}: the same in two steps (a picture of
  -- what the crosshair is on between them).
  elseif op=='transporter_ui' or op=='transporter_aim' or op=='transporter_use' then
    local room=op~='transporter_aim' and e[3] or nil
    local limit=(op=='transporter_aim' and e[3] or e[4]) or 2400
    local ui=K.transporter_ui(tools,room,op=='transporter_aim' and 'aim' or op=='transporter_use' and 'use' or nil)
    call=ui.start
    for i=1,limit do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op=op,quiet=true,call=function()
      if i==limit then ui.release();return 'ends' end
      return ui.step(i) end}
    end
  elseif op=='walk_transporter' then
    local limit,pick=e[4] or 1200,nil
    local walk=K.foot_walk(tools,function()
      if not pick then pick=tools.transporter_near() end
      return pick end,e[3] or 0.6)
    call=walk.start
    for i=1,limit do
     by[at+i]=by[at+i] or {};by[at+i][#by[at+i]+1]={op=op,quiet=true,call=function()
      if i==limit then walk.release();return 'ends' end
      return walk.step(i) end}
    end
  -- Where this client's player is: tools.foot_where() (the probe's text: the player's room and place in it).
  elseif op=='foot_where' then call=function() return tools.foot_where() end
  elseif op=='undock' then call=function() return ffi.string(tools.C.UndockPlayerShip(false)) end
  -- Read only: X4's answer to "may this client's player undock now" ({tick,'undock_check'}: 'granted', or why not,
  -- e.g. 'tradecomputerbusy' for some seconds after a docked trade), as the dock menu asks before it offers the button.
  elseif op=='undock_check' then call=function() return ffi.string(tools.C.UndockPlayerShip(true)) end
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
  -- More orders for a `player` ship (a saved ID), as the stock menus give them:
  --  {tick,'explore',ship,sector[,x,y,z]}: the interact menu's "Explore" on a sector (menu.orderExplore).
  --  {tick,'q_patrol',ship,sector}: the map's order row "Patrol": the new order, its space, the start (buttonNewOrder,
  --    the parameter's set, buttonStartOrders).
  --  {tick,'behaviour',ship,orderdef[,space]}: the map's behaviour page: a new default order and its first parameter
  --    (buttonNewOrder with default, the parameter's set); {tick,'behaviour_confirm',ship}: its Confirm
  --    (buttonDefaultOrderConfirm: ResetOrderLoop, EnablePlannedDefaultOrder), a click later as a person's is.
  elseif op=='explore' then call=function()
    local sector=tools.saved_lua(e[4])
    return tools.env.CreateOrder(tools.saved_lua(e[3]),'Explore',{targetspace=sector,destination={sector,{e[5] or 0,e[6] or 0,e[7] or 0}}},false,false,false,nil,nil,nil,true) end
  elseif op=='q_patrol' then call=function()
    local id=tools.saved_id(e[3]);local idx=tools.C.CreateOrder(id,'Patrol',false)
    if idx>0 then tools.env.SetOrderParam(tools.saved_lua(e[3]),idx,1,nil,tools.saved_lua(e[4]));tools.C.EnableOrder(id,idx) end
    return idx end
  elseif op=='behaviour' then call=function()
    local id=tools.saved_id(e[3])
    local idx=tools.C.CreateOrder(id,e[4],true)
    if e[5] then tools.env.SetOrderParam(tools.saved_lua(e[3]),'planneddefault',1,nil,tools.saved_lua(e[5])) end
    return tostring(idx) end
  -- {tick,'behaviour_param',ship,INDEX,VALUE}: one parameter of the planned default order as the behaviour page sets it
  -- (SetOrderParam on 'planneddefault'; a list parameter takes one more entry per call). VALUE: a number, a string (a
  -- ware), a boolean, or {'id',SAVED} for an object or a space.
  elseif op=='behaviour_param' then call=function()
    local v=e[5]
    if type(v)=='table' then v=tools.saved_lua(v[2]) end
    tools.env.SetOrderParam(tools.saved_lua(e[3]),'planneddefault',e[4],nil,v);return 'lua' end
  -- {tick,'behaviour_loop',ship}: the behaviour page's "Repeat orders" and its Confirm (buttonNewOrder('loop'), then
  -- buttonDefaultOrderConfirm's loop branch: RemoveAllOrders, a default Wait, EnablePlannedDefaultOrder, SetOrderLoop).
  -- {tick,'trade_loop_order',ship,KIND,station,WARE,AMOUNT[,PRICE]}: the trade menu's Confirm in its loop mode
  -- (menu.buttonConfirmTradeLoop): KIND SingleBuy or SingleSell, AMOUNT what the hold is to end with (0 for a sale),
  -- PRICE the threshold in credits (default: the ware's top price to buy, its bottom price to sell).
  elseif op=='behaviour_loop' then call=function()
    for _,line in ipairs({'void RemoveAllOrders(UniverseID controllableid);','bool EnablePlannedDefaultOrder(UniverseID controllableid, bool checkonly);',
      'bool SetOrderLoop(UniverseID controllableid, size_t orderidx, bool checkonly);'}) do pcall(ffi.cdef,line) end
    local id=tools.saved_id(e[3])
    tools.C.RemoveAllOrders(id)
    tools.C.CreateOrder(id,'Wait',true)
    tools.C.EnablePlannedDefaultOrder(id,false)
    return 'loop='..tostring(tools.C.SetOrderLoop(id,0,false)) end
  elseif op=='trade_loop_order' then call=function()
    local id,lua=tools.saved_id(e[3]),tools.saved_lua(e[3])
    local price=e[8] or tools.env.GetWareData(e[6],e[4]=='SingleBuy' and 'maxprice' or 'minprice')
    local idx=tools.C.CreateOrder(id,e[4],false)
    if idx>0 then
     tools.env.SetOrderParam(lua,idx,1,nil,e[6])
     tools.env.SetOrderParam(lua,idx,4,nil,tools.saved_lua(e[5]))
     tools.env.SetOrderParam(lua,idx,5,nil,e[7])
     tools.env.SetOrderParam(lua,idx,7,nil,price*100)
     tools.C.EnableOrder(id,idx)
    end
    return 'order='..tostring(idx)..' price='..tostring(price) end
  elseif op=='behaviour_confirm' then call=function()
    for _,line in ipairs({'void ResetOrderLoop(UniverseID controllableid);','bool EnablePlannedDefaultOrder(UniverseID controllableid, bool checkonly);'}) do pcall(ffi.cdef,line) end
    local id=tools.saved_id(e[3])
    tools.C.ResetOrderLoop(id)
    return tostring(tools.C.EnablePlannedDefaultOrder(id,false)) end
  -- {tick,'move_by',ship,sector,dx,dy,dz}: the interact menu's "Fly to" (menu.orderMoveWait) on a place of the ship's
  -- sector that far from where the ship is now; the place is kept for {tick,'ship_arrived',ship,metres} (read only:
  -- 'near=true|false distance=N').
  elseif op=='move_by' then call=function()
    local p=tools.C.GetObjectPositionInSector(tools.saved_id(e[3]))
    local x,y,z=p.x+(e[5] or 0),p.y+(e[6] or 0),p.z+(e[7] or 0)
    tools.move_targets=tools.move_targets or {}
    tools.move_targets[e[3]]={x,y,z}
    local idx=tools.env.CreateOrder(tools.saved_lua(e[3]),'MoveWait',{destination={tools.saved_lua(e[4]),{x,y,z}}},false,false,false,nil,nil,nil,true)
    return string.format('order=%s from=%.0f/%.0f/%.0f to=%.0f/%.0f/%.0f',tostring(idx),p.x,p.y,p.z,x,y,z) end
  elseif op=='ship_arrived' then call=function()
    local target=assert(tools.move_targets and tools.move_targets[e[3]],'ui replay script: no move_by for that ship yet')
    local p=tools.C.GetObjectPositionInSector(tools.saved_id(e[3]))
    local d=math.sqrt((p.x-target[1])^2+(p.y-target[2])^2+(p.z-target[3])^2)
    return string.format('near=%s distance=%.0f',tostring(d<=(e[4] or 1000)),d) end
  -- The stock menus by their own handlers, where a person's click arrives (a button's onClick names a function of its
  -- menu's table; Menus holds the tables). Whatever the session put in a handler's place runs too (the order block's
  -- refusal answers 0), so a click that is refused to a person is refused here.
  --  {tick,'menu_call',MENU,FUNCTION[,arg...]}: that handler, e.g. 'DockedMenu','buttonTrade',false (the dock menu's
  --    "Trade with station"), 'DockedMenu','buttonUndock'. Answers 'called', 'called answer=0' (the block's refusal:
  --    'blocked'), or what failed.
  --  {tick,'menu_look',MENU}: read only: on screen or not, its mode, its context menu's mode; for the map's trade
  --    context also the trading ship, the trade computer flag and the amounts set.
  --  {tick,'trade_menu_amount',WARE,AMOUNT}: the open trade menu's amount slider of that ware's row, AMOUNT units into
  --    the ship (negative: out of it): menu.orderAmountHelper with the station's offer, as the slider's handler calls.
  elseif op=='menu_call' then call=function()
    local menu=tools.menu(e[3])
    if not menu then return 'no_menu' end
    if type(menu[e[4]])~='function' then return 'no_function' end
    local answer=menu[e[4]](unpack(e,5,#e))
    if answer==0 then return 'blocked answer=0' end
    return 'called'..(answer~=nil and ' answer='..tostring(answer) or '') end
  -- {tick,'trade_menu_open',STATION}: the trade menu for the station this client's player is docked at: the dock menu's
  -- "Trade with station" button when that menu is on screen ('via=dock_menu'); when it is not, the menu that button
  -- opens, opened the same way ('via=open_menu').
  elseif op=='trade_menu_open' then call=function()
    local dock=tools.menu('DockedMenu')
    if dock and dock.shown and type(dock.buttonTrade)=='function' then dock.buttonTrade(false);return 'via=dock_menu' end
    tools.env.OpenMenu('MapMenu',{0,0,true,nil,nil,'tradecontext',{tools.saved_lua(e[3]),nil,false}},nil)
    return 'via=open_menu dock_menu='..tostring(dock and dock.shown and true or false) end
  -- {tick,'menu_open',MENU}: that stock menu on this client's screen as its hotkey opens it (OpenMenu with the plain
  -- parameter; 'MapMenu': the map); {tick,'menu_close',MENU}: closed as Escape closes it (Helper.closeMenu).
  elseif op=='menu_open' then call=function() tools.env.OpenMenu(e[3],{0,0,true},nil);return 'opened' end
  elseif op=='menu_close' then call=function()
    local menu=tools.menu(e[3])
    if not menu then return 'no_menu' end
    tools.env.Helper.closeMenu(menu,'close')
    if type(menu.cleanup)=='function' then menu.cleanup() end
    return 'closed' end
  -- {tick,'menu_back',MENU}: Back out of that stock menu as its last Back does (Helper.closeMenu 'back': to the menu it
  --   was opened from, when it has that return record). Answers 'back to=NAME' or 'back to=-'.
  elseif op=='menu_back' then call=function()
    local menu=tools.menu(e[3])
    if not menu then return 'no_menu' end
    local to=type(menu.param2)=='table' and type(menu.param2[1])=='string' and menu.param2[1] or '-'
    tools.env.Helper.closeMenu(menu,'back')
    if type(menu.cleanup)=='function' then menu.cleanup() end
    return 'back to='..to end
  -- {tick,'toplevel_tab',ID[,MENU[,NORETURN]]}: a click on entry ID of the HUD's top level bar ('cockpit' = Ship
  --   Interactions, 'docked', 'map', 'playerinfo', ...) in that open menu (default the HUD's own TopLevelMenu): the
  --   button's onClick, Helper.scrollTopLevelInternal(menu, index, nil, NORETURN) (the map's tabs pass true), when the
  --   bar offers the entry (Helper.checkTopLevelConditions). Answers 'pressed opens=MENU', 'not_offered', 'no_entry',
  --   'not_shown'. Runs in the presentation scope, as a click.
  elseif op=='toplevel_tab' then call=function()
    local H=tools.env.Helper
    local menu=tools.menu(e[4] or 'TopLevelMenu')
    if not menu or not menu.shown then return 'not_shown' end
    for i,entry in ipairs(H.topLevelMenus or {}) do
     if entry.id==e[3] then
      if not H.checkTopLevelConditions(entry) then return 'not_offered' end
      H.scrollTopLevelInternal(menu,i,nil,e[5] or nil)
      return 'pressed opens='..tostring(entry.menu)
     end
    end
    return 'no_entry' end
  elseif op=='menu_look' then call=function()
    local menu=tools.menu(e[3])
    if not menu then return 'no_menu' end
    local c=type(menu.contextMenuData)=='table' and menu.contextMenuData or {}
    local parts={'shown='..tostring(menu.shown and true or false),'mode='..tostring(menu.mode),'context='..tostring(menu.contextMenuMode)}
    -- the dock menu's ship (Ship Interactions: the occupied ship it was opened for) and whether it is this client's slot's
    if menu.name=='DockedMenu' and menu.shown and menu.currentplayership~=nil then
     local ship=(tostring(menu.currentplayership):gsub('ULL$',''))
     local own=tools.gate and tools.gate.pilot_ship and tools.gate.pilot_ship(tools.own_slot or 1)
     parts[#parts+1]='ship='..ship..' own='..tostring(own~=nil and ship==string.format('%.0f',own))
    end
    if menu.contextMenuMode=='trade' then
     local orders={}
     for id,amount in pairs(type(c.orders)=='table' and c.orders or {}) do if amount~=0 then orders[#orders+1]=tostring(amount) end end
     table.sort(orders)
     parts[#parts+1]='ship='..(tostring(c.currentShip):gsub('ULL$',''))..' immediate='..tostring(c.immediate)..' amounts='..(#orders>0 and table.concat(orders,',') or '-')
    end
    return table.concat(parts,' ') end
  elseif op=='trade_menu_amount' then call=function()
    local menu=tools.menu('MapMenu')
    if not menu or menu.contextMenuMode~='trade' or type(menu.contextMenuData)~='table' then return 'no_trade_menu' end
    local station=tools.env.ConvertStringToLuaID((tostring(menu.contextMenuData.component):gsub('ULL$','')))
    local sell,buy
    for _,td in ipairs(tools.env.GetTradeList(station) or {}) do
     if td.ware==e[3] and not td.ismissionoffer then
      if td.isselloffer then sell=sell or td.id elseif td.isbuyoffer then buy=buy or td.id end
     end
    end
    if e[4]>0 and not sell then return 'no_sell_offer' end
    if e[4]<0 and not buy then return 'no_buy_offer' end
    menu.orderAmountHelper(sell,buy,-e[4])
    return 'set' end
  -- {tick,'build',OP,...}: a script op of the build and assets families (native_gate_ui_build_core.lua K.script: buy,
  -- refit, exchange, plot, ...), run through that core's session as its own script runs it, so a chain can hold one.
  elseif op=='build' then call=function()
    local b=assert(tools.build,'ui replay script: the build family is not armed')
    local entry={0}
    for i=3,#e do entry[#entry+1]=e[i] end
    return b.script({entry},0,b.tools)[0][1].call(b.session) end
  -- Read only: {tick,'fleet_look'}: the player faction's ships; {tick,'build_look',STATION}: the player's build tasks at
  -- a shipyard or wharf; {tick,'fit_look',SHIP}: a ship's fitted engines, shields, weapons, turrets and thruster.
  elseif op=='fleet_look' then call=function() return tools.fleet_look() end
  elseif op=='build_look' then call=function() return tools.build_look(e[3]) end
  elseif op=='fit_look' then call=function() return tools.fit_look(e[3]) end
  -- Read only: {tick,'menu_entry',MENU,FUNCTION}: whether a person's click on that stock menu entry is offered in this
  -- session (tools.menu_entry: 'open why=...' or 'blocked'), e.g. 'MapMenu','buttonConfirmTrade'.
  elseif op=='menu_entry' then call=function() return tools.menu_entry(e[3],e[4])..' entry='..tostring(e[3])..'.'..tostring(e[4]) end
  -- A ship's people, as the map's crew page and the personnel page change them (SHIP: a slot number or a saved ID):
  --  {tick,'crew_fire',SHIP[,ROLE]}: "Fire" on the first crew member (of that role): RemovePerson.
  --  {tick,'captain_fire',SHIP}: "Fire" on the ship's pilot: the entity's signal npc__control_dismissed.
  --  {tick,'crew_promote',SHIP[,ROLE]}: "Promote best crew member to captain" with the first crew member (of that
  --    role; the menu takes the best of service and marines): AssignHiredActor to the pilot's post.
  elseif op=='crew_fire' then call=function()
    pcall(ffi.cdef,'void RemovePerson(UniverseID controllableid, NPCSeed person);')
    local person=tools.ship_people(e[3],e[4])[1]
    if not person then return 'nobody' end
    tools.C.RemovePerson(tools.ship_id(e[3]),person.seed)
    return 'fired='..(tostring(person.seed):gsub('ULL$',''))..' role='..person.role end
  elseif op=='captain_fire' then call=function()
    local id=tools.ship_id(e[3])
    local pilot=tools.env.GetComponentData(tools.env.ConvertStringToLuaID((tostring(id):gsub('ULL$',''))),'assignedpilot')
    if not pilot then return 'no_pilot' end
    local entity=tools.env.ConvertIDTo64Bit(pilot)
    tools.env.SignalObject(tools.env.ConvertStringTo64Bit(tostring(entity)),'npc__control_dismissed')
    return 'dismissed='..(tostring(entity):gsub('ULL$','')) end
  elseif op=='crew_promote' then call=function()
    pcall(ffi.cdef,'const char* AssignHiredActor(GenericActor actor, UniverseID targetcontrollableid, const char* postid, const char* roleid, bool checkonly);')
    local person=tools.ship_people(e[3],e[4])[1]
    if not person then return 'nobody' end
    local id=tools.ship_id(e[3])
    local actor=ffi.new('GenericActor',{entity=0,personcontrollable=id,personseed=person.seed})
    local reason=ffi.string(tools.C.AssignHiredActor(actor,id,'aipilot',nil,false))
    return 'promoted='..(tostring(person.seed):gsub('ULL$',''))..' role='..person.role..' answer='..(reason=='' and '-' or reason) end
  -- Read only: {tick,'ship_look',SHIP}: a slot's ship (a number) or a saved one: tools.ship_look (owner, pilot, docked,
  -- place, order queue, default order, people, hull, shield, commander, subordinates, name).
  elseif op=='ship_look' then call=function() return tools.ship_look(e[3]) end
  -- {tick,'order',SHIP,ORDER,{{PARAM,VALUE},...}[,CLEAR]}: an order of the interact menu's kind for a fleet ship (a
  -- saved ID): the selectable check, with CLEAR the queue's removal, then the Lua CreateOrder with the order's
  -- parameters, as menu.orderProtect, orderFollow, orderCollectRadius, orderAttackInRange... call it. A VALUE is a
  -- number, a string, a boolean, or one of: {'id',SAVED} an object; {'slot',K} a player's ship; {'pos',SECTOR,x,y,z}
  -- a place of a sector; {'at',SECTOR,SHIP[,dx,dy,dz]} the place where that ship is now (plus an offset);
  -- {'deployed',MACRO[,N]} the N-th deployable of the player's of that macro (tools.deployed); {'list',VALUE,...};
  -- {'spot',SECTOR,WARE,SHIP} the place nearest to that ship that yields the ware (tools.mine_spot; where the ship is
  -- when none does).
  elseif op=='order' then call=function()
    pcall(ffi.cdef,'bool IsOrderSelectableFor(const char* orderdefid, UniverseID controllableid);')
    local function value(v)
     if type(v)~='table' then return v end
     local kind=v[1]
     if kind=='id' then return tools.saved_lua(v[2])
     elseif kind=='slot' then return tools.slot_lua(v[2])
     elseif kind=='pos' then return {tools.saved_lua(v[2]),{v[3] or 0,v[4] or 0,v[5] or 0}}
     elseif kind=='at' then
      local p=tools.C.GetObjectPositionInSector(tools.ship_id(v[3]))
      return {tools.saved_lua(v[2]),{p.x+(v[4] or 0),p.y+(v[5] or 0),p.z+(v[6] or 0)}}
     elseif kind=='deployed' then return assert(tools.deployed(v[2],v[3] or 1),'ui replay script: no such deployable in space')
     elseif kind=='spot' then
      local spot=tools.mine_spot(v[2],v[3],v[4])
      if not spot then local p=tools.C.GetObjectPositionInSector(tools.ship_id(v[4]));spot={x=p.x,y=p.y,z=p.z} end
      return {tools.saved_lua(v[2]),{spot.x,spot.y,spot.z}}
     elseif kind=='list' then
      local out={}
      for i=2,#v do out[#out+1]=value(v[i]) end
      return out
     end
     error('ui replay script: order value '..tostring(kind)) end
    local id=tools.ship_id(e[3])
    local lua=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3])
    if not tools.C.IsOrderSelectableFor(e[4],id) then return 'not_selectable' end
    if not tools.env.GetComponentData(lua,'assignedpilot') then return 'no_pilot' end
    local params={}
    for _,pair in ipairs(e[5] or {}) do params[pair[1]]=value(pair[2]) end
    if e[6] then tools.C.RemoveAllOrders2(id,false,false) end
    return 'order='..tostring(tools.env.CreateOrder(lua,e[4],params,false,false,false,nil,nil,nil,true)) end
  -- Wings, as the interact menu's "Assign as subordinate" and "Remove assignment" (SHIP, COMMANDER: saved IDs or slots):
  --  {tick,'assign',SHIP,COMMANDER[,ASSIGNMENT[,GROUP]]}: buttonAssignCommander's calls for one selected ship: the
  --    order loop's reset, then the AssignCommander order with its parameters (menu.orderAssignCommander), enabled
  --    and moved to the front. ASSIGNMENT 'defence' (the default), 'attack', 'interception', ...; GROUP 1.
  --  {tick,'unassign',SHIP}: buttonRemoveAssignment's calls: RemoveCommander2, a default Wait, the planned default
  --    order enabled.
  elseif op=='assign' then call=function()
    for _,line in ipairs({'void ResetOrderLoop(UniverseID controllableid);','bool IsOrderSelectableFor(const char* orderdefid, UniverseID controllableid);'}) do pcall(ffi.cdef,line) end
    local id=tools.ship_id(e[3])
    local lua=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3])
    if not tools.C.IsOrderSelectableFor('AssignCommander',id) then return 'not_selectable' end
    if not tools.env.GetComponentData(lua,'assignedpilot') then return 'no_pilot' end
    tools.C.ResetOrderLoop(id)
    local idx=tools.C.CreateOrder(id,'AssignCommander',false)
    if idx>0 then
     local commander=type(e[4])=='number' and tools.slot_lua(e[4]) or tools.saved_lua(e[4])
     tools.env.SetOrderParam(lua,idx,1,nil,commander)
     tools.env.SetOrderParam(lua,idx,2,nil,e[5] or 'defence')
     tools.env.SetOrderParam(lua,idx,3,nil,e[6] or 1)
     tools.env.SetOrderParam(lua,idx,4,nil,true)
     tools.env.SetOrderParam(lua,idx,5,nil,true)
     tools.env.SetOrderParam(lua,idx,6,nil,true)
     tools.env.SetOrderParam(lua,idx,8,nil,true)
     tools.C.EnableOrder(id,idx)
     if idx~=1 then tools.C.AdjustOrder(id,idx,1,true,false,false) end
    end
    return 'order='..tostring(idx) end
  elseif op=='unassign' then call=function()
    for _,line in ipairs({'bool RemoveCommander2(UniverseID controllableid);','bool EnablePlannedDefaultOrder(UniverseID controllableid, bool checkonly);'}) do pcall(ffi.cdef,line) end
    local id=tools.ship_id(e[3])
    local removed=tools.C.RemoveCommander2(id)
    local idx=tools.C.CreateOrder(id,'Wait',true)
    return 'removed='..tostring(removed)..' wait='..tostring(idx)..' enabled='..tostring(tools.C.EnablePlannedDefaultOrder(id,false)) end
  -- Read only: {tick,'ship_value',SHIP,STATION}: what the map's "Sell ships" would show for a ship of the player's at
  -- that shipyard or wharf: the value, whether the station takes the ship (CanContainerBuildShip), whether it may be
  -- sold, and whether it is docked. (The sale itself: {tick,'build','sell',STATION,SHIP}, the build family's.)
  elseif op=='ship_value' then call=function()
    pcall(ffi.cdef,'bool CanContainerBuildShip(UniverseID containerid, UniverseID shipid);')
    local ship,station=tools.ship_id(e[3]),tools.saved_id(e[4])
    local lua=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3])
    local price=tools.env.GetTotalValue(ship,true,station)
    local sellable,docked=tools.env.GetComponentData(lua,'issellable','isdocked')
    return string.format('price=%.0f takes=%s sellable=%s docked=%s',tonumber(price) or -1,tostring(tools.C.CanContainerBuildShip(station,ship)),
     tostring(sellable and true or false),tostring(docked and true or false)) end
  -- Read only: {tick,'rules_look'}: the player faction's blacklists, fight rules and trade rules (tools.rules_look);
  -- {tick,'rules_has',NAME}: whether one of them has that name: 'has=true kind=K id=N' or 'has=false'.
  elseif op=='rules_look' then call=function() return tools.rules_look() end
  elseif op=='rules_has' then call=function()
    for _,r in ipairs(tools.rules()) do
     if r.name==e[3] then return 'has=true kind='..r.kind..' id='..r.id end
    end
    return 'has=false' end
  -- Read only: {tick,'ship_gap',SHIP,SHIP}: how far apart two ships of one sector are, in metres: 'gap=N'.
  elseif op=='ship_gap' then call=function()
    local a,b=tools.C.GetObjectPositionInSector(tools.ship_id(e[3])),tools.C.GetObjectPositionInSector(tools.ship_id(e[4]))
    return string.format('gap=%.0f',math.sqrt((a.x-b.x)^2+(a.y-b.y)^2+(a.z-b.z)^2)) end
  -- Read only: {tick,'ship_owned',SHIP}: 'mine=true' while that saved ship exists and is the player faction's, else
  -- 'mine=false' with why (gone, dead, or its owner).
  elseif op=='ship_owned' then call=function()
    local row=tools.ship_look(e[3])
    local owner=row:match(' owner=(%S+)')
    if owner=='player' then return 'mine=true' end
    return 'mine=false '..(owner and 'owner='..owner or row:match('^(state=%S+)') or 'unknown') end
  -- {tick,'crew_move',FROM,TO[,ROLE[,CHECK]]}: the crew transfer menu's confirm with one crew member of FROM (the first
  -- of that role) moved to TO and nobody back, no captain involved (PerformCrewExchange2); CHECK true: the menu's
  -- question only ("could this be done": the reason, '-' for none).
  elseif op=='crew_move' then call=function()
    pcall(ffi.cdef,'UICrewExchangeResult PerformCrewExchange2(UniverseID controllableid, UniverseID partnercontrollableid, NPCSeed* npcs, uint32_t numnpcs, NPCSeed* partnernpcs, uint32_t numpartnernpcs, NPCSeed captainfromcontainer, NPCSeed captainfrompartner, bool exchangecaptains, bool checkonly);')
    local person=tools.ship_people(e[3],e[5])[1]
    if not person then return 'nobody' end
    local from,to=tools.ship_id(e[3]),tools.ship_id(e[4])
    local moved=ffi.new('NPCSeed[1]')
    moved[0]=person.seed
    local none=ffi.new('NPCSeed[1]')
    local result=tools.C.PerformCrewExchange2(from,to,moved,1,none,0,0,0,false,e[6]==true)
    local reason=ffi.string(result.reason)
    return (e[6]==true and 'check' or 'moved')..'='..(tostring(person.seed):gsub('ULL$',''))..' role='..person.role..' reason='..(reason=='' and '-' or reason) end
  -- Read only: {tick,'deploy_look'[,SHIP]}: the player faction's deployables in space and what that ship still holds.
  elseif op=='deploy_look' then call=function() return tools.deploy_look(e[3]) end
  -- Read only, for a chain: {tick,'deploy_count',KIND}: how many units of that kind ('Satellites', 'NavBeacons',
  -- 'ResourceProbes', 'Mines', 'LaserTowers') this client's own ship holds, and how many fewer than at the first
  -- asking: 'left=N launched=M' (the first asking answers launched=0).
  elseif op=='deploy_count' then call=function()          -- (a fourth value: another ship of the player's, a saved ID)
    local now=tools.deploy_held(e[4] or tools.own_slot,e[3])
    local key=e[3]..(e[4] and ':'..tostring(e[4]) or '')
    tools.deploy_base=tools.deploy_base or {}
    tools.deploy_base[key]=tools.deploy_base[key] or now
    return 'left='..now..' launched='..(tools.deploy_base[key]-now)..' kind='..tostring(e[3]) end
  -- Mining, as the interact menu's "Mine" on a place of a sector (SHIP: a saved ID):
  --   {tick,'mine_spot',SECTOR,WARE,SHIP}: read only, the place the scripted click goes to (tools.mine_spot)
  --   {tick,'mine',SHIP,WARE,SECTOR}: menu.orderMining there: the selectable check, the hold's capacity for the ware,
  --     the MiningPlayer order
  --   {tick,'cargo_gain',SHIP}: read only, what the ship's hold has gained since this op first asked ('gain=N total=M')
  elseif op=='mine_spot' then call=function()
    local spot=tools.mine_spot(e[3],e[4],e[5])
    if not spot then
     -- what the sector does yield near the ship, so a report says whether it has resources at all
     local seen={}
     for ware,s in pairs(tools.mine_scan and tools.mine_scan(e[3],e[5]) or {}) do seen[#seen+1]=ware..':'..s.amount..'@'..math.floor(math.sqrt(s.d)/1000)..'km' end
     table.sort(seen)
     return 'no_spot yields='..(#seen>0 and table.concat(seen,',') or 'none')
    end
    return string.format('at=%.0f/%.0f/%.0f yield=%d away=%.0f',spot.x,spot.y,spot.z,spot.amount,math.sqrt(spot.d)) end
  elseif op=='mine' then call=function()
    pcall(ffi.cdef,'bool IsOrderSelectableFor(const char* orderdefid, UniverseID controllableid);')
    local id,lua=tools.ship_id(e[3]),tools.saved_lua(e[3])
    if not tools.C.IsOrderSelectableFor('MiningPlayer',id) then return 'not_selectable' end
    if not tools.env.GetComponentData(lua,'assignedpilot') then return 'no_pilot' end
    if not ((tonumber(tools.env.GetWareCapacity(lua,e[4],true)) or 0)>0) then return 'no_capacity' end
    local spot=tools.mine_spot(e[5],e[4],e[3])
    if not spot then return 'no_spot' end
    return 'order='..tostring(tools.env.CreateOrder(lua,'MiningPlayer',{destination={tools.saved_lua(e[5]),{spot.x,spot.y,spot.z}},ware=e[4]},false,false,false))..
     string.format(' at=%.0f/%.0f/%.0f yield=%d',spot.x,spot.y,spot.z,spot.amount) end
  -- Read only: {tick,'ship_moved',SHIP,METRES}: how far the ship is from where this op first saw it, in its sector's
  -- frame: 'moved=true|false distance=N' (a ship that changed sector counts as moved).
  elseif op=='ship_moved' then call=function()
    pcall(ffi.cdef,'UniverseID GetContextByClass(UniverseID componentid, const char* classname, bool includeself);')
    local id=tools.ship_id(e[3])
    local p=tools.C.GetObjectPositionInSector(id)
    local sector=tostring(tools.C.GetContextByClass(id,'sector',false))
    tools.moved_base=tools.moved_base or {}
    local base=tools.moved_base[e[3]]
    if not base then base={x=p.x,y=p.y,z=p.z,sector=sector};tools.moved_base[e[3]]=base end
    local d=math.sqrt((p.x-base.x)^2+(p.y-base.y)^2+(p.z-base.z)^2)
    local away=sector~=base.sector
    return 'moved='..tostring(away or d>=(e[4] or 1000))..' distance='..math.floor(d)..(away and ' sector=other' or '') end
  elseif op=='cargo_gain' then call=function()
    local cargo=tools.env.GetComponentData(tools.saved_lua(e[3]),'cargo')
    local total=0
    for _,amount in pairs(type(cargo)=='table' and cargo or {}) do total=total+amount end
    tools.cargo_base=tools.cargo_base or {}
    tools.cargo_base[e[3]]=tools.cargo_base[e[3]] or total
    return 'gain='..(total-tools.cargo_base[e[3]])..' total='..total end
  -- A deployable this client's script launched, as the interact menu treats a selected one (MACRO names the kind):
  --   {tick,'deploy_mark',MACRO}: notes the newest one of that macro as this script's ('marked=ID')
  --   {tick,'deployable_look',MACRO}: read only: 'state=ok active=true|false' or 'state=gone'
  --   {tick,'deployable_active',MACRO,BOOL}: "Activate" / "Deactivate" (menu.buttonActivateDeployables: ActivateObject
  --     when its state is the other one)
  --   {tick,'deployable_destroy',MACRO}: "Self-destruct" (menu.buttonSelfDestructDeployables: SelfDestructComponent)
  elseif op=='deploy_mark' then call=function() return 'marked='..tostring(tools.deploy_mark(e[3])) end
  elseif op=='deployable_look' then call=function()
    local lua=tools.deploy_own(e[3])
    if not lua then return 'state=gone ' end
    return 'state=ok active='..tostring(tools.env.GetComponentData(lua,'isactive') and true or false)..' ' end
  elseif op=='deployable_active' or op=='deployable_destroy' then call=function()
    for _,line in ipairs({'void ActivateObject(UniverseID objectid, bool active);','void SelfDestructComponent(UniverseID componentid);'}) do pcall(ffi.cdef,line) end
    local lua,id=tools.deploy_own(e[3])
    if not lua then return 'gone' end
    if op=='deployable_destroy' then tools.C.SelfDestructComponent(id);return 'ffi' end
    local want=e[4] and true or false
    if (tools.env.GetComponentData(lua,'isactive') and true or false)==want then return 'already' end
    tools.C.ActivateObject(id,want);return 'ffi' end
  -- {tick,'ship_name',SHIP,NAME}: the map's rename confirm for a ship of the player's (a slot or a saved ID).
  elseif op=='ship_name' then call=function()
    local lua=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3])
    tools.env.SetComponentName(lua,e[4]);return 'named' end
  -- Test only (MD Hull, canonical): {tick,'hull_set',SHIP,PERCENT}: that ship's hull (a slot or a saved ID).
  -- Test only, read only (MD RefitWhy, canonical): {tick,'refit_why',STATION}: the station's queued builds with the parts
  -- of its ship trader's test for starting an upgrade (rows UI_REPLAY_REFIT_WHY*).
  elseif op=='refit_why' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','RefitWhy',{station=tools.saved_lua(e[3])});return 'lua' end
  -- Test only, read only (MD DockApproach, canonical): {tick,'dock_approach',SLOT}: where X4's landing check wants that
  -- slot's ship before it lands on its assigned dock (get_docking_approach_pos) and where the ship is, in the dock's
  -- frame; the probe keeps the answer (tools.dock_approach[SLOT]) for dock_station's route.
  elseif op=='dock_approach' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','DockApproach',{ship=tools.slot_lua(e[3]),slot=e[3]});return 'lua' end
  -- Test fixture (MD DockWarp, canonical): {tick,'dock_warp',SLOT[,UP[,AHEAD]]}: that slot's ship UP metres (30) over the
  -- dock it was assigned and AHEAD metres (0) along the dock's z, facing along it, at rest, as if a person had flown
  -- it there (within the docking computer's reach; X4's own check then lands it).
  elseif op=='dock_warp' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','DockWarp',{ship=tools.slot_lua(e[3]),slot=e[3],up=e[4] or 30,ahead=e[5] or 0});return 'lua' end
  elseif op=='hull_set' then call=function()
    local lua=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3])
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Hull',{ship=lua,percent=e[4]});return 'lua' end
  -- Test only (MD Friendly, canonical: in every client's plan at the same tick; friendly fire, 24 Nov):
  -- {tick,'friendly',SLOT,NAME[,FACTION[,MACRO[,METRES[,PILOT[,SHIELD[,HULL[,HOLD[,FLOOR]]]]]]]]}: a ship of FACTION (terran)
  -- with its pilot (PILOT 0: none, it stays put; SHIELD 0: its shield down at the start; HULL: its hull percentage; HOLD 1: its
  -- pilot waits where it is; FLOOR: its minimum hull percentage), called NAME, METRES (400) straight ahead of slot SLOT's ship
  -- (MACRO: ship_ter_m_corvette_01_a_macro).
  -- {tick,'target_named',NAME[,FACTION]}: this
  -- client's player targets the ship of that name and owner (the soft target, as a click on it).
  elseif op=='friendly' then call=function()
    -- short keys, defaults left out: a UI event must fit one replay event (UIREP_EVENT_FIT, 160 bytes)
    local t={n=tools.slot_lua(e[3]),a=e[4],d=e[7] or 400,p=e[8]~=0,s=e[9]==0,w=e[11]==1}
    if e[5] and e[5]~='terran' then t.f=e[5] end
    if e[6] and e[6]~='ship_ter_m_corvette_01_a_macro' then t.m=e[6] end
    if e[10] then t.h=e[10] end
    if e[12] then t.k=e[12] end
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Friendly',t);return 'lua' end
  -- Test only (MD Hostile, canonical by its UI event; enemy AI per player, 25 Nov):
  -- {tick,'hostile',SLOT,NAME[,FACTION[,MACRO[,METRES[,COUNT[,PURPOSE[,WARP[,FLOOR[,ORDER]]]]]]]]}: COUNT (2) armed ships
  -- of FACTION (xenon, MACRO ship_xen_s_fighter_02_a_macro) with their pilots and X4's Patrol order (ORDER 'recon':
  -- Recon), METRES (3000) ahead of slot SLOT's ship, called NAME 1..COUNT; PURPOSE 1: they look for trade ships only; WARP: a sector macro the slot's
  -- ship is first put in (100 km, 0, -56 km; its protectors wait where they were); FLOOR: the slot ship's minimum hull
  -- percentage.
  elseif op=='hostile' then call=function()
    -- macros travel without 'ship_'/'cluster_' and '_macro' (MD puts them back), defaults not at all
    local function short(m) return type(m)=='string' and (m:gsub('^ship_',''):gsub('^cluster_',''):gsub('_macro$','')) or m end
    local t={n=tools.slot_lua(e[3]),a=e[4]}
    if e[5] and e[5]~='xenon' then t.f=e[5] end
    if e[6] and short(e[6])~='xen_s_fighter_02_a' then t.m=short(e[6]) end
    if e[7] and e[7]~=3000 then t.d=e[7] end
    if e[8] and e[8]~=2 then t.c=e[8] end
    if e[9]==1 then t.u=1 end
    if e[10] then t.w=short(e[10]) end
    if e[11] then t.k=e[11] end
    if e[12] and e[12]~='patrol' then t.o=e[12] end
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Hostile',t);return 'lua' end
  -- {tick,'leak_goto',NAME[,OWNER[,METRES]]}: test fixture (canonical by its UI event): this player's ship (its spacesuit)
  -- METRES (4) in front of the claim signal of the ship of that name and owner (ownerless: abandoned), looking at it (MD
  -- LeakGoto). {tick,'dock_named',NAME[,OWNER]}: this player asks to dock at that ship (RequestDockAt, the ship menu's
  -- "Request docking"; in the presentation scope, as suit_to).
  -- {tick,'suit_dock',NAME[,OWNER[,METRES]]}: test fixture (canonical by its UI event): this player's suit METRES (3) out
  -- from that ship's spacesuit dock, as if flown there (MD SuitDock; after its dock request, suit_to or dock_named).
  elseif op=='leak_goto' or op=='dock_named' or op=='suit_dock' then call=function()
    -- NAME may be a saved ID (16 hex digits): that ship, whoever owns it
    local saved=type(e[3])=='string' and #e[3]==16 and e[3]:match('^%x+$')
    for _,object in ipairs(saved and {tools.saved_lua(e[3])} or tools.env.GetContainedObjectsByOwner(e[4] or 'ownerless') or {}) do
     if saved or tools.env.GetComponentData(object,'name')==e[3] then
      local id=tools.env.ConvertIDTo64Bit(object)
      if op=='dock_named' then return tostring(tools.C.RequestDockAt(id,false))..' ship='..(tostring(id):gsub('ULL$','')) end
      if op=='suit_dock' then
       tools.env.AddUITriggeredEvent('X4LAN_UiReplay','SuitDock',{s=tools.slot_lua(tools.own_slot),t=object,d=tonumber(e[5]) or 3})
       return 'suit_dock ship='..(tostring(id):gsub('ULL$',''))
      end
      tools.env.AddUITriggeredEvent('X4LAN_UiReplay','LeakGoto',{s=tools.slot_lua(tools.own_slot),t=object,d=tonumber(e[5]) or 4})
      return 'leak_goto ship='..(tostring(id):gsub('ULL$',''))
     end
    end
    return 'no_ship' end
  elseif op=='target_named' then call=function()
    for _,object in ipairs(tools.env.GetContainedObjectsByOwner(e[4] or 'terran') or {}) do
     if tools.env.GetComponentData(object,'name')==e[3] then
      local id=tools.env.ConvertIDTo64Bit(object)
      return tostring(tools.C.SetSofttarget(id,''))..' ship='..(tostring(id):gsub('ULL$',''))
     end
    end
    return 'no_ship' end
  -- Test only (MD Hold, canonical: in every client's plan at the same tick): a slot's ship gets wares in its hold, so a
  -- scripted run has something to drop.
  elseif op=='hold_stock' then call=function()          -- (the ship: a slot or a saved ID)
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Hold',{ship=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3]),ware=e[4],amount=e[5] or 1});return 'lua' end
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
  -- (a fourth value names another ship of the player's, a slot or a saved ID: the map's loadout page sets any of them)
  elseif op=='turrets_mode' then call=function() tools.C.SetAllTurretModes(e[4] and tools.ship_id(e[4]) or tools.slot_id(tools.own_slot),e[3]);return 'ffi' end
  elseif op=='turrets_armed' then call=function() tools.C.SetAllTurretsArmed(e[4] and tools.ship_id(e[4]) or tools.slot_id(tools.own_slot),e[3] and true or false);return 'ffi' end
  elseif op=='drones_armed' then call=function() tools.C.SetAllDronesArmed(e[4] and tools.ship_id(e[4]) or tools.slot_id(tools.own_slot),e[3] and true or false);return 'ffi' end
  -- Read only: {tick,'systems_look',SHIP}: tools.systems_look (weapon groups, turret and drone modes, armed or not,
  -- the resupply level, the ship's answers to signals); {tick,'pilot_look'}: this client's flight assist and activity;
  -- {tick,'settings_look'}: the faction's settings as the player information menu reads them.
  elseif op=='systems_look' then call=function() return tools.systems_look(e[3]) end
  elseif op=='pilot_look' then call=function() return tools.pilot_look() end
  elseif op=='settings_look' then call=function() return tools.settings_look() end
  -- Test only (MD Marines and Abandoned, canonical): {tick,'marines_stock',SHIP,N}: N marines join that ship's crew;
  -- {tick,'abandon',SHIP,NAME[,METRES]}: an ownerless scout without a pilot, called NAME, appears METRES from that ship.
  elseif op=='marines_stock' or op=='abandon' then call=function()
    local lua=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3])
    if op=='marines_stock' then tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Marines',{ship=lua,amount=e[4] or 1})
    else tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Abandoned',{near=lua,name=e[4],metres=e[5] or 1500}) end
    return 'lua' end
  -- {tick,'wreck_look',NAME}: read only, the ship of that name (tools.wreck_look: owner, claimable, pilot).
  -- {tick,'claim',SHIP,NAME}: the interact menu's "Claim" on the ship of that name with one selected ship of the
  -- player's (a saved ID): the entry's conditions (claimable, marines aboard, in radar range, not docked), ClaimShip.
  elseif op=='wreck_look' then call=function() return tools.wreck_look(e[3]) end
  elseif op=='claim' then call=function()
    for _,line in ipairs({'bool ClaimShip(UniverseID shipid, UniverseID shiptobeclaimedid);',
      'float GetDistanceBetween(UniverseID component1id, UniverseID component2id);'}) do pcall(ffi.cdef,line) end
    local wreck,owner,target=tools.named_ship(e[4])
    if not wreck then return 'no_wreck' end
    local id,lua=tools.ship_id(e[3]),tools.saved_lua(e[3])
    local docked,marines,range=tools.env.GetComponentData(lua,'isdocked','hasavailablemarines','maxradarrange')
    local gap=tonumber(tools.C.GetDistanceBetween(target,id))
    if not tools.env.GetComponentData(wreck,'canbeclaimed') then return 'not_claimable owner='..tostring(owner) end
    if not marines then return 'no_marines' end
    if gap>(tonumber(range) or 0) then return 'too_far gap='..math.floor(gap) end
    if docked then return 'docked' end
    return 'claimed='..tostring(tools.C.ClaimShip(id,target))..' gap='..math.floor(gap) end
  -- A wing's group and its fleet (COMMANDER: a saved ID or a slot; GROUP 1..10), the faction family's claims of 19 Nov:
  --   {tick,'wing_look',COMMANDER,GROUP}: read only, tools.wing_look (the group's role, its five switches, the fleet's name)
  --   {tick,'wing_set',COMMANDER,GROUP,WHAT,VALUE}: WHAT 'role' (the group's assignment, as the map's dropdown and the
  --     interact menu set it after the ships' own AssignCommander orders, which are the assign op's), or one of the
  --     group's switches: 'sight' (attack on sight), 'dock' (dock at commander), 'reinforce', 'distress', 'resupply'
  --   {tick,'fleet_name',COMMANDER,NAME}: the map's rename of a fleet
  elseif op=='wing_look' then call=function() return tools.wing_look(e[3],e[4]) end
  elseif op=='wing_set' then call=function()
    local calls={role='SetSubordinateGroupAssignment',sight='SetSubordinateGroupAttackOnSight',dock='SetSubordinateGroupDockAtCommander',
     reinforce='SetSubordinateGroupReinforceFleet',distress='SetSubordinateGroupRespondToDistressCalls',resupply='SetSubordinateGroupResupplyAtFleet'}
    local name=calls[e[5]]
    if not name then error('ui replay script: wing_set '..tostring(e[5])) end
    pcall(ffi.cdef,'void '..name..'(UniverseID controllableid, int group, '..(e[5]=='role' and 'const char* assignment' or 'bool value')..');')
    if e[5]=='role' then tools.C[name](tools.ship_id(e[3]),e[4],e[6]) else tools.C[name](tools.ship_id(e[3]),e[4],e[6] and true or false) end
    return 'ffi' end
  elseif op=='fleet_name' then call=function()
    pcall(ffi.cdef,'void SetFleetName(UniverseID controllableid, const char* fleetname);')
    tools.C.SetFleetName(tools.ship_id(e[3]),e[4]);return 'ffi' end
  -- "Mark as hostile", as the user question menu's Yes makes it after the interact menu's entry (SHIP: any ship, a saved
  -- ID): {tick,'mark_hostile',SHIP}; read only {tick,'relation_look',SHIP}: 'hostile=true|false enemy=.. owner=..'.
  elseif op=='mark_hostile' then call=function()
    pcall(ffi.cdef,'void SetRelationBoostToFaction(UniverseID componentid, const char* factionid, const char* reasonid, float boostvalue, float decayrate, double decaydelay);')
    tools.C.SetRelationBoostToFaction(tools.ship_id(e[3]),'player','markedashostile',-1,1,600);return 'ffi' end
  elseif op=='relation_look' then call=function()
    local lua=tools.saved_lua(e[3])
    local hostile,enemy,owner=tools.env.GetComponentData(lua,'ishostile','isenemy','owner')
    return 'hostile='..tostring(hostile and true or false)..' enemy='..tostring(enemy and true or false)..' owner='..tostring(owner)..' ' end
  -- A docked ship of the player's and its container's internal storage (SHIP: a saved ID or a slot):
  --   {tick,'storage_look',SHIP}: read only, tools.storage_look
  --   {tick,'store',SHIP} / {tick,'retrieve',SHIP}: the interact menu's "Put into storage" and "Retrieve from storage"
  --   (menu.buttonPutIntoStorage: PutShipIntoStorage for a ship at an outside dock, else RequestShipFromInternalStorage2)
  elseif op=='storage_look' then call=function() return tools.storage_look(e[3]) end
  elseif op=='store' or op=='retrieve' then call=function()
    for _,line in ipairs({'void PutShipIntoStorage(UniverseID containerid, UniverseID shipid);',
      'bool RequestShipFromInternalStorage2(UniverseID shipid, bool highpriority, UniverseID refcomponentid);',
      'UniverseID GetContextByClass(UniverseID componentid, const char* classname, bool includeself);',
      'bool IsShipAtExternalDock(UniverseID shipid);','UniverseID GetPlayerID(void);','bool IsShipBeingRetrieved(UniverseID shipid);',
      'bool HasContainerFreeInternalShipStorage(UniverseID containerid, UniverseID shipid);',
      'bool CanPutShipIntoStorage(UniverseID containerid, UniverseID shipid);'}) do pcall(ffi.cdef,line) end
    local id=tools.ship_id(e[3])
    local container=tools.C.GetContextByClass(id,'container',false)
    if container==0 then return 'no_container' end
    -- the entry's conditions (menu_interactmenu.lua "putinstorage"): greyed out without room, or while X4 does not
    -- let the ship in (CanPutShipIntoStorage), or while a stored ship is on its way out
    if op=='store' then
     if not tools.C.IsShipAtExternalDock(id) then return 'inside' end
     if not tools.C.HasContainerFreeInternalShipStorage(container,id) then return 'greyed no_room' end
     if not tools.C.CanPutShipIntoStorage(container,id) then return 'greyed cannot' end
     tools.C.PutShipIntoStorage(container,id);return 'put'
    end
    if tools.C.IsShipAtExternalDock(id) then return 'outside' end
    if tools.C.IsShipBeingRetrieved(id) then return 'greyed on_its_way' end
    return 'requested='..tostring(tools.C.RequestShipFromInternalStorage2(id,false,tools.C.GetPlayerID())) end
  -- One part of a ship's weapons as the docked menu and the map's loadout page set it. SHIP is a slot or a saved ID, N
  -- the part's position in tools.systems' lists (1-based).
  --   {tick,'tgroup_mode',SHIP,N,MODE} / {tick,'tgroup_armed',SHIP,N,BOOL}: turret group N
  --   {tick,'turret_set',SHIP,N,MODE}: single turret N
  --   {tick,'part_mode',SHIP,MODE} / {tick,'part_armed',SHIP,BOOL}: the ship's first turret part, whichever kind it is:
  --     turret group 1 when the ship has groups (a destroyer), else single turret 1 (a frigate)
  --   {tick,'drone_set',SHIP,KIND,N}: the drone type's Nth mode / {tick,'drone_armed',SHIP,KIND,BOOL}
  --   {tick,'weapon_group',SHIP,N,PRIMARY,GROUP,BOOL}: weapon N in or out of a weapon group (the group's checkbox)
  elseif op=='tgroup_mode' or op=='tgroup_armed' then call=function()
    for _,line in ipairs({'void SetTurretGroupMode2(UniverseID defensibleid, UniverseID contextid, const char* path, const char* group, const char* mode);',
      'void SetTurretGroupArmed(UniverseID defensibleid, UniverseID contextid, const char* path, const char* group, bool arm);'}) do pcall(ffi.cdef,line) end
    local s=tools.systems(e[3])
    local g=assert(s.groups[e[4]],'ui replay script: no turret group '..tostring(e[4]))
    if op=='tgroup_mode' then tools.C.SetTurretGroupMode2(s.id,g.context,g.path,g.group,e[5])
    else tools.C.SetTurretGroupArmed(s.id,g.context,g.path,g.group,e[5] and true or false) end
    return 'ffi group='..(g.group~='' and g.group or g.path) end
  elseif op=='part_mode' or op=='part_armed' then call=function()
    for _,line in ipairs({'void SetTurretGroupMode2(UniverseID defensibleid, UniverseID contextid, const char* path, const char* group, const char* mode);',
      'void SetTurretGroupArmed(UniverseID defensibleid, UniverseID contextid, const char* path, const char* group, bool arm);',
      'void SetWeaponMode(UniverseID weaponid, const char* mode);','void SetWeaponArmed(UniverseID weaponid, bool arm);'}) do pcall(ffi.cdef,line) end
    local s=tools.systems(e[3])
    local g,t=s.groups[1],s.turrets[1]
    if g then
     if op=='part_mode' then tools.C.SetTurretGroupMode2(s.id,g.context,g.path,g.group,e[4])
     else tools.C.SetTurretGroupArmed(s.id,g.context,g.path,g.group,e[4] and true or false) end
     return 'ffi group='..(g.group~='' and g.group or g.path)
    elseif t then
     if op=='part_mode' then tools.C.SetWeaponMode(t.id,e[4]) else tools.C.SetWeaponArmed(t.id,e[4] and true or false) end
     return 'ffi turret'
    end
    return 'no_turrets' end
  elseif op=='turret_set' then call=function()
    pcall(ffi.cdef,'void SetWeaponMode(UniverseID weaponid, const char* mode);')
    local t=assert(tools.systems(e[3]).turrets[e[4]],'ui replay script: no single turret '..tostring(e[4]))
    tools.C.SetWeaponMode(t.id,e[5]);return 'ffi' end
  elseif op=='drone_set' or op=='drone_armed' then call=function()
    for _,line in ipairs({'void SetDroneMode(UniverseID defensibleid, const char* dronetype, const char* mode);',
      'void SetDroneTypeArmed(UniverseID defensibleid, const char* dronetype, bool arm);'}) do pcall(ffi.cdef,line) end
    local s=tools.systems(e[3])
    local drone
    for _,d in ipairs(s.drones) do if d.kind==e[4] then drone=d end end
    if not drone then return 'no_drones' end
    if op=='drone_armed' then tools.C.SetDroneTypeArmed(s.id,e[4],e[5] and true or false);return 'ffi' end
    local mode=assert(drone.modes[e[5]],'ui replay script: no drone mode '..tostring(e[5]))
    tools.C.SetDroneMode(s.id,e[4],mode);return 'ffi mode='..mode end
  elseif op=='weapon_group' then call=function()
    pcall(ffi.cdef,'void SetWeaponGroup(UniverseID defensibleid, UniverseID weaponid, bool primary, uint32_t groupidx, bool value);')
    local s=tools.systems(e[3])
    local w=assert(s.weapons[e[4]],'ui replay script: no weapon '..tostring(e[4]))
    tools.C.SetWeaponGroup(s.id,w.id,e[5] and true or false,e[6],e[7] and true or false);return 'ffi' end
  elseif op=='drop_cargo' then call=function() return tostring(tools.C.DropCargo(tools.slot_id(tools.own_slot),e[3],e[4])) end
  -- Test only (MD Stock, canonical: in every client's plan at the same tick): a slot's ship gets deployables to launch.
  elseif op=='launch_stock' then call=function()          -- (the ship: a slot or a saved ID)
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','Stock',{ship=type(e[3])=='number' and tools.slot_lua(e[3]) or tools.saved_lua(e[3]),macro=e[4],amount=e[5] or 1});return 'lua' end
  -- Test only (MD ProbeFar, canonical): a resource probe launched at the first ore the galaxy's sectors have.
  elseif op=='probe_far' then call=function() tools.env.AddUITriggeredEvent('X4LAN_UiReplay','ProbeFar',{});return 'lua' end
  -- Test only (MD GroupsLook, canonical): the members of md.Setup's four player groups, and whether each slot's ship is
  -- among the controlled ones (native_gate_player_groups).
  elseif op=='groups_look' then call=function()
    local ships={}
    for k=1,tools.slots or 0 do ships[k]=tools.slot_lua(k) end
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','GroupsLook',ships);return 'lua' end
  -- Test only (MD EscortLook, canonical): the ships that protect each slot's ship (the Terran cadet's wingmate), their
  -- formation, distance and target, and the pilot a protector sees on the ship (native_gate_player_escort.xml).
  elseif op=='escort_look' then call=function()
    local ships={}
    for k=1,tools.slots or 0 do ships[k]=tools.slot_lua(k) end
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','EscortLook',ships);return 'lua' end
  -- Test only (MD EscortTarget): {tick,'escort_target'[,macro][,metres]} as a person's click: a Xenon ship without a pilot
  -- that far ahead of this player's ship (default a scout at 500 m) becomes this player's target (the galaxy signal
  -- runs in this player's scope on every client, as goto_slot's).
  elseif op=='escort_target' then call=function()
    tools.env.SignalObject(tools.env.ConvertStringTo64Bit(tostring(tools.C.GetPlayerGalaxyID())),'x4lan_escort_target',
      tostring(e[3] or 'ship_xen_s_scout_01_a_macro'),tonumber(e[4]) or 500)
    return 'escort target' end
  -- Test only, read only (scoped: the colours of this client's own HUD): X4's relation status of each slot's ship and of
  -- the protectors the last escort_look found ($X4LAN_EscortGuards on the slot's player entity), as the target system
  -- reads it (GetRelationStatus4: 5 own property = green, 4 allied, 3 friendly, 2 neutral = blue, 1 enemy, 0 hostile).
  elseif op=='escort_colour' then call=function()
    pcall(ffi.cdef,'typedef struct { int relationStatus; int relationValue; int relationLEDValue; bool isBoostedValue; const char* owningFactionID; } RelationDetails2;')
    pcall(ffi.cdef,'RelationDetails2 GetRelationStatus4(const UniverseID componentid, const char*const connectionname);')
    local function status(id)
     local ok,r=pcall(tools.C.GetRelationStatus4,id,'')
     return ok and (tostring(r.relationStatus)..'/'..ffi.string(r.owningFactionID)) or '?'
    end
    -- the map's list and info colours read these (menu_map getContainerNameAndColors / getObjectColor: a mission
    -- target amber, the player's own green, hostile or enemy red, else white), the HUD's its faction colour option
    local function flags(lua)
     local ok,own,mission,enemy,hostile=pcall(tools.env.GetComponentData,lua,'isplayerowned','ismissiontarget','isenemy','ishostile')
     if not ok then return '' end
     return '/own='..tostring(own)..'/mission='..tostring(mission)..'/enemy='..tostring(enemy)..'/hostile='..tostring(hostile)
    end
    local parts={}
    pcall(ffi.cdef,'int GetConfigSetting(const char*const setting);')
    local okf,factioncolour=pcall(function() return tools.C.GetConfigSetting('factioncolortargetsystem') end)
    parts[1]='factioncolour='..(okf and tostring(factioncolour) or '?')
    for k=1,tools.slots or 0 do
     local ship=tools.slot_id(k)
     local text='slot'..k..'='..(ship and status(ship) or 'none')
     local player=tools.gate.ui_inventory_player and tools.gate.ui_inventory_player(k)
     local ok,guards=pcall(function() return player and tools.env.GetNPCBlackboard(tools.env.ConvertStringToLuaID(player),'$X4LAN_EscortGuards') end)
     if ok and type(guards)=='table' then
      for _,g in ipairs(guards) do
       local id=tools.env.ConvertStringTo64Bit(tostring(g))
       text=text..',guard:'..(tostring(id):gsub('ULL$',''))..'='..status(id)..flags(g)
      end
     else text=text..',guards=?' end
     parts[#parts+1]=text
    end
    return table.concat(parts,' ') end
  -- Test only (MD EscortFront, canonical): the protectors of slot K's ship (default this client's) are put 150 m ahead
  -- of it, looking its way, so its player's camera sees them (their order brings them back into the formation).
  elseif op=='escort_front' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_UiReplay','EscortFront',tools.slot_lua(e[3] or tools.own_slot));return 'lua' end
  -- Test only (scoped, as a click on it): the first protector the last escort_look found for this client's own ship
  -- becomes this player's target (the target box and the target monitor show it).
  elseif op=='escort_select' then call=function()
    local player=tools.gate.ui_inventory_player and tools.gate.ui_inventory_player(tools.own_slot)
    local guards=player and tools.env.GetNPCBlackboard(tools.env.ConvertStringToLuaID(player),'$X4LAN_EscortGuards')
    if type(guards)~='table' or not guards[1] then return 'no_guard' end
    local id=tools.env.ConvertStringTo64Bit(tostring(guards[1]))
    return tostring(tools.C.SetSofttarget(id,''))..' '..(tostring(id):gsub('ULL$','')) end
  -- Test only (this client's open map, as a look at it): the colour the map's lists and its info header give this
  -- player's first protector (menu_map getContainerNameAndColors / getObjectColor) against the map's player colour; a
  -- question the map opened with (its first-open hint) is closed first, so a picture shows the map.
  elseif op=='escort_map' then call=function()
    local menu=tools.menu('MapMenu')
    if not menu then return 'no_map' end
    if menu.contextMenuMode and menu.closeContextMenu then pcall(menu.closeContextMenu) end
    local player=tools.gate.ui_inventory_player and tools.gate.ui_inventory_player(tools.own_slot)
    local guards=player and tools.env.GetNPCBlackboard(tools.env.ConvertStringToLuaID(player),'$X4LAN_EscortGuards')
    if type(guards)~='table' or not guards[1] then return 'no_guard' end
    local function rgb(c) return type(c)=='table' and string.format('%d,%d,%d',c.r or -1,c.g or -1,c.b or -1) or tostring(c) end
    local id=tools.env.ConvertStringTo64Bit(tostring(guards[1]))
    local okl,_,listcolour=pcall(menu.getContainerNameAndColors,guards[1],0,false,false)
    local oko,infocolour=pcall(menu.getObjectColor,id)
    local colours=menu.holomapcolor or {}
    return 'list='..(okl and rgb(listcolour) or '?')..' info='..(oko and rgb(infocolour) or '?')..' player='..rgb(colours.playercolor)..
      ' friend='..rgb(colours.friendcolor)..' mission='..rgb(colours.missioncolor) end
  elseif op=='comm' then call=function() return tools.env.StartConversationFromMenu('default',tools.slot_lua(e[3])) end
  -- Missions and conversations (lockstep_ui_missions.h). mis_fixture: the test offer (MD native_gate_ui_replay.xml), canonical;
  -- the rest as the briefing menu, the map's mission list and the dialog call the stock entries.
  elseif op=='mis_fixture' then call=function() tools.env.AddUITriggeredEvent('X4LAN_UiMissions','Fixture',{location=tools.slot_lua(e[3] or 1)});return 'lua' end
  -- Mission type tests (MD MissionTest; tools/dev/mission_matrix.py writes the plans): mt_offer, canonical like
  -- mis_fixture, makes the test offer of type e[3] for slot e[4]; mt_accept, in that slot's plan, accepts it as the
  -- stock menus accept an offer.
  elseif op=='mt_offer' then call=function()
    tools.env.AddUITriggeredEvent('X4LAN_MissionTest','Offer',{type=tostring(e[3]),slot=tostring(e[4] or 1),location=tools.slot_lua(e[4] or 1)});return 'lua' end
  elseif op=='mt_accept' then call=function()
    local offer,actor=tools.missions.find_offer('X4LAN mt '..tostring(e[3])..' '..tostring(e[4] or 1))
    tools.env.SignalObject(actor,'accept',tools.env.ConvertStringToLuaID(offer));return offer end
  -- The stock offers as this client's menus list them (tools/dev/mission_matrix.py offers): offers_list names them
  -- (type, faction; the row is the result), offer_accept_n accepts the e[3]-th as the menus accept an offer (an offer
  -- that is taken through a talk has no actor to signal: refused here), mis_list names the missions, mis_abort_new
  -- aborts those that were not there at the last offers_list (a start's own story stays).
  elseif op=='offers_list' then call=function()
    local offers,parts=tools.missions.offers(),{}
    tools.missions.before={}
    for _,m in ipairs(tools.missions.missions_now()) do tools.missions.before[m.id]=true end
    for i,o in ipairs(offers) do parts[#parts+1]=i..':'..o.kind..':'..o.faction..(o.actor and '' or ':talk') end
    return 'n='..#offers..' '..table.concat(parts,' ') end
  elseif op=='offer_accept_n' then call=function()
    local o=tools.missions.offers()[e[3]]
    if not o then return 'none' end
    if not o.actor then return 'talk '..o.kind end
    tools.env.SignalObject(o.actor,'accept',tools.env.ConvertStringToLuaID(o.id));return 'accepted '..o.kind..' '..o.name:gsub('%s+','_') end
  -- offer_accept_next: the first stock offer (no tutorial, one with a client to signal) this plan has not tried yet;
  -- mis_new: the missions that were not there at the last offers_list.
  elseif op=='offer_accept_next' then call=function()
    tools.missions.tried=tools.missions.tried or {}
    for i,o in ipairs(tools.missions.offers()) do
     if o.actor and not o.kind:find('^tutorial') and not tools.missions.tried[o.id] then
      tools.missions.tried[o.id]=true
      tools.env.SignalObject(o.actor,'accept',tools.env.ConvertStringToLuaID(o.id))
      return 'accepted '..o.kind..' '..o.faction..' '..(o.name:gsub('%s+','_'))
     end
    end
    return 'none' end
  elseif op=='mis_new' then call=function()
    local parts={}
    for _,m in ipairs(tools.missions.missions_now()) do
     if not (tools.missions.before or {})[m.id] then parts[#parts+1]=(m.name:gsub('%s+','_')) end
    end
    return 'new='..#parts..' '..table.concat(parts,' ') end
  -- A real stock offer finished by this client's player (tools/dev/mission_matrix.py real):
  --  {tick,'offer_accept_named',name}: accepts the stock offer whose name (blanks as _) holds that text, as the
  --    menus accept an offer;
  --  {tick,'mis_goto',metres}: puts this player's ship that far from the first target of the first new mission's
  --    objective, looking at it (MD MtGoto, a signal on the galaxy made by this client: every client replays it in
  --    this player's scope. Not a UI event: one made in slot k's scope is posted on slot k's player, where no cue
  --    that nobody owns listens; seen live, 17 Nov);
  --  {tick,'mis_goto_at',x,y,z[,name]}: the same flight to a spot given in metres relative to that target, in the
  --    target's own frame (a plot's "fly to this part of the station"); with a name the mission whose name holds
  --    that text (blanks as _), new or not;
  --  {tick,'mis_target'}: that target becomes this player's target, as a click on it would make it;
  --  {tick,'account'}: the account this client's menus show (read only).
  elseif op=='offer_accept_named' then call=function()
    for _,o in ipairs(tools.missions.offers()) do
     local name=o.name:gsub('%s+','_')
     if name:find(tostring(e[3]),1,true) then
      if not o.actor then return 'talk '..name end
      tools.env.SignalObject(o.actor,'accept',tools.env.ConvertStringToLuaID(o.id))
      return 'accepted '..o.kind..' '..o.faction..' '..name
     end
    end
    return 'none' end
  -- {tick,'goto_slot',k,metres}: this player's ship is put that far from slot k's ship, looking at it (MD MtGoto, the
  -- same signal as mis_goto): two players meet in one sector without the flight.
  -- {tick,'ui_ping'}: a UI event of this client's menus kind that no stock script listens for (screen X4LAN_Audit); the
  -- audit's shared cue AuditShared, which no player owns, logs who it heard it from.
  elseif op=='ui_ping' then call=function() tools.env.AddUITriggeredEvent('X4LAN_Audit','Ping',tonumber(e[3]) or 1);return 'lua' end
  elseif op=='goto_slot' then call=function()
    tools.env.SignalObject(tools.env.ConvertStringTo64Bit(tostring(tools.C.GetPlayerGalaxyID())),'x4lan_goto',tools.slot_lua(e[3]),tonumber(e[4]) or 300)
    return 'goto slot '..tostring(e[3]) end
  -- Test only: {tick,'goto_obj',SAVED,metres}: the same for a saved object (a gate, a highway's entry gate), so a test
  -- starts in front of a transition without the flight; {tick,'goto_obj',SAVED,{x,y,z}}: at that spot in the object's own
  -- frame (metres), looking at it (mis_goto_at's form: a gate's front is along its own axis, not the zone's).
  elseif op=='goto_obj' then call=function()
    local at=type(e[4])=='table' and {tonumber(e[4][1]) or 0,tonumber(e[4][2]) or 0,tonumber(e[4][3]) or 0} or (tonumber(e[4]) or 300)
    tools.env.SignalObject(tools.env.ConvertStringTo64Bit(tostring(tools.C.GetPlayerGalaxyID())),'x4lan_goto',tools.saved_lua(e[3]),at)
    return 'goto '..tostring(e[3]) end
  -- Test only, read only (scoped: this client's own player): {tick,'place'}: the sector and zone its ship is in now (the
  -- zone may be a highway), the position in the sector and the speed; under 'every', the course of a transition.
  elseif op=='place' then call=function()
    local env=tools.env
    local ship=env.ConvertStringToLuaID(tostring(tools.C.GetPlayerOccupiedShipID()))
    if not ship or tostring(ship)=='ID: 0' then return 'ship=none' end
    local sector,zone=env.GetContextByClass(ship,'sector',false),env.GetContextByClass(ship,'zone',false)
    local highway=env.GetContextByClass(ship,'highway',false)
    local p=tools.C.GetObjectPositionInSector(env.ConvertIDTo64Bit(ship))
    -- the player's activity and the ship's pilot's AI command (who flies it: a script left running shows here)
    local pilot=env.GetComponentData(ship,'assignedpilot')
    local command,action
    if pilot then command,action=env.GetComponentData(pilot,'aicommand','aicommandaction') end
    local function word(v) return (tostring(v):gsub('%s+','_')) end
    return string.format('sector=%s zone=%s highway=%s pos=%.0f,%.0f,%.0f speed=%s activity=%s ai=%s/%s',tostring(sector and env.GetComponentData(sector,'macro')),
      tostring(zone and env.GetComponentData(zone,'macro')),tostring(highway and env.GetComponentData(highway,'macro')),p.x,p.y,p.z,
      tostring(math.floor(tonumber(env.GetComponentData(ship,'speed')) or -1)),word(env.GetPlayerActivity and env.GetPlayerActivity()),
      word(command),word(action)) end
  elseif op=='mis_goto' or op=='mis_target' or op=='mis_goto_at' then call=function()
    for i=1,tonumber(tools.env.GetNumMissions()) or 0 do
     local id,name=tools.env.GetMissionDetails(i)
     id=tostring(id)
     local wanted
     if op=='mis_goto_at' and e[6] then wanted=tostring(name):gsub('%s+','_'):find(tostring(e[6]),1,true)~=nil
     else wanted=not (tools.missions.before or {})[id] end
     if wanted then
      local target=(tostring(tools.C.GetMissionIDObjectiveTarget(tools.env.ConvertStringTo64Bit(id),1)):gsub('ULL$',''))
      if target=='0' then return 'no_target' end
      if op=='mis_target' then return tostring(tools.C.SetSofttarget(tools.env.ConvertStringTo64Bit(target),''))..' '..target end
      if op=='mis_goto_at' then
       tools.env.SignalObject(tools.env.ConvertStringTo64Bit(tostring(tools.C.GetPlayerGalaxyID())),'x4lan_goto',
        tools.env.ConvertStringToLuaID(target),{tonumber(e[3]) or 0,tonumber(e[4]) or 0,tonumber(e[5]) or 0})
       return 'goto_at '..target
      end
      tools.env.SignalObject(tools.env.ConvertStringTo64Bit(tostring(tools.C.GetPlayerGalaxyID())),'x4lan_goto',
       tools.env.ConvertStringToLuaID(target),tonumber(e[3]) or 500)
      return 'goto '..target
     end
    end
    return 'no_mission' end
  elseif op=='account' then call=function() return tostring(tools.env.GetPlayerMoney()) end
  -- mis_look (read only): what each mission that was not there at the last offers_list asks for, as this client's
  -- menus would show it: type, objective text, progress and the objective's targets (class, name, sector).
  elseif op=='mis_look' then call=function()
    local parts={}
    local function word(v) return (tostring(v):gsub('%s+','_')) end
    for i=1,tonumber(tools.env.GetNumMissions()) or 0 do
     local d={tools.env.GetMissionDetails(i)}
     if not (tools.missions.before or {})[tostring(d[1])] then
      local o={tools.env.GetMissionObjective(i)}
      local targets={}
      for index=0,3 do
       local ok,target=pcall(function() return tools.C.GetMissionIDObjectiveTarget(tools.env.ConvertStringTo64Bit(tostring(d[1])),index) end)
       local id=ok and (tostring(target):gsub('ULL$','')) or '0'
       if id~='0' then
        local lua_id=tools.env.ConvertStringToLuaID(id)
        local ok2,name,sector=pcall(tools.env.GetComponentData,lua_id,'name','sector')
        local ok3,class=pcall(function() return ffi.string(tools.C.GetComponentClass(tools.env.ConvertStringTo64Bit(id))) end)
        targets[#targets+1]=index..'='..id..'/'..(ok3 and class or '?')..'/'..word(ok2 and name or '?')..'/'..word(ok2 and sector or '?')
       end
      end
      parts[#parts+1]=word(d[2])..'|'..word(d[6])..'/'..word(d[7])..'|'..word(o[1])..'|'..word(o[4])..'/'..word(o[5])..'|'..table.concat(targets,',')
     end
    end
    return 'look='..#parts..' '..table.concat(parts,' ; ') end
  elseif op=='mis_list' then call=function()
    local parts={}
    for _,m in ipairs(tools.missions.missions_now()) do parts[#parts+1]=(m.name:gsub('%s+','_')) end
    return 'n='..#parts..' '..table.concat(parts,' ') end
  elseif op=='mis_abort_new' then call=function()
    local n=0
    for _,m in ipairs(tools.missions.missions_now()) do
     if not (tools.missions.before or {})[m.id] then tools.C.AbortMission(tools.env.ConvertStringTo64Bit(m.id));n=n+1 end
    end
    return 'aborted='..n end
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
  -- A fleet ship's question on the target monitor (an interactive notification; lockstep_md_media.h):
  --   {tick,'question_look'}: read only: 'question=ID', this client's interaction ID of the live question (0: none)
  --   {tick,'question_answer'}: the key or the click on the notification: the UI's RaisePlayerInteractionEvent(ID)
  elseif op=='question_look' then call=function()
    return 'question='..tostring(tools.gate.md_media_question and tools.gate.md_media_question() or 'none')..' ' end
  elseif op=='question_answer' then call=function()
    local id=tools.gate.md_media_question and tools.gate.md_media_question() or 0
    if id==0 then return 'no_question' end
    tools.env.RaisePlayerInteractionEvent(id);return 'answered id='..id end
  -- The C function itself, as a script that held it before the arming (the core UI's dialog menu): the guard's hook.
  elseif op=='dialog_native' then call=function() tools.env.X4LAN_UI_REPLAY_STOCK.SelectDialogOption(e[3]);return 'native' end
  elseif op=='conv_cancel' then call=function() tools.env.CancelConversation();return 'lua' end
  -- {tick,'conversation_look'}: read only, the world's one conversation and its owner slot (lockstep_conversation_owner.h
  -- status: owner=K, 0 nobody; manager=STATE.CURRENT, 1.0 idle), 'none' without conversation owners.
  elseif op=='conversation_look' then call=function()
    return tools.gate.conversation_owner_status and tostring(tools.gate.conversation_owner_status())..' ' or 'none' end
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
  -- A faction representative and the shop it opens (md/npc_factionrepresentative.xml: the conversation's choices
  -- 'Show me your licences' and 'I would like to buy a blueprint' open BlueprintOrLicenceTraderMenu by
  -- open_conversation_menu; its Confirm adds the licences or learns the blueprints and pays the representative's station):
  --   {tick,'rep_talk',FACTION}: comm the faction's representative (GetFactionRepresentative), as the interact menu's
  --     Comm does (StartConversationFromMenu 'default'); answers the person and its station.
  --   {tick,'trader_look'}: read only: the trader menu open on this client (blueprints/licences or an item trader's),
  --     its mode, the trader's faction, what is selected and the money it read.
  --   {tick,'trader_pick',WHICH}: in the open BlueprintOrLicenceTraderMenu, select a licence or blueprint the player can
  --     buy (not held, affordable, the licence or relation it needs held; WHICH 1 the cheapest, 2 the next...) and press
  --     Confirm, as a person's two clicks do; answers what was picked and its price.
  --   {tick,'library_look',FACTION}: read only: the blueprints the player holds, the licences of FACTION the player
  --     holds, and the money.
  elseif op=='rep_talk' then call=function()
    pcall(tools.ffi.cdef,'UniverseID GetFactionRepresentative(const char* factionid);')
    local rep=tools.C.GetFactionRepresentative(e[3])
    if rep==0 then error('ui replay script: '..tostring(e[3])..' has no representative') end
    local actor=tools.env.ConvertStringToLuaID(tostring(rep))
    local name,station=tools.env.GetComponentData(actor,'name'),tools.env.GetContextByClass(actor,'container',false)
    tools.env.StartConversationFromMenu('default',actor)
    return (tostring(rep):gsub('ULL$',''))..' name='..tostring(name)..' station='..tostring(station and tools.env.GetComponentData(station,'name')) end
  --   {tick,'rep_look',FACTION}: read only: the room the representative is in (its macro) and that room's station: X4
  --     moves the representative from the headquarters' control room into an office of its own (a dynamic interior)
  --     when the headquarters comes near a player (attention nearby), and only then does its conversation offer the shop.
  elseif op=='rep_look' then call=function()
    pcall(tools.ffi.cdef,'UniverseID GetFactionRepresentative(const char* factionid);')
    local rep=tools.C.GetFactionRepresentative(e[3])
    if rep==0 then return 'rep=none' end
    local env=tools.env
    local actor=env.ConvertStringToLuaID(tostring(rep))
    local room=env.GetContextByClass(actor,'room',false)
    local station=env.GetContextByClass(actor,'station',false)
    return 'room='..tostring(room and env.GetComponentData(room,'macro'))..' station='..tostring(station and env.GetComponentData(station,'idcode')) end
  -- An item trader or a shady guy on a station (md/npc_itemtrader.xml: platform actors at the docks, their conversation's
  -- 'Show me your wares' opens InventoryTraderMenu with the wares they hold):
  --   {tick,'npc_talk',SAVED_STATION,TYPE}: comm the first person of that type ('trader', 'shadyguy') of the station;
  --     answers the person or 'nobody' with the types the station has.
  --   {tick,'itemtrader_buy',WHICH}: in the open InventoryTraderMenu one piece of the WHICH-th cheapest ware the trader
  --     holds and sells, then Confirm, as the amount slider and the button do.
  elseif op=='npc_talk' then call=function()
    local env,seen=tools.env,{}
    for _,npc in ipairs(env.GetNPCs(tools.saved_id(e[3])) or {}) do
     local kind=env.GetComponentData(npc,'typestring')
     seen[#seen+1]=tostring(kind)
     if kind==e[4] then env.StartConversationFromMenu('default',npc);return tostring(npc)..' name='..tostring(env.GetComponentData(npc,'name')) end
    end
    return 'nobody types='..table.concat(seen,',') end
  elseif op=='itemtrader_buy' then call=function()
    local menu=tools.menu('InventoryTraderMenu')
    if not (menu and menu.shown) then return 'no_menu' end
    pcall(tools.ffi.cdef,'bool IsWillingToSellToPlayer(UniverseID componentid, const char* warename);')
    local offers={}
    for _,w in ipairs(menu.tradewares or {}) do
     local have=(menu.traderinventory or {})[w.ware]
     if have and (have.amount or 0)>0 and tools.C.IsWillingToSellToPlayer(menu.trader,w.ware) then offers[#offers+1]=w end
    end
    table.sort(offers,function(a,b) if a.price~=b.price then return a.price<b.price end return a.ware<b.ware end)
    local w=offers[e[3] or 1]
    if not w then return 'nothing_to_buy offers='..#offers end
    menu.updateTradeData(w.ware,1)
    local price=menu.totalprice
    menu.buttonConfirm()
    return 'bought ware='..w.ware..' price='..tostring(price)..' offers='..#offers end
  -- A station's control panel (lockstep_ui_compslot.h hack; the panels of a room are connections of its macro, e.g. the
  -- Security Office's 'connection35' hack_all_turrets, 'connection38' hack_shieldgenerators, 'connection43'
  -- hack_watchdogs):
  --   {tick,'panel_find'[,ROOM[,MACRO]]}: read only: the control panels of the room this client's player is in, or of
  --     the station's room whose transporter name holds ROOM ('Security'), or of the room of macro MACRO next to that one
  --     (the office beside the corridor that holds the transporter): each connection name ConnectionNN of the room that
  --     GetControlPanelInfo knows, name:type:ishack:hacked.
  --   {tick,'hack_yes',TYPE[,ROOM[,MACRO]]}: the question's Yes (UserQuestionMenu 'hackpanel': StartControlPanelHack) on the
  --     panel of that type in that room, as a person's click on Yes does. X4's hack itself does not ask where the player
  --     stands (the question's test before it does): on the station is enough.
  --   {tick,'panel_look',SLOT}: canonical, read only: inside that slot's scope, the panels of its player's room and
  --     whether that player has hacked them (X4's answer is the player's own memory of hacked panels).
  elseif op=='panel_find' then call=function()
    local room,found=tools.panels(e[3] and tools.room_named(e[3],e[4]) or nil)
    if not room then return 'room=none' end
    local parts={}
    for _,p in ipairs(found) do parts[#parts+1]=p.connection..':'..p.type..':'..tostring(p.ishack)..':'..tostring(p.hacked) end
    return 'room='..(tostring(room):gsub('ULL$',''))..' panels='..#found..' '..table.concat(parts,' ') end
  elseif op=='hack_yes' then call=function()
    local room,found=tools.panels(e[4] and tools.room_named(e[4],e[5]) or nil)
    for _,p in ipairs(found) do
     if p.type==e[3] then
      local slot=tools.ffi.new('UIComponentSlot');slot.component=room;slot.connection=p.connection
      tools.C.StartControlPanelHack(slot,e[3])
      return 'asked '..p.connection..' '..e[3]..' hacked_before='..tostring(p.hacked)
     end
    end
    return 'no_panel '..tostring(e[3])..' panels='..#found end
  elseif op=='panel_look' then call=function()
    local k=e[3]
    if k~=1 and tools.gate.mpp_scope(k)~=1 then error('ui replay script: no scope for slot '..tostring(k)) end
    local ok,result=pcall(function()
      local room,found=tools.panels()
      local parts={}
      for _,p in ipairs(found) do parts[#parts+1]=p.type..'='..tostring(p.hacked) end
      return 'slot='..k..' room='..(tostring(room or 'none'):gsub('ULL$',''))..' '..table.concat(parts,' ') end)
    if k~=1 and tools.gate.mpp_unscope(k)~=1 then error('ui replay script: the scope of slot '..tostring(k)..' did not close') end
    if not ok then error(result) end
    return result end
  elseif op=='trader_look' then call=function()
    for _,name in ipairs({'BlueprintOrLicenceTraderMenu','InventoryTraderMenu'}) do
     local menu=tools.menu(name)
     if menu and menu.shown then
      local picked=0
      for _ in pairs(type(menu.blueprintstopurchase)=='table' and menu.blueprintstopurchase or {}) do picked=picked+1 end
      for _ in pairs(type(menu.licencestopurchase)=='table' and menu.licencestopurchase or {}) do picked=picked+1 end
      return 'shown='..name..' mode='..tostring(menu.mode)..' faction='..tostring(menu.traderfaction)..' picked='..picked..
       ' total='..tostring(menu.totalprice)..' money='..tostring(menu.playermoney)
     end
    end
    return 'shown=none' end
  elseif op=='trader_pick' then call=function()
    local menu=tools.menu('BlueprintOrLicenceTraderMenu')
    if not (menu and menu.shown) then return 'no_menu' end
    local env=tools.env
    local which,money=e[3] or 1,env.GetPlayerMoney()
    local offers={}
    if menu.mode=='licence' then
     for _,licence in ipairs(menu.licences or {}) do
      if licence.issellable and not env.HasLicence('player',licence.type,menu.traderfaction) and licence.minrelation<=menu.uirelation
         and (licence.precursor==nil or env.HasLicence('player',licence.precursor,menu.traderfaction)) and licence.price<=money then
       offers[#offers+1]={price=licence.price,name=licence.type,pick=function() menu.buttonSelectLicence(licence,licence.price) end}
      end
     end
    else
     local function consider(ware,price)
      local tradelicence=env.GetWareData(ware,'tradelicence')
      if not menu.blueprintlist[ware] and price<money and env.HasLicence('player',tradelicence,menu.traderfaction) then
       offers[#offers+1]={price=price,name=ware,pick=function() menu.buttonSelectBlueprint(ware,price) end}
      end
     end
     for tag,wares in pairs(menu.table_wares or {}) do
      for key,value in pairs(wares) do
       if type(value)=='table' then for ware,price in pairs(value) do consider(ware,price) end else consider(key,value) end
      end
     end
    end
    table.sort(offers,function(a,b) if a.price~=b.price then return a.price<b.price end return a.name<b.name end)
    local offer=offers[which]
    if not offer then menu.buttonCancel();return 'nothing_to_buy offers='..#offers..' money='..tostring(money)..' cancelled' end
    offer.pick()
    menu.buttonConfirm()
    return 'bought '..tostring(menu.mode)..'='..offer.name..' price='..offer.price..' offers='..#offers..' money='..tostring(money) end
  elseif op=='library_look' then call=function()
    local env,C=tools.env,tools.C
    pcall(tools.ffi.cdef,'uint32_t GetNumBlueprints(const char* set, const char* category, const char* macroname);')
    local held=0
    for _,licence in ipairs(env.GetOwnLicences(e[3]) or {}) do if env.HasLicence('player',licence.type,e[3]) then held=held+1 end end
    return 'blueprints='..tostring(tonumber(C.GetNumBlueprints('','',''))).. ' licences='..held..' money='..tostring(env.GetPlayerMoney()) end
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
  -- {tick,'inv_total'}: this client's own inventory as its menus read it (the presentation scope): pieces and kinds.
  elseif op=='inv_total' then call=function()
    local pieces,kinds=0,0
    for _,item in pairs(tools.env.GetPlayerInventory() or {}) do pieces=pieces+(item.amount or 0);kinds=kinds+1 end
    return 'pieces='..pieces..' kinds='..kinds..' money='..tostring(tools.env.GetPlayerMoney()) end
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
T.SCRIPT_OPS={trade_to_ship=true,trade_from_ship=true,trade_loop=true,trade_buy=true,trade_sell=true,trade_price=true,trade_budget=true,trade_look=true,
 trade_exchange=true,exchange_look=true,exchange_vcargo=true}
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
 -- A ware exchange between two ships of the player's, as the map's trade menu in its exchange mode (SHIP is the selected
 -- ship, OTHER the one it exchanges with; slots or saved IDs):
 --   {tick,'trade_exchange',SHIP,OTHER,WARE,AMOUNT}: AMOUNT of the ware from SHIP to OTHER (OTHER's buy offer of the
 --     exchange list; a negative AMOUNT: from OTHER to SHIP, its sell offer), queued as the menu's Confirm queues it
 --   {tick,'exchange_look',SHIP,OTHER,WARE}: read only, the exchange list's offers of the ware ('offers=N buy=A sell=B')
 --   {tick,'exchange_vcargo',SHIP,OTHER,BOOL}: the trade menu's open (true) and close (false) in its exchange mode:
 --     SetVirtualCargoMode on both ships (the trade family's call), before the menu reads the exchange list
 if op=='exchange_vcargo' then return function()
   if e[5] then tools.env.SetVirtualCargoMode(shiplua(e[3]),true,-1);tools.env.SetVirtualCargoMode(shiplua(e[4]),true,-1)
   else tools.env.SetVirtualCargoMode(shiplua(e[3]),false);tools.env.SetVirtualCargoMode(shiplua(e[4]),false) end
   return 'lua' end end
 if op=='trade_exchange' or op=='exchange_look' then return function()
   local list=trade_items(tools.env.GetWareExchangeTradeList(shiplua(e[3]),shiplua(e[4])))
   if op=='exchange_look' then
    local parts={}
    for _,td in ipairs(list) do
     if td.ware==e[5] then parts[#parts+1]=(td.isselloffer and 'sell=' or 'buy=')..tostring(td.amount) end
    end
    return 'offers='..#parts..' '..table.concat(parts,' ')..(#parts>0 and ' ' or '')..'all='..#list..' ' end
   local want=e[6]>=0 and 'isbuyoffer' or 'isselloffer'
   local offer
   for _,td in ipairs(list) do
    if td.ware==e[5] and td[want] and not td.ismissionoffer then offer=td.id;break end
   end
   if offer==nil then return 'no_offer' end
   tools.env.AddTradeToShipQueue(offer,ship64(e[3]),math.abs(e[6]),false)
   return 'lua' end end
 -- {tick,'trade_price',station,ware,1 buy|0 sell,price}; {tick,'trade_budget',station,credits}: as the station menus call them.
 if op=='trade_price' then return function() tools.env.SetContainerWarePriceOverride(tools.saved_id(e[3]),e[4],e[5]==1,e[6]);return 'lua' end end
 if op=='trade_budget' then return function()
   tools.env.SetMaxBudget(tools.saved_id(e[3]),e[4]*3/2);tools.env.SetMinBudget(tools.saved_id(e[3]),e[4]);return 'lua' end end
 -- Read only: {tick,'trade_look',station,ware}: that ware's offers at the station as this client's menus list them
 -- (sell: the station sells, we buy), each price/amount, and the account.
 if op=='trade_look' then return function()
   local parts={}
   for _,td in ipairs(trade_items(tools.env.GetTradeList(tools.saved_lua(e[3])))) do
    if td.ware==e[4] and not td.ismissionoffer then
     parts[#parts+1]=(td.isselloffer and 'sell=' or 'buy=')..tostring(td.price)..'/'..tostring(td.amount)
    end
   end
   return 'offers='..#parts..' '..table.concat(parts,' ')..(#parts>0 and ' ' or '')..'money='..tostring(tools.env.GetPlayerMoney()) end end
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
 'UpdateSupplyOverrides','PauseProcessingModule','PauseProductionModule','SetDefensibleLoadoutLevel','RemoveCommander2',
 'SetSubordinateGroupAssignment','SetSubordinateGroupAttackOnSight','SetSubordinateGroupDockAtCommander','SetSubordinateGroupReinforceFleet',
 'SetSubordinateGroupRespondToDistressCalls','SetSubordinateGroupResupplyAtFleet','SetFleetName','SetRelationBoostToFaction'}
-- Claims whose component is not the slot faction's own (the authority check lets it be anybody's but another slot's ship):
-- "Mark as hostile" names the object that is to be hostile.
F.FOREIGN={SetRelationBoostToFaction=true}
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
 function R.authority(slot,subject,name)
  local mine=slot_ship(slot)
  if not mine then return 'no_ship' end
  if subject=='' or subject=='gone' then return nil end
  local other=p.slot_ships()[subject]
  if other and other~=slot then return 'slot_ship' end
  if F.FOREIGN[name] then return nil end
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
  local no=R.authority(slot,subject,name)
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
 -- one value: assert returns its message too, and as a call's last argument both would be passed (the three remove
 -- ops failed with "wrong number of arguments"; found by tools/dev/action_tests.py rules, 18 Nov)
 local function rule(kind) return (assert(F.last[kind],'ui faction script: no '..kind..' created yet')) end
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
