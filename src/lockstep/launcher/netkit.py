"""What a launcher plays with, and the session files of a netplay game (docs/NETPLAY.md: "Releases and compatibility",
"Release kit and installer", "Trust and safety rules").

- Kit: a release kit (release.json and the gate DLL in native/, no session: one is made here or downloaded), a playtest
  kit (overlay/ and snapshot/: its own session; with server.json the lobby of x4ls serve), or this checkout.
- The host's session: a playtest kit's own, or one that x4ls new-session made and the kit keeps in sessions/.
- The joiner's session: the host's three files (session.json, the session save, play-recipe.json) checked against their
  SHA-256 and against each other, the recipe checked to be data only, and the overlay built here from it with this
  kit's own gate DLL and Lua (tools/build_play_overlay.py). Nothing a host sends is run."""
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from . import lobby, session

RELEASE_FILE = 'release.json'
SESSION_JSON = 'session.json'
SESSION_SAVE = 'session-save.xml.gz'
RECIPE = 'play-recipe.json'
SESSION_FILES = (SESSION_JSON, SESSION_SAVE, RECIPE)
GATE_DLL = 'x4lan_gate_64.dll'
HOST_SESSION_START = 'x4ep1_gamestart_discover'   # the base start of a session a release kit makes (PLAYTEST.md, ns1)
HOST_SESSION_MODE = 'coop'
HEX64 = re.compile(r'[0-9a-f]{64}')
RELEASE = re.compile(r'[0-9a-z][0-9a-z.-]{0,79}')
# The recipe's values that go into the overlay's config as Lua: a key of the gate and a literal (a number, true/false,
# a quoted word, or a table of those). Anything else could carry code a host wrote, and is refused.
SET_KEY = re.compile(r'native_gate_[a-z0-9_]{1,64}')
LUA_WORD = re.compile(r"'[A-Za-z0-9_ .:/-]{0,64}'|\"[A-Za-z0-9_ .:/-]{0,64}\"")
LUA_NUMBER = re.compile(r'-?\d{1,12}(?:\.\d{1,12})?')
LUA_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_]{0,63}')
SET_VALUE_MAX = 400
TABLE_DEPTH_MAX = 2
MINUTES_MAX = 600
SESSION_MARKS = (('capture client pid', 0.15), ('saved: ', 0.7), (' play it with', 0.95))
CAPTURE_TYPICAL_S = 180   # a capture's usual length (ns1: 2.5 minutes), for the progress between its marks
SESSION_TIME_TEXT = 'about 2 minutes'   # what the page says a new session takes (rk16, 25 Nov: 1 min 55 s for 2 players)
# The capture of a new session runs beside an X4 the player looks at (the Multiplayer page) when this PC has this much
# memory free: two X4s, one in its start menu and one making a new game.
BESIDE_FREE_GIB = 6
# The log rows that count restarts (count_restarts): the player's X4 closed so that another could start, and a session
# made while no X4 of the player's was open (its capture X4 came and went in the gap).
RESTART_MARK = 'X4 RESTART'
SESSION_ALONE_MARK = 'X4 SESSION MADE alone'
SESSION_BESIDE_MARK = 'X4 SESSION MADE beside'
# A session kept in a release kit (make_release_kit.py --sessions, --prebuild-sessions): these files and the overlay
# folder, nothing of the PC that made it (no capture overlay, no base overlay, no logs, no paths).
SHIPPED_FILES = (SESSION_JSON, SESSION_SAVE, 'spec.json')
SHIPPED_FOLDER = 'overlay'
OVERLAY_MANIFEST = 'x4lane-manifest.json'


