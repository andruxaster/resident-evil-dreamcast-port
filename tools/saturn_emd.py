#!/usr/bin/env python3
"""saturn_emd.py - decode the Sega Saturn .EMD (enemy / character model).

Layout (docs/SATURN_VERSION_ANALYSIS.md section 5), all big-endian:

    +0x00  u32 0x10, then three u32 section offsets S1, S2, S3
    +0x10  section 0: the complete PS1 .EMD, byte-for-byte (the PC loader's
           format: directory in the last 0x14 bytes). The Saturn build does
           NOT draw it - for EM1016 (Tick) it is a verbatim copy of EM1006
           (Hunter), for EM1017 (Zombie Wesker) one of EM1001.
    S1     section 1: LZ77 (tools/saturn_lz77.py) -> the SGL mesh
    S2     section 2: LZ77 -> 4 bpp texture pixels (VDP1 character data)
    S3     section 3: raw 16-colour palettes, 32 bytes each, RGB555 BE

Section 1, decoded (offsets relative to its start):

    +0x00 u32 object count N
    +0x04 u32 palette count (== len(S3) / 32)
    +0x08 u32 -> root OBJECT
    +0x0C u32 -> TEXTURE table
    +0x10 u32 [N] -> XPDATA, one per joint

    XPDATA (24 B): pntbl, nbPoint, pltbl, nbPolygon, attbl, vntbl
    POINT / VECTOR: 3 x FIXED 16.16
    POLYGON (20 B): normal 3 x FIXED, 4 x u16 vertex index (v3 == v2: triangle)
    ATTR (12 B): u8 flag, u8 sort, u16 texno, u16 atrb, u16 colno, u16 gstb,
                 u16 dir. atrb = VDP1 CMDPMOD (bit 6 SPD: transparent texel
                 0 drawn opaque; colour mode 1 = 16-colour LUT). colno =
                 palette index * 4 (8-byte units). dir bit 4 = H flip,
                 bit 5 = V flip, low bits 2 = textured.
    OBJECT (48 B): pat (-> XPDATA), u16 joint id + pad, pos 3 x FIXED,
                   ang 3 x u16 + pad, scl 3 x FIXED, child, next
    TEXTURE (8 B): u16 w, h, cg (pixel offset / 8 into S2), size code;
                   terminated by 0xFFFF

Units: Saturn coordinate * 4 == PS1 coordinate. Mesh k is joint `id` in joint-
local space; OBJECT positions are the PS1 skeleton offsets / 4.

    python tools/saturn_emd.py info  assets/SATURN_ISO_CONTENT/ENEMY/EM1016.EMD
    python tools/saturn_emd.py obj   assets/SATURN_ISO_CONTENT/ENEMY/EM1016.EMD OUTDIR
    python tools/saturn_emd.py png   assets/SATURN_ISO_CONTENT/ENEMY/EM1016.EMD OUT.png
    python tools/saturn_emd.py pc    assets/SATURN_ISO_CONTENT/ENEMY/EM1016.EMD OUT.EMD

`pc` writes a PC-format .EMD: section 0's animation data (the PS1 skeleton the
Saturn joints share) + the Saturn mesh as a PS1 TMD of 0x34000609 triangles +
the Saturn textures as an 8 bpp TIM of 256 x (256 * pages), one CLUT. Every
Saturn model needs 3-4 pages; a TIM taller than 256 lines is only drawn right
by an engine that reads the page index each packet carries in the halfword
after (u2, v2) - the stock PC path divides V by 256.
"""

import argparse
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_lz77 import Decoder


def _unlz(b):
    d = Decoder(b)
    if d.run() != "end":
        raise ValueError("LZ77 stream did not terminate")
    return bytes(d.out)


def rgb555(c):
    return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
            ((c >> 10) & 31) * 255 // 31)


