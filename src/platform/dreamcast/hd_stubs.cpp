// hd_stubs.cpp - the HD texture / HD font / WebP layer compiled out.
//
// Upstream 1.4.0 adds high-resolution replacements for every texture, HD
// TrueType fonts for the game's text and WebP room backgrounds
// (system/HdTextures.cpp, system/WebpImage.cpp, game/HdText.cpp,
// game/RoomBackgroundWebp.cpp). None of it can run on the Dreamcast - 16 MB of
// RAM, 8 MB of VRAM, 640x480 output - and while inactive it still costs
// ~165 KB of code and buffers plus a std::thread worker. Makefile.dreamcast
// leaves those four files out and links this instead.
//
// Every function answers "no HD folder, no replacement, no font": the callers
// then take the original path, exactly as they do on PC without an HD pack.
// A room background stored as .webp (the asset migrator's "Backgrounds as
// WebP") is therefore not read on the Dreamcast - the data must keep the .pak
// backgrounds.
#include "../../Globals.h"
#include "../../game/HdText.h"
#include "../../game/RoomBackgroundWebp.h"
#include "../../system/HdTextures.h"

#include <stddef.h>

static const char* EmptyKey(char* out, size_t outSize)
{
    if (out != NULL && outSize > 0) out[0] = '\0';
    return out != NULL ? out : "";
}

// --- system/HdTextures.h ---------------------------------------------------------
void HdTextures_Configure(bool, bool) {}
bool HdTextures_ReplaceEnabled(void) { return false; }
bool HdTextures_Active(void) { return false; }
void HdTextures_HashName(int, int, const void*, char* out) { if (out) out[0] = '\0'; }
bool HdTextures_Find(int, int, const void*, HdPixels*, int*, int*) { return false; }
void HdTextures_Prefetch(int, int, const void*) {}
void HdTextures_PrefetchCancel(void) {}
void HdTextures_SetOwnerKey(const void*, const void*, const char*) {}
void HdTextures_SetOwnerKeyPerPalette(const void*, const void*, const char*) {}
bool HdTextures_OwnerPerPalette(const void*) { return false; }
const char* HdTextures_OwnerKey(const void*, const void** tag)
{
    if (tag) *tag = NULL;
    return NULL;
}
void HdTextures_SetBaseFallback(const char*, bool) {}
bool HdTextures_BaseFallbackKnown(const char*) { return false; }
bool HdTextures_NamedDumpWanted(const char*) { return false; }
void HdTextures_DumpNamed(const char*, int, int, const void*) {}
bool HdTextures_FindNamed(const char*, HdPixels*, int*, int*) { return false; }
bool HdTextures_PrefetchNamed(const char*) { return false; }
bool HdTextures_NamedFile(const char*, const char*, char*, size_t) { return false; }
void HdTextures_BeginSource(const char*, int, int) {}
void HdTextures_EndSource(void) {}
const char* HdTextures_SourceFor(int, int) { return NULL; }
void HdTextures_BeginRegionSource(const char*, int, int, int, int, int, int) {}
bool HdTextures_SourceRegion(int*, int*, int*, int*, int*, int*) { return false; }
void HdTextures_BeginPageSource(const char*, int, int, const void*) {}
const void* HdTextures_SourceReference(void) { return NULL; }
void HdTextures_BeginPicture(int, int, const void*, int, int, const void*) {}
void HdTextures_EndPicture(void) {}
bool HdTextures_PictureFor(int, int, const void*, const void**, int*, int*) { return false; }
const char* HdTextures_KeyForPath(const char*, char* out, size_t outSize) { return EmptyKey(out, outSize); }
void HdTextures_BeginDefer(void) {}
void HdTextures_EndDefer(void) {}
bool HdTextures_Deferring(void) { return false; }
int  HdTextures_TryNamed(const char*, HdPixels*, int*, int*, std::string*) { return 0; }
int  HdTextures_TryHash(int, int, const void*, HdPixels*, int*, int*, std::string*) { return 0; }
int  HdTextures_Poll(const std::string&, HdPixels*, int*, int*) { return -1; }
void HdTextures_NoteLoad(const void*, size_t, const char*) {}
const char* HdTextures_KeyForBuffer(const void*, char* out, size_t outSize) { return EmptyKey(out, outSize); }
// The destructor (HdTextures.h) calls HdTextures_EndSource, a no-op here.
HdBufferScope::HdBufferScope(const void*) {}

// --- game/HdText.h ---------------------------------------------------------------
const HdFont* HdFont_ForAsset(const char*) { return NULL; }
const HdFont* HdFont_Text(void) { return NULL; }
const HdFont* HdFont_Text14(void) { return NULL; }
void HdFont_ForgetAll(void) {}
int  HdFont_Glyph(const HdFont*, int) { return 0; }
int  HdFont_CapHeight(const HdFont*) { return 1; }
bool HdFont_GlyphBox(const HdFont*, int, int*, int*, int*, int*) { return false; }
int  HdFont_Advance(const HdFont*, int) { return 0; }
int  HdFont_Kern(const HdFont*, int, int) { return 0; }
int  HdFont_AddGlyph(const HdFont*, int, float, float, float, unsigned char*, int, int) { return 0; }
int  HdText_Scale(void) { return 2; }
bool HdText_Tim4ToRgba(const void*, unsigned int*, int, int*, int*) { return false; }
void HdText_Paint(const unsigned char*, int, int, unsigned int, unsigned int*, int, int, int, int) {}
void HdText_NoteFontPage(const unsigned int*, int, int) {}
int  HdText_Print8x8(short, short, unsigned short) { return 0; }   // 0 = draw the pixel font
void HdText_SetPrint8x8Size(float, int) {}
int  HdText_Glyph14(int, int, int, int) { return -1; }            // -1 = draw the sheet's cell
unsigned int HdText_Glyph14Texture(void) { return 0; }
void HdText_Layout14(HdTextGlyphSprite*, int) {}

// --- game/RoomBackgroundWebp.h ------------------------------------------------------
// 0 everywhere: "no .webp for this camera", the caller loads the .pak.
void room_bg_webp_reset(void) {}
int  room_bg_webp_preload(const char*, int) { return 0; }
int  room_bg_webp_show_cached(int, int) { return 0; }
int  room_bg_webp_show_file(const char*, int) { return 0; }
void room_hd_note_bg(const char*, int) {}
void room_hd_note_mask(const char*, int) {}
void room_hd_reset_masks(void) {}
const char* room_hd_bg_key(int) { return ""; }
const char* room_hd_mask_key(int) { return ""; }
const char* room_hd_key_for(const char*, char* out, size_t outSize) { return EmptyKey(out, outSize); }
int  room_hd_bg_width(void) { return 320; }
int  room_hd_bg_height(void) { return 240; }
void room_hd_note_room(const char*) {}
const char* room_hd_rdt_mask_key(int, char* out, size_t outSize) { return EmptyKey(out, outSize); }
const char* room_hd_model_key(const void*, char* out, size_t outSize) { return EmptyKey(out, outSize); }
void room_hd_prefetch(void) {}
void room_hd_prefetch_early(void) {}
