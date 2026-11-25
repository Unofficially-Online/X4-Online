"""x4ls play --solo K, offline: one X4 for slot K of an N-slot session, the relay standing in for every other slot from tick 0
(session.solo_check, relay_command; the run with every process start mocked: no X4 and no relay is started here)."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tools'))
from lockstep.launcher import __main__ as cli, profile, session  # noqa: E402
import build_play_overlay  # noqa: E402

CONFIG = ('X4LAN_CONFIG.port = @X4LANE_PORT@\nX4LAN_CONFIG.token = "@X4LANE_TOKEN@"\nX4LAN_CONFIG.slot = "@X4LANE_SLOT@"\n'
          'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\nX4LAN_CONFIG.native_gate_tick_hz = 30\n'
          'X4LAN_CONFIG.native_gate_lookahead = 1\nX4LAN_CONFIG.native_gate_pilot_window = {40,72040}\n')


def write_overlay(path, config=CONFIG, players=3, opened=()):
    files = {'ui/native_gate_probe.lua': 'print(1)\n', 'native/x4lan_gate_64.dll': 'MZ', 'ui/config.lua.append': config}
    for name, text in files.items():
        (path / name).parent.mkdir(parents=True, exist_ok=True)
        (path / name).write_text(text)
    if players:
        recipe = {'kind': build_play_overlay.RECIPE_KIND, 'version': 1, 'role': 'session', 'players': players, 'mode': 'coop',
                  'minutes': 40, 'full_logs': False, 'lua_block': 'Pause,Unpause,SaveGame', 'set': [], 'open': list(opened),
                  'join_mode': 'report', 'window': [40, 72040], 'gate_dll_sha': '0' * 64, 'snapshot_sha': '1' * 64}
        (path / build_play_overlay.RECIPE).write_text(json.dumps(recipe))
    manifest = {p.relative_to(path).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in path.rglob('*') if p.is_file()}
    (path / 'x4lane-manifest.json').write_text(json.dumps(manifest))
    return path


def options(overlay, **kw):
    args = dict(clients=3, overlay=overlay, snapshot=Path('s.xml.gz'), ticks=72041, tick_hz=30, lookahead=1, play=True,
                stall_drop_ms=15000, solo=2)
    args.update(kw)
    return session.Options(**args)


class SoloRelayTests(unittest.TestCase):
    def test_the_relay_stands_in_for_every_other_slot(self):
        o = options(Path('ov'))
        cmd = session.relay_command(o, 47000, 'b' * 32, Path('run'))
        self.assertEqual(cmd[cmd.index('--stand-in') + 1], '1,3')
        text = ' '.join(cmd)
        for expected in ('--clients 3', '--lookahead 1', '--observe', '--keep-going', '--stall-drop-ms 15000', '--relative-real'):
            self.assertIn(expected, text)
        self.assertNotIn('--join', text)          # the drop path, never the join path (no absent slot 1 there)
        self.assertEqual(session.solo_stand_ins(options(Path('ov'), solo=1)), [2, 3])
        self.assertEqual(session.solo_stand_ins(options(Path('ov'), clients=8, solo=8)), list(range(1, 8)))
        self.assertNotIn('--stand-in', session.relay_command(options(Path('ov'), solo=None), 1, 'b' * 32, Path('run')))


class SoloCheckTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.overlay = write_overlay(self.dir / 'ov')

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_session_overlay_is_accepted_for_any_slot(self):
        for k in (1, 2, 3):
            session.solo_check(options(self.overlay, solo=k))
        session.solo_check(options(write_overlay(self.dir / 'old', players=0), solo=2))   # an overlay without a recipe

    def test_refusals(self):
        cases = {'for x4ls play': dict(play=False), 'slot 1..3': dict(solo=4), 'slot 1..3 ': dict(solo=0),
                 'no --remote-slots or --open': dict(remote_slots=(3,)), 'no --remote-slots or --open ': dict(open_slots=(3,)),
                 'protocol 4 overlay': dict(lookahead=0), 'built for 3 players: --clients 3': dict(clients=4, solo=2)}
        for message, bad in cases.items():
            with self.subTest(bad=bad), self.assertRaises(SystemExit) as caught:
                session.solo_check(options(self.overlay, **bad))
            self.assertIn(message.strip(), str(caught.exception))
        hot = write_overlay(self.dir / 'hot', opened=(3,))
        with self.assertRaises(SystemExit) as caught:
            session.solo_check(options(hot))
        self.assertIn('without open (hot-join) slots', str(caught.exception))
        joins = write_overlay(self.dir / 'joins', CONFIG + "X4LAN_CONFIG.native_gate_join = {save = 'report', absent = {3}}\n", players=0)
        with self.assertRaises(SystemExit):
            session.solo_check(options(joins))
        rejoin = write_overlay(self.dir / 'rejoin', CONFIG + "X4LAN_CONFIG.native_gate_join = {save = 'strict', rejoin = {2}}\n", players=0)
        with self.assertRaises(SystemExit):
            session.solo_check(options(rejoin))
        saver = write_overlay(self.dir / 'saver', CONFIG + "X4LAN_CONFIG.native_gate_join = {save = 'strict'}\n", players=0)
        session.solo_check(options(saver))   # a saver's join keys without absent slots: nothing for the join path to stand in

    def test_run_refuses_before_anything_starts(self):
        def never(*_, **__):
            raise AssertionError('started something')
        with mock.patch.object(session.subprocess, 'Popen', never), mock.patch.object(session, 'x4_running', never), \
                mock.patch.object(session.steam, 'status', never), mock.patch.object(session, 'save_state', never):
            for bad in (dict(solo=5), dict(lookahead=0), dict(open_slots=(3,)), dict(play=False)):
                with self.subTest(bad=bad), self.assertRaises(SystemExit):
                    session.run(options(self.overlay, state=self.dir / 'state', runs=self.dir / 'runs', **bad), log=lambda *_: None)


class FakeProc:
    pids = iter(range(4100, 4200))

    def __init__(self, cmd, **_):
        self.cmd, self.pid, self.returncode = list(cmd), next(self.pids), 0

    def poll(self):
        return 0


class SoloRunTests(unittest.TestCase):
    def test_only_client_k_starts_with_its_own_profile_and_slot(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            overlay = write_overlay(tmp / 'ov')
            started, composed, lines = [], [], []

            def popen(cmd, **kw):
                proc = FakeProc(cmd, **kw)
                started.append(proc)
                return proc

            def compose(overlay_dir, extensions, slot, port, token, clients, **kw):
                composed.append((slot, clients, kw.get('profile_id')))
                target = Path(extensions) / 'x4_lan_lab'
                (target / 'ui').mkdir(parents=True, exist_ok=True)
                (target / 'ui/config.lua').write_text(f'slot p{slot}\n')
                return target

            def profile_dir(profile_id, root=None):
                path = tmp / 'profiles' / str(profile_id)
                path.mkdir(parents=True, exist_ok=True)
                return path

            o = options(overlay, snapshot=tmp / 's.xml.gz', state=tmp / 'state', runs=tmp / 'runs', lanes=tmp / 'lanes',
                        game=tmp / 'game')
            patches = [mock.patch.object(session.subprocess, 'Popen', popen), mock.patch.object(session, 'x4_running', return_value=False),
                       mock.patch.object(session.steam, 'status', return_value=(True, 'ok')),
                       mock.patch.object(session.extension, 'verify_overlay'), mock.patch.object(session.extension, 'compose', compose),
                       mock.patch.object(session.game, 'verify_exe'), mock.patch.object(session.game, 'create_view'),
                       mock.patch.object(session.game, 'dlc_ids', return_value=[]), mock.patch.object(session.game, 'remove_view'),
                       mock.patch.object(session.profile, 'prepare'), mock.patch.object(session.profile, 'collect'),
                       mock.patch.object(session.profile, 'unstage'), mock.patch.object(session.profile, 'profile_dir', profile_dir),
                       mock.patch.object(session.profile, 'client_settings', return_value=[]),
                       mock.patch.object(session, 'snapshot_seed', return_value=1234), mock.patch.object(session, 'release_beside'),
                       mock.patch.object(session, 'wait_for_log'), mock.patch.object(session, 'kill', side_effect=AssertionError),
                       mock.patch.object(session.instance, 'release_instance_mutex', return_value=0),
                       mock.patch.object(session, 'monitor_play', return_value={'outcome': 'played'}),
                       mock.patch.object(session.time, 'sleep')]
            for p in patches:
                p.start()
            try:
                result = session.run(o, log=lines.append)
            finally:
                for p in reversed(patches):
                    p.stop()
            self.assertEqual(len(started), 2, [p.cmd for p in started])   # the relay and client 2, nothing else
            relay, client = started
            self.assertEqual(relay.cmd[1], str(session.RELAY))
            self.assertEqual(relay.cmd[relay.cmd.index('--stand-in') + 1], '1,3')
            self.assertEqual(relay.cmd[relay.cmd.index('--clients') + 1], '3')
            self.assertEqual(Path(client.cmd[0]).name, 'X4.exe')
            self.assertEqual(client.cmd[client.cmd.index('-personalfolderid') + 1], str(profile.PROFILES[1]))
            self.assertEqual(client.cmd[-1], 'x4ls-c2.log')
            self.assertEqual(composed, [(2, 3, profile.PROFILES[1])])   # slot p2 of a 3-slot session, its own profile
            self.assertEqual((result['outcome'], result['solo'], result['stand_in']), ('played', 2, [1, 3]))
            self.assertTrue(any(line.startswith('solo: client 2 alone; the relay stands in for slot(s) 1,3') for line in lines), lines)
            self.assertFalse((tmp / 'state' / 'session.json').exists())


class SoloCliTests(unittest.TestCase):
    def test_clients_default_to_the_overlays_players_with_solo(self):
        with tempfile.TemporaryDirectory() as tmp:
            overlay = write_overlay(Path(tmp) / 'ov', players=4)
            self.assertEqual(cli.play_clients(overlay, 2), 4)
            self.assertEqual(cli.play_clients(overlay, None), 2)            # without --solo: as before
            self.assertEqual(cli.play_clients(write_overlay(Path(tmp) / 'old', players=0), 2), 2)
            seen = {}

            def run(o, log=print):
                seen['o'] = o
                return {'outcome': 'played'}
            with mock.patch.object(cli.session, 'run', run), mock.patch('builtins.print'):
                self.assertEqual(cli.main(['play', '--overlay', str(overlay), '--snapshot', 's.xml.gz', '--solo', '3']), 0)
            o = seen['o']
            self.assertEqual((o.clients, o.solo, o.lookahead, o.ticks, o.stall_drop_ms), (4, 3, 1, 72041, 15000))
            self.assertNotIn('--join', session.relay_command(o, 1, 'b' * 32, Path('run')))


if __name__ == '__main__':
    unittest.main()
