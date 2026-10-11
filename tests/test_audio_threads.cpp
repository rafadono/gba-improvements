// Concurrency stress test for the audio path: the "audio thread" runs the same code the SDL
// trampoline runs (runtime callback + process_audio_block) while a "game thread" hammers the
// control side (settings, presets, track switching, mapping reload). Meant to be run under
// ThreadSanitizer (-DGBAIMP_SANITIZE=thread) but it also checks functional invariants without it.
#include "test_harness.h"
#include "gbaimp/audio_device.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/audio_pipeline.h"
#include "gbaimp/hd_audio_player.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

using namespace gbaimp;

namespace {

void write_wav(const std::string& path, int rate, int frames, int16_t value) {
    const uint32_t data_bytes = uint32_t(frames) * 4;
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + data_bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate) * 4); u16(4); u16(16);
    f.write("data", 4); u32(data_bytes);
    for (int i = 0; i < frames; ++i) { u16(uint16_t(value)); u16(uint16_t(value)); }
}

void SDLCALL runtime_cb(void*, Uint8* stream, int len) {
    auto* s = reinterpret_cast<int16_t*>(stream);
    for (int i = 0; i < len / 2; ++i) s[i] = static_cast<int16_t>(((i * 37) % 2000) - 1000);
}

} // namespace

