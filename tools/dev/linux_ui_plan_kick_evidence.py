"""Strict receipts for the explicit guest-only plan-handler/RNG diagnostic."""
import hashlib
from linux_ui_session_budget import budget_from_overlay
import re

from linux_player_presentation_evidence import validate_presentation_rows


def parse_plan_kick_evidence(stderr_lines, trace_lines, slot, enabled, overlay):
    result = {'pass': False, 'enabled': enabled and slot == 2, 'performed': False,
              'errors': [], 'readiness_proven': False, 'safe_resume_proven': False}
    errors = result['errors']
    try:
        budget = budget_from_overlay(overlay)
    except ValueError as error:
        errors.append(str(error))
        budget = {'callbacks':160, 'last':168}
    callback_count, last_tick = budget['callbacks'], budget['last']
    first_tick = budget.get('first', 9)
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid plan-kick activation or slot')
        return result
    rows = [(i, line) for i, line in enumerate(stderr_lines) if 'X4 UI PLAN KICK' in line]
    result['raw_native_receipts'] = [line for _, line in rows]
    if not enabled or slot == 1:
        if rows:
            errors.append('unexpected disabled or host plan-kick receipts')
        result['pass'] = not errors
        return result
    if len(rows) != 4 or any(len(line) > 2048 or not line.endswith('\n') for _, line in rows):
        errors.append('missing, duplicate, oversized or truncated plan-kick receipts')
        return result
    pointer, number = r'(0|[1-9a-f][0-9a-f]{0,15})', r'(0|[1-9][0-9]{0,19})'
    observations = []
    for (index, line), stage, first, plan in zip(rows[:3], ('before', 'opposite', 'restored'),
                                               (0, 1, 0), ('cockpit', 'firstperson', 'cockpit')):
        grammar = (f'X4 UI PLAN KICK v=1 stage={stage} tick={first_tick} ' +
                   ' '.join(key+'='+pointer for key in ('manager', 'presenter', 'controller')) +
                   f' selector=1 first={first} plan_len={len(plan)} plan={plan.encode().hex()} ' +
                   r'rng=([0-9a-f]{16}) contexts='+number+r' context_hash=([0-9a-f]{64}) ' +
                   'camera_candidate='+pointer+' camera_mode='+number+' external_target='+pointer+
                   ' target_id='+number+r'\n')
        match = re.fullmatch(grammar, line)
        if not match:
            errors.append('invalid plan-kick stage order, plan or grammar')
            continue
        keys = ('manager', 'presenter', 'controller', 'rng', 'contexts', 'context_hash',
                'camera_candidate', 'camera_mode', 'external_target', 'target_id')
        values = dict(zip(keys, match.groups()))
        for key in keys:
            if key != 'context_hash':
                values[key] = int(values[key], 16 if key in
                                  ('manager', 'presenter', 'controller', 'rng',
                                   'camera_candidate', 'external_target') else 10)
                if values[key] > 0xffffffffffffffff:
                    errors.append('plan-kick field outside uint64 range')
        if values['contexts'] > 0xffffffff or values['camera_mode'] > 0xffffffff:
            errors.append('plan-kick unsigned32 field outside native range')
        observations.append(values)
    final = re.fullmatch(f'X4 UI PLAN KICK v=1 stage=result tick={first_tick} success=1 '
                         r'rng_before=([0-9a-f]{16}) rng_after=([0-9a-f]{16}) '
                         r'rng_restored=([0-9a-f]{16}) context_changed=([01]) camera_changed=([01])\n', rows[3][1])
    result['observations'] = observations
    if not final or len(observations) != 3:
        errors.append('invalid plan-kick result or incomplete stages')
        return result
    before, opposite, restored = observations
    rng_before, rng_after, rng_restored = (int(value, 16) for value in final.groups()[:3])
    context_changed, camera_changed = map(int, final.groups()[3:])
    if not (before['rng'] == rng_before == rng_restored and restored['rng'] == rng_after):
        errors.append('plan-kick RNG evidence or restoration mismatch')
    if context_changed != int(any(before[key] != restored[key] for key in ('contexts', 'context_hash'))):
        errors.append('plan-kick context delta flag mismatch')
    if camera_changed != int(any(before[key] != restored[key] for key in
                                 ('camera_candidate', 'camera_mode', 'external_target', 'target_id'))):
        errors.append('plan-kick camera delta flag mismatch')
    presentation = validate_presentation_rows(trace_lines, slot, tick=first_tick - 2)
    identity = presentation.get('ui_plan', {})
    if not presentation['pass'] or any(row[key] != identity.get(key) for row in observations
                                       for key in ('manager', 'presenter', 'controller')):
        errors.append('plan-kick identity differs from accepted presentation')
    if overlay.get('pass') is not True or overlay.get('callback_count') != callback_count:
        errors.append('plan-kick requires accepted bounded overlay evidence')
    fork_rows = [i for i, line in enumerate(stderr_lines) if line.startswith('X4 PRESENTATION FORK RESULT ')]
    if len(fork_rows) != 1 or not fork_rows[0] < rows[0][0]:
        errors.append('plan-kick must follow the accepted native child fork')
    first_frames = [i for i, line in enumerate(stderr_lines)
                    if line.startswith(f'X4 UI OVERLAY FRAME v=3 tick={first_tick} ordinal=1 count={callback_count} ')]
    if len(first_frames) != 1 or not rows[-1][0] < first_frames[0]:
        errors.append('plan-kick must finish before first overlay frame')
    view_positions, viewstates = {}, {}
    for stage in ('during', 'after'):
        marker = f'X4 UI VIEWSTATE v=1 tick={first_tick} stage={stage} '
        matching = [(i, line) for i, line in enumerate(stderr_lines) if marker in line]
        view_grammar = (marker + 'status=observed ' +
                        ' '.join(key+'='+pointer for key in ('manager', 'presenter', 'controller', 'universe')) +
                        ' selector='+number+r' first=([01]) plan_len='+number+
                        r' plan=(-|(?:[0-9a-f]{2}){1,32}) camera_candidate='+pointer+
                        ' camera_mode='+number+' external_target='+pointer+' target_id='+number+r'\n')
        match = re.fullmatch(view_grammar, matching[0][1]) if len(matching) == 1 else None
        if not match or len(matching[0][1]) > 2048:
            errors.append('plan-kick requires unique full observed during/after VIEWSTATE rows')
            continue
        keys = ('manager', 'presenter', 'controller', 'universe', 'selector', 'first', 'plan_len',
                'plan', 'camera_candidate', 'camera_mode', 'external_target', 'target_id')
        view = dict(zip(keys, match.groups()))
        for key in keys:
            if key != 'plan':
                view[key] = int(view[key], 16 if key in
                                ('manager', 'presenter', 'controller', 'universe',
                                 'camera_candidate', 'external_target') else 10)
                if view[key] > 0xffffffffffffffff:
                    errors.append('VIEWSTATE field outside uint64 range')
        plan_length = 0 if view['plan'] == '-' else len(view['plan']) // 2
        if (view['selector'] > 0xffffffff or view['camera_mode'] > 0xffffffff or
                view['plan_len'] != plan_length):
            errors.append('VIEWSTATE source field bounds or plan length mismatch')
        if any(view[key] != identity.get(key) for key in ('manager', 'presenter', 'controller')):
            errors.append('VIEWSTATE identity differs from accepted presentation')
        view_positions[stage] = matching[0][0]
        viewstates[stage] = view
    result['viewstates'] = viewstates
    if (len(view_positions) != 2 or not first_frames or
            not view_positions.get('during', -1) < rows[0][0] < rows[-1][0] <
            view_positions.get('after', -1) < first_frames[0]):
        errors.append('plan-kick must occur inside first overlay VIEWSTATE bracket')
    # The accepted overlay already binds the preceding child-fork receipt in this same stream.
    result.update(rng_before=rng_before, rng_after=rng_after, rng_restored=rng_restored,
                  context_changed=bool(context_changed), camera_changed=bool(camera_changed))
    result['pass'] = not errors
    result['performed'] = result['pass']
    result['limitations'] = ['Handler sequence and TLS RNG restoration only; context/camera deltas are observations.']
    return result


def collect_plan_kick_evidence(profile, stderr, slot, enabled, overlay):
    paths = {'engine_sha256': profile / 'engine-output.log', 'stderr_sha256': stderr,
             'native_fd_sha256': profile / 'controller-lifecycle.log'}
    lines, hashes, errors = {}, {}, []
    for key, path in paths.items():
        try:
            data = path.read_bytes()
            lines[key] = data.decode('utf-8', errors='strict').splitlines(keepends=True)
            hashes[key] = hashlib.sha256(data).hexdigest()
        except (OSError, UnicodeError) as error:
            lines[key] = []
            if (enabled and slot == 2) or key == 'stderr_sha256':
                errors.append('plan-kick evidence unavailable: '+str(error))
    result = parse_plan_kick_evidence(lines['stderr_sha256'], lines['native_fd_sha256'], slot, enabled, overlay)
    result.update(hashes)
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or overlay.get(key) != hashes[key]:
                errors.append('plan-kick and overlay proof refer to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = result['performed'] = False
    return result
