#include "gbaimp/margin_fill.h"

#include "gbaimp/host_settings.h"

#include <algorithm>

namespace gbaimp {

namespace {
constexpr int kBlurW = 32;
constexpr int kBlurH = 20;
}

MarginFill& MarginFill::instance() {
    static MarginFill s;
    return s;
}

void MarginFill::set_brightness(int percent) {
    settings_.brightness = std::clamp(percent, 0, 100);
}

int margin_bars(const SDL_Rect& dst, int win_w, int win_h, SDL_Rect out[4]) {
    int n = 0;
    if (win_w <= 0 || win_h <= 0) return 0;
    if (dst.y > 0) out[n++] = {0, 0, win_w, dst.y};
    if (dst.y + dst.h < win_h) out[n++] = {0, dst.y + dst.h, win_w, win_h - (dst.y + dst.h)};
    const int y0 = std::max(0, dst.y);
    const int y1 = std::min(win_h, dst.y + dst.h);
    if (y1 > y0) {
        if (dst.x > 0) out[n++] = {0, y0, dst.x, y1 - y0};
        if (dst.x + dst.w < win_w) out[n++] = {dst.x + dst.w, y0, win_w - (dst.x + dst.w), y1 - y0};
    }
    return n;
}

void MarginFill::release() {
    if (blur_target_) SDL_DestroyTexture(blur_target_);
    blur_target_ = nullptr;
    blur_owner_ = nullptr;
}

bool MarginFill::draw(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect& src,
                      const SDL_Rect& dst, int win_w, int win_h) {
    if (!renderer) return false;
    SDL_Rect bars[4];
    const int n = margin_bars(dst, win_w, win_h, bars);
    if (n == 0) return false;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    for (int i = 0; i < n; ++i) SDL_RenderFillRect(renderer, &bars[i]);

    if (settings_.mode == MarginFillMode::Pillar || !texture || src.w <= 0 || src.h <= 0) return false;
    const Uint8 dim = static_cast<Uint8>(std::clamp(settings_.brightness, 0, 100) * 255 / 100);

    if (settings_.mode == MarginFillMode::EdgeRepeat) {
        Uint8 r = 255, g = 255, b = 255;
        SDL_GetTextureColorMod(texture, &r, &g, &b);
        SDL_SetTextureColorMod(texture, dim, dim, dim);
        const SDL_Rect left{src.x, src.y, 1, src.h};
        const SDL_Rect right{src.x + src.w - 1, src.y, 1, src.h};
        const SDL_Rect top{src.x, src.y, src.w, 1};
        const SDL_Rect bottom{src.x, src.y + src.h - 1, src.w, 1};
        bool drawn = false;
        for (int i = 0; i < n; ++i) {
            const SDL_Rect& bar = bars[i];
            const SDL_Rect* strip = nullptr;
            if (bar.w == win_w && bar.y < dst.y) strip = &top;
            else if (bar.w == win_w) strip = &bottom;
            else if (bar.x < dst.x) strip = &left;
            else strip = &right;
            drawn |= SDL_RenderCopy(renderer, texture, strip, &bar) == 0;
        }
        SDL_SetTextureColorMod(texture, r, g, b);
        return drawn;
    }

    // Blur: shrink the picture into a tiny target with linear filtering, then stretch it over
    // the whole window (also linear). The picture is drawn over the middle afterwards.
    if (blur_target_ && blur_owner_ != renderer) release();
    if (!blur_target_) {
        blur_target_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                         kBlurW, kBlurH);
        if (!blur_target_) return false;
        blur_owner_ = renderer;
        SDL_SetTextureScaleMode(blur_target_, SDL_ScaleModeLinear);
    }
    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_ScaleMode old_mode = SDL_ScaleModeNearest;
    SDL_GetTextureScaleMode(texture, &old_mode);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    const bool ok = SDL_SetRenderTarget(renderer, blur_target_) == 0;
    if (ok) {
        SDL_RenderCopy(renderer, texture, &src, nullptr);
        SDL_SetRenderTarget(renderer, prev_target);
        SDL_SetTextureColorMod(blur_target_, dim, dim, dim);
        // One stretch over the whole window (a per-bar copy would squeeze the picture into each bar).
        SDL_RenderCopy(renderer, blur_target_, nullptr, nullptr);
        SDL_SetTextureColorMod(blur_target_, 255, 255, 255);
    }
    SDL_SetTextureScaleMode(texture, old_mode);
    return ok;
}

void register_margin_fill_settings(SettingsRegistry& reg, MarginFill& fill,
                                   const MarginFillSettingsOptions& o) {
    Setting mode;
    mode.id = o.mode_id;
    mode.menu_section = o.menu_section;
    mode.label = o.mode_label;
    mode.tooltip = o.mode_tooltip;
    mode.kind = SettingKind::Choice;
    mode.choices = {"Black (Original)", "Edge repeat", "Blur"};
    mode.min = 0; mode.max = 2; mode.step = 1;
    mode.ini_section = o.ini_section;
    mode.ini_key = o.mode_ini_key;
    mode.get = [&fill] { return static_cast<int>(fill.settings().mode); };
    mode.set = [&fill](int v) { fill.set_mode(static_cast<MarginFillMode>(std::clamp(v, 0, 2))); };
    reg.add(std::move(mode));

    Setting level;
    level.id = o.brightness_id;
    level.menu_section = o.menu_section;
    level.label = o.brightness_label;
    level.tooltip = o.brightness_tooltip;
    level.kind = SettingKind::Int;
    level.min = 0; level.max = 100; level.step = 5;
    level.ini_section = o.ini_section;
    level.ini_key = o.brightness_ini_key;
    level.get = [&fill] { return fill.settings().brightness; };
    level.set = [&fill](int v) { fill.set_brightness(v); };
    level.enabled = [&fill] { return fill.settings().mode != MarginFillMode::Pillar; };
    reg.add(std::move(level));
}

} // namespace gbaimp
