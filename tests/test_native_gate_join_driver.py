"""The hot join harness of the live tests J1/J2: tools/dev/native_gate_join_driver.py (the session driver around the relay and
the journal replay), tools/dev/native_gate_join_overlays.py (the savers' and the joiner's overlays) and native_gate_lane.py
--join-session. Synthetic peers only (the simulated gate clients of tests/test_native_gate_join.py), no X4, no lane tool:
the relay and the driver run as real processes on 127.0.0.1 where a test says so."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT/'tools/dev'
sys.path.insert(0, str(DEV))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


jt = load('test_native_gate_join_sim', ROOT/'tests/test_native_gate_join.py')   # the simulated clients and Network
join = jt.join
driver = load('native_gate_join_driver', DEV/'native_gate_join_driver.py')
overlays = load('native_gate_join_overlays', DEV/'native_gate_join_overlays.py')
evidence = load('native_world_evidence', DEV/'native_world_evidence.py')
TOKEN, HZ, K = jt.TOKEN, jt.HZ, 1
SECTIONS = {join.SEC_CTX: [(0x2001, 0xaaaa, 0, 3)], join.SEC_WAKE: [(0x2001, 0xaaaa, jt.bits(27.5), 0, 3, 2, 0)],
            join.SEC_MODULES: ['ai_wait_rng=superseded', 'script_rng=unaudited', 'world_identities=serialized'],
            join.SEC_LUA: ['fixture=v2|'+','.join(f'{0x176a13+0x61*i:016x}' for i in range(12))+'|000000010000|'+'/'.join(['1.2.3']*12),
                           'identity=v1|640|1|1|00112233|-|-,-,-', 'pilot_absent=-', 'pilot_window=v1|1500,72000',
                           'world_hash=WORLD_IMPORTED_2:600:12:'+':'.join(['ab'*32]*4)]}
WINDOW = ('--set', 'native_gate_pilot_window={1500,72000}')   # the J1/J2 window after T (9.1): a join before its take-2


def write_saver_files(folder, slot, tick, clients=3, lookahead=K, tick_hz=HZ, sections=None, flags=1, fixture='111111121111'):
    """j<T>.xml.gz and j<T>.x4join as lockstep_join.h writes them for one saver; returns (package bytes, read package). The
    fixture views: the session's view of each snapshot fixture actor at T (j1-9: actor 8 a wreck, drift)."""
    folder.mkdir(parents=True, exist_ok=True)
    save = f'<savegame slot="{slot}" tick="{tick}"/>'.encode()
    (folder/f'j{tick}.xml.gz').write_bytes(save)
    gate = {name: 0 for name in join.GATE_FIELDS}
    gate.update(tick=tick, game=jt.bits(jt.game(tick)), real=jt.bits(jt.real(tick)), wire_version=3, clients=clients,
                lookahead=lookahead, tick_hz=tick_hz, slot=slot, digest='c'*64, save_us=1500000, capture_us=2500, fixture=fixture)
    data = join.write_package(gate, sections or SECTIONS, save_sha=hashlib.sha256(save).digest(), save_bytes=len(save), flags=flags)
    (folder/f'j{tick}.x4join').write_bytes(data)
    return data, join.read_package(data)


def relay_info(**changes):
    info = driver.relay_options(['--port', '40001', '--token', TOKEN, '--clients', '3', '--lookahead', str(K), '--tick-hz', str(HZ),
                                 '--join', '--absent', '3', '--join-savers', '1,2'])
    info.update(changes)
    return info


