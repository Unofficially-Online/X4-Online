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
    """The stock starts: which one can be the session base (slot 1) and which a player's own start (slots 2..N).
    player-start=ship+story: with --omit K=world the player gets that start's ship, place and kit and, with --story K,
    its own story (the stock script for the first slot with that start, a copy of it for a slot whose start is the base
    start's or an earlier slot's, with the stock plots that continue it; --omit K=world,story leaves the story out), not
    its other world parts (stations, plots other scripts run); ship-only: a start without a story script of the stock
    shape."""
    rows = [r for r in catalog(game_dir, cache, log).rows() if show_all or r['class'] != '-']
    for r in rows:
        pl = r['player']
        loc = pl.get('location', {})
        base = 'yes' if not r['base'] else 'no'
        own = ('yes' if not r['per_player'] and r['class'] in ('a', 'c') else
               ('ship+story' if r.get('story') else 'ship-only') if not r['ship_only'] else 'no')
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
    story: tuple = ()                            # slots whose start's story script runs for that slot's player
    plots: dict = field(default_factory=dict)    # slot -> [plot ids] it runs as its own copy (native_gate_plot_copies.py: hq)
    unpiloted: tuple = ()                        # every slot's start ship has no NPC pilot now; kept for older scripts
    piloted: tuple = ()                          # slots whose start ship keeps an NPC pilot (the form before)
    minutes: float = 40
    open: str = ''                               # build_play_overlay.py --open (hot-joinable slots)
    sets: list = field(default_factory=list)
    game: Path | None = None
    lanes: Path | None = None
    timeout: int = 900
    steam_fix: bool = False
    cache: Path | None = None
    display: int | None = None                   # the capture client's window on this monitor (1 = primary)
    focus: bool = True                           # False: that window never keeps the keyboard focus or the cursor
    fixture_ships: bool = False                  # True: slots without --start fly fixture ships (the form before 24 Nov)


def own_starts(o, cat, log=print):
    """(starts, omit) for the spec: every slot 2..N without --start takes the base start's own ship, place and kit beside
    slot 1's (as --start K=BASE --omit K=world), so each player starts in that start's ship with its loadout. Before
    24 Nov such a slot flew one of the capture fixture's fighters (a two-PC game at the Terran cadet start: player 2
    in an Argon fighter). Open slots (--open) take it too: since then no fixture fighter waits in them, and a drop-in
    session's open slot without a ship refused its play overlay. Kept: o.fixture_ships and a base start that cannot
    give a slot its ship (logged)."""
    gamestarts, _ = tools()
    starts, omit = dict(o.starts), dict(o.omit)
    missing = [k for k in range(2, o.players + 1) if k not in starts]
    if o.fixture_ships or not missing:
        return starts, omit
    why = gamestarts.per_player_refusal(cat.record(o.base_start), world=True)
    if why:
        log(f"slots {', '.join(map(str, missing))}: fixture ships ({why})")
        return starts, omit
    for k in missing:
        starts[k] = o.base_start
        parts = [p for p in (omit.get(k) or '').split(',') if p] if isinstance(omit.get(k, ''), str) else list(omit[k])
        omit[k] = ','.join(parts + ([] if 'world' in parts else ['world']))
    return starts, omit


def new_session(o, log=print):
    """Spec, capture, register and the play overlay of a session (see the module text); returns the result dict."""
    from . import session
    _, session_start = tools()
    out = Path(o.out)
    if out.exists():
        raise SystemExit(f'{out} exists (a session folder is made once; pick a new --out)')
    try:
        cat = catalog(o.game, o.cache, log)
        starts, omit = own_starts(o, cat, log)
        spec = session_start.build_spec(cat, o.base_start, o.players, o.mode, starts, omit,
                                        story=o.story, unpiloted=o.unpiloted, piloted=o.piloted)
        if o.plots:
            install = Path(o.game) if o.game else game.find_install()
            session_start.add_plots(spec, o.plots, gamedata.unpack(install, o.cache or gamedata.default_cache(), log))
    except session_start.SessionError as error:
        raise SystemExit(f'refused: {error}') from None
    out.mkdir(parents=True)
    (out / 'spec.json').write_text(json.dumps(spec, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    for e in spec.get('first_plots') or []:
        log(f"slot 1: begins with the result of plot {e['id']}")
    for k, slot in sorted(spec['slots'].items(), key=lambda kv: int(kv[0])):
        log(f"slot {k}: start {slot['gamestart']} (class {slot['class']}) in {slot['hull']['sector']}, kit "
            + ', '.join(sorted(slot['kit']))
            + (f"; its story runs for this player ({slot['story']['script']}.{slot['story']['cue']})" if slot.get('story') else '')
            + (f"; its own copies: {', '.join(e['id'] + '=' + e['copy'] for e in slot['plots'])}" if slot.get('plots') else '')
            + (f"; left out: {'; '.join(slot['world_omitted'])}"[:400] if slot.get('world_omitted') else ''))
    capture = session_start.build_capture(spec, None, out / 'capture-overlay')
    save = out / 'session-save.xml.gz'
    outcome = session.capture_snapshot(save, o.base_start, o.game, o.lanes, o.timeout, log=log, steam_fix=o.steam_fix,
                                       overlay=capture, display=o.display, focus=o.focus)
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
