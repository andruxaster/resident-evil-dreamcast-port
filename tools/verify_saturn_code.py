#!/usr/bin/env python3
"""verify_saturn_code.py - prove the Saturn disc's code is big-endian SH-2.

Ghidra 12.1.2's SuperH support is big-endian only (`superh.ldefs` defines
`SuperH:BE:32:SH-1` / `:SH-2` / `:SH-2A`; the little-endian `SuperH4:LE:32:default`
is the wrong CPU, because the SH-4 puts its FPU opcodes in the 1100/1101/1110/1111
groups where an SH-2 has MOV.B / MOV.L / MOV #imm / MOV.L @(disp,PC)).  So the
modules must be imported **as they are**, with `SuperH:BE:32:SH-2` - there is no
byte-swap to apply.  This script is what proves the byte order, and it exists
because getting it wrong looks plausible: little-endian decodes a couple of
words in the shared prologue into recognisable-looking instructions.

The test uses control flow, not data movement, because MOV/CMP-style words are
so common that either byte order produces some by chance.  The discriminators
are NOP (`0x0009`) and RTS (`0x000B`): their bit patterns essentially
never occur in random data, and in correctly-ordered SH-2 code they occur
hundreds of times per module.  BSR / BRA / BT / BF corroborate.

Each module is scored in both byte orders and compared against the rate seen in
random bytes, so the output states a ratio rather than a bare count.

    python tools/verify_saturn_code.py
    python tools/verify_saturn_code.py --verbose
"""

import argparse
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_iso import Iso

# SH-2 control-flow encodings (Hitachi SH-1/SH-2 programming manual), as
# (predicate, name). An earlier table here had BSR as 0x4Fxx, BRA as 0x8Cxx and
# RTS as 0x000B-0x000F, none of which is right.
BRANCH = [
    (lambda w: w >> 12 == 0xB, "BSR"),
    (lambda w: w >> 12 == 0xA, "BRA"),
    (lambda w: w >> 8 in (0x89, 0x8D), "BT"),
    (lambda w: w >> 8 in (0x8B, 0x8F), "BF"),
    (lambda w: w & 0xF0FF == 0x402B, "JMP@Rn"),
    (lambda w: w & 0xF0FF == 0x400B, "JSR@Rn"),
    (lambda w: w == 0x000B, "RTS"),
    (lambda w: w & 0xF0FF == 0x0023, "BRAF@Rn"),
    (lambda w: w == 0x0009, "NOP"),
]
# The two that carry the argument.
STRONG = ("RTS", "NOP")

# 0.BIN is the boot module and carries a 0x40-byte header; the .PRG files start
# their code at offset 0.  All twelve share the same prologue word.
ENTRY = {"0.BIN": 0x40}
# Ten of the eleven .PRGs share their first 0x22 bytes: the crt0 prologue
# (0x2FE6 = MOV.L R14,@-R15, 0x4F22 = STS.L PR,@-R15, exactly as Ghidra shows
# it). An older comment here claimed those words were MOV.W / BSR and that the
# modules were partly compressed; both claims were wrong (the modules load
# verbatim, see docs/SATURN_VERSION_ANALYSIS.md).
STUB_TEXT = "the shared crt0 prologue (mov.l r14,@-r15 ; sts.l pr,@-r15)"


def tally(b, order):
    counts = dict((n, 0) for _, n in BRANCH)
    for o in range(0, len(b) - 1, 2):
        w = struct.unpack_from(order + "H", b, o)[0]
        for pred, n in BRANCH:
            if pred(w):
                counts[n] += 1
                break
    return counts


def modules(iso):
    out = [("0.BIN", ENTRY["0.BIN"])]
    for k in sorted(iso._list):
        if k.upper().endswith(".PRG"):
            out.append((k.lstrip("/"), 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true",
                    help="print the full per-opcode tally per byte order")
    args = ap.parse_args()

    iso = Iso()
    mods = modules(iso)
    # NB: the Random has to be built once, outside the generator - putting it
    # inside would re-seed per iteration and yield a constant byte.
    rng = random.Random(1)
    rand = bytes(rng.randrange(256) for _ in range(400000))
    base = {}
    for order in "<>":
        c = tally(rand, order)
        base[order] = c

    print("random-byte baseline (per 100k words):")
    for order, tag in (("<", "LE"), (">", "BE")):
        c = base[order]
        print("  %s  %s" % (tag, " ".join("%s=%d" % (k, c[k]) for k in sorted(c))))
    print()

    ok = True
    print("%-13s %8s  %-24s %-24s %s" % ("module", "bytes", "RTS+NOP LE", "RTS+NOP BE", "verdict"))
    for name, entry in mods:
        b = iso.get(name)
        c_le = tally(b, "<")
        c_be = tally(b, ">")
        s_le = sum(c_le[k] for k in STRONG)
        s_be = sum(c_be[k] for k in STRONG)
        words = len(b) // 2
        exp_le = (base["<"]["RTS"] + base["<"]["NOP"]) * words / 200000.0
        exp_be = (base[">"]["RTS"] + base[">"]["NOP"]) * words / 200000.0
        r_le = s_le / exp_le if exp_le else 0
        r_be = s_be / exp_be if exp_be else 0
        good = r_be > 20 and r_le < r_be / 4
        ok = ok and good
        print("%-13s %8d  %6d (%5.1fx)   %6d (%5.1fx)   %s"
              % (name, len(b), s_le, r_le, s_be, r_be,
                 "big-endian" if good else "?? CHECK"))
        print("%-13s            entry %#06x  first bytes %s"
              % ("", entry, " ".join("%02x" % c for c in b[entry:entry + 8])))
        if args.verbose:
            for order, tag, c in (("<", "LE", c_le), (">", "BE", c_be)):
                print("               %s  %s" % (tag, " ".join("%s=%d" % (k, c[k])
                                                            for k in sorted(c))))
    print()
    print("shared prefix at the entry point: %s" % STUB_TEXT)
    print("VERDICT: %s" % ("all modules are big-endian SH-2, import with "
                           "SuperH:BE:32:SH-2 and no byte swap" if ok
                           else "MIXED - investigate before importing"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
