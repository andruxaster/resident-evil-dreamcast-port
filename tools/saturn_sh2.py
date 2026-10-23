#!/usr/bin/env python3
"""An SH-2 (SH7604) 16-bit disassembler for the Saturn modules.

The opcode table is the one in the Hitachi *SH-1/SH-2 Programming Manual*
(section 5, "Instruction Code" columns). Every group is decoded by full bit
pattern -- no field is guessed from how the Saturn code happens to use it.

**History, because it matters for reading older notes.** An earlier version
of this file was written the other way round: its table was adjusted until
the Saturn code "made sense", and its self-test vectors were written to agree
with it. It was badly wrong in the commonest words of a `.PRG`:

* `4n0B` is `JSR @Rn` (the commonest word in a module -- every call). The old
  file listed it as `mov.b rN,@r0`, a store, so every call looked like a
  memory write.
* `C8` is `TST #imm,R0` and `C9` is `AND #imm,R0`; the old file had `C8` as
  `cmp/eq` and `C9` as `tst`.
* Group 3 was mis-keyed (`3nm3` is `CMP/GE`, `3nm6` is `CMP/HI`, `3nm8` is
  `SUB`, `3nmA` is `SUBC`, `3nmB` is `SUBV`, `3nmE` is `ADDC`) and `DT` is
  `4n10`, not `3n05` (`3nm5` is `DMULU.L`).
* Group 2: `2nmC` is `CMP/STR`, `2nmD` `XTRCT`, `2nmE` `MULU.W`, `2nmF`
  `MULS.W`. The old claim that "Ghidra reads `0x251E` as `CMP/HI`" came from
  the old table, not from Ghidra -- `CMP/HI` is `3nm6`.
* Conditional branches take a **signed 8-bit** displacement (the old file
  used 7 bits), `BRA`/`BSR` a **signed 12-bit** one (the old file used an
  unsigned 8-bit one), and `MOV.B/W @(disp,Rm),R0` (`84`/`85`) has Rm in bits
  7-4 and the displacement in bits 3-0 (the old file used an 8-bit disp).
* `MOV.L Rm,@(disp,Rn)` (`1nmd`) and `MOV.L @(disp,Rm),Rn` (`5nmd`) scale
  the displacement by 4; the old file printed the raw nibble.

So anything in `docs/SATURN_VERSION_ANALYSIS.md` sections 4c-4f that was read off
a listing -- rather than confirmed by execution or by table contents -- has to
be re-read with this version. The same goes for the claims that Ghidra's
SLEIGH "mis-renders" stores and multiplies: they were never established
against the manual.

Things that are easy to get wrong, all of them per the manual:

* `MOV.L @(disp,PC),Rn` reads `(PC & ~3) + 4 + disp*4`; `MOV.W @(disp,PC),Rn`
  reads `PC + 4 + disp*2` and sign-extends the halfword; `MOVA` is like
  `MOV.L`. Branch targets are `PC + 4 + disp*2`.
* `BT/S`, `BF/S`, `BRA`, `BSR`, `BRAF`, `BSRF`, `JMP`, `JSR`, `RTS`, `RTE`
  have a **delay slot**: the next instruction runs before the branch.
* `MOV #imm`, `ADD #imm`, `CMP/EQ #imm` sign-extend; `TST/AND/XOR/OR #imm`
  zero-extend.

Usage
-----

    python tools/saturn_sh2.py GAME5.PRG 0x0602DDF4 0x0602DE3A
    python tools/saturn_sh2.py GAME5.PRG 0x0602DDF4 --count 48
    python tools/saturn_sh2.py GAME5.PRG 0x0602B000 0x060A0000 --xref 0x06088CC0
    python tools/saturn_sh2.py GAME5.PRG 0x0602B040 --coherent 0x100
    python tools/saturn_sh2.py --selftest

PC-relative loads print the slot address and, when it is inside the image,
the value stored there (`[slot] = value`). `--xref ADDR` lists every
PC-relative load whose *slot* is ADDR, or whose loaded *value* is ADDR with
`--value`. `--coherent BYTES` sweeps linearly and reports undecodable words
and branches landing outside the range.
"""

