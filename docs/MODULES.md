# Modules

All modules are singletons (`Class::instance()`) with a live-editable `settings()`. Namespace `gbaimp`, headers in `include/gbaimp/`.

Every module class also has a public constructor, so a host or a test can create an independent instance (`gbaimp::PerfHud hud;`) that shares nothing with `instance()`. Modules that own SDL or controller resources (`ScreenFilters`, `FrameInterpolator`, `HdAudioPlayer`, `InputEnhancements`) are not copyable; the others keep their implicit copy. Some constructors have side effects on relative paths (`InputEnhancements` reads `keybinds.ini`, `HdAudioPlayer` reads its track config, `SavestateThumbnailManager` creates its directory), so run independent instances from a scratch directory in tests.

## Video

### `pixel_art_upscaler.h`: `PixelArtUpscaler`
- An original, edge-aware pixel-art upscaler in the Scale2x / EPX family. It is **not** xBRZ and contains no xBRZ code. 4x is two 2x passes.
- `set_scale(UpscaleScale)`: `None`, `X2`..`X5` (numeric values 1..5 equal the factor); `is_enabled()`, `scale()`.
- Deprecated compatibility header `xbrz_filter.h` keeps the old names working: `XbrzUpscaler` = `PixelArtUpscaler`, `XbrzScale` = `UpscaleScale`, and the enumerators `Xbrz2x`..`Xbrz5x` (same values as `X2`..`X5`). They are not marked `[[deprecated]]` so host builds stay quiet; new code should use the new names.
- `process(src, w, h, dst_buffer, out_w, out_h)`: scales a 32-bit pixel buffer. `None` copies the input. Output is `w*f` by `h*f`. For invalid input (null `src`, `w <= 0` or `h <= 0`, or an output of more than `INT32_MAX` pixels) at any scale, `dst_buffer` is cleared and `out_w = out_h = 0`.
- No SDL dependency. The host decides where to call it.

### `screen_filters.h`: `ScreenFilters`
- `ColorProfile`: `Raw`, `Agb001`, `Ags001`, `Ags101`, `MisterGamma16`, `MisterGamma22`. A per-channel LUT is applied with `apply_color_correction(rgb24, w, h)` (no-op for `Raw`). `MisterGamma22` is an identity mapping on purpose: the MiSTer profiles use exponent = target gamma / display gamma with the display assumed to be 2.2 (`kMisterDisplayGamma` in `screen_filters.cpp`), so `MisterGamma16` uses `1.6/2.2` and `MisterGamma22` uses `2.2/2.2 = 1`. The tests pin every profile's LUT bit for bit.
- `ScreenMaskType`: `Off`, `LcdGrid`, `SubpixelRgb`, `SubpixelBgr`, `LcdDiffusion`, `CrtScanlines`, `CrtTrinitron`, drawn with `render_mask(renderer, viewport, game_w, game_h)`.
- `set_mask_intensity` clamps to 0.05..1.0 (default 0.40).

### `frame_interpolator.h`: `FrameInterpolator`
- `FramerateMode`: `Fps60`, `Fps120`, `Fps144`, `DisplayNative`, `Uncapped`. `MotionSmoothingMode`: `Off`, `SmoothBlend`, `MotionAdaptive`.
- `on_present(renderer, viewport)`: call once per emulated frame after the game image is in the back buffer and before the host's own `SDL_RenderPresent`. At 60 Hz, a display that is not faster, or smoothing `Off` it returns immediately (no readback, no waiting). Otherwise it reads the back buffer into a streaming texture (`SDL_RenderReadPixels`), and when a previous frame is available it presents 1 (120/144 Hz) or 3 (240 Hz) intermediate frames blended from the previous toward the current one, each held for one sub-frame period, then restores the real frame into the back buffer for the host to present. `SmoothBlend` is a uniform cross-fade; `MotionAdaptive` blends only the pixels that changed noticeably (static pixels such as text stay sharp; CPU pass). A resize (or a different renderer) restarts the history; a gap longer than 100 ms does too. Renderer viewport, clip, scale and logical size are saved and restored; a renderer that cannot read pixels, or one targeting a texture, makes it a no-op. The `viewport` argument is currently unused (the whole output is blended).
- Intermediate frames go through `SDL_RenderPresent` by default; a host that intercepts that call can pass the real present with `set_present_hook(fn)` (re-entrant `on_present` calls are ignored while the default present runs). Call `release_resources()` before destroying the renderer. `readbacks()` / `extra_presents()` are counters for diagnostics.
- `target_fps()`, `target_frametime_ms()`, `detected_display_info()`.
- Keep game simulation at the native rate; only presentation runs faster.

