#include "gbaimp/audio_device.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/audio_pipeline.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/sdl_interpose.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace gbaimp {

struct AudioDeviceWrap {
    SDL_AudioCallback original = nullptr;
    void* original_userdata = nullptr;
    std::atomic<int> channels{0};
    std::atomic<int> rate{0};
    std::atomic<bool> active{false};
};

namespace {

std::atomic<uint64_t> g_devices{0};
std::atomic<uint64_t> g_callbacks{0};
std::atomic<uint64_t> g_frames{0};
std::atomic<uint64_t> g_queue_calls{0};
std::atomic<bool> g_metering{false};
std::atomic<uint64_t> g_meter_samples{0};
std::atomic<uint64_t> g_sumsq_before{0};
std::atomic<uint64_t> g_sumsq_after{0};
std::atomic<int> g_peak_before{0};
std::atomic<int> g_peak_after{0};

inline void atomic_max(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

// Sum of squares and peak of a block (integer arithmetic only).
inline void measure(const int16_t* s, size_t n, uint64_t& sumsq, int& peak) {
    uint64_t acc = 0;
    int pk = 0;
    for (size_t i = 0; i < n; ++i) {
        const int v = s[i];
        acc += static_cast<uint64_t>(static_cast<int64_t>(v) * v);
        const int a = v < 0 ? -v : v;
        if (a > pk) pk = a;
    }
    sumsq = acc;
    peak = pk;
}

// Push-mode playback devices opened through open_audio_device_processed().
struct PushDevice {
    std::atomic<uint32_t> id{0};
    std::atomic<int> channels{0};
    std::atomic<int> rate{0};
};
constexpr int kPushDevices = 8;
PushDevice g_push[kPushDevices];

void remember_push_device(SDL_AudioDeviceID dev, int channels, int rate) {
    for (auto& d : g_push) {
        uint32_t cur = d.id.load(std::memory_order_acquire);
        if (cur == dev || cur == 0) {
            d.channels.store(channels, std::memory_order_relaxed);
            d.rate.store(rate, std::memory_order_relaxed);
            d.id.store(dev, std::memory_order_release);
            return;
        }
    }
}

// Runs on SDL's audio thread.
void SDLCALL audio_trampoline(void* userdata, Uint8* stream, int len) {
    AudioDeviceWrap* w = static_cast<AudioDeviceWrap*>(userdata);
    w->original(w->original_userdata, stream, len);
    g_callbacks.fetch_add(1, std::memory_order_relaxed);
    if (len <= 0 || !w->active.load(std::memory_order_acquire)) return;

    const int ch = w->channels.load(std::memory_order_relaxed);
    const int rate = w->rate.load(std::memory_order_relaxed);
    int16_t* samples = reinterpret_cast<int16_t*>(stream);
    size_t n = static_cast<size_t>(len) / sizeof(int16_t);
    n -= n % static_cast<size_t>(ch);   // whole frames only

    const bool meter = g_metering.load(std::memory_order_relaxed);
    uint64_t sq_b = 0;
    int pk_b = 0;
    if (meter) measure(samples, n, sq_b, pk_b);

    process_audio_block(samples, n, ch, rate);
    g_frames.fetch_add(n / static_cast<size_t>(ch), std::memory_order_relaxed);

    if (meter) {
        uint64_t sq_a = 0;
        int pk_a = 0;
        measure(samples, n, sq_a, pk_a);
        g_meter_samples.fetch_add(n, std::memory_order_relaxed);
        g_sumsq_before.fetch_add(sq_b, std::memory_order_relaxed);
        g_sumsq_after.fetch_add(sq_a, std::memory_order_relaxed);
        atomic_max(g_peak_before, pk_b);
        atomic_max(g_peak_after, pk_a);
    }
}

} // namespace

AudioDeviceWrap* wrap_audio_device_spec(SDL_AudioSpec* want) {
    if (!want || !want->callback) return nullptr;                  // push mode (SDL_QueueAudio)
    if (want->format != AUDIO_S16SYS) return nullptr;
    if (want->channels != 1 && want->channels != 2) return nullptr;

    // Construct the singletons now so their one-time initialisation (which reads the track
    // map from disk) never happens on the audio thread.
    (void)AudioDsp::instance();
    (void)HdAudioPlayer::instance();

    AudioDeviceWrap* w = new AudioDeviceWrap;   // intentionally never freed: SDL's audio thread may
                                                // outlive any owner we could give it
    w->original = want->callback;
    w->original_userdata = want->userdata;
    w->channels.store(want->channels, std::memory_order_relaxed);
    w->rate.store(want->freq, std::memory_order_relaxed);
    w->active.store(false, std::memory_order_relaxed);
    want->callback = &audio_trampoline;
    want->userdata = w;
    return w;
}

void finish_audio_device_wrap(AudioDeviceWrap* wrap, SDL_AudioDeviceID device, const SDL_AudioSpec* obtained) {
    if (!wrap) return;
    if (device == 0) {
        wrap->active.store(false, std::memory_order_release);
        return;
    }
    bool ok = true;
    if (obtained) {
        ok = obtained->format == AUDIO_S16SYS && (obtained->channels == 1 || obtained->channels == 2);
        wrap->channels.store(obtained->channels, std::memory_order_relaxed);
        wrap->rate.store(obtained->freq, std::memory_order_relaxed);
    }
    wrap->active.store(ok, std::memory_order_release);
    if (ok) g_devices.fetch_add(1, std::memory_order_relaxed);
}

SDL_AudioDeviceID SDLCALL open_audio_device_processed(const char* device, int iscapture,
                                                      const SDL_AudioSpec* desired,
                                                      SDL_AudioSpec* obtained, int allowed_changes) {
    const SdlFunctions& real = sdl_real();
    if (iscapture || !desired) return real.open_audio_device(device, iscapture, desired, obtained, allowed_changes);

    SDL_AudioSpec want = *desired;
    AudioDeviceWrap* wrap = wrap_audio_device_spec(&want);
    if (!wrap) {
        const SDL_AudioDeviceID dev = real.open_audio_device(device, iscapture, desired, obtained, allowed_changes);
        if (dev != 0 && !desired->callback) {   // push mode: remember the real spec for queue_audio_processed
            const SDL_AudioSpec& got = obtained ? *obtained : *desired;
            if (got.format == AUDIO_S16SYS && (got.channels == 1 || got.channels == 2))
                remember_push_device(dev, got.channels, got.freq);
        }
        return dev;
    }

    const SDL_AudioDeviceID dev = real.open_audio_device(device, iscapture, &want, obtained, allowed_changes);
    // With a null `obtained` SDL converts to the wanted format, so the wrapped values hold.
    finish_audio_device_wrap(wrap, dev, obtained);
    return dev;
}

int SDLCALL queue_audio_processed(SDL_AudioDeviceID dev, const void* data, Uint32 len) {
    const SdlFunctions& real = sdl_real();
    if (!data || len < sizeof(int16_t)) return real.queue_audio(dev, data, len);
    int ch = 0, rate = 0;
    for (auto& d : g_push) {
        if (d.id.load(std::memory_order_acquire) == dev && dev != 0) {
            ch = d.channels.load(std::memory_order_relaxed);
            rate = d.rate.load(std::memory_order_relaxed);
            break;
        }
    }
    if (ch != 1 && ch != 2) return real.queue_audio(dev, data, len);

    static thread_local std::vector<int16_t> scratch;
    const size_t n = static_cast<size_t>(len) / sizeof(int16_t);
    scratch.assign(static_cast<const int16_t*>(data), static_cast<const int16_t*>(data) + n);
    process_audio_block(scratch.data(), n - n % static_cast<size_t>(ch), ch, rate);
    g_queue_calls.fetch_add(1, std::memory_order_relaxed);
    g_frames.fetch_add(n / static_cast<size_t>(ch), std::memory_order_relaxed);
    return real.queue_audio(dev, scratch.data(), len);
}

AudioWrapStats audio_wrap_stats() {
    AudioWrapStats s;
    s.devices_wrapped = g_devices.load(std::memory_order_relaxed);
    s.callbacks = g_callbacks.load(std::memory_order_relaxed);
    s.frames_processed = g_frames.load(std::memory_order_relaxed);
    s.queue_calls = g_queue_calls.load(std::memory_order_relaxed);
    s.metered_samples = g_meter_samples.load(std::memory_order_relaxed);
    if (s.metered_samples) {
        s.rms_before = std::sqrt(static_cast<double>(g_sumsq_before.load(std::memory_order_relaxed)) / static_cast<double>(s.metered_samples));
        s.rms_after = std::sqrt(static_cast<double>(g_sumsq_after.load(std::memory_order_relaxed)) / static_cast<double>(s.metered_samples));
    }
    s.peak_before = g_peak_before.load(std::memory_order_relaxed);
    s.peak_after = g_peak_after.load(std::memory_order_relaxed);
    return s;
}

void reset_audio_wrap_stats() {
    g_callbacks.store(0);
    g_frames.store(0);
    g_queue_calls.store(0);
    g_meter_samples.store(0);
    g_sumsq_before.store(0);
    g_sumsq_after.store(0);
    g_peak_before.store(0);
    g_peak_after.store(0);
}

void set_audio_metering(bool on) { g_metering.store(on, std::memory_order_relaxed); }

} // namespace gbaimp