import argparse
import os
import sys

PRG_BASE = 0x0602B000
BIN_BASE = 0x06004000


def _sx(value, bits):
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


# group 0x0, key = low nibble, "Rm,Rn" order in the template
_OP0_NM = {
    0x4: "mov.b r{m},@(r0,r{n})", 0x5: "mov.w r{m},@(r0,r{n})",
    0x6: "mov.l r{m},@(r0,r{n})", 0x7: "mul.l r{m},r{n}",
    0xC: "mov.b @(r0,r{m}),r{n}", 0xD: "mov.w @(r0,r{m}),r{n}",
    0xE: "mov.l @(r0,r{m}),r{n}", 0xF: "mac.l @r{m}+,@r{n}+",
}
# group 0x0, key = low byte, register = n
_OP0_N = {
    0x02: "stc sr,r{n}", 0x12: "stc gbr,r{n}", 0x22: "stc vbr,r{n}",
    0x0A: "sts mach,r{n}", 0x1A: "sts macl,r{n}", 0x2A: "sts pr,r{n}",
    0x29: "movt r{n}",
}
_OP0_FIXED = {
    0x0008: "clrt", 0x0009: "nop", 0x000B: "rts", 0x0018: "sett",
    0x0019: "div0u", 0x001B: "sleep", 0x0028: "clrmac", 0x002B: "rte",
}

_OP2 = {
    0x0: "mov.b r{m},@r{n}", 0x1: "mov.w r{m},@r{n}", 0x2: "mov.l r{m},@r{n}",
    0x4: "mov.b r{m},@-r{n}", 0x5: "mov.w r{m},@-r{n}",
    0x6: "mov.l r{m},@-r{n}", 0x7: "div0s r{m},r{n}", 0x8: "tst r{m},r{n}",
    0x9: "and r{m},r{n}", 0xA: "xor r{m},r{n}", 0xB: "or r{m},r{n}",
    0xC: "cmp/str r{m},r{n}", 0xD: "xtrct r{m},r{n}",
    0xE: "mulu.w r{m},r{n}", 0xF: "muls.w r{m},r{n}",
}

_OP3 = {
    0x0: "cmp/eq r{m},r{n}", 0x2: "cmp/hs r{m},r{n}", 0x3: "cmp/ge r{m},r{n}",
    0x4: "div1 r{m},r{n}", 0x5: "dmulu.l r{m},r{n}", 0x6: "cmp/hi r{m},r{n}",
    0x7: "cmp/gt r{m},r{n}", 0x8: "sub r{m},r{n}", 0xA: "subc r{m},r{n}",
    0xB: "subv r{m},r{n}", 0xC: "add r{m},r{n}", 0xD: "dmuls.l r{m},r{n}",
    0xE: "addc r{m},r{n}", 0xF: "addv r{m},r{n}",
}

# group 0x4, key = low byte, register = n
_OP4 = {
    0x00: "shll r{n}", 0x01: "shlr r{n}", 0x02: "sts.l mach,@-r{n}",
    0x03: "stc.l sr,@-r{n}", 0x04: "rotl r{n}", 0x05: "rotr r{n}",
    0x06: "lds.l @r{n}+,mach", 0x07: "ldc.l @r{n}+,sr", 0x08: "shll2 r{n}",
    0x09: "shlr2 r{n}", 0x0A: "lds r{n},mach", 0x0B: "jsr @r{n}",
    0x0E: "ldc r{n},sr",
    0x10: "dt r{n}", 0x11: "cmp/pz r{n}", 0x12: "sts.l macl,@-r{n}",
    0x13: "stc.l gbr,@-r{n}", 0x15: "cmp/pl r{n}", 0x16: "lds.l @r{n}+,macl",
    0x17: "ldc.l @r{n}+,gbr", 0x18: "shll8 r{n}", 0x19: "shlr8 r{n}",
    0x1A: "lds r{n},macl", 0x1B: "tas.b @r{n}", 0x1E: "ldc r{n},gbr",
    0x20: "shal r{n}", 0x21: "shar r{n}", 0x22: "sts.l pr,@-r{n}",
    0x23: "stc.l vbr,@-r{n}", 0x24: "rotcl r{n}", 0x25: "rotcr r{n}",
    0x26: "lds.l @r{n}+,pr", 0x27: "ldc.l @r{n}+,vbr", 0x28: "shll16 r{n}",
    0x29: "shlr16 r{n}", 0x2A: "lds r{n},pr", 0x2B: "jmp @r{n}",
    0x2E: "ldc r{n},vbr",
}

