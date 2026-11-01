"""Generate the site tables of the exact-reciprocal rewrite (Windows gate
native_gate_exact_reciprocal, Linux preload X4MP_EXACT_RECIPROCAL=1).

Both X4 builds evaluate rcpss/rsqrtss (approximate 1/x and 1/sqrt(x)) in simulation
code; only their relative error bound is architectural, so Intel and AMD CPUs return
different bits (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt, class A). Each such
instruction is rewritten to exact IEEE arithmetic through a trampoline
(src/native/portable_math/exact_reciprocal_emit.h). This tool finds the instructions
and the window of whole instructions (at least the 5 bytes of a jmp rel32) each
trampoline replaces, and writes only addresses, lengths, instruction kinds,
displacement offsets and FNV-1a 64 digests of the original window bytes: no game
bytes or disassembly enter the repository.

Chains: rsqrtss d,s followed in the same block by rcpss d,d, with no instruction in
between touching d (xmm/ymm/zmm) and no branch target, function boundary or referenced
address in between or at the rcpss, are the approximate length rcp(rsqrt(x)); both
halves become kinds 3 and 4 (sqrtss d,s and nothing: d[0] = sqrt(s[0])). Copied chains:
rsqrtss r,s whose result is first copied whole (movaps/movups/movapd/movupd/movdqa/movdqu
c,r, c != r) and then read by rcpss c,r, nothing else touching r before the copy nor r or
c between the copy and the rcpss (GCC's pattern for a result in another register), are
the same length in c; they become kinds 3 and 5: sqrtss r,s, the copy as it is, and
r[0] = 1/r[0] at the rcpss, so c[0] = sqrt(s[0]) and r[0] keeps exactly the value the
one-by-one rewrite gives it (1/sqrt(s[0]), kind 2): no liveness assumption about r.
MSVC's form without the copy, rsqrtss r,s whose result is first read by rcpss e,r (e != r),
becomes kinds 3 and 6: sqrtss r,s, then movss e,r and r[0] = 1/r[0] at the rcpss
(e[0] = sqrt(s[0]), e's lanes 1-3 kept as rcpss keeps them, r as above). A
chain is kept only when both halves lie in accepted windows; otherwise both are
rewritten one by one (kinds 2 and 1).

Method: recursive descent from every function root (Windows .pdata roots, the switch arms of their
MSVC jump tables and the leaf functions; Linux FDE starts, GCC jump-table targets and C++ landing
pads), direct branches followed inside the function; calls, returns, indirect jumps and
int3 end a path, so only reachable instructions qualify. MSVC leaf functions have no unwind
data, hence no .pdata entry: they are found as 16-byte aligned starts after padding or a ret
that decoded direct calls/jmps or 8-byte data pointers (vtables) name, to a fixed point
(FR27; FR26 listed five such roots by hand). An MSVC switch (cmp i,N; ja; mov r32,[base+i*4+T];
add r64,base; jmp r64) is followed when its N+1 table entries all land in the function.
--inventory lists every approximate instruction the table still leaves as it is. A window
is refused when it would contain a branch, call, return, int3, an instruction that writes
RSP, a decoded-path gap, or an inner instruction boundary that is a branch target, a
function boundary, a jump-table or landing-pad target or (Windows) any 32-bit value in the
image's data or aligned in .text, (Linux) any 64-bit pointer in its data; or when it touches
another patch: on Windows within 16 bytes of a gate module's patch reference (gate pins
over a window keep passing: hash_ok and read_local see the original bytes while
installed), on Linux the x4mp-agent's pinned sites and the preload's decimal readers.
Where the window forward from an instruction meets such an inner boundary, the window
that ENDS with the instruction is tried instead (the fewest whole instructions before it
on the same path; FR27): its end is where the trampoline jumps back, so that boundary
may be anything.

  python tools/dev/gen_exact_reciprocal.py --windows <X4.exe> [--check | --inventory]
  python tools/dev/gen_exact_reciprocal.py --linux <X4> [--check]
--check compares with the committed header instead of writing it; --inventory prints the
approximate instructions the Windows table does not rewrite.
"""
import argparse
import bisect
import collections
from pathlib import Path
import re
import struct
import sys

