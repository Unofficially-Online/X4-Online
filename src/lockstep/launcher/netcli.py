"""x4ls's netplay commands for scripts and tests (docs/NETPLAY.md; docs/LAUNCHER.md "Netplay"):

  x4ls host --players N [--name NAME] [--public] [lobby options]   host a game (x4ls lobby with HOST|... chosen)
  x4ls join --code CODE | --address HOST[:PORT][,...] | --game-id ID [lobby options]   join one
  x4ls games [--lan] [--json]                                       the public games (and the LAN's), no X4

host and join are x4ls lobby with the page's first request already made: the same X4, page and flow as a click in
the Multiplayer menu (--show opens the page, so what happens can be watched)."""
import json
from pathlib import Path

from . import lobby, netkit, netplay

LIST_LAN_S = 2.5


def add_netplay_options(parser, with_port=True):
    """The options x4ls lobby, host and join share for netplay."""
    parser.add_argument('--kit', type=Path, help='the kit folder (default: the kit around this checkout, if any)')
    parser.add_argument('--master', default='', metavar='URL',
                        help='the master server (default: X4MP_MASTER, else master.json beside the kit)')
    parser.add_argument('--no-master', action='store_true', help='LAN games only: invite codes, addresses, LAN discovery')
    parser.add_argument('--state-mirror', type=Path, metavar='FILE', help='write the page\'s state here too')
    parser.add_argument('--base-start', default='', metavar='GAMESTART',
                        help=f'hosting with a release kit: the start of the session made here (default {netkit.HOST_SESSION_START})')
    parser.add_argument('--mode', choices=('coop', 'factions'), default='', help='hosting: the new session\'s mode (default coop)')
    if with_port:
        parser.add_argument('--port', type=int, default=netplay.RELAY_UDP_PORT,
                            help="the relay's UDP port when hosting; the lobby listens on TCP port + 1")
        parser.add_argument('--bind', default='',
                            help='hosting: the address put into invite codes (listeners bind every interface anyway)')


def add_lobby_options(parser):
    """The menu and session options of x4ls host (x4ls join has its own of the same names)."""
    parser.add_argument('--lane', type=int, default=0, help='the local view and profile (default: the first free one)')
    parser.add_argument('--load-wait', type=int, default=30, metavar='SECONDS')
    parser.add_argument('--lobby-timeout', type=int, default=0, metavar='SECONDS')
    parser.add_argument('--stall-drop', type=float, default=15, metavar='SECONDS')
    parser.add_argument('--show', action='store_true', help='the Multiplayer page opens by itself')
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--timeout', type=int, default=0)
    parser.add_argument('--close-when-over', action='store_true')
    parser.add_argument('--game', type=Path)
    parser.add_argument('--lanes', type=Path)
    parser.add_argument('--steam-fix', action='store_true')


def add_parsers(sub):
    host = sub.add_parser('host', help='host a netplay game: x4ls lobby with Host chosen (the master server, else the LAN)')
    host.add_argument('--players', type=int, default=2, help='2..8')
    host.add_argument('--name', default='', help='the game\'s name in the public list (at most 40 bytes)')
    host.add_argument('--public', action='store_true', help='list the game publicly and announce it on the LAN')
    add_netplay_options(host)
    add_lobby_options(host)
    games = sub.add_parser('games', help='list the public games of the master server (and the LAN\'s with --lan); no X4')
    games.add_argument('--kit', type=Path)
    games.add_argument('--master', default='', metavar='URL')
    games.add_argument('--no-master', action='store_true')
    games.add_argument('--lan', action='store_true', help='also listen for LAN announcements (2.5 s)')
    games.add_argument('--json', action='store_true')


def add_join_options(join):
    """x4ls join's netplay forms (the relay, server and hot join forms stay as they are)."""
    join.add_argument('--code', help='netplay: a join code (K7QX-3FNA) or a long LAN invite code (X4MP-...)')
    join.add_argument('--address', metavar='HOST[:PORT][,...]',
                      help='netplay: the host\'s address (LAN, VLAN, VPN); the port is its lobby (TCP, default 47001)')
    join.add_argument('--game-id', metavar='ID', help='netplay: a game of the public list (x4ls games)')
    add_netplay_options(join, with_port=False)
    join.add_argument('--lane', type=int, default=0)
    join.add_argument('--show', action='store_true')
    join.add_argument('--lobby-timeout', type=int, default=0, metavar='SECONDS')
    join.add_argument('--stall-drop', type=float, default=15, metavar='SECONDS')


