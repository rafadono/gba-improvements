#include "gbaimp/pixel_art_upscaler.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gbaimp {

namespace {

inline uint32_t color_dist(uint32_t c1, uint32_t c2) {
    if (c1 == c2) return 0;
    int r1 = (c1 & 0xFF);
    int g1 = ((c1 >> 8) & 0xFF);
    int b1 = ((c1 >> 16) & 0xFF);

    int r2 = (c2 & 0xFF);
    int g2 = ((c2 >> 8) & 0xFF);
    int b2 = ((c2 >> 16) & 0xFF);

    int dr = r1 - r2;
    int dg = g1 - g2;
    int db = b1 - b2;
    return dr * dr * 2 + dg * dg * 4 + db * db * 3;
}

inline uint32_t blend_colors(uint32_t c1, uint32_t c2, float alpha) {
    int r1 = (c1 & 0xFF);
    int g1 = ((c1 >> 8) & 0xFF);
    int b1 = ((c1 >> 16) & 0xFF);
    int a1 = ((c1 >> 24) & 0xFF);

    int r2 = (c2 & 0xFF);
    int g2 = ((c2 >> 8) & 0xFF);
    int b2 = ((c2 >> 16) & 0xFF);
    int a2 = ((c2 >> 24) & 0xFF);

    int r = static_cast<int>(r1 * (1.0f - alpha) + r2 * alpha);
    int g = static_cast<int>(g1 * (1.0f - alpha) + g2 * alpha);
    int b = static_cast<int>(b1 * (1.0f - alpha) + b2 * alpha);
    int a = static_cast<int>(a1 * (1.0f - alpha) + a2 * alpha);

    return (static_cast<uint32_t>(r) & 0xFF) |
           ((static_cast<uint32_t>(g) & 0xFF) << 8) |
           ((static_cast<uint32_t>(b) & 0xFF) << 16) |
           ((static_cast<uint32_t>(a) & 0xFF) << 24);
}

inline uint32_t sample_clamped(const uint32_t* src, int x, int y, int w, int h) {
    int cx = std::clamp(x, 0, w - 1);
    int cy = std::clamp(y, 0, h - 1);
    return src[cy * w + cx];
}

} // namespace

PixelArtUpscaler& PixelArtUpscaler::instance() {
    static PixelArtUpscaler s_instance;
    return s_instance;
}

void PixelArtUpscaler::set_scale(UpscaleScale scale) {
    current_scale_ = scale;
}

void PixelArtUpscaler::scale_2x(const uint32_t* src, uint32_t* dst, int w, int h) {
    int out_w = w * 2;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t p = sample_clamped(src, x, y, w, h);
            uint32_t a = sample_clamped(src, x - 1, y - 1, w, h);
            uint32_t b = sample_clamped(src, x, y - 1, w, h);
            uint32_t c = sample_clamped(src, x + 1, y - 1, w, h);
            uint32_t d = sample_clamped(src, x - 1, y, w, h);
            uint32_t e = sample_clamped(src, x + 1, y, w, h);
            uint32_t f = sample_clamped(src, x - 1, y + 1, w, h);
            uint32_t g = sample_clamped(src, x, y + 1, w, h);
            uint32_t h_px = sample_clamped(src, x + 1, y + 1, w, h);

            uint32_t e0 = p;
            uint32_t e1 = p;
            uint32_t e2 = p;
            uint32_t e3 = p;

            // Top-left
            if (b == d && b != e && d != g) {
                e0 = (color_dist(a, p) < color_dist(b, c)) ? blend_colors(p, b, 0.5f) : b;
            }
            // Top-right
            if (b == e && b != d && e != h_px) {
                e1 = (color_dist(c, p) < color_dist(b, a)) ? blend_colors(p, b, 0.5f) : b;
            }
            // Bottom-left
            if (d == g && d != b && g != h_px) {
                e2 = (color_dist(f, p) < color_dist(d, a)) ? blend_colors(p, d, 0.5f) : d;
            }
            // Bottom-right
            if (e == g && e != b && g != d) {
                e3 = (color_dist(h_px, p) < color_dist(e, c)) ? blend_colors(p, e, 0.5f) : e;
            }

            int dx = x * 2;
            int dy = y * 2;
            dst[dy * out_w + dx] = e0;
            dst[dy * out_w + dx + 1] = e1;
            dst[(dy + 1) * out_w + dx] = e2;
            dst[(dy + 1) * out_w + dx + 1] = e3;
        }
    }
}

