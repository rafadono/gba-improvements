#pragma once

#include <cstddef>
#include <cstdint>

namespace gbaimp {

enum class EqPreset : int {
    Flat = 0,
    WarmRetro = 1,
    CrispModern = 2,
    BassBoost = 3,
};

struct AudioDspSettings {
    bool enabled = true;
    bool anti_aliasing_filter = true;
    EqPreset preset = EqPreset::CrispModern;
    float bass_gain_db = 2.0f;
    float mid_gain_db = 0.0f;
    float treble_gain_db = 1.5f;
    float stereo_width = 1.25f;
    bool limiter_enabled = true;
};

class AudioDsp {
public:
    static AudioDsp& instance();

    void apply_preset(EqPreset preset);
    void set_stereo_width(float width);
    void set_anti_aliasing(bool enable);
    void set_limiter(bool enable);

    AudioDspSettings& settings() { return settings_; }
    const AudioDspSettings& settings() const { return settings_; }

    void process_stereo(int16_t* interleaved_samples, std::size_t frame_count, int sample_rate = 32768);

private:
    AudioDsp();

    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        float process(float in) {
            float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }

        void reset() {
            x1 = x2 = y1 = y2 = 0.0f;
        }
    };

    void update_coefficients(int sample_rate);

    AudioDspSettings settings_;
    int last_sample_rate_ = 32768;

    Biquad lp_l_, lp_r_;
    Biquad bass_l_, bass_r_;
    Biquad mid_l_, mid_r_;
    Biquad treble_l_, treble_r_;
};

} // namespace gbaimp
