"""Assemble a release kit: what an X4 Online install holds (docs/NETPLAY.md, "Release kit and installer").

  python tools/make_release_kit.py --gate-dll DLL --out DIR [--master URL] [--python-embed ZIP] [--crypto-dll DLL]
                                   [--sessions DIR | --prebuild-sessions 2,3,4 [--game X4] [--lanes DIR] [--display N]]
  python tools/make_release_kit.py --verify DIR

  repo/          this checkout's tracked files (git archive HEAD): the launcher, the Lua pages, the tools
  native/        x4lan_gate_64.dll (and x4mp_wire_crypto.dll with --crypto-dll)
  python/        python.org's Windows embeddable package (--python-embed), told where repo/src is
  steam-menu/    the menu extension the helper puts into X4 (lockstep.launcher.steamplay.write_menu)
  helper.pyw     the helper behind "Play in Steam, then Multiplayer"
  second.pyw     one more X4 on this PC
  release.json   which release this is; players of one release can play together
  master.json    the lobby server's URL (only with --master)
  KIT.txt        what this is and how to play
  VERIFY.cmd     compares every file with kit-manifest.json

  sessions/      (only with --sessions or --prebuild-sessions) ready-made host sessions, one folder each
                 (host-<start>-<N>p-<mode>-<dll8>-<form>: session.json, session-save.xml.gz, spec.json, overlay/), so
                 that hosting a game of that many players needs no new session: X4 restarts once, into the game

A joiner always downloads the host's session. A host's PC makes a session the kit does not ship (x4ls new-session) and
keeps it in sessions/. Ready-made sessions are made with the kit's own gate DLL and code: --prebuild-sessions runs x4ls
new-session for each player count (X4 and Steam on this PC, about 2 minutes each, a clean checkout); --sessions takes
the host-* folders of an earlier build or install (their gate DLL and probe must be this kit's). The builder refuses a
kit that would hold anything private or anything of X4 (PRIVATE_FOLDERS, PRIVATE_NAMES, PRIVATE_SUFFIXES,
PRIVATE_PATTERNS, secrets in JSON files; a shipped session holds only its files, with no path of the PC that made it):
such a kit is removed again."""
import argparse
import ipaddress
import json
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import make_playtest_kit as playtest_kit  # noqa: E402  (the shared pieces: repo export, Python, manifest, helper)

MANIFEST = playtest_kit.MANIFEST
SCHEMA = 'x4-online-release-kit-v1'
CRLF = '\r\n'
GATE_DLL = 'x4lan_gate_64.dll'
CRYPTO_DLL = 'x4mp_wire_crypto.dll'
MASTER_PORT = 47100
RELEASE_PATTERN = re.compile(r'[0-9a-f]{12}-[0-9a-f]{12}')

# A host name label (RFC 1123); an IPv4 address is a host name of four numeric labels.
HOST_LABEL = r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?'
HOST_NAME = re.compile(rf'{HOST_LABEL}(?:\.{HOST_LABEL})*')
URL_PATH = re.compile(r'(?:/[A-Za-z0-9._~-]*)*')

