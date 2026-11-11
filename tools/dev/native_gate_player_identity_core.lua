-- Lockstep player identity: session plan validation, canonical text and the ordered setup steps.
-- Pure Lua, no engine calls (LuaJIT-tested). Engine glue: native_gate_player_identity.lua; MD:
-- native_gate_player_identity.xml; Python reference: native_gate_player_identity.py.
-- Design and limits: reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt.
-- Every client holds the identical plan (overlay config), so every client derives the identical
-- step list and raises it at the same gated tick. No runtime ID, pointer or machine-local value
-- (player name, local slot, wall time) enters the plan, the steps or their labels.
X4LAN_PLAYER_IDENTITY_CORE={}
local K=X4LAN_PLAYER_IDENTITY_CORE
K.VERSION=1
K.MAX_SLOTS=16
K.TICK_MIN,K.TICK_MAX=2,29   -- setup before the pilot window (controls park at 27, flight from 30)
K.FOLD_LAG=10                -- MD results must be back in Lua this many ticks after the setup tick
K.COORD_LIMIT=10000000       -- sector-local metres, integers
K.SEED_MAX=2147483647
K.MAX_STARTS=16              -- start placements per plan
K.ZERO=string.rep('0',16)
-- Relation values follow the base mod's player factions (ui/player_factions_core.lua).
K.RELATIONS={allied=0.75,neutral=0,hostile=-0.5}
K.MODES={factions=true,coop=true}
K.OCCUPIED={player=true,pool=true}
K.REPUTATION={mirror=true,keep=true}
local PLAN_KEYS={version=true,mode=true,tick=true,occupied=true,reputation=true,slots=true,relations=true,starts=true,kits=true}
-- Per-player starts (kits): the player-level parts of slot k's stock start (native_gate_gamestarts.py resolves
-- them at session setup; reports/LOCKSTEP-STARTS-20261101.txt).
local KIT_KEYS={slot=true,gamestart=true,money=true,reputation=true,blueprints=true,research=true,known=true,omitted=true,inventory=true}
local KIT_INVENTORY_KEYS={ware=true,amount=true}
local KIT_REPUTATION_KEYS={faction=true,value=true}
local KIT_KNOWN_KEYS={sectors=true,faction=true,shipyards=true}
K.KIT_PARTS={blueprints=true,inventory=true,known=true,reputation=true,research=true}
K.KIT_ITEMS=64
K.KIT_AMOUNT_MAX=1000000   -- pieces of one inventory ware in a kit
K.MONEY_MAX=1000000000000   -- credits
K.MONEY_PART=1000000        -- the wallet step's MD parts: mega=floor(money/1e6) <= 1e6, units=money%1e6 (both < 2^24)
K.VALUE_SCALE=1000000       -- reputation values travel as integer millionths
local SLOT_KEYS={ship=true,create=true,faction=true}
local CREATE_KEYS={macro=true,seed=true,sector=true,x=true,y=true,z=true,yaw=true}
local RELATION_KEYS={a=true,b=true,relation=true}
local START_KEYS={object=true,sector=true,x=true,y=true,z=true,yaw=true}

local Refusal={}
local function refuse(reason) error(setmetatable({reason=reason},Refusal),0) end
local function guarded(fn,...)
 local ok,value=pcall(fn,...)
 if ok then return value end
 if getmetatable(value)==Refusal then return nil,value.reason end
 error(value,0)
end
K.guarded=guarded
local function integer(v,lo,hi) return type(v)=='number' and v==math.floor(v) and v>=lo and v<=hi end
K.integer=integer
function K.saved(id) return type(id)=='string' and #id==16 and not id:find('[^0-9a-f]') and id~=K.ZERO end
function K.macro(name) return type(name)=='string' and #name<=64 and name:match('^[a-z][a-z0-9_]*_macro$')~=nil end
-- A Lua array with exactly the keys 1..n (config tables are built from JSON lists).
local function list(t,reason)
 if type(t)~='table' then refuse(reason) end
 local n,count=#t,0
 for _ in pairs(t) do count=count+1 end
 if count~=n then refuse(reason) end
 return n
end
local function keys(t,allowed,reason)
 if type(t)~='table' then refuse(reason) end
 for key in pairs(t) do if not allowed[key] then refuse('key_unknown') end end
end
-- Slot k's own pool faction; 'player' is the vanilla player faction.
function K.pool(slot) return 'x4lan_player'..slot end

