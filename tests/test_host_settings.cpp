#include "test_harness.h"

#include "gbaimp/audio_dsp.h"
#include "gbaimp/dialog_turbo.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/host_settings.h"
#include "gbaimp/input_enhancements.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/pixel_art_upscaler.h"
#include "gbaimp/screen_filters.h"

// The adapter is compiled against a stand-in for recomp-ui's header (tests/stubs).
#define GBARECOMP_RUNTIME_UI 1
#include "gbaimp/runtime_ui_settings.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace gbaimp;

// ---- stand-ins for the three recomp-ui entry points the adapter calls ------------------------
struct RecompRuntimeUi { bool open = false; };
extern "C" int recomp_runtime_ui_is_open(const RecompRuntimeUi* ui) { return ui->open ? 1 : 0; }
extern "C" void recomp_runtime_ui_open(RecompRuntimeUi* ui) { ui->open = true; }
extern "C" void recomp_runtime_ui_close(RecompRuntimeUi* ui) { ui->open = false; }

namespace {

// Independent module instances (no shared singleton state, no stray files in the cwd).
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

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void spit(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
}

int get(const SettingsRegistry& r, const char* id) {
    int v = -12345;
    if (!r.get(id, v)) return -12345;
    return v;
}

// What the hosts' save_*_config() wrote (host template), values chosen non-default.
const char* kLegacyHostIni =
    "[Video]\n"
    "fps_target=2\n"
    "motion_smoothing=2\n"
    "xbrz_scale=3\n"
    "color_profile=3\n"
    "screen_mask=5\n"
    "mask_intensity=65\n"
    "\n"
    "[Audio]\n"
    "sample_rate=4\n"
    "hd_enabled=0\n"
    "master_volume=70\n"
    "bgm_volume=55\n"
    "sfx_volume=45\n"
    "eq_preset=3\n"
    "stereo_width=150\n"
    "anti_aliasing=0\n"
    "limiter=0\n"
    "\n"
    "[Controls]\n"
    "analog_mode=1\n"
    "analog_deadzone=30\n"
    "gamepad_profile=2\n"
    "kb_preset=1\n"
    "\n"
    "[Gameplay]\n"
    "turbo_dialog=0\n"
    "\n"
    "[Diagnostics]\n"
    "perf_mode=2\n"
    "perf_position=3\n"
    "perf_theme=1\n";

} // namespace