# What a release never holds (docs/NETPLAY.md, "Trust and safety rules"). Names are compared in lower case.
PRIVATE_FOLDERS = {
    '.local': 'local state: logs, overlays, snapshots, toolchains, credentials',
    '.git': 'a Git folder',
    '.venv': 'a Python environment',
    '.claude': "an agent's local settings",
    '__pycache__': 'a Python cache',
    'overlay': 'a play overlay (made from a session)',
    'overlays': 'play overlays (made from sessions)',
    'snapshot': 'a snapshot (a save)',
    'snapshots': 'snapshots (saves)',
    'save': 'saves',
    'saves': 'saves',
    'sessions': "sessions (made on the host's PC or downloaded)",
    'runs': 'run folders (logs)',
    'logs': 'logs',
}
PRIVATE_NAMES = {
    'server.json': "a server's address and key",
    'session.json': 'a session',
    '.env': 'environment secrets',
    '.netrc': 'credentials',
    '.pypirc': 'credentials',
    '.git-credentials': 'credentials',
    'credentials.json': 'credentials',
    'settings.local.json': 'local settings',
    'known_hosts': 'SSH hosts',
    'authorized_keys': 'SSH keys',
    'x4.exe': "X4's executable (Egosoft files are never redistributed)",
}
PRIVATE_SUFFIXES = {
    '.key': 'a key',
    '.pem': 'a key or a certificate',
    '.pfx': 'a key',
    '.p12': 'a key',
    '.ppk': 'a key',
    '.jks': 'a key store',
    '.keystore': 'a key store',
    '.log': 'a log',
    '.xml.gz': 'a save',
    '.x4join': 'a join package (it holds a save)',
    '.sav': 'a save',
    '.dmp': 'a crash dump',
}
PRIVATE_PATTERNS = [
    (re.compile(r'id_(?:rsa|dsa|ecdsa|ed25519)(?:\.pub)?'), 'an SSH key'),
    (re.compile(r'(?:ext_|subst_)?\d+\.(?:cat|dat)'), "an X4 catalog (Egosoft files are never redistributed)"),
]
# A JSON file whose top level has one of these fields with a long hex value holds a secret (server.json's key, a
# game's secret, a player's ticket, the relay's wire crypto key file).
SECRET_FIELDS = ('key', 'secret', 'token', 'ticket', 'password', 'relay_seed', 'master')
LONG_HEX = re.compile(r'[0-9a-fA-F]{32,}')
WIRE_KEY_FORMAT = 'x4mp-wire-crypto-relay-1'
HEAD_BYTES = 4096


def refuse(text):
    raise SystemExit(text)


# --- the lobby server's address ----------------------------------------------------------------------------------

def checked_host(host, text):
    """A host name, an IPv4 address or a bracketed IPv6 address, as given."""
    if host.startswith('[') and host.endswith(']'):
        try:
            ipaddress.IPv6Address(host[1:-1])
        except ValueError:
            refuse(f'--master: {text!r} has no valid IPv6 address in brackets')
        return host
    if not HOST_NAME.fullmatch(host):
        refuse(f'--master: {text!r} has no valid host name')
    labels = host.split('.')
    if all(label.isdigit() for label in labels):
        try:
            ipaddress.IPv4Address(host)
        except ValueError:
            refuse(f'--master: {text!r} has no valid IPv4 address')
    return host


def checked_port(port, text):
    if not port.isdigit() or not 1 <= int(port) <= 65535:
        refuse(f'--master: {text!r} has no valid port')
    return int(port)


def split_host_port(netloc, text):
    """'HOST', 'HOST:PORT', '[IPv6]' or '[IPv6]:PORT' -> (host, port or None)."""
    if netloc.startswith('['):
        end = netloc.find(']')
        if end < 0:
            refuse(f'--master: {text!r} has an unclosed bracket')
        host = netloc[:end + 1]
        rest = netloc[end + 1:]
    elif netloc.count(':') > 1:
        refuse(f'--master: {text!r}: write an IPv6 address in brackets')
    else:
        host, colon, port = netloc.partition(':')
        rest = colon + port
    port = None
    if rest:
        if not rest.startswith(':'):
            refuse(f'--master: {text!r} is not HOST[:PORT]')
        port = checked_port(rest[1:], text)
    return checked_host(host, text), port


def master_url(text):
    """--master: a URL ('https://master.example.org/x4mp', 'http://203.0.113.10:47100') or HOST[:PORT] (then
    http://HOST:PORT, port 47100). The URL as master.json carries it, without a trailing slash."""
    text = text.strip()
    if '://' not in text:
        host, port = split_host_port(text, text)
        return f'http://{host}:{port or MASTER_PORT}'
    parts = urllib.parse.urlsplit(text)
    scheme = parts.scheme.lower()
    if scheme not in ('http', 'https'):
        refuse(f'--master: {text!r}: the URL must be http or https')
    if '@' in parts.netloc:
        refuse(f'--master: {text!r}: no user name or password in the URL (a release is public)')
    if parts.query or parts.fragment or not URL_PATH.fullmatch(parts.path):
        refuse(f'--master: {text!r}: a base URL only, without a query or anything but a plain path')
    split_host_port(parts.netloc, text)
    return f'{scheme}://{parts.netloc}{parts.path.rstrip("/")}'


