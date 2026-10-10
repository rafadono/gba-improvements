#include "gbaimp/audio_dsp.h"
#include "gbaimp/hd_audio_player.h"
#include <SDL.h>
#ifdef SDL_QueueAudio
#undef SDL_QueueAudio
#endif
#include <cmath>
#include <algorithm>
#include <vector>

namespace gbaimp {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void design_lowpass(AudioDspSettings, float fc, float fs, float q, float& b0, float& b1, float& b2, float& a1, float& a2) {
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
    update_coefficients(last_sample_rate_);
}

void AudioDsp::apply_preset(EqPreset preset) {
    settings_.preset = preset;
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
    update_coefficients(last_sample_rate_);
}

void AudioDsp::set_stereo_width(float width) {
    settings_.stereo_width = std::clamp(width, 0.0f, 2.0f);
}

void AudioDsp::set_anti_aliasing(bool enable) {
    settings_.anti_aliasing_filter = enable;
    update_coefficients(last_sample_rate_);
}

void AudioDsp::set_limiter(bool enable) {
    settings_.limiter_enabled = enable;
}

void AudioDsp::update_coefficients(int sample_rate) {
    last_sample_rate_ = sample_rate;
    float fs = static_cast<float>(sample_rate);

    float b0, b1, b2, a1, a2;

    float lp_cutoff = std::min(15000.0f, fs * 0.45f);
    design_lowpass(settings_, lp_cutoff, fs, 0.707f, b0, b1, b2, a1, a2);
    lp_l_.b0 = lp_r_.b0 = b0;
    lp_l_.b1 = lp_r_.b1 = b1;
    lp_l_.b2 = lp_r_.b2 = b2;
    lp_l_.a1 = lp_r_.a1 = a1;
    lp_l_.a2 = lp_r_.a2 = a2;

    design_lowshelf(settings_.bass_gain_db, 180.0f, fs, b0, b1, b2, a1, a2);
    bass_l_.b0 = bass_r_.b0 = b0;
    bass_l_.b1 = bass_r_.b1 = b1;
    bass_l_.b2 = bass_r_.b2 = b2;
    bass_l_.a1 = bass_r_.a1 = a1;
    bass_l_.a2 = bass_r_.a2 = a2;

    design_peaking(settings_.mid_gain_db, 1400.0f, fs, 1.0f, b0, b1, b2, a1, a2);
    mid_l_.b0 = mid_r_.b0 = b0;
    mid_l_.b1 = mid_r_.b1 = b1;
    mid_l_.b2 = mid_r_.b2 = b2;
    mid_l_.a1 = mid_r_.a1 = a1;
    mid_l_.a2 = mid_r_.a2 = a2;

    float treble_fc = std::min(6500.0f, fs * 0.4f);
    design_highshelf(settings_.treble_gain_db, treble_fc, fs, b0, b1, b2, a1, a2);
    treble_l_.b0 = treble_r_.b0 = b0;
    treble_l_.b1 = treble_r_.b1 = b1;
    treble_l_.b2 = treble_r_.b2 = b2;
    treble_l_.a1 = treble_r_.a1 = a1;
    treble_l_.a2 = treble_r_.a2 = a2;
}

void AudioDsp::process_stereo(int16_t* interleaved_samples, std::size_t frame_count, int sample_rate) {
    if (!settings_.enabled || !interleaved_samples || frame_count == 0) {
        return;
    }

    if (sample_rate != last_sample_rate_) {
        update_coefficients(sample_rate);
    }

    float width = settings_.stereo_width;
    bool filter = settings_.anti_aliasing_filter;
    bool limiter = settings_.limiter_enabled;

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

        interleaved_samples[i * 2]     = static_cast<int16_t>(std::clamp(l, -32768.0f, 32767.0f));
        interleaved_samples[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768.0f, 32767.0f));
    }
}

} // namespace gbaimp

#include "gbaimp/audio_pipeline.h"

namespace gbaimp {

void process_audio_block(int16_t* interleaved, size_t sample_count) {
    const int sample_rate = HdAudioPlayer::instance().settings().target_sample_rate;
    HdAudioPlayer::instance().mix_audio(interleaved, sample_count, 2, sample_rate);
    AudioDsp::instance().process_stereo(interleaved, sample_count / 2, sample_rate);
}

} // namespace gbaimp

