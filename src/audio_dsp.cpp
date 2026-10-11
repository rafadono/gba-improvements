#include "gbaimp/audio_dsp.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace gbaimp {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Biquad coefficients follow R. Bristow-Johnson's "Audio EQ Cookbook"
// (low-pass, peaking EQ, low shelf and high shelf formulas).

void design_lowpass(float fc, float fs, float q, float& b0, float& b1, float& b2, float& a1, float& a2) {
    float w0 = 2.0f * kPi * (fc / fs);
    float alpha = std::sin(w0) / (2.0f * q);
    float cos_w0 = std::cos(w0);

    float a0 = 1.0f + alpha;
    b0 = ((1.0f - cos_w0) / 2.0f) / a0;
    b1 = (1.0f - cos_w0) / a0;
    b2 = ((1.0f - cos_w0) / 2.0f) / a0;
    a1 = (-2.0f * cos_w0) / a0;
    a2 = (1.0f - alpha) / a0;
}

void design_lowshelf(float gain_db, float fc, float fs, float& b0, float& b1, float& b2, float& a1, float& a2) {
    float a = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * kPi * (fc / fs);
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / 2.0f * std::sqrt(2.0f);

    float a0 = (a + 1.0f) + (a - 1.0f) * cos_w0 + 2.0f * std::sqrt(a) * alpha;
    b0 = (a * ((a + 1.0f) - (a - 1.0f) * cos_w0 + 2.0f * std::sqrt(a) * alpha)) / a0;
    b1 = (2.0f * a * ((a - 1.0f) - (a + 1.0f) * cos_w0)) / a0;
    b2 = (a * ((a + 1.0f) - (a - 1.0f) * cos_w0 - 2.0f * std::sqrt(a) * alpha)) / a0;
    a1 = (-2.0f * ((a - 1.0f) + (a + 1.0f) * cos_w0)) / a0;
    a2 = ((a + 1.0f) + (a - 1.0f) * cos_w0 - 2.0f * std::sqrt(a) * alpha) / a0;
}

void design_peaking(float gain_db, float fc, float fs, float q, float& b0, float& b1, float& b2, float& a1, float& a2) {
    float a = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * kPi * (fc / fs);
    float cos_w0 = std::cos(w0);
    float alpha = std::sin(w0) / (2.0f * q);

    float a0 = 1.0f + alpha / a;
    b0 = (1.0f + alpha * a) / a0;
    b1 = (-2.0f * cos_w0) / a0;
    b2 = (1.0f - alpha * a) / a0;
    a1 = (-2.0f * cos_w0) / a0;
    a2 = (1.0f - alpha / a) / a0;
}

void design_highshelf(float gain_db, float fc, float fs, float& b0, float& b1, float& b2, float& a1, float& a2) {
    float a = std::pow(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * kPi * (fc / fs);
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / 2.0f * std::sqrt(2.0f);

    float a0 = (a + 1.0f) - (a - 1.0f) * cos_w0 + 2.0f * std::sqrt(a) * alpha;
    b0 = (a * ((a + 1.0f) + (a - 1.0f) * cos_w0 + 2.0f * std::sqrt(a) * alpha)) / a0;
    b1 = (-2.0f * a * ((a - 1.0f) + (a + 1.0f) * cos_w0)) / a0;
    b2 = (a * ((a + 1.0f) + (a - 1.0f) * cos_w0 - 2.0f * std::sqrt(a) * alpha)) / a0;
    a1 = (2.0f * ((a - 1.0f) - (a + 1.0f) * cos_w0)) / a0;
    a2 = ((a + 1.0f) - (a - 1.0f) * cos_w0 - 2.0f * std::sqrt(a) * alpha) / a0;
}

float soft_limit(float x) {
    if (x > 32000.0f) {
        return 32000.0f + (x - 32000.0f) / (1.0f + (x - 32000.0f) / 767.0f);
    } else if (x < -32000.0f) {
        return -32000.0f + (x + 32000.0f) / (1.0f - (x + 32000.0f) / 767.0f);
    }
    return x;
}

} // namespace

AudioDsp& AudioDsp::instance() {
    static AudioDsp s_instance;
    return s_instance;
}

AudioDsp::AudioDsp() {
    apply_preset(settings_.preset);
    refresh_coefficients(last_sample_rate_);
}

void AudioDsp::apply_preset(EqPreset preset) {
    settings_.preset = preset;
    // Fields are stored one by one: a concurrent block may see a mix of old and new values
    // for a few milliseconds (each value is valid on its own), never a torn one.
    switch (preset) {
        case EqPreset::Flat:
            settings_.bass_gain_db = 0.0f;
            settings_.mid_gain_db = 0.0f;
            settings_.treble_gain_db = 0.0f;
            settings_.anti_aliasing_filter = false;
            settings_.stereo_width = 1.0f;
            break;
        case EqPreset::WarmRetro:
            settings_.bass_gain_db = 3.0f;
            settings_.mid_gain_db = -0.5f;
            settings_.treble_gain_db = -2.0f;
            settings_.anti_aliasing_filter = true;
            settings_.stereo_width = 1.15f;
            break;
        case EqPreset::CrispModern:
            settings_.bass_gain_db = 2.0f;
            settings_.mid_gain_db = 0.5f;
            settings_.treble_gain_db = 2.0f;
            settings_.anti_aliasing_filter = true;
            settings_.stereo_width = 1.3f;
            break;
        case EqPreset::BassBoost:
            settings_.bass_gain_db = 5.0f;
            settings_.mid_gain_db = 0.0f;
            settings_.treble_gain_db = 1.0f;
            settings_.anti_aliasing_filter = true;
            settings_.stereo_width = 1.2f;
            break;
    }
}

