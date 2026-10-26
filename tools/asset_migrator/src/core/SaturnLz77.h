#pragma once
// The Sega Saturn build's general-purpose LZ77 (module decompressor
// FUN_0602b040, GAME1 0x0602B040). A C++ port of tools/saturn_lz77.py, which
// is verified against live emulator RAM and decodes all 363 .RDT rooms.
//
// Bit stream, LSB first, the first source byte is the bit buffer. A set bit
// copies one literal byte. A clear bit is a back-reference: one more bit picks
// the form. Long (1): a 2-byte LITTLE-endian token (0 ends the stream), offset
// (token >> 3) | 0xFFFFE000 (-8192..-1), length (first byte & 7) + 2, or the
// next byte + 1 when that field is 0. Short (0): two bits give 2..5, one byte
// | 0xFFFFFF00 is the offset (-256..-1). Offsets are backward from the current
// output position.

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

// Decode one stream starting at src[0]. Returns false (with a reason) on a
// malformed stream; `consumed` receives the bytes read up to the end token.
bool saturnLz77Decode(const uint8_t* src, size_t n, std::vector<uint8_t>* out,
                      size_t* consumed, std::string* error);

}  // namespace re1