class Options(unittest.TestCase):
    def test_relay_options_and_refusals(self):
        info = relay_info()
        self.assertEqual((info['port'], info['clients'], info['absent'], info['present'], info['savers'], info['lookahead']),
                         (40001, 3, [3], [1, 2], [1, 2], K))
        # Unknown relay options pass (the relay checks them); --join-lead is not --join.
        self.assertTrue(driver.relay_options(['--port', '1', '--token', TOKEN, '--join', '--absent', '2', '--join-lead', '5'])['absent'])
        for argv, why in ((['--token', TOKEN, '--join', '--absent', '3'], '--port'),
                          (['--port', '1', '--token', 'x', '--join', '--absent', '3'], '--token'),
                          (['--port', '1', '--token', TOKEN, '--join-lead', '5', '--absent', '3'], '--join'),
                          (['--port', '1', '--token', TOKEN, '--join', '--clients', '3'], '--absent or --rejoin'),
                          (['--port', '1', '--token', TOKEN, '--join', '--absent', '3', '--bind', '10.0.0.2'], 'loopback')):
            with self.subTest(why=why), self.assertRaisesRegex(ValueError, why):
                driver.relay_options(argv)

    def test_rejoin_options(self):
        # Roadmap 15: no absent slot, slot 3 rejoinable; the savers default to the other present slots.
        info = driver.relay_options(['--port', '1', '--token', TOKEN, '--clients', '3', '--join', '--rejoin', '3', '--stall-drop-ms', '3000'])
        self.assertEqual((info['absent'], info['rejoin'], info['present'], info['savers']), ([], [3], [1, 2, 3], [1, 2]))
        d = driver.Driver(info, 3, [400], {1: 'b1', 2: 'b2'}, 'x')
        self.assertIn('JOIN_DRIVER READY slot=3 join_at=400', d.ready_line())
        with self.assertRaisesRegex(ValueError, 'nor a rejoinable one'):
            driver.Driver(info, 2, [400], {1: 'b1', 2: 'b2'}, 'x')
        with self.assertRaisesRegex(ValueError, 'one of the savers'):
            driver.Driver(dict(info, rejoin=[2, 3]), 2, [400], {1: 'b1', 2: 'b2'}, 'x')
        # --join-observe (diagnostic): its PASS line says so.
        seen = driver.relay_options(['--port', '1', '--token', TOKEN, '--clients', '3', '--join', '--rejoin', '3', '--join-observe'])
        self.assertTrue(seen['observe'] and not info['observe'])
        d = driver.Driver(seen, 3, [400], {1: 'b1', 2: 'b2'}, 'x', expect='joined')
        j = d.joins[0]
        j.state, j.save_tick, j.files = 'joined', 433, {'verified': 1}
        self.assertTrue(d.check_pass(j)[0].endswith('expect=joined diagnostic=join_observe'))

    def test_saver_lanes(self):
        self.assertEqual(driver.saver_lanes('b3,b4', [1, 2]), {1: 'b3', 2: 'b4'})
        self.assertEqual(driver.saver_lanes('2=b4,1=b3', [1, 2]), {1: 'b3', 2: 'b4'})
        for text in ('b3', 'b3,b9', 'b3,b3', '1=b3,b4', '3=b5'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                driver.saver_lanes(text, [1, 2])

    def test_driver_refusals(self):
        lanes = {1: 'b3', 2: 'b4'}
        for kwargs, why in ((dict(slot=2), 'not an absent slot'), (dict(join_at=[]), 'increasing'), (dict(join_at=[50, 20]), 'increasing'),
                            (dict(lanes={1: 'b3'}), 'no lane for saver'), (dict(expect='handover'), 'expect')):
            args = dict(relay=relay_info(), slot=3, join_at=[20], lanes=lanes, copy_to='x')
            args.update(kwargs)
            with self.subTest(why=why), self.assertRaisesRegex(ValueError, why):
                driver.Driver(**args)

    def test_lane_profiles_match_the_released_lane_tool(self):
        lanecore = ROOT.parents[0]/'lanes/release/dev/lane/lanecore.py'
        if not lanecore.is_file():
            self.skipTest('no released lane tool on this machine')
        text = lanecore.read_text(encoding='utf-8')
        for lane, profile in driver.LANE_PROFILES.items():
            self.assertIn(f"'{lane}': '{profile}'", text)


class DriverStates(unittest.TestCase):
    """The driver without processes: relay lines and NGJOINSTATE answers in, JOIN_DRIVER lines and NGJOIN datagrams out."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sent, self.now = [], 100.0
        self.d = driver.Driver(relay_info(), 3, [20], {1: 'b3', 2: 'b4'}, self.root/'join', expect='saved',
                               profiles=self.root/'profiles', poll=1.0, file_wait=5.0, send=self.sent.append)

    def folder(self, slot):
        return self.root/'profiles'/driver.LANE_PROFILES['b3' if slot == 1 else 'b4']/'save'/'mpjoin'

    def lines(self, *texts):
        out = []
        for text in texts:
            out += self.d.line(text, self.now)
        return out

    def answer(self, state, tick=0, saver=0, size=0, package='-', detail='-'):
        j = self.d.joins[0]
        return self.d.answer(join.message('NGJOINSTATE', TOKEN, 3, j.nonce, state, tick, saver, size, package, detail), self.now)

    def saved_notice(self, slot, tick, data, package):
        return (f'NATIVE_GATE JOIN slot=3 saved slot={slot} tick={tick} bytes={len(data)} package={hashlib.sha256(data).hexdigest()} '
                f'portable={package["sections"]["gate"]["portable"]} at={tick+1}')

    def advance(self, tick):
        self.now += 0.1
        return self.lines(f'NATIVE_GATE tick={tick} failure=none done=False')+self.d.step(self.now)

    def test_request_poll_files_handoff_and_pass(self):
        self.assertEqual(self.advance(19)[-1], 'NATIVE_GATE tick=19 failure=none done=False')
        self.assertEqual(self.sent, [])
        out = self.advance(20)
        self.assertTrue(any(line.startswith('JOIN_DRIVER REQUEST slot=3 ') for line in out), out)
        nonce = self.d.joins[0].nonce
        self.assertEqual(self.sent, [f'NGJOIN|1|{TOKEN}|3|{nonce}'.encode()])
        self.now += 0.5
        self.d.step(self.now)
        self.assertEqual(len(self.sent), 1)                              # polls once a second
        out = self.answer('queued', 56)
        self.assertIn('state=queued T=56', out[0])
        self.now += 1.0
        self.d.step(self.now)
        self.assertEqual(len(self.sent), 2)
        data1, p1 = write_saver_files(self.folder(1), 1, 56)
        data2, p2 = write_saver_files(self.folder(2), 2, 56)
        self.lines(self.saved_notice(1, 56, data1, p1), self.saved_notice(2, 56, data2, p2))
        out = self.answer('saved', 56, 1, len(data1), hashlib.sha256(data1).hexdigest())
        kinds = [line.split()[1] for line in out]
        self.assertEqual(kinds, ['STATE', 'FILES', 'COMPARE', 'HANDOFF', 'PASS'], out)
        files = out[1]
        self.assertIn('verified=1', files)
        self.assertIn('diagnostic=1', files)
        self.assertIn('modules=serialized:1,superseded:1,unaudited:1', files)
        self.assertIn('equal=1', out[2])
        handoff = driver.load_handoff(self.root/'join/handoff.json', verify=True)
        self.assertEqual((handoff['port'], handoff['token'], handoff['slot'], handoff['clients'], handoff['save_tick'], handoff['saver']),
                         (40001, TOKEN, 3, 3, 56, 1))
        self.assertEqual(Path(handoff['save']).read_bytes(), (self.folder(1)/'j56.xml.gz').read_bytes())
        self.assertEqual(Path(handoff['packages']['2']).read_bytes(), data2)
        self.assertEqual(json.loads((self.root/'join/j56/handoff.json').read_text()), handoff)
        self.assertEqual(driver.joiner_tokens(handoff)['@X4LANE_SLOT@'], 'p3')
        # Later states only print; the session's end after a pass is no failure; the save pause is measured.
        out = []
        for tick in range(21, 57):
            out += self.advance(tick)
        self.now += 2.0                                                  # a 2 s gap at tick 57: the save
        out += self.lines('NATIVE_GATE tick=57 failure=none done=False')
        for tick in range(58, 100):
            out += self.advance(tick)
        pause = [line for line in out if line.startswith('JOIN_DRIVER PAUSE')]
        self.assertEqual(len(pause), 1)
        self.assertIn('kind=save tick=56 pause_s=2.', pause[0])
        self.assertIn('at_tick=57', pause[0])
        out = self.answer('failed', 56, 1, len(data1), hashlib.sha256(data1).hexdigest(), 'joiner_lost:never_reported')
        self.assertEqual([line.split()[1] for line in out], ['STATE'])
        self.assertEqual(self.lines('NATIVE_GATE tick=99 failure=none done=True'), ['NATIVE_GATE tick=99 failure=none done=True'])

    def test_a_refused_join_fails_loudly(self):
        self.advance(20)
        out = self.answer('failed', 56, 0, 0, '-', 'saver_disagree')
        self.assertEqual(out[-1].split()[1], 'FAIL')
        self.assertIn('reason=join_failed:saver_disagree', out[-1])
        self.assertTrue(self.d.joins[0].ended)

    def test_the_session_ending_first_fails_loudly_before_its_done_line(self):
        out = self.advance(12)
        out = self.lines('NATIVE_GATE tick=15 failure=none done=True')
        self.assertEqual([line.split()[0] for line in out], ['JOIN_DRIVER', 'NATIVE_GATE'])
        self.assertIn('reason=session_done:state_not_requested', out[0])
        self.assertEqual(self.d.step(self.now+5), [])                     # no request once the session is over
        self.assertEqual(self.sent, [])

    def test_bad_files_are_retried_then_refused(self):
        self.advance(20)
        data1, p1 = write_saver_files(self.folder(1), 1, 56)
        data2, p2 = write_saver_files(self.folder(2), 2, 56)
        self.lines(self.saved_notice(1, 56, data1, p1), self.saved_notice(2, 56, data2, p2))
        (self.folder(2)/'j56.xml.gz').write_bytes(b'another save')       # not the save slot 2's package names
        out = self.answer('saved', 56, 1, len(data1), hashlib.sha256(data1).hexdigest())
        self.assertEqual([line.split()[1] for line in out], ['STATE'])   # retried until --file-wait
        self.now += 6
        out = self.d.step(self.now)
        self.assertEqual(out[0].split()[1], 'FAIL', out)
        self.assertIn('reason=files:save_sha_slot_2', out[0])
        self.assertFalse((self.root/'join/handoff.json').exists())

    def test_wire_shape_portable_and_announcement_checks(self):
        cases = (('wire_shape_slot_1', dict(clients=4)), ('wire_shape_slot_1', dict(lookahead=2)),
                 ('savers_portable_content_differs', dict(sections={**SECTIONS, join.SEC_CTX: [(0x2002, 0xaaaa, 0, 3)]})))
        for why, change in cases:
            with self.subTest(why=why):
                self.setUp()
                self.advance(20)
                data1, p1 = write_saver_files(self.folder(1), 1, 56, **change)
                data2, p2 = write_saver_files(self.folder(2), 2, 56)
                self.lines(self.saved_notice(1, 56, data1, p1), self.saved_notice(2, 56, data2, p2))
                self.answer('saved', 56, 1, len(data1), hashlib.sha256(data1).hexdigest())
                self.now += 6
                out = self.d.step(self.now)
                self.assertTrue(out and why in out[0], out)
        self.setUp()
        self.advance(20)
        data1, p1 = write_saver_files(self.folder(1), 1, 56)
        write_saver_files(self.folder(2), 2, 56)
        self.lines(self.saved_notice(1, 56, data1, p1))                  # slot 2's SAVED notice never came
        self.answer('saved', 56, 1, len(data1), hashlib.sha256(data1).hexdigest())
        self.now += 6
        self.assertIn('no_saved_notice_slots_2', self.d.step(self.now)[0])

    def test_expect_joined_waits_for_the_handover(self):
        self.d.expect = 'joined'
        self.advance(20)
        data1, p1 = write_saver_files(self.folder(1), 1, 56)
        data2, p2 = write_saver_files(self.folder(2), 2, 56)
        self.lines(self.saved_notice(1, 56, data1, p1), self.saved_notice(2, 56, data2, p2))
        package = hashlib.sha256(data1).hexdigest()
        out = self.answer('saved', 56, 1, len(data1), package)
        self.assertNotIn('PASS', ' '.join(out))
        self.answer('catching_up', 56, 1, len(data1), package)
        out = self.answer('handover', 56, 1, len(data1), package, '400')
        self.assertNotIn('PASS', ' '.join(out))
        out = self.answer('joined', 56, 1, len(data1), package, '400')
        self.assertEqual(out[-1], 'JOIN_DRIVER PASS slot=3 T=56 state=joined expect=joined')
        self.d.step(self.now)
        self.assertEqual(len(self.sent), 1)                              # joined: no more polls


class Replay(unittest.TestCase):
    """J1's journal replay against a journal of the session relay (the Network of test_native_gate_join)."""
    def session(self, k=K, rounds=140):
        net = jt.Network(k=k, ticks=400)
        for _ in range(30):
            net.round()
        answer = net.request()
        while net.relay.join.state == 'queued':
            net.round()
        package = net.packages[net.relay.join.saved[1]['package']]
        for _ in range(rounds):
            net.round()
        journal = {t: {s: dict(row) for s, row in rows.items()} for t, rows in net.relay.states.items() if t <= net.relay.released}
        return net, int(answer[6]), package, json.loads(json.dumps({str(t): {str(s): r for s, r in rows.items()} for t, rows in journal.items()}))

    def replay(self, journal, save_tick, k=K, observe=False, last=None):
        rows = {int(t): {int(s): r for s, r in v.items()} for t, v in journal.items()}
        return driver.JournalReplay(TOKEN, rows, 3, save_tick, 3, k, HZ, last=last, observe=observe)

    def drive(self, replay, joiner, steps=2000):
        address = ('127.0.0.1', 30003)
        for _ in range(steps):
            joiner.report()
            for tick in range(max(joiner.tick-replay.lookahead, 0), joiner.tick+1):
                if tick in joiner.reports:
                    for _, packet in replay.receive(joiner.reports[tick], address, time.monotonic()):
                        joiner.receive(packet)
            if replay.state in ('done', 'failed') or not joiner.step():
                if replay.state in ('done', 'failed'):
                    break

    def test_an_equal_joiner_is_replayed_to_the_journal_end(self):
        for k in (0, 1, 2):
            with self.subTest(k=k):
                net, save_tick, package, journal = self.session(k=k)
                replay = self.replay(journal, save_tick, k=k)
                joiner = jt.Client(3, 3, k, None, tick=package['tick'], state=package['state'], joined_from=package['tick'])
                self.drive(replay, joiner)
                s = replay.summary()
                self.assertEqual((s['verdict'], s['joiner_tick'], s['mismatches']), ('equal', replay.last, 0), replay.notices)
                self.assertEqual(joiner.stopped, replay.last)
                theirs = {t: b for t, b in net.live[1].applied if save_tick < t <= replay.last}
                self.assertEqual(dict(joiner.applied), theirs)               # every permit the session itself got
                self.assertIn('NATIVE_GATE JOIN_REPLAY FIRST tick=%d slot=3' % save_tick, replay.notices)

    def test_a_diverged_joiner_is_recorded_or_stopped(self):
        net, save_tick, package, journal = self.session()
        replay = self.replay(journal, save_tick, observe=True)
        joiner = jt.Client(3, 3, K, None, tick=save_tick, state=b'not the saved world', joined_from=save_tick)
        self.drive(replay, joiner)
        s = replay.summary()
        self.assertEqual((s['verdict'], s['first_mismatch'], s['joiner_tick']), ('diverged', save_tick+1, replay.last))
        self.assertEqual(s['mismatches'], replay.last-save_tick)
        self.assertEqual(sum(n.startswith('NATIVE_GATE JOIN_REPLAY DESYNC') for n in replay.notices), 1)
        replay = self.replay(journal, save_tick)
        joiner = jt.Client(3, 3, K, None, tick=save_tick, state=b'not the saved world', joined_from=save_tick)
        self.drive(replay, joiner)
        self.assertEqual((replay.state, replay.failure), ('failed', f'mismatch:digest_tick_{save_tick+1}'))
        self.assertEqual(joiner.stopped, save_tick+1)

    def test_first_report_protocol_and_journal_checks(self):
        net, save_tick, package, journal = self.session()
        replay = self.replay(journal, save_tick)
        early = jt.Client(3, 3, K, None, tick=save_tick-1, state=package['state'], joined_from=save_tick-1)
        self.assertEqual(replay.receive(early.report(), ('127.0.0.1', 1), 0.0), [])   # not T: ignored
        self.assertEqual(replay.state, 'waiting')
        wrong = jt.Client(3, 3, 0, None, tick=save_tick, state=package['state'], joined_from=save_tick)   # protocol 3
        out = replay.receive(wrong.report(), ('127.0.0.1', 1), 0.0)
        self.assertTrue(out[0][1].startswith(b'NGSTOP|'))
        self.assertEqual(replay.failure, 'mismatch:protocol_3')
        with self.assertRaisesRegex(ValueError, 'not after the save tick'):
            self.replay(journal, save_tick, last=save_tick)
        broken = {t: v for t, v in journal.items() if int(t) != save_tick+5}
        with self.assertRaisesRegex(ValueError, f'tick {save_tick+5}'):
            self.replay(broken, save_tick)


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


class UdpClient:
    """A simulated gate client (test_native_gate_join.Client) on its own loopback socket."""
    def __init__(self, client, port):
        self.c, self.port = client, port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.setblocking(False)

    def exchange(self):
        self.c.report()
        for tick in range(max(self.c.tick-self.c.k, 0), self.c.tick+1):
            if tick in self.c.reports:
                self.sock.sendto(self.c.reports[tick], ('127.0.0.1', self.port))
        while True:
            try:
                data, _ = self.sock.recvfrom(65536)
            except (BlockingIOError, ConnectionResetError):
                break
            self.c.receive(data)
        return self.c.step()


class Saver(jt.Client):
    """A saver that writes the join files into its lane profile, as lockstep_join.h does, and announces them."""
    def __init__(self, slot, folder, packages):
        super().__init__(slot, 3, K, None, saver_packages=packages)
        self.folder = folder

    def step(self):
        before = self.saved
        ok = super().step()
        if self.saved and not before:
            data, package = write_saver_files(self.folder, self.slot, self.saved['tick'])
            self.saved['package'] = hashlib.sha256(data).digest()
            self.saved['portable'] = bytes.fromhex(package['sections']['gate']['portable'])
            self.packages[hashlib.sha256(data).hexdigest()] = {'tick': self.tick, 'state': self.state}
        return ok


class Processes(unittest.TestCase):
    """End to end on 127.0.0.1: the driver starts the real relay; two simulated savers play; the driver asks for the join,
    verifies and copies the files; then the journal replay serves a simulated joiner from the session's journal."""
    def collect(self, proc, sink):
        for line in proc.stdout:
            sink.append(line.rstrip('\r\n'))

    def wait_for(self, lines, text, seconds=20):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            if any(text in line for line in lines):
                return True
            time.sleep(0.02)
        self.fail(f'{text!r} not seen: {lines[-15:]}')

    def test_session_driver_then_journal_replay(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            port, packages = free_port(), {}
            profiles = root/'profiles'
            argv = [sys.executable, str(DEV/'native_gate_join_driver.py'), 'session', '--slot', '3', '--join-at', '15',
                    '--saver-lanes', 'b3,b4', '--copy-to', str(root/'join'), '--profiles-root', str(profiles), '--poll', '0.1',
                    '--exit-after-done', '0.5', '--', str(DEV/'native_gate_relay.py'), '--port', str(port), '--token', TOKEN,
                    '--ticks', '240', '--hold-seconds', '0', '--report', str(root/'barrier.json'), '--relative-real', '--observe',
                    '--clients', '3', '--input-journal', str(root/'inputs.jsonl'), '--lookahead', str(K), '--tick-hz', str(HZ),
                    '--join', '--absent', '3', '--join-savers', '1,2', '--join-lead', '5']
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT)
            self.addCleanup(lambda: proc.poll() is None and proc.kill())
            lines = []
            threading.Thread(target=self.collect, args=(proc, lines), daemon=True).start()
            self.wait_for(lines, 'NATIVE_GATE RELAY READY')
            folders = {s: profiles/driver.LANE_PROFILES[lane]/'save'/'mpjoin' for s, lane in ((1, 'b3'), (2, 'b4'))}
            clients = [UdpClient(Saver(s, folders[s], packages), port) for s in (1, 2)]
            deadline = time.monotonic()+60
            while time.monotonic() < deadline and not all(c.c.stopped is not None for c in clients):
                for c in clients:
                    c.exchange()
                time.sleep(0.005)                                     # about 100 ticks a second
            self.assertTrue(all(c.c.stopped is not None for c in clients), lines[-10:])
            self.assertEqual(proc.wait(timeout=30), 0, lines[-10:])
            for c in clients:
                c.sock.close()
            text = '\n'.join(lines)
            for needle in ('NATIVE_GATE RELAY JOIN absent=3', 'JOIN_DRIVER READY slot=3 join_at=15 expect=saved savers=1:b3,2:b4',
                           'JOIN_DRIVER REQUEST slot=3', 'state=queued', 'state=saved', 'verified=1', 'equal=1', 'JOIN_DRIVER HANDOFF',
                           'JOIN_DRIVER PASS slot=3', 'JOIN_DRIVER PAUSE slot=3 kind=save', 'done=True', 'JOIN_DRIVER EXIT relay_code=0'):
                self.assertIn(needle, text)
            self.assertNotIn('JOIN_DRIVER FAIL', text)
            self.assertNotIn('Traceback', text)
            handoff = driver.load_handoff(root/'join/handoff.json', verify=True)
            save_tick = handoff['save_tick']
            self.assertEqual(Path(handoff['save']).read_bytes(), (folders[1]/f'j{save_tick}.xml.gz').read_bytes())
            # J1 without the session: the journal replay serves a joiner loaded from slot 1's package.
            replay_argv = [sys.executable, str(DEV/'native_gate_join_driver.py'), 'replay', '--journal', str(root/'inputs.jsonl'),
                           '--handoff', str(root/'join/handoff.json'), '--handoff-out', str(root/'replay-handoff.json'),
                           '--report', str(root/'replay.json'), '--token', TOKEN, '--observe', '--idle-timeout', '20']
            replay = subprocess.Popen(replay_argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT)
            self.addCleanup(lambda: replay.poll() is None and replay.kill())
            out = []
            threading.Thread(target=self.collect, args=(replay, out), daemon=True).start()
            self.wait_for(out, 'JOIN_REPLAY READY')
            mine = driver.load_handoff(root/'replay-handoff.json')
            self.assertEqual((mine['source'], mine['save'], mine['slot']), ('replay', handoff['save'], 3))
            state = packages[handoff['package_sha']]
            joiner = UdpClient(jt.Client(3, 3, K, None, tick=state['tick'], state=state['state'], joined_from=state['tick']), mine['port'])
            deadline = time.monotonic()+60
            while time.monotonic() < deadline and joiner.c.stopped is None:
                joiner.exchange()
                time.sleep(0.001)
            self.assertEqual(replay.wait(timeout=30), 0, out)
            joiner.sock.close()
            summary = json.loads((root/'replay.json').read_text())
            self.assertEqual((summary['verdict'], summary['mismatches'], summary['joiner_tick']), ('equal', 0, summary['last']), out)
            self.assertIn('NATIVE_GATE JOIN_REPLAY DONE verdict=equal', '\n'.join(out))


class Overlays(unittest.TestCase):
    """native_gate_join_overlays.py: the savers' session overlay (N slots, two lanes) and the joiner's from a handoff."""
    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(overlays.main(list(map(str, argv))), 0)
        return out.getvalue()

    def refused(self, *argv):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()) as err, contextlib.redirect_stdout(io.StringIO()):
            overlays.main(list(map(str, argv)))
        return err.getvalue()

    def test_savers_and_joiner_overlays(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            dll, snap = root/'gate.dll', root/'save_009.xml.gz'
            dll.write_bytes(b'gate dll')
            snap.write_bytes(b'owned snapshot')
            notes = self.run_main('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'savers', '--set', 'native_gate_world_hash_stride=300',
                                  *WINDOW)
            self.assertIn('relay: --clients 3 --join --absent 3 --join-savers 1,2', notes)
            self.assertIn('lane session: --clients 2 ', notes)
            # A rejoin session (roadmap 15): every slot on a lane, slot 3 rejoinable, the savers the others.
            again = self.run_main('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'rejoin', '--absent', '', '--rejoin', '3', *WINDOW)
            self.assertIn('relay: --clients 3 --join --rejoin 3 --stall-drop-ms MS --join-savers 1,2', again)
            self.assertIn('lane session: --clients 3 ', again)
            self.assertIn("X4LAN_CONFIG.native_gate_join = {save = 'report', rejoin = {3}}",
                          (root/'rejoin/ui/config.lua.append').read_text().splitlines())
            self.assertIn('--rejoin needs slots', self.refused('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'rejoin2',
                                                              '--rejoin', '3'))
            text = (root/'savers/ui/config.lua.append').read_text()
            self.assertNotIn('@X4LANE_CLIENTS@', text)
            for line in ('X4LAN_CONFIG.native_gate_clients = 3', "X4LAN_CONFIG.native_gate_join = {save = 'report', absent = {3}}",
                         'X4LAN_CONFIG.native_gate_arm_timeout_ms = 120000', 'X4LAN_CONFIG.native_gate_world_hash_stride = 300',
                         'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"', 'X4LAN_CONFIG.token = "@X4LANE_TOKEN@"',
                         f'X4LAN_CONFIG.native_gate_snapshot_sha = "{hashlib.sha256(b"owned snapshot").hexdigest()}"'):
                self.assertIn(line, text.splitlines())
            plan = json.loads((root/'savers/player-identity-plan.json').read_text())
            self.assertEqual(len(plan['slots']), 3)
            files = (root/'savers/ui.xml.files').read_text().split()
            self.assertEqual((files[0], files[-1]), ('ui/native_gate_defaults.lua', 'ui/native_gate_probe.lua'))
            self.assertIn('ui/native_gate_join_core.lua', files)
            manifest = json.loads((root/'savers/x4lane-manifest.json').read_text())
            self.assertEqual(manifest['ui/config.lua.append'], hashlib.sha256((root/'savers/ui/config.lua.append').read_bytes()).hexdigest())
            record = json.loads((root/'savers/x4lane-join.json').read_text())
            self.assertEqual((record['role'], record['clients'], record['lanes'], record['absent']), ('savers', 3, 2, [3]))
            # The joiner from a session handoff: same slots, plan and keys; the join save as the snapshot. The package's IDS
            # section holds a saved identity for every saved ID the config names (the session defaults' snapshot fixture, the
            # identity plan's ships and sectors, the pilot slots), at other runtime IDs (X4 renumbers what it saves).
            named = overlays.saved_id_keys(text)
            self.assertEqual(len(named['native_gate_snapshot_fixture']), 12)
            self.assertIn('native_gate_player_identity_plan', named)
            needed = sorted({i for ids in named.values() for i in ids})
            ids = [(0x6c000+k, int(i, 16), 0, 0) for k, i in enumerate(needed)]
            folder = root/'mpjoin'
            data, package = write_saver_files(folder, 1, 640, flags=1, sections={**SECTIONS, join.SEC_IDS: ids})
            handoff = {'kind': driver.HANDOFF_KIND, 'version': 1, 'source': 'session', 'port': 40001, 'token': TOKEN, 'slot': 3,
                       'clients': 3, 'lookahead': K, 'tick_hz': HZ, 'save_tick': 640, 'saver': 1, 'save': str(folder/'j640.xml.gz'),
                       'save_sha': package['header']['save_sha'], 'package': str(folder/'j640.x4join'),
                       'package_sha': hashlib.sha256(data).hexdigest(), 'portable': package['sections']['gate']['portable']}
            (root/'handoff.json').write_text(json.dumps(handoff))
            self.assertIn('diagnostic package', self.refused('joiner', '--gate-dll', dll, '--handoff', root/'handoff.json',
                                                             '--out', root/'strict'))
            notes = self.run_main('joiner', '--gate-dll', dll, '--handoff', root/'handoff.json', '--out', root/'joiner', '--join-mode', 'report',
                                  '--set', 'native_gate_world_hash_stride=300', *WINDOW)
            self.assertIn('JOIN_OVERLAY role=joiner', notes)
            self.assertIn(f'JOIN_OVERLAY saved_ids={sum(len(v) for v in named.values())} ', notes)
            self.assertIn(' fixture_views=nav,nav,nav,nav,nav,nav,nav,drift,nav,nav,nav,nav', notes)
            self.assertEqual(json.loads((root/'joiner/x4lane-join.json').read_text())['saved_ids'], {k: len(v) for k, v in named.items()})
            self.assertIn(f'--join-session {root/"handoff.json"}', notes)
            joined = (root/'joiner/ui/config.lua.append').read_text()
            sha = handoff['package_sha']
            self.assertIn(f"X4LAN_CONFIG.native_gate_join = {{save = 'strict', joiner = {{package = './extensions/x4_lan_lab/native/join.x4join', "
                          f"sha = '{sha}', mode = 'report'}}, absent = {{3}}}}", joined.splitlines())
            self.assertEqual(json.loads((root/'joiner/x4lane-join.json').read_text())['absent'], [3])
            self.assertIn(f'X4LAN_CONFIG.native_gate_snapshot_sha = "{handoff["save_sha"]}"', joined.splitlines())
            self.assertIn('X4LAN_CONFIG.native_gate_clients = 3', joined.splitlines())
            self.assertEqual((root/'joiner/native/join.x4join').read_bytes(), data)
            self.assertEqual(json.loads((root/'joiner/player-identity-plan.json').read_text()), plan)
            same = [line for line in text.splitlines() if 'native_gate_join' not in line and 'snapshot_sha' not in line]
            self.assertEqual(same, [line for line in joined.splitlines() if 'native_gate_join' not in line and 'snapshot_sha' not in line])
            # A handoff names the session's absent slots (the relay's --absent); the joining slot must be one of them.
            (root/'handoff2.json').write_text(json.dumps(dict(handoff, absent=[2, 3])))
            self.run_main('joiner', '--gate-dll', dll, '--handoff', root/'handoff2.json', '--out', root/'joiner2', '--join-mode', 'report', *WINDOW)
            self.assertIn("absent = {2, 3}}", (root/'joiner2/ui/config.lua.append').read_text())
            (root/'handoff3.json').write_text(json.dumps(dict(handoff, absent=[2])))
            self.assertIn('absent slots', self.refused('joiner', '--gate-dll', dll, '--handoff', root/'handoff3.json', '--out', root/'joiner3',
                                                       '--join-mode', 'report'))
            # A rejoin (roadmap 15): no absent slot, the joining slot is a rejoinable one; both lists reach the joiner's config.
            (root/'handoff5.json').write_text(json.dumps(dict(handoff, absent=[], rejoin=[3])))
            self.run_main('joiner', '--gate-dll', dll, '--handoff', root/'handoff5.json', '--out', root/'joiner5', '--join-mode', 'report', *WINDOW)
            self.assertIn("mode = 'report'}, rejoin = {3}}", (root/'joiner5/ui/config.lua.append').read_text())
            self.assertEqual(json.loads((root/'joiner5/x4lane-join.json').read_text())['rejoin'], [3])
            (root/'handoff6.json').write_text(json.dumps(dict(handoff, absent=[3], rejoin=[3])))
            self.assertIn('absent or rejoin slots', self.refused('joiner', '--gate-dll', dll, '--handoff', root/'handoff6.json',
                                                                 '--out', root/'joiner6', '--join-mode', 'report'))
            # A saved ID of the config the package has no saved identity for (the session had no such object at T, or only a
            # created one): refused naming key and ID, the overlay renamed .refused.
            fixture = named['native_gate_snapshot_fixture'][0]
            short = [r for r in ids if r[1] != int(fixture, 16)]+[(0x7000, int(fixture, 16), 1, 0)]
            folder4 = root/'mpjoin4'
            data4, package4 = write_saver_files(folder4, 1, 640, flags=1, sections={**SECTIONS, join.SEC_IDS: short})
            (root/'handoff4.json').write_text(json.dumps(dict(handoff, save=str(folder4/'j640.xml.gz'), package=str(folder4/'j640.x4join'),
                                                              package_sha=hashlib.sha256(data4).hexdigest())))
            why = self.refused('joiner', '--gate-dll', dll, '--handoff', root/'handoff4.json', '--out', root/'joiner4', '--join-mode', 'report')
            self.assertIn(f'native_gate_snapshot_fixture: {fixture}', why)
            self.assertTrue((root/'joiner4.refused/ui/config.lua.append').is_file())
            self.assertFalse((root/'joiner4').exists())
            # The session's fixture views at T: missing (a saver build before them) or one the joiner cannot bind: refused.
            for n, (views, reason) in enumerate(((join.NO_FIXTURE, 'fixture_view_missing'), ('111111141111', 'fixture_view:8:other'))):
                folder5 = root/f'mpjoin5{n}'
                data5, _ = write_saver_files(folder5, 1, 640, flags=1, sections={**SECTIONS, join.SEC_IDS: ids}, fixture=views)
                (root/f'handoff5{n}.json').write_text(json.dumps(dict(handoff, save=str(folder5/'j640.xml.gz'),
                                                                     package=str(folder5/'j640.x4join'),
                                                                     package_sha=hashlib.sha256(data5).hexdigest())))
                why = self.refused('joiner', '--gate-dll', dll, '--handoff', root/f'handoff5{n}.json', '--out', root/f'joiner5{n}',
                                   '--join-mode', 'report')
                self.assertIn(reason, why)
                self.assertTrue((root/f'joiner5{n}.refused').is_dir())
            # Refusals: an existing output, absent slots that are not the last ones, a changed package, a missing handoff.
            self.assertIn('exists', self.refused('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'savers'))
            self.assertIn('last slots', self.refused('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'x', '--absent', '2'))
            self.assertIn('arm-timeout', self.refused('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'y', '--arm-timeout-ms', '200000'))
            (folder/'j640.x4join').write_bytes(data[:-1]+b'\0')
            self.assertIn('package', self.refused('joiner', '--gate-dll', dll, '--handoff', root/'handoff.json', '--out', root/'z',
                                                  '--join-mode', 'report'))
            self.assertIn('no handoff', self.refused('joiner', '--gate-dll', dll, '--handoff', root/'none.json', '--wait', '0.1',
                                                     '--out', root/'w'))


    def test_join_census_and_the_world_table(self):
        # --join-census (1.6): native_gate_world_hash_join on the savers and the joiner, only with the census stride; the joiner
        # prints the package's world side table and a strict joiner refuses a version 1 package (no world table).
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            dll, snap = root/'gate.dll', root/'save_009.xml.gz'
            dll.write_bytes(b'gate dll')
            snap.write_bytes(b'owned snapshot')
            self.assertIn('--join-census needs --set native_gate_world_hash_stride', self.refused(
                'savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'bare', '--join-census', *WINDOW))
            notes = self.run_main('savers', '--gate-dll', dll, '--snapshot', snap, '--out', root/'savers', '--set',
                                  'native_gate_world_hash_stride=300', '--join-census', *WINDOW)
            self.assertIn(' census_t1=1 ', notes)
            self.assertIn('X4LAN_CONFIG.native_gate_world_hash_join = true', (root/'savers/ui/config.lua.append').read_text().splitlines())
            # the join save at full precision: on by default for savers and joiners, a --set keeps its own value
            self.assertIn('X4LAN_CONFIG.native_gate_join_precise = true', (root/'savers/ui/config.lua.append').read_text().splitlines())
            self.assertEqual(overlays.precise_sets(['a=1']), ['a=1', 'native_gate_join_precise=true'])
            self.assertEqual(overlays.precise_sets(['native_gate_join_precise = false']), ['native_gate_join_precise = false'])
            self.assertEqual(overlays.refresh_sets(['a=1']), ['a=1', 'native_gate_join_refresh_builds=true'])
            self.assertEqual(overlays.refresh_sets(['native_gate_join_refresh_builds = false']), ['native_gate_join_refresh_builds = false'])
            self.assertEqual(overlays.ids_sets(['a=1']), ['a=1', 'native_gate_join_ids=true'])
            self.assertEqual(overlays.ids_sets(['native_gate_join_ids = false']), ['native_gate_join_ids = false'])
            text = (root/'savers/ui/config.lua.append').read_text()
            named = overlays.saved_id_keys(text)
            ids = [(0x6c000+k, int(i, 16), 0, 0) for k, i in enumerate(sorted({i for v in named.values() for i in v}))]
            health = [(0x2003, jt.bits(2500.0), 0, jt.bits(7.083), 0, join.HEALTH_SHIELD, 0)]
            folder = root/'mpjoin'
            data, package = write_saver_files(folder, 1, 640, flags=1, sections={**SECTIONS, join.SEC_IDS: ids, join.SEC_HEALTH: health})
            handoff = {'kind': driver.HANDOFF_KIND, 'version': 1, 'source': 'session', 'port': 40001, 'token': TOKEN, 'slot': 3,
                       'clients': 3, 'lookahead': K, 'tick_hz': HZ, 'save_tick': 640, 'saver': 1, 'save': str(folder/'j640.xml.gz'),
                       'save_sha': package['header']['save_sha'], 'package': str(folder/'j640.x4join'),
                       'package_sha': hashlib.sha256(data).hexdigest(), 'portable': package['sections']['gate']['portable']}
            (root/'handoff.json').write_text(json.dumps(handoff))
            notes = self.run_main('joiner', '--gate-dll', dll, '--handoff', root/'handoff.json', '--out', root/'joiner', '--join-mode', 'report',
                                  '--set', 'native_gate_world_hash_stride=300', '--join-census', *WINDOW)
            self.assertIn(' world=v13 health=1 pose=0 shield=0 nav=0 attn=0 keys=0 path=0/0/0 region=0 flight=0/0 spline=0 agent=0 route=0/0'
                          ' param=0 dock=0 steer=0/0 solver=0/0 bullet=0 grav=0/0/0 splm=0/0/0 gravn=0 gravp=0 gravq=0 jobe=0 evqt=0 gravd=0 viewk=0/0 viewf=0 viewp=0 shoot=0 attk=0 livef=0 spec=0 navc=0 jobs=0 gone=0 body=0 bupd=0 ldc=0 navt=0 nmesh=0 noct=0 agtt=0 agcb=0 census_t1=1 ',
                          notes)
            self.assertIn('X4LAN_CONFIG.native_gate_world_hash_join = true', (root/'joiner/ui/config.lua.append').read_text().splitlines())
            self.assertEqual(json.loads((root/'joiner/x4lane-join.json').read_text())['world'],
                             'v13 health=1 pose=0 shield=0 nav=0 attn=0 keys=0 path=0/0/0 region=0 flight=0/0 spline=0 agent=0 route=0/0'
                             ' param=0 dock=0 steer=0/0 solver=0/0 bullet=0 grav=0/0/0 splm=0/0/0 gravn=0 gravp=0 gravq=0 jobe=0 evqt=0 gravd=0 viewk=0/0 viewf=0 viewp=0 shoot=0 attk=0 livef=0 spec=0 navc=0 jobs=0 gone=0 body=0 bupd=0 ldc=0 navt=0 nmesh=0 noct=0 agtt=0 agcb=0')
        old = join.read_package(join.write_package(dict(package['sections']['gate']), {join.SEC_IDS: ids}, version=1))
        self.assertEqual(overlays.check_world_table(old, 'report'), 'v1 health=0 pose=0')
        with self.assertRaisesRegex(ValueError, 'package_v1'):
            overlays.check_world_table(old, 'strict')
        v2 = join.read_package(join.write_package(dict(package['sections']['gate']), {join.SEC_IDS: ids, join.SEC_HEALTH: health}, version=2))
        self.assertEqual(overlays.check_world_table(v2, 'report'), 'v2 health=1 pose=0')
        with self.assertRaisesRegex(ValueError, 'package_v2'):
            overlays.check_world_table(v2, 'strict')
        v3 = join.read_package(join.write_package(dict(package['sections']['gate']), {join.SEC_IDS: ids, join.SEC_HEALTH: health}, version=3))
        self.assertEqual(overlays.check_world_table(v3, 'report'), 'v3 health=1 pose=0 shield=0 nav=0 attn=0 keys=0')
        with self.assertRaisesRegex(ValueError, 'package_v3: the package carries no PATH or REGION'):
            overlays.check_world_table(v3, 'strict')
        # Version 4: the batched agents, the new agents and the batches of PATH, the damage fields of REGION.
        path = [(0, join.PATH_MANAGER, 0, 1, 0, 0, 0), (0, join.PATH_MANAGER, 1, 0, 0, 0, 0), (0, join.PATH_BATCH, 0, 0, 0, 1, 0),
                (0x2003, join.PATH_MEMBER, 0, 0, 0, 0, 0), (0x2004, join.PATH_PENDING, 0, 0, 0, 0, 0)]
        v4 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                  {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_REGION: [(0x2005, 1, 2, 0, 0)]}, version=4))
        self.assertEqual(overlays.check_world_table(v4, 'report'), 'v4 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=1')
        with self.assertRaisesRegex(ValueError, 'package_v4: the package carries no FLIGHT or SPLINE'):
            overlays.check_world_table(v4, 'strict')
        # Version 5: the linear flight models and the other models of FLIGHT, the records of SPLINE.
        flight = [(0x2003, join.FLIGHT_LINEAR, 0, 0, 1, bytes(204), 0), (0x2004, 0x2c4e9c8, join.FLIGHT_OTHER, 0, 0, bytes(204), 0)]
        v5 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                  {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_FLIGHT: flight,
                                                   join.SEC_SPLINE: [(0x2003, 1, 0, bytes(160))]}, version=5))
        self.assertEqual(overlays.check_world_table(v5, 'report'),
                         'v5 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=0 flight=1/1 spline=1')
        with self.assertRaisesRegex(ValueError, 'package_v5: the package carries no AGENT section'):
            overlays.check_world_table(v5, 'strict')
        # Version 6: one AGENT record per FLIGHT record.
        agents = [(0x2003, *range(18), bytes(12), 0, 0, 0, 0, 0), (0x2004, *range(18), bytes(12), 0, 0, 0, 1, 0)]
        v6 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                  {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_FLIGHT: flight,
                                                   join.SEC_SPLINE: [(0x2003, 1, 0, bytes(160))], join.SEC_AGENT: agents}, version=6))
        self.assertEqual(overlays.check_world_table(v6, 'report'),
                         'v6 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=0 flight=1/1 spline=1 agent=2')
        with self.assertRaisesRegex(ValueError, 'package_v6: the package carries no ROUTE section'):
            overlays.check_world_table(v6, 'strict')
        # Version 7: per AGENT record a ROUTE head (and the points its lengths name: none here).
        routes = [(0x2003, join.ROUTE_HEAD, 0, 0, join.ROUTE_LINEAR, 0, 0, 0, 0, 0, 0, bytes(168)),
                  (0x2004, join.ROUTE_HEAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, bytes(168))]
        v7 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                  {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_FLIGHT: flight,
                                                   join.SEC_SPLINE: [(0x2003, 1, 0, bytes(160))], join.SEC_AGENT: agents,
                                                   join.SEC_ROUTE: routes}, version=7))
        self.assertEqual(overlays.check_world_table(v7, 'report'),
                         'v7 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=0 flight=1/1 spline=1 agent=2 route=2/0')
        with self.assertRaisesRegex(ValueError, 'package_v7: the package carries no PARAM or DOCK sections'):
            overlays.check_world_table(v7, 'strict')
        # Version 8: per AGENT record a PARAM record (three slot blocks), and one DOCK record of a ship docking.
        params = [(0x2003, 0, 0, bytes(join.PARAM_SLOT), bytes(join.PARAM_SLOT), bytes(join.PARAM_SLOT)),
                  (0x2004, 0, 0, bytes(join.PARAM_SLOT), bytes(join.PARAM_SLOT), bytes(join.PARAM_SLOT))]
        docks = [(0x2001, join.DOCK_VT, 0, 0, 0, bytes(2), 0, *range(42))]
        v8 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                  {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_FLIGHT: flight,
                                                   join.SEC_SPLINE: [(0x2003, 1, 0, bytes(160))], join.SEC_AGENT: agents,
                                                   join.SEC_ROUTE: routes, join.SEC_PARAM: params, join.SEC_DOCK: docks}, version=8))
        self.assertEqual(overlays.check_world_table(v8, 'report'),
                         'v8 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=0 flight=1/1 spline=1 agent=2 route=2/0'
                         ' param=2 dock=1')
        with self.assertRaisesRegex(ValueError, 'package_v8: the package carries no STEER or STEERPT sections'):
            overlays.check_world_table(v8, 'strict')
        # Version 9: one STEER record per steering model of FLIGHT (its behaviours a chase and the generic default) and its one
        # path point in STEERPT.
        steers = [(0x2004, 0, join.STEER_VT, 0, 0, 0x2c4e840, 0x2c4edc0, 1, 0, 0, bytes(join.STEER_MODEL), bytes(join.STEER_STATE),
                   bytes(join.STEER_STATE))]
        points = [(0x2004, join.ROUTE_POINT, join.STEER_LIST, 0, 0, 0, 0, 0, 0, 0, 0, bytes(168))]
        v9 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                  {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_FLIGHT: flight,
                                                   join.SEC_SPLINE: [(0x2003, 1, 0, bytes(160))], join.SEC_AGENT: agents,
                                                   join.SEC_ROUTE: routes, join.SEC_PARAM: params, join.SEC_DOCK: docks,
                                                   join.SEC_STEER: steers, join.SEC_STEERPT: points}, version=9))
        self.assertEqual(overlays.check_world_table(v9, 'report'),
                         'v9 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=0 flight=1/1 spline=1 agent=2 route=2/0'
                         ' param=2 dock=1 steer=1/1')
        with self.assertRaisesRegex(ValueError, 'package_v9: the package carries no SOLVER or BULLET sections'):
            overlays.check_world_table(v9, 'strict')
        # Version 10: the steering model's Agent's solver (active) and a dormant one under the linear model, one bullet.
        data = bytes(join.SOLVER_BYTES)
        solvers = [(0x2003, 0, 0x2c4f610, 0, 0, bytes(4*join.SOLVER_POINTERS), data),
                   (0x2004, join.SOLVER_ACTIVE, join.STEER_VT, 0, 0, bytes(4*join.SOLVER_POINTERS), data)]
        bullets = [(0x2003, 0, 0, 0, join.BULLET_NO_SHOOTER | join.BULLET_NO_MACRO | join.BULLET_NO_MOTION, 0, bytes(join.BULLET_MACRO),
                    bytes(join.BULLET_BYTES), bytes(join.BULLET_MOTION))]
        v10 = join.read_package(join.write_package(dict(package['sections']['gate']),
                                                   {join.SEC_IDS: ids, join.SEC_PATH: path, join.SEC_FLIGHT: flight,
                                                    join.SEC_SPLINE: [(0x2003, 1, 0, bytes(160))], join.SEC_AGENT: agents,
                                                    join.SEC_ROUTE: routes, join.SEC_PARAM: params, join.SEC_DOCK: docks,
                                                    join.SEC_STEER: steers, join.SEC_STEERPT: points, join.SEC_SOLVER: solvers,
                                                    join.SEC_BULLET: bullets}, version=10))
        self.assertEqual(overlays.check_world_table(v10, 'report'),
                         'v10 health=0 pose=0 shield=0 nav=0 attn=0 keys=0 path=1/1/1 region=0 flight=1/1 spline=1 agent=2 route=2/0'
                         ' param=2 dock=1 steer=1/1 solver=1/1 bullet=1')
        with self.assertRaisesRegex(ValueError, 'package_v10: the package carries no GRAV, GRAVC or GRAVS sections'):
            overlays.check_world_table(v10, 'strict')
        # The census tools accept the extra capture ticks (T+1 of each SAVE_AT, a joiner's tick base + 1) only with the key.
        log = ('[X4LAN] NATIVE_GATE JOIN_SAVE_AT tick=603 k=3 save_tick=633 mask=3 nonce=00 mine=1\n'
               '[X4LAN] NATIVE_GATE JOIN_ADMITTED base=633 state=idle\n')
        self.assertEqual(evidence.join_capture_ticks(log, 'X4LAN_CONFIG.native_gate_world_hash_join = true'), {634})
        self.assertEqual(evidence.join_capture_ticks(log, ''), set())


