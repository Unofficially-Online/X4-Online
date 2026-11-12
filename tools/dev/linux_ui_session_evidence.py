"""Compact only successfully validated finite-session proofs.

Raw files and their original hashes remain authoritative. Failed proofs stay
expanded, including bad rows; this does not replace any existing validation.
"""
import hashlib
import json

from linux_gate_evidence import ROLLING


def sequence_digest(rows):
    digest = hashlib.sha256()
    count = 0
    for row in rows:
        digest.update(json.dumps(row, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()+b'\n')
        count += 1
    return {'count': count, 'sha256': digest.hexdigest()}


def compact_validated_result(result):
    """Called after every strict parser and exact peer-record comparison."""
    summaries = {}
    for slot, proof in result.get('physics_discard', {}).get('clients', {}).items():
        if proof.get('pass') is not True:
            continue
        frames = proof.get('frames', [])
        summary = sequence_digest(frames)
        summary['first_tick'] = frames[0]['tick'] if frames else None
        summary['last_tick'] = frames[-1]['tick'] if frames else None
        summary['body_semantics'] = sequence_digest(
            {key: body[key] for key in ('stage','index','runtime','present','id','motion_present','position','rotation','linear','angular')}
            for frame in frames for body in frame['bodies'])
        proof['frame_summary'] = summary
        proof['first_frame'] = frames[0] if frames else None
        proof['last_frame'] = frames[-1] if frames else None
        proof.pop('frames', None)
        records = proof.pop('slot_records', {})
        proof['slot_streams'] = {key: sequence_digest(rows) for key, rows in records.items()}
        worker = proof.get('worker', {})
        if worker.get('pass') is True:
            worker['event_summary'] = sequence_digest(worker.pop('events', []))
        summaries[slot] = {'body_semantics': summary['body_semantics'], 'slot_streams': proof['slot_streams']}
    result['session_peer_streams'] = summaries
    for key in ('ui_overlay_evidence', 'ui_render_lease_evidence'):
        for proof in result.get(key, {}).values():
            if proof.get('pass') is not True:
                continue
            if 'frames' in proof:
                frames = proof.pop('frames')
                proof['frame_summary'] = sequence_digest(frames)
                proof['first_frame'] = frames[0] if frames else None
                proof['last_frame'] = frames[-1] if frames else None
            if 'raw_native_receipts' in proof:
                proof['raw_receipt_summary'] = sequence_digest(proof.pop('raw_native_receipts'))
    result['session_evidence_format'] = 'validated_compact_v1'


def rolling_world_summary(engine, expected_ticks):
    """Stream the complete retained rolling-root rows, without expanding JSON."""
    digest = hashlib.sha256()
    count, errors = 0, []
    with engine.open(encoding='utf-8', errors='strict') as stream:
        for line in stream:
            if 'NATIVE_GATE WORLD_ROLLING ' not in line:
                continue
            match = ROLLING.search(line)
            count += 1
            if not match or int(match['tick']) != count:
                if len(errors) < 4:
                    errors.append(f'malformed, missing or unordered rolling root near row {count}')
            else:
                digest.update(line[line.index('NATIVE_GATE WORLD_ROLLING '):].encode())
    if count != expected_ticks:
        errors.append(f'expected {expected_ticks} rolling rows, observed {count}')
    return {'count':count, 'sha256':digest.hexdigest(), 'errors':errors,
            'full_universe_snapshot_proven':False}
