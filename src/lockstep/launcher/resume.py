"""x4ls resume: a new session whose world is a save a play session wrote while it ran (its periodic saves, saves.py).

A desync that shows an hour into a session costs that hour on every rerun from the start. This makes a session that starts
from the world of a periodic save instead, minutes before the split:
  0. one player: a session's world holds a player object per slot, and X4 without the gate refuses such a save
     ("Invalid number of players"). The save is copied without the other slots' player objects (single_player);
  1. prepare: one client on the capture profile loads that copy, the MD cue Respawn puts a fresh twelve-ship fixture beside
     the player's ship (the session's own fixture has fought by then, and every session start needs twelve live ones), the
     hulls of the per-player starts are created again, and the world is saved (session.capture_snapshot from_save;
     tools/dev/native_gate_snapshot.lua in its from-save mode);
  2. register: session.json for that save from the session's spec (native_gate_session_start.register);
  3. the play overlay, from the recipe of the overlay the session ran with (tools/build_play_overlay.py --session).
Then x4ls play as for any session.

What this is and is not. It is the saved WORLD: every station, ship, order, script and economy state as X4 saved and
loaded it, the same file on every client. It is not the session continued: the gate's own state is not in a plain save
(a hot joiner gets it from the join package). Slot 1 flies X4's own player from where the save has it. Slots 2..N get
new hulls at their start places and their kits again; their old ships stay in the world without a pilot, and what the
world's scripts held about their player objects points at nothing. Stories are not started a second time (--story to do so).
The prepare step runs the loaded world for some seconds without the gate. So a split is not replayed tick for tick:
the session gets the same crowded world to split in again, from minute 70 instead of minute 0.
"""
from dataclasses import dataclass, field
import copy
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
DEV = ROOT / 'tools/dev'


def tools():
    sys.path.insert(0, str(DEV))
    sys.path.insert(0, str(ROOT / 'tools'))
    import build_play_overlay
    import native_gate_scenarios as scenarios
    import native_gate_session_start as session_start
    return build_play_overlay, scenarios, session_start


@dataclass
class ResumeOptions:
    save: Path                 # a periodic save (RUN/saves/c<K>-t<TICK>.xml.gz)
    spec: Path                 # the session's spec.json (x4ls new-session OUT/spec.json)
    overlay: Path              # the play overlay the session ran with (its recipe)
    gate_dll: Path
    out: Path                  # new folder: capture overlay, save, session.json, overlay
    minutes: float | None = None   # the new session's length (default: the recipe's)
    story: bool = False        # start the slots' stories again
    sets: list = field(default_factory=list)   # further --set items (the recipe's own are kept)
    save_every: float | None = None            # default: the recipe's
    keep_player: str | None = None             # the saved ID of X4's own player object when the save does not tell
    display: int | None = None                 # the prepare client's window: this monitor (1 = primary)
    focus: bool = True                         # False: its window never keeps the keyboard focus or the cursor
    game: Path | None = None
    lanes: Path | None = None
    timeout: int = 900
    steam_fix: bool = False


PLAYER = re.compile(rb'^\s*<component class="player"[^>]*\bid="\[0x([0-9a-f]{1,16})\]"')


def players(save):
    """The player objects of a save: [(saved ID, has a name, line)], in file order."""
    found = []
    with gzip.open(save, 'rb') as source:
        for line in source:
            match = PLAYER.match(line)
            if match:
                found.append((f'{int(match.group(1), 16):016x}', b' name="' in line.split(b'>', 1)[0], line.strip()[:200].decode('utf-8', 'replace')))
    return found


def single_player(save, out, keep=None):
    """Copies a session's save without the player objects of the other slots; returns (kept saved ID, removed IDs).
    keep: the saved ID of X4's own player; by default the one player object that carries a name (the game start gives
    X4's own player one, the gate's player objects have none). X4 writes one element per line, so a component is its
    line through the matching </component>."""
    found = players(save)
    if not found:
        raise SystemExit(f'{save}: no player object in this save')
    if keep is None:
        named = [saved for saved, has_name, _ in found if has_name]
        if len(found) == 1:
            named = [found[0][0]]
        if len(named) != 1:
            raise SystemExit(f'{save}: {len(found)} player objects, {len(named)} of them named: say which is X4\'s own with '
                             f"--keep-player ID ({', '.join(saved for saved, _, _ in found)})")
        keep = named[0]
    keep = f'{int(keep, 16):016x}'
    if keep not in [saved for saved, _, _ in found]:
        raise SystemExit(f'{save}: no player object {keep}')
    removed, depth = [], 0
    with gzip.open(save, 'rb') as source, gzip.open(out, 'wb', compresslevel=5) as target:
        for line in source:
            if depth:
                text = line.strip()
                if text.startswith(b'<component ') and not text.endswith(b'/>'):
                    depth += 1
                elif text.startswith(b'</component>'):
                    depth -= 1
                continue
            match = PLAYER.match(line)
            if match and f'{int(match.group(1), 16):016x}' != keep:
                removed.append(f'{int(match.group(1), 16):016x}')
                depth = 0 if line.strip().endswith(b'/>') else 1
                continue
            target.write(line)
    if depth:
        raise SystemExit(f'{save}: a player object does not end (not a whole save?)')
    return keep, removed


def resume_spec(spec, story=False):
    """The session's spec for a resumed world: the same starts, hulls and kits; without the stories unless asked for (the
    saved world already holds their state: a second start of a story script is not what the first session ran)."""
    out = copy.deepcopy(spec)
    if not story:
        for slot in out.get('slots', {}).values():
            if slot.get('story'):
                slot['story'] = None
    return out


