#!/usr/bin/env python3
"""Validate the bounded, process-discard-only recurring physics trace.

Validation describes logged events and checked state only. It does not prove
coherent Jolt reads, input causation, safe resumption or exclusion of readers.
"""
import hashlib
import math
import os
import re
import stat

from linux_player_presentation_evidence import validate_presentation_rows
from pathlib import Path

from linux_gate_evidence import is_fatal_gate_message
from linux_player_context_evidence import POLICY_SUFFIX, verify_context_policy
from linux_player_camera_evidence import BEFORE, AFTER, RESULT, context_proof
from linux_player_physics_evidence import (
    BODY, BODY_LINKS, CALLBACK, NUMBER, PREFIX, SAMPLE, SNAPSHOT,
    compare_body_rows, parse_body_row,
)

MAX_TRACE_BYTES = 8 * 1024 * 1024
NETWORK_MAX_TRACE_BYTES = 32 * 1024 * 1024
SNAPSHOT_FIELDS = ('label', 'changed', 'bank', 'zone', 'zone_count', 'animation_a',
                   'animation_b', 'controller', 'vtable', 'contexts_ok', 'nodes', 'mask', 'payloads')
BRACKET_FIELDS = ('bank', 'zone', 'zone_count', 'animation_a', 'animation_b',
                  'controller', 'vtable', 'contexts_ok', 'nodes', 'mask', 'payloads',
                  'policy_manager', 'policy_epoch', 'policy_hash')
CONTEXT_FIELDS = ('contexts_ok', 'nodes', 'mask', 'payloads')
FRAME_SNAPSHOT = re.compile(SNAPSHOT.pattern.replace(
    'physics_updated|physics_canonical|',
    'physics_updated|physics_canonical|physics_update_before|physics_update_after|'))
ANCHOR_SUFFIX = re.compile(r' context_anchor_v=1 manager=([0-9a-f]+) full_hash=([0-9a-f]{64})$')
ANCHOR_RECEIPT = re.compile(r'X4LAN CONTEXT_ANCHOR v=1 tick=6 owner=([0-9a-f]+) manager=([0-9a-f]+) '
                            r'target=([0-9a-f]+) controller=([0-9a-f]+) digest=([0-9a-f]{64})\n')

BEGIN = re.compile(r'physics=frame tick=([0-9]{1,20}) event=begin')
END = re.compile(r'physics=frame tick=([0-9]{1,20}) event=end')
FINAL = 'result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0'
UPKEEP = re.compile(r'physics=upkeep stage=(enter|leave) thread=(\d+) '
                    r'controller=([0-9a-f]+) time=(' + NUMBER + ')')
INPUT = re.compile(r'physics=input tick=(\d+) id=([023]) state=([01]) '
                   r'keys=([01]{4}) digital=(' + NUMBER + ')')
BEFORE_UPDATE = re.compile(r'stage=before_update tick=(\d+) now=(' + NUMBER + r') previous=(' + NUMBER +
                           r') last=(' + NUMBER + r') initializer=(' + NUMBER + ')')
AFTER_UPDATE = re.compile(r'stage=after_update last=(' + NUMBER + ')')
UPDATE = re.compile(r'update=pass calls=1 input=(state2|state3|none|record) worker=1 '
                    r'zone_matches=1 controller_identity=1 time=(' + NUMBER + ')')
NETWORK_INPUT = re.compile(r'physics=network tick=([0-9]+) slot=2 stage=(before|applied|updated) '
                           r'held_before=([0-9]+) held_after=([0-9]+) digital=(' + NUMBER + r') record=([0-9a-f]+)')
THROTTLE = re.compile(r'throttle=(before|applied|updated) tick=(\d+) ship=([0-9a-f]+) '
                      r'controller=([0-9a-f]+) collection=([0-9a-f]+) members=(\d+) keys=([01]{2}) '
                      r'command=(' + NUMBER + r') lower=(' + NUMBER + r') upper=(' + NUMBER +
                      r') original=(' + NUMBER + r')(?: table=([0-9a-f]+) factor68=(' + NUMBER +
                      r') factor70=(' + NUMBER + r') factor74=(' + NUMBER + r') factor78=(' + NUMBER +
                      r') controller_factor=(' + NUMBER + r'))?')
THROTTLE_MEMBER = re.compile(r'throttle_member=(before|applied|updated) tick=(\d+) index=(\d+) '
                             r'object=([0-9a-f]+) field328=([0-9a-f]{8}) '
                             r'field2f0=([0-9a-f]{8}) field2f4=([0-9a-f]{2})')