local function validate(plan)
 keys(plan,PLAN_KEYS,'plan_invalid')
 if plan.version~=K.VERSION then refuse('version') end
 if not K.MODES[plan.mode] then refuse('mode') end
 if not integer(plan.tick,K.TICK_MIN,K.TICK_MAX) then refuse('tick') end
 local occupied=plan.occupied==nil and 'player' or plan.occupied
 local reputation=plan.reputation==nil and 'mirror' or plan.reputation
 if not K.OCCUPIED[occupied] then refuse('occupied') end
 if not K.REPUTATION[reputation] then refuse('reputation') end
 local n=list(plan.slots,'slots')
 if n<1 or n>K.MAX_SLOTS then refuse('slots') end
 local P={mode=plan.mode,tick=plan.tick,occupied=occupied,reputation=reputation,slots={},relations={}}
 local ships,player_slots={},0
 for k=1,n do
  local s=plan.slots[k]
  keys(s,SLOT_KEYS,'slot_invalid')
  local row={index=k,faction=s.faction}
  if (s.ship==nil)==(s.create==nil) then refuse('slot_source') end
  if s.ship~=nil then
   if not K.saved(s.ship) then refuse('ship_invalid') end
   if ships[s.ship] then refuse('ship_duplicate') end
   ships[s.ship]=true;row.ship=s.ship
  else
   local c=s.create
   keys(c,CREATE_KEYS,'create_invalid')
   if type(c.macro)~='string' or #c.macro>64 or not c.macro:match('^[a-z][a-z0-9_]*_macro$') then refuse('macro_invalid') end
   if not integer(c.seed,0,K.SEED_MAX) then refuse('seed_invalid') end
   local sector=c.sector==nil and 'player' or c.sector
   if sector~='player' and not K.saved(sector) then refuse('sector_invalid') end
   for _,axis in ipairs({'x','y','z'}) do
    if not integer(c[axis],-K.COORD_LIMIT,K.COORD_LIMIT) then refuse('coordinate_invalid') end
   end
   local yaw=c.yaw==nil and 0 or c.yaw
   if not integer(yaw,-180,180) then refuse('rotation_invalid') end
   row.create={macro=c.macro,seed=c.seed,sector=sector,x=c.x,y=c.y,z=c.z,yaw=yaw}
  end
  if plan.mode=='coop' then
   if s.faction~='player' then refuse('faction_mode') end
  else
   if s.faction~='player' and s.faction~=K.pool(k) then refuse('faction_invalid') end
   if s.faction=='player' then player_slots=player_slots+1 end
  end
  P.slots[k]=row
 end
 if player_slots>1 then refuse('faction_duplicate') end
 local m=plan.relations==nil and 0 or list(plan.relations,'relations')
 if plan.mode=='coop' then
  if m>0 then refuse('relations_mode') end
 else
  local seen={}
  for i=1,m do
   local r=plan.relations[i]
   keys(r,RELATION_KEYS,'relation_invalid')
   if not integer(r.a,1,n) or not integer(r.b,1,n) or r.a>=r.b then refuse('relation_pair') end
   if K.RELATIONS[r.relation]==nil then refuse('relation_invalid') end
   local key=r.a..'-'..r.b
   if seen[key] then refuse('relation_duplicate') end
   seen[key]=true
   P.relations[#P.relations+1]={a=r.a,b=r.b,relation=r.relation,value=K.RELATIONS[r.relation]}
  end
  -- Every pair of slot factions is stated explicitly: no implicit library default decides PvP.
  if m~=n*(n-1)/2 then refuse('relation_missing') end
  table.sort(P.relations,function(x,y) if x.a~=y.a then return x.a<y.a end return x.b<y.b end)
 end
 -- Different starts: saved ships (slot ships or any other) warped to a fixed sector-local point after
 -- ownership, in list order. A fixed point, no safe-position search (it draws randomness). The sector is
 -- a saved ID or a sector macro name (MD find_sector; also sectors the player does not know).
 local s=plan.starts==nil and 0 or list(plan.starts,'starts')
 if s>K.MAX_STARTS then refuse('starts') end
 P.starts={}
 local placed={}
 for i=1,s do
  local r=plan.starts[i]
  keys(r,START_KEYS,'start_invalid')
  if not K.saved(r.object) then refuse('start_object_invalid') end
  if placed[r.object] then refuse('start_duplicate') end
  placed[r.object]=true
  if not K.saved(r.sector) and not K.macro(r.sector) then refuse('sector_invalid') end
  for _,axis in ipairs({'x','y','z'}) do
   if not integer(r[axis],-K.COORD_LIMIT,K.COORD_LIMIT) then refuse('coordinate_invalid') end
  end
  local yaw=r.yaw==nil and 0 or r.yaw
  if not integer(yaw,-180,180) then refuse('rotation_invalid') end
  P.starts[i]={object=r.object,sector=r.sector,x=r.x,y=r.y,z=r.z,yaw=yaw}
 end
 P.kits=K.kits(plan,P,n)
 return P
end
local function ident(v) return type(v)=='string' and #v<=64 and v:match('^[a-z][a-z0-9_]*$')~=nil end
local function ascending(t) for i=2,#t do if not (t[i-1]<t[i]) then return false end end return true end
local function npc_faction(v) return ident(v) and v~='player' and v:match('^x4lan_player[0-9]+$')==nil end
local function wares(r,key,factions)
 local w=r[key]==nil and {} or r[key]
 local ok,n=pcall(list,w,'kit_wares')
 if not ok then refuse('kit_wares') end
 if n>K.KIT_ITEMS then refuse('kit_wares') end
 if n>0 and factions then refuse('kit_library_mode') end   -- one blueprint/research library: the player faction's
 local out={}
 for i=1,n do
  if not ident(w[i]) then refuse('kit_ware') end
  out[i]=w[i]
 end
 if not ascending(out) then refuse('kit_order') end
 return out
end
-- Per-player starts: slot k (2..n) takes the player-level parts of its own stock start; slot 1 plays the session
-- base start (the save). Factions: money on slot k's own account, reputation of slot k's faction. Coop: money
-- adds to the one shared wallet, blueprints/research/map knowledge join the one player library. A part that
-- cannot be applied for the slot is named in 'omitted' (never dropped silently). Same rules and reasons as Python.
function K.kits(plan,P,n)
 local m=plan.kits==nil and 0 or list(plan.kits,'kits')
 if m>K.MAX_SLOTS then refuse('kits') end
 local factions,out,seen=P.mode=='factions',{},{}
 for i=1,m do
  local r=plan.kits[i]
  keys(r,KIT_KEYS,'kit_invalid')
  if not integer(r.slot,2,n) then refuse('kit_slot') end
  if seen[r.slot] then refuse('kit_duplicate') end
  seen[r.slot]=true
  if factions and P.slots[r.slot].faction=='player' then refuse('kit_player_slot') end   -- the base start's
  if not ident(r.gamestart) then refuse('kit_gamestart') end
  if not integer(r.money,0,K.MONEY_MAX) then refuse('kit_money') end
  local rows=r.reputation==nil and {} or r.reputation
  local ok,count=pcall(list,rows,'kit_reputation')
  if not ok or count>K.KIT_ITEMS then refuse('kit_reputation') end
  if count>0 and not factions then refuse('kit_reputation_mode') end   -- coop: one reputation set, the base start's
  local reputation,names={},{}
  for j=1,count do
   local row=rows[j]
   keys(row,KIT_REPUTATION_KEYS,'kit_reputation')
   if not npc_faction(row.faction) then refuse('kit_faction') end
   if not integer(row.value,-K.VALUE_SCALE,K.VALUE_SCALE) then refuse('kit_value') end
   reputation[j]={faction=row.faction,value=row.value};names[j]=row.faction
  end
  if not ascending(names) then refuse('kit_order') end
  local blueprints,research=wares(r,'blueprints',factions),wares(r,'research',factions)
  local known=nil
  if r.known~=nil then
   keys(r.known,KIT_KNOWN_KEYS,'kit_known')
   if factions then refuse('kit_known_mode') end   -- one map knowledge: the player faction's
   local s=r.known.sectors
   local okk,ns=pcall(list,s,'kit_known')
   if not okk or ns<1 or ns>K.KIT_ITEMS or type(r.known.shipyards)~='boolean' then refuse('kit_known') end
   local sectors,dup={},{}
   for j=1,ns do
    if not K.macro(s[j]) or dup[s[j]] then refuse('kit_known') end
    dup[s[j]]=true;sectors[j]=s[j]
   end
   if r.known.faction~=nil and not npc_faction(r.known.faction) then refuse('kit_faction') end
   known={sectors=sectors,faction=r.known.faction,shipyards=r.known.shipyards}
  end
  local omitted=r.omitted==nil and {} or r.omitted
  local oko,no=pcall(list,omitted,'kit_omitted')
  if not oko then refuse('kit_omitted') end
  local omit={}
  for j=1,no do
   if type(omitted[j])~='string' or not K.KIT_PARTS[omitted[j]] then refuse('kit_omitted') end
   omit[j]=omitted[j]
  end
  if not ascending(omit) then refuse('kit_omitted') end
  -- The start's inventory: wares for slot k's own player entity, both modes (the probe adds them inside slot k's scope).
  local stock=r.inventory==nil and {} or r.inventory
  local oki,ni=pcall(list,stock,'kit_inventory')
  if not oki or ni>K.KIT_ITEMS then refuse('kit_inventory') end
  local inventory,inventory_wares={},{}
  for j=1,ni do
   local row=stock[j]
   keys(row,KIT_INVENTORY_KEYS,'kit_inventory')
   if not ident(row.ware) then refuse('kit_ware') end
   if not integer(row.amount,1,K.KIT_AMOUNT_MAX) then refuse('kit_amount') end
   inventory[j]={ware=row.ware,amount=row.amount};inventory_wares[j]=row.ware
  end
  if not ascending(inventory_wares) then refuse('kit_order') end
  local given={blueprints=#blueprints>0,research=#research>0,reputation=#reputation>0,known=known~=nil,inventory=#inventory>0}
  for _,o in ipairs(omit) do if given[o] then refuse('kit_omitted_conflict') end end
  out[#out+1]={slot=r.slot,gamestart=r.gamestart,money=r.money,reputation=reputation,blueprints=blueprints,research=research,
   known=known,omitted=omit,inventory=inventory}
 end
 table.sort(out,function(a,b) return a.slot<b.slot end)
 return out
end
-- Validated, normalized plan or nil,reason.
function K.validate(plan) return guarded(validate,plan) end

local function create_label(c)
 return string.format('%s/%d/%s/%d,%d,%d/%d',c.macro,c.seed,c.sector,c.x,c.y,c.z,c.yaw)
end
local function ship_label(row) return row.ship or 'create:'..create_label(row.create) end
local function start_label(r) return string.format('%s@%s/%d,%d,%d/%d',r.object,r.sector,r.x,r.y,r.z,r.yaw) end
-- Canonical plan text: identical in Lua and Python (tests cross-check); its SHA-256 is the plan digest.
function K.canonical(P)
 local rows={}
 for _,row in ipairs(P.slots) do
  rows[#rows+1]=row.ship and string.format('%d:ship:%s:%s',row.index,row.ship,row.faction)
   or string.format('%d:create:%s:%s',row.index,create_label(row.create),row.faction)
 end
 local relations={}
 for _,r in ipairs(P.relations) do relations[#relations+1]=r.a..'-'..r.b..':'..r.relation end
 local starts={}
 for _,r in ipairs(P.starts) do starts[#starts+1]=start_label(r) end
 local kits={}
 for _,k in ipairs(P.kits or {}) do kits[#kits+1]=K.kit_label(k) end
 -- Plans without starts or kits keep their earlier text (and digest).
 return string.format('PIDENT_PLAN_1|mode=%s|tick=%d|occupied=%s|reputation=%s|slots=%s|relations=%s%s%s',P.mode,P.tick,P.occupied,
  P.reputation,table.concat(rows,','),#relations>0 and table.concat(relations,',') or '-',
  #starts>0 and '|starts='..table.concat(starts,',') or '',#kits>0 and '|kits='..table.concat(kits,';') or '')
end
local function joined(t) return #t>0 and table.concat(t,'+') or '-' end
function K.kit_label(k)
 local rep={}
 for _,r in ipairs(k.reputation) do rep[#rep+1]=string.format('%s/%d',r.faction,r.value) end
 local known='-'
 if k.known then known=string.format('%s/%d/%s',k.known.faction or '-',k.known.shipyards and 1 or 0,table.concat(k.known.sectors,'+')) end
 local inv={}
 for _,i in ipairs(k.inventory or {}) do inv[#inv+1]=string.format('%s/%d',i.ware,i.amount) end
 return string.format('%d:%s:money=%d:rep=%s:bp=%s:rs=%s:known=%s:omit=%s',k.slot,k.gamestart,k.money,joined(rep),joined(k.blueprints),
  joined(k.research),known,joined(k.omitted))..(#inv>0 and ':inv='..table.concat(inv,'+') or '')   -- kits without one keep their text
end
local function value_text(v) return v==0 and '0' or tostring(v) end
-- Ordered setup steps. Each carries its machine-neutral head for the PLAYER_IDENTITY_APPLIED line;
-- the engine glue adds the process-local objects (runtime IDs) to the MD event only.
function K.steps(P,digest)
 local out={}
 local function add(step) step.ordinal=#out+1;out[#out+1]=step;return step end
 local function head(step,text) step.head=string.format('tick=%d ordinal=%d step=%s slot=%d %s',P.tick,step.ordinal,step.step,step.slot,text) end
 local factions=P.mode=='factions'
 local begin=add({step='begin',slot=0})
 for _,row in ipairs(P.slots) do
  if factions and row.faction~='player' then
   local s=add({step='faction',slot=row.index,faction=row.faction,mirror=P.reputation=='mirror' and 1 or 0,
    name='Player '..row.index,short='P'..row.index})
   head(s,string.format('faction=%s mirror=%d',row.faction,s.mirror))
  end
 end
 for _,row in ipairs(P.slots) do
  local s=add({step='ship',slot=row.index,faction=row.faction,source=row.ship and 'saved' or 'create',ship=row.ship,create=row.create,
   occupiedpool=P.occupied=='pool' and 1 or 0})
  head(s,string.format('ship=%s source=%s faction=%s',ship_label(row),s.source,row.faction))
 end
 for i,r in ipairs(P.starts) do
  local s=add({step='start',slot=0,start=r})
  head(s,string.format('start=%d object=%s sector=%s at=%d,%d,%d/%d',i,r.object,r.sector,r.x,r.y,r.z,r.yaw))
 end
 for _,r in ipairs(P.relations) do
  local s=add({step='relation',slot=0,a=r.a,b=r.b,fa=P.slots[r.a].faction,fb=P.slots[r.b].faction,relation=r.relation,value=r.value})
  head(s,string.format('pair=%d-%d factions=%s/%s relation=%s value=%s',r.a,r.b,s.fa,s.fb,r.relation,value_text(r.value)))
 end
 -- Per-player starts, after the relations (a kit's reputation overrides the mirror) and in slot order.
 for _,k in ipairs(P.kits or {}) do
  local faction=P.slots[k.slot].faction
  local s=add({step='kit',slot=k.slot,gamestart=k.gamestart,faction=faction})
  head(s,string.format('gamestart=%s faction=%s omitted=%s',k.gamestart,faction,joined(k.omitted)))
  -- Factions sets slot k's own account, coop adds to the shared wallet. MD gets the whole credits in two parts below
  -- 2^24 (mega = millions, units = the rest), exact however MD stores a Lua number, and rebuilds the money value
  -- from them (never one float of up to 1e14 cents).
  s=add({step='wallet',slot=k.slot,faction=faction,op=factions and 'set' or 'add',mega=math.floor(k.money/K.MONEY_PART),
   units=k.money%K.MONEY_PART})
  head(s,string.format('wallet=%s op=%s money=%d',faction,s.op,k.money))
  for _,r in ipairs(k.reputation) do
   s=add({step='reputation',slot=k.slot,faction=faction,other=r.faction,value=r.value/K.VALUE_SCALE})
   head(s,string.format('faction=%s other=%s value=%d',faction,r.faction,r.value))
  end
  for _,w in ipairs(k.blueprints) do s=add({step='blueprint',slot=k.slot,ware=w});head(s,'ware='..w) end
  for _,w in ipairs(k.research) do s=add({step='research',slot=k.slot,ware=w});head(s,'ware='..w) end
  if k.known then
   for i,sector in ipairs(k.known.sectors) do
    s=add({step='known_sector',slot=k.slot,knownsector=sector})
    head(s,string.format('sector=%s index=%d',sector,i))
   end
   s=add({step='known',slot=k.slot,count=#k.known.sectors,kfaction=k.known.faction or '',hasfaction=k.known.faction and 1 or 0,
    shipyards=k.known.shipyards and 1 or 0})
   head(s,string.format('sectors=%d faction=%s shipyards=%d',s.count,k.known.faction or '-',s.shipyards))
  end
 end
 local finish=add({step='end',slot=0})
 head(begin,string.format('mode=%s slots=%d occupied=%s reputation=%s steps=%d plan=%s',P.mode,#P.slots,P.occupied,P.reputation,#out,digest))
 head(finish,string.format('steps=%d',#out))
 return out
end
-- MD result rows ('<ordinal>|<result>|<readback>') in ordinal order, checked against the steps.
-- Returns the canonical result text or nil,reason (fail closed in the glue).
function K.results(steps,rows)
 if #rows~=#steps then return nil,'result_count:'..#rows..'/'..#steps end
 local parts={}
 for i,raw in ipairs(rows) do
  local ordinal,result,readback=tostring(raw):match('^(%d+)|([a-z_]+)|(.*)$')
  if not ordinal or tonumber(ordinal)~=i then return nil,'result_order:'..i end
  if result~='applied' and result~='unchanged' then return nil,'refused:'..i..':'..result end
  parts[#parts+1]=ordinal..'|'..result..'|'..readback
 end
 return 'PIDENT_MD_1|'..table.concat(parts,';')
end
