#!/usr/bin/env python3
"""Validate bounded external-camera log evidence, not rendered video."""
import re
import hashlib
import struct
from pathlib import Path

HEX64 = r'[0-9a-f]{64}'
U = r'(?:0|[1-9][0-9]*)'
BEFORE = re.compile(r'X4LAN CAMERA stage=before tick=6 pins=1 saved=(?P<saved>[0-9a-f]{16}) '
    r'target=(?P<target>[0-9a-f]+) runtime=(?P<runtime>'+U+r') current=(?P<current>'+U+r') '
    r'bank=(?P<bank>'+HEX64+r') contexts=(?P<nodes>'+U+r')/(?P<mask>'+U+r') '
    r'context_hash=(?P<context>'+HEX64+r') rng=(?P<rng>[0-9a-f]{16}) '
    r'ids=(?P<inserts>'+U+r')/(?P<live>'+U+r')/(?P<free>'+U+r')\n')
AFTER = re.compile(r'X4LAN CAMERA stage=after tick=6 possible=(?P<possible>[01]) set=(?P<set>[01]) '
    r'current=(?P<current>'+U+r') match=(?P<match>[01]) bank=(?P<bank>'+HEX64+r') '
    r'bank_changed=(?P<bank_changed>[01]) contexts=(?P<nodes>'+U+r')/(?P<mask>'+U+r') '
    r'context_hash=(?P<context>'+HEX64+r') context_changed=(?P<context_changed>[01]) '
    r'rng=(?P<rng>[0-9a-f]{16}) restored=(?P<restored>[0-9a-f]{16}) '
    r'ids=(?P<inserts>'+U+r')/(?P<live>'+U+r')/(?P<free>'+U+r')\n')
RESULT = re.compile(r'X4LAN CAMERA stage=result success=1 reason=matched\n')
MARKER = re.compile(r'^.*\[X4LAN\] NATIVE_GATE LINUX_EXTERNAL_CAMERA tick=6 slot=2 '
                    r'target=0000000000175310\n')

CONTEXT_ROW = re.compile(r'X4LAN CAMERA_CONTEXT v=([12]) stage=(before|after) index=(0|[1-9][0-9]{0,2}) '
                         r'count=(0|[1-9][0-9]{0,2}) payload=([0-9a-f]{32})\n')


