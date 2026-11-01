"""Generate src/native/engine/lockstep_ui_guard_table.h (native_gate_ui_guard, default off) from the inventory
(tools/dev/ui_guard_inventory.py) and the pinned X4.exe, read-only.

  python tools/dev/gen_ui_guard_table.py --image <X4.exe 9.00> --symbols <x4-re/symbols.csv> --prototypes <x4-re/data/ffi_prototypes.csv>
      [--out src/native/engine/lockstep_ui_guard_table.h] [--check]

For every entry: its RVA (PE export, or the Lua C function the engine registers under that global name, from the
labelled symbols), a relocatable entry prefix of five or more bytes decoded with capstone (no RIP-relative operand,
no branch, nothing in the function jumps into it; an entry that is a jmp rel32 becomes a jump to its target), a pin
(SHA-256 of the function's first bytes up to its end, at most 512, never across a base relocation) and what the
guard returns when it refuses (zero, an empty string for const char*, no Lua results). Names only and our own code:
no game bytes beyond the prefixes the hooks compare. --check compares the committed header with a fresh run."""
import argparse
import bisect
import csv
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/dev'))
import ui_guard_inventory as inv  # noqa: E402

PIN_MAX = 0x200
PREFIX_MAX = 16
X4_SHA256 = '19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891'


class Image:
    def __init__(self, path):
        import pefile
        self.sha = hashlib.sha256(Path(path).read_bytes()).hexdigest()
        pe = pefile.PE(str(path), fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY[d] for d in (
            'IMAGE_DIRECTORY_ENTRY_EXPORT', 'IMAGE_DIRECTORY_ENTRY_EXCEPTION', 'IMAGE_DIRECTORY_ENTRY_BASERELOC')])
        self.data = pe.get_memory_mapped_image()
        self.exports = {e.name.decode(): e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
        self.starts, self.ends = [], {}
        for e in pe.DIRECTORY_ENTRY_EXCEPTION:
            self.starts.append(e.struct.BeginAddress)
            self.ends[e.struct.BeginAddress] = e.struct.EndAddress
        self.starts.sort()
        self.relocs = sorted(r.rva for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []) for r in block.entries if r.type)
        text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
        self.text = (text.VirtualAddress, text.VirtualAddress+text.Misc_VirtualSize)

    def function_end(self, rva):
        """The end of the function whose unwind range starts at rva, or None (a leaf without unwind data)."""
        return self.ends.get(rva)

    def reloc_in(self, lo, hi):
        i = bisect.bisect_left(self.relocs, lo)
        return i < len(self.relocs) and self.relocs[i] < hi


def lua_globals(symbols):
    out = {}
    for row in csv.DictReader(open(symbols, encoding='utf-8')):
        for m in re.finditer(r"registered as '([A-Za-z_0-9]+)' \(global", row['evidence']):
            out.setdefault(m.group(1), set()).add(int(row['rva'], 16))
    return out


def prototypes_of(prototypes):
    """name -> (return type, parameter names)."""
    out = {}
    for row in csv.DictReader(open(prototypes, encoding='utf-8')):
        m = re.match(r'\s*(.*?)\b' + re.escape(row['name']) + r'\s*\((.*)\)\s*;?\s*$', row['prototype'])
        if m:
            params = [p.strip() for p in m.group(2).split(',') if p.strip() and p.strip() != 'void']
            out[row['name']] = (' '.join(m.group(1).split()), [re.split(r'[\s*]+', p)[-1] for p in params])
    return out


SCALARS = {'void', 'bool', 'int', 'int32_t', 'uint32_t', 'int64_t', 'uint64_t', 'size_t', 'UniverseID', 'BuildTaskID',
           'TradeRuleID', 'BlacklistID', 'FightRuleID', 'float', 'double'}
# Structs returned through the hidden pointer (Win64: anything but 1, 2, 4 or 8 bytes): size, offsets of const char* fields.
STRUCTS = {'UIFormationInfo': (40, (0, 8)), 'UICrewExchangeResult': (24, (0,))}


