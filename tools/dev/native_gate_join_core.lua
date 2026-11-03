-- Hot join: the JOIN input section (kind 0x4a) on every client. Pure Lua, no engine calls (LuaJIT-tested through
-- tests/test_native_gate_join_lua.py). Records and their layout: tools/dev/native_gate_join.py (the reference);
-- the gate side: src/native/engine/lockstep_join.h; report: reports/LOCKSTEP-HOTJOIN-20261101.txt.
--   SAVE_AT  (relay, in absent slot k's stand-in blob)  every client in the mask arms its gate to save after step T
--   SAVED / FAILED (a saver, in its own blob after T)   the package it wrote, or why it could not
--   HANDOVER (relay, in slot k's blob)                  slot k is a full client from tick H on: its setup runs at H
--   ABORT    (relay, in slot k's blob)                  the join of slot k is over
-- A control record counts only in the blob of the slot it names (the relay's stand-in for that absent slot); a
-- saver's record only in another slot's blob. Anything else is refused and logged, never applied.
X4LAN_JOIN_CORE={}
local K=X4LAN_JOIN_CORE
K.VERSION=1
K.OP={save_at=1,saved=2,failed=3,handover=4,abort=5}
K.OPNAME={'save_at','saved','failed','handover','abort'}
K.REASONS={'not_armed','not_possible','save_failed','file_missing','capture','unsupported_module','package_write','busy','timeout',
 'mismatch','saver_disagree','joiner_lost','pause_too_long','refused_by_relay','platform'}
K.SIZE={16,63,8,7,8}
local function hex_ok(h) return type(h)=='string' and #h%2==0 and not h:find('[^0-9a-f]') end
local function byte(h,i) return tonumber(h:sub(2*i-1,2*i),16) end
local function u(h,i,n) local v=0;for k=n,1,-1 do v=v*256+byte(h,i+k-1) end;return v end   -- little endian, n<=6 exact
local function le(v,n) local out={};for _=1,n do out[#out+1]=string.format('%02x',v%256);v=math.floor(v/256) end;return table.concat(out) end
local function integer(v,lo,hi) return type(v)=='number' and v==math.floor(v) and v>=lo and v<=hi end
-- Record (table) -> payload hex, or nil,reason. nonce is 16 hex (u64 little endian on the wire as in Python).
function K.encode(r)
 local op=K.OP[r.op]
 if not op or not integer(r.slot,1,8) or not integer(r.tick,0,4294967295) then return nil,'record_field' end
 local h=string.format('%02x%02x%02x',K.VERSION,op,r.slot)..le(r.tick,4)
 if op==K.OP.save_at then
  if not integer(r.mask,1,255) or not (type(r.nonce)=='string' and #r.nonce==16 and hex_ok(r.nonce)) then return nil,'record_field' end
  local n={};for i=8,1,-1 do n[#n+1]=r.nonce:sub(2*i-1,2*i) end
  h=h..string.format('%02x',r.mask)..table.concat(n)
 elseif op==K.OP.saved then
  if not integer(r.bytes,1,2^53) or not (hex_ok(r.package) and #r.package==64) or not (hex_ok(r.portable) and #r.portable==32) then return nil,'record_field' end
  h=h..le(r.bytes,6)..'0000'..r.package..r.portable
 elseif op==K.OP.failed or op==K.OP.abort then
  local code;for i,name in ipairs(K.REASONS) do if name==r.reason then code=i end end
  if not code then return nil,'record_field' end
  h=h..string.format('%02x',code)
 end
 return h
end
function K.decode(h)
 if not hex_ok(h) then return nil,'encoding' end
 if #h<14 then return nil,'record_length' end
 if byte(h,1)~=K.VERSION then return nil,'join_version' end
 local op=byte(h,2)
 if not K.OPNAME[op] then return nil,'record_op' end
 if #h~=2*K.SIZE[op] then return nil,'record_length' end
 local r={op=K.OPNAME[op],slot=byte(h,3),tick=u(h,4,4)}
 if r.slot<1 or r.slot>8 then return nil,'record_field' end
 if op==K.OP.save_at then
  r.mask=byte(h,8)
  local n={};for i=16,9,-1 do n[#n+1]=h:sub(2*i-1,2*i) end
  r.nonce=table.concat(n)
  if r.mask==0 then return nil,'record_field' end
 elseif op==K.OP.saved then
  if u(h,14,2)~=0 then return nil,'record_field' end   -- byte counts above 2^48 are not representable here
  r.bytes=u(h,8,6);r.package=h:sub(31,94);r.portable=h:sub(95,126)
 elseif op==K.OP.failed or op==K.OP.abort then
  r.reason=K.REASONS[byte(h,8)]
  if not r.reason then return nil,'record_field' end
 end
 return r
end

-- The section adapter of one client. o: slot (own), log(text), arm(T,k,mask) -> true|false,
-- result() -> 'state|T|k|bytes|package|portable|why|detail' (gate.join_result), handover(k,H), optional abort(k) (the
-- absent slot's records end: gate.pilot_abort), joiner (true for a client that loaded a join package; it applies no
-- SAVE_AT).
function K.client(o)
 assert(integer(o.slot,1,8) and type(o.log)=='function' and type(o.arm)=='function' and type(o.result)=='function' and
  type(o.handover)=='function','join client needs slot, log, arm, result and handover')
 -- save_ticks[T]: a SAVE_AT for save tick T was applied here (every client, in the mask or not: native_gate_world_hash_join).
 local C={armed=nil,outbox=nil,handovers={},aborted={},refusals=0,saves=0,save_ticks={}}  -- not 'refused': the multiplexer's optional callback name
 local function refuse(tick,slot,why) C.refusals=C.refusals+1;o.log('JOIN_REFUSED tick='..tick..' slot='..slot..' reason='..why) end
 function C.apply(tick,inputs,clients)
  local receipt={}
  for s=1,#inputs do
   local hex=inputs[s]
   if hex and hex~='' then
    local r,why=K.decode(hex)
    if not r then refuse(tick,s,why)
    elseif (r.op=='save_at' or r.op=='handover' or r.op=='abort') and r.slot~=s then refuse(tick,s,'control_outside_its_slot')
    elseif (r.op=='saved' or r.op=='failed') and r.slot==s then refuse(tick,s,'saver_record_in_joining_slot')
    else
     receipt[#receipt+1]=s..':'..r.op..':'..r.slot..':'..r.tick
     if r.op=='save_at' then
      local mine=math.floor(r.mask/2^(o.slot-1))%2==1
      o.log(string.format('JOIN_SAVE_AT tick=%d k=%d save_tick=%d mask=%d nonce=%s mine=%d',tick,r.slot,r.tick,r.mask,r.nonce,mine and 1 or 0))
      C.save_ticks[r.tick]=true
      if mine and not o.joiner then
       if C.armed then
        C.outbox={op='failed',slot=r.slot,tick=r.tick,reason='busy'}
       elseif r.tick<=tick+1 or not o.arm(r.tick,r.slot,r.mask) then
        C.outbox={op='failed',slot=r.slot,tick=r.tick,reason='not_armed'}
        o.log('JOIN_ARM_REFUSED save_tick='..r.tick)
       else
        C.armed={tick=r.tick,k=r.slot}
       end
      end
     elseif r.op=='handover' then
      C.handovers[r.slot]=r.tick
      o.log(string.format('JOIN_HANDOVER tick=%d k=%d at=%d',tick,r.slot,r.tick))
      o.handover(r.slot,r.tick)
     elseif r.op=='abort' then
      C.aborted[r.slot]=r.reason
      o.log(string.format('JOIN_ABORT tick=%d k=%d reason=%s',tick,r.slot,r.reason))
      if o.abort then o.abort(r.slot) end
     else
      o.log(string.format('JOIN_%s tick=%d from=%d k=%d save_tick=%d%s',r.op:upper(),tick,s,r.slot,r.tick,
       r.op=='saved' and (' bytes='..r.bytes..' package='..r.package..' portable='..r.portable) or (' reason='..r.reason)))
     end
    end
   end
  end
  return #receipt>0 and ('PJOIN_1|'..table.concat(receipt,',')) or nil
 end
 -- After the save tick: the gate's result becomes this client's SAVED or FAILED record (once).
 function C.proposal(tick,budget)
  if C.armed and tick>C.armed.tick then
   local text=o.result() or ''
   local state,t,k,bytes,package,portable,why=text:match('^(%a+)|(%d+)|(%d+)|(%d+)|(%x+)|(%x+)|(%d+)|')
   local a=C.armed;C.armed=nil
   if state=='done' and tonumber(t)==a.tick and tonumber(k)==a.k then
    C.saves=C.saves+1
    C.outbox={op='saved',slot=a.k,tick=a.tick,bytes=tonumber(bytes),package=package,portable=portable}
   else
    C.outbox={op='failed',slot=a.k,tick=a.tick,reason=K.REASONS[tonumber(why or 0) or 0] or 'save_failed'}
   end
   o.log('JOIN_RESULT '..text)
  end
  if not C.outbox then return nil end
  local hex,why=K.encode(C.outbox)
  assert(hex,'join record: '..tostring(why))
  if budget and #hex>budget then return nil end   -- the next tick has room (join is offered first)
  C.outbox=nil
  return hex
 end
 return C
end
-- Lua-side session state for a joiner (gate.join_lua / join_lua_state): providers registered by name return a
-- text without '\n' and restore from it. Text: one 'name=text' line per provider, sorted by name.
K.providers={}
function K.provide(name,save,restore)
 assert(type(name)=='string' and name:match('^[a-z][a-z0-9_]*$') and type(save)=='function' and type(restore)=='function','join state provider')
 K.providers[name]={save=save,restore=restore}
end
function K.state_text()
 local names={};for name in pairs(K.providers) do names[#names+1]=name end
 table.sort(names)
 local lines={}
 for _,name in ipairs(names) do
  local text=K.providers[name].save()
  assert(type(text)=='string' and not text:find('\n'),'join state '..name..' must be one line')
  lines[#lines+1]=name..'='..text
 end
 return table.concat(lines,'\n')
end
-- A joiner's unpaired samples after its admission (gate.join_unpaired: contexts and MD heap entries with every key part),
-- one 'JOIN_UNPAIRED <row>' line each; returns how many.
function K.log_unpaired(gate,log)
 if not gate.join_unpaired then return 0 end
 local n=0
 for i=1,40 do
  local row=gate.join_unpaired(i)
  if not row then break end
  log('JOIN_UNPAIRED '..row);n=n+1
 end
 return n
end
function K.restore_text(text)
 local seen={}
 for line in (text or ''):gmatch('[^\n]+') do
  local name,value=line:match('^([a-z][a-z0-9_]*)=(.*)$')
  assert(name and K.providers[name],'join state: unknown provider in '..tostring(line))
  K.providers[name].restore(value);seen[name]=true
 end
 for name in pairs(K.providers) do assert(seen[name],'join state: provider '..name..' missing from the package') end
end
