#pragma once
// Aspect-ratio selection for a GBA static-recompilation host (game agnostic).
//
// The runtime renders a logical view of `width x 160` pixels (240 = the original 3:2 picture).
// This module turns a player-facing choice (preset, custom ratio, window-following) into that
// width, clamped to a cap, and builds the label / width tables a launcher menu needs. It never
// touches the runtime: the host copies the numbers into its run options.
//
//   gbaimp::AspectSettings s;                 // 3:2, default cap
//   s.preset = gbaimp::AspectPreset::Ultra21x9;
//   int w = gbaimp::resolve_view_width(s, window_w, window_h);   // 373
#include <cstdint>
#include <string>
#include <vector>

namespace gbaimp {

constexpr int kNativeViewWidth = 240;
constexpr int kNativeViewHeight = 160;
// Wider than the widest preset (32:9 = 569 logical pixels): a safe default cap.
constexpr int kDefaultMaxViewWidth = 640;
// Tallest logical view a preset or a window may ask for (240x320 is a 3:4 portrait window).
constexpr int kDefaultMaxViewHeight = 320;

enum class AspectPreset : uint8_t {
    Native = 0,    // 3:2, the original picture
    Wide16x10,
    Wide16x9,
    Ultra21x9,
    Super32x9,
    Custom,        // custom_w : custom_h
    Adaptive,      // follows the window
    // Taller-than-native views (more rows, see resolve_view_size()). They are appended so the numeric
    // values of the older presets never change; aspect_choices() lists them before Custom.
    Standard4x3,   // 240x180
    Standard5x4,   // 240x192
    Console10x7    // 320x224 (the 32X-style H40 frame): wider AND taller than native
};

struct AspectSettings {
    AspectPreset preset = AspectPreset::Native;
    int custom_w = 16;
    int custom_h = 9;
    // Upper bound for the logical width. <= 0 means kDefaultMaxViewWidth.
    int max_view_width = 0;
    // Upper bound for the logical height. <= 0 means kDefaultMaxViewHeight.
    int max_view_height = 0;
};

// Width / height of the preset (Adaptive and invalid custom values give 0 = "not fixed").
double aspect_ratio(const AspectSettings& s);

// Logical width for a ratio at the native height, rounded to the nearest pixel and clamped to
// [240, cap]. A ratio <= 0 or narrower than 3:2 gives 240.
int view_width_for_ratio(double ratio, int cap = kDefaultMaxViewWidth);

// Logical width for the settings. Adaptive uses the window size (window_w x window_h);
// every other preset ignores it.
int resolve_view_width(const AspectSettings& s, int window_w = 0, int window_h = 0);

// Logical size of a view: the runtime renders `width x height` pixels, the original picture
// (240x160) sitting in the middle.
struct ViewSize {
    int width = kNativeViewWidth;
    int height = kNativeViewHeight;
};

// Logical size for a ratio. At least 3:2 grows the width at the native height (same as
// view_width_for_ratio); narrower than 3:2 keeps 240 columns and grows the height
// (240 / ratio rows, nearest pixel). Both axes are clamped to [native, cap]. A ratio <= 0 gives
// the native 240x160.
ViewSize view_size_for_ratio(double ratio, int max_width = kDefaultMaxViewWidth,
                             int max_height = kDefaultMaxViewHeight);

// Logical size for the settings. Fixed presets (Native .. Super32x9) keep 160 rows; the tall
// presets give 240x180, 240x192 and 320x224; Custom uses its ratio; Adaptive uses the window
// size (a window wider than 3:2 never gets extra rows). Caps from max_view_width /
// max_view_height. The width always equals resolve_view_width() except for the tall presets.
ViewSize resolve_view_size(const AspectSettings& s, int window_w = 0, int window_h = 0);

// Extra pixels per side (left gets the floor) of a logical width.
void split_extra(int view_width, int* left, int* right);

// Extra rows above / below a logical height. `top_share` is the fraction given to the top
// (0.5 = even split, the extra row of an odd count goes to the bottom for 0.5). Result is
// clamped so top + bottom == height - 160.
void split_extra_rows(int view_height, int* top, int* bottom, double top_share = 0.5);

// Menu entry of the preset table.
struct AspectChoice {
    AspectPreset preset;
    const char* label;
};
// Presets in menu order: Native .. Super32x9, the tall presets (4:3, 5:4, 10:7), then Custom and Adaptive.
const std::vector<AspectChoice>& aspect_choices();
const char* aspect_label(AspectPreset p);

// Fixed presets (Native .. Super32x9) as the static tables a launcher expects
// (RunOptions::launcher_aspect_labels / launcher_aspect_view_widths / launcher_num_aspects).
// Widths are the presets' own, before any cap; the arrays live for the whole process.
struct LauncherAspectTable {
    const char* const* labels;
    const uint16_t* widths;
    int count;
};
LauncherAspectTable launcher_aspect_table();
// Same, for a game whose validated maximum logical width is `max_width`: only the presets that fit
// (width <= max_width) and, when the maximum is not itself a preset, a last "Widest (N px)" entry.
// max_width <= 0 gives the full table. The arrays are static and rebuilt by each call: call it once
// while filling the run options.
LauncherAspectTable launcher_aspect_table(int max_width);

// Parses "16:9", "21:9", "2.39" or "1.7778" into custom_w / custom_h (decimals as x100 fixed
// point). Returns false and leaves `out` alone for anything else or ratios outside [1.0, 8.0].
bool parse_custom_ratio(const std::string& text, AspectSettings* out);

} // namespace gbaimp
