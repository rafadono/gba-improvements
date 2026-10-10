# Modules

All modules are singletons (`Class::instance()`) with a live-editable `settings()`. Namespace `gbaimp`, headers in `include/gbaimp/`.

## Video

### `xbrz_filter.h`: `XbrzUpscaler`
- `set_scale(XbrzScale)`: `None`, `Xbrz2x`..`Xbrz5x`; `is_enabled()`, `scale()`.
- `process(src, w, h, dst_buffer, out_w, out_h)`: scales a 32-bit pixel buffer. `None` copies the input. Output is `w*f` by `h*f`.
- No SDL dependency. The host decides where to call it.

### `screen_filters.h`: `ScreenFilters`
- `ColorProfile`: `Raw`, `Agb001`, `Ags001`, `Ags101`, `MisterGamma16`, `MisterGamma22`. A per-channel LUT is applied with `apply_color_correction(rgb24, w, h)` (no-op for `Raw`). `MisterGamma22` is an identity mapping.
- `ScreenMaskType`: `Off`, `LcdGrid`, `SubpixelRgb`, `SubpixelBgr`, `LcdDiffusion`, `CrtScanlines`, `CrtTrinitron`, drawn with `render_mask(renderer, viewport, game_w, game_h)`.
- `set_mask_intensity` clamps to 0.05..1.0 (default 0.40).

### `frame_interpolator.h`: `FrameInterpolator`
- `FramerateMode`: `Fps60`, `Fps120`, `Fps144`, `DisplayNative`, `Uncapped`. `MotionSmoothingMode`: `Off`, `SmoothBlend`, `MotionAdaptive`.
- `on_present(renderer, viewport)` blends with the previous frame and paces presentation; `target_fps()`, `target_frametime_ms()`, `detected_display_info()`.
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
- Persistence: `load_keybinds(path)` / `save_keybinds(path)` (default `keybinds.ini`). Sections: `[player1]` (key names such as `A = X`) and `[gamepad]` (`profile = N`, `pad_a/pad_b/pad_l/pad_r = N`). If `profile` is a non-Custom profile, that profile's mapping is re-applied after loading. The constructor loads `keybinds.ini` from the working directory.
- `process_axis_input(x, y, keyinput)` converts a stick to the four active-low direction bits.
- `process_keyinput(host_keys)` returns the final active-low `KEYINPUT`. While `hooks().is_input_blocked` returns true it returns `0x03FF`.

### `dialog_turbo.h`: `DialogTurbo`
- `set_enabled`, `set_held`, `set_skip_speed(1..4)` (clamped).
- `process_keyinput(keyinput, frame_index)` clears the A and B bits on pulse frames while enabled and held (speed >= 2: every even frame; speed 1: frames where `index % 4 < 2`). Otherwise it returns the input unchanged.

## Audio

### `audio_dsp.h`: `AudioDsp`
- `EqPreset`: `Flat`, `WarmRetro`, `CrispModern`, `BassBoost`; bass/mid/treble gains in dB, `stereo_width` (0..2), `anti_aliasing_filter`, `limiter_enabled`.
- `process_stereo(interleaved, frame_count, sample_rate)` works on interleaved stereo int16; coefficients are recomputed when the sample rate changes. `Flat` with width 1 is transparent; the soft limiter keeps peaks below about 32400.

### `hd_audio_player.h`: `HdAudioPlayer`
- `HdAudioSettings`: `enabled`, `master_volume`, `bgm_volume`, `sfx_volume` (each clamped to 0..1.5 by the setters), `config_path` (default `config/music_tracks.ini`), `target_sample_rate` (default 65536). Loaded tracks are converted to stereo int16 at `target_sample_rate`, so set it to your pipeline rate before the first `trigger_song`.
- `load_track_mapping(path)` reads the `[tracks]` section: `song_id = path/to/file.wav`. Comments start with `#` or `;` (also after a value); entries outside `[tracks]`, non-numeric ids and empty values are ignored.
- WAV only (`SDL_LoadWAV`); the whole track stays in memory and loops. `trigger_song(id)` stops music when the id is unmapped. Also `stop_music()` and `mix_audio(buffer, n, channels, rate)`.

### `audio_pipeline.h`
- `process_audio_block(int16_t*, size_t)`: `mix_audio` followed by `AudioDsp::process_stereo`, both at `HdAudioSettings::target_sample_rate`.

## Utilities

### `savestate_thumbnails.h`: `SavestateThumbnailManager`
- `set_base_directory(dir)` chooses where thumbnails live (created if missing; an empty string restores the default `saves/thumbnails`). `base_directory()` returns it.
- `capture_thumbnail(slot, rgba, w, h)` writes `<base>/slot_N.bmp`; invalid arguments are ignored.
- `get_slot_metadata(slot)` returns `exists`, `image_path` and `timestamp` (modification time as `YYYY-MM-DD HH:MM:SS`, or `"Empty Slot"`). `get_thumbnail_path(slot)`.

### `hooks.h`
`Hooks` struct (`save_settings`, `is_input_blocked`, `frame_counter`), accessor `hooks()`, helpers `notify_settings_changed()` and `input_blocked()`. See `INTEGRATION.md`.
