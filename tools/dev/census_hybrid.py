"""Hybrid sectors (native_gate_hybrid, slice H1): what a run says about the boundary, the cost and the bandwidth.

reports/LOCKSTEP-HYBRID-SECTORS-20261101.txt section 9 is the live-test spec this reads. For every clientK of a run:
  - the gate's own record of the split: HYBRID_ARMED, the last HYBRID status (members, movers, records, bytes, proxy
    error, crossings, failures/why) and every HYBRID_CENSUS summary (outside/inside counts, the outside state);
  - the census filter, recomputed from the WORLD_OBJECT rows: an object is inside when its parent chain reaches an
    owned sector's saved ID (an absent object: when it was inside at the previous capture); the outside roots of the
    recomputed classification must equal the gate's (classification= ok / differs);
  - client1 against every other client, capture by capture: outside objects whose hashes differ (per domain, the
    first few IDs with their class RVA), objects inside on one client and outside on the other (membership), and
    the outside roots equal or not;
  - FRAME_TIMING sim_us medians before the hybrid tick and from it on (captures and the first ticks skipped), the
    owner's and the others' difference being the price of the bubble each client no longer simulates;
  - bandwidth: the record bytes per proposal (mean, max) and the full-state estimate (every mover every step)
    in kbit/s at the run's native_gate_tick_hz.

  python tools/dev/census_hybrid.py RUN [RUN ...] [--settle 300] [--samples 8] [--out result.json]

Read only. A two-client run reports one pair; N clients report client1 against each other client.
"""
import argparse
import hashlib
import json
import re
import statistics
import struct
from pathlib import Path

from census_clients import client_slots
from compare_native_world import bounded_lines
from native_world_evidence import DOMAINS, configured_schema, configured_watches, frames

STATUS = re.compile(r'NATIVE_GATE HYBRID tick=(\d+) (.*)$')
STOP = re.compile(r'NATIVE_GATE HYBRID_STOP (.*)$')
ARMED = re.compile(r'NATIVE_GATE HYBRID_ARMED owners=([0-9,]+) tick=(\d+) poses=(\d+)')
CENSUS = re.compile(r'NATIVE_GATE HYBRID_CENSUS tick=(\d+) outside=(\d+) inside=(\d+) absent_inside=(\d+) members=(\d+) movers=(\d+) '
                    r'entered=(\d+) left=(\d+) inside_root=([0-9a-f]{64}) state=(HYBRID_OUTSIDE_1:(\d+):(\d+):'
                    r'([0-9a-f]{64}):([0-9a-f]{64}):([0-9a-f]{64}):([0-9a-f]{64}))')
FRAME = re.compile(r'FRAME_TIMING tick=(\d+) .*?\bsim_us=(\d+)')
FIELD = re.compile(r'(\w+)=(\S+)')


def config_value(config, name, default=None):
    m = re.search(r'X4LAN_CONFIG\.' + name + r'\s*=\s*([^\n;]+)', config)
    return m[1].split('--')[0].strip() if m else default


def status_fields(text):
    return {k: v for k, v in FIELD.findall(text)}


def owned_sectors(status):
    """{slot: saved sector ID} from a status' sectors=k:hex,... field."""
    value = status.get('sectors', '-')
    if value == '-':
        return {}
    return {int(k): int(v, 16) for k, v in (part.split(':') for part in value.split(','))}


def membership(objects, sectors, previous):
    """The IDs inside an owned sector: live objects by their parent chain, absent ones as at the previous capture."""
    inside = set()
    for identity, row in objects.items():
        if not row['flags'] & 1:
            if identity in previous:
                inside.add(identity)
            continue
        node, depth = identity, 0
        while node and depth < 18:
            if node in sectors:
                inside.add(identity)
                break
            parent = objects.get(node)
            node = parent['parent'] if parent else 0
            depth += 1
    return inside


def outside_roots(objects, inside):
    hashes = [hashlib.sha256() for _ in DOMAINS]
    for identity, row in sorted(objects.items()):
        if identity in inside:
            continue
        key = struct.pack('<Q', identity)
        for digest, value in zip(hashes, row['hashes']):
            digest.update(key + value)
    return [digest.hexdigest() for digest in hashes]


