"""Windows X4.exe <-> Linux X4 function pairs by call graph, strings and exports (the people-stack port's matcher).

Anchors: the RTTI vtable pairs (tools/dev/linux_pair_rtti.py output; a slot pairs when the Linux vtable has exactly one
slot more, GCC's second destructor, and the Windows function sits in one slot pair only), exported functions with the
same name (the Lua FFI API), tools/dev/lockstep_linux_known_pairs.json, and functions that alone reference the same
string on each side. Propagation: in every paired function the call sequences are aligned on the pairs already known;
a gap of the same length (1..2) between two aligned calls pairs its callees in order. Repeated until nothing changes.

  python tools/dev/linux_pair_calls.py --win X4.exe --lin X4 --rtti pairs-rtti.json --out pairs-calls.json [--cache DIR]
  python tools/dev/linux_pair_calls.py ... --query 9fc550,d8e5b0      (Windows RVAs: their pairs and evidence)

Reads both files only. Function ranges: Windows .pdata, Linux .eh_frame_hdr's table and each FDE's range."""
import argparse
import bisect
import json
import os
import pickle
import re
import struct
import sys

import capstone
import pefile
from elftools.elf.elffile import ELFFile

STRING = re.compile(rb'[\x20-\x7e]{6,200}\0')


def win_functions(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION'],
                                           pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    img = pe.get_memory_mapped_image()
    ranges = sorted({(e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION})
    exports = {}
    if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        for s in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if s.name:
                exports[s.name.decode('latin-1')] = s.address
    rdata = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.rdata')
    writable = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x80000000]
    return img, 0, ranges, exports, (rdata.VirtualAddress, rdata.VirtualAddress + rdata.Misc_VirtualSize), writable


def lin_functions(path):
    f = ELFFile(open(path, 'rb'))
    data = open(path, 'rb').read()
    hdr = f.get_section_by_name('.eh_frame_hdr')
    h0, ho = hdr['sh_addr'], hdr['sh_offset']
    version, eh_enc, cnt_enc, tab_enc = data[ho:ho + 4]
    assert version == 1 and cnt_enc == 0x03 and tab_enc == 0x3b, (version, cnt_enc, tab_enc)   # udata4 count, datarel sdata4
    count = struct.unpack_from('<I', data, ho + 8)[0]
    eh = f.get_section_by_name('.eh_frame')
    e0, eo = eh['sh_addr'], eh['sh_offset']
    ranges = []
    for k in range(count):
        loc, fde = struct.unpack_from('<ii', data, ho + 12 + 8 * k)
        start, fde_va = h0 + loc, h0 + fde
        fo = eo + fde_va - e0
        length = struct.unpack_from('<I', data, fo)[0]
        if length == 0xffffffff:
            continue
        # pc_begin (pcrel sdata4) at +8, pc_range (sdata4) at +12 for the usual 0x1b CIE encoding.
        pc_begin = fde_va + 8 + struct.unpack_from('<i', data, fo + 8)[0]
        pc_range = struct.unpack_from('<i', data, fo + 12)[0]
        if pc_begin == start and pc_range > 0:
            ranges.append((start, start + pc_range))
    ranges.sort()
    exports = {}
    ds = f.get_section_by_name('.dynsym')
    for sym in ds.iter_symbols():
        if sym['st_shndx'] != 'SHN_UNDEF' and sym['st_info']['type'] == 'STT_FUNC' and sym.name and not sym.name.startswith('_Z'):
            exports[sym.name] = sym['st_value']
    segs = [(s['p_vaddr'], s['p_offset'], s['p_filesz'], s['p_memsz']) for s in f.iter_segments() if s['p_type'] == 'PT_LOAD']
    top = max(v + m for v, o, n, m in segs)
    img = bytearray(top)
    for v, o, n, m in segs:
        img[v:v + n] = data[o:o + n]
    ro = f.get_section_by_name('.rodata')
    writable = [(s['sh_addr'], s['sh_addr'] + s['sh_size']) for s in f.iter_sections()
                if s['sh_flags'] & 1 and s['sh_flags'] & 2 and s['sh_addr']]   # SHF_WRITE and SHF_ALLOC
    return bytes(img), 0, ranges, exports, (ro['sh_addr'], ro['sh_addr'] + ro['sh_size']), writable


def scan(img, ranges, strings_range, writable, label):
    """{start: (calls [targets in order], strings [texts], data [(global, mnemonic)] in order)} by a linear sweep."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.skipdata = True
    starts = [r[0] for r in ranges]
    startset = set(starts)
    s0, s1 = strings_range
    out = {}
    rip = re.compile(r'\[rip ([+-]) 0x([0-9a-f]+)\]')
    wstarts = sorted(writable)

    def is_data(t):
        k = bisect.bisect_right(wstarts, (t, float('inf'))) - 1
        return k >= 0 and wstarts[k][0] <= t < wstarts[k][1]
    for n, (lo, hi) in enumerate(ranges):
        calls, strs, datas = [], [], []
        for address, size, mnemonic, op in md.disasm_lite(img[lo:hi], lo):
            if mnemonic == 'call' and op.startswith('0x'):
                t = int(op, 16)
                if t in startset:
                    calls.append(t)
            elif 'rip' in op:
                m = rip.search(op)
                if not m:
                    continue
                t = address + size + (int(m.group(2), 16) * (1 if m.group(1) == '+' else -1))
                if mnemonic == 'lea' and s0 <= t < s1:
                    sm = STRING.match(img, t)
                    if sm:
                        strs.append(sm.group(0)[:-1].decode('latin-1'))
                elif is_data(t):
                    datas.append((t, mnemonic))
        out[lo] = (calls, strs, datas)
        if n % 50000 == 0:
            print(f'{label}: {n}/{len(ranges)}', flush=True)
    return out


def load(args, side):
    cache = os.path.join(args.cache, f'calls2-{side}.pickle') if args.cache else None
    if cache and os.path.exists(cache):
        return pickle.load(open(cache, 'rb'))
    img, _, ranges, exports, srange, writable = (win_functions if side == 'win' else lin_functions)(
        args.win if side == 'win' else args.lin)
    graph = scan(img, ranges, srange, writable, side)
    result = (graph, exports)
    if cache:
        pickle.dump(result, open(cache, 'wb'))
    return result


def anchors(args, wg, lg, wexp, lexp):
    pairs, why = {}, {}

    def add(w, l, reason):
        if w in wg and l in lg and w not in pairs and l not in rev:
            pairs[w] = l
            rev[l] = w
            why[w] = reason
    rev = {}
    known = json.load(open(os.path.join(os.path.dirname(__file__), 'lockstep_linux_known_pairs.json')))['pairs']
    for p in known:
        if p.get('kind') == 'function':
            add(int(p['win'], 16), int(p['lin'], 16), 'known')
    for name in sorted(set(wexp) & set(lexp)):
        add(wexp[name], lexp[name], 'export:' + name)
    if args.rtti:
        slots = {}
        for cls, rows in json.load(open(args.rtti))['classes'].items():
            for r in rows:
                ws, ls = r['win_slots'], r['lin_slots']
                if len(ls) != len(ws) + 1:
                    continue
                for i in range(1, len(ws)):
                    slots.setdefault(int(ws[i], 16), set()).add(int(ls[i + 1], 16))
        for w, ls in slots.items():
            if len(ls) == 1:
                add(w, next(iter(ls)), 'vtable')
    # strings referenced by one function only on each side
    def owners(g):
        o = {}
        for f, (_, strs, _) in g.items():
            for s in set(strs):
                o.setdefault(s, []).append(f)
        return {s: fs[0] for s, fs in o.items() if len(fs) == 1}
    wo, lo = owners(wg), owners(lg)
    for s in sorted(set(wo) & set(lo)):
        add(wo[s], lo[s], 'string:' + s[:60])
    return pairs, rev, why


def gap_votes(wg, lg, pairs, rev):
    """{(w, l): number of paired parents whose aligned call sequences put w and l in the same one-call gap}."""
    votes = {}
    for w, l in pairs.items():
        cw, cl = wg[w][0], lg[l][0]
        if not cw or not cl:
            continue
        ai, aj, j = [], [], 0
        for i, t in enumerate(cw):
            if t in pairs:
                try:
                    k = cl.index(pairs[t], j)
                except ValueError:
                    continue
                ai.append(i)
                aj.append(k)
                j = k + 1
        seen = set()
        for i0, j0, i1, j1 in zip([-1] + ai, [-1] + aj, ai + [len(cw)], aj + [len(cl)]):
            gw, gl = cw[i0 + 1:i1], cl[j0 + 1:j1]
            if len(gw) == len(gl) == 1 and gw[0] not in pairs and gl[0] not in rev and (gw[0], gl[0]) not in seen:
                seen.add((gw[0], gl[0]))
                votes[(gw[0], gl[0])] = votes.get((gw[0], gl[0]), 0) + 1
    return votes


def callee_signatures(wg, lg, pairs, rev, min_shared=3):
    """{(w, l)}: unpaired functions whose paired callees (as a set, at least min_shared) are the same on both sides and
    unique to them on each side."""
    def sig(g, f, ok):
        return frozenset(t for t in g[f][0] if ok(t))
    wsig, lsig = {}, {}
    for f in wg:
        if f not in pairs:
            s = frozenset(pairs[t] for t in wg[f][0] if t in pairs)
            if len(s) >= min_shared:
                wsig.setdefault(s, []).append(f)
    for f in lg:
        if f not in rev:
            s = frozenset(t for t in lg[f][0] if t in rev)
            if len(s) >= min_shared:
                lsig.setdefault(s, []).append(f)
    return {(ws[0], lsig[s][0]) for s, ws in wsig.items() if len(ws) == 1 and len(lsig.get(s, [])) == 1}


def data_pairs(wg, lg, pairs, min_votes=2):
    """{win global: lin global}: in paired functions whose global reference sequences have the same length and the
    same instruction kinds, the n-th references pair; kept when min_votes functions agree and none disagrees."""
    votes = {}
    for w, l in pairs.items():
        dw, dl = wg[w][2], lg[l][2]
        if not dw or len(dw) != len(dl) or [m for _, m in dw] != [m for _, m in dl]:
            continue
        for (a, _), (b, _) in set(zip(dw, dl)):
            votes.setdefault(a, {}).setdefault(b, 0)
            votes[a][b] += 1
    out, taken = {}, {}
    for a, bs in votes.items():
        if len(bs) == 1:
            (b, n), = bs.items()
            if n >= min_votes:
                taken.setdefault(b, []).append(a)
                out[a] = b
    return {a: b for a, b in out.items() if len(taken[b]) == 1}


def data_signatures(wg, lg, pairs, rev, gpairs, min_shared=2):
    """{(w, l)}: unpaired functions whose paired globals (as a set, at least min_shared) match and are unique."""
    wsig, lsig = {}, {}
    grev = {b: a for a, b in gpairs.items()}
    for f in wg:
        if f not in pairs:
            s = frozenset(gpairs[t] for t, _ in wg[f][2] if t in gpairs)
            if len(s) >= min_shared:
                wsig.setdefault(s, []).append(f)
    for f in lg:
        if f not in rev:
            s = frozenset(t for t, _ in lg[f][2] if t in grev)
            if len(s) >= min_shared:
                lsig.setdefault(s, []).append(f)
    return {(ws[0], lsig[s][0]) for s, ws in wsig.items() if len(ws) == 1 and len(lsig.get(s, [])) == 1}


def propagate(wg, lg, pairs, rev, why, min_votes=2, use_data=True):
    """Vote-based: a one-call gap pairs its callees only when at least min_votes different paired parents agree and no
    rival pairing of either function has as many; plus identical paired-callee signatures. (Held out against the known
    pairs, single gap observations were wrong 3 of 3 times: inlining differs between MSVC and GCC.)"""
    changed, rounds = True, 0
    while changed:
        changed, rounds = False, rounds + 1
        votes = gap_votes(wg, lg, pairs, rev)
        best_w, best_l = {}, {}
        for (a, b), n in votes.items():
            if n > best_w.get(a, (0, 0, None))[0]:
                best_w[a] = (n, best_w.get(a, (0, 0, None))[0], b)
            elif n > best_w[a][1]:
                best_w[a] = (best_w[a][0], n, best_w[a][2])
            best_l[b] = max(best_l.get(b, 0), n) if best_l.get(b, (0,)) != n else best_l[b]
        for a, (n, second, b) in best_w.items():
            if n >= min_votes and n > second and a not in pairs and b not in rev:
                rivals = [m for (x, y), m in votes.items() if y == b and x != a]
                if all(m < n for m in rivals):
                    pairs[a], rev[b] = b, a
                    why[a] = f'call gaps ({n} parents)'
                    changed = True
        for a, b in callee_signatures(wg, lg, pairs, rev):
            if a not in pairs and b not in rev:
                pairs[a], rev[b] = b, a
                why[a] = 'callee signature'
                changed = True
        if use_data:
            for a, b in data_signatures(wg, lg, pairs, rev, data_pairs(wg, lg, pairs)):
                if a not in pairs and b not in rev:
                    pairs[a], rev[b] = b, a
                    why[a] = 'data signature'
                    changed = True
    return rounds


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--win', required=True)
    p.add_argument('--lin', required=True)
    p.add_argument('--rtti')
    p.add_argument('--out')
    p.add_argument('--cache')
    p.add_argument('--query', default='')
    args = p.parse_args(argv)
    wg, wexp = load(args, 'win')
    lg, lexp = load(args, 'lin')
    print(f'functions: windows {len(wg)}, linux {len(lg)}; exports {len(wexp)}/{len(lexp)}', flush=True)
    pairs, rev, why = anchors(args, wg, lg, wexp, lexp)
    kinds = {}
    for r in why.values():
        kinds[r.split(':')[0]] = kinds.get(r.split(':')[0], 0) + 1
    print('anchors', len(pairs), kinds, flush=True)
    rounds = propagate(wg, lg, pairs, rev, why)
    print(f'after propagation ({rounds} rounds): {len(pairs)} pairs', flush=True)
    if args.out:
        json.dump({'%x' % w: {'lin': '%x' % l, 'why': why[w]} for w, l in sorted(pairs.items())}, open(args.out, 'w'), indent=0)
    for q in filter(None, args.query.split(',')):
        w = int(q, 16)
        if w in pairs:
            print(f'{w:x} -> {pairs[w]:x} ({why[w]})')
        else:
            starts = sorted(wg)
            k = bisect.bisect_right(starts, w) - 1
            f = starts[k] if k >= 0 else None
            print(f'{w:x}: unpaired' + (f' (inside {f:x}: ' + (f'paired {pairs[f]:x}' if f in pairs else 'unpaired') + ')' if f is not None and f != w else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
