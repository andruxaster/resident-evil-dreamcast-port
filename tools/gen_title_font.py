#!/usr/bin/env python3
"""gen_title_font.py - build a bitmap font from the title-screen option art.

The Director's Cut's DATA/BT367OAB.TIM holds the title option cells (seven
256x80 4bpp cells, 0x2840 bytes each; see src/game/TitleScreen.cpp). Between
them the cells spell PRESS ANY BUTTON, NEW GAME, LOAD GAME, STANDARD, TRAINING
and ADVANCED, which covers 17 capitals:  A B D E G I L M N O P R S T U W Y.
The other nine (C F H J K Q V X Z) are drawn by hand, see HAND_DRAWN.

Each text line is cut into glyphs by column runs and named by its known string;
a line is accepted only when its run count equals its letter count, so a merged
pair can never be mislabelled. The first bright (palette index 1) occurrence of
a letter wins.

Output (BMFont text format, 1 page, white glyphs on transparent):
    <out>/title_font.png   atlas
    <out>/title_font.fnt   metrics
    <out>/title_font_preview.png
    tools/asset_migrator/src/core/TitleFontGlyphs.inc   the same glyphs as a C++
        table. The Saturn disc has no such font (its menu uses a different
        typeface) so the asset migrator carries the glyphs and writes them to
        SATURN/Data/TITLEFNT.BIN, which the port's Saturn title menu reads.
        Re-run this script and commit the .inc if a glyph changes - and then
        tools/gen_title_font_outlines.py, whose hand-drawn outlines of the same
        letters (the HD menu text) are checked against this bitmap.

Usage (repo root):  python tools/gen_title_font.py [BT367OAB.TIM] [out_dir]
"""
import struct
import sys
import os
from PIL import Image

SRC = sys.argv[1] if len(sys.argv) > 1 else 'assets/DC/Data/bt367oab.tim'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'assets/fonts'

CHUNK, CW, CH = 0x2840, 256, 80
GLYPH_H = 11            # cap height of the option art (copyright lines are smaller)
SPACING = 3             # gap the art leaves between letters
SPACE_ADV = 12          # word gap in the art (NEW | GAME)
ALPHA = {1: 255, 2: 128, 3: 64}   # palette index -> alpha (115 / 57 / 32 grey)

# cell -> strings of its GLYPH_H-tall lines, top to bottom
LINES = {
    0: ['PRESS ANY BUTTON'],
    1: ['NEW GAME', 'LOAD GAME'],
    2: ['NEW GAME', 'LOAD GAME'],
    3: ['STANDARD', 'TRAINING', 'ADVANCED'],
    4: ['STANDARD', 'TRAINING', 'ADVANCED'],
    5: ['STANDARD', 'TRAINING', 'ADVANCED'],
}

data = open(SRC, 'rb').read()


