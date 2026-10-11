#pragma once

#include "gbaimp/relaxed.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gbaimp {

enum class EqPreset : int {
    Flat = 0,
    WarmRetro = 1,
    CrispModern = 2,
    BassBoost = 3,
};

// Scalar fields are relaxed atomics (see relaxed.h) so a UI/game thread may change them
// while the audio thread is inside process_*(): `settings().enabled = false` and the
// set_*/apply_preset() calls are safe at any time. `preset` is informational for the UI
// (the DSP only reads the gains and flags) and must be written from one thread only.
struct AudioDspSettings {
    Relaxed<bool> enabled{true};
    Relaxed<bool> anti_aliasing_filter{true};
    // The defaults below are the CrispModern preset's values (AudioDsp applies that preset
    // on construction, so a default-constructed AudioDsp and a default AudioDspSettings agree).
    EqPreset preset = EqPreset::CrispModern;
    Relaxed<float> bass_gain_db{2.0f};
    Relaxed<float> mid_gain_db{0.5f};
    Relaxed<float> treble_gain_db{2.0f};
    Relaxed<float> stereo_width{1.3f};
    Relaxed<bool> limiter_enabled{true};
};

class AudioDsp {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    AudioDsp();

    static AudioDsp& instance();

    void apply_preset(EqPreset preset);
    void set_stereo_width(float width);
    void set_anti_aliasing(bool enable);
    void set_limiter(bool enable);

    AudioDspSettings& settings() { return settings_; }
    const AudioDspSettings& settings() const { return settings_; }

    // Threading: settings and the set_*/apply_preset() calls may be used from any thread
    // while process_*() runs on another (the filter coefficients are derived and owned by the
    // processing thread, which re-derives them when a gain or the sample rate changes: no
    // locks, no allocation). process_*() itself is single-consumer: if two threads call it at
    // the same instant, the second call returns immediately and leaves its buffer unprocessed.
    // Asks the processing thread to clear the filter histories before its next block (safe from
    // any thread; the histories themselves are only ever touched by the processing thread).
    void reset();

    void process_stereo(int16_t* interleaved_samples, std::size_t frame_count, int sample_rate = 32768);

    // Same chain for a mono buffer of `sample_count` int16 values: anti-aliasing
    // low-pass, bass/mid/treble EQ and the soft limiter. Stereo width has no meaning
    // for one channel and is ignored. Mono and stereo use separate filter state, so one
    // AudioDsp may serve both kinds of buffers.
    void process_mono(int16_t* samples, std::size_t sample_count, int sample_rate = 32768);

private:

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

    // Derives every filter's coefficients from the given gains/rate. Processing-thread only
    // (and the constructor).
    void update_coefficients(int sample_rate, float bass_db, float mid_db, float treble_db);
    // Re-derives when the rate or one of the gains differs from what the filters use.
    void refresh_coefficients(int sample_rate);

    AudioDspSettings settings_;
    // Parameters the coefficients below were derived from (processing thread only).
    int last_sample_rate_ = 32768;
    float applied_bass_db_ = 0.0f, applied_mid_db_ = 0.0f, applied_treble_db_ = 0.0f;
    bool applied_valid_ = false;
    std::atomic<bool> reset_requested_{false};
    std::atomic<bool> busy_{false};

    Biquad lp_l_, lp_r_;
    Biquad bass_l_, bass_r_;
    Biquad mid_l_, mid_r_;
    Biquad treble_l_, treble_r_;
    Biquad lp_m_, bass_m_, mid_m_, treble_m_;   // mono path
};

} // namespace gbaimp
