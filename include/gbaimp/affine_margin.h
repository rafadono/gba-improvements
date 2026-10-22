#pragma once
// Real artwork for the vertical margins of an expanded view (game agnostic).
//
// A runtime that renders a taller-than-160-line view can only fill the extra rows with content a
// host authored: it never extrapolates a rotation/scaling (affine) background above or below the
// native band, and a regular background ring would only show wrapped, unrelated tiles there. This
// module produces those rows for the affine backgrounds (BG2 / BG3 in video modes 1 and 2), which
// is where a game that draws a whole scene on a large affine map keeps its backdrop.
//
// What it does, per frame:
//   1. Latches the registers of the affine layers (control, matrix, reference point) and the
//      palette / tile memory pointers of the PPU (see PpuMemory).
//   2. Finds the bounding box of the real artwork of each layer in its own map (rows and columns
//      that contain at least one opaque pixel). The result is cached until the map or tile data
//      changes, so a steady scene costs one hash of the map memory per frame.
//   3. Samples every margin row with the same fixed point arithmetic the hardware (and the
//      runtime's native rows) use, then clamps the sampled map position to the artwork box. A
//      row above the artwork repeats its first row and a row below it repeats its last row, so
//      the picture never shows the wrapped other side of the map or an empty strip.
//   4. Composites the layers by hardware priority and returns the row in the runtime's margin
//      pixel format (WsMarginPixel), ready to hand out from a margin pixel provider.
//
// available_rows() / available_cols() report how many rows / columns of real artwork lie beyond the
// native band for the current camera, so a host can log or cap the extension (the camera moves, so the
// numbers change over a scene).
//
// Not handled: per-scanline register changes (HBlank DMA), mosaic, window masks and colour effects.
// They affect the native rows only; margin rows use the registers as they are at frame start.
#include "gbaimp/widescreen.h"

#include <cstdint>
#include <vector>

namespace gbaimp {

// PPU memory visible to the renderer for one frame. Pointers must stay valid until the next
// begin_frame().
struct PpuMemory {
    uint16_t dispcnt = 0;
    const uint8_t* io = nullptr;     // 1 KB register page
    const uint8_t* vram = nullptr;   // 96 KB
    const uint8_t* pal = nullptr;    // 1 KB, RGB555
};

// Rectangle of opaque artwork inside a layer's map, in map pixels. Empty when x1 <= x0 or y1 <= y0.
struct ArtBounds {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool valid() const { return x1 > x0 && y1 > y0; }
};

// Scans a 256 colour affine map (one byte per tile, 64 byte tiles; colour index 0 is transparent) and
// returns the pixel box that contains every opaque pixel. `size_px` is 128, 256, 512 or 1024.
ArtBounds scan_affine_art_bounds(const uint8_t* vram, size_t vram_size, uint32_t char_base,
                                 uint32_t screen_base, int size_px);

struct AffineMarginOptions {
    uint32_t layer_mask = 0x0C;      // bit n = BG n; BG2 and BG3 by default
    bool clamp_to_art = true;        // repeat the outermost artwork row / column instead of wrapping
    uint8_t order = 127;             // WsMarginPixel::order given to every pixel (lower wins ties)
};

class AffineMarginRenderer {
public:
    // Latches the registers and the artwork bounds of the current frame. Layers that are disabled, not
    // affine in the current video mode or outside layer_mask are ignored.
    void begin_frame(const PpuMemory& mem);
    void set_options(const AffineMarginOptions& o) { opts_ = o; }
    const AffineMarginOptions& options() const { return opts_; }

    // True when begin_frame() found at least one usable layer.
    bool active() const { return layer_count_ > 0; }
    int layer_count() const { return layer_count_; }
    // Artwork bounds of the i-th active layer in priority order (0 = frontmost), or an empty box.
    ArtBounds art_bounds(int i) const;

    // Renders margin row `y` (signed native-relative row) for hardware columns [left, left + width)
    // into `out` (at least `width` entries). Pixels with nothing behind them stay transparent.
    // Returns false when no layer is active.
    bool render_row(int y, int left, int width, WsMarginPixel* out) const;

    // Rows / columns of real artwork beyond the native 240x160 band for the current camera (0 when
    // none, large when the layer has no artwork limit in that direction). Uses the frontmost
    // wrapping layer; only meaningful for unrotated layers, a rotated one reports 0.
    void available_rows(int* above, int* below) const;
    void available_cols(int* left, int* right) const;

    // Provider adapter: call from the runtime's margin pixel provider. A new pass over the margin
    // rows is detected when `y` does not increase, and re-latches the registers with `source` (a
    // callback returning the live PPU memory) at that moment. Returns the row (valid until the next
    // call) and sets *left / *width, or null for a native row / no artwork.
    using MemorySource = PpuMemory (*)(void* user);
    void set_memory_source(MemorySource src, void* user) { source_ = src; source_user_ = user; }
    const WsMarginPixel* provide(int y, int extra_left, int extra_right, int* left, int* width);

private:
    struct Layer {
        int bg = 2;
        int priority = 0;
        int size_px = 128;
        bool wrap = false;
        uint32_t char_base = 0, screen_base = 0;
        int32_t pa = 0, pb = 0, pc = 0, pd = 0, refx = 0, refy = 0;
        int off_x = 0, off_y = 0;   // multiple of the map size: the map copy the native band is in
        ArtBounds art;
    };
    struct CacheEntry {
        bool valid = false;
        uint64_t key = 0;
        uint64_t hash = 0;
        ArtBounds art;
    };

    AffineMarginOptions opts_;
    Layer layers_[2];
    int layer_count_ = 0;
    PpuMemory mem_;
    CacheEntry cache_[2];
    MemorySource source_ = nullptr;
    void* source_user_ = nullptr;
    int last_y_ = 0;
    bool have_last_ = false;
    std::vector<WsMarginPixel> row_;
};

} // namespace gbaimp
