#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;

namespace gbaimp {

enum class ColorProfile : int {
    Raw = 0,
    Agb001 = 1,
    Ags001 = 2,
    Ags101 = 3,
    MisterGamma16 = 4,
    MisterGamma22 = 5
};

enum class ScreenMaskType : int {
    Off = 0,
    LcdGrid = 1,
    SubpixelRgb = 2,
    SubpixelBgr = 3,
    LcdDiffusion = 4,
    CrtScanlines = 5,
    CrtTrinitron = 6
};

struct ScreenFilterSettings {
    ColorProfile color_profile = ColorProfile::Raw;
    ScreenMaskType mask_type = ScreenMaskType::Off;
    float mask_intensity = 0.40f;
};

class ScreenFilters {
public:
    static ScreenFilters& instance();

    const ScreenFilterSettings& settings() const { return settings_; }
    ScreenFilterSettings& settings() { return settings_; }

    void set_color_profile(ColorProfile profile);
    void set_mask_type(ScreenMaskType type);
    void set_mask_intensity(float intensity);

    void apply_color_correction(uint8_t* rgb24, int width, int height);
    void render_mask(SDL_Renderer* renderer, const SDL_Rect* viewport, int game_w, int game_h);

private:
    ScreenFilters();
    ~ScreenFilters();

    void generate_lut(ColorProfile profile);
    void ensure_mask_texture(SDL_Renderer* renderer, ScreenMaskType type, int dest_w, int dest_h, int game_w, int game_h);

    ScreenFilterSettings settings_;
    ColorProfile active_lut_profile_ = ColorProfile::Raw;
    uint8_t lut_r_[256];
    uint8_t lut_g_[256];
    uint8_t lut_b_[256];

    SDL_Texture* mask_texture_ = nullptr;
    ScreenMaskType cached_mask_type_ = ScreenMaskType::Off;
    int cached_mask_w_ = 0;
    int cached_mask_h_ = 0;
    int cached_game_w_ = 0;
    int cached_game_h_ = 0;
    float cached_mask_intensity_ = -1.0f;
};

} // namespace gbaimp
