import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_runtime_callers', ROOT/'tools/dev/census_runtime_callers.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(tick, count, route='shared', frames='a1,b2', phase='play', thread='moveworker'):
    return f'[X4LAN] NATIVE_GATE RUNTIME_IDS_CALLER tick={tick} phase={phase} thread={thread} route={route} count={count} frames={frames}\n'


def table(*rows):
    with tempfile.TemporaryDirectory() as folder:
        log = Path(folder)/'log.txt'
        log.write_text(''.join(rows))
        return module.read_table(log)


class FirstDifferenceTests(unittest.TestCase):
    def test_names_the_tick_and_the_stack_that_parted(self):
        left = table(row(133, 5), row(134, 5), row(135, 6))
        right = table(row(133, 5), row(134, 6), row(135, 7))
        tick, agreed, rows = module.first_difference(left, right)
        self.assertEqual((tick, agreed), (134, 133))
        self.assertEqual(rows, [(-1, ('play', 'moveworker', 'shared', 'a1,b2'), 5, 6)])

    def test_the_view_lists_differ_by_design(self):
        left = table(row(10, 5), row(10, 40, route='ui', frames='c3'), row(10, 7, route='view', frames='d4'))
        right = table(row(10, 5), row(10, 90, route='ui', frames='c3'), row(10, 9, route='view', frames='d4'))
        self.assertEqual(module.first_difference(left, right), (None, 10, []))

    def test_cue_instances_of_different_cues_are_different_rows(self):
        stack = 'caab18,1111aaa,111c477,111e50a,112383d,b58ca9,111f09d,112fed3'
        left = table(row(10, 4, frames=stack + ' cue=PlayerShipHullDamaged'), row(10, 2, frames=stack + ' cue=Other'))
        right = table(row(10, 4, frames=stack + ' cue=PlayerShipHullDamaged'), row(10, 3, frames=stack + ' cue=Other'))
        tick, agreed, rows = module.first_difference(left, right)
        self.assertEqual(tick, 10)
        self.assertEqual([(r[0], r[1][3]) for r in rows], [(-1, stack + ' cue=Other')])

    def test_an_allocation_local_on_one_client_only_shows(self):
        left = table(row(10, 3, route='ui', frames='c3'))
        right = table(row(10, 3, route='shared', frames='c3'))
        tick, agreed, rows = module.first_difference(left, right)
        self.assertEqual((tick, agreed), (10, None))
        self.assertEqual(rows, [(-3, ('play', 'moveworker', 'shared', 'c3'), 0, 3)])

    def test_load_rows_do_not_count_and_only_common_ticks_compare(self):
        left = table(row(1, 5, phase='load'), row(20, 5), row(30, 9))
        right = table(row(1, 6, phase='load'), row(20, 5))
        self.assertEqual(module.first_difference(left, right), (None, 20, []))


if __name__ == '__main__':
    unittest.main()


def tick_row(tick, next_id, keys, count=None, other=''):
    total = count if count is not None else sum(int(item.split(':')[1]) for item in keys.split(',') if item)
    return (f'[X4LAN] NATIVE_GATE RUNTIME_IDS_ROW index=0 tick={tick} next={next_id} count={total} first=10001 last=10003 '
            f'threads={total}/0/0 seq=0000000000000001 sum=0000000000000002 keys={keys}{other}\n')


def rows_of(*lines):
    with tempfile.TemporaryDirectory() as folder:
        log = Path(folder)/'log.txt'
        log.write_text(''.join(lines))
        return module.read_rows(log)


class RowTests(unittest.TestCase):
    def test_an_allocation_a_tick_early_names_its_tick_and_key(self):
        stack = row(12690, 3, frames='116ea00,41a7f6').rstrip('\n') + ' key=0000abcd\n'
        left, keys = rows_of(tick_row(12688, '00000000001a8c83', '0000abcd:1,00001111:2'), tick_row(12689, '00000000001a8c90', '00001111:1'),
                             tick_row(12690, '00000000001a8c95', '0000abcd:1,00001111:1'), stack)
        right, _ = rows_of(tick_row(12688, '00000000001a8c83', '0000abcd:1,00001111:2'), tick_row(12689, '00000000001a8c91', '0000abcd:1,00001111:1'),
                           tick_row(12690, '00000000001a8c95', '00001111:1'))
        self.assertEqual(keys, {'0000abcd': 'play moveworker shared 116ea00,41a7f6'})
        tick, a, b, differ = module.first_row_difference(left, right)
        self.assertEqual(tick, 12689)
        self.assertEqual((a[0], b[0]), ('00000000001a8c90', '00000000001a8c91'))
        self.assertEqual(differ, [('0000abcd', 0, 1)])
        self.assertEqual(module.first_row_difference(left, right, 12691), None)

    def test_a_row_on_one_side_only_and_the_overflow_count(self):
        left, _ = rows_of(tick_row(5, '01', '00000001:1'), tick_row(7, '03', '00000002:1', count=40, other=' keys_other=39'))
        right, _ = rows_of(tick_row(5, '01', '00000001:1'), tick_row(6, '02', '00000003:1'), tick_row(7, '03', '00000002:1', count=40, other=' keys_other=39'))
        self.assertEqual(left[7][4], 39)
        tick, a, b, differ = module.first_row_difference(left, right)
        self.assertEqual((tick, a, differ), (6, None, [('00000003', 0, 1)]))
        self.assertEqual(module.first_row_difference(left, left), None)
