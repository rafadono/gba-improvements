# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project has no tagged release yet, so versions below are descriptive.

## [Unreleased]

Working-tree changes on top of the initial import.

### Added
- **Build kit** for host projects: `cmake/GbaimpHost.cmake` (`gbaimp_add_host()`: library link, `--wrap` options, MSVC recomp-ui symbol rename, post-build copies, test executable), parametrised templates in `templates/host/` (`CMakeLists.txt`, `build.ps1` that propagates the exit code of failing commands, `build.sh`, `docker/Dockerfile`, CI workflow), `tools/new_host/new_host.cmake` to instantiate them for a new game, `docs/NEW_HOST.md`, and the tests `buildkit_host_cmake` and `buildkit_new_host`.
- **`gbaimp/game_host.h`** (`GameHostConfig`, `install_game_host`, `game_host_stats`, `is_gba_game_texture`, `default_game_viewport`): the generic SDL handlers (game image, colour correction, letterbox, mask, interpolation, performance HUD, audio, diagnostics) with optional host callbacks for what is game specific; test `test_game_host`.
- **`gbaimp/runtime_ui_glue.h`** (`RuntimeUiGlueOptions`, `create_standard_ui`, `GBAIMP_RUNTIME_UI_WRAP`, `GBAIMP_RUNTIME_UI_RENAME_FORWARDER`): header-only tuning of the runtime settings menu for hosts that build recomp-ui.
- **SDL interposition layer** (`gbaimp/sdl_interpose.h`): `SdlFunctions` / `SdlHandlers` handler table for `SDL_PollEvent`, `SDL_UpdateTexture`, `SDL_RenderCopy`, `SDL_RenderPresent`,
  `SDL_QueueAudio` and `SDL_OpenAudioDevice`; `sdl_real()`; `set_sdl_handlers()` / `sdl_handlers()`; Linux `GBAIMP_DEFINE_LD_WRAPS()` macro for `-Wl,--wrap`;
  Windows `install_sdl_interposers()` / `uninstall_sdl_interposers()` / `sdl_interposers_patched_slots()` (Import Address Table and delay-load patching) with `SdlInstallStatus` / `SdlInstallOptions`.
- **Pull-callback audio** (`gbaimp/audio_device.h`): `wrap_audio_device_spec()`, `finish_audio_device_wrap()`, `open_audio_device_processed()` (drop-in `SDL_OpenAudioDevice` handler that
  wraps a mono / stereo S16 callback and runs `process_audio_block` on the audio thread), `queue_audio_processed()` for push mode, `audio_wrap_stats()`, `reset_audio_wrap_stats()`, `set_audio_metering()`.
- **Mono / rate-aware audio**: `process_audio_block(samples, n_int16, channels, sample_rate)`, `AudioDsp::process_mono()` (separate mono and stereo filter state), `HdAudioPlayer::mix_audio()` with
  mono fold-down and linear resampling to the device rate.
- **`gbaimp/relaxed.h`**: `Relaxed<T>`, a copyable relaxed-atomic wrapper; `AudioDspSettings` and `HdAudioSettings` scalar fields use it.
- **Thread-safety contract** for the audio modules (control thread + one audio thread): audio-thread-owned filter coefficients, lock-free HD track hand-over, single-consumer `process_*` / `mix_audio`.
- **`pixel_art_upscaler.h`** (`PixelArtUpscaler`, `UpscaleScale`), replacing the former `xbrz_filter` module (see Changed).
- **`dialog_turbo` driven by `InputEnhancements`**: configurable turbo key / pad button (`settings().turbo_key`, `turbo_pad`, `set_turbo_key`, `set_turbo_key_from_name`, `set_turbo_pad`),
  stored in the `[turbo]` section of `keybinds.ini`; `set_dialog_turbo()` to inject an instance.
