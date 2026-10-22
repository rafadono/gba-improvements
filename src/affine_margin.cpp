#include "gbaimp/affine_margin.h"

#include <algorithm>
#include <cstring>

namespace gbaimp {

namespace {

constexpr size_t kVramSize = 96u * 1024u;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int32_t read_s16(const uint8_t* io, uint32_t off) { return static_cast<int16_t>(rd16(io + off)); }

// 28 bit signed reference point register (24.8 fixed point).
int32_t read_s28(const uint8_t* io, uint32_t off) {
    uint32_t v = static_cast<uint32_t>(io[off]) | (static_cast<uint32_t>(io[off + 1]) << 8) |
                 (static_cast<uint32_t>(io[off + 2]) << 16) | (static_cast<uint32_t>(io[off + 3]) << 24);
    v &= 0x0FFFFFFFu;
    if (v & 0x08000000u) v |= 0xF0000000u;
    return static_cast<int32_t>(v);
}

uint64_t hash_bytes(const uint8_t* p, size_t n, uint64_t h) {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

} // namespace

ArtBounds scan_affine_art_bounds(const uint8_t* vram, size_t vram_size, uint32_t char_base,
                                 uint32_t screen_base, int size_px) {
    ArtBounds none;
    if (!vram || size_px < 8 || (size_px & 7) != 0) return none;
    const int tiles = size_px / 8;

    // Which rows / columns of each tile hold an opaque pixel.
    uint8_t row_mask[256] = {};
    uint8_t col_mask[256] = {};
    for (uint32_t idx = 0; idx < 256; ++idx) {
        const size_t base = static_cast<size_t>(char_base) + static_cast<size_t>(idx) * 64u;
        if (base + 64u > vram_size) continue;
        const uint8_t* t = vram + base;
        uint8_t rm = 0, cm = 0;
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; ++c) {
                if (t[r * 8 + c] != 0) { rm |= static_cast<uint8_t>(1u << r); cm |= static_cast<uint8_t>(1u << c); }
            }
        }
        row_mask[idx] = rm;
        col_mask[idx] = cm;
    }

    std::vector<uint8_t> rows(static_cast<size_t>(size_px), 0), cols(static_cast<size_t>(size_px), 0);
    for (int ty = 0; ty < tiles; ++ty) {
        for (int tx = 0; tx < tiles; ++tx) {
            const size_t off = static_cast<size_t>(screen_base) + static_cast<size_t>(ty) * tiles + tx;
            if (off >= vram_size) continue;
            const uint8_t idx = vram[off];
            const uint8_t rm = row_mask[idx];
            const uint8_t cm = col_mask[idx];
            if (!rm) continue;
            for (int r = 0; r < 8; ++r) if (rm & (1u << r)) rows[static_cast<size_t>(ty * 8 + r)] = 1;
            for (int c = 0; c < 8; ++c) if (cm & (1u << c)) cols[static_cast<size_t>(tx * 8 + c)] = 1;
        }
    }
    auto span = [](const std::vector<uint8_t>& v, int* lo, int* hi) {
        int a = -1, b = -1;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i]) { if (a < 0) a = static_cast<int>(i); b = static_cast<int>(i); }
        }
        *lo = a < 0 ? 0 : a;
        *hi = a < 0 ? 0 : b + 1;
    };
    ArtBounds out;
    span(cols, &out.x0, &out.x1);
    span(rows, &out.y0, &out.y1);
    if (!out.valid()) return none;
    return out;
}