def median_ms(values):
    return round(statistics.median(values) / 1000, 2) if values else None


def client_report(run, slot, settle):
    client = run / f'client{slot}'
    config = (client / 'installed/x4_lan_lab-config.lua').read_text()
    hz = int(config_value(config, 'native_gate_tick_hz', '60') or 60)
    stride = int(config_value(config, 'native_gate_world_hash_stride', '0') or 0)
    armed, status, stop, censuses, sims = None, None, None, {}, []
    with (client / 'engine-output.log').open(encoding='utf-8', errors='replace') as lines:
        for line in lines:
            if 'NATIVE_GATE HYBRID' in line:
                if (m := ARMED.search(line)):
                    armed = dict(owners=[int(k) for k in m[1].split(',')], tick=int(m[2]), poses=int(m[3]))
                elif (m := CENSUS.search(line)):
                    censuses[int(m[1])] = dict(outside=int(m[2]), inside=int(m[3]), absent_inside=int(m[4]), members=int(m[5]),
                                               movers=int(m[6]), entered=int(m[7]), left=int(m[8]), inside_root=m[9],
                                               roots=[m[13], m[14], m[15], m[16]])
                elif (m := STOP.search(line)):
                    stop = status_fields(m[1])
                elif (m := STATUS.search(line)):
                    status = dict(status_fields(m[2]), tick=int(m[1]))
            elif 'FRAME_TIMING tick=' in line and (m := FRAME.search(line)):
                sims.append((int(m[1]), int(m[2])))
    last = stop or status or {}
    start = armed['tick'] if armed else None
    captured = lambda tick: stride and (tick == 1 or tick % stride == 0)
    before = [us for tick, us in sims if tick > min(settle, start or settle) // 2 and start and tick < start and not captured(tick)]
    after = [us for tick, us in sims if start and tick >= start + settle and not captured(tick)]
    proposed = int(last.get('proposed', 0) or 0)
    full = int(last.get('full_bytes', 0) or 0)
    report = dict(armed=armed, tick_hz=hz, stride=stride, sim_ms_before=median_ms(before), sim_ms_after=median_ms(after),
                  frames_before=len(before), frames_after=len(after), status_tick=last.get('tick'),
                  active=last.get('active'), owner=last.get('owner'), sectors=last.get('sectors'), why=last.get('why'),
                  failures=last.get('failures'), members=last.get('members'), movers=last.get('movers'),
                  own_movers=last.get('own_movers'), entered=last.get('entered'), left=last.get('left'),
                  eval_inside=last.get('eval_inside'), eval_outside=last.get('eval_outside'), scan_us=last.get('scan_us'),
                  frame_us_max=last.get('frame_us_max'), records=last.get('records'), missing=last.get('missing'),
                  proposed=proposed, squeezed=last.get('squeezed'), bytes_mean=last.get('bytes_mean'), bytes_max=last.get('bytes_max'),
                  record_kbps=round(float(last.get('bytes_mean', 0) or 0) * 8 * hz / 1000, 1),
                  full_bytes=full, full_kbps=round(full * 8 * hz / 1000, 1),
                  proxies=last.get('proxies'), unknown=last.get('unknown'), parent_mismatch=last.get('parent_mismatch'),
                  err_mean_m=last.get('err_mean_m'), err_max_m=last.get('err_max_m'), censuses=censuses)
    return report, config, last


def captures(run, slot, config):
    errors = []
    for frame in frames(bounded_lines(run / f'client{slot}' / 'engine-output.log'), errors, slot, configured_watches(config), True,
                        configured_schema(config)):
        yield frame['tick'], frame['objects']


def compare(run, slots, reports, configs, sectors, samples):
    """client1 against every other client at every common capture from the hybrid tick on."""
    start = min((r['armed'] or {}).get('tick', 0) for r in reports.values()) if reports else 0
    streams = {slot: captures(run, slot, configs[slot]) for slot in slots}
    previous = {slot: set() for slot in slots}
    current = {slot: next(streams[slot], None) for slot in slots}
    pairs = {f'client{k}': [] for k in slots[1:]}
    classification = {f'client{k}': 'ok' for k in slots}
    while all(current[slot] is not None for slot in slots):
        ticks = {current[slot][0] for slot in slots}
        if len(ticks) > 1:  # one client captured a tick another did not: advance the lagging ones
            low = min(ticks)
            for slot in slots:
                if current[slot][0] == low:
                    current[slot] = next(streams[slot], None)
            continue
        tick = ticks.pop()
        objects = {slot: current[slot][1] for slot in slots}
        inside = {slot: membership(objects[slot], sectors, previous[slot]) if sectors else set() for slot in slots}
        for slot in slots:
            gate = reports[slot]['censuses'].get(tick)
            if gate and tick >= start and outside_roots(objects[slot], inside[slot]) != gate['roots']:
                classification[f'client{slot}'] = f'differs at {tick}'
        if tick >= start:
            a = slots[0]
            for b in slots[1:]:
                union = inside[a] | inside[b]
                ids = (set(objects[a]) | set(objects[b])) - union
                differ = {d: [] for d in DOMAINS}
                missing = []
                for identity in sorted(ids):
                    ra, rb = objects[a].get(identity), objects[b].get(identity)
                    if ra is None or rb is None:
                        missing.append(identity)
                        continue
                    for i, domain in enumerate(DOMAINS):
                        if ra['hashes'][i] != rb['hashes'][i]:
                            differ[domain].append(identity)
                outside_equal = outside_roots({k: v for k, v in objects[a].items() if k not in union},
                                              set()) == outside_roots({k: v for k, v in objects[b].items() if k not in union}, set())
                pairs[f'client{b}'].append(dict(
                    tick=tick, outside=len(ids), outside_equal=outside_equal, missing=len(missing),
                    differing={d: len(v) for d, v in differ.items()},
                    samples={d: [f'{i:016x}/{objects[a][i]["type"]:x}' for i in v[:samples]] for d, v in differ.items() if v},
                    inside=[len(inside[a]), len(inside[b])], membership_differs=len(inside[a] ^ inside[b]),
                    membership_samples=[f'{i:016x}' for i in sorted(inside[a] ^ inside[b])[:samples]]))
        previous = inside
        current = {slot: next(streams[slot], None) for slot in slots}
    return pairs, classification


def analyse(run, settle=300, samples=8):
    slots = client_slots(run)
    reports, configs, sectors = {}, {}, set()
    for slot in slots:
        report, config, last = client_report(run, slot, settle)
        reports[slot], configs[slot] = report, config
        sectors |= set(owned_sectors(last).values())
    pairs, classification = compare(run, slots, reports, configs, sectors, samples)
    for slot in slots:
        reports[slot]['classification'] = classification[f'client{slot}']
        reports[slot]['censuses'] = {str(t): {k: v for k, v in c.items() if k not in ('roots', 'inside_root')}
                                     for t, c in sorted(reports[slot]['censuses'].items())}
    owners = next((r['armed']['owners'] for r in reports.values() if r['armed']), [])
    others = [reports[s]['sim_ms_after'] for s in slots if s not in owners and reports[s]['sim_ms_after'] is not None]
    mine = [reports[s]['sim_ms_after'] for s in slots if s in owners and reports[s]['sim_ms_after'] is not None]
    verdict = dict(
        outside_identical=all(p['outside_equal'] for rows in pairs.values() for p in rows) if any(pairs.values()) else None,
        first_outside_difference=min((p['tick'] for rows in pairs.values() for p in rows if not p['outside_equal']), default=None),
        membership_identical=all(not p['membership_differs'] for rows in pairs.values() for p in rows) if any(pairs.values()) else None,
        classification_ok=all(v == 'ok' for v in classification.values()),
        failures={f'client{s}': reports[s]['why'] for s in slots if reports[s]['why'] not in (None, 'none')},
        owner_sim_ms=mine, other_sim_ms=others)
    return dict(run=run.name, sectors=[f'{s:016x}' for s in sorted(sectors)], verdict=verdict,
                clients={f'client{s}': reports[s] for s in slots}, pairs=pairs)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('runs', type=Path, nargs='+')
    p.add_argument('--settle', type=int, default=300, help='ticks after the hybrid tick (and half of it before) left out of sim_us')
    p.add_argument('--samples', type=int, default=8)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    results = [analyse(run, args.settle, args.samples) for run in args.runs]
    text = json.dumps(results if len(results) > 1 else results[0], indent=1)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
