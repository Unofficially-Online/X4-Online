"""Offline seat-pose observations; this does not gate physics or UI acceptance."""
import hashlib
import re
import struct

from linux_player_physics_evidence import BODY, PREFIX, parse_body_row
from linux_player_presentation_evidence import ROW, PREFIX as PRESENTATION_PREFIX, validate_presentation_rows

POINTER = r'(0|[1-9a-f][0-9a-f]{0,15})'
NUMBER = r'(0|[1-9][0-9]{0,19})'
STAGES = ('before', 'during', 'after')
ROLES = ('p1', 'p2')


def pose_flags(raw):
    """Mirror the native float32 input / double arithmetic basis test."""
    words = [int(raw[i:i+8], 16) for i in range(0, 128, 8)]
    normalized = ''.join(f'{0 if word & 0x7fffffff == 0 else word:08x}' for word in words)
    finite = all(word & 0x7f800000 != 0x7f800000 for word in words)
    values = [struct.unpack('<f', struct.pack('<I', word))[0] for word in words]
    rigid = finite
    if rigid:
        for row in range(1, 4):
            for other in range(1, row+1):
                dot = 0.0
                for col in range(3):
                    dot += values[4*row+col] * values[4*other+col]
                if abs(dot - (1.0 if row == other else 0.0)) > 0.001:
                    rigid = False
        determinant = (values[4] * (values[9]*values[14] - values[10]*values[13]) -
                       values[5] * (values[8]*values[14] - values[10]*values[12]) +
                       values[6] * (values[8]*values[13] - values[9]*values[12]))
        if abs(determinant - 1.0) > 0.001:
            rigid = False
    return normalized, int(finite), int(rigid)


