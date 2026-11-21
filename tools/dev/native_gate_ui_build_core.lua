-- UI replay families 'build' and 'assets', Lua half (native_gate_ui_replay lists them, default off; they need
-- native_gate_ui_guard; src/native/engine/lockstep_ui_build.h, reports/LOCKSTEP-UI-BUILD-20261101.txt).
-- The native half claims the stock FFI entries (AddBuildTask6, the plots, crew, ships, mods) at the guard's hooks and
-- keeps one batch per frame of this client's slot. This half adds what is Lua: SetComponentName, and inside a commit
-- scope (a stock menu function that pays and builds in one click) the money calls and the shipyard's repair signal, as
-- records of the same batch, in call order. It runs whole batches at their apply (every client, the probe's frame), the
-- Lua records through the stock functions, and adds the families' receipt. Pure Lua; the engine is injected.
X4LAN_UI_BUILD_CORE={}
local K=X4LAN_UI_BUILD_CORE
local function pack(...) return {n=select('#',...),...} end   -- LuaJIT without the 5.2 extensions has no table.pack
K.FAMILY={build=4096,assets=8192}   -- lockstep_pilot_inputs.h UIREP_FAMILY_BUILD / _ASSETS
K.OP_PART=0x50                      -- lockstep_ui_replay.h UIBUILD_OP_PART: a part of a batch
-- Lua records by their wire number (lockstep_ui_build.h uibuild_lua_family) and the family each belongs to.
K.LUA={'TransferPlayerMoneyTo','TransferMoneyToPlayer','SetComponentName','SignalObject'}
K.LUA_FAMILY={'build','build','assets','build'}
K.REPAIR_SIGNAL='repairs_initiate'   -- SignalObject(shipyard, 'repairs_initiate', ship, ...): md build.shiptrader repairs
-- Commit scopes: inside these stock menu functions money moves (and the repair signal goes) as part of the batch the
-- function's build calls make. Outside them the money calls are left to whatever else handles them (the trade family,
-- else the UI guard refuses them).
K.SCOPES={
 ShipConfigurationMenu={'buttonConfirm','buttonInstallMod'},                          -- buy, upgrade, repair; a mod at a shipyard
 StationConfigurationMenu={'buttonConfirmMoney','buttonSetMoneyToEstimate','buttonBuyPlot','buttonConfirm'},   -- budget, plot, plan
 MapMenu={'buttonBuyPlot','buttonSellShips','selectCV'},                              -- the plot, selling ships, hiring a builder
 InteractMenu={'buttonDeployToStation'},                                              -- hiring a builder (or sending the own one)
}
-- Station construction (reports/LOCKSTEP-UI-CONSTRUCT-20261101.txt): these scopes are open to every slot in factions mode
-- too (the native half runs their records in the slot's faction scope, the trade family's; without it they are refused);
-- every other scope stays slot 1's there and refuses the click for slots 2+ before anything runs.
K.CONSTRUCTION={['StationConfigurationMenu.buttonConfirmMoney']=true,['StationConfigurationMenu.buttonSetMoneyToEstimate']=true,
 ['StationConfigurationMenu.buttonBuyPlot']=true,['StationConfigurationMenu.buttonConfirm']=true,['MapMenu.buttonBuyPlot']=true,
 ['MapMenu.selectCV']=true,['InteractMenu.buttonDeployToStation']=true,
 ['script.plan']=true,['script.budget']=true,['script.hire']=true,['script.cancel']=true}
