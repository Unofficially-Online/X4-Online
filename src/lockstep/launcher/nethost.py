"""HOST: the session to host, the game at the master server or on the LAN, the start, and the host's part during the
session (docs/NETPLAY.md "Starting a game", "Port mapping and reachability", "VLANs").

With a master server: the ports are mapped on the router (nat.map_ports, renewed by nat.keep_mapped) and the master's
probe says whether this PC is reachable (nat.reachable). Reachable: the game is registered with direct ports and, when
the master starts it in direct mode, the relay runs on this PC (bound to every interface; it allows the addresses the
players checked in from at this PC's lobby). Not reachable: the master runs the relay (relay mode). The game's code
goes on the clipboard; a public game is announced on the LAN with its code (landisco.Announcer).

Without a master (none configured, or it does not answer): the LAN lobby, with a long invite code (X4MP-...) for a
private game, and announced on the LAN for a public one. A joiner downloads the session from this PC's file service.
"""
import secrets
import threading
import time
from pathlib import Path

from . import lobby, netdropin, netfiles, netkit, netlan, netplay

MAPPING_LEASE_S = 3600
START_ANSWER_WAIT_S = 60       # after START, the master's start reaches the lobby answer within this
LOOP_S = 0.2
SESSION_LOOP_S = 0.5     # during the session: late check-ins, the players for the in-session page
MAPPING_NAMES = {'upnp': 'upnp', 'upnp-igd': 'upnp', 'natpmp': 'natpmp', 'nat-pmp': 'natpmp', 'pcp': 'pcp'}


