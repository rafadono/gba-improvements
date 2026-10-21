#pragma once
// Backdrop for the screen area the game image does not cover (game agnostic).
//
// The game image is placed with a viewport rectangle (see game_host.h). Everything outside it
// is the "margin": pillar bars when the window is wider than the picture, letterbox bars when it
// is taller, or both. By default it is black. This module can paint it with something that
// belongs to the picture instead:
//   Pillar      black (the original look).
//   EdgeRepeat  the outermost column / row of the picture stretched over each bar, dimmed.
//   Blur        the picture itself, heavily blurred and dimmed, stretched over the whole window.
// It is drawn before the image, so the image always stays untouched on top.
#include <SDL.h>

#include <cstdint>
#include <string>

namespace gbaimp {

enum class MarginFillMode : uint8_t { Pillar = 0, EdgeRepeat = 1, Blur = 2 };

struct MarginFillSettings {
    MarginFillMode mode = MarginFillMode::Pillar;
    // Brightness of the backdrop, 0..100 (percent of the picture's own colour).
    int brightness = 45;
};

class MarginFill {
public:
    static MarginFill& instance();

    const MarginFillSettings& settings() const { return settings_; }
    void set_mode(MarginFillMode m) { settings_.mode = m; }
    void set_brightness(int percent);

    // Paints the margin around `dst` (window pixels) using the part `src` of `texture`.
    // Fills black first, then the chosen backdrop. Returns true when something other than black
    // was drawn. Safe with any texture / renderer: a failure leaves the black fill in place.
    bool draw(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect& src,
              const SDL_Rect& dst, int win_w, int win_h);

    // Frees the cached blur target (also done lazily when the renderer changes).
    void release();

private:
    MarginFillSettings settings_;
    SDL_Texture* blur_target_ = nullptr;
    SDL_Renderer* blur_owner_ = nullptr;
};

class SettingsRegistry;

// Names and .ini placement of the two settings register_margin_fill_settings() adds.
struct MarginFillSettingsOptions {
    std::string mode_id = "video.margin_fill";
    std::string brightness_id = "video.margin_brightness";
    std::string menu_section = "Display and Graphics";
    std::string mode_label = "Screen Margin Fill";
    std::string mode_tooltip = "What fills the bars around the picture: black, the picture's edge, or a blurred copy";
    std::string brightness_label = "Margin Fill Brightness";
    std::string brightness_tooltip = "How bright the margin fill is (percent of the picture)";
    std::string ini_section = "Video";
    std::string mode_ini_key = "margin_fill";
    std::string brightness_ini_key = "margin_brightness";
};

// Adds a Choice setting (Black / Edge repeat / Blur) and an Int brightness setting (0..100, step 5,
// greyed out while the mode is Black), both saved to the .ini. Opt in: register_standard_settings()
// does not call it. `fill` must outlive the registry.
void register_margin_fill_settings(SettingsRegistry& reg, MarginFill& fill,
                                   const MarginFillSettingsOptions& options = MarginFillSettingsOptions());

// The up to four bars around `dst` inside a win_w x win_h window: top and bottom span the full
// width, left and right only the rows of `dst`. Returns the number of non-empty bars written
// to `out` (capacity 4).
int margin_bars(const SDL_Rect& dst, int win_w, int win_h, SDL_Rect out[4]);

} // namespace gbaimp
