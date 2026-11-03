"""Where the MoveWorker's frame goes: native_gate_sample_profile rows of a gated run.

The gate's sampler (src/native/engine/lockstep_sample_profile.h) suspends the
simulation thread every N ms, walks its stack and counts, per engine function in its
mark table, the samples with that function on the stack (inclusive), and the innermost
X4 code of every sample. This tool reads the rows the probe logs at stop
(SAMPLE_PROFILE_STOP, SAMPLE_PROFILE_ROW) from each clientK/engine-output.log of a run
(or from log files given directly) and prints, per client:

  - the sample counts (walked, leaf classes, mean pause per sample);
  - every mark's share of all samples and of the step samples (taken inside the
    simulation window, see the header), and an estimate in ms per step: the step
    share times the median FRAME_TIMING sim_us of the same log;
  - the innermost X4 functions / call sites, optionally named from a private
    rva,name CSV (--names; names stay out of the repository).

--against RUN prints the per-mark difference in ms per step against a second run, the
form of the attention study's A/B runs (reports/LOCKSTEP-ATTENTION-STUDY-20261101.txt,
section 8): e.g. two players in two sectors with and without native_gate_view_points_mask.
Sampling estimates carry a binomial error of about sqrt(p(1-p)/n) of the share.
"""
import argparse
import bisect
import csv
import json
import math
import re
import statistics
from pathlib import Path

STOP = re.compile(r'SAMPLE_PROFILE_STOP (.*)$')
ROW = re.compile(r'SAMPLE_PROFILE_ROW index=(\d+) kind=(mark|inner) (.*)$')
FIELD = re.compile(r'(\w+)=(\S+)')
SIM = re.compile(r'FRAME_TIMING tick=(\d+) .*?frame_us=(\d+) outside_us=(\d+).*?\bsim_us=(\d+)')


def fields(text):
    return {k: v for k, v in FIELD.findall(text)}


def parse(lines):
    """The last stop's status and rows, and the FRAME_TIMING medians, of one client log."""
    status, rows, sims, frames = None, [], [], []
    for line in lines:
        if 'SAMPLE_PROFILE_STOP ' in line:
            m = STOP.search(line.rstrip())
            if m: status, rows = fields(m[1]), []
            continue
        if 'SAMPLE_PROFILE_ROW ' in line:
            m = ROW.search(line.rstrip())
            if m:
                row = fields(m[3]);row.update(index=int(m[1]), kind=m[2]);rows.append(row)
            continue
        if 'FRAME_TIMING tick=' in line:
            m = SIM.search(line)
            if m and int(m[1]) > 2 and int(m[4]) > 0:
                sims.append(int(m[4]));frames.append(int(m[2])+int(m[3]))
    return dict(status=status, rows=rows, sim_us=statistics.median(sims) if sims else None,
                frame_us=statistics.median(frames) if frames else None, timing_ticks=len(sims))


def summary(parsed):
    """Shares and per-step estimates of one client."""
    s = parsed['status']
    if not s: return None
    num = lambda k: int(s.get(k, 0))
    samples, step, walked = num('samples'), num('step'), num('walked')
    leaf = [int(v) for v in s.get('leaf', '0/0/0/0').split('/')]
    sim = parsed['sim_us']
    out = dict(samples=samples, step=step, walked=walked, leaf=dict(zip(('x4', 'gate', 'other', 'unknown'), leaf)),
               truncated=num('truncated'), faults=num('faults'), failed=num('failed'), dropped=num('dropped'),
               pause_mean_us=round(num('pause_us')/samples, 1) if samples else None, pause_max_us=num('pause_max_us'),
               sim_ms=round(sim/1000, 2) if sim else None, timing_ticks=parsed['timing_ticks'], marks={}, inner=[])
    for row in parsed['rows']:
        if row['kind'] == 'mark':
            count, in_step = int(row['count']), int(row['step'])
            share = count/samples if samples else 0.0
            step_share = in_step/step if step else None
            out['marks'][row['label']] = dict(
                rva=row['rva'], count=count, share=round(share, 4),
                step_share=round(step_share, 4) if step_share is not None else None,
                err=round(math.sqrt(max(0.0, share*(1-share))/samples), 4) if samples else None,
                ms=round(step_share*sim/1000, 2) if step_share is not None and sim else None)
        else:
            out['inner'].append(dict(rva=row['rva'], site=row['site'] == '1', count=int(row['count']), step=int(row['step'])))
    return out


