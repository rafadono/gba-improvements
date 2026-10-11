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
    for (auto& g : garbage_) g.store(nullptr, std::memory_order_relaxed);
    load_track_mapping(settings_.config_path);
}

HdAudioPlayer::~HdAudioPlayer() {
    // The owner guarantees no thread is inside mix_audio() any more.
    Track* p = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (p && p != &stop_marker_) delete p;
    delete audio_track_;
    audio_track_ = nullptr;
    collect_garbage();
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
    std::lock_guard<std::mutex> lock(control_mutex_);
    track_map_.clear();
    failed_ids_.clear();
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

void HdAudioPlayer::publish(Track* t) {
    collect_garbage();
    Track* prev = pending_.exchange(t, std::memory_order_acq_rel);
    // `prev` was never taken by the audio thread (exchange transfers ownership), so it is ours.
    if (prev && prev != &stop_marker_) delete prev;
}

void HdAudioPlayer::collect_garbage() {
    for (auto& slot : garbage_) {
        Track* g = slot.exchange(nullptr, std::memory_order_acq_rel);
        delete g;
    }
}

void HdAudioPlayer::retire(Track* t) {
    if (!t) return;
    for (auto& slot : garbage_) {
        Track* expected = nullptr;
        if (slot.compare_exchange_strong(expected, t, std::memory_order_acq_rel)) return;
    }
    // Every slot is occupied (the control side has not collected in a long while): leak this
    // one track rather than free() on the audio thread. Cannot happen in practice: each
    // retirement is caused by one publish, and each publish empties the slots first.
}

// Decodes `filepath` and publishes the track; on any failure the HD track is stopped
// (never leaves a previous song playing under the new id). Silent: a missing file is the
// normal state of a mapping whose WAVs the user has not supplied.
void HdAudioPlayer::load_audio_file(uint32_t song_id, const std::string& filepath) {
    auto stop_locked = [&]() {
        failed_ids_.insert(song_id);
        publish(&stop_marker_);
        is_playing_.store(false, std::memory_order_release);
        current_song_id_.store(0, std::memory_order_release);
    };

    SDL_AudioSpec wav_spec;
    Uint8* wav_buffer = nullptr;
    Uint32 wav_length = 0;
    if (!SDL_LoadWAV(filepath.c_str(), &wav_spec, &wav_buffer, &wav_length)) {
        stop_locked();
        return;
    }

    // Convert loaded WAV to 16-bit stereo at the configured target sample rate
    const int target_rate = std::max(1, settings_.target_sample_rate.load());
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq,
                          AUDIO_S16SYS, 2, target_rate) < 0) {
        SDL_FreeWAV(wav_buffer);
        stop_locked();
        return;
    }

    cvt.len = wav_length;
    cvt.buf = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(cvt.len) * cvt.len_mult));
    if (!cvt.buf) {
        SDL_FreeWAV(wav_buffer);
        stop_locked();
        return;
    }

    std::memcpy(cvt.buf, wav_buffer, wav_length);
    SDL_FreeWAV(wav_buffer);

    if (SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(cvt.buf);
        stop_locked();
        return;
    }

    const int16_t* converted_samples = reinterpret_cast<const int16_t*>(cvt.buf);
    const size_t sample_count = static_cast<size_t>(cvt.len_cvt) / sizeof(int16_t);

    Track* t = new Track;
    t->pcm.assign(converted_samples, converted_samples + sample_count);
    t->rate = target_rate;
    t->song_id = song_id;
    SDL_free(cvt.buf);

    publish(t);
    current_song_id_.store(song_id, std::memory_order_release);
    is_playing_.store(true, std::memory_order_release);
}

bool HdAudioPlayer::has_track(uint32_t song_id) const {
    std::lock_guard<std::mutex> lock(control_mutex_);
    return track_map_.find(song_id) != track_map_.end();
}

void HdAudioPlayer::trigger_song(uint32_t song_id) {
    if (!settings_.enabled) return;
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (song_id == current_song_id_.load(std::memory_order_acquire) &&
        is_playing_.load(std::memory_order_acquire)) return;

    auto it = track_map_.find(song_id);
    if (it != track_map_.end() && failed_ids_.count(song_id) == 0) {
        load_audio_file(song_id, it->second);
    } else {
        publish(&stop_marker_);
        is_playing_.store(false, std::memory_order_release);
        current_song_id_.store(0, std::memory_order_release);
    }
}

