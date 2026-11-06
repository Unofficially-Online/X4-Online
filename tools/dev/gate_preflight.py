"""Offline preflight: run an overlay's probe arming under LuaJIT and report the first refused option.

The gate and X4's ffi are replaced by permissive fakes (every hook "accepts"), so what fails is the
probe's own option logic: incompatible or missing companion keys, wrong values. A pass does not
mean the real gate accepts every hook on this X4 build; it removes config mistakes before a launch.

  python tools/dev/gate_preflight.py <overlay> [--slot 1] [--frames 6]
"""
import argparse
from pathlib import Path
import sys

from lupa.luajit21 import LuaError, LuaRuntime

ROOT = Path(__file__).resolve().parents[2]

FAKES = r'''
logs={};loads=0;reports=0;is_menu=true
state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0',rng_schema=X4LAN_CONFIG.native_gate_rng_schema}
local accept=function() return 1 end
gate=setmetatable({
  boot=function() return 0 end, poll=function() return state end,
  load_requested=function() state.boot_phase=1;return 1 end,
  report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
  stop=function() state.active=0;return 1 end,
  sha256=function(s) return X4LAN_SHA256.digest(s) end,
}, {__index=function(_, name)
  if name:match('_status$') then return function() return 'fake' end end
  if name:match('_row$') then return function() return nil end end
  return accept end})
package.loadlib=function() return function() return gate end end
local C=setmetatable({
  GetSaveFolderPath=function() return 'fake/'..X4LAN_CONFIG.test_profile..'/save' end,
  GetCurrentGameTime=function() return 1e9 end, IsStartmenu=function() return is_menu end,
  IsSaveListLoadingComplete=function() return true end, GetCurrentUTCDataTime=function() return 1e12 end,
}, {__index=function() return function() return 0 end end})
local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return tostring(s) end,
  new=function() return {} end,cast=function(_,v) return v end,C=C}
require=function(name) if name=='ffi' then return ffi end if name=='bit' then return bit end error('require '..name) end
DebugError=function(s) logs[#logs+1]=s end
RegisterEvent=function() end;UnregisterEvent=function() end;AddUITriggeredEvent=function() end
SetScript=function(name,fn) if name=='onUpdate' then update=fn end end
GetSaveList=function() return {{filename='save_009'}} end
LoadGame=function() loads=loads+1 end
ConvertIDTo64Bit=function(v) return v end;ConvertStringTo64Bit=function(v) return v end
getElapsedTime=function() return 1e9 end
'''


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('overlay', type=Path)
    p.add_argument('--slot', type=int, default=1)
    p.add_argument('--clients', type=int, default=2)
    p.add_argument('--frames', type=int, default=6)
    args = p.parse_args(argv)
    config = (args.overlay / 'ui/config.lua.append').read_text()
    for placeholder, value in (('@X4LANE_PORT@', '50000'), ('@X4LANE_TOKEN@', 'a' * 32),
                               ('@X4LANE_SLOT@', f'p{args.slot}'), ('@X4LANE_CLIENTS@', str(args.clients))):
        config = config.replace(placeholder, value)
    files = [line.strip() for line in (args.overlay / 'ui.xml.files').read_text().splitlines() if line.strip()]
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute((ROOT / 'src/lockstep/base/ui/sha256.lua').read_text())
    lua.execute('X4LAN_CONFIG = {enabled = false}\n' + config)
    lua.execute(FAKES)
    try:
        for name in files:
            lua.execute((args.overlay / name).read_text(encoding='utf-8'))
        if lua.globals().update is None:
            raise SystemExit('no onUpdate handler registered')
        # Arming and every option check happen in the start menu, up to the snapshot load request;
        # later frames depend on the real engine and are out of scope.
        for frame in range(args.frames):
            lua.execute('update()')
            if lua.globals().loads:
                break
    except LuaError as error:
        print('REFUSED (Lua error):', str(error).splitlines()[0])
        return 1
    errors = [line for line in lua.globals().logs.values() if ' ERROR ' in line or 'REFUSED' in line]
    if errors:
        print('REFUSED:', errors[0])
        return 1
    armed = [line for line in lua.globals().logs.values() if line.endswith('_ARMED') or '_ARMED ' in line or ' ARMED ' in line]
    print(f'preflight passed: {len(armed)} ARMED lines, load requested={bool(lua.globals().loads)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
