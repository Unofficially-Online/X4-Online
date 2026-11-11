"""x4ls serve: a server every player joins from X4's main menu (no host among the players, no invite code).

    python3 tools/x4ls.py serve --overlay DIR --server-file server.json [--bind 0.0.0.0]

Runs on a PC or server the players reach (Linux or Windows; it needs Python and this checkout, no X4). It holds the
kit's play overlay and server.json (tools/make_playtest_kit.py --server ADDRESS writes both into the kit):

- It listens for the players' launchers on TCP port + 1 (x4ls lobby --server-file, the page's Join). A launcher
  proves the kit's key by answering a nonce and names its kit; another build is refused with the reason.
- When as many players are there as the overlay has slots, the server picks the load second, starts the relay
  (tools/dev/native_gate_relay.py on UDP port, for those players' addresses only, with a token made from the key and a
  fresh salt) and tells every launcher. From here on it is the relay of x4ls play: every slot is a remote one.
- While the game runs, a joiner is told so. When the relay ends (the session's last tick, nobody left, or no step for
  --idle-end seconds), the server takes players for the next game.

Each game's files are in --runs/<time>-serve/: relay.log, native-barrier.json (the report), desync-status.json, and
players/<time>-playerK-<address>.zip, the run folder every launcher sends when its game is over (summary.json,
errors.log, the game's log; no saves). Logs of a launcher that never got into a game are in --runs/uploads/.
The players' launchers have no relay of their own, so the desync count of a game is here."""
import json
import re
import secrets
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path

from . import lobby, session


@dataclass
class ServeOptions:
    overlay: Path
    server_file: Path
    bind: str = '0.0.0.0'      # the address the lobby and the relay listen on
    load_wait: int = 45        # seconds from the last player's arrival to the common load
    stall_drop: float = 15
    idle_end: int = 420        # a game whose relay releases no step for this long is over (the load takes 2 to 3 minutes)
    runs: Path = session.ROOT / '.local/runs'
    games: int = 0             # test: end after this many games (0: serve until stopped)


def idle_seconds(status_file, seen, now):
    """Seconds since the relay's step last moved; `seen` is [step, time of its change]."""
    try:
        step = json.loads(Path(status_file).read_text()).get('tick')
    except (OSError, ValueError):
        step = None
    if step != seen[0]:
        seen[:] = step, now
    return now - seen[1]


def session_done(status_file):
    """True once the relay has released the session's last step. The relay goes on serving after that and does not
    end by itself (x4ls play ends it when its clients stopped), so the server ends the game on this."""
    try:
        return bool(json.loads(Path(status_file).read_text()).get('done'))
    except (OSError, ValueError):
        return False


class RelayLog:
    """The relay's lines as it writes them: its log file, read on from where the last read ended."""
    def __init__(self, path):
        self.path, self.offset, self.tail = Path(path), 0, ''

    def lines(self):
        try:
            with self.path.open('r', errors='replace') as f:
                f.seek(self.offset)
                data = f.read()
                self.offset = f.tell()
        except OSError:
            return []
        *rows, self.tail = (self.tail + data).split('\n')
        return rows


JOIN_ROW = re.compile(r'NATIVE_GATE JOIN slot=(\d+) (\w+)')
TAKEN = ('requested', 'queued', 'saved', 'catching_up', 'handover', 'joined')