class KitError(SystemExit):
    """A kit or a session file is not what it must be; the text says why (shown on the page)."""


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        for block in iter(lambda: source.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


@dataclass
class SessionFiles:
    """A session as this PC plays it: its overlay (built for this PC), the session save and the overlay's recipe."""
    overlay: Path
    snapshot: Path
    recipe: dict
    players: int
    sha256: str              # the session save's

    @property
    def session(self):
        return self.recipe.get('session')


def kit_root_of_checkout(root=None):
    """The kit around this checkout (a kit's repo/ folder), or None when the checkout is not in a kit."""
    root = Path(root or session.ROOT)
    parent = root.parent
    if root.name == 'repo' and ((parent / RELEASE_FILE).is_file() or (parent / 'kit-manifest.json').is_file()):
        return parent
    return None


class Kit:
    """What this launcher plays with (see the module text). root: the kit folder (None: this checkout alone)."""

    def __init__(self, root=None, overlay=None, snapshot=None, gate_dll=None):
        self.root = Path(root) if root else None
        self.overlay = Path(overlay) if overlay else self.find_overlay()
        self.snapshot = Path(snapshot) if snapshot else self.find_snapshot()
        self.given_dll = Path(gate_dll) if gate_dll else None
        self.release_info = self.read_release()

    def find_overlay(self):
        if self.root and (self.root / 'overlay' / RECIPE).is_file():
            return self.root / 'overlay'
        return None

    def find_snapshot(self):
        if not self.root or not (self.root / 'snapshot').is_dir():
            return None
        saves = sorted((self.root / 'snapshot').glob('*.xml.gz'))
        return saves[0] if saves else None

    def read_release(self):
        path = self.root / RELEASE_FILE if self.root else None
        if not path or not path.is_file():
            return None
        try:
            data = json.loads(path.read_text(encoding='utf-8'))
        except ValueError:
            raise KitError(f'{path} is not JSON') from None
        if not isinstance(data, dict) or not RELEASE.fullmatch(str(data.get('release', ''))):
            raise KitError(f'{path} names no release')
        return data

    @property
    def server_file(self):
        path = self.root / 'server.json' if self.root else None
        return path if path and path.is_file() else None

    @property
    def kind(self):
        if self.server_file:
            return 'server'
        if self.release_info:
            return 'release'
        if self.overlay and self.snapshot:
            return 'playtest'
        return 'checkout'

    @property
    def gate_dll(self):
        """This kit's own gate DLL: the one a joiner's overlay is built with."""
        candidates = [self.given_dll]
        if self.root:
            candidates.append(self.root / 'native' / GATE_DLL)
        if self.overlay:
            candidates.append(self.overlay / 'native' / GATE_DLL)
        for path in candidates:
            if path and path.is_file():
                return path
        return None

    @property
    def release(self):
        """What two players must have alike (NETPLAY.md "Releases"): a release kit's release; a playtest kit's own
        identity (kit- and its overlay and snapshot); this checkout's gate DLL (dev-)."""
        if self.release_info:
            return str(self.release_info['release'])
        if self.overlay and (self.overlay / 'x4lane-manifest.json').is_file():
            recipe = read_json(self.overlay / RECIPE)
            return 'kit-' + lobby.kit_id(self.overlay, recipe.get('snapshot_sha'))[:24]
        dll = self.gate_dll
        return 'dev-' + (sha256_file(dll)[:12] if dll else 'nogate')

    @property
    def kit_id(self):
        """The older lobby's kit identity (an overlay and its snapshot), for joiners of the same playtest kit."""
        if not self.overlay or not (self.overlay / 'x4lane-manifest.json').is_file():
            return None
        recipe = read_json(self.overlay / RECIPE)
        return lobby.kit_id(self.overlay, recipe.get('snapshot_sha'))

    @property
    def sessions(self):
        """Where sessions are kept: a release kit's sessions/ (NETPLAY.md); a playtest kit and a checkout keep them in
        .local/ (a playtest kit's folder is checked file by file against its manifest)."""
        if self.kind == 'release':
            return self.root / 'sessions'
        return session.ROOT / '.local' / 'netplay' / 'sessions'

    def own_session(self):
        """The kit's own session (a playtest kit's overlay and snapshot), or None."""
        if not self.overlay or not self.snapshot:
            return None
        return session_of_overlay(self.overlay, self.snapshot)


def read_json(path):
    try:
        data = json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        raise KitError(f'{path} is missing or not JSON') from None
    if not isinstance(data, dict):
        raise KitError(f'{path} is not a JSON object')
    return data


def session_of_overlay(overlay, snapshot):
    recipe = read_json(Path(overlay) / RECIPE)
    players = recipe.get('players')
    if not isinstance(players, int):
        raise KitError(f'{overlay}: the recipe names no players')
    sha = str(recipe.get('snapshot_sha') or '') or sha256_file(snapshot)
    return SessionFiles(overlay=Path(overlay), snapshot=Path(snapshot), recipe=recipe, players=players, sha256=sha)


# ---- the host's session ----
# The form of the sessions x4ls new-session makes, in the hosted session's folder name: 'n' since 24 Nov (every player
# in the start's own ship at its place, no capture fixture: native_gate_session_start.py); a session made before is
# not reused (its players 2..N flew fixture fighters beside a battle, slot 1 warped off its start).
HOST_SESSION_FORM = 'n'


def host_session_key(kit, players, mode, base_start, open_slots=()):
    dll = kit.gate_dll
    dll_part = sha256_file(dll)[:8] if dll else 'nogate'
    opened = f'-open{"_".join(map(str, sorted(open_slots)))}' if open_slots else ''
    return f'host-{base_start}-{players}p{opened}-{mode}-{dll_part}-{HOST_SESSION_FORM}'


def cached_session(kit, players, mode=HOST_SESSION_MODE, base_start=HOST_SESSION_START, open_slots=()):
    """A session x4ls new-session made here before for this many players (these open slots), with this kit's gate
    DLL; None if none."""
    folder = kit.sessions / host_session_key(kit, players, mode, base_start, open_slots)
    if not all((folder / name).is_file() for name in (SESSION_JSON, SESSION_SAVE)) or not (folder / 'overlay' / RECIPE).is_file():
        return None
    if read_json(folder / SESSION_JSON).get('fixture_form') != 'none':
        return None
    files = session_of_overlay(folder / 'overlay', folder / SESSION_SAVE)
    dll = kit.gate_dll
    if not dll or files.recipe.get('gate_dll_sha') != sha256_file(dll) or files.players != players:
        return None
    if sorted(files.recipe.get('open') or []) != sorted(open_slots):
        return None
    return files


def new_session_command(kit, players, mode, base_start, out, steam_fix=False, open_slots=(), game=None, lanes=None,
                        focus=True, display=None, gate_dll=None):
    """x4ls new-session for a hosted game: every slot starts in the session start's own ship; open slots (drop-in) start
    absent and are joined while the game runs. game and lanes: the lobby's own (its capture view goes beside its lane,
    not into the default lanes, where another session's capture may be). focus False: the capture's window never takes
    the keyboard focus (it runs beside an X4 the player looks at); display: its monitor. gate_dll: another file than the
    kit's own (make_release_kit.py, before the kit is written)."""
    command = [sys.executable, str(session.ROOT / 'tools' / 'x4ls.py'), 'new-session', '--base-start', base_start,
               '--players', str(players), '--mode', mode, '--gate-dll', str(gate_dll or kit.gate_dll), '--out', str(out)]
    if open_slots:
        command += ['--open', ','.join(map(str, sorted(open_slots)))]
    if game:
        command += ['--game', str(game)]
    if lanes:
        command += ['--lanes', str(lanes)]
    if steam_fix:
        command.append('--steam-fix')
    if not focus:
        command.append('--no-focus')
    if display:
        command += ['--display', str(int(display))]
    return command


def beside_fits(free_gib=BESIDE_FREE_GIB, status=None):
    """True when this PC has the memory for a session's capture X4 beside an X4 that is open now (free physical memory
    at least free_gib). status: a test's stand-in for GlobalMemoryStatusEx ((available bytes) or None). Off Windows, or
    when Windows cannot be asked: True (the capture is tried; a failed one is made again with the other X4 closed)."""
    available = status() if status else available_memory()
    if available is None:
        return True
    return available >= free_gib * (1 << 30)


def available_memory():
    """Free physical memory in bytes (GlobalMemoryStatusEx), or None."""
    if sys.platform != 'win32':
        return None
    import ctypes

    class MemoryStatus(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong), ('total', ctypes.c_ulonglong),
                    ('available', ctypes.c_ulonglong), ('total_page', ctypes.c_ulonglong),
                    ('available_page', ctypes.c_ulonglong), ('total_virtual', ctypes.c_ulonglong),
                    ('available_virtual', ctypes.c_ulonglong), ('available_extended', ctypes.c_ulonglong)]
    status = MemoryStatus()
    status.length = ctypes.sizeof(MemoryStatus)
    try:
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return None
    except (OSError, AttributeError):
        return None
    return int(status.available)


