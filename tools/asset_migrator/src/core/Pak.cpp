#include "core/Pak.h"

#include <cstdio>
#include <unordered_map>

namespace re1 {

namespace {
constexpr int kWidth = 320;
constexpr int kHeight = 240;

inline uint16_t ch5(uint8_t v) {
    const int r = ((int)v + 4) >> 3;
    return (uint16_t)(r > 31 ? 31 : r);
}

void putU16(std::vector<uint8_t>* out, uint16_t v) {
    out->push_back((uint8_t)(v & 0xFF));
    out->push_back((uint8_t)(v >> 8));
}
void putU32(std::vector<uint8_t>* out, uint32_t v) {
    putU16(out, (uint16_t)(v & 0xFFFF));
    putU16(out, (uint16_t)(v >> 16));
}
void putI16(std::vector<uint8_t>* out, int16_t v) {
    putU16(out, (uint16_t)v);
}
}  // namespace

std::vector<uint16_t> rgbTo555(const std::vector<uint8_t>& rgb, int width,
                               int height) {
    std::vector<uint16_t> out((size_t)width * height);
    for (size_t i = 0, n = out.size(); i < n; ++i) {
        const uint8_t r = rgb[i * 3 + 0];
        const uint8_t g = rgb[i * 3 + 1];
        const uint8_t b = rgb[i * 3 + 2];
        out[i] = (uint16_t)((ch5(b) << 10) | (ch5(g) << 5) | ch5(r));
    }
    return out;
}

std::vector<uint8_t> makeBackgroundTim(const std::vector<uint8_t>& rgb) {
    const std::vector<uint16_t> pix = rgbTo555(rgb, kWidth, kHeight);
    std::vector<uint8_t> out;
    out.reserve(20 + pix.size() * 2);
    putU32(&out, 0x10);  // TIM magic
    putU32(&out, 2);     // 16bpp, no CLUT
    // Image block: length counts ONLY the pixel bytes (not the 12 that follow).
    putU32(&out, (uint32_t)(pix.size() * 2));
    putI16(&out, 0);           // x
    putI16(&out, 240);         // y
    putU16(&out, kWidth);
    putU16(&out, kHeight);
    for (uint16_t p : pix) putU16(&out, p);
    return out;
}

std::vector<uint8_t> lzwPack(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int nbits = 0;

    auto emit = [&](uint32_t code, int width) {
        acc = (acc << width) | code;
        nbits += width;
        while (nbits >= 8) {
            nbits -= 8;
            out.push_back((uint8_t)((acc >> nbits) & 0xFF));
        }
    };

    std::unordered_map<std::string, int> table;
    int nextCode = 0x103;
    int width = 9;
    const int kMax = 0x1FFF;

    std::string cur;
    for (uint8_t byte : data) {
        std::string nxt = cur;
        nxt.push_back((char)byte);
        if (cur.empty()) {
            cur = nxt;
            continue;
        }
        if (table.count(nxt)) {
            cur = nxt;
            continue;
        }
        emit(cur.size() > 1 ? (uint32_t)table[cur] : (uint32_t)(uint8_t)cur[0],
             width);
        if (nextCode > kMax) {
            emit(0x102, width);
            table.clear();
            nextCode = 0x103;
            width = 9;
        } else {
            table[nxt] = nextCode++;
            // The decoder adds its entry AFTER reading the next code, so the
            // width has to grow one code early or it reads too few bits.
            if (nextCode > (1 << width) - 1 && width < 13) {
                emit(0x101, width);
                ++width;
            }
        }
        cur.assign(1, (char)byte);
    }
    if (!cur.empty()) {
        emit(cur.size() > 1 ? (uint32_t)table[cur] : (uint32_t)(uint8_t)cur[0],
             width);
    }
    emit(0x100, width);
    if (nbits) out.push_back((uint8_t)((acc << (8 - nbits)) & 0xFF));
    return out;
}

std::string pakName(char stageDigit, int roomId, int cam) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "RC%c%02X%X.pak", stageDigit, roomId, cam);
    return buf;
}

bool lzwUnpack(const uint8_t* src, size_t n, std::vector<uint8_t>* out,
               std::string* error) {
    out->clear();
    out->reserve(n * 4);
    // code -> (prefix, first character), as many entries as the game's own
    // table (g_pakDict, 34981 records at 0x00d2b0b0). This tool's encoder
    // resets before 0x2000, but Capcom's does not: the PC release's paks run
    // the dictionary far past that, and a table sized for our own output read
    // out of bounds on them. Entries below the current nextCode are stale
    // after a reset, but no code can name one: anything under 0x100 is a
    // literal and anything from 0x100 to 0x102 is a control code, so the
    // vector never has to be cleared.
    constexpr int kDictEntries = 34981;
    std::vector<std::pair<int, uint8_t>> dict(kDictEntries, {0, 0});

    size_t pos = 0;
    int codeSize = 9;
    int nextCode = 0x103;
    uint8_t cur = 0;
    uint8_t mask = 0x80;

    auto readCode = [&]() -> int {
        int result = 0;
        for (int bit = 1 << (codeSize - 1); bit != 0; bit >>= 1) {
            if (mask == 0x80) {
                if (pos >= n) return -1;
                cur = src[pos++];
            }
            if (cur & mask) result |= bit;
            mask >>= 1;
            if (mask == 0) mask = 0x80;
        }
        return result;
    };

    int prev = readCode();
    if (prev < 0 || prev == 0x100) return true;   // empty stream
    out->push_back((uint8_t)prev);
    while (true) {
        const int code = readCode();
        if (code < 0) {
            if (error) *error = "stream ended without a 0x100";
            return false;
        }
        if (code == 0x100) return true;
        if (code == 0x102) {   // dictionary reset
            nextCode = 0x103;
            codeSize = 9;
            const int again = readCode();
            if (again < 0 || again == 0x100) return true;
            out->push_back((uint8_t)again);
            prev = again;
            continue;
        }
        if (code == 0x101) {   // the decoder never grows the width on its own
            ++codeSize;
            continue;
        }
        const bool special = nextCode <= code;   // KwKwK
        int walk = special ? prev : code;
        std::vector<uint8_t> str;
        while (walk > 0xFF) {
            // A code past the table, or a chain longer than the table, is a
            // stream the game could not have decoded either.
            if (walk >= kDictEntries || str.size() > (size_t)kDictEntries) {
                if (error) *error = "code outside the dictionary";
                return false;
            }
            str.push_back(dict[walk].second);
            walk = dict[walk].first;
        }
        str.push_back((uint8_t)walk);
        // The original emits the buffer from its high index down to 0, so the
        // string comes out reversed with the first character LAST.
        for (size_t i = str.size(); i-- > 0;) out->push_back(str[i]);
        const uint8_t first = str.back();
        if (special) out->push_back(first);
        if (nextCode < (int)dict.size()) dict[nextCode] = {prev, first};
        ++nextCode;        prev = code;
    }
}

}  // namespace re1