class HostJob(netplay.Job):
    """One hosted game, from the session to the end of its session."""

    def run(self):
        legacy = self.request.players == 0
        if self.request.open:   # drop-in games play on the master's relay (it keeps the join files for the joiners)
            master, note = self.flow.master()
            if master is None:
                raise netplay.JobFailed('Drop-in slots need the master server (it relays the game and keeps the join files).',
                                        note or 'Host without drop-in slots for a game on the local network.')
        files = self.prepare(legacy)
        players = files.players
        opened = drop_in(files)
        start_players = players - len(opened)
        name = self.request.name or (f'{start_players}+{len(opened)}-player game' if opened else f'{players}-player game')
        public = self.request.public and not legacy
        self.show('preparing', 'Starting X4 with the session...')
        self.flow.backend.install_session(files, self.session_progress)
        self.check()
        lobby_host = self.open_lobby(files, public)
        master = None
        note = ''
        if not legacy or opened:
            master, note = self.flow.master()
        self.check()
        if master is None and opened:
            raise netplay.JobFailed('This session has drop-in slots, which need the master server.',
                                    note or 'The master server does not answer.')
        if master is not None:
            start, game = self.host_master(master, files, lobby_host, name, public)
        else:
            start, game = self.host_lan(files, lobby_host, name, public, note)
        self.flow.begin_session(start)
        self.in_session(start, game, lobby_host)

    # the session
    def prepare(self, legacy):
        """The session to host: the kit's own (a playtest kit), else one made here for the players asked for."""
        self.show('preparing', 'Getting the session ready...', progress={'what': 'session', 'done': 0.0})
        own = self.flow.backend.kit_session()
        if legacy:
            if own is None:
                return self.flow.backend.host_session(netplay.PLAYERS_MIN, self.session_progress, self.cancelled)
            return own
        total = self.request.players + self.request.open
        opened = netplay.drop_in_slots(self.request.players, self.request.open)
        if own is not None and own.players == total and drop_in(own) == opened:
            return own
        if own is not None:
            raise netplay.JobFailed(f'This kit\'s session is made for {counts_text(own.players, drop_in(own))}, not '
                                    f'{counts_text(total, opened)}.', f'Host a game for {counts_text(own.players, drop_in(own))}.')
        if opened:
            return self.flow.backend.host_session(total, self.session_progress, self.cancelled, open_slots=opened)
        return self.flow.backend.host_session(total, self.session_progress, self.cancelled)

    def session_progress(self, done):
        self.progress('session', done)

    def open_lobby(self, files, public):
        """The host's TCP lobby (port + 1, every interface) and the relay's allow list beside it."""
        port = self.config.port
        allow = netlan.AllowList(self.flow.backend.work_folder() / 'allow.txt')
        try:
            lobby_host = netlan.GameHost(port, files.players, self.config.release, self.config.kit.kit_id, allow,
                                         self.flow.log, public=public)
        except OSError as error:
            raise netplay.JobFailed(f'Cannot host: TCP port {port + 1} is taken ({error}).',
                                    'Close the other program or the other launcher that uses it.') from None
        self.on_end(lobby_host.close)
        return lobby_host

    # with the master
    def host_master(self, master, files, lobby_host, name, public):
        port = self.config.port
        tcp_port = port + 1
        errors = netplay.master_error_types(self.net)
        opened = drop_in(files)
        if opened:   # the master's relay: nothing reaches this PC from outside, no ports to open
            mapping, direct = None, False
            network = {'mode': 'relay', 'mapping': 'none', 'public_ip_known': False, 'drop_in': len(opened)}
        else:
            self.show('preparing', 'Opening the game\'s ports on the router...')
            mapping = self.map_ports(port, tcp_port)
            reach = self.reachable(master, port, tcp_port)
            direct = bool(reach.get('tcp') and reach.get('udp'))
            network = {'mode': 'direct' if direct else 'relay', 'mapping': mapping_name(mapping, direct),
                       'public_ip_known': bool(reach.get('ip'))}
        if mapping is not None and not direct and netplay.firewall_allowed() is False:
            network['firewall'] = 'blocked'   # no rule for this launcher's Python: the page offers FIREWALL
        known, ping = self.flow.master_ping(master)
        network['public_ip_known'] = network['public_ip_known'] or known
        if ping is not None:
            network['ping_ms'] = ping
        lan = {'candidates': self.lan_candidates(), 'udp_port': port, 'tcp_port': tcp_port}
        self.check()
        self.show('preparing', 'Registering the game at the master server...', network=network)
        try:
            created = master.create_game(name, public, self.config.release, files.players, files.sha256,
                                         open_slots=opened, want='relay' if opened else 'auto',
                                         direct={'udp_port': port, 'tcp_port': tcp_port} if direct else None, lan=lan)
        except errors as error:
            raise netplay.JobFailed('The master server did not take the game:', netplay.error_why(error)) from None
        game = checked_created(created)
        self.on_end(lambda: self.end_game(master, game))
        game['beat'] = 'lobby'
        beat = netplay.Heartbeat(master, game['game_id'], game['secret'], errors, self.flow.log,
                                 state=lambda: (game['beat'], self.session_tick()))
        self.on_end(beat.stop)
        self.upload(master, game, files)
        lobby_host.game_id = game['game_id']
        lobby_host.code = game['code']
        self.flow.copy_text = game['code']
        self.flow.backend.clipboard_set(game['code'])
        self.announce(public, game['code'], name, files.players)
        watch = netplay.LobbyWatch(master, game['game_id'], game['host_ticket'], errors, self.flow.log)
        self.on_end(watch.stop)
        game.update(master=master, watch=watch, network=network, mapping=mapping, players_max=files.players,
                    public=public, open=opened)
        start = self.wait_for_master_start(game, lobby_host, files)
        return start, game

    def map_ports(self, port, tcp_port):
        nat = self.net.get('nat')
        if nat is None:
            return None
        try:
            mapping = nat.map_ports(port, tcp_port, lease=MAPPING_LEASE_S, log=self.flow.log)
        except OSError as error:
            self.flow.log(f'netplay: no port mapping: {error}')
            return None
        if mapping is None:
            return None
        stop = threading.Event()
        self.on_end(lambda: release_mapping(mapping, self.flow.log))
        self.on_end(stop.set)
        nat.keep_mapped(mapping, stop)
        self.flow.log(f'netplay: ports mapped by {mapping.method}: UDP {mapping.udp_port}, TCP {mapping.tcp_port}')
        return mapping

    def reachable(self, master, port, tcp_port):
        """The master's probe of this PC's ports (TCP connect to the lobby, one UDP datagram to the relay's port)."""
        nat = self.net.get('nat')
        if nat is None:
            return {}
        errors = (ValueError, TypeError) + netplay.master_error_types(self.net)
        try:
            answer = nat.reachable(master, port, tcp_port)
        except errors as error:
            self.flow.log(f'netplay: the reachability probe failed: {netplay.error_why(error)}')
            return {}
        return answer if isinstance(answer, dict) else {}

    def lan_candidates(self):
        """Every usable IPv4 address of every up interface (netifs.addresses): the LAN candidates of this host."""
        netifs = self.net.get('netifs')
        if netifs is None:
            return [lobby.local_address()]
        try:
            rows = netifs.addresses()
        except OSError as error:
            self.flow.log(f'netplay: the interfaces could not be listed: {error}')
            return [lobby.local_address()]
        found = [str(row.get('ip')) for row in rows if isinstance(row, dict) and netplay.session.IPV4.fullmatch(str(row.get('ip')))]
        return list(dict.fromkeys(found)) or [lobby.local_address()]

    def upload(self, master, game, files):
        errors = netplay.master_error_types(self.net)
        paths = netkit.upload_files(files, self.flow.backend.work_folder() / 'upload')
        size = sum(Path(path).stat().st_size for path in paths.values())
        self.show('preparing', f'Uploading the session for the other players ({size >> 20} MB)...')
        for name, path in paths.items():
            self.check()
            try:
                master.upload(game['game_id'], game['secret'], name, path)
            except errors as error:
                raise netplay.JobFailed(f'The upload of {name} failed:', netplay.error_why(error)) from None

    def announce(self, public, code, name, players):
        """A public game is announced on every network this PC is on (landisco); a private one is not."""
        landisco = self.net.get('landisco')
        if not public or landisco is None or not self.config.lan_discovery:
            return None
        try:
            announcer = landisco.Announcer(self.config.release, code or '-', name, self.config.port + 1, 1, players)
            announcer.start()
        except OSError as error:
            self.flow.log(f'netplay: no LAN announcement: {error}')
            return None
        self.on_end(announcer.stop)
        self.announcer = announcer
        return announcer

    def wait_for_master_start(self, game, lobby_host, files):
        """hosting: the players come (the master's lobby), START or a full game starts it; the master's start."""
        master = game['master']
        watch = game['watch']
        asked_start = 0.0
        while True:
            self.check()
            answer = watch.latest()
            if watch.gone():
                raise netplay.JobFailed('The master server no longer holds this game.')
            players = netplay.player_rows(answer, [{'slot': 1, 'ready': True, 'host': True}])
            lobby_host.checkin_slots = {row['slot'] for row in players if not row['host']}
            lobby_host.poll()
            self.update_announcer(len(players))
            # the common load's slots (all but the drop-in ones) must be ready; the drop-in players join later
            ready = [row for row in players if not row['host'] and row['ready'] and row['slot'] not in game['open']]
            full = len(ready) == game['players_max'] - len(game['open']) - 1
            start = (answer or {}).get('start')
            if start:
                return self.master_start(netplay.checked_start(start), game, lobby_host, files)
            command = self.command()
            if command is not None and command.kind == 'LEAVE':
                raise netplay.Cancelled()
            if command is not None and command.kind == 'START':
                game['start_asked'] = True
            # A session of N players needs N clients at the common load: the game starts once every slot is ready
            # (START before that only says so). The master's start then reaches the lobby answer.
            if full and time.time() - asked_start > START_ANSWER_WAIT_S:
                asked_start = time.time()
                self.ask_start(master, game)
            self.show('hosting', *hosting_lines(game, players, full), code=game['code'], public=game['public'],
                      host=True, can_start=full, network=game['network'], players=players)
            self.sleep(LOOP_S)

    def ask_start(self, master, game):
        try:
            master.start(game['game_id'], game['secret'])
        except netplay.master_error_types(self.net) as error:
            self.flow.log(f'netplay: the master did not start the game: {netplay.error_why(error)}')

    def master_start(self, start, game, lobby_host, files):
        if game['open'] and start['mode'] != 'relay':
            raise netplay.JobFailed('The master server started this drop-in game without its relay.',
                                    'Drop-in joins need the master\'s relay: host it again.')
        game['network'] = dict(game['network'], mode=start['mode'])
        if start['mode'] == 'direct':
            plan = netplay.RelayPlan(port=start['port'], token=start['token'], allow_file=lobby_host.allow.path,
                                     allow_from=tuple(sorted(lobby_host.allow.addresses)))
            relay_host = '127.0.0.1'   # this PC's own game sends to its own relay
        else:
            plan = None
            relay_host = netplay.resolve_ipv4(start['host'])
        self.flow.log(f'netplay: the game starts in {start["mode"]} mode, relay {relay_host}:{start["port"]}, '
                      f'load at {start["load_at"]}')
        return netplay.SessionStart(slot=1, clients=files.players, relay_host=relay_host, port=start['port'],
                                    token=start['token'], arm_at=start['load_at'], overlay=files.overlay,
                                    snapshot=files.snapshot, mode=start['mode'], relay=plan)

    def update_announcer(self, players):
        announcer = getattr(self, 'announcer', None)
        if announcer is not None:
            announcer.update(players=players)

    def end_game(self, master, game):
        try:
            master.delete(game['game_id'], game['secret'])
        except netplay.master_error_types(self.net) as error:
            self.flow.log(f'netplay: the game was not deleted at the master: {netplay.error_why(error)}')

    # without the master
    def host_lan(self, files, lobby_host, name, public, note, typed=True):
        port = self.config.port
        token = secrets.token_hex(16)
        lobby_host.token = token
        address = self.config.advertise or lobby.local_address()
        invite = lobby.invite(address, port, token)
        self.flow.copy_text = invite
        if typed:
            self.flow.backend.clipboard_set(invite)
        self.offer_files(files, lobby_host, token)
        self.announce(public, '-', name, files.players)
        self.flow.log(f'netplay: hosting on the LAN, UDP {port} (the session) and TCP {port + 1} (the lobby); invite code:\n  {invite}')
        game = {'invite': invite, 'address': address, 'public': public, 'players_max': files.players,
                'network': {'mode': 'lan', 'mapping': 'none'}, 'code': invite, 'note': note}
        while True:
            self.check()
            lobby_host.poll()
            self.update_announcer(len(lobby_host.players) + 1)
            command = self.command()
            if command is not None and command.kind == 'LEAVE':
                raise netplay.Cancelled()
            full = lobby_host.all_ready()
            if full:
                break
            self.show('hosting', *lan_lines(game, lobby_host), code=invite, invite=invite, public=public, host=True,
                      can_start=False, network=game['network'], players=lobby_host.rows())
            self.sleep(LOOP_S)
        arm_at = int(time.time()) + self.config.load_wait
        addresses = lobby_host.start(arm_at, token)
        time.sleep(0.5)   # the start lines leave before anything closes
        plan = netplay.RelayPlan(port=port, token=token, allow_file=lobby_host.allow.path,
                                 allow_from=tuple(dict.fromkeys(('127.0.0.1', *addresses))))
        start = netplay.SessionStart(slot=1, clients=files.players, relay_host='127.0.0.1', port=port, token=token,
                                     arm_at=arm_at, overlay=files.overlay, snapshot=files.snapshot, mode='lan', relay=plan)
        game['players'] = lobby_host.rows()
        return start, game

    def offer_files(self, files, lobby_host, token):
        """The session files a LAN joiner of another kit downloads: served on TCP at the relay's port number."""
        paths = netkit.upload_files(files, self.flow.backend.work_folder() / 'upload')
        try:
            # allow is the lobby's live set: an admitted joiner's address may fetch from then on
            service = netfiles.FileHost(token, self.config.port, lobby_host.allow.addresses, range(2, files.players + 1))
        except OSError as error:
            self.flow.log(f'netplay: no session file service on TCP {self.config.port}: {error}')
            return
        for name, path in paths.items():
            service.offer(name, path)
        service.start()
        self.on_end(service.close)
        lobby_host.files = netkit.described_files(paths)
        lobby_host.file_port = service.port

    # during the session
    def in_session(self, start, game, lobby_host):
        """The session runs: the page's state keeps the players, the code and the network; late check-ins are taken,
        the master hears a heartbeat. Ends when the backend says the session is over. Until the backend has written
        the session's config the page hears 'loading' (it runs the config on 'session'); the players check in at this
        PC's lobby exactly then, so the lobby is followed all the while."""
        left = False
        if game.get('open') and start.mode == 'relay' and game.get('watch') is not None:
            self.send_joins(game)
        while not self.flow.session_over.is_set():
            if self.cancelled.is_set():
                return
            if game.get('watch') is not None:
                answer = game['watch'].latest()
                players = netplay.player_rows(answer, game.get('players') or [])
                lobby_host.checkin_slots = {row['slot'] for row in players if not row['host']}
                game['beat'] = 'session'   # what the heartbeat tells the master from now on
            else:
                players = game.get('players') or lobby_host.rows()
            lobby_host.poll()
            command = self.command()
            if command is not None and command.kind == 'LEAVE':
                left = True
                self.flow.log('netplay: the host left its game; the relay goes on for the others until the session ends')
            fields = {'code': game.get('code'), 'public': game.get('public'), 'host': True, 'network': game.get('network'),
                      'players': players, 'slot': 1, 'arm_at': start.arm_at}
            if not self.flow.session_started.is_set():
                seconds = max(0, start.arm_at - int(time.time()))
                self.show('loading', f'The game loads in {seconds} s: you are player 1 of {start.clients}, the host.',
                          session_network_line(start, game), **fields)
                self.sleep(LOOP_S)
                continue
            lines = ['You left the session; it goes on for the others.' if left else f'You are player 1 of {start.clients}, the host.',
                     session_network_line(start, game)]
            if game.get('open'):
                lines.append(drop_in_session_line(game, players))
            self.show('session', *lines, **fields)
            self.sleep(SESSION_LOOP_S)

    def send_joins(self, game):
        """This PC saves the drop-in players' joins (the relay asks the common load): their files go to the master."""
        folder = self.flow.backend.save_folder()
        if folder is None:
            self.flow.log('netplay: this launcher has no save folder: the drop-in joins cannot be sent from here')
            return
        sender = netdropin.JoinSender(game['master'], game['game_id'], game['secret'], folder, game['watch'],
                                      netplay.master_error_types(self.net), self.flow.log)
        self.on_end(sender.stop)
        sender.start()

    def session_tick(self):
        reader = self.flow.session_info.get('tick')
        try:
            return max(0, int(reader())) if callable(reader) else 0   # the relay says -1 before its first step
        except (OSError, ValueError, TypeError):
            return 0