-- Hiring a builder: the fee is a record of this family, the order the order families' events (the map: RemoveAllOrders,
-- CreateDeployToStationOrder, SetOrderParam, EnableOrder; the interact menu: RemoveAllOrders2, CreateOrder). Without the
-- families the order needs the click is refused whole (a fee without its order would buy nothing).
K.HIRE={['MapMenu.selectCV']={'orders','queue'},['InteractMenu.buttonDeployToStation']={'orders'},['script.hire']={'orders','queue'}}
K.ORDER_FAMILY={orders=4,queue=128,trade=1024,faction=32768}   -- lockstep_pilot_inputs.h UIREP_FAMILY_ORDERS / _QUEUE / _TRADE / _FACTION
-- The order entries native_gate_pilot_block_orders would refuse, opened when their calls are all captured.
K.UNBLOCK={['MapMenu.orderDeployToStation']={'orders','queue'},['InteractMenu.orderDeployToStation']={'orders'},
 -- "Assign Role" of the interact menu (a ship becomes a station's trader, miner or defender, or a ship's wingman):
 -- ResetOrderLoop, CreateOrder "AssignCommander", its SetOrderParam calls, EnableOrder, AdjustOrder, all captured by
 -- the two order families. Until 18 Nov the entry was refused: the click on a subordinate group did nothing in a
 -- session (run mg2). The branches that make other calls stay refused (K.GUARDS).
 ['InteractMenu.buttonAssignCommander']={'orders','queue'},['InteractMenu.orderAssignCommander']={'orders','queue'},
 -- "Remove Assignment" (the interact menu's entry of an assigned ship, the map's subordinate list): RemoveCommander2 is
 -- a claim of the faction family, the default Wait order after it (CreateOrder, EnablePlannedDefaultOrder) the queue
 -- family's. Refused until 18 Nov: the click did nothing in a session.
 ['InteractMenu.buttonRemoveAssignment']={'orders','queue','faction'},['MapMenu.buttonRemoveAssignment']={'orders','queue','faction'},
 -- "Change assignment" of the interact menu (a wing's ships get another role or another group): an AssignCommander
 -- order per ship, the order families'; the group's own role (SetSubordinateGroupAssignment) is a claim of the faction
 -- family since 19 Nov, as are the group's switches and a fleet's name. Refused until then.
 ['InteractMenu.buttonChangeAssignment']={'orders','queue','faction'}}
-- Entries opened above whose other branches make calls nothing captures: refused there, by what the menu holds at the
-- click. A fleet unit (the fleet's replacement slots: SetFleetUnitSubordinateGroupAssignment, AddFleetUnitSubordinate)
-- and the position defence assignment (Helper.setIntersectorDefence).
K.GUARDS={
 InteractMenu={buttonAssignCommander=function(menu,assignment)
  if menu.fleetunit then return 'fleet_unit' end
  if assignment=='positiondefence' then return 'position_defence' end
 end,
 buttonChangeAssignment=function(menu,assignment)
  if menu.fleetunit then return 'fleet_unit' end
  if assignment=='positiondefence' then return 'position_defence' end
  if type(menu.shipswithcurrentcommander)=='table' and #menu.shipswithcurrentcommander>0 then return end
  if type(menu.groupShips)=='table' and #menu.groupShips>0 then
   for _,entry in ipairs(menu.groupShips) do if not entry.component then return 'fleet_unit' end end
   return
  end
  -- one ship: a commander that is a fleet unit takes calls nothing captures (SetFleetUnitSubordinateGroupAssignment,
  -- ReassignControllableToFleetUnit); a commander that cannot be read is refused as one
  local ok,unit=pcall(function() return tonumber(require('ffi').C.GetCommander(menu.componentSlot.component,0).fleetunitid) end)
  if not ok or unit~=0 then return 'fleet_unit' end
 end},
}
K.NOTICE_TICKS=90
-- What a stock menu does right after its commit call assumes the call has happened. Here it happens a few ticks later,
-- at the apply, on every client. So the menu's own follow-up is held back at the click (K.AFTER) and made at the apply
-- on the clicking player's client (K.applied below): without this the station menu reloaded the plan 0.1 s after
-- Confirm, before the commit had landed, and showed the station without the modules just confirmed (run sb1, 17 Nov).
K.AFTER={
 -- Confirm module changes: the menu's timed reload of the station's plan waits for the apply
 ['StationConfigurationMenu.buttonConfirm']=function(menu,S) menu.updatePlan=nil;S.plan_pending=true end,
}
-- The stock struct layouts the native half reads and builds: size and the offsets it relies on (the menus' ffi.cdef;
-- a difference refuses the arming, a type no menu declared yet is only reported).
K.LAYOUTS={
 {'UILoadout2',184,{weapons=0,numweapons=8,turrets=16,shields=32,engines=48,turretgroups=64,shieldgroups=80,ammo=96,units=112,software=128,
   numsoftware=136,thruster=144,numcrew=160,crew=168,hascrewexperience=176}},
 {'UILoadoutMacroData2',48,{macro=0,upgradetypename=8,slot=16,optional=24,weaponsetting=32}},
 {'UILoadoutGroupData2',48,{macro=0,path=8,group=16,count=24,optional=28,weaponsetting=32}},
 {'UILoadoutWeaponSetting',16,{ammomacroname=0,weaponmode=8}},
 {'UILoadoutAmmoData',16,{macro=0,amount=8,optional=12}},
 {'UILoadoutSoftwareData',8,{ware=0}},
 {'UILoadoutVirtualMacroData',16,{macro=0,optional=8}},
 {'UILoadoutCrewData',16,{roleid=0,count=8,optional=12}},
 {'CrewTransferInfo2',48,{removed=0,numremoved=8,added=16,numadded=24,transferred=32,numtransferred=40}},
 {'CrewTransferContainer2',32,{newroleid=0,seed=8,amount=16,price=24}},
 {'CrewTransferContainer',24,{newroleid=0,seed=8,amount=16}},
 {'AddBuildTask6Container',40,{blacklists=0,numblacklists=8,fightrules=16,numfightrules=24,paintmodwareid=32}},
 {'BlacklistTypeID',16,{id=0,type=8}},
 {'FightRuleTypeID',16,{id=0,type=8}},
 {'GenericActor',24,{entity=0,personcontrollable=8,personseed=16}},
 {'UICrewExchangeResult',24,{reason=0,person=8,partnerperson=16}},
 {'UIPosRot',24,{x=0,y=4,z=8,yaw=12,pitch=16,roll=20}},
 {'Coord3D',12,{x=0,y=4,z=8}},
 -- the station plan's module loadouts (lockstep_ui_construct.h: GetConstructionMapItemLoadout2 and X4's converter)
 {'UILoadout',160,{weapons=0,numweapons=8,turrets=16,shields=32,engines=48,turretgroups=64,shieldgroups=80,ammo=96,units=112,software=128,
   numsoftware=136,thruster=144}},
 {'UILoadoutMacroData',32,{macro=0,upgradetypename=8,slot=16,optional=24}},
 {'UILoadoutGroupData',32,{macro=0,path=8,group=16,count=24,optional=28}},
 {'UILoadoutCounts',36,{numweapons=0,numturrets=4,numshields=8,numengines=12,numturretgroups=16,numshieldgroups=20,numammo=24,numunits=28,numsoftware=32}},
}
-- A stock menu function called from the probe's own frame runs where the menu's own calls run: inside this client's
-- presentation scope (p.present; the probe's frame is outside it, there X4's player and player faction are player 1's
-- on every client, and a list drawn from there would be player 1's).
function K.viewed(p,fn,...)
 local opened=p.present and p.present(true) or false
 local results={pcall(fn,...)}
 if opened then p.present(false) end
 if not results[1] then error(results[2],0) end
 return results[2]
end
-- A stock menu's table by name, when it is on screen.
function K.shown_menu(p,name)
 for _,menu in ipairs(p.menus and p.menus() or {}) do
  if type(menu)=='table' and menu.name==name then return menu.shown and menu or nil end
 end
end
-- The station overview (Logical Station Overview) draws its open node, or the whole chart, right after each of its
-- calls (a ticked box, Confirm of the account, the trade wares): in a session the call lands a few ticks later, so the
-- menu drew the state from before it: "Automatic pricing" stayed ticked and no price slider came, although the
-- override was set on every client (run mg2, 18 Nov). The menu's own draw functions are noted (wrapped once); when a
-- station call of this client's own player lands shortly after such a draw, the menu draws again, by its own flags.
-- A slider's own steps make no draw in the stock menu and make none here (a draw would take the slider from the hand).
K.OVERVIEW_WINDOW=0.75
function K.overview_frame(S,p,R,now)
 local menu=K.shown_menu(p,'StationOverviewMenu')
 if not menu then return end
 if not menu.x4lan_draw_noted then
  menu.x4lan_draw_noted=true
  for _,name in ipairs({'updateExpandedNode','display'}) do
   local orig=menu[name]
   if type(orig)=='function' then
    menu[name]=function(...)
     local t=p.env.getElapsedTime and p.env.getElapsedTime() or 0
     if name=='display' then S.overview_whole=t else S.overview_node=t end
     return orig(...)
    end
   end
  end
 end
 if not R or R.applied_station==S.station_seen then return end
 S.station_seen=R.applied_station
 if S.overview_whole and now-S.overview_whole<K.OVERVIEW_WINDOW then
  -- the chart's nodes are read by the menu's own setup, as its trade ware Confirm does before it draws
  if type(menu.setupFlowchartData)=='function' then K.viewed(p,menu.setupFlowchartData) end
  menu.refresh=true
 elseif S.overview_node and now-S.overview_node<K.OVERVIEW_WINDOW then menu.refreshnode=now end
end
-- After a batch of `slot` ran here (seen: what each record did), this client's open menus catch up; display only,
-- nothing here reaches the game. The station menu: a plan commit of this client's own player reloads the station's
-- plan (the stock menu's own timed reload, held back at the click); another player's commit does the same while this
-- player has no unconfirmed changes, and is told to this player when it has (its own Confirm would replace that plan
-- with one that does not hold the other player's modules). Money and builders: the plan side is drawn again. The
-- map's plot list: this player's new plot is selected as the stock map selects it when ReserveBuildPlot answers.
function K.applied(S,p,slot,seen)
 local own=slot==(p.own_slot or 1)
 local now=p.env.getElapsedTime and p.env.getElapsedTime() or 0
 local plan,other,plot=false,false,nil
 for _,what in ipairs(seen) do
  if what:find('^SetConstructionSequenceFromConstructionMap') or what:find('^CancelConstruction') then plan=true
  elseif what:find('^ReserveBuildPlot=') then plot=what:match('^ReserveBuildPlot=(%d+)$')
  else
   other=true
   -- a plot was resized, paid or removed: the open menus read the plot again once the calls have stopped coming
   -- (a person holds a size arrow: every step is its own call, applied a few ticks after its click)
   if what:find('^ExtendBuildPlot') or what:find('^PayBuildPlotSize') or what:find('^RemoveBuildPlot') then S.plot_settle=now+0.4 end
  end
 end
 local station=K.shown_menu(p,'StationConfigurationMenu')
 if station then
  if plan then
   if own then S.plan_pending=nil end
   if own or not station.haschanges then station.updatePlan=now
   elseif p.notify then pcall(p.notify,'Multiplayer: another player changed the plan of this station. Cancel your module changes to see it.') end
  elseif other then station.refresh=station.refresh or now end
 end
 local map=K.shown_menu(p,'MapMenu')
 if map and own and plot and plot~='0' and map.mode=='selectbuildlocation' and type(map.updatePlotData)=='function' then
  K.viewed(p,function()
   map.updatePlotData(p.env.ConvertStringTo64Bit(plot),true)
   map.refreshInfoFrame()
  end)
 elseif map and map.infoTableMode=='plots' and (plot or other) and type(map.refreshInfoFrame)=='function' then
  map.over=true      -- the stock map's own "draw the info frame again" flag (updatePlotData sets it)
 end
end
-- The open menus read a plot again (K.applied's plot_settle): the stock menus do it right after ExtendBuildPlot and
-- PayBuildPlotSize answer, when in a session the plot is still the old one: the box in the picture did not grow, the
-- size and the fee stayed, "Buy" seemed to do nothing (a person's report of run t2, 17 Nov), while the calls did land
-- on every client a few ticks later. Display only: the holomap's box, the menu's own plot record, a redraw.
function K.plot_settled(S,p,now)
 local station=K.shown_menu(p,'StationConfigurationMenu')
 if station and type(station.plotData)=='table' and station.plotData.component and station.holomap and station.holomap~=0 then
  p.C.UpdateConstructionMapBuildPlot(station.holomap)
  station.updatePlotData(station.plotData.component)
  station.refresh=station.refresh or now
 end
 local map=K.shown_menu(p,'MapMenu')
 if map and map.infoTableMode=='plots' and type(map.plotData)=='table' and map.plotData.placed and map.plotData.component and map.holomap and map.holomap~=0 then
  p.C.UpdateMapBuildPlot(map.holomap)
  map.updatePlotData(map.plotData.component,true)
  map.refreshInfoFrame()
 end
end
-- Every layout that exists checked: true and the missing type names, or nil and the first difference.
function K.check_layouts(ffi_)
 local missing={}
 for _,entry in ipairs(K.LAYOUTS) do
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
-- The families of a replay mask: build, assets (either may be off).
function K.parts(mask)
 local function has(bit) return math.floor(mask/bit)%2==1 end
 return has(K.FAMILY.build),has(K.FAMILY.assets)
end

-- Whether a replay mask holds every family of a list (by name).
function K.has_all(mask,names)
 for _,name in ipairs(names or {}) do
  local bit=K.ORDER_FAMILY[name] or K.FAMILY[name]
  if not bit or math.floor(mask/bit)%2~=1 then return false,name end
 end
 return true
end

-- p: gate, C (ffi.C), env (the Lua globals), log, mask, replay (the replay core's session: codec context, ops, extras),
-- own_slot, factions (factions mode) or nil, notify(text) or nil, local_mark or nil, script (by absolute tick) or nil,
-- fund {tick, amount} or nil, menus() -> the stock menu tables (Menus).
function K.session(p)
 local gate,env,log,R=p.gate,p.env,p.log,p.replay
 local build,assets=K.parts(p.mask)
 local core=assert(X4LAN_UI_REPLAY_CORE,'ui build: the replay core first')
 local S={applying=false,captured=0,refused=0,scopes=0,batches=0,wrapped={},guarded={},build=build,assets=assets,plots={}}
 local function on(id) local f=K.LUA_FAMILY[id];return (f=='build' and build) or (f=='assets' and assets) end
 local own_slot=p.own_slot or 1
 local trade=math.floor(p.mask/K.ORDER_FAMILY.trade)%2==1
 -- The order entries a hire needs, opened in the order block when their calls are all captured.
 if build and R and R.unblocked then
  for entry,needs in pairs(K.UNBLOCK) do if K.has_all(p.mask,needs) then R.unblocked[entry]=true end end
 end
 -- Stock functions, once per Lua state: the replay core's when it keeps them, else the global as it is now.
 local stock=env.X4LAN_UI_BUILD_STOCK
 if not stock then
  stock={}
  local kept=env.X4LAN_UI_REPLAY_STOCK or {}
  for _,name in ipairs(K.LUA) do stock[name]=kept[name] or env[name] end
  env.X4LAN_UI_BUILD_STOCK=stock
 end
 S.stock=stock
 local notice_at
 local function refuse(what,why,tick)
  S.refused=S.refused+1
  log('UI_BUILD_REFUSED tick='..tostring(tick or '-')..' op='..what..' why='..tostring(why))
  if p.notify and (not notice_at or (tick or 0)-notice_at>=K.NOTICE_TICKS) then
   notice_at=tick or 0
   pcall(p.notify,'Multiplayer: '..what..' is not shared for your slot yet - nothing happened.')
  end
 end
 -- A Lua record into this client's open batch (the native half orders it with the claimed calls); inside a station
 -- construction scope it is marked as such (factions mode: the slot's own faction pays at the apply).
 local function add(id,...)
  local hex,why=core.encode_call({...},select('#',...),R.ctx)
  if not hex then return refuse(K.LUA[id],why) end
  local ok,native_why=gate.ui_build_add(id,hex,S.scope and K.CONSTRUCTION[S.scope] and 1 or 0)
  if ok==1 then S.captured=S.captured+1 else refuse(K.LUA[id],native_why) end
 end
 S.add=add
 -- SetComponentName (assets): any rename a person's menu makes (ships, stations, the player's own name).
 if assets and stock.SetComponentName then
  env.SetComponentName=function(...)
   if S.applying or gate.ui_replay_local()~=1 then return stock.SetComponentName(...) end
   add(3,...)
  end
 end
 -- A commit scope around a stock menu function: money and the repair signal join the batch, the batch goes out at its end.
 local function capture(id,saved)
  if id==4 then
   return function(...)
    local _,signal=...
    if signal~=K.REPAIR_SIGNAL or S.applying or gate.ui_replay_local()~=1 then return saved(...) end
    add(4,...)
   end
  end
  return function(...)
   if S.applying or gate.ui_replay_local()~=1 then return saved(...) end
   add(id,...)
  end
 end
 -- Why a scope must not run at all on this client (nothing ran anywhere; a row and the info line), or nil.
 local function scope_refusal(name)
  if p.factions and own_slot>=2 then
   if not K.CONSTRUCTION[name] then return 'factions' end
   if not trade then return 'needs_trade' end
  end
  local ok,missing=K.has_all(p.mask,K.HIRE[name])
  if not ok then return 'needs_'..missing end
 end
 S.scope_refusal=scope_refusal
 function S.with_scope(name,fn,...)
  if S.scope or gate.ui_replay_local()~=1 then return fn(...) end
  local refused=scope_refusal(name)
  if refused then return refuse(name,refused) end
  S.scope=name;S.scopes=S.scopes+1
  local saved={}
  for id,global in ipairs(K.LUA) do
   if id~=3 and on(id) and env[global] then saved[global]=env[global];env[global]=capture(id,env[global]) end
  end
  local results=pack(pcall(fn,...))
  for global,f in pairs(saved) do env[global]=f end
  S.scope=nil
  gate.ui_build_flush()
  if not results[1] then error(results[2],0) end
  return unpack(results,2,results.n)
 end
 -- The stock menus' commit functions, wrapped once each menu exists (the handlers look them up at display time).
 local function wrap_menus()
  local menus=p.menus and p.menus() or {}
  for _,menu in ipairs(menus) do
   local names=type(menu)=='table' and K.SCOPES[menu.name]
   if names and not S.wrapped[menu.name] then
    S.wrapped[menu.name]=true
    local count=0
    for _,name in ipairs(names) do
     local orig=menu[name]
     if type(orig)=='function' then
      local scope=menu.name..'.'..name
      local after=K.AFTER[scope]
      menu[name]=function(...)
       local results=pack(S.with_scope(scope,orig,...))
       if after and gate.ui_replay_local()==1 then pcall(after,menu,S) end
       return unpack(results,1,results.n)
      end
      count=count+1
     end
    end
    log('UI_BUILD_SCOPES menu='..menu.name..' functions='..count)
   end
   local guards=type(menu)=='table' and K.GUARDS[menu.name]
   if guards and not S.guarded[menu.name] then
    S.guarded[menu.name]=true
    for name,why in pairs(guards) do
     local orig=menu[name]
     if type(orig)=='function' then
      local entry=menu.name..'.'..name
      menu[name]=function(...)
       local reason=gate.ui_replay_local()==1 and why(menu,...)
       if reason then refuse(entry,reason,S.tick);return 0 end
       return orig(...)
      end
     end
    end
   end
  end
 end
 -- A Lua record at its apply: the stock function with the decoded arguments (canonical, every client).
 local function run_lua(id,hex)
  local name=K.LUA[id]
  local args,n=core.decode_call(hex,R.ctx)
  if not args then
   if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_build_lua_decode') end
   return false,name..' '..tostring(n)
  end
  if not stock[name] then return false,name..' missing' end
  local ok,result=pcall(stock[name],unpack(args,1,n))
  return ok,ok and name..'('..tostring(args[1])..','..tostring(args[2])..')' or name..' '..tostring(result)
 end
 -- A part at its apply (the replay core's S.apply, every client, slot and call order): the batch runs when whole.
 function S.apply_part(slot,hex,tick)
  local whole,why=gate.ui_build_part(slot,hex)
  if whole==nil then log('UI_BUILD_DROPPED tick='..tick..' slot='..slot..' why='..tostring(why));return false,'dropped '..tostring(why) end
  if whole==0 then return true,'part' end
  S.applying=true
  local summary,ok,seen=nil,true,{}
  while true do
   local kind,a,b,scope=gate.ui_build_step(slot,tick)
   if kind=='ran' then
    log('UI_BUILD_APPLIED tick='..tick..' slot='..slot..' what='..tostring(a))
    local plot=tostring(a):match('^ReserveBuildPlot=(%d+)$')   -- the slot's newest plot (every client the same)
    if plot and plot~='0' then S.plots[slot]=plot end
    seen[#seen+1]=tostring(a)
   elseif kind=='lua' then
    -- factions mode, a construction record of slot 2+: its stock call runs in the slot's faction scope (the trade family's)
    local done,what,opened=false,K.LUA[a]..' scope',nil
    if scope==1 then opened=gate.ui_trade_scope and gate.ui_trade_scope(slot) or -1 end
    if not opened or opened>=0 then done,what=run_lua(a,b) end
    if opened and opened>=0 and gate.ui_trade_scope_end then gate.ui_trade_scope_end() end
    gate.ui_build_lua_done(slot,done and 1 or 0)
    log('UI_BUILD_APPLIED tick='..tick..' slot='..slot..' what='..tostring(what)..(opened==1 and ' scope' or '')..(done and '' or ' result=error'))
    seen[#seen+1]=tostring(what)
   elseif kind=='done' then summary=a;break
   else ok=false;summary='stop '..tostring(a);break end
  end
  S.applying=false
  S.batches=S.batches+1
  local shown,why=pcall(K.applied,S,p,slot,seen)
  if not shown then log('UI_BUILD_MENU_ERROR tick='..tick..' slot='..slot..' why='..tostring(why)) end
  return ok,summary
 end
 if R then
  R.ops=R.ops or {}
  R.ops[K.OP_PART]=S.apply_part
 end
 -- Test money (native_gate_ui_build_fund): the probe's own frame, every client at the same tick.
 local funded=false
 local function fund(tick)
  if funded or not p.fund or tick<p.fund.tick then return end
  funded=true
  p.C.AddPlayerMoney(p.fund.amount)
  log('UI_BUILD_FUND tick='..tick..' amount='..p.fund.amount..' money='..tostring(env.GetPlayerMoney and env.GetPlayerMoney()))
 end
 local script=p.script or {}
 local function run_script(tick)
  for _,e in ipairs(script[tick] or {}) do
   if p.local_mark then p.local_mark(0) end   -- as a person's click: not the probe's own frame
   local ok,result=pcall(e.call,S)
   if p.local_mark then p.local_mark(1) end
   log(string.format('UI_BUILD_SCRIPT tick=%d op=%s ok=%s result=%s',tick,tostring(e.op),tostring(ok),tostring(result)))
  end
 end
 -- The families' part of the probe's frame (the replay core's S.frame, after its own): the receipt joins the pilot receipt.
 function S.frame(tick)
  S.tick=tick
  wrap_menus()
  fund(tick)
  -- an order or another replayed call landed (a builder sent to the station, a builder fired): the open station menu
  -- shows it a moment later, as it shows its own calls in the stock game
  if S.plot_settle and env.getElapsedTime and env.getElapsedTime()>=S.plot_settle then
   S.plot_settle=nil
   local settled,why=pcall(K.viewed,p,K.plot_settled,S,p,env.getElapsedTime())
   if not settled then log('UI_BUILD_MENU_ERROR tick='..tick..' plot why='..tostring(why)) end
  end
  if R and R.applied_world~=S.world_seen then
   S.world_seen=R.applied_world
   local station=K.shown_menu(p,'StationConfigurationMenu')
   if station and env.getElapsedTime then station.refresh=station.refresh or (env.getElapsedTime()+0.2) end
  end
  local drawn,why=pcall(K.overview_frame,S,p,R,env.getElapsedTime and env.getElapsedTime() or 0)
  if not drawn then log('UI_BUILD_MENU_ERROR tick='..tick..' overview why='..tostring(why)) end
  run_script(tick)
  gate.ui_build_flush()
  local rows=gate.ui_build_refusals()
  if rows then for at,what,why in rows:gmatch('(%d+):([%w_]+):([%w_]+)') do refuse(what,why,tonumber(at)) end end
  if tick%120==0 then log('UI_BUILD_STATUS tick='..tick..' '..tostring(gate.ui_build_status())) end
  local money=env.GetPlayerMoney and env.GetPlayerMoney() or -1
  return '|'..assert(gate.ui_build_frame(tick),'ui build receipt unavailable')..':M'..string.format('%.0f',money)
 end
 if R then
  R.extras=R.extras or {}
  R.extras[#R.extras+1]=S.frame
 end
 return S
end

-- Script ops for runs without people (native_gate_ui_build_script, ticks relative to the window start), through the same
-- stock entries a person's menu calls, inside the same commit scopes.
function K.script(plan,start,tools)
 local by={}
 -- A station: 'plot' = this slot's newest plot (the apply's ReserveBuildPlot answer), else a saved ID; a decimal runtime ID.
 local function station_of(S,ref)
  if ref=='plot' then return assert(S.plots[tools.own_slot or 1],'ui build script: no plot reserved yet') end
  return (tostring(tools.saved_id(ref)):gsub('ULL$',''))
 end
 for _,e in ipairs(plan or {}) do
  local at,op=start+e[1],e[2]
  local call
  if op=='rename' then call=function() tools.env.SetComponentName(tools.slot_lua(e[3]),e[4]);return 'lua' end
  -- buttonConfirm of the ship configuration at a shipyard/wharf: pay, build with the station's generated loadout, record
  -- what was paid on the build task (the token the native half answered).
  elseif op=='buy' then call=function(S)
    return S.with_scope('script.buy',function()
     local station=tools.saved_id(e[3]);local price=e[5]
     tools.env.TransferPlayerMoneyTo(price,station)
     local loadout=tools.loadout(station,e[4])
     local crew=tools.ffi.new('CrewTransferInfo2')
     local info=tools.ffi.new('AddBuildTask6Container')
     local id=tools.C.AddBuildTask6(station,0,e[4],loadout,price,crew,false,e[6] or '',info)
     if id~=0 then tools.C.SetBuildTaskTransferredMoney(id,price) end
     return tostring(id)   -- exact: the token is above 2^53
    end) end
  -- The same button for a ship that is docked at the shipyard, wharf or equipment dock ("Upgrade / repair ship"):
  -- {t,'refit',station,ship,price[,level]}: the station's generated loadout for that hull at `level` (1: the high
  -- preset) as the upgrade plan, paid and ordered as buttonConfirm does (menu.repairandupgrade's AddBuildTask6 with the
  -- ship as its object). ship: a slot number or a saved ID.
  elseif op=='refit' then call=function(S)
    return S.with_scope('script.buy',function()
     local station=tools.saved_id(e[3]);local price=e[5]
     local ship=type(e[4])=='number' and tools.slot_id(e[4]) or tools.saved_id(e[4])
     local macro=tools.env.GetComponentData(type(e[4])=='number' and tools.slot_lua(e[4]) or tools.saved_lua(e[4]),'macro')
     tools.env.TransferPlayerMoneyTo(price,station)
     local loadout=tools.loadout(station,macro,ship,e[6] or 1)
     local crew=tools.ffi.new('CrewTransferInfo2')
     local info=tools.ffi.new('AddBuildTask6Container')
     local id=tools.C.AddBuildTask6(station,ship,macro,loadout,price,crew,false,'',info)
     if id~=0 then tools.C.SetBuildTaskTransferredMoney(id,price) end
     return tostring(id)..' macro='..tostring(macro)
    end) end
  -- The map's "Sell ships" confirm for one ship that is docked at the shipyard or wharf (MapMenu.buttonSellShips, a
  -- commit scope): {t,'sell',station,ship}: the ship's value from the station to the player, then SellPlayerShip.
  -- ship: a slot number or a saved ID.
  elseif op=='sell' then call=function(S)
    return S.with_scope('script.sell',function()
     pcall(tools.ffi.cdef,'void SellPlayerShip(UniverseID shipid, UniverseID shipyardid);')
     local station=tools.saved_id(e[3])
     local ship=type(e[4])=='number' and tools.slot_id(e[4]) or tools.saved_id(e[4])
     local price=tools.env.GetTotalValue(ship,true,station)
     tools.env.TransferMoneyToPlayer(price,station,'sellship')
     tools.C.SellPlayerShip(ship,station)
     return string.format('sold price=%.0f',tonumber(price) or -1)
    end) end
  -- The ship configuration's Confirm with a repair for a ship that is docked at the shipyard, wharf or equipment
  -- dock (menu.repairandupgrade): {t,'repair',station,ship}: the repair's price to the station, then the station's
  -- signal 'repairs_initiate' with the ship (the whole hull, no single components). ship: a slot or a saved ID.
  elseif op=='repair' then call=function(S)
    return S.with_scope('script.repair',function()
     pcall(tools.ffi.cdef,'int64_t GetRepairPrice(UniverseID componenttorepairid, UniverseID containerid);')
     local station=tools.saved_id(e[3])
     local ship=type(e[4])=='number' and tools.slot_id(e[4]) or tools.saved_id(e[4])
     local lua=type(e[4])=='number' and tools.slot_lua(e[4]) or tools.saved_lua(e[4])
     local price=tonumber(tools.C.GetRepairPrice(ship,station))
     tools.env.TransferPlayerMoneyTo(price,station)
     tools.env.SignalObject(station,'repairs_initiate',lua,{100,{}})
     return string.format('repair price=%.0f',price)
    end) end
  -- the map's plot placement: ReserveBuildPlot (answers 0 here, the plot appears at the apply)
  elseif op=='plot' then call=function()
    local pos=tools.ffi.new('UIPosRot',{e[4],e[5],e[6],0,0,0})
    return tostring(tools.C.ReserveBuildPlot(tools.saved_id(e[3]),'player','factory',pos,e[7]*1000,e[7]*1000,e[7]*1000)) end
  elseif op=='dismantle' then call=function() tools.C.DismantleShipMod(tools.slot_id(e[3]));return 'ffi' end
  elseif op=='exchange' then call=function()
    local r=tools.C.PerformCrewExchange2(tools.slot_id(e[3]),tools.slot_id(e[4]),nil,0,nil,0,0,0,false,false)
    return 'reason='..tools.ffi.string(r.reason) end
  -- Station construction (reports/LOCKSTEP-UI-CONSTRUCT-20261101.txt). The station configuration's confirm as a record
  -- of its own: {t,'plan',station,'macro,connection,pred,predconnection,x,y,z;...'} ('-' none; the identity rotation).
  elseif op=='plan' then call=function(S)
    return S.with_scope('script.plan',function()
     local ok,why=tools.gate.ui_build_plan(station_of(S,e[3]),e[4])
     return ok==1 and 'plan' or 'refused='..tostring(why)
    end) end
  -- The construction budget (buttonConfirmMoney): money to the station's build storage.
  elseif op=='budget' then call=function(S)
    return S.with_scope('script.budget',function()
     local station=tools.env.ConvertStringTo64Bit(station_of(S,e[3]))
     local storage=tools.env.GetComponentData(station,'buildstorage')
     if not storage or storage==0 then return 'no_buildstorage' end
     tools.env.TransferPlayerMoneyTo(e[4],tools.env.ConvertStringTo64Bit(tostring(storage)))
     return 'budget'
    end) end
  -- Hiring a builder for a station (the map's selectCV): the fee when the builder is not the player's, then its order.
  elseif op=='hire' then call=function(S)
    return S.with_scope('script.hire',function()
     local builder=tools.saved_id(e[3])
     if not tools.env.GetComponentData(builder,'isplayerowned') then tools.env.TransferPlayerMoneyTo(tonumber(tools.C.GetBuilderHiringFee()),builder) end
     tools.C.RemoveAllOrders(builder)
     local idx=tools.C.CreateDeployToStationOrder(builder)
     if idx>0 then tools.env.SetOrderParam(tools.saved_lua(e[3]),idx,1,nil,tools.env.ConvertStringToLuaID(station_of(S,e[4])));tools.C.EnableOrder(builder,idx) end
     return 'order='..tostring(idx)
    end) end
  -- Cancelling the station's construction (the interact menu's buttonCancelConstruction): its first build task.
  elseif op=='cancel' then call=function(S)
    return S.with_scope('script.cancel',function()
     local station=tools.env.ConvertStringTo64Bit(station_of(S,e[3]))
     for _,progress in ipairs({true,false}) do
      local n=tonumber(tools.C.GetNumBuildTasks(station,0,progress,false))
      if n and n>0 then
       local tasks=tools.ffi.new('BuildTaskInfo[?]',n)
       n=tonumber(tools.C.GetBuildTasks(tasks,n,station,0,progress,false))
       if n>0 then return 'cancel='..tostring(tools.C.CancelConstruction(station,tasks[0].id)) end
      end
     end
     return 'no_task'
    end) end
  else error('unknown ui build script op '..tostring(op)) end
  by[at]=by[at] or {};by[at][#by[at]+1]={op=op,call=call}
 end
 return by
end