def context_proof(order, before, after):
    """Validate complete, sorted payload lists and only the pinned insertions."""
    if not any('X4LAN CAMERA_CONTEXT' in row for row in order):
        return {'pass': False, 'available': False, 'errors': ['no versioned context census']}
    errors, sections = [], {'before': [], 'after': []}
    phase, stage, version = 'before_header', None, None
    for row in order:
        if row.startswith('X4LAN CAMERA stage=before '):
            if phase != 'before_header': errors.append('duplicate or reordered before header')
            phase = 'before_rows'; stage = 'before'
        elif row.startswith('X4LAN CAMERA stage=after '):
            if phase != 'before_rows': errors.append('duplicate or reordered after header')
            phase = 'after_rows'; stage = 'after'
        elif row.startswith('X4LAN CAMERA stage=result '):
            if phase != 'after_rows': errors.append('duplicate or reordered result')
            phase = 'done'; stage = None
        else:
            match = CONTEXT_ROW.fullmatch(row)
            if not match or stage is None or match[2] != stage:
                errors.append('malformed or misplaced context entry')
                continue
            if version is None:
                version = int(match[1])
            elif version != int(match[1]):
                errors.append('mixed context evidence versions')
            index, count = int(match[3]), int(match[4])
            section = sections[stage]
            if count > 512 or count != (before if stage == 'before' else after)['nodes'] or index != len(section):
                errors.append('context index or node count mismatch')
            section.append(bytes.fromhex(match[5]))
    if phase != 'done': errors.append('unterminated context evidence')
    for stage, observation in (('before', before), ('after', after)):
        if observation['mask'] != 7:
            errors.append(stage + ' lacks required target contexts')
        entries = sections[stage]
        if len(entries) != observation['nodes'] or len(entries) > 512:
            errors.append(stage + ' context census incomplete')
        keys = [struct.unpack_from('<ii', entry) for entry in entries]
        if keys != sorted(keys) or len(set(keys)) != len(keys):
            errors.append(stage + ' context keys not strictly ordered')
        targets = {struct.unpack_from('<ii', entry): entry for entry in entries}
        selected = [targets.get((context, 6), bytes(16)) for context in (8, 15, 16)]
        mask = sum(1 << index for index, context in enumerate((8, 15, 16)) if (context, 6) in targets)
        if mask != observation['mask'] or hashlib.sha256(b''.join(selected)).hexdigest() != observation['context']:
            errors.append(stage + ' target payload digest or mask mismatch')
    b = {struct.unpack_from('<ii', row): row for row in sections['before']}
    a = {struct.unpack_from('<ii', row): row for row in sections['after']}
    if version == 1:
        expected_add, expected_remove = {(13, 20), (14, 20)}, set()
    elif version == 2:
        expected_add, expected_remove = {(11, 6), (13, 20), (14, 20)}, {(10, 6)}
        if len(b) != 13 or len(a) != 15 or len(set(a) & set(b)) != 12:
            errors.append('version 2 requires 13 before, 15 after and 12 retained entries')
    else:
        expected_add, expected_remove = set(), set()
        errors.append('missing context evidence version')
    if (set(a) - set(b) != expected_add or set(b) - set(a) != expected_remove or
            any(a[k] != b[k] for k in set(a) & set(b))):
        errors.append('context transition contains an unexpected insertion, removal or payload change')
    for key in {(13, 20), (14, 20)} & set(a):
        if struct.unpack_from('<i', a[key], 8)[0] != 5 or a[key][12] != 0:
            errors.append('camera context priority or enabled byte mismatch')
    if version == 2:
        for collection, key in ((b, (10, 6)), (a, (11, 6))):
            if key not in collection or struct.unpack_from('<i', collection[key], 8)[0] != 0 or collection[key][12] != 1:
                errors.append('camera anchor context priority or enabled byte mismatch')
    # Bytes 13..15 are opaque native storage, not asserted to be padding or zero.
    opaque_tails = {'removed': {f'{c}/{r}': b[(c, r)][13:16].hex() for c, r in expected_remove & set(b)},
                    'added': {f'{c}/{r}': a[(c, r)][13:16].hex() for c, r in expected_add & set(a)}}
    return {'pass': not errors, 'available': True, 'version': version, 'errors': errors,
            'opaque_tails': opaque_tails, 'before_payloads': [row.hex() for row in sections['before']],
            'after_payloads': [row.hex() for row in sections['after']]}