### `hud_anchoring.h`: `HudAnchoring`, `HudZone`
- `HudAnchorMode`: `OriginalCentered` (no-op), `WidescreenAnchored`. `set_horizontal_offset` clamps to 0..96 (default 32).
- Zone registry, empty by default (anchoring is then a no-op): `add_zone(const HudZone&)`, `clear_zones()`, `zones()`.
- `HudZone` fields (rectangle bounds are inclusive and default to unbounded via `INT_MIN`/`INT_MAX`):
  - `direction`: -1 shifts x left (`x -= offset`), +1 shifts right.
  - `use_for_coords`, `coord_x/y_min/max`: used by `adjust_sprite_coordinate(x, y, w, h, is_hud)`.
  - `use_for_oam`, `oam_x/y_min/max`: used by `process_oam_entry(attr0, attr1)`, tested after OAM wrap-around decoding (x >= 256 becomes x-512, y >= 160 becomes y-256).
  - `oam_clamp_enabled`, `oam_clamp_x`: after shifting, x is clamped so it never goes past `oam_clamp_x` (lower bound when `direction < 0`, upper bound when `direction > 0`). Only the OAM path clamps.
- Zones are tested in registration order and the first match wins. Only the low 9 bits of `attr1` are rewritten; `attr0` is never changed.

## OSD

### `perf_hud.h`: `PerfHud`
- `PerfHudMode`: `Off`, `FpsOnly`, `FpsAndFrametime`, `FullWithGraph` (F10 cycles). `PerfHudPosition`: four corners, `BottomBlackBar`, `FreeDrag`. `PerfHudTheme`: `GlassDark`, `Neon`, `Solid`, `Minimal`.
- `on_frame_present(renderer)`, `handle_mouse_event(event)`, `set_game_viewport(rect)`.
- Draws with its own bitmap font (no SDL_ttf). Calls `hooks().save_settings` via `notify_settings_changed()` after a drag.

## Controls

