#pragma once

#include <cstdint>

namespace gbaimp {

class DialogTurbo {
public:
    static DialogTurbo& instance();

    void set_enabled(bool enable);
    bool is_enabled() const { return enabled_; }

    void set_held(bool held);
    bool is_held() const { return held_; }

    void set_skip_speed(int speed_multiplier); // 1 = 30Hz, 2 = 60Hz
    int skip_speed() const { return speed_multiplier_; }

    // Modifies GBA KEYINPUT (active-low) to pulse A and B buttons
    uint16_t process_keyinput(uint16_t keyinput, uint64_t frame_index);

private:
    DialogTurbo() = default;

    bool enabled_ = true;
    bool held_ = false;
    int speed_multiplier_ = 2;
};

} // namespace gbaimp
