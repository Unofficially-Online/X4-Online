-- Relayed UI FFI calls (lockstep_ui_exports.h): codec of the input section 'ffi' (kind 0x58). Pure Lua,
-- no engine calls (LuaJIT-tested through tests/test_native_gate_ffi_relay.py).
-- Payload: version 1, then records, each: length u8 | export index u16 le | the arguments in the order of
-- the export's kind string: U portable key, 8 bytes le (0: none); q 8 bytes; i 4; b 1; f 4; d 8 (little
-- endian); s length u8 (0xff: NULL) then the bytes. The gate captures records in this layout
-- (gate.ui_exports_take returns index and arguments; pack() adds the length byte).
X4LAN_FFI_RELAY_CORE={}
local K=X4LAN_FFI_RELAY_CORE
K.VERSION=1
K.CTYPE={U='uint64_t',q='int64_t',i='int32_t',b='bool',f='float',d='double',s='const char*'}
local SIZE={U=8,q=8,i=4,b=1,f=4,d=8}
local function byte(h,i) return tonumber(h:sub(2*i-1,2*i),16) end
-- The C function pointer type for a kind string (the return value is not used).
function K.signature(kinds)
 assert(type(kinds)=='string' and kinds:match('^[Uqibfds]*$'),'invalid relay kinds '..tostring(kinds))
 local args={}
 for k in kinds:gmatch('.') do args[#args+1]=K.CTYPE[k] end
 return 'void(*)('..(#args>0 and table.concat(args,',') or 'void')..')'
end
-- Records (hex, index and arguments) -> payload hex within budget_hex characters, and the count taken.
-- Records that do not fit stay for a later tick (the caller keeps the rest of its list).
function K.pack(records,budget_hex)
 local parts,used,taken={string.format('%02x',K.VERSION)},2,0
 for _,record in ipairs(records) do
  local n=#record/2
  assert(#record%2==0 and n>=2 and n<=255,'invalid relay record')
  if used+2+#record>budget_hex then break end
  parts[#parts+1]=string.format('%02x',n)..record;used=used+2+#record;taken=taken+1
 end
 if taken==0 then return nil,0 end
 return table.concat(parts),taken
end
-- Little-endian bytes of a hex string (count bytes from byte at) as big-endian hex digits.
local function le(hex,at,count)
 local out={}
 for i=count-1,0,-1 do out[#out+1]=hex:sub(2*(at+i)-1,2*(at+i)) end
 return table.concat(out)
end
-- Lua-global calls (C functions the UI calls as Lua globals: TransferPlayerMoneyTo, AddCargo, ...) ride the
-- same section as records with export index 0xffff: the global's name (length u8, bytes), the argument count
-- (u8), then each argument as a tag byte and its value: 0 nil, 1 false, 2 true, 3 number (length u8, then
-- '%.17g' text: exact for doubles), 4 string (length u8, bytes), 5/6/7 a component as a LuaID, a plain number
-- or a uint64 cdata (portable key, 8 bytes le). A Lua number is a component only where the global's kind
-- string has 'c' (TransferPlayerMoneyTo(fee, container): '-c'); a LuaID userdata or a cdata is one anywhere.
K.LUA_INDEX=0xffff
local FORM={[5]='luaid',[6]='number',[7]='cdata'}
local TAG={luaid=5,number=6,cdata=7}
local function hex_text(text) return (text:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
local function key_le(key) local out={};for i=15,1,-2 do out[#out+1]=key:sub(i,i+1) end;return table.concat(out) end
-- A call -> record hex (without the length byte), or nil and why. component(value, form) gives the portable
-- key (16 hex) of an object argument or nil (form: 'luaid', 'number', 'cdata'); a number 0 in a 'c' place
-- (no object) is key 0.
function K.lua_record(name,args,n,kinds,component)
 assert(type(name)=='string' and #name>=1 and #name<=64,'invalid global name')
 if n>32 then return nil,'too_many_arguments' end
 local parts={'ffff',string.format('%02x',#name),hex_text(name),string.format('%02x',n)}
 for i=1,n do
  local v,k,t=args[i],(kinds or ''):sub(i,i),type(args[i])
  local form=t=='userdata' and 'luaid' or t=='cdata' and 'cdata' or (t=='number' and k=='c') and 'number' or nil
  if form then
   local key=(form=='number' and v==0) and string.rep('0',16) or component(v,form)
   if not key then return nil,'unportable_object' end
   parts[#parts+1]=string.format('%02x',TAG[form])..key_le(key)
  elseif v==nil then parts[#parts+1]='00'
  elseif t=='boolean' then parts[#parts+1]=v and '02' or '01'
  elseif t=='number' then
   if v~=v or v==math.huge or v==-math.huge then return nil,'unportable_number' end
   local text=string.format('%.17g',v)
   parts[#parts+1]='03'..string.format('%02x',#text)..hex_text(text)
  elseif t=='string' then
   if #v>200 then return nil,'too_long' end
   parts[#parts+1]='04'..string.format('%02x',#v)..hex_text(v)
  else return nil,'unportable_'..t end
 end
 local record=table.concat(parts)
 if #record>2*255 then return nil,'too_long' end
 return record
end
local function text_at(payload,p,len)
 local chars={}
 for i=0,len-1 do chars[#chars+1]=string.char(byte(payload,p+i)) end
 return table.concat(chars)
end
-- One Lua-global record's body (bytes first..last after the index) -> {index=0xffff, name=, n=, args=} or nil,
-- reason. Components decode as {form=, key=} (key as 16 big-endian hex digits).
local function decode_lua(payload,first,last)
 local p=first
 if p>last then return nil,'truncated' end
 local len=byte(payload,p);p=p+1
 if len<1 or p+len-1>last then return nil,'truncated' end
 local name=text_at(payload,p,len);p=p+len
 if p>last then return nil,'truncated' end
 local n=byte(payload,p);p=p+1
 local args={}
 for i=1,n do
  if p>last then return nil,'truncated' end
  local tag=byte(payload,p);p=p+1
  if tag==0 then args[i]=nil
  elseif tag==1 or tag==2 then args[i]=tag==2
  elseif tag==3 or tag==4 then
   if p>last then return nil,'truncated' end
   local size=byte(payload,p);p=p+1
   if p+size-1>last then return nil,'truncated' end
   local text=text_at(payload,p,size);p=p+size
   if tag==3 then args[i]=tonumber(text);if args[i]==nil then return nil,'number' end else args[i]=text end
  elseif FORM[tag] then
   if p+7>last then return nil,'truncated' end
   args[i]={form=FORM[tag],key=le(payload,p,8)};p=p+8
  else return nil,'tag' end
 end
 if p~=last+1 then return nil,'record_size' end
 return {index=K.LUA_INDEX,name=name,n=n,args=args}
end
-- One export record's arguments (bytes first..last after the index) by its kind string.
local function decode_export(payload,first,last,kinds)
 local p,args=first,{}
 for k in kinds:gmatch('.') do
  if k=='s' then
   if p>last then return nil,'truncated' end
   local len=byte(payload,p);p=p+1
   if len==0xff then args[#args+1]=false
   else
    if p+len-1>last then return nil,'truncated' end
    local chars={}
    for i=0,len-1 do chars[#chars+1]=string.char(byte(payload,p+i)) end
    args[#args+1]=table.concat(chars);p=p+len
   end
  else
   local size=SIZE[k]
   if p+size-1>last then return nil,'truncated' end
   if k=='b' then args[#args+1]=byte(payload,p)~=0
   elseif k=='i' then
    local v=byte(payload,p)+256*byte(payload,p+1)+65536*byte(payload,p+2)+16777216*byte(payload,p+3)
    args[#args+1]=v>=2147483648 and v-4294967296 or v
   elseif k=='f' then args[#args+1]=le(payload,p,4)
   else args[#args+1]=le(payload,p,8) end
   p=p+size
  end
 end
 if p~=last+1 then return nil,'record_size' end
 return args
end
-- Payload -> list of {index=, args={...}} or nil,reason. kinds_of(index) gives the export's kind string
-- (nil: unknown export). Values: U and q as 16-digit hex, i as a signed number, b as a boolean, f and d as
-- hex of their bits (8 or 16 digits, converted by the caller), s as a string or false for NULL. Index 0xffff
-- is a Lua-global record (decode_lua); kinds_of is not asked for it.
function K.decode(payload,kinds_of)
 if type(payload)~='string' or #payload<2 or #payload%2~=0 or payload:find('[^0-9a-f]') then return nil,'encoding' end
 if byte(payload,1)~=K.VERSION then return nil,'version' end
 local n,at,out=#payload/2,2,{}
 while at<=n do
  local length=byte(payload,at)
  if length<2 or at+length>n then return nil,'record_length' end
  local first,last=at+1,at+length
  local index=byte(payload,first)+256*byte(payload,first+1)
  local r,why
  if index==K.LUA_INDEX then r,why=decode_lua(payload,first+2,last)
  else
   local kinds=kinds_of(index)
   if not kinds then return nil,'unknown_export' end
   local args;args,why=decode_export(payload,first+2,last,kinds)
   r=args and {index=index,args=args}
  end
  if not r then return nil,why end
  out[#out+1]=r
  at=last+1
 end
 return out
end
