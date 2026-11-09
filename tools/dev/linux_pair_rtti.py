"""Windows X4.exe <-> Linux X4 vtable pairs from RTTI (the people-stack port's first anchors).

Both builds keep their class names for RTTI: MSVC's complete object locators name ".?AVShip@U@@", GCC's typeinfo names
"N1U4ShipE". A class's vtables pair by (class, offset to the complete object): MSVC's locator offset, GCC's
offset-to-top (negated). Their virtual function slots then pair one to one, except that GCC has two destructor slots
(complete, deleting) where MSVC has one (scalar deleting): Linux slot = Windows slot + 1 after the destructor.

  python tools/dev/linux_pair_rtti.py --win <X4.exe> --lin <X4> --out pairs-rtti.json [--class U::PlayerController]

Reads both files only. Output: {"classes": {name: [{"offset", "win_vtable", "lin_vtable", "win_slots", "lin_slots"}]}}
with addresses as hex strings (Windows RVAs, Linux virtual addresses)."""
import argparse
import json
import re
import struct
import sys

import pefile
from elftools.elf.elffile import ELFFile


def msvc_name(decorated):
    """'.?AVPlayerController@U@@' -> 'U::PlayerController' (templates and other forms kept as they are)."""
    m = re.fullmatch(r'\.\?A[VU](.+)@@', decorated)
    if not m or '?$' in decorated:
        return None
    return '::'.join(reversed(m.group(1).split('@')))


def itanium_name(mangled):
    """'N1U16PlayerControllerE' -> 'U::PlayerController', '4Ship' -> 'Ship' (templates and other forms: None)."""
    s, parts = mangled, []
    if s.startswith('N') and s.endswith('E'):
        s = s[1:-1]
    elif not s[:1].isdigit():
        return None
    while s:
        m = re.match(r'(\d+)', s)
        if not m:
            return None
        n, s = int(m.group(1)), s[len(m.group(1)):]
        if n > len(s) or n == 0:
            return None
        parts.append(s[:n])
        s = s[n:]
    return '::'.join(parts)


def windows_vtables(path):
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    t0, t1 = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    rdata = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.rdata')
    r0, r1 = rdata.VirtualAddress, rdata.VirtualAddress + rdata.Misc_VirtualSize
    out = {}
    # A complete object locator (x64): sig=1, offset, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf (RVAs).
    for at in range(r0, r1 - 24, 4):
        sig, off, cd, td, chd, me = struct.unpack_from('<IIIiii', img, at)
        if sig != 1 or me != at or not (0 < td < len(img)):
            continue
        name = img[td + 16:img.index(b'\0', td + 16)].decode('latin-1')
        cls = msvc_name(name)
        if not cls:
            continue
        # The vtable follows the qword that points at this locator.
        ptr = struct.pack('<Q', base + at)
        start = r0
        while True:
            k = img.find(ptr, start, r1)
            if k < 0:
                break
            start = k + 8
            vt = k + 8
            slots = []
            while vt + 8 * len(slots) + 8 <= r1:
                v = struct.unpack_from('<Q', img, vt + 8 * len(slots))[0] - base
                if not t0 <= v < t1:
                    break
                slots.append(v)
            if slots:
                out.setdefault(cls, []).append({'offset': off, 'win_vtable': vt, 'win_slots': slots})
    return out


def linux_vtables(path):
    f = ELFFile(open(path, 'rb'))
    data = open(path, 'rb').read()
    segs = [(s['p_vaddr'], s['p_offset'], s['p_filesz']) for s in f.iter_segments() if s['p_type'] == 'PT_LOAD']

    def off(va):
        for v, o, n in segs:
            if v <= va < v + n:
                return o + va - v
        return None
    text = f.get_section_by_name('.text')
    t0, t1 = text['sh_addr'], text['sh_addr'] + text['sh_size']
    ro = f.get_section_by_name('.rodata')
    relro = f.get_section_by_name('.data.rel.ro')
    rr0, rr1 = relro['sh_addr'], relro['sh_addr'] + relro['sh_size']
    rrdata = data[relro['sh_offset']:relro['sh_offset'] + relro['sh_size']]
    rodata = data[ro['sh_offset']:ro['sh_offset'] + ro['sh_size']]
    # typeinfo objects: {vptr, name ptr, ...} in .data.rel.ro whose name pointer reaches a mangled class name in .rodata
    # (names are not always preceded by a NUL: alignment padding); vtables: {offset_to_top, typeinfo ptr, slots...}.
    ro0, ro1 = ro['sh_addr'], ro['sh_addr'] + ro['sh_size']
    names, typeinfo = {}, {}
    for i in range(0, len(rrdata) - 16, 8):
        p = struct.unpack_from('<Q', rrdata, i + 8)[0]
        if not ro0 <= p < ro1:
            continue
        if p not in names:
            o = p - ro0
            end = rodata.find(b'\0', o, o + 512)
            text = rodata[o:end].decode('latin-1') if end > o else ''
            names[p] = itanium_name(text) if re.fullmatch(r'N?(?:\d+[A-Za-z_][A-Za-z0-9_]*)+E?', text) else None
        if names[p]:
            typeinfo[rr0 + i] = names[p]
    out = {}
    for i in range(0, len(rrdata) - 24, 8):
        top, ti = struct.unpack_from('<qQ', rrdata, i)
        if ti not in typeinfo or not -0x100000 < top <= 0:
            continue
        slots = []
        j = i + 16
        while j + 8 <= len(rrdata):
            v = struct.unpack_from('<Q', rrdata, j)[0]
            if not t0 <= v < t1:
                break
            slots.append(v)
            j += 8
        if slots:
            out.setdefault(typeinfo[ti], []).append({'offset': -top, 'lin_vtable': rr0 + i + 16, 'lin_slots': slots})
    return out


def pair(win, lin, only=None):
    classes = {}
    for cls in sorted(set(win) & set(lin)):
        if only and cls != only:
            continue
        rows = []
        lin_by = {}
        for v in lin[cls]:
            lin_by.setdefault(v['offset'], []).append(v)
        for w in win[cls]:
            cands = lin_by.get(w['offset'], [])
            if len(cands) != 1:
                continue
            l = cands[0]
            rows.append({'offset': w['offset'], 'win_vtable': '%x' % w['win_vtable'], 'lin_vtable': '%x' % l['lin_vtable'],
                         'win_slots': ['%x' % s for s in w['win_slots']], 'lin_slots': ['%x' % s for s in l['lin_slots']]})
        if rows:
            classes[cls] = rows
    return classes


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--win', required=True)
    p.add_argument('--lin', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--class', dest='only')
    args = p.parse_args(argv)
    win = windows_vtables(args.win)
    print(f'windows: {len(win)} classes with vtables', flush=True)
    lin = linux_vtables(args.lin)
    print(f'linux: {len(lin)} classes with vtables', flush=True)
    classes = pair(win, lin, args.only)
    json.dump({'classes': classes}, open(args.out, 'w'), indent=1)
    print(f'paired: {len(classes)} classes, {sum(len(v) for v in classes.values())} vtables -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