def drop_in(files):
    """The session's drop-in slots (the recipe's open slots), sorted."""
    return tuple(sorted(int(slot) for slot in files.recipe.get('open') or ()))


def counts_text(players, opened):
    """'3 players' or '1 player and 2 drop-in slots'."""
    start = players - len(opened)
    text = f'{start} player' + ('' if start == 1 else 's')
    if opened:
        text += f' and {len(opened)} drop-in slot' + ('' if len(opened) == 1 else 's')
    return text


def checked_created(created):
    """create_game's answer, checked: {game_id, code, secret, host_ticket}."""
    if not isinstance(created, dict):
        raise netplay.JobFailed('The master server sent no game.')
    game = {key: str(created.get(key) or '') for key in ('game_id', 'code', 'secret', 'host_ticket')}
    if not netplay.GAME_ID.fullmatch(game['game_id']) or not game['code'] or not netplay.HEX32.fullmatch(game['secret']):
        raise netplay.JobFailed('The master server\'s answer is not one this launcher understands (game, code, secret).')
    if not netplay.HEX32.fullmatch(game['host_ticket']):
        raise netplay.JobFailed('The master server\'s answer has no host ticket.')
    return game


def release_mapping(mapping, log):
    try:
        mapping.release()
    except OSError as error:
        log(f'netplay: the port mapping was not deleted: {error}')


