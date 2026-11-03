"""native_gate_lane.py --session-token-file (wire crypto W1/W2): the session's one token draw takes the fixed token."""
import importlib.util
import io
import contextlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]

FAKE_LANE = '''
import json, os, secrets
class Run:
    def prepare(self): pass
    def collect(self): pass
    def restore(self): pass
class SessionRun:
    def prepare(self):
        self.token = secrets.token_hex(16)
core = None
def main(args):
    run = SessionRun(); run.prepare()
    after = secrets.token_hex(16)            # a later draw outside prepare: a fresh token again
    with open(os.environ['FAKE_LANE_OUT'], 'w') as out:
        json.dump({'token': run.token, 'after': after, 'args': args}, out)
    return 0
'''


def load_wrapper():
    spec = importlib.util.spec_from_file_location('native_gate_lane_under_test', ROOT/'tools/dev/native_gate_lane.py')
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT/'tools/dev'))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.remove(str(ROOT/'tools/dev'))
    return module


class SessionTokenFileTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir/'fake_lane.py').write_text(FAKE_LANE, encoding='utf-8')
        (self.dir/'snap.xml.gz').write_bytes(b'x')
        self.out = self.dir/'out.json'
        self.wrapper = load_wrapper()

    def tearDown(self):
        self.tmp.cleanup()

    def run_wrapper(self, *extra, lane=('session', '--clients', '2')):
        argv = ['native_gate_lane.py', '--lane-tool', str(self.dir/'fake_lane.py'), '--snapshot-in', str(self.dir/'snap.xml.gz'),
                *extra, '--', *lane]
        with mock.patch.object(sys, 'argv', argv), mock.patch.dict('os.environ', {'FAKE_LANE_OUT': str(self.out)}), \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as stop:
                self.wrapper.main()
        return stop.exception.code

    def test_the_session_draw_takes_the_fixed_token_only(self):
        token = '0123456789abcdef0123456789abcdef'
        (self.dir/'token').write_text(token + '\n', encoding='ascii')
        self.assertEqual(self.run_wrapper('--session-token-file', str(self.dir/'token')), 0)
        seen = json.loads(self.out.read_text())
        self.assertEqual(seen['token'], token)
        self.assertNotEqual(seen['after'], token)        # restored after prepare
        self.assertEqual(seen['args'], ['session', '--clients', '2'])

    def test_without_the_option_the_token_is_fresh(self):
        self.assertEqual(self.run_wrapper(), 0)
        seen = json.loads(self.out.read_text())
        self.assertRegex(seen['token'], r'^[0-9a-f]{32}$')

    def test_refusals(self):
        (self.dir/'bad').write_text('not-hex', encoding='ascii')
        self.assertEqual(self.run_wrapper('--session-token-file', str(self.dir/'bad')), 2)
        (self.dir/'token').write_text('0123456789abcdef0123456789abcdef', encoding='ascii')
        self.assertEqual(self.run_wrapper('--session-token-file', str(self.dir/'token'), lane=('run', '--lane', 'b1')), 2)
        self.assertFalse(self.out.exists())


if __name__ == '__main__':
    unittest.main()
