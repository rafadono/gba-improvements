#include "gbaimp/input_enhancements.h"
#include "gbaimp/hooks.h"
#include "gbaimp/dialog_turbo.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cctype>

namespace gbaimp {

namespace {

bool is_pad_button_pressed(SDL_GameController* pad, PadButtonChoice choice) {
    if (!pad) return false;
    switch (choice) {
        case PadButtonChoice::ButtonA:
            return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
        case PadButtonChoice::ButtonB:
            return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) != 0;
        case PadButtonChoice::ButtonX:
            return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) != 0;
        case PadButtonChoice::ButtonY:
            return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) != 0;
        case PadButtonChoice::LeftBumper:
            return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
        case PadButtonChoice::RightBumper:
            return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
        case PadButtonChoice::LeftTrigger:
            return SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        case PadButtonChoice::RightTrigger:
            return SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        default:
            return false;
    }
}

} // namespace

InputEnhancements& InputEnhancements::instance() {
    static InputEnhancements s_instance;
    return s_instance;
}

InputEnhancements::InputEnhancements() {
    load_keybinds();
}

InputEnhancements::InputEnhancements(const std::string& keybinds_path)
    : keybinds_path_(keybinds_path.empty() ? std::string("keybinds.ini") : keybinds_path) {
    load_keybinds();
}

InputEnhancements::~InputEnhancements() {
    // The host may already have shut SDL down (singleton destroyed at exit).
    if (pad_ && SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        SDL_GameControllerClose(pad_);
    }
    pad_ = nullptr;
}

void InputEnhancements::set_keybinds_path(const std::string& path) {
    keybinds_path_ = path.empty() ? std::string("keybinds.ini") : path;
}

void InputEnhancements::set_analog_mode(AnalogMode mode) {
    settings_.analog_mode = mode;
}

void InputEnhancements::set_deadzone(float deadzone) {
    settings_.deadzone = std::clamp(deadzone, 0.05f, 0.50f);
}

void InputEnhancements::set_key_scancode(GbaKeyIndex idx, SDL_Scancode sc) {
    if (idx >= 0 && idx < KEY_COUNT) {
        settings_.key_scancodes[idx] = sc;
        settings_.kb_preset = KeyboardPreset::Custom;
    }
}

SDL_Scancode InputEnhancements::get_key_scancode(GbaKeyIndex idx) const {
    if (idx >= 0 && idx < KEY_COUNT) {
        return settings_.key_scancodes[idx];
    }
    return SDL_SCANCODE_UNKNOWN;
}

const char* InputEnhancements::get_key_name(GbaKeyIndex idx) const {
    SDL_Scancode sc = get_key_scancode(idx);
    const char* name = SDL_GetScancodeName(sc);
    return (name && *name) ? name : "None";
}

bool InputEnhancements::set_key_from_name(GbaKeyIndex idx, const char* name) {
    if (idx < 0 || idx >= KEY_COUNT || !name || !*name) return false;
    SDL_Scancode sc = scancode_from_string(name);
    if (sc != SDL_SCANCODE_UNKNOWN) {
        set_key_scancode(idx, sc);
        return true;
    }
    return false;
}

GbaKeyIndex InputEnhancements::key_index_from_name(const char* name) const {
    if (!name || !*name) return KEY_COUNT;
    if (SDL_strcasecmp(name, "a") == 0) return KEY_A;
    if (SDL_strcasecmp(name, "b") == 0) return KEY_B;
    if (SDL_strcasecmp(name, "select") == 0) return KEY_SELECT;
    if (SDL_strcasecmp(name, "start") == 0) return KEY_START;
    if (SDL_strcasecmp(name, "right") == 0) return KEY_RIGHT;
    if (SDL_strcasecmp(name, "left") == 0) return KEY_LEFT;
    if (SDL_strcasecmp(name, "up") == 0) return KEY_UP;
    if (SDL_strcasecmp(name, "down") == 0) return KEY_DOWN;
    if (SDL_strcasecmp(name, "r") == 0) return KEY_R;
    if (SDL_strcasecmp(name, "l") == 0) return KEY_L;
    return KEY_COUNT;
}

