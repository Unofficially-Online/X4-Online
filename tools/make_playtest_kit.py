"""Assemble a playtest kit for two (or more) Windows PCs: one folder every PC gets a copy of.

  python tools/make_playtest_kit.py --overlay <built play overlay> --snapshot <save_009.xml.gz> --out <kit folder>
  python tools/make_playtest_kit.py --verify <kit folder>

`x4ls play --remote-slots` needs the same checkout, the same overlay folder and the same snapshot on every PC
(docs/PLAYTEST.md, "Two PCs"). The kit holds exactly that, with the command lines written out:

  repo/       this checkout's tracked files (git archive HEAD): tools/x4ls.py and what it runs (Python standard library only)
  overlay/    the play overlay (it holds the gate DLL)
  snapshot/   the session's save
  PLAY.cmd    every PC: X4 opens in its main menu; Multiplayer there, Host a game or Join a game (x4ls lobby)
  HOST.cmd    the same session from the command line: the PC that runs the relay and player 1
  JOIN.cmd    every other PC: paste the values the host printed:   JOIN.cmd <host address> <token> <arm-at> [slot]
  SAME-PC.cmd both windows on one PC (a check that the kit runs here)
  VERIFY.cmd  compares every file with kit-manifest.json (run it on each PC after copying)
  KIT.txt     what this kit is, and the steps

Nothing here is published: the snapshot is the host's own save and the overlay holds a build output. The kit folder
belongs under .local/ or outside the checkout."""
import argparse
import hashlib
import io
import json
import secrets
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = 'kit-manifest.json'
BS, CRLF = chr(92), chr(13) + chr(10)
PORT = 47000


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def tree(folder):
    """{relative posix path: sha256} of every file under folder, the manifest itself apart."""
    folder = Path(folder)
    return {p.relative_to(folder).as_posix(): sha256(p)
            for p in sorted(folder.rglob('*')) if p.is_file() and p.name != MANIFEST}


def git(*args, cwd=ROOT):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True).stdout


def export_repo(out, root=ROOT):
    """The tracked files of HEAD into out (no .git, nothing untracked: no logs, no build outputs)."""
    data = git('archive', '--format=tar', 'HEAD', cwd=root)
    out.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(out, filter='data')
    return git('rev-parse', 'HEAD', cwd=root).decode().strip()


def recipe_of(overlay):
    path = Path(overlay) / 'play-recipe.json'
    if not path.exists():
        raise SystemExit(f'{overlay} is not a play overlay (no play-recipe.json); build it with tools/build_play_overlay.py')
    return json.loads(path.read_text(encoding='utf-8'))


