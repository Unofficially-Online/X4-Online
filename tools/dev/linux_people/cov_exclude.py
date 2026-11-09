"""Drop listed functions that contain any address a per-tick-checked module pins (its header's code constants and
hash ranges), plus the world/travel pins; report how many open minimal-path targets survive."""
import re, glob, os, sys, bisect, pickle
S = os.environ.get('LINUX_PAIR_WORK', '.local/linux-pairs')
os.chdir(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
os.environ.setdefault('LINUX_PAIR_WORK', S)
os.environ.setdefault('X4_WINDOWS_EXE', os.path.join('..', 'x4-re', 'bin', 'X4.exe'))
sys.path.insert(0, 'tools/dev'); sys.path.insert(0, 'tools/dev/linux_people')
import coverage
gate = open('src/native/engine/lockstep_gate.c', errors='replace').read()
checks = sorted(set(re.findall(r'if\([a-z_]*_installed && !([a-z_]+_check)\(\)\)', gate)))
files = set(glob.glob('src/native/engine/lockstep_world_*.h')) | {'src/native/engine/lockstep_request.h', 'src/native/engine/lockstep_travel_snapshot.h'}
for c in checks:
    for f in glob.glob('src/native/engine/lockstep_*.h') + ['src/native/engine/lockstep_gate.c']:
        if os.path.basename(f).startswith('test_'):
            continue
        if re.search(r'static int %s\(void\)' % c, open(f, errors='replace').read()):
            files.add(f)
pts, rng = set(), []
for f in files:
    src = open(f, errors='replace').read()
    code = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    code = re.sub(r'"[0-9a-f]{64}"', '', code)
    for m in re.finditer(r'\b0x([0-9a-f]{5,7})u?\b', code):
        v = int(m.group(1), 16)
        if v >= 0x40000 and coverage.sec(v) == '.text':
            pts.add(v)
    for m in re.finditer(r'0x([0-9a-f]{5,8})u?\s*,\s*(\d+|0x[0-9a-f]+)u?\s*,\s*"[0-9a-f]{64}"', src):
        a = int(m.group(1), 16); rng.append((a - 16, a + int(m.group(2), 0)))
    for m in re.finditer(r'(?:hash_ok\(\s*image\s*\+\s*(?:\(?\w*\)?)?|tsnap_unpatched_hash\(\s*)0x([0-9a-f]+)u?\s*,\s*(\d+|0x[0-9a-f]+)', src):
        a = int(m.group(1), 16); rng.append((a - 16, a + int(m.group(2), 0)))
for a in (0xea4c00, 0xe85360):
    rng.append((a - 16, a + 8))
wst = sorted(coverage.wg)
wsize = dict(zip(wst, [b - a for a, b in zip(wst, wst[1:] + [wst[-1] + 1])]))
lst = [int(x, 16) for x in open(os.path.join(S, 'coverage-windows.txt')).read().split()]
def bad(f):
    end = f + wsize.get(f, 16)
    if any(f <= p < end for p in pts):
        return True
    return any(lo <= f < hi for lo, hi in rng)
keep = [f for f in lst if not bad(f)]
opens = set()
for m in ['pilot_inputs', 'pilot_native', 'pilot_stock', 'pilot_slots', 'pilot_present', 'pilot_stock_view', 'pilot_camera', 'pilot_eye']:
    opens |= set(coverage.classify(m)['fn_open'])
print('checked modules', len(checks), 'files', len(files), 'points', len(pts), 'ranges', len(rng))
print('list', len(lst), 'kept', len(keep), 'open targets kept', len(opens & set(keep)), 'of', len(opens))
open(os.path.join(S, 'coverage-windows.txt'), 'w').write('\n'.join('%x' % a for a in keep) + '\n')
