// dc_compat.h - force-included into every TU of the Dreamcast build
// (Makefile.dreamcast: -include). Keep it tiny.
//
// KallistiOS's libstdc++ is configured on newlib without
// _GLIBCXX_USE_C99_STDIO, so <cstdio> does not export the C99 functions into
// namespace std even though newlib declares them globally. Re-export them.
// A duplicate using-declaration is legal, so this stays correct if a future
// toolchain starts providing them itself.
#pragma once

// Platform macro. KallistiOS 2.2+ passes -D__DREAMCAST__ (environ_dreamcast.sh);
// older KOS used _arch_dreamcast. Shared code tests __DREAMCAST__ only.
#if !defined(__DREAMCAST__) && defined(_arch_dreamcast)
#define __DREAMCAST__ 1
#endif
#if !defined(__DREAMCAST__)
#error "Dreamcast build without __DREAMCAST__: source $KOS_BASE/environ.sh (KOS_SUBARCH=pristine)"
#endif

#ifdef __cplusplus
#include <cstdio>
#include <cstdarg>
namespace std {
using ::snprintf;
using ::vsnprintf;
}
#endif
