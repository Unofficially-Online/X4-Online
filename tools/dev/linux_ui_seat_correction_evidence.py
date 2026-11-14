"""Bounded coupled seat correction evidence, never persistent UI acceptance."""
import hashlib
from linux_ui_session_budget import budget_from_overlay
import math
import re
import struct

from linux_ui_live_pose_observer_evidence import NUMBER, POINTER
from linux_ui_seat_pose_evidence import pose_flags

HEX_POSE = r'([0-9a-f]{128})'
TOLERANCE = 1e-6


def floats(raw):
    return list(struct.unpack('<16f', bytes.fromhex(raw)))


def rigid(raw):
    words = struct.unpack('<16I', bytes.fromhex(raw))
    return pose_flags(''.join(f'{word:08x}' for word in words))[2] == 1


def rounded(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def expected_delta(canonical, guest):
    """Mirror inverse(canonical seat) * guest seat in native row order."""
    if not rigid(canonical) or not rigid(guest):
        raise ValueError('queried seats must both be finite rigid transforms')
    left, right = floats(canonical), floats(guest)
    result = [0.0]*16
    if all(left[4*row+col] == right[4*row+col] for row in range(4) for col in range(3)):
        result[4] = result[9] = result[14] = 1.0
        return result
    inverse = []
    for col in range(3):
        value = 0.0
        for k in range(3):
            value -= left[k]*left[4*(col+1)+k]
        inverse.append(value)
    for row in range(3):
        for col in range(3):
            value = 0.0
            for k in range(3):
                value += left[4*(k+1)+row]*right[4*(k+1)+col]
            result[4*(row+1)+col] = rounded(value)
    for col in range(3):
        value = right[col]
        for k in range(3):
            value += inverse[k]*right[4*(k+1)+col]
        result[col] = rounded(value)
    return result


def expected_output(raw, delta):
    original, transform = floats(raw), floats(delta)
    result = list(original)
    identity = all(transform[4*row+col] == (1.0 if row == col+1 else 0.0)
                   for row in range(4) for col in range(3))
    if not identity:
        for row in range(4):
            for col in range(3):
                value = transform[col] if row == 0 else 0.0
                for k in range(3):
                    value += original[4*row+k]*transform[4*(k+1)+col]
                result[4*row+col] = rounded(value)
    return result


def coordinates_match(actual, expected):
    return all(math.isclose(actual[4*row+col], expected[4*row+col],
                            rel_tol=TOLERANCE, abs_tol=TOLERANCE)
               for row in range(4) for col in range(3))


def output_matches(raw, delta, corrected):
    transform = floats(delta)
    identity = all(transform[4*row+col] == (1.0 if row == col+1 else 0.0)
                   for row in range(4) for col in range(3))
    return (rigid(raw) and rigid(delta) and
            (raw == corrected if identity else
             coordinates_match(floats(corrected), expected_output(raw, delta))) and
            all(bytes.fromhex(raw)[i:i+4] == bytes.fromhex(corrected)[i:i+4]
                for i in (12, 28, 44, 60)))


def parse_ui_seat_correction_evidence(lines, slot, enabled, overlay, fork, query, legacy, scope, render=None):
    result = {'pass': False, 'enabled': enabled and slot == 2, 'errors': [],
              'correction_observed': False, 'readiness_proven': False,
              'safe_resume_proven': False, 'reader_exclusion_proven': False}
    errors = result['errors']
    try:
        budget = budget_from_overlay(overlay)
    except ValueError as error:
        errors.append(str(error))
        budget = {'callbacks':160, 'last':168}
    callback_count, last_tick = budget['callbacks'], budget['last']
    first_tick = budget.get('first', 9)
    rows = [(i, line) for i, line in enumerate(lines) if 'X4 UI SEAT_CORRECTION' in line]
    result['raw_native_receipts'] = [line for _, line in rows]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid seat correction activation or slot')
        return result
    if not enabled or slot == 1:
        if rows:
            errors.append('unexpected disabled or host seat correction receipts')
        result['pass'] = not errors
        return result
    proofs = (overlay, fork, query, legacy, scope) + ((render,) if render is not None else ())
    if any(proof.get('pass') is not True for proof in proofs) or overlay.get('callback_count') != callback_count:
        errors.append('correction requires accepted bounded overlay/fork/query/live/scope evidence')
    if not 5 <= len(rows) <= 6 or any(len(line) >= 1024 or not line.endswith('\n') for _, line in rows):
        errors.append('missing, duplicate, oversized or truncated correction receipts')
        return result
    ready = re.fullmatch(f'X4 UI SEAT_CORRECTION v=1 stage=ready tick={first_tick} target='+POINTER+
                         ' delta='+HEX_POSE+' stack_low='+POINTER+' stack_high='+POINTER+r'\n', rows[0][1])
    close = re.fullmatch(f'X4 UI SEAT_CORRECTION v=1 stage=close tick={last_tick} applies='+NUMBER+
                         ' restores='+NUMBER+' calls='+NUMBER+' rows='+NUMBER+
                         r' published_restored=1 state=outside\n', rows[-1][1])
    published = re.fullmatch(f'X4 UI SEAT_CORRECTION v=1 stage=published tick={first_tick} slot='+POINTER+
                             ' original='+POINTER+' scratch='+POINTER+' raw='+HEX_POSE+
                             ' corrected='+HEX_POSE+r'\n', rows[1][1])
    restored = re.fullmatch(f'X4 UI SEAT_CORRECTION v=1 stage=published_restore tick={first_tick} slot='+POINTER+
                            ' original='+POINTER+' observed='+POINTER+r'\n', rows[2][1])
    if not ready or not published or not restored or not close:
        errors.append('invalid correction ready/terminal grammar, generation or restored state')
        return result
    target, delta, low, high = ready.groups()
    target, low, high = int(target, 16), int(low, 16), int(high, 16)
    counts = dict(zip(('applies', 'restores', 'calls', 'rows'), map(int, close.groups())))
    result.update(target=target, delta=delta, stack_low=low, stack_high=high, counts=counts)
    if (target != legacy.get('target') or target & 7 or
            not 0x10000 <= target <= 0x00007fffffffffff or
            not 0x10000 <= low < high <= 0x0000800000000000 or high-low < 64):
        errors.append('correction target or authenticated stack range invalid')
    frames = overlay.get('frames', [])
    render_count = 0
    if render is not None:
        if render.get('pass') is not True or render.get('lease_count') != callback_count:
            errors.append('correction render mode requires accepted GameRender leases')
        render_count = render.get('lease_count', 0)
    refill_count = render.get('camera_refill_count', 0) if render else 0
    projection_count = render.get('projection_fov_count', 0) if render else 0
    if (counts['applies'] > 0xffffffff or counts['restores'] > 0xffffffff or
            not 1 <= counts['calls'] <= 0xffffffffffffffff or
            counts['calls'] != scope.get('counts', {}).get('open') or
            counts['applies'] != sum(frame.get('applies', -1) for frame in frames) + render_count + refill_count + projection_count or
            counts['restores'] != sum(frame.get('restores', -1) for frame in frames) + render_count + refill_count + projection_count or
            counts['applies'] != counts['restores'] or
            counts['rows'] != min(counts['calls'], 2) or len(rows)-4 != counts['rows']):
        errors.append('correction counters disagree with first-two samples, open calls or overlay transitions')
    try:
        seats = query.get('observations', [])
        if [seat['role'] for seat in seats] != ['p1', 'p2']:
            raise ValueError('two ordered queried seats required')
        # Query raw is textual uint32 words; correction matrices are byte dumps.
        seat_bytes = [b''.join(struct.pack('<I', int(seat['raw'][i:i+8], 16))
                              for i in range(0, 128, 8)).hex() for seat in seats]
        expected = expected_delta(*seat_bytes)
        left, right = map(floats, seat_bytes)
        same_seat = all(left[4*row+col] == right[4*row+col]
                        for row in range(4) for col in range(3))
        if same_seat and bytes.fromhex(delta) != struct.pack('<16f', *expected):
            errors.append('equal-seat fast path requires exact identity delta bytes')
        if (not rigid(delta) or not coordinates_match(floats(delta), expected) or
                any(bytes.fromhex(delta)[i:i+4] != b'\0'*4 for i in (12, 28, 44, 60))):
            errors.append('correction delta contradicts queried seats or zero metadata lanes')
    except (ValueError, OverflowError, KeyError, struct.error):
        errors.append('correction delta cannot be validated from finite rigid queried seats')
    slot_pointer, original, scratch, published_raw, published_corrected = published.groups()
    slot_pointer, original, scratch = (int(value, 16) for value in (slot_pointer, original, scratch))
    restore_slot, restore_original, observed = (int(value, 16) for value in restored.groups())
    p1 = next((seat for seat in query.get('observations', []) if seat.get('role') == 'p1'), {})
    actor, bank = p1.get('actor', 0), p1.get('bank', -1)
    expected_slot = actor + 0xe0 + bank*0x80 + 0x28
    def overlaps(left, left_size, right, right_size):
        return left < right+right_size and right < left+left_size
    if (slot_pointer != expected_slot or slot_pointer & 7 or
            (restore_slot, restore_original, observed) != (slot_pointer, original, original) or
            any(value & 15 or not 0x10000 <= value <= 0x00007fffffffffff-63
                for value in (original, scratch)) or
            overlaps(original, 64, scratch, 64) or overlaps(original, 64, slot_pointer, 8) or
            overlaps(scratch, 64, actor, 0x6aac)):
        errors.append('first published lease slot/pointers or observed original restoration invalid')
    try:
        if not output_matches(published_raw, delta, published_corrected):
            errors.append('first published scratch contradicts rigid delta transform or exact metadata')
    except (ValueError, OverflowError, struct.error):
        errors.append('first published scratch transform cannot be validated')
    result['first_published_lease'] = {
        'slot': slot_pointer, 'original': original, 'scratch': scratch,
        'raw': published_raw, 'corrected': published_corrected, 'restored_observed': observed,
        'corrected_rigid': rigid(published_corrected)}
    samples = []
    open_samples = [sample for sample in scope.get('representatives', []) if sample['scope'] == 'open']
    for index, (_, line) in enumerate(rows[3:-1]):
        match = re.fullmatch('X4 UI SEAT_CORRECTION v=1 stage=sample ordinal='+NUMBER+
                             ' last_move='+NUMBER+r' caller=1314317 time=([0-9a-f]{16}) raw='+HEX_POSE+
                             ' corrected='+HEX_POSE+r'\n', line)
        if not match:
            errors.append('invalid correction sample grammar or caller')
            continue
        ordinal, generation, time, raw, corrected = match.groups()
        sample = {'ordinal': int(ordinal), 'last_move': int(generation), 'caller': 0x1314317,
                  'time_bits': time, 'raw': raw, 'corrected': corrected, 'corrected_rigid': rigid(corrected)}
        samples.append(sample)
        seconds = struct.unpack('<d', int(time, 16).to_bytes(8, 'little'))[0]
        passive = open_samples[index] if index < len(open_samples) else {}
        phase_valid = passive.get('move') == 1 and passive.get('render') == 0
        if render is not None:
            # The outer GameRender depth is authenticated by the provider but
            # is not encoded in the legacy UI callback depth fields.
            phase_valid = phase_valid or passive.get('move') == 0
        if (not first_tick <= sample['last_move'] <= last_tick or not math.isfinite(seconds) or seconds < 0 or
                any(sample[key] != passive.get(key) for key in ('ordinal', 'last_move', 'caller', 'time_bits')) or
                passive.get('mode') != 1 or not phase_valid or
                passive.get('after_valid') != 1 or raw != passive.get('after')):
            errors.append('correction sample contradicts first open raw-stock call or native mode/time bounds')
        try:
            if not output_matches(raw, delta, corrected):
                errors.append('corrected output contradicts computed transform or exact metadata preservation')
        except (ValueError, OverflowError, struct.error):
            errors.append('corrected transform cannot be validated')
    result['samples'] = samples
    def positions(prefix):
        return [i for i, line in enumerate(lines) if line.startswith(prefix)]
    bind = positions(f'X4 UI LIVE_POSE v=1 stage=bind tick={first_tick} ')
    during = positions(f'X4 UI SEAT_POSE v=1 tick={first_tick} stage=during role=p1 ')
    during_view = positions(f'X4 UI VIEWSTATE v=1 tick={first_tick} stage=during ')
    after = positions(f'X4 UI SEAT_POSE v=1 tick={first_tick} stage=after role=p1 ')
    scope_close = positions(f'X4 UI POSE_SCOPE v=1 stage=close tick={last_tick} ')
    final_frame = positions(f'X4 UI OVERLAY FRAME v=3 tick={last_tick} ordinal={callback_count} count={callback_count} ')
    if render is not None:
        final_frame = [i for i, line in enumerate(lines) if line == render.get('observation_close_receipt', render.get('close_receipt'))]
    if (any(len(values) != 1 for values in (bind, during, during_view, after, scope_close, final_frame)) or
            not bind[0] < rows[0][0] < rows[1][0] < during[0] < during_view[0] < rows[2][0] < after[0] or
            not scope_close[0] < rows[3][0] <= rows[-1][0] < final_frame[0]):
        errors.append('correction ready or deferred samples/close outside authenticated causal boundary')
    result['pass'] = not errors
    result['correction_observed'] = not errors and counts['calls'] > 0
    result['sample_coverage_complete'] = not errors and counts['calls'] <= 2
    result['limitations'] = ['Raw-stock observer samples remain unchanged; correction samples describe final writes separately.',
                            'Published-pose restoration is measured ownership, not reader exclusion, continuous object lifetime or safe resume.',
                            'At most two corrected calls are retained; no complete route coverage or rendered P2 view is established.']
    return result


def collect_ui_seat_correction_evidence(profile, stderr, slot, enabled, overlay, fork, query, legacy, scope, render=None):
    paths = {'engine_sha256': profile/'engine-output.log', 'stderr_sha256': stderr,
             'native_fd_sha256': profile/'controller-lifecycle.log'}
    hashes, lines, errors = {}, [], []
    for key, path in paths.items():
        try:
            data = path.read_bytes()
            hashes[key] = hashlib.sha256(data).hexdigest()
            if key == 'stderr_sha256':
                lines = data.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if (enabled and slot == 2) or key == 'stderr_sha256':
                errors.append('correction input unavailable: '+str(error))
    result = parse_ui_seat_correction_evidence(lines, slot, enabled, overlay, fork, query, legacy, scope, render)
    result.update(hashes)
    proofs = (overlay, fork, query, legacy, scope) + ((render,) if render is not None else ())
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or any(proof.get(key) != hashes[key] for proof in proofs):
                errors.append('correction proof refers to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = result['correction_observed'] = result['sample_coverage_complete'] = False
    return result
