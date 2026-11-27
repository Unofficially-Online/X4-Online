"""JOIN, JOINCODE, JOINGAME, JOINLAN: find the game, get its session, build the overlay here, ready, the start, and the
joiner's part during the session (docs/NETPLAY.md "Starting a game", "VLANs", "Trust and safety rules").

A game of the master server (a join code, a public game, a LAN game that carries a code): the master's join gives a
slot, a ticket and the three session files; they are downloaded (resumed, SHA-256 checked), checked against each other
and against this release, and the overlay is built here from the host's recipe with this kit's own gate DLL. Then
READY and the master's start. In direct mode this PC checks in at the host's lobby first, trying the host's LAN
candidates on this PC's subnet first (the master names them only when both are behind one public address), then the
others, then the host's public address; the address that took the check-in is the relay's.

A LAN game (a long invite code, an address, a LAN game without a code): the host's lobby admits this PC, names the
session files its file service offers (netfiles.py), and sends the start on the same connection. A joiner with the
host's own playtest kit needs no download."""
from pathlib import Path

from . import game as x4game   # "game" is the joined game in this module
from . import lobby, netdropin, netfiles, netkit, netlan, netplay

ANSWER_WAIT_S = 10            # the host's lobby answers a hello within this
ONE_ADDRESS_TIMEOUT_S = 8     # a typed address or an invite code may be across the internet or a VPN
JOIN_FILE_TRIES = 5           # a join right after the host registered may come before its uploads
JOIN_FILE_PAUSE_S = 3
LOOP_S = 0.2


