# gba-improvements

Reusable, game-agnostic enhancements for static recompilations of Game Boy Advance games
(designed to sit next to a `gbarecomp`-style runtime). The repository contains only C++17 + SDL2
code and support scripts: no game data, no ROM addresses and no recompiler code.

Namespace: `gbaimp`. Public headers live in `include/gbaimp/`. CMake target: `gbaimp::gba_improvements`. License: MIT.

Status: pre-release (version 0.1.0 plus unreleased changes, see [`CHANGELOG.md`](CHANGELOG.md)). The code is unit-tested; the CI workflow has been written but has not been run on GitHub yet.

## Catalog

### Video and presentation
| Module | What it does | Options |
|---|---|---|
| `pixel_art_upscaler` | Edge-aware pixel-art upscaler (Scale2x/EPX style), not xBRZ. The old `xbrz_filter.h` names remain as deprecated aliases | Off, 2x, 3x, 4x, 5x |
| `screen_filters` | GBA LCD color profiles + screen masks | Color: Raw, AGB-001, AGS-001, AGS-101, gamma 1.6, gamma 2.2. Mask: LCD grid, RGB/BGR subpixel, diffusion, scanlines, Trinitron. Mask intensity |
| `frame_interpolator` | High-rate presentation: intermediate frames blended from the previous presented frame, paced to the target rate (no-op at 60 Hz) | 60 / 120 / 144 / display-native / uncapped; smoothing Off / Blend / Motion-adaptive |
| `hud_anchoring` | Moves HUD sprites toward the screen edges for widescreen, driven by a list of `HudZone`s you register | Original centered / edge-anchored, horizontal offset, zones |

### OSD
| Module | What it does | Options |
|---|---|---|
| `perf_hud` | Performance overlay: FPS, frametime, min/max/avg, 120-frame graph, F10 cycles modes | Mode, position (corners, bottom bar, free drag), theme |

### Controls
| Module | What it does | Options |
|---|---|---|
| `input_enhancements` | Keyboard and gamepad remapping, profiles, analog stick | Analog: digital / 8-way / walk-run. Deadzone. Gamepad profiles (Standard, Nintendo, PlayStation, ShoulderSwap, Triggers, Custom) with `pad_a/pad_b/pad_l/pad_r`. Keyboard presets. Saved to `keybinds.ini` (`[player1]`, `[turbo]`) |
| `dialog_turbo` | Auto-pulses A/B while a key is held (to skip text); `input_enhancements` drives it from a configurable key / pad button (default Tab / Y) | Enable, speed 1..4, held flag, turbo key/button |

### Audio
| Module | What it does | Options |
|---|---|---|
| `audio_dsp` | DSP chain (biquad EQ, anti-aliasing, stereo width, soft limiter) | Presets Flat / Warm Retro / Crisp Modern / Bass Boost; bass, mid, treble dB; width; limiter |
| `hd_audio_player` | Replaces music with WAV files chosen by `song_id` | `[tracks]` ini map; master/BGM/SFX volume; `target_sample_rate` |
| `audio_device` | Processes a runtime's SDL *pull-callback* audio: wraps the callback at `SDL_OpenAudioDevice` and runs the pipeline on the audio thread (real-time safe) | n/a |
| `audio_pipeline` | `process_audio_block()`: HD mix + DSP in one call, stereo or mono, at the real device rate | n/a |

### Utilities
| Module | What it does |
|---|---|
| `savestate_thumbnails` | Saves and queries per-slot BMP thumbnails; base directory is configurable (`set_base_directory`) |
| `hooks` | Optional integration callbacks supplied by the host |
| `relaxed` | `Relaxed<T>`: copyable relaxed-atomic wrapper used by settings that an audio thread reads |

### SDL interposition
| Module | What it does |
|---|---|
| `sdl_interpose` | Handler table for six SDL functions (`PollEvent`, `UpdateTexture`, `RenderCopy`, `RenderPresent`, `QueueAudio`, `OpenAudioDevice`) plus `sdl_real()` for the originals. Linux: `GBAIMP_DEFINE_LD_WRAPS()` + `-Wl,--wrap`. Windows: Import Address Table patching (`install_sdl_interposers`) |

### Scripts (`tools/`)
See [`tools/README.md`](tools/README.md). Python 3 helpers for control-flow tracing, coverage
auditing, RAM-dispatch generation and symbol extraction, plus a small `verify_rom_hash` C++ tool (`<file> <expected_sha1>`).

## Quick start

```cmake
add_subdirectory(external/gba-improvements)
target_link_libraries(my_game PRIVATE gbaimp::gba_improvements)
```
```cpp
gbaimp::hooks().save_settings    = []{ /* persist your config */ };
gbaimp::hooks().is_input_blocked = []{ return overlay_is_open(); };
gbaimp::hooks().frame_counter    = []{ return emulated_frames(); };

// Describe which sprites belong to the HUD (see docs/MODULES.md, HudZone)
gbaimp::HudZone left; left.direction = -1; left.coord_x_max = 60; left.coord_y_max = 40;
gbaimp::HudAnchoring::instance().add_zone(left);
```
All hooks are optional. The library provides the SDL interposition *mechanism* (`gbaimp/sdl_interpose.h`);
the handlers that decide what to do in each SDL call stay in the host. See [`docs/INTEGRATION.md`](docs/INTEGRATION.md)
and the runnable [`examples/minimal_host`](examples/README.md).

## Building and testing
```sh
cmake -S . -B build -DGBAIMP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
`GBAIMP_BUILD_TESTS`, `GBAIMP_BUILD_TOOLS` and `GBAIMP_BUILD_EXAMPLES` default to ON when this is the top-level project and OFF when it is
pulled in with `add_subdirectory`; `-DGBAIMP_SANITIZE=thread|address|undefined` builds with a sanitizer (GCC/Clang). Tests are plain C++17 (a tiny assert harness, no framework) and
need neither a display nor an audio device. Requirements: CMake 3.16+, a C++17 compiler, SDL2.

## Documentation
- [`docs/INTEGRATION.md`](docs/INTEGRATION.md): using the library in a new project (CMake, hooks, SDL intercepts, per-frame input).
- [`docs/MODULES.md`](docs/MODULES.md): API and options of each module.
- [`docs/GBARECOMP_CONTRACT.md`](docs/GBARECOMP_CONTRACT.md): what the library expects from a `gbarecomp` runtime (hooks, per-frame callback, SDL call sites).
- [`examples/README.md`](examples/README.md): the minimal host and how to wire a real one.
- [`tools/README.md`](tools/README.md): the helper scripts and `verify_rom_hash`.
- [`docs/ROADMAP.md`](docs/ROADMAP.md): what is done and what is still future work.
- [`CHANGELOG.md`](CHANGELOG.md): what changed.

## License
Released under the [MIT License](LICENSE). See [`NOTICE.md`](NOTICE.md) for third-party notes and
open provenance items (including a font that should be replaced with an original or CC0 one
before a formal release).

A host that links `gbarecomp` is bound by `gbarecomp`'s PolyForm Noncommercial terms for the
combined work; this library's MIT license is compatible with that and imposes nothing extra on the
host. This library itself contains no `gbarecomp` code.
