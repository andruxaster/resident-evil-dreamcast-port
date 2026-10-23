#include "core/SaturnBss.h"

#include "core/SaturnLz77.h"

#include <algorithm>
#include <utility>

namespace re1 {

namespace {

constexpr size_t kCell = 8;
constexpr size_t kPixels = (size_t)kSaturnBgWidth * kSaturnBgHeight;
constexpr size_t kCellsX = kSaturnBgWidth / kCell;   // 40
constexpr size_t kCellsY = kSaturnBgHeight / kCell;  // 30
const uint8_t kPlaceholder[4] = {0x00, 0x00, 0x00, 0x01};

inline uint32_t readU32BE(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Slot k of a 5-5-5 Saturn colour word: 0..31 scaled to 0..255 by *255/31, the
// same widening tools/saturn_room.py does.
inline uint8_t ch5to8(uint32_t v) {
    return (uint8_t)(((int)(v & 31) * 255) / 31);
}

}  // namespace

bool saturnBssDecode(const uint8_t* src, size_t n,
                     std::vector<SaturnBssFrame>* out, std::string* error) {
    out->clear();
    if (n < 8) {
        if (error) *error = "file is too short to be a Saturn .BSS";
        return false;
    }
    const uint32_t first = readU32BE(src);
    // A PS1 MDEC frame (0x8000 bytes of headerless bitstream) lands here too:
    // its first word is the code count, which is never a plausible table size.
    if ((first & 7) || first < 8 || first > 0x100) {
        if (error) *error = "not a Saturn .BSS container";
        return false;
    }
    const size_t count = first / 4;
    if (count % 2 || 4 * count + 4 > n) {
        if (error) *error = "offset table does not fit the file";
        return false;
    }
    const size_t half = count / 2;

    std::vector<std::pair<size_t, size_t>> slots;   // begin, end
    slots.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t begin = readU32BE(src + 4 * i);
        const size_t end = (i + 1 < count) ? readU32BE(src + 4 * (i + 1)) : n;
        if (end < begin || end > n) {
            if (error) *error = "offset table entry out of range";
            return false;
        }
        slots.emplace_back(begin, end);
    }

    for (size_t k = 0; k < half; ++k) {
        const auto pal = slots[k];
        const auto img = slots[half + k];
        if (img.second - img.first == sizeof(kPlaceholder) &&
            std::equal(kPlaceholder, kPlaceholder + 4, src + img.first)) {
            continue;   // unused camera
        }
        if (pal.second - pal.first < 512) {
            if (error) *error = "palette slot is short";
            return false;
        }
        std::vector<uint8_t> cells;
        std::string why;
        if (!saturnLz77Decode(src + img.first, img.second - img.first, &cells,
                              nullptr, &why)) {
            if (error) *error = "frame " + std::to_string(k) + ": " + why;
            return false;
        }
        if (cells.size() != kCellsX * kCellsY * kCell * kCell) {
            if (error)
                *error = "frame " + std::to_string(k) + ": " +
                         std::to_string(cells.size()) + " bytes, expected " +
                         std::to_string(kPixels);
            return false;
        }

        SaturnBssFrame frame;
        frame.index = (int)k;
        frame.rgb.assign(kPixels * 3, 0);
        uint8_t palRGB[256][3];
        for (int i = 0; i < 256; ++i) {
            const uint32_t w = ((uint32_t)src[pal.first + 2 * i] << 8) |
                               (uint32_t)src[pal.first + 2 * i + 1];
            palRGB[i][0] = ch5to8(w);          // R in bits 0-4
            palRGB[i][1] = ch5to8(w >> 5);
            palRGB[i][2] = ch5to8(w >> 10);
        }
        // Cells are stored in row-major cell order; each cell is row-major.
        for (size_t c = 0; c < kCellsX * kCellsY; ++c) {
            const size_t cx = c % kCellsX, cy = c / kCellsX;
            const uint8_t* cell = cells.data() + c * kCell * kCell;
            for (size_t j = 0; j < kCell; ++j) {
                uint8_t* dst = frame.rgb.data() +
                               (((cy * kCell + j) * kSaturnBgWidth) + cx * kCell) * 3;
                for (size_t i = 0; i < kCell; ++i) {
                    const uint8_t* px = palRGB[cell[j * kCell + i]];
                    dst[i * 3 + 0] = px[0];
                    dst[i * 3 + 1] = px[1];
                    dst[i * 3 + 2] = px[2];
                }
            }
        }
        out->push_back(std::move(frame));
    }
    if (out->empty()) {
        if (error) *error = "no camera frames";
        return false;
    }
    return true;
}

}  // namespace re1
