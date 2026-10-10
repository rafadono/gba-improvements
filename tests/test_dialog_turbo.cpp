#include "test_harness.h"
#include "gbaimp/dialog_turbo.h"

using namespace gbaimp;

int main() {
    auto& dt = DialogTurbo::instance();

    // Defaults.
    CHECK(dt.is_enabled());
    CHECK(!dt.is_held());
    CHECK_EQ(dt.skip_speed(), 2);

    // Not held: passthrough on every frame.
    for (uint64_t f = 0; f < 8; ++f) CHECK_EQ(dt.process_keyinput(0x03FF, f), uint16_t(0x03FF));

    // Held, speed 2: pulses A and B (bits 0,1 cleared) on even frames only.
    dt.set_held(true);
    CHECK(dt.is_held());
    for (uint64_t f = 0; f < 8; ++f) {
        uint16_t out = dt.process_keyinput(0x03FF, f);
        CHECK_EQ(out, (f % 2 == 0) ? uint16_t(0x03FC) : uint16_t(0x03FF));
    }

    // Other bits (e.g. Start, Up) are preserved on a pulse; pressed bits stay pressed.
    CHECK_EQ(dt.process_keyinput(0x03B7, 0), uint16_t(0x03B4));
    CHECK_EQ(dt.process_keyinput(0x03B7, 1), uint16_t(0x03B7));

    // Speed 1: pulse on frames where frame % 4 < 2.
    dt.set_skip_speed(1);
    CHECK_EQ(dt.skip_speed(), 1);
    const bool expect_pulse[8] = {true, true, false, false, true, true, false, false};
    for (uint64_t f = 0; f < 8; ++f) {
        uint16_t out = dt.process_keyinput(0x03FF, f);
        CHECK_EQ(out, expect_pulse[f] ? uint16_t(0x03FC) : uint16_t(0x03FF));
    }

    // Speed is clamped to 1..4.
    dt.set_skip_speed(0);  CHECK_EQ(dt.skip_speed(), 1);
    dt.set_skip_speed(99); CHECK_EQ(dt.skip_speed(), 4);
    dt.set_skip_speed(2);

    // Disabled: passthrough even while held.
    dt.set_enabled(false);
    CHECK(!dt.is_enabled());
    for (uint64_t f = 0; f < 4; ++f) CHECK_EQ(dt.process_keyinput(0x03FF, f), uint16_t(0x03FF));
    dt.set_enabled(true);

    // Released again: passthrough.
    dt.set_held(false);
    CHECK_EQ(dt.process_keyinput(0x03FF, 0), uint16_t(0x03FF));

    return testh::finish("dialog_turbo");
}
