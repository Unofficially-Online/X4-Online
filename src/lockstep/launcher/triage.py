"""x4ls triage RUN: rerun a diverged session with diagnostic windows around its first mismatch and report.

The digest folds the world hash every 300 ticks, so the first digest mismatch F only says that the
world parted somewhere in (F - 300, F]. Triage rebuilds the run's overlay (tools/build_soak_overlay.py
with the arguments it recorded beside the overlay) with, for those ticks:

  - native_gate_trace_window: every module's status line on every tick, not every 60th;
  - native_gate_world_pages_window: per-object world pages at F - 300 and F (census_world.py);
  - row windows (at most 600 ticks each): script RNG scopes, every due-event delivery, formation
    transforms with return chains, UI lazy calls; created-identity rows; event draw rows;

runs it to F + 60 from the same snapshot and writes RUN-triage.txt (also printed): the earliest
differing status line per module, the first tick at which each row family differs (rows compared
as per-tick multisets, process-local fields removed) and the world census roots.

A divergence that depends on thread timing can come out at another tick or not at all in the
rerun; the report says so instead of guessing.
"""
import collections
import json
from pathlib import Path
import re
import subprocess
import sys

from . import session

ROOT = Path(__file__).resolve().parents[3]
DEV = ROOT / 'tools/dev'
STRIDE = 300

# Row families: (prefix, fields that are process-local and must not count)
FAMILIES = [
    ('EVENT_RNG_ROW', ('index', 'entry')),
    ('SCRIPT_RNG_ROW', ('index', 'caller')),
    ('CREATED_ROW', ('index',)),
    ('PATH_TRANSFORMS_ROW', ('index', 'time')),
    ('UI_LAZY_ROW', ('index', 'chain')),
    ('DRAW_WATCH_ROW', ('index',)),
]
TICK = re.compile(r'\btick=(\d+)')
# Status fields that differ between clients in sync (process-local bookkeeping, see docs/LAUNCHER.md):
# listed apart so that the simulation differences stand out. A module whose differing fields are all
# here is "local"; any other field makes the line a lead.
KNOWN_LOCAL = {
    'PATH_DETERMINISM': {'bank', 'reuses', 'rebuilds', 'lookups', 'id_hits'},  # walk cache statistics 'WORLD_BANKS': {'read_before', 'write_before', 'read_after', 'write_after'},
    'SHAPE_ORDER': {'appends', 'mass_forced', 'boot_forced'}, 'REGION_PASS': {'word'},
    'AI_SCHEDULE': {'order_moved', 'priority'}, 'EVENT_ORDER': {'moved'}, 'NAV_OCTREE': {'invalidations'},
    'GRAVIDAR_WIDE': {'sectors', 'moved'}, 'SCRIPT_RNG': {'ai_hits', 'ai_misses'}, 'FIND_ORDER': {'reordered'},
    'WARP_RNG': {'attributed'}, 'ACTOR': {'raw'},
    'EVENT_RNG': {'calls', 'keyed', 'local', 'keyset', 'drawset'},  # the local player's UI listeners (no draws)
}
STATUS = re.compile(r'^(\d+) ([A-Z_0-9]+) (\[.*\])$')


def classify(status_line):
    """'lead' or 'local' for a census_first_status.py line."""
    m = STATUS.match(status_line.strip())
    if not m:
        return 'lead'
    fields = set(re.findall(r"'(\w+)=", m.group(3)))
    return 'local' if fields and fields <= KNOWN_LOCAL.get(m.group(2), set()) else 'lead'


def window(first, stride=STRIDE):
    """The ticks to instrument: after the last equal capture up to the first differing one."""
    return max(1, first - stride + 1), first


def diagnostic_sets(first, stride=STRIDE):
    lo, hi = window(first, stride)
    sets = [f'native_gate_trace_window={{{lo},{hi}}}',
            f'native_gate_world_pages_window={{{max(1, first - stride)},{first}}}',
            'native_gate_script_rng_rows=true', f'native_gate_script_rng_rows_window={{{lo},{hi}}}',
            f'native_gate_event_rng_rows={{{lo},{hi}}}', 'native_gate_event_rng_play_rows=true',
            f'native_gate_path_transforms_rows={{{lo},{hi}}}', f'native_gate_ui_lazy_rows={{{lo},{hi}}}',
            'native_gate_created_rows=true']
    return sets


def rebuild_args(overlay, out, first):
    """The recorded build arguments with the diagnostic keys and a new --out."""
    recorded = Path(f'{overlay}.build.json')
    if not recorded.is_file():
        raise SystemExit(f'{recorded} missing: rebuild {overlay} with tools/build_soak_overlay.py first')
    argv = json.loads(recorded.read_text())['argv']
    extra = [x for s in diagnostic_sets(first) for x in ('--set', s)]
    return [*argv, *extra, '--out', str(out)]


def strip(line, fields):
    for f in fields:
        line = re.sub(rf' ?\b{f}=\S+', '', line)
    return line.strip()


