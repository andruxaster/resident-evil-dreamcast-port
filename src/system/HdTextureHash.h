// HdTextureHash.h - the name of a texture in the HD/ folder.
//
// Header-only and free of any game or platform dependency, because two
// programs must agree on it: the game (system/HdTextures.cpp), which hashes
// each image it uploads, and the asset migrator's HD pack importer
// (tools/asset_migrator/src/core/HdPack.cpp), which has to produce the same
// name from the game's files without running the game.
#pragma once

#include <stdio.h>

namespace hdtex {

inline unsigned int Rotl32(unsigned int x, int r)
{
    return (x << r) | (x >> (32 - r));
}

// MurmurHash3 x86_32 over whole dwords. The input is always a multiple of four
// bytes, so the reference implementation's tail step never applies.
inline unsigned int Murmur3Mix(unsigned int h, unsigned int k)
{
    k *= 0xCC9E2D51u;
    k = Rotl32(k, 15);
    k *= 0x1B873593u;
    h ^= k;
    h = Rotl32(h, 13);
    return h * 5u + 0xE6546B64u;
}

inline unsigned int Murmur3Final(unsigned int h, unsigned int byteLen)
{
    h ^= byteLen;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

// Writes the 16 hex digits naming `rgba` (width * height RGBA8, R in the
// lowest address) into `out`, which must hold 17 bytes. Two passes, seeded
// "RE1H" and "HDTX", each over the width, the height and then every pixel as a
// little-endian dword.
inline void HashName(int width, int height, const void* rgba, char* out)
{
    unsigned int h1 = 0x52453148u;   // "RE1H"
    unsigned int h2 = 0x48445458u;   // "HDTX"
    h1 = Murmur3Mix(h1, (unsigned int)width);
    h1 = Murmur3Mix(h1, (unsigned int)height);
    h2 = Murmur3Mix(h2, (unsigned int)width);
    h2 = Murmur3Mix(h2, (unsigned int)height);

    const unsigned char* p = (const unsigned char*)rgba;
    const unsigned int count = (unsigned int)width * (unsigned int)height;
    for (unsigned int i = 0; i < count; ++i, p += 4) {
        // Assembled by hand so the name does not depend on the host's byte
        // order or on the buffer's alignment.
        const unsigned int k = (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
                               ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
        h1 = Murmur3Mix(h1, k);
        h2 = Murmur3Mix(h2, k);
    }
    const unsigned int byteLen = (count + 2u) * 4u;
    snprintf(out, 17, "%08x%08x", Murmur3Final(h1, byteLen), Murmur3Final(h2, byteLen));
}

// The name of a TIM carried inside a room file (a room object's or an item
// model's texture), under HD/<TREE>/RoomModels/: one TIM is copied into many
// rooms, so it is named by its contents - a CRC-32 (reflected, 0xEDB88320) of
// the TIM's flags, CLUT size, CLUT entries, image size and pixels, as 8 hex
// digits. The CLUT entries go in as the game draws them (0x8000 -> 0x8001, a
// 0x0000 other than entry 0 of a row -> 0x0001), so a palette the game has
// already fixed up in memory names the same as the file's. `avail` bounds the
// read; false (and no name) when `tim` is not a 4/8 bpp TIM with a CLUT.
inline bool RoomTimName(const unsigned char* tim, unsigned int avail, char* out)
{
    struct Crc {
        unsigned int c = 0xFFFFFFFFu;
        void Byte(unsigned int b) {
            c ^= b & 0xFFu;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        void Half(unsigned int v) { Byte(v); Byte(v >> 8); }
    } crc;
    if (tim == NULL || avail < 20) return false;
    const unsigned int magic = tim[0] | tim[1] << 8 | tim[2] << 16 | (unsigned int)tim[3] << 24;
    const unsigned int flags = tim[4] | tim[5] << 8;
    if (magic != 0x10 || (flags & 3) > 1 || !(flags & 8)) return false;
    const unsigned int clutLen = tim[8] | tim[9] << 8 | tim[10] << 16 | (unsigned int)tim[11] << 24;
    const unsigned int clutW = tim[16] | tim[17] << 8, clutH = tim[18] | tim[19] << 8;
    const unsigned int img = 8 + clutLen;
    if (clutW == 0 || clutH == 0 || 20 + clutW * clutH * 2 > avail || img + 12 > avail) return false;
    const unsigned int imgW = tim[img + 8] | tim[img + 9] << 8;
    const unsigned int imgH = tim[img + 10] | tim[img + 11] << 8;
    const unsigned int pixLen = imgW * 2 * imgH;
    if (pixLen == 0 || img + 12 + pixLen > avail) return false;
    crc.Half(flags);
    crc.Half(clutW);
    crc.Half(clutH);
    for (unsigned int i = 0; i < clutW * clutH; ++i) {
        unsigned int c = tim[20 + i * 2] | tim[21 + i * 2] << 8;
        if (c == 0x8000) c = 0x8001;
        if (c == 0 && i % clutW != 0) c = 1;
        crc.Half(c);
    }
    crc.Half(imgW);
    crc.Half(imgH);
    for (unsigned int i = 0; i < pixLen; ++i) crc.Byte(tim[img + 12 + i]);
    snprintf(out, 9, "%08x", ~crc.c);
    return true;
}

}  // namespace hdtex