def cmd_files(players, port, opened, snapshot_name, server=False):
    """The command files' text. %~dp0 is the kit folder, so the kit runs from wherever it was copied to."""
    # python\ is the kit's own Python when it bundles one (--python-embed); it writes no caches into the kit
    head = ('@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\nset PYTHONDONTWRITEBYTECODE=1\r\nset PY=python\r\n'
            'if exist "%~dp0python\\python.exe" set PY="%~dp0python\\python.exe"\r\n')
    play = (f'python repo\\tools\\x4ls.py play --clients {players} --overlay overlay '
            f'--snapshot snapshot\\{snapshot_name}')
    opens = f' --open {",".join(map(str, opened))}' if opened else ''
    remote = ','.join(str(k) for k in range(2, players + 1) if k not in opened)
    host = head + (
        'if "%~2"=="" (\r\n'
        '  echo usage: HOST.cmd ^<this PC\'s address^> ^<the other PCs\' addresses, comma separated^> [extra x4ls play options]\r\n'
        '  echo   the addresses are the ones the PCs reach each other by: LAN, or a VPN such as Tailscale\r\n'
        '  exit /b 2\r\n)\r\n'
        'python repo\\tools\\x4ls.py doctor || exit /b 1\r\n'
        + play + opens + (f' --remote-slots {remote}' if remote else '')
        + f' --bind %1 --allow-from %2 --port {port} %3 %4 %5 %6\r\n')
    join = head + (
        'if "%~3"=="" (\r\n'
        '  echo usage: JOIN.cmd ^<host address^> ^<token^> ^<arm-at^> [slot, default 2]\r\n'
        '  echo   the host prints one "x4ls join ..." line: take --relay\'s address, --token and --arm-at from it\r\n'
        '  exit /b 2\r\n)\r\n'
        'set SLOT=%4\r\nif "%SLOT%"=="" set SLOT=2\r\n'
        'python repo\\tools\\x4ls.py doctor || exit /b 1\r\n'
        f'python repo\\tools\\x4ls.py join --relay %1:{port} --token %2 --slot %SLOT% --clients {players} --arm-at %3 '
        f'--overlay overlay --snapshot snapshot\\{snapshot_name}\r\n')
    hot = head + (
        'if "%~2"=="" (\r\n'
        '  echo usage: HOTJOIN.cmd ^<host address^> ^<token^> [slot]: join the running game in an open slot\r\n'
        '  exit /b 2\r\n)\r\n'
        f'set SLOT=%3\r\nif "%SLOT%"=="" set SLOT={opened[0] if opened else 2}\r\n'
        'python repo\\tools\\x4ls.py doctor || exit /b 1\r\n'
        f'python repo\\tools\\x4ls.py join --relay %1:{port} --token %2 --slot %SLOT% --overlay overlay\r\n')
    same = head + 'python repo\\tools\\x4ls.py doctor || exit /b 1\r\n' + play + opens + ' %*\r\n'
    verify = head + 'python repo\\tools\\make_playtest_kit.py --verify .\r\n'
    if server:   # one way to start: PLAY.cmd, Multiplayer, Join (the server's address and key are in server.json)
        files = {'VERIFY.cmd': verify + 'pause' + CRLF,
                 'PLAY.cmd': head + ('python repo%stools%sx4ls.py doctor || (pause & exit /b 1)%s' % (BS, BS, CRLF)
                                     + f'python repo{BS}tools{BS}x4ls.py lobby --overlay overlay --snapshot snapshot{BS}{snapshot_name} '
                                     + '--server-file server.json --close-when-over %*' + CRLF + 'if errorlevel 1 pause' + CRLF)}
        return {name: text.replace('python repo' + BS, '%PY% repo' + BS) for name, text in files.items()}
    files = {'HOST.cmd': host, 'JOIN.cmd': join, 'SAME-PC.cmd': same, 'VERIFY.cmd': verify}
    if not opened:   # the main menu's Multiplayer page: the same line on every PC
        files['PLAY.cmd'] = head + ('python repo\\tools\\x4ls.py doctor || exit /b 1\r\n'
                                    f'python repo\\tools\\x4ls.py lobby --overlay overlay --snapshot snapshot\\{snapshot_name} '
                                    f'--port {port} %*\r\n')
    if opened:
        files['HOTJOIN.cmd'] = hot
    return {name: text.replace('python repo' + BS, '%PY% repo' + BS) for name, text in files.items()}


