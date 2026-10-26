// WebpImage.cpp - WebP reader for port-only assets. See WebpImage.h.
//
// OS-agnostic: no Windows headers, no platform calls. The caller reads the file
// and uploads the pixels.

#include "WebpImage.h"

#include <string.h>

// simplewebp (BSD-3-Clause, version 20260718), compiled into this translation
// unit the way stb_vorbis is compiled into AudioFile.cpp. Only the memory
// reader is used, so its stdio entry points are left out.
#define SIMPLEWEBP_IMPLEMENTATION
#define SIMPLEWEBP_DISABLE_STDIO
#include "../third_party/simplewebp.h"

bool WebpIsWebp(const unsigned char* data, size_t size)
{
    return data != NULL && size >= 12 &&
           memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0;
}

bool WebpDecodeRGBA(const unsigned char* data, size_t size,
                    std::vector<unsigned char>* out, int* width, int* height)
{
    if (out == NULL || !WebpIsWebp(data, size)) return false;

    // The memory reader only ever reads from `data`; the parameter is not
    // const because the same struct also serves writable streams.
    simplewebp* webp = NULL;
    if (simplewebp_load_from_memory((void*)data, size, NULL, &webp) != SIMPLEWEBP_NO_ERROR) {
        return false;
    }

    size_t w = 0, h = 0;
    simplewebp_get_dimensions(webp, &w, &h);
    bool ok = w > 0 && h > 0 && w <= WEBP_MAX_SIDE && h <= WEBP_MAX_SIDE &&
              w * h <= (size_t)WEBP_MAX_PIXELS;
    if (ok) {
        out->resize(w * h * 4);
        ok = simplewebp_decode(webp, out->data(), NULL) == SIMPLEWEBP_NO_ERROR;
    }
    simplewebp_unload(webp);

    if (!ok) {
        out->clear();
        return false;
    }
    if (width != NULL)  *width  = (int)w;
    if (height != NULL) *height = (int)h;
    return true;
}
