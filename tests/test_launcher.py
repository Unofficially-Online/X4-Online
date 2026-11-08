import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import extension, game, profile, session  # noqa: E402


def symlink_junction(target, link):
    if os.name == 'nt':
        game._junction(target, link)  # real junction: symlinks need a privilege on Windows
    else:
        os.symlink(target, link, target_is_directory=True)


def write_overlay(path, config='X4LAN_CONFIG.port = @X4LANE_PORT@\nX4LAN_CONFIG.token = "@X4LANE_TOKEN@"\n'
                                'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\nX4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n'):
    files = {'ui/native_gate_probe.lua': 'print(1)\n', 'native/x4lan_gate_64.dll': 'MZ', 'md/battle.xml': '<mdscript/>',
             'ui.xml.files': 'ui/native_gate_probe.lua\n', 'ui/config.lua.append': config}
    for name, text in files.items():
        (path / name).parent.mkdir(parents=True, exist_ok=True)
        (path / name).write_text(text)
    manifest = {name: hashlib.sha256((path / name).read_bytes()).hexdigest() for name in files}
    (path / 'x4lane-manifest.json').write_text(json.dumps(manifest))


class Extension(unittest.TestCase):
    def test_compose_substitutes_slot_port_token_clients_and_generates_base(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write_overlay(tmp / 'overlay')
            target = extension.compose(tmp / 'overlay', tmp / 'ext', 2, 50123, 'a' * 32, 3)
            config = (target / 'ui/config.lua').read_text()
            self.assertTrue(config.startswith('X4LAN_CONFIG = {enabled = false}\n'))
            for expected in ('port = 50123', 'token = "' + 'a' * 32 + '"', 'slot = "p2"', 'native_gate_clients = 3'):
                self.assertIn(expected, config)
            ui = (target / 'ui.xml').read_text()
            self.assertLess(ui.index('ui/config.lua'), ui.index('ui/native_gate_probe.lua'))
            self.assertIn('id="x4_lan_lab"', (target / 'content.xml').read_text())
            self.assertTrue((target / 'native/x4lan_gate_64.dll').is_file())
            self.assertTrue((target / 'ui/sha256.lua').is_file())
            self.assertTrue((target / 'libraries/factions.xml').is_file())
            self.assertLess(ui.index('ui/sha256.lua'), ui.index('ui/native_gate_probe.lua'))
            for leftover in ('x4lane-manifest.json', 'ui.xml.files', 'ui/config.lua.append'):
                self.assertFalse((target / leftover).exists())

    def test_capture_overlay_without_fixedtick_gets_autostart_first(self):
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        from native_gate_scenarios import build_capture_overlay
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            overlay = build_capture_overlay('custom_creative', None, tmp / 'cap')
            self.assertFalse((overlay / 'native').exists() and any((overlay / 'native').iterdir()))
            autostart = ROOT / 'src/lockstep/launcher/autostart.lua'
            target = extension.compose(overlay, tmp / 'ext', 1, 0, '0' * 32, 1, extra={'ui/x4ls_autostart.lua': autostart},
                                       config_tail='X4LAN_CONFIG.x4ls_autostart = "custom_creative"\n')
            config = (target / 'ui/config.lua').read_text()
            self.assertIn('native_gate_snapshot_fixed = false', config)
            self.assertIn('x4ls_autostart = "custom_creative"', config)
            ui = (target / 'ui.xml').read_text()
            self.assertLess(ui.index('ui/x4ls_autostart.lua'), ui.index('ui/native_gate_snapshot.lua'))

    def test_tampered_overlay_and_unknown_placeholder_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write_overlay(tmp / 'o1')
            (tmp / 'o1/ui/native_gate_probe.lua').write_text('changed')
            with self.assertRaisesRegex(SystemExit, 'differs from its manifest'):
                extension.compose(tmp / 'o1', tmp / 'e1', 1, 1, 'a' * 32, 2)
            write_overlay(tmp / 'o2', config='x = "@X4LANE_OTHER@"\n')
            with self.assertRaisesRegex(SystemExit, 'unknown @X4LANE_'):
                extension.compose(tmp / 'o2', tmp / 'e2', 1, 1, 'a' * 32, 2)


class Profiles(unittest.TestCase):
    def test_only_disposable_ids_and_fresh_windowed_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, 'not a disposable'):
                profile.prepare(63389028, [], root=tmp)
            stale = Path(tmp) / '45850/save/autosave_01.xml.gz'
            stale.parent.mkdir(parents=True)
            stale.write_text('old world')
            snap = Path(tmp) / 'snap.xml.gz'
            snap.write_bytes(b'snapshot')
            path = profile.prepare(45850, ['ego_dlc_split', 'x4_lan_lab'], 800, 600, snap, root=tmp)
            self.assertFalse(stale.exists())
            config = (path / 'config.xml').read_text()
            for key in ('<autosave>false', '<uisafemode>false', '<fullscreen>false', '<res_width>800'):
                self.assertIn(key, config)
            self.assertEqual((path / 'save' / profile.SNAPSHOT_SLOT).read_bytes(), b'snapshot')
            self.assertIn('id="x4_lan_lab" enabled="true"', (path / 'content.xml').read_text())


