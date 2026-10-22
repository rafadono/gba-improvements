#include "gbaimp/menu_style.h"

#include <algorithm>
#include <utility>

namespace gbaimp {

namespace {

MenuTextRow row(const char* id, const char* label, const char* tip) {
    MenuTextRow r;
    r.id = id; r.label = label; r.tooltip = tip;
    return r;
}

MenuTextRow choice_row(const char* id, const char* label, const char* tip, std::vector<std::string> choices) {
    MenuTextRow r = row(id, label, tip);
    r.choices = std::move(choices);
    return r;
}

MenuTextRow int_row(const char* id, const char* label, const char* tip, int mn, int mx, int step) {
    MenuTextRow r = row(id, label, tip);
    r.has_range = true; r.min = mn; r.max = mx; r.step = step;
    return r;
}

std::vector<MenuTextRow> build_reference_text() {
    std::vector<MenuTextRow> t;
    t.push_back(choice_row("video.fps_target", "Target Framerate", "Target refresh rate without altering speed",
        {"60 FPS (Original GBA)", "120 FPS (2x High Rate)", "144 FPS", "Display Native (Auto)", "Uncapped"}));
    t.push_back(choice_row("video.motion_smoothing", "Motion Smoothing", "Temporal blending between simulation frames",
        {"Off (Direct Presentation)", "Smooth Blend (50% Lerp)", "Motion-Adaptive"}));
    t.push_back(choice_row("video.xbrz_scale", "Pixel-Art Upscaler",
        "Edge-aware pixel-art upscaler, Scale2x/EPX style (2x to 5x)",
        {"Off (Original Pixels)", "2x Pixel-Art Edges (Enhanced Edges)", "3x Pixel-Art Edges (Crisp Vectors)",
         "4x Pixel-Art Edges (Ultra Detail)", "5x Pixel-Art Edges (Retina Smoothing)"}));
    t.push_back(choice_row("video.color_profile", "Color Profile / Gamma", "Hardware color grading and gamma curve",
        {"Raw (Uncorrected)", "AGB-001 (Reflective TFT)", "AGS-001 (SP Frontlit)", "AGS-101 (SP Backlit)",
         "MiSTer Gamma 1.6", "MiSTer Gamma 2.2"}));
    t.push_back(choice_row("video.screen_mask", "Screen Mask Type", "LCD grid, RGB subpixel stripes, scanlines",
        {"Off (Clean Pixels)", "MiSTer LCD Grid (TFT Matrix)", "MiSTer Subpixel RGB (Vertical)", "MiSTer Subpixel BGR",
         "MiSTer Diffusion (AGS-001)", "Game Boy Player Scanlines", "CRT Trinitron Aperture Grille"}));
    t.push_back(int_row("video.mask_intensity", "Mask Intensity (%)", "Grid darkness: 10% (subtle) to 100% (dense)",
        10, 100, 5));

    t.push_back(choice_row("input.analog_mode", "Stick Movement Mode", "360 walk/run or 8-way digital",
        {"Digital (Original 8-Way)", "Analog 8-Way (Smooth Stick)", "True 360 Walk & Run"}));
    t.push_back(int_row("input.deadzone", "Stick Deadzone (%)", "Stick drift margin (5% to 50%)", 5, 50, 5));
    t.push_back(choice_row("input.kb_preset", "Keyboard Preset", "Keyboard layout preset",
        {"Default (X, Z, C, V, Return, RShift)", "WASD + J/K/U/I (Modern PC)", "Classic Emulation (Z, X, A, S)",
         "Arrows + Z/X/Q/W", "Custom"}));

    t.push_back(row("gameplay.turbo_dialog", "Turbo Dialog & Cutscene Skip", "Hold Tab or Y to advance text at 60Hz"));

    t.push_back(row("audio.hd_enabled", "HD Audio Pipeline", "High-definition digital audio synthesis and playback"));
    t.push_back(int_row("audio.bgm_volume", "Music (BGM) Volume (%)", "Independent music volume (0% to 150%)", 0, 150, 5));
    t.push_back(int_row("audio.sfx_volume", "Effects (SFX) Volume (%)", "Independent sound effects volume (0-150%)", 0, 150, 5));
    t.push_back(choice_row("audio.eq_preset", "Equalizer Profile", "Real-time DSP sound equalizer profile",
        {"Flat (Authentic)", "Warm Retro", "Crisp Modern", "Bass Boost"}));
    t.push_back(int_row("audio.stereo_width", "Stereo Width (%)", "Stereo width: 0% mono to 200% expanded", 0, 200, 10));
    t.push_back(row("audio.anti_aliasing", "DAC Anti-Aliasing", "Biquad filter to cut GBA audio hiss"));
    t.push_back(row("audio.limiter", "Dynamic Peak Limiter", "Dynamic limiter preventing audio clipping"));

    t.push_back(choice_row("perf.mode", "Display Mode", "FPS and frametime overlay (or press F10)",
        {"Disabled", "FPS Counter Only", "FPS + Frametime ms", "Full HUD + Frametime Graph"}));
    t.push_back(choice_row("perf.position", "Screen Position", "Screen corner or drag anywhere with mouse",
        {"Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right", "Bottom Black Bar (Letterbox)",
         "Custom (Drag with Mouse)"}));
    t.push_back(choice_row("perf.theme", "Visual Theme", "Color palette and background opacity",
        {"Glassmorphism Dark", "Neon Cyberpunk", "Solid Dark", "Minimal (Transparent)"}));
    return t;
}

int section_rank(const std::vector<std::string>& order, const std::string& section) {
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == section) return static_cast<int>(i);
    return static_cast<int>(order.size());
}

} // namespace

