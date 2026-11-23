"""Assemble a release kit: what an X4 Online install holds, with no session and no snapshot (docs/NETPLAY.md,
"Release kit and installer").

  python tools/make_release_kit.py --gate-dll DLL --out DIR [--master URL] [--python-embed ZIP] [--crypto-dll DLL]
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

A session (the save every player loads) is never part of a release: the host's PC makes it, a joiner downloads the
host's. The builder refuses a kit that would hold anything private or anything of X4 (PRIVATE_FOLDERS,
PRIVATE_NAMES, PRIVATE_SUFFIXES, PRIVATE_PATTERNS, secrets in JSON files): such a kit is removed again."""
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


def private_files(folder, skip=()):
    """[(relative path, why)] of every file under folder that a release must not hold. Files inside a folder named in
    skip are left out (the installer does not install those)."""
    folder = Path(folder)
    found = []
    for path in sorted(folder.rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(folder).as_posix()
        if any(part in skip for part in relative.split('/')[:-1]):
            continue
        reason = private_reason(folder, relative)
        if reason:
            found.append((relative, reason))
    return found


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


def kit_text(release, master, python, crypto):
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
        '- This folder is a release: the launcher, the gate DLL and the Multiplayer menu. It holds no session and no',
        '  save. A session (the save every player loads) is made on the host\'s PC when a game is hosted, or',
        '  downloaded from the game\'s host when you join. Each PC builds its game files from its own release; no',
        '  player ever runs code from another player or from the server.',
        '- Players can play together when their release lines (above) are equal. Games of another release are shown',
        '  as incompatible.',
        '',
        'You need',
        '- X4: Foundations 9.00 on Steam, the build the gate was made for, started once so that Steam knows it.',
        '  Close X4 before starting.',
        '- This whole folder, unchanged. VERIFY.cmd must end with "kit verified".',
        '',
        'Play (installed with the installer)',
        '1. Press Play in Steam. X4\'s main menu has Multiplayer ("Modified game detected": OK).',
        '2. Host a game: public or private, how many players, a name. The page shows a join code: send it to your',
        '   friends. The game loads when you press Start, or when every slot is taken.',
        '3. Or join: a join code (typed in, or pasted from the clipboard), one of the public games, or a LAN game.',
        '   X4 closes and comes back by itself in its multiplayer profile.',
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


def build(gate_dll, out, master=None, python_embed=None, crypto_dll=None, root=ROOT, built=None):
    """The release kit in out (new or empty); its manifest. master is a URL or HOST[:PORT]."""
    out = Path(out).resolve()   # absolute: the steam-menu step runs a child Python whose cwd is the kit
    master = master_url(master) if master else None
    existed = out.exists()
    if existed and any(out.iterdir()):
        refuse(f'{out} exists and is not empty; a kit is never written over another')
    out.mkdir(parents=True, exist_ok=True)
    try:
        release, python_sha = assemble(gate_dll, out, master, python_embed, crypto_dll, root, built)
        found = private_files(out)
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
    manifest['files'] = playtest_kit.tree(out)
    write_json(out / MANIFEST, manifest)
    return manifest


def made_while_playing(path, want):
    """Files a PC makes in an installed kit that are not the kit's: run folders and caches, the sessions it made or
    downloaded, and a master.json put beside a kit built without one."""
    if path.startswith('repo/.local/') or path.startswith('sessions/') or '/__pycache__/' in path:
        return True
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
    manifest = build(args.gate_dll, args.out, args.master, args.python_embed, args.crypto_dll)
    print(f'release {manifest["release"]}: {len(manifest["files"])} files in {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
