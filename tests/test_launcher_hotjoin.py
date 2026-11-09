"""x4ls hot join, offline: the join file service, the host's offer of a join's files, the joiner's NGJOIN poll, the play
recipe and the relay's join arguments (src/lockstep/launcher/joinfiles.py, hotjoin.py; tools/build_play_overlay.py)."""
import hashlib
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/dev'))
from lockstep.launcher import hotjoin, joinfiles, profile, session  # noqa: E402
import build_play_overlay  # noqa: E402
import native_gate_join as join  # noqa: E402

TOKEN = '0123456789abcdef0123456789abcdef'


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def package_for(save_bytes, tick=600, clients=3):
    """A version 1 package (the golden's shapes) naming a save of these bytes."""
    gate = {name: 0 for name in join.GATE_FIELDS}
    gate.update(tick=tick, game=bits(27.0), real=bits(120.0), wire_version=3, clients=clients, lookahead=2, tick_hz=30, slot=1,
                md_seq=4242, rid_next=0x7f0001, created_generation=5, created_live=1, identities=2, ctx_count=2, wake_count=1,
                vec_index=1, vec0=1, vec1=0, md_count=2, md_void=1, md_release=0, md_queued=0, unsupported=0, digest='c' * 64,
                save_us=1500000, capture_us=2500, ambiguous=0, flags=0, fixture='111111121111')
    sections = {
        join.SEC_IDS: [(0x1001, 0x2001, 0, 0), (0x1002, (1 << 63) | 0x55, 1, 0)],
        join.SEC_CTX: [(0x2001, 0xaaaa, 0, 3), ((1 << 63) | 0x55, 0xbbbb, 1, 1)],
        join.SEC_WAKE: [(0x2001, 0xaaaa, bits(27.5), 0, 3, 2, 0)],
        join.SEC_WAKEVEC: [((1 << 63) | 0x55, 0xbbbb, 1, 1, 0, 0, 4, 0)],
        join.SEC_MDHEAP: [(0xcccc, 0, bits(28.0), bits(20.0), 17, 2, 0, 2), (0xdddd, 0x1234, bits(27.75), bits(-1.0), 18, 1, 0, 1)],
        join.SEC_MODULES: ['ai_wait_rng=superseded', 'world_identities=serialized'],
        join.SEC_LUA: ['identity=1|ready|00112233'],
    }
    return join.write_package(gate, sections, save_sha=hashlib.sha256(save_bytes).digest(), save_bytes=len(save_bytes), version=1)


class FileServiceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.data = bytes(range(256)) * 9000   # 2.3 MB: more than one chunk
        (self.dir / 'j600.x4join').write_bytes(self.data)
        self.sha = hashlib.sha256(self.data).hexdigest()
        self.service = joinfiles.FileService(TOKEN, '127.0.0.1', 0, slots=(3,)).start()
        self.service.offer('j600.x4join', self.dir / 'j600.x4join', self.sha)

    def tearDown(self):
        self.service.close()
        self.tmp.cleanup()

    def ask(self, line):
        with socket.create_connection(('127.0.0.1', self.service.port), timeout=5) as conn:
            conn.sendall(line)
            out = b''
            while True:
                data = conn.recv(65536)
                if not data:
                    return out
                out += data

    def test_fetch_verifies_and_resumes(self):
        dest = self.dir / 'got' / 'j600.x4join'
        self.assertEqual(joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j600.x4join', dest, expect_sha=self.sha), self.sha)
        self.assertEqual(dest.read_bytes(), self.data)
        # A partial download resumes from its length: only the rest crosses the link.
        part = self.dir / 'again' / 'j600.x4join.part'
        part.parent.mkdir()
        part.write_bytes(self.data[:1000000])
        joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j600.x4join', part.parent / 'j600.x4join', expect_sha=self.sha)
        self.assertEqual((part.parent / 'j600.x4join').read_bytes(), self.data)
        self.assertFalse(part.exists())
        self.assertEqual(self.service.served[-1][2:], (1000000, len(self.data)))
        # A corrupt partial file is dropped and fetched again whole.
        part.write_bytes(b'x' * 1000)
        joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j600.x4join', part.parent / 'j600.x4join', expect_sha=self.sha)
        self.assertEqual((part.parent / 'j600.x4join').read_bytes(), self.data)

    def test_refusals(self):
        with self.assertRaisesRegex(joinfiles.FetchError, r'refused by the host \(mac\)'):
            joinfiles.fetch('127.0.0.1', self.service.port, 'f' * 32, 3, 'j600.x4join', self.dir / 'a', retries=0)
        with self.assertRaisesRegex(joinfiles.FetchError, 'not offered by the host'):
            joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j601.x4join', self.dir / 'b', retries=0, offer_wait_s=0)
        with self.assertRaisesRegex(joinfiles.FetchError, r'\(slot\)'):
            joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 2, 'j600.x4join', self.dir / 'c', retries=0)
        with self.assertRaisesRegex(joinfiles.FetchError, 'not the announced'):
            joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j600.x4join', self.dir / 'd', expect_sha='0' * 64, retries=0)
        with self.assertRaisesRegex(joinfiles.FetchError, 'not a join file name'):
            joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, '../x.x4join', self.dir / 'e', retries=0)
        line = joinfiles.request_line(TOKEN, 3, 'j600.x4join', 0, nonce='00000000000000aa')
        self.assertTrue(self.ask(line).startswith(b'OK|'))
        self.assertEqual(self.ask(line), b'NO|nonce\n')            # the same request again
        self.assertEqual(self.ask(joinfiles.request_line(TOKEN, 3, 'j600.x4join', len(self.data) + 1)), b'NO|offset\n')
        self.assertEqual(self.ask(b'GET / HTTP/1.0\r\n\r\n'), b'NO|request\n')
        self.assertEqual(self.ask(b'X' * 600), b'NO|request\n')
        with self.assertRaisesRegex(ValueError, 'not a join file name'):
            self.service.offer('session.json', self.dir / 'j600.x4join')
        with self.assertRaisesRegex(ValueError, 'not the announced'):
            self.service.offer('j600.x4join', self.dir / 'j600.x4join', '1' * 64)

    def test_waits_for_the_offer(self):
        """The relay says saved before the host offers the files (hj1): 'name' is retried until the offer, within the wait."""
        data = b'late' * 1000
        (self.dir / 'j700.x4join').write_bytes(data)
        timer = threading.Timer(1.5, self.service.offer, ('j700.x4join', self.dir / 'j700.x4join'))
        timer.start()
        try:
            got = joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j700.x4join', self.dir / 'late' / 'j700.x4join',
                                  retries=0, offer_wait_s=20)
        finally:
            timer.cancel()
        self.assertEqual(got, hashlib.sha256(data).hexdigest())
        start = time.monotonic()
        with self.assertRaisesRegex(joinfiles.FetchError, 'not offered by the host within 1 s'):
            joinfiles.fetch('127.0.0.1', self.service.port, TOKEN, 3, 'j702.x4join', self.dir / 'never', retries=0, offer_wait_s=1)
        self.assertLess(time.monotonic() - start, 10)

    def test_address_filter(self):
        other = joinfiles.FileService(TOKEN, '127.0.0.1', 0)
        other.allow = frozenset({'192.0.2.1'})   # loopback removed: the test client is not allowed
        other.start()
        try:
            with self.assertRaisesRegex(joinfiles.FetchError, r'\(address\)'):
                joinfiles.fetch('127.0.0.1', other.port, TOKEN, 3, 'j600.x4join', self.dir / 'f', retries=0)
        finally:
            other.close()


