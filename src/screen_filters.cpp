#include "gbaimp/screen_filters.h"
#include <SDL.h>
#include <cmath>
#include <algorithm>
#include <vector>

namespace gbaimp {

namespace {

// The MiSTer-style profiles are expressed as a ratio: exponent = target gamma /
// assumed display gamma. The display is assumed to already be a 2.2 gamma
// device, so a "2.2" target needs no correction (exponent 1.0, identity LUT)
// and "1.6" brightens mid-tones (exponent 1.6/2.2). MisterGamma22 being an
// identity mapping is therefore intentional; do not "fix" it.
constexpr float kMisterDisplayGamma = 2.2f;
constexpr float kMisterTargetGamma16 = 1.6f;
constexpr float kMisterTargetGamma22 = 2.2f;

} // namespace

ScreenFilters& ScreenFilters::instance() {
    static ScreenFilters s_instance;
    return s_instance;
}

ScreenFilters::ScreenFilters() {
    generate_lut(settings_.color_profile);
}

ScreenFilters::~ScreenFilters() {
    if (mask_texture_) {
        SDL_DestroyTexture(mask_texture_);
        mask_texture_ = nullptr;
    }
}

void ScreenFilters::set_color_profile(ColorProfile profile) {
    if (settings_.color_profile != profile) {
        settings_.color_profile = profile;
        generate_lut(profile);
    }
}

void ScreenFilters::set_mask_type(ScreenMaskType type) {
    settings_.mask_type = type;
}

void ScreenFilters::set_mask_intensity(float intensity) {
    settings_.mask_intensity = std::clamp(intensity, 0.05f, 1.0f);
}

void ScreenFilters::generate_lut(ColorProfile profile) {
    active_lut_profile_ = profile;
    for (int i = 0; i < 256; ++i) {
        float norm = i / 255.0f;
        float r = norm;
        float g = norm;
        float b = norm;

        switch (profile) {
        case ColorProfile::Agb001: {
            // AGB-001 reflective: desaturate and adjust gamma for unlit screen
            float gamma_corrected = std::pow(norm, 1.45f);
            r = std::clamp(gamma_corrected * 0.95f + 0.03f, 0.0f, 1.0f);
            g = std::clamp(gamma_corrected * 0.92f + 0.04f, 0.0f, 1.0f);
            b = std::clamp(gamma_corrected * 0.85f + 0.05f, 0.0f, 1.0f);
            break;
        }
        case ColorProfile::Ags001: {
            // AGS-001 frontlit: cooler tone with slightly elevated black floor
            float gamma_corrected = std::pow(norm, 1.70f);
            r = std::clamp(gamma_corrected * 0.88f + 0.06f, 0.0f, 1.0f);
            g = std::clamp(gamma_corrected * 0.92f + 0.06f, 0.0f, 1.0f);
            b = std::clamp(gamma_corrected * 1.00f + 0.08f, 0.0f, 1.0f);
            break;
        }
        case ColorProfile::Ags101: {
            // AGS-101 backlit: rich contrast, gamma 2.2
            float gamma_corrected = std::pow(norm, 2.20f);
            r = std::clamp(gamma_corrected * 1.02f, 0.0f, 1.0f);
            g = std::clamp(gamma_corrected * 1.00f, 0.0f, 1.0f);
            b = std::clamp(gamma_corrected * 0.98f, 0.0f, 1.0f);
            break;
        }
        case ColorProfile::MisterGamma16: {
            // MiSTer FPGA GBA 1.6 gamma mapping
            float gamma_corrected = std::pow(norm, kMisterTargetGamma16 / kMisterDisplayGamma);
            r = g = b = gamma_corrected;
            break;
        }
        case ColorProfile::MisterGamma22: {
            // MiSTer FPGA GBA 2.2 gamma mapping: target == display gamma, so
            // the exponent is exactly 1.0 and the LUT is an identity.
            float gamma_corrected = std::pow(norm, kMisterTargetGamma22 / kMisterDisplayGamma);
            r = g = b = gamma_corrected;
            break;
        }
        case ColorProfile::Raw:
        default:
            r = g = b = norm;
            break;
        }

        lut_r_[i] = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
        lut_g_[i] = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
        lut_b_[i] = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

void ScreenFilters::apply_color_correction(uint8_t* rgb24, int width, int height) {
    if (settings_.color_profile == ColorProfile::Raw || !rgb24 || width <= 0 || height <= 0) return;
    const size_t total_pixels = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < total_pixels; ++i) {
        rgb24[i * 3 + 0] = lut_r_[rgb24[i * 3 + 0]];
        rgb24[i * 3 + 1] = lut_g_[rgb24[i * 3 + 1]];
        rgb24[i * 3 + 2] = lut_b_[rgb24[i * 3 + 2]];
    }
}

void ScreenFilters::ensure_mask_texture(SDL_Renderer* renderer, ScreenMaskType type, int dest_w, int dest_h, int game_w, int game_h) {
    const float intensity = settings_.mask_intensity;
    if (mask_texture_ &&
        cached_mask_type_ == type &&
        cached_mask_w_ == dest_w &&
        cached_mask_h_ == dest_h &&
        cached_game_w_ == game_w &&
        cached_game_h_ == game_h &&
        std::abs(cached_mask_intensity_ - intensity) < 0.01f) {
        return;
    }

    if (mask_texture_) {
        SDL_DestroyTexture(mask_texture_);
        mask_texture_ = nullptr;
    }

    if (type == ScreenMaskType::Off || dest_w <= 0 || dest_h <= 0) return;

    // Pattern dimensions: generate a repeating texture or 1:1 viewport buffer
    // For performance and sharpness, create a texture matching dest_w x dest_h
    // or a compact repeating pattern with exact physical alignment.
    int tex_w = dest_w;
    int tex_h = dest_h;

    std::vector<uint32_t> pixels(tex_w * tex_h, 0xFFFFFFFF);

    const float cell_w = (game_w > 0) ? (static_cast<float>(dest_w) / static_cast<float>(game_w)) : 4.0f;
    const float cell_h = (game_h > 0) ? (static_cast<float>(dest_h) / static_cast<float>(game_h)) : 4.0f;

    for (int y = 0; y < tex_h; ++y) {
        for (int x = 0; x < tex_w; ++x) {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;

            switch (type) {
            case ScreenMaskType::CrtScanlines: {
                // Alternating horizontal scanline dimming
                if (y % 2 == 1) {
                    float dim = 1.0f - intensity * 0.45f;
                    r = g = b = dim;
                }
                break;
            }

            case ScreenMaskType::CrtTrinitron: {
                // Sony Trinitron aperture grille: vertical phosphor stripes + fine scanline
                const float scan_dim = (y % 2 == 1) ? (1.0f - intensity * 0.35f) : 1.0f;
                const float phos_dim = 1.0f - intensity * 0.18f;
                const int phos_idx = x % 3;
                if (phos_idx == 0) {
                    r = scan_dim;
                    g = phos_dim * scan_dim;
                    b = phos_dim * scan_dim;
                } else if (phos_idx == 1) {
                    r = phos_dim * scan_dim;
                    g = scan_dim;
                    b = phos_dim * scan_dim;
                } else {
                    r = phos_dim * scan_dim;
                    g = phos_dim * scan_dim;
                    b = scan_dim;
                }
                break;
            }

            case ScreenMaskType::LcdGrid: {
                // GBA SP AGS-101 LCD Matrix Grid: dark grid border between GBA pixels
                float fx = std::fmod(static_cast<float>(x), cell_w);
                float fy = std::fmod(static_cast<float>(y), cell_h);
                if (fx < 1.0f || fy < 1.0f) {
                    float dim = 1.0f - intensity * 0.55f;
                    r = g = b = dim;
                }
                break;
            }

            case ScreenMaskType::SubpixelRgb: {
                // Fine 1-pixel vertical RGB phosphor stripes
                const float phos_dim = 1.0f - intensity * 0.22f;
                const int phos_idx = x % 3;
                if (phos_idx == 0) {
                    r = 1.0f; g = phos_dim; b = phos_dim;
                } else if (phos_idx == 1) {
                    r = phos_dim; g = 1.0f; b = phos_dim;
                } else {
                    r = phos_dim; g = phos_dim; b = 1.0f;
                }
                break;
            }

            case ScreenMaskType::SubpixelBgr: {
                // Fine 1-pixel vertical BGR phosphor stripes
                const float phos_dim = 1.0f - intensity * 0.22f;
                const int phos_idx = x % 3;
                if (phos_idx == 0) {
                    r = phos_dim; g = phos_dim; b = 1.0f;
                } else if (phos_idx == 1) {
                    r = phos_dim; g = 1.0f; b = phos_dim;
                } else {
                    r = 1.0f; g = phos_dim; b = phos_dim;
                }
                break;
            }

            case ScreenMaskType::LcdDiffusion: {
                // Frontlit light-guide micro diffusion
                if ((x + y) % 2 == 1) {
                    float dim = 1.0f - intensity * 0.20f;
                    r = g = b = dim;
                }
                break;
            }

            default:
                break;
            }

            uint8_t ur = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
            uint8_t ug = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
            uint8_t ub = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));

            // RGBA8888
            pixels[y * tex_w + x] = (255u << 24) | (static_cast<uint32_t>(ub) << 16) | (static_cast<uint32_t>(ug) << 8) | ur;
        }
    }

    mask_texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tex_w, tex_h);
    if (mask_texture_) {
        SDL_UpdateTexture(mask_texture_, nullptr, pixels.data(), tex_w * sizeof(uint32_t));
        SDL_SetTextureBlendMode(mask_texture_, SDL_BLENDMODE_MOD);
    }

    cached_mask_type_ = type;
    cached_mask_w_ = dest_w;
    cached_mask_h_ = dest_h;
    cached_game_w_ = game_w;
    cached_game_h_ = game_h;
    cached_mask_intensity_ = intensity;
}

void ScreenFilters::render_mask(SDL_Renderer* renderer, const SDL_Rect* viewport, int game_w, int game_h) {
    if (settings_.mask_type == ScreenMaskType::Off || !renderer || !viewport) return;
    if (viewport->w <= 0 || viewport->h <= 0) return;

    ensure_mask_texture(renderer, settings_.mask_type, viewport->w, viewport->h, game_w, game_h);
    if (!mask_texture_) return;

    SDL_RenderCopy(renderer, mask_texture_, nullptr, viewport);
}

} // namespace gbaimp
