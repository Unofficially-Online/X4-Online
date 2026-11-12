#!/usr/bin/env python3
"""Validate and retain the dedicated, process-discard-only physics trace.

A pass means the admitted worker reached its tail and exited 223 with both
short canonical-state brackets intact. Getter and body samples do not establish
causation, coherent physics reads, safe resumption, or exclusion of readers.
"""
import argparse
import hashlib
import json
import math
import os
import re
import stat
from pathlib import Path

from linux_gate_evidence import is_fatal_gate_message

PREFIX = 'X4LAN LIFECYCLE_DISCARD '
NUMBER = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
SNAPSHOT = re.compile(
    r'snapshot=(initial|prepared|taken|physics_updated|physics_canonical|physics_upkeep_before|physics_upkeep_after|physics_end|physics_sampled) '
    r'changed_bytes=(\d+) bank=([0-9a-f]{1992}) zone=([0-9a-f]+) zone_count=(\d+) '
    r'animation_a=([0-9a-f]+) animation_b=([0-9a-f]+) controller=([0-9a-f]+) '
    r'controller_vtable=([0-9a-f]+) contexts_ok=([01]) contexts_valid=1 '
    r'contexts_nodes=(\d+) contexts_mask=([0-7]) contexts_payloads=([0-9a-f]{96})')
SAMPLE = re.compile(r'physics=sample stage=(before|after) getter=(transform|velocity|acceleration) '
                    r'mode=1 size=(64|32) time=(' + NUMBER + r') raw=([0-9a-f]+)')
BODY = re.compile(
    r'physics=body stage=(before|after) index=([01]) '
    r'ship=([0-9a-f]+) vtable=([0-9a-f]+) parent=([0-9a-f]+) runtime=(\d+) present=([01]) '
    r'record=([0-9a-f]+) xbody=([0-9a-f]+) scene=([0-9a-f]+) system=([0-9a-f]+) '
    r'interface=([0-9a-f]+) manager=([0-9a-f]+) array=([0-9a-f]+) body=([0-9a-f]+) '
    r'motion=([0-9a-f]+) count=(\d+) id=(\d+) motion_present=([01]) '
    r'position=(' + NUMBER + r'(?:,' + NUMBER + r'){2}) '
    r'rotation=(' + NUMBER + r'(?:,' + NUMBER + r'){3}) '
    r'linear=(' + NUMBER + r'(?:,' + NUMBER + r'){2}) '
    r'angular=(' + NUMBER + r'(?:,' + NUMBER + r'){2})')
BODY_LINKS = ('ship', 'vtable', 'parent', 'runtime', 'present', 'record', 'xbody', 'scene',
              'system', 'interface', 'manager', 'array', 'body', 'motion', 'id', 'motion_present')
BODY_POINTERS = ('ship', 'vtable', 'parent', 'record', 'xbody', 'scene', 'system',
                 'interface', 'manager', 'array', 'body', 'motion')
CALLBACK = re.compile(r'physics=callback slot=(40|41|42|43) thread=(\d+) '
                      r'controller=([0-9a-f]+) expected=([0-9a-f]+)')
EXPECTED = ['entered', 'initial', 'before_singleton', 'after_singleton', 'prepared',
            'before_take', 'after_take', 'taken', 'input_press', 'before_update',
            'after_update', 'update', 'input_release', 'physics_updated', 'physics_canonical',
            'before_transform', 'before_velocity', 'before_acceleration', 'before_body0', 'before_body1', 'ready',
            'physics_upkeep_before', 'upkeep_enter', 'physics_upkeep_after', 'upkeep_leave', 'physics_end',
            'after_body0', 'after_body1', 'after_transform', 'after_velocity', 'after_acceleration',
            'physics_sampled', 'final']


