"""Check the Linux live runner's census and stop evidence.

This is a summary-level check, not a reconstruction of world objects or proof
of native behaviour. The probe captures tick 1 and each stride tick; the relay
stops only after receiving the requested final tick's report. Unknown class,
motion and reference counts are reported as coverage limits: the native hash
includes their flags, but does not claim the missing underlying state.
"""
import hashlib
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


def validate_evidence(logs_by_slot, ticks, stride, rolling_slices=None):
    """Return runner-compatible fields and errors for each local client's log.

    logs_by_slot maps actual slot numbers to iterables of decoded lines. A
    single local slot is valid, but does not compare it with a remote client.
    """
    if not logs_by_slot or ticks < 1 or stride < 1:
        raise ValueError('at least one slot and positive ticks and stride are required')
    if rolling_slices is not None:
        return validate_rolling_evidence(logs_by_slot, ticks, rolling_slices)
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


ROLLING_FIELDS = ('tick', 'cycle', 'slice', 'slices', 'begin', 'end', 'objects', 'created')
ROLLING_COVERAGE = ('present', 'unknown_class', 'unknown_motion', 'unknown_references')
EMPTY_SHA256 = hashlib.sha256(b'').hexdigest()
ROLLING = re.compile(
    r'NATIVE_GATE WORLD_ROLLING '
    + ' '.join(name + r'=(?P<' + name + r'>\d+)' for name in ROLLING_FIELDS + ROLLING_COVERAGE)
    + r' state=(?P<state>WORLD_ROLLING_1:'
    + ':'.join(r'(?P<state_' + name + r'>\d+)' for name in ROLLING_FIELDS)
    + ':' + ':'.join([r'[0-9a-f]{64}'] * 4) + r')\s*$'
)
ROLLING_STOP = re.compile(
    r'NATIVE_GATE WORLD_ROLLING_STOP scans=(?P<scans>\d+) '
    r'cycles=(?P<cycles>\d+) failures=(?P<failures>\d+)\s*$'
)


