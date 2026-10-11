#pragma once
// SDL interposition: one handler/real-function design for every platform.
//
// A host that wants to observe or alter calls that *other* code (a static
// runtime, a UI library, ...) makes into SDL registers handlers for six SDL
// functions and calls the original SDL implementation only through sdl_real().
//
//   Linux / GNU ld   Link-time `--wrap`. The host expands GBAIMP_DEFINE_LD_WRAPS()
//                    in exactly one .cpp and adds the matching
//                    `-Wl,--wrap=SDL_...` link options (listed below). The
//                    generated __wrap_SDL_X forwards to the registered handler
//                    (or to the real function when none is registered) and the
//                    real table is filled from __real_SDL_X.
//   Windows          Import Address Table patching at run time:
//                    install_sdl_interposers() rewrites the IAT slots that point
//                    at SDL2.dll's exports, in the main executable and in every
//                    module loaded at that moment. The real table holds SDL2.dll's
//                    original export addresses.
//
// Handlers have SDL's own signatures. A handler must call the original only via
// sdl_real().<fn>(...), never via the public SDL_X symbol (that would re-enter
// the handler; on Linux the call is rewritten to __wrap_SDL_X, on Windows it goes
// through the patched IAT).
//
// Thread safety: set_sdl_handlers / install_sdl_interposers /
// uninstall_sdl_interposers are serialized by an internal mutex and may be called
// from any thread, but the intended use is once, early in main(), before any
// thread that calls SDL is started. Handlers themselves run on whatever thread
// calls SDL and must be thread-safe if SDL is called from several threads.
// Dispatch reads an immutable snapshot of the handler table, so swapping handlers
// while SDL is being called is safe (old snapshots are intentionally leaked).
//
// Windows limitations (documented, not worked around):
//  * Only IAT / delay-load IAT imports are intercepted. A module that resolves
//    SDL with GetProcAddress, or one that statically links its own SDL, is not.
//  * Modules loaded after install are not patched. Call install_sdl_interposers
//    again (idempotent) after loading a plugin DLL that imports SDL. A LoadLibrary
//    hook is deliberately not installed: it would mean patching kernel32 imports
//    of every module and racing the loader lock.
//  * Security software that hooks or write-protects import tables can make the
//    VirtualProtect/patch fail; failures are reported in the status, never fatal.
//  * Slots that point somewhere other than SDL2.dll's export (a foreign hook, for
//    example an overlay) are left untouched and counted in `skipped_foreign`.
//
// Link flags for the Linux mechanism (all six, plus any host-specific ones):
//   -Wl,--wrap=SDL_PollEvent -Wl,--wrap=SDL_UpdateTexture -Wl,--wrap=SDL_RenderCopy
//   -Wl,--wrap=SDL_RenderPresent -Wl,--wrap=SDL_QueueAudio -Wl,--wrap=SDL_OpenAudioDevice
// A host that omits -Wl,--wrap=SDL_OpenAudioDevice still links and works (that one
// function is simply not interposed; its __real_ symbol is weak).

#include <SDL.h>

#include <string>
#include <vector>

// SDL_dynapi / compatibility macros may rename these; keep the plain names.
#undef SDL_PollEvent
#undef SDL_UpdateTexture
#undef SDL_RenderCopy
#undef SDL_RenderPresent
#undef SDL_QueueAudio
#undef SDL_OpenAudioDevice

namespace gbaimp {

// Function-pointer table for the six interposed SDL functions. Used both for
// handlers (same signatures as SDL) and for the "real" implementations.
// A null entry in a handler table means "do not interpose this function".
struct SdlFunctions {
    int (SDLCALL* poll_event)(SDL_Event* event) = nullptr;
    int (SDLCALL* update_texture)(SDL_Texture* texture, const SDL_Rect* rect,
                                  const void* pixels, int pitch) = nullptr;
    int (SDLCALL* render_copy)(SDL_Renderer* renderer, SDL_Texture* texture,
                               const SDL_Rect* srcrect, const SDL_Rect* dstrect) = nullptr;
    void (SDLCALL* render_present)(SDL_Renderer* renderer) = nullptr;
    int (SDLCALL* queue_audio)(SDL_AudioDeviceID dev, const void* data, Uint32 len) = nullptr;
    SDL_AudioDeviceID (SDLCALL* open_audio_device)(const char* device, int iscapture,
                                                   const SDL_AudioSpec* desired,
                                                   SDL_AudioSpec* obtained,
                                                   int allowed_changes) = nullptr;
};
using SdlHandlers = SdlFunctions;

// The original SDL implementations. Always safe to call; filled lazily if the
// host has not (Windows: GetProcAddress on SDL2.dll, Linux: the LD macro or
// dlsym(RTLD_NEXT)). Never returns null pointers once SDL is loaded.
const SdlFunctions& sdl_real();

// Registers the handler table used by the Linux __wrap shims and by the Windows
// thunks (a null entry forwards to the real function). Does not patch anything.
// Passing a default-constructed table removes all handlers.
void set_sdl_handlers(const SdlHandlers& handlers);
// Currently registered handlers (all-null if none).
SdlHandlers sdl_handlers();

struct SdlPatchedImport {
    std::string module;    // file name of the patched module, e.g. "game.exe"
    std::string function;  // e.g. "SDL_RenderPresent"
    bool delay_load = false;
};

struct SdlInstallStatus {
    bool applicable = false;        // false on non-Windows: the host's --wrap is the mechanism
    bool sdl_module_found = false;  // SDL2.dll is loaded in this process
    int modules_scanned = 0;
    int slots_patched = 0;          // newly patched by this call
    int slots_already_patched = 0;  // already pointing at our thunk (idempotent re-run)
    int slots_skipped_foreign = 0;  // slot did not hold SDL's export (foreign hook): left alone
    int slots_failed = 0;           // VirtualProtect / write failed
    int delay_load_unresolved = 0;  // delay-load slots patched before first resolution
    std::vector<SdlPatchedImport> patched;  // only the slots newly patched by this call
    std::string error;
};

struct SdlInstallOptions {
    const char* dll_name = "SDL2.dll";   // import DLL name to match (case-insensitive)
    void (*log)(const char* line) = nullptr;  // optional progress/diagnostic sink
};

// Windows: registers `handlers` (see set_sdl_handlers) and patches the IATs of
// the main executable and all currently loaded modules for every function that
// has a non-null handler. Idempotent; calling again re-scans (use after loading
// a plugin DLL) and may change the handlers. Returns true if at least one import
// slot is patched after the call.
// Non-Windows: does nothing and returns false (the host's --wrap is the mechanism;
// it registers handlers with set_sdl_handlers). Details in `status` if given.
bool install_sdl_interposers(const SdlHandlers& handlers,
                             SdlInstallStatus* status = nullptr,
                             const SdlInstallOptions* options = nullptr);

// Windows: restores every slot patched by install (skipping unloaded modules) and
// clears the handlers. Returns the number of slots restored. Non-Windows: 0.
// Intended for tests; do not call while other threads are inside SDL.
int uninstall_sdl_interposers();

// Windows: total number of slots currently patched by this process.
int sdl_interposers_patched_slots();

namespace detail {
// Targets of the interposed calls: forward to the registered handler or to
// sdl_real(). Used by GBAIMP_DEFINE_LD_WRAPS and by the Windows IAT patches.
int SDLCALL thunk_poll_event(SDL_Event* event);
int SDLCALL thunk_update_texture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch);
int SDLCALL thunk_render_copy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect);
void SDLCALL thunk_render_present(SDL_Renderer* renderer);
int SDLCALL thunk_queue_audio(SDL_AudioDeviceID dev, const void* data, Uint32 len);
SDL_AudioDeviceID SDLCALL thunk_open_audio_device(const char* device, int iscapture, const SDL_AudioSpec* desired,
                                                  SDL_AudioSpec* obtained, int allowed_changes);
