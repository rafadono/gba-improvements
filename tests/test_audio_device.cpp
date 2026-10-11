#include "test_harness.h"
#include "gbaimp/audio_device.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/sdl_interpose.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

// Linux: this executable is linked with -Wl,--wrap=SDL_* (see CMakeLists.txt) and expands the
// shim macro once. Windows: install_sdl_interposers() patches the IAT of this executable.
#if !defined(_WIN32)
GBAIMP_DEFINE_LD_WRAPS()
#endif

using namespace gbaimp;

namespace {

// Pull callback standing in for a runtime's: fills the buffer with a constant, counts calls.
struct FakeRuntime {
    std::atomic<int> calls{0};
    int16_t value = 2000;
};

void SDLCALL fake_callback(void* userdata, Uint8* stream, int len) {
    auto* rt = static_cast<FakeRuntime*>(userdata);
    rt->calls.fetch_add(1);
    auto* s = reinterpret_cast<int16_t*>(stream);
    for (int i = 0; i < len / 2; ++i) s[i] = rt->value;
}

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

void make_transparent() {
    auto& dsp = AudioDsp::instance();
    dsp.settings().enabled = true;
    dsp.apply_preset(EqPreset::Flat);
    dsp.set_limiter(false);
    dsp.reset();
    auto& hd = HdAudioPlayer::instance();
    hd.set_enabled(false);
    hd.set_master_volume(1.0f);
    hd.set_bgm_volume(1.0f);
    hd.set_sfx_volume(1.0f);
}

} // namespace

