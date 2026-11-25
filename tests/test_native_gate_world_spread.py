"""Spread census (native_gate_world_hash_spread, tools/dev/native_gate_probe.lua X4LAN_WORLD_SPREAD_PLAN and its world hash block;
src/native/engine/lockstep_world_hash.h world_spread_step). The probe under LuaJIT with a mocked gate, and the plan with a model of
the native parts: every client reads every item once per cycle at the same tick, and a hot join's session and joiner make the same
calls from the joiner's first frame on. The native parts themselves are tested in test_lockstep_world_golden.c (full capture = parts
of a still world, byte for byte). Synthetic, no X4."""
import re
import unittest

import test_native_gate_probe as probe_fixture
from lupa.luajit21 import LuaRuntime

SOURCE = probe_fixture.SOURCE
ROOTS = ':'.join(c * 64 for c in 'abcd')


def plan_function():
    """X4LAN_WORLD_SPREAD_PLAN alone, compiled in its own runtime (it reads no upvalue)."""
    match = re.search(r'\nfunction X4LAN_WORLD_SPREAD_PLAN\(.*?\nend\n', SOURCE, re.S)
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(match.group(0))
    fn = lua.globals().X4LAN_WORLD_SPREAD_PLAN

    def plan(tick, stride, k, opened, save=False):
        result = fn(tick, stride, k, opened, save)
        return None if result is None or result == (None, None, None) else tuple(result)
    return plan


