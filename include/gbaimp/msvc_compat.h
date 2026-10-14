#pragma once
// Generic MSVC compatibility for recompiled GBA code: maps GCC's __builtin_clz onto _BitScanReverse.
// Identical in every host (they differed only in line endings). clz(0) yields 32 here; GCC leaves it
// undefined, so portable code must not rely on it.

#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>

#ifdef __cplusplus
static inline int msvc_builtin_clz(unsigned int x) {
    unsigned long idx;
    if (_BitScanReverse(&idx, x)) {
        return 31 - static_cast<int>(idx);
    }
    return 32;
}
#define __builtin_clz(x) msvc_builtin_clz(static_cast<unsigned int>(x))
#else
static inline int msvc_builtin_clz(unsigned int x) {
    unsigned long idx;
    if (_BitScanReverse(&idx, x)) {
        return 31 - (int)idx;
    }
    return 32;
}
#define __builtin_clz(x) msvc_builtin_clz((unsigned int)(x))
#endif

#endif