# --- what a release must not hold ----------------------------------------------------------------------------------

def secret_in(path):
    """Why the file's content is private (a private key, a secret in a JSON file), or None."""
    with open(path, 'rb') as file:
        head = file.read(HEAD_BYTES)
    if head.lstrip().startswith(b'-----BEGIN') and b'PRIVATE KEY' in head:
        return 'a private key'
    if path.suffix.lower() != '.json':
        return None
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (ValueError, UnicodeDecodeError):
        return None
    if not isinstance(data, dict):
        return None
    if data.get('format') == WIRE_KEY_FORMAT:
        return 'a wire crypto key file'
    for field in SECRET_FIELDS:
        value = data.get(field)
        if isinstance(value, str) and LONG_HEX.search(value):
            return f'a secret (its "{field}")'
    return None


def private_reason(folder, relative):
    """Why the file at folder/relative (a posix path) must not be in a release, or None."""
    parts = relative.split('/')
    for part in parts[:-1]:
        reason = PRIVATE_FOLDERS.get(part.lower())
        if reason:
            return reason
    name = parts[-1].lower()
    if name in PRIVATE_NAMES:
        return PRIVATE_NAMES[name]
    for suffix, reason in PRIVATE_SUFFIXES.items():
        if name.endswith(suffix):
            return reason
    for pattern, reason in PRIVATE_PATTERNS:
        if pattern.fullmatch(name):
            return reason
    return secret_in(Path(folder) / relative)


