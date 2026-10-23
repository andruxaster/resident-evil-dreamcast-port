#pragma once
// Room backgrounds as WebP.
//
// The game reads a camera's background as Stage<n>/RC<stage><room><cam>.pak (an
// LZW stream holding a 320x240 16 bpp TIM) and takes a .webp of the same name
// in its place (src/game/RoomBackgroundWebp.cpp). Every migration writes its
// backgrounds as .pak first - that is the format the converters and their
// verifiers were built around - and applyBackgroundFormat then turns a finished
// tree into the format the user asked for.
//
// Encoding goes through ffmpeg's libwebp, like every other encode in this tool;
// decoding (to check what was written) is the game's own vendored decoder,
// src/third_party/simplewebp.h.

#include "core/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

enum class BackgroundFormat {
    Pak,           // what the PC release ships; no conversion
    WebpLossless,  // the same pixels, about 80 % of the .pak's size
    WebpLossy,     // about 40 % of the .pak's size at quality 90
};

struct BackgroundFormatOptions {
    BackgroundFormat format = BackgroundFormat::Pak;
    // libwebp quality for WebpLossy, 0-100.
    int quality = 90;
    std::string ffmpegPath = "ffmpeg";
};

constexpr int kBackgroundWidth = 320;
constexpr int kBackgroundHeight = 240;

// The background named `pakName` in `dir`, in either format: the file's own
// name (case-insensitive), .pak before .webp, "" when the folder has neither.
// Coverage checks use this so a tree converted to WebP still counts.
std::string findBackgroundFile(const std::string& dir, const std::string& pakName);

// A background .pak -> 320 * 240 * 3 RGB888, each 5-bit channel expanded as the
// game's display_image does (v * 255 / 31), so a lossless .webp made from it
// decodes to the very pixels the game would have built from the .pak. False
// (with a reason) when the stream is not a 320x240 16 bpp TIM.
bool backgroundPakToRgb(const std::vector<uint8_t>& pak,
                        std::vector<uint8_t>* rgb, std::string* why);

// Decode a WebP file to RGBA8888. False on anything that is not a still WebP.
bool decodeWebp(const std::vector<uint8_t>& file, std::vector<uint8_t>* rgba,
                int* width, int* height);

// Bring every Stage*/RC*.pak background under `tree` (a USA/JPN tree or a mode
// overlay) to `o.format`:
//
//   WebP  each .pak is encoded, read back and checked (the dimensions always;
//         for lossless, every pixel against the .pak), installed as
//         RC*.webp, and only then removed. A .pak that is not a background
//         TIM is left alone and reported.
//   Pak   nothing is converted, but a .webp left beside a .pak by an earlier
//         run is removed - the game prefers the .webp, so it would shadow the
//         file this run just wrote.
//
// A tree that has no .pak backgrounds is not an error. Honours cancellation
// between batches; what was already installed stays.
bool applyBackgroundFormat(const std::string& tree,
                           const BackgroundFormatOptions& o,
                           const Progress& progress, std::string* error);

}  // namespace re1