import capstone
from capstone import x86_const as X

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lockstep_linux_map as lm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/native/engine/lockstep_exact_reciprocal_sites.h'
LINUX_HEADER = ROOT / 'src/native/portable_math/preload/exact_reciprocal_linux_sites.h'
AGENT_PINS = ROOT / 'src/rust/x4mp-agent/src/pinned.rs'
KINDS = {'rcpss': 1, 'rsqrtss': 2}
SQRT, CHAIN_END, COPY_END, MOVE_END = 3, 4, 5, 6
COPIES = ('movaps', 'movups', 'movapd', 'movupd', 'movdqa', 'movdqu')
CHAIN_REACH = 16
STOP = ('ret', 'int3', 'hlt', 'ud2')
MAX_INSNS = 4
# FR26's hand-listed roots, which the general leaf and switch follower must reach (windows_descent checks): the box
# radius writers 0xb34d0, 0x6a1590, 0xd47470 and the safesize leaf 0x14e0620 (leaves), and the speed arm 0xcf36a2 of
# the component property evaluator 0xcedeb0 (jump table 0xd0bdf0).
FR26_ROOTS = (0xb34d0, 0x6a1590, 0xd47470, 0x14e0620, 0xcf36a2)
# Data words equal to an inner boundary of an extra root's window only by coincidence: (value, RVA of the word). Each
# is the first word of an UNWIND_INFO that a .pdata entry names (version 1, prolog size, code count, frame register)
# and occurs nowhere else in the file; windows_tables verifies that before it ignores the word.
WINDOWS_WORD_COINCIDENCES = ((0xb3501, 0x2e7d87c),)  # 01 35 0b 00: 0xb34d0's rcpss
APPROX = ('rcpss', 'rsqrtss', 'rcpps', 'rsqrtps', 'vrcpss', 'vrsqrtss', 'vrcpps', 'vrsqrtps')


def fnv64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


class Code:
    def __init__(self, base, data):
        self.base, self.bytes, self.size = base, data, len(data)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def insn(self, rva):
        off = rva - self.base
        if not 0 <= off < self.size:
            return None
        for ins in self.md.disasm(self.bytes[off:off + 16], rva, count=1):
            return ins
        return None


def descend(code, functions, tables=None):
    """Instructions reachable from each function's roots, and every direct branch target.
    functions: [(roots, [(begin, end)])]. tables(ins, spans, run) -> extra targets of an
    indirect jump table addressed by this instruction (run: the instructions decoded before it
    on the same straight-line path, oldest first), or None."""
    decoded, targets = {}, set()
    for roots, spans in functions:
        inside = lambda a: any(b <= a < e for b, e in spans)  # noqa: E731
        todo = list(roots)
        while todo:
            a = todo.pop()
            run = collections.deque(maxlen=8)
            while inside(a) and a not in decoded:
                ins = code.insn(a)
                if ins is None:
                    break
                decoded[a] = ins
                m = ins.mnemonic
                if tables:
                    for t in tables(ins, spans, list(run)):
                        targets.add(t)
                        todo.append(t)
                run.append(ins)
                if m in STOP:
                    break
                if m == 'jmp' or m.startswith('j') or m.startswith('loop'):
                    op = ins.operands[0] if ins.operands else None
                    if op is not None and op.type == X.X86_OP_IMM:
                        targets.add(op.imm)
                        if inside(op.imm):
                            todo.append(op.imm)
                    if m == 'jmp':
                        break
                a += ins.size
    return decoded, targets


