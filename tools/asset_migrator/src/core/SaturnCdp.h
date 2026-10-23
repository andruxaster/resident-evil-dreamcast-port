#pragma once
// Parser for the Saturn sound driver's CD_PACK container. This exposes the
// payload segments only; the sample and sequence formats are handled separately.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace re1 {

struct SaturnCdpSegment {
    uint32_t sourceOffset = 0;
    uint32_t sizeBytes = 0;
    uint32_t loadAddress = 0;
    uint32_t continuation = 0;
};

struct SaturnCdpPack {
    uint32_t marker = 0;
    std::vector<SaturnCdpSegment> segments;
};

// Reads the CD_PACK Ver.1.0 segment table and validates every source range.
bool parseSaturnCdpPack(const std::vector<uint8_t>& data, SaturnCdpPack* out,
                        std::string* error);

}  // namespace re1