def parse_body_row(match):
    names = ('stage', 'index', 'ship', 'vtable', 'parent', 'runtime', 'present', 'record',
             'xbody', 'scene', 'system', 'interface', 'manager', 'array', 'body', 'motion',
             'count', 'id', 'motion_present', 'position', 'rotation', 'linear', 'angular')
    row = dict(zip(names, match.groups()))
    for name in BODY_POINTERS:
        row[name] = int(row[name], 16)
        if row[name] > 0xffffffffffffffff:
            raise ValueError('pointer out of range')
    for name in ('index', 'runtime', 'present', 'count', 'id', 'motion_present'):
        row[name] = int(row[name])
    if not row['ship'] or not row['vtable'] or not 0 < row['runtime'] <= 0xffffffffffffffff:
        raise ValueError('missing ship identity')
    for name in ('position', 'rotation', 'linear', 'angular'):
        row[name] = [float(value) for value in row[name].split(',')]
        if not all(map(math.isfinite, row[name])):
            raise ValueError('nonfinite body value')
    linked = ('record', 'xbody', 'scene', 'system', 'interface', 'manager', 'array', 'body')
    if row['present']:
        if (any(not row[name] for name in linked) or row['body'] & 1 or
                not 0 < row['count'] <= 0x800000 or not 0 <= row['id'] < 0xffffffff or
                (row['id'] & 0x7fffff) >= row['count'] or
                bool(row['motion']) != bool(row['motion_present'])):
            raise ValueError('invalid body links or ID')
    elif (any(row[name] for name in (*linked, 'motion', 'count', 'id', 'motion_present')) or
          any(value != 0 for name in ('position', 'rotation', 'linear', 'angular') for value in row[name])):
        raise ValueError('missing body contains state')
    if not row['motion_present'] and any(value != 0 for name in ('linear', 'angular') for value in row[name]):
        raise ValueError('missing motion contains velocity')
    return row


def compare_body_rows(before, after):
    """Observed differences only; sample coherence and causation are unproved."""
    result = {'present': bool(before['present']), 'motion_present': bool(before['motion_present']),
              'position_changed': None, 'orientation_changed': None,
              'linear_changed': None, 'angular_changed': None, 'orientation_tolerance': 1e-6}
    if not before['present']:
        return result
    result['position_changed'] = before['position'] != after['position']
    first_norm, second_norm = math.hypot(*before['rotation']), math.hypot(*after['rotation'])
    if math.isfinite(first_norm) and math.isfinite(second_norm) and first_norm > 0 and second_norm > 0:
        first = [value / first_norm for value in before['rotation']]
        second = [value / second_norm for value in after['rotation']]
        same = all(math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-6) for a, b in zip(first, second))
        negated = all(math.isclose(a, -b, rel_tol=1e-6, abs_tol=1e-6) for a, b in zip(first, second))
        result['orientation_changed'] = not (same or negated)
    if before['motion_present']:
        result['linear_changed'] = before['linear'] != after['linear']
        result['angular_changed'] = before['angular'] != after['angular']
    return result


