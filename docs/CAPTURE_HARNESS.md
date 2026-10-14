# Headless capture + scripted-input harness

Developer tooling to verify a game without playing it by hand: run the host headless (SDL dummy video driver, software
renderer), drive it with a deterministic input script and save PNG frames that can be opened with any image viewer.
Everything is **environment-gated**: with none of the variables below set, the hooks cost one cached flag test and
nothing else.

Code: `include/gbaimp/capture_harness.h`, `src/capture_harness.cpp` (namespace `gbaimp::capture`, links SDL2 like the rest
of the library). Helper scripts: `tools/capture/`. Test: `tests/test_capture_harness.cpp`.

## Variable names: the host chooses the prefix

Every variable is `<prefix>_<NAME>`. The prefix is `CaptureConfig::env_prefix` (a trailing `_` is optional). In the table,
`<P>` stands for it; with prefix `MYGAME` the first row is `MYGAME_CAPTURE_DIR`. The library never hard-codes a prefix
(the default is `GBAIMP`).

## Frame numbers

Every frame number is the **PPU frame counter** of the runtime (0 at boot, restored by a savestate load). Captures arm
when the counter reaches the number and are written at the next present, so the image is the frame at, or at most one
frame after, the requested number (with fast-forward up to `multiplier - 1` frames later).

## Environment variables

| Variable | Meaning |
|---|---|
| `<P>_CAPTURE_DIR=<dir>` | Output directory (created). Default `captures` when `<P>_CAPTURE_FRAMES` is set. |
| `<P>_CAPTURE_FRAMES=300,600-610,2000-3000/250` | Frames to capture. Ranges `a-b`, optional `/step` (at most 100000 entries). |
| `<P>_CAPTURE_MODE=output\|native\|both` | `output` (default): the final composed window image read back with `SDL_RenderReadPixels` right before the real present (size = renderer output size). `native`: the raw PPU framebuffer as given to `SDL_UpdateTexture`, before colour correction. |
| `<P>_CAPTURE_VARIANTS="852x480 key=1\|960x640 key=0"` | One `output` image per entry at each captured frame: a window size `WxH` and/or live settings `key=value`, handed to `CaptureConfig::apply_setting`. The harness waits for the size to settle (at least 4 presents) and grabs. Use it on a static frame. File name `f<frame>_output_<entry sanitized>.png`. |
| `<P>_EXIT_AFTER_CAPTURE=1` | Push `SDL_QUIT` once every requested image has been written. |
| `<P>_EXIT_FRAME=<n>` | Push `SDL_QUIT` at that frame even without captures. |
| `<P>_WINDOW_SIZE=WxH` | Window / output size, applied with `SDL_SetWindowSize` on the first presents (re-asserted for up to 60 presents). |
| `<P>_INPUT_SCRIPT=<file or inline>` | Scripted GBA input, see below. |
| `<P>_PUSH_KEYS="9000:Escape;9200:F2"` | Host key presses `<frame>:<SDL key name>` separated by `;`, pushed as SDL key events at that frame (not translated to GBA buttons). |
| `<P>_SET="key=1;other=2"` | Settings overrides at start. The host reads them with `settings_overrides()` and applies them over its config file (and should not save the config in a harness run). |
| `<P>_SAVESTATE_LOAD=<file>` | Load a savestate at start (`--load-state <file>` through `extra_runtime_args()`). |
| `<P>_SAVESTATE_SAVE=<frame>:<slot>` | Save into `<slot>` (default 1) at that frame (`GBARECOMP_ASSIST_SCRIPT=<frame>:save<slot>`, the frame is at least 2). |
| `<P>_FAST=1` | Fast-forward (`GBARECOMP_ASSIST_SCRIPT=1:fast_on`; paired with `fast_off` after a savestate save). |

`SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` (set by the runner scripts) make the run headless.

## Input script

Text file (or the inline string itself); entries separated by newlines or `;`; `#` starts a comment.