SDL_Scancode InputEnhancements::scancode_from_string(const char* name) {
    if (!name || !*name) return SDL_SCANCODE_UNKNOWN;
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    if (sc != SDL_SCANCODE_UNKNOWN) return sc;

    std::string b;
    for (const char* p = name; *p; ++p) {
        b += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
    while (!b.empty() && (b.front() == ' ' || b.front() == '\t')) b.erase(b.begin());
    while (!b.empty() && (b.back() == ' ' || b.back() == '\t')) b.pop_back();

    sc = SDL_GetScancodeFromName(b.c_str());
    if (sc != SDL_SCANCODE_UNKNOWN) return sc;

    if (b == "enter" || b == "return") return SDL_SCANCODE_RETURN;
    if (b == "tab") return SDL_SCANCODE_TAB;
    if (b == "space") return SDL_SCANCODE_SPACE;
    if (b == "lshift" || b == "shift" || b == "leftshift" || b == "left shift") return SDL_SCANCODE_LSHIFT;
    if (b == "rshift" || b == "rightshift" || b == "right shift") return SDL_SCANCODE_RSHIFT;
    if (b == "lctrl" || b == "ctrl" || b == "leftctrl" || b == "left ctrl") return SDL_SCANCODE_LCTRL;
    if (b == "rctrl" || b == "rightctrl" || b == "right ctrl") return SDL_SCANCODE_RCTRL;
    if (b == "lalt" || b == "alt" || b == "leftalt" || b == "left alt") return SDL_SCANCODE_LALT;
    if (b == "ralt" || b == "rightalt" || b == "right alt") return SDL_SCANCODE_RALT;
    if (b == "backslash") return SDL_SCANCODE_BACKSLASH;
    if (b == "escape" || b == "esc") return SDL_SCANCODE_ESCAPE;
    if (b == "backspace") return SDL_SCANCODE_BACKSPACE;
    if (b == "up" || b == "uparrow" || b == "up arrow") return SDL_SCANCODE_UP;
    if (b == "down" || b == "downarrow" || b == "down arrow") return SDL_SCANCODE_DOWN;
    if (b == "left" || b == "leftarrow" || b == "left arrow") return SDL_SCANCODE_LEFT;
    if (b == "right" || b == "rightarrow" || b == "right arrow") return SDL_SCANCODE_RIGHT;
    if (b == "pageup" || b == "page up") return SDL_SCANCODE_PAGEUP;
    if (b == "pagedown" || b == "page down") return SDL_SCANCODE_PAGEDOWN;
    if (b == "home") return SDL_SCANCODE_HOME;
    if (b == "end") return SDL_SCANCODE_END;
    if (b == "insert") return SDL_SCANCODE_INSERT;
    if (b == "delete") return SDL_SCANCODE_DELETE;
    if (b == "capslock" || b == "caps lock") return SDL_SCANCODE_CAPSLOCK;
    if (b == "leftbracket" || b == "left bracket") return SDL_SCANCODE_LEFTBRACKET;
    if (b == "rightbracket" || b == "right bracket") return SDL_SCANCODE_RIGHTBRACKET;
    if (b == "graveaccent" || b == "grave") return SDL_SCANCODE_GRAVE;
    if (b == "equal" || b == "equals") return SDL_SCANCODE_EQUALS;
    if (b == "minus") return SDL_SCANCODE_MINUS;
    if (b == "semicolon") return SDL_SCANCODE_SEMICOLON;
    if (b == "comma") return SDL_SCANCODE_COMMA;
    if (b == "period") return SDL_SCANCODE_PERIOD;
    if (b == "slash") return SDL_SCANCODE_SLASH;

    if (b.length() == 1) {
        char c = b[0];
        if (c >= 'a' && c <= 'z') {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'a'));
        }
        if (c >= '1' && c <= '9') {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (c - '1'));
        }
        if (c == '0') return SDL_SCANCODE_0;
    }
    return SDL_SCANCODE_UNKNOWN;
}

