#include "core/SaturnToneBank.h"

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

bool parseSaturnToneBank(const std::vector<uint8_t>& data,
                         SaturnToneBank* out, std::string* error) {
    if (error) error->clear();
    if (out == nullptr) {
        setError(error, "null tone-bank output");
        return false;
    }
    out->mixerOffset = 0;
    out->velocityCurveOffset = 0;
    out->voiceOffsets.clear();
    out->voices.clear();
    if (data.size() < 10) {
        setError(error, "tone-bank header is truncated");
        return false;
    }

    out->mixerOffset = readBe16(data.data());
    out->velocityCurveOffset = readBe16(data.data() + 2);
    if (out->mixerOffset < 10 || out->mixerOffset > 8 + 128 * 2 ||
        ((out->mixerOffset - 8) & 1) != 0 ||
        !hasRange(data.size(), 8, out->mixerOffset - 8)) {
        setError(error, "tone bank has an invalid voice-offset table");
        return false;
    }
    if (out->velocityCurveOffset >= data.size()) {
        setError(error, "tone-bank velocity table is outside the bank");
        return false;
    }

    for (size_t offset = 8; offset < out->mixerOffset; offset += 2) {
        const uint16_t voiceOffset = readBe16(data.data() + offset);
        if (!hasRange(data.size(), voiceOffset, 4)) {
            out->voiceOffsets.clear();
            setError(error, "tone bank voice points outside the bank");
            return false;
        }
        out->voiceOffsets.push_back(voiceOffset);
    }

    for (uint16_t voiceOffset : out->voiceOffsets) {
        SaturnToneVoice voice;
        voice.offset = voiceOffset;
        voice.playModeAndBendRange = data[voiceOffset];
        voice.portamentoTime = data[voiceOffset + 1];
        voice.layerCountMinusOne = (int8_t)data[voiceOffset + 2];
        voice.volumeBias = (int8_t)data[voiceOffset + 3];
        if (voice.layerCountMinusOne < 0) {
            out->voices.push_back(voice);
            continue;
        }
        const size_t layerCount = (size_t)(uint8_t)voice.layerCountMinusOne + 1;
        if (layerCount > 128 ||
            !hasRange(data.size(), (size_t)voiceOffset + 4, layerCount * 0x20)) {
            out->voices.clear();
            setError(error, "tone bank key layers are truncated");
            return false;
        }
        voice.layers.reserve(layerCount);
        for (size_t i = 0; i < layerCount; ++i) {
            const size_t layerOffset = (size_t)voiceOffset + 4 + i * 0x20;
            SaturnToneLayer layer;
            layer.offset = layerOffset;
            layer.startKey = data[layerOffset];
            layer.endKey = data[layerOffset + 1];
            layer.sampleReference = readBe32(data.data() + layerOffset + 2);
            layer.loopStart = readBe16(data.data() + layerOffset + 6);
            layer.loopEnd = readBe16(data.data() + layerOffset + 8);
            layer.envelope = readBe32(data.data() + layerOffset + 0x0A);
            layer.soundDirect = data[layerOffset + 0x0E];
            layer.totalLevel = data[layerOffset + 0x0F];
            layer.modulation = readBe16(data.data() + layerOffset + 0x10);
            layer.lfo = readBe16(data.data() + layerOffset + 0x14);
            layer.effectInput = readBe16(data.data() + layerOffset + 0x16);
            layer.directPan = data[layerOffset + 0x18];
            layer.baseNote = data[layerOffset + 0x19];
            layer.fineTune = (int8_t)data[layerOffset + 0x1A];
            layer.velocityCurve = data[layerOffset + 0x1D];
            voice.layers.push_back(layer);
        }
        out->voices.push_back(voice);
    }
    return true;
}

bool resolveSaturnToneSampleReference(
    const SaturnToneLayer& layer, uint32_t bankBase,
    uint32_t segmentFileOffset, uint32_t segmentSize,
    SaturnToneSampleReference* out, std::string* error) {
    if (error) error->clear();
    if (out == nullptr) {
        setError(error, "null SCSP sample-reference output");
        return false;
    }

    // The bank reference becomes the SCSP's packed control/SA register pair
    // after adding the area base; its top address byte carries PCM8B and LPCTL.
    const uint64_t sourceAddress = (uint64_t)bankBase + layer.sampleReference;
    if (sourceAddress > 0xFFFFFFFFu) {
        setError(error, "SCSP sample reference overflows 32 bits");
        return false;
    }
    const uint8_t control = (uint8_t)(sourceAddress >> 16);
    const uint32_t sampleAddress =
        ((uint32_t)(control & 0x0F) << 16) | (uint32_t)(sourceAddress & 0xFFFF);
    if (sampleAddress < bankBase) {
        setError(error, "SCSP sample reference precedes its bank");
        return false;
    }
    const uint32_t bankOffset = sampleAddress - bankBase;
    if (bankOffset >= segmentSize || layer.loopStart > layer.loopEnd ||
        segmentFileOffset > (size_t)-1 - bankOffset) {
        setError(error, "SCSP sample reference is outside its CD_PACK segment");
        return false;
    }
    const bool pcm8Bit = (control & 0x10) != 0;
    // LSA/LEA are sample indices from SA, not byte offsets.
    const size_t sampleBytesThroughLoopEnd =
        ((size_t)layer.loopEnd + 1) * (pcm8Bit ? 1 : 2);
    if (sampleBytesThroughLoopEnd > segmentSize - bankOffset) {
        setError(error, "SCSP loop end exceeds the CD_PACK segment");
        return false;
    }

    out->scspRegisterAddress = (uint32_t)sourceAddress;
    out->scspSampleAddress = sampleAddress;
    out->bankOffset = bankOffset;
    out->sourceFileOffset = (size_t)segmentFileOffset + bankOffset;
    out->sampleBytesThroughLoopEnd = sampleBytesThroughLoopEnd;
    out->pcm8Bit = pcm8Bit;
    out->loopMode = (control >> 5) & 0x03;
    return true;
}

}  // namespace re1
