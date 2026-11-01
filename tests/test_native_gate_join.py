"""tools/dev/native_gate_join.py (D14 prototype): fork request and permit hold, save hand-out, catch-up replay from the
relay's own input journal, digest check, handover. The journal comes from the relay's Barrier and is written the way
native_gate_relay.py --input-journal writes it. Synthetic peers only, no X4."""
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
from native_gate_relay import Barrier  # noqa: E402
spec = importlib.util.spec_from_file_location('native_gate_join', ROOT/'tools/dev/native_gate_join.py')
join = importlib.util.module_from_spec(spec)
spec.loader.exec_module(join)
TOKEN = 'ab'*16
CLIENTS = 3


def game(tick):
    return 7+tick/60


def digest(tick):
    return f'{tick:064x}'


def blob(slot, tick):
    return '' if tick % 3 == 0 else f'{slot:02x}{tick % 256:02x}'


def session(ticks, lookahead=0):
    """A relay session of CLIENTS clients through ticks, journaled like --input-journal; slot 3 sends empty blobs
    (the absent slot the relay stands in for)."""
    barrier = Barrier(TOKEN, ticks=ticks+10, hold_seconds=0, relative_real=True, clients=CLIENTS, lookahead=lookahead)
    version = 4 if lookahead else 3
    for tick in range(ticks+1):
        for slot in range(1, CLIENTS+1):
            real = struct.pack('>d', slot*10+tick/60).hex()
            body = blob(slot, tick) if slot != 3 else ''
            extra = f'|{lookahead}' if lookahead else ''
            barrier.receive(f'NGSTATE|{version}|{TOKEN}|{slot}|{tick}|{struct.pack(">d", game(tick)).hex()}|{real}|{digest(tick)}|'
                            f'{CLIENTS}{extra}|{body}'.encode(), ('127.0.0.1', 10000+slot), tick)
    assert barrier.failed is None and barrier.released == ticks, (barrier.failed, barrier.released)
    lines = [json.dumps({'tick': t, 'released_at': 0, 'rows': barrier.states.get(t, {})}, separators=(',', ':')) for t in range(ticks+1)]
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)/'journal.jsonl'
        path.write_text('\n'.join(lines)+'\n', encoding='ascii')
        return join.load_journal(path), barrier


def joiner_state(t, fork, version=3, wrong=False, lookahead=0):
    extra = f'|{lookahead}' if version == 4 else ''
    return (f'NGSTATE|{version}|{TOKEN}|3|{t}|{struct.pack(">d", game(fork+t)).hex()}|{"0"*16}|'
            f'{digest(fork+t+1) if wrong else digest(fork+t)}|{CLIENTS}{extra}|').encode()


