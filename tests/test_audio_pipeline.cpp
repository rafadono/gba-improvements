#include "test_harness.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/audio_pipeline.h"
#include "gbaimp/hd_audio_player.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace gbaimp;

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
        const int16_t s = (i < frames_on) ? value : 0;
        u16(uint16_t(s)); u16(uint16_t(s));
    }
}

int main() {
    testh::TempDir tmp("pipeline");
    tmp.enter();

    auto& dsp = AudioDsp::instance();
    auto& hd = HdAudioPlayer::instance();
    dsp.settings().enabled = true;
    dsp.apply_preset(EqPreset::Flat);
    dsp.set_limiter(true);
    hd.set_enabled(false);                       // no HD track: pipeline = volume + DSP only

    // Mono silence stays silent; stereo too.
    {
        std::vector<int16_t> m(512, 0), s(512, 0);
        process_audio_block(m.data(), m.size(), 1, 65536);
        process_audio_block(s.data(), s.size(), 2, 65536);
        CHECK(std::all_of(m.begin(), m.end(), [](int16_t v) { return v == 0; }));
        CHECK(std::all_of(s.begin(), s.end(), [](int16_t v) { return v == 0; }));
    }

    // Flat preset is transparent in mono and stereo.
    {
        std::vector<int16_t> orig;
        for (int i = 0; i < 512; ++i) orig.push_back(int16_t((i % 50) * 120 - 3000));
        std::vector<int16_t> m = orig, s = orig;
        process_audio_block(m.data(), m.size(), 1, 65536);
        process_audio_block(s.data(), s.size(), 2, 65536);
        int em = 0, es = 0;
        for (size_t i = 0; i < orig.size(); ++i) {
            em = std::max(em, std::abs(m[i] - orig[i]));
            es = std::max(es, std::abs(s[i] - orig[i]));
        }
        CHECK(em <= 1);
        CHECK(es <= 1);
    }

    // The 2-argument form is stereo processing: with width 0 it folds L/R together,
    // whereas the mono form ignores the width and leaves a mono signal as it is.
    {
        dsp.apply_preset(EqPreset::Flat);
        dsp.set_stereo_width(0.0f);
        std::vector<int16_t> st = {1000, -1000, 3000, 1000};
        process_audio_block(st.data(), st.size());
        CHECK(std::abs(int(st[0]) - int(st[1])) <= 1);
        CHECK(std::abs(int(st[2]) - 2000) <= 2);
        std::vector<int16_t> mono = {1000, -1000, 3000, 1000};
        process_audio_block(mono.data(), mono.size(), 1, 65536);
        CHECK(std::abs(int(mono[0]) - 1000) <= 1);
        CHECK(std::abs(int(mono[1]) + 1000) <= 1);
        dsp.set_stereo_width(1.0f);
    }

    // Unsupported channel counts, null and empty blocks are left alone.
    {
        std::vector<int16_t> v = {100, -200, 300, -400, 500, -600};
        const auto orig = v;
        process_audio_block(v.data(), v.size(), 6, 65536);
        process_audio_block(v.data(), v.size(), 0, 65536);
        process_audio_block(v.data(), 0, 1, 65536);
        process_audio_block(nullptr, 10, 1, 65536);
        process_audio_block(nullptr, 10);
        CHECK(v == orig);
    }

    // HD track through the pipeline into a MONO buffer at a device rate different from the
    // rate the track was converted to.
    {
        dsp.apply_preset(EqPreset::Flat);
        dsp.set_limiter(false);
        const std::string wav = tmp.file("t.wav");
        write_wav(wav, 32768, 100, 100, 8000);
        const std::string ini = tmp.file("t.ini");
        { std::ofstream f(ini); f << "[tracks]\n1 = " << wav << "\n"; }
        hd.settings().target_sample_rate = 32768;
        hd.load_track_mapping(ini);
        hd.set_enabled(true);
        hd.set_bgm_volume(1.0f);
        hd.set_sfx_volume(1.0f);
        hd.set_master_volume(1.0f);
        hd.trigger_song(1);

        std::vector<int16_t> buf(260, 0);
        process_audio_block(buf.data(), buf.size(), 1, 65536);   // device at 2x the track rate
        size_t loud = 0;
        for (int16_t s : buf) if (s >= 7000) ++loud;
        CHECK(std::abs(int(loud) - 200) <= 3);
        CHECK(std::abs(int(buf[10]) - 8000) <= 1);

        // 2-argument form: stereo at the target rate, track plays 1:1.
        hd.stop_music();
        hd.trigger_song(1);
        std::vector<int16_t> st(2 * 120, 0);
        process_audio_block(st.data(), st.size());
        CHECK(std::abs(int(st[0]) - 8000) <= 1);
        CHECK(std::abs(int(st[199]) - 8000) <= 1);
        CHECK(std::abs(int(st[200])) <= 1);
        hd.stop_music();
        hd.set_enabled(false);
    }

    return testh::finish("audio_pipeline");
}
