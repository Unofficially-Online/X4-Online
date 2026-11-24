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
def host_session_key(kit, players, mode, base_start, open_slots=()):
    dll = kit.gate_dll
    dll_part = sha256_file(dll)[:8] if dll else 'nogate'
    opened = f'-open{"_".join(map(str, sorted(open_slots)))}' if open_slots else ''
    return f'host-{base_start}-{players}p{opened}-{mode}-{dll_part}'


def cached_session(kit, players, mode=HOST_SESSION_MODE, base_start=HOST_SESSION_START, open_slots=()):
    """A session x4ls new-session made here before for this many players (these open slots), with this kit's gate
    DLL; None if none."""
    folder = kit.sessions / host_session_key(kit, players, mode, base_start, open_slots)
    if not all((folder / name).is_file() for name in (SESSION_JSON, SESSION_SAVE)) or not (folder / 'overlay' / RECIPE).is_file():
        return None
    files = session_of_overlay(folder / 'overlay', folder / SESSION_SAVE)
    dll = kit.gate_dll
    if not dll or files.recipe.get('gate_dll_sha') != sha256_file(dll) or files.players != players:
        return None
    if sorted(files.recipe.get('open') or []) != sorted(open_slots):
        return None
    return files


def new_session_command(kit, players, mode, base_start, out, steam_fix=False, open_slots=(), game=None, lanes=None):
    """x4ls new-session for a hosted game: every slot starts with the session's own start; open slots (drop-in) start
    absent and are joined while the game runs. game and lanes: the lobby's own (its capture view goes beside its lane,
    not into the default lanes, where another session's capture may be)."""
    command = [sys.executable, str(session.ROOT / 'tools' / 'x4ls.py'), 'new-session', '--base-start', base_start,
               '--players', str(players), '--mode', mode, '--gate-dll', str(kit.gate_dll), '--out', str(out)]
    if open_slots:
        command += ['--open', ','.join(map(str, sorted(open_slots)))]
    if game:
        command += ['--game', str(game)]
    if lanes:
        command += ['--lanes', str(lanes)]
    if steam_fix:
        command.append('--steam-fix')
    return command


def make_session(kit, players, mode=HOST_SESSION_MODE, base_start=HOST_SESSION_START, progress=None, log=print,
                 cancelled=None, popen=None, steam_fix=False, open_slots=(), game=None, lanes=None):
    """Make the session to host (x4ls new-session: a capture client on the capture profile, so X4 must be closed) and
    keep it in the kit's sessions/. progress(fraction) follows new-session's lines. Returns SessionFiles."""
    if kit.gate_dll is None:
        raise KitError('this kit has no gate DLL (native/x4lan_gate_64.dll): it cannot make a session')
    key = host_session_key(kit, players, mode, base_start, open_slots)
    final = kit.sessions / key
    work = kit.sessions / f'{key}.new-{int(time.time())}'
    kit.sessions.mkdir(parents=True, exist_ok=True)
    command = new_session_command(kit, players, mode, base_start, work, steam_fix, open_slots, game, lanes)
    log('netplay: making the session: ' + ' '.join(command[1:]))
    started = time.time()
    reached = 0.05
    tail = []
    popen = popen or subprocess.Popen   # looked up now, so that a test's patch of subprocess applies
    proc = popen(command, cwd=session.ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                 encoding='utf-8', errors='replace')
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
    if progress:
        progress(1.0)
    return session_of_overlay(final / 'overlay', final / SESSION_SAVE)


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