int main() {
    testh::TempDir tmp("audiothreads");
    tmp.enter();

    const std::string w1 = tmp.file("a.wav"), w2 = tmp.file("b.wav");
    write_wav(w1, 65536, 20000, 4000);
    write_wav(w2, 32768, 9000, -3000);
    const std::string ini = tmp.file("t.ini");
    {
        std::ofstream f(ini);
        f << "[tracks]\n1 = " << w1 << "\n2 = " << w2 << "\n3 = " << tmp.file("missing.wav") << "\n";
    }

    auto& dsp = AudioDsp::instance();
    auto& hd = HdAudioPlayer::instance();
    hd.settings().target_sample_rate = 65536;
    hd.load_track_mapping(ini);
    hd.set_enabled(true);

    // A missing file for a mapped id must stop HD playback and stay silent (no crash, no leftover).
    hd.trigger_song(1);
    hd.trigger_song(3);
    CHECK_EQ(hd.current_song_id(), 0u);
    {
        std::vector<int16_t> b(64, 0);
        hd.mix_audio(b.data(), b.size(), 1, 65536);
        for (int16_t s : b) CHECK_EQ(int(s), 0);
    }

    // A failed id is remembered: creating the file later does not matter until the mapping is reloaded.
    {
        const std::string late = tmp.file("late.wav");
        const std::string ini4 = tmp.file("t4.ini");
        { std::ofstream f(ini4); f << "[tracks]\n4 = " << late << "\n"; }
        hd.load_track_mapping(ini4);
        CHECK(hd.has_track(4));
        hd.trigger_song(4);
        CHECK_EQ(hd.current_song_id(), 0u);
        write_wav(late, 65536, 1000, 500);
        hd.trigger_song(4);
        CHECK_EQ(hd.current_song_id(), 0u);        // negative result cached
        hd.load_track_mapping(ini4);               // reload forgets it
        hd.trigger_song(4);
        CHECK_EQ(hd.current_song_id(), 4u);
        hd.stop_music();
        hd.load_track_mapping(ini);
    }

    SDL_AudioSpec spec{};
    spec.freq = 65536; spec.format = AUDIO_S16SYS; spec.channels = 1; spec.samples = 1024;
    spec.callback = &runtime_cb;
    AudioDeviceWrap* wrap = wrap_audio_device_spec(&spec);
    CHECK(wrap != nullptr);
    finish_audio_device_wrap(wrap, 1, nullptr);
    SDL_AudioCallback cb = spec.callback;
    void* ud = spec.userdata;

    std::atomic<bool> stop{false};
    std::atomic<long> blocks{0};
    std::atomic<long> bad{0};

    auto audio_thread = [&] {
        std::vector<int16_t> buf(1024);
        while (!stop.load(std::memory_order_acquire)) {
            cb(ud, reinterpret_cast<Uint8*>(buf.data()), int(buf.size() * 2));
            blocks.fetch_add(1, std::memory_order_relaxed);
            // Sanity only: int16 values cannot be out of range, but NaN-poisoned filters would
            // show up as a full-scale stuck output.
            long stuck = 0;
            for (int16_t s : buf) if (s == 32767 || s == -32768) ++stuck;
            if (stuck > 900) bad.fetch_add(1, std::memory_order_relaxed);
        }
    };
    // A second thread that also feeds the pipeline: the single-consumer guard must make this safe
    // (the loser of a race leaves its buffer alone).
    auto rogue_thread = [&] {
        std::vector<int16_t> buf(256, 100);
        while (!stop.load(std::memory_order_acquire)) {
            process_audio_block(buf.data(), buf.size(), 1, 65536);
            process_audio_block(buf.data(), buf.size(), 2, 48000);
        }
    };

    std::thread at(audio_thread), rt(rogue_thread);

    const EqPreset presets[] = {EqPreset::Flat, EqPreset::WarmRetro, EqPreset::CrispModern, EqPreset::BassBoost};
    for (int i = 0; i < 1500; ++i) {
        switch (i % 12) {
            case 0: dsp.apply_preset(presets[(i / 12) % 4]); break;
            case 1: dsp.set_stereo_width(float(i % 5) * 0.4f); break;
            case 2: dsp.set_anti_aliasing((i / 12) & 1); break;
            case 3: dsp.set_limiter((i / 12) & 1); break;
            case 4: hd.trigger_song(1); break;
            case 5: hd.trigger_song(2); break;
            case 6: hd.set_bgm_volume(float(i % 7) * 0.2f); hd.set_sfx_volume(float(i % 3) * 0.5f); break;
            case 7: hd.trigger_song(3); break;                 // missing file: stops
            case 8: hd.stop_music(); break;
            case 9: dsp.settings().bass_gain_db = float(i % 9) - 4.0f; dsp.settings().enabled = (i / 12) % 7 != 0; break;
            case 10: hd.set_enabled((i / 12) % 5 != 0); break;
            default: if (i % 120 == 11) hd.load_track_mapping(ini); else hd.set_master_volume(0.5f + float(i % 4) * 0.25f); break;
        }
        if (i % 16 == 0) std::this_thread::yield();
    }

    // Wait until the audio thread is demonstrably still running, then stop both.
    const long before = blocks.load();
    while (blocks.load() < before + 20) std::this_thread::yield();
    stop.store(true, std::memory_order_release);
    at.join();
    rt.join();
    CHECK(blocks.load() > 50);
    CHECK_EQ(bad.load(), 0L);

    // Deterministic end state after the storm: settings back to neutral, the last trigger wins.
    dsp.settings().enabled = true;
    dsp.apply_preset(EqPreset::Flat);
    dsp.set_limiter(false);
    dsp.reset();   // filter histories from the storm would add a decaying transient
    hd.set_enabled(true);
    hd.set_master_volume(1.0f);
    hd.set_bgm_volume(1.0f);
    hd.set_sfx_volume(1.0f);
    hd.stop_music();
    hd.trigger_song(1);
    {
        std::vector<int16_t> b(128, 0);
        process_audio_block(b.data(), b.size(), 1, 65536);
        for (int16_t s : b) CHECK_NEAR(int(s), 4000, 2);
        if (testh::failures()) std::fprintf(stderr, "  first samples: %d %d %d %d (song id %u)\n", b[0], b[1], b[2], b[127], hd.current_song_id());
    }
    hd.trigger_song(2);   // a different track replaces it atomically from the audio thread's view
    {
        std::vector<int16_t> b(128, 0);
        process_audio_block(b.data(), b.size(), 1, 65536);
        // Track B (-3000, converted up from 32768 Hz; the resampler needs a few frames to settle).
        CHECK(std::abs(int(b[100]) + 3000) <= 100);
        CHECK(std::abs(int(b[127]) + 3000) <= 100);
    }

    std::printf("[audio_threads] audio blocks during the storm: %ld\n", blocks.load());
    return testh::finish("audio_threads");
}
