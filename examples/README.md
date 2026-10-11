# Examples

## `minimal_host/` (target `gbaimp_example_minimal`)

A small SDL2 program that uses **only** `gba-improvements` (no recompilation runtime). It
generates a synthetic 240x160 RGB24 frame and a 440 Hz tone, and pushes them through the library
the same way a real host does from its SDL intercepts:

| Step | Library call |
|---|---|
| Before `SDL_UpdateTexture` | `ScreenFilters::apply_color_correction` |
| After drawing the game image | `PerfHud::set_game_viewport`, `ScreenFilters::render_mask` |
| Before `SDL_RenderPresent` | `FrameInterpolator::on_present`, `PerfHud::on_frame_present` |
| Before audio is queued | `gbaimp::process_audio_block` |

Build and run (`GBAIMP_BUILD_EXAMPLES` is ON for a top-level build, OFF as a subproject):

```sh
cmake -S . -B build -DGBAIMP_BUILD_EXAMPLES=ON
cmake --build build
./build/gbaimp_example_minimal --frames 300         # opens a window
./build/gbaimp_example_minimal --headless           # dummy drivers, offscreen renderer
```

`--headless` (or `SDL_VIDEODRIVER=dummy`) renders to an offscreen software surface and exits
after `--frames N` (default 120) with status 0; the ctest `example_minimal_headless` runs it that
way.

## Wiring a real host

A real host replaces the synthetic pieces with data from its game runtime:

1. **Frame source.** Instead of `draw_test_pattern`, take the RGB24 frame the runtime hands to
   SDL (see `docs/GBARECOMP_CONTRACT.md` for what `gbarecomp` exposes) and call
   `apply_color_correction` on a copy before the texture upload.
2. **Presentation.** Do the viewport computation where the runtime draws the game texture and
   report it with `PerfHud::set_game_viewport`; call `render_mask`, `on_present` and
   `on_frame_present` around the present call.
3. **Audio.** Run `process_audio_block` on the int16 buffer before it is queued to the device:
   the 2-argument form for interleaved stereo, or the `(samples, count, channels, sample_rate)`
   form for a mono buffer or a device rate other than the HD target rate.
4. **Input and hooks.** Fill `gbaimp::hooks()` (save settings, input blocking, frame counter) and
   route the per-frame `KEYINPUT` through `InputEnhancements::process_keyinput`
   (`docs/INTEGRATION.md`, sections 3 and 5).
5. **Interception.** The runtime owns the SDL calls, so the host registers handlers with `gbaimp::set_sdl_handlers` and routes SDL's calls to them:
   `GBAIMP_DEFINE_LD_WRAPS()` plus the `-Wl,--wrap=SDL_*` link flags on Linux, `gbaimp::install_sdl_interposers` (Import Address Table patching) on Windows
   (`docs/INTEGRATION.md`, section 4). This example does not need it: it calls the library from its own render loop.
6. **Pull-callback audio.** If the runtime opens its device with an SDL audio callback, register `gbaimp::open_audio_device_processed` as the
   `open_audio_device` handler instead of processing a queued buffer (`docs/MODULES.md`, `audio_device.h`).