void HdAudioPlayer::stop_music() {
    std::lock_guard<std::mutex> lock(control_mutex_);
    publish(&stop_marker_);
    is_playing_.store(false, std::memory_order_release);
    current_song_id_.store(0, std::memory_order_release);
}

void HdAudioPlayer::mix_audio(int16_t* buffer, size_t sample_count, int channels, int sample_rate) {
    if (!buffer || sample_count == 0) return;

    struct Guard {
        std::atomic<bool>& f;
        bool owns;
        explicit Guard(std::atomic<bool>& flag) : f(flag), owns(!flag.exchange(true, std::memory_order_acquire)) {}
        ~Guard() { if (owns) f.store(false, std::memory_order_release); }
    } guard(busy_);
    if (!guard.owns) return;   // another thread is mixing: leave this buffer untouched

    const float master = settings_.master_volume;
    const float sfx_gain = settings_.sfx_volume * master;
    const float bgm_gain = settings_.bgm_volume * master;
    const bool enabled = settings_.enabled;

    // Adopt a track published by the control side (once per block, wait-free).
    if (Track* p = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
        Track* old = audio_track_;
        audio_track_ = (p == &stop_marker_) ? nullptr : p;
        play_pos_ = 0.0;
        retire(old);
    }

    // First scale native GBA audio by SFX volume
    if (std::abs(sfx_gain - 1.0f) > 0.01f) {
        for (size_t i = 0; i < sample_count; ++i) {
            float s = buffer[i] * sfx_gain;
            buffer[i] = static_cast<int16_t>(std::clamp(s, -32768.0f, 32767.0f));
        }
    }

    if (!enabled || !audio_track_ || audio_track_->pcm.empty()) {
        return;
    }

    // The track is stored as interleaved stereo. Only mono and stereo device buffers
    // are mixed; other layouts just get the SFX scaling above.
    if (channels != 1 && channels != 2) return;
    const size_t pcm_frames = audio_track_->pcm.size() / 2;
    if (pcm_frames == 0) return;
    const int pcm_rate = audio_track_->rate;

    // Playback position is in track frames. When the device rate differs from the rate
    // the track was converted to, step through the track at the ratio and interpolate.
    const bool same_rate = (sample_rate <= 0 || sample_rate == pcm_rate);
    const double step = same_rate ? 1.0 : static_cast<double>(pcm_rate) / static_cast<double>(sample_rate);
    const size_t dev_frames = sample_count / static_cast<size_t>(channels);
    const int16_t* pcm = audio_track_->pcm.data();

    double pos = play_pos_;
    if (!(pos >= 0.0) || pos >= static_cast<double>(pcm_frames)) pos = 0.0;

    for (size_t f = 0; f < dev_frames; ++f) {
        size_t i0 = static_cast<size_t>(pos);
        if (i0 >= pcm_frames) i0 = pcm_frames - 1;
        float l = pcm[i0 * 2];
        float r = pcm[i0 * 2 + 1];
        if (!same_rate) {
            const float frac = static_cast<float>(pos - static_cast<double>(i0));
            const size_t i1 = (i0 + 1 >= pcm_frames) ? 0 : i0 + 1;   // wrap: the track loops
            l += (pcm[i1 * 2] - l) * frac;
            r += (pcm[i1 * 2 + 1] - r) * frac;
        }

        if (channels == 2) {
            const size_t k = f * 2;
            buffer[k]     = static_cast<int16_t>(std::clamp(buffer[k] + l * bgm_gain, -32768.0f, 32767.0f));
            buffer[k + 1] = static_cast<int16_t>(std::clamp(buffer[k + 1] + r * bgm_gain, -32768.0f, 32767.0f));
        } else {
            const float m = 0.5f * (l + r);            // stereo track folded down to mono
            buffer[f] = static_cast<int16_t>(std::clamp(buffer[f] + m * bgm_gain, -32768.0f, 32767.0f));
        }

        pos += step;
        while (pos >= static_cast<double>(pcm_frames)) pos -= static_cast<double>(pcm_frames);   // loop seamlessly
    }
    play_pos_ = pos;
}

} // namespace gbaimp
