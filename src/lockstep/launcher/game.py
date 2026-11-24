"""Locate and verify the X4 installation; build and remove per-client game views.

A view is a directory on the installation's drive whose top-level files are hard
links to the game's files and whose folders are junctions to the game's folders,
except `extensions`: that is a real folder holding junctions to the official DLCs
and the client's own x4_lan_lab. X4 runs from the view, so the probe's relative
`./extensions/x4_lan_lab/...` paths resolve per client. Third-party extensions in
the installation are not linked. Removal unlinks; it never follows a junction.
"""
import gzip
import hashlib
import os
from pathlib import Path
import re
import stat
import zlib

# X4 9.00 Windows (Steam build 23660954): the build the lockstep gate is pinned to.
EXE_SIZE = 55412768
EXE_SHA256 = '19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891'
APP_ID = '392160'
MARKER = '.x4ls-view'


def steam_root():
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
        return Path(winreg.QueryValueEx(key, 'SteamPath')[0])


def library_paths(vdf_text):
    return [Path(m.replace('\\\\', '\\')) for m in re.findall(r'"path"\s+"([^"]+)"', vdf_text)]


def find_install(steam=None):
    steam = Path(steam) if steam else steam_root()
    libraries = library_paths((steam / 'steamapps/libraryfolders.vdf').read_text(encoding='utf-8', errors='replace'))
    for library in [steam, *libraries]:
        if (library / f'steamapps/appmanifest_{APP_ID}.acf').is_file():
            game = library / 'steamapps/common/X4 Foundations'
            if (game / 'X4.exe').is_file():
                return game
    raise SystemExit('X4 Foundations not found in any Steam library; pass --game')


def verify_exe(game):
    exe = Path(game) / 'X4.exe'
    if exe.stat().st_size != EXE_SIZE:
        raise SystemExit(f'{exe} is not X4 9.00 (size {exe.stat().st_size}); the gate refuses other builds')
    digest = hashlib.sha256()
    with exe.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    if digest.hexdigest() != EXE_SHA256:
        raise SystemExit(f'{exe} SHA-256 differs from the pinned X4 9.00 build')


def _junction(target, link):
    import _winapi
    _winapi.CreateJunction(str(target), str(link))


def create_view(game, view, junction=_junction):
    """Build a view of `game` at `view` (new directory on the same drive)."""
    game, view = Path(game).resolve(), Path(view)
    if view.exists():
        raise SystemExit(f'{view} already exists; remove it with x4ls clean')
    if os.path.splitdrive(str(game))[0].lower() != os.path.splitdrive(str(view.resolve()))[0].lower():
        raise SystemExit(f'views must be on the game drive ({os.path.splitdrive(str(game))[0]}) for hard links')
    view.mkdir(parents=True)
    (view / MARKER).write_text(str(game) + '\n')
    for entry in game.iterdir():
        if entry.name == 'extensions':
            continue
        if entry.is_dir():
            junction(entry, view / entry.name)
        else:
            os.link(entry, view / entry.name)
    extensions = view / 'extensions'
    extensions.mkdir()
    for dlc in sorted((game / 'extensions').glob('ego_dlc_*')) if (game / 'extensions').is_dir() else []:
        if dlc.is_dir():
            junction(dlc, extensions / dlc.name)
    return view


def dlc_ids(view):
    return sorted(p.name for p in (Path(view) / 'extensions').glob('ego_dlc_*'))


# The official DLCs a session's world was made with, and the ones this PC would run. A client's view links every
# ego_dlc_* folder of its installation and its profile enables all of them (profile.prepare), so the DLCs installed on a
# PC are the ones its X4 runs. A joiner whose set differs from the host's runs another world from the first tick: X4
# adds a DLC's content to a save that lacks it and drops what a missing one brought.
DLC_PREFIX = 'ego_dlc_'
_ATTRIBUTE = re.compile(r'\b([a-z]+)="([^"]*)"')
_CONTENT_HEAD_BYTES = 8192
_SAVE_HEAD_CHARS = 1 << 20


def _attributes(tag):
    return dict(_ATTRIBUTE.findall(tag))


