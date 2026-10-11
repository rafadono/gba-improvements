#include "test_harness.h"
#include "gbaimp/hooks.h"
#include "gbaimp/input_enhancements.h"
#include "gbaimp/dialog_turbo.h"
#include <fstream>
#include <sstream>
#include <filesystem>

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

    // Keybinds round trip ([player1] and [turbo]; gamepad mapping is not persisted here).
    const std::string ini = tmp.file("roundtrip.ini");
    in.set_key_scancode(KEY_A, SDL_SCANCODE_Q);          // -> Custom keyboard preset
    in.set_key_scancode(KEY_START, SDL_SCANCODE_SPACE);
    in.save_keybinds(ini.c_str());
    {
        std::string text = slurp(ini);
        CHECK(text.find("[player1]") != std::string::npos);
        CHECK(text.find("[gamepad]") == std::string::npos);
        CHECK(text.find("pad_a") == std::string::npos);
    }
    in.reset_to_defaults();
    CHECK(in.get_key_scancode(KEY_A) == SDL_SCANCODE_X);
    in.load_keybinds(ini.c_str());
    CHECK(in.get_key_scancode(KEY_A) == SDL_SCANCODE_Q);
    CHECK(in.get_key_scancode(KEY_START) == SDL_SCANCODE_SPACE);

    // A stray [gamepad] section in an old file is ignored: the gamepad mapping is not persisted here.
    {
        const std::string ini2 = tmp.file("profile.ini");
        std::ofstream f(ini2);
        f << "# comment" << std::endl << "[gamepad]" << std::endl << "profile = 4" << std::endl << "pad_a = 7" << std::endl;
        f.close();
        in.reset_gamepad_defaults();
        in.load_keybinds(ini2.c_str());
        CHECK(in.settings().gamepad_profile == GamepadProfile::Standard);
        CHECK(in.settings().pad_a == PadButtonChoice::ButtonA);
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

    // --- Configurable keybinds path / autosave (no hidden writes) ---------------
    {
        namespace fs = std::filesystem;
        testh::TempDir cfg("input_cfg");
        cfg.enter();                                   // cwd is empty: stray writes are detectable
        const std::string a_ini = cfg.file("sub_a.ini");

        InputEnhancements a(a_ini);                    // missing file: defaults, no crash
        CHECK_EQ(a.keybinds_path(), a_ini);
        CHECK(a.autosave_keybinds());                  // historical default

        // Default behavior: presets persist to the configured path (not the cwd).
        a.apply_keyboard_preset(KeyboardPreset::Wasd);
        CHECK(fs::exists(a_ini));
        CHECK(!fs::exists("keybinds.ini"));
        CHECK(slurp(a_ini).find("A = J") != std::string::npos);

        // A second instance reads that file on construction.
        InputEnhancements a2(a_ini);
        CHECK_EQ(int(a2.get_key_scancode(KEY_A)), int(SDL_SCANCODE_J));

        // Autosave off: no file is written by presets or resets.
        fs::remove(a_ini);
        a.set_autosave_keybinds(false);
        CHECK(!a.autosave_keybinds());
        a.apply_keyboard_preset(KeyboardPreset::Emulation);
        a.reset_to_defaults();
        a.reset_keyboard_defaults();
        CHECK(!fs::exists(a_ini));
        CHECK(!fs::exists("keybinds.ini"));
        CHECK_EQ(int(a.get_key_scancode(KEY_A)), int(SDL_SCANCODE_X));  // preset itself still applied

        // Explicit save/load use the configured path when none is passed.
        const std::string b_ini = cfg.file("b.ini");
        a.set_keybinds_path(b_ini);
        a.apply_keyboard_preset(KeyboardPreset::ArrowsQW);
        a.save_keybinds();
        CHECK(fs::exists(b_ini));
        InputEnhancements b;                           // default ctor: reads ./keybinds.ini (absent)
        CHECK_EQ(b.keybinds_path(), std::string("keybinds.ini"));
        b.set_autosave_keybinds(false);
        b.set_keybinds_path(b_ini);
        b.load_keybinds();
        CHECK_EQ(int(b.get_key_scancode(KEY_L)), int(SDL_SCANCODE_Q));
        a.set_keybinds_path("");                       // empty restores the default name
        CHECK_EQ(a.keybinds_path(), std::string("keybinds.ini"));
        CHECK(!fs::exists("keybinds.ini"));
    }

    // --- process_keyinput state is per instance (no function-level statics) -----
    {
        testh::TempDir cfg("input_state");
        cfg.enter();
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        CHECK(ks != nullptr);
        if (ks) {
            Uint8* mut = const_cast<Uint8*>(ks);       // synthetic key state (X held)
            mut[SDL_SCANCODE_X] = 1;

            InputEnhancements p(cfg.file("p.ini"));    // default layout: X = GBA A
            InputEnhancements q(cfg.file("q.ini"));
            p.set_autosave_keybinds(false);
            q.set_autosave_keybinds(false);
            q.apply_keyboard_preset(KeyboardPreset::Wasd);   // J = A, X unbound

            uint64_t frame = 5000;
            hooks().is_input_blocked = nullptr;
            hooks().frame_counter = [&] { return frame; };

            const uint16_t kp = p.process_keyinput(0x03FF);
            CHECK_EQ(kp, uint16_t(0x03FE));            // X -> A pressed
            // Same frame and host keys on a DIFFERENT instance must not hit p's cache.
            const uint16_t kq = q.process_keyinput(0x03FF);
            CHECK_EQ(kq, uint16_t(0x03FF));
            // And p still serves its own cached value for this frame.
            CHECK_EQ(p.process_keyinput(0x03FF), uint16_t(0x03FE));

            // Within a frame q serves its cache; reset_to_defaults() drops it.
            q.set_key_scancode(KEY_A, SDL_SCANCODE_X);
            CHECK_EQ(q.process_keyinput(0x03FF), uint16_t(0x03FF));  // cached for this frame
            q.reset_to_defaults();
            CHECK_EQ(q.process_keyinput(0x03FF), uint16_t(0x03FE));  // recomputed: X = A again

            mut[SDL_SCANCODE_X] = 0;
            hooks() = Hooks{};
        }
    }

    // --- Dialog turbo: defaults, configuration, persistence -----------------------
    {
        testh::TempDir cfg("input_turbo_cfg");
        cfg.enter();
        InputEnhancements t(cfg.file("t.ini"));
        t.set_autosave_keybinds(false);
        CHECK_EQ(int(t.settings().turbo_key), int(SDL_SCANCODE_TAB));
        CHECK(t.settings().turbo_pad == PadButtonChoice::ButtonY);
        t.set_turbo_pad(99);
        CHECK(t.settings().turbo_pad == PadButtonChoice::RightTrigger);
        t.set_turbo_pad(-1);
        CHECK(t.settings().turbo_pad == PadButtonChoice::ButtonA);
        CHECK(!t.set_turbo_key_from_name("not a key"));
        CHECK(!t.set_turbo_key_from_name(nullptr));
        CHECK(t.set_turbo_key_from_name("Space"));
        CHECK_EQ(int(t.settings().turbo_key), int(SDL_SCANCODE_SPACE));
        t.set_turbo_pad(5);
        t.save_keybinds();
        CHECK(slurp(cfg.file("t.ini")).find("[turbo]") != std::string::npos);

        InputEnhancements u(cfg.file("t.ini"));
        CHECK_EQ(int(u.settings().turbo_key), int(SDL_SCANCODE_SPACE));
        CHECK(u.settings().turbo_pad == PadButtonChoice::RightBumper);

        // Key disabled ("None") round-trips.
        t.set_turbo_key(SDL_SCANCODE_UNKNOWN);
        t.save_keybinds();
        CHECK(slurp(cfg.file("t.ini")).find("key = None") != std::string::npos);
        InputEnhancements v(cfg.file("t.ini"));
        CHECK_EQ(int(v.settings().turbo_key), int(SDL_SCANCODE_UNKNOWN));

        // A file without a [turbo] section keeps the defaults.
        { std::ofstream f(cfg.file("old.ini")); f << "[player1]\nA = X\n"; }
        InputEnhancements w(cfg.file("old.ini"));
        CHECK_EQ(int(w.settings().turbo_key), int(SDL_SCANCODE_TAB));
        CHECK(w.settings().turbo_pad == PadButtonChoice::ButtonY);
    }

    // --- Dialog turbo: process_keyinput drives DialogTurbo::set_held ---------------
    {
        testh::TempDir cfg("input_turbo");
        cfg.enter();
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        CHECK(ks != nullptr);
        if (ks) {
            Uint8* mut = const_cast<Uint8*>(ks);
            InputEnhancements p(cfg.file("p.ini"));
            p.set_autosave_keybinds(false);
            DialogTurbo turbo;
            p.set_dialog_turbo(&turbo);
            CHECK(&p.dialog_turbo() == &turbo);
            CHECK(&InputEnhancements::instance().dialog_turbo() == &DialogTurbo::instance());

            uint64_t frame = 100;
            hooks().is_input_blocked = nullptr;
            hooks().frame_counter = [&] { return frame; };

            // Count A/B pulses over 8 frames.
            auto run = [&](int frames) {
                int pulses = 0;
                for (int i = 0; i < frames; ++i) {
                    ++frame;
                    const uint16_t k = p.process_keyinput(0x03FF);
                    if ((k & 0x3) == 0) ++pulses;
                    else CHECK_EQ(int(k & 0x3), 0x3);
                }
                return pulses;
            };

            // Released: never held, never pulses.
            CHECK_EQ(run(8), 0);
            CHECK(!turbo.is_held());

            // Held (default Tab): held is driven, A+B pulse (speed 2 -> every other frame).
            mut[SDL_SCANCODE_TAB] = 1;
            CHECK_EQ(run(8), 4);
            CHECK(turbo.is_held());

            // Released again: held drops and pulses stop.
            mut[SDL_SCANCODE_TAB] = 0;
            CHECK_EQ(run(8), 0);
            CHECK(!turbo.is_held());

            // Disabled DialogTurbo: held is still tracked but no pulses.
            turbo.set_enabled(false);
            mut[SDL_SCANCODE_TAB] = 1;
            CHECK_EQ(run(8), 0);
            CHECK(turbo.is_held());
            turbo.set_enabled(true);

            // Remapped key: Tab no longer triggers, the new key does.
            p.set_turbo_key(SDL_SCANCODE_F5);
            CHECK_EQ(run(4), 0);
            mut[SDL_SCANCODE_TAB] = 0;
            mut[SDL_SCANCODE_F5] = 1;
            CHECK_EQ(run(8), 4);
            mut[SDL_SCANCODE_F5] = 0;

            // Input blocked releases the held state.
            mut[SDL_SCANCODE_F5] = 1;
            run(2);
            CHECK(turbo.is_held());
            hooks().is_input_blocked = [] { return true; };
            ++frame;
            CHECK_EQ(p.process_keyinput(0x03FF), uint16_t(0x03FF));
            CHECK(!turbo.is_held());

            mut[SDL_SCANCODE_F5] = 0;
            hooks() = Hooks{};
        }
    }

    return testh::finish("input_enhancements");
}
