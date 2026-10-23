#include "core/SaturnLz77.h"

namespace re1 {

bool saturnLz77Decode(const uint8_t* src, size_t n, std::vector<uint8_t>* out,
                      size_t* consumed, std::string* error) {
    out->clear();
    if (n == 0) {
        if (error) *error = "empty LZ77 stream";
        return false;
    }
    size_t sp = 1;
    unsigned r6 = src[0];
    int r7 = 9;
    bool overrun = false;

    auto byteAt = [&](size_t i) -> unsigned {
        if (i >= n) {
            overrun = true;
            return 0;
        }
        return src[i];
    };
    // BT tests the carry SHLR produced, i.e. bit 0 BEFORE the shift.
    auto bit = [&]() -> unsigned {
        if (--r7 == 0) {
            r6 = byteAt(sp++);
            r7 = 8;
        }
        const unsigned b = r6 & 1;
        r6 >>= 1;
        return b;
    };

    for (;;) {
        while (bit()) out->push_back((uint8_t)byteAt(sp++));
        int32_t offset;
        unsigned length;
        if (bit()) {  // long form
            const unsigned first = byteAt(sp);
            const unsigned second = byteAt(sp + 1);
            sp += 2;
            const unsigned token = first | (second << 8);
            if (token == 0) break;
            const unsigned field = first & 7;
            if (field != 0) {
                length = field + 2;
            } else {
                length = byteAt(sp++) + 1;
            }
            offset = (int32_t)((token >> 3) | 0xFFFFE000u);
        } else {  // short form
            const unsigned b1 = bit();
            const unsigned b2 = bit();
            length = ((b1 << 1) | b2) + 2;
            offset = (int32_t)(byteAt(sp++) | 0xFFFFFF00u);
        }
        if (overrun) {
            if (error) *error = "LZ77 stream runs past its end";
            return false;
        }
        const int64_t pos = (int64_t)out->size() + offset;
        if (pos < 0) {
            if (error) *error = "LZ77 back-reference before the start of the output";
            return false;
        }
        size_t p = (size_t)pos;
        for (unsigned i = 0; i < length; ++i) {
            const uint8_t b = (*out)[p++];  // copy first: push_back may reallocate
            out->push_back(b);
        }
    }
    if (overrun) {
        if (error) *error = "LZ77 stream runs past its end";
        return false;
    }
    if (consumed) *consumed = sp;
    return true;
}

}  // namespace re1