class HostJoinsTests(unittest.TestCase):
    def test_saved_notice_offers_the_saver_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            save_dir = Path(tmp) / 'p1' / 'save'
            (save_dir / 'mpjoin').mkdir(parents=True)
            save = b'<savegame/>' * 1000
            package = package_for(save)
            (save_dir / 'mpjoin/j600.xml.gz').write_bytes(save)
            (save_dir / 'mpjoin/j600.x4join').write_bytes(package)
            sha = hashlib.sha256(package).hexdigest()
            service = joinfiles.FileService(TOKEN, '127.0.0.1', 0, slots=(3,)).start()
            lines = []
            try:
                hosting = hotjoin.HostJoins(service, {1: save_dir}, lines.append, wait_s=2)
                hosting.line('NATIVE_GATE tick=601 failure=none done=False')        # not a join notice
                hosting.line(f'NATIVE_GATE JOIN slot=3 saved slot=1 tick=600 bytes=11000 package={sha} portable={"a" * 32}')
                hosting.line(f'NATIVE_GATE JOIN slot=3 saved slot=2 tick=600 bytes=11000 package={sha} portable={"a" * 32}')
                for thread in hosting.threads:
                    thread.join(10)
                self.assertEqual(sorted(service.offered), ['j600.x4join', 'j600.xml.gz'])
                self.assertIn((3, 600, 1), hosting.offered)
                self.assertEqual(hosting.failed, {(3, 600, 2): 'saver_not_local'})
                self.assertTrue(any('serving j600 from saver 1' in l for l in lines))
                # The joiner side fetches and verifies both against the relay's announcement.
                state = {'tick': 600, 'package_sha': sha}
                got_save, got_package, loaded = hotjoin.fetch_files('127.0.0.1', service.port, TOKEN, 3, state, Path(tmp) / 'joiner',
                                                                    log=lines.append)
                self.assertEqual(got_save.read_bytes(), save)
                self.assertEqual(loaded['sha'], sha)
                # A saver whose package is not the announced one is not served.
                hosting.line(f'NATIVE_GATE JOIN slot=3 saved slot=1 tick=600 bytes=11000 package={"e" * 64} portable={"a" * 32}')
                for thread in hosting.threads:
                    thread.join(10)
                self.assertIn('the saver announced eeeeeeeeeeeeeeee', hosting.failed[(3, 600, 1)])
            finally:
                service.close()
            hotjoin.remove_join_files({1: save_dir})
            self.assertEqual(list((save_dir / 'mpjoin').iterdir()), [])


class RequestTests(unittest.TestCase):
    def fake_relay(self, answers):
        """A UDP relay that answers each NGJOIN with the next state of answers (the last one repeats)."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
        sock.settimeout(5)
        seen = []

        def serve():
            i = 0
            while i < 50:
                try:
                    data, address = sock.recvfrom(2048)
                except OSError:
                    return
                f = join.parse(data, TOKEN, 'NGJOIN', 2)
                if not f:
                    continue
                seen.append(f)
                state = answers[min(i, len(answers) - 1)]
                sock.sendto(join.message('NGJOINSTATE', TOKEN, f[0], f[1], *state), address)
                i += 1
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        return sock, seen

    def test_until_saved(self):
        relay, seen = self.fake_relay([('queued', 600, 0, 0, '-', '-'), ('saved', 600, 1, 4096, 'b' * 64, '-')])
        try:
            lines = []
            state = hotjoin.request('127.0.0.1', relay.getsockname()[1], TOKEN, 3, wait_s=10, log=lines.append, interval=0.05)
        finally:
            relay.close()
        self.assertEqual((state['state'], state['tick'], state['saver'], state['package_sha']), ('saved', 600, 1, 'b' * 64))
        self.assertEqual({f[1] for f in seen}, {state['nonce']})   # one nonce: a poll, not a second request
        self.assertEqual(lines, ['join: queued (save tick 600)', 'join: saved (save tick 600)'])

    def test_refused(self):
        relay, _ = self.fake_relay([('failed', 0, 0, 0, '-', 'slot_not_absent')])
        try:
            with self.assertRaisesRegex(SystemExit, 'slot_not_absent'):
                hotjoin.request('127.0.0.1', relay.getsockname()[1], TOKEN, 3, wait_s=10, log=lambda *_: None, interval=0.05)
        finally:
            relay.close()

    def test_no_relay(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as quiet:
            quiet.bind(('127.0.0.1', 0))
            with self.assertRaisesRegex(SystemExit, 'no saved join after'):
                hotjoin.request('127.0.0.1', quiet.getsockname()[1], TOKEN, 3, wait_s=0.3, log=lambda *_: None, interval=0.05)


class ResumeTests(unittest.TestCase):
    def test_previous_nonce(self):
        """A rerun resumes this PC's last join of the slot at that relay (the relay answers another nonce busy)."""
        with tempfile.TemporaryDirectory() as d:
            runs = Path(d)
            self.assertIsNone(hotjoin.previous_nonce(runs, 2, '127.0.0.1:47010'))
            for name, nonce, relay in (('20261108-154334-hotjoin2', '0d537d044f1f9b4a', '127.0.0.1:47010'),
                                       ('20261108-154335-hotjoin3', '1111111111111111', '127.0.0.1:47010')):
                (runs / name / 'join').mkdir(parents=True)
                (runs / name / 'join/state.json').write_text(json.dumps({'nonce': nonce, 'relay': relay}))
            self.assertEqual(hotjoin.previous_nonce(runs, 2, '127.0.0.1:47010'), '0d537d044f1f9b4a')
            self.assertIsNone(hotjoin.previous_nonce(runs, 2, '192.0.2.1:47010'))   # another session's relay
            self.assertIsNone(hotjoin.previous_nonce(runs, 2, '127.0.0.1:47010', max_age_s=-1))   # past the joiner timeout