def count_restarts(text):
    """How often a player's X4 went away on the way into a game, from the log rows of one press (the helper's
    helper.log, the lobby's launcher.log, or both joined): every RESTART_MARK row (an X4 the player looked at closed so
    that another could start) and every SESSION_ALONE_MARK row (a session made while none of the player's X4s was open:
    its capture X4 came and went in that gap). One restart is the least a Steam-started X4 needs (its profile is the
    player's own); an X4 already in the multiplayer profile needs none."""
    rows = str(text or '').splitlines()
    return sum(1 for row in rows if RESTART_MARK in row or SESSION_ALONE_MARK in row)


def milestone_key(kit, milestone, keep_flight=False):
    """The folder name of a session continued from a milestone save: the save's and this kit's gate DLL's hashes, and the
    form of the copy (r: the ships at rest, a session made before that is not taken again; f: their flight kept)."""
    dll = kit.gate_dll
    return f"continue-{'f' if keep_flight else 'r'}-{sha256_file(milestone)[:16]}-{sha256_file(dll)[:8] if dll else 'nogate'}"


def milestone_players(milestone, session=None):
    """The players of the session a milestone save belongs to (its sidecar), for x4ls host --milestone's default."""
    from . import milestone as continuing
    try:
        return int(continuing.read_sidecar(milestone, session)['session']['players'])
    except SystemExit as error:
        raise KitError(str(error)) from None
    except (KeyError, TypeError, ValueError):
        raise KitError(f'{milestone}: its sidecar names no players') from None