int main() {
    SDL_SetMainReady();
    testh::TempDir tmp("audiodev");
    tmp.enter();
    make_transparent();

    // ---------------------------------------------------------------------------------
    // wrap_audio_device_spec: which specs are wrapped, and what the trampoline does
    // ---------------------------------------------------------------------------------
    {
        FakeRuntime rt;
        // Push mode (no callback): untouched.
        SDL_AudioSpec push{};
        push.freq = 65536; push.format = AUDIO_S16SYS; push.channels = 1; push.samples = 1024;
        CHECK(wrap_audio_device_spec(&push) == nullptr);
        CHECK(push.callback == nullptr);

        // Other sample formats and channel counts: untouched.
        SDL_AudioSpec f32{};
        f32.freq = 48000; f32.format = AUDIO_F32SYS; f32.channels = 2; f32.samples = 512;
        f32.callback = &fake_callback; f32.userdata = &rt;
        CHECK(wrap_audio_device_spec(&f32) == nullptr);
        CHECK(f32.callback == &fake_callback);
        CHECK(f32.userdata == &rt);

        SDL_AudioSpec six = f32;
        six.format = AUDIO_S16SYS; six.channels = 6;
        CHECK(wrap_audio_device_spec(&six) == nullptr);
        CHECK(six.callback == &fake_callback);

        // Mono S16 callback: wrapped; the original callback and userdata are preserved.
        SDL_AudioSpec mono{};
        mono.freq = 65536; mono.format = AUDIO_S16SYS; mono.channels = 1; mono.samples = 1024;
        mono.callback = &fake_callback; mono.userdata = &rt;
        AudioDeviceWrap* w = wrap_audio_device_spec(&mono);
        CHECK(w != nullptr);
        CHECK(mono.callback != &fake_callback);
        CHECK(mono.userdata != &rt);

        // Until the open is confirmed the trampoline only forwards (nothing processed).
        HdAudioPlayer::instance().set_sfx_volume(0.5f);
        std::vector<int16_t> buf(256, 0);
        mono.callback(mono.userdata, reinterpret_cast<Uint8*>(buf.data()), int(buf.size() * 2));
        CHECK_EQ(rt.calls.load(), 1);
        CHECK_EQ(int(buf[0]), 2000);

        // A failed open disables it for good.
        finish_audio_device_wrap(w, 0, nullptr);
        mono.callback(mono.userdata, reinterpret_cast<Uint8*>(buf.data()), int(buf.size() * 2));
        CHECK_EQ(int(buf[10]), 2000);

        // Confirmed (obtained spec given): the runtime's callback runs first, then the pipeline.
        SDL_AudioSpec got = mono;
        got.freq = 48000;
        finish_audio_device_wrap(w, 7, &got);
        reset_audio_wrap_stats();
        mono.callback(mono.userdata, reinterpret_cast<Uint8*>(buf.data()), int(buf.size() * 2));
        CHECK_EQ(rt.calls.load(), 3);
        for (int16_t s : buf) CHECK_NEAR(int(s), 1000, 1);        // sfx volume 0.5, flat DSP
        AudioWrapStats st = audio_wrap_stats();
        CHECK_EQ(st.callbacks, uint64_t(1));
        CHECK_EQ(st.frames_processed, uint64_t(256));

        // Odd byte counts are processed in whole frames, never out of bounds.
        std::vector<int16_t> small(5, 0);
        mono.callback(mono.userdata, reinterpret_cast<Uint8*>(small.data()), 9);
        for (int i = 0; i < 4; ++i) CHECK_NEAR(int(small[i]), 1000, 1);

        // Stereo S16 callback: processed as 2 channels.
        SDL_AudioSpec stereo = mono;
        stereo.channels = 2;
        stereo.callback = &fake_callback; stereo.userdata = &rt;
        AudioDeviceWrap* ws = wrap_audio_device_spec(&stereo);
        CHECK(ws != nullptr);
        finish_audio_device_wrap(ws, 8, nullptr);   // null obtained: wanted values hold
        std::vector<int16_t> sb(200, 0);
        stereo.callback(stereo.userdata, reinterpret_cast<Uint8*>(sb.data()), int(sb.size() * 2));
        for (int16_t s : sb) CHECK_NEAR(int(s), 1000, 1);

        HdAudioPlayer::instance().set_sfx_volume(1.0f);
    }

    // The HD track reaches the device buffer through the trampoline (mono, odd device rate).
    {
        const std::string wav = tmp.file("hd.wav");
        write_wav(wav, 48000, 4800, 6000);
        const std::string ini = tmp.file("hd.ini");
        { std::ofstream f(ini); f << "[tracks]\n9 = " << wav << "\n"; }
        auto& hd = HdAudioPlayer::instance();
        hd.settings().target_sample_rate = 48000;
        hd.load_track_mapping(ini);
        hd.set_enabled(true);

        FakeRuntime rt;
        rt.value = 0;
        SDL_AudioSpec spec{};
        spec.freq = 48000; spec.format = AUDIO_S16SYS; spec.channels = 1; spec.samples = 512;
        spec.callback = &fake_callback; spec.userdata = &rt;
        AudioDeviceWrap* w = wrap_audio_device_spec(&spec);
        CHECK(w != nullptr);
        finish_audio_device_wrap(w, 3, &spec);   // (spec now holds the trampoline; freq/channels are what matter)
        std::vector<int16_t> buf(300, 0);
        hd.trigger_song(9);
        spec.callback(spec.userdata, reinterpret_cast<Uint8*>(buf.data()), int(buf.size() * 2));
        for (int16_t s : buf) CHECK_NEAR(int(s), 6000, 2);
        hd.stop_music();
        spec.callback(spec.userdata, reinterpret_cast<Uint8*>(buf.data()), int(buf.size() * 2));
        for (int16_t s : buf) CHECK_NEAR(int(s), 0, 1);
        hd.set_enabled(false);
    }

    // ---------------------------------------------------------------------------------
    // End to end through SDL: interposed SDL_OpenAudioDevice, SDL's own audio thread
    // ---------------------------------------------------------------------------------
    SdlHandlers h;
    h.open_audio_device = &open_audio_device_processed;
    h.queue_audio = &queue_audio_processed;
#if defined(_WIN32)
    SdlInstallStatus ist;
    CHECK(install_sdl_interposers(h, &ist));
    CHECK(ist.slots_patched >= 2);   // OpenAudioDevice and QueueAudio imported by this exe
#else
    set_sdl_handlers(h);
#endif
    CHECK_EQ(SDL_InitSubSystem(SDL_INIT_AUDIO), 0);

    make_transparent();
    HdAudioPlayer::instance().set_sfx_volume(0.5f);   // processed output = half of the runtime's

    {
        FakeRuntime rt;
        rt.value = 2000;
        SDL_AudioSpec want{};
        want.freq = 65536; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
        want.callback = &fake_callback; want.userdata = &rt;
        SDL_AudioSpec have{};
        reset_audio_wrap_stats();
        const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        CHECK(dev != 0);
        CHECK_EQ(int(have.channels), 1);
        CHECK(audio_wrap_stats().devices_wrapped >= 1);
        // The caller's spec must not have been modified by the wrapper.
        CHECK(want.callback == &fake_callback);
        CHECK(want.userdata == &rt);

        set_audio_metering(true);
        SDL_PauseAudioDevice(dev, 0);
        const auto t0 = std::chrono::steady_clock::now();
        while (audio_wrap_stats().callbacks < 12 &&
               std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        SDL_PauseAudioDevice(dev, 1);
        SDL_CloseAudioDevice(dev);   // joins the audio thread
        set_audio_metering(false);

        const AudioWrapStats st = audio_wrap_stats();
        CHECK(st.callbacks >= 12);
        CHECK(rt.calls.load() >= 12);                 // the runtime's own callback keeps running
        CHECK(st.frames_processed >= 12u * 100u);
        CHECK(st.metered_samples > 0);
        // Audible-data change, measured on the real audio thread: halved by the sfx volume.
        CHECK_NEAR(st.rms_before, 2000.0, 20.0);
        CHECK_NEAR(st.rms_after, 1000.0, 20.0);
        CHECK(st.peak_before == 2000);
        CHECK(st.peak_after >= 995 && st.peak_after <= 1005);
        std::printf("[audio_device] real device: callbacks=%llu frames=%llu rms %.1f -> %.1f peak %d -> %d\n",
                    (unsigned long long)st.callbacks, (unsigned long long)st.frames_processed,
                    st.rms_before, st.rms_after, st.peak_before, st.peak_after);
    }

    // Push mode through the same interposed open: the device is remembered and queued data
    // is processed with its real channel count; capture devices and unknown ids pass through.
    {
        SDL_AudioSpec want{};
        want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
        SDL_AudioSpec have{};
        reset_audio_wrap_stats();
        const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        CHECK(dev != 0);
        std::vector<int16_t> data(441, 1000);
        CHECK_EQ(SDL_QueueAudio(dev, data.data(), Uint32(data.size() * 2)), 0);
        const AudioWrapStats st = audio_wrap_stats();
        CHECK_EQ(st.queue_calls, uint64_t(1));
        CHECK_EQ(st.frames_processed, uint64_t(441));   // mono: one frame per int16
        CHECK_EQ(st.callbacks, uint64_t(0));            // no trampoline in push mode
        CHECK(SDL_QueueAudio(dev + 1000, data.data(), Uint32(data.size() * 2)) != 0);   // unknown id: real SDL error
        CHECK_EQ(audio_wrap_stats().queue_calls, uint64_t(1));
        SDL_CloseAudioDevice(dev);
    }

#if defined(_WIN32)
    uninstall_sdl_interposers();
#else
    set_sdl_handlers(SdlHandlers{});
#endif
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return testh::finish("audio_device");
}
