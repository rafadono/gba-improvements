# Integrating into a new project

How to use `gba-improvements` in a GBA static-recompilation project.

## 1. Requirements
- C++17 compiler, CMake 3.16+, SDL2. Discovery (`cmake/GbaimpFindSDL2.cmake`) tries, in order: an
  existing `SDL2::SDL2` target, `find_package(SDL2 CONFIG)`, `find_package(SDL2)` (module),
  `pkg-config sdl2`, then the cache variables `SDL2_INCLUDE_DIR` / `SDL2_LIBRARY`. It works with
  MSVC (e.g. `-DSDL2_DIR=<sdl2-VC-package>/cmake`) and with Linux packages. The headers use
  `<SDL.h>`; an include root that only contains `SDL2/SDL.h` is handled.
- Your own recompilation runtime. The library neither includes nor links one.

## 2. CMake
```cmake
add_subdirectory(external/gba-improvements)      # tests, tools and examples are OFF by default in this mode
target_link_libraries(my_game PRIVATE gbaimp::gba_improvements)
```
Options (defaults shown for a top-level build / as a subproject):

| Option | Top-level | Subproject | Effect |
|---|---|---|---|
| `GBAIMP_BUILD_TESTS` | ON | OFF | Unit tests. |
| `GBAIMP_BUILD_TOOLS` | ON | OFF | `verify_rom_hash <file> <expected_sha1>` (self-contained SHA-1; exit 0 match, 1 mismatch, 2 usage/IO error). |
| `GBAIMP_BUILD_EXAMPLES` | ON | OFF | `examples/minimal_host` (target `gbaimp_example_minimal`), see `examples/README.md`. |

`GBAIMP_SANITIZE` (cache string: `thread`, `address`, `undefined`; empty by default) builds the library and its tests with that sanitizer (not on MSVC).

`GBAIMP_GBARECOMP_INCLUDE` is no longer needed; if set it is ignored with a status message.

See also: `examples/README.md` (runnable host wiring) and `docs/GBARECOMP_CONTRACT.md` (what the
library expects from the `gbarecomp` runtime: hooks, per-frame callback, ROM overrides, SDL call sites).

## 3. Hooks
`gbaimp/hooks.h` exposes three optional callbacks. Set them once at startup:

| Hook | Used by | If unset |
|---|---|---|
| `save_settings` | `perf_hud` (after a drag) via `notify_settings_changed()` | The change is not persisted |
| `is_input_blocked` | `InputEnhancements::process_keyinput` returns `0x03FF` (nothing pressed) while it returns true | Input always reaches the game |
| `frame_counter` | `InputEnhancements` caches its result per emulated frame | Recomputed on every call |

```cpp
gbaimp::hooks().save_settings    = [] { save_my_config(); };
gbaimp::hooks().is_input_blocked = [] { return my_overlay_open(); };
gbaimp::hooks().frame_counter    = [] { return my_frame_counter(); };
```

## 4. Intercepting SDL
Most enhancements work by observing the SDL calls that the runtime makes. The library provides the *mechanism* (`gbaimp/sdl_interpose.h`); the *handlers* (what to do in each call, and how to recognise "the game image") stay in the host.