### `input_enhancements.h`: `InputEnhancements`
- `AnalogMode`: `DigitalOriginal`, `Analog8Way`, `AnalogWalkRun` (a gentle tilt below `walk_threshold` drops direction on alternate frames). `deadzone` clamps to 0.05..0.50 (default 0.18).
- Keyboard: `KeyboardPreset` (`Default`, `Wasd`, `Emulation`, `ArrowsQW`, `Custom`), `set_key_scancode`, `set_key_from_name`, `apply_keyboard_preset`.
- Gamepad: `GamepadProfile` (`Standard`, `Nintendo`, `PlayStation`, `ShoulderSwap`, `Triggers`, `Custom`). Each GBA button has a mapping slot, `settings().pad_a/pad_b/pad_l/pad_r`, holding a `PadButtonChoice` (`ButtonA/B/X/Y`, `LeftBumper`, `RightBumper`, `LeftTrigger`, `RightTrigger`). `apply_gamepad_profile(profile)` fills the slots; `set_pad_a/b/l/r(int)` clamp to 0..7 and switch the profile to `Custom`.
- Persistence: `load_keybinds(path)` / `save_keybinds(path)`; a null/omitted `path` means the configured path (`set_keybinds_path`, default `keybinds.ini`). `apply_keyboard_preset` and `reset_keyboard_defaults` save the keybinds file as a side effect (default, unchanged); call `set_autosave_keybinds(false)` to stop that. `InputEnhancements(const std::string& keybinds_path)` builds an instance bound to a specific file. Sections: `[player1]` (key names such as `A = X`) and `[turbo]` (`key = Tab`, `pad = 3`; `key = None` disables the keyboard trigger). The gamepad mapping (`pad_a/pad_b/pad_l/pad_r`, profile) is not stored in this file: the host persists it with its own settings. A stray `[gamepad]` section in an old file is ignored. The constructor loads `keybinds.ini` from the working directory.
- Dialog turbo trigger: `settings().turbo_key` (`SDL_Scancode`, default Tab) and `settings().turbo_pad` (`PadButtonChoice`, default `ButtonY`), with `set_turbo_key`, `set_turbo_key_from_name`, `set_turbo_pad(int)`. Every `process_keyinput` call drives `DialogTurbo::set_held()` from them (released also when input is blocked) and applies the A/B pulse while held. The instance uses `DialogTurbo::instance()` unless you inject another with `set_dialog_turbo(DialogTurbo*)` (tests, multi-instance hosts); `dialog_turbo()` returns the one in use.
- `process_axis_input(x, y, keyinput)` converts a stick to the four active-low direction bits.
- `process_keyinput(host_keys)` returns the final active-low `KEYINPUT`. While `hooks().is_input_blocked` returns true it returns `0x03FF`. The per-frame cache (keyed on `hooks().frame_counter` and `host_keys`) and the open controller handle are members of the instance; `reset_to_defaults()` drops the cache.

### `dialog_turbo.h`: `DialogTurbo`
- `set_enabled`, `set_held`, `set_skip_speed(1..4)` (clamped). `InputEnhancements::process_keyinput` calls `set_held` for you from the configurable turbo key / pad button; call it yourself only if you do not use `InputEnhancements`.
- `process_keyinput(keyinput, frame_index)` clears the A and B bits on pulse frames while enabled and held (speed >= 2: every even frame; speed 1: frames where `index % 4 < 2`). Otherwise it returns the input unchanged.

## Audio

### `audio_dsp.h`: `AudioDsp`
- `EqPreset`: `Flat`, `WarmRetro`, `CrispModern`, `BassBoost`; bass/mid/treble gains in dB, `stereo_width` (0..2), `anti_aliasing_filter`, `limiter_enabled`.
- `process_stereo(interleaved, frame_count, sample_rate)` works on interleaved stereo int16; coefficients are recomputed when the sample rate changes. `Flat` with width 1 is transparent (output is rounded, not truncated); the soft limiter keeps peaks below about 32400.
- `process_mono(samples, sample_count, sample_rate)` runs the same chain (anti-aliasing low-pass, bass/mid/treble, limiter) on a mono buffer; stereo width does not apply. Mono and stereo keep separate filter state, so one instance can serve both. For identical channels it matches `process_stereo` at width 1.
- Thread safety: the scalar fields of `AudioDspSettings` are relaxed atomics, so `settings().x = ...`, `apply_preset`, `set_*` may be called from the UI/game thread while the audio thread runs `process_*`. Filter coefficients are derived and owned by the processing thread (re-derived when a gain or the sample rate changes: no locks, no allocation). `process_*` is single-consumer: a second concurrent caller returns at once and leaves its buffer unprocessed. `reset()` asks the processing thread to clear the filter histories.
- `AudioDspSettings` defaults equal the `CrispModern` preset (bass 2 dB, mid 0.5 dB, treble 2 dB, width 1.3), which is also what a default `AudioDsp` starts with.

