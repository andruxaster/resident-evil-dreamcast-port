#!/usr/bin/env python3
"""Locate big-endian 32-bit literals inside a Saturn module.

The Saturn .PRG modules have no header and are loaded verbatim at
0x0602B000, so a file offset maps to a RAM address as:

    addr = 0x0602B000 + offset

Tables and globals are reached through PC-relative `MOV.L @(disp,PC),Rn`
literal pools, so every data address of interest appears somewhere in the
image as a big-endian word. This finds those words and prints the file
offset, the address the literal *sits at*, and its value.

    python tools/saturn_find_literals.py GAME5.PRG 0x06095950 0x060953E0
    python tools/saturn_find_literals.py GAME5.PRG 0x06095950 --all

Addresses may be given with or without the 0x.  `--all` prints every hit
instead of only the first few.
"""

import argparse
import os
import struct
import sys

# Every .PRG overlay loads at this address (see docs/SATURN_VERSION_ANALYSIS.md
# section 3c); 0.BIN is the exception at 0x06004000 and is handled by --base.
PRG_BASE = 0x0602B000


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module", help="module file, e.g. GAME5.PRG")
    ap.add_argument("addresses", nargs="+", help="values to find, hex")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=PRG_BASE,
                    help="load address of the module (default 0x0602B000)")
    ap.add_argument("--all", action="store_true",
                    help="print every hit instead of the first 12")
    args = ap.parse_args()

    path = args.module
    if not os.path.isfile(path):
        for root in ("assets/SATURN_ISO_CONTENT", "assets/SATURN_RAM_DUMP", "."):
            cand = os.path.join(root, os.path.basename(path))
            if os.path.isfile(cand):
                path = cand
                break
        else:
            print("no such module: %s" % args.module, file=sys.stderr)
            return 1

    with open(path, "rb") as fh:
        data = fh.read()

    print("%s  %d bytes  base 0x%08X" % (os.path.basename(path), len(data), args.base))

    limit = None if args.all else 12
    for raw in args.addresses:
        value = int(raw, 0)
        needle = struct.pack(">I", value)
        hits = []
        pos = data.find(needle)
        while pos != -1:
            hits.append(pos)
            pos = data.find(needle, pos + 1)
        shown = hits if limit is None else hits[:limit]
        print("\n0x%08X  %d hit(s)%s" % (value, len(hits),
                                        "" if limit is None else " (showing %d)" % len(shown)))
        for off in shown:
            lit = args.base + off
            print("    file +0x%05X   literal at 0x%08X   (word ends 0x%08X)"
                  % (off, lit, lit + 3))
        if not shown:
            print("    (none)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