def milestone_session(kit, milestone, players, open_slots=(), progress=None, log=print, prepare=None, session=None, overlay=None,
                      keep_flight=False):
    """x4ls host --milestone FILE: the session to host made from a milestone save (x4ls continue: every player as saved;
    no X4 runs, the save is used as it is) and kept in the kit's sessions/ by the save's and the gate's hash, so a second
    game from it needs nothing new. Joiners download its three files as any hosted session's and build their overlay from
    its recipe, the continued session's own block included (session.json 'continue', checked on their PC)."""
    from . import milestone as continuing
    if kit.gate_dll is None:
        raise KitError('this kit has no gate DLL (native/x4lan_gate_64.dll): it cannot make a session')
    if open_slots:
        raise KitError('a game continued from a milestone has no drop-in slots (its milestones are saved without them)')
    wanted = milestone_players(milestone, session)
    if players != wanted:
        raise KitError(f'the milestone is a game of {wanted} players, not {players}')
    key = milestone_key(kit, milestone, keep_flight)
    final = kit.sessions / key
    if (final / 'overlay' / RECIPE).is_file() and (final / SESSION_SAVE).is_file():
        files = session_of_overlay(final / 'overlay', final / SESSION_SAVE)
        if files.recipe.get('gate_dll_sha') == sha256_file(kit.gate_dll) and files.players == players:
            return files
        shutil.rmtree(final)
    kit.sessions.mkdir(parents=True, exist_ok=True)
    work = kit.sessions / f'{key}.new-{int(time.time())}'
    if progress:
        progress(0.2)
    try:
        (prepare or continuing.prepare)(continuing.ContinueOptions(milestone=Path(milestone), gate_dll=kit.gate_dll, out=work,
                                                                   session=session, overlay=overlay, keep_flight=keep_flight), log=log)
    except SystemExit as error:
        shutil.rmtree(work, ignore_errors=True)
        raise KitError(f'The game could not be continued from {Path(milestone).name}: {error}') from None
    if final.exists():
        shutil.rmtree(final)
    work.rename(final)
    if progress:
        progress(1.0)
    return session_of_overlay(final / 'overlay', final / SESSION_SAVE)