def _viewstate(line, stage, identity):
    prefix = f'X4 UI VIEWSTATE v=1 tick=9 stage={stage} '
    if line == prefix+'status=unavailable\n':
        return True
    grammar = (prefix+'status=observed '+
               ' '.join(key+'='+POINTER for key in ('manager', 'presenter', 'controller', 'universe'))+
               ' selector='+NUMBER+r' first=([01]) plan_len='+NUMBER+
               r' plan=(-|(?:[0-9a-f]{2}){1,32}) camera_candidate='+POINTER+
               ' camera_mode='+NUMBER+' external_target='+POINTER+' target_id='+NUMBER+r'\n')
    match = re.fullmatch(grammar, line)
    if not match:
        return False
    values = match.groups()
    return (all(int(values[i], 16) == identity.get(key) for i, key in
                enumerate(('manager', 'presenter', 'controller'))) and
            int(values[4]) <= 0xffffffff and int(values[9]) <= 0xffffffff and
            int(values[11]) <= 0xffffffffffffffff and
            int(values[6]) == (0 if values[7] == '-' else len(values[7])//2))


def parse_ui_seat_pose_evidence(stderr_lines, trace_lines, slot, enabled, overlay, fork):
    result = {'pass': False, 'enabled': enabled and slot == 2, 'errors': [],
              'observation_complete': False, 'pose_observation_complete': False,
              'readiness_proven': False, 'renderer_use_proven': False, 'safe_resume_proven': False}
    errors = result['errors']
    seats = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 UI SEAT_POSE' in line]
    result['raw_native_receipts'] = [line for _, line in seats]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid seat-pose activation or slot')
        return result
    if not enabled or slot == 1:
        if seats:
            errors.append('unexpected disabled or host seat-pose evidence')
        result['coverage'] = 'disabled'
        result['pass'] = not errors
        return result
    if len(seats) != 6 or any(len(line) >= 768 or not line.endswith('\n') for _, line in seats):
        errors.append('missing, duplicate, truncated or oversized seat-pose rows')
        result['coverage'] = 'missing' if len(seats) < 6 else 'invalid'
        return result
    presentation = validate_presentation_rows(trace_lines, slot)
    if (not presentation['pass'] or overlay.get('pass') is not True or
            type(overlay.get('callback_count')) is not int or
            overlay.get('callback_count') not in (1, 8, 160) or fork.get('pass') is not True):
        errors.append('seat-pose requires accepted overlay, fork and presentation evidence')
    proof = fork.get('measurements', {})
    bindings = {}
    for line in trace_lines:
        if line.startswith(PRESENTATION_PREFIX):
            match = ROW.fullmatch(line[len(PRESENTATION_PREFIX):])
            if match:
                bindings[match[1]] = {'actor': int(match[3], 16), 'room': int(match[4], 16),
                                      'bank': int(match[9])}
    if (set(bindings) != set(ROLES) or any(bindings[role]['actor'] != proof.get(role) for role in ROLES) or
            bindings.get('p2', {}).get('room') != proof.get('room') or
            len({binding['bank'] for binding in bindings.values()}) != 1):
        errors.append('seat-pose retained player/room/bank binding unavailable')
    body_ships = set()
    for line in trace_lines:
        if line.startswith(PREFIX+'physics=body ') and len(line) < 4096:
            match = BODY.fullmatch(line[len(PREFIX):].rstrip('\n'))
            if match:
                try:
                    body = parse_body_row(match)
                    if body['index'] == 0 and body['runtime'] == proof.get('p2_ship'):
                        body_ships.add(body['ship'])
                except (ValueError, OverflowError):
                    errors.append('invalid retained body identity')
    if len(body_ships) != 1:
        errors.append('seat-pose retained P2 ship pointer binding unavailable')
    samples = []
    for (index, line), (stage, role) in zip(seats, ((stage, role) for stage in STAGES for role in ROLES)):
        prefix = f'X4 UI SEAT_POSE v=1 tick=9 stage={stage} role={role} '
        sample = {'stage': stage, 'role': role, 'observed': False, 'pose_observed': False}
        samples.append(sample)
        if line == prefix+'status=unavailable\n':
            continue
        grammar = (prefix+'status=observed '+
                   ' '.join(key+'='+POINTER for key in ('actor', 'ship', 'room', 'parent', 'controller', 'vtable'))+
                   r' bank=([01]) view=1 position='+NUMBER+' candidate='+POINTER+
                   r' finite=(-1|[01]) rigid=(-1|[01]) words=(-|[0-9a-f]{128}) raw=(-|[0-9a-f]{128})\n')
        match = re.fullmatch(grammar, line)
        if not match:
            errors.append('seat-pose stage/role order or observed grammar invalid')
            continue
        keys = ('actor', 'ship', 'room', 'parent', 'controller', 'vtable', 'bank', 'position',
                'candidate', 'finite', 'rigid', 'words', 'raw')
        values = dict(zip(keys, match.groups()))
        for key in keys[:-2]:
            values[key] = int(values[key], 10 if key in ('bank', 'position', 'finite', 'rigid') else 16)
        sample.update(values, observed=True, pose_observed=bool(values['candidate']))
        for key, extent in (('actor', 0x6aac), ('ship', 16), ('room', 0x78),
                            ('parent', 1), ('controller', 8), ('vtable', 8)):
            address = values[key]
            nullable = key in ('parent', 'controller', 'vtable')
            if not (nullable and address == 0) and not 0x10000 <= address <= 0x00007fffffffffff - extent:
                errors.append('seat-pose '+key+' outside native user-address bounds')
        if values['position'] > 0xffffffff:
            errors.append('seat-pose position outside uint32 range')
        if any(values[key] != bindings.get(role, {}).get(key) for key in ('actor', 'room', 'bank')):
            errors.append('seat-pose player/room/bank identity mismatch')
        if not values['ship'] or role == 'p2' and values['ship'] not in body_ships:
            errors.append('seat-pose ship pointer mismatch')
        if bool(values['controller']) != bool(values['vtable']):
            errors.append('seat-pose controller/vtable nullness mismatch')
        if not values['candidate']:
            if (values['finite'], values['rigid'], values['words'], values['raw']) != (-1, -1, '-', '-'):
                errors.append('null seat-pose candidate has invented pose data')
        elif (values['candidate'] % 16 or values['candidate'] < 0x10000 or
              values['candidate'] > 0x00007fffffffffff - 64 or values['raw'] == '-' or values['words'] == '-'):
            errors.append('seat-pose nonnull candidate alignment, bounds or words invalid')
        else:
            normalized, finite, rigid = pose_flags(values['raw'])
            if (values['words'], values['finite'], values['rigid']) != (normalized, finite, rigid):
                errors.append('seat-pose raw normalization or finite/rigid flags contradict words')
    for role in ROLES:
        if len({s['ship'] for s in samples if s['role'] == role and s['observed']}) > 1:
            errors.append('seat-pose per-role ship observation changed')
    views = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 UI VIEWSTATE' in line]
    if len(views) != 3:
        errors.append('seat-pose requires exactly three corresponding VIEWSTATE rows')
    else:
        for n, stage in enumerate(STAGES):
            if (not _viewstate(views[n][1], stage, presentation.get('ui_plan', {})) or
                    not seats[2*n][0] < seats[2*n+1][0] < views[n][0] or
                    n < 2 and not views[n][0] < seats[2*n+2][0]):
                errors.append('seat-pose pair is not before its corresponding VIEWSTATE boundary')
    native_fork = fork.get('raw_native_receipts', [])
    prior = [i for i, line in enumerate(stderr_lines) if len(native_fork) == 1 and line == native_fork[0]]
    overlay_rows = overlay.get('raw_native_receipts', [])
    first_overlay = [i for i, line in enumerate(stderr_lines) if overlay_rows and line == overlay_rows[0]]
    if len(prior) != 1 or not prior[0] < seats[0][0] or len(first_overlay) != 1 or not views or not views[-1][0] < first_overlay[0]:
        errors.append('seat-pose observations outside accepted native fork/first-overlay interval')
    observed = sum(sample['observed'] for sample in samples)
    poses = sum(sample['pose_observed'] for sample in samples)
    result.update(samples=samples, observed_samples=observed, observed_poses=poses,
                  coverage='invalid' if errors else 'complete' if observed == 6 else 'partial' if observed else 'unavailable')
    result['pass'] = not errors
    result['observation_complete'] = not errors and observed == 6
    result['pose_observation_complete'] = not errors and poses == 6
    result['limitations'] = ['P1 ship pointer is a stable observation, not independently bound to a prior pointer receipt.',
                            'Parent/controller/position may differ across stages; candidates do not prove renderer use.']
    return result


def collect_ui_seat_pose_evidence(profile, stderr, slot, enabled, overlay, fork):
    paths = {'engine_sha256': profile/'engine-output.log', 'stderr_sha256': stderr,
             'native_fd_sha256': profile/'controller-lifecycle.log'}
    lines, hashes, errors = {}, {}, []
    for key, path in paths.items():
        try:
            data = path.read_bytes()
            lines[key] = data.decode('utf-8', errors='strict').splitlines(keepends=True)
            hashes[key] = hashlib.sha256(data).hexdigest()
        except (OSError, UnicodeError) as error:
            lines[key] = []
            if (enabled and slot == 2) or key == 'stderr_sha256':
                errors.append('seat-pose evidence unavailable: '+str(error))
    result = parse_ui_seat_pose_evidence(lines['stderr_sha256'], lines['native_fd_sha256'], slot, enabled, overlay, fork)
    result.update(hashes)
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or overlay.get(key) != hashes[key] or fork.get(key) != hashes[key]:
                errors.append('seat-pose overlay/fork proof refers to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = result['observation_complete'] = result['pose_observation_complete'] = False
        result['coverage'] = 'invalid'
    return result