```
120-125: A                   # hold A for frames 120..125 (inclusive)
200: START                   # a single frame N is held for frames N and N+1
1500-6000 every 100 hold 3: A     # press A for 3 frames at the start of every 100-frame period
300-340: UP+A                # several buttons
```
Buttons: `A B L R START SELECT UP DOWN LEFT RIGHT` (case-insensitive). `every` without `hold` holds 2 frames. The script is
expanded into a `frame,0xKEYINPUT` trace (active low, one line per change) written to `<capture dir>/input_trace.txt`
(or `<lower-case prefix>_input_trace.txt` when there is no capture dir) and handed to the runtime's deterministic
input replay through `GBARECOMP_INPUT_REPLAY`, so the same script always yields the same run. Writing KEYINPUT from a
per-frame hook is not enough: the host pump overwrites it before the guest reads it. If `GBARECOMP_INPUT_REPLAY` is
already set the harness leaves it alone.

## Adopting it in a host

1. Once, first thing in `main()`:
   ```cpp
   gbaimp::capture::CaptureConfig c;
   c.env_prefix = "MYGAME";                  // variables become MYGAME_CAPTURE_DIR, ...
   c.log_tag = "capture";
   c.apply_setting = &my_live_setting;       // optional: int(const char* key, int value) for variants
   gbaimp::capture::configure(c);
   ```
2. Before the runtime parses its command line, append `capture::extra_runtime_args()` to argv (it also exports
   `GBARECOMP_ASSIST_SCRIPT` / `GBARECOMP_INPUT_REPLAY` for the runtime).
3. After loading the host config, apply `capture::settings_overrides()` (pairs of ini-style key and integer value) with the
   host's own setter, and skip saving the config when `capture::active()`.
4. Hooks (the SDL ones are `GameHostConfig` callbacks, see `game_host.h`):

   | Hook | Wire to | Called |
   |---|---|---|
   | `capture::on_emulated_frame(ppu_frame_count)` | the host's per-emulated-frame hook | once per emulated frame; arms captures, pushes keys, handles `EXIT_FRAME` |
   | `capture::wants_native_frame()` / `capture::on_native_frame(px, pitch, w, h)` | `GameHostConfig::wants_native_frame` / `on_native_frame` | raw PPU framebuffer, only while a native capture is pending |
   | `capture::on_before_present(renderer)` | `GameHostConfig::before_present` | right before the real `SDL_RenderPresent`; window size, output capture, variants |

   A host that already uses one of these callbacks calls the capture function from its own function. All hooks return
   immediately when the harness is off (`capture::active()` is false).
5. Game-specific live settings (an overlay's family, a mask intensity, ...) are not known to the library: they go through
   `apply_setting`, which the host implements with its own setters.

The parsers and the PNG encoder are public (`parse_input_script`, `scripted_mask`, `build_input_trace`,
`parse_frame_list`, `parse_settings`, `parse_variants`, `encode_png_rgb`, `write_png_rgb`) and need neither SDL state nor
a ROM. `CaptureConfig::getenv_fn` / `setenv_fn` replace the environment access, which is how the tests run with arbitrary
prefixes.

## Runner and image tools (`tools/capture/`)

| File | Generic? | Purpose |
|---|---|---|
| `run_capture.sh`, `run_capture.ps1` | yes (take the prefix, executable and ROM as arguments) | set the variables, run the host with the dummy drivers and a timeout, list the PNGs |
| `montage.py` | yes | contact sheet / crop-zoom of 8-bit RGB PNGs (Python standard library only) |
| `stroke_runs.py` | yes | run-length histogram of dark strokes in a crop, to check integer scaling of text or line art |

Not in the library because they are tied to one game: input scripts that reach a given screen (frame numbers depend on
the game's boot and intro), scenario scripts that sweep that game's own settings keys, and any list of ROM or BIOS file
names. Keep them in the host repository and pass them with `-s` / `-Script`.

## Limitations

* With fast-forward only some frames are presented, so a capture can lag the requested frame by a few frames.
* The `output` capture needs a renderer that supports readback; the software renderer (dummy driver) does.
* `native` frames are before colour correction; `output` frames include colour profile, screen mask and overlays.
* Hooks are meant for the emulation / render thread; `configure()` is not meant to be called while they run.