_OP6 = {
    0x0: "mov.b @r{m},r{n}", 0x1: "mov.w @r{m},r{n}", 0x2: "mov.l @r{m},r{n}",
    0x3: "mov r{m},r{n}", 0x4: "mov.b @r{m}+,r{n}", 0x5: "mov.w @r{m}+,r{n}",
    0x6: "mov.l @r{m}+,r{n}", 0x7: "not r{m},r{n}", 0x8: "swap.b r{m},r{n}",
    0x9: "swap.w r{m},r{n}", 0xA: "negc r{m},r{n}", 0xB: "neg r{m},r{n}",
    0xC: "extu.b r{m},r{n}", 0xD: "extu.w r{m},r{n}",
    0xE: "exts.b r{m},r{n}", 0xF: "exts.w r{m},r{n}",
}

_BRANCH8 = {0x89: "bt", 0x8B: "bf", 0x8D: "bt/s", 0x8F: "bf/s"}

# kinds: "" plain, "branch" (target), "call", "ret", "jump" (register), "bad"


def decode(code, addr):
    """Decode one halfword at `addr`. Returns (text, kind, target)."""
    op = code >> 12
    n = (code >> 8) & 0xF
    m = (code >> 4) & 0xF
    lo = code & 0xF
    d8 = code & 0xFF
    sub8 = code >> 8
    pc4 = addr + 4

    def f(t):
        return t.format(n=n, m=m)

    if op == 0x0:
        if code in _OP0_FIXED:
            t = _OP0_FIXED[code]
            return t, ("ret" if t in ("rts", "rte") else ""), 0
        if lo in _OP0_NM:
            return f(_OP0_NM[lo]), "", 0
        if d8 in _OP0_N:
            return f(_OP0_N[d8]), "", 0
        if d8 == 0x03:
            return f("bsrf r{n}"), "call", 0
        if d8 == 0x23:
            return f("braf r{n}"), "jump", 0
        return "db? 0x%04X" % code, "bad", 0

    if op == 0x1:
        return "mov.l r%d,@(%d,r%d)" % (m, lo * 4, n), "", 0

    if op == 0x2:
        if lo in _OP2:
            return f(_OP2[lo]), "", 0
        return "db? 0x%04X" % code, "bad", 0

    if op == 0x3:
        if lo in _OP3:
            return f(_OP3[lo]), "", 0
        return "db? 0x%04X" % code, "bad", 0

    if op == 0x4:
        if lo == 0xF:
            return f("mac.w @r{m}+,@r{n}+"), "", 0
        if d8 in _OP4:
            t = f(_OP4[d8])
            kind = "call" if d8 == 0x0B else ("jump" if d8 == 0x2B else "")
            return t, kind, 0
        return "db? 0x%04X" % code, "bad", 0

    if op == 0x5:
        return "mov.l @(%d,r%d),r%d" % (lo * 4, m, n), "", 0

    if op == 0x6:
        return f(_OP6[lo]), "", 0

    if op == 0x7:
        return "add #%d,r%d" % (_sx(d8, 8), n), "", 0

    if op == 0x8:
        if sub8 in _BRANCH8:
            target = pc4 + _sx(d8, 8) * 2
            return "%s 0x%08X" % (_BRANCH8[sub8], target), "branch", target
        if sub8 == 0x80:
            return "mov.b r0,@(%d,r%d)" % (lo, m), "", 0
        if sub8 == 0x81:
            return "mov.w r0,@(%d,r%d)" % (lo * 2, m), "", 0
        if sub8 == 0x84:
            return "mov.b @(%d,r%d),r0" % (lo, m), "", 0
        if sub8 == 0x85:
            return "mov.w @(%d,r%d),r0" % (lo * 2, m), "", 0
        if sub8 == 0x88:
            return "cmp/eq #%d,r0" % _sx(d8, 8), "", 0
        return "db? 0x%04X" % code, "bad", 0

    if op == 0x9:
        return "mov.w @(0x%X,pc),r%d" % (d8 * 2, n), "", 0

    if op in (0xA, 0xB):
        target = pc4 + _sx(code & 0xFFF, 12) * 2
        return ("bra" if op == 0xA else "bsr") + " 0x%08X" % target, \
            ("branch" if op == 0xA else "call"), target

    if op == 0xC:
        t = {
            0xC0: "mov.b r0,@(%d,gbr)" % d8,
            0xC1: "mov.w r0,@(%d,gbr)" % (d8 * 2),
            0xC2: "mov.l r0,@(%d,gbr)" % (d8 * 4),
            0xC3: "trapa #%d" % d8,
            0xC4: "mov.b @(%d,gbr),r0" % d8,
            0xC5: "mov.w @(%d,gbr),r0" % (d8 * 2),
            0xC6: "mov.l @(%d,gbr),r0" % (d8 * 4),
            0xC7: "mova @(0x%X,pc),r0" % (d8 * 4),
            0xC8: "tst #0x%02X,r0" % d8,
            0xC9: "and #0x%02X,r0" % d8,
            0xCA: "xor #0x%02X,r0" % d8,
            0xCB: "or #0x%02X,r0" % d8,
            0xCC: "tst.b #0x%02X,@(r0,gbr)" % d8,
            0xCD: "and.b #0x%02X,@(r0,gbr)" % d8,
            0xCE: "xor.b #0x%02X,@(r0,gbr)" % d8,
            0xCF: "or.b #0x%02X,@(r0,gbr)" % d8,
        }[sub8]
        return t, "", 0

    if op == 0xD:
        return "mov.l @(0x%X,pc),r%d" % (d8 * 4, n), "", 0

    if op == 0xE:
        return "mov #%d,r%d" % (_sx(d8, 8), n), "", 0

    return "db? 0x%04X" % code, "bad", 0


