#include "core/SaturnCdp.h"

#include <cstring>

namespace re1 {
namespace {

constexpr size_t kHeaderSize = 0x14;
constexpr size_t kRecordSize = 0x10;
constexpr uint32_t kEndOfPack = 0xFFFFFFFFu;
const char kMagic[] = "CD_PACK Ver.1.0";

uint32_t readBe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void setError(std::string* error, const char* message) {
    if (error) *error = message;
}

}  // namespace

bool parseSaturnCdpPack(const std::vector<uint8_t>& data, SaturnCdpPack* out,
                        std::string* error) {
    if (error) error->clear();
    if (out == nullptr) {
        setError(error, "null CD_PACK output");
        return false;
    }
    out->marker = 0;
    out->segments.clear();
    if (data.size() < kHeaderSize) {
        setError(error, "CD_PACK header is truncated");
        return false;
    }
    if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
        setError(error, "not a CD_PACK Ver.1.0 file");
        return false;
    }

    out->marker = readBe32(data.data() + 0x10);
    bool terminated = false;
    for (size_t offset = kHeaderSize; offset <= data.size() - kRecordSize;
         offset += kRecordSize) {
        SaturnCdpSegment segment;
        segment.sourceOffset = readBe32(data.data() + offset);
        segment.sizeBytes = readBe32(data.data() + offset + 4);
        segment.loadAddress = readBe32(data.data() + offset + 8);
        segment.continuation = readBe32(data.data() + offset + 12);

        if (segment.sourceOffset > data.size() ||
            segment.sizeBytes > data.size() - segment.sourceOffset) {
            out->segments.clear();
            setError(error, "CD_PACK segment exceeds the file bounds");
            return false;
        }
        out->segments.push_back(segment);
        if (segment.continuation == kEndOfPack) {
            terminated = true;
            break;
        }
    }

    if (!terminated) {
        out->segments.clear();
        setError(error, "CD_PACK segment table has no terminator");
        return false;
    }
    if (out->segments.empty()) {
        setError(error, "CD_PACK contains no segments");
        return false;
    }
    return true;
}

}  // namespace re1