STAGE = re.compile(r'stage=(entered|before_singleton|after_singleton|before_take|after_take) '
                   r'tick=(\d+) thread=(\d+)')
FIRST_INPUT = re.compile(r'input=(press|release) id=2 state=([01]) keys=([01]{4}) digital=(' + NUMBER + ')')
READY = re.compile(r'physics=ready generation=(\d+) main_parked=1')

FIRST_EVENTS = [
    'entered', 'initial', 'before_singleton', 'after_singleton', 'prepared', 'before_take', 'after_take',
    'taken', 'input_press', 'before_update', 'after_update', 'update', 'input_release',
    'physics_updated', 'physics_canonical',
]
REPEATED_EVENTS = ['physics_update_before', 'input_press', 'before_update', 'after_update', 'update',
                   'input_release', 'physics_update_after']
COMMON_EVENTS = [
    'before_transform', 'before_velocity', 'before_acceleration', 'before_body0', 'before_body1',
    'ready', 'physics_upkeep_before', 'upkeep_enter', 'physics_upkeep_after', 'upkeep_leave',
    'physics_end', 'after_body0', 'after_body1', 'after_transform', 'after_velocity',
    'after_acceleration', 'physics_sampled',
]


def input_policy(tick):
    if 5 <= tick <= 14:
        return 2, '1000', 'state2'
    if 25 <= tick <= 34:
        return 3, '0100', 'state3'
    return 0, '0000', 'none'


def parse_worker_evidence(lines, frame_count, *, first_tick=5, final_tick=None):
    errors, events = [], []
    worker = re.compile(r'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=([1-9]\d{0,19}) '
                        r'tail=1 event=(enter|release)\n')
    completion = re.compile(r'X4LAN PLAYER_CONTROL_RECURRING tick=([1-9]\d*) completed=1 '
                            r'readiness_only=0 bank_writes=1 context_writes=unverified reference_calls=1 '
                            r'other_threads_excluded=0 tail=1\n')
    for line in lines:
        if is_fatal_gate_message(line) or 'PLAYER_CONTROL_RECURRING_FATAL' in line or 'PLAYER_PHASES_FATAL' in line:
            errors.append('fatal worker record: ' + line.strip()[:300])
        if 'PLAYER_CONTROL_RECURRING' not in line or 'PLAYER_CONTROL_RECURRING_FATAL' in line or 'PLAYER_PHASES_FATAL' in line:
            continue
        match = worker.fullmatch(line)
        if match:
            events.append((match[2], int(match[1])))
            continue
        match = completion.fullmatch(line)
        if match:
            events.append(('complete', int(match[1])))
            continue
        errors.append('malformed recurring worker record')
    final_tick = final_tick if final_tick is not None else 4 + frame_count
    expected = [(event, tick) for tick in range(first_tick, final_tick)
                for event in ('enter', 'release', 'complete')]
    expected.append(('enter', final_tick))
    if events != expected:
        errors.append('recurring worker events are missing, duplicated or out of order')
    return {'pass': not errors, 'events': events, 'errors': errors}


