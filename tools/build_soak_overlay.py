"""Build the x4ls soak overlay (the validated play profile) from a snapshot and a gate DLL.

  python tools/build_soak_overlay.py --snapshot SAVE --gate-dll DLL --out DIR [--window 36000] [--long]

1. Base overlay (build_native_gate_overlay.py): snapshot boot, 12-ship battle fixture,
   protocol 3, world identities, serial pathing and the fixture/RNG keying flags.
2. The simulation keys of src/lockstep/profiles/soak.lua (keys the Windows base builder
   does not take as flags); --long adds play mode (world hash kept in every digest,
   no per-object pages) and no real-time pacing, for multi-hour soaks.
3. Scripted pilots on two slots (native_gate_sandbox.py): slot 1 = the snapshot's player
   ship, slot 2 = the third fixture ship. With --players factions|coop instead: two player
   identities (native_gate_players.py: own pool faction per slot, or one shared faction),
   script pilots and scripted orders through the relay (--orders demo), for player-activity
   soaks.
   (sandbox:) slot 1 = the snapshot's player
   ship, slot 2 = the third fixture ship.
4. gate_preflight.py: the probe's option logic accepts the result.
Relay ticks for a full window: window end + 3.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
sys.path.insert(0, str(DEV))
from build_native_gate_overlay import snapshot_fixture  # noqa: E402
from native_gate_scenarios import snapshot_header  # noqa: E402

BASE_FLAGS = ['--mode', 'battle', '--snapshot-prebind', '--session-clients', '--serial-pathing', '--world-identities',
              '--world-hash-stride', '300', '--path-determinism', '3', '--event-order', '--md-resume-rng', '--boot-hold',
              '--ai-wait-rng', '--region-rng', 'key', '--actor-diag', '--flight-rng', '--steering-init', '--radar-rng',
              '--radar-initial-refresh', '--interrupt-diag', '--agent-rng', '--agent-full', '--spline-diag', '--spline-join', '--ship-rng',
              '--weapon-rng', '--nav-rng', '--nav-initial-refresh', '--bullet-rng', '--attention-rng',
              '--attention-initial-refresh', '--shape-rng', '--shape-initial-refresh', '--death-rng', '--strict-state',
              '--strict-physics', '--shield-refresh']  # no --ai-rng/--interrupt-rng: script_rng 'key' replaces them
PLAY_MODE = 'X4LAN_CONFIG.native_gate_play_mode = true\n'  # world hash kept in every digest, no per-object pages
NO_PACE = 'X4LAN_CONFIG.native_gate_tick_pace = false\n'  # clients run as fast as they can


# Exports the relay code already carries (UI replay, player orders): never blocked by --ui-guard.
RELAYED = {'AdjustOrder', 'CreateOrder', 'CreateOrder3', 'EnableOrder', 'EnablePlannedDefaultOrder', 'EndGuidance', 'QuickDock',
           'RemoveAllOrders', 'RemoveAllOrders2', 'RemoveOrder', 'RemoveOrder2', 'RemovePlannedDefaultOrder', 'RequestDockAt',
           'RequestDockAtReason', 'ResetOrder', 'ResetOrderLoop', 'SetCurrentLoopOrder', 'SetDefensibleActiveWeaponGroup',
           'SetGuidance', 'SetOrderLoop', 'SetSofttarget', 'SetWeaponGroup', 'SetWeaponMode', 'StartPlayerActivity',
           'StopPlayerActivity', 'ToggleAutoPilot', 'ToggleFlightAssist', 'UndockPlayerShip'}


# Lua globals the UI calls with plain arguments, relayed with --ui-relay: 'c' marks a place where the UI passes
# a component as a plain number (TransferPlayerMoneyTo(amount, container)); a LuaID or a uint64 cdata is a
# component wherever it appears, strings, booleans and nil carry themselves. From the call sites in the 9.00 UI
# scripts. Left blocked: AddTradeToShipQueue and RaisePlayerInteractionEvent (trade offer and interaction IDs
# are not components), StartDiplomacyActionOperation (a table argument).
LUA_RELAY_KINDS = {'AddAmmo': 'c', 'AddCargo': 'c', 'AddInventory': 'c', 'ClearContainerWarePriceOverride': 'c',
                   'CopyDefaultOrderParamsForPlanning': 'c', 'RemoveAmmo': 'c', 'RemoveCargo': 'c',
                   'RemoveInventory': 'c', 'RemoveOrderListParam': 'c', 'SetContainerWarePriceOverride': 'c',
                   'SetMaxBudget': 'c', 'SetMinBudget': 'c', 'SetNPCBlackboard': 'c', 'SetOrderParam': 'c',
                   'SetVirtualCargoMode': 'c', 'TransferInventoryToPlayer': '--c', 'TransferMoneyToPlayer': '-c',
                   'TransferPlayerInventoryTo': '--c', 'TransferPlayerMoneyTo': '-c'}


def guard_config(census, relay=False):
    """The ui_exports block configuration: game writes the UI calls, minus presentation and RELAYED. With relay,
    the writes whose arguments have relay kinds (scalars, IDs, strings) go through the generic FFI relay instead
    of being refused; the rest (struct arguments) stay blocked. The Lua globals of the UI globals census are
    blocked likewise, and with relay the ones in LUA_RELAY_KINDS are relayed (native_gate_ui_lua_relay)."""
    table = json.loads(Path(census).read_text())
    names = sorted(n for n, v in table.items() if v['scripts'] and v['family'] != 'presentation' and n not in RELAYED)
    relayed = {n: table[n]['kinds'] for n in names  # the gate takes at most 12 arguments (UX_KINDS)
               if relay and table[n].get('kinds') is not None and len(table[n]['kinds']) <= 12}
    listed = ','.join(f"'{n}'" for n in names if n not in relayed)
    text = f"X4LAN_CONFIG.native_gate_ui_exports = 'block'\nX4LAN_CONFIG.native_gate_ui_exports_block = {{{listed}}}\n"
    if relayed:
        pairs = ','.join(f"{n}='{k}'" for n, k in sorted(relayed.items()))
        text += f"X4LAN_CONFIG.native_gate_ui_relay = {{{pairs}}}\n"
    # The Lua layer: C functions registered as Lua globals (the UI globals census, .local/ui-lua-globals.json),
    # non-query names of a game family, are blocked too (native_gate_ui_lua).
    lua_list = Path(census).with_name('ui-lua-globals.json')
    if lua_list.is_file():
        sys.path.insert(0, str(DEV))
        from ui_export_census import QUERY, family
        ui_only = re.compile(r'^Notify|^Create(?!Order)|Descriptor|DropDown|Bar$|^Interrupt|^Select|SetSelected|^Round|'
                             r'PlayCredits|SwitchInteractive')  # listeners, widgets, pure helpers
        lua = sorted(n for n in json.loads(lua_list.read_text())
                     if not QUERY.match(n) and family(n) not in ('presentation', 'other') and not ui_only.search(n))
        lua_relayed = {n: LUA_RELAY_KINDS[n] for n in lua if relay and n in LUA_RELAY_KINDS}
        blocked = [n for n in lua if n not in lua_relayed]
        if lua:
            text += "X4LAN_CONFIG.native_gate_ui_lua = 'block'\nX4LAN_CONFIG.native_gate_ui_lua_names = {%s}\n" % ','.join(f"'{n}'" for n in blocked)
        if lua_relayed:
            text += "X4LAN_CONFIG.native_gate_ui_lua_relay = {%s}\n" % ','.join(f"{n}='{k}'" for n, k in sorted(lua_relayed.items()))
    return text


def manifest(path):
    files = {p.relative_to(path).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in path.rglob('*') if p.is_file() and p.name != 'x4lane-manifest.json'}
    (path / 'x4lane-manifest.json').write_text(json.dumps(files, indent=2) + '\n')


def run(cmd):
    result = subprocess.run([sys.executable, *map(str, cmd)], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit((result.stderr or result.stdout).strip().splitlines()[-1])
    return result.stdout


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--snapshot', type=Path, required=True)
    p.add_argument('--gate-dll', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--window', type=int, default=36000, help='last scripted pilot tick')
    p.add_argument('--long', action='store_true', help='--play-mode and --no-pace (multi-hour soaks)')
    p.add_argument('--play-mode', action='store_true', help='keep the world hash in every digest but skip per-object pages')
    p.add_argument('--no-pace', action='store_true', help='do not hold clients to real time')
    p.add_argument('--controls', choices=('device', 'native'), default='device',
                   help="device: X4's own controls parked on every client, ships flown only by the shared inputs")
    p.add_argument('--ui-guard', type=Path, nargs='?', const=ROOT / '.local/ui-export-census.json', metavar='CENSUS',
                   help="native_gate_ui_exports = 'block' for every game write the UI calls that the relay does not "
                        "cover (from tools/dev/ui_export_census.py's map; RELAYED lists the covered exports)")
    p.add_argument('--ui-relay', type=Path, nargs='?', const=ROOT / '.local/ui-export-census.json', metavar='CENSUS',
                   help='like --ui-guard, but the game writes with scalar, ID or string arguments go through the generic '
                        'FFI relay (native_gate_ui_relay, every client runs them at one tick); the rest stay blocked')
    p.add_argument('--defaults', action='store_true',
                   help='the session default stack (tools/dev/native_gate_defaults.lua) on top of the soak profile; without it the '
                        'soak overlay is bare: only the profile and these flags')
    p.add_argument('--players', choices=('factions', 'coop'),
                   help='player-activity soak: player identities, script pilots and scripted orders (native_gate_players.py)')
    p.add_argument('--players-arg', action='append', default=[], metavar='ARG',
                   help='extra native_gate_players.py overlay argument (repeatable)')
    p.add_argument('--sandbox-arg', action='append', default=[], metavar='ARG',
                   help='extra native_gate_sandbox.py argument (repeatable), e.g. --sandbox-arg=--device1=none')
    p.add_argument('--drop', action='append', default=[], metavar='native_gate_KEY',
                   help='leave a key of profiles/soak.lua out (repeatable)')
    p.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE',
                   help='extra key for the sandbox builder (repeatable), e.g. native_gate_trace_window={4650,4720}')
    p.add_argument('--world-schema', choices=('WORLD_IMPORTED_1', 'WORLD_IMPORTED_2'), default='WORLD_IMPORTED_2',
                   help='2 encodes Nav timing canonically (the pathing bank index is equivalent state, census finding 9)')
    args = p.parse_args(argv)
    if args.out.exists():
        raise SystemExit(f'{args.out} exists; overlays are frozen')
    player = snapshot_header(args.snapshot)['player']['ship']
    fixture = snapshot_fixture(args.snapshot)
    with tempfile.TemporaryDirectory(dir=args.out.parent) as tmp:
        base, keyed = Path(tmp) / 'base', Path(tmp) / 'keyed'
        # The soak profile is the whole validated key set: the session default stack (tools/dev/native_gate_defaults.lua)
        # stays off (--bare) unless --defaults asks for it on top, so a soak runs exactly the profile.
        run([DEV / 'build_native_gate_overlay.py', '--gate-dll', args.gate_dll, '--snapshot', args.snapshot, *BASE_FLAGS,
             '--world-hash-schema', args.world_schema, '--out', base, *([] if args.defaults else ['--bare'])])
        shutil.copytree(base, keyed)
        with (keyed / 'ui/config.lua.append').open('a') as config:
            profile = (ROOT / 'src/lockstep/profiles/soak.lua').read_text().splitlines(keepends=True)
            config.write(''.join(l for l in profile if not any(l.startswith(f'X4LAN_CONFIG.{k} ') for k in args.drop)))
            if args.long or args.play_mode:
                config.write(PLAY_MODE)
            if args.long or args.no_pace:
                config.write(NO_PACE)
            if args.ui_guard or args.ui_relay:
                config.write(guard_config(args.ui_relay or args.ui_guard, relay=bool(args.ui_relay)))
        manifest(keyed)
        if args.players:
            run([DEV / 'native_gate_players.py', 'overlay', '--base', keyed, '--gate-dll', args.gate_dll,
                 '--probe', DEV / 'native_gate_probe.lua', '--out', args.out, '--players', '2', '--mode', args.players,
                 '--source', 'script', '--orders', 'demo', '--ships', f'{player},{fixture[2]}', '--fixture', ','.join(fixture), '--occupied-ship', player,
                 '--window', '30', str(args.window), '--world-stride', '300',
                 '--trace-stride', '3600', '--fp', 'default', *[x for kv in args.set for x in ('--set', kv)], *args.players_arg])
        else:
            run([DEV / 'native_gate_sandbox.py', 'overlay', '--base', keyed, '--gate-dll', args.gate_dll,
                 '--probe', DEV / 'native_gate_probe.lua', '--out', args.out, '--slot1', player, '--slot2', fixture[2],
                 '--controls', args.controls, '--script-pilots', '--window', '30', str(args.window), '--world-stride', '300',
                 '--trace-stride', '3600', '--fp', 'default', *[x for kv in args.set for x in ('--set', kv)], *args.sandbox_arg])
    print(run([DEV / 'gate_preflight.py', args.out]).strip())
    # Globals the probe reads that X4's UI Lua does not have (preflight stops at the load request, so a
    # function reading a name before its local declaration only fails in a native run).
    if (ROOT / '.local/ui-lua-globals-all.json').is_file():
        ui = args.out / 'ui'
        print(run([DEV / 'probe_globals_check.py', *[ui / n for n in ('native_gate_probe.lua', 'native_gate_inputs_core.lua',
                                                                      'native_gate_ffi_relay_core.lua') if (ui / n).is_file()]]).strip())
    # The arguments beside the overlay (not in it: overlays are frozen by their manifest), for x4ls triage.
    given = sys.argv[1:] if argv is None else list(argv)
    rebuild = [a for i, a in enumerate(given) if a != '--out' and (i == 0 or given[i - 1] != '--out') and not a.startswith('--out=')]
    Path(f'{args.out}.build.json').write_text(json.dumps({'argv': rebuild}, indent=2) + '\n')
    print(f"{args.out}: relay ticks for the full window: {args.window + (3 if args.controls == 'native' else 2)}")


if __name__ == '__main__':
    main()
