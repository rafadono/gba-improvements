#include "gbaimp/sdl_interpose.h"

#include <atomic>
#include <mutex>

#if defined(_WIN32)
#else
#include <dlfcn.h>
#endif

namespace gbaimp {
namespace detail {
// Platform hooks. Windows: sdl_interpose_win32.cpp. Others: below.
bool resolve_real_platform(SdlFunctions& out);
} // namespace detail

namespace {

std::mutex& state_mutex() {
    static std::mutex m;
    return m;
}

// Handlers live in immutable heap snapshots; readers do one acquire load.
// Old snapshots are leaked on purpose so an in-flight handler call never
// dereferences freed memory.
std::atomic<const SdlFunctions*> g_handlers{nullptr};

SdlFunctions g_real;                 // guarded by g_real_ready (release/acquire)
std::atomic<bool> g_real_ready{false};

void init_real_locked() {
    if (g_real_ready.load(std::memory_order_acquire)) return;
    SdlFunctions r = g_real;
    detail::resolve_real_platform(r);
    g_real = r;
    g_real_ready.store(true, std::memory_order_release);
}

} // namespace

const SdlFunctions& sdl_real() {
    if (!g_real_ready.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(state_mutex());
        init_real_locked();
    }
    return g_real;
}

void set_sdl_handlers(const SdlHandlers& handlers) {
    std::lock_guard<std::mutex> lock(state_mutex());
    g_handlers.store(new SdlFunctions(handlers), std::memory_order_release);
}

SdlHandlers sdl_handlers() {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    return h ? *h : SdlFunctions{};
}

namespace detail {

void set_real(const SdlFunctions& real) {
    if (g_real_ready.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(state_mutex());
    if (g_real_ready.load(std::memory_order_relaxed)) return;
    SdlFunctions r = real;
    resolve_real_platform(r);   // fills entries the host could not provide (e.g. a function without --wrap)
    g_real = r;
    g_real_ready.store(true, std::memory_order_release);
}

// Used by the Windows installer, which knows the original export addresses.
void set_real_forced(const SdlFunctions& real) {
    std::lock_guard<std::mutex> lock(state_mutex());
    g_real = real;
    g_real_ready.store(true, std::memory_order_release);
}

int SDLCALL thunk_poll_event(SDL_Event* event) {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    if (h && h->poll_event) return h->poll_event(event);
    return sdl_real().poll_event(event);
}
int SDLCALL thunk_update_texture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    if (h && h->update_texture) return h->update_texture(texture, rect, pixels, pitch);
    return sdl_real().update_texture(texture, rect, pixels, pitch);
}
int SDLCALL thunk_render_copy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect) {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    if (h && h->render_copy) return h->render_copy(renderer, texture, srcrect, dstrect);
    return sdl_real().render_copy(renderer, texture, srcrect, dstrect);
}
void SDLCALL thunk_render_present(SDL_Renderer* renderer) {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    if (h && h->render_present) { h->render_present(renderer); return; }
    sdl_real().render_present(renderer);
}
int SDLCALL thunk_queue_audio(SDL_AudioDeviceID dev, const void* data, Uint32 len) {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    if (h && h->queue_audio) return h->queue_audio(dev, data, len);
    return sdl_real().queue_audio(dev, data, len);
}

SDL_AudioDeviceID SDLCALL thunk_open_audio_device(const char* device, int iscapture, const SDL_AudioSpec* desired,
                                                  SDL_AudioSpec* obtained, int allowed_changes) {
    const SdlFunctions* h = g_handlers.load(std::memory_order_acquire);
    if (h && h->open_audio_device) return h->open_audio_device(device, iscapture, desired, obtained, allowed_changes);
    return sdl_real().open_audio_device(device, iscapture, desired, obtained, allowed_changes);
}

#if !defined(_WIN32)
// Fallback when the host did not expand GBAIMP_DEFINE_LD_WRAPS (no --wrap in
// use): the next definition in the symbol search order is SDL's own.
bool resolve_real_platform(SdlFunctions& out) {
    auto sym = [](const char* n) { return dlsym(RTLD_DEFAULT, n); };
    if (!out.poll_event) out.poll_event = reinterpret_cast<decltype(out.poll_event)>(sym("SDL_PollEvent"));
    if (!out.update_texture) out.update_texture = reinterpret_cast<decltype(out.update_texture)>(sym("SDL_UpdateTexture"));
    if (!out.render_copy) out.render_copy = reinterpret_cast<decltype(out.render_copy)>(sym("SDL_RenderCopy"));
    if (!out.render_present) out.render_present = reinterpret_cast<decltype(out.render_present)>(sym("SDL_RenderPresent"));
    if (!out.queue_audio) out.queue_audio = reinterpret_cast<decltype(out.queue_audio)>(sym("SDL_QueueAudio"));
    if (!out.open_audio_device) out.open_audio_device = reinterpret_cast<decltype(out.open_audio_device)>(sym("SDL_OpenAudioDevice"));
    return true;
}
#endif

} // namespace detail

#if !defined(_WIN32)
bool install_sdl_interposers(const SdlHandlers&, SdlInstallStatus* status, const SdlInstallOptions*) {
    if (status) *status = SdlInstallStatus{};  // applicable == false
    return false;
}
int uninstall_sdl_interposers() { return 0; }
int sdl_interposers_patched_slots() { return 0; }
#endif

} // namespace gbaimp
