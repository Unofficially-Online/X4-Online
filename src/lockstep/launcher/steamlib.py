"""X4 Online in the player's Steam: an "X4 Online" entry in the library (a non-Steam game shortcut that starts the
zero-restart path, helper.pyw --play) and, when the player turns it on, X4's own Play starting X4 Online (X4's launch
options, app 392160). Nothing of X4's game files is touched.

Steam keeps both per Steam user in its userdata folder: config/shortcuts.vdf (binary KeyValues) and
config/localconfig.vdf (text KeyValues). Steam reads them at its start and writes them again while it runs and when it
exits, so they are written here only while Steam is closed (the helper waits for such a moment, or the player lets it
restart Steam). Every write is whole-file and atomic, and a copy of the file as it was before X4 Online first changed it
is kept beside it (<name>.x4online-backup). What X4 Online adds is recorded (state.json of the kit) so that it is taken out
again exactly: the shortcut by its name and program, the launch options back to what they were before (or none).

Tests use copies of these files, never the player's."""
import copy
import json
import os
import re
import subprocess
import sys
import zlib
from pathlib import Path

APP_ID = '392160'
SHORTCUT_NAME = 'X4 Online'
BACKUP_SUFFIX = '.x4online-backup'
HIDDEN = 0x08000000

# ---- binary KeyValues (shortcuts.vdf) ----
# A map is a run of entries: one type byte, the key (NUL-terminated), the value; 0x08 ends the map. Types: 0x00 a map,
# 0x01 a string (NUL-terminated), 0x02 an int32, 0x03 a float32, 0x07 a uint64, 0x0a an int64. Values are kept as
# (type, value) so that what this module does not change is written back as it was.
MAP, STRING, INT32, FLOAT32, UINT64, INT64, END = 0x00, 0x01, 0x02, 0x03, 0x07, 0x0A, 0x08
FIXED = {INT32: 4, FLOAT32: 4, UINT64: 8, INT64: 8}


class VdfError(ValueError):
    """A KeyValues file this module cannot read; it is then left as it is."""


def read_binary(data):
    """shortcuts.vdf's bytes as {key: (type, value)} (maps nested the same way)."""
    return read_binary_tail(data)[0]


def read_binary_tail(data):
    """(read_binary's map, the bytes after the root's end: none, or one more end marker some writers add)."""
    pos = 0

    def cstring():
        nonlocal pos
        end = data.find(b'\x00', pos)
        if end < 0:
            raise VdfError('a string without its end')
        text = data[pos:end].decode('utf-8', 'surrogateescape')
        pos = end + 1
        return text

    def read_map(depth):
        nonlocal pos
        if depth > 16:
            raise VdfError('maps nested too deep')
        out = {}
        while True:
            if pos >= len(data):
                raise VdfError('a map without its end')
            kind = data[pos]
            pos += 1
            if kind == END:
                return out
            key = cstring()
            if kind == MAP:
                out[key] = (MAP, read_map(depth + 1))
            elif kind == STRING:
                out[key] = (STRING, cstring())
            elif kind in FIXED:
                size = FIXED[kind]
                if pos + size > len(data):
                    raise VdfError('a number cut short')
                out[key] = (kind, data[pos:pos + size])
                pos += size
            else:
                raise VdfError(f'value type {kind:#x}')
    root = read_map(0)
    tail = data[pos:]
    if tail not in (b'', bytes([END])):
        raise VdfError('bytes after the end')
    return root, tail


def write_binary(root, tail=b''):
    out = bytearray()

    def write_map(entries):
        for key, (kind, value) in entries.items():
            out.append(kind)
            out.extend(key.encode('utf-8', 'surrogateescape') + b'\x00')
            if kind == MAP:
                write_map(value)
            elif kind == STRING:
                out.extend(value.encode('utf-8', 'surrogateescape') + b'\x00')
            else:
                out.extend(value)
        out.append(END)
    write_map(root)
    return bytes(out) + tail


def int32(value):
    return (INT32, int(value).to_bytes(4, 'little', signed=True))


def int32_of(entry):
    return int.from_bytes(entry[1], 'little', signed=True) if entry and entry[0] == INT32 else None


def shortcut_appid(exe, name):
    """The id Steam gives a non-Steam game shortcut: the CRC-32 of its program and name, top bit set (as signed int32)."""
    value = zlib.crc32((exe + name).encode('utf-8')) | 0x80000000
    return value - (1 << 32)