class DropIn:
    """One drop-in game's joins on the server: who may send to the relay, which open slots are taken, and the join files
    (a saver's launcher hands them over, joinfiles.FileService serves them to the joiner on the relay's port, TCP)."""
    def __init__(self, admissions, run_dir, token, bind, port, open_slots, addresses, log):
        from . import joinfiles
        self.admissions, self.run_dir, self.log = admissions, Path(run_dir), log
        self.allow_file, self.allowed = self.run_dir / 'allow.txt', {'127.0.0.1', *addresses}
        self.allow_file.write_text('')
        (self.run_dir / 'join').mkdir()
        self.taken, self.asked, self.lock = {}, {}, threading.Lock()   # slot -> state; tick -> (slot, saver, package sha)
        self.relay_log = RelayLog(self.run_dir / 'relay.log')
        self.service = joinfiles.FileService(token, bind, port, slots=open_slots)
        self.service.allow = self.allowed   # a live set: a late joiner's address is added when the server admits it
        self.service.start()

    def admit(self, address):
        """A late joiner may send to the relay and fetch the join files."""
        with self.lock:
            if address not in self.allowed:
                self.allowed.add(address)
                with self.allow_file.open('a', encoding='ascii') as f:
                    f.write(address + '\n')

    def poll(self):
        """Follow the relay: a saved join's files are asked from its saver's launcher."""
        from . import hotjoin
        for line in self.relay_log.lines():
            row = JOIN_ROW.match(line)
            if not row:
                continue
            slot, state = int(row.group(1)), row.group(2)
            if state in TAKEN:
                self.taken[slot] = state
            elif state == 'failed':
                self.taken.pop(slot, None)
            self.log(f'serve: relay: {line.strip()[:200]}')
            saved = hotjoin.SAVED.match(line)
            if saved:
                saver, tick, sha = int(saved.group(2)), int(saved.group(3)), saved.group(5)
                self.asked[tick] = (slot, saver, sha)
                player = self.admissions.players.get(saver)
                if not player:
                    self.log(f'serve: join slot {slot}: saver {saver} has no launcher connected; its files cannot be fetched')
                    continue
                try:
                    lobby.send(player[0].sock, {'send_join': {'tick': tick, 'slot': slot}})
                except OSError as error:
                    self.log(f'serve: join slot {slot}: saver {saver} could not be asked ({error})')

    def stored(self, path):
        """A join file arrived (an upload thread): once a tick's package and save are both here, verify and serve them."""
        from . import hotjoin
        tick = int(re.match(r'j(\d+)', path.name).group(1))
        package, save = path.with_name(f'j{tick}.x4join'), path.with_name(f'j{tick}.xml.gz')
        with self.lock:
            if tick not in self.asked or not (package.is_file() and save.is_file()):
                return
            slot, saver, sha = self.asked.pop(tick)
        join = hotjoin.join_tools()
        try:
            loaded = join.read_package(package.read_bytes())
            if loaded['sha'] != sha:
                raise ValueError(f'the package is {loaded["sha"][:16]}.., the relay announced {sha[:16]}..')
            self.service.offer(package.name, package, sha)
            size, _ = self.service.offer(save.name, save, loaded['header']['save_sha'])
            self.log(f'serve: join slot {slot}: serving j{tick} from saver {saver} ({(package.stat().st_size + size) >> 20} MB)')
        except (OSError, ValueError, join.JoinError) as error:
            self.log(f'serve: join slot {slot}: the saver\'s files are not served ({error})')

    def close(self):
        self.service.close()


