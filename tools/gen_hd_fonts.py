#!/usr/bin/env python3
"""gen_hd_fonts.py - the HD fonts, embedded in the asset migrator.

With HD textures on, the game draws text in real TrueType fonts read from the
HD folder, each named after the font file of the base tree it stands in for
(src/game/HdText.h, docs/HD_TEXTURES.md "HD fonts"):

    HD/<USA|JPN>/Data/TITLEFNT.TTF   Archivo Black - the title menu, PRESS START,
                                     NEW GAME, the Director's Cut's STANDARD ...
    HD/USA/Data/FONTUS.TTF           Archivo Narrow SemiBold - the 8x8 font
    HD/JPN/Data/FONT.TTF             (PrintText8x8) and the title's copyright
    HD/USA/Data/FONTUS_14.TTF        Libertinus Serif SemiBold - the 8x14 game
                                     text (messages, item names, files)
    HD/JPN/Data/FONT_14.TTF          M PLUS 2 SemiBold - the same for the
                                     Japanese font sheet

All four are under the SIL Open Font License 1.1, none with a Reserved Font
Name. Archivo Black and Archivo Narrow (Archivo Narrow's static SemiBold from
its upstream repository: stb_truetype draws a variable font's default instance
only) are carried unmodified. The two text fonts are SUBSETS made by this script
(--subset): Libertinus Serif SemiBold cut to Latin-1 and the punctuation the
8x14 sheet has, and M PLUS 2 - a variable font whose default
instance is Thin - instanced at SemiBold (wght 600) and cut to the 596
characters of the Japanese FONT.TIM (src/game/JpnFontTable.h) plus ASCII.

Input:  tools/asset_migrator/fonts/*.ttf (+ the *-OFL.txt beside each)
Output: tools/asset_migrator/src/core/HdFont*.inc

Each font is checked first: a TrueType outline font, not variable, with every
character it has to draw, and its licence in the name table (the OFL lets a
font travel without its OFL.txt when the file itself carries the notice).

Usage (repo root):
    python tools/gen_hd_fonts.py                  write the .inc files
    python tools/gen_hd_fonts.py --check          exit 1 when an .inc is stale
    python tools/gen_hd_fonts.py --subset LIBERTINUS_SEMIBOLD.ttf MPLUS2_VAR.ttf
        remake the two subsets in fonts/ from google/fonts'
        ofl/libertinusserif/LibertinusSerif-SemiBold.ttf and
        ofl/mplus2/MPLUS2[wght].ttf (needs fontTools), then write.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, 'asset_migrator', 'fonts')
CORE = os.path.join(HERE, 'asset_migrator', 'src', 'core')
JPN_TABLE = os.path.join(HERE, '..', 'src', 'game', 'JpnFontTable.h')
PER_LINE = 16

LETTERS = list(range(ord('A'), ord('Z') + 1)) + [ord(' ')]
PRINTABLE = list(range(0x20, 0x7F)) + [0xA9, 0xA5]    # + the 8x8 sheet's (c) and yen
# Everything the USA 8x14 sheet draws as text (HdText.cpp kUsa14).
TEXT14 = PRINTABLE + list(range(0xC0, 0x100)) + [0xB7, 0x2014, 0x2019, 0x201C, 0x201D, 0x2026]
LATIN1 = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + \
    [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2049, 0x203D]

LIBERTINUS = 'LibertinusSerif-SemiBold-Subset.ttf'
MPLUS = 'MPLUS2-SemiBold-Subset.ttf'


def jpn_codepoints():
    """The Japanese FONT.TIM's characters, from the generated table."""
    text = open(JPN_TABLE, encoding='utf-8-sig').read()
    return sorted({int(m, 16) for m in re.findall(r'\{ 0x([0-9A-Fa-f]{4}), \d,', text)})


def jobs():
    # (font file, licence file, .inc, characters it must have, what it is)
    return [
        ('ArchivoBlack-Regular.ttf', 'ArchivoBlack-OFL.txt', 'HdFontTitle.inc', LETTERS,
         'Archivo Black, Copyright 2017 The Archivo Black Project Authors\n'
         '// (https://github.com/Omnibus-Type/ArchivoBlack), unmodified'),
        ('ArchivoNarrow-SemiBold.ttf', 'ArchivoNarrow-OFL.txt', 'HdFontText.inc', PRINTABLE,
         'Archivo Narrow SemiBold, Copyright 2019 The Archivo Narrow Project Authors\n'
         '// (https://github.com/Omnibus-Type/ArchivoNarrow), unmodified'),
        (LIBERTINUS, 'LibertinusSerif-OFL.txt', 'HdFontText14.inc', TEXT14,
         'Libertinus Serif SemiBold, Copyright 2025 The Libertinus Serif Project Authors\n'
         '// (https://github.com/googlefonts/libertinus), subset to Latin-1 by gen_hd_fonts.py'),
        # M PLUS 2 may lack the interrobang (U+203D): the game draws that one
        # from the sheet.
        (MPLUS, 'MPLUS2-OFL.txt', 'HdFontTextJpn14.inc',
         [c for c in jpn_codepoints() if c != 0x203D],
         'M PLUS 2, Copyright 2021 The M+ FONTS Project Authors\n'
         '// (https://github.com/coz-m/MPLUS_FONTS), instanced at SemiBold (wght 600)\n'
         '// and subset to the Japanese FONT.TIM characters by gen_hd_fonts.py'),
    ]


