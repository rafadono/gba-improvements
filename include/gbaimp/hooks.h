#pragma once

#include <cstdint>
#include <functional>

namespace gbaimp {

// Game-side integration points. A host project fills in what it needs;
// every callback is optional and has a safe default.
struct Hooks {
    // Called when a module changed a persisted setting (e.g. HUD dragged).
    std::function<void()> save_settings;
    // Return true while input must not reach the game (overlay/menu open).
    std::function<bool()> is_input_blocked;
    // Monotonic emulated-frame counter, used to cache per-frame input work.
    // If unset, input is recomputed on every call.
    std::function<uint64_t()> frame_counter;
};

Hooks& hooks();

inline void notify_settings_changed() {
    if (hooks().save_settings) hooks().save_settings();
}
inline bool input_blocked() {
    return hooks().is_input_blocked && hooks().is_input_blocked();
}

} // namespace gbaimp