void InputEnhancements::apply_keyboard_preset(KeyboardPreset preset) {
    settings_.kb_preset = preset;
    switch (preset) {
        case KeyboardPreset::Default:
            settings_.key_scancodes[KEY_A] = SDL_SCANCODE_X;
            settings_.key_scancodes[KEY_B] = SDL_SCANCODE_Z;
            settings_.key_scancodes[KEY_SELECT] = SDL_SCANCODE_RSHIFT;
            settings_.key_scancodes[KEY_START] = SDL_SCANCODE_RETURN;
            settings_.key_scancodes[KEY_RIGHT] = SDL_SCANCODE_RIGHT;
            settings_.key_scancodes[KEY_LEFT] = SDL_SCANCODE_LEFT;
            settings_.key_scancodes[KEY_UP] = SDL_SCANCODE_UP;
            settings_.key_scancodes[KEY_DOWN] = SDL_SCANCODE_DOWN;
            settings_.key_scancodes[KEY_R] = SDL_SCANCODE_V;
            settings_.key_scancodes[KEY_L] = SDL_SCANCODE_C;
            break;
        case KeyboardPreset::Wasd:
            settings_.key_scancodes[KEY_A] = SDL_SCANCODE_J;
            settings_.key_scancodes[KEY_B] = SDL_SCANCODE_K;
            settings_.key_scancodes[KEY_SELECT] = SDL_SCANCODE_SPACE;
            settings_.key_scancodes[KEY_START] = SDL_SCANCODE_RETURN;
            settings_.key_scancodes[KEY_RIGHT] = SDL_SCANCODE_D;
            settings_.key_scancodes[KEY_LEFT] = SDL_SCANCODE_A;
            settings_.key_scancodes[KEY_UP] = SDL_SCANCODE_W;
            settings_.key_scancodes[KEY_DOWN] = SDL_SCANCODE_S;
            settings_.key_scancodes[KEY_R] = SDL_SCANCODE_I;
            settings_.key_scancodes[KEY_L] = SDL_SCANCODE_U;
            break;
        case KeyboardPreset::Emulation:
            settings_.key_scancodes[KEY_A] = SDL_SCANCODE_Z;
            settings_.key_scancodes[KEY_B] = SDL_SCANCODE_X;
            settings_.key_scancodes[KEY_SELECT] = SDL_SCANCODE_BACKSPACE;
            settings_.key_scancodes[KEY_START] = SDL_SCANCODE_RETURN;
            settings_.key_scancodes[KEY_RIGHT] = SDL_SCANCODE_RIGHT;
            settings_.key_scancodes[KEY_LEFT] = SDL_SCANCODE_LEFT;
            settings_.key_scancodes[KEY_UP] = SDL_SCANCODE_UP;
            settings_.key_scancodes[KEY_DOWN] = SDL_SCANCODE_DOWN;
            settings_.key_scancodes[KEY_R] = SDL_SCANCODE_S;
            settings_.key_scancodes[KEY_L] = SDL_SCANCODE_A;
            break;
        case KeyboardPreset::ArrowsQW:
            settings_.key_scancodes[KEY_A] = SDL_SCANCODE_Z;
            settings_.key_scancodes[KEY_B] = SDL_SCANCODE_X;
            settings_.key_scancodes[KEY_SELECT] = SDL_SCANCODE_RSHIFT;
            settings_.key_scancodes[KEY_START] = SDL_SCANCODE_RETURN;
            settings_.key_scancodes[KEY_RIGHT] = SDL_SCANCODE_RIGHT;
            settings_.key_scancodes[KEY_LEFT] = SDL_SCANCODE_LEFT;
            settings_.key_scancodes[KEY_UP] = SDL_SCANCODE_UP;
            settings_.key_scancodes[KEY_DOWN] = SDL_SCANCODE_DOWN;
            settings_.key_scancodes[KEY_R] = SDL_SCANCODE_W;
            settings_.key_scancodes[KEY_L] = SDL_SCANCODE_Q;
            break;
        case KeyboardPreset::Custom:
        default:
            break;
    }
    if (autosave_keybinds_) save_keybinds();
}

