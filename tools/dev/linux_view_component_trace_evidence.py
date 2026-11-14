"""Validate bounded identity samples, without asserting unsampled ownership."""
import hashlib
from pathlib import Path
import re

N = r'(0|[1-9][0-9]{0,19})'
P = r'(0|[1-9a-f][0-9a-f]{0,15})'


def parse_component_trace(lines, ancestry, last_tick, enabled=True):
    out = {'pass': False, 'errors': [], 'reader_exclusion_proven': False,
           'ownership_exhaustive': False, 'foreign_calls': 'lower_bound'}
    errors = out['errors']
    rows = [line for line in lines if 'X4 COMPONENT_TRACE' in line]
    if type(enabled) is not bool:
        errors.append('invalid trace activation'); return out
    if not enabled:
        if rows:
            errors.append('unexpected disabled component trace receipt')
        out['raw_receipts'] = rows
        out['pass'] = not errors
        return out
    if not isinstance(ancestry, dict) or ancestry.get('pass') is not True or not rows:
        errors.append('missing accepted ancestry or component trace')
        return out
    m = re.fullmatch(r'X4 COMPONENT_TRACE v=1 stage=bind tick='+N+' tid='+N+' p1='+P+' p2='+P+r'\n', rows[0])
    if not m:
        errors.append('invalid bind'); return out
    tick, tid = int(m[1]), int(m[2]); p1, p2 = int(m[3], 16), int(m[4], 16)
    if (tick != ancestry.get('bind_tick') or tid != ancestry.get('tid') or
            p1 != ancestry.get('bind', {}).get('p1') or p2 != ancestry.get('bind', {}).get('p2') or
            type(last_tick) is not int or last_tick < tick):
        errors.append('bind identity or tick mismatch')
    cursor, sites = 1, []
    for site in (0, 1):
        samples = []
        while cursor < len(rows) and 'stage=sample ' in rows[cursor]:
            m = re.fullmatch(r'X4 COMPONENT_TRACE v=1 stage=sample site='+N+' index='+N+' tick='+N+
                ' ordinal='+N+' phase='+N+' object='+P+' class='+P+' runtime='+N+' parent='+P+
                ' ancestor='+P+' valid='+N+' slot='+N+' failure='+N+r'\n', rows[cursor])
            if not m:
                errors.append('invalid sample grammar'); return out
            s, index, gen, ordinal, phase = map(int, m.groups()[:5])
            obj, cls, runtime, parent, ancestor = int(m[6], 16), int(m[7], 16), int(m[8]), int(m[9], 16), int(m[10], 16)
            valid, direct, failure = int(m[11]), int(m[12]), int(m[13])
            expected_slot = 1 if parent == p1 else 2 if parent == p2 else 0
            prior = (samples[-1]['tick'], samples[-1]['ordinal']) if samples else (0, 0)
            if (any(value > 0xffffffffffffffff for value in (gen, ordinal, obj, cls, runtime, parent, ancestor)) or
                s != site or index != len(samples) or not tick <= gen <= last_tick or
                ordinal not in (gen % 8, 8 + (gen // 8) % 8) or (gen, ordinal) <= prior or
                phase > 3 or valid not in (0, 1) or direct > 2 or failure > 6 or (site == 0 and ancestor) or
                (not valid and (not failure or direct)) or
                (valid and (failure or not obj or not runtime or not parent or obj == parent or
                 cls != (0x3c6a440 if site == 0 else 0x3ccad80) or direct != expected_slot))):
                errors.append('sample bounds, identity, cadence or order mismatch')
            samples.append({'tick': gen, 'ordinal': ordinal, 'phase': phase, 'object': obj,
                            'class': cls, 'runtime': runtime, 'parent': parent, 'ancestor': ancestor,
                            'valid': valid, 'slot': direct, 'failure': failure})
            cursor += 1
        if cursor >= len(rows): errors.append('missing site'); return out
        m = re.fullmatch(r'X4 COMPONENT_TRACE v=1 stage=site site='+N+' calls='+N+' sampled='+N+
            ' missed='+N+' capped='+N+' foreign_lower='+N+' rows='+N+r'\n', rows[cursor])
        if not m: errors.append('invalid site summary'); return out
        s, calls, sampled, missed, capped, foreign, count = map(int, m.groups())
        if (any(value > 0xffffffffffffffff for value in (calls, sampled, missed, capped, foreign)) or
                s != site or count != len(samples) or sampled != count or count > 256 or sampled > calls):
            errors.append('site counts mismatch')
        sites.append({'calls': calls, 'sampled': sampled, 'missed': missed, 'capped': capped,
                      'foreign_lower': foreign, 'samples': samples})
        cursor += 1
    if cursor != len(rows) - 1 or rows[cursor] != (f'X4 COMPONENT_TRACE v=1 stage=close tick={last_tick} '
                                                  f'tid={tid} sampling=bounded foreign=lower_bound\n'):
        errors.append('missing, duplicate or invalid close')
    out.update(bind_tick=tick, tid=tid, sites=sites, raw_receipts=rows)
    out['pass'] = not errors
    return out


def collect_component_trace(stderr, ancestry, physics, last_tick, enabled):
    try:
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        result = parse_component_trace(raw.decode('utf-8').splitlines(keepends=True), ancestry, last_tick, enabled)
        result['stderr_sha256'] = digest
        if (enabled or physics is not None) and (not isinstance(physics, dict) or
                physics.get('pass') is not True or physics.get('stderr_sha256') != digest):
            result['errors'].append('stderr differs from accepted physics evidence')
        if enabled and (not isinstance(ancestry, dict) or ancestry.get('pass') is not True or
                        ancestry.get('stderr_sha256') != digest):
            result['errors'].append('stderr differs from accepted ancestry evidence')
        result['pass'] = not result['errors']
        return result
    except (OSError, UnicodeError):
        return {'pass': False, 'errors': ['stderr unreadable'], 'ownership_exhaustive': False}