class SavedIdKeys(unittest.TestCase):
    """native_gate_join_overlays.check_saved_ids: every 16-hex-digit saved ID of the joiner's effective config (the session
    defaults applied, as the probe runs them) must be a saved identity of the package."""
    CONFIG = '\n'.join(['X4LAN_CONFIG.native_gate_test = true', 'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"', 'X4LAN_CONFIG.port = @X4LANE_PORT@',
                        'X4LAN_CONFIG.token = "@X4LANE_TOKEN@"', 'X4LAN_CONFIG.native_gate_clients = 3',
                        "X4LAN_CONFIG.native_gate_world_hash_watch = {'00000000000ae79c','0000000000000729'}",
                        'X4LAN_CONFIG.native_gate_snapshot_sha = "' + 'ab'*32 + '"'])

    def package(self, ids):
        return {'sections': {'ids': [(0x1000+k, portable, klass, 0) for k, (portable, klass) in enumerate(ids)]}}

    def test_keys_and_refusals(self):
        named = overlays.saved_id_keys(self.CONFIG)
        self.assertEqual(named['native_gate_world_hash_watch'], ['0000000000000729', '00000000000ae79c'])
        self.assertEqual(len(named['native_gate_snapshot_fixture']), 12)   # the session default
        self.assertNotIn('native_gate_snapshot_sha', named)                 # 64 hex digits: no saved ID
        every = [(int(i, 16), 0) for ids in named.values() for i in ids]
        self.assertEqual(overlays.check_saved_ids(self.CONFIG, self.package(every)), {k: len(v) for k, v in named.items()})
        with self.assertRaises(ValueError) as refused:
            overlays.check_saved_ids(self.CONFIG, self.package([r for r in every if r[0] != 0x729]+[(0x729, 1)]))
        self.assertIn('native_gate_world_hash_watch: 0000000000000729', str(refused.exception))
        # A config without the session defaults names only its own keys.
        bare = self.CONFIG+'\nX4LAN_CONFIG.native_gate_defaults = false'
        self.assertEqual(sorted(overlays.saved_id_keys(bare)), ['native_gate_world_hash_watch'])
        # The session's fixture views: required with the fixture (the session default), not without it.
        gate = lambda views: {'sections': {'gate': {'fixture': views}}}
        self.assertEqual(overlays.check_fixture_views(self.CONFIG, gate('111111121113')).split(','),
                         ['nav']*7+['drift']+['nav']*3+['bare'])
        for views, reason in ((join.NO_FIXTURE, 'fixture_view_missing'), ('511111111111', 'fixture_view:1:lost')):
            with self.subTest(views=views), self.assertRaisesRegex(ValueError, reason):
                overlays.check_fixture_views(self.CONFIG, gate(views))
        self.assertEqual(overlays.check_fixture_views(bare, gate(join.NO_FIXTURE)), '')

    def test_the_probe_state_the_joiner_restores(self):
        """check_lua_state: the providers the joiner's probe restores in frame T+1 (reports/LOCKSTEP-HOTJOIN-20261101.txt 1.5),
        refused before the launch as the probe would refuse them."""
        lua = dict(line.split('=', 1) for line in SECTIONS[join.SEC_LUA])
        package = lambda lines, tick=640: {'sections': {'gate': {'tick': tick}, 'lua': [f'{k}={v}' for k, v in lines.items()]}}
        # The session defaults: the snapshot fixture (battle) and a world hash stride; no pilot inputs without a plan.
        self.assertEqual(overlays.check_lua_state(self.CONFIG, package(lua)), 'fixture,identity,pilot_absent,pilot_window,world_hash')
        for changed, reason in ((dict(lua, fixture='v1|'+lua['fixture'].split('|')[1]), 'lua_state:fixture:v1|'),
                                ({k: v for k, v in lua.items() if k != 'fixture'}, 'lua_state:fixture:missing'),
                                (dict(lua, world_hash='-'), 'lua_state:world_hash:missing'),
                                ({k: v for k, v in lua.items() if k != 'world_hash'}, 'lua_state:world_hash:missing')):
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                overlays.check_lua_state(self.CONFIG, package(changed))
        bare = self.CONFIG+'\nX4LAN_CONFIG.native_gate_defaults = false'
        self.assertEqual(overlays.check_lua_state(bare, package({})), '')   # nothing configured, nothing needed
        # Pilot inputs: the session's window must be this config's and T+1 at most its take-2 (start - 5).
        pilot = self.CONFIG+'\nX4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_pilot_window = {1500,72000}'
        self.assertIn('pilot_window', overlays.check_lua_state(pilot, package(lua, tick=1494)))
        for lines, tick, reason in ((lua, 1495, 'lua_state:pilot_window:late'),
                                    (dict(lua, pilot_window='v1|1600,72000'), 640, r'lua_state:pilot_window:v1\|1600,72000'),
                                    ({k: v for k, v in lua.items() if k != 'pilot_window'}, 640, 'lua_state:pilot_window:missing')):
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                overlays.check_lua_state(pilot, package(lines, tick))