def shortcut_entry(name, exe, start_dir, launch_options, icon=''):
    """One shortcut as Steam writes it (its fields in Steam's order). exe and start_dir in quotes, as Steam keeps them."""
    quoted_exe = f'"{exe}"'
    return {
        'appid': int32(shortcut_appid(quoted_exe, name)),
        'AppName': (STRING, name),
        'Exe': (STRING, quoted_exe),
        'StartDir': (STRING, f'"{start_dir}"'),
        'icon': (STRING, icon),
        'ShortcutPath': (STRING, ''),
        'LaunchOptions': (STRING, launch_options),
        'IsHidden': int32(0),
        'AllowDesktopConfig': int32(1),
        'AllowOverlay': int32(1),
        'OpenVR': int32(0),
        'Devkit': int32(0),
        'DevkitGameID': (STRING, ''),
        'DevkitOverrideAppID': int32(0),
        'LastPlayTime': int32(0),
        'FlatpakAppID': (STRING, ''),
        'tags': (MAP, {}),
    }


def ours(entry, name, exe):
    fields = entry[1] if entry[0] == MAP else {}
    return fields.get('AppName', (None, ''))[1] == name and fields.get('Exe', (None, ''))[1].strip('"').lower() == str(exe).lower()


def with_shortcut(root, name, exe, start_dir, launch_options, icon=''):
    """root with the shortcut added or brought up to date (the same name and program: ours); (root, changed)."""
    shortcuts = root.get('shortcuts', (MAP, {}))[1]
    wanted = shortcut_entry(name, exe, start_dir, launch_options, icon)
    for key, entry in shortcuts.items():
        if ours(entry, name, exe):
            fields = dict(entry[1])
            changed = False
            for field in ('Exe', 'StartDir', 'LaunchOptions', 'AllowOverlay', 'appid'):
                if fields.get(field) != wanted[field]:
                    fields[field] = wanted[field]
                    changed = True
            if icon and fields.get('icon') != wanted['icon']:
                fields['icon'] = wanted['icon']
                changed = True
            if not changed:
                return root, False
            shortcuts = dict(shortcuts, **{key: (MAP, fields)})
            return dict(root, shortcuts=(MAP, shortcuts)), True
    index = 0
    while str(index) in shortcuts:
        index += 1
    shortcuts = dict(shortcuts, **{str(index): (MAP, wanted)})
    return dict(root, shortcuts=(MAP, shortcuts)), True


def without_shortcut(root, name, exe):
    """root without our shortcut, the others renumbered 0..n-1 in their order (as Steam numbers them); (root, changed)."""
    shortcuts = root.get('shortcuts', (MAP, {}))[1]
    kept = [entry for entry in shortcuts.values() if not ours(entry, name, exe)]
    if len(kept) == len(shortcuts):
        return root, False
    return dict(root, shortcuts=(MAP, {str(index): entry for index, entry in enumerate(kept)})), True


# ---- text KeyValues (localconfig.vdf) ----
TOKEN = re.compile(r'\s*(?:"((?:[^"\\]|\\.)*)"|(\{)|(\})|(//[^\n]*))', re.S)


def unescape(text):
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), text)


def escape(text):
    return text.replace('\\', '\\\\').replace('"', '\\"')


def read_text(text):
    """localconfig.vdf's text as [(key, value or [children])], in its order (keys may repeat)."""
    pos = 0

    def tokens():
        nonlocal pos
        while True:
            match = TOKEN.match(text, pos)
            if not match:
                if text[pos:].strip():
                    raise VdfError(f'unreadable text at {pos}')
                return
            pos = match.end()
            if match.group(4) is not None:
                continue
            if match.group(1) is not None:
                yield 'string', unescape(match.group(1))
            elif match.group(2):
                yield 'open', None
            else:
                yield 'close', None
    stream = tokens()

    def block(depth):
        out = []
        for kind, value in stream:
            if kind == 'close':
                if depth == 0:
                    raise VdfError('a closing brace too many')
                return out
            if kind != 'string':
                raise VdfError('a brace without a key')
            following = next(stream, (None, None))
            if following[0] == 'string':
                out.append((value, following[1]))
            elif following[0] == 'open':
                out.append((value, block(depth + 1)))
            else:
                raise VdfError(f'the key {value!r} has no value')
        if depth:
            raise VdfError('a block without its end')
        return out
    return block(0)


def write_text(entries, depth=0):
    """[(key, value or [children])] as Steam writes it: tab-indented, a block's braces on their own lines, two tabs between
    a key and its value."""
    lines = []
    indent = '\t' * depth
    for key, value in entries:
        if isinstance(value, list):
            lines.append(f'{indent}"{escape(key)}"\n{indent}{{\n')
            lines.append(write_text(value, depth + 1))
            lines.append(f'{indent}}}\n')
        else:
            lines.append(f'{indent}"{escape(key)}"\t\t"{escape(value)}"\n')
    return ''.join(lines)