class JoinProtocol(unittest.TestCase):
    def test_journal_matches_the_relay(self):
        journal, barrier = session(30)
        self.assertEqual(sorted(journal), list(range(31)))
        self.assertEqual(journal[7][2]['blob'], blob(2, 7))
        self.assertEqual(journal[7][1], barrier.states[7][1])
        self.assertEqual(join.absent_row(journal[7][1]), {'game': game(7), 'real': journal[7][1]['real'], 'digest': digest(7), 'blob': ''})

    def test_fork_request_holds_the_fork_permit_until_armed(self):
        c = join.JoinCoordinator(TOKEN, CLIENTS, lead=20)
        self.assertEqual(c.request(join.message('NGJOIN', 'cd'*16, 3, 'n'), 60), [])  # wrong token
        self.assertEqual(c.request(join.message('NGJOIN', TOKEN, 1, 'n'), 60), [])  # the server's own slot
        out = c.request(join.message('NGJOIN', TOKEN, 3, 'n'), 60)
        self.assertEqual(out, [('server', f'NGFORK|1|{TOKEN}|1|80|join_80'.encode())])
        self.assertTrue(c.may_release(79))
        self.assertFalse(c.may_release(80))
        self.assertEqual(c.server_message(join.message('NGFORKACK', TOKEN, 1, 79, 'join_80'), 61), [])
        self.assertFalse(c.may_release(80))  # another tick acknowledged: still held
        c.server_message(join.message('NGFORKACK', TOKEN, 1, 80, 'join_80'), 61)
        self.assertTrue(c.may_release(80))
        # A failed fork asks again for a later tick; a done one hands the save to the joiner.
        out = c.server_message(join.message('NGFORKED', TOKEN, 1, 80, 'failed', 0, '-'), 85)
        self.assertEqual(out, [('server', f'NGFORK|1|{TOKEN}|1|105|join_105'.encode())])
        self.assertFalse(c.may_release(105))
        sha = 'e'*64
        out = c.server_message(join.message('NGFORKED', TOKEN, 1, 105, 'done', 1234, sha), 106)
        self.assertEqual(out, [('joiner', f'NGSAVE|1|{TOKEN}|3|105|1234|{sha}'.encode())])
        self.assertEqual((c.state, c.save), ('loading', (1234, sha)))

    def test_retries_end(self):
        c = join.JoinCoordinator(TOKEN, CLIENTS, lead=5, retries=1)
        c.request(join.message('NGJOIN', TOKEN, 2, 'n'), 10)
        c.server_message(join.message('NGFORKED', TOKEN, 1, 15, 'failed', 0, '-'), 16)
        self.assertEqual(c.server_message(join.message('NGFORKED', TOKEN, 1, 21, 'failed', 0, '-'), 22), [])
        self.assertEqual((c.state, c.failure), ('failed', 'fork_failed_after_2'))

    def loaded(self, fork, lookahead=0, gap=10):
        c = join.JoinCoordinator(TOKEN, CLIENTS, lookahead=lookahead, lead=fork-40, handover_gap=gap)
        c.request(join.message('NGJOIN', TOKEN, 3, 'n'), 40)
        c.server_message(join.message('NGFORKED', TOKEN, 1, fork, 'done', 10, 'e'*64), 41)
        self.assertEqual(c.state, 'loading')
        return c

    def test_replay_carries_the_sessions_blobs(self):
        journal, _ = session(120)
        c = self.loaded(80)
        for t in range(0, 20):
            out = c.replay(joiner_state(t, 80), journal, 120)
            # The permit of the joiner's tick t+1 is the session's permit of 81+t: the blobs sealed with tick 80+t.
            self.assertEqual(out, [('joiner', (f'NGGO|3|{TOKEN}|3|{t+1}|' + '|'.join(blob(s, 80+t) if s != 3 else '' for s in (1, 2, 3))).encode())])
        self.assertEqual((c.state, c.joiner_tick), ('catching_up', 19))
        self.assertEqual(c.replay(joiner_state(19, 80), journal, 120), out)  # a retransmission gets the same permit
        self.assertEqual(c.replay(joiner_state(25, 80), journal, 120), [])  # from the future: ignored

    def test_replay_with_lookahead(self):
        journal, _ = session(120, lookahead=2)
        c = self.loaded(80, lookahead=2)
        out = c.replay(joiner_state(0, 80, version=4, lookahead=2), journal, 120)
        self.assertEqual(out, [('joiner', (f'NGGO|4|{TOKEN}|3|1|2|' + '|'.join(blob(s, 78) if s != 3 else '' for s in (1, 2, 3))).encode())])

    def test_a_different_world_stops_the_joiner(self):
        journal, _ = session(120)
        c = self.loaded(80)
        c.replay(joiner_state(0, 80), journal, 120)
        out = c.replay(joiner_state(1, 80, wrong=True), journal, 120)
        self.assertEqual(out, [('joiner', f'NGSTOP|3|{TOKEN}|3|1'.encode())])
        self.assertEqual((c.state, c.failure), ('failed', 'join_mismatch_tick_81'))

    def test_handover(self):
        journal, _ = session(130)
        c = self.loaded(80, gap=10)
        released = 120
        for t in range(0, 30):
            out = c.replay(joiner_state(t, 80), journal, released)
            self.assertNotIn('all', [dest for dest, _ in out])
        out = c.replay(joiner_state(30, 80), journal, released)  # session tick 110: within 10 of the live 120
        self.assertEqual(out[0], ('all', f'NGHANDOVER|1|{TOKEN}|3|121'.encode()))
        self.assertTrue(c.live_hold(122) and not c.live_hold(121))
        for t in range(31, 41):
            self.assertEqual(c.replay(joiner_state(t, 80), journal, 121)[0][0], 'joiner')
        self.assertEqual(c.replay(joiner_state(41, 80), journal, 121), [])
        self.assertEqual(c.state, 'joined')
        self.assertFalse(c.live_hold(122))

    def test_catch_up_time(self):
        self.assertEqual(join.catch_up_seconds(2550, 30, 40), 255)
        self.assertEqual(join.catch_up_seconds(100, 30, 30), math.inf)
        with self.assertRaises(ValueError):
            join.JoinCoordinator(TOKEN, CLIENTS, lookahead=4, lead=5)


if __name__ == '__main__':
    unittest.main()