class LaneJoinSession(unittest.TestCase):
    """native_gate_lane.py --join-session against a stand-in for the released lane tool (no X4, no lane): the joiner's run is
    checked loopback-only and gets the handoff's port, token and slot, and the join save is staged as the snapshot."""
    FAKE = '''
import json, re
from pathlib import Path
from types import SimpleNamespace
ROOT = Path(__file__).resolve().parent
class core:
    AUTOSTART_ID = 'x4lane_autostart'
    @staticmethod
    def is_link(path): return Path(path).is_symlink()
    @staticmethod
    def assert_inside(path, root):
        if not Path(path).resolve().is_relative_to(Path(root).resolve()): raise ValueError('outside')
    @staticmethod
    def check_tree_unlinked(path): pass
    @staticmethod
    def safe_unlink(path, root): Path(path).unlink()
    @staticmethod
    def safe_write(path, text, root): Path(path).write_text(text, encoding='utf-8')
class Run:
    def __init__(self, argv):
        self.argv, self.network, self.extra_tokens, self.result, self.proc = argv, 'offline', {}, {}, None
        lab = ROOT/'lane/extensions/x4_lan_lab'
        self.lane = SimpleNamespace(profile='45855', profile_dir=ROOT/'profiles/45855', lab=lab, extensions=ROOT/'lane/extensions', dir=ROOT/'lane')
        self.outdir = ROOT/'out'
    def prepare(self):
        overlay = Path(self.argv[self.argv.index('--overlay')+1])
        text = (overlay/'ui/config.lua.append').read_text(encoding='utf-8')
        for key, value in self.extra_tokens.items(): text = text.replace(key, value)
        (self.lane.lab/'ui').mkdir(parents=True, exist_ok=True)
        (self.lane.lab/'ui/config.lua').write_text(text, encoding='utf-8')
        settings = self.lane.extensions/core.AUTOSTART_ID/'ui'
        settings.mkdir(parents=True, exist_ok=True)
        (settings/'settings.lua').write_text('enabled = true\\n')
        (self.outdir/'installed').mkdir(parents=True, exist_ok=True)
        self.result['network'] = self.network
    def collect(self): pass
    def restore(self): pass
def main(argv):
    run = Run(argv)
    run.prepare()
    (ROOT/'result.json').write_text(json.dumps(run.result))
    return 0
'''

    def test_join_session_run(self):
        lane = load('native_gate_lane_join', DEV/'native_gate_lane.py')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root/'profiles/45855').mkdir(parents=True)
            tool = root/'x4lane.py'
            tool.write_text(self.FAKE)
            folder = root/'join'
            data, package = write_saver_files(folder, 1, 640)
            save = folder/'j640.xml.gz'
            overlay = root/'overlay/ui'
            overlay.mkdir(parents=True)
            (overlay/'config.lua.append').write_text(
                'X4LAN_CONFIG.host = "127.0.0.1"\nX4LAN_CONFIG.port = @X4LANE_PORT@\nX4LAN_CONFIG.token = "@X4LANE_TOKEN@"\n'
                'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\nX4LAN_CONFIG.native_gate_clients = 3\n'
                f'X4LAN_CONFIG.native_gate_snapshot_sha = "{hashlib.sha256(save.read_bytes()).hexdigest()}"\n')
            handoff = {'kind': driver.HANDOFF_KIND, 'version': 1, 'source': 'session', 'port': 41234, 'token': TOKEN, 'slot': 3,
                       'clients': 3, 'lookahead': K, 'tick_hz': HZ, 'save_tick': 640, 'saver': 1, 'save': str(save),
                       'save_sha': package['header']['save_sha'], 'package': str(folder/'j640.x4join'),
                       'package_sha': hashlib.sha256(data).hexdigest(), 'portable': package['sections']['gate']['portable']}
            (root/'handoff.json').write_text(json.dumps(handoff))
            argv = ['native_gate_lane.py', '--lane-tool', str(tool), '--join-session', str(root/'handoff.json'), '--', 'run', '--lane', 'b5',
                    '--overlay', str(root/'overlay')]
            with mock.patch('sys.argv', argv), self.assertRaises(SystemExit) as stop:
                lane.main()
            self.assertEqual(stop.exception.code, 0)
            result = json.loads((root/'result.json').read_text())
            self.assertEqual(result['network'], 'loopback')
            self.assertEqual(result['native_join']['slot'], 3)
            self.assertNotIn(TOKEN, json.dumps(result))
            config = (root/'lane/extensions/x4_lan_lab/ui/config.lua').read_text()
            for line in ('X4LAN_CONFIG.port = 41234', f'X4LAN_CONFIG.token = "{TOKEN}"', 'X4LAN_CONFIG.slot = "p3"'):
                self.assertIn(line, config)
            self.assertEqual((root/'profiles/45855/save/save_009.xml.gz').read_bytes(), save.read_bytes())
            self.assertEqual((root/'lane/extensions/x4lane_autostart/ui/settings.lua').read_text(), 'enabled = false\n')
            # Refusals: a session, a handoff whose save changed, both a handoff and --snapshot-in.
            for argv, change in ((argv[:5]+['session', '--clients', '2'], None), (argv, b'changed save'),
                                 (argv[:3]+['--snapshot-in', str(save)]+argv[3:], None)):
                with self.subTest(argv=argv[3:6]):
                    if change:
                        save.write_bytes(change)
                    with mock.patch('sys.argv', argv), self.assertRaises(SystemExit) as stop, contextlib.redirect_stderr(io.StringIO()):
                        lane.main()
                    self.assertEqual(stop.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
