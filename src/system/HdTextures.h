// HdTextures.h - High-resolution texture replacement by content hash.
//
// Port-only; the original has nothing like it. Every texture the game uploads
// - model skins, item and menu art, effect sheets, fonts, room masks, the room
// backgrounds - is built on the CPU as an RGBA8 image and handed to the
// renderer. This sits at that hand-over: the image is hashed, and when
// "<assets>/HD/<hash>.webp" exists that file is uploaded in its place, at
// whatever resolution it has. Nothing else changes: the game keeps the
// texture's LOGICAL size and normalises its UVs against that, so a replacement
// only has to keep the original's aspect ratio and layout.
//
// The hash is the name, so a pack is made by dumping: with [Textures]
// DumpTextures=1 every image is also written to "<assets>/HD/dump/<hash>.png"
// the first time it is seen. Upscale the PNG, save it as WebP under the same
// name one folder up, done. docs/HD_TEXTURES.md has the details and the limits
// (a page assembled from several files hashes as the finished page). Model
// skins can also be replaced by the EMD's own path (see the second half).
//
// OS-agnostic: file access goes through the platform layer.
#pragma once

#include <stddef.h>
#include <memory>
#include <string>
#include <vector>

// config.ini [Textures] HdTextures / DumpTextures. Replacement is on by default
// and costs nothing while the HD folder does not exist.
void HdTextures_Configure(bool replace, bool dump);

// The [Textures] HdTextures setting the game is running with (the HD folder
// may still be missing - HdTextures_Active() is the one that says whether
// anything is replaced).
bool HdTextures_ReplaceEnabled(void);

// True when either replacement or dumping is active, i.e. when an upload has to
// be hashed at all.
bool HdTextures_Active(void);

// Writes the 16 hex digits naming `rgba` (width * height RGBA8) into `out`,
// which must hold 17 bytes. The hash itself is hdtex::HashName in
// HdTextureHash.h, shared with the asset migrator's HD pack importer.
void HdTextures_HashName(int width, int height, const void* rgba, char* out);

// A decoded replacement: RGBA8, R in the lowest address. Shared with the cache
// that keeps it for the next upload of the same texture, hence read-only.
typedef std::shared_ptr<const std::vector<unsigned char> > HdPixels;

// Looks `rgba` up, dumping it first when dumping is on. Returns true with the
// replacement's pixels and size when there is one; false leaves the outputs
// alone and the caller uploads the original. A replacement is decoded once and
// kept (up to a memory budget): the game uploads a room's background and mask
// again on every camera cut, and decoding a 4x image takes longer than a cut
// has.
bool HdTextures_Find(int width, int height, const void* rgba,
                     HdPixels* out, int* outWidth, int* outHeight);

// Says that `rgba` is about to be uploaded - not now, but soon. If it has a
// replacement that is not decoded yet, a worker thread decodes it in the
// background so the later HdTextures_Find is a cache hit. Cheap when there is
// nothing to do. Game thread only, like everything else here.
void HdTextures_Prefetch(int width, int height, const void* rgba);

// Drops what HdTextures_Prefetch queued and the worker has not started: the
// room it was for is gone.
void HdTextures_PrefetchCancel(void);

// ---------------------------------------------------------------------------
// Replacement by source file
//
// A texture that comes from one known file can also be replaced by NAME:
// "<assets>/HD/<key>.webp", where the key is AssetSourceKey's
// "<TREE>/<path without extension>" - an EMD's skin is
// "HD/USA/ENEMY/EM1010.webp", a Director's Cut model's "HD/DC/ENEMY/...". The
// tree is the one the file was read from, so a mode's own model never picks up
// the base tree's art. A named file outranks a hash-named one for the same
// texture. With DumpTextures=1 the image is also written to
// "HD/dump/<key>.png" - the same picture, one per file, ready to upscale.
// ---------------------------------------------------------------------------

// Remembers that `owner` (an object the renderer creates textures from, such
// as a PSXTexture bank) now holds the file named `key`. `tag` is a value the
// caller can read back from the owner to tell whether it still holds that file
// (its pixel pointer), since an owner is reused for whatever is loaded next.
// A NULL or empty key forgets the owner.
void HdTextures_SetOwnerKey(const void* owner, const void* tag, const char* key);

// The same for a TIM whose palette rows are NOT the EMD layout (left half row
// 0, right half row 1): a model inside a room file, whose rows are colour
// variants of the whole image or belong to different objects. Each palette row
// is its own picture, as uploaded: "HD/<key>.webp" when the TIM has one row,
// "HD/<key>_P<row>.webp" when it has more.
void HdTextures_SetOwnerKeyPerPalette(const void* owner, const void* tag, const char* key);
// True when `owner` was registered by HdTextures_SetOwnerKeyPerPalette.
bool HdTextures_OwnerPerPalette(const void* owner);

