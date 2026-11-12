import tempfile
import re
import unittest
from pathlib import Path

from linux_player_network_evidence import (collect_network_physics_evidence, decode_record,
                                            parse_network_physics_evidence)
from test_linux_player_frames_evidence import valid_frames, worker_rows


def record(capture, events=()):
    data = bytearray(43)
    data[0] = 3
    data[2:4] = capture.to_bytes(2, 'little')
    data[42] = len(events)
    for event_id, value in events:
        data.extend((0x4000 | event_id).to_bytes(2, 'little'))
        data.extend(value.to_bytes(4, 'little'))
    return data.hex()


def network_fixture(strafe=False, vertical=False, throttle=False):
    p2 = [record(3, [(2, 1)]), record(4), record(5, [(3, 1), (2, 0), (3, 0)])]
    p1 = [record(3, [(5, 1)]), record(4), record(5, [(5, 0)])]
    if strafe:
        p2 = [record(3, [(6, 1)]), record(4), record(5, [(7, 1), (6, 0), (7, 0)])]
        p1 = [record(3, [(7, 1)]), record(4), record(5, [(7, 0)])]
    if vertical:
        p2 = [record(3, [(8, 1)]), record(4), record(5, [(9, 1), (8, 0), (9, 0)])]
        p1 = [record(3, [(9, 1)]), record(4), record(5, [(9, 0)])]
    held_bit, p1_bit = (64, 128) if vertical else ((16, 32) if strafe else (1, 8))
    if throttle:
        p2 = [record(3, [(13, 1)]), record(4), record(5, [(14, 1), (13, 0), (14, 0)])]
        p1 = [record(3, [(14, 1)]), record(4), record(5, [(14, 0)])]
        held_bit, p1_bit = 1 << 11, 1 << 12
    rows, tick, previous, after = [], 0, 0, 0
    for line in valid_frames(3):
        if 'physics=frame tick=' in line and 'event=begin' in line:
            tick = int(line.split('tick=')[1].split()[0])
            previous = [0, held_bit, held_bit][tick - 5]
            after = [held_bit, held_bit, 0][tick - 5]
        if 'input=press ' in line or ('physics=input ' in line and ' state=1 ' in line):
            for phase, value in (('before', previous), ('applied', after)):
                rows.append(f'X4LAN LIFECYCLE_DISCARD physics=network tick={tick} slot=2 stage={phase} '
                            f'held_before={previous} held_after={value} digital=0 record={p2[tick-5]}\n')
        elif 'input=release ' in line or ('physics=input ' in line and ' state=0 ' in line):
            rows.append(f'X4LAN LIFECYCLE_DISCARD physics=network tick={tick} slot=2 stage=updated '
                        f'held_before={previous} held_after={after} digital=0 record={p2[tick-5]}\n')
        else:
            rows.append(line.replace('input=state2 worker=', 'input=record worker='))
    stderr = worker_rows(3) + [f'X4LAN KEYBOARD_SLOT1 tick={tick} capture={tick-2} saved=0000000000000011 '
                               f'record={p1[tick-5]} held_before={before} held_after={after}\n'
                               for tick, before, after in ((5,0,p1_bit),(6,p1_bit,p1_bit),(7,p1_bit,0))]
    engine = [f'prefix LINUX_KEYBOARD_CAPTURE tick={i+3} record={row}\n' for i, row in enumerate(p2)]
    return rows, stderr, engine, p1, p2