### `hd_audio_player.h`: `HdAudioPlayer`
- `HdAudioSettings`: `enabled`, `master_volume`, `bgm_volume`, `sfx_volume` (each clamped to 0..1.5 by the setters), `config_path` (default `config/music_tracks.ini`), `target_sample_rate` (default 65536). Loaded tracks are converted to stereo int16 at `target_sample_rate`, so set it to your pipeline rate before the first `trigger_song`.
- `mix_audio(buffer, sample_count, channels, sample_rate)`: `sample_count` is the total number of int16 values; `channels` 1 or 2 (other layouts only get the SFX volume applied). The track is stored as interleaved stereo at `target_sample_rate`; it is folded down ((L+R)/2) for a mono buffer and resampled on the fly (linear interpolation) when `sample_rate` differs from the rate it was converted to, so a device running at another rate plays it at the right speed. A `sample_rate <= 0` means the track rate. The position is kept in track frames and loops.
- `load_track_mapping(path)` reads the `[tracks]` section: `song_id = path/to/file.wav`. Comments start with `#` or `;` (also after a value); entries outside `[tracks]`, non-numeric ids and empty values are ignored.
- WAV only (`SDL_LoadWAV`); the whole track stays in memory and loops. `trigger_song(id)` stops music when the id is unmapped **or when its file cannot be loaded** (silently; the failure is remembered until the next `load_track_mapping`, so a mapped-but-missing WAV costs one probe). `has_track(id)` tells whether a mapping exists; `current_song_id()` is the song the control side asked for (0 = none). Also `stop_music()` and `mix_audio(buffer, n, channels, rate)`.
- Thread safety: the control calls (`trigger_song`, `stop_music`, `load_track_mapping`, setters) run on the game/UI thread (they may allocate and read files); `mix_audio` runs on the audio thread and never allocates, locks or touches files. A decoded track is handed over by an atomic pointer exchange (replaced tracks are retired to a small lock-free graveyard and freed by the control side), volumes/flags are relaxed atomics. `mix_audio` is single-consumer like `process_*`. Tested under ThreadSanitizer (`-DGBAIMP_SANITIZE=thread`, `tests/test_audio_threads.cpp`).

### `audio_device.h`: process a runtime's pull-callback audio
Many runtimes (gbarecomp by default) open their SDL device with an audio *callback*; nothing is ever pushed with `SDL_QueueAudio`. Interpose `SDL_OpenAudioDevice` (`SdlHandlers::open_audio_device`) and register `gbaimp::open_audio_device_processed` as the handler. For a playback device opened with a signed 16-bit mono or stereo callback it substitutes a trampoline that runs the runtime's own callback first (userdata preserved) and then `process_audio_block(buffer, len/2, channels, obtained_freq)` on the bytes it produced. Push mode (`callback == nullptr`), other formats and channel counts, and capture devices pass through untouched. Building blocks: `wrap_audio_device_spec(SDL_AudioSpec* want)` (returns the opaque `AudioDeviceWrap*` or null) and `finish_audio_device_wrap(wrap, device, obtained)`. `queue_audio_processed` is the matching `SDL_QueueAudio` handler for direct/push mode: it uses the channel count and rate the device was opened with (remembered per device), forwards unknown devices untouched. `audio_wrap_stats()` (callbacks, frames processed, `queue_calls`) and an optional before/after level meter (`set_audio_metering(true)`: RMS and peak) are relaxed atomics.

Real-time contract of the trampoline: no allocation, no blocking locks, no file access; the singletons are constructed when the device is opened, not on the audio thread.

### `audio_pipeline.h`
- `process_audio_block(int16_t*, size_t)`: interleaved **stereo** at `HdAudioSettings::target_sample_rate`: `mix_audio` followed by `AudioDsp::process_stereo`.
- `process_audio_block(int16_t* samples, size_t sample_count, int channels, int sample_rate)`: channel- and rate-aware form (`sample_count` = total int16 values; `channels` 1 or 2, anything else leaves the buffer untouched; `sample_rate` <= 0 means the target rate). Mono buffers get the HD track folded down and `AudioDsp::process_mono`. Use this for a mono device buffer, for example mono 65536 Hz.

## Utilities

