"""The installed game's libraries and Mission Director scripts, unpacked read-only into a local cache for the start catalog
(tools/dev/native_gate_gamestarts.py) and the session start tool (tools/dev/native_gate_session_start.py).

X4 keeps its files in catalog pairs: NN.cat is a text index (one line per file: path, size, time stamp, MD5) and NN.dat
the files' bytes in that order. The base game's catalogs are <game>/NN.cat, an extension's <game>/extensions/<ext>/ext_NN.cat;
an extension's subst_NN.cat replaces base files. A later catalog wins for the same path, as in X4.

The cache (<cache>/<ext>/libraries/*.xml, <cache>/<ext>/md/*.xml: the per-extension layout the catalog reads) stays on
this PC: .local/x4ls/gamedata by default, never in git. It is unpacked again when any catalog's size or time changes.
"""
import json
import os
from pathlib import Path
import re
import shutil

STOCK_EXTENSIONS = ('ego_dlc_boron', 'ego_dlc_mini_01', 'ego_dlc_mini_02', 'ego_dlc_pirate', 'ego_dlc_split',
                    'ego_dlc_terran', 'ego_dlc_timelines')
WANTED = ('libraries/', 'md/')
STAMP = 'catalogs.json'


def catalogs(game):
    """[(extension directory name, catalog path)] in the order X4 reads them: base, then each stock extension."""
    game = Path(game)
    out = [('base', cat) for cat in sorted(game.glob('[0-9][0-9].cat'))]
    for ext in STOCK_EXTENSIONS:
        folder = game / 'extensions' / ext
        if not folder.is_dir():
            continue
        out += [('base', cat) for cat in sorted(folder.glob('subst_[0-9][0-9].cat'))]
        out += [(ext, cat) for cat in sorted(folder.glob('ext_[0-9][0-9].cat'))]
    return [(ext, cat) for ext, cat in out if cat.with_suffix('.dat').is_file()]


def index(cat):
    """[(path, size)] of one catalog, in file order (the .dat offsets follow from the sizes)."""
    rows = []
    with open(cat, encoding='utf-8', errors='replace') as lines:
        for line in lines:
            parts = line.rstrip('\r\n').rsplit(' ', 3)
            if len(parts) != 4 or not parts[1].isdigit():
                raise ValueError(f'{cat}: not a catalog line: {line[:80]!r}')
            rows.append((parts[0], int(parts[1])))
    return rows


def safe_parts(path):
    parts = path.replace('\\', '/').split('/')
    if not parts or any(p in ('', '.', '..') or ':' in p for p in parts):
        raise ValueError(f'unsafe catalog path {path!r}')
    return parts


def unpack(game, cache, log=print):
    """Unpack libraries/ and md/ XML of every catalog into cache (skipped when the catalogs are unchanged); returns cache."""
    cache = Path(cache)
    found = catalogs(game)
    if not found:
        raise SystemExit(f'{game}: no X4 catalogs (NN.cat with NN.dat)')
    stamp = [[ext, cat.name, cat.stat().st_size, int(cat.stat().st_mtime), cat.with_suffix('.dat').stat().st_size] for ext, cat in found]
    try:
        if json.loads((cache / STAMP).read_text()) == stamp:
            return cache
    except (OSError, ValueError):
        pass
    if cache.exists():
        shutil.rmtree(cache)
    count = 0
    for ext, cat in found:
        offset = 0
        with open(cat.with_suffix('.dat'), 'rb') as dat:
            for path, size in index(cat):
                low = path.lower()
                if low.startswith(WANTED) and low.endswith('.xml'):
                    target = cache.joinpath(ext, *safe_parts(path))
                    target.parent.mkdir(parents=True, exist_ok=True)
                    dat.seek(offset)
                    target.write_bytes(dat.read(size))
                    count += 1
                offset += size
    (cache / STAMP).write_text(json.dumps(stamp))
    log(f'game data: {count} library and MD files from {len(found)} catalogs into {cache}')
    return cache


def default_cache():
    from .session import ROOT
    return Path(os.environ.get('X4LS_GAMEDATA') or ROOT / '.local/x4ls/gamedata')


# ---- the game's own text (t/0001-l<language>.xml), read straight from the catalogs ----
TEXT_REF = re.compile(r'\{\s*(\d{1,9})\s*,\s*(\d{1,9})\s*\}')
TEXT_COMMENT = re.compile(r'(?<!\\)\((?:[^()\\]|\\.)*\)')   # X4 drops unescaped (...) from a text: the writers' notes
TEXT_DEPTH = 4
ENGLISH = 44


def read_file(game, path):
    """The bytes of one file of the game as X4 reads it (the last catalog that has it), or None."""
    wanted = path.replace('\\', '/').lower()
    found = None
    for _ext, cat in catalogs(game):
        offset = 0
        for name, size in index(cat):
            if name.lower() == wanted:
                found = (cat, offset, size)
            offset += size
    if found is None:
        return None
    cat, offset, size = found
    with open(cat.with_suffix('.dat'), 'rb') as dat:
        dat.seek(offset)
        return dat.read(size)


def text_table(data):
    """{(page, id): raw text} of one language file's bytes."""
    import xml.etree.ElementTree as ET
    table = {}
    for page in ET.fromstring(data).iter('page'):
        try:
            page_id = int(page.get('id'))
        except (TypeError, ValueError):
            continue
        for entry in page.iter('t'):
            try:
                table[(page_id, int(entry.get('id')))] = entry.text or ''
            except (TypeError, ValueError):
                continue
    return table


def resolve_text(table, page, line, depth=0):
    """One text as X4 shows it: the writers' notes in parentheses left out, {page,id} references resolved, escaped
    parentheses kept; None when the table has no such line."""
    raw = table.get((int(page), int(line)))
    if raw is None or depth > TEXT_DEPTH:
        return None
    text = TEXT_COMMENT.sub('', raw)
    text = TEXT_REF.sub(lambda m: resolve_text(table, m.group(1), m.group(2), depth + 1) or '', text)
    return text.replace('\\(', '(').replace('\\)', ')').strip()


def texts(game, refs, language=ENGLISH):
    """{(page, id): text} of the game's own text for these references (X4's language file; English by default). A
    reference the game has no text for is left out; an unreadable file gives {}."""
    data = read_file(game, f't/0001-l{int(language):03d}.xml')
    if not data:
        return {}
    try:
        table = text_table(data)
    except Exception:   # a broken file: the names are only labels
        return {}
    out = {}
    for page, line in refs:
        text = resolve_text(table, page, line)
        if text:
            out[(int(page), int(line))] = text
    return out
