#include "gbaimp/text_raster.h"

#include <algorithm>
#include <cmath>

namespace gbaimp {
namespace text {

namespace {

const uint8_t* boost_lut() {
    static uint8_t lut[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i)
            lut[i] = static_cast<uint8_t>(std::lround(255.0 * std::pow(i / 255.0, 0.85)));
        init = true;
    }
    return lut;
}

inline float sample(const GlyphBitmap& b, int x, int y) {   // x,y in bitmap pixels
    if (x < 0 || y < 0 || x >= b.w || y >= b.h) return 0.0f;
    return static_cast<float>(b.a[static_cast<size_t>(y) * b.w + x]);
}

} // namespace

void make_shadow_bitmap(const GlyphBitmap& body, float dx, float dy, GlyphBitmap& out) {
    out = GlyphBitmap();
    if (body.empty()) return;
    dx = std::max(0.0f, dx);
    dy = std::max(0.0f, dy);
    const int ex = static_cast<int>(std::ceil(dx)), ey = static_cast<int>(std::ceil(dy));
    out.x0 = body.x0;
    out.y0 = body.y0;
    out.w = body.w + ex;
    out.h = body.h + ey;
    out.a.assign(static_cast<size_t>(out.w) * out.h, 0);
    const float shifts[3][2] = {{dx, 0.0f}, {0.0f, dy}, {dx, dy}};
    for (int s = 0; s < 3; ++s) {
        const float ox = shifts[s][0], oy = shifts[s][1];
        const int ix = static_cast<int>(std::floor(ox)), iy = static_cast<int>(std::floor(oy));
        const float fx = ox - ix, fy = oy - iy;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                const int sx = x - ix, sy = y - iy;
                const float v = (1 - fx) * (1 - fy) * sample(body, sx, sy) + fx * (1 - fy) * sample(body, sx - 1, sy) +
                                (1 - fx) * fy * sample(body, sx, sy - 1) + fx * fy * sample(body, sx - 1, sy - 1);
                uint8_t& d = out.a[static_cast<size_t>(y) * out.w + x];
                const uint8_t q = static_cast<uint8_t>(std::min(255.0f, v + 0.5f));
                if (q > d) d = q;
            }
    }
}

void make_outline_bitmap(const GlyphBitmap& body, float radius, GlyphBitmap& out) {
    out = GlyphBitmap();
    if (body.empty()) return;
    radius = std::max(0.0f, radius);
    const int pad = static_cast<int>(std::ceil(radius)) + 1;
    out.x0 = body.x0 - pad;
    out.y0 = body.y0 - pad;
    out.w = body.w + 2 * pad;
    out.h = body.h + 2 * pad;
    out.a.assign(static_cast<size_t>(out.w) * out.h, 0);
    struct Off { int i, j; float w; };
    std::vector<Off> offs;
    const float R = radius + 0.5f;
    const int ri = static_cast<int>(std::ceil(R));
    for (int j = -ri; j <= ri; ++j)
        for (int i = -ri; i <= ri; ++i) {
            const float d = std::sqrt(static_cast<float>(i * i + j * j));
            const float w = std::min(1.0f, R - d);
            if (w > 0.0f) offs.push_back({i, j, w});
        }
    // rows with ink (skip empty source rows quickly)
    std::vector<uint8_t> row_ink(body.h, 0);
    for (int y = 0; y < body.h; ++y)
        for (int x = 0; x < body.w; ++x)
            if (body.a[static_cast<size_t>(y) * body.w + x]) { row_ink[y] = 1; break; }
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const int bx = x - pad, by = y - pad;   // position in body coordinates
            float best = 0.0f;
            for (const Off& o : offs) {
                const int px = bx + o.i, py = by + o.j;
                if (px < 0 || py < 0 || px >= body.w || py >= body.h || !row_ink[py]) continue;
                const float v = body.a[static_cast<size_t>(py) * body.w + px] * o.w;
                if (v > best) best = v;
            }
            out.a[static_cast<size_t>(y) * out.w + x] = static_cast<uint8_t>(std::min(255.0f, best + 0.5f));
        }
}

bool rasterize_glyph(const FontFace& face, int gi, float em_px, int phase, GlyphVariant v, float param_px, GlyphBitmap& out) {
    out = GlyphBitmap();
    const FaceScale sc = face.scale_for(em_px);
    const float shift = static_cast<float>(phase) / kSubpixelPhases;
    GlyphBitmap body;
    if (!face.bitmap(gi, sc, shift, body.x0, body.y0, body.w, body.h, body.a)) return false;
    const uint8_t* lut = boost_lut();
    for (uint8_t& c : body.a) c = lut[c];
    switch (v) {
        case GlyphVariant::Body: out = std::move(body); break;
        case GlyphVariant::Shadow: make_shadow_bitmap(body, param_px, param_px, out); break;
        case GlyphVariant::Outline: make_outline_bitmap(body, param_px, out); break;
    }
    return !out.empty();
}

} // namespace text
} // namespace gbaimp