def parse_frame(lines, tick, baseline, previous_time, previous_digital, input_mode="scripted", allow_throttle=False, throttle_start_tick=5, *, take_tick=5, apply_tick=5):
    errors, events, snapshots, samples, bodies, inputs, upkeep = [], [], {}, [], [], [], []
    stage_threads = []
    before_update = after_update = update = None
    throttle_rows, pending_throttle = [], None
    first = tick == take_tick
    preapply = tick < apply_tick
    for line in lines:
        if not line.endswith('\n') or not line.startswith(PREFIX):
            errors.append('unexpected or incomplete frame record')
            continue
        text = line[len(PREFIX):-1]
        member = THROTTLE_MEMBER.fullmatch(text) if allow_throttle else None
        if member:
            phase, member_tick, index, address, field328, field2f0, field2f4 = member.groups()
            if (pending_throttle is None or int(member_tick) != tick or phase != pending_throttle['stage'] or
                    int(index) != len(pending_throttle['members']) or
                    len(pending_throttle['members']) >= pending_throttle['member_count'] or
                    not 0 < int(address, 16) <= 0xffffffffffffffff):
                errors.append('malformed, misplaced or duplicate throttle member')
            else:
                pending_throttle['members'].append({'index': int(index), 'object': int(address, 16),
                    'field328': field328, 'field2f0': field2f0, 'field2f4': field2f4})
            continue
        if pending_throttle is not None:
            if len(pending_throttle['members']) != pending_throttle['member_count']:
                errors.append('missing throttle member records')
            pending_throttle = None
        match = THROTTLE.fullmatch(text) if allow_throttle else None
        if match:
            (phase, row_tick, ship, controller, collection, count, keys, command, lower, upper, original,
             table, factor68, factor70, factor74, factor78, controller_factor) = match.groups()
            factors = None
            if table is not None:
                factor_values = list(map(float, (factor68, factor70, factor74, factor78, controller_factor)))
                if not 0 < int(table, 16) <= 0xffffffffffffffff or not all(map(math.isfinite, factor_values)):
                    errors.append('invalid throttle factor values')
                factors = dict(table=int(table, 16), values=factor_values)
            if (throttle_start_tick == 6) != (factors is not None):
                errors.append('throttle factor evidence missing or unexpected')
            values = list(map(float, (command, lower, upper, original)))
            if (tick not in (throttle_start_tick, throttle_start_tick + 1) or int(row_tick) != tick or int(count) > 64 or
                    not all(math.isfinite(value) for value in values) or values[1] > values[2] or
                    not 0 < int(ship, 16) <= 0xffffffffffffffff or
                    not 0 < int(controller, 16) <= 0xffffffffffffffff or
                    int(collection, 16) > 0xffffffffffffffff or (int(count) and not int(collection, 16))):
                errors.append('invalid throttle stage values or identity')
            pending_throttle = {'stage': phase, 'tick': int(row_tick), 'ship': int(ship, 16),
                'controller': int(controller, 16), 'collection': int(collection, 16),
                'member_count': int(count), 'members': [], 'keys': keys, 'command': values[0],
                'lower': values[1], 'upper': values[2], 'original': values[3], 'factors': factors}
            throttle_rows.append(pending_throttle)
            events.append('throttle_' + phase)
            continue
        anchor_suffix = ANCHOR_SUFFIX.search(text)
        policy_suffix = POLICY_SUFFIX.search(text)
        suffix = anchor_suffix or policy_suffix
        snapshot_text = text[:suffix.start()] if suffix else text
        match = FRAME_SNAPSHOT.fullmatch(snapshot_text)
        if match:
            fields = dict(zip(SNAPSHOT_FIELDS, match.groups()))
            fields['anchor_manager'] = anchor_suffix[1] if anchor_suffix else None
            fields['anchor_hash'] = anchor_suffix[2] if anchor_suffix else None
            fields['policy_manager'] = policy_suffix[1] if policy_suffix else None
            fields['policy_epoch'] = policy_suffix[2] if policy_suffix else None
            fields['policy_hash'] = policy_suffix[3] if policy_suffix else None
            label = fields['label']
            events.append(label)
            if label in snapshots:
                errors.append('duplicate snapshot ' + label)
            snapshots[label] = fields
            if int(fields['changed']) > 996 or int(fields['contexts_ok']) != int(fields['mask'] == '7'):
                errors.append('invalid snapshot counts or context census')
            continue
        match = STAGE.fullmatch(text)
        if match:
            events.append(match[1])
            if int(match[2]) != take_tick or tick != take_tick:
                errors.append('lifecycle stage outside the explicit Take epoch')
            stage_threads.append(int(match[3]))
            continue
        match = NETWORK_INPUT.fullmatch(text) if input_mode == 'network' else None
        if match:
            observed_tick, phase, before, after, digital, record = match.groups()
            inputs.append({'phase': phase, 'tick': int(observed_tick), 'held_before': int(before),
                           'held_after': int(after), 'digital': float(digital), 'record': record})
            events.append('network_' + phase)
            continue
        match = (FIRST_INPUT.fullmatch(text) if first else INPUT.fullmatch(text)) if input_mode == "scripted" else None
        if match:
            if first:
                phase, state, keys, digital = match.groups()
                input_tick, input_id = tick, 2
            else:
                input_tick, input_id, state, keys, digital = match.groups()
                input_tick, input_id = int(input_tick), int(input_id)
                phase = 'press' if len(inputs) == 0 else 'release'
            measurement = float(digital)
            inputs.append({'phase': phase, 'tick': input_tick, 'id': input_id,
                           'state': int(state), 'keys': keys, 'digital': measurement})
            events.append('input_' + phase)
            continue
        match = BEFORE_UPDATE.fullmatch(text)
        if match:
            observed_tick = int(match[1])
            now, previous, last, initializer = map(float, match.groups()[1:])
            elapsed = now - previous
            if (observed_tick != tick or not all(map(math.isfinite, (now, previous, last, initializer, elapsed))) or
                    not (now > 0 and previous < now and last < now and elapsed > 0 and
                         initializer == now - elapsed and initializer != now) or
                    (previous_time is not None and now <= previous_time)):
                errors.append('invalid or nonmonotonic update admission times')
            before_update = {'now': now, 'previous': previous, 'last': last, 'initializer': initializer}
            events.append('before_update')
            continue
        match = AFTER_UPDATE.fullmatch(text)
        if match:
            after_update = float(match[1])
            events.append('after_update')
            continue
        match = UPDATE.fullmatch(text)
        if match:
            update = {'input': match[1], 'time': float(match[2])}
            events.append('update')
            continue
        match = SAMPLE.fullmatch(text)
        if match:
            stage, getter, size, time, raw = match.groups()
            size, sample_time = int(size), float(time)
            if (size != (64 if getter == 'transform' else 32) or len(raw) != size * 2 or
                    not math.isfinite(sample_time) or sample_time <= 0):
                errors.append('invalid controller getter sample')
            samples.append({'stage': stage, 'getter': getter, 'size': size, 'time': sample_time, 'raw': raw})
            events.append(stage + '_' + getter)
            continue
        match = BODY.fullmatch(text)
        if match:
            try:
                body = parse_body_row(match)
                bodies.append(body)
                events.append(body['stage'] + '_body' + str(body['index']))
            except ValueError as error:
                errors.append('invalid body observation: ' + str(error))
            continue
        if text == 'physics=take_no_update version=1' and preapply and first:
            events.append('take_no_update')
            continue
        if text == f'physics=preapply generation={tick} update=0' and preapply and not first:
            events.append('preapply')
            continue
        match = READY.fullmatch(text)
        if match:
            if int(match[1]) != tick:
                errors.append('wrong ready generation')
            events.append('ready')
            continue
        match = UPKEEP.fullmatch(text)
        if match:
            upkeep.append({'stage': match[1], 'thread': int(match[2]),
                           'controller': match[3], 'time': float(match[4])})
            events.append('upkeep_' + match[1])
            continue
        match = CALLBACK.fullmatch(text)
        if match:
            errors.append('forbidden controller callback slot=' + match[1])
            continue
        errors.append('malformed or unexpected frame record: ' + text[:160])

    if pending_throttle is not None and len(pending_throttle['members']) != pending_throttle['member_count']:
        errors.append('missing throttle member records')
    expected_events = (FIRST_EVENTS if first else REPEATED_EVENTS) + COMMON_EVENTS
    if preapply:
        expected_events = ((FIRST_EVENTS[:8] + ['take_no_update', 'physics_canonical'])
                           if first else []) + COMMON_EVENTS
        if not first:
            expected_events.insert(expected_events.index('ready'), 'preapply')
            expected_events.remove('ready')
    elif input_mode == 'network':
        expected_events = [event for event in expected_events if event not in ('input_press', 'input_release')]
        offset = expected_events.index('before_update')
        expected_events[offset:offset] = ['network_before', 'network_applied']
        expected_events.insert(expected_events.index('update') + 1, 'network_updated')
        if allow_throttle and tick in (throttle_start_tick, throttle_start_tick + 1):
            offset = expected_events.index('before_update')
            expected_events[offset:offset] = ['throttle_before', 'throttle_applied']
            expected_events.insert(expected_events.index('network_updated'), 'throttle_updated')
    if events != expected_events:
        errors.append('missing, duplicate, extra or reordered frame records')
    expected_update = 'record'
    if input_mode == 'scripted':
        expected_id, expected_keys, expected_update = input_policy(tick)
        if len(inputs) != 2:
            errors.append('missing input press/release pair')
        else:
            press, release = inputs
            expected_state = int(expected_id != 0)
            if (press['phase'] != 'press' or release['phase'] != 'release' or
                    press['tick'] != tick or release['tick'] != tick or
                    press['id'] != expected_id or release['id'] != expected_id or
                    press['state'] != expected_state or press['keys'] != expected_keys or
                    release['state'] != 0 or release['keys'] != '0000' or
                    not math.isfinite(press['digital']) or not math.isfinite(release['digital']) or
                    (first and (press['digital'] != 0 or release['digital'] != -1)) or
                    (not first and (previous_digital is None or press['digital'] != previous_digital))):
                errors.append('input policy or digital carry mismatch')
    if not preapply and (not before_update or not update or update['input'] != expected_update or
            not math.isfinite(update['time']) or after_update != before_update['now'] or
            update['time'] != before_update['now']):
        errors.append('native update late time write or input mismatch')
    if preapply and (before_update or after_update is not None or update or inputs):
        errors.append('controller Update or input before the common apply epoch')
    if update:
        for stage in ('before', 'after'):
            times = [sample['time'] for sample in samples if sample['stage'] == stage]
            if (len(times) != 3 or any(value != times[0] for value in times) or
                    (times and ((stage == 'before' and times[0] != update['time']) or
                                (stage == 'after' and times[0] < update['time'])))):
                errors.append(stage + ' getter times disagree with update')
    if first:
        if not stage_threads or not stage_threads[0] or any(value != stage_threads[0] for value in stage_threads):
            errors.append('lifecycle worker thread changed')
        worker_thread = stage_threads[0] if stage_threads else None
    else:
        worker_thread = baseline.get('thread')
        if stage_threads:
            errors.append('unexpected later lifecycle stage')
    controller = (snapshots.get('taken', {}).get('controller') if first else baseline.get('controller'))
    if (len(upkeep) != 2 or [row['stage'] for row in upkeep] != ['enter', 'leave'] or
            any(row['thread'] != worker_thread or row['controller'] != controller or
                not math.isfinite(row['time']) or row['time'] != upkeep[0]['time'] for row in upkeep)):
        errors.append('missing or contradictory native upkeep scope')

    frame = {'tick': tick, 'events': events, 'snapshots': snapshots, 'inputs': inputs,
             'update': before_update, 'update_result': update, 'after_update': after_update,
             'worker_thread': worker_thread, 'samples': samples, 'bodies': bodies, 'upkeep': upkeep,
             'body_comparisons': [], 'throttle': throttle_rows}
    return frame, errors, worker_thread