int apply_menu_text(SettingsRegistry& reg, const std::vector<MenuTextRow>& rows) {
    int changed = 0;
    for (const MenuTextRow& r : rows) {
        Setting* s = reg.find(r.id);
        if (!s) continue;
        if (!r.label.empty()) s->label = r.label;
        if (!r.tooltip.empty()) s->tooltip = r.tooltip;
        if (!r.choices.empty() && s->kind == SettingKind::Choice) {
            s->choices = r.choices;
            s->min = 0;
            s->max = static_cast<int>(r.choices.size()) - 1;
            s->step = 1;
        } else if (r.has_range && (s->kind == SettingKind::Int || s->kind == SettingKind::Choice)) {
            s->min = r.min; s->max = r.max; s->step = r.step;
        }
        ++changed;
    }
    return changed;
}

const std::vector<MenuTextRow>& reference_menu_text() {
    static const std::vector<MenuTextRow> t = build_reference_text();
    return t;
}

const std::vector<std::string>& reference_menu_section_order() {
    static const std::vector<std::string> o = {"Graphics", "Controller (Gamepad)", "Keyboard Controls",
                                               "Gameplay & Assist", "Audio", "Diagnostics & HUD"};
    return o;
}

const std::vector<std::string>& reference_ini_section_order() {
    static const std::vector<std::string> o = {"Video", "Audio", "Controls", "Gameplay", "Diagnostics"};
    return o;
}

std::vector<std::string> insert_section(std::vector<std::string> order, const std::string& section,
                                            const std::string& before) {
    auto it = std::find(order.begin(), order.end(), before);
    order.insert(it, section);
    return order;
}

void apply_reference_menu_style(SettingsRegistry& reg) {
    reg.rename_menu_section("Display and Graphics", "Graphics");
    apply_menu_text(reg, reference_menu_text());
}

void order_menu_sections(SettingsRegistry& reg, const std::vector<std::string>& order) {
    std::vector<Setting> all = reg.settings();
    const int last = static_cast<int>(order.size());
    // Rank of a setting: its menu section, or that of the last menu row sharing its .ini section.
    auto rank = [&](const Setting& s) {
        if (!s.menu_section.empty()) return section_rank(order, s.menu_section);
        int r = last + 1;
        for (const Setting& o : all)
            if (!o.menu_section.empty() && o.ini_section == s.ini_section && !s.ini_section.empty()) r = section_rank(order, o.menu_section);
        return r;
    };
    std::vector<std::pair<int, size_t>> keys;
    for (size_t i = 0; i < all.size(); ++i) keys.emplace_back(rank(all[i]), i);
    std::stable_sort(keys.begin(), keys.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> ids;
    for (const Setting& s : all) ids.push_back(s.id);
    for (const std::string& id : ids) reg.remove(id);
    for (const auto& k : keys) reg.add(std::move(all[k.second]));
}

} // namespace gbaimp