class NetworkEvidenceTest(unittest.TestCase):
    def parse(self, rows, stderr, engine, slot=2, rc=223):
        return parse_network_physics_evidence(rows, stderr, engine, rc, frame_count=3, slot=slot)

    def test_valid_both_slots_and_wire_export(self):
        rows, stderr, engine, p1, p2 = network_fixture()
        for slot, records in ((1,p1),(2,p2)):
            own = [f'LINUX_KEYBOARD_CAPTURE tick={i+3} record={r}\n' for i,r in enumerate(records)]
            result = self.parse(rows, stderr, own, slot)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['slot_records'], {'1':p1, '2':p2})
            self.assertFalse(result['movement_proven'] or result['resumed'])

    def test_strafe_records_and_independent_held_bits(self):
        rows, stderr, _, p1, p2 = network_fixture(strafe=True)
        for slot, records in ((1, p1), (2, p2)):
            engine = [f'LINUX_KEYBOARD_CAPTURE tick={i+3} record={row}\n'
                      for i, row in enumerate(records)]
            result = self.parse(rows, stderr, engine, slot)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['slot_records'], {'1': p1, '2': p2})
        self.assertEqual(decode_record(record(3, [(6,1),(7,1)]), 5, 0), 48)
        self.assertEqual(decode_record(record(4), 6, 48), 48)
        self.assertEqual(decode_record(record(5, [(6,0),(7,0)]), 7, 48), 0)
        self.assertEqual(decode_record(record(3, [(6,1),(6,0),(6,1)]), 5, 0), 16)
        for bad in (record(3, [(10,1)]), record(3, [(6,2)]), record(3, [(0x3fff,1)])):
            self.assertRaises(ValueError, decode_record, bad, 5, 0)
        engine = [f'LINUX_KEYBOARD_CAPTURE tick={i+3} record={row}\n' for i, row in enumerate(p2)]
        self.assertFalse(self.parse([x.replace('held_after=16','held_after=1') for x in rows],
                                    stderr, engine)['pass'])
        self.assertFalse(self.parse(rows, [x.replace('held_after=32','held_after=8') for x in stderr],
                                    engine)['pass'])
        self.assertFalse(self.parse(list(reversed(rows)), stderr, engine)['pass'])

    def test_vertical_strafe_records_persist_and_release_distinct_bits(self):
        rows, stderr, _, p1, p2 = network_fixture(vertical=True)
        for slot, records in ((1, p1), (2, p2)):
            engine = [f'LINUX_KEYBOARD_CAPTURE tick={i+3} record={row}\n'
                      for i, row in enumerate(records)]
            result = self.parse(rows, stderr, engine, slot)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['slot_records'], {'1': p1, '2': p2})
        self.assertEqual(decode_record(record(3, [(8,1),(9,1)]), 5, 0), 192)
        self.assertEqual(decode_record(record(4), 6, 192), 192)
        self.assertEqual(decode_record(record(5, [(8,0),(9,0)]), 7, 192), 0)
        for bad in (record(3, [(10,1)]), record(3, [(9,2)]), record(3, [(0x3fff,1)])):
            self.assertRaises(ValueError, decode_record, bad, 5, 0)
        engine = [f'LINUX_KEYBOARD_CAPTURE tick={i+3} record={row}\n' for i, row in enumerate(p2)]
        self.assertFalse(self.parse([x.replace('held_after=64','held_after=16') for x in rows],
                                    stderr, engine)['pass'])
        self.assertFalse(self.parse(rows, [x.replace('held_after=128','held_after=32') for x in stderr],
                                    engine)['pass'])
        self.assertFalse(self.parse(rows[:-1], stderr, engine)['pass'])

    def test_reject_record_semantics(self):
        good = record(3, [(2,1),(2,0)])
        self.assertEqual(decode_record(good, 5, 0), 0)
        for bad in (good + 'AF', good[:-2], record(4), record(3, [(10,1)]), record(3, [(2,2)]),
                    good[:2] + '01' + good[4:], good[:8] + '01' + good[10:]):
            with self.assertRaises(ValueError): decode_record(bad, 5, 0)

    def test_network_throttle_persists_and_requires_real_release(self):
        rows, stderr, _, p1, p2 = network_fixture(throttle=True)
        for slot, records in ((1, p1), (2, p2)):
            engine = [f'LINUX_KEYBOARD_CAPTURE tick={index + 3} record={row}\n'
                      for index, row in enumerate(records)]
            result = self.parse(rows, stderr, engine, slot)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['slot_records'], {'1': p1, '2': p2})
        held = decode_record(record(3, [(13, 1), (14, 1)]), 5, 0)
        self.assertEqual(held, 0x1800)
        self.assertEqual(decode_record(record(4), 6, held), held)
        self.assertEqual(decode_record(record(5, [(13, 0), (14, 0)]), 7, held), 0)
        for event_id in (10, 11, 12, 15, 0x3fff):
            with self.subTest(event_id=event_id), self.assertRaises(ValueError):
                decode_record(record(3, [(event_id, 1)]), 5, 0)
        for mask in (-1, True, 1 << 8, 1 << 9, 1 << 10, 1 << 13):
            with self.subTest(mask=mask), self.assertRaises(ValueError):
                decode_record(record(3), 5, mask)
        for event_id in (13, 14):
            for value in (0, 1):
                with self.subTest(event_id=event_id, value=value), self.assertRaises(ValueError):
                    decode_record(record(3, [(event_id, value)]), 5, 0, scripted_throttle=True)
        engine = [f'LINUX_KEYBOARD_CAPTURE tick={index + 3} record={row}\n'
                  for index, row in enumerate(p2)]
        wrong_mask = [row.replace('held_after=2048', 'held_after=256') for row in rows]
        self.assertFalse(self.parse(wrong_mask, stderr, engine)['pass'])
        missing_release = [row.replace(p2[2], record(5)) for row in rows]
        self.assertFalse(self.parse(missing_release, stderr, engine)['pass'])

    def test_reject_missing_duplicate_order_crossslot_and_fatal(self):
        rows, stderr, engine, p1, p2 = network_fixture()
        self.assertTrue(self.parse(rows, stderr, engine)['pass'])
        for changed in (rows[:-1], rows + [rows[-1]],
                        [x.replace('stage=applied', 'stage=before') for x in rows],
                        [x.replace('slot=2', 'slot=1') for x in rows],
                        [x.replace('held_after=1', 'held_after=2') for x in rows]):
            self.assertFalse(self.parse(changed, stderr, engine)['pass'])
        for changed in (stderr[:-1], stderr + [stderr[-1]], stderr[:-2] + [stderr[-1], stderr[-2]],
                        [x.replace('held_after=0', 'held_after=8') for x in stderr],
                        stderr + ['X4LAN PLAYER_CONTROL_RECURRING_FATAL test\n']):
            self.assertFalse(self.parse(rows, changed, engine)['pass'])
        for changed in (engine[:-1], engine + [engine[-1]], list(reversed(engine)),
                        [engine[0].replace(p2[0], p1[0]), *engine[1:]],
                        engine + ['LINUX_KEYBOARD_CAPTURE invalid\n']):
            self.assertFalse(self.parse(rows, stderr, changed)['pass'])
        self.assertFalse(self.parse(rows, stderr, engine, slot=3)['pass'])
        self.assertFalse(self.parse(rows, stderr, engine, rc=0)['pass'])

    def test_collector_retains_raw_and_requires_output(self):
        rows, stderr, engine, _, _ = network_fixture()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, content in [('trace',rows),('stderr',stderr),('engine',engine),('output',[])]:
                (root/name).write_text(''.join(content))
            result = collect_network_physics_evidence(root/'trace', root/'retained', root/'engine',
                      root/'output', 223, frame_count=3, stderr=root/'stderr', slot=2)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual((root/'trace').read_bytes(), (root/'retained').read_bytes())
            self.assertFalse(collect_network_physics_evidence(root/'trace', root/'retained', root/'engine',
                      root/'output', 223, frame_count=3, stderr=root/'stderr', slot=2)['pass'])


