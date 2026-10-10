// memwrap.cpp - malloc that can take memory back from caches.
//
// The port keeps optional caches in otherwise idle RAM (whole files read from
// the disc, dc_iso.cpp). They must never cost the game an allocation: the
// link wraps malloc/calloc/realloc/memalign (-Wl,--wrap=..., Makefile), and
// when an allocation fails the wrapper asks the caches to release memory and
// tries again. With the caches empty this is exactly the plain allocator.

#include <stddef.h>
#include <stdint.h>
#include <kos.h>

extern "C" {
void* __real_malloc(size_t n);
void* __real_calloc(size_t n, size_t s);
void* __real_realloc(void* p, size_t n);
void* __real_memalign(size_t a, size_t n);

// dc_iso.cpp: frees cached files, least recently used first, until roughly
// `need` bytes are released. Returns the bytes freed (0 = nothing left).
size_t dc_iso_cache_reclaim(size_t need);

volatile uint32_t g_dcReclaims = 0;     // allocations rescued (diagnostics)
}

namespace {
// A failed allocation: release cache memory, a bit more than asked so the
// allocator has room to split, until the retry succeeds or nothing is left.
template <typename Fn>
void* Retry(size_t need, Fn alloc)
{
    for (int round = 0; round < 64; ++round) {
        if (dc_iso_cache_reclaim(need + need / 8 + 4096) == 0) return nullptr;
        void* p = alloc();
        if (p != nullptr) { g_dcReclaims = g_dcReclaims + 1; return p; }
    }
    return nullptr;
}
}  // namespace

extern "C" void* __wrap_malloc(size_t n)
{
    void* p = __real_malloc(n);
    if (p == nullptr && n != 0) p = Retry(n, [&] { return __real_malloc(n); });
    return p;
}

extern "C" void* __wrap_calloc(size_t n, size_t s)
{
    void* p = __real_calloc(n, s);
    if (p == nullptr && n != 0 && s != 0) p = Retry(n * s, [&] { return __real_calloc(n, s); });
    return p;
}

extern "C" void* __wrap_realloc(void* old, size_t n)
{
    void* p = __real_realloc(old, n);
    // realloc(x, 0) may legitimately return NULL; a failed grow keeps `old`.
    if (p == nullptr && n != 0) p = Retry(n, [&] { return __real_realloc(old, n); });
    return p;
}

extern "C" void* __wrap_memalign(size_t a, size_t n)
{
    void* p = __real_memalign(a, n);
    if (p == nullptr && n != 0) p = Retry(n + a, [&] { return __real_memalign(a, n); });
    return p;
}
