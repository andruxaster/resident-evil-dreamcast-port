#pragma once
// The Sega Saturn's room background container, `STAGEn/ROOMxyy.BSS`, decoded
// into the 320x240 RGB888 frames the PC background writer wants.
//
// Layout (see tools/saturn_room.py and tools/saturn_bss_to_pak.py):
//
//   +0x00  big-endian u32 offset table; its FIRST entry is the table's own
//          size in bytes, so the count is first/4 (normally 20)
//   slots [0, n/2)      palettes: 512 B = 256 Saturn RGB555 words (big-endian,
//                       bit 15 is the STP/transparency flag, R in bits 0-4,
//                       G in 5-9, B in 10-14)
//   slots [n/2, n)      images: one LZ77 stream (SaturnLz77.h) -> 76,800 B =
//                       320x240 8bpp, stored as 8x8 cells, 40 per row, 30 rows,
//                       each cell row-major
//
// Palette slot k belongs to image slot n/2 + k. An unused slot is the 4-byte
// placeholder 00 00 00 01, and STAGE3/ROOM311.BSS is not a Saturn container at
// all (an unconverted PS1 MDEC frame) - both are rejected here.
//
// A C++ port of the same code in tools/saturn_room.py; the output is
// byte-identical to tools/saturn_bss_to_pak.py's frames.

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

struct SaturnBssFrame {
    int index = 0;             // camera slot, i.e. the PC pak's camera digit
    std::vector<uint8_t> rgb;  // 320 * 240 * 3, RGB888
};

constexpr int kSaturnBgWidth = 320;
constexpr int kSaturnBgHeight = 240;

// Every camera frame in the container, in slot order. False (with a reason) on
// anything that is not a Saturn .BSS.
bool saturnBssDecode(const uint8_t* src, size_t n,
                     std::vector<SaturnBssFrame>* out, std::string* error);

}  // namespace re1