def run(o: ServeOptions, log=print):
    recipe = json.loads((Path(o.overlay) / 'play-recipe.json').read_text(encoding='utf-8'))
    clients = int(recipe['players'])
    open_slots = sorted(int(k) for k in recipe.get('open') or [])
    present = [k for k in range(1, clients + 1) if k not in open_slots]   # the slots of the common load
    _, port, key = lobby.read_server_file(o.server_file)
    lookahead, tick_hz, last = session.play_settings(o.overlay, clients)
    if not last or not lookahead:
        raise SystemExit('the overlay has no pilot window or no lookahead: build it with tools/build_play_overlay.py')
    kit = lobby.kit_id(o.overlay, recipe.get('snapshot_sha'))
    admissions = lobby.Host(o.bind, port, None, clients, kit, log, key=key)
    starts = []
    if open_slots:   # a drop-in game: the common load takes the slots that are not open, the others are joined while it runs
        from . import hotjoin
        admissions.slots = present
        starts = hotjoin.open_starts(recipe)
    log(f'serve: kit {kit[:12]}, {clients} players' + (f' ({len(present)} to start, open slots {open_slots})' if open_slots else '')
        + f', lobby on TCP {o.bind}:{port + 1}, relay on UDP {o.bind}:{port}')
    played, games = 0, {}   # games: a game's id (the salt of its token) -> its folder
    live = {'salt': None, 'joins': None}
    host_sector = (recipe.get('session') or {}).get('player_sector')

    def status():
        """What a launcher's page shows before anyone clicks: is a game running, who is there, which starts are free."""
        joins = live['joins']
        taken = dict(joins.taken) if joins else {}
        tick = 0
        if joins:
            try:
                tick = int(json.loads((joins.run_dir / 'desync-status.json').read_text()).get('tick') or 0)
            except (OSError, ValueError, TypeError):
                pass
        return {'running': live['salt'] is not None, 'clients': clients, 'here': len(admissions.players) + len(taken), 'tick': tick,
                'drop_in': bool(open_slots), 'joinable': bool(joins and admissions.players),   # the savers' launchers are connected
                'base': (recipe.get('session') or {}).get('base_start') or '',
                'starts': [{'slot': k, 'start': name, 'sector': sector or '', 'taken': k in taken,
                            'near': not own or not host_sector or sector == host_sector}   # a far start: another sector than the host's
                           for k, name, sector, own in starts]}

    def running(address):
        live['joins'].admit(address)
        return {'salt': live['salt'], 'port': port, 'starts': status()['starts']}

    admissions.status = status
    admissions.upload_dir = lambda game: (games[game] / 'players') if game in games else o.runs / 'uploads'
    try:
        while not o.games or played < o.games:
            while not admissions.poll():
                time.sleep(0.2)
            arm_at, salt = int(time.time()) + o.load_wait, secrets.token_hex(16)
            addresses = admissions.start(arm_at, salt, drop_in=bool(open_slots))
            run_dir = o.runs / time.strftime('%Y%m%d-%H%M%S-serve')
            run_dir.mkdir(parents=True)
            games[salt] = run_dir
            token = lobby.session_token(key, salt)
            play = session.Options(clients=clients, overlay=o.overlay, snapshot=Path('snapshot'),   # a snapshot session
                                   ticks=last + session.PLAY_TAIL_TICKS, tick_hz=tick_hz, lookahead=lookahead, play=True,
                                   stall_drop_ms=1000 * o.stall_drop, port=port,
                                   remote_slots=tuple(range(1, clients + 1)), bind=o.bind, allow_from=addresses)
            command = session.relay_command(play, port, token, run_dir)
            joins = None
            if open_slots:
                joins = DropIn(admissions, run_dir, token, o.bind, port, open_slots, addresses, log)
                command += ['--allow-file', str(joins.allow_file), *hotjoin.relay_join_args(open_slots, present, observe=True)]
            with (run_dir / 'relay.log').open('w') as relay_log:
                relay = subprocess.Popen(command, cwd=session.ROOT, stdout=relay_log, stderr=subprocess.STDOUT)
                log(f'serve: game {played + 1}: players from {", ".join(addresses)}, loads at {arm_at}, relay pid {relay.pid}, {run_dir}')
                time.sleep(0.5)   # the start lines leave before the sockets close
                if joins:   # the players' launchers stay connected: they hand the join files over
                    live.update(salt=salt, joins=joins)
                    admissions.join_dir = lambda game: (run_dir / 'join') if game == salt else None
                    admissions.join_done, admissions.running = joins.stored, running
                else:
                    admissions.release()
                    admissions.busy = 'a game is running on the server; join again when it is over'
                seen, ended = [None, time.time()], 'the relay ended'
                try:
                    while relay.poll() is None:
                        admissions.poll()   # joiners are told that a game is running, or admitted to it
                        if joins:
                            joins.poll()
                        time.sleep(0.5 if not joins else 0.2)
                        if idle_seconds(run_dir / 'desync-status.json', seen, time.time()) > o.idle_end:
                            ended = f'no step for {o.idle_end} s'
                            break
                        if session_done(run_dir / 'desync-status.json'):
                            ended = 'the session is over'
                            for _ in range(20):   # the launchers close their games and send their logs to this game's folder
                                admissions.poll()
                                time.sleep(0.5)
                            break
                finally:
                    if relay.poll() is None:
                        relay.kill()
                        relay.wait(10)
                    if joins:
                        joins.close()
            live.update(salt=None, joins=None)
            admissions.busy = admissions.running = admissions.join_dir = admissions.join_done = None
            admissions.release()
            played += 1
            if relay.returncode and ended == 'the relay ended':   # it stopped by itself and not at the session's end: say why
                tail = (run_dir / 'relay.log').read_text(errors='replace').strip().splitlines()[-3:]
                ended = f'the relay ended with code {relay.returncode}: ' + ' | '.join(tail)
            status_file = {}
            try:
                status_file = json.loads((run_dir / 'desync-status.json').read_text())
            except (OSError, ValueError):
                pass
            log(f'serve: game {played} over ({ended}): step {status_file.get("tick")}, desync {status_file.get("desync")}, '
                f'mismatches {status_file.get("mismatches")}')
    finally:
        admissions.close()
    return played