def validate_frame_state(frame, initial, canonical, taken, baseline_bodies, anchor=None, *, take_tick=5, apply_tick=5):
    errors = []
    tick, snapshots, bodies = frame['tick'], frame['snapshots'], frame['bodies']
    initial_bank = bytes.fromhex(initial['bank'])
    for label, snapshot in snapshots.items():
        changed = sum(a != b for a, b in zip(initial_bank, bytes.fromhex(snapshot['bank'])))
        if changed != int(snapshot['changed']):
            errors.append('snapshot changed byte count mismatch: ' + label)
    if tick == take_tick:
        equal = ('bank', 'zone', 'animation_a', 'animation_b', *CONTEXT_FIELDS)
        if (initial['changed'] != '0' or canonical['changed'] != '0' or not int(initial['zone'], 16) or
                any(initial[key] != canonical[key] for key in equal) or
                int(canonical['zone_count']) != int(initial['zone_count']) + 1):
            errors.append('initial canonical restoration mismatch')
        if (not int(taken['controller'], 16) or canonical['controller'] != taken['controller'] or
                not int(taken['vtable'], 16) or not int(canonical['vtable'], 16) or
                canonical['vtable'] == taken['vtable']):
            errors.append('first-frame controller/private vtable mismatch')
        updated = snapshots.get('physics_updated')
        if tick >= apply_tick and (not updated or updated['controller'] != taken['controller'] or updated['vtable'] != taken['vtable']):
            errors.append('first-frame controller changed after take')
    brackets = [('physics_upkeep_before', 'physics_upkeep_after'), ('physics_end', 'physics_sampled')]
    if tick >= apply_tick and tick != take_tick:
        brackets.insert(0, ('physics_update_before', 'physics_update_after'))
    for first, second in brackets:
        left, right = snapshots.get(first), snapshots.get(second)
        if not left or not right:
            errors.append('missing state bracket ' + first)
            continue
        if any(left[key] != right[key] for key in BRACKET_FIELDS):
            errors.append('canonical state changed inside ' + first + '/' + second)
        for snapshot in (left, right):
            expected = snapshot.get('context_policy')
            contexts = ({'contexts_ok': '1', 'nodes': str(expected['nodes']), 'mask': '7', 'payloads': expected['selected']}
                        if expected else {key: ('15' if key == 'nodes' and snapshot['anchor_hash'] else initial[key])
                                          for key in CONTEXT_FIELDS})
            if (snapshot['zone'] != initial['zone'] or snapshot['controller'] != canonical['controller'] or
                    snapshot['vtable'] != canonical['vtable'] or
                    any(snapshot[key] != contexts[key] for key in CONTEXT_FIELDS)):
                errors.append('canonical zone, controller or context identity changed')
    if [(row['stage'], row['index']) for row in bodies] != [
            ('before', 0), ('before', 1), ('after', 0), ('after', 1)]:
        errors.append('missing or reordered body observations')
        return errors
    ship_identity = tuple(bodies[0][key] for key in ('ship', 'vtable', 'parent', 'runtime'))
    if any(tuple(row[key] for key in ('ship', 'vtable', 'parent', 'runtime')) != ship_identity for row in bodies):
        errors.append('body ship identity changed')
    for index in range(2):
        first, last = bodies[index], bodies[index + 2]
        # Manager size may change when unrelated bodies are allocated. Every
        # individual BodyID is still range-checked against its own sample.
        if any(first[key] != last[key] for key in BODY_LINKS):
            errors.append(f'body {index} links changed during frame')
        elif baseline_bodies and any(first[key] != baseline_bodies[index][key] for key in BODY_LINKS):
            errors.append(f'body {index} links changed across frames')
        else:
            frame['body_comparisons'].append(dict(index=index, **compare_body_rows(first, last)))
    return errors