void PixelArtUpscaler::scale_3x(const uint32_t* src, uint32_t* dst, int w, int h) {
    int out_w = w * 3;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t p = sample_clamped(src, x, y, w, h);
            uint32_t b = sample_clamped(src, x, y - 1, w, h);
            uint32_t d = sample_clamped(src, x - 1, y, w, h);
            uint32_t e = sample_clamped(src, x + 1, y, w, h);
            uint32_t g = sample_clamped(src, x, y + 1, w, h);

            int dx = x * 3;
            int dy = y * 3;

            for (int sy = 0; sy < 3; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    dst[(dy + sy) * out_w + (dx + sx)] = p;
                }
            }

            if (d == b && d != g && b != e) dst[dy * out_w + dx] = d;
            if (b == e && b != d && e != g) dst[dy * out_w + dx + 2] = e;
            if (d == g && d != b && g != e) dst[(dy + 2) * out_w + dx] = d;
            if (g == e && g != d && e != b) dst[(dy + 2) * out_w + dx + 2] = e;
        }
    }
}

void PixelArtUpscaler::scale_4x(const uint32_t* src, uint32_t* dst, int w, int h) {
    std::vector<uint32_t> intermediate(w * 2 * h * 2);
    scale_2x(src, intermediate.data(), w, h);
    scale_2x(intermediate.data(), dst, w * 2, h * 2);
}

void PixelArtUpscaler::scale_5x(const uint32_t* src, uint32_t* dst, int w, int h) {
    int out_w = w * 5;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t p = sample_clamped(src, x, y, w, h);
            uint32_t b = sample_clamped(src, x, y - 1, w, h);
            uint32_t d = sample_clamped(src, x - 1, y, w, h);
            uint32_t e = sample_clamped(src, x + 1, y, w, h);
            uint32_t g = sample_clamped(src, x, y + 1, w, h);

            int dx = x * 5;
            int dy = y * 5;

            for (int sy = 0; sy < 5; ++sy) {
                for (int sx = 0; sx < 5; ++sx) {
                    dst[(dy + sy) * out_w + (dx + sx)] = p;
                }
            }

            if (d == b && d != g && b != e) {
                dst[dy * out_w + dx] = d;
                dst[dy * out_w + dx + 1] = blend_colors(p, d, 0.5f);
                dst[(dy + 1) * out_w + dx] = blend_colors(p, d, 0.5f);
            }
            if (b == e && b != d && e != g) {
                dst[dy * out_w + dx + 4] = e;
                dst[dy * out_w + dx + 3] = blend_colors(p, e, 0.5f);
                dst[(dy + 1) * out_w + dx + 4] = blend_colors(p, e, 0.5f);
            }
            if (d == g && d != b && g != e) {
                dst[(dy + 4) * out_w + dx] = d;
                dst[(dy + 4) * out_w + dx + 1] = blend_colors(p, d, 0.5f);
                dst[(dy + 3) * out_w + dx] = blend_colors(p, d, 0.5f);
            }
            if (g == e && g != d && e != b) {
                dst[(dy + 4) * out_w + dx + 4] = e;
                dst[(dy + 4) * out_w + dx + 3] = blend_colors(p, e, 0.5f);
                dst[(dy + 3) * out_w + dx + 4] = blend_colors(p, e, 0.5f);
            }
        }
    }
}

void PixelArtUpscaler::process(const uint32_t* src, int src_width, int src_height,
                           std::vector<uint32_t>& dst_buffer, int& out_width, int& out_height) {
    // Invalid input (null source, non-positive size, or an output that would not
    // fit the int pixel indexing used by the scalers): produce an empty result.
    const int64_t factor64 = static_cast<int64_t>(current_scale_);
    if (!src || src_width <= 0 || src_height <= 0 ||
        static_cast<int64_t>(src_width) * src_height * factor64 * factor64 > INT32_MAX) {
        dst_buffer.clear();
        out_width = 0;
        out_height = 0;
        return;
    }

    if (current_scale_ == UpscaleScale::None) {
        out_width = src_width;
        out_height = src_height;
        dst_buffer.assign(src, src + static_cast<size_t>(src_width) * src_height);
        return;
    }

    int factor = static_cast<int>(current_scale_);
    out_width = src_width * factor;
    out_height = src_height * factor;
    dst_buffer.resize(out_width * out_height);

    switch (current_scale_) {
        case UpscaleScale::X2:
            scale_2x(src, dst_buffer.data(), src_width, src_height);
            break;
        case UpscaleScale::X3:
            scale_3x(src, dst_buffer.data(), src_width, src_height);
            break;
        case UpscaleScale::X4:
            scale_4x(src, dst_buffer.data(), src_width, src_height);
            break;
        case UpscaleScale::X5:
            scale_5x(src, dst_buffer.data(), src_width, src_height);
            break;
        default:
            dst_buffer.assign(src, src + src_width * src_height);
            break;
    }
}

} // namespace gbaimp
