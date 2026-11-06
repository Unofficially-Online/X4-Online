"""x4ls command line: doctor, session, snapshot, triage, clean, steam-fix."""
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