void AffineMarginRenderer::begin_frame(const PpuMemory& mem) {
    mem_ = mem;
    layer_count_ = 0;
    have_last_ = false;
    if (!mem.io || !mem.vram || !mem.pal) return;
    const unsigned mode = mem.dispcnt & 7u;
    if (mode != 1 && mode != 2) return;

    Layer found[2];
    int n = 0;
    for (int bg = 2; bg <= 3; ++bg) {
        if (!((opts_.layer_mask >> bg) & 1u)) continue;
        if (bg == 3 && mode != 2) continue;
        if (!(mem.dispcnt & (0x0100u << bg))) continue;
        Layer l;
        l.bg = bg;
        const uint16_t bgcnt = rd16(mem.io + 0x08 + 2 * bg);
        l.priority = bgcnt & 3;
        l.char_base = ((bgcnt >> 2) & 3u) * 0x4000u;
        l.screen_base = ((bgcnt >> 8) & 0x1Fu) * 0x800u;
        l.wrap = (bgcnt & 0x2000u) != 0;
        l.size_px = 128 << ((bgcnt >> 14) & 3u);
        const uint32_t p = bg == 2 ? 0x20u : 0x30u;
        l.pa = read_s16(mem.io, p + 0);
        l.pb = read_s16(mem.io, p + 2);
        l.pc = read_s16(mem.io, p + 4);
        l.pd = read_s16(mem.io, p + 6);
        l.refx = read_s28(mem.io, p + 8);
        l.refy = read_s28(mem.io, p + 12);
        {   // the map copy (multiple of the map size) that holds the centre of the native band
            const int mask = l.size_px - 1;
            const int cx = (l.refx + 80 * l.pb + 120 * l.pa) >> 8;
            const int cy = (l.refy + 80 * l.pd + 120 * l.pc) >> 8;
            l.off_x = cx & ~mask;
            l.off_y = cy & ~mask;
        }

        // Artwork bounds, cached on (layout, content hash).
        CacheEntry& c = cache_[bg - 2];
        const uint64_t key = (static_cast<uint64_t>(l.screen_base) << 32) | (static_cast<uint64_t>(l.char_base) << 8) |
                             static_cast<uint64_t>((bgcnt >> 14) & 3u);
        const size_t tiles = static_cast<size_t>(l.size_px / 8);
        const size_t map_bytes = std::min(tiles * tiles, kVramSize - std::min<size_t>(l.screen_base, kVramSize));
        const size_t char_bytes = std::min<size_t>(256u * 64u, kVramSize - std::min<size_t>(l.char_base, kVramSize));
        uint64_t h = 0xCBF29CE484222325ull;
        h = hash_bytes(mem.vram + l.screen_base, map_bytes, h);
        h = hash_bytes(mem.vram + l.char_base, char_bytes, h);
        if (!c.valid || c.key != key || c.hash != h) {
            c.valid = true;
            c.key = key;
            c.hash = h;
            c.art = scan_affine_art_bounds(mem.vram, kVramSize, l.char_base, l.screen_base, l.size_px);
        }
        l.art = c.art;
        if (opts_.clamp_to_art && !l.art.valid()) continue;   // an empty map contributes nothing
        found[n++] = l;
    }
    if (n == 2 && (found[1].priority < found[0].priority)) std::swap(found[0], found[1]);
    for (int i = 0; i < n; ++i) layers_[i] = found[i];
    layer_count_ = n;
}

ArtBounds AffineMarginRenderer::art_bounds(int i) const {
    if (i < 0 || i >= layer_count_) return ArtBounds{};
    return layers_[i].art;
}

