"""Earliest tick at which each module's NATIVE_GATE status line differs between two clients (timing fields ignored).

  python tools/dev/census_first_status.py <x4ls run dir | X4 documents dir>
  (run dir: client1/engine-output.log and client2/engine-output.log; documents: 45850/x4ls-c1.log and 45851/x4ls-c2.log)
"""
import re, sys
from pathlib import Path
docs = Path(sys.argv[1]); logs = [docs / 'client1/engine-output.log', docs / 'client2/engine-output.log']
if not logs[0].exists():
    logs = [docs / '45850/x4ls-c1.log', docs / '45851/x4ls-c2.log']
LOCAL = {'VIEW_POINTS', 'REGION_CAMERA', 'MASS_TRAFFIC', 'RNG', 'FRAME_TIMING', 'PROBE_PROFILE', 'RENDER_RNG_SPLIT',
         'PILOT_INPUTS', 'PLAYER_TIMER', 'BULLET_CADENCE', 'CREATED_IDENTITIES', 'RUNTIME_IDS'}
TIMING = re.compile(r'\b\w*(_us|_ns|ms|wait|busy|time|max_\w+)=\S+')
def rows(path):
    out = {}
    for line in open(path, errors='replace'):
        m = re.search(r'NATIVE_GATE ([A-Z_0-9]+) tick=(\d+) (.*)', line)
        if m and m.group(1) not in LOCAL:
            out[(m.group(1), int(m.group(2)))] = TIMING.sub('', m.group(3))
    return out
a, b = rows(logs[0]), rows(logs[1])
first = {}
for (mod, tick) in sorted(set(a) & set(b), key=lambda k: k[1]):
    if a[(mod, tick)] != b[(mod, tick)] and mod not in first:
        x, y = a[(mod, tick)].split(), b[(mod, tick)].split()
        first[mod] = (tick, [(u, v) for u, v in zip(x, y) if u != v][:2])
for mod, (tick, d) in sorted(first.items(), key=lambda kv: kv[1][0])[:14]:
    print(tick, mod, d)