// Called by the macro with the __real_SDL_X addresses.
void set_real(const SdlFunctions& real);
} // namespace detail

} // namespace gbaimp

#if !defined(_WIN32)
// Expand ONCE in a single host .cpp (outside any namespace) when linking with
// `-Wl,--wrap=SDL_X`. Defines __wrap_SDL_X for the six functions and fills the
// real table from __real_SDL_X (at static-init time and on every wrapped call).
#define GBAIMP_DEFINE_LD_WRAPS()                                                          \
    extern "C" {                                                                          \
    int __real_SDL_PollEvent(SDL_Event*);                                                 \
    int __real_SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int);        \
    int __real_SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*); \
    void __real_SDL_RenderPresent(SDL_Renderer*);                                         \
    int __real_SDL_QueueAudio(SDL_AudioDeviceID, const void*, Uint32);                    \
    __attribute__((weak)) SDL_AudioDeviceID __real_SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int);\
    }                                                                                     \
    namespace gbaimp_ld_wraps {                                                           \
    inline void fill_real() {                                                             \
        ::gbaimp::SdlFunctions r;                                                         \
        r.poll_event = &__real_SDL_PollEvent;                                             \
        r.update_texture = &__real_SDL_UpdateTexture;                                     \
        r.render_copy = &__real_SDL_RenderCopy;                                           \
        r.render_present = &__real_SDL_RenderPresent;                                     \
        r.queue_audio = &__real_SDL_QueueAudio;                                           \
        r.open_audio_device = &__real_SDL_OpenAudioDevice;  /* null without the wrap flag */\
        ::gbaimp::detail::set_real(r);                                                    \
    }                                                                                     \
    static const bool registered_ = (fill_real(), true);                                  \
    }                                                                                     \
    extern "C" {                                                                          \
    int __wrap_SDL_PollEvent(SDL_Event* e) {                                              \
        gbaimp_ld_wraps::fill_real();                                                     \
        return ::gbaimp::detail::thunk_poll_event(e);                                     \
    }                                                                                     \
    int __wrap_SDL_UpdateTexture(SDL_Texture* t, const SDL_Rect* r, const void* p, int pitch) { \
        gbaimp_ld_wraps::fill_real();                                                     \
        return ::gbaimp::detail::thunk_update_texture(t, r, p, pitch);                    \
    }                                                                                     \
    int __wrap_SDL_RenderCopy(SDL_Renderer* rd, SDL_Texture* t, const SDL_Rect* s, const SDL_Rect* d) { \
        gbaimp_ld_wraps::fill_real();                                                     \
        return ::gbaimp::detail::thunk_render_copy(rd, t, s, d);                          \
    }                                                                                     \
    void __wrap_SDL_RenderPresent(SDL_Renderer* rd) {                                     \
        gbaimp_ld_wraps::fill_real();                                                     \
        ::gbaimp::detail::thunk_render_present(rd);                                       \
    }                                                                                     \
    int __wrap_SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len) {      \
        gbaimp_ld_wraps::fill_real();                                                     \
        return ::gbaimp::detail::thunk_queue_audio(dev, data, len);                       \
    }                                                                                     \
    SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char* dn, int cap, const SDL_AudioSpec* want, SDL_AudioSpec* have, int allowed) {\
        gbaimp_ld_wraps::fill_real();                                                     \
        return ::gbaimp::detail::thunk_open_audio_device(dn, cap, want, have, allowed);   \
    }                                                                                     \
    }
#endif