def referenced_words(pe, code):
    """Windows: every 32-bit value inside .text found in data (any offset) or aligned in .text."""
    import numpy as np
    words = set()
    lo, hi = code.base, code.base + code.size
    for name, va, size, raw, raw_size, flags in pe.sections:
        data = np.frombuffer(pe.data[raw:raw + raw_size], dtype=np.uint8)
        if len(data) < 4:
            continue
        if name == '.text':
            values = np.frombuffer(data[:len(data) // 4 * 4].tobytes(), dtype='<u4')
        else:
            values = (data[:-3].astype(np.uint32) | data[1:-2].astype(np.uint32) << 8 |
                      data[2:-1].astype(np.uint32) << 16 | data[3:].astype(np.uint32) << 24)
        words.update(np.unique(values[(values >= lo) & (values < hi)]).tolist())
    return words


def xmm_index(reg, md):
    name = md.reg_name(reg) or ''
    m = re.fullmatch(r'[xyz]mm(\d+)', name)
    return int(m.group(1)) if m else None


def chains(code, decoded, targets, words, marks):
    """{rsqrtss address: (rcpss address, 4 or 5)} for every rcp(rsqrt(x)) chain, 5 when the
    rsqrtss result reaches the rcpss through a whole-register copy (module docstring)."""
    found = {}
    for a, ins in decoded.items():
        if ins.mnemonic != 'rsqrtss' or ins.operands[1].type != X.X86_OP_REG:
            continue
        d, copy = xmm_index(ins.operands[0].reg, code.md), None
        at = a + ins.size
        for _ in range(CHAIN_REACH):
            nxt = decoded.get(at)
            if nxt is None or at in targets or at in words or at in marks:
                break
            m = nxt.mnemonic
            if m in STOP or m.startswith('j') or m in ('call', 'loop', 'syscall') or m.startswith(('vzero', 'xrstor', 'fxrstor')):
                break
            read, written = nxt.regs_access()
            watched = {d} if copy is None else {d, copy}
            if any(xmm_index(r, code.md) in watched for r in list(read) + list(written)):
                ops = nxt.operands
                regs = [xmm_index(o.reg, code.md) if o.type == X.X86_OP_REG else None for o in ops]
                if m == 'rcpss' and copy is None and regs == [d, d]:
                    found[a] = (at, CHAIN_END)
                elif m == 'rcpss' and copy is None and len(regs) == 2 and regs[1] == d and regs[0] not in (None, d):
                    found[a] = (at, MOVE_END)
                elif m == 'rcpss' and copy is not None and regs == [copy, d]:
                    found[a] = (at, COPY_END)
                elif m in COPIES and copy is None and len(regs) == 2 and regs[1] == d and regs[0] not in (None, d):
                    copy = regs[0]
                    at += nxt.size
                    continue
                break
            at += nxt.size
    return found


def copyable(ins):
    """None when a window may hold this instruction next to a rewritten one, else why not."""
    if ins.mnemonic in STOP or ins.mnemonic.startswith('j') or ins.mnemonic in ('call', 'loop', 'syscall'):
        return 'branch'
    if X.X86_REG_RSP in ins.regs_access()[1] or ins.mnemonic in ('push', 'pop', 'leave', 'enter'):
        return 'rsp'
    if ins.mnemonic in KINDS and ins.operands[1].type != X.X86_OP_REG:
        return 'memory operand'
    return None


def backward(a, decoded, previous, targets, words, marks, taken):
    """FR27: the window ENDING with the rewritten instruction at a, opened by the fewest whole instructions decoded
    right before it on the same straight-line path (no inner boundary a branch target, function boundary or
    referenced word, none of them already in a window, approximate or a branch); its end, a's successor, may be
    anything since the trampoline jumps back there. Used only where the forward window met an inner target."""
    seq, total, at = [decoded[a]], decoded[a].size, a
    while total < 5:
        if at in targets or at in words or at in marks:
            return None
        p = previous.get(at)
        if p is None or p in taken or decoded[p].mnemonic in APPROX or copyable(decoded[p]):
            return None
        seq.insert(0, decoded[p])
        total += decoded[p].size
        at = p
    return seq, total


def windows(code, decoded, targets, words, marks, busy, chain=None):
    """Windows of whole instructions; chain {rsqrtss: (rcpss, end kind)} gives those halves kinds 3 and 4/5."""
    chain = chain or {}
    special = {a: SQRT for a in chain} | {b: kind for b, kind in chain.values()}
    sites = sorted(a for a, i in decoded.items() if i.mnemonic in KINDS)
    previous = {a + i.size: a for a, i in decoded.items()}
    taken, rows, refused = set(), [], collections.Counter()
    for a in sites:
        if a in taken:
            continue
        seq, total, why = [decoded[a]], decoded[a].size, None
        if seq[0].operands[1].type != X.X86_OP_REG:
            why = 'memory operand'
        while why is None and total < 5:
            nxt = a + total
            ins = decoded.get(nxt)
            if ins is None:
                why = 'gap'
            elif copyable(ins) == 'branch':
                why = 'branch'
            elif nxt in targets or nxt in words or nxt in marks:
                why = 'inner target'
            else:
                why = copyable(ins)
                if why is None:
                    seq.append(ins)
                    total += ins.size
        start = a
        if why == 'inner target':
            back = backward(a, decoded, previous, targets, words, marks, taken)
            if back:
                seq, total = back
                start, why = seq[0].address, None
        if why is None and len(seq) > MAX_INSNS:
            why = 'too many instructions'
        if why is None and any(lo < start + total and start < hi for lo, hi in busy):
            why = 'gate patch reference' if code.base < 0x400000 else 'other patch'
        if why:
            refused[why] += 1
            continue
        a = start
        insns = []
        for ins in seq:
            kind = special.get(ins.address, KINDS.get(ins.mnemonic, 0))
            rip = ins.disp_offset if kind == 0 and 'rip' in ins.op_str else 0
            if kind == 0 and 'rip' in ins.op_str and not rip:
                why = 'rip without displacement'
            insns.append((ins.address - a, ins.size, kind, rip))
            taken.update(range(ins.address, ins.address + ins.size))
        if why:
            refused[why] += 1
            continue
        rows.append((a, total, insns, fnv64(code.bytes[a - code.base:a - code.base + total])))
    covered = sum(sum(1 for i in r[2] if i[2]) for r in rows)
    return rows, refused, len(sites), covered


def rewrite(code, decoded, targets, words, marks, busy):
    """Windows with the chains whose two halves are both in accepted windows."""
    chain = chains(code, decoded, targets, words, marks)
    rows = windows(code, decoded, targets, words, marks, busy)[0]
    accepted = {a + off for a, _, insns, _ in rows for off, _, kind, _ in insns if kind}
    kept = {a: (b, kind) for a, (b, kind) in chain.items() if a in accepted and b in accepted}
    rows, refused, sites, covered = windows(code, decoded, targets, words, marks, busy, kept)
    copied = sum(1 for _, kind in kept.values() if kind == COPY_END)
    moved = sum(1 for _, kind in kept.values() if kind == MOVE_END)
    return rows, refused, sites, covered, (len(kept), copied, moved), len(chain) - len(kept)


def chain_line(kept, dropped):
    total, copied, moved = kept
    return (f' * {total} rcp(rsqrt(x)) chains become sqrt(x)' + (f' ({copied} through a register copy)' if copied else '') +
            (f' ({moved} into another register)' if moved else '') +
            (f' ({dropped} not: a half outside the windows)' if dropped else '') + '.')


def render(rows, refused, sites, covered, linear, kept, dropped, linux=False, roots=(0, 0)):
    extra = ((', 5 rcpss c,r closing a copied chain (r[0] = 1/r[0])' if kept[1] else '') +
             (', 6 rcpss e,r closing a chain into e (movss e,r; r[0] = 1/r[0])' if kept[2] else ''))
    kinds = (' * instruction {offset, length, kind, RIP displacement offset}; kind 0 copy, 1 rcpss, 2 rsqrtss,',
             ' * 3 rsqrtss opening a chain (sqrtss), 4 rcpss closing it (nothing)' + extra + '. */')
    if linux:
        out = ['/* Generated by tools/dev/gen_exact_reciprocal.py for Linux X4 9.00 (SHA-256 113f73d7...045f20). Do not edit.',
               f' * {sites} reachable rcpss/rsqrtss instructions ({linear} by linear sweep of the FDEs); {covered} rewritten in',
               f' * {len(rows)} windows; refused: ' + (', '.join(f'{k} {v}' for k, v in sorted(refused.items())) or 'none') + '.',
               chain_line(kept, dropped),
               ' * Row: window address, length, instruction count, FNV-1a 64 of the original bytes, then per',
               *kinds,
               f'#define XRCP_LINUX_WINDOWS {len(rows)}u',
               f'#define XRCP_LINUX_REWRITTEN {covered}u',
               'static const xrcp_window xrcp_linux_windows[XRCP_LINUX_WINDOWS]={']
    else:
        out = ['/* Generated by tools/dev/gen_exact_reciprocal.py for X4.exe 9.00 (SHA-256 19750a65...6891). Do not edit.',
               f' * {sites} rcpss/rsqrtss instructions reachable from the .pdata roots, {roots[1]} switch tables and {roots[0]}',
               f' * leaf functions ({linear} by linear sweep of the .pdata ranges, which hold no leaf; --inventory lists the rest);',
               f' * {covered} rewritten in {len(rows)} windows; refused: ' +
               (', '.join(f'{k} {v}' for k, v in sorted(refused.items())) or 'none') + '.',
               chain_line(kept, dropped),
               ' * Row: window RVA, length, instruction count, FNV-1a 64 of the original bytes, then per',
               *kinds,
               f'#define XRCP_WINDOWS {len(rows)}u',
               f'#define XRCP_REWRITTEN {covered}u',
               'static const xrcp_window xrcp_windows[XRCP_WINDOWS]={']
    for a, total, insns, digest in rows:
        parts = ','.join('{%d,%d,%d,%d}' % i for i in insns)
        out.append('    {0x%xu,%d,%d,0x%016xULL,{%s}},' % (a, total, len(insns), digest, parts))
    out.append('};')
    return '\n'.join(out) + '\n'


def linear_count(code, spans):
    count = 0
    for b, e in spans:
        for ins in code.md.disasm(code.bytes[b - code.base:e - code.base], b):
            if ins.mnemonic in KINDS:
                count += 1
    return count


# ---------------------------------------------------------------- Windows
def leaf_end(pe, code, rva):
    """End of the code after rva that no .pdata entry covers: the next entry's begin (or the end of .text)."""
    i = bisect.bisect_right(pe._starts, rva)
    return pe._starts[i] if i < len(pe._starts) else code.base + code.size


def gpr(name):
    """The 64-bit register a general-purpose register name belongs to (eax -> rax, r8d -> r8)."""
    if re.fullmatch(r'e[a-z]{2}', name or ''):
        return 'r' + name[1:]
    if re.fullmatch(r'r\d+[dwb]', name or ''):
        return name[:-1]
    return name


def msvc_jump_tables(pe, code, followed):
    """FR27: descent callback for MSVC x64 switches, `cmp i, N; ja default; (cdqe | movsxd r, i | mov r, i |
    lea b, [__ImageBase])*; mov r32, [b + r*4 + T]; add r64, b; jmp r64`, whose table at RVA T holds N+1 int32 RVAs.
    The arms are followed only when the whole pattern holds, the index is the compared value, nothing else writes it
    or the base in between, and all N+1 entries land in the function's own ranges (the base is then the image base);
    otherwise the switch stays unreached (--inventory lists what that leaves). followed {mov: (T, N+1)}."""
    def tables(ins, spans, run):
        if ins.mnemonic != 'mov' or len(ins.operands) != 2:
            return ()
        dst, src = ins.operands
        if dst.type != X.X86_OP_REG or src.type != X.X86_OP_MEM or src.size != 4 or src.mem.scale != 4 or \
                not src.mem.base or not src.mem.index or src.mem.segment or src.mem.disp <= 0:
            return ()
        name = ins.reg_name
        base, index, out = gpr(name(src.mem.base)), gpr(name(src.mem.index)), gpr(name(dst.reg))
        if base in ('rip', 'rsp'):
            return ()
        add = code.insn(ins.address + ins.size)
        jump = add and code.insn(add.address + add.size)
        if not (add and add.mnemonic == 'add' and add.op_str == f'{out}, {base}' and jump and jump.mnemonic == 'jmp' and
                jump.op_str == out):
            return ()
        count, related = None, set()
        for k in range(len(run) - 1, 0, -1):
            cmp, ja = run[k - 1], run[k]
            if ja.mnemonic != 'ja' or cmp.mnemonic != 'cmp' or cmp.operands[0].type != X.X86_OP_REG or \
                    cmp.operands[1].type != X.X86_OP_IMM:
                continue
            related = {gpr(name(cmp.operands[0].reg))}
            for b in run[k + 1:]:
                ops = b.operands
                if b.mnemonic == 'cdqe' and 'rax' in related:
                    continue
                if b.mnemonic in ('movsxd', 'mov') and len(ops) == 2 and all(o.type == X.X86_OP_REG for o in ops) and \
                        gpr(name(ops[1].reg)) in related:
                    related.add(gpr(name(ops[0].reg)))
                    continue
                if b.mnemonic == 'lea' and ops[0].type == X.X86_OP_REG and gpr(name(ops[0].reg)) == base and \
                        ops[1].mem.base == X.X86_REG_RIP and b.address + b.size + ops[1].mem.disp == 0:
                    continue
                if {gpr(name(r)) for r in b.regs_access()[1]} & (related | {base}):
                    related = set()
                    break
            count = cmp.operands[1].imm + 1
            break
        if count is None or index not in related or not 0 < count <= 4096:
            return ()
        raw = pe.read(src.mem.disp, 4 * count)
        if not raw or len(raw) != 4 * count:
            return ()
        entries = struct.unpack(f'<{count}I', raw)
        if not all(any(b <= t < e for b, e in spans) for t in entries):
            return ()
        followed[ins.address] = (src.mem.disp, count)
        return entries
    return tables


def windows_descent(pe, code):
    """FR27: every function's reachable instructions: the .pdata roots with their MSVC switch arms, then the leaf
    functions (no unwind data, hence no .pdata entry) to a fixed point. A leaf start is a 16-byte aligned address
    after int3 padding or a ret, outside every .pdata range, that a decoded direct call or jmp targets or an 8-byte
    pointer in the image's data names (vtables, callbacks); a leaf is decoded from its start to the next .pdata
    function. Returns (decoded, targets, leaves, followed switch tables)."""
    import numpy as np
    followed = {}
    tables = msvc_jump_tables(pe, code, followed)
    functions = [([root], pe.ranges(root)) for root in sorted({r for _, _, r in pe.entries})]
    decoded, targets = descend(code, functions, tables)
    lo, hi = code.base, code.base + code.size
    named = set()
    for name, va, size, off, raw_size, flags in pe.sections:
        if name == '.text':
            continue
        q = np.frombuffer(pe.data[off:off + raw_size // 8 * 8], dtype='<u8')
        named.update((q[(q >= 0x140000000 + lo) & (q < 0x140000000 + hi)] - 0x140000000).tolist())

    def leaf_start(s):
        return lo < s < hi and s % 16 == 0 and not pe.function_of(s) and code.bytes[s - 1 - lo] in (0xcc, 0xc3)

    def direct(instructions):
        out = set()
        for ins in instructions:
            if ins.mnemonic in ('call', 'jmp') and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                out.add(ins.operands[0].imm)
        return out
    leaves, fresh = set(), {s for s in named | direct(decoded.values()) if leaf_start(s)}
    while fresh:
        leaves |= fresh
        more, more_targets = descend(code, [([s], [(s, leaf_end(pe, code, s))]) for s in sorted(fresh)], tables)
        new = {a: i for a, i in more.items() if a not in decoded}
        decoded.update(new)
        targets |= more_targets
        fresh = {s for s in direct(new.values()) if leaf_start(s)} - leaves
    for root in FR26_ROOTS:
        if root not in decoded:
            raise SystemExit(f'FR26 root {root:#x} not reached by the descent')
    return decoded, targets, leaves, followed


def windows_image(path):
    pe = lm.PeImage(lm.verify_build(path, lm.WINDOWS_BUILD))
    text = [s for s in pe.sections if s[0] == '.text'][0]
    return pe, Code(text[1], pe.read(text[1], text[4]))


def windows_tables(path):
    pe, code = windows_image(path)
    decoded, targets, leaves, followed = windows_descent(pe, code)
    pins, refs, unresolved = lm.extract_sites(lm.gate_files(ROOT / 'src/native/engine'))
    busy = sorted((r['rva'] - 16, r['rva'] + 16) for r in refs if r['rva'] is not None)
    marks = {b for b, _, _ in pe.entries} | {e for _, e, _ in pe.entries} | leaves
    words = referenced_words(pe, code)
    table = pe.read(*pe.exception)
    unwinds = {struct.unpack_from('<I', table, i + 8)[0] for i in range(0, len(table) - 11, 12)}
    for value, at in WINDOWS_WORD_COINCIDENCES:
        word = struct.pack('<I', value)
        if pe.read(at, 4) != word or pe.data.count(word) != 1 or at not in unwinds:
            raise SystemExit(f'{value:#x} at {at:#x} is not a lone UNWIND_INFO header')
        words.discard(value)
    rows, refused, sites, covered, kept, dropped = rewrite(code, decoded, targets, words, marks, busy)
    return render(rows, refused, sites, covered, linear_count(code, [(b, e) for b, e, _ in pe.entries]), kept, dropped,
                  roots=(len(leaves), len(followed))), rows


def windows_inventory(path, rows):
    """Every approximate reciprocal instruction of X4.exe the table does not rewrite: a linear sweep of each .pdata range,
    and a descent from every direct call/jmp target and 8-byte data pointer that lies outside all of them (leaf
    functions). Each row: address, instruction, why ('unreached': a .pdata function's code the descent never decodes,
    i.e. a switch arm behind a jump table; 'refused': decoded, no window; 'leaf': no unwind data) and its function."""
    import numpy as np
    pe, code = windows_image(path)
    covered = {a + off for a, _, insns, _ in rows for off, _, kind, _ in insns if kind}
    decoded, _, _, _ = windows_descent(pe, code)
    found = {}
    for b, e, root in pe.entries:
        for ins in code.md.disasm(code.bytes[b - code.base:e - code.base], b):
            if ins.mnemonic in APPROX:
                found[ins.address] = (ins, 'refused' if ins.address in decoded else 'unreached', root)
    raw = np.frombuffer(code.bytes, dtype=np.uint8)
    at = np.nonzero((raw[:-4] == 0xe8) | (raw[:-4] == 0xe9))[0]
    rel = raw[at + 1].astype(np.int64) | raw[at + 2].astype(np.int64) << 8 | raw[at + 3].astype(np.int64) << 16 | \
        raw[at + 4].astype(np.int64) << 24
    starts = set((at + code.base + 5 + np.where(rel >= 2 ** 31, rel - 2 ** 32, rel)).tolist())
    for name, va, size, off, raw_size, flags in pe.sections:
        words = np.frombuffer(pe.data[off:off + raw_size // 8 * 8], dtype='<u8')
        starts.update((words[(words >= 0x140000000 + code.base) & (words < 0x140000000 + code.base + code.size)] -
                       0x140000000).tolist())
    leaves = sorted(s for s in starts if code.base <= s < code.base + code.size and not pe.function_of(s) and
                    code.bytes[s - 1 - code.base] in (0xcc, 0xc3))
    for s in leaves:
        leaf, _ = descend(code, [([s], [(s, leaf_end(pe, code, s))])])
        for a, ins in leaf.items():
            if ins.mnemonic in APPROX and a not in found:
                found[a] = (ins, 'leaf', s)
    out = []
    for a in sorted(found):
        ins, why, fn = found[a]
        if a not in covered:
            out.append(f'{a:#x} {ins.mnemonic} {ins.op_str}  {why}  function {fn:#x}')
    return out


# ---------------------------------------------------------------- Linux
def landing_pads(elf):
    """Every C++ landing pad of the ELF (FDE augmentation 'L' -> LSDA call-site tables)."""
    frame_s = elf.sections['.eh_frame']
    frame, frame_addr = elf.section_bytes('.eh_frame'), frame_s['addr']
    pads, at, cies = set(), 0, {}
    while at + 4 <= len(frame):
        length = struct.unpack_from('<I', frame, at)[0]
        if length == 0:
            break
        cie_ptr = struct.unpack_from('<I', frame, at + 4)[0]
        if cie_ptr == 0:
            cies[at] = parse_cie(frame, at)
        else:
            cie = cies.get(at + 4 - cie_ptr) or parse_cie(frame, at + 4 - cie_ptr)
            fde_enc, lsda_enc = cie
            begin, p = lm._encoded(frame, at + 8, fde_enc, frame_addr)
            _, p = lm._encoded(frame, p, fde_enc & 0x0f, frame_addr)
            if lsda_enc is not None:
                _, p = lm._uleb(frame, p)
                if lsda_enc != 0xff:
                    lsda, _ = lm._encoded(frame, p, lsda_enc, frame_addr)
                    if lsda:
                        pads.update(lsda_pads(elf, lsda, begin))
        at += 4 + length
    return pads


def parse_cie(frame, at):
    version, p = frame[at + 8], at + 9
    end = frame.index(b'\0', p)
    aug, p = frame[p:end].decode('ascii'), end + 1
    _, p = lm._uleb(frame, p)
    _, p = lm._uleb(frame, p)
    p = p + 1 if version == 1 else lm._uleb(frame, p)[1]
    fde_enc, lsda_enc = 0, None
    if aug.startswith('z'):
        _, p = lm._uleb(frame, p)
        for ch in aug[1:]:
            if ch == 'R':
                fde_enc = frame[p]; p += 1
            elif ch == 'L':
                lsda_enc = frame[p]; p += 1
            elif ch == 'P':
                enc = frame[p]; _, p = lm._encoded(frame, p + 1, enc & 0x0f, 0)
    return fde_enc, lsda_enc


def lsda_pads(elf, lsda, function_start):
    blob = None
    for vaddr, offset, filesz, _ in elf.segments:
        if vaddr <= lsda < vaddr + filesz:
            blob = elf.data[offset + lsda - vaddr:offset + filesz]
    if not blob:
        return set()
    p, lp_enc = 1, blob[0]
    lp_start = function_start
    if lp_enc != 0xff:
        lp_start, p = lm._encoded(blob, p, lp_enc, lsda)
    tt_enc = blob[p]; p += 1
    if tt_enc != 0xff:
        _, p = lm._uleb(blob, p)
    cs_enc = blob[p]; p += 1
    cs_len, p = lm._uleb(blob, p)
    end, pads = p + cs_len, set()
    while p < end:
        if cs_enc & 0x0f == 0x01:  # call site start, length (unused), landing pad
            _, p = lm._uleb(blob, p)
            _, p = lm._uleb(blob, p)
            pad, p = lm._uleb(blob, p)
        else:
            n = {0x03: 4, 0x0b: 4, 0x04: 8, 0x0c: 8}[cs_enc & 0x0f]
            pad = struct.unpack_from('<I' if n == 4 else '<Q', blob, p + 2 * n)[0]
            p += 3 * n
        _, p = lm._uleb(blob, p)
        if pad:
            pads.add(lp_start + pad)
    return pads


def agent_busy():
    text = AGENT_PINS.read_text()
    return [(int(va, 16), int(va, 16) + int(n, 16)) for va, n in re.findall(r'va: (0x[0-9a-f]+), len: (0x[0-9a-f]+)', text)]


def linux_tables(path):
    data = lm.verify_build(path, lm.LINUX_BUILD)
    elf = lm.ElfImage(data)
    text = elf.sections['.text']
    code = Code(text['addr'], elf.section_bytes('.text'))
    rodata = [(s['addr'], s['addr'] + s['size']) for n, s in elf.sections.items() if s['addr'] and n.startswith('.rodata')]
    pads = landing_pads(elf)
    jump_targets = set()

    def tables(ins, spans, run):
        """lea reg, [rip+T] with T in .rodata: T plus int32 entries that land in the function."""
        if ins.mnemonic != 'lea' or 'rip' not in ins.op_str:
            return ()
        t = ins.address + ins.size + ins.disp
        if not any(lo <= t < hi for lo, hi in rodata):
            return ()
        found = []
        for i in range(4096):
            raw = elf.read(t + 4 * i, 4)
            if raw is None:
                break
            target = t + struct.unpack('<i', raw)[0]
            if not any(b <= target < e for b, e in spans):
                break
            found.append(target)
        jump_targets.update(found)
        return found
    functions = []
    for b, e in elf.functions:
        roots = [b] + [p for p in pads if b <= p < e]
        functions.append((roots, [(b, e)]))
    decoded, targets = descend(code, functions, tables)
    targets |= pads | jump_targets
    import numpy as np
    pointers = set()
    for name, s in elf.sections.items():
        if s['addr'] and s['type'] != 8 and name.startswith(('.data', '.rodata', '.init_array', '.fini_array', '.got')):
            raw = np.frombuffer(elf.section_bytes(name)[:s['size'] // 8 * 8], dtype='<u8')
            pointers.update(np.unique(raw[(raw >= code.base) & (raw < code.base + code.size)]).tolist())
    marks = {b for b, _ in elf.functions} | {e for _, e in elf.functions}
    busy = agent_busy() + [(0x1a3b1a0, 0x1a3b53f), (0x1a3ac40, 0x1a3afff)]
    rows, refused, sites, covered, kept, dropped = rewrite(code, decoded, targets, pointers, marks, busy)
    return render(rows, refused, sites, covered, linear_count(code, elf.functions), kept, dropped, linux=True), rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--windows', type=Path)
    group.add_argument('--linux', type=Path)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--inventory', action='store_true')
    args = parser.parse_args()
    text, rows = windows_tables(args.windows) if args.windows else linux_tables(args.linux)
    if args.inventory:
        if not args.windows:
            parser.error('--inventory needs --windows')
        lines = windows_inventory(args.windows, rows)
        print('\n'.join(lines))
        print(f'{len(lines)} approximate instructions not rewritten')
        return
    header = HEADER if args.windows else LINUX_HEADER
    if args.check:
        if header.read_text() != text:
            raise SystemExit(f'{header} differs from the generator output')
        print(f'{header.name} matches ({len(rows)} windows)')
        return
    header.write_text(text, newline='\n')
    print(f'{header}: {len(rows)} windows')
    print('\n'.join(text.splitlines()[:3]))


if __name__ == '__main__':
    main()