def private_files(folder, skip=(), sessions=False):
    """[(relative path, why)] of every file under folder that a release must not hold. Files inside a folder named in
    skip are left out (the installer does not install those). sessions: the kit's top-level sessions/ holds ready-made
    host sessions, checked by shipped_session_reason instead (a session is otherwise private: a save, an overlay)."""
    folder = Path(folder)
    found = []
    for path in sorted(folder.rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(folder).as_posix()
        if sessions and relative.startswith(SESSIONS + '/'):
            reason = shipped_session_reason(folder, relative)
        elif any(part in skip for part in relative.split('/')[:-1]):
            continue
        else:
            reason = private_reason(folder, relative)
        if reason:
            found.append((relative, reason))
    return found


# --- ready-made host sessions (--sessions, --prebuild-sessions) ----------------------------------------------------

SESSIONS = 'sessions'
SESSION_TOP = ('session.json', 'session-save.xml.gz', 'spec.json')      # netkit.SHIPPED_FILES
OVERLAY_SUFFIXES = ('.xml', '.lua', '.json', '.dll', '.append', '.files')
# A path of the PC that made a file: a drive, a UNC share, a home folder.
LOCAL_PATH = re.compile(rb'(?<![A-Za-z])[A-Za-z]:[\\/]|\\\\[A-Za-z0-9._-]+\\|/home/|/Users/|\\Users\\')
SCAN_BLOCK = 8 << 20


def launcher():
    """This checkout's launcher modules (netkit: the session folder names and checks the launcher uses)."""
    src = str(ROOT / 'src')
    if src not in sys.path:
        sys.path.insert(0, src)
    from lockstep.launcher import netkit
    return netkit


def path_in(path, gzipped=False):
    """True when the file names a path of a PC (LOCAL_PATH); a gzip file is read unpacked, in blocks."""
    import gzip
    opener = gzip.open if gzipped else open
    tail = b''
    with opener(path, 'rb') as file:
        while True:
            block = file.read(SCAN_BLOCK)
            if not block:
                return False
            if LOCAL_PATH.search(tail + block):
                return True
            tail = block[-64:]


def shipped_session_reason(folder, relative):
    """Why a file under sessions/ does not belong in a release, or None: only host-* session folders, their
    SESSION_TOP files and overlay/, data and code files there, no secret, no path of the PC that made it."""
    parts = relative.split('/')
    if len(parts) < 3 or not launcher().HOST_KEY.fullmatch(parts[1]):
        return 'not a ready-made host session (sessions/host-<start>-<N>p-<mode>-<dll8>-<form>/)'
    inner = parts[2:]
    if len(inner) == 1 and inner[0] not in SESSION_TOP:
        return 'not a file of a ready-made session'
    if len(inner) > 1:
        if inner[0] != 'overlay' or not inner[-1].lower().endswith(OVERLAY_SUFFIXES):
            return 'not a file of a ready-made session\'s overlay'
        for part in inner[1:-1]:
            if part.lower() in PRIVATE_FOLDERS:
                return PRIVATE_FOLDERS[part.lower()]
    name = inner[-1].lower()
    for pattern, reason in PRIVATE_PATTERNS:
        if pattern.fullmatch(name):
            return reason
    path = Path(folder) / relative
    if name.endswith('.dll'):
        return None
    if name.endswith('.lua') and (Path(folder) / 'repo' / 'tools' / 'dev' / inner[-1]).is_file():
        return None   # the kit's own code (session_code_problems: the same bytes as repo/tools/dev), shipped there anyway
    secret = secret_in(path)
    if secret:
        return secret
    if path_in(path, gzipped=name.endswith('.gz')):
        return 'a path of the PC that made it'
    return None


def session_code_problems(kit, session_folder):
    """A ready-made session must be the kit's own: its overlay's gate DLL is native/x4lan_gate_64.dll, and each of its
    ui files that the kit's repo/tools/dev holds too is that file (the probe and its cores are copied into an overlay as
    they are). [problem lines]."""
    overlay = Path(session_folder) / 'overlay'
    problems = []
    if playtest_kit.sha256(overlay / 'native' / GATE_DLL) != playtest_kit.sha256(Path(kit) / 'native' / GATE_DLL):
        problems.append(f'{Path(session_folder).name}: its gate DLL is not this kit\'s')
    for path in sorted((overlay / 'ui').glob('*.lua')):
        source = Path(kit) / 'repo' / 'tools' / 'dev' / path.name
        if source.is_file() and source.read_bytes() != path.read_bytes():
            problems.append(f'{Path(session_folder).name}: overlay/ui/{path.name} is not this kit\'s repo/tools/dev/{path.name}')
    return problems


def checkout_clean(root):
    """True when the checkout's src/ and tools/ are its HEAD (the kit's repo/ is git archive HEAD): a session made here
    is then made by the kit's own code."""
    done = subprocess.run(['git', '-C', str(root), 'status', '--porcelain', '--', 'src', 'tools'], capture_output=True,
                          text=True)
    return done.returncode == 0 and not done.stdout.strip()


def add_sessions(out, sessions=None, prebuild=(), options=None, make=None, clean=None, log=print):
    """The kit's sessions/: the host-* folders of `sessions` (an earlier kit's or an install's sessions/, a folder of
    sessions made before) that are this kit's, and one new session for each player count in `prebuild` (x4ls
    new-session with the kit's gate DLL: X4 runs). options: new-session's {game, lanes, steam_fix} for prebuild.
    make, clean: a test's stand-ins for netkit.make_session and checkout_clean. Returns the folder names."""
    netkit = launcher()
    kit = netkit.Kit(out)
    target = out / SESSIONS
    shipped = []
    if sessions:
        source = Path(sessions)
        if not source.is_dir():
            refuse(f'--sessions: {source} is not a folder')
        for folder in sorted(source.iterdir()):
            if not folder.is_dir() or not netkit.HOST_KEY.fullmatch(folder.name):
                log(f'sessions: {folder.name} left out (not a host session)')
                continue
            if (target / folder.name).exists():
                continue
            netkit.ship_session(folder, target / folder.name)
            shipped.append(folder.name)
    if prebuild:
        if not (clean or checkout_clean)(ROOT):
            refuse('--prebuild-sessions: this checkout\'s src/ or tools/ differ from HEAD; commit first (the kit\'s '
                   'sessions must be made by its own code, git archive HEAD)')
        options = dict(options or {})
        for players in prebuild:
            key = netkit.host_session_key(kit, players, netkit.HOST_SESSION_MODE, netkit.HOST_SESSION_START)
            if (target / key).exists():
                continue
            log(f'sessions: making the session for {players} players (x4ls new-session, about 2 minutes)')
            extra = {'display': options['display']} if options.get('display') else {}
            made = (make or netkit.make_session)(kit, players, netkit.HOST_SESSION_MODE, netkit.HOST_SESSION_START,
                                                 None, log, None, steam_fix=bool(options.get('steam_fix')),
                                                 game=options.get('game'), lanes=options.get('lanes'), **extra)
            made_folder = Path(made.overlay).parent
            work = made_folder.with_name(made_folder.name + '.made')
            made_folder.rename(work)
            try:
                netkit.ship_session(work, target / key)
            finally:
                shutil.rmtree(work, ignore_errors=True)
            shipped.append(key)
    from lockstep.launcher import extension
    problems = []
    for name in shipped:
        folder = target / name
        files = netkit.cached_session(kit, *session_counts(netkit, name))
        if files is None:
            problems.append(f'{name}: this kit could not host it (its gate DLL, its players or its form)')
            continue
        try:
            extension.verify_overlay(files.overlay)
        except SystemExit as error:
            problems.append(f'{name}: {error}')
        problems += session_code_problems(out, folder)
    if problems:
        refuse('refused: ready-made sessions that are not this kit\'s:\n' + '\n'.join(f'  {line}' for line in problems[:20]))
    return shipped


def session_counts(netkit, name):
    """cached_session's (players, mode, base start, open slots) of a host session folder name."""
    match = netkit.HOST_KEY.fullmatch(name)
    opened = tuple(int(slot) for slot in match.group('open').split('_')) if match.group('open') else ()
    return int(match.group('players')), match.group('mode'), match.group('start'), opened


def player_counts(text):
    """--prebuild-sessions: '2,3,4' -> (2, 3, 4); each 2..8."""
    try:
        counts = tuple(dict.fromkeys(int(part) for part in str(text).split(',') if part.strip()))
    except ValueError:
        refuse(f'--prebuild-sessions: {text!r} is not a list of player counts (2,3,4)')
    if not counts or not all(2 <= count <= 8 for count in counts):
        refuse(f'--prebuild-sessions: player counts are 2..8, not {text!r}')
    return counts


# --- the kit's files -----------------------------------------------------------------------------------------------

# Run by the builder's Python on the exported checkout: the menu extension is the commit's, not this working tree's.
MENU_SCRIPT = ('import sys\n'
               'sys.path.insert(0, sys.argv[1])\n'
               'from lockstep.launcher import steamplay\n'
               'steamplay.write_menu(sys.argv[2], sys.argv[3], release=True)\n')   # x4online_menu: pipe x4online, the netplay page


def write_steam_menu(kit):
    """steam-menu/ by the exported checkout's own steamplay.write_menu, with its own lobby page."""
    repo = kit / 'repo'
    command = [sys.executable, '-B', '-I', '-c', MENU_SCRIPT, str(repo / 'src'), str(kit / 'steam-menu'),
               str(repo / 'tools' / 'dev' / 'native_gate_lobby.lua')]
    done = subprocess.run(command, cwd=kit, capture_output=True, text=True)
    if done.returncode:
        refuse('steam-menu could not be written: ' + (done.stdout + done.stderr).strip()[-2000:])


def write_starters(kit):
    """helper.pyw and second.pyw: the playtest kit's, without a snapshot (a release has none; see docs/NETPLAY.md,
    "Release notes")."""
    helper = playtest_kit.HELPER.replace("'@SNAPSHOT@'", 'None')
    second = playtest_kit.SECOND.replace("'@SNAPSHOT@'", 'None')
    (kit / 'helper.pyw').write_text(helper, encoding='utf-8')
    (kit / 'second.pyw').write_text(second, encoding='utf-8')


def copy_dll(source, target):
    source = Path(source)
    if not source.is_file():
        refuse(f'no DLL at {source}')
    with open(source, 'rb') as file:
        if file.read(2) != b'MZ':
            refuse(f'{source} is not a Windows DLL')
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    return playtest_kit.sha256(target)


def release_of(commit, gate_sha, built):
    """release.json (docs/NETPLAY.md, "Releases and compatibility")."""
    return {'release': f'{commit[:12]}-{gate_sha[:12]}', 'commit': commit, 'gate_dll_sha256': gate_sha, 'built': built}


def write_json(path, data):
    path.write_text(json.dumps(data, indent=1) + '\n', encoding='utf-8', newline='\n')


def verify_cmd():
    return playtest_kit.CMD_HEAD + '%PY% repo\\tools\\make_release_kit.py --verify .' + CRLF + 'pause' + CRLF


def sessions_text(shipped):
    """KIT.txt's line about the ready-made sessions: '2, 3 and 4 players' (drop-in ones named so)."""
    netkit = launcher()
    counts = []
    for name in sorted(shipped, key=lambda name: session_counts(netkit, name)[0]):
        players, _mode, _start, opened = session_counts(netkit, name)
        counts.append(f'{players - len(opened)}+{len(opened)}' if opened else str(players))
    return ', '.join(counts[:-1]) + (' and ' if len(counts) > 1 else '') + counts[-1]


def kit_text(release, master, python, crypto, sessions=()):
    """KIT.txt: what the kit is and how to play."""
    if master:
        lobby_line = f'lobby      {master}'
    else:
        lobby_line = 'lobby      none named (LAN games and join by address; X4MP_MASTER or master.json names one)'
    if python:
        python_line = 'Python     bundled (python' + chr(92) + ')'
    else:
        python_line = 'Python     not bundled: helper.pyw needs Python 3.10 or newer (pythonw) on this PC'
    lines = [
        'X4 Online release kit',
        '',
        f'release    {release["release"]}',
        f'checkout   {release["commit"]}',
        f'gate DLL   sha256 {release["gate_dll_sha256"]}',
        f'built      {release["built"]}',
        lobby_line,
        python_line,
        *(['wire DLL   native/' + CRYPTO_DLL] if crypto else []),
        '',
        'What this is',
        '- Multiplayer for X4: Foundations by deterministic lockstep: every player runs the same game. An unofficial',
        '  fan project; nothing of X4 is in this kit, and no Egosoft file is changed.',
        *(['- This folder is a release: the launcher, the gate DLL and the Multiplayer menu, and ready-made sessions',
           f'  (the save every player loads) for games of {sessions_text(sessions)} players: hosting one of those needs',
           '  nothing new. Another game\'s session is made on the host\'s PC when it is hosted (about 2 minutes), and a',
           '  joiner downloads the host\'s. Each PC builds its game files from its own release; no player ever runs',
           '  code from another player or from the server.'] if sessions else
          ['- This folder is a release: the launcher, the gate DLL and the Multiplayer menu. It holds no session and no',
           '  save. A session (the save every player loads) is made on the host\'s PC when a game is hosted, or',
           '  downloaded from the game\'s host when you join. Each PC builds its game files from its own release; no',
           '  player ever runs code from another player or from the server.']),
        '- Players can play together when their release lines (above) are equal. Games of another release are shown',
        '  as incompatible.',
        '',
        'You need',
        '- X4: Foundations 9.00 on Steam, the build the gate was made for, started once so that Steam knows it.',
        '  Close X4 before starting.',
        '- This whole folder, unchanged. VERIFY.cmd must end with "kit verified".',
        '',
        'Play (installed with the installer)',
        '1. When the installer finishes, X4 opens on its Multiplayer page, in its multiplayer profile. Later, start',
        '   "X4 Online" from the desktop, the Start menu or your Steam library: the same. Hosting and joining there',
        '   need no restart. ("Modified game detected": OK.)',
        '2. Host a game: public or private, how many players, a name. The page shows a join code: send it to your',
        '   friends. The game loads when you press Start, or when every slot is taken.',
        '3. Or join: a join code (typed in, or pasted from the clipboard), one of the public games, or a LAN game.',
        "   X4's own Play in Steam starts your single-player game as always; its main menu has Multiplayer too",
        '   (X4 then starts once more, in its multiplayer profile, straight into the game). The page\'s "Steam\'s',
        '   Play starts X4 Online" makes Play open X4 Online instead (Single-player is then one click on that page).',
        '4. In the game: Escape, "Multiplayer session": the players, the join code (Copy), how the game is',
        '   connected, Leave.',
        '   A second player on the same PC: Start menu, "X4 Online (second window)".',
        '   The helper behind this starts with Windows (Start menu: X4 Online). It puts a small menu extension into',
        '   X4\'s extensions folder; with it X4 counts as modified, as with any extension. Removing the installer',
        '   removes both. Your own profile, saves and settings are not touched.',
        '',
        'Play (from this folder, without the installer)',
        '- Start helper.pyw with python' + chr(92) + 'pythonw.exe, then press Play in Steam.',
        '',
        'Hosting',
        '- The host\'s PC takes UDP 47000 and TCP 47001. While hosting, the launcher asks the router for them (UPnP,',
        '  NAT-PMP or PCP) and removes the mapping afterwards; when the others cannot reach the host, the lobby',
        '  server relays the game.',
        '',
        'Problems: repo' + chr(92) + '.local' + chr(92) + 'helper.log (it stays on your PC).',
        'docs: repo/docs/NETPLAY.md',
    ]
    return CRLF.join(lines) + CRLF


# --- building and checking -----------------------------------------------------------------------------------------

def assemble(gate_dll, out, master, python_embed, crypto_dll, root, built):
    """Every file but the manifest; returns (release.json's data, python embed sha256 or None)."""
    commit = playtest_kit.export_repo(out / 'repo', root)
    gate_sha = copy_dll(gate_dll, out / 'native' / GATE_DLL)
    if crypto_dll:
        copy_dll(crypto_dll, out / 'native' / CRYPTO_DLL)
    python_sha = None
    if python_embed:
        playtest_kit.add_python(Path(python_embed), out / 'python')
        python_sha = playtest_kit.sha256(python_embed)
    write_steam_menu(out)
    write_starters(out)
    release = release_of(commit, gate_sha, built or time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))
    write_json(out / 'release.json', release)
    if master:
        write_json(out / 'master.json', {'url': master})
    (out / 'KIT.txt').write_bytes(kit_text(release, master, bool(python_embed), bool(crypto_dll)).encode('ascii'))
    (out / 'VERIFY.cmd').write_bytes(verify_cmd().encode('ascii'))
    return release, python_sha