### `savestate_thumbnails.h`: `SavestateThumbnailManager`
- `set_base_directory(dir)` chooses where thumbnails live (created if missing; an empty string restores the default `saves/thumbnails`). `base_directory()` returns it.
- `capture_thumbnail(slot, rgba, w, h)` writes `<base>/slot_N.bmp`; invalid arguments are ignored.
- `get_slot_metadata(slot)` returns `exists`, `image_path` and `timestamp` (modification time as `YYYY-MM-DD HH:MM:SS`, or `"Empty Slot"`). `get_thumbnail_path(slot)`.

### `hooks.h`
`Hooks` struct (`save_settings`, `is_input_blocked`, `frame_counter`), accessor `hooks()`, helpers `notify_settings_changed()` and `input_blocked()`. See `INTEGRATION.md`.

The callbacks are `std::function` on purpose. They are called at most a few times per emulated frame (and `save_settings` only on a user action), so the indirect call is negligible, while `std::function` lets a host pass lambdas that capture its own state without any registration boilerplate.

## Integration helpers

### `sdl_interpose.h`: SDL interposition
One handler / real-function design for every platform, used to observe or alter the SDL calls that *other* code (a static runtime, a UI library) makes. Six functions are covered: `SDL_PollEvent`, `SDL_UpdateTexture`, `SDL_RenderCopy`, `SDL_RenderPresent`, `SDL_QueueAudio`, `SDL_OpenAudioDevice`.
- `SdlFunctions` (alias `SdlHandlers`): a table of function pointers with SDL's own signatures (`poll_event`, `update_texture`, `render_copy`, `render_present`, `queue_audio`, `open_audio_device`). In a handler table a null entry means "do not interpose this function".
- `sdl_real()`: the original SDL implementations. A handler must call SDL **only** through it (never through the public `SDL_X` symbol, which would re-enter the handler).
- `set_sdl_handlers(handlers)` / `sdl_handlers()`: register / read the handler table (does not patch anything; passing a default-constructed table removes all handlers). Dispatch reads an immutable snapshot, so swapping handlers while SDL is being called is safe.
- **Linux / GNU ld**: expand `GBAIMP_DEFINE_LD_WRAPS()` once in one host `.cpp` (outside any namespace) and link with `-Wl,--wrap=SDL_PollEvent -Wl,--wrap=SDL_UpdateTexture -Wl,--wrap=SDL_RenderCopy -Wl,--wrap=SDL_RenderPresent -Wl,--wrap=SDL_QueueAudio -Wl,--wrap=SDL_OpenAudioDevice`. The macro defines the `__wrap_SDL_X` shims and fills the real table from `__real_SDL_X`. Omitting the `SDL_OpenAudioDevice` flag still links (that function is simply not interposed; its `__real_` symbol is weak).
- **Windows**: `install_sdl_interposers(handlers, &status, &options)` patches the Import Address Table slots (also delay-load imports) that point at `SDL2.dll`'s exports, in the main executable and in every module loaded at that moment, for each function with a non-null handler. It is idempotent (call it again after loading a plugin DLL), returns true when at least one slot is patched and reports details in `SdlInstallStatus` (`modules_scanned`, `slots_patched`, `slots_already_patched`, `slots_skipped_foreign`, `slots_failed`, `delay_load_unresolved`, `patched`, `error`). `SdlInstallOptions` sets the import DLL name (default `SDL2.dll`) and a log sink. `uninstall_sdl_interposers()` restores the slots (for tests); `sdl_interposers_patched_slots()` counts them. On other platforms `install_sdl_interposers` does nothing and returns false (`status.applicable == false`).
- Windows limitations (reported, never fatal): only IAT / delay-load imports are intercepted (not `GetProcAddress` lookups or a statically linked SDL); modules loaded after the install are not patched; a slot that does not hold SDL's own export (a foreign hook) is left alone and counted as foreign; security software that write-protects import tables can make a patch fail.
- Thread safety: install / set / uninstall are serialized by an internal mutex; intended use is once, early in `main()`. Handlers run on whatever thread calls SDL.