// The key registered for `owner`, or NULL. *tag receives the tag it was
// registered with.
const char* HdTextures_OwnerKey(const void* owner, const void** tag);

// A mode's key ("DC/...", "SATURN/...") with no HD picture of its own borrows
// the base release's picture of the same name ("USA/..."). That is only right
// when the mode's file holds the same picture: the loader that knows says so
// here (false = never borrow for `key`), before the texture is uploaded. A key
// never set may borrow. Known = it has been decided this session.
void HdTextures_SetBaseFallback(const char* key, bool allowed);
bool HdTextures_BaseFallbackKnown(const char* key);

// True when dumping is on and `key` has not been dumped yet this session; the
// caller then builds the image and hands it to HdTextures_DumpNamed.
bool HdTextures_NamedDumpWanted(const char* key);
void HdTextures_DumpNamed(const char* key, int width, int height, const void* rgba);

// Looks "HD/<key>.webp" up. Same contract as HdTextures_Find: true with the
// decoded pixels (kept in the same cache), false when there is no such file.
bool HdTextures_FindNamed(const char* key, HdPixels* out, int* outWidth, int* outHeight);

// The same, ahead of time: decodes "HD/<key>.webp" on the worker thread if it
// exists, so a later HdTextures_FindNamed is a cache hit (room backgrounds and
// masks, which every camera cut uploads again). True when the file exists.
bool HdTextures_PrefetchNamed(const char* key);

// The file "HD/<key><ext>" itself, for HD content that is not a texture (a
// movie: "HD/USA/MOVIE/OU" + ".mp4"). True with its normalized path in `out`
// when replacement is on and the file exists.
bool HdTextures_NamedFile(const char* key, const char* ext, char* out, size_t outSize);

// Names the uploads that follow, for textures that are not traced by owner:
// a room background or mask page is uploaded straight from a file the room
// code knows. Until the matching HdTextures_EndSource, an upload of `width` x
// `height` (0 x 0 = any size) is looked up as "HD/<key>.webp" first. Sources
// nest (the innermost one names the upload); an empty key names nothing.
// Game thread only; HdSourceScope / HdBufferScope are the usual way to use it.
void HdTextures_BeginSource(const char* key, int width, int height);
void HdTextures_EndSource(void);
// The key the current source names an upload of this size by, or NULL.
const char* HdTextures_SourceFor(int width, int height);

// The same for an upload that is only PART of the named picture: the texture
// is the (x, y, w, h) rectangle of a pageW x pageH page (Effspr's weapon-effect
// bands are rows of a 256x256 page). "HD/<key>.webp" is the whole page; the
// matching part of it is uploaded.
void HdTextures_BeginRegionSource(const char* key, int x, int y, int w, int h,
                                  int pageW, int pageH);
// When the current source is a region one: true with the rectangle and page.
bool HdTextures_SourceRegion(int* x, int* y, int* w, int* h, int* pageW, int* pageH);

// The same for an upload that was assembled to LOOK LIKE a file but may differ
// from it in places: the room's effect-sprite pages are composited from the
// TIMs inside the RDT, which are the Effspr page file's art, except that a
// room can draw one sprite in another palette row. `reference` is the file as
// the game would have made it (width x height RGBA8, kept alive until the
// matching HdTextures_EndSource). "HD/<key>.webp" is uploaded, and wherever the
// upload's texel is not the reference's the HD picture there is recoloured by
// the ratio of the two colours (per channel), so a sprite the room draws in
// another palette row keeps its colours and stays HD. The caller
// only opens one when every opaque texel of the upload is opaque in the
// reference; the named picture is not dumped (one file stands for many pages).
void HdTextures_BeginPageSource(const char* key, int width, int height,
                                const void* reference);
// When the current source is a page one: its reference image, else NULL.
const void* HdTextures_SourceReference(void);

struct HdPageScope {
    HdPageScope(const char* key, int width, int height, const void* reference) {
        HdTextures_BeginPageSource(key, width, height, reference);
    }
    ~HdPageScope() { HdTextures_EndSource(); }
    HdPageScope(const HdPageScope&) = delete;
    HdPageScope& operator=(const HdPageScope&) = delete;
};

struct HdRegionScope {
    HdRegionScope(const char* key, int x, int y, int w, int h, int pageW, int pageH) {
        HdTextures_BeginRegionSource(key, x, y, w, h, pageW, pageH);
    }
    ~HdRegionScope() { HdTextures_EndSource(); }
    HdRegionScope(const HdRegionScope&) = delete;
    HdRegionScope& operator=(const HdRegionScope&) = delete;
};

