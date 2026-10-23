#!/usr/bin/env python3
"""saturn_ranking_logo.py - the Japanese Saturn's rotating "BIO HAZARD" logo.

The Battle Game's results screens (RANKING.PRG) of the Japanese Saturn release
draw a 3D "BIO HAZARD" behind the score and ranking tables. The model is in
BOTH discs' RANKING.PRG - an SGL object tree of nine XPDATA parts, one per
letter - but only the Japanese module calls its draw routine:

    Bio Hazard RANKING.PRG (base 0x0602B000)          USA RANKING.PRG
      0x0602D2E4  draw the logo (7 callers)             0x0602D33C (0 callers)
      0x0602D230  SGL tree walk (push / slTranslate /   0x0602D288
                  slRotZ,Y,X / slScale / slPutPolygonX
                  (part, light 0x06042620) / children
                  under the same matrix / siblings)
      0x0603C18C  root object (part 8, children 0..7)   0x0603C2EC
      0x0603BF54  XPDATA[9], 24 bytes each              0x0603C0B4

  draw (0x0602D2E4): slPerspective(0x2000 = 45 degrees), push, slTranslate(
  0x06042610), slRotX/Y/Z(0x06042608/A/C), walk the tree, pop; then
  rot Y -= 91, rot Z += 182 (SGL angles, 0x10000 = 360 degrees) - every frame.
  init (state 0 of 0x0602D398): angles 0, position (0, 0, 64.0); CRAM bank 16
  (0x25F00020) = four black entries and a red ramp R = 0..11 (of 31); light
  (0.577, 0.577, 0.577), i.e. normalised (1, 1, 1).

  Every polygon has the same ATTR (00 2B 0000 00E0 0010 0000 0004): single
  plane, centre Z sort with light and palette, 256-colour bank, colour 16, no
  texture. So the logo is flat-shaded geometry over one 16-entry ramp.

PORT APPROXIMATION: SGL's light-to-palette offset is not read out of the
library. This renders offset = clamp(int(-(n.l) * 31), 0, 15) into the ramp,
calibrated against a capture of the real screen: the face toward the camera
(-(n.l) = 0.577 at rest) lands on the brightest entry, R = 11 (~90/255, the
capture's 88..96), and faces turned away from the light fall on the four black
entries - the dark edges the logo has there. src/game/saturn/SaturnBattle.cpp
uses the same formula.

Usage:
    saturn_ranking_logo.py info [RANKING.PRG]
    saturn_ranking_logo.py png OUT.png [FRAME] [RANKING.PRG]
"""

import math
import os
import struct
import sys

BASE = 0x0602B000
JPN_PRG = os.path.join('assets', 'SATURN_JPN_ISO_CONTENT', 'RANKING.PRG')
# root object, XPDATA array, per module
LAYOUT = {
    'JPN': (0x0603C18C, 0x0603BF54),
    'USA': (0x0603C2EC, 0x0603C0B4),
}
PARTS = 9
DIST = 64.0                    # slTranslate(0, 0, 64.0)
FOV = 45.0                     # slPerspective(0x2000)
ROT_Y_STEP = -91               # per frame, SGL angle units
ROT_Z_STEP = 182
LIGHT = (0x93CD / 65536.0,) * 3


