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

Every save of a session (native_gate_milestones; tools/dev/native_gate_milestones.py) has SAVE_SLOT rows before it (each
slot's player, ship, inventory, research) and a sidecar beside its file: the kind, its tick, the slots' rows, the
clients' file hashes and the session it belongs to (session.json, spec.json, the play recipe), all that x4ls continue needs
(src/lockstep/launcher/milestone.py):
  - periodic: RUN/saves/c<K>-t<T>.json beside each kept file (pruned with it);
  - milestone (a mission chapter of a story is over, MILESTONE_SAVE saved): RUN/saves/milestones/<story>-M<n>-slot<k>.xml.gz;
  - player (a person's Save game or quick save, PLAYER_SAVE saved): RUN/saves/player/<name>-t<T>.xml.gz, the sidecar with
    who asked, the name they typed and the menu's save file.
The last two are one file each (the save of the lowest client of this PC; every client's SHA-256 is in the sidecar) and
never pruned. A remote PC's clients are not seen here.
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
MILESTONE_SAVED = re.compile(r'NATIVE_GATE MILESTONE_SAVE saved name=([a-z][a-z0-9_]{0,31}-M\d{1,3}-slot[1-8]) tick=(\d+) file=(\S+) '
                             r'bytes=(\d+) sha256=([0-9a-f]{64}) ms=(\d+)')
PLAYER_SAVED = re.compile(r'NATIVE_GATE PLAYER_SAVE saved name=([A-Za-z0-9_-]{1,40}-t\d+) tick=(\d+) file=(\S+) '
                          r'bytes=(\d+) sha256=([0-9a-f]{64}) ms=(\d+) slot=([1-8]) save=([A-Za-z0-9_]{1,32})')
SAVE_SLOT = re.compile(r'NATIVE_GATE SAVE_SLOT name=(\S+) tick=(\d+) slot=([1-8]) player=(\S+) ship=(\S+) walking=([01]) '
                       r'inventory=(\S+) research=(\S+)(?: active=(\S+))?')
ACTIVE = re.compile(r'([1-9]\d{0,4}):([A-Za-z0-9_]{1,40}|-)')   # slot k's active mission: its place in the list and its type
MILESTONE_REACHED = re.compile(r'NATIVE_GATE MILESTONE reached name=(\S+) age=(\S+) due=(\d+) save=(\S+)')
MILESTONE_NAME = re.compile(r'([a-z][a-z0-9_]{0,31})-M(\d{1,3})-slot([1-8])')
MILESTONES = 'milestones'
PLAYER = 'player'
SIDECAR_FORMAT = 'X4LAN_MILESTONE_SAVE_1'


def hex_id(text):
    """A decimal runtime ID of a row as the save writes it (16 hex digits); None for '-'."""
    return f'{int(text):016x}' if text and text.isdigit() and int(text) > 0 else None


def wares(text, amounts=True):
    """A row's ware list: 'ware:n,ware:n' (amounts) or 'ware,ware'; '-', 'none' and 'error' are none."""
    if text in ('-', 'none', 'error', ''):
        return []
    out = []
    for item in text.split(','):
        ware, _, n = item.partition(':')
        if not re.fullmatch(r'[a-z][a-z0-9_]{0,63}', ware):
            continue
        if amounts:
            if n.isdigit() and int(n) > 0:
                out.append({'ware': ware, 'amount': int(n)})
        else:
            out.append(ware)
    return out


def overlay_context(overlay):
    """What a milestone's sidecar names of the session a play overlay runs: its recipe, the registered session.json and the
    spec beside the session save when it is that session's (x4ls new-session writes OUT/spec.json). {} without a recipe."""
    try:
        recipe = json.loads((Path(overlay) / 'play-recipe.json').read_text(encoding='utf-8'))
    except (OSError, ValueError, TypeError):
        return {}
    context = {'overlay': str(Path(overlay).resolve()), 'recipe': recipe, 'session': recipe.get('session'), 'spec': None}
    session = recipe.get('session') or {}
    path = (session.get('snapshot') or {}).get('path')
    if path and not Path(path).is_absolute():   # a kept session names its save by file name, beside the overlay folder
        path = Path(overlay).resolve().parent / path
    if path:
        try:
            spec = json.loads(Path(path).with_name('spec.json').read_text(encoding='utf-8'))
            if hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest() == session.get('spec_sha256'):
                context['spec'] = spec
        except (OSError, ValueError):
            pass
    return context


def milestone_title(name):
    """The catalogue's title of a milestone name (tools/dev/native_gate_milestones.py); None when unknown."""
    try:
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/dev'))
        import native_gate_milestones as catalogue
        story, n, _ = catalogue.parse_name(name)
        return catalogue.milestone(story, n)['title']
    except (ImportError, TypeError, ValueError, KeyError):
        return None


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
    """The saves of one run. line() takes a client's save rows; poll() moves what is ready and prunes."""

    def __init__(self, folder, keep=DEFAULT_KEEP, tick_hz=30, log=print, context=None):
        self.folder, self.keep, self.tick_hz, self.log = Path(folder), max(1, int(keep)), tick_hz, log
        self.pending, self.kept, self.failed, self.frozen_before = [], {}, 0, None   # kept: tick -> {client: path}
        self.made, self.refused, self.uneven = {}, {}, set()                         # tick -> clients; ticks already named
        self.context = context or {}
        self.milestones, self.milestone_pending = {}, []   # name -> what its sidecar says; saves to collect (both kinds)
        self.players = {}                                  # a person's saves: name -> what its sidecar says
        self.slot_rows, self.reached = {}, {}              # (client, name, tick) -> {slot: row}; name -> reached row

    def milestone_line(self, client, line, save_folder):
        """A milestone or person's save row of a client's log, or a save's slot row (see the module text); True when it was one."""
        match = SAVE_SLOT.search(line)
        if match:
            name, tick, slot, player, ship, walking, inventory, research, active = match.groups()
            place = ACTIVE.fullmatch(active or '')
            self.slot_rows.setdefault((client, name, int(tick)), {})[int(slot)] = {
                'player': hex_id(player), 'ship': hex_id(ship), 'walking': walking == '1',
                'inventory': None if inventory == '-' else wares(inventory), 'research': wares(research, amounts=False),
                'active': [int(place.group(1)), place.group(2)] if place else None}
            return True
        match = MILESTONE_REACHED.search(line)
        if match:
            self.reached.setdefault(match.group(1), {'age': match.group(2), 'due': int(match.group(3)), 'save': match.group(4)})
            return True
        match = MILESTONE_SAVED.search(line) or PLAYER_SAVED.search(line)
        if match:
            name, tick, file, size, digest, ms = match.groups()[:6]
            if '..' in file or file.startswith(('/', '\\')) or ':' in file:
                self.log(f'client {client}: save row names {file!r}: ignored')
                return True
            item = {'client': client, 'name': name, 'tick': int(tick), 'source': Path(save_folder) / file, 'bytes': int(size),
                    'sha256': digest, 'ms': int(ms), 'tries': 0, 'kind': 'milestone'}
            if match.re is PLAYER_SAVED:
                item.update(kind='player', asked_by=int(match.group(7)), save_file=match.group(8))
            self.milestone_pending.append(item)
            return True
        return False

    def milestone_poll(self):
        """Moves the announced milestone saves: the lowest client's file is the milestone's, every client's hash goes into
        its sidecar, the other files are deleted (one world, saved by each client)."""
        waiting = []
        for item in self.milestone_pending:
            folder = self.folder / (PLAYER if item['kind'] == 'player' else MILESTONES)
            records = self.players if item['kind'] == 'player' else self.milestones
            source = item['source']
            if not source.exists() and source.name.endswith('.xml.gz') and source.with_suffix('').exists():
                source = source.with_suffix('')
            if not source.exists():
                item['tries'] += 1
                if item['tries'] < 30:
                    waiting.append(item)
                else:
                    self.log(f"client {item['client']}: milestone save {item['name']} not found ({source})")
                continue
            try:
                ok = sha256(source) == item['sha256']
            except OSError:
                waiting.append(item)
                continue
            record = records.setdefault(item['name'], {'tick': item['tick'], 'clients': {}, 'client': None, 'kind': item['kind'],
                                                       'asked_by': item.get('asked_by'), 'save_file': item.get('save_file')})
            if record['tick'] != item['tick']:
                self.log(f"client {item['client']}: milestone {item['name']} saved at tick {item['tick']}, the others at {record['tick']}")
            record['clients'][str(item['client'])] = {'sha256': item['sha256'], 'bytes': item['bytes'], 'verified': ok,
                                                      'save_ms': item['ms'], 'tick': item['tick']}
            target = folder / (item['name'] + ('.xml.gz' if source.name.endswith('.gz') else '.xml'))
            keep = record['client'] is None or (ok and item['client'] < record['client'])
            try:
                folder.mkdir(parents=True, exist_ok=True)
                if keep:
                    source.replace(target)
                    record['client'], record['file'] = item['client'], target.name
                else:
                    source.unlink()
            except OSError as error:
                item['tries'] += 1
                if item['tries'] < 30:
                    waiting.append(item)
                else:
                    self.log(f"client {item['client']}: milestone save {item['name']} not collected ({error})")
                continue
            self.write_sidecar(item['name'], item['kind'])
            self.log(f"{'save' if item['kind'] == 'player' else 'milestone'} {item['name']} (tick {item['tick']}), client {item['client']}: "
                     + (f'kept as {target.name}' if keep else 'its hash noted') + ('' if ok else ", SHA-256 DIFFERS from the gate's"))
        self.milestone_pending = waiting

    def sidecar(self, kind, name, rows, client, tick, file, sha, size, verified, clients=None):
        """What a save's sidecar says (any kind): the rows of the client whose file it is, the session's files."""
        slots = self.slot_rows.get((client, rows, tick)) or {}
        return {'format': SIDECAR_FORMAT, 'kind': kind, 'name': name, 'story': None, 'milestone': None, 'slot': None, 'title': None,
                'tick': tick, 'file': file, 'client': client, 'sha256': sha, 'bytes': size, 'verified': verified,
                'clients': clients or {str(client): {'sha256': sha, 'bytes': size, 'verified': verified, 'tick': tick}},
                'slots': {str(k): v for k, v in sorted(slots.items())}, 'prepared': bool(slots),
                'session': self.context.get('session'), 'spec': self.context.get('spec'), 'recipe': self.context.get('recipe'),
                'overlay': self.context.get('overlay'), 'run': str(self.folder.parent), 'collected': time.strftime('%Y-%m-%d %H:%M:%S')}

    def write_sidecar(self, name, kind='milestone'):
        record = (self.players if kind == 'player' else self.milestones)[name]
        kept = record['clients'][str(record['client'])]
        data = self.sidecar(kind, name, name, record['client'], record['tick'], record.get('file'), kept['sha256'], kept['bytes'],
                            kept['verified'], record['clients'])
        if kind == 'player':
            data.update(slot=record['asked_by'], asked_by=record['asked_by'], save_file=record['save_file'],
                        save_name=name.rsplit('-t', 1)[0])
        else:
            story, n, slot = MILESTONE_NAME.fullmatch(name).groups()
            data.update(story=story, milestone=int(n), slot=int(slot), title=milestone_title(name), reached=self.reached.get(name))
        folder = self.folder / (PLAYER if kind == 'player' else MILESTONES)
        (folder / f'{name}.json').write_text(json.dumps(data, indent=1, sort_keys=True) + '\n', encoding='utf-8')

    def line(self, client, line, save_folder):
        """A client's log line: queues a save it announces; True when the line was a periodic or milestone save row."""
        if ('MILESTONE' in line or 'SAVE_SLOT' in line or 'PLAYER_SAVE' in line) and self.milestone_line(client, line, save_folder):
            return True
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
        if self.milestone_pending:
            self.milestone_poll()
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
            # its sidecar (x4ls continue --save): the rows the probe wrote before it, the session it belongs to
            data = self.sidecar('periodic', f"c{item['client']}-t{item['tick']}", f"periodic-t{item['tick']}", item['client'], item['tick'],
                                target.name, item['sha256'], record['bytes'], ok)
            target.with_name(NAME.sub(lambda m: f'c{m.group(1)}-t{m.group(2)}.json', target.name)).write_text(
                json.dumps(data, indent=1, sort_keys=True) + '\n', encoding='utf-8')
            self.log(f"save of tick {item['tick']} (minute {record['minute']:g}), client {item['client']}: {target.name}, "
                     f"{record['bytes'] / 1e6:.1f} MB, written in {item['ms'] / 1000:.1f} s" + ('' if ok else ', SHA-256 DIFFERS from the gate\'s'))
        self.pending = waiting
        self.prune(first_desync)

    def prune(self, first_desync=None):
        stay = keep_ticks(self.kept, self.keep, first_desync)
        for tick in sorted(set(self.kept) - stay):
            for path in self.kept.pop(tick).values():
                for one in (path, path.with_name(NAME.sub(lambda m: f'c{m.group(1)}-t{m.group(2)}.json', path.name))):
                    try:
                        one.unlink()
                    except OSError:
                        pass
        if first_desync is not None and self.frozen_before != first_desync:
            self.frozen_before = first_desync
            before = sorted(t for t in self.kept if t < first_desync)
            self.log(f'first desync at tick {first_desync}: the saves of ' + (', '.join(f'tick {t}' for t in before) or 'no earlier tick') +
                     f' stay in {self.folder} (a rerun starts from one of them)')

    def summary(self):
        return {'folder': str(self.folder), 'ticks': sorted(self.kept), 'failed': self.failed, 'uneven': sorted(self.uneven),
                'files': sorted(path.name for files in self.kept.values() for path in files.values()),
                'milestones': sorted(self.milestones), 'player_saves': sorted(self.players)}