class Unstage(unittest.TestCase):
    def test_the_staged_snapshot_is_removed_after_a_session_and_saves_stay(self):
        with tempfile.TemporaryDirectory() as tmp:
            snapshot = Path(tmp) / 'snap.xml.gz'
            snapshot.write_bytes(b'snapshot')
            pid = profile.PROFILES[0]
            path = profile.prepare(pid, ['ego_dlc_boron'], snapshot=snapshot, root=tmp)
            (path / 'save' / 'save_001.xml.gz').write_bytes(b'checkpoint')
            self.assertTrue(profile.unstage(pid, root=tmp))
            self.assertFalse((path / 'save' / profile.SNAPSHOT_SLOT).exists())
            self.assertTrue((path / 'save' / 'save_001.xml.gz').exists())
            self.assertFalse(profile.unstage(pid, root=tmp))


class Views(unittest.TestCase):
    def test_view_links_game_and_removal_never_enters_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            install = tmp / 'game'
            (install / 'extensions/ego_dlc_split').mkdir(parents=True)
            (install / 'extensions/thirdparty_mod').mkdir()
            (install / 'aiscripts').mkdir()
            (install / 'aiscripts/keep.xml').write_text('k')
            (install / 'X4.exe').write_text('exe')
            view = game.create_view(install, tmp / 'lanes/c1', junction=symlink_junction)
            self.assertTrue(os.path.samefile(view / 'X4.exe', install / 'X4.exe'))
            self.assertEqual(game.dlc_ids(view), ['ego_dlc_split'])
            self.assertFalse((view / 'extensions/thirdparty_mod').exists())
            (view / 'extensions/x4_lan_lab').mkdir()
            game.remove_view(view)
            self.assertFalse(view.exists())
            self.assertEqual((install / 'aiscripts/keep.xml').read_text(), 'k')
            self.assertTrue((install / 'X4.exe').is_file())
            self.assertTrue((install / 'extensions/ego_dlc_split').is_dir())

    def test_removal_refuses_foreign_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, 'not an x4ls view'):
                game.remove_view(tmp)

    def test_library_paths_parse_escaped_vdf(self):
        vdf = '"libraryfolders"\n{\n "0" { "path" "C:\\\\Program Files (x86)\\\\Steam" }\n "1" { "path" "F:\\\\SteamLibrary" } }'
        self.assertEqual([str(p) for p in game.library_paths(vdf)], ['C:\\Program Files (x86)\\Steam', 'F:\\SteamLibrary'])

    def test_pin_matches_gate_source(self):
        gate = (ROOT / 'src/native/engine/lockstep_gate.c').read_text()
        self.assertIn(f'#define EXE_SHA "{game.EXE_SHA256}"', gate)
        self.assertIn(f'size.QuadPart!={game.EXE_SIZE}', gate)


