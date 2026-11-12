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


def validate_evidence(logs_by_slot, ticks, stride, rolling_slices=None, motion_watch=None,
                      motion_window=None, player_control_bank_probe=False,
                      player_control_boundary_probe=False):
    """Add explicit probe receipts without changing default world validation."""
    if not player_control_bank_probe and not player_control_boundary_probe:
        return _validate_motion_world_evidence(logs_by_slot, ticks, stride, rolling_slices,
                                               motion_watch, motion_window)
    groups = {}
    if player_control_bank_probe:
        groups['player_control_bank_probe'] = {
            slot: ControlBankProbeEvidence(slot, ticks) for slot in logs_by_slot}
    if player_control_boundary_probe:
        groups['player_control_boundary_probe'] = {
            slot: ControlBoundaryEvidence(slot, ticks) for slot in logs_by_slot}

    def observed_lines(slot, lines):
        for line in lines:
            for watchers in groups.values():
                watchers[slot].observe(line)
            yield line

    result = _validate_motion_world_evidence(
        {slot: observed_lines(slot, lines) for slot, lines in logs_by_slot.items()},
        ticks, stride, rolling_slices, motion_watch, motion_window)
    for name, watchers in groups.items():
        probe_errors = []
        samples = {}
        for slot, watcher in watchers.items():
            watcher.finish()
            probe_errors.extend(watcher.errors)
            if watcher.capture is not None:
                samples[slot] = watcher.capture
        if samples and any(sample != next(iter(samples.values())) for sample in samples.values()):
            probe_errors.append(f'{name.upper()} captured evidence differs between clients')
        result[name] = samples
        result[name + '_errors'] = probe_errors
        result['evidence_errors'].extend(probe_errors)
    if player_control_boundary_probe:
        result['player_control_boundary_scope'] = (
            'main/worker rendezvous readiness only; no claim that other threads are excluded')
    result['evidence_ok'] = not result['evidence_errors']
    return result


class ControlBoundaryEvidence:
    """Tick-five readiness receipts; never a full writer-exclusion verdict."""
    FIXED = {
        'requested': '1', 'failure': '0', 'readiness_only': '1', 'bank_writes': '0',
        'context_writes': '0', 'reference_calls': '0', 'other_threads_excluded': '0',
    }
    PENDING = {
        'captured_tick': '0', 'main_parked': '0', 'worker_entered': '0',
        'worker_released': '0', 'main_return_allowed': '0', 'complete': '0', 'state': '0',
    }
    COMPLETE = {
        'captured_tick': '5', 'main_parked': '1', 'worker_entered': '1',
        'worker_released': '1', 'main_return_allowed': '1', 'complete': '1', 'state': '126',
    }
    WAITING_MAIN_ACK = {**COMPLETE, 'main_return_allowed': '0', 'complete': '0', 'state': '62'}

    def __init__(self, slot, ticks):
        self.slot, self.ticks = slot, ticks
        self.events = set()
        self.last_tick = 1
        self.stops = 0
        self.capture = None
        self.first_capture = None
        self.errors = []

    def error(self, message):
        self.errors.append(f'client {self.slot}: PLAYER_CONTROL_BOUNDARY {message}')

    def observe(self, line):
        match = re.search(r'NATIVE_GATE PLAYER_CONTROL_BOUNDARY(_STOP)?(?:\s|$)(.*)$', line)
        if not match:
            return
        fields = {}
        for token in match[2].strip().split():
            if token.count('=') != 1:
                self.error('malformed status field')
                return
            name, value = token.split('=', 1)
            if not name or not value or name in fields:
                self.error('missing or duplicate status field')
                return
            fields[name] = value
        is_stop = bool(match[1])
        if is_stop:
            self.stops += 1
            if self.stops != 1:
                self.error('duplicate STOP')
            if 'tick' in fields:
                self.error('unexpected STOP tick')
            expected = self.COMPLETE
            if not any(tick >= 6 for tick in self.events):
                self.error('STOP before a tick-six-or-later event')
        else:
            tick_text = fields.pop('tick', '')
            if not tick_text.isdecimal():
                self.error('missing or invalid event tick')
                return
            tick = int(tick_text)
            if tick in self.events:
                self.error(f'duplicate event tick {tick}')
            self.events.add(tick)
            if not 2 <= tick <= self.ticks:
                self.error(f'event tick {tick} outside requested run')
            if tick <= self.last_tick:
                self.error('events out of order')
            self.last_tick = tick
            if self.stops:
                self.error('event after STOP')
            expected = self.PENDING if tick < 5 else self.COMPLETE
            if tick == 5 and fields.get('state') == '62':
                expected = self.WAITING_MAIN_ACK
        expected = {**self.FIXED, **expected}
        if any(fields.get(key) != value for key, value in expected.items()):
            self.error('unexpected capture, failure, write claim, or rendezvous state')
            return
        if fields['captured_tick'] == '5':
            # Main acknowledgement may legitimately change after tick five.
            stable = {key: value for key, value in fields.items()
                      if key not in ('main_return_allowed', 'complete', 'state')}
            if self.first_capture is None:
                self.first_capture = stable
            elif stable != self.first_capture:
                self.error('stable captured fields changed')
            if fields['complete'] == '1':
                if self.capture is not None and fields != self.capture:
                    self.error('complete cached evidence changed')
                self.capture = fields

    def finish(self):
        if self.ticks < 6:
            self.error('requested run is shorter than six ticks')
        missing = set(range(2, self.ticks + 1)) - self.events
        if missing:
            self.error(f'missing event ticks {sorted(missing)}')
        if self.capture is None:
            self.error('missing successful tick-five readiness capture')
        if self.stops != 1:
            self.error('missing or duplicate STOP')