def mapping_name(mapping, direct):
    """The state's network.mapping: how this PC is reachable (or 'none')."""
    if mapping is not None:
        method = str(getattr(mapping, 'method', '') or '').lower()
        return MAPPING_NAMES.get(method, method or 'upnp')
    return 'manual' if direct else 'none'


def network_line(network):
    mode = network.get('mode')
    if mode == 'direct':
        how = {'upnp': 'UPnP', 'natpmp': 'NAT-PMP', 'pcp': 'PCP', 'manual': 'a forwarded port'}.get(network.get('mapping'), 'its ports')
        line = f'Direct: the other players reach this PC ({how}).'
    elif mode == 'relay' and network.get('mapping') not in (None, 'none', 'manual'):
        # the router opened the ports, yet the master's probe did not reach this PC: a firewall on the PC (Windows Defender
        # Firewall has never been told about a release's own python.exe; the live run of 23 Nov)
        line = 'Through the server\'s relay: a firewall on this PC blocks the ports the router opened.'   # Allow: FIREWALL
    elif mode == 'relay' and network.get('drop_in'):
        line = 'Through the master server\'s relay (drop-in players join there).'
    elif mode == 'relay':
        line = 'Through the master server\'s relay (this PC is not reachable from outside).'
    else:
        line = 'On the local network.'
    if network.get('ping_ms') is not None:
        line += f' Ping to the server {network["ping_ms"]} ms.'
    return line


