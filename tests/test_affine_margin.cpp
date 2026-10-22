#include "test_harness.h"
#include "gbaimp/affine_margin.h"

#include <cstring>
#include <vector>

using namespace gbaimp;

namespace {

// A fake PPU: 256x256 affine map (size code 1), tiles at char base 0, map at screen base 0x4000.
// Tile 1 is solid colour index 5, tile 2 has a single opaque pixel (row 2, column 3).
struct Fake {
    std::vector<uint8_t> vram = std::vector<uint8_t>(96 * 1024, 0);
    std::vector<uint8_t> pal = std::vector<uint8_t>(1024, 0);
    std::vector<uint8_t> io = std::vector<uint8_t>(1024, 0);
    uint16_t dispcnt = 2 | 0x0400;   // mode 2, BG2 on

    static constexpr uint32_t kMap = 0x4000;
    void put16(uint32_t off, uint16_t v) { io[off] = uint8_t(v); io[off + 1] = uint8_t(v >> 8); }
    void put32(uint32_t off, uint32_t v) { put16(off, uint16_t(v)); put16(off + 2, uint16_t(v >> 16)); }

    Fake() {
        std::memset(&vram[64], 5, 64);          // tile 1
        vram[2 * 64 + 2 * 8 + 3] = 7;           // tile 2
        pal[5 * 2] = 0x1F; pal[5 * 2 + 1] = 0x00;   // colour 5 = 0x001F (red)
        pal[7 * 2] = 0xE0; pal[7 * 2 + 1] = 0x03;   // colour 7 = 0x03E0 (green)
        pal[9 * 2] = 0x00; pal[9 * 2 + 1] = 0x7C;   // colour 9 = 0x7C00 (blue)
        // art: tiles (tx 4..27, ty 8..23) of tile 1 -> pixels x 32..224, y 64..192
        for (int ty = 8; ty < 24; ++ty)
            for (int tx = 4; tx < 28; ++tx) vram[kMap + ty * 32 + tx] = 1;
        put16(0x0C, uint16_t(2 | (8 << 8) | (1 << 13) | (1 << 14)));   // BG2CNT: prio 2, screen 8, wrap, 256x256
        put16(0x20, 256); put16(0x26, 256);                             // PA = PD = 1.0
    }
    void window_at(int x, int y) { put32(0x28, uint32_t(x) << 8); put32(0x2C, uint32_t(y) << 8); }
    PpuMemory mem() const { return {dispcnt, io.data(), vram.data(), pal.data()}; }
};

void test_bounds() {
    Fake f;
    ArtBounds b = scan_affine_art_bounds(f.vram.data(), f.vram.size(), 0, Fake::kMap, 256);
    CHECK(b.valid());
    CHECK_EQ(b.x0, 32); CHECK_EQ(b.y0, 64); CHECK_EQ(b.x1, 224); CHECK_EQ(b.y1, 192);

    // One opaque pixel of tile 2 far outside the block widens the box to that pixel exactly.
    f.vram[Fake::kMap + 2 * 32 + 2] = 2;       // tile (2, 2) -> pixel (19, 18)
    b = scan_affine_art_bounds(f.vram.data(), f.vram.size(), 0, Fake::kMap, 256);
    CHECK_EQ(b.x0, 19); CHECK_EQ(b.y0, 18); CHECK_EQ(b.x1, 224); CHECK_EQ(b.y1, 192);

    // Empty map, bad arguments.
    Fake g;
    std::memset(&g.vram[Fake::kMap], 0, 1024);
    CHECK(!scan_affine_art_bounds(g.vram.data(), g.vram.size(), 0, Fake::kMap, 256).valid());
    CHECK(!scan_affine_art_bounds(nullptr, 0, 0, 0, 256).valid());
    CHECK(!scan_affine_art_bounds(g.vram.data(), g.vram.size(), 0, Fake::kMap, 100).valid());
    // A map that runs off the end of VRAM does not read past it.
    CHECK(!scan_affine_art_bounds(g.vram.data(), g.vram.size(), 0, 96 * 1024 - 16, 512).valid());
}

void test_rows() {
    Fake f;
    f.window_at(60, 100);                       // native band = map x 60..299, y 100..259 (wraps)
    AffineMarginRenderer r;
    r.begin_frame(f.mem());
    CHECK(r.active());
    CHECK_EQ(r.layer_count(), 1);
    CHECK(r.art_bounds(0).valid());

    std::vector<WsMarginPixel> row(280);
    // Row above the band, inside the art: opaque red on the whole visible span.
    CHECK(r.render_row(-10, -20, 280, row.data()));
    CHECK_EQ(row[20].color, 0x001F);            // hardware x 0 -> map x 60
    CHECK_EQ(row[20].priority, 2);
    CHECK_EQ(row[20].order, 127);
    // Row far above the art (map y 50 < 64): the first art row repeats, not blank.
    CHECK(r.render_row(-50, 0, 240, row.data()));
    CHECK_EQ(row[10].color, 0x001F);
    // Columns left of the art (hardware x -50 -> map x 10 < 32): the first art column repeats.
    CHECK(r.render_row(-10, -50, 10, row.data()));
    CHECK_EQ(row[0].color, 0x001F);
    // Below the band: map y 260.. is past the art end (192) and past the seam; it repeats the LAST art
    // row, never the far side of the map.
    CHECK(r.render_row(170, 0, 240, row.data()));
    CHECK_EQ(row[10].color, 0x001F);

    // Clamping off: the empty map above the art stays transparent.
    AffineMarginOptions o; o.clamp_to_art = false;
    r.set_options(o);
    r.begin_frame(f.mem());
    CHECK(r.render_row(-50, 0, 240, row.data()));
    CHECK((row[10].color & 0x8000) != 0);
    CHECK(r.render_row(-10, 0, 240, row.data()));     // inside the art it is the same picture
    CHECK_EQ(row[10].color, 0x001F);
}

// The map seam: a window low in the map must not repeat the TOP row of the art below it.
void test_seam_uses_last_row() {
    Fake f;
    std::memset(&f.vram[Fake::kMap], 0, 1024);
    // Art only in the lower half (map y 128..255): tile 1 on tile rows 16..31.
    for (int ty = 16; ty < 32; ++ty)
        for (int tx = 0; tx < 32; ++tx) f.vram[Fake::kMap + ty * 32 + tx] = 1;
    // Mark the last art row blue to tell it from the others: tile 3 solid colour 9 on tile row 31.
    std::memset(&f.vram[3 * 64], 9, 64);
    for (int tx = 0; tx < 32; ++tx) f.vram[Fake::kMap + 31 * 32 + tx] = 3;
    f.window_at(0, 100);                       // band map y 100..259: crosses the seam at 256
    AffineMarginRenderer r;
    r.begin_frame(f.mem());
    std::vector<WsMarginPixel> row(240);
    CHECK(r.render_row(165, 0, 240, row.data()));   // map y 265 -> unwrapped, past the art
    CHECK_EQ(row[5].color, 0x7C00);                 // the last art row (blue), not the first (red)
}

void test_layers_and_modes() {
    Fake f;
    f.window_at(60, 100);
    // BG3: prio 1, screen base 0x4800, non-wrapping, tile 3 (colour 9) in the middle of the map.
    std::memset(&f.vram[3 * 64], 9, 64);
    const uint32_t map3 = 0x4800;
    for (int ty = 10; ty < 20; ++ty)
        for (int tx = 8; tx < 24; ++tx) f.vram[map3 + ty * 32 + tx] = 3;
    f.put16(0x0E, uint16_t(1 | (9 << 8) | (1 << 14)));   // BG3CNT: prio 1, screen 9, no wrap
    f.put16(0x30, 256); f.put16(0x36, 256);
    f.put32(0x38, 60u << 8); f.put32(0x3C, 100u << 8);
    f.dispcnt = 2 | 0x0400 | 0x0800;
    AffineMarginRenderer r;
    r.begin_frame(f.mem());
    CHECK_EQ(r.layer_count(), 2);
    std::vector<WsMarginPixel> row(240);
    CHECK(r.render_row(-10, 0, 240, row.data()));   // map y 90: BG3 art spans y 80..160 -> BG3 on top
    CHECK_EQ(row[100].color, 0x7C00);               // map x 160, inside BG3 art 64..192
    CHECK_EQ(row[100].priority, 1);
    CHECK_EQ(row[5].priority, 1);                   // x 65: still inside 64..192
    CHECK_EQ(row[220].color, 0x001F);               // x 280 wraps to 24: BG3 has no art there, BG2 shows

    // Mode 1: only BG2 is affine. Mode 0: nothing.
    f.dispcnt = 1 | 0x0400 | 0x0800;
    r.begin_frame(f.mem());
    CHECK_EQ(r.layer_count(), 1);
    f.dispcnt = 0 | 0x0400;
    r.begin_frame(f.mem());
    CHECK(!r.active());
    CHECK(!r.render_row(-5, 0, 10, row.data()));
    // Layer mask: BG2 only.
    f.dispcnt = 2 | 0x0400 | 0x0800;
    AffineMarginOptions o; o.layer_mask = 0x04;
    r.set_options(o);
    r.begin_frame(f.mem());
    CHECK_EQ(r.layer_count(), 1);
}

void test_headroom() {
    Fake f;
    f.window_at(60, 100);
    AffineMarginRenderer r;
    r.begin_frame(f.mem());
    int a = -1, b = -1, l = -1, rr = -1;
    r.available_rows(&a, &b);
    CHECK_EQ(a, 36);                      // 100 - 64
    CHECK_EQ(b, 0);                       // art ends at 192, band ends at 260
    r.available_cols(&l, &rr);
    CHECK_EQ(l, 28);                      // 60 - 32
    CHECK_EQ(rr, 0);
    f.window_at(40, 0);
    r.begin_frame(f.mem());
    r.available_rows(&a, &b);
    CHECK_EQ(a, 0); CHECK_EQ(b, 32);      // band 0..160, art ends 192
    // A scaled layer (PD = 2.0): 160 rows cover 320 map rows.
    f.put16(0x26, 512);
    f.window_at(40, 0);
    r.begin_frame(f.mem());
    r.available_rows(&a, &b);
    CHECK_EQ(b, 0);
    // A rotated layer reports nothing.
    f.put16(0x26, 256); f.put16(0x22, 40);
    r.begin_frame(f.mem());
    r.available_rows(&a, &b);
    CHECK_EQ(a, 0); CHECK_EQ(b, 0);
}

struct Source { Fake* f; int calls = 0; };
PpuMemory get_mem(void* u) { auto* s = static_cast<Source*>(u); ++s->calls; return s->f->mem(); }

void test_provider() {
    Fake f;
    f.window_at(60, 100);
    Source src{&f};
    AffineMarginRenderer r;
    r.set_memory_source(&get_mem, &src);
    int left = 0, width = 0;
    // Native rows never produce anything and never start a pass.
    CHECK(r.provide(0, 20, 20, &left, &width) == nullptr);
    CHECK(r.provide(159, 20, 20, &left, &width) == nullptr);
    CHECK_EQ(src.calls, 0);
    // One pass: rows -3..-1 then 160..161. The memory is latched once, at the first row.
    const WsMarginPixel* p = r.provide(-3, 20, 20, &left, &width);
    CHECK(p != nullptr);
    CHECK_EQ(left, -20); CHECK_EQ(width, 280);
    CHECK_EQ(p[20].color, 0x001F);
    r.provide(-2, 20, 20, &left, &width);
    r.provide(-1, 20, 20, &left, &width);
    r.provide(0, 20, 20, &left, &width);          // native row in between: ignored
    p = r.provide(160, 20, 20, &left, &width);
    CHECK(p != nullptr);
    r.provide(161, 20, 20, &left, &width);
    CHECK_EQ(src.calls, 1);
    // The next frame starts again at the top: a second latch, and it sees the new camera.
    f.window_at(60, 90);
    r.provide(-3, 20, 20, &left, &width);
    CHECK_EQ(src.calls, 2);
    // No extras: nothing to say about width.
    p = r.provide(-1, 0, 0, &left, &width);
    CHECK_EQ(width, 240);
    // A frame with no affine layer gives no row.
    f.dispcnt = 0;
    CHECK(r.provide(-3, 20, 20, &left, &width) == nullptr);
}

} // namespace

int main() {
    test_bounds();
    test_rows();
    test_seam_uses_last_row();
    test_layers_and_modes();
    test_headroom();
    test_provider();
    return testh::finish("affine_margin");
}