void InputEnhancements::apply_gamepad_profile(GamepadProfile profile) {
    settings_.gamepad_profile = profile;
    switch (profile) {
        case GamepadProfile::Standard:
            settings_.pad_a = PadButtonChoice::ButtonA;
            settings_.pad_b = PadButtonChoice::ButtonB;
            settings_.pad_l = PadButtonChoice::LeftBumper;
            settings_.pad_r = PadButtonChoice::RightBumper;
            break;
        case GamepadProfile::Nintendo:
            settings_.pad_a = PadButtonChoice::ButtonB;
            settings_.pad_b = PadButtonChoice::ButtonA;
            settings_.pad_l = PadButtonChoice::LeftBumper;
            settings_.pad_r = PadButtonChoice::RightBumper;
            break;
        case GamepadProfile::PlayStation:
            settings_.pad_a = PadButtonChoice::ButtonX;
            settings_.pad_b = PadButtonChoice::ButtonA;
            settings_.pad_l = PadButtonChoice::LeftBumper;
            settings_.pad_r = PadButtonChoice::RightBumper;
            break;
        case GamepadProfile::ShoulderSwap:
            settings_.pad_a = PadButtonChoice::ButtonA;
            settings_.pad_b = PadButtonChoice::ButtonB;
            settings_.pad_l = PadButtonChoice::RightBumper;
            settings_.pad_r = PadButtonChoice::LeftBumper;
            break;
        case GamepadProfile::Triggers:
            settings_.pad_a = PadButtonChoice::ButtonA;
            settings_.pad_b = PadButtonChoice::ButtonB;
            settings_.pad_l = PadButtonChoice::LeftTrigger;
            settings_.pad_r = PadButtonChoice::RightTrigger;
            break;
        case GamepadProfile::Custom:
        default:
            break;
    }
}

void InputEnhancements::set_pad_a(int choice) {
    settings_.pad_a = static_cast<PadButtonChoice>(std::clamp(choice, 0, 7));
    settings_.gamepad_profile = GamepadProfile::Custom;
}

void InputEnhancements::set_pad_b(int choice) {
    settings_.pad_b = static_cast<PadButtonChoice>(std::clamp(choice, 0, 7));
    settings_.gamepad_profile = GamepadProfile::Custom;
}

void InputEnhancements::set_pad_l(int choice) {
    settings_.pad_l = static_cast<PadButtonChoice>(std::clamp(choice, 0, 7));
    settings_.gamepad_profile = GamepadProfile::Custom;
}

void InputEnhancements::set_pad_r(int choice) {
    settings_.pad_r = static_cast<PadButtonChoice>(std::clamp(choice, 0, 7));
    settings_.gamepad_profile = GamepadProfile::Custom;
}

void InputEnhancements::set_turbo_key(SDL_Scancode sc) {
    settings_.turbo_key = sc;
}

bool InputEnhancements::set_turbo_key_from_name(const char* name) {
    if (!name || !*name) return false;
    if (SDL_strcasecmp(name, "none") == 0) {
        settings_.turbo_key = SDL_SCANCODE_UNKNOWN;
        return true;
    }
    SDL_Scancode sc = scancode_from_string(name);
    if (sc == SDL_SCANCODE_UNKNOWN) return false;
    settings_.turbo_key = sc;
    return true;
}

void InputEnhancements::set_turbo_pad(int choice) {
    settings_.turbo_pad = static_cast<PadButtonChoice>(std::clamp(choice, 0, 7));
}

DialogTurbo& InputEnhancements::dialog_turbo() {
    return turbo_ ? *turbo_ : DialogTurbo::instance();
}

void InputEnhancements::load_keybinds(const char* path) {
    std::FILE* f = std::fopen(path ? path : keybinds_path_.c_str(), "r");
    if (!f) return;
    char line[256];
    bool in_player1 = false;
    bool in_turbo = false;
    while (std::fgets(line, sizeof(line), f)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        size_t len = std::strlen(s);
        while (len && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t')) {
            s[--len] = '\0';
        }
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            in_player1 = (SDL_strcasecmp(s, "[player1]") == 0);
            in_turbo = (SDL_strcasecmp(s, "[turbo]") == 0);
            continue;
        }
        if (!in_player1 && !in_turbo) continue;
        char* eq = std::strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = s;
        char* v = eq + 1;
        while (*k && (k[std::strlen(k)-1] == ' ' || k[std::strlen(k)-1] == '\t')) {
            k[std::strlen(k)-1] = '\0';
        }
        while (*v == ' ' || *v == '\t') ++v;
        if (in_turbo) {
            if (SDL_strcasecmp(k, "key") == 0) set_turbo_key_from_name(v);
            else if (SDL_strcasecmp(k, "pad") == 0) set_turbo_pad(std::atoi(v));
            continue;
        }
        GbaKeyIndex idx = key_index_from_name(k);
        if (idx != KEY_COUNT) {
            set_key_from_name(idx, v);
        }
    }
    std::fclose(f);

}