def names_table(path):
    """rva,name CSV (for example the private RE workspace's symbols.csv): sorted starts and names."""
    starts, names = [], []
    with open(path, encoding='utf-8', errors='replace', newline='') as f:
        for r in csv.DictReader(f):
            try: rva = int(r['rva'], 16)
            except (KeyError, ValueError): continue
            starts.append(rva);names.append(r.get('name', ''))
    order = sorted(range(len(starts)), key=lambda i: starts[i])
    return [starts[i] for i in order], [names[i] for i in order]


def name_of(table, rva):
    if not table: return ''
    starts, names = table
    i = bisect.bisect_right(starts, rva)-1
    if i < 0: return ''
    return names[i] if starts[i] == rva else f'{names[i]}+{rva-starts[i]:#x}'


def logs_of(path):
    path = Path(path)
    if path.is_file(): return {path.name: path}
    found = {}
    for client in sorted(path.iterdir(), key=lambda p: p.name):
        log = client/'engine-output.log'
        if client.is_dir() and re.fullmatch(r'client[1-9][0-9]*', client.name) and log.is_file(): found[client.name] = log
    return found


def analyse(path):
    result = {}
    for client, log in logs_of(path).items():
        with log.open(encoding='utf-8', errors='replace') as lines:
            s = summary(parse(lines))
        if s: result[client] = s
    return result


def compare(a, b):
    """Per client and mark: ms per step in a, in b, and a-b."""
    out = {}
    for client in sorted(set(a) & set(b)):
        marks = {}
        for label in a[client]['marks']:
            x, y = a[client]['marks'][label].get('ms'), b[client]['marks'].get(label, {}).get('ms')
            marks[label] = dict(a=x, b=y, delta=round(x-y, 2) if x is not None and y is not None else None)
        sa, sb = a[client]['sim_ms'], b[client]['sim_ms']
        out[client] = dict(sim_ms_a=sa, sim_ms_b=sb, sim_delta=round(sa-sb, 2) if sa and sb else None, marks=marks)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', type=Path, help='run directory with clientK/engine-output.log, or one log file')
    p.add_argument('--against', type=Path, help='second run: per-mark ms per step, run minus against')
    p.add_argument('--names', type=Path, help='private rva,name CSV for the inner rows')
    p.add_argument('--top', type=int, default=20)
    p.add_argument('--json', action='store_true')
    args = p.parse_args()
    a = analyse(args.run)
    if args.against:
        result = compare(a, analyse(args.against))
        if args.json: print(json.dumps(result, indent=1));return
        for client, c in result.items():
            print(f"{client}: sim {c['sim_ms_a']} vs {c['sim_ms_b']} ms (delta {c['sim_delta']})")
            for label, m in sorted(c['marks'].items(), key=lambda kv: -(kv[1]['delta'] or 0)):
                print(f"  {label:18s} {m['a']!s:>7} {m['b']!s:>7} {m['delta']!s:>7}")
        return
    if args.json: print(json.dumps(a, indent=1));return
    table = names_table(args.names) if args.names else None
    for client, s in a.items():
        print(f"{client}: samples={s['samples']} step={s['step']} walked={s['walked']} leaf={s['leaf']} "
              f"pause_mean_us={s['pause_mean_us']} sim_ms={s['sim_ms']} (median of {s['timing_ticks']} ticks)")
        for label, m in sorted(s['marks'].items(), key=lambda kv: -kv[1]['count']):
            print(f"  {label:18s} share={m['share']:.3f}+-{m['err']} step_share={m['step_share']} ms/step={m['ms']}")
        for row in s['inner'][:args.top]:
            rva = int(row['rva'], 16)
            print(f"  {'site' if row['site'] else 'func'} {rva:#x} {row['count']:6d} {row['step']:6d} {name_of(table, rva)}")


if __name__ == '__main__': main()