- **`InputEnhancements`**: `set_keybinds_path`, `set_autosave_keybinds`, constructor taking a keybinds path.
- **`FrameInterpolator`**: `set_present_hook`, `release_resources`, `readbacks()`, `extra_presents()`.
- **Independent instances**: every module class has a public constructor (private in 0.1.0, for example `PerfHud`) (`PerfHud hud;`), covered by `tests/test_instances.cpp`.
- **Build**: `cmake/GbaimpFindSDL2.cmake` (target `gbaimp_sdl2`; existing `SDL2::SDL2`, config package, module, pkg-config or cache variables), options `GBAIMP_BUILD_TOOLS`, `GBAIMP_BUILD_EXAMPLES`,
  `GBAIMP_SANITIZE`; tests and ctest names prefixed `gbaimp_` when used as a subproject.
- **Tests**: `audio_pipeline`, `audio_device`, `audio_threads`, `frame_interpolator`, `instances`, `perf_hud`, `pixel_art_upscaler`, `sdl_interpose`, plus extended `audio_dsp`, `hd_audio_player`, `input_enhancements`, `screen_filters`.
- **Tools**: self-contained `verify_rom_hash` with its own SHA-1 (`sha1.h`), `verify_rom_hash_sha1_vectors` and `verify_rom_hash_cli` tests.
- **Example**: `examples/minimal_host` (`gbaimp_example_minimal`, ctest `example_minimal_headless`).
- **Docs**: `docs/GBARECOMP_CONTRACT.md`, `NOTICE.md`, `LICENSE` (MIT), this changelog; rewritten README, INTEGRATION, MODULES, ROADMAP.

### Changed
- `xbrz_filter` renamed to `pixel_art_upscaler`: the implementation is an original edge-aware Scale2x / EPX-style filter, never xBRZ. `gbaimp/xbrz_filter.h` remains with the aliases
  `XbrzUpscaler`, `XbrzScale` and the enumerators `Xbrz2x`..`Xbrz5x` (same values as `X2`..`X5`), not marked `[[deprecated]]`.
- `FrameInterpolator` now blends intermediate frames from the previously presented frame (back-buffer readback into a streaming texture); `SmoothBlend` is a uniform cross-fade,
  `MotionAdaptive` blends only the pixels that changed noticeably. Previously it did not blend real frames.
- `process_audio_block(samples, n)` keeps meaning interleaved stereo at `HdAudioSettings::target_sample_rate`; the new overload covers mono and other device rates.
- `HdAudioPlayer::trigger_song` stops the music when the id is unmapped or its file cannot be loaded (the failure is remembered until the next `load_track_mapping`).
- `AudioDspSettings` defaults now equal the `CrispModern` preset (default stereo width 1.3, was 1.25).
- `keybinds.ini`: the gamepad mapping is no longer stored there (the host persists it); a stray `[gamepad]` section in an old file is ignored. `[turbo]` holds the turbo key and pad button.
- `verify_rom_hash` no longer needs the host's `gbarecomp` headers; `GBAIMP_GBARECOMP_INCLUDE` is deprecated and ignored.
- CI workflow extended to build and test on Ubuntu (GCC) and Windows (MSVC, pinned SDL2 development package). It has not been run on GitHub yet.

### Removed
- `src/xbrz_filter.cpp` and `tests/test_xbrz.cpp` (replaced by `pixel_art_upscaler`).

## [0.1.0] - 2026-10-10

Initial import (commit "Initial commit: GBA improvements library and tools").

- Library target `gbaimp::gba_improvements` (C++17, SDL2, namespace `gbaimp`): `xbrz_filter` (pixel-art upscaler, later renamed), `screen_filters` (colour profiles, screen masks), `frame_interpolator`,
  `hud_anchoring` (configurable `HudZone` list), `perf_hud`, `input_enhancements` (keyboard / gamepad remapping, analog modes), `dialog_turbo`, `audio_dsp`, `hd_audio_player`, `audio_pipeline`,
  `savestate_thumbnails` (configurable base directory), `hooks`.
- Unit tests, one executable per module (`GBAIMP_BUILD_TESTS`), run by `ctest`.
- Python tools: `trace_control_flow.py`, `merge_coverage_toml.py`, `audit_code_coverage.py`, `gen_ram_dispatch.py`, `gen_symbols.py`, parametrized by command-line arguments; `verify_rom_hash` C++ tool.
- Docs: README, `docs/INTEGRATION.md`, `docs/MODULES.md`, `docs/ROADMAP.md`.
