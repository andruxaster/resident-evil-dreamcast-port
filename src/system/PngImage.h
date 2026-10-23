// PngImage.h - Minimal PNG reader (and writer) for port-only assets.
//
// The original game has no PNG support; this exists for data the port adds
// (the PS1 FMV subtitle sets, docs/PS1_FMV_SUBTITLES.md), so those assets can be
// viewed and edited with ordinary tools. Self-contained: its own inflate, no
// zlib, no OS calls.
//
// Supported: non-interlaced PNG, colour types 0 (grey), 2 (RGB), 3 (palette),
// 4 (grey+alpha) and 6 (RGBA), every bit depth the format allows for them.
#pragma once

#include <stddef.h>
#include <vector>

// Decode `data` into one byte per pixel holding the FIRST sample: the grey
// level, or the red channel for RGB / palette images (a 16-bit sample keeps its
// high byte, a 1/2/4-bit grey level is scaled to 0..255). A pixel whose alpha
// is 0 decodes as 0, so a transparent background saved by an image editor
// stays transparent. Returns false on a malformed or unsupported file.
bool PngDecodeFirstChannel(const unsigned char* data, size_t size,
                           std::vector<unsigned char>* out,
                           int* width, int* height);

// Encode width * height RGBA8 pixels (R in the lowest address) as an 8-bit
// RGBA PNG: filter 0, stored deflate blocks, so the file is as large as the
// pixels. Used for the texture dump (system/HdTextures.h), where being readable
// by every image tool matters and size does not.
void PngEncodeRGBA(const unsigned char* rgba, int width, int height,
                   std::vector<unsigned char>* out);
