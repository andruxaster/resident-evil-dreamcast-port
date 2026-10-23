#include "core/HdFonts.h"

#include "core/Util.h"

#include <cstdio>
#include <cstring>

namespace re1 {

namespace {

const uint8_t kHdFontTitle[] = {
#include "core/HdFontTitle.inc"
};

const uint8_t kHdFontText[] = {
#include "core/HdFontText.inc"
};

const uint8_t kHdFontText14[] = {
#include "core/HdFontText14.inc"
};

const uint8_t kHdFontTextJpn14[] = {
#include "core/HdFontTextJpn14.inc"
};

uint32_t getBE16(const std::vector<uint8_t>& d, size_t pos) {
    return static_cast<uint32_t>((d[pos] << 8) | d[pos + 1]);
}

uint32_t getBE32(const std::vector<uint8_t>& d, size_t pos) {
    return (getBE16(d, pos) << 16) | getBE16(d, pos + 2);
}

bool writeOne(const std::string& dir, const char* name, const std::vector<uint8_t>& data,
              const char* what, const Progress& progress, std::string* error) {
    const std::string path = joinPath(dir, name);
    if (!writeFile(path, data)) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    progress.info(std::string(what) + " -> " + path + " (" + std::to_string(data.size()) +
                  " bytes)");
    return true;
}

}  // namespace

std::vector<uint8_t> buildHdTitleFont() {
    return std::vector<uint8_t>(kHdFontTitle, kHdFontTitle + sizeof(kHdFontTitle));
}

std::vector<uint8_t> buildHdTextFont() {
    return std::vector<uint8_t>(kHdFontText, kHdFontText + sizeof(kHdFontText));
}

bool checkHdFont(const std::vector<uint8_t>& d, int* tables) {
    if (d.size() < 12 || getBE32(d, 0) != 0x00010000u) return false;
    const size_t count = getBE16(d, 4);
    if (count == 0 || 12 + count * 16 > d.size()) return false;
    static const char* const kNeed[] = {"cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp"};
    size_t found = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t rec = 12 + i * 16;
        const uint64_t off = getBE32(d, rec + 8);
        const uint64_t len = getBE32(d, rec + 12);
        if (off + len > d.size()) return false;
        for (const char* need : kNeed) {
            if (std::memcmp(d.data() + rec, need, 4) == 0) ++found;
        }
    }
    if (found != sizeof(kNeed) / sizeof(kNeed[0])) return false;
    if (tables) *tables = static_cast<int>(count);
    return true;
}

std::vector<uint8_t> buildHdText14Font(AssetVersion tree) {
    if (tree == AssetVersion::JPN) {
        return std::vector<uint8_t>(kHdFontTextJpn14, kHdFontTextJpn14 + sizeof(kHdFontTextJpn14));
    }
    return std::vector<uint8_t>(kHdFontText14, kHdFontText14 + sizeof(kHdFontText14));
}

const char* hdText14FontName(AssetVersion tree) {
    return tree == AssetVersion::JPN ? "FONT_14.TTF" : "FONTUS_14.TTF";
}

const char* hdTextFontName(AssetVersion tree) {
    return tree == AssetVersion::JPN ? "FONT.TTF" : "FONTUS.TTF";
}

bool writeHdFonts(const std::string& targetRoot, AssetVersion tree,
                  const Progress& progress, std::string* error) {
    const std::string dir =
        joinPath(joinPath(joinPath(targetRoot, "HD"), versionName(tree)), "Data");
    if (!makeDirs(dir)) {
        if (error) *error = "cannot create " + dir;
        return false;
    }
    if (!writeOne(dir, "TITLEFNT.TTF", buildHdTitleFont(), "title font (HD, Archivo Black)",
                  progress, error) ||
        !writeOne(dir, hdTextFontName(tree), buildHdTextFont(),
                  "text font (HD, Archivo Narrow SemiBold)", progress, error) ||
        !writeOne(dir, hdText14FontName(tree), buildHdText14Font(tree),
                  tree == AssetVersion::JPN ? "game text font (HD, M PLUS 2 SemiBold)"
                                            : "game text font (HD, Libertinus Serif SemiBold)",
                  progress, error)) {
        return false;
    }
    // The hand-drawn outlines an earlier version wrote; the game no longer
    // reads them.
    const std::string vec = joinPath(dir, "TITLEFNT.VEC");
    if (isRegularFile(vec)) std::remove(vec.c_str());
    return true;
}

}  // namespace re1
