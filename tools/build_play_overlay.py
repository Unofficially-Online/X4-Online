"""Build a play overlay for people (x4ls play) from the owned snapshot and a gate DLL.

  python tools/build_play_overlay.py --snapshot SAVE --gate-dll DLL --out DIR [--players 2] [--mode coop] [--minutes 40]

1. Base overlay (tools/dev/build_native_gate_overlay.py --mode battle --session-clients): the snapshot boot and
   the session header; the session defaults (tools/dev/native_gate_defaults.lua) arm the whole people stack.
2. Players (tools/dev/native_gate_players.py overlay --source stock --defaults-only): the identity plan for
   --players slots in coop (one shared faction and wallet) or factions mode (a pool faction per player), every
   slot flown through X4's own controls, camera and HUD by its own client, the pilot window --minutes long.
3. tools/dev/check_gate_options.py: the probe's option logic accepts the result.

Then: python tools/x4ls.py play --overlay DIR --snapshot SAVE (docs/PLAYTEST.md).
"""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
TICK_HZ = 30          # the defaults' native_gate_tick_hz
WINDOW_START = 40     # the people window's first tick (native_gate_players.py)
TICK_MAX = 8388607    # pilot window bound (lockstep_pilot_inputs.h PILOT_TICK_MAX)
# X4's options and ship configuration menus pause through the Lua global Pause(); a save pauses too and ends the session on
# that client (x4ls --checkpoint-at). The Lua guard refuses these globals while the session runs (an info line on the first
# refusal); the menus then work unpaused. The pause test (native_gate_pause_test) shows what an unrefused Pause() does.
PLAY_LUA_BLOCK = ('Pause', 'Unpause', 'SaveGame')
# Play mode keeps the world hash in every digest (desyncs are still found) but writes no per-object world pages: a capture
# is about 120 MB of WORLD_OBJECT rows per client (smoke 8 Nov: 238 of 280 MB after 2 minutes). x4ls triage asks for
# pages of its window again (native_gate_world_pages_window).


def step(name, cmd):
    print(f'{name}: {" ".join(map(str, cmd))}', flush=True)
    result = subprocess.run([sys.executable, *map(str, cmd)], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.stdout.write(result.stdout[-4000:])
        sys.stderr.write(result.stderr[-4000:])
        raise SystemExit(f'{name} failed ({result.returncode})')
    return result.stdout


def window_end(minutes):
    end = WINDOW_START + round(minutes * 60 * TICK_HZ)
    if not WINDOW_START < end <= TICK_MAX:
        raise SystemExit(f'--minutes gives a window end of {end}; the bound is {TICK_MAX} ticks')
    return end


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--snapshot', type=Path, required=True, help='the owned snapshot 005 save (save_009.xml.gz)')
    p.add_argument('--gate-dll', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True, help='new overlay directory (its base goes to OUT.base)')
    p.add_argument('--players', type=int, default=2, help='2..4')
    p.add_argument('--mode', choices=('coop', 'factions'), default='coop')
    p.add_argument('--minutes', type=float, default=40, help='length of the pilot window (default 40)')
    p.add_argument('--full-logs', action='store_true', help='keep the per-object world pages (no play mode; ~120 MB per capture)')
    p.add_argument('--lua-block', default=','.join(PLAY_LUA_BLOCK), metavar='NAMES',
                   help='Lua globals the Lua guard refuses during the session (default: %(default)s; empty: none)')
    p.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE',
                   help='extra config key (repeatable), e.g. native_gate_own_predict=false')
    args = p.parse_args(argv)
    out = args.out.resolve()
    base = out.with_name(out.name + '.base')
    for path in (out, base):
        if path.exists():
            raise SystemExit(f'exists: {path} (overlays are frozen; pick a new --out)')
    end = window_end(args.minutes)
    step('base', [DEV / 'build_native_gate_overlay.py', '--mode', 'battle', '--gate-dll', args.gate_dll.resolve(),
                  '--snapshot', args.snapshot.resolve(), '--session-clients', '--out', base])
    cmd = [DEV / 'native_gate_players.py', 'overlay', '--base', base, '--out', out, '--gate-dll', args.gate_dll.resolve(),
           '--probe', DEV / 'native_gate_probe.lua', '--players', str(args.players), '--mode', args.mode, '--source', 'stock',
           '--defaults-only', '--set', f'native_gate_pilot_window={{{WINDOW_START},{end}}}']   # --defaults-only takes no --window
    if not args.full_logs:
        cmd += ['--set', 'native_gate_play_mode=true']
    block = [name for name in args.lua_block.split(',') if name.strip()]
    if block:
        cmd += ['--set', "native_gate_ui_lua='block'",
                '--set', 'native_gate_ui_lua_names={' + ','.join(f"'{name.strip()}'" for name in block) + '}']
    for item in args.set:
        cmd += ['--set', item]
    step('players', cmd)
    print(step('check', [DEV / 'check_gate_options.py', out / 'ui/config.lua.append']).strip()[-600:])
    print(f'play overlay: {out}\n  window {WINDOW_START}..{end} ({args.minutes:g} min at {TICK_HZ} Hz), {args.players} players, '
          f'{args.mode}\n  run: python tools/x4ls.py play --overlay "{out}" --snapshot "{args.snapshot.resolve()}"')
    return 0


if __name__ == '__main__':
    sys.exit(main())
