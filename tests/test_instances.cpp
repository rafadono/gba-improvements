// Every module can be constructed without its singleton, and instances are
// independent of each other and of instance().
#include "test_harness.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/dialog_turbo.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/hud_anchoring.h"
#include "gbaimp/input_enhancements.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/savestate_thumbnails.h"
#include "gbaimp/screen_filters.h"
#include "gbaimp/pixel_art_upscaler.h"
#include <filesystem>
#include <type_traits>
#include <vector>

using namespace gbaimp;

// Modules that own SDL resources must not be copyable.
static_assert(!std::is_copy_constructible<ScreenFilters>::value, "ScreenFilters owns a texture");
static_assert(!std::is_copy_assignable<ScreenFilters>::value, "ScreenFilters owns a texture");
static_assert(!std::is_copy_constructible<FrameInterpolator>::value, "FrameInterpolator owns a texture");
static_assert(!std::is_copy_constructible<HdAudioPlayer>::value, "HdAudioPlayer owns PCM state");
static_assert(!std::is_copy_constructible<InputEnhancements>::value, "InputEnhancements owns a controller");
// Plain value-like modules keep their implicit copy semantics (source compatible).
static_assert(std::is_copy_constructible<PixelArtUpscaler>::value, "");
static_assert(std::is_default_constructible<PerfHud>::value, "");

int main() {
    testh::TempDir tmp("instances");
    tmp.enter();   // default-constructed modules touch relative paths

    // PixelArtUpscaler
    {
        PixelArtUpscaler a, b;
        a.set_scale(UpscaleScale::X3);
        CHECK(a.is_enabled());
        CHECK(!b.is_enabled());
    }
    // ScreenFilters
    {
        ScreenFilters a, b;
        a.set_color_profile(ColorProfile::Ags101);
        a.set_mask_intensity(0.9f);
        CHECK(a.settings().color_profile == ColorProfile::Ags101);
        CHECK(b.settings().color_profile == ColorProfile::Raw);
        CHECK_NEAR(b.settings().mask_intensity, 0.40f, 1e-6f);
        std::vector<uint8_t> pa = {128, 128, 128}, pb = {128, 128, 128};
        a.apply_color_correction(pa.data(), 1, 1);
        b.apply_color_correction(pb.data(), 1, 1);
        CHECK(pa != pb);
        CHECK_EQ(int(pb[0]), 128);
    }
    // AudioDsp
    {
        AudioDsp a, b;
        a.apply_preset(EqPreset::BassBoost);
        a.set_stereo_width(0.5f);
        CHECK(a.settings().preset == EqPreset::BassBoost);
        CHECK(b.settings().preset == EqPreset::CrispModern);
        CHECK_NEAR(b.settings().stereo_width, 1.3f, 1e-6f);   // CrispModern preset width
        // A default AudioDsp and a default AudioDspSettings must agree (they once did not:
        // the struct said 1.25 / 0.0 dB mid / 1.5 dB treble while the constructor applied
        // the CrispModern preset).
        const AudioDspSettings def;
        CHECK(b.settings().preset == def.preset);
        CHECK_NEAR(b.settings().bass_gain_db, def.bass_gain_db, 1e-6f);
        CHECK_NEAR(b.settings().mid_gain_db, def.mid_gain_db, 1e-6f);
        CHECK_NEAR(b.settings().treble_gain_db, def.treble_gain_db, 1e-6f);
        CHECK_NEAR(b.settings().stereo_width, def.stereo_width, 1e-6f);
        CHECK(b.settings().anti_aliasing_filter == def.anti_aliasing_filter);
        CHECK(b.settings().limiter_enabled == def.limiter_enabled);
    }
    // FrameInterpolator
    {
        FrameInterpolator a, b;
        a.set_mode(FramerateMode::Fps144);
        CHECK_NEAR(a.target_fps(), 144.0, 1e-9);
        CHECK_NEAR(b.target_fps(), 60.0, 1e-9);
    }
    // PerfHud
    {
        PerfHud a, b;
        a.set_mode(PerfHudMode::FpsOnly);
        a.set_game_viewport({1, 2, 3, 4});
        CHECK(a.settings().mode == PerfHudMode::FpsOnly);
        CHECK(b.settings().mode == PerfHudMode::FullWithGraph);
        CHECK_EQ(b.game_viewport().w, 240);
    }
    // HudAnchoring
    {
        HudAnchoring a, b;
        HudZone z;
        a.add_zone(z);
        a.set_mode(HudAnchorMode::WidescreenAnchored);
        CHECK_EQ(a.zones().size(), size_t(1));
        CHECK_EQ(b.zones().size(), size_t(0));
        CHECK(b.settings().mode == HudAnchorMode::OriginalCentered);
    }
    // DialogTurbo
    {
        DialogTurbo a, b;
        a.set_enabled(false);
        a.set_skip_speed(1);
        CHECK(!a.is_enabled());
        CHECK(b.is_enabled());
        CHECK_EQ(b.skip_speed(), 2);
    }
    // HdAudioPlayer
    {
        HdAudioPlayer a, b;
        a.set_master_volume(0.25f);
        CHECK_NEAR(a.settings().master_volume, 0.25f, 1e-6f);
        CHECK_NEAR(b.settings().master_volume, 1.0f, 1e-6f);
    }
    // SavestateThumbnailManager
    {
        SavestateThumbnailManager a, b;
        a.set_base_directory(tmp.file("thumbs_a"));
        CHECK_EQ(a.base_directory(), tmp.file("thumbs_a"));
        CHECK_EQ(b.base_directory(), std::string("saves/thumbnails"));
        CHECK(std::filesystem::exists(tmp.file("thumbs_a")));
    }
    // InputEnhancements
    {
        InputEnhancements a(tmp.file("a.ini")), b(tmp.file("b.ini"));
        a.set_autosave_keybinds(false);
        a.apply_keyboard_preset(KeyboardPreset::Wasd);
        CHECK_EQ(int(a.get_key_scancode(KEY_A)), int(SDL_SCANCODE_J));
        CHECK_EQ(int(b.get_key_scancode(KEY_A)), int(SDL_SCANCODE_X));
        CHECK_EQ(a.keybinds_path(), tmp.file("a.ini"));
        CHECK_EQ(b.keybinds_path(), tmp.file("b.ini"));
    }

    // The singletons are still the shared defaults and are unaffected by the instances above.
    CHECK(&ScreenFilters::instance() == &ScreenFilters::instance());
    CHECK(ScreenFilters::instance().settings().color_profile == ColorProfile::Raw);
    CHECK(AudioDsp::instance().settings().preset == EqPreset::CrispModern);
    CHECK(&PerfHud::instance() != nullptr);

    return testh::finish("instances");
}
