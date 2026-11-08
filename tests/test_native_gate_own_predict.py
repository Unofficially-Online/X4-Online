"""Own-ship display prediction (lockstep_own_predict.h, stop reason 184), default off.

Covers the probe (two synthetic stock clients under LuaJIT with the real probe and a mocked gate:
per-client arming, the OWN_PREDICT rows written at once, the status and stop rows, refusals before
loading, nothing of it in the declared state) and the option checker's own-predict rules. The native
half (features, models, correction policy, observe on fake engine memory, the slot-10 swap) is tested
in C: src/native/engine/test_lockstep_own_predict.h through test_lockstep_gate.c."""
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT/path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stock = load('native_gate_stock_harness_own', 'tests/test_native_gate_pilot_stock.py')
options = load('check_gate_options_own', 'tools/dev/check_gate_options.py')

PRELUDE = '''
  own_calls={};own_frames={}
  gate.render_rng_split=function() return 1 end
  gate.render_rng_split_status=function() return 'mode=split calls=0' end
  gate.own_predict=function(mode,others,tau,cap)
    own_calls[#own_calls+1]=mode..':'..others..':'..tau..':'..cap
    if OWN_REFUSAL then return 0,OWN_REFUSAL end
    return 1 end
  gate.own_predict_frame=function(tick)
    own_frames[#own_frames+1]=tick
    if tick==7 then return nil,'tick' end
    return 'tick='..tick..' h=3 s'..SLOT..'=3/0.012/0.0007/9.4/0.02/0/0'
  end
  gate.own_predict_status=function() return 'mode=present armed=1 horizon=3 failures=0' end
'''


def client(slot, refusal=None, **extra):
    prelude = PRELUDE + (f"OWN_REFUSAL='{refusal}'\n" if refusal else '')
    extra.setdefault('native_gate_render_rng_split', True)
    return stock.StockClient(slot, slots_all=True, prelude=prelude, **extra)


class OwnPredictProbeTests(unittest.TestCase):
    def test_arms_per_client_writes_rows_at_once_and_stays_out_of_the_hash(self):
        clients = [client(1, native_gate_own_predict=['present', 'observe'], native_gate_own_predict_tau=120,
                          native_gate_own_predict_horizon=8),
                   client(2, native_gate_own_predict=['present', 'observe'], native_gate_own_predict_tau=120,
                          native_gate_own_predict_horizon=8, native_gate_own_predict_others=False)]
        stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [])
        self.assertEqual(clients[0].values('own_calls'), ['present:1:120:8', 'present:1:120:8'])  # again after the UI reload
        self.assertEqual(clients[1].values('own_calls'), ['observe:0:120:8', 'observe:0:120:8'])
        self.assertEqual(clients[0].values('own_frames'), list(range(1, 13)))
        logs = '\n'.join(clients[0].values('logs'))
        self.assertIn('OWN_PREDICT_ARMED slot=1 mode=present others=true tau_ms=120 horizon_cap=8 rows=1', logs)
        self.assertIn('OWN_PREDICT_ARMED slot=2 mode=observe others=false', '\n'.join(clients[1].values('logs')))
        for tick in (5, 6, 12):
            self.assertIn(f'OWN_PREDICT tick={tick} h=3 s1=3/0.012/', logs)
        self.assertIn('OWN_PREDICT tick=7 refused=tick', logs)   # never fatal
        hashed = ''.join(clients[0].values('hashes'))
        self.assertNotIn('OWN_PREDICT', hashed)
        self.assertNotIn('0.012', hashed)
        c = clients[0]
        c.lua.execute("state.active=0;state.completed=12;setmetatable(state,{__index=function() return 0 end});update()")
        self.assertIn('OWN_PREDICT_STOP mode=present armed=1', '\n'.join(c.values('logs')))

    def test_row_stride(self):
        c = client(1, native_gate_own_predict='observe', native_gate_own_predict_rows=4)
        stock.run([c, client(2)], 12)
        logs = '\n'.join(c.values('logs'))
        self.assertEqual(sorted(int(t) for t in re.findall(r'OWN_PREDICT tick=(\d+) ', logs)), [4, 8, 12])
        self.assertEqual(c.values('own_frames'), list(range(1, 13)))  # every tick predicts, only the rows thin out
        c = client(1, native_gate_own_predict=True, native_gate_own_predict_rows=0)
        stock.run([c, client(2)], 6)
        self.assertNotIn('OWN_PREDICT tick=', '\n'.join(c.values('logs')))
        self.assertEqual(c.values('own_calls')[0], 'observe:1:150:10')   # true = observe, the defaults
        c = client(1, native_gate_own_predict='swap')
        stock.run([c, client(2)], 3)
        self.assertEqual(c.values('own_calls')[0], 'swap:1:150:10')      # the census separation run

    def test_off_by_default_and_per_client_false(self):
        c = client(1)
        stock.run([c, client(2)], 6)
        self.assertEqual(c.values('own_calls'), [])
        self.assertEqual(c.values('own_frames'), [])
        c = client(2, native_gate_own_predict=['observe', False])
        stock.run([client(1, native_gate_own_predict=['observe', False]), c], 6)
        self.assertEqual(c.values('own_calls'), [])
        self.assertFalse(any('OWN_PREDICT' in line for line in c.values('logs')))

    def test_refusals_before_loading(self):
        refused = 'own predict refused'
        cases = (
            ('present without the split', dict(native_gate_own_predict='present', native_gate_render_rng_split=None)),
            ('swap without the split', dict(native_gate_own_predict='swap', native_gate_render_rng_split=None)),
            ('mode', dict(native_gate_own_predict='fast')),
            ('tau', dict(native_gate_own_predict='observe', native_gate_own_predict_tau=2001)),
            ('tau fraction', dict(native_gate_own_predict='observe', native_gate_own_predict_tau=12.5)),
            ('horizon 0', dict(native_gate_own_predict='observe', native_gate_own_predict_horizon=0)),
            ('horizon 31', dict(native_gate_own_predict='observe', native_gate_own_predict_horizon=31)),
            ('rows', dict(native_gate_own_predict='observe', native_gate_own_predict_rows=-1)),
            ('others', dict(native_gate_own_predict='observe', native_gate_own_predict_others='yes')),
            ('a key without the option', dict(native_gate_own_predict_tau=100)),
        )
        for name, extra in cases:
            with self.subTest(name):
                c = client(1, **extra)
                errors = c.errors()
                self.assertTrue(errors and refused in errors[0], errors)
                self.assertEqual(c.lua.globals().loads, 0)
        # The gate's own reason reaches the error row.
        errors = client(1, refusal='needs_stock_capture', native_gate_own_predict='observe').errors()
        self.assertTrue(errors and 'own predict refused: needs_stock_capture' in errors[0], errors)
        # A client that does not capture its slot through the stock path has nothing to predict from.
        errors = stock.StockClient(2, prelude=PRELUDE, native_gate_own_predict=[False, 'observe']).errors()
        self.assertTrue(errors and refused in errors[0], errors)