def capture_overlay(session_start, scenarios, spec, out):
    """The spec's capture overlay in from-save mode."""
    overlay = session_start.build_capture(spec, None, out)
    config = Path(overlay) / 'ui/config.lua.append'
    config.write_text(config.read_text(encoding='utf-8') + 'X4LAN_CONFIG.native_gate_snapshot_from_save = true\n', encoding='utf-8')
    scenarios.write_manifest(overlay)
    return overlay


def overlay_command(recipe, save, session_json, gate_dll, out, minutes=None, sets=(), save_every=None):
    """build_play_overlay.py's arguments: the first session's recipe on the resumed save."""
    cmd = ['--snapshot', str(save), '--gate-dll', str(gate_dll), '--out', str(out), '--players', str(recipe['players']),
           '--mode', recipe['mode'], '--minutes', repr(recipe['minutes'] if minutes is None else minutes),
           '--lua-block', recipe['lua_block'], '--session', str(session_json),
           '--save-every', repr(recipe.get('save_every', 0) if save_every is None else save_every)]
    if recipe['full_logs']:
        cmd.append('--full-logs')
    if recipe.get('mixed_platform'):
        cmd.append('--mixed-platform')
    if recipe.get('open'):
        cmd += ['--open', ','.join(map(str, recipe['open'])), '--join-mode', recipe['join_mode']]
    for item in list(recipe['set']) + list(sets):
        cmd += ['--set', item]
    return cmd


def resume(o, log=print):
    from . import session
    build_play_overlay, scenarios, session_start = tools()
    save, out = Path(o.save), Path(o.out)
    if not save.is_file():
        raise SystemExit(f'{save}: no such save')
    if out.exists():
        raise SystemExit(f'{out} exists (a session folder is made once; pick a new --out)')
    recipe = build_play_overlay.read_recipe(o.overlay)
    if recipe is None or recipe.get('role') != 'session':
        raise SystemExit(f'{o.overlay}: not a session\'s play overlay (no recipe)')
    first = json.loads(Path(o.spec).read_text(encoding='utf-8'))
    registered_first = recipe.get('session') or {}
    if registered_first.get('spec_sha256') and \
            hashlib.sha256(json.dumps(first, sort_keys=True).encode()).hexdigest() != registered_first['spec_sha256']:
        raise SystemExit(f'{o.spec} is not the spec of the session {o.overlay} was built for')
    if first.get('players') != recipe['players'] or first.get('mode') != recipe['mode']:
        raise SystemExit(f"{o.spec}: {first.get('players')} players in {first.get('mode')} mode, the overlay "
                         f"{recipe['players']} in {recipe['mode']}")
    header = scenarios.snapshot_header(save)
    if header['start'] != first['base_start']:
        raise SystemExit(f"{save}: a save of {header['start']}, the session's base start is {first['base_start']}")
    spec = resume_spec(first, o.story)
    out.mkdir(parents=True)
    (out / 'spec.json').write_text(json.dumps(spec, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    (out / 'resumed-from.json').write_text(json.dumps({'save': str(save.resolve()), 'sha256': scenarios.digest(save),
                                                        'bytes': save.stat().st_size, 'overlay': str(Path(o.overlay).resolve()),
                                                        'spec': str(Path(o.spec).resolve()), 'story': bool(o.story)},
                                                       indent=1, sort_keys=True) + '\n', encoding='utf-8')
    capture = capture_overlay(session_start, scenarios, spec, out / 'capture-overlay')
    prepared, single = out / 'session-save.xml.gz', out / 'one-player.xml.gz'
    kept, removed = single_player(save, single, o.keep_player)
    log(f'one player: X4\'s own player object {kept} kept' + (f", {len(removed)} other slot(s)' removed ({', '.join(removed)})" if removed else ''))
    log(f'prepare: loading {save.name} ({save.stat().st_size / 1e6:.1f} MB) on the capture profile')
    outcome = session.capture_snapshot(prepared, first['base_start'], o.game, o.lanes, o.timeout, log=log, steam_fix=o.steam_fix,
                                       overlay=capture, from_save=single, display=o.display, focus=o.focus)
    single.unlink(missing_ok=True)
    if outcome != 'saved':
        raise SystemExit(f'the prepare step ended {outcome}; nothing registered')
    try:
        registered = session_start.register(spec, prepared)
    except (session_start.SessionError, ValueError) as error:
        raise SystemExit(f'the prepared save is refused: {error}') from None
    (out / 'session.json').write_text(json.dumps(registered, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    cmd = [sys.executable, str(ROOT / 'tools/build_play_overlay.py')] + \
        overlay_command(recipe, prepared, out / 'session.json', o.gate_dll, out / 'overlay', o.minutes, o.sets, o.save_every)
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.stdout.write(result.stdout[-3000:])
        sys.stderr.write(result.stderr[-3000:])
        raise SystemExit(f'the play overlay was refused ({result.returncode})')
    run = f'python tools/x4ls.py play --clients {recipe["players"]} --overlay "{out / "overlay"}" --snapshot "{prepared}"'
    log(f'resumed session {out}: the world of {save.name}; slot 1 flies {registered["player_macro"]} in {registered["player_sector"]}; '
        f'play it with:\n  {run}')
    return {'outcome': 'ready', 'session': str(out / 'session.json'), 'save': str(prepared), 'overlay': str(out / 'overlay'), 'run': run}
