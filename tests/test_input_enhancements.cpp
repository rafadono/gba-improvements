#include "test_harness.h"
#include "gbaimp/hooks.h"
#include "gbaimp/input_enhancements.h"
#include <fstream>
#include <sstream>

using namespace gbaimp;

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main() {
    // InputEnhancements reads/writes keybinds.ini in the working directory.
    testh::TempDir tmp("input");
    tmp.enter();
    auto& in = InputEnhancements::instance();

    // Defaults.
    CHECK(in.settings().analog_mode == AnalogMode::AnalogWalkRun);
    CHECK_NEAR(in.settings().deadzone, 0.18f, 1e-6f);
    CHECK(in.settings().gamepad_profile == GamepadProfile::Standard);
    CHECK(in.settings().pad_a == PadButtonChoice::ButtonA);
    CHECK(in.settings().pad_l == PadButtonChoice::LeftBumper);
    CHECK(in.get_key_scancode(KEY_A) == SDL_SCANCODE_X);
    CHECK(in.get_key_scancode(KEY_L) == SDL_SCANCODE_C);

    // Gamepad profiles (renamed pad_a/pad_b/pad_l/pad_r API).
    in.apply_gamepad_profile(GamepadProfile::Nintendo);
    CHECK(in.settings().pad_a == PadButtonChoice::ButtonB);
    CHECK(in.settings().pad_b == PadButtonChoice::ButtonA);
    in.apply_gamepad_profile(GamepadProfile::PlayStation);
    CHECK(in.settings().pad_a == PadButtonChoice::ButtonX);
    in.apply_gamepad_profile(GamepadProfile::ShoulderSwap);
    CHECK(in.settings().pad_l == PadButtonChoice::RightBumper);
    CHECK(in.settings().pad_r == PadButtonChoice::LeftBumper);
    in.apply_gamepad_profile(GamepadProfile::Triggers);
    CHECK(in.settings().gamepad_profile == GamepadProfile::Triggers);
    CHECK(in.settings().pad_l == PadButtonChoice::LeftTrigger);
    CHECK(in.settings().pad_r == PadButtonChoice::RightTrigger);

    // Setters clamp and switch the profile to Custom.
    in.apply_gamepad_profile(GamepadProfile::Standard);
    in.set_pad_a(99);
    CHECK(in.settings().pad_a == PadButtonChoice::RightTrigger);
    CHECK(in.settings().gamepad_profile == GamepadProfile::Custom);
    in.set_pad_b(-4);
    CHECK(in.settings().pad_b == PadButtonChoice::ButtonA);

    // Keyboard names, presets.
    CHECK(in.key_index_from_name("Select") == KEY_SELECT);
    CHECK(in.key_index_from_name("bogus") == KEY_COUNT);
    CHECK(in.key_index_from_name(nullptr) == KEY_COUNT);
    CHECK(InputEnhancements::scancode_from_string("space") == SDL_SCANCODE_SPACE);
    CHECK(InputEnhancements::scancode_from_string("Return") == SDL_SCANCODE_RETURN);
    CHECK(InputEnhancements::scancode_from_string("nonsense key") == SDL_SCANCODE_UNKNOWN);
    in.apply_keyboard_preset(KeyboardPreset::Wasd);
    CHECK(in.get_key_scancode(KEY_UP) == SDL_SCANCODE_W);
    CHECK(in.get_key_scancode(KEY_A) == SDL_SCANCODE_J);

    // Keybinds round trip including the [gamepad] section.
    const std::string ini = tmp.file("roundtrip.ini");
    in.set_key_scancode(KEY_A, SDL_SCANCODE_Q);          // -> Custom keyboard preset
    in.set_key_scancode(KEY_START, SDL_SCANCODE_SPACE);
    in.set_pad_a(static_cast<int>(PadButtonChoice::ButtonY));
    in.set_pad_b(static_cast<int>(PadButtonChoice::ButtonX));
    in.set_pad_l(static_cast<int>(PadButtonChoice::LeftTrigger));
    in.set_pad_r(static_cast<int>(PadButtonChoice::RightBumper));
    in.save_keybinds(ini.c_str());
    {
        std::string text = slurp(ini);
        CHECK(text.find("[player1]") != std::string::npos);
        CHECK(text.find("[gamepad]") != std::string::npos);
        CHECK(text.find("pad_a = 3") != std::string::npos);
        CHECK(text.find("pad_l = 6") != std::string::npos);
        CHECK(text.find("profile = 5") != std::string::npos);   // Custom
    }
    in.reset_to_defaults();
    CHECK(in.get_key_scancode(KEY_A) == SDL_SCANCODE_X);
    CHECK(in.settings().pad_a == PadButtonChoice::ButtonA);
    in.load_keybinds(ini.c_str());
    CHECK(in.get_key_scancode(KEY_A) == SDL_SCANCODE_Q);
    CHECK(in.get_key_scancode(KEY_START) == SDL_SCANCODE_SPACE);
    CHECK(in.settings().gamepad_profile == GamepadProfile::Custom);
    CHECK(in.settings().pad_a == PadButtonChoice::ButtonY);
    CHECK(in.settings().pad_b == PadButtonChoice::ButtonX);
    CHECK(in.settings().pad_l == PadButtonChoice::LeftTrigger);
    CHECK(in.settings().pad_r == PadButtonChoice::RightBumper);

    // A non-Custom profile in the file re-applies that profile's mapping.
    {
        const std::string ini2 = tmp.file("profile.ini");
        std::ofstream f(ini2);
        f << "# comment\n[gamepad]\nprofile = 4\npad_a = 7\n";
        f.close();
        in.load_keybinds(ini2.c_str());
        CHECK(in.settings().gamepad_profile == GamepadProfile::Triggers);
        CHECK(in.settings().pad_a == PadButtonChoice::ButtonA);   // profile wins over stray key
        CHECK(in.settings().pad_l == PadButtonChoice::LeftTrigger);
    }

    // Missing file is a silent no-op.
    in.load_keybinds(tmp.file("missing.ini").c_str());

    // Deadzone clamp.
    in.set_deadzone(0.0f);  CHECK_NEAR(in.settings().deadzone, 0.05f, 1e-6f);
    in.set_deadzone(5.0f);  CHECK_NEAR(in.settings().deadzone, 0.50f, 1e-6f);
    in.set_deadzone(0.2f);  CHECK_NEAR(in.settings().deadzone, 0.20f, 1e-6f);

    // Analog stick -> directions (active-low bits 4..7: right, left, up, down).
    in.set_analog_mode(AnalogMode::Analog8Way);
    in.set_deadzone(0.2f);
    {
        uint16_t k = 0x03FF;                       // inside deadzone: untouched
        in.process_axis_input(2000, -2000, k);
        CHECK_EQ(k, uint16_t(0x03FF));

        k = 0x03FF;                                // full right
        in.process_axis_input(32767, 0, k);
        CHECK_EQ(k, uint16_t(0x03EF));

        k = 0x03FF;                                // full left
        in.process_axis_input(-32767, 0, k);
        CHECK_EQ(k, uint16_t(0x03DF));

        k = 0x03FF;                                // full up (SDL y negative)
        in.process_axis_input(0, -32767, k);
        CHECK_EQ(k, uint16_t(0x03BF));

        k = 0x03FF;                                // down + right diagonal
        in.process_axis_input(23000, 23000, k);
        CHECK_EQ(k, uint16_t(0x036F));

        k = 0x030F;                                // non-direction bits are preserved
        in.process_axis_input(32767, 0, k);
        CHECK_EQ(k & 0x000F, 0x000F);
    }
    in.set_analog_mode(AnalogMode::DigitalOriginal);
    {
        uint16_t k = 0x03FF;
        in.process_axis_input(32767, 0, k);        // digital mode ignores the stick
        CHECK_EQ(k, uint16_t(0x03FF));
    }

    // Walk/run: a gentle tilt (below walk_threshold) drops direction on alternate frames.
    in.set_analog_mode(AnalogMode::AnalogWalkRun);
    {
        int pressed = 0;
        for (int i = 0; i < 10; ++i) {
            uint16_t k = 0x03FF;
            in.process_axis_input(12000, 0, k);    // magnitude ~0.37 < 0.55
            if ((k & 0x0010) == 0) ++pressed;
        }
        CHECK(pressed >= 4 && pressed <= 6);
        int full = 0;
        for (int i = 0; i < 10; ++i) {
            uint16_t k = 0x03FF;
            in.process_axis_input(32767, 0, k);    // run: always pressed
            if ((k & 0x0010) == 0) ++full;
        }
        CHECK_EQ(full, 10);
    }

    // process_keyinput returns "nothing pressed" while the host blocks input.
    {
        bool blocked = true;
        hooks().is_input_blocked = [&] { return blocked; };
        CHECK_EQ(in.process_keyinput(0x0000), uint16_t(0x03FF));
        CHECK_EQ(in.process_keyinput(0x03FE), uint16_t(0x03FF));

        // Unblocked, no controller attached: host keys are merged in.
        blocked = false;
        uint64_t frame = 1000;
        hooks().frame_counter = [&] { return frame; };
        CHECK_EQ(in.process_keyinput(0x03FE), uint16_t(0x03FE));   // A pressed by the host
        // Same frame + same host keys: cached result.
        CHECK_EQ(in.process_keyinput(0x03FE), uint16_t(0x03FE));
        ++frame;
        CHECK_EQ(in.process_keyinput(0x03FF), uint16_t(0x03FF));

        hooks() = Hooks{};
    }

    return testh::finish("input_enhancements");
}