class JoinJob(netplay.Job):
    """One joined game, from the code or address to the end of its session."""

    def run(self):
        kind, value, entry = self.flow.join_plan(self.request)
        if kind == 'invalid':
            raise netplay.JobFailed(value)
        if kind == 'game':
            self.join_master(game_id=value, listed=entry)
        elif value.kind == 'code':
            self.join_master(code=value.code, hint=value.addresses)
        else:
            self.join_lan(value)

    # ---- a game of the master server ----
    def join_master(self, code=None, game_id=None, listed=None, hint=()):
        master, game, files = self.master_files(code, game_id, listed)
        if not game['late']:
            self.install(files)   # a drop-in joiner's X4 is started by the hot join, with the join save
        self.ready(game)
        start = self.wait_for_master_start(game, files, hint)
        self.flow.begin_session(start)
        self.in_session(start, game)

    def master_files(self, code=None, game_id=None, listed=None):
        """The game at the master joined (a ticket and a slot; left again when the job ends) and its session here: the
        files downloaded and checked, the overlay built. (master, game, files)."""
        master, note = self.flow.master()
        if master is None:
            raise netplay.JobFailed('Join codes and public games need the master server.',
                                    note or 'This kit names no master server: join with an invite code or an address.')
        info = self.look_up(master, code, game_id, listed)
        self.check_release(info)
        joined = self.master_join(master, info['game_id'], code)
        game = {'master': master, 'game_id': info['game_id'], 'ticket': joined['ticket'], 'slot': joined['slot'],
                'name': netplay.clean_name(info.get('name') or 'the game'), 'code': code or '', 'info': info,
                'players_max': int(info.get('players_max') or 0), 'beat': 'lobby',
                'network': {'mode': str(info.get('mode') or '') or None}}
        self.on_end(lambda: self.leave(game))
        game['heartbeat'] = netplay.Heartbeat(master, game['game_id'], game['ticket'], netplay.master_error_types(self.net),
                                              self.flow.log, state=lambda: (game['beat'], 0))
        self.flow.copy_text = code or ''
        files = self.master_session(master, game, joined['files'])
        if not 2 <= game['slot'] <= files.players:
            raise netplay.JobFailed(f'The master server gave slot {game["slot"]}, which this session does not have.')
        game['open'] = tuple(sorted(int(slot) for slot in files.recipe.get('open') or ()))
        game['late'] = game['slot'] in game['open']   # a drop-in slot: joined while the game runs (hotjoin)
        return master, game, files

    def look_up(self, master, code, game_id, listed):
        errors = netplay.master_error_types(self.net)
        self.show('joining', f'Looking up {code}...' if code else 'Looking up the game...')
        try:
            if code:
                info = master.code(code)
            else:
                info = listed or self.find_listed(master, game_id)
        except errors as error:
            raise netplay.JobFailed('The master server does not know this game:', netplay.error_why(error)) from None
        if not isinstance(info, dict) or not netplay.GAME_ID.fullmatch(str(info.get('game_id') or '')):
            raise netplay.JobFailed('The master server sent no game for this code.')
        return info

    def find_listed(self, master, game_id):
        """A public game by its id (JOINGAME without a list in this process: the Steam helper's hand-over)."""
        for game in master.games(self.config.release):
            if isinstance(game, dict) and str(game.get('game_id')) == game_id:
                return game
        return {'game_id': game_id}

    def check_release(self, info):
        """Another release cannot be joined (NETPLAY.md): its overlay would not be this PC's."""
        theirs = str(info.get('release') or '')
        mine = self.config.release
        if (theirs and theirs != mine) or info.get('compatible') is False:
            raise netplay.JobFailed(f'This game is from another release ({theirs[:40] or "unknown"}); this PC has {mine[:40]}.',
                                    'Every player needs the same release: install the host\'s.')

    def master_join(self, master, game_id, code):
        """The master's join (a ticket, a slot, the session files); again when the host has not uploaded them all."""
        errors = netplay.master_error_types(self.net)
        self.show('joining', 'Joining the game...')
        for attempt in range(JOIN_FILE_TRIES):
            self.check()
            try:
                joined = master.join(game_id, self.config.release, code=code)
            except errors as error:
                raise netplay.JobFailed('The game cannot be joined:', netplay.error_why(error)) from None
            ticket = str((joined or {}).get('ticket') or '')
            slot = (joined or {}).get('slot')
            files = (joined or {}).get('files')
            if not netplay.HEX32.fullmatch(ticket) or not isinstance(slot, int) or not isinstance(files, dict):
                raise netplay.JobFailed('The master server\'s answer to the join is not one this launcher understands.')
            if all(isinstance(files.get(name), dict) for name in netkit.SESSION_FILES):
                return {'ticket': ticket, 'slot': slot, 'files': files}
            self.leave({'master': master, 'game_id': game_id, 'ticket': ticket})
            self.sleep(JOIN_FILE_PAUSE_S)
        raise netplay.JobFailed('The host has not uploaded its session yet: try again in a moment.')

    def master_session(self, master, game, files):
        """The session's files downloaded (or found from an earlier game), checked, and the overlay built here."""
        folder = netkit.joined_folder(self.config.kit, files[netkit.RECIPE]['sha256'], files[netkit.SESSION_SAVE]['sha256'])
        folder.mkdir(parents=True, exist_ok=True)
        errors = netplay.master_error_types(self.net)
        total = sum(int(files[name].get('size') or 0) for name in netkit.SESSION_FILES) or 1
        before = 0
        for name in (netkit.RECIPE, netkit.SESSION_JSON, netkit.SESSION_SAVE):
            spec = files[name]
            size = int(spec.get('size') or 0)
            dest = folder / name
            if not (dest.is_file() and netkit.sha256_file(dest) == spec.get('sha256')):
                self.show('preparing', f'Downloading the session from the master server ({total >> 20} MB)...',
                          progress={'what': 'download', 'done': before / total})
                try:
                    master.download(game['game_id'], game['ticket'], name, dest, spec.get('sha256'), size,
                                    progress=byte_progress(self, 'download', before, total, size))
                except errors as error:
                    raise netplay.JobFailed(f'The download of {name} failed:', netplay.error_why(error)) from None
            before += size
        return self.checked_session(folder, files)

    def checked_session(self, folder, expected):
        recipe = netkit.check_session_files(folder, netkit.sha256_file(self.config.kit.gate_dll), expected)
        self.check_dlcs(Path(folder) / netkit.SESSION_SAVE)
        self.show('preparing', 'Building the overlay for this game, here, with this PC\'s own release...',
                  progress={'what': 'overlay', 'done': 0.0})
        return netkit.joined_session(self.config.kit, folder, recipe, progress=lambda done: self.progress('overlay', done),
                                     log=self.flow.log)

    def check_dlcs(self, save):
        """The host's world and this PC's X4 run the same official DLCs (docs/NETPLAY.md "Same DLCs"), else the join stops
        here, before X4 starts: a world loaded with a DLC more or less is another world from its first tick. The save's
        own <patches> header names the host's set; nothing is checked when either side is unknown."""
        installed = self.flow.backend.installed_dlcs()
        used = x4game.save_dlcs(save) if installed is not None else None
        if used is None:
            return
        missing, extra, other = x4game.dlc_mismatch(used, installed)
        if missing:
            raise netplay.JobFailed(f'The host\'s game uses DLC this PC does not have: {x4game.dlc_names(missing)}.',
                                    'Every player needs the same DLCs: install them in Steam, then join again.')
        if extra:
            raise netplay.JobFailed(f'This PC has DLC the host\'s game does not use: {x4game.dlc_names(extra)}.',
                                    'Every player needs the same DLCs: turn them off for X4 in Steam (Properties, DLC), '
                                    'or the host installs them and hosts a new game.')
        if other:
            raise netplay.JobFailed(f'Another version of {x4game.dlc_names(other)} than the host\'s.',
                                    'Every player needs the same DLC versions: let Steam update X4, then join again.')

    def install(self, files):
        self.show('preparing', 'X4 starts with the session (about a minute)...', progress={'what': 'session', 'done': 0.0})
        self.flow.backend.install_session(files, lambda done: self.progress('session', done))
        self.check()

    def ready(self, game):
        try:
            game['master'].ready(game['game_id'], game['ticket'])
        except netplay.master_error_types(self.net) as error:
            raise netplay.JobFailed('The master server did not take this player as ready:', netplay.error_why(error)) from None

    def wait_for_master_start(self, game, files, hint):
        errors = netplay.master_error_types(self.net)
        watch = netplay.LobbyWatch(game['master'], game['game_id'], game['ticket'], errors, self.flow.log)
        self.on_end(watch.stop)
        game['watch'] = watch
        known, ping = self.flow.master_ping(game['master'])
        if ping is not None:
            game['network']['ping_ms'] = ping
        game['network']['public_ip_known'] = known
        while True:
            self.check()
            if watch.gone():
                raise netplay.JobFailed('The game is gone: the host left or cancelled it.')
            answer = watch.latest()
            start = (answer or {}).get('start')
            if start:
                break
            command = self.command()
            if command is not None and command.kind == 'LEAVE':
                raise netplay.Cancelled()
            if command is not None and command.kind == 'READY':
                self.ready(game)
            players = netplay.player_rows(answer)
            ready = sum(1 for row in players if row['ready'])
            last = ('You join the game once it runs (a drop-in slot).' if game.get('late')
                    else 'The game loads when every player is ready.')
            self.show('waiting', f'Joined {game["name"]} as player {game["slot"]} of {files.players}.',
                      f'{len(players)} of {files.players} players here, {ready} ready.',
                      last, code=game['code'] or None, host=False,
                      network=game['network'], players=players, slot=game['slot'],
                      game={'name': game['name'], 'players_max': int(files.players)})
            self.sleep(LOOP_S)
        start = netplay.checked_start(start)
        game['network'] = dict(game['network'], mode=start['mode'])
        if game.get('late') and start['mode'] != 'relay':
            raise netplay.JobFailed('This game runs on the host\'s relay, which takes no drop-in players yet.',
                                    'Ask the host for a game with drop-in slots at the master server.')
        relay_host = self.relay_address(start, game, hint)
        self.flow.log(f'netplay: the game starts in {start["mode"]} mode: player {game["slot"]}, relay {relay_host}:'
                      f'{start["port"]}, ' + ('a drop-in join' if game.get('late') else f'load at {start["load_at"]}'))
        return netplay.SessionStart(slot=game['slot'], clients=files.players, relay_host=relay_host, port=start['port'],
                                    token=start['token'], arm_at=start['load_at'], overlay=files.overlay,
                                    snapshot=files.snapshot, mode=start['mode'], late=bool(game.get('late')))

    def relay_address(self, start, game, hint):
        """Relay mode: the master's relay. Direct mode: the host's address that takes this PC's check-in."""
        if start['mode'] != 'direct':
            return netplay.resolve_ipv4(start['host'])
        lan = game['info'].get('lan') if isinstance(game['info'].get('lan'), dict) else {}
        tcp_port = int(lan.get('tcp_port') or start['port'] + 1)
        candidates = [str(ip) for ip in lan.get('candidates') or [] if netplay.session.IPV4.fullmatch(str(ip))]
        addresses = [(ip, tcp_port) for ip in netplay.order_candidates(candidates, self.net)]
        addresses += [address for address in hint if address not in addresses]
        if start['host']:
            addresses.append((start['host'], tcp_port))
        self.show('loading', 'The game starts: reaching the host...', network=game['network'])
        reached = netlan.check_in(addresses, game['game_id'], game['slot'], log=self.flow.log)
        if reached is None:
            raise netplay.JobFailed('This PC cannot reach the host directly (its lobby did not answer on any address).',
                                    'Ask the host to check its firewall, or to host again (the master then picks its relay).')
        return netplay.resolve_ipv4(reached)

    def leave(self, game):
        if game.get('left'):
            return
        game['left'] = True
        if game.get('heartbeat') is not None:
            game['heartbeat'].stop()
        try:
            game['master'].leave(game['game_id'], game['ticket'])
        except netplay.master_error_types(self.net) as error:
            self.flow.log(f'netplay: leaving the game at the master failed: {netplay.error_why(error)}')

    def in_session(self, start, game):
        """The session runs: the state keeps the players and the network for the in-session page; LEAVE says so to
        the master (the game's own Leave stops this PC's gate). A common-load player of a drop-in game may be asked
        to save a join (netdropin.JoinSender)."""
        self.wait_until_running(start, code=game.get('code') or None, host=False, network=game.get('network'))
        if game.get('open') and not start.late and start.mode == 'relay' and game.get('watch') is not None:
            self.send_joins(game)
        while not self.flow.session_over.is_set():
            if self.cancelled.is_set():
                return
            command = self.command()
            if command is not None and command.kind == 'LEAVE' and game.get('master') is not None:
                self.leave(game)
            game['beat'] = 'session'   # what the heartbeat tells the master from now on
            watch = game.get('watch')
            players = netplay.player_rows(watch.latest() if watch else None, game.get('players') or [])
            first = ('You left the session; it goes on for the others.' if game.get('left')
                     else f'You are player {start.slot} of {start.clients}' + (', joined while it runs.' if start.late else '.'))
            self.show('session', first, session_line(start), code=game.get('code') or None, host=False,
                      network=game['network'], players=players, slot=start.slot, arm_at=start.arm_at)
            self.sleep(1.0)

    def send_joins(self, game):
        folder = self.flow.backend.save_folder()
        if folder is None:
            return
        sender = netdropin.JoinSender(game['master'], game['game_id'], game['ticket'], folder, game['watch'],
                                      netplay.master_error_types(self.net), self.flow.log)
        self.on_end(sender.stop)
        sender.start()

    # ---- a LAN game ----
    def join_lan(self, target):
        joined = self.lan_files(target)
        if joined is None:   # the host's game is the master's: joined by its code (refused)
            return
        sock, lines, game, files = joined
        self.install(files)
        lobby.send(sock, {'ready': True})
        start = self.wait_for_lan_start(lines, game, files)
        sock.close()
        self.flow.begin_session(start)
        self.in_session(start, game)

    def lan_files(self, target):
        """Admitted at the host's lobby (a slot; the connection closes when the job ends, which frees it) and the session
        here: (socket, its lines, game, files), or None when the host's game is the master's and was joined by its code."""
        self.invite_token = target.token   # an invite code's; a public game's host sends its own
        first = target.addresses[0]
        self.show('joining', f'Joining {first[0]}...')
        timeout = ONE_ADDRESS_TIMEOUT_S if len(target.addresses) == 1 else netlan.CONNECT_TIMEOUT_S
        try:
            sock, address, tcp_port = netlan.connect_first(target.addresses, timeout, self.flow.log)
        except OSError as error:
            raise netplay.JobFailed(f'Could not reach the host at {first[0]}:{first[1]}.', str(error)[:netplay.LINE_MAX_CHARS]) from None
        self.on_end(sock.close)
        hello = {'join': 1, 'release': self.config.release, 'kit': self.config.kit.kit_id, 'token': target.token or None}
        try:
            lines, answer = netlan.ask(sock, hello, ANSWER_WAIT_S)
        except OSError as error:
            raise netplay.JobFailed(f'The host at {address} did not answer.', str(error)) from None
        if not isinstance(answer, dict):
            raise netplay.JobFailed(f'The host at {address} did not answer.')
        if not answer.get('ok'):
            self.refused(answer, sock, address, tcp_port)
            return None
        game = self.admitted(answer, address)
        files = self.lan_session(answer, game, address)
        if not 2 <= game['slot'] <= files.players:
            raise netplay.JobFailed(f'The host gave slot {game["slot"]}, which this session does not have.')
        return sock, lines, game, files

    def refused(self, answer, sock, address, tcp_port):
        """A refusal; one that names a code is a master game: joined by its code, this host's address first."""
        nat = self.net.get('nat')
        code = nat.normalize_code(str(answer.get('code'))) if nat is not None and answer.get('code') else None
        if code:
            sock.close()
            self.flow.log(f'netplay: {address} hosts a game of the master server: joining it by its code')
            self.join_master(code=code, hint=((address, tcp_port),))
            return
        raise netplay.JobFailed('The host refused:', str(answer.get('why') or 'no reason given')[:netplay.LINE_MAX_CHARS])

    def admitted(self, answer, address):
        slot = answer.get('slot')
        clients = answer.get('clients')
        token = str(answer.get('token') or self.invite_token)
        if not isinstance(slot, int) or not isinstance(clients, int) or not netplay.HEX32.fullmatch(token):
            raise netplay.JobFailed('The host\'s answer is not one this launcher understands.')
        self.flow.log(f'netplay: joined {address} as player {slot} of {clients}')
        return {'slot': slot, 'clients': clients, 'token': token, 'address': address, 'code': '',
                'network': {'mode': 'lan', 'mapping': 'none'}, 'name': address}

    def lan_session(self, answer, game, address):
        """The host's own playtest kit: this PC has the session. Otherwise the host's file service has it."""
        own = self.flow.backend.kit_session()
        if own is not None and answer.get('kit') and answer.get('kit') == self.config.kit.kit_id:
            self.check_dlcs(own.snapshot)
            return own
        files = answer.get('files')
        port = answer.get('file_port')
        if not isinstance(files, dict) or not isinstance(port, int) or not all(isinstance(files.get(name), dict) for name in netkit.SESSION_FILES):
            raise netplay.JobFailed('The host offers no session files for this kit.')
        folder = netkit.joined_folder(self.config.kit, files[netkit.RECIPE]['sha256'], files[netkit.SESSION_SAVE]['sha256'])
        total = sum(int(files[name].get('size') or 0) for name in netkit.SESSION_FILES) or 1
        before = 0
        for name in (netkit.RECIPE, netkit.SESSION_JSON, netkit.SESSION_SAVE):
            spec = files[name]
            size = int(spec.get('size') or 0)
            dest = folder / name
            if not (dest.is_file() and netkit.sha256_file(dest) == spec.get('sha256')):
                self.show('preparing', f'Downloading the session from the host ({total >> 20} MB)...',
                          progress={'what': 'download', 'done': before / total})
                try:
                    netfiles.fetch(address, port, game['token'], game['slot'], name, dest, str(spec.get('sha256')),
                                   progress=byte_progress(self, 'download', before, total, size))
                except (netfiles.FetchError, OSError) as error:
                    raise netplay.JobFailed('The download from the host failed:', str(error)[:netplay.LINE_MAX_CHARS]) from None
            before += size
        return self.checked_session(folder, files)

    def wait_for_lan_start(self, lines, game, files):
        players = []
        while True:
            self.check()
            for message in lines.poll():
                if not isinstance(message, dict):
                    continue
                if isinstance(message.get('start'), dict):
                    return self.lan_start(message['start'], game, files)
                if 'here' in message:
                    players = netplay.player_rows(message, players)
                    game['players'] = players
            if lines.closed:
                raise netplay.JobFailed('The host closed the game.')
            command = self.command()
            if command is not None and command.kind == 'LEAVE':
                raise netplay.Cancelled()
            here = len(players) or '?'
            self.show('waiting', f'Joined {game["address"]} as player {game["slot"]} of {files.players}.',
                      f'{here} of {files.players} players are here.', 'The game loads when every player is ready.',
                      host=False, network=game['network'], players=players, slot=game['slot'])
            self.sleep(LOOP_S)

    def lan_start(self, start, game, files):
        arm_at = start.get('arm_at')
        port = start.get('port')
        slot = start.get('slot', game['slot'])
        token = str(start.get('token') or game['token'])
        if not isinstance(arm_at, int) or not isinstance(port, int) or not 0 < port < 65536 or slot != game['slot'] \
                or not netplay.HEX32.fullmatch(token):
            raise netplay.JobFailed('The host\'s start is not one this launcher can use.')
        # the address this PC reached the host's lobby at is the one its relay answers on (the host binds every one)
        return netplay.SessionStart(slot=slot, clients=files.players, relay_host=game['address'], port=port, token=token,
                                    arm_at=arm_at, overlay=files.overlay, snapshot=files.snapshot, mode='lan')


def byte_progress(job, what, before, total, size):
    """A download's progress callback for the page: it takes (done, size) bytes, (name, done, size), or a fraction of
    this file, whichever the downloader passes, and a cancel ends the download."""
    def report(*values):
        numbers = [value for value in values if isinstance(value, (int, float)) and not isinstance(value, bool)]
        if len(numbers) >= 2:
            done = numbers[-2]
        elif len(numbers) == 1 and numbers[0] <= 1:
            done = numbers[0] * size
        else:
            done = numbers[0] if numbers else 0
        job.progress(what, (before + done) / total)
        job.check()
    return report


def session_line(start):
    if start.mode == 'lan':
        return f'On the local network: the host\'s relay at {start.relay_host}.'
    if start.mode == 'direct':
        return f'Direct to the host ({start.relay_host}).'
    return 'Through the master server\'s relay.'


__all__ = ['JoinJob', 'byte_progress']