def cell_pixels(c):
    o = c * CHUNK
    return [[(data[o + 0x40 + y * 128 + x // 2] >> (4 if x & 1 else 0)) & 15
             for x in range(CW)] for y in range(CH)]


def runs(flags):
    out, s = [], None
    for i, f in enumerate(flags + [False]):
        if f and s is None:
            s = i
        elif not f and s is not None:
            out.append((s, i - 1))
            s = None
    return out


glyphs = {}          # char -> (alpha rows, bright)
for c, strings in LINES.items():
    px = cell_pixels(c)
    rows = runs([any(r) for r in px])
    bands = [b for b in rows if b[1] - b[0] + 1 == GLYPH_H]
    if len(bands) != len(strings):
        sys.exit(f'cell {c}: {len(bands)} text lines, expected {len(strings)}')
    for (y0, y1), text in zip(bands, strings):
        letters = text.replace(' ', '')
        cols = runs([any(px[y][x] for y in range(y0, y1 + 1)) for x in range(CW)])
        if len(cols) != len(letters):
            print(f'  skip cell {c} "{text}": {len(cols)} runs for {len(letters)} letters')
            continue
        hist = {}
        for y in range(y0, y1 + 1):
            for v in px[y]:
                hist[v] = hist.get(v, 0) + 1
        bright = hist.get(1, 0) > hist.get(2, 0)
        for (x0, x1), ch in zip(cols, letters):
            if ch in glyphs and (glyphs[ch][1] or not bright):
                continue
            rows_a = [[ALPHA.get(px[y][x], 0) for x in range(x0, x1 + 1)]
                      for y in range(y0, y1 + 1)]
            glyphs[ch] = (rows_a, bright)

# The nine capitals the option art never spells (no other game art uses this
# typeface). Drawn by hand on the same grid as the extracted glyphs - 11 rows,
# 3 px strokes, '+' = the half-tone corner pixel the original uses - and
# shaped after their nearest extracted neighbours (C/Q after O and G, H/K/X/V
# after N/A/Y, F after E, J after U, Z after the E/T horizontals).
HAND_DRAWN = {
    'C': ['..+#####+.', '.#########', '+##+...+##', '###.......', '###.......',
          '###.......', '###.......', '###.......', '+##+...+##', '.#########',
          '..+#####+.'],
    'F': ['#########', '#########', '###......', '###......', '########.',
          '########.', '###......', '###......', '###......', '###......',
          '###......'],
    'H': ['###....###', '###....###', '###....###', '###....###', '##########',
          '##########', '###....###', '###....###', '###....###', '###....###',
          '###....###'],
    'J': ['......###', '......###', '......###', '......###', '......###',
          '......###', '......###', '......###', '+##...###', '.########',
          '..+####+.'],
    'K': ['###....###', '###...###.', '###..###..', '###.###...', '#######...',
          '######....', '#######...', '###.###...', '###..###..', '###...###.',
          '###....###'],
    'Q': ['..+#####+..', '.#########.', '+##+...+##+', '###.....###', '###.....###',
          '###.....###', '###.....###', '###...#.###', '+##+..###+.', '.#########+',
          '..+#####+##'],
    'V': ['###.....###', '###.....###', '###.....###', '+##+...+##+', '.###...###.',
          '.+##+.+##+.', '..###.###..', '..+##.##+..', '...#####...', '...+###+...',
          '....+#+....'],
    'X': ['###.....###', '.###...###.', '..###.###..', '...#####...', '...+###+...',
          '....###....', '...+###+...', '...#####...', '..###.###..', '.###...###.',
          '###.....###'],
    'Z': ['##########', '##########', '.......###', '......###.', '.....###..',
          '....###...', '...###....', '..###.....', '.###......', '##########',
          '##########'],
}
_A = {'#': 255, '+': 128, '.': 0}
for _ch, _rows in HAND_DRAWN.items():
    assert len(_rows) == GLYPH_H and len({len(r) for r in _rows}) == 1, _ch
    glyphs.setdefault(_ch, ([[_A[c] for c in r] for r in _rows], True))

chars = sorted(glyphs)
print('glyphs:', ''.join(chars), f'({len(chars)})')

# atlas: one row of glyphs with a 1px gutter
atlas_w = sum(len(glyphs[ch][0][0]) + 1 for ch in chars) + 1
atlas = Image.new('RGBA', (atlas_w, GLYPH_H + 2), (255, 255, 255, 0))
fnt = [f'info face="RE1 Title" size={GLYPH_H} bold=0 italic=0 charset="" unicode=0 '
       f'stretchH=100 smooth=0 aa=1 padding=0,0,0,0 spacing=1,1',
       f'common lineHeight={GLYPH_H} base={GLYPH_H} scaleW={atlas_w} scaleH={GLYPH_H + 2} '
       f'pages=1 packed=0',
       'page id=0 file="title_font.png"',
       f'chars count={len(chars) + 1}']
x = 1
for ch in chars:
    a = glyphs[ch][0]
    w = len(a[0])
    for yy, row in enumerate(a):
        for xx, v in enumerate(row):
            atlas.putpixel((x + xx, 1 + yy), (255, 255, 255, v))
    fnt.append(f'char id={ord(ch)} x={x} y=1 width={w} height={GLYPH_H} '
               f'xoffset=0 yoffset=0 xadvance={w + SPACING} page=0 chnl=15')
    x += w + 1
fnt.append(f'char id=32 x=0 y=0 width=0 height=0 xoffset=0 yoffset=0 '
           f'xadvance={SPACE_ADV} page=0 chnl=15')

os.makedirs(OUT, exist_ok=True)
atlas.save(os.path.join(OUT, 'title_font.png'))
open(os.path.join(OUT, 'title_font.fnt'), 'w').write('\n'.join(fnt) + '\n')


def render(text):
    w = sum(glyphs[c][0][0].__len__() + SPACING if c != ' ' else SPACE_ADV for c in text)
    im = Image.new('RGBA', (w, GLYPH_H), (0, 0, 0, 255))
    x = 0
    for c in text:
        if c == ' ':
            x += SPACE_ADV
            continue
        a = glyphs[c][0]
        for yy, row in enumerate(a):
            for xx, v in enumerate(row):
                im.putpixel((x + xx, yy), (v, v, v, 255))
        x += len(a[0]) + SPACING
    return im


lines = [''.join(c for c in s if c == ' ' or c in glyphs) for s in
         ['ABCDEFGHIJKLM', 'NOPQRSTUVWXYZ', 'PRESS ANY BUTTON',
          'STANDARD TRAINING ADVANCED', 'QUICK FOX JUMPS VEXED']]
ims = [render(s) for s in lines]
pv = Image.new('RGBA', (max(i.width for i in ims) + 8, sum(i.height + 4 for i in ims) + 4),
               (0, 0, 0, 255))
yy = 4
for i in ims:
    pv.paste(i, (4, yy))
    yy += i.height + 4
pv.resize((pv.width * 3, pv.height * 3), Image.Resampling.NEAREST).save(
    os.path.join(OUT, 'title_font_preview.png'))

INC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   'asset_migrator', 'src', 'core', 'TitleFontGlyphs.inc')
SHADE = {255: '#', 128: '+', 0: '.'}
with open(INC, 'w', newline='\n') as f:
    f.write('// Generated by tools/gen_title_font.py - do not edit by hand.\n')
    f.write('// One entry per glyph: character, then %d rows of [#+.]: # = full,\n' % GLYPH_H)
    f.write('// + = half-tone corner pixel, . = clear.\n')
    f.write('// GLYPH_H %d  SPACING %d  SPACE_ADV %d\n' % (GLYPH_H, SPACING, SPACE_ADV))
    for ch in chars:
        rows = ', '.join('"%s"' % ''.join(SHADE[v] for v in r) for r in glyphs[ch][0])
        f.write("{'%s', {%s}},\n" % (ch, rows))
print('wrote', OUT, 'and', INC)