def own_failures(text):
    return [f['rule'] for f in options.own_predict_rules(text, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', text)))]


STOCK_ALL = ('X4LAN_CONFIG.native_gate_pilot_slots = {\'000000000000a001\',\'000000000000a002\'}\n'
             'X4LAN_CONFIG.native_gate_pilot_stock = true\n'
             "X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'\n"
             "X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','stock'}\n"
             'X4LAN_CONFIG.native_gate_render_rng_split = true\n')


class OwnPredictOptionTests(unittest.TestCase):
    def test_valid_settings(self):
        for line in ("X4LAN_CONFIG.native_gate_own_predict = 'present'\n",
                     "X4LAN_CONFIG.native_gate_own_predict = {'present','observe'}\n",
                     "X4LAN_CONFIG.native_gate_own_predict = true\n",
                     "X4LAN_CONFIG.native_gate_own_predict = {'swap','observe'}\n",
                     "X4LAN_CONFIG.native_gate_own_predict = false\nX4LAN_CONFIG.native_gate_own_predict_tau = 100\n",
                     "X4LAN_CONFIG.native_gate_own_predict = 'observe' -- this client\nX4LAN_CONFIG.native_gate_own_predict_tau = 0\n"
                     "X4LAN_CONFIG.native_gate_own_predict_horizon = 30\nX4LAN_CONFIG.native_gate_own_predict_rows = 0\n"
                     "X4LAN_CONFIG.native_gate_own_predict_others = false\n"):
            with self.subTest(line):
                self.assertEqual(own_failures(STOCK_ALL+line), [])
        self.assertEqual(own_failures(STOCK_ALL), [])

    def test_rules(self):
        cases = {
            "native_gate_own_predict must be true, 'observe', 'present', 'swap' or a per-client list of them":
                "X4LAN_CONFIG.native_gate_own_predict = 'fast'\n",
            'native_gate_own_predict needs native_gate_pilot_stock':
                "X4LAN_CONFIG.native_gate_own_predict = 'observe'\n",
            "native_gate_own_predict 'present' needs native_gate_render_rng_split = true":
                "X4LAN_CONFIG.native_gate_own_predict = {false,'present'}\n",
            'native_gate_own_predict_tau needs native_gate_own_predict': 'X4LAN_CONFIG.native_gate_own_predict_tau = 100\n',
            'native_gate_own_predict_horizon must be a whole number 1..30':
                "X4LAN_CONFIG.native_gate_own_predict = true\nX4LAN_CONFIG.native_gate_own_predict_horizon = 40\n",
            'native_gate_own_predict_rows must be a whole number 0..3600':
                "X4LAN_CONFIG.native_gate_own_predict = true\nX4LAN_CONFIG.native_gate_own_predict_rows = 2.5\n",
            'native_gate_own_predict_others must be true or false':
                "X4LAN_CONFIG.native_gate_own_predict = true\nX4LAN_CONFIG.native_gate_own_predict_others = 1\n",
        }
        for rule, line in cases.items():
            with self.subTest(rule):
                base = STOCK_ALL
                if 'pilot_stock' in rule:
                    base = ''
                if 'render_rng_split' in rule:
                    base = STOCK_ALL.replace('X4LAN_CONFIG.native_gate_render_rng_split = true\n', '')
                self.assertIn(rule, own_failures(base+line))
        # Only a stock slot's client captures its own records: prediction on a device client is refused.
        device = STOCK_ALL.replace("{'stock','stock'}", "{'stock','device'}").replace("'all'", '{1}')
        self.assertTrue(own_failures(device+"X4LAN_CONFIG.native_gate_own_predict = {false,'observe'}\n"))
        self.assertEqual(own_failures(device+"X4LAN_CONFIG.native_gate_own_predict = {'observe',false}\n"), [])

    def test_wired_into_the_probe_rules(self):
        invalid = "X4LAN_CONFIG.native_gate_own_predict = 'observe'\n"
        missing_stock = 'native_gate_own_predict needs native_gate_pilot_stock'
        self.assertIn(missing_stock, [f['rule'] for f in options.probe_rules(invalid, set())])
        valid = STOCK_ALL + invalid
        enabled = set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', valid))
        self.assertNotIn(missing_stock, [f['rule'] for f in options.probe_rules(valid, enabled)])


if __name__ == '__main__':
    unittest.main()
