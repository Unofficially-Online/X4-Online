"""tools/deploy_master_server.py: the unit, the ufw rules, the nginx block and the commands it would run. Nothing here
connects anywhere: every command goes to a stand-in runner."""
import contextlib
import gzip
import importlib.util
import io
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('deploy_master_server', ROOT / 'tools/deploy_master_server.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)

HOST = '203.0.113.10'


class Runner:
    """Records the commands; git answers with a stand-in archive and commit, ssh succeeds."""
    def __init__(self, returncode=0):
        self.commands = []
        self.inputs = []
        self.returncode = returncode

    def __call__(self, command, **options):
        self.commands.append(command)
        self.inputs.append(options.get('input'))
        if command[:1] == ['git'] and 'archive' in command:
            return subprocess.CompletedProcess(command, 0, stdout=b'tar bytes', stderr=b'')
        if command[:1] == ['git']:
            return subprocess.CompletedProcess(command, 0, stdout=b'0123456789abcdef0123456789abcdef01234567\n', stderr=b'')
        return subprocess.CompletedProcess(command, self.returncode, stdout=b'active\n', stderr=b'failed')


class UnitTests(unittest.TestCase):
    def test_the_unit_runs_the_master_on_the_contract_ports_as_the_unprivileged_user(self):
        unit = deploy.unit_text('x4master', '/home/x4master/x4mp-master')
        self.assertIn('User=x4master\n', unit)
        self.assertIn('Group=x4master\n', unit)
        self.assertNotIn('User=root', unit)
        start = next(line for line in unit.splitlines() if line.startswith('ExecStart='))
        self.assertEqual(start, 'ExecStart=/usr/bin/python3 -u /home/x4master/x4mp-master/current/tools/x4ls.py master '
                                '--bind 0.0.0.0 --http-port 47100 --echo-port 47101 --relay-ports 47200-47263 '
                                '--data /home/x4master/x4mp-master/data')
        for line in ('NoNewPrivileges=yes', 'ProtectSystem=strict', 'ReadWritePaths=/home/x4master/x4mp-master/data',
                     'Restart=always', 'PrivateTmp=yes'):
            self.assertIn(line + '\n', unit)

    def test_behind_the_proxy_the_api_listens_on_loopback(self):
        unit = deploy.unit_text('x4master', '/srv/x4mp', public_host=HOST, proxy_prefix='/x4mp')
        start = next(line for line in unit.splitlines() if line.startswith('ExecStart='))
        self.assertIn('--http-bind 127.0.0.1 --trust-proxy --public-prefix /x4mp --public-host 203.0.113.10', start)
        self.assertIn('--bind 0.0.0.0', start)   # the echo and the relays stay public

    def test_ufw_opens_exactly_the_masters_ports(self):
        direct = deploy.ufw_rules(proxied=False)
        self.assertEqual([rule.split(' comment')[0] for rule in direct],
                         ['ufw allow 47100/tcp', 'ufw allow 47101/udp', 'ufw allow 47200:47263/udp',
                          'ufw allow 47200:47263/tcp'])
        proxied = deploy.ufw_rules(proxied=True)
        self.assertEqual([rule.split(' comment')[0] for rule in proxied],
                         ['ufw allow 47101/udp', 'ufw allow 47200:47263/udp', 'ufw allow 47200:47263/tcp'])

    def test_the_nginx_block(self):
        text = deploy.nginx_text('/x4mp')
        self.assertIn('location /x4mp/ {', text)
        self.assertIn('location = /x4mp {', text)
        self.assertIn('proxy_pass http://127.0.0.1:47100/;', text)
        self.assertIn('proxy_set_header X-Forwarded-For $remote_addr;', text)
        self.assertIn('client_max_body_size 512m;', text)
        self.assertIn('master.example.org', text)

    def test_the_remote_script(self):
        script = deploy.remote_script('x4master', '/home/x4master/x4mp-master', '20261123-000000-0123456789ab', 'x4mp-master',
                                      public_host=HOST, proxy_prefix='/x4mp')
        self.assertIn('useradd --system --create-home --home-dir /home/x4master --shell /usr/sbin/nologin x4master', script)
        self.assertIn('ln -sfn releases/20261123-000000-0123456789ab /home/x4master/x4mp-master/current', script)
        self.assertIn("cat > /etc/systemd/system/x4mp-master.service <<'UNIT'", script)
        self.assertIn("cat > /home/x4master/x4mp-master/nginx-location.conf <<'NGINX'", script)
        self.assertNotIn('/etc/nginx', script)
        self.assertNotIn('systemctl reload nginx\n', script.replace('(nginx -t, systemctl reload nginx)', ''))
        self.assertNotIn('47100/tcp', script)
        self.assertEqual(script.count('ufw allow'), 3)
        plain = deploy.remote_script('x4master', '/home/x4master/x4mp-master', 'name', 'x4mp-master')
        self.assertNotIn('nginx', plain)
        self.assertEqual(plain.count('ufw allow'), 4)


class MainTests(unittest.TestCase):
    def test_a_deploy_runs_git_locally_and_two_ssh_commands(self):
        runner = Runner()
        printed = []
        self.assertEqual(deploy.main(['--ssh', f'root@{HOST}'], run=runner, out=printed.append), 0)
        self.assertEqual([command[0] for command in runner.commands], ['git', 'git', 'ssh', 'ssh'])
        self.assertEqual(gzip.decompress(runner.inputs[2]), b'tar bytes')
        unpack = runner.commands[2]
        self.assertEqual(unpack[:4], ['ssh', '-o', 'BatchMode=yes', f'root@{HOST}'])
        self.assertRegex(unpack[4], r'^mkdir -p /home/x4master/x4mp-master/releases/\d{8}-\d{6}-0123456789ab && tar xzf - -C ')
        self.assertEqual(runner.commands[3][4], 'bash -s')
        self.assertIn(b'x4mp-master.service', runner.inputs[3])
        self.assertIn('service x4mp-master', printed[-1])

    def test_a_failing_remote_script_is_reported(self):
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(deploy.main(['--ssh', f'root@{HOST}'], run=Runner(returncode=1), out=lambda text: None), 1)
        self.assertIn('failed', errors.getvalue())

    def test_print_nginx_connects_nowhere(self):
        runner = Runner()
        printed = []
        self.assertEqual(deploy.main(['--print-nginx', '--proxy-prefix', '/x4mp/'], run=runner, out=printed.append), 0)
        self.assertEqual(runner.commands, [])
        self.assertIn('location /x4mp/ {', printed[0])

    def test_arguments_that_would_reach_the_shell_are_refused(self):
        refused = [['--ssh', 'root@h', '--user', 'x;rm'], ['--ssh', 'root@h', '--dir', 'relative'],
                   ['--ssh', 'root@h', '--dir', '/srv/x4 mp'], ['--ssh', 'root@h', '--service', 'a b'],
                   ['--ssh', 'root@h', '--public-host', 'master.example.org'], ['--ssh', 'root@h', '--proxy-prefix', '/x4mp'],
                   ['--ssh', 'root@h', '--proxy-prefix', '/x"4', '--public-host', HOST], []]
        for argv in refused:
            runner = Runner()
            with self.assertRaises(SystemExit, msg=argv), contextlib.redirect_stderr(io.StringIO()):
                deploy.main(argv, run=runner, out=lambda text: None)
            self.assertEqual(runner.commands, [], argv)


if __name__ == '__main__':
    unittest.main()
