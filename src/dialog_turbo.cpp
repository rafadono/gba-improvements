#include "gbaimp/dialog_turbo.h"
#include <algorithm>

namespace gbaimp {

DialogTurbo& DialogTurbo::instance() {
    static DialogTurbo s_instance;
    return s_instance;
}

void DialogTurbo::set_enabled(bool enable) {
    enabled_ = enable;
}

void DialogTurbo::set_held(bool held) {
    held_ = held;
}

void DialogTurbo::set_skip_speed(int speed_multiplier) {
    speed_multiplier_ = std::clamp(speed_multiplier, 1, 4);
}

uint16_t DialogTurbo::process_keyinput(uint16_t keyinput, uint64_t frame_index) {
    if (!enabled_ || !held_) {
        return keyinput;
    }

    // GBA KEYINPUT: bit 0 = A, bit 1 = B. Active low (0 = pressed).
    // Alternate pulse every 1 or 2 frames to rapidly trigger dialogue advance
    bool pulse = (speed_multiplier_ >= 2) ? (frame_index % 2 == 0) : (frame_index % 4 < 2);

    if (pulse) {
        // Press A (clear bit 0)
        keyinput &= ~static_cast<uint16_t>(1 << 0);
        // Press B (clear bit 1)
        keyinput &= ~static_cast<uint16_t>(1 << 1);
    }

    return keyinput;
}

} // namespace gbaimp