class Session(unittest.TestCase):
    def test_relay_command_and_log_patterns(self):
        o = session.Options(clients=3, overlay=Path('o'), snapshot=Path('s.xml.gz'), ticks=600, tick_hz=30, observe=True)
        cmd = session.relay_command(o, 5000, 'b' * 32, Path('run'))
        self.assertEqual(cmd[1], str(session.RELAY))
        for expected in (['--clients', '3'], ['--ticks', '600'], ['--tick-hz', '30']):
            self.assertIn(' '.join(expected), ' '.join(cmd))
        for flag in ('--relative-real', '--observe', '--input-journal'):
            self.assertIn(flag, cmd)
        self.assertTrue(session.RELAY.is_file())
        self.assertTrue(session.STOP_OK.search('[X4LAN] NATIVE_GATE STOP tick=600 reason=1 restored=1'))
        self.assertIsNone(session.FAIL.search('[X4LAN] NATIVE_GATE STOP tick=600 reason=1 restored=1'))
        self.assertTrue(session.FAIL.search('[X4LAN] NATIVE_GATE STOP tick=20 reason=91 restored=1'))
        self.assertTrue(session.FAIL.search('[X4LAN] NATIVE_GATE REFUSED exe'))
        # The defaults' loud warnings do not end a session; every other ERROR row does.
        self.assertIsNone(session.FAIL.search('[X4LAN] NATIVE_GATE ERROR DEGRADED status=FIND_ORDER unmapped=12 (+12) tick=1'))
        self.assertIsNone(session.FAIL.search('[X4LAN] NATIVE_GATE ERROR DEFAULT_SKIPPED key=native_gate_render_fps'))
        self.assertTrue(session.FAIL.search('[X4LAN] NATIVE_GATE ERROR CONFIG_REFUSED native_gate_x'))
        self.assertTrue(session.FAIL.search('[X4LAN] NATIVE_GATE ERROR frame: attempt to index nil'))
        args = session.client_command(Path('v'), 45851, 2)
        self.assertEqual(args[1:3], ['-personalfolderid', '45851'])
        self.assertIn(f'-seed:{session.SESSION_SEED}', args)
        self.assertTrue(re.fullmatch(r'x4ls-c2\.log', args[-1]))



