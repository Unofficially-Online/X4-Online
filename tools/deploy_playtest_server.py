"""Put a kit's server on a Linux machine: python tools/deploy_playtest_server.py --kit KIT --ssh root@ADDRESS

For a kit made with tools/make_playtest_kit.py --server ADDRESS. The server needs the kit's checkout, overlay and
server.json (not the snapshot, not X4) and Python 3.10 or newer. This tool copies those three over ssh and sets up:

  <dir>/kits/<name>/{repo,overlay,server.json}   the kit's server files (<dir>/current points at them)
  <dir>/runs/                                    each game's relay files and the players' logs
  /etc/systemd/system/<service>.service          python3 repo/tools/x4ls.py serve ..., as --user, restarted if it ends
  ufw: UDP <port> (the session) and TCP <port + 1> (the lobby and the logs), when ufw is there

A kit has its own key and its own overlay, so every new kit is deployed again; players of an older kit are then
refused with the reason. The restart ends a game that is running.

ssh must reach the machine without a question (a key, an agent). Nothing of this is stored in the repository: the
address and the key are the kit's."""
import argparse
import io
import json
import subprocess
import sys
import tarfile
import time
from pathlib import Path

UNIT = """[Unit]
Description=X4 lockstep playtest server (x4ls serve: lobby and relay)
After=network-online.target
Wants=network-online.target

[Service]
User={user}
WorkingDirectory={dir}/current
ExecStart=/usr/bin/python3 -u repo/tools/x4ls.py serve --overlay overlay --server-file server.json --bind 0.0.0.0 --runs {dir}/runs
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
"""

REMOTE = """set -e
chmod 600 {dir}/kits/{name}/server.json
ln -sfn kits/{name} {dir}/current
mkdir -p {dir}/runs
chown -R {user}: {dir}
cat > /etc/systemd/system/{service}.service <<'UNIT'
{unit}UNIT
systemctl daemon-reload
systemctl enable {service} >/dev/null 2>&1
systemctl restart {service}
if command -v ufw >/dev/null 2>&1; then
  ufw allow {port}/udp comment 'x4 lockstep relay' >/dev/null
  ufw allow {tcp}/tcp comment 'x4 lockstep lobby' >/dev/null
fi
sleep 2
systemctl is-active {service}
journalctl -u {service} -n 5 --no-pager -o cat
"""


def pack(kit):
    """repo/, overlay/ and server.json of a kit as a tar.gz in memory (run folders and caches left out)."""
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode='w:gz') as tar:
        for name in ('repo', 'overlay', 'server.json'):
            tar.add(kit / name, arcname=name,
                    filter=lambda item: None if '/.local' in item.name or '__pycache__' in item.name else item)
    return data.getvalue()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--kit', type=Path, required=True, help='a kit folder made with make_playtest_kit.py --server')
    p.add_argument('--ssh', required=True, metavar='USER@ADDRESS', help='an account that may write the unit file (root)')
    p.add_argument('--user', default='x4server', help='the account the server runs as (default x4server)')
    p.add_argument('--dir', default='', help='where it lives (default /home/<user>/lockstep-lobby)')
    p.add_argument('--service', default='x4mp-lobby')
    args = p.parse_args(argv)
    if not (args.kit / 'server.json').is_file():
        p.error(f'{args.kit} has no server.json: build the kit with make_playtest_kit.py --server ADDRESS')
    server = json.loads((args.kit / 'server.json').read_text())
    manifest = json.loads((args.kit / 'kit-manifest.json').read_text())
    folder = args.dir or f'/home/{args.user}/lockstep-lobby'
    name = f'{time.strftime("%Y%m%d-%H%M%S")}-{manifest["commit"][:12]}'
    ssh = ['ssh', '-o', 'BatchMode=yes', args.ssh]
    data = pack(args.kit)
    print(f'copying {len(data) // 1024} KiB to {args.ssh}:{folder}/kits/{name}')
    target = f'{folder}/kits/{name}'
    subprocess.run([*ssh, f'mkdir -p {target} && tar xzf - -C {target}'], input=data, check=True)
    unit = UNIT.format(user=args.user, dir=folder)
    script = REMOTE.format(dir=folder, name=name, user=args.user, service=args.service, unit=unit, port=server['port'],
                           tcp=server['port'] + 1)
    done = subprocess.run([*ssh, 'bash -s'], input=script.encode(), capture_output=True)
    print(done.stdout.decode(errors='replace').strip())
    if done.returncode:
        print(done.stderr.decode(errors='replace').strip(), file=sys.stderr)
        return 1
    print(f'server {server["host"]}: UDP {server["port"]}, TCP {server["port"] + 1}; service {args.service}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