void InputEnhancements::save_keybinds(const char* path) const {
    std::ofstream f(path ? path : keybinds_path_.c_str());
    if (!f.is_open()) return;
    f << "[player1]\n";
    f << "A = " << get_key_name(KEY_A) << "\n";
    f << "B = " << get_key_name(KEY_B) << "\n";
    f << "Select = " << get_key_name(KEY_SELECT) << "\n";
    f << "Start = " << get_key_name(KEY_START) << "\n";
    f << "Right = " << get_key_name(KEY_RIGHT) << "\n";
    f << "Left = " << get_key_name(KEY_LEFT) << "\n";
    f << "Up = " << get_key_name(KEY_UP) << "\n";
    f << "Down = " << get_key_name(KEY_DOWN) << "\n";
    f << "R = " << get_key_name(KEY_R) << "\n";
    f << "L = " << get_key_name(KEY_L) << "\n";
    const char* tn = SDL_GetScancodeName(settings_.turbo_key);
    f << "\n[turbo]\n";
    f << "key = " << ((tn && *tn) ? tn : "None") << "\n";
    f << "pad = " << static_cast<int>(settings_.turbo_pad) << "\n";
}

void InputEnhancements::reset_gamepad_defaults() {
    settings_.analog_mode = AnalogMode::AnalogWalkRun;
    settings_.deadzone = 0.18f;
    apply_gamepad_profile(GamepadProfile::Standard);
}

void InputEnhancements::reset_keyboard_defaults() {
    apply_keyboard_preset(KeyboardPreset::Default);
    if (autosave_keybinds_) save_keybinds();
}

void InputEnhancements::reset_to_defaults() {
    reset_gamepad_defaults();
    reset_keyboard_defaults();
    // Bindings changed: drop the per-frame input cache so the next poll recomputes.
    last_frame_ = ~0ULL;
    last_host_keys_ = 0xFFFFu;
    cached_keys_ = 0x03FFu;
}

void InputEnhancements::process_axis_input(int16_t axis_x, int16_t axis_y, uint16_t& keyinput) {
    if (settings_.analog_mode == AnalogMode::DigitalOriginal) {
        return;
    }

    // Normalize coordinates to [-1.0, 1.0]
    float norm_x = axis_x / 32767.0f;
    float norm_y = axis_y / 32767.0f;
    norm_x = std::clamp(norm_x, -1.0f, 1.0f);
    norm_y = std::clamp(norm_y, -1.0f, 1.0f);

    float magnitude = std::sqrt(norm_x * norm_x + norm_y * norm_y);
    if (magnitude < settings_.deadzone) {
        return;
    }

    ++walk_frame_counter_;

    // If walk mode is active and stick is tilted gently (< walk_threshold)
    bool is_walking = (settings_.analog_mode == AnalogMode::AnalogWalkRun) &&
                      (magnitude < settings_.walk_threshold);

    if (is_walking && (walk_frame_counter_ % 2 == 1)) {
        // Drop directional input on alternate frames to halve the effective movement speed
        return;
    }

    // Clear direction bits (active low: 1 = released, 0 = pressed)
    // Bit 4: Right, Bit 5: Left, Bit 6: Up, Bit 7: Down
    keyinput |= 0x00F0u;

    // Calculate angle in degrees [0, 360)
    float angle = std::atan2(norm_y, norm_x) * (180.0f / 3.14159265f);
    if (angle < 0.0f) angle += 360.0f;

    // 8-directional sectors with 45-degree slices
    if (angle >= 337.5f || angle < 22.5f) {
        keyinput &= ~0x0010u; // Right
    } else if (angle >= 22.5f && angle < 67.5f) {
        keyinput &= ~0x0010u; // Right
        keyinput &= ~0x0080u; // Down
    } else if (angle >= 67.5f && angle < 112.5f) {
        keyinput &= ~0x0080u; // Down
    } else if (angle >= 112.5f && angle < 157.5f) {
        keyinput &= ~0x0020u; // Left
        keyinput &= ~0x0080u; // Down
    } else if (angle >= 157.5f && angle < 202.5f) {
        keyinput &= ~0x0020u; // Left
    } else if (angle >= 202.5f && angle < 247.5f) {
        keyinput &= ~0x0020u; // Left
        keyinput &= ~0x0040u; // Up
    } else if (angle >= 247.5f && angle < 292.5f) {
        keyinput &= ~0x0040u; // Up
    } else if (angle >= 292.5f && angle < 337.5f) {
        keyinput &= ~0x0010u; // Right
        keyinput &= ~0x0040u; // Up
    }
}