def hosting_lines(game, players, full):
    opened = game.get('open') or ()
    if opened:
        return drop_in_lines(game, players, full)
    joined = len(players)
    ready = sum(1 for row in players if row['ready'])
    lines = [f'Join code: {game["code"]} (on the clipboard). Send it to the others.',
             f'{"Public" if game["public"] else "Private"} game, {joined} of {game["players_max"]} players here, {ready} ready.',
             network_line(game['network'])]
    if full:
        lines.append('Every player is ready: the game loads in a moment.')
    elif game.get('start_asked'):
        lines.append(f'Start waits for every player: this session is made for {game["players_max"]}.')
    else:
        lines.append(f'The game loads when all {game["players_max"]} players are ready.')
    return lines


def drop_in_lines(game, players, full):
    """A drop-in game before its start: the common load's players, then the drop-in slots."""
    opened = game['open']
    start = game['players_max'] - len(opened)
    here = [row for row in players if row['slot'] not in opened]
    ready = sum(1 for row in here if row['ready'])
    lines = [f'Join code: {game["code"]} (on the clipboard). Send it to the others.',
             f'{"Public" if game["public"] else "Private"} game, {len(here)} of {start} players here, {ready} ready; '
             f'{len(opened)} drop-in slot{"" if len(opened) == 1 else "s"} for players who come later.',
             network_line(game['network'])]
    if full:
        lines.append('The game loads in a moment; drop-in players join it while it runs.')
    else:
        lines.append(f'The game loads when all {start} players are ready.')
    return lines


def drop_in_session_line(game, players):
    """During a drop-in game: how many drop-in slots are taken."""
    opened = game.get('open') or ()
    taken = sum(1 for row in players if row['slot'] in opened)
    return f'Drop-in: {taken} of {len(opened)} slot{"" if len(opened) == 1 else "s"} taken; the code still works for them.'


def lan_lines(game, lobby_host):
    names = ', '.join(f'player {slot}' for slot in sorted(lobby_host.players)) or 'nobody yet'
    lines = [f'Hosting on {game["address"]} (UDP {lobby_host.port}, TCP {lobby_host.port + 1}).',
             f'Invite code (on the clipboard): {game["invite"]}',
             f'Joined: {names} ({len(lobby_host.players) + 1} of {lobby_host.clients}).',
             'The game loads when every player has joined and is ready.']
    if game.get('note'):
        lines.append(game['note'])
    return lines


def session_network_line(start, game):
    if start.mode == 'lan':
        return f'On the local network; the relay runs on this PC (UDP {start.port}).'
    if start.mode == 'direct':
        return f'Direct: the relay runs on this PC (UDP {start.port}).'
    return 'Through the master server\'s relay.'
