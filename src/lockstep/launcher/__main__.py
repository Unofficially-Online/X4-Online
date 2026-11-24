"""x4ls command line: doctor, session, play, join, starts, new-session, resume, snapshot, triage, clean, steam-fix, master."""
import argparse
import json
from pathlib import Path
import sys

from . import extension, game, netcli, session, steam


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


def run_master(args):
    """x4ls master: the options as server.MasterOptions, then serve until stopped."""
    from ..master import server
    from ..master.limits import MasterLimits
    first, dash, last = args.relay_ports.partition('-')
    if not dash or not first.isdigit() or not last.isdigit():
        raise SystemExit('--relay-ports must be FIRST-LAST, e.g. 47200-47263')
    limits = MasterLimits(games_max=args.games_max, relays_max=args.relays_max, games_per_ip=args.games_per_ip,
                          data_bytes_max=int(args.data_max_gib * (1 << 30)))
    options = server.MasterOptions(bind=args.bind, http_bind=args.http_bind, http_port=args.http_port,
                                   echo_port=args.echo_port, relay_ports=(int(first), int(last)), data=args.data,
                                   limits=limits, public_host=args.public_host, public_prefix=args.public_prefix,
                                   trust_proxy=args.trust_proxy, relay_releases=tuple(args.relay_release))
    return server.run(options)


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
    pl.add_argument('--ticks', type=int, help="relay ticks (default: the overlay's pilot window end + 1, the tick the controls go back)")
    pl.add_argument('--stall-drop', type=float, default=session.PLAY_STALL_DROP_S, metavar='SECONDS',
                    help='drop a client the others waited for this long (its gate stopped, it froze or it exited)')
    pl.add_argument('--timeout', type=int, default=0, help='seconds (default 0: none; the session ends with its window)')
    pl.add_argument('--keep-saves', type=int, default=3, metavar='N',
                    help="periodic saves (the overlay's --save-every): how many save ticks stay in RUN/saves while the session is "
                         'in sync (default 3); after a desync the last N before it stay for good')
    pl.add_argument('--close-when-over', action='store_true',
                    help='close the X4 windows and collect once the session is over (unattended runs)')
    pl.add_argument('--size', default='1280x720', help='client window size, WIDTHxHEIGHT')
    pl.add_argument('--borderless', action='store_true',
                    help="X4's borderless window at --size for every client of this PC (it cannot be resized; a resize reloads "
                         "X4's UI). The windows lie on top of each other: Alt+Tab between them")
    pl.add_argument('--display', type=int,
                    help='keep the client windows on this monitor only (the number Windows gives it: 1, 2, 3, ...)')
    pl.add_argument('--no-focus', action='store_true',
                    help='while the clients start, a client window that takes the keyboard focus gives it back to the window '
                         'the person was working in (click a client window after that to play in it)')
    pl.add_argument('--never-focus', action='store_true',
                    help='--no-focus for the whole session: a client window never keeps the keyboard focus or the cursor '
                         '(scripted test clients beside a person at work; such a window cannot be played in)')
    pl.add_argument('--tile-skip', type=int, default=0,
                    help="the first client window's cell on the tiling grid (two columns): for a second session beside a "
                         'running one, whose windows hold the cells before it')
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
    j.add_argument('--relay', metavar='HOST:PORT')
    j.add_argument('--server-file', type=Path, metavar='server.json',
                   help="hot join the game on a kit's server (x4ls serve) instead of --relay and --token")
    j.add_argument('--token')
    j.add_argument('--slot', type=int)
    j.add_argument('--start', metavar='GAMESTART',
                   help='hot join: take an open slot that plays this start instead of naming a slot (see --starts)')
    j.add_argument('--starts', action='store_true', help='list the open slots of --overlay with the start each one plays, and stop')
    j.add_argument('--clients', type=int, help='with --arm-at: the session\'s slots (a hot join reads them from the overlay)')
    j.add_argument('--arm-at', type=int, metavar='UTC_SECOND',
                   help='the UTC second the session loads at (a --remote-slots slot); without it: a hot join of an --open slot')
    j.add_argument('--overlay', type=Path, help='the same play overlay as the host (a copy of its folder); not with netplay\'s '
                                                '--code, --address, --game-id')
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
    j.add_argument('--late-boot', action='store_true',
                   help='hot join: start X4 only after the join files are here (the order before 23 Nov); by default X4 boots '
                        'to its start menu first and the session saves once it waits there')
    j.add_argument('--join-fetch-delay', type=float, default=0.0, metavar='S',
                   help='DIAGNOSTIC: an early start acts as if the download took S seconds longer (a slow link)')
    j.add_argument('--ask-first', action='store_true',
                   help='hot join, early start: ask for the save as X4 starts (in about 10 s sooner; the others wait a few seconds '
                        'longer at the handover); by default the save is asked once X4 waits in its start menu')
    j.add_argument('--display', type=int, help='keep the client window on this monitor only (as x4ls play --display)')
    j.add_argument('--no-focus', action='store_true', help='while the client starts its window gives the keyboard focus back')
    j.add_argument('--never-focus', action='store_true',
                   help='--no-focus for the whole session (a scripted test client beside a person at work)')
    j.add_argument('--tile-skip', type=int, default=0, help="the window's cell on the tiling grid (as x4ls play --tile-skip)")
    netcli.add_join_options(j)

    sv = sub.add_parser('serve', help='the server of a kit made with make_playtest_kit.py --server: admits the players who '
                                      'choose Join and runs the relay (no X4 needed; Linux or Windows)')
    sv.add_argument('--overlay', type=Path, required=True, help="the kit's play overlay")
    sv.add_argument('--server-file', type=Path, required=True, metavar='server.json', help="the kit's server.json (address, port, key)")
    sv.add_argument('--bind', default='0.0.0.0', help='the address the lobby (TCP port + 1) and the relay (UDP port) listen on')
    sv.add_argument('--load-wait', type=int, default=45, metavar='SECONDS', help='from the last player joining to the common load')
    sv.add_argument('--stall-drop', type=float, default=15, metavar='SECONDS')
    sv.add_argument('--idle-end', type=int, default=420, metavar='SECONDS', help='a game without a step for this long is over')
    sv.add_argument('--idle-left', type=int, default=60, metavar='SECONDS',
                    help='the same once the game has stepped: every player has left (default 60)')
    sv.add_argument('--runs', type=Path, help="where each game's relay files go (default: .local/runs)")
    sv.add_argument('--join-max-pause', type=int, default=0, metavar='SECONDS',
                    help="the longest pause a join may cost everyone (default: 25 s less than the clients' wait for the relay, at most 170)")
    sv.add_argument('--join-catchup', type=int, default=120, metavar='SECONDS',
                    help='how long a joiner catches up while the others play before the pause is placed (default 120)')
    sv.add_argument('--keep-joins', action='store_true', help="keep each game's join files (default: deleted when the game is over)")
    sv.add_argument('--games', type=int, default=0, help='test: end after this many games')

    ms = sub.add_parser('master', help='the central lobby server of netplay (docs/NETPLAY.md): join codes, public games, '
                                       'the UDP echo and a relay pool (no X4 needed; Linux or Windows)')
    ms.add_argument('--bind', default='0.0.0.0', help='the address the UDP echo and the relays listen on')
    ms.add_argument('--http-bind', default='',
                    help='the address the HTTP API listens on (default --bind; 127.0.0.1 behind a reverse proxy)')
    ms.add_argument('--http-port', type=int, default=47100)
    ms.add_argument('--echo-port', type=int, default=47101)
    ms.add_argument('--relay-ports', default='47200-47263', metavar='FIRST-LAST',
                    help='UDP (relay) and TCP (its join files) ports of the relay pool')
    ms.add_argument('--data', type=Path, required=True,
                    help="where the games' files and relay folders go (its games/ folder is emptied at the start)")
    ms.add_argument('--public-host', default='',
                    help='the IPv4 address players send UDP to (the echo and the relays); default: the address a request '
                         'reached. Needed behind a proxy or NAT')
    ms.add_argument('--public-prefix', default='', help="the HTML view's link prefix behind a proxy (e.g. /x4mp)")
    ms.add_argument('--trust-proxy', action='store_true',
                    help='the HTTP side is behind a reverse proxy on this machine: for connections from 127.0.0.1/::1 the '
                         'client IP is the last address of X-Forwarded-For')
    ms.add_argument('--relay-release', action='append', default=[], metavar='RELEASE',
                    help='a release the relay pool serves (repeatable; default: any). Other releases can still play direct')
    ms.add_argument('--games-max', type=int, default=200)
    ms.add_argument('--relays-max', type=int, default=32)
    ms.add_argument('--games-per-ip', type=int, default=4)
    ms.add_argument('--data-max-gib', type=float, default=8, help='every uploaded file together')

    lb = sub.add_parser('lobby', help='open X4 in its main menu and choose there: Multiplayer, Host a game or Join a game '
                                      '(netplay: a code, the public list, the LAN; a kit with a server: Join)')
    lb.add_argument('--overlay', type=Path, help='a play overlay without open slots (a playtest kit\'s; a release kit has none)')
    lb.add_argument('--snapshot', type=Path, help='the save the overlay was built for')
    netcli.add_netplay_options(lb)
    lb.add_argument('--request', default='', metavar='TEXT',
                    help="the page's first request, as if it had been chosen there (HOST|1|4|name, JOINCODE|K7QX-3FNA, ...)")
    lb.add_argument('--lane', type=int, default=0,
                    help='the local view and profile (default: the first free one, so a second start on one PC just works)')
    lb.add_argument('--load-wait', type=int, default=30, metavar='SECONDS',
                    help='from the last player joining to the common load (default 30)')
    lb.add_argument('--lobby-timeout', type=int, default=0, metavar='SECONDS', help='give up in the menu after this long (default: never)')
    lb.add_argument('--stall-drop', type=float, default=15, metavar='SECONDS')
    lb.add_argument('--server-file', type=Path, metavar='server.json',
                    help="a kit's server (make_playtest_kit.py --server): the page has Join only, x4ls serve runs the relay")
    lb.add_argument('--show', action='store_true', help='test: the Multiplayer page opens by itself')
    lb.add_argument('--auto', default='', metavar='host|join|start|CODE',
                    help='test: host, join the server, take the first free start of its running game, or join with this '
                         'invite code, without a click in the menu')
    lb.add_argument('--size', default='1280x720')
    lb.add_argument('--timeout', type=int, default=0)
    lb.add_argument('--close-when-over', action='store_true')
    lb.add_argument('--game', type=Path)
    lb.add_argument('--lanes', type=Path)
    lb.add_argument('--steam-fix', action='store_true')

    netcli.add_parsers(sub)

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
                    help="parts slot K's start leaves out (inventory, reputation, blueprints, research, known; world: only "
                         "that start's ship, place and kit, for a start x4ls starts lists as player-start=ship-only or ship+story)")
    ns.add_argument('--story', default='', metavar='K[,K...]',
                    help="slots whose start's own story (its missions) runs for that slot's player; for a start x4ls starts lists "
                         'as player-start=ship+story, taken with --omit K=world. First form, in test')
    ns.add_argument('--piloted', default='', metavar='K[,K...]',
                    help="slots whose start ship keeps an NPC pilot. The default is none, as a stock start's own ship: with one, "
                         'that pilot appears in the seat when the player gets up')
    ns.add_argument('--unpiloted', default='', metavar='K[,K...]', help='the default for every slot now; accepted for older scripts')
    ns.add_argument('--plot', action='append', default=[], metavar='K=PLOT[,PLOT]',
                    help="slot K runs its own copy of a stock plot, as its own player (hq: the player headquarters plot; "
                         "hq_ready: the same begun with the headquarters there, the mentor in it and research unlocked, as a "
                         "start that comes with one; each PC makes the copy from its own game files). Factions mode. "
                         "1=hq_ready: player 1 begins with the headquarters (in coop the one the players share)")
    ns.add_argument('--gate-dll', type=Path, required=True)
    ns.add_argument('--out', type=Path, required=True, help='new session folder (spec, save, session.json, overlay)')
    ns.add_argument('--minutes', type=float, default=40)
    ns.add_argument('--open', default='', metavar='K[,K...]', help='hot-joinable slots (x4ls play --open)')
    ns.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE')
    ns.add_argument('--game', type=Path)
    ns.add_argument('--lanes', type=Path)
    ns.add_argument('--timeout', type=int, default=900, help='seconds for the capture')
    ns.add_argument('--display', type=int, help="the capture client's window on this monitor (1 = primary)")
    ns.add_argument('--no-focus', action='store_true', help="the capture client's window never keeps the keyboard focus or the cursor")
    ns.add_argument('--steam-fix', action='store_true')

    rs = sub.add_parser('resume', help='a new session from the world of a periodic save (RUN/saves; src/lockstep/launcher/resume.py)')
    rs.add_argument('--save', type=Path, required=True, help='a periodic save of a play session (RUN/saves/c<K>-t<TICK>.xml.gz)')
    rs.add_argument('--spec', type=Path, required=True, help="that session's spec.json (x4ls new-session OUT/spec.json)")
    rs.add_argument('--overlay', type=Path, required=True, help='the play overlay that session ran with (its recipe is used again)')
    rs.add_argument('--gate-dll', type=Path, required=True)
    rs.add_argument('--out', type=Path, required=True, help='new session folder (capture overlay, save, session.json, overlay)')
    rs.add_argument('--minutes', type=float, help="length of the new session (default: the first session's)")
    rs.add_argument('--story', action='store_true', help="start the slots' stories again (default: the saved world keeps their state)")
    rs.add_argument('--save-every', type=float, metavar='MINUTES', help="periodic saves of the new session (default: the first session's)")
    rs.add_argument('--set', action='append', default=[], metavar='native_gate_KEY=LUA_VALUE', help='further settings')
    rs.add_argument('--keep-player', metavar='ID', help="the saved ID of X4's own player object, when the save holds several named ones")
    rs.add_argument('--display', type=int, help="the prepare client's window on this monitor (1 = primary)")
    rs.add_argument('--no-focus', action='store_true', help="the prepare client's window never keeps the keyboard focus or the cursor")
    rs.add_argument('--game', type=Path)
    rs.add_argument('--lanes', type=Path)
    rs.add_argument('--timeout', type=int, default=900, help='seconds for the prepare step')
    rs.add_argument('--steam-fix', action='store_true')

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
    if args.command == 'resume':
        from . import resume
        result = resume.resume(resume.ResumeOptions(save=args.save, spec=args.spec, overlay=args.overlay, gate_dll=args.gate_dll,
                                                    out=args.out, minutes=args.minutes, story=args.story, sets=args.set,
                                                    save_every=args.save_every, keep_player=args.keep_player, display=args.display,
                                                    focus=not args.no_focus, game=args.game, lanes=args.lanes,
                                                    timeout=args.timeout, steam_fix=args.steam_fix))
        return 0 if result['outcome'] == 'ready' else 1
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
            story=tuple(int(k) for k in args.story.split(',') if k.strip()),
            plots={k: [p for p in v.split(',') if p] for k, v in slots(args.plot, '--plot').items()},
            unpiloted=tuple(int(k) for k in args.unpiloted.split(',') if k.strip()),
            piloted=tuple(int(k) for k in args.piloted.split(',') if k.strip()),
            sets=args.set, game=args.game, lanes=args.lanes, timeout=args.timeout, steam_fix=args.steam_fix,
            display=args.display, focus=not args.no_focus))
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
    if args.command == 'serve':
        from . import serve
        serve.run(serve.ServeOptions(overlay=args.overlay, server_file=args.server_file, bind=args.bind, load_wait=args.load_wait,
                                     stall_drop=args.stall_drop, idle_end=args.idle_end, idle_left=args.idle_left, keep_joins=args.keep_joins, join_max_pause=args.join_max_pause,
                                     join_catchup=args.join_catchup, games=args.games,
                                     **({'runs': args.runs} if args.runs else {})))
        return 0
    if args.command == 'master':
        return run_master(args)
    if args.command == 'games':
        return netcli.run_games(args)
    width, height = (int(v) for v in args.size.lower().split('x'))
    if args.command == 'host':
        return netcli.run_lobby(netcli.lobby_options(args, width, height, netcli.host_request(args)))
    if args.command == 'join' and netcli.is_netplay_join(args):
        return netcli.run_lobby(netcli.lobby_options(args, width, height, netcli.join_request(args)))
    if args.command == 'join' and not args.overlay:
        raise SystemExit('x4ls join needs --overlay (or netplay\'s --code, --address or --game-id)')
    if args.command == 'lobby':
        from . import lobby
        result = lobby.run(lobby.LobbyOptions(overlay=args.overlay, snapshot=args.snapshot, port=args.port, bind=args.bind,
                                              lane=args.lane, load_wait=args.load_wait, width=width, height=height,
                                              game=args.game, lanes=args.lanes, steam_fix=args.steam_fix, timeout=args.timeout,
                                              close_when_over=args.close_when_over, stall_drop=args.stall_drop,
                                              lobby_timeout=args.lobby_timeout, auto=args.auto, show=args.show,
                                              server_file=args.server_file, kit=args.kit, master=args.master,
                                              no_master=args.no_master, request=args.request, state_mirror=args.state_mirror,
                                              base_start=args.base_start, mode=args.mode))
        print(json.dumps(result, indent=2, default=str))
        return 0 if result['outcome'] in ('played', 'closed') else 1
    if args.command == 'join' and args.starts:
        from . import hotjoin
        recipe = hotjoin.recipe_tools().read_recipe(args.overlay)
        if recipe is None:
            raise SystemExit(f'{args.overlay}: not a play overlay with a recipe')
        print(hotjoin.starts_text(recipe))
        return 0
    if args.command == 'join' and args.server_file:
        from . import lobby
        if (args.slot is None) == (args.start is None):
            raise SystemExit('x4ls join --server-file needs one of --slot K or --start GAMESTART')
        result = lobby.join_running(args.overlay, args.server_file, slot=args.slot, start=args.start, wait=args.wait,
                                    width=width, height=height, game=args.game, lanes=args.lanes, startup=args.startup,
                                    steam_fix=args.steam_fix, timeout=args.timeout, close_when_over=args.close_when_over,
                                    same_pc=args.same_pc,
                                    **({'state': session.ROOT / '.local/x4ls-join'} if args.same_pc else {}))
        print(json.dumps(result, indent=2))
        return 0 if result['outcome'] == 'played' else 1
    if args.command == 'join' and (not args.relay or not args.token or (args.slot is None) == (args.start is None)):
        raise SystemExit('x4ls join needs --relay, --token and one of --slot K or --start GAMESTART (x4ls join --overlay DIR --starts lists them)')
    if args.command == 'join' and args.start and args.arm_at is not None:
        raise SystemExit('x4ls join --start is for a hot join (no --arm-at)')
    if args.command == 'join' and args.arm_at is None:
        from . import hotjoin
        if args.snapshot or args.clients:
            raise SystemExit('a hot join (no --arm-at) takes no --snapshot or --clients: the join save and the slots come from the session')
        result = hotjoin.hot_join(hotjoin.HotJoinOptions(relay=args.relay, token=args.token, slot=args.slot, start=args.start,
                                                         overlay=args.overlay,
                                                         wait=args.wait, nonce=args.nonce, width=width, height=height, game=args.game,
                                                         lanes=args.lanes, startup=args.startup, steam_fix=args.steam_fix,
                                                         timeout=args.timeout, close_when_over=args.close_when_over,
                                                         same_pc=args.same_pc, display=args.display,
                                                         focus=not (args.no_focus or args.never_focus), never_focus=args.never_focus,
                                                         tile_skip=args.tile_skip, early=not args.late_boot, ask_first=args.ask_first,
                                                         fetch_delay=args.join_fetch_delay,
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
        ticks = args.ticks or (last + session.PLAY_TAIL_TICKS if last else None)
        if not ticks:
            raise SystemExit('the overlay has no pilot window: pass --ticks')
        if not lookahead:
            print('note: a protocol 3 overlay (no lookahead): the relay cannot drop a stopped client, the others would wait')
        options = session.Options(clients=args.clients, overlay=args.overlay, snapshot=args.snapshot, ticks=ticks, tick_hz=tick_hz,
                                  lookahead=lookahead, timeout=args.timeout, width=width, height=height, game=args.game,
                                  lanes=args.lanes, startup=args.startup, steam_fix=args.steam_fix, play=True,
                                  stall_drop_ms=1000 * args.stall_drop if lookahead else 0, relay_args=args.relay_args,
                                  close_when_over=args.close_when_over, borderless=args.borderless, display=args.display, focus=not (args.no_focus or args.never_focus), never_focus=args.never_focus, tile_skip=args.tile_skip,
                                  keep_saves=args.keep_saves,
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
