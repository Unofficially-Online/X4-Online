"""Bounded pre-comparison evidence; never infer ownership from missing samples."""
import hashlib
from pathlib import Path
import re

SOURCES = ('tools/dev/linux_view_precompare_bundle.py',)
HELPERS = ('linux_view_precompare_bundle.py',)
ENVIRONMENT = 'X4_DEV_VIEW_PRECOMPARE_TRACE'
_U = r'(0|[1-9][0-9]{0,19})'
_H = r'(0|[1-9a-f][0-9a-f]{0,15})'
_SAMPLE = re.compile(
    r'X4 PRECOMPARE v=1 stage=sample index=' + _U + ' tick=' + _U + ' ordinal=' + _U +
    ' phase=' + _U + ' object=' + _H + ' class=' + _H + ' runtime=' + _U +
    ' parent=' + _H + ' ancestor=' + _H + ' result=' + _H + ' valid=' + _U +
    ' slot=' + _U + ' failure=' + _U + r'\n')
_SITE = re.compile(r'X4 PRECOMPARE v=1 stage=site calls=' + _U + ' sampled=' + _U +
                   ' missed=' + _U + ' capped=' + _U + ' foreign_lower=' + _U +
                   ' rows=' + _U + r'\n')


def enabled(options, component_enabled, ancestry_enabled, network):
    active = options.get('view_precompare_trace', False)
    if type(active) is not bool:
        raise ValueError('precompare activation must be boolean')
    if active and (component_enabled is not True or ancestry_enabled is not True or network is not True):
        raise ValueError('precompare requires component trace, ancestry and network physics')
    return active


def environment(env, active):
    if type(active) is not bool:
        raise ValueError('precompare activation must be boolean')
    result = dict(env)
    result.pop(ENVIRONMENT, None)
    if active:
        result[ENVIRONMENT] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('precompare source closure incomplete')
    return paths


def parse(lines, ancestry, component, last_tick, active):
    out = {'pass': False, 'errors': [], 'ownership_exhaustive': False,
           'foreign_calls': 'lower_bound', 'raw_receipts': []}
    errors = out['errors']
    rows = [line for line in lines if 'X4 PRECOMPARE' in line]
    out['raw_receipts'] = rows
    if type(active) is not bool:
        errors.append('invalid precompare activation'); return out
    if not active:
        if rows:
            errors.append('unexpected disabled precompare receipt')
        out['pass'] = not errors
        return out
    if (not isinstance(ancestry, dict) or ancestry.get('pass') is not True or
        not isinstance(component, dict) or component.get('pass') is not True or not rows):
        errors.append('missing accepted ancestry, component trace or precompare receipt'); return out
    bind = ancestry.get('bind')
    tick, tid = ancestry.get('bind_tick'), ancestry.get('tid')
    if (not isinstance(bind, dict) or type(tick) is not int or type(tid) is not int or
        type(last_tick) is not int or tick < 1 or last_tick < tick or
        type(bind.get('p1')) is not int or type(bind.get('p2')) is not int or
        bind['p1'] <= 0 or bind['p2'] <= 0 or bind['p1'] == bind['p2'] or
        component.get('bind_tick') != tick or component.get('tid') != tid):
        errors.append('invalid upstream binding'); return out
    close = (f'X4 COMPONENT_TRACE v=1 stage=close tick={last_tick} '
             f'tid={tid} sampling=bounded foreign=lower_bound\n')
    if lines.count(close) != 1 or lines.index(close) >= lines.index(rows[0]):
        errors.append('precompare receipts outside component closure')
    samples = []
    for line in rows[:-1]:
        match = _SAMPLE.fullmatch(line)
        if not match:
            errors.append('invalid precompare sample grammar'); continue
        v = match.groups()
        index, gen, ordinal, phase = map(int, v[:4])
        obj, cls, runtime, parent, ancestor, result = (int(v[4], 16), int(v[5], 16), int(v[6]),
                                                     int(v[7], 16), int(v[8], 16), int(v[9], 16))
        valid, slot, failure = map(int, v[10:])
        previous = (samples[-1]['tick'], samples[-1]['ordinal']) if samples else (0, 0)
        expected_slot = 1 if parent == bind['p1'] else 2 if parent == bind['p2'] else 0
        if (any(value > 0xffffffffffffffff for value in (index, gen, ordinal, phase, obj, cls, runtime,
                                                           parent, ancestor, result, valid, slot, failure)) or
            index != len(samples) or not tick <= gen <= last_tick or
            ordinal not in (gen % 8, 8 + (gen // 8) % 8) or (gen, ordinal) <= previous or
            phase > 3 or valid not in (0, 1) or slot > 2 or failure > 6 or
            (not valid and (not failure or slot)) or
            (valid and (failure or not obj or not runtime or not parent or obj == parent or
                        cls != 0x3ccad80 or slot != expected_slot))):
            errors.append('precompare sample bounds, identity, cadence or order mismatch')
        samples.append({'tick': gen, 'ordinal': ordinal, 'phase': phase, 'object': obj,
                        'class': cls, 'runtime': runtime, 'parent': parent, 'ancestor': ancestor,
                        'result': result, 'valid': valid, 'slot': slot, 'failure': failure})
    match = _SITE.fullmatch(rows[-1])
    if not match:
        errors.append('missing or invalid precompare site summary'); return out
    calls, sampled, missed, capped, foreign, count = map(int, match.groups())
    if (any(value > 0xffffffffffffffff for value in (calls, sampled, missed, capped, foreign, count)) or
        count != len(samples) or sampled != count or count > 256 or sampled > calls):
        errors.append('precompare counts mismatch')
    out.update(calls=calls, sampled=sampled, missed=missed, capped=capped,
               foreign_lower=foreign, samples=samples,
               observation=('zero_calls' if not calls else 'no_samples' if not sampled else 'samples'),
               valid_samples=sum(sample['valid'] for sample in samples),
               failed_samples=sum(not sample['valid'] for sample in samples))
    out['pass'] = not errors
    return out


def collect(stderr, ancestry, component, physics, last_tick, active):
    try:
        raw = Path(stderr).read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        out = parse(raw.decode('utf-8').splitlines(keepends=True), ancestry, component, last_tick, active)
        out['stderr_sha256'] = digest
        if (active or physics is not None) and (not isinstance(physics, dict) or
                physics.get('pass') is not True or physics.get('stderr_sha256') != digest):
            out['errors'].append('stderr differs from accepted physics evidence')
        if active:
            for name, evidence in (('ancestry', ancestry), ('component', component)):
                if (not isinstance(evidence, dict) or evidence.get('pass') is not True or
                        evidence.get('stderr_sha256') != digest):
                    out['errors'].append('stderr differs from accepted ' + name + ' evidence')
        out['pass'] = not out['errors']
        return out
    except (OSError, UnicodeError):
        return {'pass': False, 'errors': ['stderr unreadable'], 'ownership_exhaustive': False}
