#include "gbaimp/hd_audio_player.h"
#include <SDL.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gbaimp {

HdAudioPlayer& HdAudioPlayer::instance() {
    static HdAudioPlayer s_instance;
    return s_instance;
}

HdAudioPlayer::HdAudioPlayer() {
    load_track_mapping(settings_.config_path);
}

HdAudioPlayer::~HdAudioPlayer() {
    stop_music();
}

void HdAudioPlayer::set_enabled(bool enabled) {
    settings_.enabled = enabled;
    if (!enabled) stop_music();
}

void HdAudioPlayer::set_bgm_volume(float vol) {
    settings_.bgm_volume = std::clamp(vol, 0.0f, 1.5f);
}

void HdAudioPlayer::set_sfx_volume(float vol) {
    settings_.sfx_volume = std::clamp(vol, 0.0f, 1.5f);
}

void HdAudioPlayer::set_master_volume(float vol) {
    settings_.master_volume = std::clamp(vol, 0.0f, 1.5f);
}

void HdAudioPlayer::load_track_mapping(const std::string& config_file) {
    track_map_.clear();
    std::ifstream file(config_file);
    if (!file.is_open()) return;

    std::string line;
    bool in_tracks_section = false;

    while (std::getline(file, line)) {
        // Strip whitespace and comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);
        comment_pos = line.find(';');
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);

        auto trim = [](std::string& s) {
            size_t p = s.find_first_not_of(" \t\r\n");
            size_t q = s.find_last_not_of(" \t\r\n");
            if (p == std::string::npos) s.clear();
            else s = s.substr(p, q - p + 1);
        };
        trim(line);
        if (line.empty()) continue;

        if (line[0] == '[' && line.back() == ']') {
            std::string sec = line.substr(1, line.size() - 2);
            trim(sec);
            in_tracks_section = (sec == "tracks");
            continue;
        }

        if (in_tracks_section) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                trim(k);
                trim(v);
                try {
                    uint32_t song_id = static_cast<uint32_t>(std::stoul(k));
                    if (!v.empty()) {
                        track_map_[song_id] = v;
                    }
                } catch (...) {}
            }
        }
    }
}

void HdAudioPlayer::load_audio_file(uint32_t song_id, const std::string& filepath) {
    SDL_AudioSpec wav_spec;
    Uint8* wav_buffer = nullptr;
    Uint32 wav_length = 0;

    if (!SDL_LoadWAV(filepath.c_str(), &wav_spec, &wav_buffer, &wav_length)) {
        return;
    }

    // Convert loaded WAV to 16-bit stereo at the configured target sample rate
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq,
                          AUDIO_S16SYS, 2, std::max(1, settings_.target_sample_rate)) < 0) {
        SDL_FreeWAV(wav_buffer);
        return;
    }

    cvt.len = wav_length;
    cvt.buf = static_cast<Uint8*>(SDL_malloc(cvt.len * cvt.len_mult));
    if (!cvt.buf) {
        SDL_FreeWAV(wav_buffer);
        return;
    }

    std::memcpy(cvt.buf, wav_buffer, wav_length);
    SDL_FreeWAV(wav_buffer);

    if (SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(cvt.buf);
        return;
    }

    const int16_t* converted_samples = reinterpret_cast<const int16_t*>(cvt.buf);
    size_t sample_count = cvt.len_cvt / sizeof(int16_t);

    current_pcm_.assign(converted_samples, converted_samples + sample_count);
    SDL_free(cvt.buf);

    current_song_id_ = song_id;
    playback_cursor_ = 0;
    is_playing_ = true;
}

void HdAudioPlayer::trigger_song(uint32_t song_id) {
    if (!settings_.enabled) return;
    if (song_id == current_song_id_ && is_playing_) return;

    auto it = track_map_.find(song_id);
    if (it != track_map_.end()) {
        load_audio_file(song_id, it->second);
    } else {
        stop_music();
    }
}

void HdAudioPlayer::stop_music() {
    is_playing_ = false;
    current_song_id_ = 0;
    playback_cursor_ = 0;
}

void HdAudioPlayer::mix_audio(int16_t* buffer, size_t sample_count, int channels, int sample_rate) {
    if (!buffer || sample_count == 0) return;

    const float sfx_gain = settings_.sfx_volume * settings_.master_volume;
    const float bgm_gain = settings_.bgm_volume * settings_.master_volume;

    // First scale native GBA audio by SFX volume
    if (std::abs(sfx_gain - 1.0f) > 0.01f) {
        for (size_t i = 0; i < sample_count; ++i) {
            float s = buffer[i] * sfx_gain;
            buffer[i] = static_cast<int16_t>(std::clamp(s, -32768.0f, 32767.0f));
        }
    }

    if (!settings_.enabled || !is_playing_ || current_pcm_.empty()) {
        return;
    }

    // Mix HD BGM track
    for (size_t i = 0; i < sample_count; ++i) {
        float native_s = buffer[i];
        float hd_s = current_pcm_[playback_cursor_] * bgm_gain;
        float mixed = native_s + hd_s;

        buffer[i] = static_cast<int16_t>(std::clamp(mixed, -32768.0f, 32767.0f));

        ++playback_cursor_;
        if (playback_cursor_ >= current_pcm_.size()) {
            playback_cursor_ = 0; // Loop seamlessly
        }
    }
}

} // namespace gbaimp
