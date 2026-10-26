#include "core/SaturnSequenceBank.h"

#include <algorithm>

namespace re1 {
namespace {

uint16_t readBe16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

uint32_t readBe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool hasRange(size_t size, size_t offset, size_t count) {
    return offset <= size && count <= size - offset;
}

void setError(std::string* error, const char* message) {
    if (error) *error = message;
}

}  // namespace

bool parseSaturnSequenceBank(const std::vector<uint8_t>& data,
                             SaturnSequenceBank* out, std::string* error) {
    if (error) error->clear();
    if (out == nullptr) {
        setError(error, "null sequence-bank output");
        return false;
    }
    out->songs.clear();
    if (!hasRange(data.size(), 0, 2)) {
        setError(error, "sequence-bank header is truncated");
        return false;
    }

    const uint16_t songCount = readBe16(data.data());
    if (songCount == 0 || songCount > 256 ||
        !hasRange(data.size(), 2, (size_t)songCount * 4)) {
        setError(error, "sequence bank has an invalid song-offset table");
        return false;
    }
    std::vector<uint32_t> offsets;
    offsets.reserve(songCount);
    for (size_t i = 0; i < songCount; ++i) {
        const uint32_t offset = readBe32(data.data() + 2 + i * 4);
        if (!hasRange(data.size(), offset, 16)) {
            setError(error, "sequence song header points outside the bank");
            return false;
        }
        offsets.push_back(offset);
    }

    for (size_t i = 0; i < offsets.size(); ++i) {
        const size_t songOffset = offsets[i];
        size_t songEnd = data.size();
        for (size_t j = 0; j < offsets.size(); ++j) {
            if (offsets[j] > songOffset)
                songEnd = std::min(songEnd, (size_t)offsets[j]);
        }
        if (songEnd < songOffset + 16) {
            out->songs.clear();
            setError(error, "sequence song header overlaps the next song");
            return false;
        }

        SaturnSequenceSongHeader song;
        song.offset = songOffset;
        song.resolution = readBe16(data.data() + songOffset);
        song.tempoEventCount = readBe16(data.data() + songOffset + 2);
        song.eventOffset = readBe16(data.data() + songOffset + 4);
        song.tempoLoopOffset = readBe16(data.data() + songOffset + 6);
        const size_t tempoTableEnd = 8 + (size_t)song.tempoEventCount * 8;
        if (song.resolution < 24 || song.resolution > 960 ||
            song.tempoEventCount == 0 ||
            !hasRange(songEnd - songOffset, 8, (size_t)song.tempoEventCount * 8) ||
            song.eventOffset < tempoTableEnd ||
            !hasRange(songEnd - songOffset, song.eventOffset, 1) ||
            (song.tempoLoopOffset != 0 &&
             (song.tempoLoopOffset < 8 || song.tempoLoopOffset >= tempoTableEnd ||
              ((song.tempoLoopOffset - 8) & 7) != 0))) {
            out->songs.clear();
            setError(error, "sequence song header fields are invalid");
            return false;
        }
        song.tempos.reserve(song.tempoEventCount);
        for (size_t tempoIndex = 0; tempoIndex < song.tempoEventCount; ++tempoIndex) {
            const size_t tempoOffset = songOffset + 8 + tempoIndex * 8;
            song.tempos.push_back({readBe32(data.data() + tempoOffset),
                                   readBe32(data.data() + tempoOffset + 4)});
        }
        out->songs.push_back(song);
    }
    return true;
}

}  // namespace re1
