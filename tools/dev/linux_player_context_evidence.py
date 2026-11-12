"""Verify bounded context-policy receipts; no inference about unobserved readers."""
import hashlib
import re
import struct

from linux_player_camera_evidence import BEFORE, AFTER, context_proof

PREFIX = 'X4LAN LIFECYCLE_DISCARD '
U = r'(?:0|[1-9][0-9]{0,19})'
RECEIPT = re.compile(
    r'X4LAN CONTEXT_POLICY v=1 event=(init|camera|add|remove) tick=(' + U + r') '
    r'boundary=(ready|camera|begin|end) epoch=(' + U + r') '
    r'owner=([0-9a-f]{1,16}) manager=([0-9a-f]{1,16}) target=([0-9a-f]{1,16}) controller=([0-9a-f]{1,16}) '
    r'base_nodes=(13|15) nodes=(' + U + r') digest=([0-9a-f]{64}) payloads=([0-9a-f]+)\n')
POLICY_SUFFIX = re.compile(r' context_policy_v=1 manager=([0-9a-f]{1,16}) epoch=(' + U + r') full_hash=([0-9a-f]{64})$')
KNOWN_ADDITION = bytes.fromhex('3b000000080000000000000001000000')


def payload_set(text, count):
    if count not in (13, 14, 15, 16) or len(text) != count * 32:
        raise ValueError('payload count or byte length mismatch')
    payloads = [bytes.fromhex(text[offset:offset + 32]) for offset in range(0, len(text), 32)]
    keys = [struct.unpack_from('<ii', row) for row in payloads]
    if keys != sorted(keys) or len(set(keys)) != len(keys):
        raise ValueError('payload keys are not unique and sorted')
    values = dict(zip(keys, payloads))
    if any(key not in values for key in ((8, 6), (15, 6), (16, 6))):
        raise ValueError('required selected contexts missing')
    return values


def selected_bytes(values):
    return b''.join(values[key] for key in ((8, 6), (15, 6), (16, 6))).hex()