def server_text(commit, recipe, server, snapshot_name, dll_sha):
    """KIT.txt of a kit with a server: every player does the same three things."""
    players, (host, port) = recipe['players'], server
    return CRLF.join([
        'X4 lockstep playtest kit',
        '',
        f'checkout   {commit}',
        f'gate DLL   sha256 {dll_sha}',
        f'session    {players} players, {recipe.get("mode", "coop")}, {recipe.get("minutes", "?")} minutes',
        f'snapshot   snapshot/{snapshot_name}',
        f'server     {host}: UDP {port} (the session) and TCP {port + 1} (the lobby)',
        '',
        'You need',
        '- X4: Foundations 9.00 on Steam, the build the gate was made for (PLAY.cmd says so), with the DLC set of the',
        '  snapshot, started once so that Steam knows it. Close X4 before starting.',
        '- Python 3.10 or newer on PATH, unless this folder has its own in python' + BS + ' (an installer made from it has).',
        '- This whole folder, unchanged. VERIFY.cmd must end with "kit verified".',
        '- A clock that is right within a few seconds (Windows time sync): every PC loads at an agreed second.',
        '',
        'Play (installed with the installer)',
        '1. Press Play in Steam. X4\'s main menu has Multiplayer ("Modified game detected": OK).',
        '2. Choose Multiplayer, then Join. X4 closes and comes back by itself in its multiplayer profile, joined.',
        '   The page says how many players are here.',
        '   This works while the helper runs (it starts with Windows; Start menu: X4 Multiplayer Playtest). The helper',
        '   puts a small menu extension into X4\'s extensions folder; with it X4 counts as modified, as with any',
        '   extension. Removing the installer removes both. Your own profile, saves and settings are not touched.',
        '',
        'Play (from this folder, without the installer)',
        '1. PLAY.cmd. X4 opens in its main menu ("Modified game detected": OK). Choose Multiplayer.',
        '2. Choose Join. The page says how many players are here.',
        f'3. When {players} players have joined, every game loads the snapshot at the same second (2 to 3 minutes).',
        '   The session starts at step 0 and the controls come at step 40. The session\'s end closes X4.',
        '',
        'In the game',
        '- Escape, "Multiplayer session": who you are, the step, whether you are still in the session, Leave.',
        '- Pause, save and SETA are off. A desync is silent in game; the server\'s run folder logs it.',
        '- Close your window to leave; the others go on after 15 seconds.',
        '',
        'Afterwards: your run folder repo/.local/runs/<time>-lobby1/ (summary.json, errors.log, the game\'s log; no',
        'saves, nothing of your own X4 profile) is sent to the server by itself. It also stays on your PC.',
        '',
        'Whoever runs the server (not needed to play): a copy of this folder on the server, then',
        '    python3 repo/tools/x4ls.py serve --overlay overlay --server-file server.json',
        'It needs Python only, no X4. One game at a time; the next players can join when a game is over.',
        'Keep server.json private: its key lets a launcher join this server.',
        '',
        'docs: repo/docs/PLAYTEST.md',
    ]) + CRLF


def kit_text(commit, recipe, port, opened, snapshot_name, dll_sha):
    players = recipe['players']
    lines = [
        'X4 lockstep playtest kit',
        '',
        f'checkout   {commit}',
        f'gate DLL   sha256 {dll_sha}',
        f'session    {players} players, {recipe.get("mode", "coop")}, {recipe.get("minutes", "?")} minutes, '
        + (f'open slots {",".join(map(str, opened))}' if opened else 'no open slots'),
        f'snapshot   snapshot/{snapshot_name}',
        f'relay      UDP port {port} on the host (also TCP {port} for a hot join\'s files)',
        '',
        'Every PC needs',
        '- X4: Foundations 9.00 on Steam, the build the gate was made for (x4ls doctor says so), with the DLC set of',
        '  the snapshot, started once so that Steam knows it. Close X4 before starting.',
        '- Python 3.10 or newer on PATH (the launcher uses the standard library only).',
        '- A copy of this whole folder, unchanged. Run VERIFY.cmd after copying: it must end with "kit verified".',
        '- The same clock within a few seconds (Windows time sync): the PCs load at an agreed second.',
        '',
        'Start from the main menu (every PC)',
        '1. PLAY.cmd on every PC. X4 opens in its main menu; choose Multiplayer.',
        '2. One player chooses "Host a game". The invite code is put on the clipboard: send it to the others.',
        f'   The others reach the host on UDP {port} (the session) and TCP {port + 1} (the lobby): on one LAN this needs',
        '   nothing but the firewall question answered with yes; over the internet use a VPN address (Tailscale,',
        '   ZeroTier) and start the host with PLAY.cmd --bind <that address>.',
        '3. Each other player copies the code and chooses "Join a game (invite code from the clipboard)".',
        '4. When everyone has joined, every game loads the snapshot at the same second and the session starts.',
        '   X4 says "Modified game detected" at its start: OK. The session\'s end closes X4.',
        '   Two windows on one PC: PLAY.cmd --bind 127.0.0.1 for the host, then PLAY.cmd --lane 2 for the other.',
        '',
        'The same from the command line (the host runs the relay and player 1)',
        '1. Host:   HOST.cmd <host address> <other PC address>',
        '   The addresses are the ones the PCs reach each other by. On one LAN: the LAN addresses. Over the',
        f'   internet: a VPN address (Tailscale, ZeroTier), or forward UDP {port} to the host and add',
        '   --public-host <router address> after the two addresses.',
        '2. The host prints one "x4ls join ..." line per other player. Each other PC, within 4 minutes:',
        '       JOIN.cmd <host address> <token> <arm-at> [slot]',
        '   with the values of --token and --arm-at from that line.',
        '3. Every window loads the snapshot at the agreed second (2 to 3 minutes), the session starts at step 0 and',
        '   the controls come at step 40.',
    ]
    if opened:
        lines += ['4. An open slot can be taken while the game runs: HOTJOIN.cmd <host address> <token> [slot].',
                  '   Everyone pauses while the joiner loads and catches up.']
    lines += [
        '',
        'In the game',
        '- Escape, "Multiplayer session": who you are, the step, whether you are still in the session, Leave.',
        '- Pause, save and SETA are off. A desync is silent in game; the host\'s run folder logs it.',
        '- Close your window to leave; the others go on after 15 seconds.',
        '',
        'Afterwards: each PC\'s run folder is repo/.local/runs/<time>/ (summary.json, errors.log, desyncs.log,',
        'relay.log on the host, clientK/engine-output.log). Send those back.',
        '',
        'docs: repo/docs/PLAYTEST.md',
    ]
    return '\r\n'.join(lines) + '\r\n'


