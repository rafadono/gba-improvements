# What `gba-improvements` expects from the `gbarecomp` runtime

`gba-improvements` itself never includes or links `gbarecomp`. A *host* (your game executable)
links both and connects them. This document lists the `gbarecomp` surface that a typical host
integration touches, so that you know what to hook, when, and what is guaranteed.

- Reference revision read for this document: `gbarecomp` commit `ecc9c55`.
- All paths are relative to the `gbarecomp` repository root. Nothing here is copied code; read
  the cited headers for the authoritative text.
- Items marked **(uncertain)** were inferred from reading and not exercised by this library's
  tests. `gbarecomp` is an actively changing third-party project: re-check against the revision
  you pin. See also its `CLAUDE.md`, `ENHANCEMENTS.md` and `docs/WIDESCREEN_STEPC_PLAN.md`.

## 1. Lifecycle and the single most important rule

The host calls `int gbarecomp::run_game(int argc, char** argv, const RunOptions& opts)`
(`src/runtime/runtime.h`). It owns the main loop and returns when the game exits.

**`run_game` resets all game-owned enhancement hooks to `nullptr` at the very start**
(`src/runtime/runtime.cpp`, start of `run_game`: `g_rom_read16_override`,
`g_rom_read32_override`, every `g_ws_*` provider and policy flag, `g_runtime_fn_entry_hook`,
`g_runtime_thumb_alu_imm_override`, `g_runtime_bus_read_override`; the RAM-dispatch hooks are
cleared on return). The stated reason is that hooks must not leak between runs in one process.

Consequence: **install hooks after `run_game` has started**, not in `main()` before the call.
The seams that run after that reset are the `RunOptions` callbacks `extended_view_init`,
`extended_view_frame` and `input_frame` (below). A host that sets a global hook before calling
`run_game` will have it cleared. (If you need an earlier point, `RunOptions::tcp_command` /
`ui_*` callbacks also run inside the loop, but they are not meant for this.)

## 2. Widescreen ("extended view") state and hooks

Declared in `src/gba/gba_ppu.h` (namespace `gba`, but all are `extern "C"`, so the linker symbol
has no namespace and no mangling) unless noted.

### 2.1 Read-only state written by the runtime
Defined in `src/runtime/runtime.cpp` (`extern "C"`, no header; declare them yourself with the
exact types):

| Symbol | Type | Meaning |
|---|---|---|
| `g_ws_active` | `unsigned` | 1 when the PPU view is expanded beyond 240x160, else 0. |
| `g_ws_extra_left`, `g_ws_extra_right` | `unsigned` | Extra pixels added left / right of the native 240-wide view. |
| `g_ws_extra` | `unsigned` | Legacy single margin: `max(extra_left, extra_right)`. |
| `g_ws_view_width` | `unsigned` | Total render width (`240 + left + right`). |

They are refreshed at start-up and again after a live resize
(`src/runtime/runtime.cpp`, where `ppu.view_expanded()` is evaluated). Treat them as read-only.
There is no `g_ws_extra_top/bottom` global; vertical margins are only available through
`ExtendedViewFrameInfo` (2.3) and the `max_resize_view_height` option.

### 2.2 Provider hooks installed by the host (all default `nullptr`/0 = faithful behaviour)
Only the expanded renderer calls them; the native 240x160 renderer never does.

