#include "gbaimp/host_settings.h"

#include "gbaimp/audio_dsp.h"
#include "gbaimp/dialog_turbo.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/input_enhancements.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/pixel_art_upscaler.h"
#include "gbaimp/screen_filters.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gbaimp {

namespace {

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// std::stoi semantics: optional leading whitespace, a sign, digits; trailing text is ignored;
// no digits or an out-of-range value fail.
bool parse_int(const std::string& s, int& out) {
    const char* b = s.c_str();
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(b, &end, 10);
    if (end == b || errno == ERANGE || v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// SettingsRegistry
// ---------------------------------------------------------------------------------------------

Setting& SettingsRegistry::add(Setting s) {
    for (Setting& e : settings_) {
        if (e.id == s.id) { e = std::move(s); return e; }
    }
    settings_.push_back(std::move(s));
    return settings_.back();
}

bool SettingsRegistry::remove(const std::string& id) {
    for (auto it = settings_.begin(); it != settings_.end(); ++it) {
        if (it->id == id) { settings_.erase(it); return true; }
    }
    return false;
}

Setting* SettingsRegistry::find(const std::string& id) {
    for (Setting& s : settings_) if (s.id == id) return &s;
    return nullptr;
}

const Setting* SettingsRegistry::find(const std::string& id) const {
    for (const Setting& s : settings_) if (s.id == id) return &s;
    return nullptr;
}

void SettingsRegistry::rename_menu_section(const std::string& from, const std::string& to) {
    for (Setting& s : settings_) if (s.menu_section == from) s.menu_section = to;
}

void SettingsRegistry::add_ignored_id(const std::string& id) { ignored_ids_.push_back(id); }

bool SettingsRegistry::get(const std::string& id, int& value_out) const {
    const Setting* s = find(id);
    if (!s || !s->get) return false;
    if (s->kind != SettingKind::Bool && s->kind != SettingKind::Int && s->kind != SettingKind::Choice) return false;
    value_out = s->get();
    return true;
}

bool SettingsRegistry::set(const std::string& id, int value) {
    const Setting* found = find(id);
    if (!found) return contains(ignored_ids_, id);
    const Setting& s = *found;
    if (!s.set) return false;
    switch (s.kind) {
        case SettingKind::Bool: value = value != 0 ? 1 : 0; break;
        case SettingKind::Int:
        case SettingKind::Choice: value = std::min(std::max(value, s.min), std::max(s.min, s.max)); break;
        default: return false;
    }
    s.set(value);
    notify(s);
    autosave_now();
    return true;
}

bool SettingsRegistry::run_action(const std::string& id) {
    const Setting* s = find(id);
    if (!s || s->kind != SettingKind::Action || !s->action) return false;
    s->action();
    notify(*s);
    if (s->save_after_action) autosave_now();
    return true;
}

bool SettingsRegistry::get_text(const std::string& id, std::string& out) const {
    const Setting* s = find(id);
    if (!s || s->kind != SettingKind::Text || !s->get_text) return false;
    return s->get_text(out);
}

bool SettingsRegistry::set_text(const std::string& id, const std::string& value) {
    const Setting* s = find(id);
    if (!s || s->kind != SettingKind::Text || !s->set_text) return false;
    if (!s->set_text(value)) return false;
    notify(*s);
    return true;
}

bool SettingsRegistry::is_enabled(const std::string& id) const {
    const Setting* s = find(id);
    return !s || !s->enabled || s->enabled();
}

const Setting* SettingsRegistry::by_ini_key(const std::string& key) const {
    for (const Setting& s : settings_) {
        if (s.kind == SettingKind::Action || s.kind == SettingKind::Text) continue;
        if (!s.ini_key.empty() && s.ini_key == key) return &s;
    }
    for (const Setting& s : settings_) {
        if (s.kind == SettingKind::Action || s.kind == SettingKind::Text) continue;
        if (contains(s.ini_aliases, key)) return &s;
    }
    return nullptr;
}

void SettingsRegistry::autosave_now() const {
    if (autosave_ && !loading_) save_ini();
}

std::string SettingsRegistry::ini_text() const {
    std::vector<std::string> order = section_order_;
    auto note = [&order](const std::string& sec) {
        if (!sec.empty() && !contains(order, sec)) order.push_back(sec);
    };
    for (const Setting& s : settings_) {
        if (s.ini_save && !s.ini_key.empty() && s.kind != SettingKind::Action && s.kind != SettingKind::Text) note(s.ini_section);
    }
    for (const IniExtension& e : extensions_) note(e.section);

    std::ostringstream out;
    if (!ini_header_.empty()) {
        std::istringstream h(ini_header_);
        std::string line;
        while (std::getline(h, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out << "# " << line << "\n";
        }
        out << "\n";
    }
    bool first = true;
    for (const std::string& sec : order) {
        std::ostringstream body;
        for (const IniExtension& e : extensions_) if (e.section == sec && e.write) e.write(body);
        for (const Setting& s : settings_) {
            if (s.ini_section != sec || !s.ini_save || s.ini_key.empty() || !s.get) continue;
            if (s.kind == SettingKind::Action || s.kind == SettingKind::Text) continue;
            body << s.ini_key << "=" << s.get() << "\n";
        }
        const std::string text = body.str();
        if (text.empty()) continue;
        if (!first) out << "\n";
        first = false;
        out << "[" << sec << "]\n" << text;
    }
    return out.str();
}

bool SettingsRegistry::save_ini() const {
    if (ini_path_.empty()) return false;
    if (save_blocked_ && save_blocked_()) return false;
    std::ofstream file(ini_path_);
    if (!file.is_open()) return false;
    file << ini_text();
    return static_cast<bool>(file);
}

void SettingsRegistry::process_text(const std::string& text, IniLoadStats& st) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '[' || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        const std::string raw = line.substr(eq + 1);

        if (const Setting* s = by_ini_key(key)) {
            int v = 0;
            if (!parse_int(raw, v)) { ++st.unknown; continue; }
            if (set(s->id, v)) ++st.applied; else ++st.unknown;
            continue;
        }
        bool consumed = false;
        for (const IniExtension& e : extensions_) {
            if (e.read && e.read(key, raw)) { consumed = true; break; }
        }
        if (consumed) { ++st.applied; continue; }
        if (contains(ignored_keys_, key)) { ++st.ignored; continue; }
        ++st.unknown;
    }
}

IniLoadStats SettingsRegistry::load_ini_text(const std::string& text) {
    IniLoadStats st;
    const bool was_loading = loading_;
    loading_ = true;
    process_text(text, st);
    loading_ = was_loading;
    return st;
}

IniLoadStats SettingsRegistry::load_ini(const std::string& overrides) {
    IniLoadStats st;
    for (auto& fn : before_load_) if (fn) fn();

    std::string text;
    if (!ini_path_.empty()) {
        std::ifstream file(ini_path_);
        if (file.is_open()) {
            st.file_found = true;
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }
    if (!st.file_found && overrides.empty()) {
        if (write_defaults_) save_ini();
        return st;
    }
    const bool was_loading = loading_;
    loading_ = true;
    process_text(text, st);
    process_text(overrides, st);
    loading_ = was_loading;
    return st;
}

// ---------------------------------------------------------------------------------------------
// Standard panel
// ---------------------------------------------------------------------------------------------

SettingsTargets SettingsTargets::shared() {
    SettingsTargets t;
    t.dsp = &AudioDsp::instance();
    t.hd_audio = &HdAudioPlayer::instance();
    t.perf_hud = &PerfHud::instance();
    t.interpolator = &FrameInterpolator::instance();
    t.filters = &ScreenFilters::instance();
    t.upscaler = &PixelArtUpscaler::instance();
    t.input = &InputEnhancements::instance();
    t.turbo = &DialogTurbo::instance();
    return t;
}

namespace {

int pct(float v) { return static_cast<int>(v * 100.0f + 0.5f); }

int rate_to_index(int rate) {
    return (rate == 32768) ? 0 : (rate == 44100) ? 1 : (rate == 48000) ? 2 : (rate == 96000) ? 4 : 3;
}
int index_to_rate(int idx) {
    return idx == 0 ? 32768 : idx == 1 ? 44100 : idx == 2 ? 48000 : idx == 4 ? 96000 : 65536;
}

const char* const kSecVideo = "Display and Graphics";
const char* const kSecAudio = "Audio";
const char* const kSecPad = "Controller (Gamepad)";
const char* const kSecKeys = "Keyboard Controls";
const char* const kSecGame = "Gameplay & Assist";
const char* const kSecDiag = "Diagnostics & HUD";

struct Adder {
    SettingsRegistry& reg;
    const StandardSettingsOptions& opt;

    bool skip(const char* id) const { return contains(opt.exclude, id); }

    Setting* base(const char* id, const char* menu, const char* label, const char* tip, SettingKind kind,
                  const char* ini_section, const char* ini_key) {
        if (skip(id)) return nullptr;
        Setting s;
        s.id = id; s.menu_section = menu ? menu : ""; s.label = label ? label : ""; s.tooltip = tip ? tip : "";
        s.kind = kind;
        s.ini_section = ini_section ? ini_section : ""; s.ini_key = ini_key ? ini_key : "";
        s.in_menu = menu != nullptr;
        return &reg.add(std::move(s));
    }

    void boolean(const char* id, const char* menu, const char* label, const char* tip, const char* isec,
                 const char* ikey, std::function<int()> get, std::function<void(int)> set) {
        if (Setting* s = base(id, menu, label, tip, SettingKind::Bool, isec, ikey)) {
            s->min = 0; s->max = 1; s->get = std::move(get); s->set = std::move(set);
        }
    }
    void integer(const char* id, const char* menu, const char* label, const char* tip, const char* isec,
                 const char* ikey, int mn, int mx, int step, std::function<int()> get, std::function<void(int)> set) {
        if (Setting* s = base(id, menu, label, tip, SettingKind::Int, isec, ikey)) {
            s->min = mn; s->max = mx; s->step = step; s->get = std::move(get); s->set = std::move(set);
        }
    }
    void choice(const char* id, const char* menu, const char* label, const char* tip, const char* isec,
                const char* ikey, std::vector<std::string> choices, std::function<int()> get,
                std::function<void(int)> set, int step = 1) {
        if (Setting* s = base(id, menu, label, tip, SettingKind::Choice, isec, ikey)) {
            s->choices = std::move(choices);
            s->min = 0; s->max = static_cast<int>(s->choices.size()) - 1; s->step = step;
            s->get = std::move(get); s->set = std::move(set);
        }
    }
    void action(const char* id, const char* menu, const char* label, const char* tip, std::function<void()> fn) {
        if (Setting* s = base(id, menu, label, tip, SettingKind::Action, nullptr, nullptr)) s->action = std::move(fn);
    }
};

} // namespace

void register_standard_settings(SettingsRegistry& reg) {
    register_standard_settings(reg, SettingsTargets::shared());
}

void register_standard_settings(SettingsRegistry& reg, const SettingsTargets& t, const StandardSettingsOptions& opt) {
    Adder a{reg, opt};

    // ---- video ---------------------------------------------------------------------------
    if (FrameInterpolator* in = t.interpolator) {
        a.choice("video.fps_target", kSecVideo, "Target Frame Rate",
                 "Higher presentation frame rates keep the authentic emulated cadence", "Video", "fps_target",
                 {"60 FPS (Original GBA)", "120 FPS (High Rate)", "144 FPS", "Display Native (Auto)", "Uncapped"},
                 [in] { return static_cast<int>(in->settings().mode); },
                 [in](int v) { in->set_mode(static_cast<FramerateMode>(v)); });
        a.choice("video.motion_smoothing", kSecVideo, "Motion Smoothing", "Intermediate frame blending",
                 "Video", "motion_smoothing", {"Off (Direct)", "Smooth Blend (50% Lerp)", "Motion-Adaptive"},
                 [in] { return static_cast<int>(in->settings().smoothing); },
                 [in](int v) { in->set_smoothing(static_cast<MotionSmoothingMode>(v)); });
    }
    if (PixelArtUpscaler* up = t.upscaler) {
        a.choice("video.xbrz_scale", kSecVideo, "Pixel-Art Upscaler",
                 "Edge-aware pixel-art upscaler, Scale2x/EPX style (2x to 5x)", "Video", "xbrz_scale",
                 {"Off (Original Pixels)", "2x Scale", "3x Scale", "4x Scale", "5x Scale"},
                 [up] { return up->scale() == UpscaleScale::None ? 0 : static_cast<int>(up->scale()) - 1; },
                 [up](int v) { up->set_scale(v <= 0 ? UpscaleScale::None : static_cast<UpscaleScale>(v + 1)); });
    }
    if (ScreenFilters* f = t.filters) {
        a.choice("video.color_profile", kSecVideo, "Color Profile",
                 "Hardware display gamma and color correction curves", "Video", "color_profile",
                 {"Raw (Uncorrected)", "AGB-001 (Reflective TFT)", "AGS-001 (SP Frontlit)", "AGS-101 (SP Backlit)",
                  "MiSTer Gamma 1.6", "MiSTer Gamma 2.2"},
                 [f] { return static_cast<int>(f->settings().color_profile); },
                 [f](int v) { f->set_color_profile(static_cast<ColorProfile>(v)); });
        a.choice("video.screen_mask", kSecVideo, "Screen Mask",
                 "Simulates physical pixel grids, subpixel stripes and scanlines", "Video", "screen_mask",
                 {"Off (Clean Pixels)", "MiSTer LCD Grid", "MiSTer Subpixel RGB", "MiSTer Subpixel BGR",
                  "MiSTer Diffusion", "Game Boy Player Scanlines", "CRT Trinitron"},
                 [f] { return static_cast<int>(f->settings().mask_type); },
                 [f](int v) { f->set_mask_type(static_cast<ScreenMaskType>(v)); });
        a.integer("video.mask_intensity", kSecVideo, "Mask Intensity (%)",
                  "Opacity percentage of the screen mask overlay (10% to 100%)", "Video", "mask_intensity", 10, 100, 5,
                  [f] { return pct(f->settings().mask_intensity); },
                  [f](int v) { f->set_mask_intensity(static_cast<float>(v) / 100.0f); });
    }

    // ---- audio ---------------------------------------------------------------------------
    if (HdAudioPlayer* hd = t.hd_audio) {
        a.choice("audio.sample_rate", kSecAudio, "Pipeline Sample Rate",
                 "Internal DSP and high-resolution playback sample rate", "Audio", "sample_rate",
                 {"32.8 kHz (Standard GBA)", "44.1 kHz (CD Quality)", "48.0 kHz (Studio Quality)",
                  "65.5 kHz (High-Res Native)", "96.0 kHz (Ultra High-Res)"},
                 [hd] { return rate_to_index(hd->settings().target_sample_rate.load()); },
                 [hd](int v) { hd->settings().target_sample_rate = index_to_rate(v); }, 3);
        a.boolean("audio.hd_enabled", kSecAudio, "HD Audio", "High-definition audio playback", "Audio", "hd_enabled",
                  [hd] { return hd->settings().enabled.load() ? 1 : 0; },
                  [hd](int v) { hd->set_enabled(v != 0); });
        a.integer("audio.master_volume", kSecAudio, "Master Volume (%)", "Overall master volume level", "Audio",
                  "master_volume", 0, 100, 5,
                  [hd] { return pct(hd->settings().master_volume.load()); },
                  [hd](int v) { hd->set_master_volume(static_cast<float>(v) / 100.0f); });
        a.integer("audio.bgm_volume", kSecAudio, "Music Volume (%)", "Background music volume level", "Audio",
                  "bgm_volume", 0, 100, 5,
                  [hd] { return pct(hd->settings().bgm_volume.load()); },
                  [hd](int v) { hd->set_bgm_volume(static_cast<float>(v) / 100.0f); });
        a.integer("audio.sfx_volume", kSecAudio, "Sound Effects Volume (%)", "Sound effects volume level", "Audio",
                  "sfx_volume", 0, 100, 5,
                  [hd] { return pct(hd->settings().sfx_volume.load()); },
                  [hd](int v) { hd->set_sfx_volume(static_cast<float>(v) / 100.0f); });
    }
    if (AudioDsp* dsp = t.dsp) {
        a.choice("audio.eq_preset", kSecAudio, "Equalizer Profile", "Real-time DSP sound equalizer profile", "Audio",
                 "eq_preset", {"Flat (Authentic)", "Warm Retro", "Crisp Modern", "Bass Boost"},
                 [dsp] { return static_cast<int>(dsp->settings().preset); },
                 [dsp](int v) { dsp->apply_preset(static_cast<EqPreset>(v)); });
        a.integer("audio.stereo_width", kSecAudio, "Stereo Width (%)", "Stereo width: 0% mono to 200% expanded",
                  "Audio", "stereo_width", 0, 200, 10,
                  [dsp] { return pct(dsp->settings().stereo_width.load()); },
                  [dsp](int v) { dsp->set_stereo_width(static_cast<float>(v) / 100.0f); });
        a.boolean("audio.anti_aliasing", kSecAudio, "DAC Anti-Aliasing", "Biquad filter to cut GBA audio hiss",
                  "Audio", "anti_aliasing",
                  [dsp] { return dsp->settings().anti_aliasing_filter.load() ? 1 : 0; },
                  [dsp](int v) { dsp->set_anti_aliasing(v != 0); });
        a.boolean("audio.limiter", kSecAudio, "Dynamic Peak Limiter", "Dynamic limiter preventing audio clipping",
                  "Audio", "limiter",
                  [dsp] { return dsp->settings().limiter_enabled.load() ? 1 : 0; },
                  [dsp](int v) { dsp->set_limiter(v != 0); });
    }

    // ---- input ---------------------------------------------------------------------------
    if (InputEnhancements* in = t.input) {
        a.choice("input.analog_mode", kSecPad, "Analog Stick Mode", "Enhanced analog stick interpretation",
                 "Controls", "analog_mode", {"Digital (8-Way)", "Analog 8-Way", "True 360"},
                 [in] { return static_cast<int>(in->settings().analog_mode); },
                 [in](int v) { in->set_analog_mode(static_cast<AnalogMode>(v)); });
        a.integer("input.deadzone", kSecPad, "Stick Deadzone (%)", "Stick drift margin (5% to 50%)", "Controls",
                  "analog_deadzone", 5, 50, 5,
                  [in] { return pct(in->settings().deadzone); },
                  [in](int v) { in->set_deadzone(static_cast<float>(v) / 100.0f); });
        a.choice("input.gamepad_profile", kSecPad, "Gamepad Button Layout",
                 "Button mapping presets for modern gamepads", "Controls", "gamepad_profile",
                 {"Xbox / Standard", "Nintendo Style", "PlayStation Style", "Swapped Bumpers",
                  "Triggers for L/R", "Custom Profile"},
                 [in] { return static_cast<int>(in->settings().gamepad_profile); },
                 [in](int v) { in->apply_gamepad_profile(static_cast<GamepadProfile>(v)); });
        const std::vector<std::string> buttons = {
            "Button A (Bottom / Cross)", "Button B (Right / Circle)", "Button X (Left / Square)",
            "Button Y (Top / Triangle)", "Left Bumper (LB / L1)", "Right Bumper (RB / R1)",
            "Left Trigger (LT / L2)", "Right Trigger (RT / R2)"};
        a.choice("input.pad_attack", kSecPad, "GBA A Button", "Gamepad button that presses GBA A", "Controls",
                 "pad_attack", buttons, [in] { return static_cast<int>(in->settings().pad_a); },
                 [in](int v) { in->set_pad_a(v); });
        a.choice("input.pad_jump", kSecPad, "GBA B Button", "Gamepad button that presses GBA B", "Controls",
                 "pad_jump", buttons, [in] { return static_cast<int>(in->settings().pad_b); },
                 [in](int v) { in->set_pad_b(v); });
        a.choice("input.pad_deck_l", kSecPad, "GBA L Button", "Gamepad button that presses GBA L", "Controls",
                 "pad_deck_l", buttons, [in] { return static_cast<int>(in->settings().pad_l); },
                 [in](int v) { in->set_pad_l(v); });
        a.choice("input.pad_deck_r", kSecPad, "GBA R Button", "Gamepad button that presses GBA R", "Controls",
                 "pad_deck_r", buttons, [in] { return static_cast<int>(in->settings().pad_r); },
                 [in](int v) { in->set_pad_r(v); });
        a.action("input.reset_pad", kSecPad, "Reset Gamepad to Defaults", "Restore default gamepad mapping",
                 [in] { in->reset_gamepad_defaults(); });

        a.choice("input.kb_preset", kSecKeys, "Keyboard Preset", "Keyboard layout preset", "Controls", "kb_preset",
                 {"Default (X, Z, C, V, Return, RShift)", "WASD + J/K/U/I", "Classic Emulation (Z, X, A, S)",
                  "Arrows + Z/X/Q/W", "Custom"},
                 [in] { return static_cast<int>(in->settings().kb_preset); },
                 [in](int v) { in->apply_keyboard_preset(static_cast<KeyboardPreset>(v)); });
        struct KeyRow { const char* name; const char* label; const char* tip; };
        static const KeyRow kKeys[] = {
            {"a", "GBA A Key", "Keyboard key for GBA A"},       {"b", "GBA B Key", "Keyboard key for GBA B"},
            {"l", "GBA L Key", "Keyboard key for GBA L"},       {"r", "GBA R Key", "Keyboard key for GBA R"},
            {"start", "Start Key", "Keyboard key for Start"},   {"select", "Select Key", "Keyboard key for Select"},
            {"up", "Move Up", "Keyboard key for D-pad up"},     {"down", "Move Down", "Keyboard key for D-pad down"},
            {"left", "Move Left", "Keyboard key for D-pad left"}, {"right", "Move Right", "Keyboard key for D-pad right"},
        };
        for (const KeyRow& k : kKeys) {
            const std::string id = std::string("input.key_") + k.name;
            const std::string name = k.name;
            if (a.skip(id.c_str())) continue;
            Setting s;
            s.id = id; s.menu_section = kSecKeys; s.label = k.label; s.tooltip = k.tip; s.kind = SettingKind::Text;
            s.get_text = [in, name](std::string& out) {
                const GbaKeyIndex idx = in->key_index_from_name(name.c_str());
                if (idx == KEY_COUNT) return false;
                out = in->get_key_name(idx);
                return true;
            };
            s.set_text = [in, name](const std::string& value) {
                const GbaKeyIndex idx = in->key_index_from_name(name.c_str());
                if (idx == KEY_COUNT) return false;
                if (!in->set_key_from_name(idx, value.c_str())) return false;
                in->save_keybinds();
                return true;
            };
            reg.add(std::move(s));
        }
        a.action("input.reset_kb", kSecKeys, "Reset Keyboard to Defaults", "Restore default keyboard mapping",
                 [in] { in->reset_keyboard_defaults(); });
        // Resets both gamepad and keyboard; reachable by id only (no menu row).
        a.action("input.reset_controls", nullptr, "Reset Controls", "Restore all default controls",
                 [in] { in->reset_to_defaults(); });

        if (opt.reload_keybinds_before_ini) reg.add_before_load([in] { in->load_keybinds(); });
    }
    if (DialogTurbo* turbo = t.turbo) {
        a.boolean("gameplay.turbo_dialog", kSecGame, "Turbo Dialog & Cutscene Skip",
                  "Hold the turbo key or button to advance text quickly", "Gameplay", "turbo_dialog",
                  [turbo] { return turbo->is_enabled() ? 1 : 0; },
                  [turbo](int v) { turbo->set_enabled(v != 0); });
    }

    // ---- diagnostics ---------------------------------------------------------------------
    if (PerfHud* hud = t.perf_hud) {
        a.choice("perf.mode", kSecDiag, "Display Mode", "FPS and frametime overlay (or press F10)", "Diagnostics",
                 "perf_mode", {"Disabled", "FPS Counter Only", "FPS + Frametime ms", "Full HUD + Frametime Graph"},
                 [hud] { return static_cast<int>(hud->settings().mode); },
                 [hud](int v) { hud->set_mode(static_cast<PerfHudMode>(v)); });
        a.choice("perf.position", kSecDiag, "Screen Position", "Screen corner or drag anywhere with the mouse",
                 "Diagnostics", "perf_position",
                 {"Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right", "Bottom Black Bar (Letterbox)",
                  "Custom (Drag with Mouse)"},
                 [hud] { return static_cast<int>(hud->settings().position); },
                 [hud](int v) { hud->set_position(static_cast<PerfHudPosition>(v)); });
        a.choice("perf.theme", kSecDiag, "Visual Theme", "Color palette and background opacity", "Diagnostics",
                 "perf_theme", {"Glassmorphism Dark", "Neon Cyberpunk", "Solid Dark", "Minimal (Transparent)"},
                 [hud] { return static_cast<int>(hud->settings().theme); },
                 [hud](int v) { hud->set_theme(static_cast<PerfHudTheme>(v)); });
        // Stored position of the dragged HUD: persisted, no menu row.
        a.integer("perf.custom_x", nullptr, "HUD X", "", "Diagnostics", "hud_custom_x", INT_MIN, INT_MAX, 1,
                  [hud] { return static_cast<int>(hud->settings().custom_x); },
                  [hud](int v) { hud->settings().custom_x = static_cast<float>(v); });
        a.integer("perf.custom_y", nullptr, "HUD Y", "", "Diagnostics", "hud_custom_y", INT_MIN, INT_MAX, 1,
                  [hud] { return static_cast<int>(hud->settings().custom_y); },
                  [hud](int v) { hud->settings().custom_y = static_cast<float>(v); });
    }

    reg.add_ignored_ini_key("hud_anchoring");   // retired: HUD anchoring follows the view mode
}

} // namespace gbaimp
