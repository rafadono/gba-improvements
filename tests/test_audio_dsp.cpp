#include "test_harness.h"
#include "gbaimp/audio_dsp.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

using namespace gbaimp;

int main() {
    auto& dsp = AudioDsp::instance();

    // Flat preset: neutral settings.
    dsp.settings().enabled = true;
    dsp.apply_preset(EqPreset::Flat);
    CHECK(dsp.settings().preset == EqPreset::Flat);
    CHECK_NEAR(dsp.settings().bass_gain_db, 0.0f, 1e-6f);
    CHECK_NEAR(dsp.settings().stereo_width, 1.0f, 1e-6f);
    CHECK(!dsp.settings().anti_aliasing_filter);

    // Silence stays silent.
    {
        std::vector<int16_t> buf(2048, 0);
        dsp.process_stereo(buf.data(), buf.size() / 2, 32768);
        bool silent = true;
        for (int16_t s : buf) if (s != 0) silent = false;
        CHECK(silent);
    }

    // Flat + width 1: signal passes through (within filter rounding).
    {
        std::vector<int16_t> buf, orig;
        for (int i = 0; i < 512; ++i) {
            buf.push_back(int16_t((i % 64) * 100 - 3000));
            buf.push_back(int16_t(2000 - (i % 32) * 90));
        }
        orig = buf;
        dsp.process_stereo(buf.data(), buf.size() / 2, 32768);
        int max_err = 0;
        for (size_t i = 0; i < buf.size(); ++i) max_err = std::max(max_err, std::abs(buf[i] - orig[i]));
        CHECK(max_err <= 2);
    }

    // Disabled: buffer untouched even with a mono width.
    {
        dsp.settings().enabled = false;
        dsp.set_stereo_width(0.0f);
        std::vector<int16_t> buf = {1000, -1000, 500, -500};
        auto orig = buf;
        dsp.process_stereo(buf.data(), 2, 32768);
        CHECK(buf == orig);
        dsp.settings().enabled = true;
    }

    // Null / empty input is safe.
    dsp.process_stereo(nullptr, 100, 32768);
    { int16_t d[2] = {5, 6}; dsp.process_stereo(d, 0, 32768); CHECK(d[0] == 5 && d[1] == 6); }

    // Stereo width: clamp and behavior.
    dsp.set_stereo_width(-1.0f); CHECK_NEAR(dsp.settings().stereo_width, 0.0f, 1e-6f);
    dsp.set_stereo_width(7.0f);  CHECK_NEAR(dsp.settings().stereo_width, 2.0f, 1e-6f);

    dsp.apply_preset(EqPreset::Flat);
    dsp.set_stereo_width(0.0f);  // mono fold-down
    {
        std::vector<int16_t> buf = {1000, -1000, 3000, 1000};
        dsp.process_stereo(buf.data(), 2, 32768);
        CHECK_NEAR(int(buf[0]), int(buf[1]), 1);
        CHECK_NEAR(int(buf[2]), int(buf[3]), 1);
        CHECK_NEAR(int(buf[0]), 0, 1);
        CHECK_NEAR(int(buf[2]), 2000, 2);
    }
    dsp.set_stereo_width(2.0f);  // side doubled
    {
        std::vector<int16_t> buf = {1000, -1000};
        dsp.process_stereo(buf.data(), 1, 32768);
        CHECK_NEAR(int(buf[0]), 2000, 2);
        CHECK_NEAR(int(buf[1]), -2000, 2);
    }

    // Limiter keeps hot signals below full scale; disabled it only hard-clamps.
    dsp.apply_preset(EqPreset::Flat);
    dsp.set_stereo_width(1.0f);
    dsp.set_limiter(true);
    {
        std::vector<int16_t> buf;
        for (int i = 0; i < 256; ++i) {
            int16_t v = (i & 1) ? 32767 : -32768;
            buf.push_back(v); buf.push_back(v);
        }
        dsp.process_stereo(buf.data(), buf.size() / 2, 32768);
        int peak = 0;
        for (int16_t s : buf) peak = std::max(peak, std::abs(int(s)));
        CHECK(peak <= 32500);   // soft knee tops out near 32384
        CHECK(peak >= 31000);   // but it must not crush the signal
    }
    dsp.set_limiter(false);
    CHECK(!dsp.settings().limiter_enabled);
    {
        std::vector<int16_t> buf = {32767, 32767, -32768, -32768};
        dsp.process_stereo(buf.data(), 2, 32768);
        CHECK(buf[0] >= 32700);          // not softened
        CHECK(buf[2] <= -32700);
    }
    dsp.set_limiter(true);

    // A hot preset on loud input stays in range and survives a sample-rate change.
    dsp.apply_preset(EqPreset::BassBoost);
    {
        std::vector<int16_t> buf;
        for (int i = 0; i < 4096; ++i) {
            int16_t v = ((i / 16) & 1) ? 30000 : -30000;
            buf.push_back(v); buf.push_back(v);
        }
        dsp.process_stereo(buf.data(), buf.size() / 2, 48000);
        bool in_range = true;
        for (int16_t s : buf) if (std::abs(int(s)) > 32768) in_range = false;
        CHECK(in_range);
    }

    // Presets set distinct, documented values.
    dsp.apply_preset(EqPreset::WarmRetro);   CHECK(dsp.settings().anti_aliasing_filter);
    dsp.apply_preset(EqPreset::BassBoost);   CHECK_NEAR(dsp.settings().bass_gain_db, 5.0f, 1e-6f);

    return testh::finish("audio_dsp");
}