bool AffineMarginRenderer::render_row(int y, int left, int width, WsMarginPixel* out) const {
    if (!out || width <= 0) return false;
    for (int i = 0; i < width; ++i) out[i] = WsMarginPixel{};
    if (layer_count_ == 0) return false;
    for (int li = layer_count_ - 1; li >= 0; --li) {   // back to front: the frontmost layer writes last
        const Layer& l = layers_[li];
        const int mask = l.size_px - 1;
        const int tiles = l.size_px / 8;
        for (int i = 0; i < width; ++i) {
            const int x = left + i;
            const int32_t sx = l.refx + y * l.pb + x * l.pa;
            const int32_t sy = l.refy + y * l.pd + x * l.pc;
            int tx = sx >> 8;
            int ty = sy >> 8;
            if (l.wrap) {
                // Position relative to the map copy the native band sits in: a margin row past the
                // seam keeps clamping against that copy's artwork instead of landing on the far side.
                tx -= l.off_x;
                ty -= l.off_y;
                if (!(opts_.clamp_to_art && l.art.valid())) {
                    tx &= mask;
                    ty &= mask;
                }
            } else if (tx < 0 || tx >= l.size_px || ty < 0 || ty >= l.size_px) {
                continue;
            }
            if (opts_.clamp_to_art && l.art.valid()) {
                tx = std::clamp(tx, l.art.x0, l.art.x1 - 1);
                ty = std::clamp(ty, l.art.y0, l.art.y1 - 1);
            }
            const size_t map_off = static_cast<size_t>(l.screen_base) + static_cast<size_t>(ty >> 3) * tiles + (tx >> 3);
            if (map_off >= kVramSize) continue;
            const uint8_t tile = mem_.vram[map_off];
            const size_t addr = static_cast<size_t>(l.char_base) + static_cast<size_t>(tile) * 64u +
                                static_cast<size_t>(ty & 7) * 8u + static_cast<size_t>(tx & 7);
            if (addr >= kVramSize) continue;
            const uint8_t idx = mem_.vram[addr];
            if (idx == 0) continue;
            WsMarginPixel& px = out[i];
            px.color = static_cast<uint16_t>(rd16(mem_.pal + idx * 2u) & 0x7FFFu);
            px.priority = static_cast<uint8_t>(l.priority);
            px.order = opts_.order;
            px.semi_transparent = false;
        }
    }
    return true;
}

void AffineMarginRenderer::available_rows(int* above, int* below) const {
    int a = 0, b = 0;
    if (layer_count_ > 0) {
        const Layer& l = layers_[0];
        if (l.art.valid() && l.pb == 0 && l.pc == 0 && l.pd > 0) {
            const int top = (l.refy >> 8) & (l.size_px - 1);
            const int bottom = top + static_cast<int>((160ll * l.pd) >> 8);
            a = static_cast<int>((static_cast<int64_t>(std::max(0, top - l.art.y0)) << 8) / l.pd);
            b = static_cast<int>((static_cast<int64_t>(std::max(0, l.art.y1 - bottom)) << 8) / l.pd);
        }
    }
    if (above) *above = a;
    if (below) *below = b;
}

void AffineMarginRenderer::available_cols(int* left, int* right) const {
    int a = 0, b = 0;
    if (layer_count_ > 0) {
        const Layer& l = layers_[0];
        if (l.art.valid() && l.pb == 0 && l.pc == 0 && l.pa > 0) {
            const int lo = (l.refx >> 8) & (l.size_px - 1);
            const int hi = lo + static_cast<int>((240ll * l.pa) >> 8);
            a = static_cast<int>((static_cast<int64_t>(std::max(0, lo - l.art.x0)) << 8) / l.pa);
            b = static_cast<int>((static_cast<int64_t>(std::max(0, l.art.x1 - hi)) << 8) / l.pa);
        }
    }
    if (left) *left = a;
    if (right) *right = b;
}

const WsMarginPixel* AffineMarginRenderer::provide(int y, int extra_left, int extra_right, int* left, int* width) {
    if (!widescreen_is_margin_row(y)) return nullptr;
    const bool new_pass = !have_last_ || y <= last_y_;
    if (new_pass && source_) begin_frame(source_(source_user_));
    have_last_ = true;
    last_y_ = y;
    if (layer_count_ == 0) return nullptr;
    const int w = 240 + std::max(0, extra_left) + std::max(0, extra_right);
    row_.resize(static_cast<size_t>(w));
    if (!render_row(y, -std::max(0, extra_left), w, row_.data())) return nullptr;
    if (left) *left = -std::max(0, extra_left);
    if (width) *width = w;
    return row_.data();
}

} // namespace gbaimp
