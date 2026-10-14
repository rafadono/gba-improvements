# Roadmap

## Done
- [x] Library builds with CMake and SDL2; `GBAIMP_BUILD_TESTS` unit tests (one executable per module, run by `ctest`); CI workflow for Ubuntu (GCC) and Windows (MSVC) authored in `.github/workflows/ci.yml` (it has not been run on GitHub yet).
- [x] Game-agnostic HUD anchoring: `HudZone` list with `HudAnchoring::add_zone/clear_zones/zones`.
- [x] Generic gamepad API: `pad_a/pad_b/pad_l/pad_r`, `GamepadProfile::Triggers`.
- [x] Configurable HD audio output rate: `HdAudioSettings::target_sample_rate`.
- [x] Mono / rate-aware audio: `process_audio_block(samples, n, channels, sample_rate)`, `AudioDsp::process_mono`, HD track fold-down and resampling.
- [x] Pull-callback audio support: `audio_device.h` wraps the runtime's SDL audio callback and processes each buffer on the audio thread (real-time safe), plus `queue_audio_processed` for push mode.
- [x] Thread-safety contract for the audio modules (relaxed-atomic settings via `relaxed.h`, audio-thread-owned filter state, lock-free HD track hand-over), tested with `test_audio_threads` (ThreadSanitizer via `-DGBAIMP_SANITIZE=thread`).
- [x] SDL interposition layer: `sdl_interpose.h` (handler table + `sdl_real()`, Linux `--wrap` macro, Windows IAT patching).
- [x] Frame interpolation really blends the previous presented frame (readback into a streaming texture).
- [x] Dialog turbo driven from `process_keyinput` with a configurable key / pad button (`[turbo]` ini section).
- [x] `xbrz_filter` renamed to `pixel_art_upscaler` (it was never xBRZ); old names kept as aliases.
- [x] Configurable thumbnail location: `SavestateThumbnailManager::set_base_directory`.
- [x] Python scripts parametrized by command-line arguments (see `tools/README.md`); self-contained `verify_rom_hash` with SHA-1 test vectors.
- [x] Runnable example host (`examples/minimal_host`), `docs/GBARECOMP_CONTRACT.md`, MIT license.

- [x] Generic SDL host glue (`game_host.h`) and runtime-menu glue (`runtime_ui_glue.h`), so hosts only supply their game-specific callbacks.

## Ideas for the existing modules
- [ ] `hd_audio_player`: formats beyond WAV, per-track loop points, streaming instead of whole-file decode.
- [ ] `savestate_thumbnails`: selectable image format.
- [ ] `input_enhancements`: more `PadButtonChoice` entries (stick clicks, back/guide), persistence of analog settings in the ini.
- [ ] `frame_interpolator`: use the `viewport` argument (it currently blends the whole output).

## Done: generic text overlay
A host project currently carries an output-resolution text overlay whose generic parts are independent of any game: a TrueType glyph rasterizer with a glyph cache and atlas, a font manager (embedded and user fonts, validation of untrusted font files), and a text layout engine (wrapping, auto-fit, shadow/outline). The plan is to move those generic parts into the library behind a small interface, keeping everything that reads game memory (what text is on screen, when to hide the original) in the host.
- [x] Define the interface (a "page of text + origin + box" in, pixels out) and the atlas ownership rules (`text_types.h`, `text_renderer.h`).
- [x] Move rasterizer, glyph cache, font manager and layout into the library with their tests; the embedded font data and its licences stay in the host (injected with `FontLibrary::add_builtin_font()`).
- [ ] Optional: an opt-in module that bundles a few redistributable fonts, if a host without its own fonts wants one.

## Future work: widescreen core
A reusable widescreen layer is the most valuable missing piece. The architecture (tilemap, background X and sprite X providers, effective viewport computation, pillarboxing) is generic, but it has to be kept separate from per-game knowledge.
- [ ] Define a game-profile interface (for example: whether the current mode should be widened, which layers to extend, which HUD zones to register).
- [ ] Move the generic providers and viewport math into the library; keep each game's profile (addresses, mode detection) in the host project.
- [ ] Confirm which hooks are needed from the recompilation runtime and document them (a first list is in `docs/GBARECOMP_CONTRACT.md`).

## Future work: option registry and UI overlay
- [ ] A declarative option registry (name, type, range, default) that each module fills in.
- [ ] A generic ini persistence layer and an overlay (for example ImGui-based) generated from the registry, so a host does not need to know every module's settings individually.

## Out of scope
Game-specific logic: which memory holds the dialogue, ROM addresses, mode detection, per-game HUD geometry. These belong in the host project; only general techniques can be documented as guides.

## Open items
- [x] License: MIT (see `LICENSE`).
- [ ] Replace the 5x7 bitmap font in `perf_hud.cpp` with an original or CC0 one (see `NOTICE.md`).
- [ ] Run the CI workflow on GitHub and fix whatever it finds (it has only been authored so far).
