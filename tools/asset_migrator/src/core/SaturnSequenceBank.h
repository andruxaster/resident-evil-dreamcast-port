#pragma once
// Sega sound-library sequence-bank song headers. This parses header metadata
// only; the event stream is played by core/SaturnSound.cpp.
//
// Song header (driver 0x04EDA / 0x05B18 / 0x05B30): +0 PPQN, +2 number of
// tempo events, +4 offset of the event stream, +6 offset of the tempo loop
// point (0 = none); the tempo events (BE32 steps, BE32 us per quarter) start
// at +8.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

struct SaturnSequenceTempoRecord {
    uint32_t step = 0;
    uint32_t microsecondsPerQuarter = 0;
};

struct SaturnSequenceSongHeader {
    size_t offset = 0;
    uint16_t resolution = 0;
    uint16_t tempoEventCount = 0;
    uint16_t eventOffset = 0;
    uint16_t tempoLoopOffset = 0;
    std::vector<SaturnSequenceTempoRecord> tempos;
};

struct SaturnSequenceBank {
    std::vector<SaturnSequenceSongHeader> songs;
};

bool parseSaturnSequenceBank(const std::vector<uint8_t>& data,
                             SaturnSequenceBank* out, std::string* error);

}  // namespace re1