def tables(path, data):
    """The sfnt table directory: {tag: (offset, length)}."""
    version, count = struct.unpack_from('>IH', data, 0)
    if version != 0x00010000:
        raise SystemExit('%s is not a TrueType outline font (sfnt version %08X)' % (path, version))
    out = {}
    for i in range(count):
        tag, _, off, length = struct.unpack_from('>4sIII', data, 12 + i * 16)
        if off + length > len(data):
            raise SystemExit('%s: table %s runs past the end of the file' % (path, tag))
        out[tag.decode('latin-1')] = (off, length)
    return out


def cmap_missing(data, tabs, codepoints):
    """The codepoints the font's Windows Unicode BMP cmap (format 4) does not map."""
    off, _ = tabs['cmap']
    _, n = struct.unpack_from('>HH', data, off)
    for i in range(n):
        pid, eid, sub = struct.unpack_from('>HHI', data, off + 4 + i * 8)
        if (pid, eid) != (3, 1):
            continue
        p = off + sub
        if struct.unpack_from('>H', data, p)[0] != 4:
            continue
        segs = struct.unpack_from('>H', data, p + 6)[0] // 2
        ends = struct.unpack_from('>%dH' % segs, data, p + 14)
        starts = struct.unpack_from('>%dH' % segs, data, p + 16 + segs * 2)
        return [c for c in codepoints if not any(s <= c <= e for s, e in zip(starts, ends))]
    return list(codepoints)


def licence(data, tabs):
    """The name table's licence description (name id 13, Windows English)."""
    off, _ = tabs['name']
    _, count, strings = struct.unpack_from('>HHH', data, off)
    for i in range(count):
        pid, eid, lang, nid, length, soff = struct.unpack_from('>6H', data, off + 6 + i * 12)
        if nid == 13 and pid == 3 and lang == 0x409:
            s = off + strings + soff
            return data[s:s + length].decode('utf-16-be')
    return ''


def check_font(path, data, needs):
    tabs = tables(path, data)
    for need in ('cmap', 'glyf', 'head', 'hhea', 'hmtx', 'loca', 'maxp', 'name'):
        if need not in tabs:
            raise SystemExit('%s has no %s table' % (path, need))
    if 'fvar' in tabs:
        raise SystemExit('%s is a variable font; stb_truetype draws only its default instance' % path)
    missing = cmap_missing(data, tabs, needs)
    if missing:
        raise SystemExit('%s lacks %s' % (path, ' '.join('U+%04X' % c for c in missing[:20])))
    if 'Open Font License' not in licence(data, tabs):
        raise SystemExit('%s carries no OFL notice in its name table' % path)


def subset(libertinus_src, mplus_src):
    from fontTools import subset as ftsubset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    def cut(font, codepoints, out):
        opts = ftsubset.Options()
        opts.layout_features = ['kern', 'liga']
        opts.name_IDs = ['*']            # keeps the copyright and licence records
        opts.notdef_outline = True
        opts.drop_tables += ['vhea', 'vmtx', 'VORG', 'BASE', 'STAT']
        sub = ftsubset.Subsetter(opts)
        sub.populate(unicodes=codepoints)
        sub.subset(font)
        font.save(out)
        print('wrote %s (%d bytes, %d glyphs)' % (out, os.path.getsize(out), font['maxp'].numGlyphs))

    cut(TTFont(libertinus_src), LATIN1, os.path.join(FONTS, LIBERTINUS))
    mplus = instancer.instantiateVariableFont(TTFont(mplus_src), {'wght': 600})
    cut(mplus, sorted(set(jpn_codepoints()) | set(range(0x20, 0x7F)) | {0x3000}),
        os.path.join(FONTS, MPLUS))


def inc_text(name, licence_file, what, data):
    lines = ['// Generated by tools/gen_hd_fonts.py - do not edit by hand.',
             '// tools/asset_migrator/fonts/%s, %d bytes:' % (name, len(data)),
             '// ' + what + ',',
             '// SIL Open Font License 1.1 (fonts/%s).' % licence_file]
    for i in range(0, len(data), PER_LINE):
        lines.append(','.join('0x%02X' % b for b in data[i:i + PER_LINE]) + ',')
    return '\n'.join(lines) + '\n'


def main():
    if sys.argv[1:2] == ['--subset']:
        if len(sys.argv) != 4:
            raise SystemExit('usage: gen_hd_fonts.py --subset LIBERTINUS.ttf MPLUS2_VAR.ttf')
        subset(sys.argv[2], sys.argv[3])
    check = sys.argv[1:2] == ['--check']
    stale = 0
    for name, licence_file, inc, needs, what in jobs():
        path = os.path.join(FONTS, name)
        data = open(path, 'rb').read()
        check_font(path, data, needs)
        if not os.path.exists(os.path.join(FONTS, licence_file)):
            raise SystemExit('%s has no %s beside it' % (name, licence_file))
        text = inc_text(name, licence_file, what, data)
        out = os.path.join(CORE, inc)
        if check:
            cur = open(out).read() if os.path.exists(out) else ''
            if cur != text:
                print('%s is stale - re-run tools/gen_hd_fonts.py' % out)
                stale = 1
            else:
                print('ok: %s holds %s (%d bytes)' % (inc, name, len(data)))
            continue
        with open(out, 'w', newline='\n') as f:
            f.write(text)
        print('wrote', out, '(%d bytes of %s)' % (len(data), name))
    return stale


if __name__ == '__main__':
    sys.exit(main())
