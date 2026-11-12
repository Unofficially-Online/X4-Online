"""Strict receipts for the opt-in canonical parent-worker stock seat query.

This validates one two-ship attempt, not read-only behavior, pose installation,
rendered camera selection, gameplay readiness or safe resumption.
"""
import hashlib
import re

from linux_ui_seat_pose_evidence import collect_ui_seat_pose_evidence, pose_flags

POINTER = r'(0|[1-9a-f][0-9a-f]{0,15})'
NUMBER = r'(0|[1-9][0-9]{0,19})'


def parse_ui_seat_query_evidence(stderr_lines, slot, enabled, overlay, fork, seats):
    result = {'pass': False, 'enabled': enabled and slot == 2, 'errors': [],
              'attempt_complete': False, 'readiness_proven': False, 'read_only_proven': False,
              'pose_installation_proven': False, 'safe_resume_proven': False}
    errors = result['errors']
    rows = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 UI SEAT_QUERY' in line]
    result['raw_native_receipts'] = [line for _, line in rows]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid seat-query activation or slot')
        return result
    if not enabled or slot == 1:
        if rows:
            errors.append('unexpected disabled or host seat-query evidence')
        result['pass'] = not errors
        return result
    if len(rows) != 3 or any(len(line) >= 1024 or not line.endswith('\n') for _, line in rows):
        errors.append('missing, duplicate, truncated or oversized seat-query receipts')
        return result
    if (overlay.get('pass') is not True or type(overlay.get('callback_count')) is not int or
            overlay.get('callback_count') != 160 or fork.get('pass') is not True):
        errors.append('seat-query requires accepted overlay160 and fork proofs')
    baseline = {sample['role']: sample for sample in seats.get('samples', [])
                if sample['stage'] == 'before' and sample['observed']}
    if seats.get('pass') is not True or set(baseline) != {'p1', 'p2'}:
        errors.append('seat-query before-census identity binding unavailable')
    observations = []
    for (_, line), role in zip(rows[:2], ('p1', 'p2')):
        grammar = (f'X4 UI SEAT_QUERY v=1 tick=9 role={role} return=1 '+
                   ' '.join(key+'='+POINTER for key in ('actor', 'ship', 'room'))+
                   ' position='+NUMBER+r' bank=([01]) rng_before=([0-9a-f]{16}) '+
                   r'rng_after=([0-9a-f]{16}) rng_restored=([0-9a-f]{16}) contexts_before='+NUMBER+
                   r' hash_before=([0-9a-f]{64}) contexts_after='+NUMBER+
                   r' hash_after=([0-9a-f]{64}) finite=([01]) rigid=([01]) raw=([0-9a-f]{128}) words=([0-9a-f]{128})\n')
        match = re.fullmatch(grammar, line)
        if not match:
            errors.append('seat-query role order, return value or full grammar invalid')
            continue
        keys = ('actor', 'ship', 'room', 'position', 'bank', 'rng_before', 'rng_after',
                'rng_restored', 'contexts_before', 'hash_before', 'contexts_after', 'hash_after',
                'finite', 'rigid', 'raw', 'words')
        sample = dict(zip(keys, match.groups()))
        sample['role'] = role
        for key in keys:
            if key not in ('hash_before', 'hash_after', 'raw', 'words'):
                sample[key] = int(sample[key], 16 if key in
                                  ('actor', 'ship', 'room', 'rng_before', 'rng_after', 'rng_restored') else 10)
        for key, extent in (('actor', 0x6aac), ('ship', 16), ('room', 0x78)):
            if not 0x10000 <= sample[key] <= 0x00007fffffffffff - extent:
                errors.append('seat-query identity outside native user-address bounds')
        if any(sample[key] > 0xffffffff for key in ('position', 'contexts_before', 'contexts_after')):
            errors.append('seat-query field outside uint32 range')
        if any(sample[key] != baseline.get(role, {}).get(key) for key in ('actor', 'ship', 'room', 'bank')):
            errors.append('seat-query retained actor/ship/room/bank identity mismatch')
        if sample['rng_before'] != sample['rng_restored']:
            errors.append('seat-query TLS RNG restoration mismatch')
        normalized, finite, rigid = pose_flags(sample['raw'])
        if (sample['words'], sample['finite'], sample['rigid']) != (normalized, finite, rigid):
            errors.append('seat-query raw normalization or finite/rigid flags contradict words')
        sample['context_changed'] = (sample['contexts_before'], sample['hash_before']) != (
            sample['contexts_after'], sample['hash_after'])
        observations.append(sample)
    complete = 'X4 UI SEAT_QUERY v=1 tick=9 stage=complete calls=2 p1_holds_released=1\n'
    if rows[-1][1] != complete:
        errors.append('seat-query completion, call count or hold release invalid')
    before_view = [(i, line) for i, line in enumerate(stderr_lines)
                   if line.startswith('X4 UI VIEWSTATE v=1 tick=9 stage=before ')]
    before_p2 = seats.get('raw_native_receipts', [])[1:2]
    during_p1 = seats.get('raw_native_receipts', [])[2:3]
    prior = [i for i, line in enumerate(stderr_lines) if before_p2 and line == before_p2[0]]
    following = [i for i, line in enumerate(stderr_lines) if during_p1 and line == during_p1[0]]
    if (len(before_view) != 1 or len(prior) != 1 or len(following) != 1 or
            not prior[0] < before_view[0][0] < rows[0][0] < rows[1][0] < rows[2][0] < following[0]):
        errors.append('seat-query attempt is not between before-VIEWSTATE and during-seat observations')
    # Accepted same-stream census and overlay proofs already order the native fork and UI callback.
    result['observations'] = observations
    result['rows_complete'] = len(observations) == 2 and rows[-1][1] == complete
    result['pass'] = not errors
    result['attempt_complete'] = not errors
    result['limitations'] = ['Two stock calls may allocate cache or UI anchors; recorded context/RNG fields do not exclude other effects.',
                            'Position is bounded and checked by the producer per call, not equated with an earlier census.',
                            'Scratch output is not installed as an actor/camera pose.']
    return result


def collect_ui_seat_query_evidence(profile, stderr, slot, enabled, overlay, fork):
    seats = collect_ui_seat_pose_evidence(profile, stderr, slot, enabled, overlay, fork)
    paths = {'engine_sha256': profile/'engine-output.log', 'stderr_sha256': stderr,
             'native_fd_sha256': profile/'controller-lifecycle.log'}
    hashes, errors, lines = {}, [], []
    for key, path in paths.items():
        try:
            data = path.read_bytes()
            hashes[key] = hashlib.sha256(data).hexdigest()
            if key == 'stderr_sha256':
                lines = data.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if (enabled and slot == 2) or key == 'stderr_sha256':
                errors.append('seat-query input unavailable: '+str(error))
    result = parse_ui_seat_query_evidence(lines, slot, enabled, overlay, fork, seats)
    result.update(hashes)
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or any(proof.get(key) != hashes[key] for proof in (overlay, fork, seats)):
                errors.append('seat-query proof refers to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = result['attempt_complete'] = False
    return result