1. Fill an `SdlHandlers` table (function pointers with SDL's own signatures; a null entry means "do not interpose") and register it with `gbaimp::set_sdl_handlers(h)`.
2. A handler calls the original SDL **only** through `gbaimp::sdl_real().<function>(...)`, never through the public `SDL_X` symbol (that would re-enter the handler).
3. Make SDL's calls reach the handlers, per platform:
   - **Linux / GNU ld**: expand `GBAIMP_DEFINE_LD_WRAPS()` once in one `.cpp` and link with all of
     ```cmake
     target_link_options(my_game PRIVATE
       -Wl,--wrap=SDL_PollEvent -Wl,--wrap=SDL_RenderPresent
       -Wl,--wrap=SDL_UpdateTexture -Wl,--wrap=SDL_RenderCopy -Wl,--wrap=SDL_QueueAudio -Wl,--wrap=SDL_OpenAudioDevice)
     ```
     `--wrap` rewrites undefined references in every linked object, including a static runtime library, but not calls made inside SDL's own shared library.
   - **Windows / MSVC** (no `--wrap`): call `gbaimp::install_sdl_interposers(h)` first thing in `main()`, before the runtime makes its first SDL call. It patches the Import Address Table of the executable and of every module loaded at that time. See `docs/MODULES.md` ("SDL interposition") for the status struct and the limitations (IAT imports only, modules loaded later need another call, foreign hooks are left alone).
   - Other approaches (calling the library from your own renderer) work just as well.

Minimal wiring:
```cpp
GBAIMP_DEFINE_LD_WRAPS()                         // Linux only, once, at namespace scope in one .cpp

void install() {
    gbaimp::SdlHandlers h;
    h.update_texture    = &my_update_texture;    // calls gbaimp::sdl_real().update_texture(...)
    h.render_copy       = &my_render_copy;
    h.render_present    = &my_render_present;
    h.open_audio_device = &gbaimp::open_audio_device_processed;   // pull-callback audio, see below
    gbaimp::set_sdl_handlers(h);
#if defined(_WIN32)
    gbaimp::install_sdl_interposers(h);
#endif
}
```

Suggested calls from each intercept:

| Intercept | Library calls |
|---|---|
| `SDL_UpdateTexture` | If the texture is the game frame (RGB24) and the profile is not `Raw`: copy the buffer and call `ScreenFilters::instance().apply_color_correction(buf, w, h)` |
| `SDL_RenderCopy` | Only for the game image. Compute the destination rect, call `PerfHud::instance().set_game_viewport(dst)`, draw, then `ScreenFilters::instance().render_mask(renderer, &dst, base_w, base_h)` |
| `SDL_RenderPresent` | `FrameInterpolator::instance().on_present(renderer, &dst)`, then `PerfHud::instance().on_frame_present(renderer)` |
| `SDL_PollEvent` | `PerfHud::instance().handle_mouse_event(*event)`; if it returns `true`, drop the event |
| `SDL_OpenAudioDevice` | For runtimes that use an audio **callback** (gbarecomp's default): register `gbaimp::open_audio_device_processed` (see `audio_device.h`) as the `open_audio_device` handler; it wraps the callback and processes each buffer on SDL's audio thread |
| `SDL_QueueAudio` | Push mode only: register `gbaimp::queue_audio_processed` (uses the device's real channels/rate), or copy the samples to your own buffer and call `gbaimp::process_audio_block(buf, n_int16)` (interleaved stereo) or `process_audio_block(buf, n_int16, channels, sample_rate)` (mono or stereo at the real device rate) before queueing |

Detecting which texture or render call is "the game image" (format, size, streaming flag, render
target) is the host's job and depends on how your runtime presents frames. The library only needs
the final rectangle (`set_game_viewport`) and the base resolution.

## 5. Per-frame input
In your runtime's per-frame hook:
```cpp
uint16_t host = runtime_host_keyinput();
runtime_set_keyinput(gbaimp::InputEnhancements::instance().process_keyinput(host));
```
`KEYINPUT` is active-low (0 = pressed), as on hardware. `process_keyinput` applies keyboard and
gamepad remapping, analog handling and dialog turbo. When no controller is attached the
`host_keys` argument is merged in.

`InputEnhancements` loads `keybinds.ini` from the working directory when it is first used.
Pass another path to `load_keybinds()`/`save_keybinds()` if you keep configs elsewhere.

## 6. HUD zones for widescreen
`HudAnchoring` does nothing until you register zones. Each `HudZone` is a rectangle in GBA screen
pixels plus a direction (-1 shift left, +1 shift right). Register your game's HUD regions once:
```cpp
auto& hud = gbaimp::HudAnchoring::instance();
hud.clear_zones();
gbaimp::HudZone z;
z.direction = -1;
z.coord_x_max = 63;  z.coord_y_max = 31;     // adjust_sprite_coordinate() bounds
z.oam_x_max   = 63;  z.oam_y_max   = 31;     // process_oam_entry() bounds
hud.add_zone(z);
hud.set_mode(gbaimp::HudAnchorMode::WidescreenAnchored);
```
Deciding which sprites are HUD and what the widened viewport is belongs to your widescreen layer.

## 7. What the host must provide
- **HD music**: call `HdAudioPlayer::instance().trigger_song(song_id)` where your game starts a song, and supply the `[tracks]` ini (default path `config/music_tracks.ini`, see `HdAudioSettings::config_path`).
- **Audio format**: set `HdAudioSettings::target_sample_rate` to the rate of your audio pipeline (default 65536 Hz) before the first `trigger_song`. The 2-argument `process_audio_block` assumes interleaved stereo at that rate; for a mono buffer or a different rate use `process_audio_block(buf, n_int16, channels, sample_rate)` (the HD track is folded down and resampled to match).
- **Dialog turbo**: `process_keyinput` drives `DialogTurbo::set_held` every frame from a configurable key / pad button (default Tab / pad Y; `settings().turbo_key`, `settings().turbo_pad`, saved in the `[turbo]` section of `keybinds.ini`). If you do not use `InputEnhancements`, call `DialogTurbo::instance().set_held(bool)` yourself.
- **Thumbnails**: call `SavestateThumbnailManager::instance().capture_thumbnail(slot, rgba, w, h)` when saving a state. Use `set_base_directory(dir)` to place files (default `saves/thumbnails`).
- **Options/UI**: every module exposes `settings()`; connect them to your menu and persistence.

## 8. Assumptions to verify
- Audio is int16, mono or interleaved stereo (see `process_audio_block`); DSP coefficients are recomputed when the sample rate changes.
- `FrameInterpolator::on_present` presents intermediate frames itself. If your host intercepts `SDL_RenderPresent` (link-time wrap or import-table patching) either rely on the built-in re-entrancy guard or give it the real present with `set_present_hook`.
- The link-time SDL wrap (`GBAIMP_DEFINE_LD_WRAPS`) is GNU ld / Clang-on-ELF specific; on Windows the IAT patcher (`install_sdl_interposers`) is the mechanism and has the limitations listed in `docs/MODULES.md`.
- Modules are singletons. The audio modules (`AudioDsp`, `HdAudioPlayer`, `audio_pipeline`, `audio_device`) are designed for a control thread plus one audio thread (see `docs/MODULES.md`); every other module is not thread-safe and should be called from the thread that owns the matching SDL object (renderer, audio queue).