def pc_load(code, addr):
    """(slot address, size) of a PC-relative load/MOVA, or None."""
    op = code >> 12
    d8 = code & 0xFF
    if op == 0x9:
        return addr + 4 + d8 * 2, 2
    if op == 0xD:
        return ((addr + 4) & ~3) + d8 * 4, 4
    if code >> 8 == 0xC7:
        return ((addr + 4) & ~3) + d8 * 4, 0      # MOVA: the address itself
    return None


def read_slot(data, base, slot, size):
    off = slot - base
    if size == 0:
        return slot
    if off < 0 or off + size > len(data):
        return None
    v = int.from_bytes(data[off:off + size], "big")
    if size == 2:
        v = _sx(v, 16) & 0xFFFFFFFF
    return v


def annotate(data, base, addr, code, text):
    pl = pc_load(code, addr)
    if pl is None:
        return text
    slot, size = pl
    v = read_slot(data, base, slot, size)
    if size == 0:
        return "%s  ; r0 = 0x%08X" % (text, slot)
    if v is None:
        return "%s  ; [0x%08X]" % (text, slot)
    return "%s  ; [0x%08X] = 0x%08X" % (text, slot, v)


def sweep(data, base, start, length):
    out = []
    addr, end = start, start + length
    while addr < end:
        off = addr - base
        if off < 0 or off + 2 > len(data):
            break
        code = int.from_bytes(data[off:off + 2], "big")
        text, kind, target = decode(code, addr)
        out.append((addr, code, text, kind, target))
        addr += 2
    return out


