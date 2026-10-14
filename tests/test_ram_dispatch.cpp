#include "test_harness.h"
#include "gbaimp/ram_dispatch.h"
#include "gbaimp/ram_dispatch_gbarecomp.h"

#include <cstdio>
#include <vector>

// The host executable normally gets this variable from gbarecomp; here the test owns it.
extern "C" int (*g_runtime_ram_dispatch_hook)(uint32_t pc, int thumb) = nullptr;

namespace {
int g_calls[8] = {};
void f0() { ++g_calls[0]; }
void f1() { ++g_calls[1]; }
void f2() { ++g_calls[2]; }
void f3() { ++g_calls[3]; }
void f4() { ++g_calls[4]; }

const gbaimp::RamDispatchEntry kTable[] = {
    {0x02000010u, 1, f0},
    {0x02038738u, 1, f1},
    {0x03000100u, 0, f2},
    {0x03000100u, 1, f3},   // same pc, other mode
    {0x03007F00u, 0, f4},
};
constexpr std::size_t kCount = sizeof(kTable) / sizeof(kTable[0]);

// Old generated files keep their own row type: the templates must accept it.
struct LegacyEntry { uint32_t pc; int thumb; void (*fn)(void); };
const LegacyEntry kLegacy[] = {{0x03000000u, 0, f0}, {0x03001218u, 1, f1}};

// The hook that every host used to carry (and the old generator emitted), kept here as the reference
// semantics. Returns the 1-based row index it would run, or 0.
int reference_hook(const gbaimp::RamDispatchEntry* t, std::size_t n, uint32_t pc, int thumb) {
    uint32_t target_pc = pc & ~1u;
    int lo = 0, hi = static_cast<int>(n) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const auto& e = t[mid];
        if (e.pc < target_pc) lo = mid + 1;
        else if (e.pc > target_pc) hi = mid - 1;
        else {
            if (e.thumb == thumb) return mid + 1;
            if (mid > 0 && t[mid - 1].pc == target_pc && t[mid - 1].thumb == thumb) return mid;
            if (mid + 1 < static_cast<int>(n) && t[mid + 1].pc == target_pc && t[mid + 1].thumb == thumb) return mid + 2;
            return 0;
        }
    }
    return 0;
}
} // namespace

// What the generated host file contains after its table.
GBAIMP_DEFINE_RAM_DISPATCH(testgame, "TestGame", kTable)

int main() {
    using namespace gbaimp;

    CHECK(ram_dispatch_is_sorted(kTable, kCount));
    CHECK(ram_dispatch_is_sorted(kTable, 0));
    CHECK(ram_dispatch_is_sorted(kTable, 1));
    const RamDispatchEntry unsorted[] = {{2, 0, f0}, {1, 0, f1}};
    CHECK(!ram_dispatch_is_sorted(unsorted, 2));
    const RamDispatchEntry dup[] = {{1, 0, f0}, {1, 0, f1}};
    CHECK(!ram_dispatch_is_sorted(dup, 2));

    // exact hits, including both modes at one pc
    CHECK(find_ram_dispatch(kTable, kCount, 0x02000010u, 1) == &kTable[0]);
    CHECK(find_ram_dispatch(kTable, kCount, 0x03000100u, 0) == &kTable[2]);
    CHECK(find_ram_dispatch(kTable, kCount, 0x03000100u, 1) == &kTable[3]);
    CHECK(find_ram_dispatch(kTable, kCount, 0x03007F00u, 0) == &kTable[4]);
    // Thumb PCs may arrive with bit 0 set
    CHECK(find_ram_dispatch(kTable, kCount, 0x02038739u, 1) == &kTable[1]);
    // misses: wrong mode, unknown pc, below / above range, empty table
    CHECK(find_ram_dispatch(kTable, kCount, 0x02000010u, 0) == nullptr);
    CHECK(find_ram_dispatch(kTable, kCount, 0x02000012u, 0) == nullptr);
    CHECK(find_ram_dispatch(kTable, kCount, 0x03000104u, 0) == nullptr);
    CHECK(find_ram_dispatch(kTable, kCount, 0x00000000u, 0) == nullptr);
    CHECK(find_ram_dispatch(kTable, kCount, 0xFFFFFFFEu, 1) == nullptr);
    CHECK(find_ram_dispatch(kTable, 0, 0x02000010u, 1) == nullptr);
    CHECK(find_ram_dispatch(kTable, kCount, 0x03000100u, 2) == nullptr);   // exact mode compare

    // agrees with the original hook around every row
    for (std::size_t i = 0; i < kCount; ++i) {
        for (uint32_t d = 0; d < 4; ++d) {
            for (int m = 0; m < 2; ++m) {
                const uint32_t pc = kTable[i].pc + d - 1;
                const RamDispatchEntry* got = find_ram_dispatch(kTable, kCount, pc, m);
                const int ref = reference_hook(kTable, kCount, pc, m);
                CHECK_EQ(got ? static_cast<int>(got - kTable) + 1 : 0, ref);
            }
        }
    }

    // ram_dispatch_call
    CHECK_EQ(ram_dispatch_call(kTable, kCount, 0x02000010u, 1), 1);
    CHECK_EQ(g_calls[0], 1);
    CHECK_EQ(ram_dispatch_call(kTable, kCount, 0x02000010u, 0), 0);
    CHECK_EQ(g_calls[0], 1);
    const RamDispatchEntry nullfn[] = {{0x02000000u, 0, nullptr}};
    CHECK_EQ(ram_dispatch_call(nullfn, 1, 0x02000000u, 0), 0);   // placeholder row is harmless

    // legacy row type
    CHECK(find_ram_dispatch(kLegacy, 2, 0x03001218u, 1) == &kLegacy[1]);
    CHECK_EQ(ram_dispatch_call(kLegacy, 2, 0x03000000u, 0), 1);
    CHECK_EQ(g_calls[0], 2);

    // a large table (the biggest host has ~170 rows; this is far beyond that)
    std::vector<RamDispatchEntry> big;
    for (uint32_t i = 0; i < 100000; ++i) big.push_back({0x02000000u + i * 2u, static_cast<int>(i & 1u), f0});
    CHECK(ram_dispatch_is_sorted(big.data(), big.size()));
    bool all = true;
    for (uint32_t i = 0; i < 100000; i += 37) {
        all &= find_ram_dispatch(big.data(), big.size(), big[i].pc, big[i].thumb) == &big[i];
    }
    CHECK(all);

    // generated-file macros + adapter install
    CHECK(g_runtime_ram_dispatch_hook == nullptr);
    CHECK_EQ(testgame_ram_dispatch_hook(0x03000100u, 1), 1);
    CHECK_EQ(g_calls[3], 1);
    CHECK_EQ(testgame_ram_dispatch_hook(0x03000104u, 1), 0);
    testgame_install_ram_dispatch();
    CHECK(g_runtime_ram_dispatch_hook == &testgame_ram_dispatch_hook);
    g_runtime_ram_dispatch_hook = nullptr;
    testgame_install_ram_dispatch();   // second call is a no-op
    CHECK(g_runtime_ram_dispatch_hook == nullptr);

    log_ram_dispatch_installed("TestGame", 5);
    log_ram_dispatch_installed(nullptr, 0);
    return testh::finish("ram_dispatch");
}