def make_session(kit, players, mode=HOST_SESSION_MODE, base_start=HOST_SESSION_START, progress=None, log=print,
                 cancelled=None, popen=None, steam_fix=False, open_slots=(), game=None, lanes=None, beside=False,
                 display=None):
    """Make the session to host (x4ls new-session: a capture client on the capture profile) and keep it in the kit's
    sessions/. progress(fraction) follows new-session's lines. Returns SessionFiles.
    beside: an X4 the player looks at stays open meanwhile (the Multiplayer page, which shows the progress): the capture
    is told so (X4LS_BESIDE=1) and its window never takes the keyboard focus. The caller frees that X4's instance mutex
    first (a launcher client's is free once it reached its start menu; a Steam-started X4's: steamplay). Without beside,
    no X4 of the player's runs: the capture then is the only X4 (SESSION_ALONE_MARK, a restart in count_restarts)."""
    if kit.gate_dll is None:
        raise KitError('this kit has no gate DLL (native/x4lan_gate_64.dll): it cannot make a session')
    key = host_session_key(kit, players, mode, base_start, open_slots)
    final = kit.sessions / key
    work = kit.sessions / f'{key}.new-{int(time.time())}'
    kit.sessions.mkdir(parents=True, exist_ok=True)
    command = new_session_command(kit, players, mode, base_start, work, steam_fix, open_slots, game, lanes,
                                  focus=not beside and display is None, display=display)
    env = dict(os.environ, X4LS_BESIDE='1') if beside else None
    log(f'{SESSION_BESIDE_MARK if beside else SESSION_ALONE_MARK}: {players} players'
        + (f', drop-in slots {",".join(map(str, sorted(open_slots)))}' if open_slots else '')
        + (' (the X4 the player looks at stays open)' if beside else ' (no X4 of the player is open meanwhile)'))
    log('netplay: making the session: ' + ' '.join(command[1:]))
    started = time.time()
    reached = 0.05
    tail = []
    popen = popen or subprocess.Popen   # looked up now, so that a test's patch of subprocess applies
    options = {'env': env} if env is not None else {}
    proc = popen(command, cwd=session.ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                 encoding='utf-8', errors='replace', **options)
    try:
        for line in proc.stdout:
            tail = (tail + [line.rstrip()])[-20:]
            for mark, fraction in SESSION_MARKS:
                if mark in line:
                    reached = max(reached, fraction)
            if progress:
                progress(session_progress(reached, time.time() - started))
            if cancelled is not None and cancelled.is_set():
                proc.kill()
                break
        code = proc.wait()
    finally:
        if proc.poll() is None:
            proc.kill()
    if cancelled is not None and cancelled.is_set():
        shutil.rmtree(work, ignore_errors=True)
        raise KitError('cancelled')
    if code:
        shutil.rmtree(work, ignore_errors=True)
        raise KitError('The session could not be made: ' + ' | '.join(line for line in tail[-3:] if line))
    if final.exists():
        shutil.rmtree(final)
    work.rename(final)
    portable_session(final)   # its files named the work folder, which is gone now
    if progress:
        progress(1.0)
    return session_of_overlay(final / 'overlay', final / SESSION_SAVE)


# ---- a session as a release kit ships it ----
def portable_snapshot(data):
    """session.json's form without the path of the PC that made it: the save is named by its file name (beside
    session.json, and beside the overlay folder). Returns (data, changed)."""
    if not isinstance(data, dict) or not isinstance(data.get('snapshot'), dict):
        return data, False
    if data['snapshot'].get('path') == SESSION_SAVE:
        return data, False
    out = json.loads(json.dumps(data))
    out['snapshot']['path'] = SESSION_SAVE
    return out, True


def write_session_json(path, data):
    Path(path).write_text(json.dumps(data, indent=1, sort_keys=True) + '\n', encoding='utf-8')


def portable_session(folder):
    """A kept session (folder: session.json, the save, overlay/) without this PC's paths: session.json, the overlay's
    session.json and its recipe's session name the save by its file name; the overlay's manifest follows the files that
    changed. Nothing X4 reads changes (the recipe and session.json are the launcher's), and a session made since 24 Nov
    named its work folder there, which make_session renames anyway. Returns the files rewritten."""
    folder = Path(folder)
    changed = []
    for path in (folder / SESSION_JSON, folder / SHIPPED_FOLDER / SESSION_JSON):
        if path.is_file():
            data, rewritten = portable_snapshot(read_json(path))
            if rewritten:
                write_session_json(path, data)
                changed.append(path)
    recipe_path = folder / SHIPPED_FOLDER / RECIPE
    if recipe_path.is_file():
        recipe = read_json(recipe_path)
        data, rewritten = portable_snapshot(recipe.get('session'))
        if rewritten:
            recipe['session'] = data
            write_session_json(recipe_path, recipe)
            changed.append(recipe_path)
    manifest_path = folder / SHIPPED_FOLDER / OVERLAY_MANIFEST
    if changed and manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        for path in changed:
            if path.parent == folder / SHIPPED_FOLDER and path.name in manifest:
                manifest[path.name] = sha256_file(path)
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')   # as native_gate_scenarios.write_manifest
    return changed