class CollectTests(unittest.TestCase):
    def test_collect_copies_files_and_leaves_the_join_folder(self):
        """A hot join's save/mpjoin folder (hundreds of MB) must not end the collect (hj1's PermissionError)."""
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            p = root / str(profile.PROFILES[0])
            (p / 'save/mpjoin').mkdir(parents=True)
            (p / 'save/mpjoin/j701.x4join').write_bytes(b'x')
            (p / 'save' / profile.SNAPSHOT_SLOT).write_bytes(b'staged')
            (p / 'save/save_001.xml.gz').write_bytes(b'checkpoint')
            (p / 'x4ls-c1.log').write_text('log')
            out = root / 'run/client1'
            profile.collect(profile.PROFILES[0], 'x4ls-c1.log', out, root=root)
            self.assertEqual(sorted(f.name for f in out.iterdir()), ['engine-output.log', 'save_001.xml.gz'])


class OpenRecipeTests(unittest.TestCase):
    def test_open_overlay_gets_the_join_keys(self):
        """--open gives the base the join stack and the players the join keys, native_gate_join_in_window among them."""
        calls = []
        def fake_step(name, cmd):
            calls.append((name, [str(c) for c in cmd]))
            if name == 'players':
                out = Path(cmd[cmd.index('--out') + 1])
                (out / 'native').mkdir(parents=True)
                (out / 'native/x4lan_gate_64.dll').write_bytes(b'gate')
                (out / 'ui').mkdir()
                (out / 'ui/config.lua.append').write_text('X4LAN_CONFIG.enabled = true\n')
            return 'ok'
        with tempfile.TemporaryDirectory() as d, mock.patch.object(build_play_overlay, 'step', fake_step):
            snap = Path(d) / 'save_009.xml.gz'
            snap.write_bytes(b'save')
            build_play_overlay.main(['--snapshot', str(snap), '--gate-dll', str(snap), '--out', str(Path(d) / 'ov'), '--players', '3',
                                     '--open', '3', '--set', 'native_gate_render_motion=false'])
            base = dict(calls)['base']
            players = dict(calls)['players']
            self.assertEqual(base[base.index('--join-absent') + 1], '3')
            self.assertIn('--world-identities', base)
            self.assertEqual(base[base.index('--join') + 1], 'report')
            sets = [players[i + 1] for i, a in enumerate(players) if a == '--set']
            for key in ('native_gate_join_in_window=true', 'native_gate_world_hash_join=true', 'native_gate_join_ids=true', 'native_gate_join_precise=true',
                        'native_gate_join_lstn=true', 'native_gate_render_motion=false', 'native_gate_pilot_first_stores=true'):
                self.assertIn(key, sets)
            recipe = build_play_overlay.read_recipe(Path(d) / 'ov')
            self.assertEqual((recipe['open'], recipe['players'], recipe['set']), ([3], 3, ['native_gate_render_motion=false']))
            manifest = json.loads((Path(d) / 'ov/x4lane-manifest.json').read_text())
            self.assertIn(build_play_overlay.RECIPE, manifest)   # x4ls verifies every overlay file (hj1's first refusal)


