#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_AudioSpec;

namespace gbaimp {

struct HdAudioSettings {
    bool enabled = true;
    float master_volume = 1.0f;
    float bgm_volume = 0.85f;
    float sfx_volume = 1.0f;
    std::string config_path = "config/music_tracks.ini";
    // Output sample rate (Hz) of the host audio pipeline; loaded tracks are converted to it
    int target_sample_rate = 65536;
};

class HdAudioPlayer {
public:
    static HdAudioPlayer& instance();

    const HdAudioSettings& settings() const { return settings_; }
    HdAudioSettings& settings() { return settings_; }

    void set_enabled(bool enabled);
    void set_bgm_volume(float vol);
    void set_sfx_volume(float vol);
    void set_master_volume(float vol);

    void load_track_mapping(const std::string& config_file);
    void trigger_song(uint32_t song_id);
    void stop_music();

    // Mix active HD audio into the SDL push buffer
    void mix_audio(int16_t* buffer, size_t sample_count, int channels, int sample_rate);

private:
    HdAudioPlayer();
    ~HdAudioPlayer();

    void load_audio_file(uint32_t song_id, const std::string& filepath);

    HdAudioSettings settings_;
    std::unordered_map<uint32_t, std::string> track_map_;
    
    // Active track audio buffer
    uint32_t current_song_id_ = 0;
    std::vector<int16_t> current_pcm_;
    size_t playback_cursor_ = 0;
    bool is_playing_ = false;
};

} // namespace gbaimp
