"""Closed GameRender callback leases; software evidence is not a rendered view."""
import hashlib
from linux_ui_session_budget import budget_from_overlay
import re

from linux_ui_live_pose_observer_evidence import NUMBER, POINTER

PREFIX = 'X4 UI GAME_RENDER '


def parse_ui_render_lease_evidence(lines, slot, enabled, overlay, fork, query, callback, camera_refill=False):
    result = {'pass': False, 'enabled': enabled and slot == 2, 'errors': [],
              'lease_count': 0, 'rendered_view_proven': False, 'readiness_proven': False,
              'safe_resume_proven': False, 'reader_exclusion_proven': False}
    errors = result['errors']
    if type(camera_refill) is not bool:
        errors.append('invalid camera refill mode')
        return result
    result['camera_refill_enabled'] = camera_refill and enabled and slot == 2
    try:
        budget = budget_from_overlay(overlay)
    except ValueError as error:
        errors.append(str(error))
        budget = {'callbacks':160, 'last':168}
    callback_count, last_tick = budget['callbacks'], budget['last']
    first_tick = budget.get('first', 9)
    rows = [(index, line) for index, line in enumerate(lines) if 'X4 UI GAME_RENDER' in line]
    result['raw_native_receipts'] = [line for _, line in rows]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid render lease activation or slot')
        return result
    if not enabled or slot == 1:
        if rows:
            errors.append('unexpected disabled or host GameRender receipts')
        result['pass'] = not errors
        return result
    if any(proof.get('pass') is not True for proof in (overlay, fork, query, callback)) or overlay.get('callback_count') != callback_count:
        errors.append('render lease requires accepted bounded overlay/fork/query/callback evidence')
    if len(rows) != callback_count+1 or any(len(line) >= 1024 or not line.endswith('\n') for _, line in rows):
        errors.append('missing, duplicate, oversized or truncated GameRender receipts')
        return result
    close = PREFIX+f'v=1 stage=close tick={last_tick} count={callback_count} restored=1\n'
    if rows[-1][1] != close:
        errors.append('invalid GameRender closure, count or restored flag')
    actors = {sample.get('role'): sample.get('actor') for sample in query.get('observations', [])}
    ui_rows = [line for line in overlay.get('raw_native_receipts', []) if line.startswith('X4 UI OVERLAY FRAME ')]
    frames = []
    object_identity = None
    for ordinal, (position, line) in enumerate(rows[:-1], 1):
        match = re.fullmatch(PREFIX+'v=1 stage=frame tick='+NUMBER+' ordinal='+NUMBER+' tid='+NUMBER+
                             ' object='+POINTER+' p1='+POINTER+' p2='+POINTER+r' bank=([01]) slot='+POINTER+
                             ' original='+POINTER+' observed='+POINTER+r' restored=1\n', line)
        if not match:
            errors.append('invalid GameRender frame grammar or restored flag')
            continue
        keys = ('tick', 'ordinal', 'tid', 'object', 'p1', 'p2', 'bank', 'slot', 'original', 'observed')
        frame = {key: int(value, 10 if key in ('tick','ordinal','tid','bank') else 16)
                 for key, value in zip(keys, match.groups())}
        frames.append(frame)
        if (frame['tick'] != first_tick + ordinal - 1 or frame['ordinal'] != ordinal or
                not 1 <= frame['tid'] <= 0xffffffff or frame['tid'] != callback.get('owner') or
                frame['p1'] != actors.get('p1') or frame['p2'] != actors.get('p2') or
                frame['slot'] != frame['p1']+0x108+frame['bank']*0x80 or
                frame['original'] != frame['observed'] or
                frame['object'] & 7 or not 0x10000 <= frame['object'] <= 0x00007fffffffffff-7 or
                frame['original'] & 15 or not 0x10000 <= frame['original'] <= 0x00007fffffffffff-63):
            errors.append('GameRender frame order, owner, actors, slot or observed restoration invalid')
        if object_identity is None:
            object_identity = frame['object']
        elif frame['object'] != object_identity:
            errors.append('GameRender object changed within capture')
        current = [i for i, row in enumerate(lines) if len(ui_rows) == callback_count and row == ui_rows[ordinal-1]]
        following = [i for i, row in enumerate(lines) if ordinal < callback_count and len(ui_rows) == callback_count and row == ui_rows[ordinal]]
        if (len(current) != 1 or not current[0] < position or
                (ordinal < callback_count and (len(following) != 1 or not position < following[0]))):
            errors.append('GameRender return must follow matching UI frame before next UI frame')
    boundaries = []
    for prefix in (f'X4 UI LIVE_POSE v=1 stage=close tick={last_tick} ',
                   f'X4 UI POSE_SCOPE v=1 stage=close tick={last_tick} ',
                   f'X4 UI SEAT_CORRECTION v=1 stage=close tick={last_tick} '):
        found = [i for i, line in enumerate(lines) if line.startswith(prefix)]
        if len(found) != 1:
            errors.append('missing or repeated final observer/correction closure for render epoch')
        boundaries.extend(found)
    observation_close = close
    if camera_refill:
        observation_close = f'X4 UI CAMERA_REFILL v=1 stage=close tick={last_tick} count={callback_count-1} restored=1\n'
        camera_close = [i for i, row in enumerate(lines) if row == observation_close]
        camera_final = [i for i, row in enumerate(lines)
                        if row.startswith(f'X4 UI CAMERA_REFILL v=1 stage=frame tick={last_tick} ')]
        if (len(boundaries) != 3 or len(camera_close) != 1 or len(camera_final) != 1 or
                not rows[-2][0] < rows[-1][0] < boundaries[0] < boundaries[1] < boundaries[2] < camera_close[0] or
                not camera_final[0] < boundaries[0]):
            errors.append('camera-mode final render/observer/correction/tail order invalid')
    elif len(boundaries) == 3 and not rows[-2][0] < boundaries[0] < boundaries[1] < boundaries[2] < rows[-1][0]:
        errors.append('GameRender final return/observer/scope/correction/terminal order invalid')
    result.update(frames=frames, object=object_identity, owner=callback.get('owner'),
                  final_frame_receipt=rows[-2][1], close_receipt=rows[-1][1],
                  observation_close_receipt=observation_close,
                  camera_refill_count=callback_count-1 if camera_refill else 0)
    result['pass'] = not errors
    result['lease_count'] = callback_count if not errors else 0
    result['limitations'] = ['Receipts bound synchronous GameRender entry/return leases and pointer readback; they do not prove which pose a renderer consumed.',
                            'Other readers, asynchronous rendering, continuous object lifetime and safe resume remain unproved.']
    return result


def collect_ui_render_lease_evidence(profile, stderr, slot, enabled, overlay, fork, query, callback, camera_refill=False):
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
                errors.append('render lease input unavailable: '+str(error))
    result = parse_ui_render_lease_evidence(lines, slot, enabled, overlay, fork, query, callback, camera_refill)
    result.update(hashes)
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or any(proof.get(key) != hashes[key] for proof in (overlay, fork, query, callback)):
                errors.append('render lease proof refers to different or missing '+key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = False
        result['lease_count'] = 0
    return result
