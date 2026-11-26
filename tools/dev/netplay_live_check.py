"""Live checks of the Multiplayer page and its restarts, with disposable profiles only (docs/NETPLAY.md).

  python tools/dev/netplay_live_check.py restarts LOG [LOG ...] [--expect N]
      How often the player's X4 went away on the way into the game (netkit.count_restarts over the helper's helper.log
      and the lobby's launcher.log of one press); exit 1 when it is not --expect.

  python tools/dev/netplay_live_check.py public-list --kit KIT --lanes DIR [--display 3] [--seconds 300]
      A public game is drawn on another X4's page: a master server on this PC (x4ls master on 127.0.0.1, its own data
      folder), lobby A hosts a public 2-player game (x4ls host --public, lane 1), lobby B opens the Multiplayer page
      (x4ls lobby --show, lane 2). Passes when B's launcher logged the game in its BROWSE row and B's page noted it drew
      it ([X4LAN] LOBBY GAMES public=1 or more). The kit must hold a 2-player host session (a release kit with
      --prebuild-sessions, or one made before): hosting then starts no third X4.

Two X4s run (lanes 1 and 2 of --lanes, profiles from X4LS_FIRST_PROFILE): take the two slots of C:/x4ls-locks first
(read its PROFILES.txt). Every process runs below normal priority; X4's windows are kept on --display. Nothing is
started in the player's own profile, and only processes whose command line names --lanes or this run's folders are
ended."""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import netkit, profile, session  # noqa: E402

BELOW_NORMAL = 0x00004000
GAMES_NOTE = re.compile(r'\[X4LAN\] LOBBY GAMES public=(\d+)')


def restarts(paths, expect=None):
    text = '\n'.join(Path(path).read_text(encoding='utf-8', errors='replace') for path in paths)
    count = netkit.count_restarts(text)
    rows = [line for line in text.splitlines() if netkit.RESTART_MARK in line or netkit.SESSION_ALONE_MARK in line]
    for row in rows:
        print('  ' + row)
    print(f'restarts: {count}')
    return 0 if expect is None or count == expect else 1


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def start(command, log, env=None):
    out = open(log, 'w', encoding='utf-8', errors='replace')
    return subprocess.Popen(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env,
                            creationflags=BELOW_NORMAL if os.name == 'nt' else 0)


def own_x4(lanes):
    """pids of the X4s started from these lanes (never another's)."""
    found = []
    for pid, _client in session.other_clients():
        command = subprocess.run(['powershell', '-NoProfile', '-Command',
                                  f"(Get-CimInstance Win32_Process -Filter \"ProcessId={pid}\").CommandLine"],
                                 capture_output=True, text=True).stdout
        if str(lanes).lower().replace('/', '\\') in command.lower():
            found.append(pid)
    return found


def public_list(args):
    kit = netkit.Kit(args.kit)
    if not any(not files.recipe.get('open') and files.players == 2 for files in netkit.host_sessions(kit).values()):
        raise SystemExit(f'{args.kit} has no 2-player host session (make_release_kit.py --prebuild-sessions 2, or host one '
                         'game first): hosting would start a third X4 for its capture')
    work = Path(tempfile.mkdtemp(prefix='netplay-live-', dir=ROOT / '.local'))
    port = free_port()
    master_url = f'http://127.0.0.1:{port}'
    python = sys.executable
    x4ls = [python, str(Path(args.kit) / 'repo' / 'tools' / 'x4ls.py')]
    env = dict(os.environ)
    procs = []
    try:
        procs.append(start([python, str(ROOT / 'tools' / 'x4ls.py'), 'master', '--bind', '127.0.0.1', '--http-port', str(port),
                            '--echo-port', str(free_port()), '--relay-ports', '47290-47293', '--data', str(work / 'master')],
                           work / 'master.log', env))
        time.sleep(3)
        common = ['--kit', str(args.kit), '--master', master_url, '--lanes', str(args.lanes), '--close-when-over']
        procs.append(start([*x4ls, 'host', '--players', '2', '--public', '--name', 'live check', '--lane', '1',
                            '--port', str(free_port()), *common], work / 'host.log', env))
        procs.append(start([*x4ls, 'lobby', '--lane', '2', '--show', *common], work / 'page.log', env))
        end = time.time() + args.seconds
        drawn = listed = None
        page_log = profile.profile_dir(profile.PROFILES[1]) / 'x4ls-c2.log'
        while time.time() < end and (drawn is None or listed is None):
            time.sleep(3)
            if args.display:
                pids = own_x4(args.lanes)
                if pids:
                    session.tile(pids, 1280, 720, 2, args.display)
            text = (work / 'page.log').read_text(encoding='utf-8', errors='replace')
            if re.search(r"BROWSE: [1-9]\d* public game\(s\) \[[^\]]*live check", text):
                listed = True
            if page_log.is_file():
                notes = [int(n) for n in GAMES_NOTE.findall(page_log.read_text(encoding='utf-8', errors='replace'))]
                if notes and max(notes) >= 1:
                    drawn = max(notes)
        print(json.dumps({'listed_by_the_launcher': bool(listed), 'drawn_by_the_page': drawn, 'run': str(work)}))
        return 0 if listed and drawn else 1
    finally:
        for pid in own_x4(args.lanes):
            session.kill(pid)
        for proc in reversed(procs):
            if proc.poll() is None:
                session.kill(proc.pid)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    r = sub.add_parser('restarts')
    r.add_argument('logs', nargs='+', type=Path)
    r.add_argument('--expect', type=int)
    p = sub.add_parser('public-list')
    p.add_argument('--kit', type=Path, required=True)
    p.add_argument('--lanes', type=Path, required=True)
    p.add_argument('--display', type=int, default=3)
    p.add_argument('--seconds', type=int, default=300)
    args = parser.parse_args(argv)
    if args.command == 'restarts':
        return restarts(args.logs, args.expect)
    return public_list(args)


if __name__ == '__main__':
    sys.exit(main())
