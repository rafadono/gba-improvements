#pragma once
// Generic SDL host glue for a GBA static-recompilation runtime.
//
// Every host used to carry its own copy of the six SDL handlers that route the runtime's
// SDL calls into this library (colour correction, game-image detection, letterboxing,
// screen mask, frame interpolation, performance HUD, audio processing, diagnostics).
// This module owns that code. A host supplies only what is specific to its game through
// GameHostConfig (every callback is optional) and calls install_game_host() once, first
// thing in main().
//
// The mechanism that makes SDL calls reach the handlers is unchanged (see sdl_interpose.h):
//   Linux / GNU ld   the host expands GBAIMP_DEFINE_LD_WRAPS() in one .cpp and links with
//                    the -Wl,--wrap=SDL_... flags; install_game_host() only registers the
//                    handlers (call it from a static initializer so it does not depend on
//                    main()).
//   Windows / MSVC   install_game_host() also patches the Import Address Tables.
//
// Minimal host:
//   GBAIMP_DEFINE_LD_WRAPS()                       // non-Windows only, once
//   bool my_install() {
//       gbaimp::GameHostConfig c;
//       c.tag = "mygame"; c.log_env = "MYGAME_LOG_INTERPOSE";
//       c.compute_viewport = &my_viewport;          // optional
//       return gbaimp::install_game_host(c);
//   }
#include "gbaimp/sdl_interpose.h"

#include <cstdint>

namespace gbaimp {

struct GameHostConfig {
    // Prefix of the diagnostic lines printed on stderr ("[tag] ...").
    const char* tag = "gbaimp";
    // Name of an environment variable that, when set to anything but "" or "0", enables
    // diagnostics (call counters at exit and every ~300 presents, audio level metering,
    // verbose IAT patching). nullptr disables diagnostics.
    const char* log_env = nullptr;

    // Called at the start of every SDL_PollEvent (per-frame host housekeeping).
    void (*on_poll)() = nullptr;
    // Called for every polled event after the library's own Escape-key fix. Return true to
    // swallow the event (the library's performance HUD sees it only when this returns false).
    bool (*on_event)(SDL_Event& event) = nullptr;

    // Raw PPU framebuffer (RGB24, 240x160 or 284x160) right before colour correction.
    // Only called while wants_native_frame is null or returns true.
    bool (*wants_native_frame)() = nullptr;
    void (*on_native_frame)(const void* pixels, int pitch, int w, int h) = nullptr;

    // Where the game image goes. Receives the texture and window sizes and fills the source
    // rectangle (part of the texture), the destination rectangle (window pixels) and the
    // base resolution used by the screen mask. nullptr uses default_game_viewport().
    void (*compute_viewport)(int tex_w, int tex_h, int win_w, int win_h,
                             SDL_Rect* out_src, SDL_Rect* out_dst,
                             int* out_base_w, int* out_base_h) = nullptr;
    // Called right after the game image and the screen mask were drawn (once per presented
    // frame) with the destination rectangle. Drawn here, host content stays under the
    // runtime's own menu, which is drawn afterwards.
    void (*after_game_image)(SDL_Renderer* renderer, const SDL_Rect& dst) = nullptr;

    // Called at the start of every SDL_RenderPresent (frame counter, etc.).
    void (*on_present_begin)() = nullptr;
    // Called after the library drew its frame interpolation and performance HUD, with the
    // renderer output size.
    void (*render_present_overlay)(SDL_Renderer* renderer, int win_w, int win_h) = nullptr;
    // Called last, right before the real SDL_RenderPresent.
    void (*before_present)(SDL_Renderer* renderer) = nullptr;

    // Extra diagnostic lines printed with the call counters (only when diagnostics are on).
    void (*print_stats)(const char* tag) = nullptr;
};

// Counters of how often each intercepted SDL function ran through the handlers.
struct GameHostStats {
    unsigned long long poll_event = 0;
    unsigned long long update_texture = 0;
    unsigned long long render_copy = 0;
    unsigned long long render_present = 0;
    unsigned long long queue_audio = 0;
    unsigned long long open_audio_device = 0;
};
GameHostStats game_host_stats();

// Registers the handlers and, on Windows, patches the import tables. Idempotent (a second
// call replaces the configuration). Returns true when import-table patching is active
// (always false on Linux, where the --wrap link step is the mechanism).
bool install_game_host(const GameHostConfig& config);

// --- helpers, public so hosts can reuse them and tests can check them -------------------

// True for the texture gbarecomp presents: the direct PPU framebuffer (RGB24 streaming,
// 240x160 or 284x160) or its integer-scaled sharp render target (RGBA8888 target,
// 240*K x 160*K or 284*K x 160*K).
bool is_gba_game_texture(uint32_t sdl_pixel_format, int sdl_texture_access, int tex_w, int tex_h);

// Default placement: the whole texture, scaled uniformly (aspect preserved, centered) into
// the window. base_w/base_h is the unscaled GBA resolution of that texture (e.g. 240x160).
void default_game_viewport(int tex_w, int tex_h, int win_w, int win_h,
                           SDL_Rect* out_src, SDL_Rect* out_dst,
                           int* out_base_w, int* out_base_h);

} // namespace gbaimp
