#include "test_harness.h"

#include "gbaimp/audio_dsp.h"
#include "gbaimp/dialog_turbo.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/input_enhancements.h"
#include "gbaimp/menu_style.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/pixel_art_upscaler.h"
#include "gbaimp/screen_filters.h"

#include <set>
#include <string>
#include <vector>

using namespace gbaimp;

namespace {

struct Modules {
    AudioDsp dsp;
    HdAudioPlayer hd;
    PerfHud hud;
    FrameInterpolator interp;
    ScreenFilters filters;
    PixelArtUpscaler up;
    InputEnhancements input;
    DialogTurbo turbo;
    explicit Modules(const std::string& keybinds) : input(keybinds) {
        input.set_autosave_keybinds(false);
        input.set_dialog_turbo(&turbo);
    }
    SettingsTargets targets() {
        SettingsTargets t;
        t.dsp = &dsp; t.hd_audio = &hd; t.perf_hud = &hud; t.interpolator = &interp;
        t.filters = &filters; t.upscaler = &up; t.input = &input; t.turbo = &turbo;
        return t;
    }
};

std::vector<std::string> sections_in_menu_order(const SettingsRegistry& reg) {
    std::vector<std::string> out;
    for (const Setting& s : reg.settings())
        if (!s.menu_section.empty() && (out.empty() || out.back() != s.menu_section)) out.push_back(s.menu_section);
    return out;
}

} // namespace

int main() {
    testh::TempDir tmp("menu_style");
    tmp.enter();

    // ---- every row of the shared table exists in the standard panel and stays consistent ----------
    {
        Modules m(tmp.file("kb.ini"));
        SettingsRegistry reg;
        reg.set_autosave(false);
        register_standard_settings(reg, m.targets());
        for (const MenuTextRow& r : reference_menu_text()) CHECK(reg.find(r.id) != nullptr);

        const int changed = apply_menu_text(reg, reference_menu_text());
        CHECK_EQ(changed, static_cast<int>(reference_menu_text().size()));
        for (const Setting& s : reg.settings()) {
            if (s.kind == SettingKind::Choice && !s.choices.empty()) CHECK_EQ(s.max, static_cast<int>(s.choices.size()) - 1);
            CHECK(s.min <= s.max);
        }
        CHECK_EQ(reg.find("video.fps_target")->label, std::string("Target Framerate"));
        CHECK_EQ(reg.find("video.xbrz_scale")->choices.size(), static_cast<size_t>(5));
        CHECK_EQ(reg.find("audio.hd_enabled")->label, std::string("HD Audio Pipeline"));
        CHECK_EQ(reg.find("audio.bgm_volume")->max, 150);
        CHECK_EQ(reg.find("audio.bgm_volume")->step, 5);
        CHECK_EQ(reg.find("input.analog_mode")->label, std::string("Stick Movement Mode"));
        // Rows the shared table does not own keep the library wording.
        CHECK_EQ(reg.find("input.pad_attack")->label, std::string("GBA A Button"));
        // Unknown ids and empty fields are harmless.
        std::vector<MenuTextRow> odd(2);
        odd[0].id = "no.such.row"; odd[0].label = "x";
        odd[1].id = "video.fps_target";                       // empty label and tooltip keep the text
        CHECK_EQ(apply_menu_text(reg, odd), 1);
        CHECK_EQ(reg.find("video.fps_target")->label, std::string("Target Framerate"));
        // Values stay clamped to the new range.
        CHECK(reg.set("audio.bgm_volume", 140));
        int v = 0; CHECK(reg.get("audio.bgm_volume", v)); CHECK_EQ(v, 140);
    }

    // ---- section rename and menu order ------------------------------------------------------------
    {
        Modules m(tmp.file("kb2.ini"));
        SettingsRegistry reg;
        reg.set_autosave(false);
        register_standard_settings(reg, m.targets());
        Setting extra;
        extra.id = "dialogue.test_row"; extra.menu_section = "Dialogue & Story"; extra.label = "Row";
        extra.kind = SettingKind::Bool; extra.ini_section = "Dialogue"; extra.ini_key = "test_row";
        extra.get = [] { return 0; }; extra.set = [](int) {};
        reg.add(extra);
        apply_reference_menu_style(reg);
        for (const Setting& s : reg.settings()) CHECK(s.menu_section != "Display and Graphics");
        CHECK(reg.find("video.fps_target")->menu_section == "Graphics");

        const std::vector<std::string> order = insert_section(reference_menu_section_order(), "Dialogue & Story", "Audio");
        order_menu_sections(reg, order);
        const std::vector<std::string> got = sections_in_menu_order(reg);
        const std::vector<std::string> want = {"Graphics", "Controller (Gamepad)", "Keyboard Controls", "Gameplay & Assist",
                                               "Dialogue & Story", "Audio", "Diagnostics & HUD"};
        CHECK(got == want);
        // The dragged HUD position has no menu row and follows Diagnostics: it is after perf.theme.
        size_t theme = 0, cx = 0, i = 0;
        for (const Setting& s : reg.settings()) { if (s.id == "perf.theme") theme = i; if (s.id == "perf.custom_x") cx = i; ++i; }
        CHECK(cx > theme);
        // Reordering keeps every setting, its callbacks and the order inside a section.
        CHECK(reg.set("video.mask_intensity", 40));
        int v = 0; CHECK(reg.get("video.mask_intensity", v)); CHECK_EQ(v, 40);
        std::set<std::string> ids;
        for (const Setting& s : reg.settings()) ids.insert(s.id);
        CHECK_EQ(ids.size(), reg.settings().size());
        // A second pass is a no-op.
        std::vector<std::string> before;
        for (const Setting& s : reg.settings()) before.push_back(s.id);
        order_menu_sections(reg, order);
        std::vector<std::string> after;
        for (const Setting& s : reg.settings()) after.push_back(s.id);
        CHECK(before == after);
    }

    // ---- .ini section order helpers ---------------------------------------------------------------
    {
        const std::vector<std::string> o = insert_section(reference_ini_section_order(), "Dialogue", "Controls");
        const std::vector<std::string> want = {"Video", "Audio", "Dialogue", "Controls", "Gameplay", "Diagnostics"};
        CHECK(o == want);
        CHECK_EQ(insert_section({"A"}, "B", "missing").back(), std::string("B"));
    }

    return testh::finish("menu_style");
}
