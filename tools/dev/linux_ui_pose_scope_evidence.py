"""Supplemental owner-notified scopes for the resident FPC entry observer.

Representatives are existing calls, identified by their global 1-based ordinal.
They are not extra invocations or complete per-call scope/renderer coverage.
"""
import hashlib
from linux_ui_session_budget import budget_from_overlay
import re

from linux_ui_live_pose_observer_evidence import NUMBER, POINTER, PHASES, MODES

SCOPES = ('outside', 'open', 'paused', 'changing')
SAMPLE_FIELDS = ('last_move', 'caller', 'time_bits', 'mode', 'move', 'render',
                 'before_valid', 'after_valid', 'before', 'after')


def parse_ui_pose_scope_evidence(stderr_lines, slot, enabled, legacy, overlay, render=None):
    result = {
        'pass': False,
        'enabled': enabled and slot == 2,
        'errors': [],
        'scope_coverage_complete': False,
        'readiness_proven': False,
        'safe_resume_proven': False,
        'reader_exclusion_proven': False,
    }
    errors = result['errors']
    try:
        budget = budget_from_overlay(overlay)
    except ValueError as error:
        errors.append(str(error))
        budget = {'callbacks':160, 'last':168}
    callback_count, last_tick = budget['callbacks'], budget['last']
    first_tick = budget.get('first', 9)
    rows = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 UI POSE_SCOPE' in line]
    result['raw_native_receipts'] = [line for _, line in rows]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid pose-scope activation or slot')
        return result
    if not enabled or slot == 1:
        if rows:
            errors.append('unexpected disabled or host pose-scope receipts')
        result['pass'] = not errors
        return result
    if not 1 <= len(rows) <= 9 or any(len(line) >= 1024 or not line.endswith('\n') for _, line in rows):
        errors.append('missing, duplicate, truncated or oversized pose-scope receipts')
        return result
    close_fields = (*SCOPES, 'opens', 'pauses', 'closes')
    close = re.fullmatch(fr'X4 UI POSE_SCOPE v=1 stage=close tick={last_tick} '+
                         ' '.join(key+'='+NUMBER for key in close_fields)+r' state=outside\n', rows[-1][1])
    if not close:
        errors.append('invalid pose-scope terminal grammar, generation or final state')
        return result
    counts = {key: int(value) for key, value in zip(close_fields, close.groups())}
    result['counts'] = counts
    if any(value > 0xffffffffffffffff for value in counts.values()):
        errors.append('pose-scope counter outside uint64 range')
    totals = legacy.get('counts', {})
    frames = overlay.get('frames', [])
    if (legacy.get('pass') is not True or overlay.get('pass') is not True or
            overlay.get('callback_count') != callback_count or len(frames) != callback_count):
        errors.append('pose-scope requires accepted legacy observer and overlay160 evidence')
    if sum(counts[key] for key in SCOPES) != totals.get('started'):
        errors.append('pose-scope call counts do not equal legacy started calls')
    render_count = 0
    if render is not None:
        if render.get('pass') is not True or render.get('lease_count') != callback_count:
            errors.append('pose-scope render mode requires accepted GameRender leases')
        render_count = render.get('lease_count', 0)
    refill_count = render.get('camera_refill_count', 0) if render else 0
    if (counts['closes'] != callback_count + render_count + refill_count or counts['opens'] != counts['closes'] + counts['pauses'] or
            counts['pauses'] != sum(frame.get('updates', -1) for frame in frames) or
            counts['opens'] != sum(frame.get('applies', -1) for frame in frames) + render_count + refill_count or
            counts['pauses'] + counts['closes'] != sum(frame.get('restores', -1) for frame in frames) + render_count + refill_count):
        errors.append('pose-scope transitions do not match balanced overlay apply/pause/return receipts')
    representatives = []
    for _, line in rows[:-1]:
        match = re.fullmatch(r'X4 UI POSE_SCOPE v=1 stage=sample scope=(outside|open|paused|changing) ordinal='+NUMBER+
                             ' last_move='+NUMBER+' caller='+POINTER+r' time=([0-9a-f]{16}) mode='+NUMBER+
                             ' move='+NUMBER+' render='+NUMBER+r' before_valid=([01]) after_valid=([01]) '+
                             r'before=([0-9a-f]{128}) after=([0-9a-f]{128})\n', line)
        if not match:
            errors.append('invalid pose-scope representative grammar')
            continue
        keys = ('scope', 'ordinal', *SAMPLE_FIELDS)
        sample = dict(zip(keys, match.groups()))
        for key in keys:
            if key not in ('scope', 'time_bits', 'before', 'after'):
                sample[key] = int(sample[key], 16 if key == 'caller' else 10)
        if (not 1 <= sample['ordinal'] <= totals.get('started', 0) or
                not first_tick <= sample['last_move'] <= last_tick or sample['mode'] > 255 or
                max(sample['move'], sample['render']) > 0xffffffff or
                not 0x10000 <= sample['caller'] <= 0x00007fffffffffff):
            errors.append('pose-scope representative ordinal or native field bounds invalid')
        for phase in ('before', 'after'):
            if not sample[phase+'_valid'] and sample[phase] != '0'*128:
                errors.append('unavailable pose-scope bytes must be zero filled')
        representatives.append(sample)
    expected_scopes = [scope for scope in SCOPES for _ in range(min(counts[scope], 2))]
    if [sample['scope'] for sample in representatives] != expected_scopes:
        errors.append('pose-scope requires first two representatives per nonempty state in scope order')
    ordinals = [sample['ordinal'] for sample in representatives]
    if len(set(ordinals)) != len(ordinals):
        errors.append('pose-scope representative call ordinal repeated across states')
    for scope in SCOPES:
        group = [sample['ordinal'] for sample in representatives if sample['scope'] == scope]
        if group != sorted(group):
            errors.append('pose-scope representative ordinals out of order within state')
    # Before a state's second representative, an unrepresented call cannot
    # belong to that state. Check prefix feasibility without iterating counters.
    second = {scope: group[1] for scope in SCOPES
              if len(group := [sample['ordinal'] for sample in representatives
                               if sample['scope'] == scope]) == 2}
    for index, ordinal in enumerate(sorted(set(ordinals))):
        unseen_before = ordinal - 1 - index
        capacity = sum(max(counts[scope] - 2, 0) for scope in SCOPES
                       if second.get(scope, 0xffffffffffffffff) < ordinal)
        if unseen_before > capacity:
            errors.append('pose-scope representatives cannot be the first two calls per state')
            break
    chronological = sorted(representatives, key=lambda sample: sample['ordinal'])
    generations = [sample['last_move'] for sample in chronological]
    if generations != sorted(generations):
        errors.append('pose-scope call generations decrease with global ordinal')
    union = {sample['index']+1: sample for sample in legacy.get('samples', [])}
    overlap = 0
    for sample in representatives:
        ordinal = sample['ordinal']
        if ordinal in union:
            overlap += 1
            if any(sample[key] != union[ordinal][key] for key in SAMPLE_FIELDS):
                errors.append('pose-scope representative contradicts overlapping legacy sample')
        else:
            union[ordinal] = sample
    union_generations = [union[ordinal]['last_move'] for ordinal in sorted(union)]
    if union_generations != sorted(union_generations):
        errors.append('pose-scope generations contradict chronological legacy samples')
    phase_mode = dict.fromkeys((*PHASES, *MODES), 0)
    for sample in union.values():
        phase = 'both' if sample['move'] and sample['render'] else 'move' if sample['move'] else 'render' if sample['render'] else 'other'
        mode = 'mode0' if sample['mode'] == 0 else 'mode1' if sample['mode'] == 1 else 'mode_other'
        phase_mode[phase] += 1
        phase_mode[mode] += 1
    if any(value > totals.get(key, -1) for key, value in phase_mode.items()):
        errors.append('unique legacy/scope samples exceed aggregate phase or mode counts')
    legacy_rows = legacy.get('raw_native_receipts', [])
    legacy_close = [i for i, line in enumerate(stderr_lines) if legacy_rows and line == legacy_rows[-1]]
    overlay_rows = overlay.get('raw_native_receipts', [])
    last_frame = [i for i, line in enumerate(stderr_lines) if len(overlay_rows) == callback_count+1 and line == overlay_rows[-2]]
    if render is not None:
        last_frame = [i for i, line in enumerate(stderr_lines) if line == render.get('observation_close_receipt', render.get('close_receipt'))]
    if (len(legacy_close) != 1 or len(last_frame) != 1 or
            not legacy_close[0] < rows[0][0] <= rows[-1][0] < last_frame[0]):
        errors.append('pose-scope receipts must follow legacy close and precede final overlay frame')
    result.update(representatives=representatives, overlapping_legacy_samples=overlap,
                  additional_unique_samples=len(union)-len(legacy.get('samples', [])),
                  combined_unique_call_samples=len(union), state='outside')
    result['pass'] = not errors
    result['scope_coverage_complete'] = not errors and all(counts[key] <= 2 for key in SCOPES)
    result['all_call_ordinals_retained'] = not errors and len(union) == totals.get('started')
    result['all_call_bytes_retained'] = (result['all_call_ordinals_retained'] and
                                         all(sample['before_valid'] and sample['after_valid']
                                             for sample in union.values()))
    result['limitations'] = [
        'Changing labels apply/restore transaction intervals; other scopes are owner notifications, not atomic visibility to other readers.',
        'Representatives are existing calls; overlapping ordinals are not additional invocations and sampling is not complete per-call scope coverage.',
        'Pointer ABA/lifetime, renderer selection, pose installation, gameplay and safe resume remain unproved.',
    ]
    return result


def collect_ui_pose_scope_evidence(profile, stderr, slot, enabled, legacy, overlay, render=None):
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
                errors.append('pose-scope input unavailable: '+str(error))
    result = parse_ui_pose_scope_evidence(lines, slot, enabled, legacy, overlay, render)
    result.update(hashes)
    proofs = (legacy, overlay) + ((render,) if render is not None else ())
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or any(proof.get(key) != hashes[key] for proof in proofs):
                errors.append('pose-scope proof refers to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = result['scope_coverage_complete'] = False
        result['all_call_bytes_retained'] = result['all_call_ordinals_retained'] = False
    return result
