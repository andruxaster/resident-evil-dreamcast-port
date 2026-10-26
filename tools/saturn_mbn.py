#!/usr/bin/env python3
"""saturn_mbn.py - decode the Sega Saturn A_*.DAT room prop (MBN container).

The `A_*` props are the Saturn-only room props: `A_TRS1`/`A_TRS2` are the two
Tyrant capsules, `A_PLT2`/`A_PLT4` extra plants, `A_KKM1` the kitchen machine.
They are a DIFFERENT container from the .EMD that `saturn_emd.py` handles - an
EMD wraps a whole PS1 EMD plus three LZ77 sections, whereas an A_*.DAT is a
standalone mesh with the SAME SGL layout as an EMD's section 1, stored
uncompressed. All big-endian, and all FIXED values are 16.16.

    +0x00 u32  BASE  - file offset of the SGL mesh (always 8)
    +0x04 u32  END   - file offset where the mesh ends; the palettes follow
    BASE        the SGL mesh, pointers below are relative to BASE
      +0x00 u32  object count N          (+0x08 u32 -> root OBJECT)
      +0x04 u32  palette count            (+0x0C u32 -> TEXTURE table)
      +0x10 u32[N] -> XPDATA, one per joint
    END    32 bytes per palette, 16 x RGB555 BE

    XPDATA (24 B): pntbl, nbPoint, pltbl, nbPolygon, attbl, vntbl
    POINT / VECTOR: 3 x FIXED 16.16
    POLYGON (20 B): normal 3 x FIXED, 4 x u16 vertex index (v3 == v2: triangle)
    ATTR (12 B): u8 flag, u8 sort, u16 texno, u16 atrb, u16 colno, u16 gstb,
                 u16 dir
    OBJECT (48 B): pat (-> XPDATA), u16 joint id + pad, pos 3 x FIXED,
                   ang 3 x u16 + pad, scl 3 x FIXED, child, next
    TEXTURE (8 B): u16 w, h, offset/8, (w/8)<<8 | h; w == 0xFFFF terminates

The two mesh types tile exactly, which is what confirms the layout - for
A_TRS1 (412 B) the sections land at BASE+0x14 points, +0x74 polygons, +0x9C
attrs, +0xB4 vectors, +0x114 xpdata, +0x12C root OBJECT (48 B), +0x15C the
texture table, and END is the file size minus one 32-byte palette.

**The pixels are NOT in this file.** A_TRS1.DAT is the mesh + palette; the 4 bpp
VDP1 character data lives in the sibling A_TRS1T.DAT and is addressed purely by
each texture's `offset` field. For A_TRS1 the two 40x64 images are 1280 B each,
and 1280 + 1280 == the 2560 B of A_TRS1T.DAT, which is what confirms that field
is a byte offset in 8-byte units.

These are cheap background props, not solid meshes: A_TRS1 is two textured
quads. Units are Saturn * 4 == PS1.

    python tools/saturn_mbn.py info assets/SATURN_ISO_CONTENT/ENEMY/A_TRS1.DAT
    python tools/saturn_mbn.py png  assets/SATURN_ISO_CONTENT/ENEMY/A_TRS1.DAT out.png
    python tools/saturn_mbn.py obj  assets/SATURN_ISO_CONTENT/ENEMY/A_TRS1.DAT outdir
"""

import argparse
import os
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fixed(v):
    return v / 65536.0