def parse_recurring_physics_evidence(lines, stderr_lines, returncode, *, frame_count, input_mode="scripted", allow_camera=False, allow_throttle=False, throttle_start_tick=5, presentation_slot=None, session_frames=0, startup=None):
    errors, frames, blocks = [], [], []
    from linux_player_startup_phases import startup_phases
    try:
        phase = startup_phases(startup)
        if phase['version'] and (not session_frames or input_mode != 'network' or allow_throttle or allow_camera):
            raise ValueError('common startup requires a finite neutral-diagnostic network session')
    except ValueError as error:
        return {'pass': False, 'errors': [str(error)], 'frames': []}
    if (input_mode not in ('scripted', 'network') or type(allow_throttle) is not bool or
            (allow_throttle and input_mode != 'network') or
            type(throttle_start_tick) is not int or throttle_start_tick not in (5, 6) or
            (not allow_throttle and throttle_start_tick != 5)):
        return {'pass': False, 'errors': ['invalid input_mode'], 'frames': [], 'movement_proven': False,
                'restored': False, 'resumed': False, 'other_threads_excluded': False}
    maximum = 600 if input_mode == 'network' else 180
    if session_frames:
        from linux_ui_session_budget import session_budget
        try:
            if input_mode != 'network' or session_budget(session_frames)['frames'] != frame_count:
                raise ValueError('session physics budget mismatch')
            maximum = session_frames
        except ValueError as error:
            return {'pass':False,'errors':[str(error)],'frames':[]}
    if type(frame_count) is not int or not 2 <= frame_count <= maximum:
        return {'pass': False, 'errors': [f'frame_count must be an integer from 2 through {maximum}'], 'frames': [],
                'movement_proven': False, 'restored': False, 'resumed': False, 'other_threads_excluded': False}
    presentation = None
    if presentation_slot is not None:
        if input_mode != 'network' or frame_count < 4 or type(presentation_slot) is not int or presentation_slot not in (1, 2):
            return {'pass': False, 'errors': ['invalid presentation admission'], 'frames': [], 'movement_proven': False,
                    'restored': False, 'resumed': False, 'other_threads_excluded': False}
        presentation = validate_presentation_rows(lines, presentation_slot, tick=phase['presentation_probe'])
        errors.extend(presentation['errors'])
        if presentation['pass']:
            first_index = presentation['indices'][0]
            previous = lines[first_index - 1] if first_index else ''
            frame_begin = PREFIX + f'physics=frame tick={phase["presentation_probe"]} event=begin\n'
            frame_end = PREFIX + f'physics=frame tick={phase["presentation_probe"]} event=end\n'
            in_frame = frame_begin in lines[:first_index] and frame_end in lines[presentation['indices'][-1] + 1:]
            if not in_frame or not previous.startswith(PREFIX + 'physics=upkeep stage=leave '):
                errors.append('presentation outside tick 7 upkeep/end boundary')
            else:
                indices = set(presentation['indices'])
                lines = [line for index, line in enumerate(lines) if index not in indices]
    policy = verify_context_policy(lines, allow_camera=allow_camera, first_tick=phase['take'])
    errors.extend(policy['errors'])
    lines = policy['lines']
    current_tick, current_lines, saw_final = None, [], False
    camera_rows, receipt, receipt_count = [], None, 0
    active_anchor = None
    for line_number, line in enumerate(lines, 1):
        if line.startswith('X4LAN CAMERA'):
            if not camera_rows and (not current_lines or not current_lines[-1].startswith(PREFIX + 'physics=upkeep stage=leave ')):
                errors.append('camera transition outside tick 6 upkeep/end boundary')
            if not allow_camera or current_tick != 6 or receipt_count:
                errors.append('camera evidence outside its permitted frame or after receipt')
            camera_rows.append(line)
            continue
        if line.startswith('X4LAN CONTEXT_ANCHOR'):
            receipt_count += 1
            receipt = ANCHOR_RECEIPT.fullmatch(line)
            if not allow_camera or current_tick != 6 or receipt_count != 1 or not receipt:
                errors.append('malformed, duplicate or misplaced context anchor receipt')
            elif not current_lines or not current_lines[-1].startswith(PREFIX + 'physics=upkeep stage=leave '):
                errors.append('context anchor receipt outside tick 6 upkeep/end boundary')
            else:
                try:
                    headers = [x for x in camera_rows if x.startswith('X4LAN CAMERA stage=')]
                    before_match, after_match = BEFORE.fullmatch(headers[0]), AFTER.fullmatch(headers[1])
                    if len(headers) != 3 or not before_match or not after_match or not RESULT.fullmatch(headers[2]):
                        raise ValueError('invalid camera header sequence')
                    before, after = before_match.groupdict(), after_match.groupdict()
                    for row in (before, after):
                        row['nodes'], row['mask'] = int(row['nodes']), int(row['mask'])
                    proof = context_proof(camera_rows, before, after)
                    owner, manager, target, controller = (int(receipt[i], 16) for i in range(1, 5))
                    digest = hashlib.sha256(b''.join(bytes.fromhex(x) for x in proof.get('after_payloads', []))).hexdigest()
                    if (not proof['pass'] or proof.get('version') != 2 or not 0 < owner < (1 << 64) - 16 or manager != owner + 16 or
                        not 0 < target < 1 << 64 or not 0 < controller < 1 << 64 or not 0 < int(before['runtime']) < 1 << 64 or target != int(before['target'], 16) or
                        before['saved'] != '0000000000175310' or before['current'] == before['runtime'] or
                        after['current'] != before['runtime'] or (after['possible'], after['set'], after['match']) != ('1','1','1') or
                        before['bank'] != after['bank'] or after['bank_changed'] != '0' or
                        before['rng'] != after['restored'] or digest != receipt[5]):
                        raise ValueError('camera transition or identity mismatch')
                    active_anchor = {'manager': receipt[2], 'target': receipt[3], 'controller': receipt[4], 'digest': digest, 'selected_hash': before['context'], 'runtime': int(before['runtime'])}
                except (ValueError, IndexError, KeyError):
                    errors.append('context anchor receipt does not match complete camera proof')
            continue
        if camera_rows and not receipt_count and line.startswith(PREFIX):
            errors.append('interleaved record inside camera transition')
        if line.startswith(PREFIX + 'snapshot='):
            suffix = ANCHOR_SUFFIX.search(line[:-1]) if line.endswith('\n') else None
            if active_anchor and not policy['enabled']:
                if not suffix or suffix[1] != active_anchor['manager'] or suffix[2] != active_anchor['digest']:
                    errors.append('snapshot full context anchor missing or changed')
            elif suffix:
                errors.append('snapshot context anchor before valid receipt')
        if not line.endswith('\n') or not line.startswith(PREFIX):
            errors.append(f'line {line_number}: unexpected or incomplete line')
            continue
        text = line[len(PREFIX):-1]
        begin, end = BEGIN.fullmatch(text), END.fullmatch(text)
        if begin:
            if current_tick is not None or saw_final:
                errors.append('nested or late frame begin')
            else:
                current_tick, current_lines = int(begin[1]), []
            continue
        if end:
            if current_tick is None or int(end[1]) != current_tick:
                errors.append('frame end without matching begin')
            else:
                blocks.append((current_tick, current_lines))
                current_tick, current_lines = None, []
            continue
        if text == FINAL:
            if saw_final or current_tick is not None:
                errors.append('duplicate or premature final record')
            saw_final = True
            continue
        if current_tick is None or saw_final:
            errors.append('record outside a framed block')
        else:
            current_lines.append(line)
    if allow_camera and (receipt_count != 1 or active_anchor is None):
        errors.append('missing valid camera context anchor')
    if current_tick is not None:
        errors.append('unterminated frame')
    if not saw_final or returncode != 223:
        errors.append('missing final discard result or process exit 223')
    if [tick for tick, _ in blocks] != list(range(phase['take'], phase['apply'] + frame_count)):
        errors.append('frame generations are missing, duplicated or out of order')

    baseline = {}
    initial = canonical = taken = None
    baseline_bodies = None
    previous_time = previous_digital = None
    for tick, records in blocks:
        try:
            frame, frame_errors, thread = parse_frame(records, tick, baseline, previous_time, previous_digital, input_mode, allow_throttle, throttle_start_tick,
                                                       take_tick=phase['take'], apply_tick=phase['apply'])
        except (ValueError, OverflowError) as error:
            frame = {'tick': tick, 'events': [], 'snapshots': {}, 'inputs': [], 'update': None,
                     'samples': [], 'bodies': [], 'upkeep': [], 'body_comparisons': []}
            frame_errors, thread = ['malformed numeric field: ' + str(error)[:160]], None
        if tick == phase['take']:
            initial = frame['snapshots'].get('initial')
            canonical = frame['snapshots'].get('physics_canonical')
            taken = frame['snapshots'].get('taken')
            baseline = {'thread': thread, 'controller': taken.get('controller') if taken else None}
        for label, snapshot in frame['snapshots'].items():
            snapshot['context_policy'] = policy['snapshots'].get((tick, label))
        if initial and canonical and taken:
            frame_errors.extend(validate_frame_state(frame, initial, canonical, taken, baseline_bodies, active_anchor,
                                                     take_tick=phase['take'], apply_tick=phase['apply']))
        else:
            frame_errors.append('missing first-frame baseline')
        if active_anchor and tick == 6 and initial and (initial['nodes'] != '13' or hashlib.sha256(bytes.fromhex(initial['payloads'])).hexdigest() != active_anchor['selected_hash']):
            frame_errors.append('camera proof does not match initial selected contexts')
        if active_anchor and tick == 6 and (not frame['bodies'] or frame['bodies'][0]['ship'] != int(active_anchor['target'], 16) or frame['bodies'][0]['runtime'] != active_anchor['runtime'] or not canonical or canonical['controller'] != active_anchor['controller']):
            frame_errors.append('camera anchor target or controller identity mismatch')
        if tick == phase['take'] and len(frame['bodies']) == 4:
            baseline_bodies = frame['bodies'][:2]
        if frame['update']:
            previous_time = frame['update']['now']
        if len(frame['inputs']) == 2:
            previous_digital = frame['inputs'][1]['digital']
        frame['errors'] = frame_errors
        errors.extend(f'frame {tick}: {error}' for error in frame_errors)
        frames.append(frame)
    worker = parse_worker_evidence(stderr_lines, frame_count, first_tick=phase['take'],
                                   final_tick=phase['apply'] + frame_count - 1)
    errors.extend(worker['errors'])
    return {'pass': not errors, 'returncode': returncode, 'frame_count': frame_count,
            'frames': frames, 'worker': worker, 'errors': errors, 'presentation': presentation, 'context_policy': {
                'enabled': policy['enabled'], 'receipts': policy['receipts']}, 'movement_proven': False,
            'restored': False, 'resumed': False, 'other_threads_excluded': False}


