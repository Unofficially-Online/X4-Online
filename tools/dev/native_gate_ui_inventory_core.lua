-- UI replay family 'inventory', Lua half (native_gate_ui_replay lists it, bit 524288; src/native/engine/lockstep_ui_replay.h
-- UIINV_OP; docs/LOCKSTEP-REGISTRY.txt). A player's inventory is the player entity's, and every slot has its own player
-- entity: X4's inventory functions take "the player" from the universe's player singleton (AddInventory(nil, ...) reads
-- Universe+0x230), which slot k's scope sets to slot k's player on every client (lockstep_pilot_slots.h, entered here with
-- gate.mpp_scope as the missions' calls are). So each player has an inventory of their own, and this family makes the
-- stock menus' changes of it events of the client's slot:
--   capture  a click on a stock button that changes the inventory (K.SCOPES) is a scope: the inventory and money calls
--            the stock function makes inside it are recorded, nothing runs, and the click becomes ONE event of this
--            client's slot (kind, then the codec's arguments). Outside a scope nothing changes: the UI guard refuses
--            the inventory globals in a local phase, as before.
--   apply    every client, the probe's frame of capture tick + delay, slot order and call order: the click is checked
--            again against the world of that tick (the resources are still there, the trader still has the wares, the
--            money is there) and then run whole through the stock functions inside the slot's scope, or not at all
--            (the same answer on every client: the world is the same).
--   money    a trader's price moves with the stock TransferPlayerMoneyTo / TransferMoneyToPlayer inside the slot's
--            faction scope (the trade family's gate.ui_trade_scope: the shared wallet in coop, the slot's own account
--            in factions mode), between the check and the wares.
--   exports  what a stock button calls through FFI inside a scope (AddPlayerMoney, LearnBlueprint, StartResearch,
--            ClearProductionItems, the terraforming start and abort) is recorded by the native half, which claims those
--            guard entries (lockstep_ui_inventory.h: gate.ui_inventory_scope opens and closes the recording, the close
--            answers the calls); outside a scope the guard refuses them as before. So the blueprint and licence
--            trader's Confirm, the research menu's start and cancel and the terraforming menu's start and abort are one
--            event each; they change the player faction's library, research and money: every slot in coop, slot 1 in
--            factions mode.
--   drop     the inventory menu's Drop is an FFI export (DropInventory): the native half claims it in the UI guard's hook
--            and makes the kind 3 event there (lockstep_ui_inventory.h); the apply checks the amounts and calls the
--            export for the slot's player inside its scope.
--   reads    client k's menus read "the player's" inventory in the presentation scope, whose player singleton stays slot
--            1's entity (live inv1: client 2's menu read answered slot 1's amounts). On a client of slots 2..N the two
--            Lua readers the menus use (GetPlayerInventory, HasAllResourcesToCraft for the player) answer for the slot's
--            own player entity instead (gate.ui_inventory_player), in a local phase only: the apply and the probe read
--            the stock functions. Presentation only, nothing of it reaches the simulation.
--   known    AddKnownItem (the encyclopedia entry a menu or the HUD adds for what the player has seen) was refused for
--            everyone: with the family each new entry of a client is an event (kind 10, once per entry and client in a
--            session) and every client adds it, so what one player has seen is known to all, as the one library of a
--            coop game. Quiet: no info line, a refused push is tried again with the next call.
--   join     slot k's player entity is the gate's own (lockstep_pilot_slots.h: built by X4's factory, never part of the
--            component tree), so no save holds its inventory and a joiner builds an empty one. The saver's inventories
--            of slots 2..N at its save tick ride the join package as this family's state (S.join_state, the join
--            core's provider 'inventory'); the joiner gives each of its new player entities what the session's holds as
--            soon as that entity exists, before any event of the family runs (S.join_restore, S.join_give). Slot 1's
--            is X4's own player: the save has it.
--   sliders  the trade menu's amount sliders plan on a container's VIRTUAL cargo: AddCargo / RemoveCargo with their fourth
--            argument true and AddAmmo / RemoveAmmo with their fifth (0x291e30: the flag goes to 0x3fb890 as "virtual", no
--            cargo event is raised), and the menu computes with the amount the call answers. The UI guard refused them
--            like every cargo change of a menu, so a slider answered nothing and its handler failed on the missing
--            number: nobody could set a trade amount. Those virtual calls, and AddAmmo's check-only question (the craft
--            button's "is there room"), run at once on the client that asks, as X4's own; the planning copy is this
--            client's until the menu's SetVirtualCargoMode(false) (the trade family's event) frees it everywhere. A
--            real cargo or ammunition change from a menu stays refused.
--   receipt  on a tick with an apply: slot, kind, result and the amounts the click left behind, read in the slot's scope.
-- Pure Lua; the engine is injected.
X4LAN_UI_INVENTORY_CORE={}
local K=X4LAN_UI_INVENTORY_CORE
local function pack(...) return {n=select('#',...),...} end   -- LuaJIT without the 5.2 extensions has no table.pack
K.FAMILY=524288   -- lockstep_pilot_inputs.h UIREP_FAMILY_INVENTORY
K.OP=0x90         -- lockstep_ui_replay.h UIINV_OP: one click of this family
K.KIND={craft=1,trade=2,drop=3,purchase=4,research=5,research_cancel=6,terraform=7,terraform_abort=8,trade_part=9,known=10}   -- the event's first byte
K.KIND_NAME={'craft','trade','drop','purchase','research','research_cancel','terraform','terraform_abort','trade_part','known'}   -- (UIINV_KINDS; 3 is captured natively)
K.KNOWN_MAX=5000         -- encyclopedia entries one client sends in a session
-- A trade of many different wares does not fit one event: the wares beyond the first K.TRADE_WARES travel ahead in
-- trade_part events, which change nothing: every client keeps them for the slot until the trade event that names their
-- count runs them with its own, all or nothing. A trade whose parts are not all there stops the replay.
K.TRADE_WARES=5          -- different wares in one event
K.TRADE_WARES_MAX=60     -- in one click
-- The stock buttons that change the inventory: menu name -> function -> kind.
K.SCOPES={
 PlayerInfoMenu={buttonInventoryCraft='craft'},        -- the inventory page's Craft
 CraftingMenu={buttonInventoryCraft='craft'},          -- a crafting bench
 InventoryTraderMenu={buttonConfirm='trade'},          -- an NPC trader's (or a lockbox's) wares
 BlueprintOrLicenceTraderMenu={buttonConfirm='purchase'},   -- blueprints or licences from a faction representative
 ResearchMenu={buttonStartResearch='research',buttonCancelResearch='research_cancel'},
 TerraformingMenu={buttonStartProject='terraform',buttonAbortProject='terraform_abort'},
}
-- These change what X4 keeps for the player faction alone (the blueprint library, licences, research, terraforming): in
-- factions mode slots 2+ are refused before the click runs.
K.PLAYER_FACTION={purchase=true,research=true,research_cancel=true,terraform=true,terraform_abort=true}
-- The FFI exports the native half records inside a scope, with their arguments (lockstep_ui_inventory.h uiinv_calls: I a
-- UniverseID, s a string, q an int64).
K.FFI={AddPlayerMoney='q',LearnBlueprint='s',StartResearch='sI',ClearProductionItems='I',StartTerraformingProject='Is',
 AbortActiveTerraformingProject='I'}
