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

## Text

Output-resolution text: the host describes "a page of text + origin + box", the library draws it with SDL at the real output resolution (instead of the upscaled 240x160 bitmap glyphs). Namespace `gbaimp::text`. Nothing here reads game memory or knows a text encoding; the host decides *what* text is on screen and *when* to hide the original.

### `text_types.h`: the interface (`TextPage`, `TextChar`, `TextSettings`, `DrawInfo`)
- `TextPage`: `valid`, `chars` (reading order, spaces and line breaks included), `revealed` (how many reveal entries are visible), `page_id` (layout-cache key, change it for a new page), `origin_x/y` (first pen position, source px), `wrap_px` (width of the text area), `line_pitch`, `max_lines`, `cell_height` / `baseline_row` (the source glyph cell, defaults 16 / 11), optional `box_x/y/w/h`, and the colours the original draws the page with (`native_text_rgba`, `native_shadow_rgba`, `native_highlight_rgba`, 0 = unknown). Source px are the units of the 240x160 picture (or of a wider presented image, see `DrawInfo::gba_w`).
- `TextChar`: `cp` (Unicode scalar; `0x0A` line break, `0x20` / `0xA0` space, `0` = not renderable so the page cannot be drawn), `colour` (0 text, 1 highlight), `entry` (reveal index in typewriter order, consecutive from 0, `-1` = never revealed), and an optional source pen position `has_pos` / `gx` / `gy`. When positions are present the source's own line structure is taken from them (and, in keep mode, the source's per-line indents are reproduced); without them the line breaks are the `0x0A` characters.
- `TextSettings` (all live): `enabled`, `family`, `size_em`, `line_spacing`, `letter_spacing`, `wrap` (`KeepSourceBreaks` / `Reflow`), `style` (`Plain` / `Shadow` / `Outline`), `shadow_offset`, `outline_width`, `text_rgba` / `shadow_rgba` (0 = match the page's `native_*` colour, falling back to `fallback_text_rgba` / `fallback_shadow_rgba`), `outline_rgba`, `highlight_rgba`, `opacity`, `box_padding`, `auto_fit`, `min_size_em`. The library keeps no global settings object; the host owns one.
- `DrawInfo`: `renderer`, the output rectangle of the source image (`dst_x/y/w/h`, `gba_w/h` source size), `global_alpha`, and a brightness fade (`fade_mode` as `FadeMode`, `fade_level`, `fade_from_palette`: the fade was derived from the native text colours, so colours matched to them are not faded twice).

### `text_renderer.h`: `TextRenderer`
- `draw(page, settings, info)` draws the revealed part of the page (entries `< page.revealed`) and returns whether anything was drawn. `can_draw(page, settings, info)` is the cheap check a host calls BEFORE hiding the original text (enabled, every character has a glyph, the page fits or can be auto-fitted). `invalidate()`, `rescan_fonts()`, `family_names()`, `family_index(name)`, `fonts()`, and counters (`draws`, `layout_builds`, `glyphs_rasterized`, `atlas_flushes`) for logs and tests.
- Caches: the layout of the current page (keyed on page, settings, output size and font generation) and a glyph atlas keyed on face / glyph / size / subpixel phase / variant. Warm frames rasterise and lay out nothing.
- **Atlas ownership**: the atlas is one `SDL_Texture` owned by the `TextRenderer` that created it, bound to the `SDL_Renderer` of `DrawInfo::renderer`, never shared. A different renderer pointer makes the renderer forget the stale texture (it is not destroyed: its renderer may be gone) and build a new atlas. Call `release_gpu()` before destroying the `SDL_Renderer`; the destructor destroys the texture too, so the renderer must still be alive then. `TextRenderer::instance()` is deliberately leaked (its destructor never runs), so it needs an explicit `release_gpu()` at shutdown. The atlas starts at 1024x1024, doubles to at most 4096x4096 when a page does not fit, and is flushed only before anything of the current frame is queued. Calls are serialised by a mutex; SDL calls stay on the renderer's thread.
- `TextRenderer(FontLibrary*)` lets a host or test use a private font library; independent instances share nothing.

### `text_fonts.h`: `FontLibrary`, `FontFace`, `validate_truetype`
- The library ships **no font data**. A host registers its embedded fonts with `add_builtin_font(name, data, size, size_adjust)` (TrueType data that stays valid for the life of the library, for example a `static const unsigned char[]`; `size_adjust` is an optical correction of the em of that family; returns the family index or -1). Built-ins come first, in registration order, then user fonts. Typical host code: one `add_builtin_font` call per embedded font, once at start-up, before the first draw.
- User fonts: every `*.ttf` of a folder (`set_user_font_dir`, default `fonts` relative to the working directory, `""` disables them), at most `kMaxUserFonts` (8) files of at most `kMaxUserFontBytes` (8 MiB), named `<file stem> (fonts/)`. `rescan_user_fonts()` re-reads the folder. Files are untrusted: `validate_truetype()` checks the table directory, `head` / `hhea` / `maxp` / `hmtx`, `loca` / `glyf` (including composite glyphs), every `cmap` subtable stb_truetype can use (canonical format 4 search parameters, ascending segments, range offsets inside the table), a legacy `kern` table, and rejects CFF outlines. GPOS is hidden from user fonts (stb_truetype reads it unchecked), so user fonts get `kern`-table kerning only. Glyph indices handed out by a cmap are range-checked. Built-in fonts are trusted and not validated.
- `FontFace`: metrics (`upem`, `ascent`, `descent`, `xheight`, `capheight`), `glyph_index`, `advance`, `kern`, `glyph_extent`, `scale_for(em_px)` (the x-height is snapped to whole output pixels) and `bitmap()`. `generation()` changes whenever the family list changes. `FontLibrary` is not internally synchronised (the renderer locks around its use).

### `text_layout.h` and `text_raster.h`
- `layout_page(page, settings, sx, sy, out, lib)`: pure CPU layout at the output scale (`sx`, `sy` = output px per source px): word wrap (`KeepSourceBreaks` keeps the source's lines, `Reflow` re-wraps to the box width with paragraph breaks and orphan avoidance), auto-fit (shrinks in 0.25 steps down to `min_size_em`), vertical placement on the source baseline, kerning, per-line indents, glyph fallback to the first family and typographic substitutions (curly quotes to ASCII, dashes, bullet, ellipsis to three dots) when a font lacks a glyph. `PageLayout::reason` says why a page cannot be drawn. `layout_key()` hashes everything that influences the layout.
- `rasterize_glyph(face, glyph, em_px, phase, variant, param_px, out)`: light-hinted body coverage with four horizontal subpixel phases and stem darkening, shadow (the body shifted E / S / SE) and outline (disc dilation) variants. Coverage only; colour is applied when drawing.

### `text_fade.h` and `text_synth.h`
- `text_fade.h`: pure fade maths. `fade_from_regs()` decodes BLDCNT / BLDALPHA / BLDY for the OBJ layer (inert by default, `kPpuBlendsNormalObj`), `fade_from_text_colours()` derives a fade from the original text palette, `fade_rgb()` applies one. The host decodes, the renderer applies it through `DrawInfo`.
- `text_synth.h`: `synth_page(lines, ...)` builds a `TextPage` for tests and tools.

### Adopting it in a host
1. Add the library; the embedded fonts and their licence texts stay in the host. Call `FontLibrary::instance().add_builtin_font(...)` once per embedded font.
2. Keep the game-specific side: reading what the game shows, mapping the game's text words to Unicode (`TextChar::cp`, folding colour-code words into `colour`, numbering `entry`), deciding when to hide the original glyphs (ask `TextRenderer::can_draw()` first), and the settings storage / UI. Fill a `TextPage` once per presented frame.
3. Per frame: `TextRenderer::instance().draw(page, settings, info)` with the output rectangle of the game image. At shutdown, `release_gpu()` before destroying the SDL renderer.
4. A host's own copies of the fade maths (`fade_from_regs`, `fade_from_text_colours`, `fade_rgb`) are replaced by `text_fade.h`.

## OSD

### `perf_hud.h`: `PerfHud`
- `PerfHudMode`: `Off`, `FpsOnly`, `FpsAndFrametime`, `FullWithGraph` (F10 cycles). `PerfHudPosition`: four corners, `BottomBlackBar`, `FreeDrag`. `PerfHudTheme`: `GlassDark`, `Neon`, `Solid`, `Minimal`.
- `on_frame_present(renderer)`, `handle_mouse_event(event)`, `set_game_viewport(rect)`.
- Draws with its own bitmap font (no SDL_ttf; the table is shared with `text_history.h`). Calls `hooks().save_settings` via `notify_settings_changed()` after a drag.

### `text_history.h`: `TextHistory`, `register_text_history_settings`
A game-independent scrollable text-history sidebar (a "conversation log" or "backlog"). It keeps a bounded list of `TextHistoryEntry {speaker, text, timestamp}`, draws a translucent panel docked to the left of the window (dimmed backdrop, header, word-wrapped entries, scroll bar) with the built-in 5x7 bitmap font, and handles hotkeys, scrolling and the input blocking while it is open. What an entry is (and when to add one) is the host's business; the library needs no game or gbarecomp header.
- `TextHistory::instance()` or an own object. `push_entry(speaker, text[, timestamp])` returns false for empty text or a duplicate inside `dedup_window` (default 3) of the last entries; the list is capped at `max_entries` (default 200, 0 = unlimited). The timestamp defaults to local `HH:MM:SS`; `set_timestamp_source(fn)` replaces the clock. `entries()`, `clear()`.
- `handle_event(SDL_Event)`: toggle with F2 or the controller Back button (raw joystick buttons 4/6/8 as a fallback); while open, Esc / controller B / joystick button 1 close, arrows / PageUp / PageDown / D-pad / mouse wheel scroll, a left click outside the panel or on the header close hint closes, and **every** other keyboard, controller, joystick and mouse event is swallowed (`SDL_QUIT` and `SDL_APP_TERMINATING` never are). While closed or disabled only the toggle events are consumed (none when disabled). Return the result from the SDL event handler.
- `render_sidebar(renderer, win_w, win_h)`: call it at present time; it resets the renderer's logical size / viewport / scale to raw window pixels. Panel width = `bar_width_percent` of the window clamped to `[bar_min_width, bar_max_width]` (40%, 380..600); windows at least `large_font_min_window_height` (900) tall use the 2x font. Opening scrolls to the newest entry (applied at the next render, so the first open after new entries also lands on the newest one).
- `config()`: `controls` (`TextHistoryControls`: every key / button list, `line_step`, `page_step`, `wheel_step`, `close_on_outside_click`; extra hotkeys such as `SDLK_l` are just more entries in `toggle_keys`) and `style` (`TextHistoryStyle`: bar geometry, all captions, all colors, `speaker_colors` = exact speaker name to color).
- `register_text_history_settings(registry, history, options)`: opt-in helper (not part of `register_standard_settings`) that adds the Bool setting `dialogue.backlog_enable` (`.ini` `[Dialogue] backlog_enable`) and the Action `dialogue.open_backlog`; ids, labels, menu section and `.ini` placement are `TextHistorySettingsOptions`, and the defaults match the ids and keys that hosts shipped, so existing `.ini` files keep loading.
- Font: the 5x7 table lives in the internal header `src/font5x7.h`, shared with `perf_hud.cpp`; its provenance is an open item in `NOTICE.md`.
- Adoption: keep the game-specific observer (which memory holds the text, speaker ids, decoding) in the host and call `push_entry` from it; delete the host's own widget code (entry list, hotkeys, drawing, font table, wrapping); install `handle_event` and `render_sidebar` where the host did; replace the two registry rows with `register_text_history_settings`.

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

### `m4a_music_hook.h`: `M4aMusicHook`, `locate_m4a`
HD music trigger for games that use the MusicPlayer2000 ("m4a") sound engine. Needs no gbarecomp header.
- `locate_m4a(rom, size, rom_base = 0x08000000)` returns an `M4aAddresses` with `song_num_start`, `song_num_start_or_change`, `song_num_start_or_continue`, `song_num_stop`, `mplay_all_stop` (guest addresses), plus `mplay_start`, `mplay_stop`, `song_table`, `mplay_table` and `song_table_entries`. It matches the Thumb code shape of the five functions (two compiler register allocations are recognised, no fixed addresses) and accepts a group only when they appear in order within 0x400 bytes, the three start functions call the same routine and `song_num_stop` calls the routine `mplay_all_stop` loops over (that is what tells it apart from the identically shaped continue function). `complete()` says whether all five were found; on failure fill `M4aAddresses` from a per-game table instead. `count_m4a_idents()` counts the `Smsh` ident literals (0 means the ROM does not use the engine).
- `M4aMusicHook` (`instance()` or an independent object): `locate(rom, size)` / `set_addresses(a)`; `set_bus_reader(fn)` (guest memory read, used to read `gSongTable[id].player`), `set_rom_image(...)` or `set_song_player_reader(fn)` as alternatives; `load_track_mapping(ini)` (the `[tracks]` section, `<song id> = <wav>`, is parsed by `HdAudioPlayer::load_track_mapping`); `set_log_tag`, `set_logging`, `set_logging_from_env("PREFIX_")` (reads `PREFIX_LOG_INTERPOSE`; the first 40 events are printed); `stats()` (`starts`, `mapped_triggers`, `hd_stops`), `print_stats(tag)` (one line on stderr or a given `FILE*`: `[tag] m4a song hook: starts=N hd_mapped=N hd_stops=N`).
- One-call start-up: `setup_from_rom(rom, size, M4aSetupOptions)` sets the log tag and environment logging, runs `locate()`, applies the host's `fallback` address table when the locator does not find all five functions, and installs the bus reader (`bus_read_u32`; without one the ROM image itself is used for the song table). It does nothing and returns `None` while `rom` is null or `size` is 0, so the host can call it again once the ROM is mapped. The result, also kept in `setup_source()`, says how the addresses were obtained: `Locator`, `Fallback`, `None` (no addresses) or `Manual` (`set_addresses()` was called directly).
- The host keeps owning the runtime's function-entry hook and calls `on_function_entry(pc, r0)`. Policy: a started song with a mapping calls `trigger_song`; an unmapped song that starts on the same music player the HD track was started for stops it; `m4aSongNumStop` of the HD song and `m4aMPlayAllStop` stop it; everything else is left alone. The original m4a music keeps playing underneath.
- `HdMusicSink` is the back end interface (default: the shared `HdAudioPlayer`; `set_sink()` replaces it, e.g. in tests).
- Adopting it: delete the host's own hook (address constants, policy, counters) and call `locate()` once the ROM is loaded, `set_bus_reader()` with the runtime's bus read, `load_track_mapping()` and `on_function_entry()` from the function-entry hook.

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

The game texture is recognised at any logical size from 240x160 up to 2048x2048 (at most 4:1), directly or as an integer-prescaled render target, so every aspect ratio of `view_aspect.h` is covered.
The six SDL handlers that route a runtime's SDL calls into the library (game-image detection, colour correction, letterboxing with the viewport reported to `PerfHud`, screen mask, frame interpolation, performance HUD, pull-callback and direct-queue audio, call counters and audio metering for diagnostics) so a host does not carry its own copy. A host fills a `GameHostConfig` with the optional callbacks that are specific to its game (`on_poll`, `on_event`, `on_native_frame`, `compute_viewport`, `after_game_image`, `on_present_begin`, `render_present_overlay`, `before_present`, `print_stats`) and calls `install_game_host()` once. The Linux `--wrap` shims (`GBAIMP_DEFINE_LD_WRAPS()`) stay in the host because they must be expanded in the final executable. `is_gba_game_texture()` and `default_game_viewport()` are public helpers.

### `view_aspect.h`: `AspectSettings`, `resolve_view_width`, `resolve_view_size`

Turns a player choice (3:2, 16:10, 16:9, 21:9, 32:9, 4:3, 5:4, 10:7, custom ratio, adaptive = follow the window) into the
logical view the runtime renders, clamped to caps (`max_view_width`, default 640; `max_view_height`, default 320).
It never touches the runtime: copy `aspect_choices()` and the resolved sizes into the host run options.
`parse_custom_ratio` accepts `W:H` or a decimal ratio.

- `resolve_view_width()` keeps the original contract: the width at the native 160 rows (a ratio narrower than 3:2 gives 240).
- `resolve_view_size()` returns a `ViewSize` (`width`, `height`). A ratio of at least 3:2 grows the width at 160 rows; a
  narrower one keeps 240 columns and grows the rows (`240 / ratio`), so an adaptive window never gets extra rows unless it is
  taller than 3:2. The tall presets are `Standard4x3` (240x180), `Standard5x4` (240x192) and `Console10x7` (320x224, wider
  AND taller than native). The enum values of the older presets did not change; `aspect_choices()` lists the tall presets
  after `Super32x9` (the fixed launcher table of widths, `launcher_aspect_table()`, is unchanged: a launcher only carries widths).
- `split_extra_rows(height, &top, &bottom, top_share)` splits the extra rows (`top_share` 0.5 = even). Pair it with
  `split_extra()` for the columns, then set the runtime `ViewGeometry` from the result.

### `affine_margin.h`: `AffineMarginRenderer`, `scan_affine_art_bounds`

Real artwork for the vertical margins of a taller view, for games that draw their scene on a large affine background (modes 1 and 2,
BG2 / BG3). The runtime never extrapolates an affine layer above or below the native band, and a regular ring would only wrap, so a host
that wants more rows renders them itself. Per frame the module latches the layer registers and the PPU memory (`PpuMemory`: DISPCNT,
I/O page, VRAM, palette), finds the bounding box of each layer opaque pixels (`ArtBounds`, cached on a hash of the map and tile
memory) and samples every margin row with the fixed point arithmetic of the hardware. The sampled position is then clamped to the
artwork box relative to the map copy that holds the native band, so a row above the art repeats its first row, a row below repeats its
last row, and the far side of a wrapping map never shows. Layers are composited by hardware priority into `WsMarginPixel` rows.
- `set_memory_source(fn, user)` + `provide(y, extra_left, extra_right, &left, &width)` is the adapter for the margin pixel provider:
  native rows return null, the first margin row of a pass re-latches the registers, the rest reuse them.
- `available_rows()` / `available_cols()` report how many rows / columns of real art lie beyond the native band for the current camera
  (the camera moves, so hosts can log or cap with it). Unrotated layers only.
- Options: `layer_mask` (default BG2 + BG3), `clamp_to_art` (off = hardware wrap), `order`.
- Limits: per-scanline register changes, mosaic, window masks and colour effects are not applied to margin rows; 256 colour affine
  maps only (the only affine format the hardware has).

### `obj_geometry.h`

Header-only decoding of raw OAM attributes for placement policies: `obj_signed_y`, `obj_signed_x`, `obj_disabled`, `obj_size` and
`obj_bounds` (affine double-size included).

### `margin_fill.h`: `MarginFill`

Backdrop for the window area the game image does not cover (pillar / letterbox bars): black (`Pillar`),
the picture's outermost column / row stretched and dimmed (`EdgeRepeat`), or the picture heavily blurred and
dimmed (`Blur`). `game_host` draws it before the image on every presented frame, so the picture itself is never
altered. `set_mode` / `set_brightness` are the only knobs.

### `widescreen.h`: `Widescreen`, `WidescreenProfile`, `WidescreenBindings`
The game-independent part of widescreen support. The runtime can render a view wider than 240x160; what the extra columns show is host policy. This module owns the policy that is the same for every game, and the game-specific part (what counts as "wide", which providers, HUD defaults) is a `WidescreenProfile` filled by the host.

**Derived state, no cache.** Everything is computed from the runtime's live values:
- `view_active()`: the runtime flag is set and the extras are greater than 0 (the expanded view is selected).
- `presentation_active()`: expanded view AND the game's *wide context* is on. The picture is really shown widened, and this is exactly when the HUD is anchored to the screen edges.
- `effective_extra_pixels(&left, &right)`: the real extra width per side while the presentation is active, else 0/0 (dialogue and menu code should size boxes with this). `extra_left()` / `extra_right()` give the raw extras of an expanded view.
- `notify_present()` / `frame_counter()`: presented-frame counter (atomic). `last_wide_frame()` / `last_narrow_frame()` hold the counter at the last context switch.

**Wide context.** A bool owned by the module. The host drives it by push (`set_wide_context(bool)`, for example from the function-entry hook) and/or pull (`profile.poll_context(user)` returns -1 unknown, 0 narrow, 1 wide; read by `update()` and `compute_effective_viewport()`). What it means is the game's decision: a battle arena, a scene authored for a wider screen, or always (`initial_wide_context = true` and never set it false).

**`apply_policy()`** installs or withdraws everything the runtime consults, from (expanded?, context wide?). It is idempotent and stateless, so a view switch, a context change or a save-state load cannot leave anything stale:
- Expanded: tilemap / background-X / sprite-X providers from the profile (null stays null), `bg_x_provider_layers`, `authored_margin_layers` and `obj_native_clip` from the profile, `pillarbox = 1` while the context is narrow (`pillarbox_outside_context`), 0 while wide.
- Not expanded (the "return to the original" invariant): providers null, layer mask `0xF`, authored margin layers 0, native clip 0, pillarbox 0/0/0, which is what the runtime leaves for a faithful native run. `HudAnchoring` goes back to `OriginalCentered` with `profile.hud_default_offset`.
- `HudAnchoring` (the shared instance, or `profile.hud`; skipped with `manage_hud = false`): `WidescreenAnchored` with offset `max(extra_left, extra_right)` while the presentation is active, `OriginalCentered` with the default offset otherwise. The anchoring mode is derived, not a user option.

**Lifecycle.** `configure(bindings, profile)` once at start-up, then `install()` (policy plus the function-entry hook) from the runtime's extended-view init callback, and `update()` (poll the context, policy, hook) from its per-frame callback and from every SDL poll. All are cheap and safe to repeat.

**Bindings.** `WidescreenBindings` holds pointers to the runtime's values: `active`, `extra_left`, `extra_right` (read) and `tilemap_provider`, `bg_x_provider`, `bg_x_provider_layers`, `obj_attr_x_provider`, `authored_margin_layers`, `obj_native_clip`, `pillarbox`, `pillarbox_left`, `pillarbox_right`, `fn_entry_hook` (written). The library never links those symbols. A host includes `gbaimp/widescreen_gbarecomp.h` in one source file (header-only, declares the runtime's `extern "C"` globals itself, needs no runtime header) and passes `gbaimp::gbarecomp_widescreen_bindings()`. Every pointer may be null. Tests pass plain variables.

**Function-entry hook.** When `profile.on_fn_entry` is set, `install_fn_entry_hook()` chains a trampoline into the runtime's hook slot (the previous hook runs first; installing twice is a no-op; a slot overwritten by someone else is re-chained by the next `update()`) and `remove_fn_entry_hook()` restores the previous one. The trampoline is a plain function, so only `Widescreen::instance()` can install it. `on_fn_entry(pc, user)` runs on the guest thread for every function entry: reject quickly and call `set_wide_context()` when the game changes mode.

**Vertical extent (rows).** The runtime can also render extra rows above and below the native 160 (a taller logical view). Pass
`extra_top` / `extra_bottom` in the bindings (the runtime exports no globals for them: with `widescreen_gbarecomp.h` call
`gbarecomp_set_vertical_extras(top, bottom)` from the view-geometry callback and the per-frame callback). `view_active()` then
also becomes true for a view that only adds rows (4:3 at 240 columns); `extra_top()`, `extra_bottom()`, `has_vertical_extent()` and
`effective_extra_rows()` mirror the column queries. The runtime draws a row outside 0..159 only for content a host authored, and
fails closed (black) otherwise, so two providers matter:
- `profile.obj_attr_y_provider` (`g_ws_obj_attr_y_provider`): authorizes a sprite at a signed Y, which is the only way a sprite may be
  drawn outside the native band (HUD anchored to the top / bottom edge, a world sprite overhanging the band).
- `profile.margin_pixel_provider` (`g_ws_obj_margin_provider`, `WsMarginPixel`): authored pixels for a margin row. This is how affine
  backgrounds, which the runtime never extrapolates vertically, get their rows (see `affine_margin.h`).
- `default_bg_x_provider` suppresses every regular background on a row outside 0..159 (a ring has nothing real to show there).
- Not wide context: the extra rows are black (`pillarbox`) and `widescreen_viewport_rows()` presents only the central 240x160.

**Viewport.** `compute_effective_viewport()` (and the free `widescreen_compute_viewport()`, which has the `GameHostConfig::compute_viewport` signature) polls the context and fills the source rectangle, destination rectangle and base size. Expanded texture (`tex_w * 2 > tex_h * 3`) with a wide context: the whole texture, scaled uniformly and centred. Expanded texture with a narrow context: the central 240x160 area only. Native texture: the whole texture as 240x160. `widescreen_viewport(wide, ...)` is the pure math. When the view has extra rows the
instance uses `widescreen_viewport_rows(wide, left, right, top, bottom, ...)`: the texture is `factor` times the logical view, wide
context shows all of it, narrow context crops the central 240x160 at the real left / top offset (a texture that does not match falls
back to the older rule).

**Ready-made providers** (use `Widescreen::instance()` and the profile): `default_tilemap_provider` (keep the hardware-wrapped entry while the presentation is active), `default_bg_x_provider` (expanded view only; margin columns only; answers from the margin policy below), `default_obj_attr_x_provider` (sprites parked at raw X >= 256 get their signed X) and `widescreen_obj_x(attr1)`. A provider that tells HUD sprites from world sprites is game specific, so the host writes it (it can call `default_obj_attr_x_provider` for everything else).

**Margin policy per scene.** The 22-ish extra columns on each side (outside the central 240) are not the same in every scene: a menu authored for 240 pixels must show nothing there, a scrolling map can continue its wrapped ring, a battle may extend only its backdrop. `profile.margin_policy` declares that per scene, with no custom provider:
- `WsMarginPolicy`: `Extend` (the layer shows its wrapped ring, the runtime's picture), `Blank` (nothing of the layer in the margins; the provider answers "suppress") and `Runtime` (decline, the runtime decides; today the same answer as `Extend`).
- `WsMarginPolicySet`: one policy for each of BG 0..3 plus `obj`. Helpers: `WsMarginPolicySet::all(p)` and the chainable `set_bg(layer_mask, p)`. For OBJ, `Blank` makes `apply_policy()` write `obj_native_clip = 1` (sprites clipped to the native 240 pixels); otherwise `profile.obj_native_clip` applies.
- `WsMarginPolicySet (*margin_policy)(bool wide_context, void* user)`: return the set for what the game is showing now (read your mode flag from `user`, like `poll_context`). The module calls it from `apply_policy()` and after every `poll_context`, caches the answer in one atomic word, and `default_bg_x_provider` only reads that word: no call, allocation or `std::function` in the per-pixel path. `Widescreen::margin_policy()` / `margin_policy_for_bg(bg)` read the cache.
- Not set: the previous rule (narrow context blanks every layer, wide context blanks `centered_bg_layers`), so existing profiles behave as before. While the view is not expanded nothing is installed, so the 3:2 invariant is unchanged.

```cpp
// A battle is a wide scene that keeps its UI layers (BG0, BG1) and a fill layer (BG3) out of the margins;
// menus show nothing there; maps extend everything.
static bool g_in_battle = false;                    // refreshed by poll_context from the game's RAM
WsMarginPolicySet my_margins(bool wide, void*) {
    WsMarginPolicySet s;                            // all Extend
    if (!wide) {                                    // menu: nothing in the margins (sprites untouched)
        s.set_bg(0xF, WsMarginPolicy::Blank);
        return s;
    }
    if (g_in_battle) s.set_bg(0xB, WsMarginPolicy::Blank);   // bits 0, 1, 3
    return s;
}
// profile.bg_x_provider = &default_bg_x_provider;  profile.margin_policy = &my_margins;
```

**Writing a profile for a new game.**
1. `Widescreen::instance().configure(gbaimp::gbarecomp_widescreen_bindings(), profile)`.
2. Decide the wide context: which game modes are authored for a wider picture. Either read the game's mode word (pull, `poll_context`; return -1 while it is not valid yet) or watch function entries (push, `on_fn_entry` and `set_wide_context`). This needs the game's ROM/IWRAM addresses, which stay in the host and have to be found per game.
3. Pick the providers: start with `default_tilemap_provider` / `default_bg_x_provider` / `default_obj_attr_x_provider`, set `margin_policy` (or the simpler `centered_bg_layers`) for the layers that must stay out of the margins, and replace the sprite provider when the HUD has to move.
4. Register the HUD zones on `HudAnchoring` (`add_zone`, game geometry, host side). The offset is set by this module.
5. Call `install()` from the extended-view init callback and `update()` from the per-frame callback and the SDL poll handler; call `notify_present()` from the present handler; assign `widescreen_compute_viewport` to `GameHostConfig::compute_viewport`.
6. Replace the per-game helpers (view active, presentation active, effective extra pixels, frame counter) with the member functions above.

Only the shared instance can install the hook and back the default providers. Independent `Widescreen` objects (tests, unusual hosts) work with the bindings and profile they are given, except for those two.

### `capture_harness.h`: `capture::configure`, `on_emulated_frame`, `on_before_present`
Headless capture and scripted-input harness for verifying a game without playing it (developer tooling, environment-gated, inert when no variable is set). Every variable is `<prefix>_<NAME>` with the prefix chosen by the host (`CaptureConfig::env_prefix`). Features: PNG capture of chosen emulated frames (`output` = final window image, `native` = raw PPU frame), capture variants (window size and live settings through `CaptureConfig::apply_setting`), window size, exit at a frame, scripted GBA input expanded into a runtime input-replay trace, scripted host key presses, settings overrides, savestate load/save and fast-forward arguments (`extra_runtime_args()`). The PNG writer is self-contained (stored deflate). The host wires `on_emulated_frame`, `wants_native_frame` / `on_native_frame` and `on_before_present` (the last two are `GameHostConfig` callbacks). Pure parsers are public for tests. Full reference and adoption steps: `docs/CAPTURE_HARNESS.md`; runner and image scripts: `tools/capture/`.

**Savestates by PPU frame.** `<prefix>_SAVESTATE_SAVE_PPU=<frame>:<slot>[,<frame>:<slot>...]` saves several slots per run at absolute PPU frames; the harness translates them to the runtime's pump-based assist script with the pure `plan_savestate_pumps()`. The frame of a loaded state comes from `<prefix>_SAVESTATE_LOAD_PPU_FRAME` or from a `<state file>.meta` sidecar that the harness writes next to the slot file when it knows the ROM path (`CaptureConfig::rom_path` or `<prefix>_SAVESTATE_ROM`). Frames already passed, two saves on one frame and a slot used twice are reported as errors (`capture::configuration_error()`), and nothing is scheduled. Details and the measured pump offsets are in `docs/CAPTURE_HARNESS.md`.

### `runtime_ui_glue.h`: `create_standard_ui`
Header-only, compiled only by hosts that build recomp-ui (needs `recomp_runtime_ui.h` and `GBARECOMP_RUNTIME_UI`). Adjusts the view-mode labels and removes the RESUME row of the menu that `recomp_runtime_ui_create_standard()` builds, with the macros `GBAIMP_RUNTIME_UI_WRAP` (GNU ld `--wrap`) and `GBAIMP_RUNTIME_UI_RENAME_FORWARDER` (MSVC compile-time symbol rename) to interpose that call.

### `host_settings.h`: `SettingsRegistry`, `register_standard_settings`
The settings registry, the standard panel and the `.ini` persistence that every host used to copy (a table of menu items, a string-keyed get / set / action callback chain and a hand written reader / writer).
- `Setting`: `id` ("section.key", also the runtime-menu key), menu section / label / tooltip, `kind` (`Bool`, `Int`, `Choice`, `Action`, `Text`), `min` / `max` / `step`, `choices`, `.ini` section / key / aliases, and the `get` / `set` / `action` / `get_text` / `set_text` / `enabled` callbacks.
- `SettingsRegistry`: `add()` (replaces in place by id), `find()`, `remove()`, `rename_menu_section()`, `get()` / `set()` (values are clamped to the declared range, Bool is normalized) / `run_action()` / `get_text()` / `set_text()` / `is_enabled()`, `set_change_listener()`, and the `.ini` side: `set_ini_path()`, `set_ini_header()` (optional `# ` comment block), `set_section_order()`, `save_ini()` / `ini_text()`, `load_ini(overrides)` / `load_ini_text()` returning `IniLoadStats` (applied / ignored / unknown). `set()` and finished actions write the file unless a load is running, `set_autosave(false)` or `set_save_blocked()` says otherwise; `load_ini()` with no file writes the current values (`set_write_defaults_if_missing`).
- `register_standard_settings(reg, targets, options)` registers once every setting of the library modules, with the ids and `.ini` keys the hosts have always used (full list in the header): `video.*` (frame rate, motion smoothing, upscaler, colour profile, screen mask, mask intensity), `audio.*` (sample rate, HD audio, master / music / effects volume, equaliser, stereo width, anti-aliasing, limiter), `input.*` (analog mode, dead zone, gamepad profile, four pad button choices, keyboard preset, ten keyboard key rows stored in the keybinds file, reset actions), `gameplay.turbo_dialog`, `perf.*` (mode, position, theme, plus the dragged HUD position, which is `.ini` only). `SettingsTargets` selects the module instances (default `SettingsTargets::shared()` = the singletons); `StandardSettingsOptions::exclude` drops ids a host does not offer. The keybinds file is reloaded before the `.ini`, as before. The legacy key `hud_anchoring` is accepted and ignored.
- Extension points for host specific settings: `add()` any `Setting` with your own callbacks (and its own `.ini` section / key), `IniExtension` for free form lines (e.g. a font stored by name), `add_ignored_ini_key()` / `add_ignored_id()` for retired keys, `add_before_load()`, `set_save_blocked()`, the `overrides` argument of `load_ini()` (applied after the file), and direct edits of the `Setting` returned by `find()` (labels, ranges, dynamic `choices`).
- Unknown keys, non-integer values, junk lines, comments and CRLF line endings never fail a load. Matching is by `.ini` key only (sections are not significant), as in the files written so far. File layout: `[Section]` lines, `key=value`, one empty line between sections, no trailing blank line.

### `runtime_ui_settings.h`: `RuntimeUiBinding`, `runtime_menu_*`
Header-only adapter, compiled only by hosts that build recomp-ui (needs `recomp_runtime_ui.h` and `GBARECOMP_RUNTIME_UI`; it does not include gbarecomp's `runtime.h`). `RuntimeUiBinding(reg).rebuild()` turns the registry into a `RecompRuntimeUiItem` array (menu settings only, in registration order) and `apply(opts)` fills `ui_extra_items`, `ui_extra_item_count`, `ui_get`, `ui_set`, `ui_action`, `ui_enabled` (the `system.resume` row stays greyed out) and the two text callbacks of the host's `gbarecomp::RunOptions`. `runtime_menu_is_open/open/close/toggle(RecompRuntimeUi*)` replace the four menu helper functions each host defined. The binding and registry must outlive `run_game()`.
**Lifetime.** `RuntimeUiBinding` records itself as the single active binding, so construct it in place with static storage duration (`static gbaimp::RuntimeUiBinding g_ui(g_settings);`). Returning it by value (for example from a lambda) leaves the recorded pointer dangling until the next `apply()` re-registers it.

**Adopting it in a host**: delete the item tables, the choice-label arrays, `ui_get_callback` / `ui_set_callback` / `ui_action_callback` / the text and enabled callbacks, `save_*_config_impl`, `load_*_config` and the four menu helpers from `game_config.cpp`. Keep a small function that creates the registry (`set_ini_path("<game>_config.ini")`, `set_section_order(...)`, `register_standard_settings(...)`, host rows, `load_ini()`), calls `RuntimeUiBinding::apply(opts)`, and exposes `save_ini()` / `load_ini()` under the old function names if other code calls them. Per-host wording and ranges are edits of the registered `Setting`s.

### `ram_dispatch.h` / `ram_dispatch_gbarecomp.h`: native IWRAM/EWRAM routines
Some games copy code to RAM; `tools/gen_ram_dispatch.py` compiles those routines natively and emits a sorted table. This module owns everything else about dispatching them. `gbaimp/ram_dispatch.h` is pure (no gbarecomp include): `RamDispatchEntry {pc, thumb, fn}`, `find_ram_dispatch(table, count, pc, thumb)` (binary search; `pc & ~1u` is compared and the mode exactly, as in the old per-host hook), `ram_dispatch_call` (search + call, returns 1/0 like the runtime hook), `ram_dispatch_is_sorted`, `log_ram_dispatch_installed`, and the macro `GBAIMP_RAM_DISPATCH_HOOK[_N]`. The templates accept any row type with `pc` / `thumb` / `fn`, so tables generated by older versions still work. `gbaimp/ram_dispatch_gbarecomp.h` is the header-only adapter; its whole contract with gbarecomp is `extern "C" int (*g_runtime_ram_dispatch_hook)(uint32_t pc, int thumb)` (`RuntimeRamDispatchHook`, declared in `armv4t/runtime_arm.h`, null by default, cleared by `run_game()` on return), and `GBAIMP_DEFINE_RAM_DISPATCH[_N](prefix, "Log name", table[, count])` defines `<prefix>_ram_dispatch_hook` and `<prefix>_install_ram_dispatch()` (idempotent; prints the same one-line message as before). A generated `ram_overlay_dispatch.cpp` is now the routines, the table and one macro line. **Adopting:** regenerate with the script, or run `python tools/gen_ram_dispatch.py --migrate src/ram_overlay_dispatch.cpp` on an existing generated file (replaces the copied hook code by the macro, keeps CRLF/LF, idempotent). Hand-written stubs such as an empty dispatcher can use the same migration. The script also fixes a bug: an ARM and a Thumb routine at the same address used to produce two functions with one name; the Thumb one is now `ram_func_<PC>_thumb`.

### `host_main.h`: `HostMainConfig`, `run_host_main`
The start-up sequence every host's `main()` repeated: SDL interposers, `GBARECOMP_RAM_OVERLAY_HEAL=1` default (never overrides the user's value; `set_env_default`), RAM dispatcher install, the `"<title> Static Recompilation (<banner>)"` line, optional extra arguments (a headless capture harness), run options, then either launcher pre-boot (`true` = user quit = exit 0; may edit the argument list and options) plus `run_game`, or a direct `run_game` with the process' own `argc/argv` when there is nothing to add. It is a template over the options type, includes nothing from gbarecomp and is unit-tested. With `#define GBAIMP_HOST_MAIN_GBARECOMP` before including it (after `runtime.h`, and `launcher_seam.h` when `RECOMP_LAUNCHER` is defined) `run_gbarecomp_host(cfg, argc, argv)` fills in `gbarecomp::run_game` and `gbarecomp_launcher_preboot`. **Adopting:** `main.cpp` shrinks to filling a `HostMainConfig` (title, banner, the two install callbacks, optional `extra_args`, `create_options`) and returning `run_gbarecomp_host(...)`. The rebuilt argv gets a terminating null pointer after `argc` entries.

### `msvc_compat.h` and `scaffold_stub.h`
`gbaimp/msvc_compat.h` maps `__builtin_clz` to `_BitScanReverse` under MSVC (clz(0) = 32); the six host copies were identical apart from line endings, so a host file can become `#include "gbaimp/msvc_compat.h"` (or the include path / `/FI` flag can point at the library). `gbaimp/scaffold_stub.h` holds the only generic parts of a host's `scaffold_stub.cpp`: `GBAIMP_SCAFFOLD_EMPTY_DISPATCH_TABLE()` (the empty `kDispatchTable` / `kDispatchTableLen`) and `GBAIMP_WEAK` (`[[gnu::weak]]` where supported; two hosts used it and four did not). The game-namespace stubs stay in the host.
**Force-include path.** If the host compiles with `/FI` (as gbarecomp does for its own targets), point `/FI` at the library header itself (`${GBAIMP_ROOT}/include/gbaimp/msvc_compat.h`) and define `GBAIMP_ROOT` before the `if(MSVC)` block. A host-local `msvc_compat.h` that merely includes `gbaimp/msvc_compat.h` breaks the gbarecomp subprojects: they receive the `/FI` flag but do not have the library include directory.

### Build kit: `cmake/GbaimpHost.cmake`, `templates/host/`, `tools/new_host/`
Not a C++ module: the shared build plumbing of host projects. `gbaimp_add_host(<exe> SYMBOL_PREFIX <p> SOURCES ... [TEST_TARGET <t> TEST_SOURCES ...])` configures the game executable and its native unit-test executable (library link, `-Wl,--wrap=SDL_*` on GNU ld, the MSVC rename of `recomp_runtime_ui_create_standard`, generated / BIOS sources compiled without warnings, post-build copy of the executable and `SDL2.dll` to the source root). `tools/new_host/new_host.cmake` instantiates the templates (`CMakeLists.txt`, `build.ps1` that propagates failing exit codes, `build.sh`, `docker/Dockerfile`, `.github/workflows/ci.yml`) for a new game. Step by step: [`NEW_HOST.md`](NEW_HOST.md).

### `optional/recomp_ui/runtime_ui_imgui_enhanced.cpp`: enhanced menu renderer
Not part of the `gba_improvements` library target (the library never links or includes recomp-ui or ImGui). It is the ImGui renderer of the recomp-ui in-game menu (`recomp_runtime_ui_render_imgui()`): draggable window (double-click the header to centre it, position kept per run and clamped on screen), sidebar plus item list or a single drill-down column on narrow windows, touch-friendly metrics, `-`/`+` steppers for integer rows, text edit fields, keybind capture badges for `input.key_*` rows, status line and a Resume button. It has no game-specific content.
**Dependencies**: from recomp-ui `recomp_runtime_ui_internal.h` (`RecompRuntimeUi`, `RecompRuntimeUiItem`, `RECOMP_RUNTIME_UI_*` constants and the `recomp_runtime_ui_*` accessor functions) and `launcher_theme.h` (`LauncherTheme`, `LngColor`, `launcher_theme_by_name`); `imgui.h` (Dear ImGui 1.89.7 or newer). The include paths come from `recomp_target_launcher_ui()`.
**Adopting it**: `gbaimp_add_host()` already compiles it, in place of recomp-ui's stock `runtime_ui_imgui.cpp`, into the executable and the test target whenever `recomp_target_launcher_ui` exists (recomp-ui present). A host deletes its own `src/runtime_ui_imgui_enhanced.cpp` and passes nothing; `ENHANCED_IMGUI_SOURCE <file>` substitutes a customised renderer. The build-kit test checks the wiring against stubs; compiling the file for real needs recomp-ui and ImGui, so it is only exercised by hosts that have them.

### `relaxed.h`: `Relaxed<T>`
A copyable, implicitly convertible wrapper around `std::atomic<T>` with relaxed ordering. The settings structs read by an audio thread while the UI thread writes them (`AudioDspSettings`, `HdAudioSettings`) use it for scalar fields, which keeps the field-style API (`s.enabled = true; float g = s.gain;`) while making each access data-race free.

## Tests

`ctest` runs one executable per module (`tests/test_<module>.cpp`, built on `tests/test_harness.h`). They need no display or audio device (SDL dummy drivers, software renderers). The tests are on by default only for a top-level build; a parent project turns them on with `-DGBAIMP_BUILD_TESTS=ON` (and must call `enable_testing()` itself). As a subproject the targets and test names are prefixed `gbaimp_` to avoid collisions.

The suites are `pixel_art_upscaler`, `screen_filters`, `audio_dsp`, `hd_audio_player`, `dialog_turbo`, `hud_anchoring`, `input_enhancements`, `savestate_thumbnails`, `hooks`, `perf_hud`, `frame_interpolator`, `instances` (independent instances), `audio_pipeline`, `sdl_interpose`, `audio_device`, `audio_threads`, `game_host`, `widescreen` (a fake runtime: plain variables as the g_ws_* values, a fake game mode word, the 3:2 return-to-original invariant, asymmetric extras, vertical extras, the hook chain and the viewport math), `view_aspect`, `affine_margin` (a fake PPU: artwork bounds, clamped rows, the map seam, layer priority, headroom, the provider adapter), `obj_geometry`, `capture_harness`, `host_settings`, `ram_dispatch`, `host_boot`, `m4a_music_hook`, `text_history`, and the text engine suites `text_fonts`, `text_layout`, `text_renderer` and `text_fade`; with Python 3 also `gen_ram_dispatch_py`, `tools_py`, `ram_tools_py` (the RAM snapshot / diff tools against a fake TCP server) and `ram_dispatch_generated`; the build kit tests `buildkit_host_cmake` (fixture host with stubs, configured for the GNU and MSVC branches) and `buildkit_new_host` (template instantiation); plus, when tools/examples are built, `verify_rom_hash_sha1_vectors`, `verify_rom_hash_cli` and `example_minimal_headless`. `sdl_interpose` and `audio_device` exercise the `--wrap` shims on Linux and the IAT patcher on Windows. `-DGBAIMP_SANITIZE=thread` (or `address`, `undefined`; GCC/Clang only) builds the library and tests with that sanitizer; `audio_threads` is the one that matters for the thread-safety contract.
