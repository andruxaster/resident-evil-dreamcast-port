#!/usr/bin/env python3
"""dump_emd.py - inspect an enemy model file (.EMD from ENEMY/).

Reports an .EMD's shape. For the Saturn build's layout use
tools/saturn_emd.py, which decodes it fully: a 16-byte big-endian index, then
the COMPLETE PS1 .EMD byte-for-byte, then three Saturn sections (LZ77 SGL
mesh, LZ77 4 bpp textures, 16-colour palettes). An older note here said only
the first 0x800 bytes were shared; that came from comparing at the wrong
offset (the 16-byte index was not skipped on one side).

`--vs FILE` proves that relationship for a matched pair instead of asserting it.

Usage:
    python tools/dump_emd.py FILE.EMD [--vs OTHER.EMD]
"""

import argparse
import struct
import sys


SATURN_INDEX_TAG = b'\x00\x00\x00\x10'


def saturn_index(b):
    """Return the three section offsets of a Saturn .EMD, or None.

    The tag at +0 is the byte string 00 00 00 10 (0x10 as a big-endian u32,
    i.e. not the header's own size), so match the raw bytes.
    """
    if len(b) < 16 or b[0:4] != SATURN_INDEX_TAG:
        return None
    return struct.unpack_from('>3I', b, 4)


def report(b, label):
    n = len(b)
    print('%s: %d bytes' % (label, n))
    idx = saturn_index(b)
    if idx:
        print('  Saturn 16-byte index: header=0x%08X  section offsets %s'
              % (0x10, ' '.join('%#x' % v for v in idx)))
        print('  the PS1 OG texture block should be at +0x10..+0x810')
    else:
        print('  no Saturn 16-byte index (PS1/OG EMD)')
    head = b[0x10:0x28] if idx else b[0:0x18]
    print('  head: %s' % ' '.join('%02x' % c for c in head))
    return idx


def compare(a, b, la, lb):
    print()
    print('=== %s vs %s ===' % (la, lb))
    off = 0x10 if saturn_index(a) else 0
    offb = 0x10 if saturn_index(b) else 0
    n = min(len(a) - off, len(b) - offb)
    same = sum(1 for i in range(n) if a[off + i] == b[offb + i])
    first = next((i for i in range(n) if a[off + i] != b[offb + i]), None)
    print('  size %d vs %d (delta %d)' % (len(a), len(b), len(a) - len(b)))
    print('  body compared from +%#x: %d/%d bytes equal (%.1f%%)'
          % (off, same, n, 100.0 * same / max(1, n)))
    print('  first differing body byte at +%#x%s'
          % (off + first, '' if first is not None else ' (identical)'))
    if saturn_index(a) and saturn_index(b):
        print('  -> Saturn header + shared texture + re-authored mesh' if first == 0x800
              else '  -> differs from the start of the texture block too')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('emd')
    ap.add_argument('--vs', help='a matching .EMD from the other disc')
    args = ap.parse_args()

    a = open(args.emd, 'rb').read()
    report(a, args.emd)
    if args.vs:
        b = open(args.vs, 'rb').read()
        report(b, args.vs)
        compare(a, b, args.emd, args.vs)
    return 0


if __name__ == '__main__':
    sys.exit(main())