class MixedPlatformTests(unittest.TestCase):
    def test_mixed_platform_adds_option_b_and_the_joiner_keeps_it(self):
        """--mixed-platform: fp_redirect, its solver and drones on every client, recorded for a hot joiner's rebuild."""
        calls = []
        def fake_step(name, cmd):
            calls.append((name, [str(c) for c in cmd]))
            if name == 'players':
                out = Path(cmd[cmd.index('--out') + 1])
                (out / 'native').mkdir(parents=True)
                (out / 'native/x4lan_gate_64.dll').write_bytes(b'gate')
                (out / 'ui').mkdir()
                (out / 'ui/config.lua.append').write_text('X4LAN_CONFIG.enabled = true\n')
            return 'ok'
        with tempfile.TemporaryDirectory() as d, mock.patch.object(build_play_overlay, 'step', fake_step):
            snap = Path(d) / 'save_009.xml.gz'
            snap.write_bytes(b'save')
            build_play_overlay.main(['--snapshot', str(snap), '--gate-dll', str(snap), '--out', str(Path(d) / 'ov'),
                                     '--mixed-platform', '--set', 'native_gate_fp_redirect_ticks=5'])
            players = dict(calls)['players']
            sets = [players[i + 1] for i, a in enumerate(players) if a == '--set']
            for key in ("native_gate_fp_redirect='redirect'", "native_gate_fp_redirect_solver='redirect'",
                        "native_gate_fp_redirect_drones='redirect'", 'native_gate_fp_redirect_ticks=5'):
                self.assertIn(key, sets)
            self.assertNotIn('native_gate_fp_redirect_ticks=3', sets)   # the builder's own value wins
            recipe = build_play_overlay.read_recipe(Path(d) / 'ov')
            self.assertTrue(recipe['mixed_platform'])
            self.assertIn('--mixed-platform', build_play_overlay.joiner_command(recipe, 's', 'g', 'o', 'p', 'b' * 64))


class SessionBindTests(unittest.TestCase):
    def test_session_base_binds_the_session_saves_fixture(self):
        """--session: the battle base takes the session save's checksum, fixture and watch list (ns1: without them the gate
        bound snapshot 005's fixture and stopped at its first frame); a fixture other than the registered one is refused."""
        sys.path.insert(0, str(Path(build_play_overlay.__file__).resolve().parent / 'dev'))
        import native_gate_scenarios as scenarios
        ids = [f'{0x16e8e5 + 19 * i:016x}' for i in range(12)]
        lines = ['X4LAN_CONFIG.native_gate_snapshot_sha = "' + 'a' * 64 + '"',
                 'X4LAN_CONFIG.native_gate_snapshot_fixture = {' + ','.join(repr(i) for i in ids) + '}',
                 'X4LAN_CONFIG.native_gate_world_hash_watch = {' + ','.join(repr(i) for i in ids) + '}']
        def fake_step(name, cmd):
            cmd = [str(c) for c in cmd]
            if name == 'check':
                return 'ok'
            out = Path(cmd[cmd.index('--out') + 1])
            if name == 'base':
                (out / 'ui').mkdir(parents=True)
                (out / 'ui/config.lua.append').write_text(chr(10).join([
                    'X4LAN_CONFIG.native_gate_snapshot_sha = "' + 'b' * 64 + '"',
                    "X4LAN_CONFIG.native_gate_world_hash_watch = {'0000000000176b97'}",
                    'X4LAN_CONFIG.native_gate_test = true']) + chr(10))
            if name == 'players':
                (out / 'native').mkdir(parents=True)
                (out / 'native/x4lan_gate_64.dll').write_bytes(b'gate')
                (out / 'ui').mkdir()
                (out / 'ui/config.lua.append').write_text('X4LAN_CONFIG.enabled = true' + chr(10))
            return 'ok'
        with tempfile.TemporaryDirectory() as d, mock.patch.object(build_play_overlay, 'step', fake_step), \
                mock.patch.object(scenarios, 'snapshot_lines', lambda path: (lines, {'start': 'gs_x'})):
            snap = Path(d) / 'session-save.xml.gz'
            snap.write_bytes(b'save')
            session_file = Path(d) / 'session.json'
            data = {'players': 2, 'mode': 'coop', 'base_start': 'gs_x', 'fixture': ids,
                    'snapshot': {'sha256': hashlib.sha256(b'save').hexdigest()}}
            session_file.write_text(json.dumps(data))
            args = ['--snapshot', str(snap), '--gate-dll', str(snap), '--players', '2', '--mode', 'coop', '--session', str(session_file)]
            build_play_overlay.main(args + ['--out', str(Path(d) / 'ov')])
            text = (Path(d) / 'ov.base/ui/config.lua.append').read_text()
            for line in lines:
                self.assertIn(line, text)
            self.assertNotIn('b' * 64, text)
            self.assertNotIn("{'0000000000176b97'}", text)
            self.assertIn('native_gate_test = true', text)
            self.assertTrue((Path(d) / 'ov.base/x4lane-manifest.json').is_file())
            data['fixture'] = ids[::-1]
            session_file.write_text(json.dumps(data))
            with self.assertRaises(SystemExit):
                build_play_overlay.main(args + ['--out', str(Path(d) / 'ov2')])


