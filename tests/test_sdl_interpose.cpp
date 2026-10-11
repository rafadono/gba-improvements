#define SDL_MAIN_HANDLED  // the test provides plain main(); no SDL_main redirection
#include "test_harness.h"
#include "gbaimp/sdl_interpose.h"

// Linux: the test executable is linked with -Wl,--wrap=SDL_* (see CMakeLists.txt)
// and expands the shim macro once. Windows: install_sdl_interposers() patches the
// IAT of this executable instead.
#if !defined(_WIN32)
GBAIMP_DEFINE_LD_WRAPS()
#endif

using namespace gbaimp;

namespace {
int g_poll_calls = 0;
int g_present_calls = 0;
int g_queue_calls = 0;
int g_open_calls = 0;
int g_open_capture_flag = -1;
int g_poll_real_result = -123;

int SDLCALL h_poll(SDL_Event* e) {
    ++g_poll_calls;
    // The original must stay reachable from inside a handler without recursion.
    g_poll_real_result = sdl_real().poll_event(e);
    return 77;  // visibly different from SDL's own result (0 with no events)
}
void SDLCALL h_present(SDL_Renderer* r) {
    ++g_present_calls;
    sdl_real().render_present(r);  // SDL_RenderPresent(nullptr) is a harmless error
}
int SDLCALL h_queue(SDL_AudioDeviceID dev, const void* d, Uint32 len) {
    ++g_queue_calls;
    return sdl_real().queue_audio(dev, d, len);
}
SDL_AudioDeviceID SDLCALL h_open(const char* name, int iscapture, const SDL_AudioSpec* want,
                                 SDL_AudioSpec* have, int allowed) {
    ++g_open_calls;
    g_open_capture_flag = iscapture;
    return sdl_real().open_audio_device(name, iscapture, want, have, allowed);
}
} // namespace

int main() {
    SDL_SetMainReady();

    // sdl_real() works before anything is installed.
    CHECK(sdl_real().poll_event != nullptr);
    CHECK(sdl_real().render_present != nullptr);
    CHECK(sdl_real().queue_audio != nullptr);
    CHECK(sdl_real().update_texture != nullptr);
    CHECK(sdl_real().render_copy != nullptr);
    CHECK(sdl_real().open_audio_device != nullptr);

    SdlHandlers h;
    h.poll_event = &h_poll;
    h.render_present = &h_present;
    h.queue_audio = &h_queue;
    h.open_audio_device = &h_open;

    // Without a handler registered the call is forwarded to the real function.
    SDL_Event ev;
    CHECK_EQ(SDL_PollEvent(&ev), 0);
    CHECK_EQ(g_poll_calls, 0);

    SdlInstallStatus st;
#if defined(_WIN32)
    CHECK(install_sdl_interposers(h, &st));
    CHECK(st.applicable);
    CHECK(st.sdl_module_found);
    CHECK(st.slots_patched >= 4);          // poll, present, queue, open_audio_device imported by this exe
    CHECK_EQ(st.slots_failed, 0);
    CHECK_EQ(sdl_interposers_patched_slots(), st.slots_patched);

    // Idempotent: a second install patches nothing new and reports "already".
    SdlInstallStatus st2;
    CHECK(install_sdl_interposers(h, &st2));
    CHECK_EQ(st2.slots_patched, 0);
    CHECK_EQ(st2.slots_already_patched, st.slots_patched);
#else
    // Not applicable: the host's --wrap is the mechanism; install does nothing.
    CHECK(!install_sdl_interposers(h, &st));
    CHECK(!st.applicable);
    CHECK_EQ(st.slots_patched, 0);
    // The --wrap shims route through the registered table.
    set_sdl_handlers(h);
#endif

    // Handler runs and the real function is still reachable through sdl_real().
    g_poll_calls = 0;
    CHECK_EQ(SDL_PollEvent(&ev), 77);
    CHECK_EQ(g_poll_calls, 1);
    CHECK_EQ(g_poll_real_result, 0);

    SDL_RenderPresent(nullptr);
    CHECK_EQ(g_present_calls, 1);

    CHECK(SDL_QueueAudio(0, nullptr, 0) <= 0);
    CHECK_EQ(g_queue_calls, 1);

    // SDL_OpenAudioDevice: handler runs, the real one (reached through sdl_real()) opens the device.
    CHECK_EQ(SDL_InitSubSystem(SDL_INIT_AUDIO), 0);
    {
        SDL_AudioSpec want{};
        want.freq = 22050;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        SDL_AudioSpec have{};
        g_open_calls = 0;
        const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        CHECK(dev != 0);
        CHECK_EQ(g_open_calls, 1);
        CHECK_EQ(g_open_capture_flag, 0);
        CHECK_EQ(int(have.channels), 1);
        if (dev) SDL_CloseAudioDevice(dev);
    }

    // A function without a handler is forwarded untouched.
    SDL_Rect r{};
    CHECK(SDL_UpdateTexture(nullptr, &r, nullptr, 0) != 0);  // real SDL: invalid texture error

    // Handlers can be swapped at runtime.
    SdlHandlers h2;
    h2.poll_event = [](SDL_Event*) -> int { return 55; };
    set_sdl_handlers(h2);
    CHECK_EQ(SDL_PollEvent(&ev), 55);
    CHECK(sdl_handlers().render_present == nullptr);

#if defined(_WIN32)
    CHECK(uninstall_sdl_interposers() >= 3);
    CHECK_EQ(sdl_interposers_patched_slots(), 0);
#else
    set_sdl_handlers(SdlHandlers{});
#endif
    // After uninstall / clearing, handlers no longer run and SDL answers directly.
    // (Opening/closing the audio device queued device add/remove events: drain them first.)
    while (SDL_PollEvent(&ev)) {}
    g_poll_calls = 0;
    CHECK_EQ(SDL_PollEvent(&ev), 0);
    CHECK_EQ(g_poll_calls, 0);
    {
        SDL_AudioSpec want{};
        want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
        SDL_AudioSpec have{};
        g_open_calls = 0;
        const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        CHECK(dev != 0);
        CHECK_EQ(g_open_calls, 0);   // handler no longer runs
        if (dev) SDL_CloseAudioDevice(dev);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    return testh::finish("sdl_interpose");
}