def rows(path, prefix, fields):
    """{tick: Counter(row)} of one row family in a client log."""
    out = collections.defaultdict(collections.Counter)
    marker = f'NATIVE_GATE {prefix} '
    with open(path, errors='replace') as f:
        for line in f:
            at = line.find(marker)
            if at < 0:
                continue
            text = strip(line[at + len(marker):].rstrip('\r\n'), fields)
            m = TICK.search(text)
            if m:
                out[int(m.group(1))][text] += 1
    return out


def first_row_difference(a, b):
    """(tick, rows only in A, rows only in B) at the first tick where the multisets differ, else None."""
    for tick in sorted(set(a) | set(b)):
        if a.get(tick) != b.get(tick):
            left = a.get(tick, collections.Counter())
            right = b.get(tick, collections.Counter())
            return tick, sorted((left - right).elements()), sorted((right - left).elements())
    return None


def tool(script, *args):
    result = subprocess.run([sys.executable, str(DEV / script), *map(str, args)], capture_output=True, text=True,
                            errors='replace')
    return result.stdout


def report(run_dir, first):
    """The triage report of a (rerun) session directory."""
    run_dir = Path(run_dir)
    logs = [run_dir / 'client1/engine-output.log', run_dir / 'client2/engine-output.log']
    lo, hi = window(first)
    lines = [f'triage of {run_dir.name}: first digest mismatch of the original run at {first}; window {lo}..{hi}']
    summary = json.loads((run_dir / 'summary.json').read_text())
    lines.append(f"rerun: {summary.get('outcome')}, first digest mismatch {summary.get('first_digest_mismatch')}")
    lines.append('')
    status = tool('census_first_status.py', run_dir).splitlines()
    lines.append('earliest differing status line per module (tools/dev/census_first_status.py), leads:')
    lines += ['  ' + l[:240] for l in status if classify(l) == 'lead'] or ['  none']
    local = [l.split(' [')[0] for l in status if classify(l) == 'local']
    lines.append(f"known local differences (bookkeeping, not simulation): {', '.join(local) or 'none'}")
    lines.append('')
    lines.append('row families (per-tick multisets, process-local fields removed):')
    for prefix, fields in FAMILIES:
        a, b = rows(logs[0], prefix, fields), rows(logs[1], prefix, fields)
        if not a and not b:
            continue
        diff = first_row_difference(a, b)
        if not diff:
            lines.append(f'  {prefix}: equal ({sum(sum(c.values()) for c in a.values())} rows)')
            continue
        tick, left, right = diff
        lines.append(f'  {prefix}: first differs at tick {tick} ({len(left)} only in client 1, {len(right)} only in client 2)')
        lines += [f'    1: {r[:220]}' for r in left[:5]] + [f'    2: {r[:220]}' for r in right[:5]]
    lines.append('')
    census = run_dir / 'census-world.json'
    tool('census_world.py', run_dir, '--out', census, '--limit', 20)
    if census.is_file():
        world = json.loads(census.read_text()).get('world', {})
        lines.append(f"world census: {world.get('diverged_objects', 0)} diverged objects")
        for root in world.get('roots', [])[:10]:
            seeds = ', '.join(f"{s['saved_id']} {s.get('type', '')} {'/'.join(s.get('domains', []))}" for s in root['seeds'][:4])
            lines.append(f"  first {root['first_tick']} parent {root['parent']} ({root.get('parent_origin')}): {seeds}")
    return '\n'.join(lines) + '\n'


def triage(run_dir, steam_fix=False, log=print):
    run_dir = Path(run_dir)
    summary = json.loads((run_dir / 'summary.json').read_text())
    first = summary.get('first_digest_mismatch')
    if not first:
        raise SystemExit(f'{run_dir}: no digest mismatch to triage ({summary.get("outcome")})')
    if not summary.get('overlay'):
        raise SystemExit(f'{run_dir}: the summary does not name its overlay (a run from before triage)')
    overlay = Path(summary['overlay'])
    out = ROOT / '.local/overlays' / f'triage-{run_dir.name}'
    if not out.exists():
        log(f'building {out.name}')
        built = subprocess.run([sys.executable, str(ROOT / 'tools/build_soak_overlay.py'), *rebuild_args(overlay, out, first)],
                               capture_output=True, text=True)
        if built.returncode:
            raise SystemExit((built.stderr or built.stdout).strip().splitlines()[-1])
    snapshot = Path(summary['snapshot']) if summary.get('snapshot') else None
    result = session.run(session.Options(clients=2, overlay=out, snapshot=snapshot, ticks=first + 60, observe=True,
                                         lookahead=1, timeout=max(900, first // 10), fast=True, steam_fix=steam_fix), log=log)
    rerun = Path(result["run_dir"])
    text = report(rerun, first)
    (run_dir.parent / f'{run_dir.name}-triage.txt').write_text(text)
    log(text)
    return text
