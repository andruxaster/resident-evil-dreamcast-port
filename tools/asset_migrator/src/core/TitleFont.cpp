#include "core/TitleFont.h"

#include "core/Util.h"

#include <cstring>

namespace re1 {

namespace {

const int kGlyphH = 11;
const int kSpacing = 3;
const int kSpaceAdvance = 12;

struct Glyph {
    char ch;
    const char* rows[kGlyphH];
};

const Glyph kGlyphs[] = {
#include "core/TitleFontGlyphs.inc"
};


}  // namespace

std::vector<uint8_t> buildTitleFont() {
    std::vector<uint8_t> out;
    out.push_back('T');
    out.push_back('F');
    out.push_back('N');
    out.push_back('T');
    out.push_back(static_cast<uint8_t>(kGlyphH));
    out.push_back(static_cast<uint8_t>(kSpacing));
    out.push_back(static_cast<uint8_t>(kSpaceAdvance));
    out.push_back(static_cast<uint8_t>(sizeof(kGlyphs) / sizeof(kGlyphs[0])));
    for (const Glyph& g : kGlyphs) {
        const size_t w = std::strlen(g.rows[0]);
        out.push_back(static_cast<uint8_t>(g.ch));
        out.push_back(static_cast<uint8_t>(w));
        for (int y = 0; y < kGlyphH; ++y) {
            for (size_t x = 0; x < w; ++x) {
                const char c = g.rows[y][x];
                out.push_back(c == '#' ? 2 : (c == '+' ? 1 : 0));
            }
        }
    }
    return out;
}

bool checkTitleFont(const std::vector<uint8_t>& d, int* glyphs) {
    if (d.size() < 8 || std::memcmp(d.data(), "TFNT", 4) != 0) return false;
    const size_t h = d[4];
    const size_t count = d[7];
    if (h != static_cast<size_t>(kGlyphH) || count == 0) return false;
    size_t pos = 8;
    for (size_t i = 0; i < count; ++i) {
        if (pos + 2 > d.size()) return false;
        const size_t w = d[pos + 1];
        if (w == 0 || pos + 2 + w * h > d.size()) return false;
        pos += 2 + w * h;
    }
    if (pos != d.size()) return false;
    if (glyphs) *glyphs = static_cast<int>(count);
    return true;
}

}  // namespace re1
