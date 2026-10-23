#!/usr/bin/env python3
"""saturn_prg.py - the Sega Saturn SH-2 modules on the Resident Evil disc.

Twelve modules: `0.BIN` (the boot module, carrying the 11-entry `.PRG` name
table at +0x190) and the eleven `.PRG`s it names.

**They are big-endian SH-2, and they must be imported into Ghidra as they are,
with `SuperH:BE:32:SH-2` - no byte swap.**  Ghidra 12.1.2's SuperH support is
big-endian only (`superh.ldefs` defines `SuperH:BE:32:SH-1` / `:SH-2` /
`:SH-2A`; the little-endian `SuperH4:LE:32:default` is the wrong CPU, because
the SH-4 puts its FPU opcodes in the 1100/1101/1110/1111 groups where an SH-2
has `MOV.B @(disp,Gn)`, `MOV.L Rm,@(disp,Rn)`, `MOV #imm,Rn` and
`MOV.L @(disp,PC),Rn`).  `tools/verify_saturn_code.py` proves the byte order.

**None of them uses the standard Saturn executable header** - there is no
`branch` / `bss_size` / `init_pc` / `init_sp` / `load_address` / `file_size` at
offset 0.  Each module starts directly with the shared prologue

    mov.l r14,@-r15 ; sts.l pr,@-r15 ...

at file offset 0, except `0.BIN`, which carries a 0x40-byte header and starts
its code at 0x40.  Ten of the eleven `.PRG`s share their first 0x22 bytes;
`MOVIE.PRG` does not.

    python tools/saturn_prg.py list
    python tools/saturn_prg.py info GAME1.PRG
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_iso import Iso

# The Saturn SH-2 BIOS names its public entry points B-IO-<name>; the modules
# that bind them carry the names as ASCII.
BIOS_SYM = re.compile(rb"[TB]BIO[A-Z0-9_]{3,}")
DISC_FILE = re.compile(rb"[\x20-\x7e]{3,}\.(?:PRG|BIN|PCK|CPK|CDP|RDT|BSS|EMD|DAT|MSH)")

# 0.BIN carries a 0x40-byte header before its code; the .PRG files do not.
ENTRY = {"0.BIN": 0x40}

# The first 0x22 bytes are identical in ten of the eleven .PRGs (MOVIE.PRG is
# the exception): the crt0 prologue. 0x2FE6 0x4F22 is "mov.l r14,@-r15 ;
# sts.l pr,@-r15", exactly as Ghidra renders it (an older comment here said
# otherwise and was wrong; see tools/saturn_sh2.py).
STUB_TEXT = "shared 0x22-byte crt0 prologue"


def entry_of(name):
    return ENTRY.get(name.upper(), 0)


def strings(b, n=5):
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % n, b):
        out.append((m.start(), m.group().decode("latin1")))
    return out


def module_list(iso):
    out = ["0.BIN"]
    out += sorted(k.lstrip("/") for k in iso._list if k.upper().endswith(".PRG"))
    return out



def cmd_list(iso, args):
    print("%-13s %9s %8s  %-9s %s" % ("module", "bytes", "entry", "BIOSsyms",
                                      "disc files it names"))
    for p in module_list(iso):
        b = iso.get(p)
        e = entry_of(p)
        syms = BIOS_SYM.findall(b)
        files = sorted({f.decode() for f in DISC_FILE.findall(b)})
        print("%-13s %9d %#8x  %-9d %s" % (p, len(b), e, len(syms),
                                            " ".join(files[:3])))
    print()
    print("all twelve are big-endian SH-2, entry as shown - import with")
    print("SuperH:BE:32:SH-2 and no byte swap (tools/verify_saturn_code.py)")
    return 0


def cmd_info(iso, args):
    name = args[0]
    if not name.upper().endswith((".PRG", ".BIN")):
        name += ".PRG"
    b = iso.get(name)
    e = entry_of(name)
    print("%s: %d bytes" % (name, len(b)))
    print("  code entry: file offset %#06x" % e)
    print("  big-endian SH-2, no standard Saturn exe header")
    print("  starts with: %s" % STUB_TEXT)
    print("  at entry:  %s" % ' '.join('%02x' % c for c in b[e:e + 16]))
    syms = [(m.start(), m.group().decode()) for m in BIOS_SYM.finditer(b)]
    if syms:
        print("  SH-2 BIOS symbol names: %d" % len(syms))
        for o, s in syms[:14]:
            print("     %#06x  %s" % (o, s))
    st = strings(b)
    names = [(o, s) for o, s in st if DISC_FILE.fullmatch(s.encode("latin1"))]
    if names:
        print("  disc filenames it names: %d" % len(names))
        for o, s in names[:24]:
            print("     %#06x  %s" % (o, s))
    return 0


def main(argv):
    iso = Iso()
    table = dict(list=cmd_list, info=cmd_info)
    if len(argv) < 2 or argv[1] not in table:
        print(__doc__)
        return 1
    return table[argv[1]](iso, argv[2:]) or 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
