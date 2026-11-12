import tempfile
import unittest
from pathlib import Path

from linux_player_frames_evidence import (collect_recurring_physics_evidence, input_policy,
                                           parse_recurring_physics_evidence)
from test_linux_player_physics_evidence import P, body_row, snapshot, valid_trace


def sample(stage, getter, size, time):
    return (f'{P}physics=sample stage={stage} getter={getter} mode=1 size={size} '
            f'time={time} raw={"00" * size}\n')


def valid_frames(frame_count):
    lines = [f'{P}physics=frame tick=5 event=begin\n', *valid_trace()[:-1],
             f'{P}physics=frame tick=5 event=end\n']
    previous_digital = -1
    for tick in range(6, 5 + frame_count):
        input_id, keys, kind = input_policy(tick)
        now = tick + 2
        after_digital = -1 if input_id == 2 else 1 if input_id == 3 else 0
        lines.extend([f'{P}physics=frame tick={tick} event=begin\n',
                      snapshot('physics_update_before', vtable='cc'),
                      f'{P}physics=input tick={tick} id={input_id} state={int(input_id != 0)} '
                      f'keys={keys} digital={previous_digital}\n',
                      f'{P}stage=before_update tick={tick} now={now} previous={now - 1} last={now - 2} '
                      f'initializer={now - 1}\n',
                      f'{P}stage=after_update last={now}\n',
                      f'{P}update=pass calls=1 input={kind} worker=1 zone_matches=1 controller_identity=1 '
                      f'time={now}\n',
                      f'{P}physics=input tick={tick} id={input_id} state=0 keys=0000 digital={after_digital}\n',
                      snapshot('physics_update_after', vtable='cc')])
        lines.extend(sample('before', getter, size, now)
                     for getter, size in (('transform', 64), ('velocity', 32), ('acceleration', 32)))
        lines.extend(body_row('before', index) for index in range(2))
        lines.extend([f'{P}physics=ready generation={tick} main_parked=1\n',
                      snapshot('physics_upkeep_before', vtable='cc'),
                      f'{P}physics=upkeep stage=enter thread=42 controller=aa time={now}\n',
                      snapshot('physics_upkeep_after', vtable='cc'),
                      f'{P}physics=upkeep stage=leave thread=42 controller=aa time={now}\n',
                      snapshot('physics_end', vtable='cc')])
        lines.extend(body_row('after', index) for index in range(2))
        lines.extend(sample('after', getter, size, now)
                     for getter, size in (('transform', 64), ('velocity', 32), ('acceleration', 32)))
        lines.extend([snapshot('physics_sampled', vtable='cc'),
                      f'{P}physics=frame tick={tick} event=end\n'])
        previous_digital = after_digital
    lines.append(f'{P}result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n')
    return lines


def worker_rows(frame_count):
    rows = []
    for tick in range(5, 5 + frame_count):
        rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n')
        if tick != 4 + frame_count:
            rows.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release\n')
            rows.append(f'X4LAN PLAYER_CONTROL_RECURRING tick={tick} completed=1 readiness_only=0 '
                        'bank_writes=1 context_writes=unverified reference_calls=1 other_threads_excluded=0 tail=1\n')
    return rows


