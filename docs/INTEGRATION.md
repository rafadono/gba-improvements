# Integrating into a new project

How to use `gba-improvements` in a GBA static-recompilation project.

## 1. Requirements
- C++17 compiler, CMake 3.16+, SDL2 (`find_package(SDL2)`).
- Your own recompilation runtime. The library neither includes nor links one.

## 2. CMake
```cmake
add_subdirectory(external/gba-improvements)      # tests are OFF by default in this mode
target_link_libraries(my_game PRIVATE gbaimp::gba_improvements)

# Optional: build the verify_rom_hash CLI (needs gba/sha1.h from your runtime headers)
set(GBAIMP_GBARECOMP_INCLUDE ${YOUR_RUNTIME_ROOT}/include)
```
Set `-DGBAIMP_BUILD_TESTS=ON` to also build the unit tests from a parent project.

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

## 4. Intercepting SDL (host code, not library code)
The library never wraps SDL itself. A common approach on GCC/Clang is to wrap SDL entry points
at link time and forward to the real function:
```cmake
target_link_options(my_game PRIVATE
  -Wl,--wrap=SDL_PollEvent -Wl,--wrap=SDL_RenderPresent
  -Wl,--wrap=SDL_UpdateTexture -Wl,--wrap=SDL_RenderCopy -Wl,--wrap=SDL_QueueAudio)
```
Each `__wrap_X` runs your logic and then calls `__real_X`. Other mechanisms (calling the library
from your own renderer, a different strategy on MSVC) work just as well.

Suggested calls from each intercept:

| Intercept | Library calls |
|---|---|
| `SDL_UpdateTexture` | If the texture is the game frame (RGB24) and the profile is not `Raw`: copy the buffer and call `ScreenFilters::instance().apply_color_correction(buf, w, h)` |
| `SDL_RenderCopy` | Only for the game image. Compute the destination rect, call `PerfHud::instance().set_game_viewport(dst)`, draw, then `ScreenFilters::instance().render_mask(renderer, &dst, base_w, base_h)` |
| `SDL_RenderPresent` | `FrameInterpolator::instance().on_present(renderer, &dst)`, then `PerfHud::instance().on_frame_present(renderer)` |
| `SDL_PollEvent` | `PerfHud::instance().handle_mouse_event(*event)`; if it returns `true`, drop the event |
| `SDL_QueueAudio` | Copy the samples to your own buffer and call `gbaimp::process_audio_block(buf, n_int16)` before queueing |

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
- **Audio format**: set `HdAudioSettings::target_sample_rate` to the rate of your audio pipeline (default 65536 Hz) before the first `trigger_song`. `process_audio_block` and `HdAudioPlayer` assume interleaved stereo int16.
- **Dialog turbo**: `process_keyinput` holds it on Tab / the gamepad Y button; you can also call `DialogTurbo::instance().set_held(bool)` yourself.
- **Thumbnails**: call `SavestateThumbnailManager::instance().capture_thumbnail(slot, rgba, w, h)` when saving a state. Use `set_base_directory(dir)` to place files (default `saves/thumbnails`).
- **Options/UI**: every module exposes `settings()`; connect them to your menu and persistence.

## 8. Assumptions to verify
- Audio is stereo int16; DSP coefficients are recomputed when the sample rate passed to `process_stereo` changes.
- The link-time SDL wrap is GCC/Clang specific.
- Modules are singletons and not thread-safe; call them from the thread that owns the matching SDL object (renderer, audio queue).
