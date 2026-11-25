"""RUNTIME_IDS_CALLER rows in 'delta' form (native_gate_runtime_ids_caller_rows, tools/dev/native_gate_probe.lua): the probe's own
blocks, extracted, under LuaJIT with a mocked gate; what they write is read back by census_runtime_callers.py. Synthetic, no X4."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8').replace('\r', '')
spec = importlib.util.spec_from_file_location('census_runtime_callers', ROOT/'tools/dev/census_runtime_callers.py')
census = importlib.util.module_from_spec(spec)
spec.loader.exec_module(census)


def _between(start, end):
    at = SOURCE.index(start)
    return SOURCE[at:SOURCE.index(end, at)]


SETUP = _between('-- RUNTIME_IDS_CALLER rows (native_gate_runtime_ids_caller_rows', 'function ticklog.write(text)')
TABLE = _between('  local every=cfg.native_gate_runtime_ids_caller_stride', '  if cfg.native_gate_runtime_ids_rows then')


def stack(count, frames):
    return f'phase=play thread=moveworker route=shared count={count} frames={frames}'


def run(mode, tables):
    """The lines written for each {tick: [row, ...]} in order, with native_gate_runtime_ids_caller_rows = mode."""
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute('cfg={};lines={};ticklog={};rows_now={}')
    if mode is not None:
        lua.globals().cfg.native_gate_runtime_ids_caller_rows = mode
    lua.execute('local cfg,ticklog=cfg,ticklog;' + SETUP)
    lua.execute('''
      gate={runtime_ids_caller_row=function(i) return rows_now[i+1] end}
      function ticklog.write(text) lines[#lines+1]=text end
      function run_table(tick)
        local fn=assert(loadstring('local cfg,gate,ticklog,tick,log=...;'..TABLE))
        fn(cfg,gate,ticklog,tick,function(text) lines[#lines+1]=text end)
      end
    ''')
    lua.globals().TABLE = TABLE
    for tick, rows in tables:
        lua.execute('rows_now={}')
        for i, text in enumerate(rows, start=1):
            lua.globals().rows_now[i] = text
        lua.globals().run_table(tick)
    return list(lua.globals().lines.values())


TABLES = [(300, [stack(5, 'a1'), stack(2, 'c3')]), (600, [stack(2, 'c3'), stack(7, 'a1')]), (900, [stack(7, 'a1'), stack(2, 'c3')]),
          (1200, [stack(8, 'a1'), stack(1, 'e5'), stack(3, 'c3')])]


class CallerRowTests(unittest.TestCase):
    def test_delta_writes_the_moved_stacks_and_a_marker_per_table(self):
        lines = run('delta', TABLES)
        self.assertEqual(lines, [
            'RUNTIME_IDS_CALLER tick=300 ' + stack(5, 'a1'), 'RUNTIME_IDS_CALLER tick=300 ' + stack(2, 'c3'),
            'RUNTIME_IDS_CALLERS tick=300 mode=delta rows=2 written=2',
            'RUNTIME_IDS_CALLER tick=600 ' + stack(7, 'a1'), 'RUNTIME_IDS_CALLERS tick=600 mode=delta rows=2 written=1',
            'RUNTIME_IDS_CALLERS tick=900 mode=delta rows=2 written=0',
            'RUNTIME_IDS_CALLER tick=1200 ' + stack(8, 'a1'), 'RUNTIME_IDS_CALLER tick=1200 ' + stack(1, 'e5'),
            'RUNTIME_IDS_CALLER tick=1200 ' + stack(3, 'c3'), 'RUNTIME_IDS_CALLERS tick=1200 mode=delta rows=3 written=3'])

    def test_full_and_absent_write_every_row_and_the_census_reads_both_alike(self):
        full = run('full', TABLES)
        self.assertEqual(run(None, TABLES), full)
        self.assertEqual(len(full), 9)
        self.assertFalse(any('RUNTIME_IDS_CALLERS ' in line for line in full))

        def read(lines):
            with tempfile.TemporaryDirectory() as folder:
                log = Path(folder)/'log.txt'
                log.write_text(''.join('[X4LAN] NATIVE_GATE ' + line + '\n' for line in lines))
                return census.read_table(log)
        self.assertEqual(read(run('delta', TABLES)), read(full))
        self.assertEqual(sorted(read(full)), [300, 600, 900, 1200])

    def test_an_unknown_mode_is_refused(self):
        with self.assertRaises(LuaError):
            run('sometimes', TABLES)


if __name__ == '__main__':
    unittest.main()