def installed_dlcs(game):
    """{DLC id: version} of the official DLCs in the installation (the version attribute of each one's content.xml, ''
    when it has none). A folder without a content.xml (a DLC Steam took out) is no extension to X4 and not counted.
    {} without an extensions folder."""
    extensions = Path(game) / 'extensions'
    found = {}
    for folder in sorted(extensions.glob(DLC_PREFIX + '*')) if extensions.is_dir() else []:
        try:
            with open(folder / 'content.xml', 'rb') as content:
                head = content.read(_CONTENT_HEAD_BYTES).decode('utf-8', 'replace')
        except OSError:
            continue
        tag = re.search(r'<content\b[^>]*>', head)
        found[folder.name] = _attributes(tag.group(0)).get('version', '') if tag else ''
    return found


def save_dlcs(save):
    """{DLC id: version} of the official DLCs a save was made with: the <patch> rows of its <patches> header before the
    <history> X4 keeps there (a DLC once used and later disabled is only in the history). None when the file is not an
    X4 save (gzip or plain XML) or has no <patches> header in its first MB."""
    save = Path(save)
    try:
        with open(save, 'rb') as raw:
            magic = raw.read(2)
        opener = gzip.open if magic == b'\x1f\x8b' else open
        with opener(save, 'rb') as source:
            head = source.read(_SAVE_HEAD_CHARS).decode('utf-8', 'replace')
    except (OSError, EOFError, zlib.error):
        return None
    start = head.find('<patches')
    if '<savegame' not in head[:4096] or start < 0:
        return None
    end = min(i for i in (head.find('<history', start), head.find('</patches>', start), len(head)) if i >= 0)
    rows = {}
    for tag in re.findall(r'<patch\b[^>]*>', head[start:end]):
        values = _attributes(tag)
        name = values.get('extension', '')
        if name.startswith(DLC_PREFIX):
            rows[name] = values.get('version', '')
    return rows


DLC_NAMES = {'ego_dlc_split': 'Split Vendetta', 'ego_dlc_terran': 'Cradle of Humanity', 'ego_dlc_pirate': 'Tides of Avarice',
             'ego_dlc_boron': 'Kingdom End', 'ego_dlc_timelines': 'Timelines', 'ego_dlc_mini_01': 'Hyperion Pack',
             'ego_dlc_mini_02': 'Envoy Pack', 'ego_dlc_ventures': 'Ventures'}


def dlc_names(ids):
    """The DLCs' shop names for a player's message ('Kingdom End, Timelines'); an unknown id as it is."""
    return ', '.join(DLC_NAMES.get(name, name) for name in ids)


def dlc_mismatch(save_rows, installed):
    """(missing, extra, other_version): the DLC ids a save uses that are not installed, the ones installed that the save
    does not use, and the ones whose versions differ (only when both name a version). All empty when they agree."""
    missing = sorted(set(save_rows) - set(installed))
    extra = sorted(set(installed) - set(save_rows))
    other = sorted(name for name in set(save_rows) & set(installed)
                   if save_rows[name] and installed[name] and save_rows[name] != installed[name])
    return missing, extra, other


def is_link_or_reparse(path):
    """True for a symbolic link and, on Windows, for any reparse point (a junction is one). Path.is_junction is new in
    Python 3.12 and is_symlink is False for a junction: on 3.10 and 3.11 a view's junction would look like a plain folder,
    and removing the view would delete the game's files through it. The file attributes say it on every version."""
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        return False
    if stat.S_ISLNK(info.st_mode):
        return True
    if os.name == 'nt':
        attributes = getattr(info, 'st_file_attributes', None)
        if not isinstance(attributes, int):
            raise OSError(f'cannot read the reparse attributes of {path}')
        return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)
    return False


def _unlink_link(entry):
    """Remove the link itself: a directory link or junction with rmdir on Windows, anything else with unlink."""
    if os.name == 'nt' and os.path.isdir(entry):
        os.rmdir(entry)
    else:
        os.unlink(entry)


def _remove_owned(path):
    """Delete a folder the launcher created; links inside are unlinked, never entered."""
    for entry in path.iterdir():
        if is_link_or_reparse(entry):
            _unlink_link(entry)
        elif entry.is_dir():
            _remove_owned(entry)
        else:
            os.unlink(entry)
    os.rmdir(path)


def remove_view(view):
    view = Path(view)
    if is_link_or_reparse(view):
        raise SystemExit(f'{view} is a link; refusing to delete')
    if not (view / MARKER).is_file():
        raise SystemExit(f'{view} is not an x4ls view; refusing to delete')
    _remove_owned(view)
