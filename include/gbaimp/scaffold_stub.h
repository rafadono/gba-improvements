#pragma once
// Building blocks for a host's scaffold_stub.cpp: placeholder symbols that let a host link before
// its first recompilation has produced the real ones. Only the parts that are identical in every
// host live here; the game-namespace stubs (menu-open predicate, load/save config) stay in the host
// because their namespace and names are game-specific.
//
//   #include "gbaimp/scaffold_stub.h"
//   GBAIMP_SCAFFOLD_EMPTY_DISPATCH_TABLE()      // extern "C" kDispatchTable / kDispatchTableLen
//   namespace mygame {
//   bool is_runtime_menu_open() { return false; }
//   GBAIMP_WEAK void save_mygame_config() {}    // weak where the toolchain supports it
//   }
//
// Delete the host's scaffold_stub.cpp from the build once the real recompiled sources exist.
#include <cstdint>

// Weak definition where supported (GCC/Clang); a plain definition on MSVC, where the real symbol
// must then not also be linked (the same situation as before this header existed).
#if defined(__GNUC__) || defined(__clang__)
#define GBAIMP_WEAK [[gnu::weak]]
#else
#define GBAIMP_WEAK
#endif

namespace gbaimp {
// Layout of the recompiler's dispatch table row (address, mode, resume flag, routine).
struct ScaffoldDispatchEntry {
    uint32_t addr;
    uint8_t thumb;
    uint8_t resume;
    void (*fn)(void);
};
} // namespace gbaimp

// Defines the one-row, zero-length placeholder `kDispatchTable` / `kDispatchTableLen`.
#define GBAIMP_SCAFFOLD_EMPTY_DISPATCH_TABLE()                                                      \
    extern "C" const ::gbaimp::ScaffoldDispatchEntry kDispatchTable[1] = {{0, 0, 0, nullptr}};      \
    extern "C" const unsigned kDispatchTableLen = 0;