// A picture the game draws at HD size itself, for a texture it also builds at
// the original size: the title menu's text strips, drawn from the title font's
// outlines (TitleMenuFont.cpp). Until the matching HdTextures_EndPicture,
// an upload whose pixels are exactly `rgba` (width x height RGBA8) gets
// `hdRgba` (hdWidth x hdHeight RGBA8, same aspect ratio) instead - so a copy
// of the same pixels (LoadTexturePage's STP twin) gets it too, and anything
// else uploaded meanwhile does not. Both buffers stay the caller's and must
// live until the end. A file for the texture's hash in HD/ still outranks it.
// Not nested; game thread only.
void HdTextures_BeginPicture(int width, int height, const void* rgba,
                             int hdWidth, int hdHeight, const void* hdRgba);
void HdTextures_EndPicture(void);
// The picture for an upload of these pixels: true with it, else false.
bool HdTextures_PictureFor(int width, int height, const void* rgba,
                           const void** hdRgba, int* hdWidth, int* hdHeight);

struct HdPictureScope {
    HdPictureScope(int width, int height, const void* rgba,
                   int hdWidth, int hdHeight, const void* hdRgba) {
        HdTextures_BeginPicture(width, height, rgba, hdWidth, hdHeight, hdRgba);
    }
    ~HdPictureScope() { HdTextures_EndPicture(); }
    HdPictureScope(const HdPictureScope&) = delete;
    HdPictureScope& operator=(const HdPictureScope&) = delete;
};

// The name of the asset file `path` (an asset-rooted path) as the HD layer
// keys it, into `out` ("" when HD textures are off or it is not an asset).
const char* HdTextures_KeyForPath(const char* path, char* out, size_t outSize);

struct HdSourceScope {
    HdSourceScope(const char* key, int width, int height) { HdTextures_BeginSource(key, width, height); }
    ~HdSourceScope() { HdTextures_EndSource(); }
    HdSourceScope(const HdSourceScope&) = delete;
    HdSourceScope& operator=(const HdSourceScope&) = delete;
};

// ---------------------------------------------------------------------------
// Deferred uploads
//
// A room load uploads a dozen textures whose HD pictures take far longer to
// decode than the load may stall: on the PlayStation the door animation and
// the load message were the screens the CD read hid behind, so here they are
// what the decoding hides behind. While a defer scope is open, a lookup does
// not wait: TryNamed / TryHash return 1 with the pixels when they are decoded,
// 0 when there is no replacement, and 2 when it is queued - the caller then
// uploads the original and polls `token` (Poll: 1 decoded, 0 not yet, -1
// failed) to swap the picture in (MarniSystem.cpp, MarniHd_PumpDeferred).
// Outside a scope both are HdTextures_FindNamed / HdTextures_Find.
// ---------------------------------------------------------------------------
void HdTextures_BeginDefer(void);
void HdTextures_EndDefer(void);
bool HdTextures_Deferring(void);
int  HdTextures_TryNamed(const char* key, HdPixels* out, int* outWidth, int* outHeight,
                         std::string* token);
int  HdTextures_TryHash(int width, int height, const void* rgba,
                        HdPixels* out, int* outWidth, int* outHeight, std::string* token);
int  HdTextures_Poll(const std::string& token, HdPixels* out, int* outWidth, int* outHeight);

struct HdDeferScope {
    HdDeferScope() { HdTextures_BeginDefer(); }
    ~HdDeferScope() { HdTextures_EndDefer(); }
    HdDeferScope(const HdDeferScope&) = delete;
    HdDeferScope& operator=(const HdDeferScope&) = delete;
};

// ---------------------------------------------------------------------------
// Naming by the file a buffer was loaded from
//
// LoadFile records which asset it read into which buffer, with a checksum of
// the bytes (HdTextures_NoteLoad). A loader that builds a texture straight
// from such a buffer (LoadTexturePage, display_image, ...) opens an
// HdBufferScope on it: if the buffer still holds that file - the checksum is
// checked, so a buffer since unpacked into or assembled in names nothing - the
// upload is named after the file, "USA/data/status"; a pointer into the
// middle of the file (a TIM inside a container) adds "@<hex offset>".
// ---------------------------------------------------------------------------
void HdTextures_NoteLoad(const void* buffer, size_t size, const char* assetPath);

// The key of the file `ptr` points into, into `out`; "" when there is none or
// the buffer no longer holds it.
const char* HdTextures_KeyForBuffer(const void* ptr, char* out, size_t outSize);

// Names the uploads that follow after the file `ptr` points into - unless an
// enclosing source already names them (a room background's own key wins).
struct HdBufferScope {
    explicit HdBufferScope(const void* ptr);
    ~HdBufferScope() { HdTextures_EndSource(); }
    HdBufferScope(const HdBufferScope&) = delete;
    HdBufferScope& operator=(const HdBufferScope&) = delete;
};