### `game_host.h`: `GameHostConfig`, `install_game_host`
The six SDL handlers that route a runtime's SDL calls into the library (game-image detection, colour correction, letterboxing with the viewport reported to `PerfHud`, screen mask, frame interpolation, performance HUD, pull-callback and direct-queue audio, call counters and audio metering for diagnostics) so a host does not carry its own copy. A host fills a `GameHostConfig` with the optional callbacks that are specific to its game (`on_poll`, `on_event`, `on_native_frame`, `compute_viewport`, `after_game_image`, `on_present_begin`, `render_present_overlay`, `before_present`, `print_stats`) and calls `install_game_host()` once. The Linux `--wrap` shims (`GBAIMP_DEFINE_LD_WRAPS()`) stay in the host because they must be expanded in the final executable. `is_gba_game_texture()` and `default_game_viewport()` are public helpers.

### `capture_harness.h`: `capture::configure`, `on_emulated_frame`, `on_before_present`
Headless capture and scripted-input harness for verifying a game without playing it (developer tooling, environment-gated, inert when no variable is set). Every variable is `<prefix>_<NAME>` with the prefix chosen by the host (`CaptureConfig::env_prefix`). Features: PNG capture of chosen emulated frames (`output` = final window image, `native` = raw PPU frame), capture variants (window size and live settings through `CaptureConfig::apply_setting`), window size, exit at a frame, scripted GBA input expanded into a runtime input-replay trace, scripted host key presses, settings overrides, savestate load/save and fast-forward arguments (`extra_runtime_args()`). The PNG writer is self-contained (stored deflate). The host wires `on_emulated_frame`, `wants_native_frame` / `on_native_frame` and `on_before_present` (the last two are `GameHostConfig` callbacks). Pure parsers are public for tests. Full reference and adoption steps: `docs/CAPTURE_HARNESS.md`; runner and image scripts: `tools/capture/`.

### `runtime_ui_glue.h`: `create_standard_ui`
Header-only, compiled only by hosts that build recomp-ui (needs `recomp_runtime_ui.h` and `GBARECOMP_RUNTIME_UI`). Adjusts the view-mode labels and removes the RESUME row of the menu that `recomp_runtime_ui_create_standard()` builds, with the macros `GBAIMP_RUNTIME_UI_WRAP` (GNU ld `--wrap`) and `GBAIMP_RUNTIME_UI_RENAME_FORWARDER` (MSVC compile-time symbol rename) to interpose that call.

### `relaxed.h`: `Relaxed<T>`
A copyable, implicitly convertible wrapper around `std::atomic<T>` with relaxed ordering. The settings structs read by an audio thread while the UI thread writes them (`AudioDspSettings`, `HdAudioSettings`) use it for scalar fields, which keeps the field-style API (`s.enabled = true; float g = s.gain;`) while making each access data-race free.

## Tests

`ctest` runs one executable per module (`tests/test_<module>.cpp`, built on `tests/test_harness.h`). They need no display or audio device (SDL dummy drivers, software renderers). The tests are on by default only for a top-level build; a parent project turns them on with `-DGBAIMP_BUILD_TESTS=ON` (and must call `enable_testing()` itself). As a subproject the targets and test names are prefixed `gbaimp_` to avoid collisions.

The suites are `pixel_art_upscaler`, `screen_filters`, `audio_dsp`, `hd_audio_player`, `dialog_turbo`, `hud_anchoring`, `input_enhancements`, `savestate_thumbnails`, `hooks`, `perf_hud`, `frame_interpolator`, `instances` (independent instances), `audio_pipeline`, `sdl_interpose`, `audio_device` and `audio_threads`; plus, when tools/examples are built, `verify_rom_hash_sha1_vectors`, `verify_rom_hash_cli` and `example_minimal_headless`. `sdl_interpose` and `audio_device` exercise the `--wrap` shims on Linux and the IAT patcher on Windows. `-DGBAIMP_SANITIZE=thread` (or `address`, `undefined`; GCC/Clang only) builds the library and tests with that sanitizer; `audio_threads` is the one that matters for the thread-safety contract.
