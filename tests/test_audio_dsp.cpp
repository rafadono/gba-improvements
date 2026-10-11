#include "test_harness.h"
#include "gbaimp/audio_dsp.h"
#include <algorithm>
#include <cmath>
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

    // ---------------------------------------------------------------------------------
    // Mono path (independent instances: no state shared with the singleton above)
    // ---------------------------------------------------------------------------------
    {
        AudioDsp m;
        m.apply_preset(EqPreset::Flat);

        // Silence stays silent.
        std::vector<int16_t> silence(1024, 0);
        m.process_mono(silence.data(), silence.size(), 65536);
        bool silent = true;
        for (int16_t s : silence) if (s != 0) silent = false;
        CHECK(silent);

        // Flat preset is transparent (rounded, so exact up to filter float error).
        std::vector<int16_t> buf, orig;
        for (int i = 0; i < 1024; ++i) buf.push_back(int16_t((i % 64) * 100 - 3000));
        orig = buf;
        m.process_mono(buf.data(), buf.size(), 65536);
        int max_err = 0;
        for (size_t i = 0; i < buf.size(); ++i) max_err = std::max(max_err, std::abs(buf[i] - orig[i]));
        CHECK(max_err <= 1);

        // Stereo width does not touch mono.
        m.set_stereo_width(0.0f);
        buf = orig;
        m.process_mono(buf.data(), buf.size(), 65536);
        max_err = 0;
        for (size_t i = 0; i < buf.size(); ++i) max_err = std::max(max_err, std::abs(buf[i] - orig[i]));
        CHECK(max_err <= 1);
        m.set_stereo_width(1.0f);

        // Disabled / null / empty are no-ops.
        m.settings().enabled = false;
        buf = orig;
        m.process_mono(buf.data(), buf.size(), 65536);
        CHECK(buf == orig);
        m.settings().enabled = true;
        m.process_mono(nullptr, 100, 65536);
        { int16_t d[2] = {7, 8}; m.process_mono(d, 0, 65536); CHECK(d[0] == 7 && d[1] == 8); }

        // Limiter bounds a full-scale square wave; disabled it only hard-clamps.
        m.set_limiter(true);
        std::vector<int16_t> hot;
        for (int i = 0; i < 512; ++i) hot.push_back((i & 1) ? 32767 : -32768);
        m.process_mono(hot.data(), hot.size(), 65536);
        int peak = 0;
        for (int16_t s : hot) peak = std::max(peak, std::abs(int(s)));
        CHECK(peak <= 32500);
        CHECK(peak >= 31000);
        m.set_limiter(false);
        std::vector<int16_t> hard = {32767, 32767, -32768, -32768};
        m.process_mono(hard.data(), hard.size(), 65536);
        CHECK(hard[0] >= 32700 && hard[2] <= -32700);
        m.set_limiter(true);

        // A hot preset stays in range at a mono-device rate.
        m.apply_preset(EqPreset::BassBoost);
        std::vector<int16_t> loud;
        for (int i = 0; i < 4096; ++i) loud.push_back(((i / 16) & 1) ? 30000 : -30000);
        m.process_mono(loud.data(), loud.size(), 65536);
        bool in_range = true;
        for (int16_t s : loud) if (std::abs(int(s)) > 32768) in_range = false;
        CHECK(in_range);
    }

    // Mono equals stereo with identical channels (width 1): same chain, same coefficients.
    {
        AudioDsp ms, ss;
        ms.apply_preset(EqPreset::BassBoost);
        ss.apply_preset(EqPreset::BassBoost);
        ms.set_stereo_width(1.0f);
        ss.set_stereo_width(1.0f);
        std::vector<int16_t> mono, st;
        for (int i = 0; i < 2048; ++i) {
            const int16_t v = int16_t(((i * 37) % 2000) * 8 - 8000);
            mono.push_back(v);
            st.push_back(v);
            st.push_back(v);
        }
        for (int rate : {32768, 65536, 48000}) {
            std::vector<int16_t> a = mono, b = st;
            ms.process_mono(a.data(), a.size(), rate);
            ss.process_stereo(b.data(), b.size() / 2, rate);
            int diff = 0;
            for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::abs(a[i] - b[i * 2]));
            CHECK(diff <= 1);
        }
    }

    // The sample rate drives the coefficients: a 144 Hz tone (at 48 kHz) sits at the bass
    // shelf corner for fs = 48000 but far below it when the same stream is declared 8000 Hz.
    {
        auto gain_at = [](int declared_rate) {
            AudioDsp d;
            d.apply_preset(EqPreset::BassBoost);
            d.set_anti_aliasing(false);
            d.settings().treble_gain_db = 0.0f;
            d.settings().limiter_enabled = false;
            std::vector<int16_t> t(8000);
            for (size_t i = 0; i < t.size(); ++i)
                t[i] = int16_t(6000.0 * std::sin(2.0 * 3.14159265358979 * 144.0 * double(i) / 48000.0));
            std::vector<int16_t> o = t;
            d.process_mono(o.data(), o.size(), declared_rate);
            double ein = 0, eout = 0;
            for (size_t i = 4000; i < t.size(); ++i) { ein += double(t[i]) * t[i]; eout += double(o[i]) * o[i]; }
            return std::sqrt(eout / ein);
        };
        const double g48 = gain_at(48000);
        const double g8 = gain_at(8000);
        CHECK(g48 > 1.1);              // boosted
        CHECK(g8 > g48 * 1.05);        // different coefficients for a different declared rate
    }

    return testh::finish("audio_dsp");
}
