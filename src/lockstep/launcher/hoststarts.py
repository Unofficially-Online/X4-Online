"""The starts a hosted game may have: the Host form's Start (docs/NETPLAY.md "The start of a hosted game").

A hosted game gives every player the same start (coop): slot 1 is the session's base start, and every slot 2..N takes
that start's own ship, place and kit beside it (x4ls new-session's own_starts: --start K=BASE --omit K=world). So a
start can be hosted when it can be both: the session base (native_gate_gamestarts.session_base_refusal: the player in a
ship in open space of the main galaxy) and a slot's own start in the ship-only form (per_player_refusal with world=True).
A start with a story script of the stock shape (a class b start: the Terran cadet, the Split Patriarch's) gives every
slot 2..N its own copy of that story (x4ls new-session --story 2..N, as the release check's satellite sessions:
GS_Terran1_P<k> and Story_Yaki_P<k>); slot 1 plays the stock script. A class c start (Discover, Fight) has no story per
player: its slots get the ship, place and kit.

The table comes from this PC's game data (gamedata.unpack: the installed DLCs), every start's own name from the game's
English text (the page shows X4's own text in the player's language: ReadText of the start's name reference)."""
from pathlib import Path
import re
import threading

DEFAULT = 'x4ep1_gamestart_discover'   # netkit.HOST_SESSION_START
START_ID = re.compile(r'[a-z0-9_]{1,64}')
PREFIX = 'x4ep1_gamestart_'
CHECK_PLAYERS = 2   # the dry run of each start's session spec (the story copies are made per slot alike)


def short(start):
    """'x4ep1_gamestart_terran1' -> 'terran1' (make_release_kit.py --prebuild-starts, log rows)."""
    return start[len(PREFIX):] if start.startswith(PREFIX) else start


def full(name, rows):
    """The id of the row named 'terran1' or 'x4ep1_gamestart_terran1' (short() or the id), or None."""
    name = str(name or '').strip().lower()
    ids = {row['id'] for row in rows or ()}
    return next((candidate for candidate in (name, PREFIX + name) if candidate in ids), None)


def name_ref(text):
    """A gamestart's name attribute '{1021,900}' -> (1021, 900), or None."""
    m = re.fullmatch(r'\s*\{\s*(\d{1,9})\s*,\s*(\d{1,9})\s*\}\s*', str(text or ''))
    return (int(m.group(1)), int(m.group(2))) if m else None


def table(cat, names=None, mode='coop', log=print):
    """(rows, refused) of a start catalog (native_gate_gamestarts.Catalog): rows are the starts a hosted game can give
    every player, DEFAULT first and then in the game's own order, each {id, page, line, label, story}; refused is
    {id: why} for every other start of the catalog that is not refused outright (tutorials, tests, Timelines).
    names: {(page, line): text} of the game's text (gamedata.texts) for the labels; a start without one is named by
    its id."""
    from . import newsession
    gamestarts, session_start = newsession.tools()
    names = names or {}
    rows, refused = [], {}
    order = sorted(cat.starts, key=lambda sid: sid != DEFAULT)   # stable: the catalog's order after the default
    for sid in order:
        rec = cat.record(sid)
        klass, reasons = gamestarts.classify(rec)
        if klass == '-':
            continue
        why = gamestarts.session_base_refusal(rec) or gamestarts.per_player_refusal(rec, world=True)
        tpl = None
        if not why:
            try:
                tpl = gamestarts.template(cat, sid, world=True)
            except gamestarts.CatalogError as error:
                why = str(error)
        if not why:
            story = bool(tpl.get('story'))
            slots = range(2, CHECK_PLAYERS + 1)
            try:
                session_start.build_spec(cat, sid, CHECK_PLAYERS, mode, {k: sid for k in slots}, {k: 'world' for k in slots},
                                         story=tuple(slots) if story else ())
            except (session_start.SessionError, gamestarts.CatalogError) as error:
                why = f'its session cannot be made: {error}'
        if why:
            refused[sid] = why
            continue
        ref = name_ref(cat.starts[sid].get('name'))
        row = {'id': sid, 'label': (names.get(ref) if ref else None) or short(sid), 'story': story}
        if ref:
            row['page'], row['line'] = ref
        rows.append(row)
    return rows, refused


def load(game_dir=None, cache=None, mode='coop', log=print):
    """table() of this PC's game: its catalog (gamedata.unpack into the cache, seconds the first time) and the names in
    the game's English text."""
    from . import game, gamedata, newsession
    install = Path(game_dir) if game_dir else game.find_install()
    cat = newsession.catalog(install, cache, log)
    refs = [ref for ref in (name_ref(g.get('name')) for g in cat.starts.values()) if ref]
    try:
        names = gamedata.texts(install, refs)
    except (OSError, ValueError) as error:
        log(f'host starts: no names from the game\'s text ({error})')
        names = {}
    return table(cat, names, mode, log)


def find(rows, start):
    """The row of this start, or None."""
    return next((row for row in rows or () if row.get('id') == start), None)


class Starts:
    """The table of one launcher (x4ls lobby, the Steam helper), loaded once in its own thread: the page must not wait
    for the game data, and the first HOST waits for it at most `wait` seconds. loader() -> (rows, refused) or None
    (no table: a backend without game data)."""

    def __init__(self, loader, log=print):
        self.loader = loader
        self.log = log
        self.rows = None
        self.refused = {}
        self.error = None
        self.done = threading.Event()
        self.thread = None

    def start(self, then=None):
        """Load in a thread; then(rows) once the table is there (not called without one)."""
        def run():
            try:
                found = self.loader()
            except (SystemExit, Exception) as error:   # no game data here: hosting keeps its default start
                self.error = f'{type(error).__name__}: {error}' if not isinstance(error, SystemExit) else str(error)
                self.log(f'host starts: not read ({self.error})')
                found = None
            if found is not None:
                self.rows, self.refused = list(found[0]), dict(found[1])
                self.log('host starts: ' + ', '.join(f"{short(r['id'])}{' (story)' if r['story'] else ''}" for r in self.rows)
                         + (f"; not hostable: {', '.join(short(s) for s in sorted(self.refused))}" if self.refused else ''))
            self.done.set()
            if self.rows is not None and then is not None:
                then(self.rows)
        self.thread = threading.Thread(target=run, name='netplay-host-starts', daemon=True)
        self.thread.start()
        return self

    def wait(self, timeout=60):
        """The rows (None without a table) once loaded, waiting at most timeout seconds."""
        self.done.wait(timeout)
        return self.rows
