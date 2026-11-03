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
-- Payload -> list of {index=, args={...}} or nil,reason. kinds_of(index) gives the export's kind string
-- (nil: unknown export). Values: U and q as 16-digit hex, i as a signed number, b as a boolean, f and d as
-- hex of their bits (8 or 16 digits, converted by the caller), s as a string or false for NULL.
function K.decode(payload,kinds_of)
 if type(payload)~='string' or #payload<2 or #payload%2~=0 or payload:find('[^0-9a-f]') then return nil,'encoding' end
 if byte(payload,1)~=K.VERSION then return nil,'version' end
 local n,at,out=#payload/2,2,{}
 while at<=n do
  local length=byte(payload,at)
  if length<2 or at+length>n then return nil,'record_length' end
  local first,last=at+1,at+length
  local index=byte(payload,first)+256*byte(payload,first+1)
  local kinds=kinds_of(index)
  if not kinds then return nil,'unknown_export' end
  local p,args=first+2,{}
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
  out[#out+1]={index=index,args=args}
  at=last+1
 end
 return out
end
