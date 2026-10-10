#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <SDL.h>

namespace gbaimp {

enum class AnalogMode : int {
    DigitalOriginal = 0,
    Analog8Way = 1,
    AnalogWalkRun = 2
};

enum GbaKeyIndex : int {
    KEY_A = 0,
    KEY_B = 1,
    KEY_SELECT = 2,
    KEY_START = 3,
    KEY_RIGHT = 4,
    KEY_LEFT = 5,
    KEY_UP = 6,
    KEY_DOWN = 7,
    KEY_R = 8,
    KEY_L = 9,
    KEY_COUNT = 10
};

enum class GamepadProfile : int {
    Standard = 0,       // Xbox/Standard: A->GBA A, B->GBA B, LB->L, RB->R
    Nintendo = 1,       // Nintendo Style: B->GBA A, A->GBA B, LB->L, RB->R
    PlayStation = 2,    // PlayStation Style: X->GBA A, A->GBA B, LB->L, RB->R
    ShoulderSwap = 3,   // Swapped Bumpers: RB->L, LB->R
    Triggers = 4,    // Triggers: LT->L, RT->R
    Custom = 5
};

enum class KeyboardPreset : int {
    Default = 0,        // X=A, Z=B, C=L, V=R, Return=Start, RShift=Select, Arrows
    Wasd = 1,           // J=A, K=B, U=L, I=R, Return=Start, Space=Select, WASD=Directions
    Emulation = 2,      // Z=A, X=B, A=L, S=R, Return=Start, Backspace=Select, Arrows
    ArrowsQW = 3,       // Z=A, X=B, Q=L, W=R, Return=Start, RShift=Select, Arrows
    Custom = 4
};

enum class PadButtonChoice : int {
    ButtonA = 0,
    ButtonB = 1,
    ButtonX = 2,
    ButtonY = 3,
    LeftBumper = 4,
    RightBumper = 5,
    LeftTrigger = 6,
    RightTrigger = 7
};

struct InputEnhancementSettings {
    AnalogMode analog_mode = AnalogMode::AnalogWalkRun;
    float deadzone = 0.18f;
    float walk_threshold = 0.55f;

    GamepadProfile gamepad_profile = GamepadProfile::Standard;
    PadButtonChoice pad_a = PadButtonChoice::ButtonA;
    PadButtonChoice pad_b = PadButtonChoice::ButtonB;
    PadButtonChoice pad_l = PadButtonChoice::LeftBumper;
    PadButtonChoice pad_r = PadButtonChoice::RightBumper;

    KeyboardPreset kb_preset = KeyboardPreset::Default;
    std::array<SDL_Scancode, KEY_COUNT> key_scancodes = {
        SDL_SCANCODE_X,       // A
        SDL_SCANCODE_Z,       // B
        SDL_SCANCODE_RSHIFT,  // Select
        SDL_SCANCODE_RETURN,  // Start
        SDL_SCANCODE_RIGHT,   // Right
        SDL_SCANCODE_LEFT,    // Left
        SDL_SCANCODE_UP,      // Up
        SDL_SCANCODE_DOWN,    // Down
        SDL_SCANCODE_V,       // R
        SDL_SCANCODE_C        // L
    };
};

class InputEnhancements {
public:
    static InputEnhancements& instance();

    const InputEnhancementSettings& settings() const { return settings_; }
    InputEnhancementSettings& settings() { return settings_; }

    void set_analog_mode(AnalogMode mode);
    void set_deadzone(float deadzone);

    // Keyboard configuration
    void set_key_scancode(GbaKeyIndex idx, SDL_Scancode sc);
    SDL_Scancode get_key_scancode(GbaKeyIndex idx) const;
    const char* get_key_name(GbaKeyIndex idx) const;
    bool set_key_from_name(GbaKeyIndex idx, const char* name);
    GbaKeyIndex key_index_from_name(const char* name) const;
    void apply_keyboard_preset(KeyboardPreset preset);

    // Gamepad configuration
    void apply_gamepad_profile(GamepadProfile profile);
    void set_pad_a(int choice);
    void set_pad_b(int choice);
    void set_pad_l(int choice);
    void set_pad_r(int choice);

    // Persistence
    void load_keybinds(const char* path = "keybinds.ini");
    void save_keybinds(const char* path = "keybinds.ini") const;
    void reset_to_defaults();
    void reset_gamepad_defaults();
    void reset_keyboard_defaults();

    // Process inputs: modulate and produce GBA active-low KEYINPUT
    void process_axis_input(int16_t axis_x, int16_t axis_y, uint16_t& keyinput);
    uint16_t process_keyinput(uint16_t host_keys);

    static SDL_Scancode scancode_from_string(const char* name);

private:
    InputEnhancements();

    InputEnhancementSettings settings_;
    uint32_t walk_frame_counter_ = 0;
    uint64_t turbo_frame_counter_ = 0;
};

} // namespace gbaimp
