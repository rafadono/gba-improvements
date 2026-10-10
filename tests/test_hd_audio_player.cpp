#include "test_harness.h"
#include "gbaimp/hd_audio_player.h"
#include <cstring>
#include <fstream>
#include <vector>

using namespace gbaimp;

// Writes a 16-bit stereo PCM WAV: `frames_on` frames of `value`, then `frames_off` frames of silence.
static void write_wav(const std::string& path, int rate, int frames_on, int frames_off, int16_t value) {
    const int frames = frames_on + frames_off;
    const uint32_t data_bytes = uint32_t(frames) * 4;
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + data_bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate) * 4); u16(4); u16(16);
    f.write("data", 4); u32(data_bytes);
    for (int i = 0; i < frames; ++i) {
        int16_t s = (i < frames_on) ? value : 0;
        u16(uint16_t(s)); u16(uint16_t(s));
    }
}

static size_t count_nonzero(const std::vector<int16_t>& v) {
    size_t n = 0;
    for (int16_t s : v) if (s != 0) ++n;
    return n;
}

int main() {
    testh::TempDir tmp("hdaudio");
    tmp.enter();
    auto& hd = HdAudioPlayer::instance();

    // Defaults.
    CHECK(hd.settings().enabled);
    CHECK_EQ(hd.settings().target_sample_rate, 65536);
    CHECK(hd.settings().config_path == "config/music_tracks.ini");

    // Volume setters clamp to [0, 1.5].
    hd.set_master_volume(-1.0f); CHECK_NEAR(hd.settings().master_volume, 0.0f, 1e-6f);
    hd.set_master_volume(9.0f);  CHECK_NEAR(hd.settings().master_volume, 1.5f, 1e-6f);
    hd.set_bgm_volume(9.0f);     CHECK_NEAR(hd.settings().bgm_volume, 1.5f, 1e-6f);
    hd.set_sfx_volume(-2.0f);    CHECK_NEAR(hd.settings().sfx_volume, 0.0f, 1e-6f);
    hd.set_master_volume(1.0f);
    hd.set_bgm_volume(1.0f);
    hd.set_sfx_volume(1.0f);

    // WAV fixtures: 100 frames loud + 100 silent at 32768 Hz.
    const std::string wav_a = tmp.file("a.wav");
    write_wav(wav_a, 32768, 100, 100, 10000);

    // ini with sections, comments (# and ;), trailing comments, bad keys, and
    // entries outside [tracks] that must be ignored.
    const std::string ini = tmp.file("tracks.ini");
    {
        std::ofstream f(ini);
        f << "# header comment\n"
          << "5 = " << wav_a << "   ; trailing comment\n"
          << "[other]\n"
          << "9 = " << wav_a << "\n"
          << "[tracks]\n"
          << "; 7 = " << wav_a << "\n"
          << "# 8 = " << wav_a << "\n"
          << "  5   =   " << wav_a << "  \n"
          << "notanumber = " << wav_a << "\n"
          << "6 =\n"
          << "7\n";
    }

    // Missing config file is safe.
    hd.load_track_mapping(tmp.file("does_not_exist.ini"));

    hd.settings().target_sample_rate = 65536;
    hd.load_track_mapping(ini);

    // Mapped track plays.
    hd.trigger_song(5);
    {
        std::vector<int16_t> buf(380, 0);
        hd.mix_audio(buf.data(), buf.size(), 2, 65536);
        CHECK(count_nonzero(buf) >= 340);        // 100 source frames -> ~200 frames at 2x rate
        CHECK_NEAR(int(buf[100]), 10000, 150);   // source level * bgm 1.0 * master 1.0 (resampler tolerance)
    }

    // Unmapped / commented / wrong-section / empty-value ids do not play.
    for (uint32_t id : {7u, 8u, 9u, 6u, 42u}) {
        hd.trigger_song(id);
        std::vector<int16_t> buf(64, 0);
        hd.mix_audio(buf.data(), buf.size(), 2, 65536);
        CHECK_EQ(count_nonzero(buf), size_t(0));
    }

    // Native audio is only scaled by sfx*master when no HD track plays.
    hd.set_sfx_volume(0.5f);
    {
        std::vector<int16_t> buf(8, 1000);
        hd.mix_audio(buf.data(), buf.size(), 2, 65536);
        for (int16_t s : buf) CHECK_NEAR(int(s), 500, 1);
    }
    hd.set_sfx_volume(1.0f);

    // target_sample_rate controls the conversion: same source, half the output rate -> half the length.
    hd.settings().target_sample_rate = 32768;
    hd.load_track_mapping(ini);
    hd.stop_music();
    hd.trigger_song(5);
    {
        std::vector<int16_t> buf(380, 0);
        hd.mix_audio(buf.data(), buf.size(), 2, 32768);
        CHECK(count_nonzero(buf) <= 260);        // ~200 loud samples, then silence
        CHECK(count_nonzero(buf) >= 150);
    }

    // Disabling stops music and trigger_song becomes a no-op.
    hd.set_enabled(false);
    hd.trigger_song(5);
    {
        std::vector<int16_t> buf(64, 0);
        hd.mix_audio(buf.data(), buf.size(), 2, 32768);
        CHECK_EQ(count_nonzero(buf), size_t(0));
    }
    hd.set_enabled(true);

    // Null buffer is safe.
    hd.mix_audio(nullptr, 100, 2, 32768);

    return testh::finish("hd_audio_player");
}
