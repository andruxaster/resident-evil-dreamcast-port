#!/usr/bin/env python3
"""gen_title_font_outlines.py - the title-menu typeface as vector outlines.

tools/gen_title_font.py builds the title font as an 11-row bitmap (17 letters
cut from the Director's Cut's option cells, nine drawn by hand). That is what
the game draws at 320x240. For HD mode the same letters are needed at any
size, so this file draws each of the 26 glyphs again as outlines on the
bitmap's own grid: 1 unit = 1 bitmap pixel, y down from the cap line, x from
the glyph's left edge, the same width as the bitmap glyph (the advance and the
spacing stay the bitmap's, so a string lays out exactly as it does in 4bpp).

Each glyph is a list of contours in a small path language:

    M x y          start
    L x y          straight line
    A cx cy x y    quarter ellipse around (cx, cy) to (x, y): one end level
                   with the centre, the other straight above or below it, so
                   the radii are the two ends' distances (a circle when equal)
    Q cx cy x y    quadratic Bezier with control point (cx, cy) to (x, y)

'+' contours are filled, '-' contours are holes; the writer orients them
(fills clockwise on screen, holes the other way) and everything is filled with
the NONZERO rule, so fills may overlap (Q's tail, X's two bars).

The shapes follow the bitmap: stems 3 wide, bars 2 tall, pixel staircases read
as straight diagonals through the middle of each step. What the bitmap can only
hint at with a half-tone pixel is drawn as the letter means it: the bowls and
counters of B C D G J O P Q R S U are curves (C G O Q nearly round, the others
with radius-3 or radius-2 corners, the counters rounded inside them), the
terminals of C, G and S meet their counter's step in a hard corner, and the
joints of S's spine are smooth. The check
below rasterises every glyph back onto the 11-row grid and reports how far it
lands from the bitmap ('#' = 1, '+' = 0.5, '.' = 0).

Output:
    <out>/title_font.ttf          the typeface as a TrueType font (curves as quadratics)
    <out>/title_font.svg          every glyph as an SVG path, for editing
    <out>/title_font_hd_preview.png

The game's HD title menu is drawn in a real font, Archivo Black, which the
migrator writes to HD/<USA|JPN>/Data/TITLEFNT.TTF (tools/gen_hd_fonts.py).
These outlines are no longer part of a migration (they were TITLEFNT.VEC), but
title_font.ttf is a TrueType font with A-Z, so copying it over TITLEFNT.TTF
gives the menu these hand-drawn letters instead.

Usage (repo root):  python tools/gen_title_font_outlines.py [out_dir]
                    python tools/gen_title_font_outlines.py --check
--check exits 1 when a glyph no longer matches TitleFontGlyphs.inc.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GLYPHS_INC = os.path.join(HERE, 'asset_migrator', 'src', 'core', 'TitleFontGlyphs.inc')

GLYPH_H = 11
CURVE_STEPS = 8       # segments per quarter ellipse / per Bezier when flattened

# O, Q, C and G are round, not square: their outer corners take 4.5 of each
# 11-unit side and the counters inside them are rounded to match (2.5).
O_OUTER = ('M 4.5 0 L 6.5 0 A 6.5 4.5 11 4.5 L 11 6.5 A 6.5 6.5 6.5 11 L 4.5 11 '
           'A 4.5 6.5 0 6.5 L 0 4.5 A 4.5 4.5 4.5 0')
O_HOLE = 'M 5.5 2 A 5.5 4.5 8 4.5 L 8 6.5 A 5.5 6.5 5.5 9 A 5.5 6.5 3 6.5 L 3 4.5 A 5.5 4.5 5.5 2'
# The terminals of C and G: the terminal's underside (y 3) meets the step up
# to the bar's (y 2) in a hard corner; the counter rounds into the step.
C_TOP = 'L 7 3 L 7 2.75 Q 7 2 6.25 2 L 5.5 2 A 5.5 4.5 3 4.5'
C_BOTTOM = 'L 3 6.5 A 5.5 6.5 5.5 9 L 6.25 9 Q 7 9 7 8.25 L 7 8'

OUTLINES = {
    'A': [('+', 'M 4.5 0 L 6.5 0 L 11 9 L 11 11 L 8 11 L 8 9 L 3 9 L 3 11 L 0 11 L 0 9'),
          ('-', 'M 5.5 3.5 L 7.25 7 L 3.75 7')],
    'B': [('+', 'M 0 0 L 8 0 A 8 2.25 10 2.25 A 8 2.25 8 4.5 A 8 6.75 10 6.75 L 10 8.75 '
                'A 7.75 8.75 7.75 11 L 0 11'),
          ('-', 'M 3 2 L 6 2 A 6 3 7 3 A 6 3 6 4 L 3 4'),
          ('-', 'M 3 6 L 5.5 6 A 5.5 7.5 7 7.5 A 5.5 7.5 5.5 9 L 3 9')],
    'C': [('+', 'M 4.5 0 L 7 0 A 7 3 10 3 ' + C_TOP + ' ' + C_BOTTOM + ' L 10 8 '
                'A 7 8 7 11 L 4.5 11 A 4.5 6.5 0 6.5 L 0 4.5 A 4.5 4.5 4.5 0')],
    'D': [('+', 'M 0 0 L 7 0 A 7 3 10 3 L 10 8 A 7 8 7 11 L 0 11'),
          ('-', 'M 3 2 L 5 2 A 5 4 7 4 L 7 7 A 5 7 5 9 L 3 9')],
    'E': [('+', 'M 0 0 L 9 0 L 9 2 L 3 2 L 3 4 L 8 4 L 8 6 L 3 6 L 3 9 L 9 9 L 9 11 L 0 11')],
    'F': [('+', 'M 0 0 L 9 0 L 9 2 L 3 2 L 3 4 L 8 4 L 8 6 L 3 6 L 3 11 L 0 11')],
    'G': [('+', 'M 4.5 0 L 8 0 A 8 3 11 3 ' + C_TOP + ' L 3 6.5 A 5.5 6.5 5.5 9 L 7 9 '
                'A 7 8 8 8 L 8 7 L 6 7 L 6 5 L 11 5 L 11 11 L 8 11 L 7.5 10 L 7 11 '
                'L 4.5 11 A 4.5 6.5 0 6.5 L 0 4.5 A 4.5 4.5 4.5 0')],
    'H': [('+', 'M 0 0 L 3 0 L 3 4 L 7 4 L 7 0 L 10 0 L 10 11 L 7 11 L 7 6 L 3 6 L 3 11 L 0 11')],
    'I': [('+', 'M 0 0 L 3 0 L 3 11 L 0 11')],
    'J': [('+', 'M 6 0 L 9 0 L 9 9 A 7 9 7 11 L 3 11 A 3 8 0 8 L 3 8 A 4.5 8 4.5 9 '
                'A 4.5 8 6 8')],
    # The arms' right edges meet in a point, not on a flat.
    'K': [('+', 'M 0 0 L 3 0 L 3 4.5 L 7.5 0 L 10 0 L 10 0.5 L 5.25 5.5 L 10 10.5 L 10 11 '
                'L 7.5 11 L 3 6.5 L 3 11 L 0 11')],
    'L': [('+', 'M 0 0 L 3 0 L 3 9 L 9 9 L 9 11 L 0 11')],
    'M': [('+', 'M 0 0 L 3.5 0 L 6.5 6.5 L 9.5 0 L 13 0 L 13 11 L 10 11 L 10 5 L 7 11 '
                'L 6 11 L 3 5 L 3 11 L 0 11')],
    'N': [('+', 'M 0 0 L 3 0 L 3 1.5 L 7 5.5 L 7 0 L 10 0 L 10 11 L 8.5 11 L 3 5.5 L 3 11 L 0 11')],
    'O': [('+', O_OUTER), ('-', O_HOLE)],
    'P': [('+', 'M 0 0 L 7 0 A 7 3 10 3 L 10 4 A 7 4 7 7 L 3 7 L 3 11 L 0 11'),
          ('-', 'M 3 2 L 5.5 2 A 5.5 3.5 7 3.5 A 5.5 3.5 5.5 5 L 3 5')],
    'Q': [('+', O_OUTER),
          ('+', 'M 6 7 L 7 7 L 11 10.5 L 11 11 L 9 11 L 6 9'),
          ('-', O_HOLE)],
    'R': [('+', 'M 0 0 L 8 0 A 8 2 10 2 L 10 3.5 A 8 3.5 8 5.5 L 10 9 L 10 11 L 7 11 L 7 8 '
                'L 6 7 L 3 7 L 3 11 L 0 11'),
          ('-', 'M 3 2 L 5.5 2 A 5.5 3.5 7 3.5 A 5.5 3.5 5.5 5 L 3 5')],
    # The upper counter's tip and every joint of the spine are tangent-
    # continuous: each Bezier's control point lies on both neighbours' lines.
    'S': [('+', 'M 3 0 L 7 0 A 7 2 9 2 L 9 3 L 6 3 L 6 2.75 Q 6 2 5.25 2 L 4.5 2 '
                'Q 1.47 2 3.5 2.75 Q 9.6 5 10 7.5 L 10 8 A 7 8 7 11 L 3 11 A 3 8 0 8 '
                'L 0 7 L 3 7 Q 3 9 5 9 L 6 9 Q 7 9 7 8 Q 7 7.5 6.5 7.25 '
                'L 1.5 4.75 Q 0 4 0 3 A 3 3 3 0')],
    'T': [('+', 'M 0 0 L 9 0 L 9 2 L 6 2 L 6 11 L 3 11 L 3 2 L 0 2')],
    'U': [('+', 'M 0 0 L 3 0 L 3 7 A 5 7 5 9 A 5 7 7 7 L 7 0 L 10 0 L 10 8 A 7 8 7 11 '
                'L 3 11 A 3 8 0 8')],
    'V': [('+', 'M 0 0 L 3 0 L 3 2 L 5.5 8.6 L 8 2 L 8 0 L 11 0 L 11 2 L 6.5 11 L 4.5 11 L 0 2')],
    'W': [('+', 'M 0 0 L 3 0 L 3 2 L 4.5 5.5 L 6 2 L 6 0 L 9 0 L 9 2 L 10.5 5.5 L 12 2 '
                'L 12 0 L 15 0 L 15 1.5 L 12 9.5 L 12 11 L 9 11 L 9 9.5 L 7.5 5.5 '
                'L 6 9.5 L 6 11 L 3 11 L 3 9.5 L 0 1.5')],
    'X': [('+', 'M 0 0 L 3 0 L 11 11 L 8 11'),
          ('+', 'M 8 0 L 11 0 L 3 11 L 0 11')],
    'Y': [('+', 'M 0 0 L 3 0 L 3 1.5 L 5.5 4 L 8 1.5 L 8 0 L 11 0 L 11 1.5 L 7 5.5 '
                'L 7 11 L 4 11 L 4 5.5 L 0 1.5')],
    'Z': [('+', 'M 0 0 L 10 0 L 10 2.5 L 3.5 9 L 10 9 L 10 11 L 0 11 L 0 9 L 0.5 9 '
                'L 7.5 2 L 0 2')],
}


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
# Segments: ('L', p0, p1), ('A', p0, p1, centre), ('Q', p0, p1, control).
def parse(path):
    """-> list of segments, closed."""
    tok = path.split()
    segs, i, start, cur = [], 0, None, None
    while i < len(tok):
        op = tok[i]
        if op == 'M':
            start = cur = (float(tok[i + 1]), float(tok[i + 2]))
            i += 3
        elif op == 'L':
            p = (float(tok[i + 1]), float(tok[i + 2]))
            segs.append(('L', cur, p))
            cur = p
            i += 3
        elif op in ('A', 'Q'):
            c = (float(tok[i + 1]), float(tok[i + 2]))
            p = (float(tok[i + 3]), float(tok[i + 4]))
            if op == 'A':
                ellipse(cur, p, c)            # validates
            segs.append((op, cur, p, c))
            cur = p
            i += 5
        else:
            raise ValueError('bad path op %r' % op)
    if cur != start:
        segs.append(('L', cur, start))
    return segs


def ellipse(p0, p1, c):
    """A quarter ellipse's (rx, ry, a0, d): radii, start angle, signed sweep
    (+90 or -90 degrees, y down), with point(a) = c + (rx cos a, ry sin a)."""
    d0 = (p0[0] - c[0], p0[1] - c[1])
    d1 = (p1[0] - c[0], p1[1] - c[1])
    eps = 1e-9
    if abs(d0[1]) < eps and abs(d1[0]) < eps:      # p0 level with c, p1 above/below
        rx, ry = abs(d0[0]), abs(d1[1])
    elif abs(d0[0]) < eps and abs(d1[1]) < eps:    # p0 above/below c, p1 level
        rx, ry = abs(d1[0]), abs(d0[1])
    else:
        raise ValueError('arc %r -> %r around %r is not a quarter ellipse' % (p0, p1, c))
    if rx < eps or ry < eps:
        raise ValueError('degenerate arc %r -> %r around %r' % (p0, p1, c))
    a0 = math.atan2(d0[1] / ry, d0[0] / rx)
    a1 = math.atan2(d1[1] / ry, d1[0] / rx)
    d = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
    return rx, ry, a0, d


def flatten(segs, steps=CURVE_STEPS):
    pts = []
    for s in segs:
        pts.append(s[1])
        if s[0] == 'A':
            p0, p1, c = s[1], s[2], s[3]
            rx, ry, a0, d = ellipse(p0, p1, c)
            for k in range(1, steps):
                a = a0 + d * k / steps
                pts.append((c[0] + rx * math.cos(a), c[1] + ry * math.sin(a)))
        elif s[0] == 'Q':
            p0, p1, q = s[1], s[2], s[3]
            for k in range(1, steps):
                t = k / steps
                u = 1 - t
                pts.append((u * u * p0[0] + 2 * u * t * q[0] + t * t * p1[0],
                            u * u * p0[1] + 2 * u * t * q[1] + t * t * p1[1]))
    return pts


def area(pts):
    return sum(pts[i - 1][0] * pts[i][1] - pts[i][0] * pts[i - 1][1]
               for i in range(len(pts))) / 2


def reverse(segs):
    out = []
    for s in reversed(segs):
        out.append((s[0], s[2], s[1]) + tuple(s[3:]))
    return out


def oriented(ch):
    """Contours as segment lists, fills clockwise on screen (positive area with
    y down), holes counter-clockwise: the nonzero rule then fills the glyph."""
    res = []
    for role, path in OUTLINES[ch]:
        segs = parse(path)
        a = area(flatten(segs))
        if (role == '+') != (a > 0):
            segs = reverse(segs)
        res.append(segs)
    return res


# ---------------------------------------------------------------------------
# The bitmap glyphs, for widths and the check
# ---------------------------------------------------------------------------
def bitmap_glyphs():
    src = open(GLYPHS_INC).read()
    return {m.group(1): re.findall(r'"([#+.]+)"', m.group(2))
            for m in re.finditer(r"\{'(.)', \{(.*?)\}\}", src)}


def coverage(contours, w, h, s):
    """Nonzero coverage of flattened `contours` at s x s samples per unit."""
    import numpy as np
    ys = (np.arange(h * s) + 0.5) / s
    xs = (np.arange(w * s) + 0.5) / s
    X, Y = np.meshgrid(xs, ys)
    wind = np.zeros(X.shape, np.int32)
    for c in contours:
        for i in range(len(c)):
            (x0, y0), (x1, y1) = c[i], c[(i + 1) % len(c)]
            if y0 == y1:
                continue
            t = (Y - y0) / (y1 - y0)
            right = X < x0 + t * (x1 - x0)
            wind += np.where(((y0 <= Y) & (Y < y1)) & right, 1, 0)
            wind -= np.where(((y1 <= Y) & (Y < y0)) & right, 1, 0)
    return (wind != 0).astype(np.float32)


def check_against_bitmap(bmp):
    import numpy as np
    ok = True
    for ch in sorted(OUTLINES):
        rows = bmp[ch]
        w = len(rows[0])
        flat = [flatten(c) for c in oriented(ch)]
        for c in flat:
            for x, y in c:
                if not (-1e-9 <= x <= w + 1e-9 and -1e-9 <= y <= GLYPH_H + 1e-9):
                    print('  %s: point (%g, %g) outside its %dx%d box' % (ch, x, y, w, GLYPH_H))
                    ok = False
        cov = coverage(flat, w, GLYPH_H, 8).reshape(GLYPH_H, 8, w, 8).mean(axis=(1, 3))
        tgt = np.array([[{'#': 1.0, '+': 0.5, '.': 0.0}[c] for c in r] for r in rows])
        err = float(np.abs(cov - tgt).mean())
        worst = float(np.abs(cov - tgt).max())
        print('  %s  mean |error| %.3f  worst pixel %.2f' % (ch, err, worst))
        if err > 0.15:
            ok = False
    return ok


# ---------------------------------------------------------------------------
# Writers
# ---------------------------------------------------------------------------
def quad_arc(p0, p1, c):
    """A quarter ellipse as two quadratic Beziers (45 degrees each, exact for a
    circle to 0.03 %, and an affine map keeps that for an ellipse): [(ctrl, end)]."""
    rx, ry, a0, d = ellipse(p0, p1, c)
    out = []
    for k in range(2):
        ta, tb = a0 + d * k / 2, a0 + d * (k + 1) / 2
        m = (ta + tb) / 2
        sc = 1 / math.cos((tb - ta) / 2)
        ctrl = (c[0] + rx * sc * math.cos(m), c[1] + ry * sc * math.sin(m))
        end = (c[0] + rx * math.cos(tb), c[1] + ry * math.sin(tb))
        out.append((ctrl, end))
    out[-1] = (out[-1][0], p1)
    return out


def write_ttf(path, bmp):
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    U = 100                       # font units per bitmap pixel
    upm = 1000
    asc = GLYPH_H * U + 100
    spacing, space_adv = 3, 12    # TitleFontGlyphs.inc: SPACING / SPACE_ADV

    def fy(y):                    # screen y (down) -> font y (up, baseline 0)
        return round((GLYPH_H - y) * U)

    def fx(x):
        return round(x * U)

    names = ['.notdef', 'space'] + list(sorted(OUTLINES))
    cmap = {32: 'space'}
    cmap.update({ord(ch): ch for ch in OUTLINES})
    glyphs, metrics = {}, {}
    pen = TTGlyphPen(None)
    glyphs['.notdef'] = pen.glyph()
    metrics['.notdef'] = (space_adv * U, 0)
    glyphs['space'] = TTGlyphPen(None).glyph()
    metrics['space'] = (space_adv * U, 0)
    for ch in sorted(OUTLINES):
        pen = TTGlyphPen(None)
        # TrueType fills clockwise in y-up space, i.e. counter-clockwise on
        # screen: every contour is drawn reversed.
        for segs in oriented(ch):
            segs = reverse(segs)
            pen.moveTo((fx(segs[0][1][0]), fy(segs[0][1][1])))
            for s in segs:
                if s[0] == 'L':
                    pen.lineTo((fx(s[2][0]), fy(s[2][1])))
                elif s[0] == 'Q':
                    pen.qCurveTo((fx(s[3][0]), fy(s[3][1])), (fx(s[2][0]), fy(s[2][1])))
                else:
                    for ctrl, end in quad_arc(s[1], s[2], s[3]):
                        pen.qCurveTo((fx(ctrl[0]), fy(ctrl[1])), (fx(end[0]), fy(end[1])))
            pen.closePath()
        g = pen.glyph()
        glyphs[ch] = g
        w = len(bmp[ch][0])
        metrics[ch] = ((w + spacing) * U, 0)

    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=asc, descent=-100)
    fb.setupNameTable({'familyName': 'RE1 Title', 'styleName': 'Regular'})
    fb.setupOS2(sTypoAscender=asc, sTypoDescender=-100, usWinAscent=asc, usWinDescent=100,
                sCapHeight=GLYPH_H * U, sxHeight=GLYPH_H * U)
    fb.setupPost()
    # Q and X draw with overlapping contours.
    for ch in ('Q', 'X'):
        fb.font['glyf'][ch].flags[0] |= 0x40          # OVERLAP_SIMPLE
    fb.save(path)


def svg_path(segs):
    def n(v):
        return ('%.4f' % v).rstrip('0').rstrip('.')
    out = ['M %s %s' % (n(segs[0][1][0]), n(segs[0][1][1]))]
    for s in segs:
        if s[0] == 'L':
            out.append('L %s %s' % (n(s[2][0]), n(s[2][1])))
        elif s[0] == 'Q':
            out.append('Q %s %s %s %s' % (n(s[3][0]), n(s[3][1]), n(s[2][0]), n(s[2][1])))
        else:
            rx, ry, _, d = ellipse(s[1], s[2], s[3])
            out.append('A %s %s 0 0 %d %s %s' % (n(rx), n(ry), 1 if d > 0 else 0,
                                                 n(s[2][0]), n(s[2][1])))
    return ' '.join(out) + ' Z'


def write_svg(path, bmp):
    pad, gap = 2, 3
    x = pad
    parts = []
    for ch in sorted(OUTLINES):
        d = ' '.join(svg_path(s) for s in oriented(ch))
        parts.append('  <path id="glyph-%s" transform="translate(%d %d)" d="%s"/>' % (ch, x, pad, d))
        x += len(bmp[ch][0]) + gap
    w, h = x - gap + pad, GLYPH_H + 2 * pad
    with open(path, 'w', newline='\n') as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">\n'
                % (w, h, w * 16, h * 16))
        f.write('  <!-- Generated by tools/gen_title_font_outlines.py. 1 unit = 1 pixel of the\n'
                '       11-row bitmap font; nonzero fill. -->\n')
        f.write('  <rect width="100%" height="100%" fill="#000"/>\n')
        f.write('  <g fill="#fff" fill-rule="nonzero">\n')
        f.write('\n'.join(parts) + '\n  </g>\n</svg>\n')


def write_preview(path, bmp):
    import numpy as np
    from PIL import Image
    S = 8
    lines = ['ABCDEFGHIJKLM', 'NOPQRSTUVWXYZ', 'NEW GAME  LOAD GAME', 'EXTRA  OPTIONS',
             'BATTLE GAME  BACK']
    ims = []
    for text in lines:
        w = sum(12 if c == ' ' else len(bmp[c][0]) + 3 for c in text) - 3
        cs, x = [], 0
        for c in text:
            if c == ' ':
                x += 12
                continue
            for cont in oriented(c):
                cs.append([(px + x, py) for px, py in flatten(cont, 16)])
            x += len(bmp[c][0]) + 3
        cov = coverage(cs, w, GLYPH_H, S * 4).reshape(GLYPH_H * S, 4, w * S, 4).mean(axis=(1, 3))
        ims.append(Image.fromarray((cov * 255).astype(np.uint8)))
    pv = Image.new('L', (max(i.width for i in ims) + 8 * S, sum(i.height + 4 * S for i in ims) + 4 * S))
    y = 4 * S
    for i in ims:
        pv.paste(i, (4 * S, y))
        y += i.height + 4 * S
    pv.save(path)


def main():
    bmp = bitmap_glyphs()
    if sorted(bmp) != sorted(OUTLINES):
        print('glyph sets differ: bitmap %s, outlines %s' % (''.join(sorted(bmp)),
                                                             ''.join(sorted(OUTLINES))))
        return 1
    if sys.argv[1:2] == ['--check']:
        ok = check_against_bitmap(bmp)
        print('outlines match the bitmap' if ok else 'the check failed')
        return 0 if ok else 1
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join('assets', 'fonts')
    print('check against the bitmap:')
    if not check_against_bitmap(bmp):
        print('outlines do not match the bitmap glyphs')
        return 1
    os.makedirs(out, exist_ok=True)
    write_ttf(os.path.join(out, 'title_font.ttf'), bmp)
    write_svg(os.path.join(out, 'title_font.svg'), bmp)
    write_preview(os.path.join(out, 'title_font_hd_preview.png'), bmp)
    print('wrote', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