uint16_t InputEnhancements::process_keyinput(uint16_t host_keys) {
    if (input_blocked()) {
        dialog_turbo().set_held(false);
        return 0x03FFu;
    }

    const uint64_t current_frame = hooks().frame_counter ? hooks().frame_counter() : ++fallback_frame_;
    if (current_frame == last_frame_ && host_keys == last_host_keys_) {
        return cached_keys_;
    }

    uint16_t keys = 0x03FFu;

    // 1. Keyboard processing
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (ks) {
        for (int bit = 0; bit < KEY_COUNT; ++bit) {
            SDL_Scancode sc = settings_.key_scancodes[bit];
            if (sc != SDL_SCANCODE_UNKNOWN && ks[sc]) {
                keys &= static_cast<uint16_t>(~(1u << bit));
            }
        }
    }

    // 2. Controller processing: rate-limit joystick detection if none is attached
    if (!pad_ || !SDL_GameControllerGetAttached(pad_)) {
        if (pad_) {
            SDL_GameControllerClose(pad_);
            pad_ = nullptr;
        }
        uint32_t now = SDL_GetTicks();
        if (now - last_joy_detect_ms_ >= 2000) {
            last_joy_detect_ms_ = now;
            int num_joysticks = SDL_NumJoysticks();
            for (int i = 0; i < num_joysticks; ++i) {
                if (SDL_IsGameController(i)) {
                    pad_ = SDL_GameControllerOpen(i);
                    if (pad_) break;
                }
            }
        }
    }

    if (pad_) {
        if (is_pad_button_pressed(pad_, settings_.pad_a)) keys &= ~(1u << KEY_A);
        if (is_pad_button_pressed(pad_, settings_.pad_b)) keys &= ~(1u << KEY_B);
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_BACK)) keys &= ~(1u << KEY_SELECT);
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_START)) keys &= ~(1u << KEY_START);
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) keys &= ~(1u << KEY_RIGHT);
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) keys &= ~(1u << KEY_LEFT);
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_UP)) keys &= ~(1u << KEY_UP);
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) keys &= ~(1u << KEY_DOWN);
        if (is_pad_button_pressed(pad_, settings_.pad_r)) keys &= ~(1u << KEY_R);
        if (is_pad_button_pressed(pad_, settings_.pad_l)) keys &= ~(1u << KEY_L);

        int16_t axis_x = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t axis_y = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
        process_axis_input(axis_x, axis_y, keys);
    } else {
        // Fallback: merge host keys
        keys &= host_keys;
    }

    // 3. Dialog Turbo: drive the held state every frame, then apply the A/B pulse.
    bool turbo_held = false;
    if (ks && settings_.turbo_key != SDL_SCANCODE_UNKNOWN && ks[settings_.turbo_key]) turbo_held = true;
    if (is_pad_button_pressed(pad_, settings_.turbo_pad)) turbo_held = true;
    DialogTurbo& turbo = dialog_turbo();
    turbo.set_held(turbo_held);
    if (turbo_held && turbo.is_enabled()) {
        keys = turbo.process_keyinput(keys, ++turbo_frame_counter_);
    }

    last_frame_ = current_frame;
    last_host_keys_ = host_keys;
    cached_keys_ = keys;

    return keys;
}

} // namespace gbaimp