def parse_physics_evidence(lines, returncode):
    errors, events, snapshots, samples, callbacks, bodies = [], [], {}, [], [], []
    thread = None
    before_update = after_update = update_time = None
    final = None
    upkeep = []
    for line_number, line in enumerate(lines, 1):
        if not line.endswith('\n') or not line.startswith(PREFIX):
            errors.append(f'line {line_number}: unexpected or incomplete line')
            continue
        text = line[len(PREFIX):-1]
        match = re.fullmatch(r'physics=upkeep stage=(enter|leave) thread=(\d+) '
                             r'controller=([0-9a-f]+) time=(' + NUMBER + ')', text)
        if match:
            upkeep.append(dict(stage=match[1], thread=int(match[2]), controller=match[3], time=float(match[4])))
            events.append('upkeep_' + match[1])
            continue
        match = CALLBACK.fullmatch(text)
        if match:
            callbacks.append(dict(slot=int(match[1]), thread=int(match[2]),
                                  controller=match[3], expected=match[4]))
            errors.append(f'forbidden controller callback slot={match[1]}')
            continue
        match = re.fullmatch(r'stage=(entered|before_singleton|after_singleton|before_take|after_take) tick=5 thread=(\d+)', text)
        if match:
            events.append(match[1])
            current = int(match[2])
            if not current or (thread is not None and current != thread):
                errors.append('lifecycle worker thread changed')
            thread = current if thread is None else thread
            continue
        match = SNAPSHOT.fullmatch(text)
        if match:
            fields = dict(zip(('label', 'changed', 'bank', 'zone', 'zone_count', 'animation_a',
                               'animation_b', 'controller', 'vtable', 'contexts_ok', 'nodes',
                               'mask', 'payloads'), match.groups()))
            events.append(fields['label'])
            if fields['label'] in snapshots:
                errors.append('duplicate snapshot ' + fields['label'])
            snapshots[fields['label']] = fields
            if int(fields['changed']) > 996 or int(fields['contexts_ok']) != int(fields['mask'] == '7'):
                errors.append('invalid snapshot counts or context census')
            continue
        match = re.fullmatch(r'input=(press|release) id=2 state=([01]) keys=([01]{4}) digital=(' + NUMBER + ')', text)
        if match:
            wanted = ('1', '1000', 0.0) if match[1] == 'press' else ('0', '0000', -1.0)
            if (match[2], match[3], float(match[4])) != wanted:
                errors.append('invalid input readback')
            events.append('input_' + match[1])
            continue
        match = re.fullmatch(r'stage=before_update tick=5 now=(' + NUMBER + r') previous=(' + NUMBER +
                             r') last=(' + NUMBER + r') initializer=(' + NUMBER + ')', text)
        if match:
            before_update = tuple(map(float, match.groups()))
            now, previous, last, initializer = before_update
            elapsed = now - previous
            expected_initializer = now - elapsed
            if (not all(map(math.isfinite, (*before_update, elapsed, expected_initializer))) or
                    not (now > 0 and previous < now and last < now and elapsed > 0 and
                         initializer == expected_initializer and initializer != now)):
                errors.append('invalid update admission times')
            events.append('before_update')
            continue
        match = re.fullmatch(r'stage=after_update last=(' + NUMBER + ')', text)
        if match:
            after_update = float(match[1]); events.append('after_update'); continue
        match = re.fullmatch(r'update=pass calls=1 input=state2 worker=1 zone_matches=1 controller_identity=1 time=(' + NUMBER + ')', text)
        if match:
            update_time = float(match[1]); events.append('update'); continue
        match = BODY.fullmatch(text)
        if match:
            try:
                body = parse_body_row(match)
                bodies.append(body)
                events.append(body['stage'] + '_body' + str(body['index']))
            except ValueError as error:
                errors.append(f'line {line_number}: invalid body observation: {error}')
            continue
        match = SAMPLE.fullmatch(text)
        if match:
            stage, getter, size, time, raw = match.groups()
            required = 64 if getter == 'transform' else 32
            if int(size) != required or len(raw) != required * 2 or not math.isfinite(float(time)) or float(time) <= 0:
                errors.append('invalid motion sample')
            samples.append(dict(stage=stage, getter=getter, size=int(size), time=time, raw=raw))
            events.append(stage + '_' + getter)
            continue
        if text == 'physics=ready generation=5 main_parked=1':
            events.append('ready'); continue
        match = re.fullmatch(r'result=(pass|fail) reason=(\w+) exit=223 restored=0 resumed=0', text)
        if match:
            final = dict(result=match[1], reason=match[2]); events.append('final'); continue
        errors.append(f'line {line_number}: malformed or unexpected lifecycle record')
    if events != EXPECTED:
        errors.append('missing, duplicate, extra or reordered records')
    if not before_update or after_update != before_update[0] or update_time != before_update[0]:
        errors.append('native update late time write mismatch')
    for stage in ('before', 'after'):
        times = [float(sample['time']) for sample in samples if sample['stage'] == stage]
        if len(times) != 3 or any(value != times[0] for value in times):
            errors.append(stage + ' motion sample times disagree')
        elif update_time is not None and ((stage == 'before' and times[0] != update_time) or
                                         (stage == 'after' and times[0] < update_time)):
            errors.append(stage + ' motion sample time contradicts the admitted update')
    if final != {'result': 'pass', 'reason': 'physics_frame_returned'}:
        errors.append('missing physics tail pass result')
    if (len(upkeep) != 2 or [row['stage'] for row in upkeep] != ['enter', 'leave'] or
            any(row['thread'] != thread or row['controller'] != snapshots.get('taken', {}).get('controller') or
                not math.isfinite(row['time']) or row['time'] != upkeep[0]['time'] for row in upkeep)):
        errors.append('missing or contradictory native upkeep scope')
    if returncode != 223:
        errors.append(f'expected process exit 223, got {returncode}')
    if set(snapshots) == {'initial', 'prepared', 'taken', 'physics_updated', 'physics_canonical',
                          'physics_upkeep_before', 'physics_upkeep_after', 'physics_end', 'physics_sampled'}:
        initial, canonical, taken = (snapshots[k] for k in ('initial', 'physics_canonical', 'taken'))
        initial_bank = bytes.fromhex(initial['bank'])
        for snapshot in snapshots.values():
            changed = sum(a != b for a, b in zip(initial_bank, bytes.fromhex(snapshot['bank'])))
            if changed != int(snapshot['changed']):
                errors.append('snapshot changed byte count mismatch: ' + snapshot['label'])
        equal_fields = ('bank', 'zone', 'animation_a', 'animation_b', 'contexts_ok', 'nodes', 'mask', 'payloads')
        if (initial['changed'] != '0' or canonical['changed'] != '0' or not int(initial['zone'], 16) or
                any(initial[key] != canonical[key] for key in equal_fields) or
                int(canonical['zone_count']) != int(initial['zone_count']) + 1):
            errors.append('canonical snapshot differs from initial globals or held-zone count')
        if (not int(taken['controller'], 16) or canonical['controller'] != taken['controller'] or
                not int(taken['vtable'], 16) or not int(canonical['vtable'], 16) or
                canonical['vtable'] == taken['vtable']):
            errors.append('P2 controller or private vtable evidence mismatch')
        bracket_fields = ('bank', 'zone', 'zone_count', 'animation_a', 'animation_b',
                          'controller', 'vtable', 'contexts_ok', 'nodes', 'mask', 'payloads')
        for first, second in (('physics_upkeep_before', 'physics_upkeep_after'),
                              ('physics_end', 'physics_sampled')):
            if any(snapshots[first][key] != snapshots[second][key] for key in bracket_fields):
                errors.append(f'canonical state changed within {first}/{second} bracket')
            for label in (first, second):
                if (snapshots[label]['zone'] != initial['zone'] or
                        snapshots[label]['controller'] != canonical['controller'] or
                        snapshots[label]['vtable'] != canonical['vtable']):
                    errors.append(f'canonical zone or controller identity changed: {label}')
                if any(snapshots[label][key] != initial[key]
                       for key in ('contexts_ok', 'nodes', 'mask', 'payloads')):
                    errors.append(f'canonical contexts changed: {label}')
        updated = snapshots['physics_updated']
        if updated['controller'] != taken['controller'] or updated['vtable'] != taken['vtable']:
            errors.append('P2 controller identity changed between take and updated snapshot')
    comparisons = []
    if len(bodies) == 4:
        before = bodies[:2]
        after = bodies[2:]
        if [(row['stage'], row['index']) for row in bodies] == [
                ('before', 0), ('before', 1), ('after', 0), ('after', 1)]:
            ship_identity = tuple(before[0][key] for key in ('ship', 'vtable', 'parent', 'runtime'))
            if any(tuple(row[key] for key in ('ship', 'vtable', 'parent', 'runtime')) != ship_identity
                   for row in bodies):
                errors.append('body ship identity changed within this process')
            for index in range(2):
                # BodyManager count can grow/shrink from unrelated allocations;
                # each ID was independently range-checked in each observation.
                if any(before[index][key] != after[index][key] for key in BODY_LINKS):
                    errors.append(f'body {index} identity or links changed')
                else:
                    comparisons.append(dict(index=index, **compare_body_rows(before[index], after[index])))
    return {"pass": not errors, "returncode": returncode, "errors": errors, "events": events,
            "snapshots": snapshots, "samples": samples, "bodies": bodies, "body_comparisons": comparisons,
            "callbacks": callbacks, "upkeep": upkeep, "final": final,
            "restored": False, "resumed": False, "movement_proven": False, "other_threads_excluded": False}