def parse_rolling_client(slot, lines, ticks, slices):
    """Validate in log order; completed slices precede STOP and its footers."""
    errors = []
    captures = {}
    coverage = []
    stops = []
    identities = []
    footers = []
    problems = []
    last_tick = 0
    imported_count = None
    cohort_by_cycle = {}
    last_created = None
    contiguous_ticks = 0
    prefix_open = True
    for line in lines:
        if is_fatal_gate_message(line):
            errors.append(f'client {slot}: fatal gate message: {line.strip()}')
        if 'NATIVE_GATE WORLD_CAPTURE_PROBLEM' in line:
            problems.append(line.split('NATIVE_GATE WORLD_CAPTURE_PROBLEM', 1)[1].strip())
        elif 'NATIVE_GATE WORLD_HASH ' in line or 'NATIVE_GATE WORLD_HASH_STOP' in line:
            errors.append(f'client {slot}: unexpected instantaneous world hash evidence')
        elif 'NATIVE_GATE WORLD_ROLLING_STOP' in line:
            match = ROLLING_STOP.search(line)
            if not match:
                errors.append(f'client {slot}: malformed WORLD_ROLLING_STOP')
            else:
                footers.append(numeric_fields(match))
            if not stops:
                errors.append(f'client {slot}: WORLD_ROLLING_STOP before STOP')
        elif 'NATIVE_GATE WORLD_ROLLING' in line:
            match = ROLLING.search(line)
            if not match:
                errors.append(f'client {slot}: malformed WORLD_ROLLING')
                prefix_open = False
                continue
            previous_errors = len(errors)
            fields = match.groupdict()
            row = {name: int(fields[name]) for name in ROLLING_FIELDS + ROLLING_COVERAGE}
            tick = row['tick']
            if stops or footers or identities:
                errors.append(f'client {slot}: WORLD_ROLLING after STOP or footer')
            if tick != last_tick + 1 or tick > ticks:
                errors.append(f'client {slot}: duplicate, missing, or out-of-order WORLD_ROLLING at tick {tick}')
            last_tick = tick
            if any(row[name] != int(fields['state_' + name]) for name in ROLLING_FIELDS):
                errors.append(f'client {slot}: WORLD_ROLLING state metadata mismatch at tick {tick}')
            if tick < 1 or row['slices'] != slices or row['cycle'] != 1 + (tick - 1) // slices or row['slice'] != (tick - 1) % slices:
                errors.append(f'client {slot}: WORLD_ROLLING schedule mismatch at tick {tick}')
            total = row['objects']
            created = row['created']
            expected_begin = total * row['slice'] // slices
            expected_end = total * (row['slice'] + 1) // slices
            if (total < 1 or created > total or row['slice'] >= slices or
                    row['begin'] != expected_begin or row['end'] != expected_end):
                errors.append(f'client {slot}: invalid WORLD_ROLLING bounds at tick {tick}')
            width = max(0, row['end'] - row['begin'])
            if row['begin'] == row['end'] and fields['state'].split(':')[-4:] != [EMPTY_SHA256] * 4:
                errors.append(f'client {slot}: invalid empty WORLD_ROLLING roots at tick {tick}')
            if (row['present'] > width or
                    any(row[name] > row['present'] for name in ROLLING_COVERAGE[1:])):
                errors.append(f'client {slot}: invalid WORLD_ROLLING coverage at tick {tick}')
            imported = total - created
            if imported_count is None:
                imported_count = imported
            elif imported != imported_count:
                errors.append(f'client {slot}: imported cohort count changed at tick {tick}')
            cycle = row['cycle']
            if cycle in cohort_by_cycle and cohort_by_cycle[cycle] != (total, created):
                errors.append(f'client {slot}: cohort changed within cycle {cycle}')
            elif cycle not in cohort_by_cycle:
                if last_created is not None and created < last_created:
                    errors.append(f'client {slot}: created cohort count decreased at cycle {cycle}')
                cohort_by_cycle[cycle] = (total, created)
                last_created = created
            if tick in captures:
                errors.append(f'client {slot}: duplicate WORLD_ROLLING at tick {tick}')
            else:
                captures[tick] = fields['state']
            coverage.append(row)
            if prefix_open and len(errors) == previous_errors and tick == contiguous_ticks + 1:
                contiguous_ticks = tick
            else:
                prefix_open = False
        elif 'NATIVE_GATE WORLD_IDENTITY_STOP' in line:
            match = IDENTITY.search(line)
            if not match:
                errors.append(f'client {slot}: malformed WORLD_IDENTITY_STOP')
            else:
                identities.append(numeric_fields(match))
            if not stops:
                errors.append(f'client {slot}: WORLD_IDENTITY_STOP before STOP')
        elif re.search(r'NATIVE_GATE STOP(?:\s|$)', line):
            match = STOP.search(line)
            if not match:
                errors.append(f'client {slot}: malformed STOP')
            else:
                stops.append(numeric_fields(match))
            if footers or identities:
                errors.append(f'client {slot}: STOP after footer')
    if set(captures) != set(range(1, ticks + 1)):
        errors.append(f'client {slot}: incomplete WORLD_ROLLING tick coverage')
    stop = stops[0] if len(stops) == 1 else None
    identity = identities[0] if len(identities) == 1 else None
    if stop != {'slot': slot, 'completed': ticks, 'reason': 1, 'restored': 1}:
        errors.append(f'client {slot}: missing, duplicate, or unsuccessful STOP')
    if (not identity or identity['restored'] != 1 or identity['failures'] != 0
            or identity['count'] < 1 or identity['calls'] < identity['count']):
        errors.append(f'client {slot}: missing, duplicate, or unsuccessful WORLD_IDENTITY_STOP')
    if identity and imported_count is not None and imported_count != identity['count']:
        errors.append(f'client {slot}: imported cohort count differs from WORLD_IDENTITY_STOP')
    if footers != [{'scans': ticks, 'cycles': ticks // slices, 'failures': 0}]:
        errors.append(f'client {slot}: missing, duplicate, or unsuccessful WORLD_ROLLING_STOP')
    if problems:
        errors.append(f'client {slot}: world capture problem')
    return stop, identity, problems, captures, coverage, errors, contiguous_ticks


def validate_rolling_evidence(logs_by_slot, ticks, slices):
    """Compare sequential slice receipts, not simultaneous whole-world states.

    Counts establish the declared cohort size, not its actual membership.
    Newly created objects may wait until the next cycle; changes between slice
    samples are not an instantaneous whole-world capture.
    """
    if isinstance(slices, bool) or not isinstance(slices, int) or not 1 <= slices <= 18000:
        raise ValueError('rolling_slices must be an integer from 1 to 18000')
    stops, identities, problems, hashes, coverage, errors = [], [], [], [], [], []
    contiguous_counts = []
    for slot, lines in logs_by_slot.items():
        stop, identity, client_problems, captures, client_coverage, client_errors, contiguous = parse_rolling_client(
            slot, lines, ticks, slices)
        stops.append(stop)
        identities.append(identity)
        problems.append(client_problems)
        hashes.append(captures)
        coverage.append(client_coverage)
        errors.extend(client_errors)
        contiguous_counts.append(contiguous)
    common = sorted(set.intersection(*(set(client_hashes) for client_hashes in hashes)))
    difference = next((tick for tick in common
                       if any(client_hashes[tick] != hashes[0][tick] for client_hashes in hashes[1:])), None)
    if difference is not None:
        errors.append(f'WORLD_ROLLING differs at tick {difference}')
    observed_ticks = min(contiguous_counts)
    partial = observed_ticks % slices
    return {'stops': stops, 'world_identity': identities, 'world_capture_problems': problems,
            'world_hash_ticks': len(common), 'world_hash_first_difference': difference,
            'world_hash_coverage': coverage, 'evidence_errors': errors, 'evidence_ok': not errors,
            'mode': 'rolling', 'requestedcycles': ticks // slices,
            'observed_contiguous_ticks': observed_ticks, 'completecycles': observed_ticks // slices,
            'partialcycle': {'cycle': observed_ticks // slices + 1, 'sampled_slices': partial,
                             'total_slices': slices} if partial else None,
            'coverage_semantic': 'Sequential slices are sampled on different ticks; counts do not prove cohort membership. New objects may wait until the next cycle.'}