def xrefs(data, base, start, length, want, by_value=False):
    hits = []
    for addr, code, text, kind, _ in sweep(data, base, start, length):
        pl = pc_load(code, addr)
        if pl is None:
            continue
        slot, size = pl
        key = read_slot(data, base, slot, size) if by_value else slot
        if key == want:
            hits.append((addr, code, annotate(data, base, addr, code, text)))
    return hits


def coherent(name, data, base, start, length):
    rows = sweep(data, base, start, length)
    bad = [(a, c, t) for a, c, t, k, _ in rows if k == "bad"]
    branches = [(a, tg) for a, c, t, k, tg in rows if k in ("branch", "call") and tg]
    miss = [(a, tg) for a, tg in branches if tg < start or tg >= start + length]
    print("%s  0x%08X..0x%08X  %d halfword(s)"
          % (name, start, start + length, len(rows)))
    print("  undecodable words : %d" % len(bad))
    for a, c, t in bad[:24]:
        print("    0x%08X  %04X  %s" % (a, c, t))
    print("  branches          : %d" % len(branches))
    print("  outside the range : %d" % len(miss))
    for a, tg in miss[:24]:
        print("    from 0x%08X -> 0x%08X" % (a, tg))
    return 0 if not bad and not miss else 1


# --------------------------------------------------------------------------
# self test. Each vector is an instruction's encoding worked out from the
# manual's bit pattern (e.g. JSR @Rn = 0100nnnn00001011 -> r1 = 0x410B);
# the address used for PC-relative forms is 0x06000000.
# --------------------------------------------------------------------------

ENCODINGS = [
    (0x0009, "nop"), (0x000B, "rts"), (0x0008, "clrt"), (0x0018, "sett"),
    (0x0019, "div0u"), (0x0028, "clrmac"), (0x002B, "rte"),
    (0x0116, "mov.l r1,@(r0,r1)"), (0x00EC, "mov.b @(r0,r14),r0"),
    (0x0217, "mul.l r1,r2"), (0x021A, "sts macl,r2"), (0x0F2A, "sts pr,r15"),
    (0x0329, "movt r3"), (0x0103, "bsrf r1"), (0x0223, "braf r2"),
    (0x1EE6, "mov.l r14,@(24,r14)"),
    (0x2212, "mov.l r1,@r2"), (0x2FE6, "mov.l r14,@-r15"),
    (0x2118, "tst r1,r1"), (0x2219, "and r1,r2"), (0x291B, "or r1,r9"),
    (0x251E, "mulu.w r1,r5"), (0x251F, "muls.w r1,r5"),
    (0x3100, "cmp/eq r0,r1"), (0x3106, "cmp/hi r0,r1"),
    (0x3103, "cmp/ge r0,r1"), (0x3107, "cmp/gt r0,r1"),
    (0x317C, "add r7,r1"), (0x3178, "sub r7,r1"), (0x3205, "dmulu.l r0,r2"),
    (0x4210, "dt r2"), (0x410B, "jsr @r1"), (0x402B, "jmp @r0"),
    (0x4000, "shll r0"), (0x4001, "shlr r0"), (0x4109, "shlr2 r1"),
    (0x4118, "shll8 r1"), (0x4128, "shll16 r1"), (0x4121, "shar r1"),
    (0x4011, "cmp/pz r0"), (0x4015, "cmp/pl r0"),
    (0x4F22, "sts.l pr,@-r15"), (0x4F26, "lds.l @r15+,pr"),
    (0x5261, "mov.l @(4,r6),r2"),
    (0x6EF6, "mov.l @r15+,r14"), (0x6123, "mov r2,r1"),
    (0x628C, "extu.b r8,r2"), (0x611E, "exts.b r1,r1"),
    (0x611F, "exts.w r1,r1"), (0x6120, "mov.b @r2,r1"),
    (0x7101, "add #1,r1"), (0x71FF, "add #-1,r1"),
    (0x8012, "mov.b r0,@(2,r1)"), (0x8411, "mov.b @(1,r1),r0"),
    (0x8513, "mov.w @(6,r1),r0"), (0x88FF, "cmp/eq #-1,r0"),
    (0x8901, "bt 0x06000006"), (0x8BFE, "bf 0x06000000"),
    (0x8D80, "bt/s 0x05FFFF04"), (0x8F03, "bf/s 0x0600000A"),
    (0xA10B, "bra 0x0600021A"), (0xAFFE, "bra 0x06000000"),
    (0xB800, "bsr 0x05FFF004"),
    (0xC801, "tst #0x01,r0"), (0xC9FD, "and #0xFD,r0"),
    (0xCB02, "or #0x02,r0"), (0xC701, "mova @(0x4,pc),r0"),
    (0xC410, "mov.b @(16,gbr),r0"),
    (0xD241, "mov.l @(0x104,pc),r2"), (0x9045, "mov.w @(0x8A,pc),r0"),
    (0xE1FF, "mov #-1,r1"),
]


