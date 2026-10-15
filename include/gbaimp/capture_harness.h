#pragma once
// Headless capture + scripted-input harness (developer tooling, environment-gated).
//
// Lets a host run without a screen, drive the game with a deterministic input script and
// save PNG frames, to verify a game without playing it by hand. Everything is inert unless
// one of the variables documented in docs/CAPTURE_HARNESS.md is set: the hot-path entry
// points start with a cached flag test and return immediately when the harness is off.
//
// Every variable is "<prefix>_<NAME>"; the prefix is chosen by the host through
// CaptureConfig::env_prefix (for example prefix "MYGAME" gives MYGAME_CAPTURE_DIR).
//
// Host wiring (see docs/CAPTURE_HARNESS.md):
//   capture::configure(cfg)                  once, first thing in main()
//   capture::extra_runtime_args()            before the runtime parses its argv
//   capture::settings_overrides()            applied by the host on top of its config file
//   capture::on_emulated_frame(frame)        once per emulated frame
//   capture::wants_native_frame / on_native_frame   GameHostConfig hooks (raw PPU frame)
//   capture::on_before_present(renderer)     GameHostConfig::before_present
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SDL_Renderer;

namespace gbaimp {
namespace capture {

struct CaptureConfig {
    // Prefix of every environment variable (a trailing '_' is optional). Default "GBAIMP".
    const char* env_prefix = "GBAIMP";
    // Diagnostic lines are printed on stderr as "[log_tag] ...".
    const char* log_tag = "capture";
    // Applies one live setting (an ini-style key and an integer value) for capture variants
    // ("key=value" entries). Return value is ignored. Game specific settings live here.
    int (*apply_setting)(const char* key, int value) = nullptr;
    // Environment access, overridable for tests. nullptr uses std::getenv / the C runtime.
    const char* (*getenv_fn)(const char* name) = nullptr;
    void (*setenv_fn)(const char* name, const char* value) = nullptr;
    // Path of the ROM file. Only used to find where the runtime writes its savestate slots
    // (<rom without extension>.state<N>) so the ".meta" sidecar of <P>_SAVESTATE_SAVE_PPU can be
    // written next to them. The variable <P>_SAVESTATE_ROM overrides it. Optional.
    const char* rom_path = nullptr;
};

// Parses the environment (once per call) and resets all harness state. Calling a hook
// before configure() configures with the defaults above.
void configure(const CaptureConfig& config);

// True when any harness feature is enabled.
bool active();

// Once per emulated frame. `hw_frame_count` is the PPU frame counter of the runtime. Arms
// captures, pushes scripted host key events, handles <prefix>_EXIT_FRAME. Scripted GBA input
// is not applied here but through extra_runtime_args() (input replay trace).
void on_emulated_frame(std::uint64_t hw_frame_count);

// True while a "native" capture is pending (wire to GameHostConfig::wants_native_frame).
bool wants_native_frame();
// The raw RGB24 PPU framebuffer before colour correction (GameHostConfig::on_native_frame).
void on_native_frame(const void* pixels, int pitch, int width, int height);
// Right before the real present (GameHostConfig::before_present): applies WINDOW_SIZE,
// writes an "output" capture when one is pending, runs capture variants, handles exit.
void on_before_present(SDL_Renderer* renderer);

// <prefix>_SET overrides ("key=value;key=value"): the host applies them over its config.
const std::vector<std::pair<std::string, int>>& settings_overrides();

// Changes the live-setting callback after configure().
void set_setting_callback(int (*cb)(const char* key, int value));

// Arguments to inject into argv: "--load-state <file>" for <prefix>_SAVESTATE_LOAD. Also
// exports GBARECOMP_ASSIST_SCRIPT (savestate save, fast-forward) and GBARECOMP_INPUT_REPLAY
// (the expanded input script) for the runtime to pick up.
std::vector<std::string> extra_runtime_args();

// Non-empty when a variable could not be honoured (for example <P>_SAVESTATE_SAVE_PPU asks for a
// frame that has already passed). The problem is also printed on stderr as "ERROR ...", and the
// affected savestate saves are not scheduled. Valid after extra_runtime_args().
const std::string& configuration_error();

// --- pure helpers (no SDL, no global state), public for tests and tools -----------------

// GBA KEYINPUT bit positions (active low: a cleared bit means "pressed").
enum : unsigned {
    kKeyA = 1u << 0, kKeyB = 1u << 1, kKeySelect = 1u << 2, kKeyStart = 1u << 3,
    kKeyRight = 1u << 4, kKeyLeft = 1u << 5, kKeyUp = 1u << 6, kKeyDown = 1u << 7,
    kKeyR = 1u << 8, kKeyL = 1u << 9,
};

struct InputRule {
    std::uint64_t first = 0, last = 0;  // inclusive emulated-frame range
    std::uint64_t period = 0;           // 0 = hold for the whole range
    std::uint64_t hold = 0;             // frames held at the start of each period
    unsigned mask = 0;                  // pressed buttons (kKey* bits)
};

struct Variant {
    std::string spec;                                // e.g. "852x480 screen_mask=0"
    int w = 0, h = 0;                                // 0 = keep the current size
    std::vector<std::pair<std::string, int>> sets;   // applied through apply_setting
};

// "A", "start", "UP", "NONE" (empty / NONE add nothing). False for an unknown name.
bool parse_button(const std::string& token, unsigned* mask);
// Input script: entries "<first>[-<last>] [every <P>] [hold <H>] : BTN[+BTN...]" separated by
// newlines or ';', '#' starts a comment. A single frame N is held for frames N and N+1.
std::vector<InputRule> parse_input_script(const std::string& text);
unsigned scripted_mask(const std::vector<InputRule>& rules, std::uint64_t frame);
// "# gbarecomp-keyinput-v1" trace ("frame,0xKEYINPUT" on every change) for the rules.
std::string build_input_trace(const std::vector<InputRule>& rules, const char* origin_name);
// "300,600-610,2000-3000/250" -> sorted unique frame list (capped at 100000 entries).
std::vector<std::uint64_t> parse_frame_list(const std::string& text);
// "key=value;key=value" (also ',' or newline separated).
std::vector<std::pair<std::string, int>> parse_settings(const std::string& text);
// "852x480 screen_mask=0|960x640|1536x864 mask_intensity=60".
std::vector<Variant> parse_variants(const std::string& text);
// "WxH" with both > 0.
bool parse_size(const std::string& text, int* w, int* h);
// "f000300" for frame 300.
std::string frame_label(std::uint64_t frame);
// Variant spec to a file name fragment: keeps letters, digits, x and _; space , = become _
// ("852x480 screen_mask=0" -> "852x480_screen_mask_0").
std::string sanitize_variant(const std::string& spec);

// --- savestates by PPU frame ---------------------------------------------------------------

// The runtime's assist script ("<pump>:save<slot>", GBARECOMP_ASSIST_SCRIPT) counts pumps of the
// host loop since the process started, not PPU frames. Measured on the runtime (one pump per
// emulated frame, with or without fast-forward):
//   fresh boot                        the state saved at pump P holds PPU frame P + 1
//   run started from a state at frame L   the state saved at pump P holds PPU frame L + P - 2
// (L is the frame the runtime reports when it loads that state). The first allowed pump is 2.
constexpr std::uint64_t kSavestateLoadPumpBias = 2;           // pump = G - L + 2 after a state load
constexpr std::uint64_t kSavestateBootPumpBias = 1;           // pump = G - 1 from a fresh boot
// The runtime accepts slots 1..10.
constexpr int kSavestateMinSlot = 1;
constexpr int kSavestateMaxSlot = 10;

struct SavestateRequest {
    std::uint64_t ppu_frame = 0;  // absolute PPU frame to save at
    int slot = 1;                 // runtime slot, 1..10
};
struct SavestatePumpEvent {
    std::uint64_t pump = 0;       // assist-script pump index
    int slot = 1;
    std::uint64_t ppu_frame = 0;  // the request it came from
};

// "300:1,900:2" (separators ',' ';' or whitespace; slot optional, default 1). Entries are kept in
// the order given. False with a message in *error for an unparsable entry or a slot out of range.
bool parse_savestate_requests(const std::string& text, std::vector<SavestateRequest>* out,
                              std::string* error);
// Pure translation of absolute PPU frames to pumps. `from_state` false: the run boots from the
// ROM (loaded_ppu_frame is ignored); true: it starts from a state at `loaded_ppu_frame`. Output is
// sorted by pump. Errors (false, message in *error): a frame that has already passed (before the
// loaded frame, or before the first possible save), two saves on the same frame, one slot used
// twice. The runtime performs one save per pump, hence no two events share a pump.
bool plan_savestate_pumps(const std::vector<SavestateRequest>& requests, bool from_state,
                          std::uint64_t loaded_ppu_frame, std::vector<SavestatePumpEvent>* out,
                          std::string* error);
// "1:fast_on;<pump>:save<slot>;...;<last+1>:fast_off" (the fast-forward parts only when asked).
std::string build_assist_script(bool fast_forward, const std::vector<SavestatePumpEvent>& saves);
// Sidecar of a savestate file: the file name plus ".meta", text "ppu_frame=<n>" plus a line feed.
std::string savestate_meta_path(const std::string& state_path);
std::string format_savestate_meta(std::uint64_t ppu_frame);
bool parse_savestate_meta(const std::string& text, std::uint64_t* ppu_frame);

// --- PNG ---------------------------------------------------------------------------------

// 8-bit RGB PNG (stored deflate, no zlib dependency). Empty vector on invalid arguments.
std::vector<std::uint8_t> encode_png_rgb(const std::uint8_t* rgb, int width, int height,
                                         int pitch_bytes);
// Returns false on invalid arguments or I/O error.
bool write_png_rgb(const std::string& path, const std::uint8_t* rgb, int width, int height,
                   int pitch_bytes);

}  // namespace capture
}  // namespace gbaimp
