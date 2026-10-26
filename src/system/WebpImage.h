// WebpImage.h - WebP reader for port-only assets.
//
// The original game has no WebP support; this exists for the images the port
// adds or lets a player replace: room backgrounds stored as .webp instead of
// .pak, and the high-resolution texture replacements (system/HdTextures.h).
// The decoder is simplewebp (third_party/simplewebp.h, BSD-3-Clause), compiled
// into WebpImage.cpp: lossy (VP8) and lossless (VP8L), with or without alpha.
// Animated files are not supported.
#pragma once

#include <stddef.h>
#include <vector>

// Largest image accepted, per side and in total. A 32-bit process has to hold
// the decoded pixels, the decoder's planes and the upload copy at once.
#define WEBP_MAX_SIDE    8192
#define WEBP_MAX_PIXELS  (4096 * 4096)

// True when `data` starts with a RIFF/WEBP header.
bool WebpIsWebp(const unsigned char* data, size_t size);

// Decode `data` into width * height RGBA8 pixels, R in the lowest address -
// read as a little-endian dword that is 0xAABBGGRR, the layout every texture
// upload in this port uses. An image without an alpha channel decodes fully
// opaque. Returns false on a malformed, unsupported or oversized file.
bool WebpDecodeRGBA(const unsigned char* data, size_t size,
                    std::vector<unsigned char>* out, int* width, int* height);
