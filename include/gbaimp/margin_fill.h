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

// The up to four bars around `dst` inside a win_w x win_h window: top and bottom span the full
// width, left and right only the rows of `dst`. Returns the number of non-empty bars written
// to `out` (capacity 4).
int margin_bars(const SDL_Rect& dst, int win_w, int win_h, SDL_Rect out[4]);

} // namespace gbaimp