def throttle_fixture(members=2, start=5):
    rows, stderr, _, p1, old_p2 = network_fixture()
    p2 = [record(3), record(4), record(5)]
    output = []
    tick = 0
    for line in rows:
        if 'physics=frame tick=' in line and 'event=begin' in line:
            tick = int(line.split('tick=')[1].split()[0])
        if 'physics=network ' in line:
            line = line.replace(old_p2[tick - 5], p2[tick - 5])
            line = re.sub(r'held_before=\d+ held_after=\d+', 'held_before=0 held_after=0', line)
        output.append(line)
        if tick in (start, start + 1):
            if 'physics=network ' in line and 'stage=applied' in line:
                phases = ('before', 'applied')
            elif 'update=pass calls=1' in line:
                phases = ('updated',)
            else:
                phases = ()
            for phase in phases:
                key = ('00' if phase == 'before' else '10') if tick == start else ('10' if phase == 'before' else '00')
                output.append(f'X4LAN LIFECYCLE_DISCARD throttle={phase} tick={tick} ship=aaa controller=aa '
                              f'collection=1234 members={members} keys={key} command=0.25 lower=-1 upper=1 original=0' +
                              (' table=111 factor68=1 factor70=2 factor74=3 factor78=4 controller_factor=5'
                               if start == 6 else '') + '\n')
                for index in range(members):
                    output.append(f'X4LAN LIFECYCLE_DISCARD throttle_member={phase} tick={tick} index={index} '
                                  f'object={0x400 + index:x} field328=00000000 field2f0=3f800000 field2f4=01\n')
    engine = [f'LINUX_KEYBOARD_CAPTURE tick={index+3} record={row}\n' for index, row in enumerate(p2)]
    return output, stderr, engine, p1, p2


