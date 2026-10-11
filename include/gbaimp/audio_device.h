#pragma once
// Processing the audio a host *runtime* produces, at the SDL audio device.
//
// Many runtimes open their device with an SDL audio PULL callback (SDL asks for N bytes,
// the callback fills them). Nothing is ever pushed with SDL_QueueAudio, so the push-side
// interception cannot see that audio. This helper handles the pull case: interpose
// SDL_OpenAudioDevice (see sdl_interpose.h, SdlHandlers::open_audio_device), and for every
// playback device opened with a signed 16-bit mono or stereo callback, substitute a
// trampoline that first runs the runtime's own callback and then applies
// process_audio_block() (HD track mix + volume + DSP) to the bytes it just produced.
//
// Typical host code:
//   h.open_audio_device = &gbaimp::open_audio_device_processed;   // that is all
//   gbaimp::set_sdl_handlers(h);                                    // (Windows: install_sdl_interposers)
//
// Real-time contract of the trampoline (it runs on SDL's audio thread): no allocation, no
// blocking locks, no file access. Settings it reads are atomics, filter coefficients are
// owned by the audio thread, and the HD track is handed over by an atomic pointer exchange
// (see audio_dsp.h / hd_audio_player.h). The only shared mutable data it touches are
// relaxed atomic statistics.
#include <SDL.h>

#include <cstdint>

#undef SDL_OpenAudioDevice
#undef SDL_QueueAudio

namespace gbaimp {

// Opaque per-device state created by wrap_audio_device_spec(). Lives until process exit.
struct AudioDeviceWrap;

// Low-level step. If `want` describes a pull-mode (callback != nullptr), signed 16-bit,
// 1- or 2-channel stream, replaces want->callback / want->userdata by the trampoline and
// its context and returns that context. Otherwise (format other than S16, other channel
// counts, callback == nullptr i.e. push mode via SDL_QueueAudio) `want` is left untouched and
// nullptr is returned. Not real-time safe (allocates); call it from the thread that opens
// the device. It also makes sure the audio singletons are constructed here, not on the
// audio thread.
AudioDeviceWrap* wrap_audio_device_spec(SDL_AudioSpec* want);

// Second step, after SDL_OpenAudioDevice returned: tells the trampoline the channel count and
// rate the callback really receives (the *obtained* spec; it may differ from `want`).
// Pass `device` = 0 when the open failed: the wrap is disabled. A null `obtained` means "SDL
// converts to the wanted format", so the wanted values stay. Null `wrap` is a no-op.
void finish_audio_device_wrap(AudioDeviceWrap* wrap, SDL_AudioDeviceID device, const SDL_AudioSpec* obtained);

// Drop-in replacement for SDL_OpenAudioDevice that does the two steps above around the
// original (sdl_real().open_audio_device). Capture devices pass through untouched.
SDL_AudioDeviceID SDLCALL open_audio_device_processed(const char* device, int iscapture,
                                                      const SDL_AudioSpec* desired,
                                                      SDL_AudioSpec* obtained, int allowed_changes);

// Push mode (SDL_QueueAudio, e.g. gbarecomp with GBARECOMP_AUDIO_DIRECT): drop-in replacement
// for SDL_QueueAudio. It processes the data with the channel count and rate of the device as
// opened through open_audio_device_processed() (the helper remembers each push-mode playback
// device's obtained spec), then forwards to sdl_real().queue_audio. Data for a device it does
// not know (not opened through the helper, or not S16 mono/stereo) is forwarded untouched.
// Runs on the caller's thread; it may allocate (a per-thread scratch buffer).
int SDLCALL queue_audio_processed(SDL_AudioDeviceID dev, const void* data, Uint32 len);

// Statistics (relaxed atomics; reading is free).
struct AudioWrapStats {
    uint64_t devices_wrapped = 0;     // playback devices whose callback was substituted
    uint64_t callbacks = 0;           // trampoline invocations
    uint64_t frames_processed = 0;    // frames (samples / channels) run through process_audio_block
    uint64_t queue_calls = 0;         // queue_audio_processed() invocations that processed data
    // Level meter, only filled while metering is enabled (set_audio_metering):
    uint64_t metered_samples = 0;
    double rms_before = 0.0;          // over every metered sample, before / after processing
    double rms_after = 0.0;
    int peak_before = 0;
    int peak_after = 0;
};
AudioWrapStats audio_wrap_stats();
void reset_audio_wrap_stats();

// Enables/disables the before/after level meter (off by default: it costs two extra passes
// over each buffer). Safe to toggle from any thread.
void set_audio_metering(bool on);

} // namespace gbaimp
