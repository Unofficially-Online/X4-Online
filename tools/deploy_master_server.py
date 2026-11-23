"""Put the netplay master on a Linux machine: python tools/deploy_master_server.py --ssh root@ADDRESS

The master (x4ls master, docs/NETPLAY.md "The central server") needs this checkout's committed files and Python 3.10 or
newer, no X4 and no kit. This tool copies HEAD (git archive) over ssh and sets up:

  <dir>/releases/<time>-<commit>/   the checkout (<dir>/current points at the newest)
  <dir>/data/                       the games' files and relay folders (emptied when the master starts)
  /etc/systemd/system/<service>.service
                                    python3 current/tools/x4ls.py master ..., as the unprivileged --user (made when
                                    missing, no login shell), restarted if it ends, with systemd's sandboxing
  ufw, when ufw is there:           UDP 47101 (the echo), UDP and TCP 47200-47263 (the relays and their join files), and
                                    TCP 47100 (the HTTP API) unless it is behind a proxy
  <dir>/nginx-location.conf         with --proxy-prefix only: a location block for nginx (HTTPS on the provider's host
                                    name). The operator includes it in the server block; this tool edits no nginx file
                                    and reloads nothing

Behind the proxy the API listens on 127.0.0.1 only (--http-bind 127.0.0.1 --trust-proxy) and the master needs
--public-host, the IPv4 address the players send UDP to. The host name of the proxy should resolve to IPv4 only (an A
record): a player's relay traffic is allowed from the address its HTTP requests came from.

ssh must reach the machine without a question (a key, an agent). Nothing of this is stored in the repository: the
address is given on the command line. --print-nginx shows the nginx block without connecting anywhere."""
import argparse
import gzip
import ipaddress
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
HTTP_PORT = 47100            # the contract's master ports (docs/NETPLAY.md "Ports")
ECHO_PORT = 47101
RELAY_FIRST = 47200
RELAY_LAST = 47263
USER = re.compile(r'[a-z_][a-z0-9_-]{0,31}')
FOLDER = re.compile(r'/[A-Za-z0-9._/-]+')
PREFIX = re.compile(r'(?:/[A-Za-z0-9._~-]+)+')
SERVICE = re.compile(r'[A-Za-z0-9_.-]+')

UNIT = """[Unit]
Description=X4 Multiplayer master (x4ls master: lobby API, join codes, UDP echo, relay pool)
After=network-online.target
Wants=network-online.target

[Service]
User={user}
Group={user}
WorkingDirectory={dir}/current
Environment=PYTHONDONTWRITEBYTECODE=1
ExecStart=/usr/bin/python3 -u {dir}/current/tools/x4ls.py master {arguments}
Restart=always
RestartSec=3
NoNewPrivileges=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectSystem=strict
ProtectHome=read-only
ReadWritePaths={dir}/data
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
LimitNOFILE=8192
TasksMax=4096

[Install]
WantedBy=multi-user.target
"""

NGINX = """# x4mp-master behind nginx: include this file inside the HTTPS server {{ }} block of the master's host name (for
# example master.example.org), then check and reload nginx yourself (nginx -t, systemctl reload nginx).
# The host name should resolve to IPv4 only: the relays and the echo are IPv4, and a player's relay traffic is allowed
# from the address its HTTP requests came from (X-Forwarded-For below; the master trusts it from 127.0.0.1 only).
location = {prefix} {{
    return 301 {prefix}/;
}}
location {prefix}/ {{
    proxy_pass http://127.0.0.1:{port}/;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $remote_addr;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_read_timeout 60s;          # the lobby's long poll waits up to 25 s
    proxy_send_timeout 60s;
    client_max_body_size 512m;       # a drop-in game's join files (the master checks every cap itself)
    proxy_request_buffering off;     # uploads stream to the master, which refuses what is too large at once
    proxy_buffering off;             # downloads and long polls stream back
}}
"""

REMOTE = """set -e
if ! id -u {user} >/dev/null 2>&1; then
  useradd --system --create-home --home-dir /home/{user} --shell /usr/sbin/nologin {user}
fi
mkdir -p {dir}/data
ln -sfn releases/{name} {dir}/current
chown -R {user}: {dir}/data
chown -R root: {dir}/releases
cat > /etc/systemd/system/{service}.service <<'UNIT'
{unit}UNIT
{nginx}systemctl daemon-reload
systemctl enable {service} >/dev/null 2>&1
systemctl restart {service}
if command -v ufw >/dev/null 2>&1; then
{ufw}fi
sleep 2
systemctl is-active {service}
journalctl -u {service} -n 5 --no-pager -o cat
"""


def master_arguments(folder, public_host='', proxy_prefix=''):
    """The x4ls master arguments of the unit: the contract's ports; behind a proxy the API on loopback only."""
    arguments = ['--bind', '0.0.0.0', '--http-port', str(HTTP_PORT), '--echo-port', str(ECHO_PORT),
                 '--relay-ports', f'{RELAY_FIRST}-{RELAY_LAST}', '--data', f'{folder}/data']
    if proxy_prefix:
        arguments += ['--http-bind', '127.0.0.1', '--trust-proxy', '--public-prefix', proxy_prefix]
    if public_host:
        arguments += ['--public-host', public_host]
    return arguments


def unit_text(user, folder, public_host='', proxy_prefix=''):
    return UNIT.format(user=user, dir=folder, arguments=' '.join(master_arguments(folder, public_host, proxy_prefix)))


