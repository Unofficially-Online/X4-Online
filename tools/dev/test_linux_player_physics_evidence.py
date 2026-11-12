import tempfile
import unittest
from pathlib import Path

from linux_player_physics_evidence import collect_physics_evidence, parse_physics_evidence

P = 'X4LAN LIFECYCLE_DISCARD '


def snapshot(label, changed=0, bank=None, zone_count=12, controller='aa', vtable='bb'):
    if bank is None:
        bank = bytes(996)
    return (f'{P}snapshot={label} changed_bytes={changed} bank={bank.hex()} zone=abc zone_count={zone_count} '
            f'animation_a=0 animation_b=0 controller={controller} controller_vtable={vtable} '
            f'contexts_ok=1 contexts_valid=1 contexts_nodes=13 contexts_mask=7 contexts_payloads={"00" * 48}\n')


def body_row(stage, index, *, present=True, moving=True, count=4, position='1,2,3',
             rotation='0,0,0,1', linear='4,5,6', angular='7,8,9'):
    if present:
        values = (f'record={0x101 + index:x} xbody={0x201 + index:x} scene=301 system=401 '
                  f'interface=501 manager=601 array=701 body={0x800 + index * 2:x} '
                  f'motion={0x901 + index if moving else 0:x} count={count} id={index + 1} '
                  f'motion_present={int(moving)} position={position} rotation={rotation} '
                  f'linear={linear if moving else "0,0,0"} angular={angular if moving else "0,0,0"}')
    else:
        values = ('record=0 xbody=0 scene=0 system=0 interface=0 manager=0 array=0 body=0 '
                  'motion=0 count=0 id=0 motion_present=0 position=0,0,0 rotation=0,0,0,0 '
                  'linear=0,0,0 angular=0,0,0')
    return (f'{P}physics=body stage={stage} index={index} ship=aaa vtable=bbb parent=ccc '
            f'runtime=123 present={int(present)} {values}\n')


def valid_trace():
    changed_bank = b'\x01' + bytes(995)
    lines = [f'{P}stage=entered tick=5 thread=42\n', snapshot('initial', zone_count=11),
             f'{P}stage=before_singleton tick=5 thread=42\n', f'{P}stage=after_singleton tick=5 thread=42\n',
             snapshot('prepared', 1, changed_bank), f'{P}stage=before_take tick=5 thread=42\n',
             f'{P}stage=after_take tick=5 thread=42\n', snapshot('taken', 1, changed_bank),
             f'{P}input=press id=2 state=1 keys=1000 digital=0\n',
             f'{P}stage=before_update tick=5 now=7 previous=6 last=-1 initializer=6\n',
             f'{P}stage=after_update last=7\n',
             f'{P}update=pass calls=1 input=state2 worker=1 zone_matches=1 controller_identity=1 time=7\n',
             f'{P}input=release id=2 state=0 keys=0000 digital=-1\n',
             snapshot('physics_updated', 1, changed_bank), snapshot('physics_canonical', vtable='cc')]
    for stage in ('before', 'after'):
        if stage == 'after':
            lines.append(f'{P}physics=ready generation=5 main_parked=1\n')
            lines.append(snapshot('physics_upkeep_before', vtable='cc'))
            lines.append(f'{P}physics=upkeep stage=enter thread=42 controller=aa time=7\n')
            lines.append(snapshot('physics_upkeep_after', vtable='cc'))
            lines.append(f'{P}physics=upkeep stage=leave thread=42 controller=aa time=7\n')
            lines.append(snapshot('physics_end', vtable='cc'))
            lines.extend(body_row(stage, index) for index in range(2))
        for getter, size in (('transform', 64), ('velocity', 32), ('acceleration', 32)):
            lines.append(f'{P}physics=sample stage={stage} getter={getter} mode=1 size={size} time=7 raw={"00" * size}\n')
        if stage == 'before':
            lines.extend(body_row(stage, index) for index in range(2))
        else:
            lines.append(snapshot('physics_sampled', vtable='cc'))
    lines.append(f'{P}result=pass reason=physics_frame_returned exit=223 restored=0 resumed=0\n')
    return lines