def selftest():
    bad = 0
    for code, want in ENCODINGS:
        text, _, _ = decode(code, 0x06000000)
        ok = text == want
        bad += not ok
        print("  %s  0x%04X  %-26s %s" % ("ok  " if ok else "FAIL", code, text,
                                         "" if ok else "want " + want))
    print("\n%s  %d/%d" % ("SELFTEST PASS" if bad == 0 else "SELFTEST FAIL",
                          len(ENCODINGS) - bad, len(ENCODINGS)))
    return 0 if bad == 0 else 1


def resolve(path):
    if os.path.isfile(path):
        return path
    for root in ("assets/SATURN_ISO_CONTENT", "assets/SATURN_RAM_DUMP", "."):
        cand = os.path.join(root, os.path.basename(path))
        if os.path.isfile(cand):
            return cand
    return None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module", nargs="?")
    ap.add_argument("start", nargs="?", type=lambda s: int(s, 0))
    ap.add_argument("end", nargs="?", type=lambda s: int(s, 0))
    ap.add_argument("--count", type=int, default=0x40,
                    help="halfwords to list when END is omitted")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=None)
    ap.add_argument("--coherent", type=lambda s: int(s, 0), default=None,
                    metavar="BYTES")
    ap.add_argument("--xref", type=lambda s: int(s, 0), default=None,
                    metavar="ADDR")
    ap.add_argument("--value", action="store_true",
                    help="with --xref: match the loaded value, not the slot")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest and not args.module:
        return selftest()
    if not args.module or args.start is None:
        ap.error("need MODULE and START (or --selftest alone)")

    path = resolve(args.module)
    if path is None:
        print("no such module: %s" % args.module, file=sys.stderr)
        return 1
    with open(path, "rb") as fh:
        data = fh.read()
    name = os.path.basename(path)
    base = args.base
    if base is None:
        base = BIN_BASE if name.upper() == "0.BIN" else PRG_BASE

    if args.coherent is not None:
        return coherent(name, data, base, args.start, args.coherent)

    if args.xref is not None:
        end = args.end if args.end else base + len(data)
        hits = xrefs(data, base, args.start, end - args.start, args.xref,
                     args.value)
        print("%s  PC-relative loads of %s 0x%08X: %d"
              % (name, "value" if args.value else "slot", args.xref, len(hits)))
        for addr, code, text in hits:
            print("%08X  %04X  %s" % (addr, code, text))
        return 0

    end = args.end if args.end else args.start + args.count * 2
    for addr, code, text, kind, target in sweep(data, base, args.start,
                                                end - args.start):
        print("%08X  %04X  %s" % (addr, code, annotate(data, base, addr, code, text)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
