#pragma once
// Sega sound-library tone-bank directory and 0x20-byte key-layer parser.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

struct SaturnToneLayer {
    size_t offset = 0;
    uint8_t startKey = 0;
    uint8_t endKey = 0;
    uint32_t sampleReference = 0;
    uint16_t loopStart = 0;
    uint16_t loopEnd = 0;
    uint32_t envelope = 0;
    uint8_t soundDirect = 0;
    uint8_t totalLevel = 0;
    uint16_t modulation = 0;
    uint16_t lfo = 0;
    uint16_t effectInput = 0;
    uint8_t directPan = 0;
    uint8_t baseNote = 0;
    int8_t fineTune = 0;
    uint8_t velocityCurve = 0;
};

struct SaturnToneVoice {
    size_t offset = 0;
    uint8_t playModeAndBendRange = 0;
    uint8_t portamentoTime = 0;
    int8_t layerCountMinusOne = 0;
    int8_t volumeBias = 0;
    std::vector<SaturnToneLayer> layers;
};

struct SaturnToneBank {
    uint16_t mixerOffset = 0;
    uint16_t velocityCurveOffset = 0;
    std::vector<uint16_t> voiceOffsets;
    std::vector<SaturnToneVoice> voices;
};

struct SaturnToneSampleReference {
    uint32_t scspRegisterAddress = 0;
    uint32_t scspSampleAddress = 0;
    uint32_t bankOffset = 0;
    size_t sourceFileOffset = 0;
    size_t sampleBytesThroughLoopEnd = 0;
    bool pcm8Bit = false;
    uint8_t loopMode = 0;
};

// Parses the Sega tone-bank header and voice/layer directory, without
// interpreting the sample bytes or rendering SCSP voices.
bool parseSaturnToneBank(const std::vector<uint8_t>& data,
                         SaturnToneBank* out, std::string* error);

// Resolves a tone-layer address inside one CD_PACK sound-RAM segment. bankBase
// is the segment destination relative to SCSP RAM base 0x25A00000.
bool resolveSaturnToneSampleReference(
    const SaturnToneLayer& layer, uint32_t bankBase,
    uint32_t segmentFileOffset, uint32_t segmentSize,
    SaturnToneSampleReference* out, std::string* error);

}  // namespace re1