def find_block(entries, path, create=False):
    """The children of the block at path (keys compared without case, as Steam does); None when absent (create: made)."""
    current = entries
    for name in path:
        found = next((value for key, value in current if key.lower() == name.lower() and isinstance(value, list)), None)
        if found is None:
            if not create:
                return None
            found = []
            current.append((name, found))
        current = found
    return current


APP_PATH = ('UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', APP_ID)
ABSENT = None   # recorded for launch options there were none of


def launch_options(entries):
    """X4's launch options in a localconfig.vdf (None when it has none)."""
    app = find_block(entries, APP_PATH)
    if app is None:
        return ABSENT
    return next((value for key, value in app if key.lower() == 'launchoptions' and isinstance(value, str)), ABSENT)


def with_launch_options(entries, value):
    """entries with X4's launch options set to value (ABSENT: the key taken out)."""
    entries = copy.deepcopy(entries)
    app = find_block(entries, APP_PATH, create=value is not ABSENT)
    if app is None:
        return entries
    index = next((i for i, (key, item) in enumerate(app) if key.lower() == 'launchoptions' and isinstance(item, str)), None)
    if value is ABSENT:
        if index is not None:
            del app[index]
    elif index is None:
        app.append(('LaunchOptions', value))
    else:
        app[index] = (app[index][0], value)
    return entries


# ---- files ----
def write_atomic(path, data):
    path = Path(path)
    part = path.with_name(path.name + '.x4online-part')
    part.write_bytes(data)
    os.replace(part, path)


def keep_backup(path):
    """A copy of the file as it was before X4 Online first wrote it (once)."""
    path = Path(path)
    backup = path.with_name(path.name + BACKUP_SUFFIX)
    if path.is_file() and not backup.exists():
        backup.write_bytes(path.read_bytes())


def steam_root():
    """Steam's folder (HKCU Software\\Valve\\Steam SteamPath), or None."""
    if sys.platform != 'win32':
        return None
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
            return Path(winreg.QueryValueEx(key, 'SteamPath')[0])
    except OSError:
        return None


def user_configs(root=None):
    """Every Steam user's config folder (userdata/<account id>/config) under Steam's folder."""
    root = Path(root) if root else steam_root()
    if root is None or not (root / 'userdata').is_dir():
        return []
    return [folder / 'config' for folder in sorted((root / 'userdata').iterdir())
            if folder.name.isdigit() and folder.name != '0' and (folder / 'config').is_dir()]


def steam_running(tasklist=None):
    """True while steam.exe runs (its files are its own then)."""
    if tasklist is not None:
        return 'steam.exe' in tasklist.lower()
    if sys.platform != 'win32':
        return False
    try:   # Popen, not run: the launcher's tests stand in for run() (the overlay build)
        process = subprocess.Popen(['tasklist', '/FI', 'IMAGENAME eq steam.exe', '/FO', 'CSV', '/NH'],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, creationflags=HIDDEN)
        out, _ = process.communicate(timeout=20)
    except (OSError, subprocess.SubprocessError):
        return True   # unknown: as if it ran (nothing is written)
    return b'"steam.exe"' in out.lower()


def add_shortcut(config, name, exe, start_dir, launch_options, icon=''):
    """Our shortcut in config/shortcuts.vdf (a new file when there is none); True when the file changed."""
    path = Path(config) / 'shortcuts.vdf'
    root, tail = read_binary_tail(path.read_bytes()) if path.is_file() else ({'shortcuts': (MAP, {})}, b'')
    root, changed = with_shortcut(root, name, exe, start_dir, launch_options, icon)
    if changed:
        keep_backup(path)
        write_atomic(path, write_binary(root, tail))
    return changed


def remove_shortcut(config, name, exe):
    path = Path(config) / 'shortcuts.vdf'
    if not path.is_file():
        return False
    root, tail = read_binary_tail(path.read_bytes())
    root, changed = without_shortcut(root, name, exe)
    if changed:
        write_atomic(path, write_binary(root, tail))
    return changed


def set_launch_options(config, value):
    """X4's launch options in config/localconfig.vdf set to value (ABSENT: none); (the value before, changed)."""
    path = Path(config) / 'localconfig.vdf'
    if not path.is_file():
        if value is ABSENT:
            return ABSENT, False
        raise VdfError(f'{path} is missing: Steam has not run for this user')
    text = path.read_text(encoding='utf-8')
    entries = read_text(text)
    before = launch_options(entries)
    if before == value:
        return before, False
    keep_backup(path)
    write_atomic(path, write_text(with_launch_options(entries, value)).encode('utf-8'))
    return before, True


