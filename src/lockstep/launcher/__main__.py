"""x4ls command line: doctor, session, play, join, starts, new-session, snapshot, triage, clean, steam-fix."""
import argparse
import json
from pathlib import Path
import sys

from . import extension, game, session, steam


def doctor(args):
    install = Path(args.game) if args.game else game.find_install()
    print('X4:', install)
    game.verify_exe(install)
    print('X4.exe: pinned 9.00 build')
    ok, message = steam.status()
    print('Steam:', message)
    if args.overlay:
        extension.verify_overlay(args.overlay)
        print('overlay: manifest matches')
    print('relay:', session.RELAY, 'present' if session.RELAY.is_file() else 'MISSING')
    return 0 if ok else 1


def main(argv=None):
    p = argparse.ArgumentParser(prog='x4ls', description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)

    d = sub.add_parser('doctor', help='check installation, Steam and (optionally) an overlay')
    d.add_argument('--game', type=Path)
    d.add_argument('--overlay', type=Path)

    s = sub.add_parser('session', help='run relay + N windowed clients until the relay finishes')
    s.add_argument('--clients', type=int, default=2)
    s.add_argument('--overlay', type=Path, required=True, help='built gate overlay directory')
    s.add_argument('--snapshot', type=Path, help='native .xml.gz save the overlay was built for')
    s.add_argument('--ticks', type=int, default=1800)
    s.add_argument('--tick-hz', type=int, choices=(60, 40, 30, 20), help="default: the overlay's native_gate_tick_hz")
    s.add_argument('--observe', action='store_true', help='keep running through desyncs and record them')
    s.add_argument('--lookahead', type=int, default=0)
    s.add_argument('--timeout', type=int, default=900, help='seconds')
    s.add_argument('--size', default='960x540', help='client window size, WIDTHxHEIGHT')
    s.add_argument('--game', type=Path)
    s.add_argument('--lanes', type=Path, help='where client views go (default <game drive>:\\x4ls-lanes)')
    s.add_argument('--keep-views', action='store_true')
    s.add_argument('--fast', action='store_true', help='minimum render settings, no frame cap, no sound (soaks)')
    s.add_argument('--startup', type=int, default=70, help='seconds budgeted per client to reach the start menu (snapshot clients then arm together)')
    s.add_argument('--steam-fix', action='store_true', help='register the Steam pid before each client and restore it afterwards')
    s.add_argument('--checkpoint-at', type=int, metavar='TICK',
                   help='every client saves the world after this tick (save_001) and the session ends there; '
                        'a save is a --snapshot for later runs')
    s.add_argument('--stop-after-desync', type=int, metavar='TICKS', help='with --observe: end the session TICKS ticks after the first mismatch')
    s.add_argument('relay_args', nargs='*', help='extra relay arguments after --')

    pl = sub.add_parser('play', help='a session for people: desyncs are silent, errors are logged, nothing ends it early')
    pl.add_argument('--clients', type=int, default=2)
    pl.add_argument('--overlay', type=Path, required=True, help='built play overlay (tools/build_play_overlay.py)')
    pl.add_argument('--snapshot', type=Path, required=True, help='native .xml.gz save the overlay was built for')
    pl.add_argument('--ticks', type=int, help="relay ticks (default: the overlay's pilot window end + 3)")
    pl.add_argument('--stall-drop', type=float, default=session.PLAY_STALL_DROP_S, metavar='SECONDS',
                    help='drop a client the others waited for this long (its gate stopped, it froze or it exited)')
    pl.add_argument('--timeout', type=int, default=0, help='seconds (default 0: none; the session ends with its window)')
    pl.add_argument('--close-when-over', action='store_true',
                    help='close the X4 windows and collect once the session is over (unattended runs)')
    pl.add_argument('--size', default='1280x720', help='client window size, WIDTHxHEIGHT')
    pl.add_argument('--game', type=Path)
    pl.add_argument('--lanes', type=Path, help='where client views go (default <game drive>:\\x4ls-lanes)')
    pl.add_argument('--startup', type=int, default=70, help='seconds budgeted per client to reach the start menu')
    pl.add_argument('--steam-fix', action='store_true', help='register the Steam pid before each client and restore it afterwards')
    pl.add_argument('--remote-slots', default='', metavar='K[,K...]',
                    help='slots that play on other PCs (x4ls join there); needs --port, --bind and --allow-from')
    pl.add_argument('--bind', default='127.0.0.1', help='with --remote-slots: the address of this PC the other PCs reach')
    pl.add_argument('--allow-from', default='', metavar='IP[,IP...]', help='with --remote-slots: the other PCs\' addresses')
    pl.add_argument('--port', type=int, default=0, help='with --remote-slots: the relay\'s UDP port (forward it on the router)')
    pl.add_argument('--public-host', default='', help='with --remote-slots: the address the other PCs reach (default --bind)')
    pl.add_argument('--join-wait', type=int, default=240, metavar='SECONDS',
                    help='with --remote-slots: extra time before the common load for the other PCs to start')
    pl.add_argument('--open', default='', metavar='K[,K...]',
                    help='hot join: these slots start absent and a player joins one while the session runs (x4ls join without '
                         '--arm-at); the overlay must be built with build_play_overlay.py --open K')
    pl.add_argument('relay_args', nargs='*', help='extra relay arguments after --')

    j = sub.add_parser('join', help='play one slot of a session whose relay runs on another PC (the line x4ls play printed): '
                                    'with --arm-at a slot that loads with the others, without it an open slot (hot join)')
    j.add_argument('--relay', required=True, metavar='HOST:PORT')
    j.add_argument('--token', required=True)
    j.add_argument('--slot', type=int, required=True)
    j.add_argument('--clients', type=int, help='with --arm-at: the session\'s slots (a hot join reads them from the overlay)')
    j.add_argument('--arm-at', type=int, metavar='UTC_SECOND',
                   help='the UTC second the session loads at (a --remote-slots slot); without it: a hot join of an --open slot')
    j.add_argument('--overlay', type=Path, required=True, help='the same play overlay as the host (a copy of its folder)')
    j.add_argument('--snapshot', type=Path, help='with --arm-at: the same snapshot save as the host (a hot join gets the join save)')
    j.add_argument('--wait', type=int, default=600, metavar='SECONDS',
                   help='hot join: how long to wait for the session to save the join (default 600)')
    j.add_argument('--nonce', metavar='HEX16', help="hot join: resume the join this PC asked for (its run folder's join/state.json; "
                                                     'the last one of the slot and relay is resumed by default)')
    j.add_argument('--size', default='1280x720')
    j.add_argument('--timeout', type=int, default=0)
    j.add_argument('--close-when-over', action='store_true')
    j.add_argument('--game', type=Path)
    j.add_argument('--lanes', type=Path)
    j.add_argument('--startup', type=int, default=70)
    j.add_argument('--steam-fix', action='store_true')
    j.add_argument('--same-pc', action='store_true', help='test: the host runs on this PC too (own state file, its X4 tolerated)')

    st = sub.add_parser('starts', help="the game's starts: which can be the session's (slot 1) and which a player's own")
    st.add_argument('--game', type=Path)
    st.add_argument('--all', action='store_true', help='also list the refused starts (tutorials, tests, Timelines)')

    ns = sub.add_parser('new-session', help='a session from a chosen start with per-player starts: capture, register, overlay')
    ns.add_argument('--base-start', required=True, metavar='GAMESTART', help='the session start (slot 1; x4ls starts: session-base=yes)')
    ns.add_argument('--players', type=int, default=2)
    ns.add_argument('--mode', choices=('coop', 'factions'), default='coop')
    ns.add_argument('--start', action='append', default=[], metavar='K=GAMESTART',
                    help="slot K's own start (x4ls starts: player-start=yes); other slots start with slot 1")
    ns.add_argument('--omit', action='append', default=[], metavar='K=PART[,PART]',
                    help="kit parts slot K's start leaves out (money, reputation, blueprints, research, map)")
    ns.add_argument('--gate-dll', type=Path, required=True)
    ns.add_argument('--out', type=Path, required=True, help='new session folder (spec, save, session.json, overlay)')
    ns.add_argument('--minutes', type=float, default=40)
    ns.add_argument('--open', default='', metavar='K[,K...]', help='hot-joinable slots (x4ls play --open)')
    ns.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE')
    ns.add_argument('--game', type=Path)
    ns.add_argument('--lanes', type=Path)
    ns.add_argument('--timeout', type=int, default=900, help='seconds for the capture')
    ns.add_argument('--steam-fix', action='store_true')

    n = sub.add_parser('snapshot', help='capture a starting snapshot (new game + 12-ship gate fixture)')
    n.add_argument('--out', type=Path, required=True, help='new .xml.gz file')
    n.add_argument('--gamestart', default='custom_creative')
    n.add_argument('--timeout', type=int, default=900)
    n.add_argument('--game', type=Path)
    n.add_argument('--lanes', type=Path)
    n.add_argument('--seed', type=int, default=session.SESSION_SEED, help='X4 session seed of the new game (saved in its header)')
    n.add_argument('--steam-fix', action='store_true')

    c = sub.add_parser('clean', help='stop processes and remove views left by a crashed session')
    c.add_argument('--keep-views', action='store_true')

    t = sub.add_parser('triage', help='rerun a diverged session with diagnostic windows around its first mismatch')
    t.add_argument('run', type=Path, help='the diverged run directory (.local/runs/<time>)')
    t.add_argument('--steam-fix', action='store_true')
    t.add_argument('--report-only', type=Path, metavar='RERUN', help='only write the report of an existing rerun directory')

    f = sub.add_parser('steam-fix', help='register the running Steam pid so direct X4 starts can reach Steam')
    f.add_argument('--restore', action='store_true', help='put back the value recorded by steam-fix')

    args = p.parse_args(argv)
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(line_buffering=True)  # progress lines reach a redirected log as they happen
    state = session.ROOT / '.local/x4ls'
    if args.command == 'doctor':
        return doctor(args)
    if args.command == 'starts':
        from . import newsession
        newsession.starts(args.game, show_all=args.all)
        return 0
    if args.command == 'new-session':
        from . import newsession
        def slots(texts, what):
            out = {}
            for text in texts:
                k, _, v = text.partition('=')
                if not k.isdigit() or not v or int(k) in out:
                    raise SystemExit(f'{what} takes K=VALUE once per slot, not {text!r}')
                out[int(k)] = v
            return out
        result = newsession.new_session(newsession.NewSessionOptions(
            base_start=args.base_start, players=args.players, mode=args.mode, out=args.out, gate_dll=args.gate_dll,
            starts=slots(args.start, '--start'), omit=slots(args.omit, '--omit'), minutes=args.minutes, open=args.open,
            sets=args.set, game=args.game, lanes=args.lanes, timeout=args.timeout, steam_fix=args.steam_fix))
        print(json.dumps(result, indent=2))
        return 0
    if args.command == 'snapshot':
        outcome = session.capture_snapshot(args.out, args.gamestart, args.game, args.lanes, args.timeout,
                                           seed=args.seed, steam_fix=args.steam_fix)
        return 0 if outcome == 'saved' else 1
    if args.command == 'clean':
        print(session.clean(state, args.keep_views))
        return 0
    if args.command == 'triage':
        from . import triage
        if args.report_only:
            first = json.loads((args.run / 'summary.json').read_text())['first_digest_mismatch']
            print(triage.report(args.report_only, first))
        else:
            triage.triage(args.run, steam_fix=args.steam_fix)
        return 0
    if args.command == 'steam-fix':
        if args.restore:
            print('ActiveProcess pid restored to', steam.restore(state / 'steam-fix.json'))
        else:
            previous, pid = steam.fix(state / 'steam-fix.json')
            print(f'ActiveProcess pid {previous} -> {pid}; undo with: x4ls steam-fix --restore')
        return 0
    width, height = (int(v) for v in args.size.lower().split('x'))
    if args.command == 'join' and args.arm_at is None:
        from . import hotjoin
        if args.snapshot or args.clients:
            raise SystemExit('a hot join (no --arm-at) takes no --snapshot or --clients: the join save and the slots come from the session')
        result = hotjoin.hot_join(hotjoin.HotJoinOptions(relay=args.relay, token=args.token, slot=args.slot, overlay=args.overlay,
                                                         wait=args.wait, nonce=args.nonce, width=width, height=height, game=args.game,
                                                         lanes=args.lanes, startup=args.startup, steam_fix=args.steam_fix,
                                                         timeout=args.timeout, close_when_over=args.close_when_over,
                                                         same_pc=args.same_pc,
                                                         **({'state': session.ROOT / '.local/x4ls-join'} if args.same_pc else {})))
        print(json.dumps(result, indent=2))
        return 0 if result['outcome'] == 'played' else 1
    if args.command == 'join':
        if not args.clients or not args.snapshot:
            raise SystemExit('x4ls join --arm-at needs --clients and --snapshot')
        _, tick_hz, _ = session.play_settings(args.overlay, args.clients)
        result = session.join(session.JoinOptions(relay=args.relay, token=args.token, slot=args.slot, clients=args.clients,
                                                  arm_at=args.arm_at, overlay=args.overlay, snapshot=args.snapshot, width=width,
                                                  height=height, game=args.game, lanes=args.lanes, startup=args.startup,
                                                  steam_fix=args.steam_fix, timeout=args.timeout,
                                                  close_when_over=args.close_when_over, tick_hz=tick_hz, same_pc=args.same_pc,
                                                  **({'state': session.ROOT / '.local/x4ls-join'} if args.same_pc else {})))
        print(json.dumps(result, indent=2))
        return 0 if result['outcome'] == 'played' else 1
    if args.command == 'play':
        lookahead, tick_hz, last = session.play_settings(args.overlay, args.clients)
        ticks = args.ticks or (last + 3 if last else None)
        if not ticks:
            raise SystemExit('the overlay has no pilot window: pass --ticks')
        if not lookahead:
            print('note: a protocol 3 overlay (no lookahead): the relay cannot drop a stopped client, the others would wait')
        options = session.Options(clients=args.clients, overlay=args.overlay, snapshot=args.snapshot, ticks=ticks, tick_hz=tick_hz,
                                  lookahead=lookahead, timeout=args.timeout, width=width, height=height, game=args.game,
                                  lanes=args.lanes, startup=args.startup, steam_fix=args.steam_fix, play=True,
                                  stall_drop_ms=1000 * args.stall_drop if lookahead else 0, relay_args=args.relay_args,
                                  close_when_over=args.close_when_over,
                                  remote_slots=tuple(int(v) for v in args.remote_slots.split(',') if v.strip()),
                                  bind=args.bind, allow_from=tuple(a for a in args.allow_from.split(',') if a.strip()),
                                  port=args.port, join_wait=args.join_wait, public_host=args.public_host,
                                  open_slots=tuple(int(v) for v in args.open.split(',') if v.strip()))
        print(f'play: {args.clients} clients, relay ticks {ticks} ({ticks / tick_hz / 60:.0f} min at {tick_hz} Hz), lookahead '
              f'{lookahead}, stall drop ' + (f'{args.stall_drop:g} s' if lookahead else 'off'))
        result = session.run(options)
        print(json.dumps(result, indent=2))
        return 0 if result['outcome'] == 'played' else 1
    options = session.Options(clients=args.clients, overlay=args.overlay, snapshot=args.snapshot, ticks=args.ticks,
                              tick_hz=args.tick_hz, observe=args.observe, lookahead=args.lookahead,
                              timeout=args.timeout, width=width, height=height, game=args.game, lanes=args.lanes,
                              keep_views=args.keep_views, fast=args.fast, startup=args.startup, steam_fix=args.steam_fix,
                              checkpoint_at=args.checkpoint_at,
                              stop_after_desync=args.stop_after_desync, relay_args=args.relay_args)
    result = session.run(options)
    return 0 if result['outcome'] in ('passed', 'checkpointed') else 1


if __name__ == '__main__':
    sys.exit(main())