class Play(unittest.TestCase):
    def test_relay_command_settings_and_log_tail(self):
        with tempfile.TemporaryDirectory() as tmp:
            overlay = Path(tmp) / 'o'
            write_overlay(overlay, 'X4LAN_CONFIG.native_gate_tick_hz = 30\nX4LAN_CONFIG.native_gate_lookahead = 1\n'
                                   'X4LAN_CONFIG.native_gate_pilot_window = {40,81040}\nX4LAN_CONFIG.port = @X4LANE_PORT@\n')
            self.assertEqual(session.play_settings(overlay), (1, 30, 81040))
            o = session.Options(clients=2, overlay=overlay, snapshot=Path('s.xml.gz'), ticks=81043, tick_hz=30, lookahead=1,
                                play=True, stall_drop_ms=15000)
            cmd = ' '.join(session.relay_command(o, 5000, 'b' * 32, Path('run')))
            for expected in ('--observe', '--keep-going', '--lookahead 1', '--stall-drop-ms 15000', '--ticks 81043', '--status-file'):
                self.assertIn(expected, cmd)
            self.assertNotIn('--keep-going', ' '.join(session.relay_command(session.Options(clients=2, overlay=overlay), 1, 'b' * 32,
                                                                            Path('run'))))
            log = Path(tmp) / 'x.log'
            tail = session.LogTail(log)
            self.assertEqual(tail.lines(), [])                   # not there yet
            log.write_bytes(b'one\r\ntw')
            self.assertEqual(tail.lines(), ['one'])
            with log.open('ab') as f:
                f.write(b'o\nthree\n')
            self.assertEqual(tail.lines(), ['two', 'three'])
            self.assertEqual(tail.lines(), [])

    def test_two_pcs_relay_tail_and_join_line(self):
        o = session.Options(clients=2, overlay=Path('o'), snapshot=Path('s.xml.gz'), ticks=900, tick_hz=30, lookahead=1, play=True,
                            stall_drop_ms=15000, remote_slots=(2,), bind='192.168.1.5', allow_from=('192.168.1.7',), port=47000)
        cmd = ' '.join(session.relay_command(o, 47000, 'b' * 32, Path('run')))
        self.assertIn('--bind 192.168.1.5 --allow-from 192.168.1.7,192.168.1.5', cmd)   # the host's own clients too
        tail = session.client_tail(1700000000, relay_host='192.168.1.5')
        self.assertIn('native_gate_arm_at = 1700000000', tail)
        self.assertIn("native_gate_relay_host = '192.168.1.5'", tail)
        self.assertNotIn('relay_host', session.client_tail(1700000000))
        with self.assertRaises(SystemExit):
            session.client_tail(relay_host='relay.example')
        line = session.join_command(o, '203.0.113.9', 47000, 'b' * 32, 2, 1700000000)
        self.assertIn('join --relay 203.0.113.9:47000 --token ' + 'b' * 32 + ' --slot 2 --clients 2 --arm-at 1700000000', line)
        for bad in (dict(port=0), dict(bind='127.0.0.1'), dict(allow_from=()), dict(remote_slots=(1,)), dict(public_host='x')):
            with self.subTest(bad=bad), self.assertRaises(SystemExit):
                session.run(session.Options(**{**o.__dict__, **bad, 'state': Path(tempfile.gettempdir()) / 'x4ls-none'}),
                            log=lambda *_: None)
        with self.assertRaises(SystemExit):   # a join checks its arguments before anything else
            session.join(session.JoinOptions(relay='relay.example:47000', token='b' * 32, slot=2, clients=2, arm_at=0,
                                             overlay=Path('o'), snapshot=Path('s')))

    def test_errors_and_exits_are_logged_and_the_session_runs_until_every_window_closed(self):
        class Proc:
            def __init__(self, pid):
                self.pid, self.returncode, self.alive = pid, None, True

            def poll(self):
                return None if self.alive else self.returncode

        with tempfile.TemporaryDirectory() as tmp:
            run_dir, docs = Path(tmp) / 'run', Path(tmp) / 'docs'
            run_dir.mkdir()
            ids = profile.PROFILES[:2]
            for i in ids:
                (docs / str(i)).mkdir(parents=True)
            clients = [(1, ids[0], Proc(11)), (2, ids[1], Proc(12))]
            logs = {k: docs / str(i) / f'x4ls-c{k}.log' for k, i, _ in clients}
            for path in logs.values():
                path.write_text('')
            relay_log = run_dir / 'relay.log'
            relay_log.write_text('')
            script = [
                lambda: relay_log.write_text('NATIVE_GATE tick=0 failure=none done=False\n'),
                lambda: logs[1].write_text('[X4LAN] NATIVE_GATE ERROR frame: attempt to index nil\n'
                                           '[X4LAN] NATIVE_GATE ERROR frame: attempt to index nil\n'),
                lambda: relay_log.write_text('NATIVE_GATE tick=0 failure=none done=False\n'
                                             'NATIVE_GATE DESYNC tick=90 slots=2 first=90 mismatches=1\n'),
                lambda: logs[2].write_text('[X4LAN] NATIVE_GATE STOP tick=120 reason=91 restored=1\n'),
                lambda: setattr(clients[1][2], 'alive', False) or setattr(clients[1][2], 'returncode', 3),
                lambda: logs[1].write_text(logs[1].read_text() + '[X4LAN] NATIVE_GATE STOP tick=900 reason=1 restored=1\n'),
                lambda: setattr(clients[0][2], 'alive', False) or setattr(clients[0][2], 'returncode', 0),
            ]
            said = []

            def sleep(_):
                self.assertTrue(script, 'monitor_play ran past the end')
                script.pop(0)()
            keep = session.time.sleep, profile.documents, session.tile
            session.time.sleep, profile.documents, session.tile = sleep, (lambda: docs), (lambda *a: 2)
            try:
                o = session.Options(clients=2, overlay=Path(tmp), tick_hz=30, play=True)
                result = session.monitor_play(o, run_dir, None, clients, said.append)
            finally:
                session.time.sleep, profile.documents, session.tile = keep
            self.assertEqual(result['outcome'], 'played')
            self.assertEqual(result['ended'], 'every X4 window closed')
            self.assertEqual((result['errors'], result['stopped'], result['exited']),
                             (3, {"client1": 1, "client2": 91}, {'client1': 0, 'client2': 3}))
            text = (run_dir / 'errors.log').read_text()
            self.assertEqual(text.count('client1 [X4LAN] NATIVE_GATE ERROR frame'), 2)
            self.assertIn('client2 [X4LAN] NATIVE_GATE STOP tick=120 reason=91', text)
            self.assertIn('client2 X4 exited with 3', text)
            self.assertIn('NATIVE_GATE DESYNC tick=90', (run_dir / 'desyncs.log').read_text())
            # The console: one line per kind and client, the drop explained, the end of the session.
            self.assertEqual(len([s for s in said if 'ERROR frame:' in s]), 1)
            self.assertTrue(any('its gate stopped (reason 91)' in s for s in said))
            self.assertTrue(any('the session is over' in s for s in said))


