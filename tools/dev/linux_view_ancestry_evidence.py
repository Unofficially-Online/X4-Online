"""Strict bounded VIEW ancestry receipts; no inference of global reader safety."""
import hashlib
import re

NUMBER = r'(0|[1-9][0-9]{0,19})'
POINTER = r'(0|[1-9a-f][0-9a-f]{0,15})'
SITES = ('weapon', 'engine', 'ship')


def parse_view_ancestry_evidence(lines, slot, enabled, last_tick, expected):
    result = {'pass': False, 'errors': [], 'p2_observed': {},
              'reader_exclusion_proven': False, 'safe_resume_proven': False,
              'foreign_calls': 'lower_bound', 'raw_receipts': []}
    errors = result['errors']
    rows = [line for line in lines if 'X4 VIEW_ANCESTRY' in line]
    result['raw_receipts'] = rows
    if type(enabled) is not bool or type(slot) is not int or slot not in (1, 2):
        errors.append('invalid activation or slot')
        return result
    if not enabled:
        if rows:
            errors.append('unexpected disabled ancestry receipt')
        result['pass'] = not errors
        return result
    if type(last_tick) is not int or not 5 <= last_tick <= 0xffffffffffffffff or not isinstance(expected, dict):
        errors.append('missing authenticated boundary or identity')
        return result
    keys = ('p1', 'p2', 'p1_runtime', 'p2_runtime', 'p1_saved', 'p2_saved')
    required = keys[1:]
    if any(type(expected.get(key)) is not int or not 0 < expected[key] <= 0xffffffffffffffff for key in required) or (
            'p1' in expected and (type(expected['p1']) is not int or not 0 < expected['p1'] <= 0xffffffffffffffff)):
        errors.append('missing authenticated ship identities')
        return result
    if not rows or any(not row.endswith('\n') or len(row) >= 1400 for row in rows):
        errors.append('missing, truncated or oversized ancestry receipts')
        return result
    bind = re.fullmatch(r'X4 VIEW_ANCESTRY v=1 stage=bind tick='+NUMBER+' slot='+NUMBER+
        ' tid='+NUMBER+' p1='+POINTER+' p2='+POINTER+' p1_runtime='+NUMBER+' p2_runtime='+NUMBER+
        ' p1_saved='+NUMBER+' p2_saved='+NUMBER+' ref_before='+NUMBER+' ref_after='+NUMBER+r'\n', rows[0])
    if not bind:
        errors.append('invalid bind receipt')
        return result
    raw = bind.groups()
    tick, found_slot, tid = map(int, raw[:3])
    actual = {'p1': int(raw[3], 16), 'p2': int(raw[4], 16),
              **dict(zip(keys[2:], map(int, raw[5:9])))}
    before, after = map(int, raw[9:11])
    if (found_slot != slot or not 5 <= tick <= last_tick or not 0 < tid <= 0xffffffff or
            any(actual[key] != expected[key] for key in required) or
            ('p1' in expected and actual['p1'] != expected['p1']) or actual['p1'] == actual['p2'] or
            any(not 0x10000 <= actual[key] <= 0x7fffffffffff for key in ('p1', 'p2')) or
            before < 1 or after != before + 1 or after >= 0xffffffff):
        errors.append('bind identity, generation or reference mismatch')
    cursor = 1
    sites = {}
    for site in SITES:
        samples, counts, previous = [], [0, 0, 0, 0], 0
        while cursor < len(rows) and rows[cursor].startswith('X4 VIEW_ANCESTRY v=1 stage=sample '):
            pattern = (r'X4 VIEW_ANCESTRY v=1 stage=sample site='+site+' index='+NUMBER+
                ' tick='+NUMBER+' tid='+NUMBER+' self='+POINTER+' arg='+POINTER+' category='+NUMBER+
                ' complete='+NUMBER+' count='+NUMBER+r' nodes=([^\s]+)\n')
            match = re.fullmatch(pattern, rows[cursor])
            if not match:
                errors.append('invalid sample grammar or site ordering')
                return result
            index, gen, sample_tid = map(int, match.groups()[:3])
            self_pointer, argument = int(match[4], 16), int(match[5], 16)
            category, complete, count = map(int, match.groups()[5:8])
            nodes = []
            if match[9] != '-':
                for node in match[9].split(','):
                    parts = re.fullmatch(POINTER+':'+POINTER+':'+NUMBER, node)
                    if not parts:
                        errors.append('invalid ancestry node')
                        return result
                    nodes.append((int(parts[1], 16), int(parts[2], 16), int(parts[3])))
            first = any(node[0] == actual['p1'] for node in nodes)
            second = any(node[0] == actual['p2'] for node in nodes)
            derived = (1 if first else 2) if first != second else (3 if not first and complete == 1 else 0)
            if (index != len(samples) or sample_tid != tid or not tick <= gen <= last_tick or
                    (previous and gen - previous < 6) or not 0 <= category <= 3 or complete not in (0, 1) or
                    count != len(nodes) or count > 8 or not 0x10000 <= self_pointer <= 0x7fffffffffff or
                    (nodes and nodes[0][0] != self_pointer) or
                    (site != 'ship' and argument) or category != derived or
                    any(not 0x10000 <= node[0] <= 0x7fffffffffff or not node[1] or
                        node[2] > 0xffffffffffffffff for node in nodes)):
                errors.append('sample identity, cadence, count or category mismatch')
            if category in range(4):
                counts[category] += 1
                if counts[category] > 2:
                    errors.append('too many samples in category')
            samples.append({'tick': gen, 'self': self_pointer, 'argument': argument,
                            'category': category, 'complete': complete, 'nodes': nodes})
            previous = gen
            cursor += 1
        if cursor >= len(rows):
            errors.append('missing site summary')
            return result
        summary = re.fullmatch(r'X4 VIEW_ANCESTRY v=1 stage=site site='+site+
            ' started='+NUMBER+' returned='+NUMBER+' examined='+NUMBER+' throttled='+NUMBER+
            ' overflow='+NUMBER+' foreign_lower='+NUMBER+' rows='+NUMBER+r'\n', rows[cursor])
        if not summary:
            errors.append('invalid site summary or ordering')
            return result
        started, returned, examined, throttled, overflow, foreign, count = map(int, summary.groups())
        if (any(value > 0xffffffffffffffff for value in (started, returned, examined, throttled, overflow, foreign)) or
                started != returned or started != examined + throttled + overflow or examined > 128 or
                count != len(samples) or count > examined or count > 8):
            errors.append('site accounting mismatch')
        sites[site] = {'started': started, 'examined': examined, 'throttled': throttled,
                       'overflow': overflow, 'foreign_lower': foreign, 'samples': samples}
        result['p2_observed'][site] = counts[2] > 0
        cursor += 1
    if cursor != len(rows) - 1:
        errors.append('extra, missing or reordered ancestry receipts')
        return result
    close = re.fullmatch(r'X4 VIEW_ANCESTRY v=1 stage=close tick='+NUMBER+' slot='+NUMBER+
        ' ref_before='+NUMBER+' ref_after='+NUMBER+r' sampling_closed=1 unpatched=0 foreign=lower_bound\n', rows[cursor])
    if not close:
        errors.append('invalid terminal ancestry receipt')
        return result
    end, end_slot, ref_before, ref_after = map(int, close.groups())
    if end != last_tick or end_slot != slot or not 1 < ref_before < 0xffffffff or ref_after != ref_before - 1:
        errors.append('close generation, slot or reference mismatch')
    result.update(bind=actual, bind_tick=tick, tid=tid, sites=sites,
                  p1_pointer_independently_bound='p1' in expected,
                  capture_closed=True, forwarders_resident=True,
                  p2_mapping_eligible=any(result['p2_observed'].values()),
                  coverage_complete=False)
    result['pass'] = not errors
    return result


def collect_view_ancestry_evidence(profile, stderr, slot, enabled, last_tick, expected):
    try:
        data = stderr.read_bytes()
        text = data.decode('utf-8', errors='strict').splitlines(keepends=True)
        result = parse_view_ancestry_evidence(text, slot, enabled, last_tick, expected)
        result['stderr_sha256'] = hashlib.sha256(data).hexdigest()
        for name in ('engine-output.log', 'controller-lifecycle.log'):
            result[name.replace('-', '_').replace('.log', '_sha256')] = hashlib.sha256((profile / name).read_bytes()).hexdigest()
        return result
    except (OSError, UnicodeError):
        return {'pass': False, 'errors': ['evidence files unreadable'], 'raw_receipts': []}
