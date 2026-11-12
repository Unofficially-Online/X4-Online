"""Strict camera-refill lease receipts; no visual or camera-pose rollback claim."""
import hashlib
import re

from linux_ui_session_budget import budget_from_overlay

PREFIX = 'X4 UI CAMERA_REFILL '
NUMBER = r'(0|[1-9][0-9]{0,19})'
POINTER = r'([1-9a-f][0-9a-f]{0,15})'


def parse_camera_refill_evidence(lines, slot, enabled, overlay, fork, query, render):
    result = {'pass': False, 'enabled': enabled and slot == 2, 'errors': [],
              'refill_count': 0, 'performed': False, 'rendered_view_proven': False, 'readiness_proven': False,
              'safe_resume_proven': False, 'reader_exclusion_proven': False,
              'camera_output_restored': False, 'camera_pose_bytes_verified': False}
    errors = result['errors']
    rows = [(i, row) for i, row in enumerate(lines) if 'X4 UI CAMERA_REFILL' in row]
    result['raw_native_receipts'] = [row for _, row in rows]
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid camera refill activation or slot')
        return result
    if not enabled or slot == 1:
        if rows:
            errors.append('unexpected disabled or host camera refill evidence')
        result['pass'] = not errors
        return result
    try:
        budget = budget_from_overlay(overlay)
    except ValueError as error:
        errors.append(str(error))
        return result
    count, last = budget['callbacks'] - 1, budget['last']
    if budget['callbacks'] < 160:
        errors.append('camera refill requires the full multi-callback presentation window')
        return result
    first = budget.get('first', 9) + 1
    proofs = (overlay, fork, query, render)
    if (any(proof.get('pass') is not True for proof in proofs) or
            render.get('camera_refill_enabled') is not True):
        errors.append('camera refill requires accepted overlay/fork/query and explicit render mode')
    if len(rows) != count + 1 or any(len(row) >= 1024 or not row.endswith('\n') for _, row in rows):
        errors.append('missing, duplicate, oversized or truncated camera refill receipts')
        return result
    close = f'{PREFIX}v=1 stage=close tick={last} count={count} restored=1\n'
    if rows[-1][1] != close:
        errors.append('camera refill close tick, count or restored state invalid')
    actor = fork.get('measurements', {}).get('p1')
    if type(actor) is not int or not 0x10000 <= actor <= 0x00007fffffffffff - 0x18f:
        errors.append('camera refill lacks a source-bounded P1 actor identity')
        actor = None
    frames, target = [], None
    worker_enter, worker_release = {}, {}
    for index, row in enumerate(lines):
        match = re.fullmatch(r'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=([0-9]{1,20}) tail=1 event=(enter|release)\n', row)
        if match:
            (worker_enter if match[2] == 'enter' else worker_release).setdefault(int(match[1]), []).append(index)
    grammar = (PREFIX + 'v=1 stage=frame tick=' + NUMBER + ' ordinal=' + NUMBER +
               ' camera=' + POINTER + ' output=' + POINTER + r' camera_bank=([01]) actor_bank=([01]) slot=' +
               POINTER + ' original=' + POINTER + ' observed=' + POINTER + r' restored=1\n')
    for ordinal, (position, row) in enumerate(rows[:-1], 1):
        match = re.fullmatch(grammar, row)
        if not match:
            errors.append('malformed camera refill frame or restoration flag')
            continue
        names = ('tick', 'ordinal', 'camera', 'output', 'camera_bank', 'actor_bank', 'slot', 'original', 'observed')
        frame = {key: int(value, 10 if key in ('tick', 'ordinal', 'camera_bank', 'actor_bank') else 16)
                 for key, value in zip(names, match.groups())}
        frames.append(frame)
        if (frame['tick'] != first + ordinal - 1 or frame['ordinal'] != ordinal or
                not 0x10000 <= frame['camera'] <= 0x00007fffffffffff - 0x787 or
                frame['output'] != frame['camera'] + 0x20 + frame['camera_bank'] * 0x2c0 or
                actor is None or frame['slot'] != actor + 0x108 + frame['actor_bank'] * 0x80 or
                frame['observed'] != frame['original'] or frame['original'] & 15 or
                not 0x10000 <= frame['original'] <= 0x00007fffffffffff - 63):
            errors.append('camera refill epoch, bank address or actor pose-pointer restoration invalid')
        if target is None:
            target = frame['camera']
        elif frame['camera'] != target:
            errors.append('camera target changed during the epoch')
        enter, release = worker_enter.get(frame['tick'], []), worker_release.get(frame['tick'], [])
        if len(enter) != 1 or len(release) != 1 or not enter[0] < position < release[0]:
            errors.append('camera refill return outside its logged worker interval')
    # The final tail closes after the real final GameRender and all observers.
    prefixes = (f'X4 UI GAME_RENDER v=1 stage=close tick={last} ',
                f'X4 UI LIVE_POSE v=1 stage=close tick={last} ',
                f'X4 UI POSE_SCOPE v=1 stage=close tick={last} ',
                f'X4 UI SEAT_CORRECTION v=1 stage=close tick={last} ')
    boundaries = [[i for i, row in enumerate(lines) if row.startswith(prefix)] for prefix in prefixes]
    if (any(len(found) != 1 for found in boundaries) or
            not all(left[0] < right[0] for left, right in zip(boundaries, boundaries[1:])) or
            not boundaries[-1][0] < rows[-1][0] or not rows[-2][0] < boundaries[1][0]):
        errors.append('camera final render/observer/correction/tail closure order invalid')
    final_release = worker_release.get(last, [])
    if len(final_release) != 1 or not rows[-1][0] < final_release[0]:
        errors.append('camera close must precede final presentation worker release')
    # These candidates are observations already checked by the seat/query chain.
    candidates = []
    for row in lines:
        if row.startswith(f'X4 UI VIEWSTATE v=1 tick={first-1} '):
            match = re.search(r' camera_candidate=([0-9a-f]{1,16}) ', row)
            if match and int(match[1], 16):
                candidates.append(int(match[1], 16))
    if any(candidate != target for candidate in candidates):
        errors.append('camera refill target differs from available first-UI view-state samples')
    result.update(frames=frames, observed_frame_rows=len(frames), camera=target,
                  performed=not errors and count > 0, camera_identity_correlated=bool(candidates),
                  actor_pose_pointer_restored=not errors, close_receipt=rows[-1][1])
    result['pass'] = not errors
    result['refill_count'] = count if not errors else 0
    result['limitations'] = ['Stock camera output bytes and the displayed view are not captured by these receipts.',
                            'Pointer readback and bank-derived addresses do not establish asynchronous-reader exclusion or safe resume.']
    return result


def collect_camera_refill_evidence(profile, stderr, slot, enabled, overlay, fork, query, render):
    paths = {'engine_sha256': profile / 'engine-output.log', 'stderr_sha256': stderr,
             'native_fd_sha256': profile / 'controller-lifecycle.log'}
    hashes, lines, errors = {}, [], []
    for key, path in paths.items():
        try:
            data = path.read_bytes()
            hashes[key] = hashlib.sha256(data).hexdigest()
            if key == 'stderr_sha256':
                lines = data.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if (enabled and slot == 2) or key == 'stderr_sha256':
                errors.append('camera refill input unavailable: ' + str(error))
    result = parse_camera_refill_evidence(lines, slot, enabled, overlay, fork, query, render)
    result.update(hashes)
    if enabled and slot == 2:
        for key in paths:
            if key not in hashes or any(proof.get(key) != hashes[key] for proof in (overlay, fork, query, render)):
                errors.append('camera refill proof refers to different or missing ' + key)
    result['errors'].extend(errors)
    if errors:
        result['pass'] = False
        result['refill_count'] = 0
        result['performed'] = False
        result['actor_pose_pointer_restored'] = False
    return result
