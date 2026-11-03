"""Static check of the probe: every global it reads must exist in X4's UI Lua or be set by the probe itself.

  python tools/dev/probe_globals_check.py [--globals .local/ui-lua-globals.json] [files ...]

The gate preflight runs the probe only up to the load request, so a function that reads a name before
its `local` declaration (it then compiles as a global and reads nil), or a library X4's UI Lua does not
have (`debug`), only fails in a native run. This loads each file with LuaJIT (lupa), walks the bytecode
of every function (jit.util) and lists GGET names that are neither set by one of the files (GSET), nor
in X4's UI globals (the probe's UI_GLOBALS census, .local/ui-lua-globals.json, or --globals), nor in
the base library X4 provides. Exit 1 when a name is left.
"""
import argparse
import json
from pathlib import Path
import sys

from lupa.luajit21 import LuaRuntime  # jit.util: the bytecode walker needs LuaJIT

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FILES = ['tools/dev/native_gate_probe.lua', 'tools/dev/native_gate_inputs_core.lua', 'tools/dev/native_gate_ffi_relay_core.lua']
# Base library names present in X4's UI Lua (the census lists functions; these are tables and values).
BASE = {'string', 'table', 'math', 'bit', 'coroutine', 'jit', '_G', '_VERSION', 'pairs', 'ipairs', 'next', 'type', 'tostring',
        'tonumber', 'pcall', 'xpcall', 'error', 'assert', 'select', 'unpack', 'rawget', 'rawset', 'rawequal', 'setmetatable',
        'getmetatable', 'require', 'print', 'collectgarbage', 'loadstring', 'load', 'setfenv', 'getfenv', 'package',
        'Menus'}  # the UI's menu table (not a function, so not in the census)
# Names X4's UI Lua lacks that the probe may only read behind a type check.
OPTIONAL = {'debug'}

WALK = r'''
local util=require('jit.util')
-- The opcode of GGET and GSET, found from tiny functions (no opcode tables needed).
local function op_naming(f,name)
 for pc=1,8 do
  local ins=util.funcbc(f,pc);if not ins then break end
  local d=math.floor(ins/65536)%65536
  if util.funck(f,-d-1)==name then return ins%256 end
 end
end
local GGET=op_naming(function() return probe_check_name end,'probe_check_name')
local GSET=op_naming(function() probe_check_name=1 end,'probe_check_name')
return function(chunk)
 local gets,sets={},{}
 local function walk(f)
  local info=util.funcinfo(f)
  for pc=1,info.bytecodes-1 do
   local ins,m=util.funcbc(f,pc)
   if not ins then break end
   local op=ins%256
   if op==GGET or op==GSET then
    local d=math.floor(ins/65536)%65536  -- operand D: bits 16..31
    local name=util.funck(f,-d-1)
    if type(name)=='string' then
     if op==GGET then gets[name]=gets[name] or info.currentline or info.linedefined
     else sets[name]=true end
    end
   end
  end
  for i=0,info.nconsts-1 do local k=util.funck(f,i) end
  local i=-1
  while true do
   local k=util.funck(f,i)
   if k==nil then break end
   if type(k)=='proto' or type(k)=='function' then walk(k) end
   i=i-1
  end
 end
 walk(chunk)
 return gets,sets
end
'''


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('files', nargs='*', default=DEFAULT_FILES)
    p.add_argument('--globals', type=Path, default=ROOT / '.local/ui-lua-globals-all.json',
                   help="every global function of X4's UI Lua (the UI_GLOBALS census, all=1)")
    args = p.parse_args(argv)
    lua = LuaRuntime(unpack_returned_tuples=True)
    walk = lua.execute(WALK)
    gets, sets = {}, set()
    for name in args.files:
        path = ROOT / name if not Path(name).is_absolute() else Path(name)
        chunk = lua.eval('function(src, name) local f, err = loadstring(src, name); if not f then error(err) end; return f end')(
            path.read_text(encoding='utf-8'), '@' + path.name)
        g, s = walk(chunk)
        for k, v in g.items():
            gets.setdefault(k, (path.name, v))
        sets |= set(s.keys())
    known = set(BASE) | sets
    if args.globals.is_file():
        known |= set(json.loads(args.globals.read_text()))
    else:
        print(f'note: {args.globals} missing (run a session with native_gate_ui_exports to census the UI globals)')
    unknown = sorted((k, v) for k, v in gets.items() if k not in known and k not in OPTIONAL and not k.startswith('X4LAN'))
    for name, (file, line) in unknown:
        print(f'{file}: global {name} read, never set and not an X4 UI global')
    print(f'{len(gets)} globals read, {len(unknown)} unknown')
    return 1 if unknown else 0


if __name__ == '__main__':
    sys.exit(main())