class Mbn:
    def __init__(self, path):
        self.path = path
        self.data = d = open(path, "rb").read()
        self.base = struct.unpack_from(">I", d, 0)[0]
        self.end = struct.unpack_from(">I", d, 4)[0]
        if self.base != 8 or not (0 < self.end < len(d)):
            raise ValueError("unexpected container header: base=%d end=%d"
                             % (self.base, self.end))
        # 32-byte palette(s) immediately after the mesh
        nbytes = len(d) - self.end
        self.palettes = []
        for p in range(nbytes // 32):
            o = self.end + 32 * p
            self.palettes.append([struct.unpack_from(">H", d, o + 2 * i)[0]
                                  for i in range(16)])

        m = self.m
        u = lambda o: struct.unpack_from(">I", m, o)[0]
        self.nobj, self.npal, root, tex = (u(0), u(4), u(8), u(12))
        self.textures = []
        o = tex
        while o + 8 <= self.end - self.base:
            w, h, off8, sz = struct.unpack_from(">4H", m, o)
            if w == 0xFFFF or w == 0:
                break
            # f4 packs the size in 8x8 cells: high byte = w/8, low = h
            self.textures.append((w, h, off8 * 8,
                                  ((sz >> 8) * 8) * (sz & 0xFF) // 2))
            o += 8

        self.parts = [self._xpdata(u(16 + 4 * i)) for i in range(self.nobj)]
        self.objects = []
        self._walk(root, None)

    @property
    def m(self):
        return self.data[self.base:self.end]

    def _xpdata(self, o):
        """o is mesh-relative; returns the part with file-absolute pointers."""
        d = self.data
        pt, npt, pl, npl, at, vn = struct.unpack_from(">6I", self.m, o)
        f = self.base
        pts = [tuple(fixed(struct.unpack_from(">i", d, f + pt + 12 * k + 4 * a)[0])
                     for a in range(3)) for k in range(npt)]
        polys = []
        for k in range(npl):
            v = struct.unpack_from(">4H", d, f + pl + 20 * k + 12)
            fl, so, tx, atrb, col, gst, dr = struct.unpack_from(
                ">BBHHHHH", d, f + at + 12 * k)
            polys.append(dict(v=v, tex=tx, atrb=atrb, pal=col // 4, dir=dr,
                              flag=fl, sort=so))
        return dict(points=pts, polys=polys)

    def _walk(self, o, parent):
        d = self.data
        f = self.base
        while o:
            pat = struct.unpack_from(">I", self.m, o)[0]
            jid = struct.unpack_from(">H", self.m, o + 4)[0]
            pos = [fixed(struct.unpack_from(">i", self.m, o + 8 + 4 * a)[0])
                   for a in range(3)]
            scl = [fixed(struct.unpack_from(">i", self.m, o + 24 + 4 * a)[0])
                   for a in range(3)]
            child, nxt = struct.unpack_from(">II", self.m, o + 40)
            part = next((i for i, p in enumerate(self.parts)
                         if p is not None and self._pat(i) == pat), None)
            self.objects.append(dict(part=part, jid=jid, pos=pos, scl=scl,
                                     parent=parent))
            if child:
                self._walk(child, jid)
            o = nxt

    def _pat(self, i):
        pt = struct.unpack_from(">I", self.m, 16 + 4 * i)[0]
        return pt

    def texel(self, tex, x, y):
        w, h, off, size = self.textures[tex]
        if x >= w or y >= h:
            return 0
        b = self.pix[off + (y * w + x) // 2]
        return (b >> 4) if (x & 1) == 0 else (b & 0xF)

    def load_pixels(self):
        """The sibling A_*T.DAT: raw 4 bpp VDP1 character data."""
        stem, ext = os.path.splitext(self.path)
        tp = stem + "T" + ext
        if not os.path.exists(tp):
            raise SystemExit("missing texture file %s" % tp)
        return open(tp, "rb").read()

    def rgb(self, c):
        return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                ((c >> 10) & 31) * 255 // 31)


# ------------------------------------------------------------------ render
def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    chunk = lambda t, d: (struct.pack(">I", len(d)) + t + d
                          + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">2I5B", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def texture_rgba(mbn, tex):
    w, h, off, size = mbn.textures[tex]
    pal = mbn.palettes[0]
    out = bytearray()
    for y in range(h):
        for x in range(w):
            c = pal[mbn.texel(tex, x, y) & 15]
            r, g, b = mbn.rgb(c)
            out += bytes((r, g, b, 255))
    return w, h, bytes(out)


def _project(p, yaw, pitch):
    x, y, z = p
    a = yaw * 3.14159265 / 180.0
    x, z = x * a and (x * __import__("math").cos(a) - z * __import__("math").sin(a)), \
        (x * __import__("math").sin(a) + z * __import__("math").cos(a))
    b = pitch * 3.14159265 / 180.0
    y, z = y * __import__("math").cos(b) - z * __import__("math").sin(b), \
        y * __import__("math").sin(b) + z * __import__("math").cos(b)
    return x, y, z


def render(mbn, path, size=320, yaw=35.0, pitch=20.0):
    import math
    mbn.pix = mbn.load_pixels()
    tex_rgba = {}
    for t in range(len(mbn.textures)):
        tex_rgba[t] = texture_rgba(mbn, t)
    W = H = size
    img = bytearray(b"\x20\x20\x30\xff" * (W * H))

    allpts = [p for part in mbn.parts for p in part["points"]]
    if not allpts:
        raise SystemExit("no points")
    cx = sum(p[0] for p in allpts) / len(allpts)
    cy = sum(p[1] for p in allpts) / len(allpts)
    cz = sum(p[2] for p in allpts) / len(allpts)
    rad = max(math.dist(p, (cx, cy, cz)) for p in allpts) or 1.0
    sc = (size * 0.40) / rad

    for part in mbn.parts:
        for poly in part["polys"]:
            v = poly["v"]
            if v[0] == v[1] and v[1] == v[2]:
                continue
            quad = [(v[0], v[1], v[2]), (v[0], v[2], v[3])]
            for tri in quad:
                pts = [part["points"][i] for i in tri]
                if len(pts) < 3:
                    continue
                sp = [(( _project(p, yaw, pitch)[0] - cx) * sc + size / 2,
                       -(_project(p, yaw, pitch)[1] - cy) * sc + size / 2,
                       _project(p, yaw, pitch)[2]) for p in pts]
                (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = sp
                den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
                if abs(den) < 1e-9:
                    continue
                tw, th, rgba = tex_rgba[poly["tex"]]
                lo = (min(x0, x1, x2), min(y0, y1, y2))
                hi = (max(x0, x1, x2), max(y0, y1, y2))
                for py in range(max(0, int(lo[1])), min(H - 1, int(hi[1]) + 1)):
                    for px in range(max(0, int(lo[0])), min(W - 1, int(hi[0]) + 1)):
                        fx, fy = px + 0.5, py + 0.5
                        l0 = ((y1 - y2) * (fx - x2) - (x1 - x2) * (fy - y2)) / den
                        l1 = ((y2 - y0) * (fx - x2) - (x0 - x2) * (fy - y2)) / den
                        l2 = 1.0 - l0 - l1
                        if l0 < 0 or l1 < 0 or l2 < 0:
                            continue
                        u = (l0 * (x0 - x2) + l1 * (x1 - x2) + tw) / tw
                        v = (l0 * (y0 - y2) + l1 * (y1 - y2) + th) / th
                        tx, ty = int(u) % tw, int(v) % th
                        o = (ty * tw + tx) * 4
                        img[(py * W + px) * 4:(py * W + px) * 4 + 4] = rgba[o:o + 4]
    write_png(path, W, H, bytes(img))


def export_obj(mbn, outdir, name):
    os.makedirs(outdir, exist_ok=True)
    mbn.pix = mbn.load_pixels()
    lines = ["# %s (%d points, %d polygons)" % (
        os.path.basename(mbn.path),
        sum(len(p["points"]) for p in mbn.parts),
        sum(len(p["polys"]) for p in mbn.parts))]
    base = 1
    mtls = {}
    for pi, part in enumerate(mbn.parts):
        for vi, p in enumerate(part["points"]):
            lines.append("v %.5f %.5f %.5f" % (p[0] * 4, p[1] * 4, p[2] * 4))
        for poly in part["polys"]:
            v = poly["v"]
            t = poly["tex"]
            mtls[t] = True
            lines.append("usemtl tex%d" % t)
            quad = [(v[0], v[1], v[2]), (v[0], v[2], v[3])]
            for tri in quad:
                tw, th = mbn.textures[t][0], mbn.textures[t][1]
                uv = {v[0]: (0, 0), v[1]: (tw, 0), v[2]: (tw, th), v[3]: (0, th)}
                f = []
                for i in tri:
                    u, vv = uv[i]
                    f.append("%d/%d" % (base + i, u))
                    f[-1] = "%d/%d/%d" % (base + i, u, vv)
                lines.append("f " + " ".join(f))
        base += len(part["points"])
    open(os.path.join(outdir, name + ".obj"), "w").write("\n".join(lines) + "\n")

    with open(os.path.join(outdir, name + ".mtl"), "w") as f:
        for t in sorted(mtls):
            w, h, off, size = mbn.textures[t]
            f.write("newmtl tex%d\n" % t)
            for ci in range(16):
                r, g, b = mbn.rgb(mbn.palettes[0][ci])
                f.write("Kd %.3f %.3f %.3f\n" % (r / 255, g / 255, b / 255))
    # one PNG per texture, side by side
    for t in sorted(mtls):
        w, h, rgba = texture_rgba(mbn, t)
        write_png(os.path.join(outdir, name + "_tex%d.png" % t), w, h, rgba)
    return outdir


def info(mbn):
    print("%s: %d bytes, %d object(s), %d palette(s)"
          % (os.path.basename(mbn.path), len(mbn.data), mbn.nobj, mbn.npal))
    print("  mesh at +0x%X .. +0x%X, palettes at +0x%X"
          % (mbn.base, mbn.end, mbn.end))
    for i, part in enumerate(mbn.parts):
        o = mbn.objects[i] if i < len(mbn.objects) else None
        print("  part %d: %d points, %d polygons%s"
              % (i, len(part["points"]), len(part["polys"]),
                 "" if o is None else "  joint id %d" % o["jid"]))
    for i, (w, h, off, size) in enumerate(mbn.textures):
        print("  texture %d: %dx%d 4bpp, %d bytes at +0x%X"
              % (i, w, h, size, off))
    try:
        px = mbn.load_pixels()
        need = max(o + s for (_, _, o, s) in mbn.textures) if mbn.textures else 0
        print("  texture file: %d bytes, meshes need %d -> %s"
              % (len(px), need, "exact" if need == len(px) else "MISMATCH"))
    except SystemExit as e:
        print("  %s" % e)
    if mbn.palettes:
        print("  palette 0: %s" % " ".join(
            "%02x" % c for c in mbn.palettes[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("info", "obj", "png"))
    ap.add_argument("file")
    ap.add_argument("out", nargs="?")
    a = ap.parse_args()
    mbn = Mbn(a.file)
    if a.cmd == "info":
        info(mbn)
    elif a.cmd == "obj":
        export_obj(mbn, a.out or "out", os.path.splitext(
            os.path.basename(a.file))[0].lower())
    else:
        render(mbn, a.out or "out.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