def ship_session(source, target):
    """A host session as a release kit holds it (make_release_kit.py): from the folder x4ls new-session made, only
    SHIPPED_FILES and the overlay (no capture overlay, no base overlay), and portable_session's form. Returns target."""
    source, target = Path(source), Path(target)
    if target.exists():
        raise KitError(f'{target} exists')
    if not (source / SHIPPED_FOLDER / RECIPE).is_file() or not (source / SESSION_SAVE).is_file():
        raise KitError(f'{source} is not a session folder (no overlay/{RECIPE} or {SESSION_SAVE})')
    target.mkdir(parents=True)
    for name in SHIPPED_FILES:
        if (source / name).is_file():
            shutil.copyfile(source / name, target / name)
    shutil.copytree(source / SHIPPED_FOLDER, target / SHIPPED_FOLDER, ignore=shutil.ignore_patterns('__pycache__'))
    portable_session(target)
    return target


def host_sessions(kit):
    """{folder name: SessionFiles} of the host sessions in the kit's sessions/ that this kit can host as they are
    (cached_session's checks: its gate DLL, no fixture, the players and open slots of its name)."""
    found = {}
    root = kit.sessions
    if not root.is_dir() or kit.gate_dll is None:
        return found
    for folder in sorted(root.glob('host-*')):
        match = HOST_KEY.fullmatch(folder.name)
        if not folder.is_dir() or not match:
            continue
        opened = tuple(int(slot) for slot in match.group('open').split('_')) if match.group('open') else ()
        try:
            files = cached_session(kit, int(match.group('players')), match.group('mode'), match.group('start'), opened)
        except KitError:
            files = None
        if files is not None:
            found[folder.name] = files
    return found


HOST_KEY = re.compile(r'host-(?P<start>[a-z0-9_]+)-(?P<players>[2-8])p(?:-open(?P<open>[2-8](?:_[2-8])*))?'
                      r'-(?P<mode>coop|factions)-(?P<dll>[0-9a-f]{8}|nogate)-' + HOST_SESSION_FORM)
JOINED_KEY = re.compile(r'joined-[0-9a-f]{16}-[0-9a-f]{12}')


def joined_sessions(kit):
    """{folder name: SessionFiles} of the games this kit joined before whose overlay was built here with this kit's gate
    DLL (joined_session's folders), newest first."""
    found = {}
    root = kit.sessions
    if not root.is_dir() or kit.gate_dll is None:
        return found
    dll_sha = sha256_file(kit.gate_dll)
    folders = [folder for folder in root.glob('joined-*') if JOINED_KEY.fullmatch(folder.name)]
    for folder in sorted(folders, key=lambda path: path.stat().st_mtime, reverse=True):
        overlay = folder / SHIPPED_FOLDER
        if not (overlay / RECIPE).is_file() or not (overlay / OVERLAY_MANIFEST).is_file() or not (folder / SESSION_SAVE).is_file():
            continue
        try:
            files = session_of_overlay(overlay, folder / SESSION_SAVE)
        except KitError:
            continue
        if files.recipe.get('gate_dll_sha') == dll_sha:
            found[folder.name] = files
    return found


def session_progress(reached, elapsed):
    """Between new-session's marks the capture runs about three minutes: the bar moves with the time meanwhile."""
    if reached >= 0.7:
        return reached
    estimate = 0.15 + 0.55 * min(1.0, elapsed / CAPTURE_TYPICAL_S)
    return min(0.69, max(reached, estimate if reached >= 0.15 else reached))


def sanitized(data):
    """session.json (or a recipe's session) without this PC's paths: the save is named by its file name only
    (NETPLAY.md: nothing personal is published, no paths)."""
    if not isinstance(data, dict):
        return data
    out = json.loads(json.dumps(data))
    snapshot = out.get('snapshot')
    if isinstance(snapshot, dict) and 'path' in snapshot:
        snapshot['path'] = SESSION_SAVE
    return out


def upload_files(files, folder):
    """The host's three files for the master (or for LAN joiners), in folder: {name: path}. session.json and the recipe
    are copies without paths; the save is the session's own file."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    recipe = dict(files.recipe)
    recipe['session'] = sanitized(recipe.get('session'))
    described = recipe['session'] or {'format': 'x4ls-netplay-session-1', 'players': files.players,
                                      'mode': recipe.get('mode'), 'snapshot': {'path': SESSION_SAVE, 'sha256': files.sha256}}
    (folder / RECIPE).write_text(json.dumps(recipe, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    (folder / SESSION_JSON).write_text(json.dumps(described, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    return {SESSION_JSON: folder / SESSION_JSON, SESSION_SAVE: Path(files.snapshot), RECIPE: folder / RECIPE}


def described_files(paths):
    """{name: {size, sha256}} of upload_files' result (what a LAN host tells its joiners)."""
    return {name: {'size': Path(path).stat().st_size, 'sha256': sha256_file(path)} for name, path in paths.items()}