HELPER = '''"""The helper behind "Play in Steam, then Multiplayer" (repo/src/lockstep/launcher/steamplay.py).

Run without a window (pythonw). No argument (or --quiet): start the helper; it shows nothing unless there is a problem.
--stop: end a running helper. --remove: end it and take the menu extension out of X4 again."""
import sys
from pathlib import Path

KIT = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(KIT / 'repo' / 'src'))
from lockstep.launcher import steamplay  # noqa: E402

sys.exit(steamplay.main(KIT, '@SNAPSHOT@'))
'''


def parse_server(text, port=PORT):
    """'ADDRESS' or 'ADDRESS:PORT' (an IPv4 address: the gate takes no host names) -> (address, port)."""
    m = re.fullmatch(r'((?:%s.){3}%s)(?::(%s))?' % ((r'%sd{1,3}' % BS,) * 2 + (r'%sd{1,5}' % BS,)), text.strip())
    if not m or any(int(part) > 255 for part in m.group(1).split('.')):
        raise SystemExit(f'--server takes an IPv4 address, optionally with :port, not {text!r}')
    return m.group(1), int(m.group(2) or port)


def add_python(archive, folder):
    """The kit's own Python: python.org's embeddable package, told where the launcher's modules are."""
    import zipfile
    with zipfile.ZipFile(archive) as z:
        z.extractall(folder)
    paths = sorted(folder.glob('python3*._pth'))
    if not (folder / 'python.exe').is_file() or not paths:
        raise SystemExit(f'{archive} is not a Windows embeddable Python package (python.org, "embeddable package (64-bit)")')
    # the package ignores the script's folder and PYTHONPATH: the launcher's module folders are named here, relative to python\
    lines = [paths[0].stem + '.zip', '.', '..' + BS + 'repo' + BS + 'src', '..' + BS + 'repo' + BS + 'tools',
             '..' + BS + 'repo' + BS + 'tools' + BS + 'dev']
    paths[0].write_text(CRLF.join(lines) + CRLF, newline='')


