"""Players choose the session's start and their own starts (x4ls starts, x4ls new-session).

Every client loads one session save (option B, reports/LOCKSTEP-STARTS-20261101.txt: X4's new game is not reproducible
across processes, the save is). x4ls new-session makes that save and the play overlay for it in one go:
  1. the start catalog from this PC's game data (gamedata.py; native_gate_gamestarts.py classes a/b/c, refusals named);
  2. the spec (native_gate_session_start.build_spec): the session base start for slot 1, a per-player start for any
     slot 2..N (its kit: money, reputation, blueprints, research, map knowledge; its hull at the start position);
  3. one capture client on the capture profile: X4's new game for the base start, the 12-ship fixture (the census
     anchor) and the per-player hulls, saved (native_gate_session_start.build_capture, session.capture_snapshot);
  4. register (session.json: the save's start, seed, player ship, fixture, hulls, sha256);
  5. the play overlay (tools/build_play_overlay.py --session, and --open K for hot-joinable slots).
Then x4ls play --clients N --overlay OUT/overlay --snapshot OUT/session-save.xml.gz; the per-player kits are applied at
the setup tick on every client.
"""
from dataclasses import dataclass, field
import json
from pathlib import Path
import subprocess
import sys

from . import game, gamedata

ROOT = Path(__file__).resolve().parents[3]
DEV = ROOT / 'tools/dev'


def tools():
    sys.path.insert(0, str(DEV))
    import native_gate_gamestarts as gamestarts
    import native_gate_session_start as session_start
    return gamestarts, session_start


def catalog(game_dir=None, cache=None, log=print):
    gamestarts, _ = tools()
    install = Path(game_dir) if game_dir else game.find_install()
    root = gamedata.unpack(install, cache or gamedata.default_cache(), log)
    return gamestarts.Catalog(root, root)


def starts(game_dir=None, cache=None, show_all=False, log=print):
    """The stock starts: which one can be the session base (slot 1) and which a player's own start (slots 2..N)."""
    rows = [r for r in catalog(game_dir, cache, log).rows() if show_all or r['class'] != '-']
    for r in rows:
        pl = r['player']
        loc = pl.get('location', {})
        base = 'yes' if not r['base'] else 'no'
        own = 'yes' if not r['per_player'] and r['class'] in ('a', 'c') else 'no'
        log(f"{r['id']:34} class={r['class']} session-base={base} player-start={own} "
            f"sector={loc.get('sector') or loc.get('zone') or '-'} ship={pl.get('ship', {}).get('macro', '-')} money={pl.get('money', '-')}")
        for reason in r['reasons'] + ([f"player start refused: {r['per_player']}"] if r['per_player'] and r['class'] in ('a', 'c') else []):
            log(f'    {reason}')
    return rows


@dataclass
class NewSessionOptions:
    base_start: str
    players: int
    mode: str
    out: Path
    gate_dll: Path
    starts: dict = field(default_factory=dict)   # slot -> gamestart id (slots 2..players)
    omit: dict = field(default_factory=dict)     # slot -> kit parts left out (money, reputation, ...)
    minutes: float = 40
    open: str = ''                               # build_play_overlay.py --open (hot-joinable slots)
    sets: list = field(default_factory=list)
    game: Path | None = None
    lanes: Path | None = None
    timeout: int = 900
    steam_fix: bool = False
    cache: Path | None = None


def new_session(o, log=print):
    """Spec, capture, register and the play overlay of a session (see the module text); returns the result dict."""
    from . import session
    _, session_start = tools()
    out = Path(o.out)
    if out.exists():
        raise SystemExit(f'{out} exists (a session folder is made once; pick a new --out)')
    try:
        spec = session_start.build_spec(catalog(o.game, o.cache, log), o.base_start, o.players, o.mode, o.starts, o.omit)
    except session_start.SessionError as error:
        raise SystemExit(f'refused: {error}') from None
    out.mkdir(parents=True)
    (out / 'spec.json').write_text(json.dumps(spec, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    for k, slot in sorted(spec['slots'].items(), key=lambda kv: int(kv[0])):
        log(f"slot {k}: start {slot['gamestart']} (class {slot['class']}) in {slot['hull']['sector']}, kit "
            + ', '.join(sorted(slot['kit'])))
    capture = session_start.build_capture(spec, None, out / 'capture-overlay')
    save = out / 'session-save.xml.gz'
    outcome = session.capture_snapshot(save, o.base_start, o.game, o.lanes, o.timeout, log=log, steam_fix=o.steam_fix,
                                       overlay=capture)
    if outcome != 'saved':
        raise SystemExit(f'the capture of {o.base_start} ended {outcome}; nothing registered')
    try:
        registered = session_start.register(spec, save)
    except session_start.SessionError as error:
        raise SystemExit(f'the captured save is refused: {error}') from None
    (out / 'session.json').write_text(json.dumps(registered, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    cmd = [sys.executable, str(ROOT / 'tools/build_play_overlay.py'), '--snapshot', str(save), '--gate-dll', str(o.gate_dll),
           '--out', str(out / 'overlay'), '--players', str(o.players), '--mode', o.mode, '--minutes', repr(o.minutes),
           '--session', str(out / 'session.json')] + (['--open', o.open] if o.open else [])
    for item in o.sets:
        cmd += ['--set', item]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.stdout.write(result.stdout[-3000:])
        sys.stderr.write(result.stderr[-3000:])
        raise SystemExit(f'the play overlay was refused ({result.returncode})')
    run = (f'python tools/x4ls.py play --clients {o.players} --overlay "{out / "overlay"}" --snapshot "{save}"'
           + (f' --open {o.open}' if o.open else ''))
    log(f'session {out}: base start {o.base_start}, {o.players} players ({o.mode}); play it with:\n  {run}')
    return {'outcome': 'ready', 'session': str(out / 'session.json'), 'save': str(save), 'overlay': str(out / 'overlay'), 'run': run}