def ufw_rules(proxied):
    """The ufw commands: exactly the master's ports (the API's only when it is not behind the proxy)."""
    rules = [f"ufw allow {ECHO_PORT}/udp comment 'x4mp master echo'",
             f"ufw allow {RELAY_FIRST}:{RELAY_LAST}/udp comment 'x4mp master relays'",
             f"ufw allow {RELAY_FIRST}:{RELAY_LAST}/tcp comment 'x4mp master relay join files'"]
    if not proxied:
        rules.insert(0, f"ufw allow {HTTP_PORT}/tcp comment 'x4mp master api'")
    return rules


def nginx_text(prefix):
    return NGINX.format(prefix=prefix.rstrip('/'), port=HTTP_PORT)


def remote_script(user, folder, name, service, public_host='', proxy_prefix=''):
    """The bash script run on the machine (as the --ssh account) after the checkout was unpacked."""
    nginx = ''
    if proxy_prefix:
        nginx = (f"cat > {folder}/nginx-location.conf <<'NGINX'\n{nginx_text(proxy_prefix)}NGINX\n"
                 f"echo 'nginx: include {folder}/nginx-location.conf in the server block, then nginx -t and reload'\n")
    ufw = ''.join(f'  {rule} >/dev/null\n' for rule in ufw_rules(bool(proxy_prefix)))
    unit = unit_text(user, folder, public_host, proxy_prefix)
    return REMOTE.format(user=user, dir=folder, name=name, service=service, unit=unit, nginx=nginx, ufw=ufw)


def pack(run=subprocess.run, root=ROOT):
    """HEAD of the checkout as a tar.gz in memory (committed files only: no .local, no logs, no build outputs)."""
    archived = run(['git', '-C', str(root), 'archive', '--format=tar', 'HEAD'], capture_output=True, check=True)
    commit = run(['git', '-C', str(root), 'rev-parse', 'HEAD'], capture_output=True, check=True)
    return gzip.compress(archived.stdout), commit.stdout.decode('ascii').strip()


def check(parser, args):
    if not USER.fullmatch(args.user):
        parser.error('--user: a Linux account name')
    if not SERVICE.fullmatch(args.service):
        parser.error('--service: letters, digits, . _ -')
    if args.dir and not FOLDER.fullmatch(args.dir):
        parser.error('--dir: an absolute path of letters, digits, . _ - /')
    if args.proxy_prefix and not PREFIX.fullmatch(args.proxy_prefix.rstrip('/') or '/'):
        parser.error('--proxy-prefix: a path like /x4mp')
    if args.public_host:
        try:
            address = ipaddress.ip_address(args.public_host)
        except ValueError:
            parser.error('--public-host: the dotted IPv4 address players send UDP to')
        if not isinstance(address, ipaddress.IPv4Address):
            parser.error('--public-host: the dotted IPv4 address players send UDP to')
    if args.proxy_prefix and not args.public_host and not args.print_nginx:
        parser.error('--proxy-prefix needs --public-host (behind the proxy the master cannot see its UDP address)')
    if not args.print_nginx and not args.ssh:
        parser.error('--ssh USER@ADDRESS is needed (or --print-nginx)')


def main(argv=None, run=subprocess.run, out=print):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--ssh', metavar='USER@ADDRESS', help='an account that may write the unit file (root)')
    p.add_argument('--user', default='x4master', help='the unprivileged account the master runs as (default x4master)')
    p.add_argument('--dir', default='', help='where it lives (default /home/<user>/x4mp-master)')
    p.add_argument('--service', default='x4mp-master')
    p.add_argument('--public-host', default='', help='the IPv4 address players send UDP to (needed behind a proxy or NAT)')
    p.add_argument('--proxy-prefix', default='', metavar='PATH',
                   help='the API sits behind nginx under this path (e.g. /x4mp): listen on 127.0.0.1, trust its '
                        'X-Forwarded-For, write <dir>/nginx-location.conf, keep TCP 47100 closed')
    p.add_argument('--print-nginx', action='store_true', help='print the nginx location block for --proxy-prefix and stop')
    args = p.parse_args(argv)
    check(p, args)
    prefix = args.proxy_prefix.rstrip('/')
    if args.print_nginx:
        out(nginx_text(prefix or '/x4mp').rstrip())
        return 0
    folder = args.dir or f'/home/{args.user}/x4mp-master'
    data, commit = pack(run)
    name = f'{time.strftime("%Y%m%d-%H%M%S")}-{commit[:12]}'
    ssh = ['ssh', '-o', 'BatchMode=yes', args.ssh]
    target = f'{folder}/releases/{name}'
    out(f'copying {len(data) // 1024} KiB ({commit[:12]}) to {args.ssh}:{target}')
    run([*ssh, f'mkdir -p {target} && tar xzf - -C {target}'], input=data, check=True)
    script = remote_script(args.user, folder, name, args.service, args.public_host, prefix)
    done = run([*ssh, 'bash -s'], input=script.encode(), capture_output=True)
    out(done.stdout.decode(errors='replace').strip())
    if done.returncode:
        print(done.stderr.decode(errors='replace').strip(), file=sys.stderr)
        return 1
    api = f'behind the proxy at {prefix}/' if prefix else f'TCP {HTTP_PORT}'
    out(f'master: API {api}, echo UDP {ECHO_PORT}, relays UDP/TCP {RELAY_FIRST}-{RELAY_LAST}; service {args.service}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
