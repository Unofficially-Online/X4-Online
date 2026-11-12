"""Validate retained native Linux stock-keyboard observer evidence, never replay.

Usage (reads complete, unfiltered game logs):
  python tools/dev/linux_stock_keyboard_evidence.py --ticks 180 \
    --log1 RUN/game1.log --log2 RUN/game2.log --result RUN/run-result.json

Default acceptance requires zero events and proves only the recorded idle
installation/input-boundary/restoration lifecycle. Nonzero observation needs
--events-manifest JSON with exact ordered events for both peers, for example:
  {"schema": 1, "module_base": "0x400000", "events": {
    "1": [{"tick": 26, "kind": 1, "id": 8, "state": 1,
           "caller_rva": "0xc51041"}], "2": []}}
Caller RVAs are checked against the explicitly declared module base. This
numeric check does not verify executable identity or resolve call-site symbols.
The manifest is an expected-input specification, not generated missing proof.
No acceptance here establishes input replay, multiplayer, world completeness,
performance, or complete input-dispatch coverage. Existing nonfatal gate
DEGRADED/DEFAULT_SKIPPED rows are retained explicitly as evidence limits.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

try:
    from .linux_gate_evidence import is_fatal_gate_message
except ImportError:
    from linux_gate_evidence import is_fatal_gate_message

STATUS_NUMBERS = {
    'requested', 'installed_tick', 'input_tick', 'active', 'owned', 'uncertain',
    'pending_readback', 'permission_pending', 'depth', 'overflow', 'queued',
    'thread_failure', 'failed', 'cleaned',
}
STATUS_TEXT = {'mode', 'problem', 'failure_reason'}
STATE_IDS = {2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 18, 19}
DECIMAL = re.compile(r'0|[1-9][0-9]*')
RAW_HEX = re.compile(r'0|[1-9a-f][0-9a-f]*')
MANIFEST_HEX = re.compile(r'0x(?:0|[1-9a-f][0-9a-f]*)')
CLAIM_LIMIT = (
    'Observer evidence only: no input replay, multiplayer, performance, full-world '
    'coverage, or complete dispatch coverage is established.'
)


def parse_fields(payload, required, optional=()):
    """Reject duplicate/unknown/missing raw fields before any normalization."""
    fields = {}
    for item in payload.split():
        if item.count('=') != 1:
            raise ValueError(f'malformed field {item!r}')
        name, value = item.split('=', 1)
        if not name or not value or name in fields:
            raise ValueError(f'empty or duplicate field {name!r}')
        fields[name] = value
    if set(fields) - set(required) - set(optional) or set(required) - set(fields):
        raise ValueError(f'field inventory mismatch: {sorted(fields)}')
    return fields


def decimal(value):
    if not isinstance(value, str) or not DECIMAL.fullmatch(value):
        raise ValueError(f'noncanonical unsigned decimal {value!r}')
    return int(value)


def manifest_number(value):
    if not isinstance(value, str) or not MANIFEST_HEX.fullmatch(value):
        raise ValueError(f'expected canonical hexadecimal string, got {value!r}')
    return int(value, 16)


def expected_events(manifest, ticks):
    if manifest is None:
        return None, {1: [], 2: []}
    if not isinstance(manifest, dict) or set(manifest) != {'schema', 'module_base', 'events'}:
        raise ValueError('event manifest requires exactly schema/module_base/events')
    if type(manifest['schema']) is not int or manifest['schema'] != 1:
        raise ValueError('unsupported event manifest schema')
    module_base = manifest_number(manifest['module_base'])
    if module_base != 0x400000:
        raise ValueError('this observer contract requires the pinned Linux ET_EXEC base 0x400000')
    if not isinstance(manifest['events'], dict) or set(manifest['events']) != {'1', '2'}:
        raise ValueError('expected event lists must explicitly name both slots')
    result = {}
    for slot in (1, 2):
        events = manifest['events'][str(slot)]
        if not isinstance(events, list):
            raise ValueError('expected events must be lists')
        normalized = []
        previous_tick = 2
        for event in events:
            if not isinstance(event, dict) or set(event) != {'tick', 'kind', 'id', 'state', 'caller_rva'}:
                raise ValueError('expected event requires tick/kind/id/state/caller_rva')
            if any(type(event[name]) is not int for name in ('tick', 'kind', 'id', 'state')):
                raise ValueError('expected event numeric fields must be integers')
            if not previous_tick <= event['tick'] <= ticks:
                raise ValueError('expected event ticks must be ordered within 2..N')
            validate_event_identity(event)
            relative_caller = manifest_number(event['caller_rva'])
            if not 0 < relative_caller < 0x100000000:
                raise ValueError('expected caller RVA outside supported numeric range')
            normalized.append({**event, 'caller_rva': relative_caller})
            previous_tick = event['tick']
        result[slot] = normalized
    return module_base, result


def validate_event_identity(event):
    if event['kind'] == 0 and event['id'] == 0x7b and event['state'] == 0:
        return
    if event['kind'] == 1 and event['id'] in STATE_IDS and event['state'] in (0, 1):
        return
    raise ValueError('event is outside the selected action/state observer contract')


def validate_status(fields, tick, stopped=False):
    numbers = {name: decimal(fields[name]) for name in STATUS_NUMBERS}
    expected = {name: 0 for name in STATUS_NUMBERS}
    expected['requested'] = 1
    if stopped:
        expected.update(installed_tick=2, input_tick=tick, cleaned=1)
        problem = 'restored'
    elif tick == 1:
        problem = 'off'
    else:
        expected.update(installed_tick=2, input_tick=tick, active=1, owned=32767)
        problem = 'observing_only'
    for name, value in expected.items():
        if numbers[name] != value:
            raise ValueError(f'{name}={numbers[name]}, expected {value}')
    if fields['mode'] != 'observe_only' or fields['problem'] != problem or fields['failure_reason'] != 'none':
        raise ValueError('observer mode/problem/failure_reason does not match lifecycle')


def same_typed_value(actual, expected):
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            same_typed_value(left, right) for left, right in zip(actual, expected))
    if isinstance(expected, dict):
        return set(actual) == set(expected) and all(
            same_typed_value(actual[name], expected[name]) for name in expected)
    return actual == expected


def validate_receipt(result, ticks, errors):
    if not isinstance(result, dict):
        errors.append('run-result must be an object')
        return
    expected = {
        'pass': True, 'ticks': ticks, 'local_slots': [1, 2], 'armed': [True, True],
        'owned_processes_remaining': {}, 'steam_unchanged': True, 'x4_left': [],
        'evidence_ok': True, 'evidence_errors': [],
    }
    for name, value in expected.items():
        if name not in result or not same_typed_value(result[name], value):
            errors.append(f'run-result {name}: missing or unexpected value')
    if type(result.get('ticks')) is not int:
        errors.append('run-result ticks must be an integer')
    for name in ('error', 'postprocessing_error', 'cleanup_errors'):
        if result.get(name):
            errors.append(f'run-result reports {name}')
    before = result.get('steam_before')
    after = result.get('steam_after')
    identity_valid = (isinstance(before, dict) and set(before) == {'pid', 'start', 'uid'}
                      and all(type(before[name]) is int for name in before)
                      and before['pid'] > 0 and before['start'] > 0 and before['uid'] >= 0)
    if not identity_valid or not same_typed_value(after, before):
        errors.append('run-result lacks matching Steam identity receipts')
    expected_stops = [{'slot': slot, 'completed': ticks, 'reason': 1, 'restored': 1} for slot in (1, 2)]
    if not same_typed_value(result.get('stops'), expected_stops):
        errors.append('run-result STOP receipts missing, duplicate, or inconsistent')


def validate_logs(logs_by_slot, result, ticks, event_manifest=None):
    errors = []
    report = {'accepted': False, 'mode': 'idle_observation', 'claim_limit': CLAIM_LIMIT,
              'errors': errors, 'peers': {}, 'raw_nonfatal_limits': []}
    if type(ticks) is not int or not 2 <= ticks <= 8388607:
        errors.append('ticks must be an integer within 2..8388607')
        return report
    validate_receipt(result, ticks, errors)
    try:
        module_base, expected = expected_events(event_manifest, ticks)
    except ValueError as error:
        errors.append(str(error))
        return report
    if event_manifest is not None:
        report['mode'] = 'specified_event_observation'
        report['declared_module_base'] = hex(module_base)
    if (not isinstance(logs_by_slot, dict) or set(logs_by_slot) != {1, 2}
            or any(type(slot) is not int for slot in logs_by_slot)):
        errors.append('exactly two raw logs, keyed by slots 1 and 2, are required')
        return report
    for slot in (1, 2):
        raw = logs_by_slot[slot]
        if not isinstance(raw, str) or not raw:
            errors.append(f'peer {slot}: raw game log absent or empty')
            continue
        observer_ticks = []
        events = []
        gate_stops = 0
        observer_stops = 0
        next_tick = 1
        event_thread = None
        for line_number, line in enumerate(raw.splitlines(), 1):
            context = f'peer {slot} line {line_number}'
            if is_fatal_gate_message(line):
                errors.append(f'{context}: fatal gate record: {line}')
            if re.search(r'NATIVE_GATE ERROR (?:DEGRADED|DEFAULT_SKIPPED)(?:\s|$)', line):
                report['raw_nonfatal_limits'].append({'slot': slot, 'line': line_number, 'raw': line})
            marker = 'NATIVE_GATE '
            if marker not in line:
                if 'STOCK_KEYBOARD_' in line:
                    errors.append(f'{context}: observer row lacks raw gate prefix')
                continue
            payload = line.split(marker, 1)[1]
            tag, _, body = payload.partition(' ')
            if not tag.startswith('STOCK_KEYBOARD') and tag != 'STOP':
                continue
            try:
                if line.count(marker) != 1:
                    raise ValueError('multiple gate records on one raw line')
                if tag == 'STOCK_KEYBOARD_OBSERVER':
                    fields = parse_fields(body, STATUS_NUMBERS | STATUS_TEXT | {'tick'})
                    tick = decimal(fields['tick'])
                    if gate_stops or observer_stops or tick != next_tick or not 1 <= tick <= ticks:
                        raise ValueError(f'duplicate/out-of-order observer tick {tick}; expected {next_tick}')
                    validate_status(fields, tick)
                    observer_ticks.append(tick)
                    next_tick += 1
                elif tag == 'STOCK_KEYBOARD_EVENT':
                    fields = parse_fields(body, {'tick', 'thread', 'caller', 'kind', 'id', 'state'})
                    event = {name: decimal(fields[name]) for name in ('tick', 'thread', 'kind', 'id', 'state')}
                    if gate_stops or observer_stops or event['tick'] != next_tick or not 2 <= event['tick'] <= ticks:
                        raise ValueError('event row outside its pre-status drain phase')
                    validate_event_identity(event)
                    if not event['thread'] or (event_thread is not None and event['thread'] != event_thread):
                        raise ValueError('event thread is zero or changed')
                    event_thread = event['thread']
                    if not RAW_HEX.fullmatch(fields['caller']):
                        raise ValueError('noncanonical raw caller address')
                    if module_base is None:
                        raise ValueError('idle validation saw an event; an explicit expected-events manifest is required')
                    caller = int(fields['caller'], 16)
                    if caller <= module_base:
                        raise ValueError('raw caller is not above declared module base')
                    events.append({name: event[name] for name in ('tick', 'kind', 'id', 'state')} | {'caller_rva': caller - module_base})
                elif tag == 'STOP':
                    fields = parse_fields(body, {'slot', 'completed', 'reason', 'restored'}, {'wait_game_delta', 'max_dt_error'})
                    if gate_stops or observer_stops or next_tick != ticks + 1:
                        raise ValueError('gate STOP duplicated or before complete observer rows')
                    if fields['slot'] != f'p{slot}' or decimal(fields['completed']) != ticks or decimal(fields['reason']) != 1 or decimal(fields['restored']) != 1:
                        raise ValueError('gate STOP identity/completion/restoration mismatch')
                    for name in ('wait_game_delta', 'max_dt_error'):
                        if name in fields and not math.isfinite(float(fields[name])):
                            raise ValueError('nonfinite gate STOP measurement')
                    gate_stops += 1
                elif tag == 'STOCK_KEYBOARD_STOP':
                    fields = parse_fields(body, STATUS_NUMBERS | STATUS_TEXT)
                    if gate_stops != 1 or observer_stops or next_tick != ticks + 1:
                        raise ValueError('observer STOP duplicated or outside final cleanup order')
                    validate_status(fields, ticks, stopped=True)
                    observer_stops += 1
                else:
                    raise ValueError(f'unknown observer record {tag}')
            except (ValueError, OverflowError) as error:
                errors.append(f'{context}: {error}')
        if observer_ticks != list(range(1, ticks + 1)):
            errors.append(f'peer {slot}: incomplete exact observer tick sequence')
        if gate_stops != 1 or observer_stops != 1:
            errors.append(f'peer {slot}: missing successful gate/observer STOP')
        if events != expected[slot]:
            errors.append(f'peer {slot}: observed ordered events differ from expectation')
        report['peers'][str(slot)] = {'observer_rows': len(observer_ticks), 'events': len(events),
                                     'gate_stops': gate_stops, 'observer_stops': observer_stops}
    report['accepted'] = not errors
    return report


def parse_json(text):
    def reject_duplicate_fields(pairs):
        result = {}
        for name, value in pairs:
            if name in result:
                raise ValueError(f'duplicate JSON field {name!r}')
            result[name] = value
        return result
    def reject_nonfinite(value):
        raise ValueError(f'nonfinite JSON constant {value}')
    return json.loads(text, object_pairs_hook=reject_duplicate_fields, parse_constant=reject_nonfinite)


def load_json(path):
    return parse_json(Path(path).read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--log1', type=Path, required=True)
    parser.add_argument('--log2', type=Path, required=True)
    parser.add_argument('--result', type=Path, required=True)
    parser.add_argument('--ticks', type=int, required=True)
    parser.add_argument('--events-manifest', type=Path)
    args = parser.parse_args()
    try:
        paths = {1: args.log1, 2: args.log2}
        source_paths = [*paths.values(), args.result]
        if args.events_manifest:
            source_paths.append(args.events_manifest)
        sources = {path: path.read_bytes() for path in source_paths}
        logs = {slot: sources[path].decode('utf-8', errors='strict') for slot, path in paths.items()}
        result = parse_json(sources[args.result].decode('utf-8', errors='strict'))
        manifest = (parse_json(sources[args.events_manifest].decode('utf-8', errors='strict'))
                    if args.events_manifest else None)
        report = validate_logs(logs, result, args.ticks, manifest)
        report['source_sha256'] = {str(path): hashlib.sha256(raw).hexdigest()
                                   for path, raw in sources.items()}
    except (OSError, ValueError) as error:
        report = {'accepted': False, 'errors': [str(error)], 'claim_limit': CLAIM_LIMIT}
    print(json.dumps(report, indent=2))
    return 0 if report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