| Symbol | Signature | Semantics |
|---|---|---|
| `g_ws_tilemap_provider` | `int (*)(int bg, int hw_x, int screen_y, uint16_t* out_entry)` | Supplies an authored tilemap entry for margin columns. Returns `kWsTilemapUnavailable` (0), `kWsTilemapReplace` (1, use `*out_entry`) or `kWsTilemapKeepWrapped` (2, keep the hardware-wrapped entry deliberately). |
| `g_ws_bg_x_provider` + `g_ws_bg_x_provider_layers` | `int (*)(int bg, int output_x, int screen_y, int* out_hw_x)`; `unsigned` bitmask (default `0xF`) | Remaps the X of regular BG samples. Returns -1 suppress, 0 unchanged, 1 use `*out_hw_x`. Narrow the mask to avoid per-pixel callback cost. |
| `g_ws_bg_xy_provider` + `g_ws_bg_xy_provider_layers` | `int (*)(int bg, int x, int y, int* source_x, int* source_y)`; bitmask | 2D remap in native-relative coordinates (negative = left margin). Same -1/0/1 result convention; window masks of the source still apply. |
| `g_ws_obj_x_provider` | `int (*)(int raw_x, int* out_x)` | Reinterprets the raw 9-bit OBJ X. Default is hardware signed decoding. |
| `g_ws_obj_attr_x_provider` | `int (*)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x)` | Richer OBJ X hook that sees the raw attributes (to tell HUD from world sprites). Called **before** `g_ws_obj_x_provider`. |
| `g_ws_obj_native_clip` | `int` | Non-zero clips OBJ pixels to the native 240 px viewport so sprites parked off-screen do not leak into margins. |
| `g_ws_obj_margin_provider` | `const WsMarginObjPixel* (*)(int screen_y, int* hardware_left, int* width)` | Supplies authored OBJ coverage for margins (`WsMarginObjPixel`: RGB555 `color` with bit 15 = transparent, `priority` 0..3, `order`, `semi_transparent`). Never consulted inside the native viewport. |
| `g_ws_obj_attr_y_provider` | `int (*)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_y)` | Authorizes a sprite at a signed native-relative Y; only sprites accepted here may draw outside the native 160 rows (vertical margins). There is no `g_ws_extra_top/bottom`: the vertical extras come from `ExtendedViewFrameInfo` and the host own `view_geometry` callback. |
| `g_ws_affine_filter_enabled` / `g_ws_affine_filter_provider` | `int` / `int (*)(int bg, int screen_y)` | Optional affine-background enhancement; the provider authorises layers/scenes. **(uncertain: exact return semantics, read `gba_ppu.cpp`.)** |
| `g_ws_authored_margin_layers` | `int` | Non-zero lets provider-sourced BG margins render independently of native windows; zero keeps the fail-closed behaviour. |
| `g_ws_native_view_left` | `int` | Placement of native X=0 inside the expanded canvas. Negative keeps the configured left margin; otherwise clamped to `[0, render_width-240]`. Shared by BG, OBJ, window and margin coordinates. |
| `g_ws_pillarbox`, `g_ws_pillarbox_left`, `g_ws_pillarbox_right` | `int` | Policy flags: black margins on scenes without authored margin content. After a save-state restore the PPU itself sets `g_ws_pillarbox = g_ws_authored_margin_layers ? 0 : 1` and clears the left/right flags (`src/gba/gba_ppu.cpp`), so a host policy should re-assert its own values every frame (the `extended_view_frame` hook, 2.3). |

A game-agnostic library cannot know which scenes have margin content, so these remain host
policy. `gba-improvements` ships `HudAnchoring` (screen-space HUD placement) as one consumer.

### 2.3 Per-frame host hook
```
struct gbarecomp::ExtendedViewFrameInfo {            // src/runtime/runtime.h
    uint64_t frame_count;
    uint32_t view_width, extra_left, extra_right;
    uint32_t view_height, extra_top, extra_bottom;
    const uint8_t* io;  size_t io_size;              // read-only IO register page (0x04000000..)
    uint64_t state_epoch;                            // changes on save-state restore
};
RunOptions::extended_view_frame : void (*)(const ExtendedViewFrameInfo*)
RunOptions::extended_view_init  : void (*)(uint32_t extra_left, uint32_t extra_right)
```
- `extended_view_frame` runs at the **start of every emulated frame, before scanline 0 is
  composed**, on the guest thread, and again right after a live adaptive resize. It is the right
  place to refresh `g_ws_pillarbox*`, margin providers and per-scene state.
- `extended_view_init` runs exactly once, after the first non-native view is applied (at start-up
  for a fixed view; deferred until the aspect first exceeds 240 for resize-driven view). Not
  called while the view stays at 240.
- `state_epoch` changes when a save state is restored: invalidate any cached margin data then.
- The pointers in the struct are valid only during the call.
- Opt-in: a game must also set `max_view_width` / `max_resize_view_width` / `max_resize_view_height`
  above the native size, otherwise the view stays 240x160 and none of the above is active.

## 3. Input

- Register file: `gba::GbaIo` (`src/gba/gba_io.h`). `KEYINPUT` is active-low in the low 10 bits
  (1 = released). Relevant members: `set_keyinput(uint16_t)`, `host_keyinput()`,
  `set_synthesized_keyinput(uint16_t)`, `synthesized_keyinput()`, `composed_keyinput()`.
