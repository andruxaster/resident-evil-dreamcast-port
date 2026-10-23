#!/usr/bin/env python3
"""Dump a big-endian table out of a Saturn module at a given RAM address.

Every .PRG overlay loads verbatim at 0x0602B000, so a RAM address maps into
the file as `addr - 0x0602B000`. Reading tables out of the module this way is
more reliable than counting hex by hand, and it keeps the byte offsets honest
when a table is not a whole number of records.

    python tools/saturn_dump_table.py GAME5.PRG 0x06095950 --stride 5 --count 23
    python tools/saturn_dump_table.py GAME5.PRG 0x060959A0 --count 17
    python tools/saturn_dump_table.py GAME5.PRG 0x060953E0 --stride 6 --count 30
"""

import argparse
import os
import sys

PRG_BASE = 0x0602B000


def resolve(path):
    if os.path.isfile(path):
        return path
    for root in ("assets/SATURN_ISO_CONTENT", "assets/SATURN_RAM_DUMP", "."):
        cand = os.path.join(root, os.path.basename(path))
        if os.path.isfile(cand):
            return cand
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module")
    ap.add_argument("address", type=lambda s: int(s, 0))
    ap.add_argument("--stride", type=int, default=0,
                    help="bytes per record; 0 prints a plain byte dump")
    ap.add_argument("--count", type=int, default=16,
                    help="records to print (or bytes when --stride is 0)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=PRG_BASE)
    ap.add_argument("--u16", action="store_true",
                    help="also show each record read as two big-endian u16s")
    args = ap.parse_args()

    path = resolve(args.module)
    if path is None:
        print("no such module: %s" % args.module, file=sys.stderr)
        return 1

    with open(path, "rb") as fh:
        data = fh.read()

    off = args.address - args.base
    if off < 0 or off > len(data):
        print("0x%08X is outside %s (0x%08X..0x%08X)"
              % (args.address, os.path.basename(path), args.base,
                 args.base + len(data)), file=sys.stderr)
        return 1

    span = args.count * args.stride if args.stride else args.count
    blob = data[off:off + span]

    print("%s @ 0x%08X  (file +0x%05X, %d byte(s))"
          % (os.path.basename(path), args.address, off, len(blob)))
    if not args.stride:
        for i in range(0, len(blob), 16):
            chunk = blob[i:i + 16]
            print("  +0x%03X  %s" % (i, " ".join("%02X" % b for b in chunk)))
        return 0

    for i in range(args.count):
        rec = blob[i * args.stride:(i + 1) * args.stride]
        if len(rec) < args.stride:
            break
        addr = args.address + i * args.stride
        text = " ".join("%02X" % b for b in rec)
        extra = ""
        if args.u16 and args.stride % 2 == 0:
            words = [int.from_bytes(rec[j:j + 2], "big")
                     for j in range(0, len(rec), 2)]
            extra = "   u16: " + " ".join("%d" % w for w in words)
        print("  [%2d]  0x%08X  %s%s" % (i, addr, text, extra))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
