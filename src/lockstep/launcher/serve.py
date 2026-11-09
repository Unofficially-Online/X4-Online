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
import secrets
import subprocess
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


def run(o: ServeOptions, log=print):
    recipe = json.loads((Path(o.overlay) / 'play-recipe.json').read_text(encoding='utf-8'))
    clients = int(recipe['players'])
    if recipe.get('open'):
        raise SystemExit('x4ls serve takes an overlay without open slots (every player is there at the start)')
    _, port, key = lobby.read_server_file(o.server_file)
    lookahead, tick_hz, last = session.play_settings(o.overlay, clients)
    if not last or not lookahead:
        raise SystemExit('the overlay has no pilot window or no lookahead: build it with tools/build_play_overlay.py')
    kit = lobby.kit_id(o.overlay, recipe.get('snapshot_sha'))
    admissions = lobby.Host(o.bind, port, None, clients, kit, log, key=key)
    log(f'serve: kit {kit[:12]}, {clients} players, lobby on TCP {o.bind}:{port + 1}, relay on UDP {o.bind}:{port}')
    played, games = 0, {}   # games: a game's id (the salt of its token) -> its folder

    admissions.upload_dir = lambda game: (games[game] / 'players') if game in games else o.runs / 'uploads'
    try:
        while not o.games or played < o.games:
            while not admissions.poll():
                time.sleep(0.2)
            arm_at, salt = int(time.time()) + o.load_wait, secrets.token_hex(16)
            addresses = admissions.start(arm_at, salt)
            run_dir = o.runs / time.strftime('%Y%m%d-%H%M%S-serve')
            run_dir.mkdir(parents=True)
            games[salt] = run_dir
            play = session.Options(clients=clients, overlay=o.overlay, snapshot=Path('snapshot'),   # a snapshot session
                                   ticks=last + 3, tick_hz=tick_hz, lookahead=lookahead, play=True,
                                   stall_drop_ms=1000 * o.stall_drop, port=port,
                                   remote_slots=tuple(range(1, clients + 1)), bind=o.bind, allow_from=addresses)
            with (run_dir / 'relay.log').open('w') as relay_log:
                relay = subprocess.Popen(session.relay_command(play, port, lobby.session_token(key, salt), run_dir),
                                         cwd=session.ROOT, stdout=relay_log, stderr=subprocess.STDOUT)
                log(f'serve: game {played + 1}: players from {", ".join(addresses)}, loads at {arm_at}, relay pid {relay.pid}, {run_dir}')
                time.sleep(0.5)   # the start lines leave before the sockets close
                admissions.release()
                admissions.busy = 'a game is running on the server; join again when it is over'
                seen, ended = [None, time.time()], 'the relay ended'
                try:
                    while relay.poll() is None:
                        admissions.poll()   # joiners are told that a game is running
                        time.sleep(0.5)
                        if idle_seconds(run_dir / 'desync-status.json', seen, time.time()) > o.idle_end:
                            ended = f'no step for {o.idle_end} s'
                            break
                finally:
                    if relay.poll() is None:
                        relay.kill()
                        relay.wait(10)
            admissions.busy = None
            played += 1
            if relay.returncode and ended == 'the relay ended':   # it stopped by itself and not at the session's end: say why
                tail = (run_dir / 'relay.log').read_text(errors='replace').strip().splitlines()[-3:]
                ended = f'the relay ended with code {relay.returncode}: ' + ' | '.join(tail)
            status = {}
            try:
                status = json.loads((run_dir / 'desync-status.json').read_text())
            except (OSError, ValueError):
                pass
            log(f'serve: game {played} over ({ended}): step {status.get("tick")}, desync {status.get("desync")}, '
                f'mismatches {status.get("mismatches")}')
    finally:
        admissions.close()
    return played
