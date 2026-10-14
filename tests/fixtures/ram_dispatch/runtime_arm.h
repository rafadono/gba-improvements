#pragma once
// Stand-in for gbarecomp's armv4t/runtime_arm.h, only for the generator end-to-end test:
// the real hook variable (same type and C linkage) plus a hit counter used by the fixtures.
#include <cstdint>

extern "C" {
typedef int (*RuntimeRamDispatchHook)(uint32_t pc, int thumb);
extern RuntimeRamDispatchHook g_runtime_ram_dispatch_hook;
extern int g_fixture_hits[8];
}
#define OVL_EXPORT