int main() {
    SDL_SetMainReady();
    testh::TempDir tmp("host_settings");
    tmp.enter();

    // ---- every standard id, get/set against the real module -----------------------------------
    {
        Modules m(tmp.file("kb1.ini"));
        SettingsRegistry reg;
        reg.set_autosave(false);
        register_standard_settings(reg, m.targets());

        CHECK(reg.set("video.fps_target", 3));          CHECK_EQ(static_cast<int>(m.interp.settings().mode), 3);
        CHECK_EQ(get(reg, "video.fps_target"), 3);
        CHECK(reg.set("video.motion_smoothing", 2));    CHECK_EQ(static_cast<int>(m.interp.settings().smoothing), 2);
        CHECK_EQ(get(reg, "video.motion_smoothing"), 2);
        CHECK(reg.set("video.xbrz_scale", 0));          CHECK(m.up.scale() == UpscaleScale::None);
        CHECK_EQ(get(reg, "video.xbrz_scale"), 0);
        CHECK(reg.set("video.xbrz_scale", 3));          CHECK(m.up.scale() == UpscaleScale::X4);
        CHECK_EQ(get(reg, "video.xbrz_scale"), 3);
        CHECK(reg.set("video.color_profile", 4));       CHECK_EQ(static_cast<int>(m.filters.settings().color_profile), 4);
        CHECK_EQ(get(reg, "video.color_profile"), 4);
        CHECK(reg.set("video.screen_mask", 6));         CHECK_EQ(static_cast<int>(m.filters.settings().mask_type), 6);
        CHECK_EQ(get(reg, "video.screen_mask"), 6);
        CHECK(reg.set("video.mask_intensity", 55));     CHECK_NEAR(m.filters.settings().mask_intensity, 0.55f, 0.001f);
        CHECK_EQ(get(reg, "video.mask_intensity"), 55);

        CHECK(reg.set("audio.sample_rate", 0));         CHECK_EQ(m.hd.settings().target_sample_rate.load(), 32768);
        CHECK_EQ(get(reg, "audio.sample_rate"), 0);
        CHECK(reg.set("audio.sample_rate", 1));         CHECK_EQ(m.hd.settings().target_sample_rate.load(), 44100);
        CHECK(reg.set("audio.sample_rate", 2));         CHECK_EQ(m.hd.settings().target_sample_rate.load(), 48000);
        CHECK(reg.set("audio.sample_rate", 3));         CHECK_EQ(m.hd.settings().target_sample_rate.load(), 65536);
        CHECK_EQ(get(reg, "audio.sample_rate"), 3);
        CHECK(reg.set("audio.sample_rate", 4));         CHECK_EQ(m.hd.settings().target_sample_rate.load(), 96000);
        CHECK_EQ(get(reg, "audio.sample_rate"), 4);
        m.hd.settings().target_sample_rate = 12345;     CHECK_EQ(get(reg, "audio.sample_rate"), 3);   // unknown rate -> "native"
        CHECK(reg.set("audio.hd_enabled", 0));          CHECK(!m.hd.settings().enabled.load());
        CHECK_EQ(get(reg, "audio.hd_enabled"), 0);
        CHECK(reg.set("audio.master_volume", 40));      CHECK_NEAR(m.hd.settings().master_volume.load(), 0.40f, 0.001f);
        CHECK_EQ(get(reg, "audio.master_volume"), 40);
        CHECK(reg.set("audio.bgm_volume", 35));         CHECK_NEAR(m.hd.settings().bgm_volume.load(), 0.35f, 0.001f);
        CHECK_EQ(get(reg, "audio.bgm_volume"), 35);
        CHECK(reg.set("audio.sfx_volume", 25));         CHECK_NEAR(m.hd.settings().sfx_volume.load(), 0.25f, 0.001f);
        CHECK_EQ(get(reg, "audio.sfx_volume"), 25);
        CHECK(reg.set("audio.eq_preset", 1));           CHECK(m.dsp.settings().preset == EqPreset::WarmRetro);
        CHECK_EQ(get(reg, "audio.eq_preset"), 1);
        CHECK(reg.set("audio.stereo_width", 80));       CHECK_NEAR(m.dsp.settings().stereo_width.load(), 0.80f, 0.001f);
        CHECK_EQ(get(reg, "audio.stereo_width"), 80);
        CHECK(reg.set("audio.anti_aliasing", 0));       CHECK(!m.dsp.settings().anti_aliasing_filter.load());
        CHECK_EQ(get(reg, "audio.anti_aliasing"), 0);
        CHECK(reg.set("audio.limiter", 0));             CHECK(!m.dsp.settings().limiter_enabled.load());
        CHECK_EQ(get(reg, "audio.limiter"), 0);

        CHECK(reg.set("input.analog_mode", 1));         CHECK(m.input.settings().analog_mode == AnalogMode::Analog8Way);
        CHECK_EQ(get(reg, "input.analog_mode"), 1);
        CHECK(reg.set("input.deadzone", 30));           CHECK_NEAR(m.input.settings().deadzone, 0.30f, 0.001f);
        CHECK_EQ(get(reg, "input.deadzone"), 30);
        CHECK(reg.set("input.gamepad_profile", 4));     CHECK(m.input.settings().gamepad_profile == GamepadProfile::Triggers);
        CHECK_EQ(get(reg, "input.gamepad_profile"), 4);
        CHECK(reg.set("input.pad_attack", 2));          CHECK(m.input.settings().pad_a == PadButtonChoice::ButtonX);
        CHECK_EQ(get(reg, "input.pad_attack"), 2);
        CHECK(reg.set("input.pad_jump", 3));            CHECK(m.input.settings().pad_b == PadButtonChoice::ButtonY);
        CHECK_EQ(get(reg, "input.pad_jump"), 3);
        CHECK(reg.set("input.pad_deck_l", 6));          CHECK(m.input.settings().pad_l == PadButtonChoice::LeftTrigger);
        CHECK_EQ(get(reg, "input.pad_deck_l"), 6);
        CHECK(reg.set("input.pad_deck_r", 7));          CHECK(m.input.settings().pad_r == PadButtonChoice::RightTrigger);
        CHECK_EQ(get(reg, "input.pad_deck_r"), 7);
        CHECK(m.input.settings().gamepad_profile == GamepadProfile::Custom);   // the pad_* setters switch to Custom
        CHECK(reg.set("input.kb_preset", 2));           CHECK(m.input.settings().kb_preset == KeyboardPreset::Emulation);
        CHECK_EQ(get(reg, "input.kb_preset"), 2);
        CHECK(reg.run_action("input.reset_pad"));       CHECK(m.input.settings().gamepad_profile == GamepadProfile::Standard);
        CHECK(reg.run_action("input.reset_kb"));        CHECK(m.input.settings().kb_preset == KeyboardPreset::Default);
        CHECK(reg.set("input.gamepad_profile", 1));
        CHECK(reg.run_action("input.reset_controls"));  CHECK(m.input.settings().gamepad_profile == GamepadProfile::Standard);

        CHECK(reg.set("gameplay.turbo_dialog", 0));     CHECK(!m.turbo.is_enabled());
        CHECK_EQ(get(reg, "gameplay.turbo_dialog"), 0);
        CHECK(reg.set("gameplay.turbo_dialog", 1));     CHECK(m.turbo.is_enabled());

        CHECK(reg.set("perf.mode", 1));                 CHECK(m.hud.settings().mode == PerfHudMode::FpsOnly);
        CHECK_EQ(get(reg, "perf.mode"), 1);
        CHECK(reg.set("perf.position", 5));             CHECK(m.hud.settings().position == PerfHudPosition::FreeDrag);
        CHECK_EQ(get(reg, "perf.position"), 5);
        CHECK(reg.set("perf.theme", 2));                CHECK(m.hud.settings().theme == PerfHudTheme::Solid);
        CHECK_EQ(get(reg, "perf.theme"), 2);
        CHECK(reg.set("perf.custom_x", 123));           CHECK_NEAR(m.hud.settings().custom_x, 123.0f, 0.001f);
        CHECK(reg.set("perf.custom_y", -7));            CHECK_NEAR(m.hud.settings().custom_y, -7.0f, 0.001f);
        CHECK_EQ(get(reg, "perf.custom_x"), 123);
        CHECK_EQ(get(reg, "perf.custom_y"), -7);

        // Text rows: key names are stored by the input module.
        std::string name;
        CHECK(reg.get_text("input.key_a", name));       CHECK(!name.empty());
        CHECK(reg.set_text("input.key_a", "Q"));        CHECK(m.input.get_key_scancode(KEY_A) == SDL_SCANCODE_Q);
        CHECK(reg.get_text("input.key_a", name));       CHECK(name == m.input.get_key_name(KEY_A));
        CHECK(!reg.set_text("input.key_a", "definitely-not-a-key"));
        CHECK(reg.set_text("input.key_left", "J"));     CHECK(m.input.get_key_scancode(KEY_LEFT) == SDL_SCANCODE_J);
        CHECK(!reg.get_text("video.fps_target", name)); // wrong kind

        // Unknown id / wrong kind.
        int dummy = 0;
        CHECK(!reg.get("nope.nothing", dummy));
        CHECK(!reg.set("nope.nothing", 1));
        CHECK(!reg.set("input.reset_pad", 1));          // action, not a value
        CHECK(!reg.run_action("video.fps_target"));
        CHECK(!reg.get("input.key_a", dummy));

        // Values are clamped to the declared range (no out-of-range enum reaches a module).
        CHECK(reg.set("video.fps_target", 99));         CHECK_EQ(get(reg, "video.fps_target"), 4);
        CHECK(reg.set("video.fps_target", -5));         CHECK_EQ(get(reg, "video.fps_target"), 0);
        CHECK(reg.set("audio.limiter", 7));             CHECK_EQ(get(reg, "audio.limiter"), 1);
        CHECK(reg.set("input.deadzone", 1));            CHECK_EQ(get(reg, "input.deadzone"), 5);
    }

    // ---- menu metadata ---------------------------------------------------------------------------
    {
        Modules m(tmp.file("kb2.ini"));
        SettingsRegistry reg;
        register_standard_settings(reg, m.targets());
        const Setting* fps = reg.find("video.fps_target");
        CHECK(fps != nullptr);
        if (fps) {
            CHECK(fps->kind == SettingKind::Choice);
            CHECK_EQ(fps->choices.size(), static_cast<std::size_t>(5));
            CHECK_EQ(fps->max, 4);
            CHECK(fps->menu_section == "Display and Graphics");
            CHECK(fps->ini_section == "Video");
            CHECK(fps->ini_key == "fps_target");
        }
        const Setting* gp = reg.find("input.gamepad_profile");
        CHECK(gp && gp->choices.size() == 6);
        const Setting* kb = reg.find("input.kb_preset");
        CHECK(kb && kb->choices.size() == 5);
        const Setting* dz = reg.find("input.deadzone");
        CHECK(dz && dz->min == 5 && dz->max == 50 && dz->step == 5 && dz->ini_key == "analog_deadzone");
        const Setting* cx = reg.find("perf.custom_x");
        CHECK(cx && !cx->in_menu);
        const Setting* rc = reg.find("input.reset_controls");
        CHECK(rc && !rc->in_menu);
        // Every menu choice item is consistent.
        for (const Setting& s : reg.settings()) {
            if (s.kind == SettingKind::Choice) CHECK_EQ(s.max, static_cast<int>(s.choices.size()) - 1);
        }
        // A host retitles a section and a row.
        reg.rename_menu_section("Display and Graphics", "Graphics");
        CHECK(reg.find("video.screen_mask")->menu_section == "Graphics");
        reg.find("audio.bgm_volume")->max = 150;
        CHECK(reg.set("audio.bgm_volume", 140));
        CHECK_NEAR(m.hd.settings().bgm_volume.load(), 1.40f, 0.001f);
    }

    // ---- exclude / missing targets ----------------------------------------------------------------
    {
        Modules m(tmp.file("kb3.ini"));
        SettingsRegistry reg;
        StandardSettingsOptions o;
        o.exclude = {"audio.master_volume", "audio.sample_rate"};
        register_standard_settings(reg, m.targets(), o);
        CHECK(reg.find("audio.master_volume") == nullptr);
        CHECK(reg.find("audio.sample_rate") == nullptr);
        CHECK(reg.find("audio.bgm_volume") != nullptr);

        SettingsRegistry reg2;
        SettingsTargets t;
        t.dsp = &m.dsp;
        register_standard_settings(reg2, t);
        CHECK(reg2.find("audio.eq_preset") != nullptr);
        CHECK(reg2.find("video.fps_target") == nullptr);
        CHECK(reg2.find("input.deadzone") == nullptr);
    }

    // ---- .ini: exact format, round trip --------------------------------------------------------------
    const std::string ini = tmp.file("settings.ini");
    {
        Modules m(tmp.file("kb4.ini"));
        SettingsRegistry reg;
        reg.set_ini_path(ini);
        reg.set_section_order({"Video", "Audio", "Controls", "Gameplay", "Diagnostics"});
        StandardSettingsOptions o;
        o.exclude = {"input.pad_attack", "input.pad_jump", "input.pad_deck_l", "input.pad_deck_r",
                     "perf.custom_x", "perf.custom_y"};
        register_standard_settings(reg, m.targets(), o);
        reg.load_ini_text(kLegacyHostIni);

        // The exact text a host writes for those values: same sections, order, "key=value".
        CHECK(reg.ini_text() == kLegacyHostIni);

        // File round trip.
        CHECK(reg.save_ini());
        CHECK(slurp(ini) == kLegacyHostIni);
        Modules m2(tmp.file("kb4b.ini"));
        SettingsRegistry reg2;
        reg2.set_ini_path(ini);
        reg2.set_section_order({"Video", "Audio", "Controls", "Gameplay", "Diagnostics"});
        register_standard_settings(reg2, m2.targets(), o);
        const IniLoadStats st = reg2.load_ini();
        CHECK(st.file_found);
        CHECK_EQ(st.applied, 23);
        CHECK_EQ(st.unknown, 0);
        CHECK(reg2.ini_text() == kLegacyHostIni);
        // Module values actually arrived.
        CHECK(m2.interp.settings().mode == FramerateMode::Fps144);
        CHECK(m2.up.scale() == UpscaleScale::X4);
        CHECK_NEAR(m2.filters.settings().mask_intensity, 0.65f, 0.001f);
        CHECK_EQ(m2.hd.settings().target_sample_rate.load(), 96000);
        CHECK(!m2.hd.settings().enabled.load());
        CHECK(m2.dsp.settings().preset == EqPreset::BassBoost);
        CHECK_NEAR(m2.dsp.settings().stereo_width.load(), 1.5f, 0.001f);
        CHECK(m2.input.settings().gamepad_profile == GamepadProfile::PlayStation);
        CHECK(m2.input.settings().kb_preset == KeyboardPreset::Wasd);
        CHECK(!m2.turbo.is_enabled());
        CHECK(m2.hud.settings().position == PerfHudPosition::BottomRight);
    }

    // ---- .ini: keys the standard panel also persists (pad mapping, HUD position) ----------------------
    {
        Modules m(tmp.file("kb5.ini"));
        SettingsRegistry reg;
        register_standard_settings(reg, m.targets());
        reg.set_autosave(false);
        reg.set("input.pad_attack", 2);
        reg.set("input.pad_jump", 3);
        reg.set("perf.custom_x", 55);
        const std::string text = reg.ini_text();
        CHECK(text.find("pad_attack=2\n") != std::string::npos);
        CHECK(text.find("pad_jump=3\n") != std::string::npos);
        CHECK(text.find("hud_custom_x=55\n") != std::string::npos);
        CHECK(text.find("analog_deadzone=") != std::string::npos);
        CHECK(text.find("input.") == std::string::npos);   // ids never leak into the file
    }

    // ---- .ini: tolerance (legacy / unknown keys, junk lines, CRLF) ------------------------------------
    {
        Modules m(tmp.file("kb6.ini"));
        SettingsRegistry reg;
        reg.set_autosave(false);
        register_standard_settings(reg, m.targets());
        const std::string junk =
            "; comment\r\n"
            "# another\r\n"
            "[Whatever]\r\n"
            "hud_anchoring=1\r\n"            // retired, ignored by the panel
            "some_future_key=4\r\n"          // unknown
            "no equals sign here\r\n"
            "perf_mode=abc\r\n"              // not an integer
            "fps_target =2\r\n"              // key is right-trimmed
            "limiter=0\r\n"                  // CRLF value
            "mask_intensity=999\r\n"         // clamped to the range
            "=5\r\n"
            "\r\n";
        const IniLoadStats st = reg.load_ini_text(junk);
        CHECK_EQ(st.applied, 3);
        CHECK_EQ(st.ignored, 1);
        CHECK_EQ(st.unknown, 3);
        CHECK(m.interp.settings().mode == FramerateMode::Fps144);
        CHECK(!m.dsp.settings().limiter_enabled.load());
        CHECK_EQ(get(reg, "video.mask_intensity"), 100);
        CHECK(m.hud.settings().mode == PerfHudMode::FullWithGraph);   // untouched default
        CHECK(!reg.loading());
    }

    // ---- host extension points ------------------------------------------------------------------------
    {
        Modules m(tmp.file("kb7.ini"));
        const std::string path = tmp.file("host.ini");
        std::remove(path.c_str());

        int backlog = 1, text_size = 100, opens = 0, saves_seen = 0;
        std::string font = "Serif";
        std::string extra_font_seen;

        SettingsRegistry reg;
        reg.set_ini_path(path);
        reg.set_ini_header("Host settings\nedit with the game closed");
        reg.set_section_order({"Video", "Dialogue", "HdText"});
        register_standard_settings(reg, m.targets());

        Setting b;
        b.id = "dialogue.backlog_enable"; b.menu_section = "Dialogue & Story"; b.label = "Backlog"; b.kind = SettingKind::Bool;
        b.ini_section = "Dialogue"; b.ini_key = "backlog_enable";
        b.get = [&] { return backlog; }; b.set = [&](int v) { backlog = v; };
        reg.add(b);
        Setting sz;
        sz.id = "hdtext.size"; sz.menu_section = "Dialogue Text (HD)"; sz.label = "Font Size"; sz.kind = SettingKind::Int;
        sz.min = 30; sz.max = 200; sz.step = 5; sz.ini_section = "HdText"; sz.ini_key = "hdtext_size";
        sz.get = [&] { return text_size; }; sz.set = [&](int v) { text_size = v; };
        reg.add(sz);
        Setting op;
        op.id = "dialogue.open_backlog"; op.menu_section = "Dialogue & Story"; op.label = "Open"; op.kind = SettingKind::Action;
        op.action = [&] { ++opens; }; op.save_after_action = false;
        reg.add(op);
        reg.add_ini_extension({"HdText",
                               [&](std::ostream& o) { o << "hdtext_font=" << font << "\n"; },
                               [&](const std::string& k, const std::string& v) {
                                   if (k != "hdtext_font") return false;
                                   std::string t = v;
                                   while (!t.empty() && (t.back() == '\r' || t.back() == ' ')) t.pop_back();
                                   extra_font_seen = t;
                                   font = t;
                                   return true;
                               }});
        for (const char* k : {"text_mode", "font_family", "font_size"}) reg.add_ignored_ini_key(k);
        for (const char* k : {"dialogue.text_mode", "dialogue.font_size"}) reg.add_ignored_id(k);
        reg.set_change_listener([&](const Setting&) { ++saves_seen; });

        CHECK(reg.set("dialogue.text_mode", 3));               // legacy menu id: accepted and dropped
        int dummy = 0;
        CHECK(!reg.get("dialogue.text_mode", dummy));

        // No file: defaults are written (the historical behaviour), with the header.
        IniLoadStats st = reg.load_ini();
        CHECK(!st.file_found);
        std::string text = slurp(path);
        CHECK(text.rfind("# Host settings\n# edit with the game closed\n\n[Video]\n", 0) == 0);
        const auto d = text.find("[Dialogue]\nbacklog_enable=1\n\n[HdText]\nhdtext_font=Serif\nhdtext_size=100\n");
        CHECK(d != std::string::npos);
        CHECK(text.find("[Audio]") != std::string::npos);      // sections outside the order go last, by appearance
        CHECK(text.find("[Video]") < text.find("[Dialogue]"));
        CHECK(text.find("[Dialogue]") < text.find("[HdText]"));
        CHECK(text.find("[HdText]") < text.find("[Audio]"));

        // set() through the registry saves; actions with save_after_action=false do not.
        std::remove(path.c_str());
        CHECK(reg.set("hdtext.size", 500));                    // clamped to 200
        CHECK_EQ(text_size, 200);
        CHECK(slurp(path).find("hdtext_size=200\n") != std::string::npos);
        std::remove(path.c_str());
        CHECK(reg.run_action("dialogue.open_backlog"));
        CHECK_EQ(opens, 1);
        CHECK(slurp(path).empty());
        CHECK(reg.run_action("input.reset_pad"));              // standard resets do save
        CHECK(!slurp(path).empty());

        // Load: legacy host keys + free form font + harness overrides applied after the file.
        spit(path,
             "[HdText]\nhdtext_font=Sans Serif\nhdtext_size=80\ntext_mode=2\nfont_size=3\nbacklog_enable=0\n");
        saves_seen = 0;
        st = reg.load_ini("hdtext_size=120\nperf_mode=3\n");
        CHECK(st.file_found);
        CHECK_EQ(st.ignored, 2);
        CHECK_EQ(st.unknown, 0);
        CHECK(extra_font_seen == "Sans Serif");
        CHECK_EQ(backlog, 0);
        CHECK_EQ(text_size, 120);                              // the override won
        CHECK(m.hud.settings().mode == PerfHudMode::FullWithGraph);
        CHECK(saves_seen >= 3);                                // listener sees loads too
        // ... and loading never rewrote the file.
        CHECK(slurp(path).find("hdtext_size=80\n") != std::string::npos);

        // Replace an item in place (keeps its position) and drop one.
        const std::size_t before = reg.settings().size();
        Setting b2 = b; b2.label = "Conversation Log";
        reg.add(b2);
        CHECK_EQ(reg.settings().size(), before);
        CHECK(reg.find("dialogue.backlog_enable")->label == "Conversation Log");
        CHECK(reg.remove("dialogue.open_backlog"));
        CHECK(!reg.remove("dialogue.open_backlog"));

        // A block predicate stops every write (capture / headless runs).
        bool blocked = true;
        reg.set_save_blocked([&] { return blocked; });
        std::remove(path.c_str());
        CHECK(!reg.save_ini());
        reg.set("video.fps_target", 1);
        CHECK(slurp(path).empty());
        blocked = false;
        reg.set("video.fps_target", 2);
        CHECK(!slurp(path).empty());

        // Enabled predicate.
        reg.find("video.screen_mask")->enabled = [] { return false; };
        CHECK(!reg.is_enabled("video.screen_mask"));
        CHECK(reg.is_enabled("video.fps_target"));
        CHECK(reg.is_enabled("unknown.id"));
    }

    // ---- no file + write_defaults off; before-load hook; no path ------------------------------------------
    {
        Modules m(tmp.file("kb8.ini"));
        SettingsRegistry reg;
        const std::string path = tmp.file("absent.ini");
        std::remove(path.c_str());
        reg.set_ini_path(path);
        reg.set_write_defaults_if_missing(false);
        register_standard_settings(reg, m.targets());
        int hook = 0;
        reg.add_before_load([&] { ++hook; });
        const IniLoadStats st = reg.load_ini();
        CHECK(!st.file_found);
        CHECK_EQ(hook, 1);
        CHECK(slurp(path).empty());

        SettingsRegistry nopath;
        register_standard_settings(nopath, m.targets());
        CHECK(!nopath.save_ini());
        nopath.set("video.fps_target", 1);                     // autosave with no path: silently nothing
    }

    // ---- the keybinds file is reloaded before the .ini (as the hosts do) --------------------------------
    {
        const std::string kb = tmp.file("kb9.ini");
        {
            Modules writer(kb);
            writer.input.set_key_scancode(KEY_A, SDL_SCANCODE_Q);
            writer.input.save_keybinds();
        }
        Modules m(tmp.file("kb9-other.ini"));
        m.input.set_keybinds_path(kb);
        SettingsRegistry reg;
        register_standard_settings(reg, m.targets());
        reg.set_autosave(false);
        CHECK(m.input.get_key_scancode(KEY_A) != SDL_SCANCODE_Q);
        reg.load_ini_text("");                                 // text loads do not run before-load hooks
        CHECK(m.input.get_key_scancode(KEY_A) != SDL_SCANCODE_Q);
        reg.load_ini("fps_target=1\n");
        CHECK(m.input.get_key_scancode(KEY_A) == SDL_SCANCODE_Q);
    }

    // ---- recomp-ui adapter (against the stand-in header) ----------------------------------------------------
    {
        Modules m(tmp.file("kb10.ini"));
        SettingsRegistry reg;
        reg.set_autosave(false);
        register_standard_settings(reg, m.targets());
        Setting hidden;
        hidden.id = "host.hidden"; hidden.kind = SettingKind::Bool; hidden.get = [] { return 1; }; hidden.set = [](int) {};
        reg.add(hidden);                                       // no menu section: not an item

        RuntimeUiBinding ui(reg);
        ui.rebuild();
        std::size_t menu_count = 0;
        for (const Setting& s : reg.settings()) if (s.in_menu && !s.menu_section.empty()) ++menu_count;
        CHECK_EQ(ui.item_count(), menu_count);
        bool saw_choice = false, saw_text = false, saw_action = false, saw_hidden = false;
        for (std::size_t i = 0; i < ui.item_count(); ++i) {
            const RecompRuntimeUiItem& it = ui.items()[i];
            if (std::string(it.key) == "host.hidden") saw_hidden = true;
            if (std::string(it.key) == "video.screen_mask") {
                saw_choice = it.type == RECOMP_RUNTIME_UI_CHOICE && it.choice_count == 7 && it.maximum == 6 &&
                             std::string(it.choices[0]) == "Off (Clean Pixels)" &&
                             std::string(it.choices[6]) == "CRT Trinitron" &&
                             std::string(it.section) == "Display and Graphics";
            }
            if (std::string(it.key) == "input.key_start") saw_text = it.type == RECOMP_RUNTIME_UI_TEXT;
            if (std::string(it.key) == "input.reset_kb") saw_action = it.type == RECOMP_RUNTIME_UI_ACTION;
            CHECK(it.type != RECOMP_RUNTIME_UI_CHOICE || it.choices != nullptr);
        }
        CHECK(saw_choice); CHECK(saw_text); CHECK(saw_action); CHECK(!saw_hidden);

        struct FakeRunOptions {
            const void* ui_extra_items = nullptr;
            std::size_t ui_extra_item_count = 0;
            int (*ui_get)(const char*, int*) = nullptr;
            int (*ui_set)(const char*, int) = nullptr;
            int (*ui_action)(const char*) = nullptr;
            int (*ui_enabled)(const char*) = nullptr;
            int (*ui_get_text)(const char*, char*, std::size_t) = nullptr;
            int (*ui_set_text)(const char*, const char*) = nullptr;
        } opts;
        ui.apply(opts);
        CHECK(opts.ui_extra_items == ui.items());
        CHECK_EQ(opts.ui_extra_item_count, ui.item_count());
        int v = 0;
        CHECK(opts.ui_set("audio.eq_preset", 3));
        CHECK(opts.ui_get("audio.eq_preset", &v)); CHECK_EQ(v, 3);
        CHECK(!opts.ui_set("not.a.key", 1));
        CHECK(!opts.ui_get("not.a.key", &v));
        CHECK(opts.ui_action("input.reset_kb"));
        CHECK(!opts.ui_action("video.fps_target"));
        CHECK(opts.ui_enabled("video.fps_target"));
        CHECK(!opts.ui_enabled("system.resume"));
        CHECK(opts.ui_enabled(nullptr));
        char buf[32];
        CHECK(opts.ui_set_text("input.key_b", "W"));
        CHECK(opts.ui_get_text("input.key_b", buf, sizeof(buf)));
        CHECK(std::string(buf) == m.input.get_key_name(KEY_B));
        CHECK(!opts.ui_get_text("video.fps_target", buf, sizeof(buf)));

        RecompRuntimeUi menu;
        CHECK(!runtime_menu_is_open(&menu));
        runtime_menu_toggle(&menu);   CHECK(runtime_menu_is_open(&menu));
        runtime_menu_open(&menu);     CHECK(runtime_menu_is_open(&menu));
        runtime_menu_close(&menu);    CHECK(!runtime_menu_is_open(&menu));
        runtime_menu_toggle(&menu);   runtime_menu_toggle(&menu);
        CHECK(!runtime_menu_is_open(&menu));
        CHECK(!runtime_menu_is_open(nullptr));
        runtime_menu_toggle(nullptr);
    }

    return testh::finish("host_settings");
}
