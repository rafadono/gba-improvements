#pragma once
// Native dispatch of IWRAM/EWRAM routines for a GBA static-recompilation host.
//
// Some games copy code into RAM at run time. The recompiler cannot translate those
// routines ahead of time at their ROM address, so a generator (tools/gen_ram_dispatch.py)
// compiles them separately and emits a table {pc, thumb, function}. The runtime asks
// "is there a native routine at this PC?" through a hook; this header owns everything about
// that question except the runtime's hook variable:
//
//   * RamDispatchEntry            the table row type;
//   * find_ram_dispatch()         binary search by (pc, mode);
//   * ram_dispatch_call()         search + call, with the hook's 0/1 return convention;
//   * ram_dispatch_is_sorted()    sanity check of a table (used by tests / debug builds);
//   * GBAIMP_RAM_DISPATCH_HOOK()  defines the extern "C" hook function of a host;
//   * log_ram_dispatch_installed().
//
// This header is pure: it includes nothing from gbarecomp. Installing the hook into the
// runtime lives in gbaimp/ram_dispatch_gbarecomp.h (header-only adapter).
//
// The generated host file therefore contains only the translated routines, the table and
//   GBAIMP_DEFINE_RAM_DISPATCH(<prefix>, "<log name>", kRamDispatchEntries)
// (see ram_dispatch_gbarecomp.h).
#include <cstddef>
#include <cstdint>

namespace gbaimp {

// One row of the table. `pc` has bit 0 clear; `thumb` is 1 for Thumb code and 0 for ARM.
// The table must be sorted ascending by (pc, thumb): the generator guarantees it.
struct RamDispatchEntry {
    uint32_t pc;
    int thumb;
    void (*fn)(void);
};

// Returns the row whose pc equals `pc & ~1u` and whose mode equals `thumb` (compared exactly,
// like the hooks of existing hosts), or nullptr. `E` is any row type with the members
// pc / thumb / fn, so tables generated before this module existed work unchanged.
template <class E>
inline const E* find_ram_dispatch(const E* table, std::size_t count, uint32_t pc, int thumb) {
    const uint32_t target = pc & ~1u;
    std::size_t lo = 0, hi = count;   // first row not below (target, thumb)
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const E& e = table[mid];
        if (e.pc < target || (e.pc == target && e.thumb < thumb)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < count && table[lo].pc == target && table[lo].thumb == thumb) return &table[lo];
    return nullptr;
}

// Runs the routine at (pc, thumb) when the table has one. Returns 1 when it ran, else 0
// (the contract of the runtime's RAM-dispatch hook).
template <class E>
inline int ram_dispatch_call(const E* table, std::size_t count, uint32_t pc, int thumb) {
    const E* e = find_ram_dispatch(table, count, pc, thumb);
    if (!e || !e->fn) return 0;
    e->fn();
    return 1;
}

// True when the table is strictly ascending by (pc, thumb) (so find_ram_dispatch is valid).
template <class E>
inline bool ram_dispatch_is_sorted(const E* table, std::size_t count) {
    for (std::size_t i = 1; i < count; ++i) {
        const E& a = table[i - 1];
        const E& b = table[i];
        if (!(a.pc < b.pc || (a.pc == b.pc && a.thumb < b.thumb))) return false;
    }
    return true;
}

// Prints "[<log_name>] Native RAM overlay dispatcher installed (<count> functions registered)".
void log_ram_dispatch_installed(const char* log_name, std::size_t count);

} // namespace gbaimp

// Defines `extern "C" int <prefix>_ram_dispatch_hook(uint32_t pc, int thumb)` over the first `count`
// rows of `table` (a C array visible at this point). Its declaration normally comes from the
// host's own ram_overlay_dispatch.h. The _N form takes the row count explicitly (a table with
// zero routines still needs one placeholder row because C++ has no empty arrays).
#define GBAIMP_RAM_DISPATCH_HOOK_N(prefix, table, count)                                         extern "C" int prefix##_ram_dispatch_hook(uint32_t pc, int thumb) {                              return ::gbaimp::ram_dispatch_call(table, static_cast<std::size_t>(count), pc, thumb);     }
#define GBAIMP_RAM_DISPATCH_HOOK(prefix, table)     GBAIMP_RAM_DISPATCH_HOOK_N(prefix, table, sizeof(table) / sizeof((table)[0]))
