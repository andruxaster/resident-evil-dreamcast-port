"""Saturn room files: .RDT and .BSS decoded with the module LZ77.

Both are packed with the SAME codec as the .PRG tails (`FUN_0602b040`,
`tools/saturn_lz77.py`), from byte 0, with no header:

  .RDT  one LZ77 stream. Decoded, it is BYTE-IDENTICAL to the PS1 USA disc's
        file of the same name - all 348 shared rooms. There is no Saturn RDT
        format; the PC/PS1 room tools read the decoded file as it is.
        STAGE8 (Battle Game, 15 rooms) has no PS1 counterpart.

  .BSS  a container, not a stream. Big-endian u32 offset table (its first
        entry is its own size, so count = first / 4, normally 20) splitting
        into two halves of equal length:
          slots [0, n/2)    palettes: 512 B = 256 Saturn RGB555 words (BE,
                            bit 15 set, R in bits 0-4, B in bits 10-14)
          slots [n/2, n)    images: an LZ77 stream -> 76,800 B = 320x240
                            8bpp, stored as 8x8 cells, 40x30, row-major,
                            each cell row-major
        palette slot k belongs to image slot n/2 + k. An unused slot is the
        4-byte placeholder 00 00 00 01.
        STAGE3/ROOM311.BSS is the exception: an unconverted PS1 MDEC frame.

Usage (from the repo root):
  python tools/saturn_room.py rdt FILE.RDT [OUT]      decode one room
  python tools/saturn_room.py verify                  decode every .RDT and
                                                      compare with the PS1 disc
  python tools/saturn_room.py bss FILE.BSS OUTDIR     every background -> PNG
"""

import glob
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_lz77 import Decoder  # noqa: E402

SATURN_ROOT = os.path.join("assets", "SATURN_ISO_CONTENT")
PS1_IMAGE = os.path.join("assets", "Resident Evil [U] [SLUS-00170]",
                         "Resident Evil (NTSC) SLUS_001.70.img")
PLACEHOLDER = b"\x00\x00\x00\x01"
IMAGE_W, IMAGE_H, CELL = 320, 240, 8


def unpack(data):
    """Decode one LZ77 stream; it must end on its zero token."""
    d = Decoder(data)
    why = d.run()
    if why != "end":
        raise ValueError("stream did not terminate (%s)" % why)
    return bytes(d.out), d.sp


# -- .RDT ---------------------------------------------------------------------
def decode_rdt(path):
    data = open(path, "rb").read()
    out, used = unpack(data)
    if used != len(data):
        raise ValueError("%s: %d trailing bytes" % (path, len(data) - used))
    return out


def ps1_iso():
    import saturn_iso
    saturn_iso.USER_OFFSET = 24            # the PS1 disc is MODE2/2352
    return saturn_iso.Iso(PS1_IMAGE)


def cmd_verify(args):
    iso = ps1_iso()
    same = differ = new = 0
    for path in sorted(glob.glob(os.path.join(SATURN_ROOT, "STAGE*", "*.RDT"))):
        stage = os.path.basename(os.path.dirname(path))
        name = os.path.basename(path)
        out = decode_rdt(path)
        try:
            ref = iso.get("/PSX/%s/%s" % (stage, name))
        except Exception:
            ref = None
        if ref is None:
            new += 1
            print("  new   %s/%s  %d B, %d cameras" % (stage, name, len(out), out[1]))
        elif ref == out:
            same += 1
        else:
            differ += 1
            print("  DIFF  %s/%s" % (stage, name))
    print("identical to PS1: %d   differ: %d   Saturn-only: %d"
          % (same, differ, new))
    return 1 if differ else 0


def cmd_rdt(args):
    out = decode_rdt(args[0])
    dst = args[1] if len(args) > 1 else os.path.basename(args[0]) + ".dec"
    open(dst, "wb").write(out)
    print("%s -> %s  %d B, %d cameras" % (args[0], dst, len(out), out[1]))


# -- .BSS ---------------------------------------------------------------------
def bss_slots(data):
    first = struct.unpack_from(">I", data, 0)[0]
    if first % 8 or not 8 <= first <= 0x100:
        raise ValueError("not a Saturn BSS container (PS1 MDEC frame?)")
    n = first // 4
    offs = list(struct.unpack_from(">%dI" % n, data, 0)) + [len(data)]
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


def bss_backgrounds(data):
    """Yield (camera slot, 256 RGB triples, 76,800 linear 8bpp pixels)."""
    slots = bss_slots(data)
    half = len(slots) // 2
    for k in range(half):
        pal, img = slots[k], slots[half + k]
        if img[:4] == PLACEHOLDER and len(img) == 4:
            continue
        cells, _ = unpack(img)
        per_row = IMAGE_W // CELL
        pix = bytearray(IMAGE_W * IMAGE_H)
        for c in range(len(cells) // (CELL * CELL)):
            cx, cy = c % per_row, c // per_row
            base = c * CELL * CELL
            for j in range(CELL):
                y = cy * CELL + j
                pix[y * IMAGE_W + cx * CELL:y * IMAGE_W + cx * CELL + CELL] = \
                    cells[base + j * CELL:base + j * CELL + CELL]
        rgb = []
        for i in range(256):
            v = struct.unpack_from(">H", pal, 2 * i)[0]
            rgb.append(bytes(((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                              ((v >> 10) & 31) * 255 // 31)))
        yield k, rgb, bytes(pix)


def write_png(path, rgb, pix):
    raw = b"".join(b"\0" + b"".join(rgb[p] for p in pix[y * IMAGE_W:(y + 1) * IMAGE_W])
                   for y in range(IMAGE_H))

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body)))
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", IMAGE_W, IMAGE_H, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def cmd_bss(args):
    src, outdir = args[0], args[1]
    os.makedirs(outdir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(src))[0]
    for k, rgb, pix in bss_backgrounds(open(src, "rb").read()):
        dst = os.path.join(outdir, "%s_%02d.png" % (stem, k))
        write_png(dst, rgb, pix)
        print(dst)


def main(argv):
    cmds = {"rdt": cmd_rdt, "verify": cmd_verify, "bss": cmd_bss}
    if not argv or argv[0] not in cmds:
        print(__doc__)
        return 2
    return cmds[argv[0]](argv[1:]) or 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
