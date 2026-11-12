"""Strict owner-worker/pointer-filtered pose-entry observations.

The entry forwarder remains resident through process discard. A valid closed
capture is not object lifetime/ABA protection, reader exclusion or UI readiness.
"""
import hashlib
from linux_ui_session_budget import budget_from_overlay
import re

NUMBER = r'(0|[1-9][0-9]{0,19})'
POINTER = r'(0|[1-9a-f][0-9a-f]{0,15})'
PHASES = ('move', 'render', 'both', 'other')
MODES = ('mode0', 'mode1', 'mode_other')


def parse_ui_live_pose_observer_evidence(stderr_lines, slot, enabled, overlay, fork, query, render=None):
    result = {
        'pass': False,
        'enabled': enabled and slot == 2,
        'errors': [],
        'route_observed': False,
        'sample_coverage_complete': False,
        'readiness_proven': False,
        'pose_installation_proven': False,
        'safe_resume_proven': False,
        'reader_exclusion_proven': False,
        'foreign_calls': 'unobserved',
    }
    errors = result['errors']
    try:
        budget = budget_from_overlay(overlay)
    except ValueError as error:
        errors.append(str(error))
        budget = {'callbacks':160, 'last':168}
    callback_count, last_tick = budget['callbacks'], budget['last']
    first_tick = budget.get('first', 9)
    rows = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 UI LIVE_POSE' in line]
    result['raw_native_receipts'] = [line for _, line in rows]
    activation = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 LIVE POSE PRELOAD' in line]
    result['raw_activation_receipts'] = [line for _, line in activation]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid live-pose activation or slot')
        return result
    if not enabled or slot == 1:
        if rows or activation:
            errors.append('unexpected disabled or host live-pose evidence')
        result['pass'] = not errors
        return result
    if result['raw_activation_receipts'] != ['X4 LIVE POSE PRELOAD ACTIVE version=1\n']:
        errors.append('missing, repeated or refused live-pose preload activation')
    if not 2 <= len(rows) <= 34 or any(len(line) >= 1024 or not line.endswith('\n') for _, line in rows):
        errors.append('missing, duplicate, truncated or oversized live-pose receipts')
        return result
    bind = re.fullmatch(f'X4 UI LIVE_POSE v=1 stage=bind tick={first_tick} target='+POINTER+
                        r' lifetime=pointer_only\n', rows[0][1])
    if not bind:
        errors.append('invalid live-pose bind grammar')
        return result
    target = int(bind[1], 16)
    if target % 8 or not 0x10000 <= target <= 0x00007fffffffffff - 8:
        errors.append('live-pose target outside native user-address bounds')
    result['target'] = target
    if len(activation) != 1 or not activation[0][0] < rows[0][0]:
        errors.append('live-pose preload activation must precede bind')
    names = ('started', 'returned', 'nested', 'rows', 'omitted', *PHASES, *MODES)
    close = re.fullmatch(rf'X4 UI LIVE_POSE v=1 stage=close tick={last_tick} '+
                         ' '.join(key+'='+NUMBER for key in names)+
                         r' foreign=unobserved closed=1 lifetime=pointer_only\n', rows[-1][1])
    if not close:
        errors.append('invalid live-pose close grammar or closed epoch')
        return result
    totals = {key: int(value) for key, value in zip(names, close.groups())}
    result['counts'] = totals
    if any(value > 0xffffffffffffffff for value in totals.values()):
        errors.append('live-pose counter outside uint64 range')
    if (totals['nested'] or totals['started'] != totals['returned'] or
            totals['rows'] != min(totals['started'], 32) or
            totals['omitted'] != max(totals['started']-32, 0) or
            totals['started'] != sum(totals[key] for key in PHASES) or
            totals['started'] != sum(totals[key] for key in MODES) or
            totals['rows'] != len(rows)-2):
        errors.append('live-pose started/returned/sample/omitted or phase/mode accounting mismatch')
    samples = []
    observed = {key: 0 for key in (*PHASES, *MODES)}
    last_move = first_tick
    for index, (_, line) in enumerate(rows[1:-1]):
        sample = re.fullmatch(r'X4 UI LIVE_POSE v=1 stage=sample index='+NUMBER+
                              ' last_move='+NUMBER+' caller='+POINTER+r' time=([0-9a-f]{16}) mode='+NUMBER+
                              ' move='+NUMBER+' render='+NUMBER+r' before_valid=([01]) after_valid=([01]) '+
                              r'before=([0-9a-f]{128}) after=([0-9a-f]{128})\n', line)
        if not sample:
            errors.append('invalid live-pose sample grammar or ordering')
            continue
        keys = ('index', 'last_move', 'caller', 'time_bits', 'mode', 'move', 'render',
                'before_valid', 'after_valid', 'before', 'after')
        values = dict(zip(keys, sample.groups()))
        for key in keys[:-2]:
            if key != 'time_bits':
                values[key] = int(values[key], 16 if key == 'caller' else 10)
        if (values['index'] != index or not last_move <= values['last_move'] <= last_tick or
                values['mode'] > 255 or values['move'] > 0xffffffff or values['render'] > 0xffffffff or
                not 0x10000 <= values['caller'] <= 0x00007fffffffffff):
            errors.append('live-pose sample index, generation, mode, depth or caller outside native bounds')
        last_move = values['last_move']
        for phase in ('before', 'after'):
            if values[phase+'_valid'] == 0 and values[phase] != '0'*128:
                errors.append('unavailable live-pose bytes must be zero filled')
        phase = 'both' if values['move'] and values['render'] else 'move' if values['move'] else 'render' if values['render'] else 'other'
        mode = 'mode0' if values['mode'] == 0 else 'mode1' if values['mode'] == 1 else 'mode_other'
        observed[phase] += 1
        observed[mode] += 1
        samples.append(values)
    for group in (PHASES, MODES):
        if (any(observed[key] > totals[key] for key in group) or
                sum(totals[key]-observed[key] for key in group) != totals['omitted']):
            errors.append('live-pose retained samples contradict aggregate phase/mode counts')
    if (overlay.get('pass') is not True or type(overlay.get('callback_count')) is not int or
            overlay.get('callback_count') != callback_count or fork.get('pass') is not True or query.get('pass') is not True or
            query.get('attempt_complete') is not True):
        errors.append('live-pose requires accepted bounded overlay, fork and completed seat-query evidence')
    query_rows = query.get('raw_native_receipts', [])
    completed = [i for i, line in enumerate(stderr_lines) if len(query_rows) == 3 and line == query_rows[-1]]
    during = [i for i, line in enumerate(stderr_lines)
              if line.startswith(f'X4 UI SEAT_POSE v=1 tick={first_tick} stage=during role=p1 ')]
    overlay_frames = [line for line in overlay.get('raw_native_receipts', [])
                      if line.startswith('X4 UI OVERLAY FRAME ')]
    previous = [i for i, line in enumerate(stderr_lines) if len(overlay_frames) == callback_count and line == overlay_frames[-2]]
    final = [i for i, line in enumerate(stderr_lines) if len(overlay_frames) == callback_count and line == overlay_frames[-1]]
    if render is not None:
        if render.get('pass') is not True or render.get('lease_count') != callback_count:
            errors.append('live-pose render mode requires accepted GameRender leases')
        previous = [i for i, line in enumerate(stderr_lines) if line == render.get('final_frame_receipt')]
        final = [i for i, line in enumerate(stderr_lines) if line == render.get('observation_close_receipt', render.get('close_receipt'))]
    if (len(completed) != 1 or len(during) != 1 or len(previous) != 1 or len(final) != 1 or
            not completed[0] < rows[0][0] < during[0] or
            not previous[0] < rows[1][0] <= rows[-1][0] < final[0]):
        errors.append('live-pose bind or deferred sample/close placement outside authenticated epoch')
    result.update(samples=samples, sample_phase_mode_counts=observed,
                  capture_closed=True, resident_forwarder=True, calls_observed=totals['started'])
    result['pass'] = not errors
    result['route_observed'] = not errors and totals['started'] > 0
    result['sample_coverage_complete'] = not errors and totals['omitted'] == 0
    result['limitations'] = [
        'Owner-worker and pointer-value filtering only; pointer ABA and other object lifetime paths are not excluded.',
        'last_move is the last authenticated generation, not exact call timing; samples are emitted at close.',
        'TLS Move/Render depths do not prove overlay-open state. Foreign calls are unobserved.',
        'Validity flags mean readable bytes, not finite/rigid transforms or renderer use.',
        'The pass-through hook remains resident until discard; capture closure is not hook restoration or safe resume.',
    ]
    return result


def collect_ui_live_pose_observer_evidence(profile, stderr, slot, enabled, overlay, fork, query, render=None):
    paths = {
        'engine_sha256': profile/'engine-output.log',
        'stderr_sha256': stderr,
        'native_fd_sha256': profile/'controller-lifecycle.log',
    }
    hashes, lines, errors = {}, [], []
    for key, path in paths.items():
        try:
            data = path.read_bytes()
            hashes[key] = hashlib.sha256(data).hexdigest()
            if key == 'stderr_sha256':
                lines = data.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if (enabled and slot == 2) or key == 'stderr_sha256':
                errors.append('live-pose input unavailable: '+str(error))
    result = parse_ui_live_pose_observer_evidence(lines, slot, enabled, overlay, fork, query, render)
    result.update(hashes)
    proofs = (overlay, fork, query) + ((render,) if render is not None else ())
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or any(proof.get(key) != hashes[key] for proof in proofs):
                errors.append('live-pose proof refers to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = result['route_observed'] = result['sample_coverage_complete'] = False
    return result