class ControlBankProbeEvidence:
    """One tick-five native capture may legitimately appear in many cached rows."""
    SUCCESS = {
        'requested': '1', 'attempted': '1', 'captured_tick': '5', 'complete': '1',
        'clean': '1', 'canonical_bytes_restored': '1', 'input_tree_unchanged': '1',
        'canonical_only': '1', 'native_update_calls': '0',
        'temporary_requested_space': '0', 'cleanup_complete': '1', 'reason': 'none',
    }

    def __init__(self, slot, ticks):
        self.slot, self.ticks = slot, ticks
        self.events = set()
        self.stops = 0
        self.capture = None
        self.success_event = False
        self.errors = []

    def error(self, message):
        self.errors.append(f'client {self.slot}: PLAYER_CONTROL_BANK_PROBE {message}')

    def observe(self, line):
        match = re.search(r'NATIVE_GATE PLAYER_CONTROL_BANK_PROBE(_STOP)?(?:\s|$)(.*)$', line)
        if not match:
            return
        is_stop = bool(match[1])
        tokens = match[2].strip().split()
        fields = {}
        for token in tokens:
            if token.count('=') != 1:
                self.error('malformed status field')
                return
            name, value = token.split('=', 1)
            if not name or not value or name in fields:
                self.error('missing or duplicate status field')
                return
            fields[name] = value
        if is_stop:
            self.stops += 1
            if self.stops != 1:
                self.error('duplicate STOP')
            if 'tick' in fields:
                self.error('unexpected STOP tick')
        else:
            tick_text = fields.pop('tick', '')
            if not tick_text.isdecimal():
                self.error('missing or invalid event tick')
                return
            tick = int(tick_text)
            if tick in self.events:
                self.error(f'duplicate event tick {tick}')
            self.events.add(tick)
            if not 2 <= tick <= self.ticks:
                self.error(f'event tick {tick} outside requested run')
            if self.stops:
                self.error('event after STOP')
            if tick < 5:
                pending = {'requested': '1', 'attempted': '0', 'captured_tick': '0', 'complete': '0'}
                if any(fields.get(key) != value for key, value in pending.items()):
                    self.error('early capture or malformed pending status')
                return
        if any(fields.get(key) != value for key, value in self.SUCCESS.items()):
            self.error('missing or unsuccessful tick-five capture/restoration evidence')
            return
        before = fields.get('zone_count_before', '')
        after = fields.get('zone_count_after', '')
        if not before.isdecimal() or int(before) < 1 or after != before:
            self.error('zone counts must be positive and unchanged')
            return
        if is_stop and not self.success_event:
            self.error('STOP has no preceding successful event')
        if self.capture is None:
            self.capture = fields
        elif self.capture != fields:
            self.error('cached capture evidence changed')
        if not is_stop:
            self.success_event = True

    def finish(self):
        if self.ticks < 6:
            self.error('requested run is shorter than six ticks')
        if not self.success_event:
            self.error('missing successful event at or after tick five')
        if self.stops != 1:
            self.error('missing or duplicate STOP')


def _validate_motion_world_evidence(logs_by_slot, ticks, stride, rolling_slices=None, motion_watch=None, motion_window=None):
    """Validate census and optional independent motion receipts in one stream pass."""
    if motion_watch is None and motion_window is None:
        return validate_world_evidence(logs_by_slot, ticks, stride, rolling_slices)
    from world_motion_evidence import MotionEvidence, compare_evidence
    watchers = {slot: MotionEvidence(slot, motion_watch, motion_window, ticks) for slot in logs_by_slot}

    def observed_lines(slot, lines):
        for line in lines:
            watchers[slot].observe(line)
            yield line

    result = validate_world_evidence({slot: observed_lines(slot, lines) for slot, lines in logs_by_slot.items()},
                                     ticks, stride, rolling_slices)
    motion = compare_evidence(list(watchers.values()))
    result.update(motion)
    result['evidence_errors'].extend(motion['world_motion_errors'])
    result['evidence_ok'] = not result['evidence_errors']
    return result


def validate_world_evidence(logs_by_slot, ticks, stride, rolling_slices=None):
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