def verify_context_policy(lines, *, allow_camera=False):
    """Return filtered lines and verified expectations keyed by (tick, snapshot).

    Legacy input is returned unchanged. For policy input, each snapshot carries
    the exact current epoch and full-set digest; existing frame checks remain
    responsible for native ABI, worker, timing, bank and body evidence.
    """
    enabled = any('CONTEXT_POLICY' in row or 'context_policy_v' in row for row in lines)
    result = {'enabled': enabled, 'lines': [], 'snapshots': {}, 'errors': [], 'receipts': []}
    if not enabled:
        result['lines'] = lines
        return result
    errors = result['errors']
    tick, state, base, identity = None, None, None, None
    epoch, camera_seen, init_seen = 0, False, False
    camera_rows, first_body, canonical_controller = [], None, None
    for index, line in enumerate(lines):
        begin = re.fullmatch(PREFIX + r'physics=frame tick=([0-9]+) event=begin\n', line)
        end = re.fullmatch(PREFIX + r'physics=frame tick=([0-9]+) event=end\n', line)
        if begin:
            tick = int(begin[1])
        if line.startswith('X4LAN CAMERA'):
            camera_rows.append(line)
        if first_body is None and tick == 5:
            body = re.search(r'physics=body stage=before index=0 ship=([0-9a-f]{1,16}) ', line)
            if body:
                first_body = int(body[1], 16)
        if tick == 5 and line.startswith(PREFIX + 'snapshot=physics_canonical '):
            controller = re.search(r' controller=([0-9a-f]{1,16}) ', line)
            if controller:
                canonical_controller = int(controller[1], 16)
        if 'CONTEXT_POLICY' in line:
            match = RECEIPT.fullmatch(line)
            try:
                if not match:
                    raise ValueError('malformed policy receipt')
                event, observed_tick, boundary, next_epoch, owner, manager, target, controller, base_nodes, nodes, digest, text = match.groups()
                observed_tick, next_epoch, base_nodes, nodes = map(int, (observed_tick, next_epoch, base_nodes, nodes))
                ids = tuple(int(value, 16) for value in (owner, manager, target, controller))
                if (tick is None or observed_tick != tick or not 0 < next_epoch < 1 << 64 or next_epoch != epoch + 1 or
                    any(not 0 < value < 1 << 64 for value in ids) or ids[0] > (1 << 64) - 17 or ids[1] != ids[0] + 16):
                    raise ValueError('policy tick, epoch or identity invalid')
                if identity is not None and ids != identity:
                    raise ValueError('policy owner or target identity changed')
                values = payload_set(text, nodes)
                if hashlib.sha256(bytes.fromhex(text)).hexdigest() != digest:
                    raise ValueError('full payload digest mismatch')
                previous = lines[index - 1] if index else ''
                following = lines[index + 1] if index + 1 < len(lines) else ''
                if event == 'init':
                    if (init_seen or state is not None or tick != 5 or boundary != 'ready' or next_epoch != 1 or
                        base_nodes != 13 or nodes != 13 or (59, 8) in values or
                        not following.startswith(PREFIX + 'snapshot=physics_canonical ')):
                        raise ValueError('invalid initial policy boundary or baseline')
                    base, identity, init_seen = values, ids, True
                elif state is None:
                    raise ValueError('policy transition before init')
                elif event == 'camera':
                    headers = [row for row in camera_rows if row.startswith('X4LAN CAMERA stage=')]
                    before_match = BEFORE.fullmatch(headers[0]) if headers else None
                    after_match = AFTER.fullmatch(headers[1]) if len(headers) > 1 else None
                    if (not allow_camera or camera_seen or tick != 6 or boundary != 'camera' or
                        base_nodes != 15 or nodes != 15 or len(state['values']) != 13 or
                        not previous.startswith('X4LAN CONTEXT_ANCHOR v=1 tick=6 ') or
                        not following.startswith(PREFIX + 'snapshot=physics_end ') or not before_match or not after_match):
                        raise ValueError('invalid camera policy boundary')
                    anchor = re.fullmatch(r'X4LAN CONTEXT_ANCHOR v=1 tick=6 owner=([0-9a-f]{1,16}) manager=([0-9a-f]{1,16}) '
                                          r'target=([0-9a-f]{1,16}) controller=([0-9a-f]{1,16}) digest=([0-9a-f]{64})\n', previous)
                    if not anchor or tuple(int(anchor[i], 16) for i in range(1, 5)) != ids or anchor[5] != digest:
                        raise ValueError('camera policy identity differs from anchor receipt')
                    before, after = before_match.groupdict(), after_match.groupdict()
                    for observation in (before, after):
                        observation['nodes'], observation['mask'] = int(observation['nodes']), int(observation['mask'])
                    proof = context_proof(camera_rows, before, after)
                    old = b''.join(state['values'][key] for key in sorted(state['values'])).hex()
                    if (not proof['pass'] or proof.get('version') != 2 or
                        ''.join(proof['before_payloads']) != old or ''.join(proof['after_payloads']) != text):
                        raise ValueError('camera policy does not match full before/after proof')
                    base, camera_seen = values, True
                else:
                    if base_nodes != len(base) or boundary not in ('begin', 'end'):
                        raise ValueError('transition baseline or boundary invalid')
                    if boundary == 'begin':
                        if (tick == 5 or previous != PREFIX + f'physics=frame tick={tick} event=begin\n' or
                            not following.startswith(PREFIX + 'snapshot=physics_update_before ')):
                            raise ValueError('transition outside begin boundary')
                    elif (not previous.startswith(PREFIX + 'physics=upkeep stage=leave ') or
                          not following.startswith(PREFIX + 'snapshot=physics_end ')):
                        raise ValueError('transition outside end boundary')
                    expected = dict(state['values'])
                    if event == 'add':
                        if (59, 8) in expected:
                            raise ValueError('repeated context addition')
                        expected[(59, 8)] = KNOWN_ADDITION
                    else:
                        if expected.get((59, 8)) != KNOWN_ADDITION:
                            raise ValueError('removal without known addition')
                        del expected[(59, 8)]
                    if values != expected or any(values.get(key) != payload for key, payload in base.items()):
                        raise ValueError('unexpected base or added payload mutation')
                epoch = next_epoch
                state = {'values': values, 'nodes': nodes, 'manager': ids[1], 'epoch': epoch,
                         'digest': digest, 'selected': selected_bytes(values)}
                result['receipts'].append({'event': event, 'tick': tick, 'boundary': boundary, 'epoch': epoch,
                                           'nodes': nodes, 'digest': digest})
            except (ValueError, KeyError, IndexError, OverflowError) as error:
                errors.append(f'context policy line {index + 1}: {error}')
            continue
        if line.startswith(PREFIX + 'snapshot='):
            suffix = POLICY_SUFFIX.search(line[:-1]) if line.endswith('\n') else None
            label = line[len(PREFIX):].split(' ', 1)[0].split('=', 1)[1]
            if 'context_anchor_v=' in line:
                errors.append('mixed legacy anchor and context policy suffixes')
            if state is None:
                if suffix or 'context_policy_v' in line:
                    errors.append('policy snapshot before init')
            else:
                if (not suffix or int(suffix[1], 16) != state['manager'] or int(suffix[2]) != state['epoch'] or suffix[3] != state['digest']):
                    errors.append('snapshot policy identity, epoch or digest missing or changed')
                result['snapshots'][(tick, label)] = {key: state[key] for key in ('nodes', 'manager', 'epoch', 'digest', 'selected')}
                observed = re.search(r' contexts_ok=([01]) contexts_valid=1 contexts_nodes=([0-9]+) contexts_mask=([0-7]) contexts_payloads=([0-9a-f]{96})(?: |\n)', line)
                if not observed or observed.groups() != ('1', str(state['nodes']), '7', state['selected']):
                    errors.append('snapshot does not match verified full context set')
        result['lines'].append(line)
        if end:
            tick = None
    if not init_seen or identity is None:
        errors.append('missing valid context policy init')
    elif first_body != identity[2] or canonical_controller != identity[3]:
        errors.append('policy target/controller does not match first-frame native identity')
    if allow_camera and not camera_seen:
        errors.append('missing camera policy rebase')
    return result