# ---- the joiner's checks ----
def lua_literal_ok(text, depth=0):
    """True for a Lua literal of plain data: a number, true/false, a quoted word, or a table of those."""
    text = text.strip()
    if text in ('true', 'false') or LUA_NUMBER.fullmatch(text) or LUA_WORD.fullmatch(text):
        return True
    if not (text.startswith('{') and text.endswith('}')) or depth >= TABLE_DEPTH_MAX:
        return False
    inner = text[1:-1].strip()
    if not inner:
        return True
    return all(lua_literal_ok(item, depth + 1) for item in split_table(inner))


def split_table(inner):
    """The items of a table literal's inside, split at its own commas (not those of a nested table)."""
    items = []
    level = 0
    current = ''
    for ch in inner:
        if ch == '{':
            level += 1
        elif ch == '}':
            level -= 1
        if ch == ',' and level == 0:
            items.append(current)
            current = ''
        else:
            current += ch
    items.append(current)
    return items


def safe_set_item(item):
    """A recipe's --set item (native_gate_KEY=VALUE) that is a gate key and a data literal."""
    if not isinstance(item, str) or len(item) > SET_VALUE_MAX or '\n' in item or '\r' in item:
        return False
    key, equals, value = item.partition('=')
    return bool(equals) and bool(SET_KEY.fullmatch(key.strip())) and lua_literal_ok(value)


def validate_recipe(data):
    """A host's play-recipe.json checked field by field (build_play_overlay.read_recipe's checks, and every value that
    reaches the config as Lua is plain data). Returns it; KitError names the first bad field."""
    if not isinstance(data, dict):
        raise KitError('the host\'s recipe is not a JSON object')
    checks = (
        ('kind', data.get('kind') == 'x4ls-play-recipe' and data.get('version') == 1),
        ('role', data.get('role', 'session') == 'session'),
        ('players', type(data.get('players')) is int and 2 <= data['players'] <= 8),
        ('mode', data.get('mode') in ('coop', 'factions')),
        ('minutes', isinstance(data.get('minutes'), (int, float)) and 0 < data['minutes'] <= MINUTES_MAX),
        ('set', isinstance(data.get('set'), list) and all(safe_set_item(item) for item in data['set'])),
        ('open', isinstance(data.get('open'), list)
         and all(type(slot) is int and 2 <= slot <= data.get('players', 0) for slot in data['open'])),
        ('join_mode', data.get('join_mode') in ('report', 'strict')),
        ('lua_block', isinstance(data.get('lua_block'), str)
         and all(LUA_NAME.fullmatch(name.strip()) for name in data['lua_block'].split(',') if name.strip())),
        ('full_logs', isinstance(data.get('full_logs'), bool)),
        ('mixed_platform', isinstance(data.get('mixed_platform', False), bool)),
        ('save_every', isinstance(data.get('save_every', 0), (int, float)) and 0 <= data.get('save_every', 0) <= MINUTES_MAX),
        ('gate_dll_sha', isinstance(data.get('gate_dll_sha'), str) and bool(HEX64.fullmatch(data['gate_dll_sha']))),
        ('snapshot_sha', isinstance(data.get('snapshot_sha'), str) and bool(HEX64.fullmatch(data['snapshot_sha']))),
        ('session', data.get('session') is None or isinstance(data.get('session'), dict)),
    )
    for name, ok in checks:
        if not ok:
            raise KitError(f'the host\'s recipe is refused: {name}')
    return data


