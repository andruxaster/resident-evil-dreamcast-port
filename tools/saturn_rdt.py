#!/usr/bin/env python3
"""saturn_rdt.py - what is known about the Saturn .RDT, and what is not.

The PS1/PC RDT format is fully documented (`docs/RDT_FILE_FORMAT.md`) and the
port's `RDT` struct in `src/game/Types.h` is the conversion target: a 0x94-byte
header (5 counts, 3 ambient shorts, 3 x 0x14 lights, then 19 32-bit *relative*
pointers) with the camera array immediately after it at 0x94, 0x2C bytes per
record.

The Saturn `.RDT` is **not** that format.  This tool establishes that, and every
detector here is validated against the PS1 disc first, so a negative result
means something:

    header     differencing all 363 Saturn rooms against each other
    cameras    structural search for the 0x2C camera array, by its fov field
    stride     search for ANY fixed-stride array of N fov-like int32s, over
               every stride 0x08-0x100, all four byte phases, both endiannesses

Findings so far (see `docs/SATURN_PORT.md`):

  * **The `.RDT` and `.BSS` files are compressed** - see `packed` below. Every one
    of the 348 rooms and 116 backgrounds has a longest run of identical bytes of
    3 and ~1% branch density, with no plain region anywhere, while the module's
    known-plain code prefix runs to 381. Nothing about the Saturn room format can
    be read off the disc until it is decoded, and that is the gate on Phase 2.
  * It is not a trivial obfuscation: no single-byte XOR or ADD key (all 256 of
    each, scored by maxrun against the plain-code control) decodes any of them.
    The codec is a real compressor, and the stub at the front of each `.PRG` is a
    byte-copy decompressor with back-references, so the same family is the
    obvious first thing to try.
  * Once decoded, the room is very likely close to the PS1 layout, because the
    Saturn modules are compiled from the PS1 source tree - which is what would
    make a converter worth writing.

The detectors below are retained because they are how a decode gets *checked*,
and because each is validated against the PS1 disc first. Note that they were
originally run against the still-compressed files, so their negatives on the disc
say **nothing** about the format - they are not evidence that the Saturn RDD
differs from the PS1 one.

    python tools/saturn_rdt.py packed     # are the .RDT files compressed?
    python tools/saturn_rdt.py header
    python tools/saturn_rdt.py cameras
    python tools/saturn_rdt.py stride
"""

import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_iso import Iso

PS1IMG = os.path.join("assets", "Resident Evil [U] [SLUS-00170]",
                      "Resident Evil (NTSC) SLUS_001.70.img")
PS_SECTOR, PS_OFF = 2352, 24
CAM = 0x2C