void AudioDsp::set_stereo_width(float width) {
    settings_.stereo_width = std::clamp(width, 0.0f, 2.0f);
}

void AudioDsp::set_anti_aliasing(bool enable) {
    settings_.anti_aliasing_filter = enable;
}

void AudioDsp::set_limiter(bool enable) {
    settings_.limiter_enabled = enable;
}

void AudioDsp::reset() {
    reset_requested_.store(true, std::memory_order_release);
}

void AudioDsp::refresh_coefficients(int sample_rate) {
    if (reset_requested_.exchange(false, std::memory_order_acq_rel)) {
        for (Biquad* q : {&lp_l_, &lp_r_, &bass_l_, &bass_r_, &mid_l_, &mid_r_, &treble_l_, &treble_r_,
                          &lp_m_, &bass_m_, &mid_m_, &treble_m_}) {
            q->reset();
        }
    }
    const float bass = settings_.bass_gain_db;
    const float mid = settings_.mid_gain_db;
    const float treble = settings_.treble_gain_db;
    if (sample_rate == last_sample_rate_ && bass == applied_bass_db_ && mid == applied_mid_db_ &&
        treble == applied_treble_db_ && applied_valid_) {
        return;
    }
    update_coefficients(sample_rate, bass, mid, treble);
}

void AudioDsp::update_coefficients(int sample_rate, float bass_db, float mid_db, float treble_db) {
    last_sample_rate_ = sample_rate;
    applied_bass_db_ = bass_db;
    applied_mid_db_ = mid_db;
    applied_treble_db_ = treble_db;
    applied_valid_ = true;
    float fs = static_cast<float>(sample_rate);

    float b0, b1, b2, a1, a2;
    auto assign = [&](Biquad& l, Biquad& r, Biquad& m) {
        for (Biquad* q : {&l, &r, &m}) {
            q->b0 = b0; q->b1 = b1; q->b2 = b2;
            q->a1 = a1; q->a2 = a2;
        }
    };

    float lp_cutoff = std::min(15000.0f, fs * 0.45f);
    design_lowpass(lp_cutoff, fs, 0.707f, b0, b1, b2, a1, a2);
    assign(lp_l_, lp_r_, lp_m_);

    design_lowshelf(bass_db, 180.0f, fs, b0, b1, b2, a1, a2);
    assign(bass_l_, bass_r_, bass_m_);

    design_peaking(mid_db, 1400.0f, fs, 1.0f, b0, b1, b2, a1, a2);
    assign(mid_l_, mid_r_, mid_m_);

    float treble_fc = std::min(6500.0f, fs * 0.4f);
    design_highshelf(treble_db, treble_fc, fs, b0, b1, b2, a1, a2);
    assign(treble_l_, treble_r_, treble_m_);
}

namespace {
inline int16_t to_s16(float v) {
    return static_cast<int16_t>(std::lrintf(std::clamp(v, -32768.0f, 32767.0f)));
}
} // namespace

namespace {
// RAII claim of the single-consumer processing slot.
struct BusyGuard {
    explicit BusyGuard(std::atomic<bool>& f) : flag(f), owns(!f.exchange(true, std::memory_order_acquire)) {}
    ~BusyGuard() { if (owns) flag.store(false, std::memory_order_release); }
    std::atomic<bool>& flag;
    bool owns;
};
} // namespace

void AudioDsp::process_stereo(int16_t* interleaved_samples, std::size_t frame_count, int sample_rate) {
    if (!settings_.enabled || !interleaved_samples || frame_count == 0) {
        return;
    }
    BusyGuard guard(busy_);
    if (!guard.owns) return;   // another thread is processing: leave this buffer untouched

    refresh_coefficients(sample_rate > 0 ? sample_rate : last_sample_rate_);

    const float width = settings_.stereo_width;
    const bool filter = settings_.anti_aliasing_filter;
    const bool limiter = settings_.limiter_enabled;

    for (std::size_t i = 0; i < frame_count; ++i) {
        float l = static_cast<float>(interleaved_samples[i * 2]);
        float r = static_cast<float>(interleaved_samples[i * 2 + 1]);

        if (filter) {
            l = lp_l_.process(l);
            r = lp_r_.process(r);
        }

        l = bass_l_.process(l);
        r = bass_r_.process(r);
        l = mid_l_.process(l);
        r = mid_r_.process(r);
        l = treble_l_.process(l);
        r = treble_r_.process(r);

        float mid = 0.5f * (l + r);
        float side = 0.5f * (l - r) * width;
        l = mid + side;
        r = mid - side;

        if (limiter) {
            l = soft_limit(l);
            r = soft_limit(r);
        }

        interleaved_samples[i * 2]     = to_s16(l);
        interleaved_samples[i * 2 + 1] = to_s16(r);
    }
}

void AudioDsp::process_mono(int16_t* samples, std::size_t sample_count, int sample_rate) {
    if (!settings_.enabled || !samples || sample_count == 0) {
        return;
    }
    BusyGuard guard(busy_);
    if (!guard.owns) return;

    refresh_coefficients(sample_rate > 0 ? sample_rate : last_sample_rate_);

    const bool filter = settings_.anti_aliasing_filter;
    const bool limiter = settings_.limiter_enabled;

    for (std::size_t i = 0; i < sample_count; ++i) {
        float x = static_cast<float>(samples[i]);
        if (filter) x = lp_m_.process(x);
        x = bass_m_.process(x);
        x = mid_m_.process(x);
        x = treble_m_.process(x);
        if (limiter) x = soft_limit(x);
        samples[i] = to_s16(x);
    }
}

} // namespace gbaimp
