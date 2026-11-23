"""Disposable X4 profiles (-personalfolderid) for launcher clients.

Only the ids in PROFILES are ever written; anything else, including the player's
own Steam-id profile, is refused. A client profile is wiped before each session
so no world, autosave or setting carries over. A profile that a running X4 was
started with is never wiped: it belongs to another session on this PC.
"""
import os
import re
import stat
import subprocess
import sys
from pathlib import Path
import shutil

ALL_PROFILES = (45850, 45851, 45853, 45854, 45855, 45856, 45857, 45858)
# A second session on the same PC beside a running one takes its profiles from further down the list: the environment
# variable X4LS_FIRST_PROFILE is the index its client 1 starts at (2: clients 1 and 2 use 45853 and 45854).
FIRST_PROFILE = max(0, min(len(ALL_PROFILES) - 1, int(os.environ.get('X4LS_FIRST_PROFILE', '0') or 0)))
PROFILES = ALL_PROFILES[FIRST_PROFILE:] + ALL_PROFILES[:FIRST_PROFILE]
CAPTURE_PROFILE = 45841  # tools/dev/native_gate_snapshot.lua only saves inside .../45841/save
SNAPSHOT_SLOT = 'save_009.xml.gz'


def documents_folder():
    """The Windows Documents folder as X4 sees it: the known folder FOLDERID_Documents, which may be redirected (OneDrive's
    folder backup moves it to %USERPROFILE%\\OneDrive\\Documents, and X4's profiles move with it); %USERPROFILE%\\Documents
    when the shell cannot be asked."""
    return known_documents() or Path(os.environ['USERPROFILE']) / 'Documents'


def known_documents():
    """FOLDERID_Documents from the shell (SHGetKnownFolderPath), None off Windows or when the shell has no answer."""
    if os.name == 'nt':
        try:
            import ctypes
            import uuid
            guid = (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID('{FDD39AD0-238F-46AF-ADB4-6C85480369C7}').bytes_le)
            path = ctypes.c_wchar_p()
            if ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(guid), 0, None, ctypes.byref(path)) == 0:
                try:
                    if path.value:
                        return Path(path.value)
                finally:
                    ctypes.windll.ole32.CoTaskMemFree(path)
        except (OSError, AttributeError, ValueError):
            pass
    return None


def documents():
    return documents_folder() / 'Egosoft/X4'


def profile_dir(profile, root=None):
    if profile not in (*PROFILES, CAPTURE_PROFILE):
        raise SystemExit(f'profile {profile} is not a disposable launcher profile')
    path = (Path(root) if root else documents()) / str(profile)
    if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
        raise SystemExit(f'{path} is a link; refusing')
    return path


# Render-only settings turned down for unattended soaks (--fast): more frames per second means more
# admitted ticks per second. Detail distances (lodfactor, effectdistance) stay at 1.0 because they can
# change what the simulation does; every client gets identical settings either way.
FAST = {'gfxquality': 0, 'shaderquality': 0, 'shadows': 0, 'softshadows': 'false', 'ssao': 0, 'glow': 0, 'uiglow': 0,
        'distortion': 'false', 'ssr': 'false', 'texturequality': 'low', 'chromaticaberration': 'false', 'volumetric': 0,
        'pom': 'off', 'envmapprobes': 'low', 'frameratelimit': 'false', 'presentmode': 'immediate', 'nosound': 'true'}


def config_xml(width, height, fast=False, borderless=False):
    """borderless: X4's borderless window (it fills the screen and cannot be resized: a resize reloads X4's UI).
    X4's own option "confine mouse" is off: a test client that has the focus does not keep the cursor inside its window."""
    extra = ''.join(f'<{k}>{v}</{k}>' for k, v in FAST.items()) if fast else ''
    return ('<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode>'
            f'<fullscreen>false</fullscreen><borderless>{"true" if borderless else "false"}</borderless><res_width>{width}</res_width>'
            f'<res_height>{height}</res_height><antialiasing>0</antialiasing><vsync>false</vsync><confinemouse>false</confinemouse>{extra}</root>\n')


def client_settings(spec, client):
    """X4LS_CLIENT_CONFIG (test only): 'K:key=value,key=value;K2:...' gives client K its own X4 settings, as a person on
    another PC has (resolution, detail distances, graphics quality). [] when the client has none."""
    items = []
    for part in (spec or '').split(';'):
        head, _, body = part.partition(':')
        if head.strip().isdigit() and int(head) == client:
            for pair in body.split(','):
                key, _, value = pair.partition('=')
                if not re.fullmatch(r'[a-z_]+', key.strip()) or not re.fullmatch(r'[\w.\-]+', value.strip()):
                    raise SystemExit(f'X4LS_CLIENT_CONFIG: bad setting {pair!r}')
                items.append((key.strip(), value.strip()))
    return items


