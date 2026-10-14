#pragma once
// CPU rasterisation of single glyphs of the text engine: body coverage, drop shadow and outline bitmaps.
// Coverage only (0..255); colour is applied at draw time. Deterministic, no SDL.
#include "gbaimp/text_fonts.h"

#include <cstdint>
#include <vector>

namespace gbaimp {
namespace text {

enum class GlyphVariant : uint8_t { Body = 0, Shadow = 1, Outline = 2 };

struct GlyphBitmap {
    int x0 = 0, y0 = 0;          // offset of the top-left pixel from the pen origin on the baseline (y down)
    int w = 0, h = 0;
    std::vector<uint8_t> a;      // w*h coverage
    bool empty() const { return w <= 0 || h <= 0; }
};

constexpr int kSubpixelPhases = 4;   // horizontal subpixel buckets (quarter pixels)

// Body: light-hinted coverage at output size `em_px` with the pen at x = phase / kSubpixelPhases, the
// baseline on an integer row, stem darkening via coverage^0.85.
// Shadow: union of the body shifted E, S and SE by `param_px` output px (the game's own rule, fractional
// shifts are resampled bilinearly). Outline: body dilated by a disc of radius `param_px`.
bool rasterize_glyph(const FontFace& face, int gi, float em_px, int phase, GlyphVariant v, float param_px, GlyphBitmap& out);

// Exposed for tests.
void make_shadow_bitmap(const GlyphBitmap& body, float dx, float dy, GlyphBitmap& out);
void make_outline_bitmap(const GlyphBitmap& body, float radius, GlyphBitmap& out);

} // namespace text
} // namespace gbaimp
