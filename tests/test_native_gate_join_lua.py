"""tools/dev/native_gate_join_core.lua (LuaJIT via lupa) against the Python reference tools/dev/native_gate_join.py:
JOIN record codec, the per-client section adapter (save arming, SAVED/FAILED proposals, handover, refusals) and the
probe state providers a joiner restores. No X4."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
spec = importlib.util.spec_from_file_location('native_gate_join', ROOT/'tools/dev/native_gate_join.py')
join = importlib.util.module_from_spec(spec)
spec.loader.exec_module(join)
try:  # the game's runtime is LuaJIT 2.1: prefer it where lupa ships it
    from lupa.luajit21 import LuaRuntime, LuaError
except ImportError:  # pragma: no cover
    try:
        from lupa import LuaRuntime, LuaError
    except ImportError:
        LuaRuntime = None
CORE = (ROOT/'tools/dev/native_gate_join_core.lua').read_text(encoding='utf-8')


def runtime():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(CORE)
    return lua, lua.globals().X4LAN_JOIN_CORE


@unittest.skipIf(LuaRuntime is None, 'lupa missing')
class Codec(unittest.TestCase):
    def test_every_op_matches_python(self):
        lua, K = runtime()
        cases = [({'op': 'save_at', 'slot': 3, 'tick': 600, 'mask': 3, 'nonce': '0123456789abcdef'},
                  dict(op=join.OP_SAVE_AT, slot=3, tick=600, mask=3, nonce=0x0123456789abcdef)),
                 ({'op': 'saved', 'slot': 3, 'tick': 600, 'bytes': 123456, 'package': 'ab'*32, 'portable': 'cd'*16},
                  dict(op=join.OP_SAVED, slot=3, tick=600, size=123456, package=b'\xab'*32, portable=b'\xcd'*16)),
                 ({'op': 'failed', 'slot': 2, 'tick': 70000, 'reason': 'not_possible'}, dict(op=join.OP_FAILED, slot=2, tick=70000, reason=2)),
                 ({'op': 'handover', 'slot': 3, 'tick': 4000000000}, dict(op=join.OP_HANDOVER, slot=3, tick=4000000000)),
                 ({'op': 'abort', 'slot': 4, 'tick': 1, 'reason': 'pause_too_long'}, dict(op=join.OP_ABORT, slot=4, tick=1, reason=13))]
        for record, python in cases:
            with self.subTest(op=record['op']):
                text = K.encode(lua.table_from(record))
                self.assertEqual(text, join.encode_record(**python))
                back = K.decode(text)
                self.assertEqual({k: back[k] for k in record}, record)
                ref = join.decode_record(text)
                self.assertEqual((ref['op'], ref['slot'], ref['tick']), (record['op'], record['slot'], record['tick']))

    def test_refusals_match_python(self):
        lua, K = runtime()
        good = join.encode_record(join.OP_FAILED, 3, 600, reason=2)
        for bad in (good[:-2], good+'00', '02'+good[2:], good[:2]+'09'+good[4:], good[:4]+'00'+good[6:], good[:-2]+'ee', 'zz'):
            with self.subTest(bad=bad):
                value, why = K.decode(bad)
                self.assertIsNone(value)
                with self.assertRaises(join.JoinError) as error:
                    join.decode_record(bad)
                self.assertEqual(why, str(error.exception))


@unittest.skipIf(LuaRuntime is None, 'lupa missing')
class Client(unittest.TestCase):
    def setUp(self):
        self.lua, self.K = runtime()
        self.lua.execute('''
          logs={};armed={};handed={};result='armed|0|0|0|00|00|0|-'
          function make(slot,joiner)
            return X4LAN_JOIN_CORE.client{slot=slot,joiner=joiner,log=function(t) logs[#logs+1]=t end,
              arm=function(t,k,mask) armed[#armed+1]=t..'/'..k..'/'..mask;return t~=999 end,
              result=function() return result end,handover=function(k,h) handed[#handed+1]=k..'@'..h end}
          end''')
        self.g = self.lua.globals()

    def test_the_client_registers_with_the_real_multiplexer(self):
        # j1-1 live: the probe's input_section('join', ...) refused the adapter ('invalid input section adapter join')
        # because a counter named 'refused' shadowed the multiplexer's optional refused(tick, reason) callback.
        self.lua.execute((ROOT/'tools/dev/native_gate_inputs_core.lua').read_text(encoding='utf-8'))
        self.lua.execute("M=X4LAN_INPUT_MUX_CORE.mux(function(t) logs[#logs+1]=t end);M.register('join','join',make(1))")
        self.assertIn('INPUT_SECTION ARMED section=join owner=join', list(self.g.logs.values()))

    def inputs(self, *blobs):
        return self.lua.table_from(list(blobs))

    def test_saver_arms_and_announces(self):
        c = self.g.make(1)
        save_at = join.encode_record(join.OP_SAVE_AT, 3, 600, mask=3, nonce=1)
        self.assertEqual(c.apply(560, self.inputs('', '', save_at), 3), 'PJOIN_1|3:save_at:3:600')
        self.assertEqual(list(self.g.armed.values()), ['600/3/3'])
        self.assertIsNone(c.proposal(600, 480))                       # nothing before the save tick has passed
        package, portable = 'ab'*32, 'cd'*16
        self.g.result = f'done|600|3|99999|{package}|{portable}|0|-'
        text = c.proposal(601, 480)
        self.assertEqual(join.decode_record(text), {'op': 'saved', 'slot': 3, 'tick': 600, 'bytes': 99999, 'package': package,
                                                     'portable': portable})
        self.assertIsNone(c.proposal(602, 480))                       # once

    def test_failed_save_and_late_or_refused_arming(self):
        c = self.g.make(2)
        c.apply(560, self.inputs('', '', join.encode_record(join.OP_SAVE_AT, 3, 600, mask=2, nonce=1)), 3)
        self.g.result = 'failed|600|3|0|00|00|2|is_saving_possible'
        self.assertEqual(join.decode_record(c.proposal(601, 480))['reason'], 'not_possible')
        late = self.g.make(2)
        late.apply(600, self.inputs('', '', join.encode_record(join.OP_SAVE_AT, 3, 601, mask=2, nonce=1)), 3)
        self.assertEqual(join.decode_record(late.proposal(600, 480))['reason'], 'not_armed')
        refused = self.g.make(2)
        refused.apply(500, self.inputs('', '', join.encode_record(join.OP_SAVE_AT, 3, 999, mask=2, nonce=1)), 3)
        self.assertEqual(join.decode_record(refused.proposal(500, 480))['reason'], 'not_armed')

    def test_not_in_the_mask_and_joiners_never_save(self):
        other = self.g.make(2)
        other.apply(560, self.inputs('', '', join.encode_record(join.OP_SAVE_AT, 3, 600, mask=1, nonce=1)), 3)
        joiner = self.g.make(3, True)
        joiner.apply(560, self.inputs('', '', join.encode_record(join.OP_SAVE_AT, 3, 600, mask=7, nonce=1)), 3)
        self.assertEqual(list(self.g.armed.values()), [])
        self.assertIsNone(other.proposal(601, 480))

    def test_records_outside_their_slot_are_refused(self):
        c = self.g.make(1)
        c.apply(560, self.inputs('', join.encode_record(join.OP_HANDOVER, 3, 700), ''), 3)       # control in slot 2's blob
        c.apply(561, self.inputs('', '', join.encode_record(join.OP_SAVED, 3, 600, size=1, package=b'1'*32, portable=b'2'*16)), 3)
        c.apply(562, self.inputs('', '', 'zz'), 3)
        self.assertEqual(c.refusals, 3)
        self.assertEqual(list(self.g.handed.values()), [])
        c.apply(563, self.inputs('', '', join.encode_record(join.OP_HANDOVER, 3, 700)), 3)
        self.assertEqual(list(self.g.handed.values()), ['3@700'])

    def test_handover_and_abort_reach_every_client(self):
        # The absent slot's pilot modules (gate.pilot_handover / gate.pilot_abort in the probe): every client, saver or
        # joiner, gets slot k's HANDOVER and ABORT at the tick it applies them; abort is optional for older adapters.
        self.lua.execute('''
          aborted={}
          function make_abort(slot,joiner)
            return X4LAN_JOIN_CORE.client{slot=slot,joiner=joiner,log=function(t) logs[#logs+1]=t end,
              arm=function() return true end,result=function() return result end,
              handover=function(k,h) handed[#handed+1]=k..'@'..h end,abort=function(k) aborted[#aborted+1]=k end}
          end''')
        for c in (self.g.make_abort(1), self.g.make_abort(3, True)):
            c.apply(700, self.inputs('', '', join.encode_record(join.OP_HANDOVER, 3, 712)), 3)
            c.apply(705, self.inputs('', '', join.encode_record(join.OP_ABORT, 3, 704, reason=join.REASON_CODES['joiner_lost'])), 3)
        self.assertEqual(list(self.g.handed.values()), ['3@712', '3@712'])
        self.assertEqual(list(self.g.aborted.values()), [3, 3])
        plain = self.g.make(2)                                          # no abort callback: logged only
        plain.apply(706, self.inputs('', '', join.encode_record(join.OP_ABORT, 3, 705, reason=join.REASON_CODES['joiner_lost'])), 3)
        self.assertEqual(list(self.g.aborted.values()), [3, 3])
        self.assertIn('JOIN_ABORT tick=706 k=3 reason=joiner_lost', list(self.g.logs.values()))

    def test_budget_defers_the_record(self):
        c = self.g.make(1)
        c.apply(560, self.inputs('', '', join.encode_record(join.OP_SAVE_AT, 3, 600, mask=1, nonce=1)), 3)
        self.g.result = f'done|600|3|5|{"ab"*32}|{"cd"*16}|0|-'
        self.assertIsNone(c.proposal(601, 20))
        self.assertEqual(len(c.proposal(602, 480)), 126)

    def test_state_providers(self):
        self.lua.execute('''
          a_value='x';b_value='y'
          X4LAN_JOIN_CORE.provide('beta',function() return 'b:'..b_value end,function(t) b_value=t end)
          X4LAN_JOIN_CORE.provide('alpha',function() return 'a:'..a_value end,function(t) a_value=t end)
          text=X4LAN_JOIN_CORE.state_text()
          X4LAN_JOIN_CORE.restore_text('alpha=1\\nbeta=2')''')
        self.assertEqual(self.g.text, 'alpha=a:x\nbeta=b:y')
        self.assertEqual((self.g.a_value, self.g.b_value), ('1', '2'))
        with self.assertRaises(LuaError):
            self.lua.execute("X4LAN_JOIN_CORE.restore_text('alpha=1')")             # a provider without its state
        with self.assertRaises(LuaError):
            self.lua.execute("X4LAN_JOIN_CORE.restore_text('alpha=1\\nbeta=2\\ngamma=3')")


    def test_unpaired_rows_are_logged(self):
        # gate.join_unpaired(i): the admission's samples (1-based, nil past the last); one JOIN_UNPAIRED line each.
        self.lua.execute('''
          lines={}
          rows={'md side=session name=1','ctx side=joiner class=0'}
          fake={join_unpaired=function(i) return rows[i] end}
          count=X4LAN_JOIN_CORE.log_unpaired(fake,function(text) lines[#lines+1]=text end)
          none=X4LAN_JOIN_CORE.log_unpaired({},function() error('no rows without the method') end)''')
        self.assertEqual(self.g.count, 2)
        self.assertEqual(self.g.none, 0)
        self.assertEqual([self.g.lines[1], self.g.lines[2]], ['JOIN_UNPAIRED md side=session name=1', 'JOIN_UNPAIRED ctx side=joiner class=0'])


if __name__ == '__main__':
    unittest.main()
