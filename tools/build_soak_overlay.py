"""Build the x4ls soak overlay (the validated play profile) from a snapshot and a gate DLL.

  python tools/build_soak_overlay.py --snapshot SAVE --gate-dll DLL --out DIR [--window 36000] [--long]

1. Base overlay (build_native_gate_overlay.py): snapshot boot, 12-ship battle fixture,
   protocol 3, world identities, serial pathing and the fixture/RNG keying flags.
2. The simulation keys of src/lockstep/profiles/soak.lua (keys the Windows base builder
   does not take as flags); --long adds play mode (world hash kept in every digest,
   no per-object pages) and no real-time pacing, for multi-hour soaks.
3. Scripted pilots on two slots (native_gate_sandbox.py): slot 1 = the snapshot's player
   ship, slot 2 = the third fixture ship.
4. gate_preflight.py: the probe's option logic accepts the result.
Relay ticks for a full window: window end + 3.
"""
import argparse
import hashlib
import json
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
        run([DEV / 'build_native_gate_overlay.py', '--gate-dll', args.gate_dll, '--snapshot', args.snapshot, *BASE_FLAGS,
             '--world-hash-schema', args.world_schema, '--out', base])
        shutil.copytree(base, keyed)
        with (keyed / 'ui/config.lua.append').open('a') as config:
            profile = (ROOT / 'src/lockstep/profiles/soak.lua').read_text().splitlines(keepends=True)
            config.write(''.join(l for l in profile if not any(l.startswith(f'X4LAN_CONFIG.{k} ') for k in args.drop)))
            if args.long or args.play_mode:
                config.write(PLAY_MODE)
            if args.long or args.no_pace:
                config.write(NO_PACE)
        manifest(keyed)
        run([DEV / 'native_gate_sandbox.py', 'overlay', '--base', keyed, '--gate-dll', args.gate_dll,
             '--probe', DEV / 'native_gate_probe.lua', '--out', args.out, '--slot1', player, '--slot2', fixture[2],
             '--controls', args.controls, '--script-pilots', '--window', '30', str(args.window), '--world-stride', '300',
             '--trace-stride', '3600', '--fp', 'default', *[x for kv in args.set for x in ('--set', kv)]])
    print(run([DEV / 'gate_preflight.py', args.out]).strip())
    print(f"{args.out}: relay ticks for the full window: {args.window + (3 if args.controls == 'native' else 2)}")


if __name__ == '__main__':
    main()