- The guest reads `composed = host & synthesized & 0x03FF`. `host` is written by the runtime's
  own loop (keyboard/gamepad via `HostWindow`) with `set_keyinput` once per event batch
  (`src/runtime/runtime.cpp`, several call sites).
- **Sanctioned steering path:** `RunOptions::input_frame`
  (`std::uint16_t (*)(const TouchFrameInfo*)`, `src/runtime/touch_input.h`) is called once per
  emulated frame at VBlank start, *before* the VBlank IRQ, and returns an active-low mask that is
  ANDed into the synthesized half. Because it is an AND of active-low values it can **add**
  presses but cannot release a key the host reports pressed; `TouchFrameInfo::host_keyinput`
  gives the host keys. It is suspended during input replay, and while the runtime menu is open.
  It also turns on the touch-input machinery (`TouchHub`, virtual pad) as a side effect
  **(uncertain: side effects of enabling it only for key steering)**.
- **Remapping path (host-owned bus access):** `gbarecomp::active_bus()`
  (`src/runtime/runtime_bus_bridge.h`) returns the live `gba::GbaBus*`; its `io()` gives the
  `GbaIo` above. A host can read `host_keyinput()` and call `set_keyinput()` with the result of
  `InputEnhancements::process_keyinput`. The runtime itself overwrites `host_keyinput` whenever
  it processes input, so the host's write has to happen *after* that and *before* the guest
  reads it each frame. The exact ordering relative to the runtime's own write is
  **uncertain** and is what the `frame_counter` hook of this library is meant to cope with; a
  `runtime_set_vblank_input_hook` / `runtime_set_frame_start_hook` exist in
  `src/runtime/runtime_bus_bridge.cpp` but are **not declared in any public header** (only
  forward-declared in `runtime.cpp`), so using them means declaring them yourself and accepting
  that they are internal and are cleared/replaced by `run_game` itself.

## 4. ROM read overrides
Declared in `src/gba/gba_bus.h` (namespace `gba`, `extern "C"`):

```
int (*g_rom_read16_override)(uint32_t address, uint16_t original_value, uint16_t* out_value);
int (*g_rom_read32_override)(uint32_t address, uint32_t original_value, uint32_t* out_value);
```
Return non-zero to make the read return `*out_value`; zero keeps the original. They are
read-only views: the ROM image itself is not modified, so hash verification, save states and
other consumers of the raw bytes are unaffected. Intended for narrowly scoped, opt-in patches
where guest code must see a configuration-derived constant (e.g. a table read with LDRH/LDRSH or
a literal-pool word). `address` is a guest address (`0x08xxxxxx`...). They are called on the guest
thread for every matching read, so keep them cheap. They are reset by `run_game` (section 1).

Related, narrower hooks in `src/armv4t/runtime_arm.h`: `g_runtime_bus_read_override` (exact-PC,
any mapped read) and `g_runtime_thumb_alu_imm_override`.

## 5. Function-entry hook
`extern "C" void (*g_runtime_fn_entry_hook)(uint32_t entry_pc)` (`src/armv4t/runtime_arm.h`,
defined in `src/armv4t/runtime_arm.cpp`). When non-null, the generated prologue of every
recompiled function calls it with the guest entry PC before its first instruction. At that
moment `g_cpu.R[0..3]` hold the AAPCS arguments and `g_cpu.R[14]` the return address. It is
observation-only. A non-null hook costs a call per function entry; null costs one not-taken
branch. It has C linkage (the header declares it inside its `extern "C"` block), so declare it
in the host as `extern "C" void (*g_runtime_fn_entry_hook)(uint32_t);`. For replacement semantics (changing CPU
state) `gbarecomp` has a separate, reviewed `[[mod_function_hook]]` mechanism
(`gba_mod_function_entry`), not a general hook.

## 6. `RunOptions` fields a host usually fills
(`src/runtime/runtime.h`; `RunOptions` is aggregate-default-initialised.)

