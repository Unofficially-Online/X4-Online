"""Build a play overlay for people (x4ls play) from the owned snapshot and a gate DLL.

  python tools/build_play_overlay.py --snapshot SAVE --gate-dll DLL --out DIR [--players 2] [--mode coop] [--minutes 40]

1. Base overlay (tools/dev/build_native_gate_overlay.py --mode battle --session-clients): the snapshot boot and
   the session header; the session defaults (tools/dev/native_gate_defaults.lua) arm the whole people stack.
2. Players (tools/dev/native_gate_players.py overlay --source stock --defaults-only): the identity plan for
   --players slots in coop (one shared faction and wallet) or factions mode (a pool faction per player), every
   slot flown through X4's own controls, camera and HUD by its own client, the pilot window --minutes long.
3. tools/dev/check_gate_options.py: the probe's option logic accepts the result.

Then: python tools/x4ls.py play --overlay DIR --snapshot SAVE (docs/PLAYTEST.md).

Hot join (--open K[,K...]): slots K start absent and a player can join one of them while the session runs (x4ls play
--open K, x4ls join without --arm-at). The base gets the join stack (--world-identities, --join report|strict,
--join-absent K) and the players the join keys every joiner needs (native_gate_join_precise, _refresh_builds, _ids and
JOIN_DEFAULT_ON, as tools/dev/native_gate_join_overlays.py gives savers). The joiner's overlay is the same recipe with the
join save as --snapshot and the saver's package (--join-package, --join-package-sha); it is checked as
native_gate_join_overlays.py joiner checks one (the world table, every saved ID of the config, the fixture views, the
probe's Lua state). OUT/play-recipe.json records the recipe; x4ls join rebuilds the joiner's overlay from it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
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


RECIPE, RECIPE_KIND, RECIPE_VERSION = 'play-recipe.json', 'x4ls-play-recipe', 1
# A session with Windows and Linux PCs (--mixed-platform, on every PC's overlay or bundle): Option B's strict float
# recomputes, so both platforms write the flight parameters, the solver's and the drones' words in one order
# (W36g against lx38a, 8 Nov: Windows with these = Linux with these at tick 14; without them Windows keeps MSVC's
# native bits and a mixed session parts at the first steering update). Not in the defaults until the people stack
# runs on the Linux gate: a Windows-only session does not need them.
# Linux-only, logged no-ops on Windows (x4mods-e7, 9 Nov; lx54a/lx55a equal to W36g ticks 1..30 with them): the skeleton
# joints' wrapped target in MSVC's order (ik_wrap: the tick-15 gun gimbal targets, then the tick-28 bullet hits), a ship's
# second body with Windows' creation-time mass properties (body1_mass), and the joints' sub-step in MSVC's order (ik_step:
# not reached by the battle fixture's fighters, Windows' own arithmetic where turrets reach it).
MIXED_PLATFORM_SETS = ("native_gate_fp_redirect='redirect'", 'native_gate_fp_redirect_ticks=3',
                       "native_gate_fp_redirect_solver='redirect'", "native_gate_fp_redirect_drones='redirect'",
                       'native_gate_ik_wrap=true', "native_gate_body1_mass='fallback'", 'native_gate_ik_step=true')
NL = chr(10)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def open_slots(text, players):
    """--open K[,K...]: sorted slots 2..players, leaving slot 1 and at least one more to play from the start."""
    parts = [v.strip() for v in text.split(',') if v.strip()]
    if not all(v.isdigit() for v in parts):
        raise SystemExit('--open takes slot numbers K[,K...]')
    opened = sorted({int(v) for v in parts})
    if opened and (min(opened) < 2 or max(opened) > players or len(opened) > players - 1):
        raise SystemExit(f'--open: slots 2..{players} (slot 1 plays from the start)')
    return opened


def read_recipe(overlay):
    """The recipe a play overlay was built with (OUT/play-recipe.json), checked field by field; None for an older overlay."""
    path = Path(overlay) / RECIPE
    if not path.is_file():
        return None
    data = json.loads(path.read_text(encoding='utf-8'))
    ok = (isinstance(data, dict) and data.get('kind') == RECIPE_KIND and data.get('version') == RECIPE_VERSION and
          type(data.get('players')) is int and 2 <= data['players'] <= 8 and data.get('mode') in ('coop', 'factions') and
          isinstance(data.get('minutes'), (int, float)) and isinstance(data.get('set'), list) and
          all(isinstance(s, str) for s in data['set']) and isinstance(data.get('open'), list) and
          all(type(k) is int and 2 <= k <= data['players'] for k in data['open']) and data.get('join_mode') in ('report', 'strict')
          and isinstance(data.get('lua_block'), str) and isinstance(data.get('full_logs'), bool) and
          isinstance(data.get('gate_dll_sha'), str))
    if not ok:
        raise SystemExit(f'{path}: not a play recipe')
    return data


def joiner_command(recipe, snapshot, gate_dll, out, package, package_sha, session=None):
    """The build_play_overlay.py arguments of a joiner's overlay: the session's recipe with the join save and package
    (session: the recipe's session.json written on the joiner's PC, when the session has one)."""
    cmd = ['--snapshot', str(snapshot), '--gate-dll', str(gate_dll), '--out', str(out), '--players', str(recipe['players']),
           '--mode', recipe['mode'], '--minutes', repr(recipe['minutes']), '--lua-block', recipe['lua_block'],
           '--open', ','.join(map(str, recipe['open'])), '--join-mode', recipe['join_mode'],
           '--join-package', str(package), '--join-package-sha', package_sha]
    if recipe['full_logs']:
        cmd.append('--full-logs')
    if recipe.get('mixed_platform'):
        cmd.append('--mixed-platform')
    if session:
        cmd += ['--session', str(session)]
    for item in recipe['set']:
        cmd += ['--set', item]
    return cmd


def bind_session_save(base, snapshot, session_data):
    """--session: the base overlay bound to the session save itself, its checksum, its fixture (the twelve ships the capture
    spawned) and the census watch list (native_gate_session_start.py rebase). The battle base binds snapshot 005's fixture by
    default, so a session from another start stopped at its first frame (ns1, 8 Nov: stop 74, SNAPSHOT_BOUNDARY bind_actor)."""
    sys.path.insert(0, str(DEV))
    import native_gate_scenarios as scenarios
    lines, header = scenarios.snapshot_lines(snapshot)
    if header['start'] != session_data.get('base_start'):
        raise SystemExit(f"--session: the save's start {header['start']} is not the session's {session_data.get('base_start')}")
    fixture = re.findall(r"'([0-9a-f]{16})'", next(line for line in lines if '.native_gate_snapshot_fixture ' in line))
    if fixture != session_data.get('fixture'):
        raise SystemExit("--session: the save's fixture is not the registered one (session.json fixture)")
    config = base / 'ui/config.lua.append'
    keys = tuple(f'X4LAN_CONFIG.{key} ' for key in scenarios.SNAPSHOT_KEYS)
    kept = [line for line in config.read_text(encoding='utf-8').splitlines() if not line.startswith(keys)]
    config.write_text(NL.join(kept + lines) + NL, encoding='utf-8')
    scenarios.write_manifest(base)


def check_joiner(out, package, sha, save, players, join_mode):
    """A joiner's overlay as native_gate_join_overlays.py joiner checks it: the package against its announced hash and the save,
    the session's slots, the world table, every saved ID of the config, the fixture views and the probe's Lua state."""
    sys.path.insert(0, str(DEV))
    import native_gate_join as join
    import native_gate_join_overlays as join_overlays
    try:
        loaded = join.read_package(Path(package).read_bytes())
    except join.JoinError as error:
        raise SystemExit(f'join package refused: {error}') from None
    if loaded['sha'] != sha:
        raise SystemExit('join package refused: package_sha_announced')
    if sha256(save) != loaded['header']['save_sha']:
        raise SystemExit('join package refused: save_sha (the save is not the one the package names)')
    if loaded['sections']['gate']['clients'] != players:
        raise SystemExit(f"join package refused: the session has {loaded['sections']['gate']['clients']} slots, not {players}")
    if loaded['header']['flags'] & join.FLAG_DIAGNOSTIC and join_mode == 'strict':
        raise SystemExit('a diagnostic package refuses a strict joiner (--join-mode report)')
    text = (Path(out) / 'ui/config.lua.append').read_text(encoding='utf-8')
    try:
        world = join_overlays.check_world_table(loaded, join_mode)
        ids = join_overlays.check_saved_ids(text, loaded)
        views = join_overlays.check_fixture_views(text, loaded)
        providers = join_overlays.check_lua_state(text, loaded)
    except ValueError as error:
        Path(out).rename(Path(out).with_name(Path(out).name + '.refused'))
        raise SystemExit(f'joiner overlay refused: {error}') from None
    print(f'joiner: T={loaded["sections"]["gate"]["tick"]} saver={loaded["header"]["slot"]} world={world} '
          f'saved_ids={sum(ids.values())} fixture_views={views or "-"} lua_state={providers or "-"}')


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
    p.add_argument('--players', type=int, default=2, help='2..8')
    p.add_argument('--mode', choices=('coop', 'factions'), default='coop')
    p.add_argument('--minutes', type=float, default=40, help='length of the pilot window (default 40)')
    p.add_argument('--coverage', type=int, metavar='TICKS', help='diagnostic: the listed functions that run in ticks 1..TICKS')
    p.add_argument('--coverage-list', type=Path, help='with --coverage: X4.exe image RVAs, one per line')
    p.add_argument('--coverage-from', type=int, metavar='TICK', help='with --coverage: mark at tick TICK instead of tick 1')
    p.add_argument('--full-logs', action='store_true', help='keep the per-object world pages (no play mode; ~120 MB per capture)')
    p.add_argument('--lua-block', default=','.join(PLAY_LUA_BLOCK), metavar='NAMES',
                   help='Lua globals the Lua guard refuses during the session (default: %(default)s; empty: none)')
    p.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE',
                   help='extra config key (repeatable), e.g. native_gate_own_predict=false')
    p.add_argument('--open', default='', metavar='K[,K...]',
                   help='hot join: these slots start absent; a player joins one while the session runs (x4ls play --open)')
    p.add_argument('--join-mode', choices=('report', 'strict'), default='report',
                   help="with --open: the join's mode (report: what the live join tests run; strict refuses diagnostic packages)")
    p.add_argument('--join-package', type=Path, help="the joiner's overlay: the saver's j<T>.x4join (--snapshot is the join save)")
    p.add_argument('--join-package-sha', help='the package SHA-256 the relay announced (NGJOINSTATE)')
    p.add_argument('--mixed-platform', action='store_true',
                   help='a session with Windows and Linux PCs: the strict float recomputes of Option B on every client '
                        '(native_gate_fp_redirect, _solver, _drones); give it to every PC of the session')
    p.add_argument('--session', type=Path,
                   help='session.json (x4ls new-session; native_gate_session_start.py register): a session from any usable stock '
                        'start with per-player starts; --snapshot is its session save (a joiner: the join save)')
    args = p.parse_args(argv)
    session_data = None
    if args.session:
        session_data = json.loads(args.session.read_text(encoding='utf-8'))
        if session_data.get('players') != args.players or session_data.get('mode') != args.mode:
            raise SystemExit(f"--session: {session_data.get('players')} players in {session_data.get('mode')} mode, not "
                             f'{args.players} in {args.mode}')
        if not args.join_package and sha256(args.snapshot) != session_data.get('snapshot', {}).get('sha256'):
            raise SystemExit('--session: --snapshot is not its session save')
    opened = open_slots(args.open, args.players)
    if bool(args.join_package) != bool(args.join_package_sha) or (args.join_package and not opened):
        raise SystemExit('--join-package needs --join-package-sha and --open')
    out = args.out.resolve()
    base = out.with_name(out.name + '.base')
    for path in (out, base):
        if path.exists():
            raise SystemExit(f'exists: {path} (overlays are frozen; pick a new --out)')
    end = window_end(args.minutes)
    coverage = ['--coverage', str(args.coverage), '--coverage-list', args.coverage_list.resolve()] if args.coverage else []
    if args.coverage and args.coverage_from:
        coverage += ['--coverage-from', str(args.coverage_from)]
    join_base, sets = [], list(args.set)
    if args.mixed_platform:
        named = {t.replace(' ', '').split('=', 1)[0] for t in sets}
        sets += [t for t in MIXED_PLATFORM_SETS if t.split('=', 1)[0] not in named]
    if opened:
        sys.path.insert(0, str(DEV))
        import native_gate_join_overlays as join_overlays
        join_base = ['--world-identities', '--join-absent', ','.join(map(str, opened))]
        join_base += (['--join-package', args.join_package.resolve(), '--join-package-sha', args.join_package_sha.lower(),
                       '--join-mode', args.join_mode] if args.join_package else ['--join', args.join_mode])
        # Every join of a play session is inside the pilot window (it starts at WINDOW_START): the savers' package must carry
        # the window's Lua state and the joiner must resume into it (reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt; hj1:
        # lua_state:pilot_window:late without it).
        if not any(t.replace(' ', '').startswith('native_gate_join_in_window=') for t in sets):
            sets = sets + ['native_gate_join_in_window=true']
        # One world capture at T+1 on the session and the joiner (native_gate_world_hash_join): the joiner's census table takes
        # the session's created rows at its first capture (the IDS pad), and the per-tick digest needs that before the next
        # stride capture (hj1: digest apart at 1289, hj3 at T+3 with the default stride 1800; hj2 with it: equal to the end).
        if not any(t.replace(' ', '').startswith('native_gate_world_hash_join=') for t in sets):
            sets = sets + ['native_gate_world_hash_join=true']
        # Every slot's first store from the take, carried to a joiner (native_gate_pilot_first_stores, x4mods-28): an absent
        # slot seated late took the joiner's own canonical flight state, so its ship went down the other authority branch from
        # the handover (hj21..hj26: apart from H-1); hj27 with it: equal through the handover and 2,080 ticks after, 0 desyncs.
        if not any(t.replace(' ', '').startswith('native_gate_pilot_first_stores=') for t in sets):
            sets = sets + ['native_gate_pilot_first_stores=true']
        sets = join_overlays.default_sets(join_overlays.ids_sets(join_overlays.refresh_sets(join_overlays.precise_sets(sets))))
    step('base', [DEV / 'build_native_gate_overlay.py', '--mode', 'battle', '--gate-dll', args.gate_dll.resolve(),
                  '--snapshot', args.snapshot.resolve(), '--session-clients', *join_base, '--out', base, *coverage])
    if session_data and not args.join_package:   # a joiner's base boots the join save (its package carries the session's state)
        bind_session_save(base, args.snapshot, session_data)
    cmd = [DEV / 'native_gate_players.py', 'overlay', '--base', base, '--out', out, '--gate-dll', args.gate_dll.resolve(),
           '--probe', DEV / 'native_gate_probe.lua', '--players', str(args.players), '--mode', args.mode, '--source', 'stock',
           '--defaults-only', '--set', f'native_gate_pilot_window={{{WINDOW_START},{end}}}']   # --defaults-only takes no --window
    if not args.full_logs:
        cmd += ['--set', 'native_gate_play_mode=true']
    # Other players' ships drawn between the last two steps (smoke play-sE: 0 desyncs). The defaults' 'extrapolate'
    # draws them ahead, and the map then shows their icons flicking between the guessed and the true place (user, 8 Nov).
    if not any(item.startswith('native_gate_render_motion=') for item in args.set):
        cmd += ['--set', "native_gate_render_motion='interpolate'"]
    block = [name for name in args.lua_block.split(',') if name.strip()]
    if block:
        cmd += ['--set', "native_gate_ui_lua='block'",
                '--set', 'native_gate_ui_lua_names={' + ','.join(f"'{name.strip()}'" for name in block) + '}']
    if args.session:
        cmd += ['--session', args.session.resolve()] + (['--session-joiner'] if args.join_package else [])
    for item in sets:
        cmd += ['--set', item]
    step('players', cmd)
    print(step('check', [DEV / 'check_gate_options.py', out / 'ui/config.lua.append']).strip()[-600:])
    if args.join_package:
        check_joiner(out, args.join_package, args.join_package_sha.lower(), args.snapshot, args.players, args.join_mode)
    recipe = {'kind': RECIPE_KIND, 'version': RECIPE_VERSION, 'role': 'joiner' if args.join_package else 'session',
              'players': args.players, 'mode': args.mode, 'minutes': args.minutes, 'full_logs': args.full_logs,
              'lua_block': args.lua_block, 'set': list(args.set), 'open': opened, 'join_mode': args.join_mode,
              'window': [WINDOW_START, end], 'gate_dll_sha': sha256(out / 'native/x4lan_gate_64.dll'),
              'snapshot_sha': sha256(args.snapshot), 'session': session_data, 'mixed_platform': args.mixed_platform}
    (out / RECIPE).write_text(json.dumps(recipe, indent=1, sort_keys=True) + NL, encoding='utf-8')
    sys.path.insert(0, str(DEV))
    import native_gate_scenarios
    native_gate_scenarios.write_manifest(out)   # the recipe is part of the overlay: x4ls verifies every file against the manifest
    print(f'play overlay: {out}\n  window {WINDOW_START}..{end} ({args.minutes:g} min at {TICK_HZ} Hz), {args.players} players, '
          f'{args.mode}\n  run: python tools/x4ls.py play --overlay "{out}" --snapshot "{args.snapshot.resolve()}"')
    return 0


if __name__ == '__main__':
    sys.exit(main())
