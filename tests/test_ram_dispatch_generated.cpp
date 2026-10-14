// End-to-end check of tools/gen_ram_dispatch.py: CMake runs the generator on a small fixture cache
// (tests/fixtures/ram_dispatch) and compiles the produced ram_overlay_dispatch.cpp into this test,
// with a stand-in runtime_arm.h. Proves that the generated file builds against the library and that
// the installed hook dispatches the right routine for each (pc, mode).
#include "test_harness.h"
#include "ram_overlay_dispatch.h"   // generated: khfix_ram_dispatch_hook / khfix_install_ram_dispatch
#include "runtime_arm.h"            // fixture stand-in

extern "C" {
RuntimeRamDispatchHook g_runtime_ram_dispatch_hook = nullptr;
int g_fixture_hits[8] = {};
}

int main() {
    CHECK(g_runtime_ram_dispatch_hook == nullptr);
    khfix_install_ram_dispatch();
    CHECK(g_runtime_ram_dispatch_hook == &khfix_ram_dispatch_hook);
    khfix_install_ram_dispatch();   // idempotent

    auto h = g_runtime_ram_dispatch_hook;
    CHECK_EQ(h(0x03000100u, 0), 1);   // fixture 0
    CHECK_EQ(h(0x02000010u, 1), 1);   // fixture 1 (Thumb)
    CHECK_EQ(h(0x02000011u, 1), 1);   // Thumb PC with bit 0 set
    CHECK_EQ(h(0x03000100u, 1), 1);   // fixture 2: same pc, other mode
    CHECK_EQ(h(0x03007F00u, 0), 1);   // fixture 3
    CHECK_EQ(g_fixture_hits[0], 1);
    CHECK_EQ(g_fixture_hits[1], 2);
    CHECK_EQ(g_fixture_hits[2], 1);
    CHECK_EQ(g_fixture_hits[3], 1);

    CHECK_EQ(h(0x08000200u, 0), 0);   // ROM routine was not extracted
    CHECK_EQ(h(0x02000010u, 0), 0);   // wrong mode
    CHECK_EQ(h(0x03000104u, 0), 0);   // unknown pc
    CHECK_EQ(g_fixture_hits[4], 0);
    return testh::finish("ram_dispatch_generated");
}