def remove_made(out, existed):
    """Take back a refused kit: nothing it held stays in the output folder."""
    if not out.exists():
        return
    if existed:
        for child in out.iterdir():
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        shutil.rmtree(out)


def build(gate_dll, out, master=None, python_embed=None, crypto_dll=None, root=ROOT, built=None, sessions=None,
          prebuild=(), session_options=None, make=None, clean=None):
    """The release kit in out (new or empty); its manifest. master is a URL or HOST[:PORT]. sessions, prebuild: the
    ready-made host sessions (add_sessions)."""
    out = Path(out).resolve()   # absolute: the steam-menu step runs a child Python whose cwd is the kit
    master = master_url(master) if master else None
    existed = out.exists()
    if existed and any(out.iterdir()):
        refuse(f'{out} exists and is not empty; a kit is never written over another')
    out.mkdir(parents=True, exist_ok=True)
    shipped = []
    try:
        release, python_sha = assemble(gate_dll, out, master, python_embed, crypto_dll, root, built)
        if sessions or prebuild:
            shipped = add_sessions(out, sessions, prebuild, session_options, make=make, clean=clean)
            if shipped:   # KIT.txt names them
                (out / 'KIT.txt').write_bytes(kit_text(release, master, bool(python_embed), bool(crypto_dll),
                                                       shipped).encode('ascii'))
        found = private_files(out, sessions=True)
        if found:
            listed = '\n'.join(f'  {path}: {reason}' for path, reason in found[:40])
            refuse(f'refused: the kit would hold {len(found)} private files (it was removed again):\n{listed}')
    except BaseException:
        remove_made(out, existed)
        raise
    manifest = {'schema': SCHEMA, **release}
    if master:
        manifest['master'] = master
    if python_sha:
        manifest['python_embed_sha256'] = python_sha
    if shipped:
        manifest['sessions'] = sorted(shipped)
    manifest['files'] = playtest_kit.tree(out)
    write_json(out / MANIFEST, manifest)
    return manifest