class SaturnEmd:
    def __init__(self, data):
        if data[:4] != b"\x00\x00\x00\x10":
            raise ValueError("not a Saturn .EMD (no 00000010 tag)")
        s1, s2, s3 = struct.unpack_from(">3I", data, 4)
        self.ps1 = data[0x10:s1]
        self.mesh = _unlz(data[s1:s2])
        self.pix = _unlz(data[s2:s3])
        pal = data[s3:]
        self.palettes = [[struct.unpack_from(">H", pal, 32 * p + 2 * i)[0]
                          for i in range(16)] for p in range(len(pal) // 32)]
        m = self.mesh
        u = lambda o: struct.unpack_from(">I", m, o)[0]
        self.nobj, self.npal, root, tex = (u(0), u(4), u(8), u(12))
        if self.npal != len(self.palettes):
            raise ValueError("palette count %d vs section 3 %d"
                             % (self.npal, len(self.palettes)))
        self.textures = []
        o = tex
        while o + 8 <= len(m):
            w, h, cg, sz = struct.unpack_from(">4H", m, o)
            if w == 0xFFFF:
                break
            self.textures.append((w, h, cg * 8))
            o += 8
        self.parts = [self._xpdata(u(16 + 4 * i)) for i in range(self.nobj)]
        # skeleton: OBJECT tree -> joint id, parent, offset (PS1 units)
        self.joint_of = {}
        self.skel = {}
        self._walk(root, None)

    def _xpdata(self, o):
        m = self.mesh
        pt, npt, pl, npl, at, vn = struct.unpack_from(">6I", m, o)
        fx = lambda a: struct.unpack_from(">i", m, a)[0] / 65536.0
        pts = [tuple(fx(pt + 12 * k + 4 * a) * 4 for a in range(3))
               for k in range(npt)]
        polys = []
        for k in range(npl):
            v = struct.unpack_from(">4H", m, pl + 20 * k + 12)
            f, s, tx, atrb, col, gst, dr = struct.unpack_from(
                ">BBHHHHH", m, at + 12 * k)
            polys.append(dict(v=v, tex=tx, atrb=atrb, pal=col // 4, dir=dr,
                              flag=f, sort=s))
        return dict(offset=o, points=pts, polys=polys)

    def _walk(self, o, parent):
        m = self.mesh
        while o:
            pat = struct.unpack_from(">I", m, o)[0]
            jid = struct.unpack_from(">H", m, o + 4)[0]
            pos = [struct.unpack_from(">i", m, o + 8 + 4 * a)[0] / 65536.0 * 4
                   for a in range(3)]
            child, nxt = struct.unpack_from(">II", m, o + 40)
            part = next(i for i, p in enumerate(self.parts)
                        if p["offset"] == pat)
            self.joint_of[part] = jid
            self.skel[jid] = (parent, pos, part)
            if child:
                self._walk(child, jid)
            o = nxt

    def texel(self, tex, x, y):
        w, h, off = self.textures[tex]
        b = self.pix[off + (y * w + x) // 2]
        return (b >> 4) if (x & 1) == 0 else (b & 15)

    def world_points(self):
        """Bind-pose world position of every part's points (PS1 units)."""
        world = {}

        def origin(j):
            if j in world:
                return world[j]
            parent, pos, _ = self.skel[j]
            base = origin(parent) if parent is not None else (0.0, 0.0, 0.0)
            world[j] = tuple(base[a] + pos[a] for a in range(3))
            return world[j]
        out = []
        for i, p in enumerate(self.parts):
            o = origin(self.joint_of[i])
            out.append([tuple(v[a] + o[a] for a in range(3)) for v in p["points"]])
        return out


# ---------------------------------------------------------------- export

def corners(poly, w, h):
    """Texture corner for each of the 4 vertices (VDP1 distorted sprite)."""
    c = [(0, 0), (w, 0), (w, h), (0, h)]
    if poly["dir"] & 0x10:
        c = [(w - x, y) for x, y in c]
    if poly["dir"] & 0x20:
        c = [(x, h - y) for x, y in c]
    return c


def atlas(emd, pad=1, width=1024):
    """Shelf-pack every texture (RGBA). Returns (w, h, rgba, placement)."""
    order = sorted(range(len(emd.textures)), key=lambda t: -emd.textures[t][1])
    place, x, y, shelf = {}, 0, 0, 0
    for t in order:
        w, h, _ = emd.textures[t]
        if x + w + pad > width:
            x, y, shelf = 0, y + shelf + pad, 0
        place[t] = (x, y)
        x += w + pad
        shelf = max(shelf, h)
    H = y + shelf
    # palette per texture: the first polygon that uses it decides
    tpal = {}
    for p in emd.parts:
        for q in p["polys"]:
            tpal.setdefault(q["tex"], (q["pal"], q["atrb"]))
    img = bytearray(width * H * 4)
    for t, (px, py) in place.items():
        w, h, _ = emd.textures[t]
        pal, atrb = tpal.get(t, (0, 0xCC))
        lut = emd.palettes[min(pal, len(emd.palettes) - 1)]
        for yy in range(h):
            for xx in range(w):
                i = emd.texel(t, xx, yy)
                r, g, b = rgb555(lut[i])
                a = 0 if (i == 0 and not atrb & 0x40) else 255
                o = ((py + yy) * width + px + xx) * 4
                img[o:o + 4] = bytes((r, g, b, a))
    return width, H, bytes(img), place


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    chunk = lambda t, d: (struct.pack(">I", len(d)) + t + d
                          + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def export_obj(emd, outdir, name):
    os.makedirs(outdir, exist_ok=True)
    W, H, rgba, place = atlas(emd)
    write_png(os.path.join(outdir, name + ".png"), W, H, rgba)
    with open(os.path.join(outdir, name + ".mtl"), "w") as f:
        f.write("newmtl skin\nmap_Kd %s.png\n" % name)
    wp = emd.world_points()
    lines = ["mtllib %s.mtl" % name, "usemtl skin"]
    vbase, tbase = 1, 1
    for i, p in enumerate(emd.parts):
        lines.append("o joint%02d" % emd.joint_of[i])
        for x, y, z in wp[i]:
            lines.append("v %.3f %.3f %.3f" % (x, -y, z))  # PS1 y is down
        for q in p["polys"]:
            w, h, _ = emd.textures[q["tex"]]
            px, py = place[q["tex"]]
            cs = corners(q, w, h)
            for cx, cy in cs:
                lines.append("vt %.5f %.5f" % ((px + cx) / W, 1 - (py + cy) / H))
            v = q["v"]
            idx = [0, 1, 2] if v[3] == v[2] else [0, 1, 2, 3]
            lines.append("f " + " ".join("%d/%d" % (vbase + v[k], tbase + k)
                                         for k in idx))
            tbase += 4
        vbase += len(p["points"])
    with open(os.path.join(outdir, name + ".obj"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return W, H


def render(emd, path, size=512, yaw=30.0):
    """Software preview: textured, z-buffered, orthographic, two views."""
    W, H, rgba, place = atlas(emd)
    wp = emd.world_points()
    views = []
    for ang in (yaw, yaw + 180.0):
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        views.append(lambda p, c=c, s=s: (p[0] * c + p[2] * s, p[1],
                                          -p[0] * s + p[2] * c))
    allp = [v for part in wp for v in part]
    ys = [p[1] for p in allp]
    span = max(max(ys) - min(ys), 1.0)
    sc = size * 0.9 / span
    img = bytearray(b"\x30" * (size * 2 * size * 3))
    for vi, tf in enumerate(views):
        zb = [1e30] * (size * size)
        pts = [[tf(p) for p in part] for part in wp]
        xs = [p[0] for part in pts for p in part]
        cx = (max(xs) + min(xs)) / 2
        cy = (max(ys) + min(ys)) / 2
        scr = lambda p: ((p[0] - cx) * sc + size / 2, (p[1] - cy) * sc + size / 2, p[2])
        for i, part in enumerate(emd.parts):
            for q in part["polys"]:
                w, h, _ = emd.textures[q["tex"]]
                px, py = place[q["tex"]]
                cs = corners(q, w, h)
                v = q["v"]
                tris = [(0, 1, 2)] if v[3] == v[2] else [(0, 1, 2), (0, 2, 3)]
                for tri in tris:
                    P = [scr(pts[i][v[k]]) for k in tri]
                    T = [cs[k] for k in tri]
                    _raster(P, T, px, py, W, H, rgba, img, zb, size, vi)
    write_ppm_or_png(path, size * 2, size, img)


def _raster(P, T, px, py, W, H, rgba, img, zb, size, vi):
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = P
    den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    if abs(den) < 1e-9:
        return
    minx, maxx = max(int(min(x0, x1, x2)), 0), min(int(max(x0, x1, x2)) + 1, size - 1)
    miny, maxy = max(int(min(y0, y1, y2)), 0), min(int(max(y0, y1, y2)) + 1, size - 1)
    for y in range(miny, maxy + 1):
        for x in range(minx, maxx + 1):
            a = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den
            b = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den
            c = 1 - a - b
            if a < 0 or b < 0 or c < 0:
                continue
            z = a * z0 + b * z1 + c * z2
            k = y * size + x
            if z >= zb[k]:
                continue
            u = a * T[0][0] + b * T[1][0] + c * T[2][0]
            v = a * T[0][1] + b * T[1][1] + c * T[2][1]
            tx = min(int(px + u), W - 1)
            ty = min(int(py + v), H - 1)
            o = (ty * W + tx) * 4
            if rgba[o + 3] == 0:
                continue
            zb[k] = z
            d = (y * size * 2 + x + vi * size) * 3
            img[d:d + 3] = rgba[o:o + 3]


def write_ppm_or_png(path, w, h, rgb):
    rgba = bytearray()
    for i in range(0, len(rgb), 3):
        rgba += rgb[i:i + 3] + b"\xff"
    write_png(path, w, h, bytes(rgba))


# ---------------------------------------------------------------- PC EMD

PAGE = 256


def pack_pages(emd, pad=1):
    """Shelf-pack the used textures into 256 x 256 pages.

    A texture never straddles a page boundary, so its page and 8-bit UVs are
    exact. Returns (npages, {tex: (page, x, y)})."""
    used = sorted({q["tex"] for p in emd.parts for q in p["polys"]},
                  key=lambda t: (-emd.textures[t][1], -emd.textures[t][0]))
    place, page, x, y, shelf = {}, 0, 0, 0, 0
    for t in used:
        w, h, _ = emd.textures[t]
        if w > PAGE or h > PAGE:
            raise ValueError("texture %d is %dx%d, larger than a page" % (t, w, h))
        if x + w > PAGE:
            x, y, shelf = 0, y + shelf + pad, 0
        if y + h > PAGE:
            page, x, y, shelf = page + 1, 0, 0, 0
        place[t] = (page, x, y)
        x += w + pad
        shelf = max(shelf, h)
    return page + 1, place


def build_tim(emd, npages, place):
    """8 bpp TIM, 256 x (256 * npages), one 256-colour CLUT.

    Index 0 is the transparent key (the PC engine keys on index 0); Saturn
    palette k colour c becomes index 1 + 16k + c."""
    if 1 + 16 * emd.npal > 256:
        raise ValueError("%d palettes do not fit one 256-colour CLUT" % emd.npal)
    clut = [0] * 256
    for k, lut in enumerate(emd.palettes):
        for c in range(16):
            clut[1 + 16 * k + c] = lut[c] & 0x7FFF
    # One page stays exactly 256 lines (the engine's stock V / 256 path); a
    # taller TIM is trimmed to the rows in use, rounded up to 16.
    used = max(pg * PAGE + y0 + emd.textures[t][1] for t, (pg, x0, y0) in place.items())
    H = PAGE if npages == 1 else (used + 15) // 16 * 16
    img = bytearray(PAGE * H)
    tpal = {}
    for p in emd.parts:
        for q in p["polys"]:
            tpal.setdefault(q["tex"], (q["pal"], q["atrb"]))
    for t, (pg, x0, y0) in place.items():
        w, h, _ = emd.textures[t]
        pal, atrb = tpal[t]
        keyed = not (atrb & 0x40)          # SPD clear: texel 0 is a hole
        for yy in range(h):
            row = (pg * PAGE + y0 + yy) * PAGE + x0
            for xx in range(w):
                i = emd.texel(t, xx, yy)
                img[row + xx] = 0 if (keyed and i == 0) else 1 + 16 * pal + i
    clut_blk = struct.pack("<I4H", 12 + 512, 0, 0x1E0, 256, 1) + \
        struct.pack("<256H", *clut)
    img_blk = struct.pack("<I4H", 12 + len(img), 0x280, 0, PAGE // 2, H) + bytes(img)
    return struct.pack("<II", 0x10, 9) + clut_blk + img_blk


def build_tmd(emd, place):
    """PS1 TMD in the PC loader's layout: [size, 0, nobj], 7-word object
    table (offsets relative to the table), then per object vertices,
    normals and 0x34000609 packets (textured Gouraud triangles). The
    texture page index goes in the pad halfword after (u2, v2)."""
    by_joint = {emd.joint_of[i]: i for i in range(emd.nobj)}
    objs = []
    for j in range(emd.nobj):
        part = emd.parts[by_joint[j]]
        m = emd.mesh
        # Vertex normals = the average of the adjacent polygons' normals. The
        # XPDATA's sixth word points at a point-sized block that is NOT a
        # normal table (only its first entry is a unit vector).
        pl = struct.unpack_from(">I", m, part["offset"] + 8)[0]
        verts = [tuple(int(round(c)) for c in v) for v in part["points"]]
        acc = [[0.0, 0.0, 0.0] for _ in verts]
        for k, q in enumerate(part["polys"]):
            fn = [struct.unpack_from(">i", m, pl + 20 * k + 4 * a)[0] / 65536.0
                  for a in range(3)]
            for vi in set(q["v"]):
                for a in range(3):
                    acc[vi][a] += fn[a]
        norms = []
        for n in acc:
            l = math.sqrt(sum(c * c for c in n)) or 1.0
            norms.append(tuple(int(round(c / l * 4096)) for c in n))
        prims = []
        for q in part["polys"]:
            w, h, _ = emd.textures[q["tex"]]
            pg, x0, y0 = place[q["tex"]]
            uv = [(x0 + min(cx, w - 1), y0 + min(cy, h - 1))
                  for cx, cy in corners(q, w, h)]
            v = q["v"]
            tris = [(0, 1, 2)] if v[3] == v[2] else [(0, 1, 2), (0, 2, 3)]
            for a, b, c in tris:
                a, b, c = a, c, b          # Saturn winding is the reverse of PS1's
                pk = struct.pack("<I", 0x34000609)
                # CBA / TSB exactly as the PC EMDs carry them: CLUT at VRAM
                # y 0x1E0 (0x1E0 << 6), tpage colour mode 1 = 8 bpp (0x80).
                # PSXObject::EnumKind rejects a packet whose TSB is not 8 bpp
                # ("clut range error"), and the loader rebases both fields.
                pk += struct.pack("<BBH", uv[a][0], uv[a][1], 0x7800)  # CBA
                pk += struct.pack("<BBH", uv[b][0], uv[b][1], 0x0080)  # TSB
                pk += struct.pack("<BBH", uv[c][0], uv[c][1], pg)      # page
                for k in (a, b, c):
                    pk += struct.pack("<HH", v[k], v[k])
                prims.append(pk)
        objs.append((verts, norms, prims))
    table = 12 + 28 * len(objs)
    body = b""
    entries = []
    for verts, norms, prims in objs:
        vt = table - 12 + len(body)
        body += b"".join(struct.pack("<4h", *v, 0) for v in verts)
        nt = table - 12 + len(body)
        body += b"".join(struct.pack("<4h", *n, 0) for n in norms)
        pt = table - 12 + len(body)
        body += b"".join(prims)
        entries.append(struct.pack("<7I", vt, len(verts), nt, len(norms), pt,
                                   len(prims), 0))
    size = table + len(body)
    return struct.pack("<3I", size, 0, len(objs)) + b"".join(entries) + body


def build_pc_emd(emd):
    """The PS1 section's animation data + the Saturn mesh and textures, as a
    PC .EMD. Directory (last 0x14 bytes): extra, anim header, anim, TMD, TIM."""
    ps1 = emd.ps1
    d = list(struct.unpack_from("<5I", ps1, len(ps1) - 20))
    head = ps1[:d[3]]
    npages, place = pack_pages(emd)
    tmd = build_tmd(emd, place)
    tim = build_tim(emd, npages, place)
    out = bytearray(head)
    while len(out) % 4:
        out += b"\0"
    d[3] = len(out)
    out += tmd
    while len(out) % 4:
        out += b"\0"
    d[4] = len(out)
    out += tim
    while len(out) % 4:
        out += b"\0"
    out += struct.pack("<5I", *d)
    return bytes(out), npages


def info(emd, name):
    print("%s: %d parts, %d palettes, %d textures (%d B of 4 bpp pixels)"
          % (name, emd.nobj, emd.npal, len(emd.textures), len(emd.pix)))
    print("  PS1 section: %d bytes" % len(emd.ps1))
    tp = tv = 0
    for i, p in enumerate(emd.parts):
        parent, pos, _ = emd.skel[emd.joint_of[i]]
        tri = sum(1 for q in p["polys"] if q["v"][3] == q["v"][2])
        tp += len(p["polys"])
        tv += len(p["points"])
        print("  part %2d joint %2d parent %4s  %3d points %3d polys (%d tri)"
              "  offset (%.0f, %.0f, %.0f)"
              % (i, emd.joint_of[i], parent, len(p["points"]), len(p["polys"]),
                 tri, pos[0], pos[1], pos[2]))
    print("  total %d points, %d polygons" % (tv, tp))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("info", "obj", "png", "pc"))
    ap.add_argument("emd")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--yaw", type=float, default=30.0)
    a = ap.parse_args()
    emd = SaturnEmd(open(a.emd, "rb").read())
    name = os.path.splitext(os.path.basename(a.emd))[0]
    if a.cmd == "info":
        info(emd, name)
    elif a.cmd == "obj":
        W, H = export_obj(emd, a.out or ".", name)
        print("wrote %s.obj/.mtl/.png (atlas %dx%d) to %s" % (name, W, H, a.out or "."))
    elif a.cmd == "pc":
        data, npages = build_pc_emd(emd)
        out = a.out or name + ".pc.EMD"
        open(out, "wb").write(data)
        th = struct.unpack_from("<HH", data, struct.unpack_from("<I", data, len(data) - 4)[0] + 8 + 524 + 8)[1]
        print("wrote %s: %d bytes, %d texture page(s) (TIM 256x%d)"
              % (out, len(data), npages, th))
    else:
        render(emd, a.out or name + ".png", yaw=a.yaw)
        print("wrote", a.out or name + ".png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