def check_session_files(folder, own_dll_sha, expected=None):
    """The downloaded files of a game, checked: each one's SHA-256 against what the master or the host announced
    (expected: {name: {size, sha256}}), the save against the recipe and session.json, the recipe as data only, and the
    gate the host built with against this kit's (another release is refused). Returns the recipe."""
    folder = Path(folder)
    for name in SESSION_FILES:
        if not (folder / name).is_file():
            raise KitError(f'the host\'s {name} is missing')
        if expected and name in expected and sha256_file(folder / name) != expected[name].get('sha256'):
            raise KitError(f'the host\'s {name} does not match its SHA-256')
    recipe = validate_recipe(read_json(folder / RECIPE))
    save_sha = sha256_file(folder / SESSION_SAVE)
    if recipe['snapshot_sha'] != save_sha:
        raise KitError('the host\'s session save is not the one its recipe was built for')
    if recipe['gate_dll_sha'] != own_dll_sha:
        raise KitError('The host plays another release (its gate DLL differs from this one): install the host\'s release.')
    described = read_json(folder / SESSION_JSON)
    if recipe.get('session') is not None and sanitized(described) != sanitized(recipe['session']):
        raise KitError('the host\'s session.json and its recipe name different sessions')
    snapshot = described.get('snapshot') if isinstance(described.get('snapshot'), dict) else {}
    if snapshot.get('sha256') not in (None, save_sha):
        raise KitError('the host\'s session.json names another session save')
    return recipe


def overlay_arguments(recipe, save, gate_dll, out, session_json=None):
    """tools/build_play_overlay.py's arguments for the host's recipe with this PC's save copy and gate DLL."""
    arguments = ['--snapshot', str(save), '--gate-dll', str(gate_dll), '--out', str(out),
                 '--players', str(recipe['players']), '--mode', recipe['mode'], '--minutes', repr(recipe['minutes']),
                 '--lua-block', recipe['lua_block'], '--join-mode', recipe['join_mode'],
                 '--save-every', repr(recipe.get('save_every', 0))]
    if recipe['open']:
        arguments += ['--open', ','.join(str(slot) for slot in recipe['open'])]
    if recipe['full_logs']:
        arguments.append('--full-logs')
    if recipe.get('mixed_platform'):
        arguments.append('--mixed-platform')
    if session_json:
        arguments += ['--session', str(session_json)]
    for item in recipe['set']:
        arguments += ['--set', item]
    return arguments


def build_overlay(recipe, save, gate_dll, out, progress=None, log=print, run=None):
    """The joiner's overlay, built here: this checkout's build_play_overlay.py with the host's recipe (checked), this
    PC's copy of the save and this kit's own gate DLL. Returns the overlay folder."""
    out = Path(out)
    session_json = None
    if recipe.get('session') is not None:
        session_json = out.parent / 'build-session.json'   # as hotjoin.build_joiner_overlay writes it
        session_json.write_text(json.dumps(recipe['session'], indent=1, sort_keys=True) + '\n', encoding='utf-8')
    command = [sys.executable, str(session.ROOT / 'tools' / 'build_play_overlay.py'),
               *overlay_arguments(recipe, Path(save).resolve(), Path(gate_dll).resolve(), out.resolve(), session_json)]
    log(f'netplay: building the overlay in {out}')
    if progress:
        progress(0.1)
    run = run or subprocess.run   # looked up now, so that a test's patch of subprocess applies
    result = run(command, cwd=session.ROOT, capture_output=True, text=True)
    if result.returncode:
        log((result.stdout or '')[-2000:] + (result.stderr or '')[-2000:])
        raise KitError(f'The overlay for this game could not be built here ({result.returncode}).')
    if progress:
        progress(1.0)
    return out


def joined_folder(kit, recipe_sha, save_sha):
    """Where a joined game's files and overlay are kept: one folder per session and recipe, so a second game of the
    same session needs no download and no build."""
    return kit.sessions / f'joined-{save_sha[:16]}-{recipe_sha[:12]}'


def joined_session(kit, folder, recipe, progress=None, log=print, run=None):
    """The joiner's SessionFiles of checked files in folder: the overlay built once (kept in folder/overlay)."""
    folder = Path(folder)
    overlay = folder / 'overlay'
    dll_sha = sha256_file(kit.gate_dll)
    if (overlay / RECIPE).is_file():
        built = read_json(overlay / RECIPE)
        if built.get('gate_dll_sha') == dll_sha and built.get('snapshot_sha') == recipe['snapshot_sha']:
            return session_of_overlay(overlay, folder / SESSION_SAVE)
        shutil.rmtree(overlay)
        shutil.rmtree(folder / 'overlay.base', ignore_errors=True)
    shutil.rmtree(folder / 'overlay.base', ignore_errors=True)
    build_overlay(recipe, folder / SESSION_SAVE, kit.gate_dll, overlay, progress, log, run)
    return session_of_overlay(overlay, folder / SESSION_SAVE)