def made_while_playing(path, want):
    """Files a PC makes in an installed kit that are not the kit's: run folders and caches, the sessions it made or
    downloaded (a ready-made session of the kit is the kit's: checked), and a master.json put beside a kit built
    without one."""
    if path.startswith('repo/.local/') or '/__pycache__/' in path:
        return True
    if path.startswith(SESSIONS + '/'):
        return path not in want
    return path == 'master.json' and path not in want


def verify(kit):
    """(manifest, the differences between the kit folder and its manifest)."""
    kit = Path(kit)
    manifest = json.loads((kit / MANIFEST).read_text(encoding='utf-8'))
    want = manifest['files']
    have = {path: digest for path, digest in playtest_kit.tree(kit).items() if not made_while_playing(path, want)}
    return manifest, playtest_kit.differences(want, have)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--gate-dll', metavar='DLL', help='x4lan_gate_64.dll (tools/dev/build_native_gate.py)')
    parser.add_argument('--out', metavar='DIR', help='a new or empty folder')
    parser.add_argument('--master', metavar='URL',
                        help='the lobby server: a URL (http or https) or HOST[:PORT] (http, port 47100); written to master.json')
    parser.add_argument('--python-embed', metavar='ZIP',
                        help="python.org's Windows embeddable package (64-bit): the kit then needs no Python on the PC")
    parser.add_argument('--crypto-dll', metavar='DLL',
                        help='x4mp_wire_crypto.dll, for a relay run with --wire-crypto (the launcher does not use it)')
    parser.add_argument('--sessions', metavar='DIR',
                        help='ready-made host sessions to ship: the host-* folders of DIR (an earlier kit\'s or an '
                             'install\'s sessions/), made with this --gate-dll and this checkout\'s code')
    parser.add_argument('--prebuild-sessions', metavar='N,N,...',
                        help='make a host session for each player count (2,3,4: the default start, coop) with x4ls '
                             'new-session and ship them (X4 and Steam on this PC, about 2 minutes each; a clean checkout)')
    parser.add_argument('--game', metavar='DIR', help='with --prebuild-sessions: the X4 installation (default: Steam\'s)')
    parser.add_argument('--lanes', metavar='DIR', help='with --prebuild-sessions: the folder for the capture\'s game view')
    parser.add_argument('--steam-fix', action='store_true', help='with --prebuild-sessions: x4ls new-session --steam-fix')
    parser.add_argument('--display', type=int, metavar='N', help='with --prebuild-sessions: the monitor of the capture window (it never takes the focus)')
    parser.add_argument('--verify', metavar='KIT', help='compare a kit folder with its kit-manifest.json')
    args = parser.parse_args(argv)
    if args.verify:
        manifest, problems = verify(args.verify)
        for line in problems[:40]:
            print(line)
        result = 'kit verified' if not problems else f'{len(problems)} differences'
        print(f'release {manifest["release"]}: {len(manifest["files"])} files, {result}')
        return 1 if problems else 0
    if not (args.gate_dll and args.out):
        parser.error('--gate-dll and --out (or --verify KIT)')
    prebuild = player_counts(args.prebuild_sessions) if args.prebuild_sessions else ()
    options = {'game': args.game, 'lanes': args.lanes, 'steam_fix': args.steam_fix, 'display': args.display}
    manifest = build(args.gate_dll, args.out, args.master, args.python_embed, args.crypto_dll, sessions=args.sessions,
                     prebuild=prebuild, session_options=options)
    shipped = manifest.get('sessions') or []
    print(f'release {manifest["release"]}: {len(manifest["files"])} files in {args.out}'
          + (f'; ready-made sessions: {", ".join(shipped)}' if shipped else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