def is_netplay_join(args):
    return bool(getattr(args, 'code', None) or getattr(args, 'address', None) or getattr(args, 'game_id', None))


def join_request(args):
    chosen = [value for value in (args.code, args.address, args.game_id) if value]
    if len(chosen) != 1:
        raise SystemExit('x4ls join takes one of --code, --address or --game-id')
    if args.game_id:
        return f'JOINGAME|{args.game_id}'
    return f'JOINCODE|{args.code or args.address}'


def host_request(args):
    if not netplay.PLAYERS_MIN <= args.players <= netplay.PLAYERS_MAX:
        raise SystemExit(f'--players must be {netplay.PLAYERS_MIN}..{netplay.PLAYERS_MAX}')
    public = '1' if args.public else '0'
    return f'HOST|{public}|{args.players}|{netplay.clean_name(args.name)}'


def lobby_options(args, width, height, request):
    """x4ls lobby's options for x4ls host or join (the page's first request made)."""
    if netplay.parse_request(request) is None:
        raise SystemExit(f'not a request: {request}')
    return lobby.LobbyOptions(kit=args.kit, master=args.master, no_master=args.no_master, state_mirror=args.state_mirror,
                              base_start=args.base_start, mode=args.mode, port=getattr(args, 'port', netplay.RELAY_UDP_PORT),
                              bind=getattr(args, 'bind', ''), lane=args.lane, load_wait=getattr(args, 'load_wait', 30),
                              lobby_timeout=args.lobby_timeout, stall_drop=args.stall_drop, show=args.show, width=width,
                              height=height, timeout=args.timeout, close_when_over=args.close_when_over, game=args.game,
                              lanes=args.lanes, steam_fix=args.steam_fix, request=request)


def run_lobby(options):
    result = lobby.run(options)
    print(json.dumps(result, indent=2, default=str))
    return 0 if result.get('outcome') in ('played', 'closed') else 1


def list_games(kit, master_url, lan=False, net=None, log=print):
    """The public games of the master server (and the LAN's): rows of dicts, as x4ls games prints them."""
    net = net or netplay.Network()
    rows = []
    module = net.get('masterclient')
    if master_url and module is not None:
        client = module.MasterClient(master_url)
        try:
            games = client.games(kit.release)
        except netplay.master_error_types(net) as error:
            log(f'the master server does not answer: {netplay.error_why(error)}')
            games = []
        for game in games:
            if isinstance(game, dict):
                rows.append({'where': 'public', **netplay.game_row(game, kit.release)})
    elif master_url:
        log('this checkout has no master client (package 2): no public list')
    if lan:
        landisco = net.get('landisco')
        found = landisco.scan(LIST_LAN_S) if landisco is not None else []
        for index, entry in enumerate(found[:netplay.LAN_SHOWN_MAX], 1):
            row = netplay.lan_row(index, entry, kit.release)
            row.update(where='lan', code=str(entry.get('code') or '-'), candidates=list(entry.get('candidates') or []),
                       tcp_port=entry.get('tcp_port'))
            rows.append(row)
    return rows


def run_games(args, net=None):
    from . import netlobby
    kit = netkit.Kit(args.kit or netkit.kit_root_of_checkout())
    options = lobby.LobbyOptions(kit=args.kit, master=args.master, no_master=args.no_master)
    url = netlobby.master_url(options, kit)
    rows = list_games(kit, url, args.lan, net)
    if args.json:
        print(json.dumps(rows, indent=2))
        return 0
    if not url:
        print('no master server is configured (X4MP_MASTER, master.json or --master)')
    for row in rows:
        compatible = 'yes' if row['compatible'] else 'another release'
        if row['where'] == 'public':
            print(f"{row['id']:12} {row['name'][:40]:40} {row['players']}/{row['max']} {row['state']:8} "
                  f"{row.get('mode') or '-':6} {compatible}")
        else:
            print(f"lan {row['index']:<8} {row['name'][:40]:40} {row['players']}/{row['max']} code {row['code']:9} "
                  f"{','.join(row['candidates'])}:{row['tcp_port']} {compatible}")
    if not rows:
        print('no games')
    return 0
