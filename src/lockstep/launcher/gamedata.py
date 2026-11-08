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
