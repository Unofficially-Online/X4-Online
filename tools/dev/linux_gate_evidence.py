"""Check the Linux live runner's census and stop evidence.

This is a summary-level check, not a reconstruction of world objects or proof
of native behaviour. The probe captures tick 1 and each stride tick; the relay
stops only after receiving the requested final tick's report. Unknown class,
motion and reference counts are reported as coverage limits: the native hash
includes their flags, but does not claim the missing underlying state.
"""
import re

STOP = re.compile(
    r'NATIVE_GATE STOP slot=p(?P<slot>\d+) completed=(?P<completed>\d+) '
    r'reason=(?P<reason>\d+) restored=(?P<restored>\d+)(?:\s|$)'
)
IDENTITY = re.compile(
    r'NATIVE_GATE WORLD_IDENTITY_STOP calls=(?P<calls>\d+) count=(?P<count>\d+) '
    r'late=(?P<late>\d+) restored=(?P<restored>\d+) failures=(?P<failures>\d+)(?:\s|$)'
)
HASH_STOP = re.compile(
    r'NATIVE_GATE WORLD_HASH_STOP scans=(?P<scans>\d+) failures=(?P<failures>\d+)(?:\s|$)'
)
HASH = re.compile(
    r'NATIVE_GATE WORLD_HASH tick=(?P<tick>\d+) objects=(?P<objects>\d+) '
    r'present=(?P<present>\d+) changed=(?P<changed>\d+) '
    r'unknown_class=(?P<unknown_class>\d+) unknown_motion=(?P<unknown_motion>\d+) '
    r'unknown_references=(?P<unknown_references>\d+)(?: created=\d+)? '
    r'state=(?P<state>WORLD_IMPORTED_2:(?P<state_tick>\d+):(?P<state_objects>\d+):'
    + ':'.join([r'[0-9a-f]{64}'] * 4) + r')\s*$'
)


def is_fatal_gate_message(line):
    """Recognize fatal gate records even after the runner stops watching live."""
    if re.search(r'NATIVE_GATE REFUSED(?:\s|$)', line):
        return True
    error = re.search(r'NATIVE_GATE ERROR(?:\s|$)', line)
    if not error:
        return False
    detail = line[error.end():].lstrip()
    return not re.match(r'(?:DEGRADED|DEFAULT_SKIPPED)(?:\s|$)', detail)


def numeric_fields(match):
    return {name: int(value) for name, value in match.groupdict().items()}


def parse_client(slot, lines):
    """Read only the records relevant to this verdict, retaining duplicates."""
    records = {'stops': [], 'identities': [], 'scan_stops': [], 'captures': {},
               'coverage': [], 'problems': [], 'errors': []}
    errors = records['errors']
    for line in lines:
        if is_fatal_gate_message(line):
            errors.append(f'client {slot}: fatal gate message: {line.strip()}')
        if 'NATIVE_GATE STOP ' in line:
            match = STOP.search(line)
            if match:
                records['stops'].append(numeric_fields(match))
            else:
                errors.append(f'client {slot}: malformed STOP')
        elif 'NATIVE_GATE WORLD_IDENTITY_STOP ' in line:
            match = IDENTITY.search(line)
            if match:
                records['identities'].append(numeric_fields(match))
            else:
                errors.append(f'client {slot}: malformed WORLD_IDENTITY_STOP')
        elif 'NATIVE_GATE WORLD_HASH_STOP ' in line:
            match = HASH_STOP.search(line)
            if match:
                records['scan_stops'].append(numeric_fields(match))
            else:
                errors.append(f'client {slot}: malformed WORLD_HASH_STOP')
        elif 'NATIVE_GATE WORLD_CAPTURE_PROBLEM' in line:
            records['problems'].append(line.split('NATIVE_GATE WORLD_CAPTURE_PROBLEM', 1)[1].strip())
        elif 'NATIVE_GATE WORLD_HASH ' in line:
            match = HASH.search(line)
            if not match:
                errors.append(f'client {slot}: malformed WORLD_HASH')
                continue
            fields = match.groupdict()
            tick = int(fields['tick'])
            objects = int(fields['objects'])
            if objects == 0 or int(fields['state_tick']) != tick or int(fields['state_objects']) != objects:
                errors.append(f'client {slot}: empty or inconsistent WORLD_HASH at tick {tick}')
            if tick in records['captures']:
                errors.append(f'client {slot}: duplicate WORLD_HASH at tick {tick}')
            else:
                records['captures'][tick] = fields['state']
            coverage = {name: int(fields[name]) for name in (
                'tick', 'objects', 'present', 'unknown_class', 'unknown_motion', 'unknown_references')}
            records['coverage'].append(coverage)
    return records


def validate_client(slot, records, ticks, expected_ticks):
    errors = records['errors']
    stops = records['stops']
    identities = records['identities']
    stop = stops[0] if len(stops) == 1 else None
    identity = identities[0] if len(identities) == 1 else None
    expected_stop = {'slot': slot, 'completed': ticks, 'reason': 1, 'restored': 1}
    if stop != expected_stop:
        errors.append(f'client {slot}: missing, duplicate, or unsuccessful STOP for tick {ticks}')
    if (not identity or identity['restored'] != 1 or identity['failures'] != 0
            or identity['count'] < 1 or identity['calls'] < identity['count']):
        errors.append(f'client {slot}: missing, duplicate, or unsuccessful WORLD_IDENTITY_STOP')
    expected_scan_stop = {'scans': len(expected_ticks), 'failures': 0}
    if records['scan_stops'] != [expected_scan_stop]:
        errors.append(f'client {slot}: missing, duplicate, or unsuccessful WORLD_HASH_STOP')
    if records['problems']:
        errors.append(f'client {slot}: world capture problem')
    captured_ticks = records['captures'].keys()
    missing = expected_ticks - captured_ticks
    extra = captured_ticks - expected_ticks
    if missing:
        errors.append(f'client {slot}: missing WORLD_HASH ticks {sorted(missing)}')
    if extra:
        errors.append(f'client {slot}: unexpected WORLD_HASH ticks {sorted(extra)}')
    return stop, identity


def validate_evidence(logs_by_slot, ticks, stride):
    """Return runner-compatible fields and errors for each local client's log.

    logs_by_slot maps actual slot numbers to iterables of decoded lines. A
    single local slot is valid, but does not compare it with a remote client.
    """
    if not logs_by_slot or ticks < 1 or stride < 1:
        raise ValueError('at least one slot and positive ticks and stride are required')
    expected_ticks = {1, *range(stride, ticks + 1, stride)}
    stops = []
    identities = []
    problems = []
    hashes = []
    coverage = []
    errors = []
    for slot, lines in logs_by_slot.items():
        records = parse_client(slot, lines)
        stop, identity = validate_client(slot, records, ticks, expected_ticks)
        stops.append(stop)
        identities.append(identity)
        problems.append(records['problems'])
        hashes.append(records['captures'])
        coverage.append(records['coverage'])
        errors.extend(records['errors'])
    common = sorted(set.intersection(*(set(client_hashes) for client_hashes in hashes)))
    difference = next((tick for tick in common
                       if any(client_hashes[tick] != hashes[0][tick] for client_hashes in hashes[1:])), None)
    if difference is not None:
        errors.append(f'WORLD_HASH differs at tick {difference}')
    return {'stops': stops, 'world_identity': identities, 'world_capture_problems': problems,
            'world_hash_ticks': len(common), 'world_hash_first_difference': difference,
            'world_hash_coverage': coverage, 'evidence_errors': errors, 'evidence_ok': not errors}