class ThrottleEvidenceTest(unittest.TestCase):
    def parse(self, rows, stderr, engine, *, allow=True, slot=2):
        return parse_network_physics_evidence(rows, stderr, engine, 223, frame_count=3,
                                              slot=slot, allow_throttle=allow)

    def test_stages_and_empty_component_coverage(self):
        for count, coverage in ((0, 'empty'), (2, 'observed')):
            rows, stderr, engine, _, _ = throttle_fixture(count)
            result = self.parse(rows, stderr, engine)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['throttle']['component_coverage'], coverage)
            self.assertFalse(result['throttle']['movement_proven'])
            self.assertFalse(self.parse(rows, stderr, engine, allow=False)['pass'])
            self.assertFalse(self.parse(rows, stderr, engine, allow='yes')['pass'])

    def test_rejects_missing_reordered_malformed_and_identity_changes(self):
        rows, stderr, engine, _, _ = throttle_fixture()
        position = next(index for index, row in enumerate(rows) if 'throttle=before tick=5' in row)
        member = position + 1
        cases = [rows[:position] + rows[position+1:], rows[:member] + rows[member+1:],
                 rows[:member] + [rows[member]] + rows[member:],
                 rows[:position] + [rows[position+1], rows[position]] + rows[position+2:],
                 [row.replace('throttle=applied tick=5', 'throttle=before tick=5') for row in rows],
                 [row.replace('keys=10 command=', 'keys=00 command=', 1) for row in rows],
                 [row.replace('throttle=updated tick=6 ship=aaa', 'throttle=updated tick=6 ship=aab') for row in rows],
                 rows[:member+1] + [rows[member+1].replace('object=401', 'object=499')] + rows[member+2:],
                 [row.replace('command=0.25', 'command=1e999', 1) for row in rows],
                 [row.replace('field328=00000000', 'field328=7fc00000', 1) for row in rows],
                 [row.replace('members=2 keys=', 'members=65 keys=', 1) for row in rows],
                 [row.replace('index=1 object=', 'index=3 object=', 1) for row in rows],
                 rows[:member] + [rows[member].rstrip('\n')] + rows[member+1:]]
        for changed in cases:
            with self.subTest(case=cases.index(changed)):
                self.assertFalse(self.parse(changed, stderr, engine)['pass'])

    def test_cross_peer_values_without_cross_process_pointers(self):
        from linux_player_network_evidence import compare_throttle_peers
        rows, stderr, engine, _, _ = throttle_fixture()
        first = self.parse(rows, stderr, engine)
        other_rows = [row.replace('ship=aaa', 'ship=bbb').replace('controller=aa ', 'controller=bb ')
                      .replace('collection=1234', 'collection=5678').replace('object=400', 'object=500')
                      .replace('object=401', 'object=501') for row in rows]
        second = self.parse(other_rows, stderr, engine)
        self.assertTrue(first['pass'], first['errors'])
        self.assertTrue(second['pass'], second['errors'])
        self.assertEqual(compare_throttle_peers(first, second), [])
        second['throttle']['stages'][0]['members'][0]['field328'] = '3f800000'
        self.assertTrue(compare_throttle_peers(first, second))
        self.assertTrue(compare_throttle_peers({'pass': True, 'throttle': {}}, second))

    def test_warm_mode_requires_tick_six_and_factors(self):
        rows, stderr, engine, _, _ = throttle_fixture(start=6)
        result = parse_network_physics_evidence(rows, stderr, engine, 223, frame_count=3,
                    slot=2, allow_throttle=True, throttle_start_tick=6)
        self.assertTrue(result['pass'], result['errors'])
        self.assertFalse(self.parse(rows, stderr, engine)['pass'])
        for changed in ([row.replace(' factor68=1', ' factor68=1e999', 1) for row in rows],
                        [row.replace('table=111', 'table=222', 1) if 'throttle=updated tick=7 ' in row else row for row in rows],
                        [row.split(' table=')[0] + '\n' if 'throttle=before tick=6 ' in row else row
                         for row in rows]):
            self.assertFalse(parse_network_physics_evidence(changed, stderr, engine, 223,
                frame_count=3, slot=2, allow_throttle=True, throttle_start_tick=6)['pass'])

    def test_raw_collector_preserves_throttle_trace(self):
        rows, stderr, engine, _, _ = throttle_fixture()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, data in [('trace', rows), ('stderr', stderr), ('engine', engine), ('output', [])]:
                (root / name).write_text(''.join(data))
            result = collect_network_physics_evidence(root/'trace', root/'retained', root/'engine',
                root/'output', 223, frame_count=3, stderr=root/'stderr', slot=2, allow_throttle=True)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual((root/'trace').read_bytes(), (root/'retained').read_bytes())
