# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project has no tagged release yet, so versions below are descriptive.

## [Unreleased]

Working-tree changes on top of the initial import.

### Added
- **`tools/ram_snapshot.py` and `tools/ram_diff.py`**: dump guest memory through the runtime's observe-mode TCP port into a file with metadata, and compare snapshots (`changes`: merged ranges with context; `flags`: mode-flag finder, values constant inside each group of snapshots of one context and different between contexts; noise filters, JSON output). Test `ram_tools_py` (fake TCP server speaking the observe protocol).
- **Capture harness: savestates by PPU frame** (`<P>_SAVESTATE_SAVE_PPU=<frame>:<slot>[,...]`, `<P>_SAVESTATE_LOAD_PPU_FRAME`, `<P>_SAVESTATE_ROM`, `CaptureConfig::rom_path`, `capture::configuration_error()`, pure `parse_savestate_requests` / `plan_savestate_pumps` / `build_assist_script` / `parse_savestate_meta` / `format_savestate_meta` / `savestate_meta_path`): several slots per run at absolute PPU frames, the loaded state's frame taken from a `<state>.meta` sidecar written next to the slot file or from an explicit variable, and clear errors for frames already passed; the runners gained `-P` / `-L` (`-SavePpu` / `-LoadPpuFrame`). Test `test_capture_harness`.
- **Widescreen margin policy** (`WsMarginPolicy`, `WsMarginPolicySet`, `WidescreenProfile::margin_policy`, `Widescreen::margin_policy()` / `margin_policy_for_bg()`): declarative per-scene choice of what each BG layer (and OBJ) shows outside the central 240 pixels (extend the wrapped ring, blank, or leave to the runtime). `default_bg_x_provider` answers from a cached atomic word, so the per-pixel path calls nothing; a profile without `margin_policy` behaves as before. Test `test_widescreen`.
- **`gbaimp/text_history.h`** (`TextHistory`, `TextHistoryConfig`, `TextHistoryControls`, `TextHistoryStyle`, `wrap_text_fixed_advance`, `register_text_history_settings`): the generic scrollable text-history sidebar (entry list with dedup and bound, left-docked translucent panel, scroll, configurable hotkeys, input blocking while open); the 5x7 font table is now one internal header shared with the performance HUD; test `test_text_history`.
- **`M4aMusicHook::setup_from_rom()`** (`M4aSetupOptions`, `M4aSetupSource`, `setup_source()`, `print_stats(tag)`): one-call start-up (locator, optional fallback address table, bus reader) that reports whether the locator or the fallback was used.
- **`gbaimp/widescreen.h`** (`Widescreen`, `WidescreenProfile`, `WidescreenBindings`, `widescreen_viewport`, `widescreen_compute_viewport`, `default_tilemap_provider`, `default_bg_x_provider`, `default_obj_attr_x_provider`) and header-only **`gbaimp/widescreen_gbarecomp.h`**: the generic widescreen core (view / presentation state, provider install and withdrawal, derived `HudAnchoring` state, presented-frame counter, effective viewport, function-entry hook chaining) driven by a game profile and bindings to the runtime's `g_ws_*` values; test `test_widescreen`.
- **`gbaimp/capture_harness.h`** (`capture::configure`, `CaptureConfig`, `on_emulated_frame`, `on_native_frame`, `on_before_present`, `extra_runtime_args`, `settings_overrides`, `encode_png_rgb` / `write_png_rgb` and the public parsers): environment-gated headless capture and scripted-input harness with a host-chosen variable prefix; `docs/CAPTURE_HARNESS.md`, generic runners and image tools in `tools/capture/`; test `test_capture_harness`.
- **`gbaimp/ram_dispatch.h`, `gbaimp/ram_dispatch_gbarecomp.h`** (`RamDispatchEntry`, `find_ram_dispatch`, `ram_dispatch_call`, `ram_dispatch_is_sorted`, `GBAIMP_DEFINE_RAM_DISPATCH[_N]`): table search and hook logic of the native RAM-overlay dispatcher; the generated host file keeps only the routines and the table. `tools/gen_ram_dispatch.py` emits the new form, has `--migrate` for existing generated files, and no longer emits duplicate names for an ARM and a Thumb routine at one address (tests: `ram_dispatch`, `ram_dispatch_generated`, `gen_ram_dispatch_py`).
- **`gbaimp/host_main.h`** (`HostMainConfig`, `run_host_main`, `run_gbarecomp_host`, `set_env_default`, `build_run_args`), **`gbaimp/msvc_compat.h`** and **`gbaimp/scaffold_stub.h`**: the common `main()` start-up sequence and the generic host boilerplate (test: `host_boot`).
- **`gbaimp/m4a_music_hook.h`** (`M4aMusicHook`, `HdMusicSink`, `M4aAddresses`, `locate_m4a`, `count_m4a_idents`): HD music trigger driven by the m4a sound engine's song calls, with a ROM-signature locator for the five song-control functions (no fixed addresses); test `test_m4a_music_hook`.
- **`optional/recomp_ui/runtime_ui_imgui_enhanced.cpp`**: the enhanced recomp-ui ImGui menu renderer (draggable window, touch-friendly layout, keybind capture), shared by all hosts. `gbaimp_add_host()` compiles it in place of recomp-ui's stock `runtime_ui_imgui.cpp` when recomp-ui is present; new argument `ENHANCED_IMGUI_SOURCE <file>` overrides it. Hosts no longer keep a copy in `src/`.
- **Tools**: `tools/merge_cache_misses.py` (recompiler miss cache to `[[extra_func]]` entries), `tools/update_symbols.py` (seed plus task/mode symbols to a symbol TSV), `tools/import_decomp_symbols.py` (wrapper of gbarecomp's decompilation symbol importer), `tools/png_to_ppm.py` (PNG to PPM) and `tools/gen_font_data.py` (embed TrueType fonts as C++ arrays), all parametrised by command-line arguments, with the `tools_py` unit test.
- **Build kit** for host projects: `cmake/GbaimpHost.cmake` (`gbaimp_add_host()`: library link, `--wrap` options, MSVC recomp-ui symbol rename, post-build copies, test executable), parametrised templates in `templates/host/` (`CMakeLists.txt`, `build.ps1` that propagates the exit code of failing commands, `build.sh`, `docker/Dockerfile`, CI workflow), `tools/new_host/new_host.cmake` to instantiate them for a new game, `docs/NEW_HOST.md`, and the tests `buildkit_host_cmake` and `buildkit_new_host`.
- **Text engine** (`gbaimp/text_types.h`, `text_fonts.h`, `text_raster.h`, `text_layout.h`, `text_renderer.h`, `text_fade.h`, `text_synth.h`): game-independent output-resolution text. `TextPage` (characters with reveal entries and optional source pen positions, origin, wrap width, line pitch, box) + `TextSettings` go in, `TextRenderer::draw()` draws them with SDL at the output resolution (layout cache, glyph atlas, subpixel positioning, shadow / outline, fades); `FontLibrary` manages injected built-in fonts and untrusted user `.ttf` files; `validate_truetype()` checks files before stb_truetype sees them (table directory, loca / glyf, cmap subtables, kern, no CFF); vendored `third_party/stb/stb_truetype.h`. Tests `text_fonts`, `text_layout`, `text_renderer`, `text_fade` (synthetic font, no font files).
- **`gbaimp/game_host.h`** (`GameHostConfig`, `install_game_host`, `game_host_stats`, `is_gba_game_texture`, `default_game_viewport`): the generic SDL handlers (game image, colour correction, letterbox, mask, interpolation, performance HUD, audio, diagnostics) with optional host callbacks for what is game specific; test `test_game_host`.
- **`gbaimp/host_settings.h`** (`SettingsRegistry`, `Setting`, `IniExtension`, `register_standard_settings`, `SettingsTargets`): declarative settings registry with get / set by id, `.ini` save / load (file name, header and section order are parameters, legacy and unknown keys tolerated) and the standard panel of every library module with the ids and `.ini` keys hosts already use; test `test_host_settings`.
- **`gbaimp/runtime_ui_settings.h`** (`RuntimeUiBinding`, `runtime_menu_is_open/open/close/toggle`): header-only recomp-ui / `RunOptions` adapter for the registry, for hosts that build recomp-ui.
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
