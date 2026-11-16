"""Periodic saves of a play session (native_gate_save_every; tools/build_play_overlay.py --save-every).

Every client's gate saves the world at the same tick boundaries and plays on (lockstep_periodic_save.h); the probe writes
one PERIODIC_SAVE row per save. x4ls play moves each save of its own clients out of the disposable profile into
RUN/saves/c<K>-t<TICK>.xml.gz, checks it against the SHA-256 the gate announced, lists it in RUN/saves/saves.jsonl and
keeps only what a later look needs:
  - while the session is in sync: the newest KEEP ticks (default 3: half an hour at a save every 10 minutes);
  - once the relay has named a first desync tick D: the newest KEEP ticks before D stay for good (the worlds a rerun starts
    from), of the ticks from D on only the first one (both clients' worlds right after the split, for
    tools/dev/checkpoint_diff.py), later ones are deleted as they arrive.
A save is about the size of the session save (25 MB at the start of a two-player session, more as the world grows).
"""
import hashlib
import json
from pathlib import Path
import re
import time

SAVED = re.compile(r'NATIVE_GATE PERIODIC_SAVE saved tick=(\d+) file=(\S+) bytes=(\d+) sha256=([0-9a-f]{64}) ms=(\d+)')
FAILED = re.compile(r'NATIVE_GATE ERROR PERIODIC_SAVE_FAILED tick=(\d+) state=(\S+) why=(\S+)')
NAME = re.compile(r'c(\d+)-t(\d+)\.xml(?:\.gz)?$')
DEFAULT_KEEP = 3


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def keep_ticks(ticks, keep, first_desync=None):
    """Which of the saved ticks stay (see the module text)."""
    ticks = sorted(set(ticks))
    if first_desync is None:
        return set(ticks[-keep:])
    before = [t for t in ticks if t < first_desync]
    after = [t for t in ticks if t >= first_desync]
    return set(before[-keep:]) | set(after[:1])


class Collector:
    """The saves of one run. saved() takes a client's PERIODIC_SAVE row; poll() moves what is ready and prunes."""

    def __init__(self, folder, keep=DEFAULT_KEEP, tick_hz=30, log=print):
        self.folder, self.keep, self.tick_hz, self.log = Path(folder), max(1, int(keep)), tick_hz, log
        self.pending, self.kept, self.failed, self.frozen_before = [], {}, 0, None   # kept: tick -> {client: path}
        self.made, self.refused, self.uneven = {}, {}, set()                         # tick -> clients; ticks already named

    def line(self, client, line, save_folder):
        """A client's log line: queues a save it announces; True when the line was a periodic save row."""
        match = SAVED.search(line)
        if match:
            tick, name, size, digest, ms = int(match.group(1)), match.group(2), int(match.group(3)), match.group(4), int(match.group(5))
            if '..' in name or name.startswith(('/', '\\')) or ':' in name:
                self.log(f'client {client}: periodic save row names {name!r}: ignored')
                return True
            self.made.setdefault(tick, set()).add(client)
            self.uneven_check(tick)
            self.pending.append({'client': client, 'tick': tick, 'source': Path(save_folder) / name, 'bytes': size,
                                 'sha256': digest, 'ms': ms, 'tries': 0})
            return True
        match = FAILED.search(line)
        if match:
            self.failed += 1
            self.refused.setdefault(int(match.group(1)), set()).add(client)
            self.log(f'client {client}: no periodic save of tick {match.group(1)} ({match.group(2)}, {match.group(3)})')
            self.uneven_check(int(match.group(1)))
            return True
        return False

    def uneven_check(self, tick):
        """A tick some clients saved and others did not: said once (a save has effects on the world that made it)."""
        if tick not in self.uneven and self.made.get(tick) and self.refused.get(tick):
            self.uneven.add(tick)
            self.log(f'PERIODIC SAVE UNEVEN at tick {tick}: saved by client(s) {sorted(self.made[tick])}, not by {sorted(self.refused[tick])}; '
                     f'a desync after this tick may come from the save itself')

    def poll(self, first_desync=None):
        """Moves the announced saves into the run folder (a file X4 still holds is tried again) and prunes."""
        waiting = []
        for item in self.pending:
            source = item['source']
            if not source.exists() and source.name.endswith('.xml.gz') and source.with_suffix('').exists():
                source = source.with_suffix('')   # a profile that writes its saves uncompressed
            target = self.folder / f"c{item['client']}-t{item['tick']}{'.xml.gz' if source.name.endswith('.gz') else '.xml'}"
            try:
                self.folder.mkdir(parents=True, exist_ok=True)
                source.replace(target)
            except OSError as error:
                item['tries'] += 1
                if item['tries'] < 30 and source.exists():
                    waiting.append(item)
                else:
                    self.log(f"client {item['client']}: periodic save of tick {item['tick']} not collected ({error})")
                continue
            ok = sha256(target) == item['sha256']
            record = {'tick': item['tick'], 'client': item['client'], 'file': target.name, 'bytes': target.stat().st_size,
                      'sha256': item['sha256'], 'verified': ok, 'save_ms': item['ms'], 'minute': round(item['tick'] / self.tick_hz / 60, 1),
                      'collected': time.strftime('%Y-%m-%d %H:%M:%S')}
            with (self.folder / 'saves.jsonl').open('a', encoding='utf-8') as index:
                index.write(json.dumps(record, sort_keys=True) + '\n')
            self.kept.setdefault(item['tick'], {})[item['client']] = target
            self.log(f"save of tick {item['tick']} (minute {record['minute']:g}), client {item['client']}: {target.name}, "
                     f"{record['bytes'] / 1e6:.1f} MB, written in {item['ms'] / 1000:.1f} s" + ('' if ok else ', SHA-256 DIFFERS from the gate\'s'))
        self.pending = waiting
        self.prune(first_desync)

    def prune(self, first_desync=None):
        stay = keep_ticks(self.kept, self.keep, first_desync)
        for tick in sorted(set(self.kept) - stay):
            for path in self.kept.pop(tick).values():
                try:
                    path.unlink()
                except OSError:
                    pass
        if first_desync is not None and self.frozen_before != first_desync:
            self.frozen_before = first_desync
            before = sorted(t for t in self.kept if t < first_desync)
            self.log(f'first desync at tick {first_desync}: the saves of ' + (', '.join(f'tick {t}' for t in before) or 'no earlier tick') +
                     f' stay in {self.folder} (a rerun starts from one of them)')

    def summary(self):
        return {'folder': str(self.folder), 'ticks': sorted(self.kept), 'failed': self.failed, 'uneven': sorted(self.uneven),
                'files': sorted(path.name for files in self.kept.values() for path in files.values())}
