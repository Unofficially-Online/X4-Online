import re, json, pickle, os, bisect, sys
S = os.environ.get('LINUX_PAIR_WORK', os.path.join(os.path.dirname(__file__), '..', '..', '..', '.local', 'linux-pairs'))
os.chdir(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
wg, _ = pickle.load(open(os.path.join(S, 'paircache', 'calls2-win.pickle'), 'rb'))
pairs = {int(k, 16): v for k, v in json.load(open(os.path.join(S, 'pairs-calls.json'))).items()}
sites = {int(s['win'], 16): s for s in json.load(open('tools/dev/linux_people_sites.json'))['sites']}
starts = sorted(wg)
import pefile
pe = pefile.PE(os.environ.get('X4_WINDOWS_EXE', os.path.join('..', 'x4-re', 'bin', 'X4.exe')), fast_load=True)
secs = [(s.Name.rstrip(b'\0').decode(), s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections]


def sec(r):
    for n, a, b in secs:
        if a <= r < b:
            return n


def rvas_of(mod):
    src = open('src/native/engine/lockstep_%s.h' % mod).read()
    code = re.sub(r'/\*.*?\*/', '', src, flags=re.S)          # comments name Windows addresses too: count code only
    code = re.sub(r'//[^\n]*', '', code)
    code = re.sub(r'"[0-9a-f]{64}"', '', code)                 # site hashes
    return {v for v in (int(m, 16) for m in re.findall(r'\b0x([0-9a-f]{5,7})u?\b', code)) if v >= 0x40000 and sec(v)}


def classify(mod):
    c = {'fn_paired': [], 'fn_open': [], 'in_paired_fn': [], 'in_open_fn': [], 'data': [], 'rdata': [], 'other': []}
    for r in sorted(rvas_of(mod)):
        s = sec(r)
        if s == '.text':
            if r in wg:
                c['fn_paired' if (r in pairs or (r in sites and sites[r].get('lin'))) else 'fn_open'].append(r)
            else:
                k = bisect.bisect_right(starts, r) - 1
                c['in_paired_fn' if starts[k] in pairs else 'in_open_fn'].append(r)
        elif s == '.data':
            c['data'].append(r)
        elif s == '.rdata':
            c['rdata'].append(r)
        else:
            c['other'].append(r)
    return c


if __name__ == '__main__':
    total = {}
    for mod in sys.argv[1:]:
        c = classify(mod)
        print(mod, sum(len(v) for v in c.values()), {k: len(v) for k, v in c.items()})
        for k, v in c.items():
            total[k] = total.get(k, 0) + len(v)
    print('total', sum(total.values()), total)