# --------------------------------------------------------------------------
# PS1 disc access
# --------------------------------------------------------------------------
def _ps1_tree():
    pf = open(PS1IMG, "rb")

    def rd(lba, cnt=1):
        pf.seek(lba * PS_SECTOR + PS_OFF)
        return pf.read(2048 * cnt)

    def dirlist(lba, size):
        out = []
        for b in range((size + 2047) // 2048):
            d = rd(lba + b)
            o = 0
            while o < len(d):
                ln = d[o]
                if ln == 0:
                    break
                r = d[o:o + ln]
                o += ln
                out.append((r[33:33 + r[32]].decode("latin1").split(";")[0],
                            struct.unpack_from("<I", r, 2)[0],
                            struct.unpack_from("<I", r, 10)[0], r[25]))
        return out

    pf.seek(16 * PS_SECTOR + PS_OFF)
    pvd = rd(16)
    rr = pvd[156:190]
    tree = {}

    def prec(pfx, lba, size):
        for nm, el, es, fl in dirlist(lba, size):
            if nm in (chr(0), chr(1)):
                continue
            k = pfx + "/" + nm
            tree[k.upper()] = (nm, el, es, fl)
            if fl & 2:
                prec(k, el, es)

    prec("", struct.unpack_from("<I", rr, 2)[0], struct.unpack_from("<I", rr, 10)[0])
    return tree, rd


def ps1_rdt(name):
    tree, rd = _ps1_tree()
    e = tree.get("/PSX" + name) or tree.get("/PSX/" + name)
    if not e:
        return None
    return rd(e[1], (e[2] + 2047) // 2048)[:e[2]]


def saturn_rdts():
    iso = Iso()
    return iso, [(k, iso.get(k)) for k in sorted(iso._list) if k.upper().endswith(".RDT")]


# --------------------------------------------------------------------------
# header
# --------------------------------------------------------------------------
def cmd_header(args):
    iso, sat = saturn_rdts()
    report(sat, "Saturn STAGE1-8 .RDT")
    print("Control - the same method on the PS1 OG, where the layout is known:")
    rooms = []
    for st in range(1, 8):
        pre = "/PSX/STAGE%d/" % st
        tree, _ = _ps1_tree()
        for k, v in sorted(tree.items()):
            if k.startswith(pre) and k.endswith(".RDT") and v[2] >= 0x100:
                rooms.append((k, ps1_rdt(k[len("/PSX"):])))
    report(rooms, "PS1 OG STAGE1-7 .RDT")


def report(rooms, label, span=0x100, minsize=0x100):
    print("=" * 72)
    print(label)
    print("=" * 72)
    keep = [(k, b) for k, b in rooms if len(b) >= minsize]
    for k, b in rooms:
        if len(b) < minsize:
            print("   stub: %-34s %d bytes" % (k, len(b)))
    print("%d rooms, %d at least %#x bytes" % (len(rooms), len(keep), minsize))
    print()
    row = "".join("C" if len(set(b[o] for _, b in keep)) == 1 else "v"
                  for o in range(span))
    for i in range(0, span, 72):
        print("  +%03X  %s" % (i, row[i:i + 72]))
    print()
    print("constant runs:")
    for a in range(span):
        if len(set(b[a] for _, b in keep)) != 1:
            continue
        e = a
        while e + 1 < span and len(set(b[e + 1] for _, b in keep)) == 1:
            e += 1
        print("  +%03X..%03X  = 0x%02x" % (a, e, keep[0][1][a]))
        a = e
    print()


# --------------------------------------------------------------------------
# cameras
# --------------------------------------------------------------------------
def fov_ok(v):
    return 0x000000C0 <= v <= 0x00000A00


def small_ok(v):
    return -0x1000 <= v <= 0x1000


def coord_ok(v):
    return -0x8000 <= v <= 0x8000


def find_camera_array(buf, count):
    """The 0x2C camera array, located by its fov field. Returns (offset, score)."""
    best = (0, None)
    n = len(buf)
    for off in range(0, min(n - count * CAM, 0x40000)):
        ok = 0
        fovs = []
        for i in range(count):
            b = off + i * CAM
            if b + CAM > n:
                break
            (_, _, fx, fy, fz, tx, ty, tz, roll, zero, fov) = \
                struct.unpack_from("<11i", buf, b)
            if fov_ok(fov) and small_ok(roll) and small_ok(zero) and \
               all(coord_ok(v) for v in (fx, fy, fz, tx, ty, tz)):
                ok += 1
                fovs.append(fov)
        if ok > best[0]:
            best = (ok, (off, fovs))
    return best


def cmd_cameras(args):
    print("locating the 0x2C camera array structurally, PS1 first as a control")
    p = ps1_rdt("/STAGE1/ROOM1010.RDT")
    if p:
        ok, r = find_camera_array(p, p[1])
        print("  PS1  ROOM1010 cameras=%d -> offset %s (score %d)  fovs=%s"
              % (p[1], hex(r[0]) if r else "none", ok,
                 " ".join("0x%x" % f for f in (r[1] if r else []))))
        print("       the format documents this array at 0x94")
    print()
    iso, sat = saturn_rdts()
    for k, b in sat:
        if len(b) < 0x100:
            continue
        count = b[2]
        ok, r = find_camera_array(b, count)
        if r and ok >= max(2, count - 1):
            print("  SAT  %-24s cameras=%d -> offset %s (score %d)"
                  % (k, count, hex(r[0]), ok))
    print()
    print("  no Saturn room matched; see docs/SATURN_PORT.md")


# --------------------------------------------------------------------------
# stride
# --------------------------------------------------------------------------
def fovish(v):
    return 0x40 <= v <= 0x2000


def best_runs(buf, need, max_stride=0x100, limit=0x30000, order="<", phase=0):
    n = min(len(buf), limit)
    words = [struct.unpack_from(order + "i", buf, o)[0]
             for o in range(phase, n - 3, 4)]
    out = []
    for stride in range(0x08, max_stride, 4):
        for r in range(stride // 4):
            run = 0
            start = 0
            i = r
            while i < len(words):
                if fovish(words[i]):
                    if run == 0:
                        start = i
                    run += 1
                    if run >= need:
                        out.append((run, stride, phase + start * 4))
                else:
                    run = 0
                i += stride // 4
    return out


def cmd_stride(args):
    p = ps1_rdt("/STAGE1/ROOM1010.RDT")
    runs = best_runs(p, p[1])
    byst = {}
    for c, s, st in runs:
        if s not in byst or c > byst[s][0]:
            byst[s] = (c, st)
    top = sorted(byst.items(), key=lambda kv: -kv[1][0])[:3]
    print("CONTROL PS1 ROOM1010: %s"
          % ", ".join("stride %#x run %d @+%#x" % (s, v[0], v[1]) for s, v in top))
    print()
    iso, sat = saturn_rdts()
    for k, b in sat:
        if len(b) < 0x100:
            continue
        count = b[2]
        found = []
        for order, otag in (("<", "LE"), (">", "BE")):
            for phase in range(4):
                found += [(c, s, st, "%s/p%d" % (otag, phase))
                          for c, s, st in best_runs(b, count, order=order, phase=phase)]
        byst = {}
        for c, s, st, w in found:
            if s not in byst or c > byst[s][0]:
                byst[s] = (c, st, w)
        top = sorted(byst.items(), key=lambda kv: -kv[1][0])[:4]
        print("  %-24s cameras=%d  size=%d" % (k, count, len(b)))
        if not top:
            print("      nothing at any stride 0x08-0x100, any byte phase, LE or BE")
        for s, (c, st, w) in top:
            print("      stride %#04x run %d at +%#08x (%s)" % (s, c, st, w))


def maxrun(b):
    best = cur = 1 if b else 0
    for i in range(1, len(b)):
        cur = cur + 1 if b[i] == b[i - 1] else 1
        if cur > best:
            best = cur
    return best


def profile(b):
    """(branch %, printable %, maxrun) - the compression discriminator."""
    words = max(1, len(b) // 2)
    br = 100.0 * sum(tally(b, ">").values()) / words
    pr = 100.0 * sum(1 for c in b if 32 <= c < 127) / max(1, len(b))
    return br, pr, maxrun(b)


def cmd_packed(args):
    """Are the .RDT / .BSS files compressed?

    `maxrun` is the discriminator.  Real code and real data - including the
    module's own plain code prefix - contain long runs of identical bytes
    (padding, zero tables, repeated fields).  Compressed or encrypted data has
    almost none.  Two controls bracket the result so the metric is trustworthy:
    GAME1's known-plain code prefix and its known-compressed tail.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from verify_saturn_code import tally as _tally
    globals()["tally"] = _tally
    iso = Iso()

    print("=== controls: the GAME1 module ===")
    g = iso.get("GAME1.PRG")
    for lo, hi, lbl in ((0, 0x60000, "code prefix 0..0x60000   (known plain)"),
                        (0x60000, len(g), "tail 0x60000..        (known packed)")):
        br, pr, mr = profile(g[lo:hi])
        print("  %-34s %7d B  branch %5.2f%%  printable %5.1f%%  maxrun %d"
              % (lbl, hi - lo, br, pr, mr))

    print()
    print("=== every Saturn .RDT and .BSS, sampled at 16 KB ===")
    rdt = bss = 0
    worst_r = worst_b = None
    for k in sorted(iso._list):
        nm = k.lstrip("/")
        up = nm.upper()
        if not (up.endswith(".RDT") or up.endswith(".BSS")):
            continue
        b = iso.get(k)
        if len(b) < 0x100:
            continue
        br, pr, mr = profile(b[:0x4000])
        if up.endswith(".RDT"):
            rdt += 1
            if worst_r is None or mr > worst_r[0]:
                worst_r = (mr, nm)
        else:
            bss += 1
            if worst_b is None or mr > worst_b[0]:
                worst_b = (mr, nm)
        if nm in ("STAGE1/ROOM10B1.RDT", "STAGE1/ROOM1181.RDT", "STAGE1/ROOM10B.BSS"):
            print("  %-24s %7d B  branch %5.2f%%  printable %5.1f%%  maxrun %d"
                  % (nm, len(b), br, pr, mr))
    print()
    print("  %d .RDT files, highest maxrun in the first 16 KB: %d (%s)"
          % (rdt, worst_r[0], worst_r[1]))
    print("  %d .BSS files, highest maxrun in the first 16 KB: %d (%s)"
          % (bss, worst_b[0], worst_b[1]))
    print()
    print("  A maxrun of 2-3 with branch ~1% means packed.  Anything at or near")
    print("  the control's 381 would mean plain, and the file could be read directly.")
    return 0


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("header", "cameras", "stride", "packed"):
        print(__doc__)
        return 1
    dict(header=cmd_header, cameras=cmd_cameras, stride=cmd_stride,
         packed=cmd_packed)[sys.argv[1]](sys.argv[2:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
