#pragma once

#include "gbaimp/relaxed.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SDL_AudioSpec;

namespace gbaimp {

// Scalar fields are relaxed atomics (see relaxed.h): safe to change from the UI/game thread
// while the audio thread mixes. `config_path` is only read by load_track_mapping() callers.
struct HdAudioSettings {
    Relaxed<bool> enabled{true};
    Relaxed<float> master_volume{1.0f};
    Relaxed<float> bgm_volume{0.85f};
    Relaxed<float> sfx_volume{1.0f};
    std::string config_path = "config/music_tracks.ini";
    // Output sample rate (Hz) of the host audio pipeline; loaded tracks are converted to it
    Relaxed<int> target_sample_rate{65536};
};

class HdAudioPlayer {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    HdAudioPlayer();
    ~HdAudioPlayer();
    HdAudioPlayer(const HdAudioPlayer&) = delete;
    HdAudioPlayer& operator=(const HdAudioPlayer&) = delete;

    static HdAudioPlayer& instance();

    const HdAudioSettings& settings() const { return settings_; }
    HdAudioSettings& settings() { return settings_; }

    void set_enabled(bool enabled);
    void set_bgm_volume(float vol);
    void set_sfx_volume(float vol);
    void set_master_volume(float vol);

    // Control side (game / UI thread; these may allocate, read files and take a mutex that
    // the audio thread never touches):
    //   load_track_mapping  replaces the song-id -> WAV map (and forgets earlier load failures).
    //                       A missing file leaves it empty.
    //   trigger_song        starts the mapped WAV (decoded and resampled here, on the calling
    //                       thread) or stops HD playback when the id is unmapped or the file
    //                       cannot be loaded (a failed id is remembered until the next load_track_mapping, so a
    //                       mapped-but-missing WAV costs one file probe, not one per call and logs nothing).
    //                       Re-triggering the id that already plays is a no-op.
    //   stop_music          stops HD playback.
    // The decoded track is handed to the audio thread through an atomic pointer exchange, so a
    // mix_audio() running concurrently either keeps the old track for its current block or
    // switches to the new one at the start of the next block; it never sees a half-built track.
    void load_track_mapping(const std::string& config_file);
    void trigger_song(uint32_t song_id);
    void stop_music();
    // True when a mapping exists for `song_id` (does not load anything).
    bool has_track(uint32_t song_id) const;
    // Song id currently requested to play (0 when stopped). Control-side view.
    uint32_t current_song_id() const { return current_song_id_.load(std::memory_order_acquire); }

    // Mix active HD audio into a device buffer. `sample_count` is the total number of
    // int16 values; `channels` must be 1 or 2 for the HD track to be mixed (other
    // layouts only get the SFX volume applied). The track is stored as interleaved
    // stereo at the rate it was loaded for (HdAudioSettings::target_sample_rate); it is
    // folded down to mono for a mono buffer, and resampled on the fly (linear
    // interpolation) when `sample_rate` differs from that rate. The native audio already
    // in the buffer is scaled by sfx * master volume first.
    // Real-time safe: no allocation, no locks, no file access. Single consumer: if two
    // threads call it at once, the second returns at once with its buffer untouched.
    void mix_audio(int16_t* buffer, size_t sample_count, int channels, int sample_rate);

private:
    struct Track {
        std::vector<int16_t> pcm;   // interleaved stereo at `rate`
        int rate = 65536;
        uint32_t song_id = 0;
    };

    void load_audio_file(uint32_t song_id, const std::string& filepath);
    void publish(Track* t);             // hand a track (or the stop marker) to the audio thread
    void collect_garbage();             // free tracks the audio thread has retired
    void retire(Track* t);              // audio thread: queue a replaced track for the control side

    HdAudioSettings settings_;

    // Control side.
    mutable std::mutex control_mutex_;  // guards track_map_ and serializes trigger/stop
    std::unordered_map<uint32_t, std::string> track_map_;
    std::unordered_set<uint32_t> failed_ids_;   // mapped ids whose file could not be loaded
    std::atomic<uint32_t> current_song_id_{0};
    std::atomic<bool> is_playing_{false};

    // Control -> audio hand-off. `pending_` owns whatever it points to until the audio thread
    // takes it with exchange(); the stop marker means "stop", nullptr means "no change".
    Track stop_marker_;
    std::atomic<Track*> pending_{nullptr};
    static constexpr int kGarbageSlots = 8;
    std::atomic<Track*> garbage_[kGarbageSlots];

    // Audio side (touched only while holding the busy flag).
    Track* audio_track_ = nullptr;
    double play_pos_ = 0.0;             // playback position in track frames
    std::atomic<bool> busy_{false};
};

} // namespace gbaimp