# ---- what X4 Online changed, and its settings ----
class SteamSetup:
    """The kit's Steam integration: the library shortcut (always, once Steam is closed) and X4's Play (when the player
    turned it on). state.json: {"shortcut": {config: exe}, "play_wanted": bool, "play": {config: {"ours": text,
    "before": text or null}}}. apply() does what is wanted while Steam is closed; undo() takes everything out again."""

    def __init__(self, kit, state_file=None, configs=None, running=None, log=print):
        self.kit = Path(kit)
        self.state_file = Path(state_file) if state_file else self.kit / 'repo' / '.local' / 'steam.json'
        self.configs = configs if configs is not None else user_configs
        self.running = running or steam_running
        self.log = log

    def read(self):
        try:
            data = json.loads(self.state_file.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            data = {}
        return data if isinstance(data, dict) else {}

    def write(self, data):
        self.state_file.parent.mkdir(parents=True, exist_ok=True)
        write_atomic(self.state_file, (json.dumps(data, indent=1, sort_keys=True) + '\n').encode('utf-8'))

    # what X4 Online writes into Steam
    def pythonw(self):
        own = self.kit / 'python' / 'pythonw.exe'
        return own if own.is_file() else Path(sys.executable).with_name('pythonw.exe')

    def shortcut_options(self):
        return f'"{self.kit / "helper.pyw"}" --play'

    def play_options(self):
        """X4's launch options when Steam's Play starts X4 Online: our launcher, then Steam's own command line."""
        return f'"{self.pythonw()}" "{self.kit / "helper.pyw"}" --steam-play %command%'

    # the player's choice
    def play_wanted(self):
        return bool(self.read().get('play_wanted'))

    def want_play(self, on):
        data = self.read()
        data['play_wanted'] = bool(on)
        self.write(data)

    def view(self):
        """{on, pending, steam_running} for the page's switch; None for a kit that is not installed (no switch)."""
        if not self.installed():
            return None
        data = self.read()
        wanted = bool(data.get('play_wanted'))
        applied = bool(data.get('play'))
        return {'on': wanted, 'pending': wanted != applied, 'steam_running': self.running()}

    def installed(self):
        """Only an installed kit (its manifest and helper.pyw) changes Steam: a checkout, a test's fake kit never do."""
        return (self.kit / 'kit-manifest.json').is_file() and (self.kit / 'helper.pyw').is_file()

    # doing it
    def apply(self, icon=''):
        """While Steam is closed: the shortcut for every Steam user, and X4's Play as the player wants it. False while
        Steam runs, or for a kit that is not installed (nothing written)."""
        if not self.installed() or self.running():
            return False
        data = self.read()
        exe = str(self.pythonw())
        shortcuts = dict(data.get('shortcut') or {})
        play = dict(data.get('play') or {})
        for config in self.configs():
            key = str(config)
            try:
                if add_shortcut(config, SHORTCUT_NAME, exe, str(self.kit), self.shortcut_options(), icon):
                    self.log(f'steam: "{SHORTCUT_NAME}" is in the library of Steam user {Path(config).parent.name}')
                shortcuts[key] = exe
                if data.get('play_wanted') and key not in play:
                    before, _ = set_launch_options(config, self.play_options())
                    play[key] = {'ours': self.play_options(), 'before': before}
                    self.log(f'steam: X4\'s Play starts X4 Online for Steam user {Path(config).parent.name}')
                elif not data.get('play_wanted') and key in play:
                    self.restore_play(config, play.pop(key))
            except (OSError, VdfError) as error:
                self.log(f'steam: {config} left as it is: {error}')
        data['shortcut'] = shortcuts
        data['play'] = play
        self.write(data)
        return True

    def restore_play(self, config, record):
        """X4's launch options back to what they were, unless the player changed them since (then they stay theirs)."""
        path = Path(config) / 'localconfig.vdf'
        if not path.is_file():
            return
        now = launch_options(read_text(path.read_text(encoding='utf-8')))
        if now != record.get('ours'):
            self.log(f'steam: X4\'s launch options of Steam user {Path(config).parent.name} were changed since; left as they are')
            return
        set_launch_options(config, record.get('before'))
        self.log(f'steam: X4\'s Play is Steam\'s own again for Steam user {Path(config).parent.name}')

    def undo(self):
        """Everything X4 Online put into Steam taken out (uninstall). False while Steam runs (nothing written)."""
        if not self.installed():
            return True
        if self.running():
            return False
        data = self.read()
        for key, exe in (data.get('shortcut') or {}).items():
            try:
                remove_shortcut(key, SHORTCUT_NAME, exe)
            except (OSError, VdfError) as error:
                self.log(f'steam: {key}: the shortcut was not taken out: {error}')
        for key, record in (data.get('play') or {}).items():
            try:
                self.restore_play(key, record)
            except (OSError, VdfError) as error:
                self.log(f'steam: {key}: X4\'s launch options were not put back: {error}')
        data.update(shortcut={}, play={}, play_wanted=False)
        self.write(data)
        return True