def ramp():
    """CRAM bank 16 as state 0 writes it: 4 x 0x8000, then 0x8000 | (i - 4)."""
    out = []
    for i in range(16):
        r5 = 0 if i < 4 else i - 4
        out.append((r5 * 255 // 31, 0, 0))
    return out


def fx(v):
    return v / 65536.0


def load(path):
    d = open(path, 'rb').read()
    for region, (root, xp) in LAYOUT.items():
        ok = True
        parts = []
        for i in range(PARTS):
            off = xp + 24 * i - BASE
            if off + 24 > len(d):
                ok = False
                break
            pnt, npnt, pol, npol, att, _vn = struct.unpack_from('>6I', d, off)
            if not (BASE <= pnt < BASE + len(d) and BASE <= pol < BASE + len(d)
                    and 0 < npnt < 1000 and 0 < npol < 1000):
                ok = False
                break
            pts = [tuple(fx(c) for c in struct.unpack_from('>3i', d, pnt - BASE + 12 * k))
                   for k in range(npnt)]
            polys = []
            for k in range(npol):
                nx, ny, nz, a, b, c, e = struct.unpack_from('>3i4H', d, pol - BASE + 20 * k)
                polys.append(((fx(nx), fx(ny), fx(nz)), (a, b, c, e)))
            parts.append((pts, polys))
        if ok:
            return region, parts
    raise ValueError('%s: no logo model at the JPN or USA address' % path)


def rot_matrix(ax, ay, az):
    """slRotX(ax) then slRotY(ay) then slRotZ(az) on the current matrix: a
    point is transformed by Rx * Ry * Rz (the last call is applied first)."""
    def ang(a):
        return a * 2.0 * math.pi / 65536.0
    cx, sx = math.cos(ang(ax)), math.sin(ang(ax))
    cy, sy = math.cos(ang(ay)), math.sin(ang(ay))
    cz, sz = math.cos(ang(az)), math.sin(ang(az))
    rx = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    ry = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rz = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))

    def mul(a, b):
        return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3))
                     for i in range(3))
    return mul(mul(rx, ry), rz)


def apply(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


def faces(parts, frame):
    """Screen-space polygons for one frame, far to near: [(z, [(x, y)...], rgb)]."""
    m = rot_matrix(0, (ROT_Y_STEP * frame) & 0xFFFF, (ROT_Z_STEP * frame) & 0xFFFF)
    scr = 160.0 / math.tan(math.radians(FOV / 2))
    pal = ramp()
    out = []
    for pts, polys in parts:
        view = [apply(m, p) for p in pts]
        view = [(x, y, z + DIST) for x, y, z in view]
        for n, idx in polys:
            corners = [view[i] for i in (idx if idx[2] != idx[3] else idx[:3])]
            nv = apply(m, n)
            cxyz = [sum(c[k] for c in corners) / len(corners) for k in range(3)]
            # single plane: drawn only when the normal faces the camera
            if nv[0] * cxyz[0] + nv[1] * cxyz[1] + nv[2] * cxyz[2] >= 0:
                continue
            d = -(nv[0] * LIGHT[0] + nv[1] * LIGHT[1] + nv[2] * LIGHT[2])
            d = max(-1.0, min(1.0, d))
            offset = max(0, min(15, int(d * 31.0)))
            xy = [(160.0 + x * scr / z, 120.0 + y * scr / z) for x, y, z in corners]
            out.append((cxyz[2], xy, pal[offset]))
    out.sort(key=lambda f: -f[0])
    return out


def cmd_info(path):
    region, parts = load(path)
    npts = sum(len(p) for p, _ in parts)
    npol = sum(len(q) for _, q in parts)
    print('%s: %s module, %d parts, %d points, %d polygons' % (path, region, len(parts), npts, npol))
    for i, (p, q) in enumerate(parts):
        xs = [v[0] for v in p]
        print('  part %d: %3d points %3d polygons  x %.2f..%.2f' % (i, len(p), len(q), min(xs), max(xs)))
    return 0


def cmd_png(out, frame, path):
    from PIL import Image, ImageDraw
    _region, parts = load(path)
    im = Image.new('RGB', (320, 240), (0, 0, 48))
    dr = ImageDraw.Draw(im)
    for _z, xy, rgb in faces(parts, frame):
        dr.polygon(xy, fill=rgb)
    im = im.resize((640, 480))
    im.save(out)
    print('wrote %s (frame %d)' % (out, frame))
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == 'info':
        return cmd_info(argv[2] if len(argv) > 2 else JPN_PRG)
    if len(argv) >= 3 and argv[1] == 'png':
        frame = int(argv[3]) if len(argv) > 3 else 0
        return cmd_png(argv[2], frame, argv[4] if len(argv) > 4 else JPN_PRG)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