def ret_kind(text):
    """(refusal kind, struct size, string slot mask)."""
    if text in SCALARS:
        return 'UIGUARD_RET_ZERO', 0, 0
    if text.replace(' ', '') in ('constchar*', 'char*'):
        return 'UIGUARD_RET_STRING', 0, 0
    if text in STRUCTS:
        size, strings = STRUCTS[text]
        return 'UIGUARD_RET_STRUCT', size, sum(1 << (o//8) for o in strings)
    raise ValueError(f'return type {text!r}: add it to SCALARS if it fits a register, else to STRUCTS with its layout')


JCC = {'jo', 'jno', 'jb', 'jae', 'je', 'jne', 'jbe', 'ja', 'js', 'jns', 'jp', 'jnp', 'jl', 'jge', 'jle', 'jg'}


def decode(img, rva, md):
    """A hookable entry: dict(prefix, tail, rip=[disp offsets], jcc=(at, length, target) or None), or (None, why).
    The prefix is copied with its layout kept: a RIP-relative displacement is moved by the distance to the copy at
    install, one conditional branch is pointed at an absolute jump to its target behind the copy."""
    import capstone
    code = bytes(img.data[rva:rva+0x40])
    first = next(md.disasm(code, rva), None)
    if first is None:
        return None, 'undecodable'
    if first.bytes[0] == 0xe9 and first.size == 5:
        return dict(prefix=5, tail=int(first.op_str, 16), rip=[], jcc=None), None
    size, rip, jcc = 0, [], None
    for ins in md.disasm(code, rva):
        at = ins.address-rva
        if ins.mnemonic in JCC and ins.op_str.startswith('0x') and jcc is None and ins.size in (2, 6):
            jcc = (at, ins.size, int(ins.op_str, 16))
        elif ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_RET) or                 ins.group(capstone.CS_GRP_INT) or ins.mnemonic in ('int3', 'ud2'):
            return None, f'{ins.mnemonic} at +{at:x}'
        elif 'rip' in ins.op_str:
            if ins.disp_size != 4 or not ins.disp_offset:
                return None, f'rip-relative without a 32-bit displacement at +{at:x}'
            rip.append(at+ins.disp_offset)
        size += ins.size
        if size >= 5:
            break
    if size < 5 or size > PREFIX_MAX:
        return None, f'prefix {size}'
    if len(rip) > 2:
        return None, 'more than two RIP-relative operands in the prefix'
    if jcc and jcc[0]+jcc[1] > size:
        return None, 'branch past the prefix'
    return dict(prefix=size, tail=0, rip=rip, jcc=jcc), None


def span(img, rva, md):
    """The bytes the function's own code covers from its entry: the unwind range, else up to the first ret/jmp past
    the prefix followed by padding (a leaf), at most PIN_MAX."""
    end = img.function_end(rva)
    if end:
        return min(end-rva, PIN_MAX)
    at = rva
    for ins in md.disasm(bytes(img.data[rva:rva+PIN_MAX]), rva):
        at = ins.address+ins.size
        if ins.mnemonic in ('ret', 'int3') or (ins.mnemonic == 'jmp' and at-rva >= 5):
            break
    return min(at-rva, PIN_MAX)


def jumps_into_prefix(img, rva, length, prefix, md):
    """A branch of the function's own code to a byte inside the prefix (not its first)."""
    for ins in md.disasm(bytes(img.data[rva:rva+length]), rva):
        if (ins.mnemonic.startswith('j') or ins.mnemonic.startswith('loop')) and ins.op_str.startswith('0x'):
            target = int(ins.op_str, 16)
            if rva < target < rva+prefix:
                return True
    return False


def build(args):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    img = Image(args.image)
    if img.sha != X4_SHA256:
        raise SystemExit(f'not the pinned X4.exe 9.00: {img.sha}')
    globals_ = lua_globals(args.symbols)
    protos = prototypes_of(args.prototypes)
    rows, problems, seen = [], [], {}
    group_of = {g: i for i, g in enumerate(inv.GROUPS)}
    for (kind, name), (_, group, covered) in sorted(inv.INVENTORY.items(), key=lambda kv: (group_of[kv[1][1]], kv[0][1], kv[0][0])):
        if kind == 'ffi':
            rva = img.exports.get(name)
            if rva is None:
                problems.append(f'{name}: no export')
                continue
            if name not in protos:
                problems.append(f'{name}: no prototype')
                continue
            ret, size, strings = ret_kind(protos[name][0])
            params = protos[name][1]
            # The physical argument the check-only flag is in (a hidden struct pointer is the first); -1: none.
            checkonly = params.index('checkonly')+(1 if size else 0) if 'checkonly' in params else -1
        else:
            rvas = globals_.get(name)
            if not rvas or len(rvas) != 1:
                problems.append(f'{name}: Lua global at {sorted(map(hex, rvas or []))}')
                continue
            rva, ret, size, strings, checkonly = next(iter(rvas)), 'UIGUARD_RET_ZERO', 0, 0, -1   # a Lua C function: no results
        if rva in seen:
            problems.append(f'{name}: same entry as {seen[rva]}')
            continue
        seen[rva] = name
        plan, why = decode(img, rva, md)
        if why:
            problems.append(f'{name} {rva:#x}: {why}')
            continue
        prefix, tail = plan['prefix'], plan['tail']
        length = span(img, rva, md)
        if tail == 0 and jumps_into_prefix(img, rva, length, prefix, md):
            problems.append(f'{name} {rva:#x}: a branch into the prefix')
            continue
        pin = length
        while pin > prefix and img.reloc_in(rva, rva+pin):
            pin -= 1
        if pin < prefix or img.reloc_in(rva, rva+prefix):
            problems.append(f'{name} {rva:#x}: relocation in the prefix')
            continue
        if (rva & 15) > 11:
            problems.append(f'{name} {rva:#x}: the entry patch would cross a 16-byte block (one CAS)')
            continue
        if not (img.text[0] <= rva and rva+pin <= img.text[1]):
            problems.append(f'{name} {rva:#x}: outside .text')
            continue
        rows.append(dict(name=name, rva=rva, kind=kind, group=group, covered=covered, ret=ret, size=size, strings=strings,
                         checkonly=checkonly, prefix=prefix, tail=tail, rip=plan['rip'], jcc=plan['jcc'],
                         bytes=bytes(img.data[rva:rva+prefix]), pin=pin, sha=hashlib.sha256(bytes(img.data[rva:rva+pin])).hexdigest()))
    if problems:
        raise SystemExit('ui guard table refused:\n  ' + '\n  '.join(problems))
    return render(rows, img)


def render(rows, img):
    out = ['/* GENERATED by tools/dev/gen_ui_guard_table.py from tools/dev/ui_guard_inventory.py against the pinned X4.exe 9.00',
           f' * (SHA-256 {img.sha}); do not edit. The UI guard (lockstep_ui_guard.h, native_gate_ui_guard, default off):',
           ' * name, RVA, kind, group, families covering it, refusal value, prefix length, a tail jump\'s target, the prefix, pin. */',
           f'#define UIGUARD_ENTRIES {len(rows)}u',
           'static const uiguard_entry uiguard_table[UIGUARD_ENTRIES]={']
    for r in rows:
        prefix = ','.join(f'0x{b:02x}' for b in r['bytes'])
        rip = ','.join(str(o) for o in r['rip']) or '0'
        jcc = f'{r["jcc"][0]},{r["jcc"][1]},0x{r["jcc"][2]:x}u' if r['jcc'] else '0,0,0u'
        out.append(f'    {{"{r["name"]}",0x{r["rva"]:x}u,UIGUARD_{r["kind"].upper()},UIGUARD_G_{r["group"].upper()},{r["covered"]}u,{r["ret"]},{r["size"]},{r["strings"]},{r["checkonly"]},'
                   f'{r["prefix"]},0x{r["tail"]:x}u,{{{prefix}}},{{{rip}}},{jcc},0x{r["pin"]:x}u,"{r["sha"]}"}},')
    out.append('};')
    return '\n'.join(out) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--symbols', type=Path, required=True)
    p.add_argument('--prototypes', type=Path, required=True)
    p.add_argument('--out', type=Path, default=ROOT/'src/native/engine/lockstep_ui_guard_table.h')
    p.add_argument('--check', action='store_true', help='compare with the committed header instead of writing it')
    args = p.parse_args(argv)
    text = build(args)
    if args.check:
        if args.out.read_text(encoding='utf-8') != text:
            raise SystemExit(f'{args.out} differs from a fresh generation')
        print(f'{args.out.name}: up to date ({text.count(chr(10)+"    {")} entries)')
        return
    args.out.write_text(text, encoding='utf-8', newline='\n')
    print(f'{args.out}: {text.count(chr(10)+"    {")} entries')


if __name__ == '__main__':
    main()