class Client:
    """One client's spread cycles as the probe and world_spread_step run them: (tick, first, publish, first item, end item) per part."""
    def __init__(self, plan, stride, k, total, start=1, saves=()):
        self.plan, self.stride, self.k, self.total, self.tick, self.saves = plan, stride, k, total, start - 1, set(saves)
        self.opened, self.parts, self.published, self.next = False, [], [], 0

    def step(self):
        self.tick += 1
        planned = self.plan(self.tick, self.stride, self.k, self.opened, self.tick in self.saves)
        if planned is None:
            return
        first, publish, finish = planned
        if self.tick == first:
            self.next = 0
        end = self.total if finish or self.tick == publish else self.total * (self.tick - first + 1) // (publish - first + 1)
        self.parts.append((self.tick, first, publish, self.next, end))
        self.next = end
        self.opened = end < self.total
        if end == self.total:
            self.published.append(self.tick)


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.plan = plan_function()

    def test_every_item_is_read_once_per_cycle_and_published_at_the_stride_ticks(self):
        for stride, k, total in ((1800, 900, 553036), (10, 4, 13), (10, 10, 13), (300, 900, 1000), (7, 3, 2)):
            with self.subTest(stride=stride, k=k):
                k = min(k, stride)
                client = Client(self.plan, stride, k, total)
                for _ in range(stride * 4 + 3):
                    client.step()
                self.assertEqual(client.published, [stride * c for c in range(1, 5)])
                for c in range(1, 5):
                    parts = [p for p in client.parts if p[2] == stride * c]
                    first = max(2, stride * c - k + 1)
                    self.assertEqual([p[0] for p in parts], list(range(first, stride * c + 1)))
                    self.assertEqual(parts[0][3], 0)
                    self.assertEqual(parts[-1][4], total)
                    for a, b in zip(parts, parts[1:]):
                        self.assertEqual(a[4], b[3])   # contiguous: no item twice, none left out
                    # the parts are even: none holds more than its share plus one
                    self.assertLessEqual(max(p[4] - p[3] for p in parts), -(-total // len(parts)) + 1)

    def test_no_part_before_tick_2_or_without_a_k(self):
        self.assertIsNone(self.plan(1, 10, 10, False))
        self.assertIsNone(self.plan(5, 10, None, False))
        self.assertEqual(self.plan(2, 10, 10, False), (2, 10, False))
        self.assertIsNone(self.plan(3, 10, 4, False))       # before the cycle's first tick 7
        self.assertIsNone(self.plan(8, 10, 4, False))       # inside a cycle this client did not open
        self.assertEqual(self.plan(8, 10, 4, True), (7, 10, False))
        self.assertEqual(self.plan(8, 10, 4, True, True), (7, 10, True))

    def test_a_hot_join_session_and_joiner_make_the_same_parts_from_the_joiners_first_frame(self):
        stride, k, total = 30, 12, 100
        for save in range(2, 95):
            with self.subTest(save=save):
                session = Client(self.plan, stride, k, total, saves=(save,))
                for _ in range(save):
                    session.step()
                at_save = list(session.published)
                # A cycle open at the save tick is published at it: the package carries a complete state.
                self.assertFalse(session.opened)
                cycle_open_before = any(p[0] == save for p in session.parts)
                if cycle_open_before:
                    self.assertEqual(at_save[-1], save)
                joiner = Client(self.plan, stride, k, total, start=save + 1)
                for _ in range(120):
                    session.step()
                    joiner.step()
                after = lambda parts: [p for p in parts if p[0] > save]
                self.assertEqual(after(session.parts), joiner.parts)
                self.assertEqual([t for t in session.published if t > save], joiner.published)


def run_probe(ticks, config, spread_result=None):
    fixture = probe_fixture.SnapshotProbeTests()
    fixture.setUp()
    lua = fixture.lua
    lua.globals().ROOTS = ROOTS
    lua.execute('''
      X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'
      local gate=package.loadlib('gate')()
      full_calls={};spread_calls={}
      local function summary(tick) return 'tick='..tick..' objects=13 present=12 changed=0 unknown_class=0 unknown_motion=0 '..
        'unknown_references=0 state=WORLD_IMPORTED_2:'..tick..':13:'..ROOTS end
      gate.world_capture_nav_v2=function(tick) full_calls[#full_calls+1]=tick;return summary(tick) end
      gate.world_capture_spread=function(tick,first,publish,finish,schema)
        spread_calls[#spread_calls+1]=table.concat({tick,first,publish,finish,schema},',')
        if finish==1 or tick==publish then return summary(tick) end
        return 0
      end
      gate.world_spread_status=function() return 'cycles=1' end
      gate.world_banks=function() return 'tick=0 read_before=0 write_before=1 read_after=0 write_after=1' end
      gate.world_page=function(n) return 0 end
      gate.world_detail=function(n) return 0 end
      state.boot_phase=3;is_menu=false
      digest_inputs={}
      X4LAN_SHA256.digest=function(data) digest_inputs[#digest_inputs+1]=data;return string.rep('a',64) end
    ''')
    lua.execute(config)
    lua.execute(SOURCE)
    for tick in ticks:
        lua.globals().state.issued = tick
        lua.execute('update()')
    return lua, list(lua.globals().logs.values())


class ProbeTests(unittest.TestCase):
    def test_parts_in_the_ticks_before_each_stride_tick_and_the_state_published_there(self):
        lua, logs = run_probe(range(1, 22), 'X4LAN_CONFIG.native_gate_world_hash_stride=10 X4LAN_CONFIG.native_gate_world_hash_spread=4')
        self.assertEqual(list(lua.globals().full_calls.values()), [1])
        calls = list(lua.globals().spread_calls.values())
        self.assertEqual(calls, [f'{t},7,10,0,2' for t in range(7, 11)] + [f'{t},17,20,0,2' for t in range(17, 21)])
        hashes = [line for line in logs if 'NATIVE_GATE WORLD_HASH ' in line]
        self.assertEqual([re.search(r'WORLD_HASH tick=(\d+)', line).group(1) for line in hashes], ['1', '10', '20'])
        self.assertEqual(len([line for line in logs if 'NATIVE_GATE WORLD_HASH_SPREAD tick=' in line]), 2)
        digests = [data for data in lua.globals().digest_inputs.values() if '|WORLD_IMPORTED_2:' in data]   # the per-tick declared states
        self.assertEqual(len(digests), 21)
        for tick, data in enumerate(digests, start=1):
            published = 1 if tick < 10 else 10 if tick < 20 else 20
            self.assertTrue(data.endswith(f'|WORLD_IMPORTED_2:{published}:13:{ROOTS}'), (tick, data[-90:]))

    def test_k_is_at_most_the_stride_and_below_two_full_captures_stay(self):
        lua, _ = run_probe(range(1, 12), 'X4LAN_CONFIG.native_gate_world_hash_stride=5 X4LAN_CONFIG.native_gate_world_hash_spread=900')
        self.assertEqual(list(lua.globals().full_calls.values()), [1])
        self.assertEqual([c.split(',')[0] for c in lua.globals().spread_calls.values()], [str(t) for t in range(2, 12)])   # 11: the cycle 11..15
        lua, _ = run_probe(range(1, 5), 'X4LAN_CONFIG.native_gate_world_hash_stride=1 X4LAN_CONFIG.native_gate_world_hash_spread=900')
        self.assertEqual(list(lua.globals().full_calls.values()), [1, 2, 3, 4])
        self.assertEqual(list(lua.globals().spread_calls.values()), [])

    def test_without_the_key_every_stride_tick_is_a_full_capture(self):
        lua, _ = run_probe(range(1, 22), 'X4LAN_CONFIG.native_gate_world_hash_stride=10')
        self.assertEqual(list(lua.globals().full_calls.values()), [1, 10, 20])
        self.assertEqual(list(lua.globals().spread_calls.values()), [])

    def test_refused_beside_a_census_of_one_tick_or_without_a_stride(self):
        for extra in ('X4LAN_CONFIG.native_gate_world_pages_window={10,20}', 'X4LAN_CONFIG.native_gate_world_hash_join=true',
                      'X4LAN_CONFIG.native_gate_world_nav_stride=5', 'X4LAN_CONFIG.native_gate_world_hash_stride=nil',
                      'X4LAN_CONFIG.native_gate_world_hash_spread=1', "X4LAN_CONFIG.native_gate_world_hash_spread='900'"):
            with self.subTest(extra=extra), self.assertRaises(probe_fixture.LuaError):
                run_probe([1], 'X4LAN_CONFIG.native_gate_world_hash_stride=10 X4LAN_CONFIG.native_gate_world_hash_spread=4 ' + extra)


if __name__ == '__main__':
    unittest.main()