def parse_camera_evidence(logs):
    """logs maps '1'/'2' to lifecycle, engine, stdout and stderr line lists."""
    errors, native, context_order, markers, mirrored_markers = [], [], [], [], []
    if set(logs) != {'1', '2'}:
        return {'pass': False, 'errors': ['camera evidence requires both clients'], 'visual_proven': False}
    for slot in ('1', '2'):
        if set(logs[slot]) != {'lifecycle', 'engine', 'stdout', 'stderr'}:
            errors.append('missing camera evidence source for client ' + slot)
            continue
        for source, lines in logs[slot].items():
            for line in lines:
                if 'X4LAN CAMERA' in line:
                    if slot != '2' or source != 'lifecycle':
                        errors.append('camera native row on unexpected client or source')
                    else:
                        context_order.append(line)
                        if 'X4LAN CAMERA ' in line:
                            native.append(line)
                if 'LINUX_EXTERNAL_CAMERA' in line:
                    if slot == '2' and source == 'engine':
                        markers.append(line)
                    elif slot == '2' and source == 'stdout':
                        mirrored_markers.append(line)
                    else:
                        errors.append('camera Lua marker on unexpected client or source')
    if len(markers) != 1 or not MARKER.fullmatch(markers[0]):
        errors.append('missing, duplicate or malformed camera Lua marker')
    # X4 duplicates its Lua message to stdout and engine-output.log. Accept
    # at most one byte-exact message mirror; the engine marker is mandatory.
    if len(mirrored_markers) > 1 or (mirrored_markers and
        mirrored_markers[0] != '[X4LAN] NATIVE_GATE LINUX_EXTERNAL_CAMERA tick=6 slot=2 target=0000000000175310\n'):
        errors.append('duplicate or malformed stdout camera marker mirror')
    if len(native) != 3:
        errors.append('camera native rows must occur exactly once in before/after/result order')
        return {'pass': False, 'errors': errors, 'visual_proven': False}
    before_match, after_match = BEFORE.fullmatch(native[0]), AFTER.fullmatch(native[1])
    if not before_match or not after_match or not RESULT.fullmatch(native[2]):
        errors.append('malformed, failed or reordered camera native rows')
        return {'pass': False, 'errors': errors, 'visual_proven': False}
    before, after = before_match.groupdict(), after_match.groupdict()
    try:
        for row in (before, after):
            for key in ('current', 'nodes', 'mask', 'inserts', 'live', 'free'):
                row[key] = int(row[key])
            if (row['current'] >= 1 << 64 or row['inserts'] >= 1 << 64 or
                any(row[key] >= 1 << 32 for key in ('nodes', 'live', 'free')) or row['mask'] > 7):
                raise ValueError('counter or runtime exceeds native type')
        before['runtime'] = int(before['runtime'])
        before['target'] = int(before['target'], 16)
        if (before['saved'] != '0000000000175310' or not 0 < before['runtime'] < 1 << 64 or
            not 0 < before['target'] < 1 << 64 or before['current'] == before['runtime']):
            raise ValueError('invalid target identity or no actual transition')
        if (after['possible'], after['set'], after['match']) != ('1', '1', '1') or after['current'] != before['runtime']:
            raise ValueError('camera set or getter did not match target')
        if after['restored'] != before['rng']:
            raise ValueError('worker RNG was not restored')
        if int(after['bank_changed']) != int(before['bank'] != after['bank']):
            raise ValueError('bank changed flag disagrees with hashes')
        changed = any(before[k] != after[k] for k in ('nodes', 'mask', 'context'))
        if int(after['context_changed']) != int(changed):
            raise ValueError('context changed flag disagrees with observed census')
    except ValueError as error:
        errors.append(str(error))
        return {'pass': False, 'errors': errors, 'visual_proven': False}
    deltas = {key: after[key] - before[key] for key in ('nodes', 'inserts', 'live', 'free')}
    proof = context_proof(context_order, before, after)
    if proof['available'] and not proof['pass']:
        errors.extend(proof['errors'])
    return {'pass': not errors, 'errors': errors, 'before': before, 'after': after,
            'deltas': deltas, 'context_proof': proof, 'visual_proven': False}


def collect_camera_evidence(run_dir, profiles):
    """Collect only camera lines; raw source logs remain in the owned run."""
    logs, errors = {}, []
    for slot in ('1', '2'):
        try:
            profile = profiles[slot]
            paths = {'lifecycle': profile / 'controller-lifecycle.log', 'engine': profile / 'engine-output.log',
                     'stdout': run_dir / f'game{slot}-stdout.log', 'stderr': run_dir / f'game{slot}-stderr.log'}
            logs[slot] = {}
            for source, path in paths.items():
                with Path(path).open(encoding='utf-8', errors='strict') as stream:
                    logs[slot][source] = [line for line in stream if 'X4LAN CAMERA' in line or 'LINUX_EXTERNAL_CAMERA' in line]
        except (OSError, UnicodeError, KeyError) as error:
            errors.append(f'client {slot}: camera evidence unavailable: {error}')
    result = parse_camera_evidence(logs)
    result['errors'].extend(errors)
    result['pass'] = not result['errors']
    return result
