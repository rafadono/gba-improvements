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

enum class AspectPreset : uint8_t {
    Native = 0,    // 3:2, the original picture
    Wide16x10,
    Wide16x9,
    Ultra21x9,
    Super32x9,
    Custom,        // custom_w : custom_h
    Adaptive       // follows the window
};

struct AspectSettings {
    AspectPreset preset = AspectPreset::Native;
    int custom_w = 16;
    int custom_h = 9;
    // Upper bound for the logical width. <= 0 means kDefaultMaxViewWidth.
    int max_view_width = 0;
};

// Width / height of the preset (Adaptive and invalid custom values give 0 = "not fixed").
double aspect_ratio(const AspectSettings& s);

// Logical width for a ratio at the native height, rounded to the nearest pixel and clamped to
// [240, cap]. A ratio <= 0 or narrower than 3:2 gives 240.
int view_width_for_ratio(double ratio, int cap = kDefaultMaxViewWidth);

// Logical width for the settings. Adaptive uses the window size (window_w x window_h);
// every other preset ignores it.
int resolve_view_width(const AspectSettings& s, int window_w = 0, int window_h = 0);

// Extra pixels per side (left gets the floor) of a logical width.
void split_extra(int view_width, int* left, int* right);

// Menu entry of the preset table.
struct AspectChoice {
    AspectPreset preset;
    const char* label;
};
// Presets in menu order: Native .. Super32x9, then Custom and Adaptive.
const std::vector<AspectChoice>& aspect_choices();
const char* aspect_label(AspectPreset p);

// Parses "16:9", "21:9", "2.39" or "1.7778" into custom_w / custom_h (decimals as x100 fixed
// point). Returns false and leaves `out` alone for anything else or ratios outside [1.0, 8.0].
bool parse_custom_ratio(const std::string& text, AspectSettings* out);

} // namespace gbaimp