class RepeatedPhysicsEvidenceTest(unittest.TestCase):
    def parse(self, lines, frame_count=2, stderr=None, returncode=223):
        return parse_recurring_physics_evidence(lines, worker_rows(frame_count) if stderr is None else stderr,
                                                returncode, frame_count=frame_count)

    def test_two_frames_and_maximum_schedule(self):
        for count in (2, 31, 180):
            result = self.parse(valid_frames(count), count)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(len(result['frames']), count)
            self.assertFalse(result['movement_proven'])
            self.assertFalse(result['restored'])
            self.assertFalse(result['resumed'])
            self.assertFalse(result['other_threads_excluded'])
        for count in (1, 181, True):
            self.assertFalse(self.parse([], count)['pass'])

    def test_requires_real_first_frame_and_final(self):
        lines = valid_frames(2)
        for case in (lines[1:], lines[:-1], lines + [lines[-1]],
                     lines[:2] + lines[3:], lines[:-1] + [lines[-1].replace('frames_returned', 'frame_returned')]):
            self.assertFalse(self.parse(case)['pass'])
        self.assertFalse(self.parse(lines, returncode=0)['pass'])

    def test_worker_exact_enter_release_completion_and_final_enter(self):
        lines, rows = valid_frames(3), worker_rows(3)
        cases = [rows[:-1], rows + [rows[-1]], rows[:1] + rows[2:],
                 [row.replace('readiness_only=0', 'readiness_only=1') for row in rows],
                 [row.replace('context_writes=unverified', 'context_writes=0') for row in rows],
                 rows[:1] + [rows[2], rows[1]] + rows[3:], rows + ['X4LAN PLAYER_CONTROL_RECURRING_FATAL x\n']]
        for case in cases:
            self.assertFalse(self.parse(lines, 3, case)['pass'])

    def test_left_neutral_right_policy_and_carry(self):
        lines = valid_frames(31)
        self.assertTrue(self.parse(lines, 31)['pass'])
        cases = [('tick=15 id=0 state=0 keys=0000 digital=-1',
                  'tick=15 id=2 state=1 keys=1000 digital=-1'),
                 ('tick=16 id=0 state=0 keys=0000 digital=0',
                  'tick=16 id=0 state=0 keys=0000 digital=-1'),
                 ('tick=25 id=3 state=1 keys=0100 digital=0',
                  'tick=25 id=3 state=1 keys=1000 digital=0'),
                 ('tick=25 id=3 state=0 keys=0000 digital=1',
                  'tick=25 id=3 state=0 keys=0000 digital=1e999')]
        for before, after in cases:
            changed = [row.replace(before, after) for row in lines]
            self.assertNotEqual(changed, lines)
            self.assertFalse(self.parse(changed, 31)['pass'])

    def test_update_time_is_monotonic_without_assuming_prior_last(self):
        lines = valid_frames(2)
        result = self.parse(lines)
        self.assertTrue(result['pass'], result['errors'])
        # Frame 6 has last 6 while the preceding frame wrote 7: it remains valid.
        self.assertEqual(result['frames'][1]['update']['last'], 6)
        cases = [('tick=6 now=8 previous=7 last=6 initializer=7',
                  'tick=6 now=7 previous=6 last=5 initializer=6'),
                 ('tick=6 now=8 previous=7 last=6 initializer=7',
                  'tick=6 now=8 previous=7 last=8 initializer=7'),
                 ('stage=after_update last=8', 'stage=after_update last=7')]
        for before, after in cases:
            self.assertFalse(self.parse([row.replace(before, after) for row in lines])['pass'])

    def test_each_scope_is_restored_and_frame_bank_may_change(self):
        lines = valid_frames(2)
        frame6 = next(index for index, row in enumerate(lines) if 'tick=6 event=begin' in row)
        changed_bank = b'\x01' + bytes(995)
        for index in range(frame6, len(lines)):
            if 'snapshot=physics_' in lines[index]:
                label = lines[index].split('snapshot=')[1].split()[0]
                lines[index] = snapshot(label, 1, changed_bank, vtable='cc')
        self.assertTrue(self.parse(lines)['pass'])
        for target in ('physics_update_after', 'physics_upkeep_after', 'physics_sampled'):
            altered = lines.copy()
            index = next(i for i, row in enumerate(altered[frame6:], frame6) if 'snapshot=' + target in row)
            altered[index] = snapshot(target, 2, b'\x01\x02' + bytes(994), vtable='cc')
            self.assertFalse(self.parse(altered)['pass'], target)

    def test_body_links_and_contents_must_remain_valid_across_frames(self):
        lines = valid_frames(2)
        frame6 = next(index for index, row in enumerate(lines) if 'tick=6 event=begin' in row)
        for before, after in [('scene=301', 'scene=302'), ('runtime=123', 'runtime=124'),
                              ('position=1,2,3', 'position=1e999,2,3')]:
            altered = lines.copy()
            for index in range(frame6, len(altered)):
                if 'physics=body' in altered[index]:
                    altered[index] = altered[index].replace(before, after)
            self.assertFalse(self.parse(altered)['pass'])
        # Shared BodyManager capacity is not a per-body identity.
        changed = [row.replace('count=4', 'count=5') if i >= frame6 and 'physics=body' in row else row
                   for i, row in enumerate(lines)]
        self.assertTrue(self.parse(changed)['pass'])

    def test_oversized_numeric_fields_fail_without_crashing(self):
        rows = valid_frames(2)
        long_number = '9' * 5000
        cases = [rows[0].replace('tick=5', 'tick=' + long_number),
                 next(row for row in rows if 'snapshot=initial ' in row).replace('changed_bytes=0',
                                                                                  'changed_bytes=' + long_number),
                 next(row for row in rows if 'physics=body' in row).replace('runtime=123',
                                                                            'runtime=' + long_number)]
        for replacement in cases:
            altered = rows.copy()
            if 'event=begin' in replacement:
                index = 0
            elif 'snapshot=initial ' in replacement:
                index = next(i for i, row in enumerate(rows) if 'snapshot=initial ' in row)
            else:
                index = next(i for i, row in enumerate(rows) if 'physics=body' in row)
            altered[index] = replacement
            self.assertFalse(self.parse(altered)['pass'])

    def test_missing_body_is_no_coverage_across_repeated_frames(self):
        lines = valid_frames(2)
        for index, row in enumerate(lines):
            if 'physics=body' in row and 'index=1' in row:
                stage = 'before' if 'stage=before' in row else 'after'
                lines[index] = body_row(stage, 1, present=False)
        result = self.parse(lines)
        self.assertTrue(result['pass'], result['errors'])
        for frame in result['frames']:
            body = frame['body_comparisons'][1]
            self.assertFalse(body['present'])
            self.assertIsNone(body['position_changed'])
        self.assertFalse(result['movement_proven'])

    def test_retains_bytes_before_failure_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace, retained, engine, output, stderr = (root / name for name in
                                                       ('trace', 'retained', 'engine', 'output', 'stderr'))
            trace.write_text(''.join(valid_frames(2)))
            stderr.write_text(''.join(worker_rows(2)))
            engine.write_text('')
            output.write_text('')
            result = collect_recurring_physics_evidence(trace, retained, engine, output, 223,
                                                         frame_count=2, stderr=stderr)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(trace.read_bytes(), retained.read_bytes())
            self.assertFalse(collect_recurring_physics_evidence(trace, retained, engine, output, 223,
                                                                frame_count=2, stderr=stderr)['pass'])
            retained.unlink()
            trace.write_bytes(b'x' * (8 * 1024 * 1024 + 1))
            result = collect_recurring_physics_evidence(trace, retained, engine, output, 223,
                                                         frame_count=2, stderr=stderr)
            self.assertFalse(result['pass'])
            self.assertEqual(retained.stat().st_size, trace.stat().st_size)


if __name__ == '__main__':
    unittest.main()