def build(overlay, snapshot, out, port=PORT, root=ROOT, server=None, python_embed=None):
    overlay, snapshot, out = Path(overlay), Path(snapshot), Path(out)
    recipe = recipe_of(overlay)
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f'{out} exists and is not empty; a kit is never written over another')
    if not snapshot.is_file():
        raise SystemExit(f'no snapshot at {snapshot}')
    dlls = sorted(overlay.rglob('x4lan_gate_64.dll'))
    if not dlls:
        raise SystemExit(f'{overlay} holds no gate DLL')
    out.mkdir(parents=True, exist_ok=True)
    commit = export_repo(out / 'repo', root)
    shutil.copytree(overlay, out / 'overlay')
    (out / 'snapshot').mkdir()
    shutil.copy2(snapshot, out / 'snapshot' / snapshot.name)
    if python_embed:
        add_python(Path(python_embed), out / 'python')
    opened = [int(k) for k in recipe.get('open') or []]
    for name, text in cmd_files(recipe['players'], port, opened, snapshot.name, bool(server)).items():
        (out / name).write_bytes(text.encode('ascii'))
    if server:   # the address and a new key, in the kit only: neither is part of the repository
        port = server[1]
        # Play in Steam: the menu extension the helper puts into X4, and the helper's start file (no console window)
        sys.path.insert(0, str(Path(root) / 'src'))
        from lockstep.launcher import steamplay
        steamplay.write_menu(out / 'steam-menu')
        (out / 'helper.pyw').write_text(HELPER.replace('@SNAPSHOT@', 'snapshot/' + snapshot.name), encoding='utf-8')
        (out / 'server.json').write_text(json.dumps({'host': server[0], 'port': port, 'key': secrets.token_hex(32)}, indent=1) + '\n')
        text = server_text(commit, recipe, server, snapshot.name, sha256(dlls[0]))
    else:
        text = kit_text(commit, recipe, port, opened, snapshot.name, sha256(dlls[0]))
    (out / 'KIT.txt').write_bytes(text.encode('ascii'))
    manifest = {'schema': 'x4-lockstep-playtest-kit-v1', 'commit': commit, 'port': port, 'players': recipe['players'],
                'open': opened, 'snapshot': snapshot.name, 'gate_dll_sha256': sha256(dlls[0]),
                **({'server': f'{server[0]}:{port}'} if server else {}), 'files': tree(out)}
    (out / MANIFEST).write_text(json.dumps(manifest, indent=1) + '\n', encoding='utf-8')
    return manifest


def verify(kit):
    kit = Path(kit)
    manifest = json.loads((kit / MANIFEST).read_text(encoding='utf-8'))
    want, have = manifest['files'], tree(kit)
    # run folders and caches a PC makes while playing are not part of the kit
    have = {k: v for k, v in have.items() if not k.startswith('repo/.local/') and '/__pycache__/' not in k}
    problems = ([f'missing {k}' for k in want if k not in have] + [f'changed {k}' for k in want if k in have and have[k] != want[k]]
                + [f'extra {k}' for k in have if k not in want])
    return manifest, problems


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--overlay')
    p.add_argument('--snapshot')
    p.add_argument('--out')
    p.add_argument('--port', type=int, default=PORT)
    p.add_argument('--server', metavar='ADDRESS[:PORT]',
                   help='a server every player joins (x4ls serve runs there): PLAY.cmd, Multiplayer, Join is all a player does')
    p.add_argument('--python-embed', metavar='ZIP',
                   help="python.org's Windows embeddable package: the kit then runs without a Python on the player's PC")
    p.add_argument('--verify', metavar='KIT')
    args = p.parse_args(argv)
    if args.verify:
        manifest, problems = verify(args.verify)
        for line in problems[:40]:
            print(line)
        print(f'kit {manifest["commit"][:12]}: {len(manifest["files"])} files, ' + ('kit verified' if not problems else f'{len(problems)} differences'))
        return 1 if problems else 0
    if not (args.overlay and args.snapshot and args.out):
        p.error('--overlay, --snapshot and --out (or --verify KIT)')
    manifest = build(args.overlay, args.snapshot, args.out, args.port,
                     server=parse_server(args.server, args.port) if args.server else None, python_embed=args.python_embed)
    print(f'kit {manifest["commit"][:12]}: {len(manifest["files"])} files in {args.out}; copy the folder to every PC, then VERIFY.cmd and KIT.txt')
    return 0


if __name__ == '__main__':
    sys.exit(main())