| Field | Purpose |
|---|---|
| `builtin_game_name`, `builtin_rom_sha1`, `builtin_rom_crc32` | Baked-in identity so the executable runs without a `game.toml`. The SHA-1 is what `tools/verify_rom_hash` of this repo can check. |
| `max_view_width`, `max_resize_view_width`, `max_resize_view_height`, `resize_driven_view`, `resize_view_sizing`, `density_mm_per_logical_px` | Opt-in and ceilings for widescreen / adaptive view. Defaults keep 240x160. |
| `presentation_request` | Per-present request for native-view / top anchoring. |
| `extended_view_init`, `extended_view_frame` | Section 2.3. |
| `input_frame`, `touch_gesture_claims`, `touch_pad_default` | Section 3. |
| `host_overlay` | `void (*)(HostOverlay*)`, drawn after the game image and before the runtime menu (`src/runtime/host_overlay.h`): lines, rects, circles, arcs, text (if the UI font stack is built). A drawing seam that needs no SDL interception. |
| `ui_extra_items`, `ui_get/ui_set/ui_action/ui_enabled`, `ui_get_text/ui_set_text` | Game-owned entries in the in-game settings menu (a place to expose this library's settings). |
| `expose_assist_tools`, `save_state_slot_count`, `rewind_*` | Assist tools (save states, rewind, fast-forward). |
| `launcher_*` | Pre-boot launcher identity; the runtime itself ignores them. |
| `tcp_command`, `diagnostics_snapshot` | Debug/diagnostics integration. |

## 7. Where `gbarecomp` calls SDL (what a host can wrap)

Every SDL call of the presentation/audio/input path lives in `src/runtime/host_window.cpp`
(`gbarecomp::HostWindow`); `src/runtime/runtime.cpp` makes none (only
`src/runtime/mobile_platform.cpp` calls `SDL_AndroidGetInternalStoragePath`; the runtime UI
backend is third-party code and may add its own calls, **(uncertain)**).

| Purpose | SDL entry point | Notes |
|---|---|---|
| Game frame upload | `SDL_UpdateTexture` | One call in `HostWindow::present(const uint8_t* rgb888)`. Texture is `SDL_PIXELFORMAT_RGB24`, `SDL_TEXTUREACCESS_STREAMING`, size = the render width x height (expanded view included), pitch = `width*3`. This is the **only** `SDL_UpdateTexture` call, so "the game texture" is every call from the runtime (excluding any UI backend). |
| Draw game image | `SDL_RenderCopy` | Several call sites in `present` (letterboxed destination rect, optional sharp-filter path). Not every `RenderCopy` is the game image (menu/overlay may add more **(uncertain)**). |
| Present | `SDL_RenderPresent` | Two sites in `present`. |
| Events | `SDL_PollEvent`, `SDL_WaitEvent` (blocking helper), `SDL_PumpEvents` | `HostWindow` drains events itself; `SDL_PumpEvents` is also used. Keyboard state is polled with `SDL_GetKeyboardState`; gamepads via `SDL_GameController*`. |
| Audio | `SDL_OpenAudioDevice`, `SDL_QueueAudio`, `SDL_PauseAudioDevice`, `SDL_GetQueuedAudioSize` | See the warning below. |
| Window/renderer | `SDL_CreateWindow`, `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_SetWindowTitle`, ... | Useful for capturing the `SDL_Renderer*`. |

**Audio warning.**
0. **The default audio path is reachable: interpose `SDL_OpenAudioDevice`.** Register `gbaimp::open_audio_device_processed` (`gbaimp/audio_device.h`) as the `open_audio_device` handler (Linux: add `-Wl,--wrap=SDL_OpenAudioDevice`; Windows: `install_sdl_interposers` patches it). gbarecomp calls it once per run with `want.callback = gba_audio_callback`, S16, mono, 65536 Hz, `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE`, and `gba_audio_callback` runs on SDL's audio thread; the helper wraps it and processes each filled buffer at the *obtained* rate (which may differ from 65536 Hz). In direct mode (`GBARECOMP_AUDIO_DIRECT`) `callback` is null and `queue_audio_processed` handles `SDL_QueueAudio`.
1. `SDL_QueueAudio` is only used in the *direct* path, enabled by the environment variable
   `GBARECOMP_AUDIO_DIRECT` (non-empty and not `0`). The default path uses an SDL audio
   **callback** (`gba_audio_callback`) fed by a rate-adaptive bridge, so wrapping
   `SDL_QueueAudio` sees nothing unless a host sets that variable before the window opens
   (`setenv`/`_putenv` early in `main`, **or** inside an early `RunOptions` callback).
2. The device is opened at **65536 Hz, mono, signed 16-bit** (`want.freq = 65536`,
   `want.channels = 1`), and `HostWindow::push_audio_samples` takes mono samples. The comment in
   `host_window.h` still says 32.768 kHz, but the code uses 65536 (`host_window.cpp`, around the
   `SDL_OpenAudioDevice` call).
   `gbaimp::process_audio_block(samples, n)` (2 arguments) still means **interleaved stereo** at
   `HdAudioSettings::target_sample_rate`, but there is now a channel- and rate-aware overload,
   `gbaimp::process_audio_block(samples, n_int16, channels, sample_rate)`. A host that routes the
   `gbarecomp` stream into it passes `channels = 1` and `sample_rate = 65536` and needs no
   stereo duplication; the HD track is folded down to mono and resampled to the device rate, and
   `AudioDsp::process_mono` applies the DSP chain.

### Intercepting on GCC/Clang (ELF, MinGW)
Expand `GBAIMP_DEFINE_LD_WRAPS()` in one host `.cpp` and link with the six `-Wl,--wrap=SDL_*` flags listed in `gbaimp/sdl_interpose.h`; the underlying `-Wl,--wrap=SDL_UpdateTexture` etc. works when `gbarecomp`'s object code and the host are linked
into one executable and SDL is resolved at link time (static archive of `gbarecomp`, any SDL
link mode on ELF/MinGW where the references are undefined symbols in `gbarecomp`'s objects).
`--wrap` rewrites undefined references in *all* linked objects including the static `gbarecomp`
archive, which is what makes it work. It does not affect calls made *inside* SDL's own shared
library.

### On MSVC (no `--wrap`)
`gbarecomp` itself provides no SDL-interception seam for MSVC (nothing equivalent to `--wrap`, no function table, no callback around
`SDL_UpdateTexture` / `SDL_RenderPresent`; its CMake has explicit MSVC handling for the stack size and the host link stack). This library
fills that gap on the host side with **Import Address Table patching**: `gbaimp::install_sdl_interposers()` (`gbaimp/sdl_interpose.h`)
rewrites the slots that point at `SDL2.dll`'s exports in the host executable and in the modules loaded at that moment, so `gbarecomp`'s object code
(statically linked into the host) reaches the host's handlers; the handlers call the originals through `gbaimp::sdl_real()`. Requirements and limits:
`SDL2.dll` must be loaded as a DLL by that name (an import library), only IAT / delay-load imports are intercepted, and the call has to run before
`run_game()` makes its first SDL call (first thing in `main()`). The `sdl_interpose` and `audio_device` tests exercise it on the test executable's own import table; verify it in your host against the `gbarecomp` revision you pin. Alternatives, if you prefer not to patch:

1. **Use the seams `gbarecomp` already provides**:
   - `RunOptions::host_overlay` to draw HUD/overlays (`PerfHud` needs an `SDL_Renderer*`, which the overlay interface does not expose; you would have to
     draw with the overlay primitives or capture the renderer some other way).
   - Colour correction: `gbarecomp` has its own present-time colour LUT (`src/runtime/color_lut.{h,cpp}`, configured from the `[screen]` settings)
     applied in `HostWindow::present` before `SDL_UpdateTexture`.
   - `RunOptions::input_frame` / `active_bus()` for input (section 3).
2. **Patch or fork `gbarecomp`** to add a small present/audio callback in `HostWindow` (`present()` and `push_audio_samples()` are single functions with
   the data already in hand). An upstream seam would remove the need for any interception but means maintaining a change in a third-party repository.
3. **Link SDL statically and rename** the symbols to host-provided functions (needs a custom SDL build; unverified).
4. A MinGW-w64 toolchain on Windows supports `--wrap` like Linux.

## 8. Summary of what the host must guarantee
1. Install hooks after `run_game` starts (section 1); keep them cheap and thread-safe, they run
   on the guest thread.
2. Opt in to a wider view through the `RunOptions` ceilings, or the `g_ws_*` machinery stays idle.
3. Treat `g_ws_*` read-only state and `ExtendedViewFrameInfo` as valid only for the current frame.
4. Do not assume SDL audio is queued (section 7); detect the actual format.
5. Pin the `gbarecomp` revision: these are internal APIs and change between releases.