class Checkpoint(unittest.TestCase):
    class Done:  # a client process that has already exited
        def poll(self):
            return 0

    class Running:
        def poll(self):
            return None

    def test_every_request_is_answered_before_collection(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'x4ls-c1.log'
            log.write_text('x' * 3000 + '\n[X4LAN] NATIVE_GATE CHECKPOINT_REQUEST slot=save_001 tick=900\n'
                           '[X4LAN] NATIVE_GATE CHECKPOINT saved slot=save_001 tick=900\n'
                           '[X4LAN] NATIVE_GATE CHECKPOINT refused slot=save_002 tick=1500\n')
            self.assertEqual(session.checkpoint_results(log, self.Running(), timeout=5), {900: 'saved', 1500: 'refused'})
            self.assertNotIn('xxxx' * 300, session.log_tail(log, size=200))
            with log.open('a') as f:
                f.write('[X4LAN] NATIVE_GATE CHECKPOINT_REQUEST slot=save_003 tick=2100\n')
            self.assertIsNone(session.checkpoint_results(log, self.Done(), timeout=5))  # exited with a save pending
            self.assertEqual(session.checkpoint_results(Path(tmp) / 'missing.log', self.Done(), timeout=5), {})


class Seed(unittest.TestCase):
    def test_session_seed_comes_from_the_save_header(self):
        import gzip
        with tempfile.TemporaryDirectory() as tmp:
            save = Path(tmp) / 's.xml.gz'
            with gzip.open(save, 'wb') as f:
                f.write(b'<?xml version="1.0"?><savegame><info><save name="x"/><game id="X4" version="900" '
                        b'start="custom_creative" seed="276944566" guid="g"/></info><universe/></savegame>')
            self.assertEqual(session.snapshot_seed(save), 276944566)


class Sha256(unittest.TestCase):
    def test_lua_digest_matches_hashlib_across_padding_boundaries(self):
        from lupa.luajit21 import LuaRuntime
        lua = LuaRuntime(encoding=None)
        lua.execute((ROOT / 'src/lockstep/base/ui/sha256.lua').read_bytes())
        digest = lua.eval('X4LAN_SHA256.digest')
        for data in (b'', b'abc', b'a' * 55, b'a' * 56, b'a' * 64, b'a' * 119, bytes(range(256)) * 5):
            self.assertEqual(digest(data).decode(), hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    unittest.main()
