"""Drop-in games of the master server (docs/NETPLAY.md "Drop-in games"): the savers' part.

A hosted game with drop-in slots is a session whose last slots are open (build_play_overlay.py --open): the common load
takes the others, and a drop-in player joins while it runs (hotjoin.hot_join against the master's relay). The relay asks
one of the common load's players (a saver) for a join save; the master names that join in the saver's lobby answers
(send_join: [{tick, slot}]). JoinSender is that saver's launcher: it waits for the two files its X4 wrote in its
profile (save/mpjoin/j<T>.x4join and j<T>.xml.gz) and uploads them, the package compressed first (joinfiles.gzip_file,
about a sixth of its size: a home connection's upload is the slow part of a join). The master unpacks and checks it
(server.unpack_join) and serves it to the joiner on the relay's port; a master that refuses the compressed copy gets
the raw package.
"""
from pathlib import Path
import threading
import time

from . import joinfiles

POLL_S = 0.5
FILES_WAIT_S = 90      # the relay announces a join once the saver's X4 wrote it; the files are there by then
TRIES = 3              # a failed upload is tried again while the master still lists the join
RETRY_PAUSE_S = 5
GONE_STATUSES = (403, 404)   # the game or this player is gone at the master: nothing more to send


def wanted(answer):
    """The joins a lobby answer asks this player for: [(tick, slot)], checked."""
    rows = answer.get('send_join') if isinstance(answer, dict) else None
    found = []
    for row in rows if isinstance(rows, list) else ():
        if not isinstance(row, dict):
            continue
        tick, slot = row.get('tick'), row.get('slot')
        if isinstance(tick, int) and not isinstance(tick, bool) and 0 < tick < 10_000_000 and isinstance(slot, int):
            found.append((tick, slot))
    return found


class JoinSender:
    """One saver's launcher in a running drop-in game: see the module text. auth is the game's secret (the host) or
    this player's ticket; watch a netplay.LobbyWatch of this game."""

    def __init__(self, master, game_id, auth, save_dir, watch, errors, log=print, files_wait_s=FILES_WAIT_S):
        self.master = master
        self.game_id = game_id
        self.auth = auth
        self.save_dir = Path(save_dir)
        self.watch = watch
        self.errors = tuple(errors)
        self.log = log
        self.files_wait_s = files_wait_s
        self.tries = {}        # tick -> uploads tried
        self.sent = set()      # ticks whose files the master has
        self.next_try = {}     # tick -> monotonic time of the next try
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, name='netplay-joins', daemon=True)

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.stopped.set()

    def run(self):
        while not self.stopped.wait(POLL_S):
            for tick, slot in wanted(self.watch.latest()):
                if tick in self.sent or self.tries.get(tick, 0) >= TRIES or time.monotonic() < self.next_try.get(tick, 0):
                    continue
                self.tries[tick] = self.tries.get(tick, 0) + 1
                try:
                    self.send(tick, slot)
                    self.sent.add(tick)
                except self.errors as error:
                    status = getattr(error, 'status', 0)
                    self.log(f'netplay: the join files of tick {tick} (slot {slot}) were not sent: {error}')
                    if status in GONE_STATUSES:
                        return
                    self.next_try[tick] = time.monotonic() + RETRY_PAUSE_S
                except (OSError, ValueError) as error:
                    self.log(f'netplay: the join files of tick {tick} (slot {slot}) were not sent: {error}')
                    self.next_try[tick] = time.monotonic() + RETRY_PAUSE_S

    def send(self, tick, slot):
        folder = self.save_dir / 'mpjoin'
        package = folder / f'j{tick}.x4join'
        save = folder / f'j{tick}.xml.gz'
        deadline = time.monotonic() + self.files_wait_s
        while not (package.is_file() and save.is_file()):
            if time.monotonic() > deadline:
                raise OSError(f'this PC\'s X4 wrote no join files for tick {tick} in {self.files_wait_s} s ({folder})')
            if self.stopped.wait(0.5):
                raise OSError('the session ended')
        started = time.monotonic()
        how = 'raw'
        packed = folder / f'j{tick}.x4join.gz'
        try:
            joinfiles.gzip_file(package, packed)
            self.master.upload(self.game_id, self.auth, packed.name, packed)
            how = f'compressed to {packed.stat().st_size >> 20} MB'
        except self.errors as error:
            if getattr(error, 'status', 0) in GONE_STATUSES:
                raise
            self.log(f'netplay: the master did not take the compressed package ({error}); sending it raw')
            self.master.upload(self.game_id, self.auth, package.name, package)
        except OSError as error:
            self.log(f'netplay: the package was not compressed ({error}); sending it raw')
            self.master.upload(self.game_id, self.auth, package.name, package)
        self.master.upload(self.game_id, self.auth, save.name, save)
        self.log(f'netplay: join of slot {slot}: tick {tick}\'s files sent to the master (package '
                 f'{package.stat().st_size >> 20} MB {how}, save {save.stat().st_size >> 20} MB, '
                 f'{time.monotonic() - started:.0f} s)')


__all__ = ['JoinSender', 'wanted']