class RecipeTests(unittest.TestCase):
    def test_open_slots(self):
        self.assertEqual(build_play_overlay.open_slots('', 4), [])
        self.assertEqual(build_play_overlay.open_slots('4, 3,3', 4), [3, 4])
        for bad in ('1', '5', '2,3,4,x', 'a'):
            with self.assertRaises(SystemExit):
                build_play_overlay.open_slots(bad, 4)
        self.assertEqual(build_play_overlay.open_slots('2,3', 3), [2, 3])   # the host plays alone until someone joins

    def test_recipe_and_joiner_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            overlay = Path(tmp)
            self.assertIsNone(build_play_overlay.read_recipe(overlay))
            recipe = {'kind': build_play_overlay.RECIPE_KIND, 'version': 1, 'role': 'session', 'players': 3, 'mode': 'coop',
                      'minutes': 40, 'full_logs': False, 'lua_block': 'Pause,Unpause,SaveGame', 'set': ['native_gate_own_predict=false'],
                      'open': [3], 'join_mode': 'report', 'window': [40, 72040], 'gate_dll_sha': '0' * 64, 'snapshot_sha': '1' * 64}
            (overlay / build_play_overlay.RECIPE).write_text(json.dumps(recipe))
            self.assertEqual(build_play_overlay.read_recipe(overlay)['open'], [3])
            cmd = build_play_overlay.joiner_command(recipe, 'j600.xml.gz', 'gate.dll', 'out', 'j600.x4join', 'b' * 64)
            self.assertEqual(cmd[cmd.index('--open') + 1], '3')
            self.assertEqual(cmd[cmd.index('--join-package-sha') + 1], 'b' * 64)
            self.assertEqual(cmd[cmd.index('--snapshot') + 1], 'j600.xml.gz')
            self.assertEqual(cmd[cmd.index('--set') + 1], 'native_gate_own_predict=false')
            self.assertNotIn('--full-logs', cmd)
            recipe['open'] = [4]                                 # beyond the players
            (overlay / build_play_overlay.RECIPE).write_text(json.dumps(recipe))
            with self.assertRaises(SystemExit):
                build_play_overlay.read_recipe(overlay)


class RelayTests(unittest.TestCase):
    def test_open_slots_join_args(self):
        o = session.Options(clients=4, overlay=Path('ov'), snapshot=Path('s.xml.gz'), ticks=72043, tick_hz=30, lookahead=1, play=True,
                            stall_drop_ms=15000, open_slots=(4,), remote_slots=(3,), bind='192.0.2.5', allow_from=('192.0.2.7',), port=47000)
        cmd = ' '.join(session.relay_command(o, 47000, TOKEN, Path('run')))
        self.assertIn('--join --absent 4 --join-savers 1,2', cmd)    # the remote slot 3 does not save for this host
        self.assertIn('--bind 192.0.2.5 --allow-from 192.0.2.7,192.0.2.5', cmd)
        local = session.Options(clients=2, overlay=Path('ov'), snapshot=Path('s.xml.gz'), tick_hz=30, lookahead=1, play=True, open_slots=(2,))
        cmd = ' '.join(session.relay_command(local, 47000, TOKEN, Path('run')))
        self.assertIn('--join --absent 2 --join-savers 1 --join-catchup-limit 20 --join-max-pause 180 --join-observe', cmd)
        lane = session.Options(clients=2, overlay=Path('ov'), snapshot=Path('s.xml.gz'), tick_hz=30, lookahead=1, play=False, open_slots=(2,))
        self.assertNotIn('--join-observe', session.relay_command(lane, 47000, TOKEN, Path('run')))   # play only: its desyncs are logged
        own = session.Options(clients=2, overlay=Path('ov'), snapshot=Path('s.xml.gz'), tick_hz=30, lookahead=1, play=True, open_slots=(2,),
                              relay_args=['--join-max-pause', '30'])
        cmd2 = session.relay_command(own, 47000, TOKEN, Path('run'))
        self.assertEqual(cmd2.count('--join-max-pause'), 1)   # the host's own value wins
        self.assertNotIn('--bind', cmd)                              # a hot join on this PC only: loopback
        self.assertIn('--overlay <a copy of this overlay folder>', session.join_command(local, '127.0.0.1', 47000, TOKEN, 2, None))
        self.assertNotIn('--arm-at', session.join_command(local, '127.0.0.1', 47000, TOKEN, 2, None))


if __name__ == '__main__':
    unittest.main()