def with_settings(text, items):
    """config.xml text with each (key, value): an element replaced where it is, else added before </root>."""
    for key, value in items:
        element = f'<{key}>{value}</{key}>'
        text, n = re.subn(rf'<{key}>[^<]*</{key}>', element, text)
        if not n:
            text = text.replace('</root>', element + '</root>')
    return text


def content_xml(extension_ids):
    rows = ''.join(f'<extension id="{name}" enabled="true" />' for name in extension_ids)
    return f'<?xml version="1.0"?><content>{rows}</content>\n'


PROFILE_ARGUMENT = re.compile(r'-personalfolderid\s+(\d+)')
X4_COMMAND = re.compile(r'(?i)\bX4(\.exe)?\b')


def running_profiles():
    """{profile id: pid} of every running X4 that was started with -personalfolderid (this PC's launcher clients, of
    any session and any checkout). An X4 without it is a person's own game and has no profile of ours."""
    found = {}
    try:
        if os.name == 'nt':
            out = subprocess.run(['powershell', '-NoProfile', '-Command',
                                  "Get-CimInstance Win32_Process -Filter \"Name='X4.exe'\" | ForEach-Object { '{0}|{1}' -f $_.ProcessId, $_.CommandLine }"],
                                 capture_output=True, text=True, timeout=60).stdout
            rows = [line.partition('|')[::2] for line in out.splitlines()]
        else:
            rows = []
            for entry in Path('/proc').iterdir():
                if entry.name.isdigit():
                    try:
                        rows.append((entry.name, (entry / 'cmdline').read_bytes().replace(bytes(1), b' ').decode('utf-8', 'replace')))
                    except OSError:
                        pass
    except (OSError, subprocess.SubprocessError):
        return found
    for pid, command in rows:
        match = PROFILE_ARGUMENT.search(command)
        if match and pid.strip().isdigit() and X4_COMMAND.search(command):
            found[int(match.group(1))] = int(pid)
    return found


def refuse_in_use(profile):
    """Wiping the profile of a running client took its config, its arm file and its staged save away under it (two
    sessions on one PC that picked the same pair, 16 Nov). The session that comes second stops here instead."""
    pid = running_profiles().get(int(profile))
    if pid:
        raise SystemExit(f'profile {profile} is in use by a running X4 (pid {pid}): another session on this PC has it; '
                         'take other profiles (X4LS_FIRST_PROFILE) or end that session first')


def _writable_again(remove, path, _error):
    """shutil.rmtree's error handler: the entry is made writable and removed again. OneDrive marks the folders of a
    Documents it backs up read-only, and Windows refuses to remove a read-only folder ("Access is denied", the laptop's
    profiles of 23 Nov); a second refusal is raised as it is. The folder holding it too (where a folder without write
    access is what refuses the removal, as on Linux)."""
    for entry in (os.path.dirname(path), path):
        try:
            os.chmod(entry, os.stat(entry).st_mode | stat.S_IWRITE)
        except OSError:
            pass
    remove(path)


def remove_tree(path):
    """A profile folder and everything in it, read-only entries too."""
    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=_writable_again)
    else:
        shutil.rmtree(path, onerror=_writable_again)


def prepare(profile, extension_ids, width=1280, height=720, snapshot=None, root=None, fast=False, borderless=False, settings=()):
    """Fresh profile: windowed config, autosave and protected UI off, optional snapshot; settings: client_settings()."""
    path = profile_dir(profile, root)
    if root is None:
        refuse_in_use(profile)   # the real profiles only: a test's own folder has no X4 on it
    if path.exists():
        remove_tree(path)
    (path / 'save').mkdir(parents=True)
    (path / 'config.xml').write_text(with_settings(config_xml(width, height, fast, borderless), settings))
    (path / 'content.xml').write_text(content_xml(extension_ids))
    if snapshot:
        shutil.copyfile(snapshot, path / 'save' / SNAPSHOT_SLOT)
    return path


def unstage(profile, root=None):
    """Remove the snapshot prepare() staged: the lane tool shares these profiles and refuses to overwrite a staged save."""
    staged = profile_dir(profile, root) / 'save' / SNAPSHOT_SLOT
    if staged.is_file():
        staged.unlink()
        return True
    return False


def collect(profile, logfile, out, root=None):
    """Copy the client's log (and any saves it wrote) into the run directory."""
    path = profile_dir(profile, root)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    if (path / logfile).is_file():
        shutil.copy2(path / logfile, out / 'engine-output.log')  # the name the census tools read
    if (path / 'x4lan-crash.txt').is_file():  # the crash note (native_gate_crash_note): an unhandled exception's CRASH line
        shutil.copy2(path / 'x4lan-crash.txt', out / 'x4lan-crash.txt')
    saves = [p for p in (path / 'save').glob('*') if p.is_file() and p.name != SNAPSHOT_SLOT] if (path / 'save').is_dir() else []   # files only: a hot join's save/mpjoin stays
    for save in saves:
        shutil.copy2(save, out / save.name)