class PhysicsEvidenceTest(unittest.TestCase):
    def test_body_records_reject_missing_duplicate_reordered_or_malformed(self):
        rows = valid_trace()
        body_indices = [i for i, row in enumerate(rows) if 'physics=body' in row]
        first, second, third, fourth = body_indices
        cases = [rows[:first] + rows[first + 1:], rows[:first] + [rows[first]] + rows[first:],
                 rows[:first] + [rows[second], rows[first]] + rows[second + 1:],
                 rows[:fourth] + [rows[fourth].rstrip('\n')] + rows[fourth + 1:]]
        for case in cases:
            self.assertFalse(parse_physics_evidence(case, 223)['pass'])
        for old, new in [('position=1,2,3', 'position=1,2'),
                         ('rotation=0,0,0,1', 'rotation=0,0,0,nan'),
                         ('position=1,2,3', 'position=1e999,2,3'),
                         ('id=1', 'id=4294967295'),
                         ('motion_present=1', 'motion_present=0')]:
            case = rows.copy()
            case[first] = case[first].replace(old, new)
            self.assertFalse(parse_physics_evidence(case, 223)['pass'], (old, new))

    def test_body_identity_stability_and_capacity_changes(self):
        rows = valid_trace()
        after = next(i for i, row in enumerate(rows) if 'stage=after index=0' in row)
        for old, new in [('ship=aaa', 'ship=aab'), ('parent=ccc', 'parent=ccd'),
                         ('record=101', 'record=102'), ('scene=301', 'scene=302'),
                         ('manager=601', 'manager=602'), ('body=800', 'body=802'),
                         ('motion=901', 'motion=902'), ('id=1', 'id=2')]:
            case = rows.copy()
            case[after] = case[after].replace(old, new)
            self.assertFalse(parse_physics_evidence(case, 223)['pass'], (old, new))
        rows[after] = rows[after].replace('count=4', 'count=5')
        self.assertTrue(parse_physics_evidence(rows, 223)['pass'])

    def test_missing_and_static_bodies_are_explicit_no_coverage(self):
        rows = valid_trace()
        for i, row in enumerate(rows):
            if 'physics=body' in row:
                stage = 'before' if 'stage=before' in row else 'after'
                index = 0 if 'index=0' in row else 1
                rows[i] = body_row(stage, index, present=index == 1, moving=False)
        result = parse_physics_evidence(rows, 223)
        self.assertTrue(result['pass'], result['errors'])
        missing, static = result['body_comparisons']
        self.assertFalse(missing['present'])
        self.assertIsNone(missing['position_changed'])
        self.assertTrue(static['present'])
        self.assertFalse(static['motion_present'])
        self.assertIsNone(static['linear_changed'])
        self.assertFalse(result['movement_proven'])

    def test_body_numeric_comparison_treats_quaternion_sign_as_equivalent(self):
        rows = valid_trace()
        for i, row in enumerate(rows):
            if 'physics=body stage=after index=0' in row:
                rows[i] = row.replace('position=1,2,3', 'position=1,2,4').replace(
                    'rotation=0,0,0,1', 'rotation=0,0,0,-1').replace('linear=4,5,6', 'linear=4,5,7')
        result = parse_physics_evidence(rows, 223)
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['body_comparisons'][0]['position_changed'])
        self.assertFalse(result['body_comparisons'][0]['orientation_changed'])
        self.assertTrue(result['body_comparisons'][0]['linear_changed'])
        self.assertFalse(result['movement_proven'])

    def test_scope_brackets_allow_frame_changes_but_reject_scope_changes(self):
        rows = valid_trace()
        frame_bank = b'\x01' + bytes(995)
        for label in ('physics_upkeep_before', 'physics_upkeep_after', 'physics_end', 'physics_sampled'):
            index = next(i for i, row in enumerate(rows) if f'snapshot={label} ' in row)
            rows[index] = snapshot(label, 1, frame_bank, vtable='cc')
        result = parse_physics_evidence(rows, 223)
        self.assertTrue(result['pass'], result['errors'])
        for label in ('physics_upkeep_after', 'physics_sampled'):
            altered = rows.copy()
            index = next(i for i, row in enumerate(altered) if f'snapshot={label} ' in row)
            altered[index] = snapshot(label, 2, b'\x01\x02' + bytes(994), vtable='cc')
            result = parse_physics_evidence(altered, 223)
            self.assertFalse(result['pass'])
            self.assertTrue(any('canonical state changed within' in error for error in result['errors']))

    def test_matching_bracket_contexts_must_still_be_canonical(self):
        rows = valid_trace()
        for index, row in enumerate(rows):
            if any(f'snapshot={label} ' in row for label in ('physics_end', 'physics_sampled')):
                rows[index] = row.replace('contexts_nodes=13', 'contexts_nodes=14')
        result = parse_physics_evidence(rows, 223)
        self.assertFalse(result['pass'])
        self.assertTrue(any('canonical contexts changed:' in error for error in result['errors']))

    def test_older_evidence_without_new_markers_cannot_pass(self):
        rows = [row for row in valid_trace() if not any(
            f'snapshot={label} ' in row for label in
            ('physics_upkeep_before', 'physics_upkeep_after', 'physics_sampled'))]
        self.assertFalse(parse_physics_evidence(rows, 223)['pass'])

    def test_reject_missing_or_mismatched_native_upkeep(self):
        rows = valid_trace()
        mutations = [lambda line: '' if 'physics=upkeep' in line else line,
                     lambda line: line.replace('thread=42', 'thread=43') if 'physics=upkeep' in line else line,
                     lambda line: line.replace('controller=aa', 'controller=bb') if 'physics=upkeep' in line else line,
                     lambda line: line.replace('time=7', 'time=8') if 'physics=upkeep stage=leave' in line else line]
        for change in mutations:
            self.assertFalse(parse_physics_evidence([change(row) for row in rows], 223)['pass'])

    def test_reject_impossible_initializer_and_overflowing_elapsed(self):
        rows = valid_trace()
        for before, after in [('initializer=6', 'initializer=123'),
                              ('now=7 previous=6 last=-1 initializer=6',
                               'now=1e308 previous=-1e308 last=-1 initializer=0')]:
            changed = [line.replace(before, after) for line in rows]
            result = parse_physics_evidence(changed, 223)
            self.assertFalse(result['pass'])
            self.assertIn('invalid update admission times', result['errors'])

    def test_reject_detached_sample_times_but_do_not_require_movement(self):
        rows = valid_trace()
        self.assertTrue(parse_physics_evidence(rows, 223)['pass'])
        for stage, time in [('before', '8'), ('before', '6'), ('after', '6')]:
            changed = [line.replace('time=7', 'time=' + time) if f'stage={stage} getter=' in line
                       else line for line in rows]
            result = parse_physics_evidence(changed, 223)
            self.assertFalse(result['pass'])
            self.assertIn(stage + ' motion sample time contradicts the admitted update', result['errors'])

    def test_reject_controller_or_vtable_swap_hidden_between_matching_endpoints(self):
        rows = valid_trace()
        for before, after in [('controller=aa', 'controller=dead'),
                              ('controller_vtable=bb', 'controller_vtable=dead')]:
            changed = [line.replace(before, after) if 'snapshot=physics_updated ' in line else line
                       for line in rows]
            result = parse_physics_evidence(changed, 223)
            self.assertFalse(result['pass'])
            self.assertIn('P2 controller identity changed between take and updated snapshot', result['errors'])

    def test_bounded_acceptance_does_not_claim_movement(self):
        result = parse_physics_evidence(valid_trace(), 223)
        self.assertEqual(result['errors'], [])
        self.assertTrue(result['pass'])
        self.assertFalse(result['movement_proven'])
        self.assertFalse(result['resumed'])
        self.assertEqual(len(result['samples']), 6)

    def test_missing_duplicate_extra_and_reordered_records(self):
        rows = valid_trace()
        cases = [rows[:15] + rows[16:], rows[:18] + [rows[17]] + rows[18:],
                 rows[:4] + [f'{P}stage=surprise tick=5 thread=42\n'] + rows[4:],
                 rows[:15] + [rows[16], rows[15]] + rows[17:], rows[:-1], rows + [rows[-1]]]
        for case in cases:
            with self.subTest(case=case[0:2]):
                self.assertFalse(parse_physics_evidence(case, 223)['pass'])

    def test_reject_semantic_lies(self):
        rows = valid_trace()
        changes = [(14, 'zone_count=12', 'zone_count=13'),
                   (14, 'controller_vtable=cc', 'controller_vtable=bb'),
                   (9, 'previous=6', 'previous=7'), (10, 'last=7', 'last=6'),
                   (8, 'keys=1000', 'keys=0000'), (15, 'size=64', 'size=32'),
                   (15, 'time=7', 'time=1e999'), (4, 'changed_bytes=1', 'changed_bytes=2'),
                   (2, 'thread=42', 'thread=43')]
        for index, old, new in changes:
            changed = rows.copy(); changed[index] = changed[index].replace(old, new)
            with self.subTest(index=index, replacement=new):
                self.assertFalse(parse_physics_evidence(changed, 223)['pass'])
        self.assertFalse(parse_physics_evidence(rows, 0)['pass'])

    def test_callback_reports_actual_slot_and_addresses(self):
        rows = valid_trace()[:15] + [f'{P}physics=callback slot=43 thread=99 controller=123 expected=aa\n',
            f'{P}result=fail reason=physics_callback_43 exit=223 restored=0 resumed=0\n']
        result = parse_physics_evidence(rows, 223)
        self.assertFalse(result['pass'])
        self.assertEqual(result['callbacks'], [{'slot': 43, 'thread': 99, 'controller': '123', 'expected': 'aa'}])
        self.assertIn('forbidden controller callback slot=43', result['errors'])

    def test_retains_bad_bytes_and_checks_external_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace, retained, engine, output = (root / n for n in ('trace', 'retained', 'engine', 'output'))
            trace.write_bytes(b'\xffbroken\n'); engine.write_text(''); output.write_text('')
            result = collect_physics_evidence(trace, retained, engine, output, 223)
            self.assertFalse(result['pass']); self.assertEqual(retained.read_bytes(), trace.read_bytes())
            trace.write_text(''.join(valid_trace())); retained.unlink()
            output.write_text('X4LAN PLAYER_CONTROL_RECURRING_FATAL exit=222\n')
            result = collect_physics_evidence(trace, retained, engine, output, 223)
            self.assertFalse(result['pass']); self.assertTrue(any('FATAL' in e for e in result['errors']))
            self.assertEqual(retained.read_bytes(), trace.read_bytes())

    def test_reject_symlinks_and_never_overwrite_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace, retained, engine, output = (root / n for n in ('trace', 'retained', 'engine', 'output'))
            source = root / 'source'; source.write_text(''.join(valid_trace())); trace.symlink_to(source)
            engine.write_text(''); output.write_text('')
            self.assertFalse(collect_physics_evidence(trace, retained, engine, output, 223)['pass'])
            trace.unlink(); trace.write_text(''.join(valid_trace())); retained.write_text('old')
            self.assertFalse(collect_physics_evidence(trace, retained, engine, output, 223)['pass'])
            self.assertEqual(retained.read_text(), 'old')


if __name__ == '__main__':
    unittest.main()