def collect_physics_evidence(trace, retained, engine, output, returncode):
    """Retain the exact bytes first, including malformed or failed evidence."""
    errors = []
    data = None
    try:
        fd = os.open(trace, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, 'rb') as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise OSError('native trace is not a regular file')
            retained_fd = os.open(retained, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            digest = hashlib.sha256()
            chunks, total = [], 0
            with os.fdopen(retained_fd, 'wb') as copy:
                while True:
                    chunk = stream.read(65536)
                    if not chunk:
                        break
                    copy.write(chunk)
                    digest.update(chunk)
                    total += len(chunk)
                    if total <= 1 << 20:
                        chunks.append(chunk)
            if total > 1 << 20:
                raise OSError('native trace exceeds 1 MiB; full trace retained')
            data = b''.join(chunks)
        evidence = parse_physics_evidence(data.decode('utf-8').splitlines(keepends=True), returncode)
        evidence['log_sha256'] = digest.hexdigest()
    except (OSError, UnicodeError) as error:
        evidence = parse_physics_evidence([], returncode)
        errors.append(f'native trace unavailable/invalid: {error}')
    for source in (engine, output):
        try:
            with Path(source).open(errors='replace') as stream:
                errors.extend(f'{Path(source).name}: {line.strip()[:300]}' for line in stream
                              if is_fatal_gate_message(line) or 'PLAYER_CONTROL_RECURRING_FATAL' in line)
        except OSError as error:
            errors.append(f'fatal log unavailable: {error}')
    evidence.update(native_fd_path=str(trace), retained_log=str(retained))
    evidence['errors'].extend(errors)
    evidence['pass'] = not evidence['errors']
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('trace', 'retained', 'engine', 'output'):
        parser.add_argument('--' + flag, type=Path, required=True)
    parser.add_argument('--returncode', type=int, required=True)
    args = parser.parse_args()
    result = collect_physics_evidence(args.trace, args.retained, args.engine, args.output, args.returncode)
    print(json.dumps(result, sort_keys=True))
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
