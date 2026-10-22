#pragma once
// Header-only gbarecomp binding of the m4a HD music trigger (gbaimp/m4a_music_hook.h).
//
// Needs the runtime headers of the host (armv4t/runtime_arm.h for g_cpu / bus_read_u32,
// runtime_bus_bridge.h and gba/gba_bus.h for the active bus); the library itself never includes them.
//
// Host adoption:
//   * once the cartridge is mapped (any per-frame callback is fine, it is idempotent):
//         gbaimp::m4a_gbarecomp_setup("mygame", "MYGAME_");
//   * from the function-entry callback the host already owns (e.g. WidescreenProfile::on_fn_entry):
//         gbaimp::m4a_gbarecomp_on_function_entry(pc);
//   * optional: config/music_tracks.ini maps m4a song ids to .wav files (see docs/MODULES.md).
// Without a track mapping the hook does nothing audible; a ROM without the m4a engine is detected by
// the locator (setup source None) and the entry callback then returns immediately.
#include <cstdint>

#include "armv4t/runtime_arm.h"
#include "gba/gba_bus.h"
#include "gbaimp/m4a_music_hook.h"
#include "runtime_bus_bridge.h"

namespace gbaimp {

inline M4aSetupSource& m4a_gbarecomp_source() {
    static M4aSetupSource s = M4aSetupSource::None;
    return s;
}

// Locates the m4a entry points in the mapped cartridge image, once. Returns false while the
// cartridge is not mapped yet (call again later), true once the attempt has been made.
inline bool m4a_gbarecomp_setup(const char* log_tag, const char* env_prefix,
                                const M4aAddresses* fallback = nullptr) {
    static bool done = false;
    if (done) return true;
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || !bus->rom_ptr() || bus->rom_size() == 0) return false;
    done = true;
    M4aSetupOptions o;
    o.log_tag = log_tag;
    o.env_prefix = env_prefix;
    o.fallback = fallback;
    o.bus_read_u32 = &bus_read_u32;
    m4a_gbarecomp_source() = M4aMusicHook::instance().setup_from_rom(bus->rom_ptr(), bus->rom_size(), o);
    return true;
}

inline void m4a_gbarecomp_on_function_entry(uint32_t pc) {
    M4aMusicHook::instance().on_function_entry(pc, g_cpu.R[0]);
}

} // namespace gbaimp