-- The Lua globals a scope records instead of running (the globals are exchanged for the length of the stock function and
-- put back). Outside a scope they are whoever's they were: the UI guard refuses the inventory ones in a local phase, the
-- trade family takes the money ones. SetNPCBlackboard: the trader menu's SetNPCBlackboard(trader, '$TradeDone', true).
K.RECORDED={'AddInventory','RemoveInventory','TransferInventoryToPlayer','TransferPlayerInventoryTo','TransferPlayerMoneyTo','TransferMoneyToPlayer',
 'SetNPCBlackboard','AddLicence'}
K.READS={'GetPlayerInventory','HasAllResourcesToCraft','GetInventory'}   -- kept as they are for the apply; the first two answer per slot
-- Cargo and ammunition functions of the menus: the argument that says "virtual" (the trade menu's planning copy), and
-- AddAmmo's check-only argument. With one of them true the call runs here at once; without, it is the guard's.
K.VIRTUAL={AddCargo=4,RemoveCargo=4,AddAmmo=5,RemoveAmmo=5}
K.CHECK_ONLY={AddAmmo=4}
K.TRADE_DONE='$TradeDone'
K.CRAFT_MAX=100000                     -- pieces in one craft click
K.NOTICE_TICKS=90
-- p: gate, C (ffi.C), ffi, env (the Lua globals), log, replay (the replay core's session: ctx, ops, extras), own_slot,
-- factions (factions mode), trade (the trade family is armed), mpp (missions per player: the slots' scopes), notify(text)
-- or nil, menus() -> the stock menu tables.
function K.session(p)
 local gate,C,env,log,R=p.gate,p.C,p.env,p.log,p.replay
 local core=assert(X4LAN_UI_REPLAY_CORE,'ui inventory: the replay core first')
 local S={captured=0,applied=0,skipped=0,refused=0,scopes=0,wrapped={},rows={},tick=0,reads=0,read_failures=0,known=0,known_applied=0,virtual=0}
 local own_slot=p.own_slot or 1
 -- Stock functions, once per Lua state: the replay core's when it keeps them (the trade family wraps the money globals),
 -- else the global as it is now.
 local stock=env.X4LAN_UI_INVENTORY_STOCK
 if not stock then
  stock={}
  local kept=env.X4LAN_UI_REPLAY_STOCK or {}
  for _,name in ipairs(K.RECORDED) do stock[name]=kept[name] or env[name] end
  for _,name in ipairs(K.READS) do stock[name]=env[name] end
  stock.AddKnownItem=env.AddKnownItem
  for name in pairs(K.VIRTUAL) do stock[name]=env[name] end
  env.X4LAN_UI_INVENTORY_STOCK=stock
 end
 S.stock=stock
 local notice_at
 local function refuse(what,why)
  S.refused=S.refused+1
  log('UI_INVENTORY_REFUSED tick='..S.tick..' what='..tostring(what)..' why='..tostring(why))
  if p.notify and (not notice_at or S.tick-notice_at>=K.NOTICE_TICKS) then
   notice_at=S.tick
   pcall(p.notify,'Multiplayer: '..tostring(what)..' ('..tostring(why)..') is not shared yet - nothing happened.')
  end
 end
 local function local_phase() return not S.applying and not R.replaying and gate.ui_replay_local()==1 end
 -- The menus' reads of the player's inventory: this client's own slot's (slot 1: the stock functions as they are).
 local function own_player()
  local id=gate.ui_inventory_player and gate.ui_inventory_player(own_slot)
  return id and env.ConvertStringToLuaID(id) or nil
 end
 if stock.GetPlayerInventory and stock.GetInventory then
  env.GetPlayerInventory=own_slot==1 and stock.GetPlayerInventory or function(...)
   local player=local_phase() and own_player()
   if not player then return stock.GetPlayerInventory(...) end
   local ok,inventory=pcall(stock.GetInventory,player)
   if not ok or type(inventory)~='table' then S.read_failures=S.read_failures+1;return stock.GetPlayerInventory(...) end   -- X4's own answer
   S.reads=S.reads+1
   return inventory
  end
 end
 if stock.HasAllResourcesToCraft and stock.GetInventory then
  env.HasAllResourcesToCraft=own_slot==1 and stock.HasAllResourcesToCraft or function(entity,ware,amount,...)
   local player=entity==nil and local_phase() and own_player()
   if not player then return stock.HasAllResourcesToCraft(entity,ware,amount,...) end
   local ok,inventory=pcall(stock.GetInventory,player)
   if not ok or type(inventory)~='table' then S.read_failures=S.read_failures+1;return stock.HasAllResourcesToCraft(entity,ware,amount,...) end
   S.reads=S.reads+1
   for _,resource in ipairs(env.GetWareData(ware,'resources') or {}) do
    local item=inventory[resource.ware]
    if not item or (tonumber(item.amount) or 0)<resource.amount*(amount or 1) then return false end
   end
   return true
  end
 end
 -- The trade menu's sliders and the craft button's room check: X4's own function, here, now (the guard lets the probe's
 -- bypass through; nothing else runs inside these calls).
 for name,index in pairs(K.VIRTUAL) do
  if stock[name] then
   env[name]=function(...)
    if not local_phase() then return stock[name](...) end
    local check=K.CHECK_ONLY[name]
    if select(index,...)~=true and not (check and select(check,...)==true) then return stock[name](...) end   -- a real change: the guard's
    gate.ui_replay_bypass(1)
    local results=pack(pcall(stock[name],...))
    gate.ui_replay_bypass(0)
    S.virtual=S.virtual+1
    if not results[1] then error(results[2],0) end
    return unpack(results,2,results.n)
   end
  end
 end
 -- ---- capture ----
 local scope   -- the open click: {kind=, calls={{name, args...}}}
 local function record(name,...)
  scope.calls[#scope.calls+1]=pack(name,...)
 end
 -- The exports the native half recorded while the scope was open: "name<tab>argument...<newline>" rows, in call order.
 local function recorded_exports(rows,calls)
  for row in tostring(rows or ''):gmatch('[^\n]+') do
   local fields={}
   for field in (row..'\t'):gmatch('(.-)\t') do fields[#fields+1]=field end
   local name=fields[1]
   local spec=K.FFI[name]
   if spec and #fields==#spec+1 then
    local call={n=#spec+1,name}
    for i=1,#spec do
     local kind,text=spec:sub(i,i),fields[i+1]
     if kind=='I' then call[i+1]=env.ConvertStringTo64Bit(text) elseif kind=='q' then call[i+1]=tonumber(text) else call[i+1]=text end
    end
    calls[#calls+1]=call
   else log('UI_INVENTORY_EXPORT_ROW_IGNORED row='..tostring(name)) end
  end
 end
 local function push(kind,args,n,what,quiet)
  local hex,why=core.encode_call(args,n,R.ctx)
  if hex and gate.ui_replay_push(K.OP,string.format('%02x',K.KIND[kind])..hex)==1 then
   S.captured=S.captured+1
   if not quiet then log('UI_INVENTORY_CAPTURED kind='..kind..' what='..tostring(what)) end
   return true
  end
  if not quiet then refuse(what,hex and 'native' or why) end   -- native: UI_REPLAY_REFUSED_NATIVE says why
  return false
 end
 -- The encyclopedia: a new entry of this client is an event, once (the menus and the HUD ask again and again).
 local known,known_count={},0
 if stock.AddKnownItem then
  env.AddKnownItem=function(library,item,...)
   if not local_phase() then return stock.AddKnownItem(library,item,...) end
   if type(library)~='string' or type(item)~='string' or select('#',...)>0 then return end
   local key=library..'/'..item
   if known[key] or known_count>=K.KNOWN_MAX or (own_slot~=1 and not p.mpp) then return end
   if push('known',{library,item},2,key,true) then known[key]=true;known_count=known_count+1;S.known=S.known+1 end
  end
 end
 -- A craft click: the stock function removed the resources and added the product; the event is the product and the amount
 -- (the resources are the ware's own, read again at the apply).
 local function finish_craft(calls)
  for _,call in ipairs(calls) do
   if call[1]=='AddInventory' then return push('craft',{call[3],call[4]},2,'craft '..tostring(call[3])..' x'..tostring(call[4])) end
  end
 end
 -- A trader click: the price (positive: the player pays the trader's container), the wares each way, the trader's mark.
 local function finish_trade(calls)
  local trader,container,money,mark=nil,nil,0,false
  local wares={}
  for _,call in ipairs(calls) do
   local name=call[1]
   if name=='TransferPlayerMoneyTo' then money,container=money+call[2],call[3]
   elseif name=='TransferMoneyToPlayer' then money,container=money-call[2],call[3]
   elseif name=='AddPlayerMoney' then money=money-call[2]/100   -- a trader on the player's own station: the account itself (cents)
   elseif name=='TransferInventoryToPlayer' then wares[#wares+1]=call[2];wares[#wares+1]=call[3];trader=trader or call[4]
   elseif name=='TransferPlayerInventoryTo' then wares[#wares+1]=call[2];wares[#wares+1]=-call[3];trader=trader or call[4]
   elseif name=='SetNPCBlackboard' and call[3]==K.TRADE_DONE then mark=true;trader=trader or call[2] end
  end
  if #wares==0 and money==0 then return end
  if not trader then return refuse('inventory trade','no_trader') end
  if #wares/2>K.TRADE_WARES_MAX then return refuse('inventory trade','too_many_wares') end
  -- The wares beyond one event's go ahead as parts (the first one empties what an unfinished click left behind).
  local own=math.min(#wares,2*K.TRADE_WARES)
  local staged=#wares-own
  for at=1,staged,2*K.TRADE_WARES do
   local part={at==1}
   for i=at,math.min(at+2*K.TRADE_WARES-1,staged) do part[#part+1]=wares[i] end
   if not push('trade_part',part,#part,'trade part wares='..((#part-1)/2)) then return end   -- refused: no trade event follows
  end
  local args={trader,money,container,mark,staged/2}
  for i=staged+1,#wares do args[#args+1]=wares[i] end
  push('trade',args,5+own,'trade wares='..(#wares/2)..' money='..tostring(money))
 end
 -- Blueprints or licences from a faction representative: the price (to the container, or from the account itself on the
 -- player's own station) and what was bought. Nothing recorded: the stock function found the money missing.
 local function finish_purchase(calls)
  local money,container,licences,blueprints=0,nil,{},{}
  for _,call in ipairs(calls) do
   local name=call[1]
   if name=='TransferPlayerMoneyTo' then money,container=money+call[2],call[3]
   elseif name=='AddPlayerMoney' then money=money-call[2]/100
   elseif name=='AddLicence' then
    if call[2]~='player' then return refuse('purchase','licence_holder') end
    licences[#licences+1]=call[3];licences[#licences+1]=call[4]
   elseif name=='LearnBlueprint' then blueprints[#blueprints+1]=call[2] end
  end
  if #licences==0 and #blueprints==0 then return end
  if #licences>0 and #blueprints>0 then return refuse('purchase','mixed') end
  local items=#licences>0 and licences or blueprints
  local args={money,container,#licences>0 and 'licence' or 'blueprint'}
  for _,v in ipairs(items) do args[#args+1]=v end
  push('purchase',args,3+#items,'purchase '..args[3]..'s='..(#licences>0 and #licences/2 or #blueprints)..' money='..tostring(money))
 end
 -- One recorded export is the whole click: its arguments are the event's.
 local function finish_export(kind,name)
  return function(calls)
   for _,call in ipairs(calls) do
    if call[1]==name then return push(kind,{unpack(call,2,call.n)},call.n-1,kind..' '..tostring(call[2])) end
   end
  end
 end
 local finish={craft=finish_craft,trade=finish_trade,purchase=finish_purchase,research=finish_export('research','StartResearch'),
  research_cancel=finish_export('research_cancel','ClearProductionItems'),terraform=finish_export('terraform','StartTerraformingProject'),
  terraform_abort=finish_export('terraform_abort','AbortActiveTerraformingProject')}
 -- Before a trader click runs: what this family cannot share yet is refused whole (the stock function does not run).
 local function trade_refusal(menu)
  if type(menu)~='table' then return nil end
  if p.factions and own_slot~=1 and not p.trade and not menu.islockbox then return 'needs_trade_family' end
 end
 function S.with_scope(menu,kind,what,orig,...)
  if not local_phase() or scope then return orig(...) end
  if own_slot~=1 and not p.mpp then return refuse(what,'needs_missions_per_player') end   -- no scope of its own for the slot
  if kind=='trade' then
   local why=trade_refusal(menu)
   if why then return refuse(what,why) end
  end
  if K.PLAYER_FACTION[kind] and p.factions and own_slot~=1 then return refuse(what,'factions') end
  scope={kind=kind,calls={}}
  S.scopes=S.scopes+1
  local saved={}
  for _,name in ipairs(K.RECORDED) do
   if env[name] then saved[name]=env[name];env[name]=function(...) return record(name,...) end end
  end
  if gate.ui_inventory_scope then gate.ui_inventory_scope(1) end   -- the exports of this click are recorded, not run
  local results=pack(pcall(orig,...))
  for name,f in pairs(saved) do env[name]=f end
  local done=scope;scope=nil
  local lost
  if gate.ui_inventory_scope then
   local rows,why=gate.ui_inventory_scope(0)
   if rows==nil then lost=why or 'lost' else recorded_exports(rows,done.calls) end
  end
  if lost then refuse(what,'exports_'..tostring(lost))   -- a call the record could not hold: nothing of the click is sent
  elseif results[1] then finish[kind](done.calls) else log('UI_INVENTORY_SCOPE_ERROR what='..what..' error='..tostring(results[2])) end
  if not results[1] then error(results[2],0) end
  return unpack(results,2,results.n)
 end
 -- The stock menus' buttons, wrapped once each menu exists (the rows look them up when they are drawn). The stock function
 -- stays with its menu, so a later session of the same Lua state wraps it and not the earlier session's wrapper.
 local function wrap_menus()
  for _,menu in ipairs(p.menus and p.menus() or {}) do
   local names=type(menu)=='table' and K.SCOPES[menu.name]
   if names and not S.wrapped[menu.name] then
    S.wrapped[menu.name]=true
    local count=0
    menu.x4lan_inventory_stock=menu.x4lan_inventory_stock or {}
    for name,kind in pairs(names) do
     local orig=menu.x4lan_inventory_stock[name] or menu[name]
     if type(orig)=='function' then
      menu.x4lan_inventory_stock[name]=orig
      local what=menu.name..'.'..name
      menu[name]=function(...) return S.with_scope(menu,kind,what,orig,...) end
      count=count+1
     end
    end
    log('UI_INVENTORY_SCOPES menu='..menu.name..' functions='..count)
   end
  end
 end
 -- ---- apply ----
 -- fn inside slot's scope (slot 1: the canonical player as it is): ok, what. A scope that cannot be entered or left stops
 -- the replay: the call never runs as slot 1's on some clients.
 local function scoped(slot,fn)
  if slot~=1 and gate.mpp_scope(slot)~=1 then gate.ui_replay_fail('inventory_scope');return false,'scope' end
  local results=pack(pcall(fn))
  if slot~=1 and gate.mpp_unscope(slot)~=1 then gate.ui_replay_fail('inventory_leave') end
  if not results[1] then return false,'error:'..tostring(results[2]) end
  return unpack(results,2,results.n)
 end
 local function have(inventory,ware)
  local item=type(inventory)=='table' and inventory[ware]
  return type(item)=='table' and tonumber(item.amount) or 0
 end
 local function whole(v,low,high) return type(v)=='number' and v%1==0 and v>=low and v<=high end
 -- ---- hot join ----
 local function slot_player(slot)
  local id=gate.ui_inventory_player and gate.ui_inventory_player(slot)
  return id and env.ConvertStringToLuaID(id) or nil
 end
 -- The saver, at its save tick: "slot:ware*amount,...;slot:..." for slots 2..slots that hold something (read only).
 function S.join_state(slots)
  local parts={}
  for slot=2,slots or 0 do
   local player=slot_player(slot)
   if player then
    local ok,inventory=pcall(stock.GetInventory,player)
    local wares={}
    for ware,item in pairs(ok and type(inventory)=='table' and inventory or {}) do
     local amount=type(item)=='table' and tonumber(item.amount) or 0
     if type(ware)=='string' and ware:match('^[%w_]+$') and amount>=1 then wares[#wares+1]=ware..'*'..string.format('%.0f',amount) end
    end
    table.sort(wares)
    if #wares>0 then parts[#parts+1]=slot..':'..table.concat(wares,',') end
   end
  end
  return table.concat(parts,';')
 end
 -- The joiner: what each slot's player holds in the session, given as soon as this client has that player entity.
 local pending
 function S.join_give()
  if not pending then return end
  local left=false
  for slot,wares in pairs(pending) do
   local player=slot_player(slot)
   if player then
    local _,now=pcall(stock.GetInventory,player)
    local given=0
    for i=1,#wares,2 do
     local missing=wares[i+1]-have(now,wares[i])
     if missing>0 then stock.AddInventory(player,wares[i],missing);given=given+1 end
    end
    pending[slot]=nil
    log('UI_INVENTORY_JOIN_GIVEN tick='..S.tick..' slot='..slot..' wares='..(#wares/2)..' given='..given)
   else left=true end
  end
  if not left then pending=nil end
 end
 function S.join_restore(text)
  pending={}
  local slots=0
  for slot,list in tostring(text):gmatch('(%d+):([^;]*)') do
   local wares={}
   for ware,amount in list:gmatch('([%w_]+)%*(%d+)') do wares[#wares+1]=ware;wares[#wares+1]=tonumber(amount) end
   pending[tonumber(slot)]=wares;slots=slots+1
  end
  log('UI_INVENTORY_JOIN_STATE slots='..slots..' text='..tostring(text):sub(1,160))
  S.join_give()
 end
 -- A receipt's text: the same characters on every client (an error's text may name an address).
 local function plain(text) return (tostring(text):gsub('0x%x+','0x'):gsub('[^%w_=,.:%- ]','?'):sub(1,120)) end
 local function run_craft(slot,args,n)
  local ware,amount=args[1],args[2]
  if n~=2 or type(ware)~='string' or not whole(amount,1,K.CRAFT_MAX) then return false,'arguments' end
  return scoped(slot,function()
   local resources=env.GetWareData(ware,'resources')
   if type(resources)~='table' or #resources==0 then return false,'no_recipe' end
   if not stock.HasAllResourcesToCraft(nil,ware,amount) then return false,'resources' end
   -- Ammunition taken out of its bundle goes to the ship the player sits in: as the stock button, nothing when it has no room.
   local unbundle,component=env.GetWareData(ware,'isunbundleammo','component')
   if unbundle then
    local ship=C.GetPlayerOccupiedShipID()
    if ship~=0 and stock.AddAmmo(env.ConvertStringToLuaID(tostring(ship)),component,amount,true)~=amount then return false,'ammo_space' end
   end
   for _,resource in ipairs(resources) do stock.RemoveInventory(nil,resource.ware,amount*resource.amount) end
   stock.AddInventory(nil,ware,amount,true)
   return true,ware..'='..have(stock.GetPlayerInventory(),ware)
  end)
 end
 -- The wares a slot's trade_part events brought ahead of its trade event (ware, signed amount, ...), per slot.
 local staged={}
 local function ware_pairs(args,from,n)
  if (n-from+1)%2~=0 then return false end
  for i=from,n,2 do
   if type(args[i])~='string' or type(args[i+1])~='number' or args[i+1]%1~=0 or args[i+1]==0 then return false end
  end
  return true
 end
 local function run_trade_part(slot,args,n)
  if n<3 or type(args[1])~='boolean' or not ware_pairs(args,2,n) then return false,'arguments' end
  if args[1] or not staged[slot] then staged[slot]={} end
  local list=staged[slot]
  if (#list+n-1)/2>K.TRADE_WARES_MAX then staged[slot]=nil;return false,'too_many_wares' end
  for i=2,n do list[#list+1]=args[i] end
  return true,'staged='..(#list/2)
 end
 local function run_trade(slot,args,n)
  local trader,money,container,mark,ahead=args[1],args[2],args[3],args[4],args[5]
  local parts=staged[slot] or {}
  staged[slot]=nil
  if n<5 or trader==nil or type(money)~='number' or type(mark)~='boolean' or not whole(ahead,0,K.TRADE_WARES_MAX) or not ware_pairs(args,6,n) then
   return false,'arguments'
  end
  -- Its parts are the events right before it: a client that does not hold them all would trade something else.
  if #parts/2~=ahead then gate.ui_replay_fail('inventory_trade_parts');return false,'parts' end
  local wares={}
  for _,v in ipairs(parts) do wares[#wares+1]=v end
  for i=6,n do wares[#wares+1]=args[i] end
  -- The wares are there, on both sides, in the world of this tick.
  local ok,why=scoped(slot,function()
   local mine,theirs=stock.GetPlayerInventory(),stock.GetInventory(trader)
   local need_mine,need_theirs={},{}
   for i=1,#wares,2 do
    local ware,amount=wares[i],wares[i+1]
    if amount>0 then need_theirs[ware]=(need_theirs[ware] or 0)+amount else need_mine[ware]=(need_mine[ware] or 0)-amount end
   end
   for ware,amount in pairs(need_theirs) do if have(theirs,ware)<amount then return false,'trader_stock' end end
   for ware,amount in pairs(need_mine) do if have(mine,ware)<amount then return false,'player_stock' end end
   return true
  end)
  if not ok then return false,why end
  -- The price: the slot's faction pays or is paid (coop: the shared wallet).
  if money~=0 then
   local opened=p.trade and gate.ui_trade_scope(slot) or 0
   if opened<0 then return false,'no_faction' end
   local paid,result=pcall(function()
    if money>0 and (tonumber(env.GetPlayerMoney()) or 0)<money then return 'money' end
    if container==nil then C.AddPlayerMoney(-money*100)   -- a trader on the player's own station: the account itself
    elseif money>0 then stock.TransferPlayerMoneyTo(money,container)
    else stock.TransferMoneyToPlayer(-money,container) end
   end)
   if opened==1 and gate.ui_trade_scope_end()~=1 then gate.ui_replay_fail('inventory_trade_scope');return false,'scope' end
   if not paid then return false,'error:'..tostring(result) end
   if result then return false,result end
  end
  return scoped(slot,function()
   local left={}
   for i=1,#wares,2 do
    local ware,amount=wares[i],wares[i+1]
    if amount>0 then stock.TransferInventoryToPlayer(ware,amount,trader) else stock.TransferPlayerInventoryTo(ware,-amount,trader) end
   end
   if mark then stock.SetNPCBlackboard(trader,K.TRADE_DONE,true) end
   local mine=stock.GetPlayerInventory()
   for i=1,#wares,2 do left[#left+1]=wares[i]..'='..have(mine,wares[i]) end
   return true,table.concat(left,','):sub(1,100)
  end)
 end
 -- Dropped wares (captured natively): the amounts are still the slot's, then X4's own DropInventory for the slot's player.
 local function run_drop(slot,args,n)
  local lockbox=args[1]
  if n<3 or (n-1)%2~=0 or (lockbox~=nil and type(lockbox)~='string') then return false,'arguments' end
  for i=2,n,2 do if type(args[i])~='string' or not whole(args[i+1],1,2147483647) then return false,'arguments' end end
  return scoped(slot,function()
   local mine,need=stock.GetPlayerInventory(),{}
   for i=2,n,2 do need[args[i]]=(need[args[i]] or 0)+args[i+1] end
   for ware,amount in pairs(need) do if have(mine,ware)<amount then return false,'player_stock' end end
   local count=(n-1)/2
   local wares=p.ffi.new('UIWareAmount[?]',count)
   for i=1,count do wares[i-1].wareid=args[2*i];wares[i-1].amount=args[2*i+1] end   -- args keeps the strings alive
   local dropped=C.DropInventory(C.GetPlayerID(),lockbox,wares,count)
   local left={}
   mine=stock.GetPlayerInventory()
   for i=2,n,2 do left[#left+1]=args[i]..'='..have(mine,args[i]) end
   return dropped==true,(dropped==true and '' or 'x4_refused:')..table.concat(left,',')
  end)
 end
 -- What follows changes the player faction's own things: canonical (no slot scope), the slot's faction scope for money.
 local function player_faction(slot,fn)
  if p.factions and slot~=1 then return false,'factions' end
  local opened=p.trade and gate.ui_trade_scope(slot) or 0
  if opened<0 then return false,'no_faction' end
  local results=pack(pcall(fn))
  if opened==1 and gate.ui_trade_scope_end()~=1 then gate.ui_replay_fail('inventory_trade_scope');return false,'scope' end
  if not results[1] then return false,'error:'..tostring(results[2]) end
  return unpack(results,2,results.n)
 end
 local function owned(id)
  local ok,own=pcall(env.GetComponentData,id,'isplayerowned')
  return ok and own==true
 end
 local function run_purchase(slot,args,n)
  local money,container,mode=args[1],args[2],args[3]
  if n<4 or type(money)~='number' or money<0 or (mode~='licence' and mode~='blueprint') or (mode=='licence' and (n-3)%2~=0) then return false,'arguments' end
  for i=4,n do if type(args[i])~='string' then return false,'arguments' end end
  return player_faction(slot,function()
   if (tonumber(env.GetPlayerMoney()) or 0)<money then return false,'money' end
   if mode=='licence' then for i=4,n,2 do stock.AddLicence('player',args[i],args[i+1]) end
   else for i=4,n do C.LearnBlueprint(args[i]) end end
   if money>0 then
    if container==nil then C.AddPlayerMoney(-money*100) else stock.TransferPlayerMoneyTo(money,container) end
   end
   return true,mode..'s='..(mode=='licence' and (n-3)/2 or n-3)..' money='..tostring(money)
  end)
 end
 local function run_research(slot,args,n)
  if n~=2 or type(args[1])~='string' or args[2]==nil then return false,'arguments' end
  return player_faction(slot,function()
   if not owned(args[2]) then return false,'not_owner' end
   C.StartResearch(args[1],args[2])
   return true,args[1]
  end)
 end
 local function run_research_cancel(slot,args,n)
  if n~=1 or args[1]==nil then return false,'arguments' end
  return player_faction(slot,function()
   if not owned(args[1]) then return false,'not_owner' end
   C.ClearProductionItems(args[1])
   return true,'cancelled'
  end)
 end
 local function run_terraform(slot,args,n)
  if n~=2 or args[1]==nil or type(args[2])~='string' then return false,'arguments' end
  return player_faction(slot,function()
   if not C.CanStartTerraformingProject(args[1],args[2]) then return false,'cannot_start' end
   C.StartTerraformingProject(args[1],args[2])
   return true,args[2]
  end)
 end
 local function run_terraform_abort(slot,args,n)
  if n~=1 or args[1]==nil then return false,'arguments' end
  return player_faction(slot,function()
   if not C.CanAbortActiveTerraformingProject(args[1]) then return false,'cannot_abort' end
   C.AbortActiveTerraformingProject(args[1])
   return true,'aborted'
  end)
 end
 -- An encyclopedia entry: the one library, whoever saw it.
 local function run_known(slot,args,n)
  if n~=2 or type(args[1])~='string' or type(args[2])~='string' then return false,'arguments' end
  stock.AddKnownItem(args[1],args[2])
  S.known_applied=S.known_applied+1
  return true,args[1]..'/'..args[2]
 end
 local run={run_craft,run_trade,run_drop,run_purchase,run_research,run_research_cancel,run_terraform,run_terraform_abort,run_trade_part,run_known}
 local QUIET_KIND={[10]=true}   -- applied without a row of its own (hundreds in a session); the status counts them
 R.ops[K.OP]=function(slot,hex,tick)
  S.join_give()   -- a joiner's catch-up comes before any event of the family
  local kind=tonumber(hex:sub(1,2),16) or 0
  if not run[kind] then gate.ui_replay_fail('ui_inventory_op');return false,'kind' end
  local args,n=core.decode_call(hex:sub(3),R.ctx)
  if not args then
   if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
   S.skipped=S.skipped+1
   S.rows[#S.rows+1]=slot..':'..K.KIND_NAME[kind]..':'..plain(n)
   return false,K.KIND_NAME[kind]..' '..tostring(n)
  end
  S.applying=true
  local ok,what=run[kind](slot,args,n)
  S.applying=false
  if QUIET_KIND[kind] and ok then return ok,K.KIND_NAME[kind]..' '..tostring(what) end
  if ok then S.applied=S.applied+1 else S.skipped=S.skipped+1 end
  S.rows[#S.rows+1]=slot..':'..K.KIND_NAME[kind]..':'..(ok and 'ran' or 'skipped')..':'..plain(what)
  log('UI_INVENTORY_APPLIED tick='..tostring(tick)..' slot='..slot..' kind='..K.KIND_NAME[kind]..' result='..(ok and 'ran' or 'skipped')..' what='..tostring(what))
  return ok,K.KIND_NAME[kind]..' '..tostring(what)
 end
 -- The probe's frame, after the tick's applies: the menus' buttons, the status, the receipt's part (only a tick with an apply
 -- has one: it is read from the world of that tick, so a joiner has nothing to catch up with).
 R.extras[#R.extras+1]=function(tick)
  S.tick=tick
  S.join_give()
  wrap_menus()
  if tick%600==0 then
   log('UI_INVENTORY_STATUS tick='..tick..' captured='..S.captured..' applied='..S.applied..' skipped='..S.skipped..' refused='..S.refused..' scopes='..S.scopes..' reads='..S.reads..'/'..S.read_failures..' known='..S.known..'/'..S.known_applied..' virtual='..S.virtual..
    ' '..tostring(gate.ui_inventory_status and gate.ui_inventory_status() or '-'))
  end
  if #S.rows==0 then return '' end
  local text='|UIINV_1:'..table.concat(S.rows,';')
  S.rows={}
  return text
 end
 -- Scripted clicks for runs without people (native_gate_ui_replay_script): the same capture as a button's scope.
 function S.script_scope(kind,fn) return S.with_scope(nil,kind,'script.'..kind,fn) end
 return S
end
-- Scripted use (native_gate_ui_replay_script), as the stock buttons call the globals:
--   {tick,'inv_drop',lockbox macro or false,{ware,amount,...}}  the Drop button: the export itself, captured natively (the
--                                                           op runs in the presentation scope, K.SCOPED of the replay core)
--   {tick,'inv_craft',ware,amount}                          the Craft button: the resources out, the product in
--   {tick,'inv_trade',saved trader,money,{ware,amount,...}}  a trader's Confirm: positive amounts come to the player
--   {tick,'inv_blueprints',saved representative,price,{ware,...}}  the blueprint trader's Confirm (the export, then the price)
--   {tick,'inv_research',ware,saved research module} / {tick,'inv_research_cancel',saved research module}
--   {tick,'vcargo',slot,ware,amount}  a trade slider's planning call on slot's ship: AddCargo(ship, ware, amount, true),
--                                     negative: RemoveCargo; answers what X4 answered (this client only)
--   {tick,'vcargo_mode',slot,on}      the trade menu's open (true) or close (false) for slot's ship: SetVirtualCargoMode, the
--                                     trade family's event
K.SCRIPT_OPS={inv_craft=true,inv_trade=true,inv_drop=true,inv_blueprints=true,inv_research=true,inv_research_cancel=true,vcargo=true,
 vcargo_mode=true}
function K.script_call(e,tools)
 local op=e[2]
 if op=='inv_craft' then return function(S)
   local I=assert(S.inventory,'ui replay script: the inventory family is off')
   I.script_scope('craft',function()
    for _,resource in ipairs(tools.env.GetWareData(e[3],'resources') or {}) do tools.env.RemoveInventory(nil,resource.ware,e[4]*resource.amount) end
    tools.env.AddInventory(nil,e[3],e[4],true)
   end)
   return 'craft '..tostring(e[3])..' x'..tostring(e[4]) end
 end
 if op=='inv_trade' then return function(S)
   local I=assert(S.inventory,'ui replay script: the inventory family is off')
   local trader=tools.saved_id(e[3])
   local container=tools.env.GetContextByClass(trader,'container',false)
   I.script_scope('trade',function()
    if e[4]>0 then tools.env.TransferPlayerMoneyTo(e[4],container) elseif e[4]<0 then tools.env.TransferMoneyToPlayer(-e[4],container) end
    for i=1,#e[5],2 do
     if e[5][i+1]>0 then tools.env.TransferInventoryToPlayer(e[5][i],e[5][i+1],trader) else tools.env.TransferPlayerInventoryTo(e[5][i],-e[5][i+1],trader) end
    end
    tools.env.SetNPCBlackboard(trader,K.TRADE_DONE,true)
   end)
   return 'trade wares='..(#e[5]/2)..' money='..tostring(e[4]) end
 end
 if op=='vcargo_mode' then return function()
   if e[4] then tools.env.SetVirtualCargoMode(tools.slot_lua(e[3]),true,-1) else tools.env.SetVirtualCargoMode(tools.slot_lua(e[3]),false) end
   return 'SetVirtualCargoMode '..tostring(e[4]) end
 end
 if op=='vcargo' then return function()
   local ship=tools.slot_lua(e[3])
   if e[5]>=0 then return 'AddCargo virtual answer='..tostring(tools.env.AddCargo(ship,e[4],e[5],true)) end
   local answer=tools.env.RemoveCargo(ship,e[4],-e[5],true)   -- X4's RemoveCargo answers nothing
   return 'RemoveCargo virtual answer='..tostring(answer) end
 end
 if op=='inv_blueprints' then return function(S)
   local I=assert(S.inventory,'ui replay script: the inventory family is off')
   local container=tools.env.GetContextByClass(tools.saved_id(e[3]),'container',false)
   I.script_scope('purchase',function()
    for _,ware in ipairs(e[5]) do tools.C.LearnBlueprint(ware) end
    tools.env.TransferPlayerMoneyTo(e[4],container)
   end)
   return 'blueprints='..#e[5]..' money='..tostring(e[4]) end
 end
 if op=='inv_research' then return function(S)
   local I=assert(S.inventory,'ui replay script: the inventory family is off')
   I.script_scope('research',function() tools.C.StartResearch(e[3],tools.saved_id(e[4])) end)
   return 'research '..tostring(e[3]) end
 end
 if op=='inv_research_cancel' then return function(S)
   local I=assert(S.inventory,'ui replay script: the inventory family is off')
   I.script_scope('research_cancel',function() tools.C.ClearProductionItems(tools.saved_id(e[3])) end)
   return 'research cancel' end
 end
 if op=='inv_drop' then return function()
   local count=#e[4]/2
   local wares=tools.ffi.new('UIWareAmount[?]',count)
   for i=1,count do wares[i-1].wareid=e[4][2*i-1];wares[i-1].amount=e[4][2*i] end
   return 'drop wares='..count..' answer='..tostring(tools.C.DropInventory(tools.C.GetPlayerID(),e[3] or nil,wares,count)) end
 end
 error('unknown ui inventory script op '..tostring(op))
end
