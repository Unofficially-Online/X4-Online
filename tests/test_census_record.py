"""Census record (native_gate_world_record, lockstep_world_hash.h world_record_write): the probe arms it beside the log and
writes one record after each published capture but the first; x4ls collects the files; tools/dev/census_record_diff.py names
the objects two clients' records disagree on. The gate's own rows and file are tested in test_lockstep_world_golden.c.
Synthetic (a mocked gate under LuaJIT, files written here), no X4."""
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import census_record_diff as diff  # noqa: E402
from lockstep.launcher import profile  # noqa: E402
import test_native_gate_world_spread as spread  # noqa: E402

HASH = {'t': '1' * 16, 'h': '2' * 16, 'm': '3' * 16, 'i': '4' * 16}


def row(saved, motion=HASH['m'], health=HASH['h'], parent='00000000000000aa', kind='02c51158'):
    return f"{saved:016x} {kind} 00000005 {parent} {HASH['t']} {health} {motion} {HASH['i']}"


def state(tick, motion_root='c' * 64):
    return f"WORLD_IMPORTED_2:{tick}:541640:{'a' * 64}:{'b' * 64}:{motion_root}:{'d' * 64}"


def write(folder, k, tick, rows, motion_root='c' * 64):
    folder.mkdir(parents=True, exist_ok=True)
    text = f'WORLD_RECORD_1 tick={tick} objects=541640 changed={len(rows)} rows={len(rows)} state={state(tick, motion_root)}\n'
    (folder / f'x4lan-census-{k}.txt').write_text(text + ''.join(r + '\n' for r in rows), encoding='ascii')


class DiffTests(unittest.TestCase):
    def test_the_first_differing_capture_names_every_object_and_its_domains(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / 'client1', Path(d) / 'client2'
            same = [row(0x10), row(0x11)]
            write(a, 0, 18000, same)
            write(b, 0, 18000, same)
            # 19800: 0x12 moved differently, 0x13 changed on client 1 only, 0x14 has another health and parent
            write(a, 1, 19800, same + [row(0x12, motion='5' * 16), row(0x13), row(0x14)], motion_root='e' * 64)
            write(b, 1, 19800, same + [row(0x12, motion='6' * 16), row(0x14, health='7' * 16, parent='00000000000000bb')])
            write(a, 2, 21600, same)
            write(b, 2, 21600, same)
            report = diff.compare(a, b)
            self.assertEqual([(e['tick'], e['equal'], e['exact']) for e in report], [(18000, True, False), (19800, False, True), (21600, True, False)])
            objects = {o[0]: o for o in report[1]['objects']}
            self.assertEqual(sorted(objects), ['0000000000000012', '0000000000000013', '0000000000000014'])
            self.assertEqual(objects['0000000000000012'][1:], ('both', '02c51158', '00000000000000aa', ['motion']))
            self.assertEqual(objects['0000000000000013'][1], 'a')
            self.assertEqual(objects['0000000000000014'][4], ['type/flags/parent', 'health'])
            self.assertEqual(report[1]['roots'], ['motion'])
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(diff.main([str(a), str(b)]), 0)
            self.assertIn('tick 19800: roots differ in motion; 3 objects, exact (the capture before agreed)', out.getvalue())
            self.assertIn('0000000000000013 type=02c51158 parent=00000000000000aa changed (a row on client A only)', out.getvalue())

    def test_a_split_without_an_agreeing_capture_before_it_is_a_lead(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / 'a', Path(d) / 'b'
            write(a, 0, 1800, [row(1, motion='5' * 16)], motion_root='e' * 64)
            write(b, 0, 1800, [row(1)])
            self.assertEqual([(e['equal'], e['exact']) for e in diff.compare(a, b)], [(False, False)])

    def test_a_damaged_record_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            a = Path(d) / 'a'
            write(a, 0, 1800, [row(1), row(2)])
            path = a / 'x4lan-census-0.txt'
            path.write_text(path.read_text().replace('rows=2', 'rows=3'), encoding='ascii')
            with self.assertRaises(ValueError):
                diff.records(a)
            path.write_text('WORLD_HASH tick=1\n', encoding='ascii')
            with self.assertRaises(ValueError):
                diff.records(a)


class CollectTests(unittest.TestCase):
    def test_collect_copies_the_census_records_with_the_log(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            p = root / str(profile.PROFILES[0])
            (p / 'save').mkdir(parents=True)
            (p / 'x4ls-c1.log').write_text('log')
            for k in range(3):
                (p / f'x4lan-census-{k}.txt').write_text(f'record {k}')
            out = root / 'run/client1'
            profile.collect(profile.PROFILES[0], 'x4ls-c1.log', out, root=root)
            self.assertEqual(sorted(f.name for f in out.iterdir()),
                             ['engine-output.log', 'x4lan-census-0.txt', 'x4lan-census-1.txt', 'x4lan-census-2.txt'])


RECORD_GATE = '''
  local gate=package.loadlib('gate')()
  record_calls={};record_writes={}
  gate.world_record=function(prefix,keep) record_calls[#record_calls+1]=prefix..'|'..keep;return 1 end
  gate.world_record_write=function(tick) record_writes[#record_writes+1]=tick
    if tick==20 then return nil,'file' end
    return 'keep=3 writes=1 failures=0 rows=12 bytes=1500 us=40 max_us=40 stage=written' end
'''


class ProbeTests(unittest.TestCase):
    def test_armed_beside_the_log_and_written_after_each_published_capture_but_the_first(self):
        for config in ('X4LAN_CONFIG.native_gate_world_hash_stride=10 X4LAN_CONFIG.native_gate_world_hash_spread=4',
                       'X4LAN_CONFIG.native_gate_world_hash_stride=10'):
            with self.subTest(config=config):
                lua, logs = spread.run_probe(range(1, 22), RECORD_GATE + config + ' X4LAN_CONFIG.native_gate_world_record=3')
                self.assertEqual(list(lua.globals().record_calls.values()), ['fake/45841/x4lan-census-|3'])
                self.assertEqual(list(lua.globals().record_writes.values()), [10, 20])
                self.assertTrue(any('WORLD_RECORD_ARMED keep=3 files=45841/x4lan-census-<k>.txt' in l for l in logs))
                self.assertTrue(any('NATIVE_GATE WORLD_RECORD tick=10 keep=3 writes=1' in l for l in logs))
                self.assertTrue(any('NATIVE_GATE WORLD_RECORD_FAILED tick=20 stage=file' in l for l in logs))   # a row, no stop
                self.assertEqual(lua.globals().state.issued, 21)

    def test_off_without_the_key_and_refused_with_a_bad_count(self):
        lua, _ = spread.run_probe(range(1, 12), RECORD_GATE + 'X4LAN_CONFIG.native_gate_world_hash_stride=10')
        self.assertEqual(list(lua.globals().record_writes.values()), [])
        for bad in ('0', '17', '2.5', "'8'"):
            with self.subTest(bad=bad), self.assertRaises(spread.probe_fixture.LuaError):
                spread.run_probe([1], RECORD_GATE + 'X4LAN_CONFIG.native_gate_world_hash_stride=10 X4LAN_CONFIG.native_gate_world_record=' + bad)
        with self.assertRaises(spread.probe_fixture.LuaError):
            spread.run_probe([1], RECORD_GATE + 'X4LAN_CONFIG.native_gate_world_record=8')   # no census stride


if __name__ == '__main__':
    unittest.main()