def collect_recurring_physics_evidence(trace, retained, engine, output, returncode, *, frame_count, stderr,
                                       input_mode="scripted", allow_camera=False, allow_throttle=False, throttle_start_tick=5, presentation_slot=None, session_frames=0, startup=None):
    """Retain all trace bytes before parsing; failures never produce a pass."""
    errors = []
    trace_limit = NETWORK_MAX_TRACE_BYTES if input_mode == 'network' else MAX_TRACE_BYTES
    try:
        fd = os.open(trace, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, 'rb') as source:
            if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
                raise OSError('native trace is not a regular file')
            retained_fd = os.open(retained, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            digest = hashlib.sha256()
            chunks, total = [], 0
            with os.fdopen(retained_fd, 'wb') as destination:
                while True:
                    chunk = source.read(65536)
                    if not chunk:
                        break
                    destination.write(chunk)
                    digest.update(chunk)
                    total += len(chunk)
                    if total <= trace_limit:
                        chunks.append(chunk)
            if total > trace_limit:
                raise OSError(f'native trace exceeds {trace_limit // (1024 * 1024)} MiB; full trace retained')
        with Path(stderr).open(encoding='utf-8', errors='strict') as worker_file:
            worker_lines = worker_file.readlines()
        evidence = parse_recurring_physics_evidence(b''.join(chunks).decode('utf-8').splitlines(keepends=True),
                                                    worker_lines, returncode, frame_count=frame_count, input_mode=input_mode, allow_camera=allow_camera, allow_throttle=allow_throttle, throttle_start_tick=throttle_start_tick, presentation_slot=presentation_slot, session_frames=session_frames, startup=startup)
        evidence['log_sha256'] = digest.hexdigest()
    except (OSError, UnicodeError) as error:
        evidence = parse_recurring_physics_evidence([], [], returncode, frame_count=frame_count, input_mode=input_mode, allow_camera=allow_camera, allow_throttle=allow_throttle, throttle_start_tick=throttle_start_tick, presentation_slot=presentation_slot, session_frames=session_frames, startup=startup)
        errors.append('native trace or worker evidence unavailable/invalid: ' + str(error))
    for source in (engine, output, stderr):
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
