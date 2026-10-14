// Font library, validation of untrusted TrueType files and glyph rasterisation (no SDL, no real font files: the tests
// build a synthetic TrueType font in memory, see synth_font.h).
#include "test_harness.h"
#include "synth_font.h"
#include "gbaimp/text_fonts.h"
#include "gbaimp/text_raster.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace gbaimp::text;

namespace {

void write_file(const std::string& path, const std::vector<uint8_t>& d) {
    std::ofstream o(path, std::ios::binary);
    o.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

long ink_sum(const GlyphBitmap& b) {
    long s = 0;
    for (uint8_t a : b.a) s += a;
    return s;
}

void test_face_metrics() {
    const std::vector<uint8_t> ttf = testfont::make_font(testfont::default_cps(), true);
    CHECK(validate_truetype(ttf.data(), ttf.size()));
    FontLibrary lib;
    lib.set_user_font_dir("");
    CHECK_EQ(lib.add_builtin_font("Test Sans", ttf.data(), ttf.size()), 0);
    CHECK_EQ(lib.builtin_count(), 1);
    CHECK_EQ(lib.family_count(), 1);
    CHECK(lib.family_name(0) == "Test Sans");
    CHECK(lib.family_name(5).empty() && lib.family_name(-1).empty());
    CHECK(lib.face(1) == nullptr && lib.face(-1) == nullptr);
    CHECK_EQ(lib.find_family("test SANS"), 0);
    CHECK_EQ(lib.find_family("nope"), -1);
    const FontFace* f = lib.face(0);
    CHECK(f && f->valid() && f->builtin && f->id == 0);
    if (!f) return;
    CHECK_EQ(f->upem, 1000);
    CHECK(f->xheight == 500.0f && f->capheight == 700.0f);
    CHECK(f->ascent == 800.0f && f->descent == -200.0f);
    CHECK(f->glyph_index('A') != 0 && f->glyph_index(0x3042) == 0);
    CHECK(f->glyph_index('A') == f->glyph_index('A'));   // cached path
    CHECK(f->glyph_index(0x3042) == f->glyph_index(0x3042));
    CHECK_EQ(f->advance(f->glyph_index('A')), 600.0f);
    CHECK_EQ(f->advance(f->glyph_index(' ')), 300.0f);
    CHECK_EQ(f->kern(f->glyph_index('A'), f->glyph_index('V')), -100.0f);
    CHECK_EQ(f->kern(f->glyph_index('V'), f->glyph_index('A')), 0.0f);
    CHECK_EQ(f->kern(0, f->glyph_index('A')), 0.0f);
    float y0 = 0, y1 = 0;
    CHECK(f->glyph_extent(f->glyph_index('g'), y0, y1) && y0 == -200.0f && y1 == 500.0f);
    CHECK(!f->glyph_extent(f->glyph_index(' '), y0, y1));

    // light vertical hint: the x-height lands on a whole number of output pixels, the width keeps the true em
    const FaceScale sc = f->scale_for(33.0f);
    CHECK_EQ(sc.xheight_px, 17);                 // 33 * 0.5 = 16.5 -> 17
    CHECK(std::fabs(sc.sx - 0.033f) < 1e-6f);
    CHECK(std::fabs(sc.sy * 500.0f - 17.0f) < 1e-4f);

    // size_adjust scales the em of the family
    FontLibrary lib2;
    lib2.set_user_font_dir("");
    lib2.add_builtin_font("Small", ttf.data(), ttf.size(), 0.5f);
    CHECK(std::fabs(lib2.face(0)->scale_for(40.0f).sx - 0.02f) < 1e-6f);

    // bitmap of a rectangle glyph: 'H' at em 40 -> 0.5*40 = 20 wide, 28 tall
    int x0, ym, w, h;
    std::vector<uint8_t> px;
    const FaceScale s40 = f->scale_for(40.0f);
    CHECK(f->bitmap(f->glyph_index('H'), s40, 0.0f, x0, ym, w, h, px));
    CHECK(w >= 19 && w <= 21 && h >= 27 && h <= 29 && ym == -h);
    CHECK(!f->bitmap(0, s40, 0.0f, x0, ym, w, h, px));          // missing glyph
    CHECK(!f->bitmap(f->glyph_index(' '), s40, 0.0f, x0, ym, w, h, px));   // no ink
}

void test_builtin_validation() {
    FontLibrary lib;
    lib.set_user_font_dir("");
    const unsigned char junk[64] = {1, 2, 3};
    CHECK_EQ(lib.add_builtin_font("Junk", junk, sizeof junk), -1);
    CHECK_EQ(lib.add_builtin_font("Null", nullptr, 100), -1);
    CHECK_EQ(lib.add_builtin_font("", junk, sizeof junk), -1);
    CHECK_EQ(lib.family_count(), 0);
    const std::vector<uint8_t> ttf = testfont::make_font(testfont::default_cps());
    const uint64_t g0 = lib.generation();
    CHECK_EQ(lib.add_builtin_font("A", ttf.data(), ttf.size()), 0);
    CHECK_EQ(lib.add_builtin_font("B", ttf.data(), ttf.size()), 1);
    CHECK(lib.generation() > g0);
    CHECK(lib.face(1) && lib.face(1)->id == 1 && lib.face(1)->name == "B");
}

void test_user_fonts_and_validation() {
    namespace fs = std::filesystem;
    testh::TempDir tmp("textfonts");
    const std::vector<uint8_t> good = testfont::make_font(testfont::default_cps());
    CHECK(validate_truetype(good.data(), good.size()));
    write_file(tmp.file("MyFont.ttf"), good);
    write_file(tmp.file("Broken.ttf"), std::vector<uint8_t>(good.begin(), good.begin() + good.size() / 2));
    write_file(tmp.file("Garbage.ttf"), std::vector<uint8_t>(2000, 0x5A));
    write_file(tmp.file("notafont.txt"), good);
    write_file(tmp.file("tiny.ttf"), std::vector<uint8_t>(10, 0));

    FontLibrary lib;
    lib.set_user_font_dir(tmp.str());
    CHECK_EQ(lib.family_count(), 1);                         // only the valid .ttf is listed
    CHECK(lib.family_name(0) == "MyFont (fonts/)");
    CHECK(lib.face(0) && !lib.face(0)->builtin && lib.face(0)->valid());

    // built-ins come first; adding one later re-numbers the user font
    const std::vector<uint8_t> ttf = testfont::make_font(testfont::default_cps());
    CHECK_EQ(lib.add_builtin_font("Test Sans", ttf.data(), ttf.size()), 0);
    CHECK_EQ(lib.family_count(), 2);
    CHECK(lib.family_name(0) == "Test Sans" && lib.family_name(1) == "MyFont (fonts/)");
    CHECK(lib.face(1)->id == 1 && lib.find_family("myfont (FONTS/)") == 1);

    // rescan picks up new files and bumps the generation
    const uint64_t g = lib.generation();
    write_file(tmp.file("Another.ttf"), good);
    lib.rescan_user_fonts();
    CHECK(lib.generation() > g);
    CHECK_EQ(lib.family_count(), 3);
    CHECK(lib.family_name(1) == "Another (fonts/)" && lib.family_name(2) == "MyFont (fonts/)");   // sorted by path

    // user font count is capped
    for (int i = 0; i < 12; ++i) write_file(tmp.file("F" + std::to_string(100 + i) + ".ttf"), good);
    lib.rescan_user_fonts();
    CHECK_EQ(lib.family_count(), 1 + FontLibrary::kMaxUserFonts);

    // an oversized file is skipped
    {
        FontLibrary big;
        testh::TempDir t2("textfonts_big");
        std::vector<uint8_t> huge = good;
        huge.resize(FontLibrary::kMaxUserFontBytes + 1, 0);
        write_file(t2.file("Huge.ttf"), huge);
        big.set_user_font_dir(t2.str());
        CHECK_EQ(big.family_count(), 0);
    }

    lib.set_user_font_dir((fs::path(tmp.str()) / "nonexistent").string());
    CHECK_EQ(lib.family_count(), 1);                         // removing the folder removes the user fonts
    lib.set_user_font_dir("");
    CHECK_EQ(lib.family_count(), 1);

    // hostile inputs
    CHECK(!validate_truetype(nullptr, 0) && !validate_truetype(good.data(), 8));
    CHECK(!validate_truetype(std::vector<uint8_t>(2000, 0x5A).data(), 2000));
    {
        std::vector<uint8_t> m = good;
        m[0] = 'O'; m[1] = 'T'; m[2] = 'T'; m[3] = 'O';       // CFF outlines are not accepted
        CHECK(!validate_truetype(m.data(), m.size()));
    }
    // targeted hostile cmaps: segment offsets that point outside the table, unsorted segments, bad search parameters,
    // a CFF table next to glyf, an out-of-range kern table
    {
        auto table_off = [&](const std::vector<uint8_t>& f, const char* tag) -> size_t {
            const size_t n = (static_cast<size_t>(f[4]) << 8) | f[5];
            for (size_t i = 0; i < n; ++i)
                if (std::memcmp(&f[12 + 16 * i], tag, 4) == 0)
                    return (static_cast<size_t>(f[12 + 16 * i + 8]) << 24) | (static_cast<size_t>(f[12 + 16 * i + 9]) << 16) |
                           (static_cast<size_t>(f[12 + 16 * i + 10]) << 8) | f[12 + 16 * i + 11];
            return 0;
        };
        const size_t cm = table_off(good, "cmap");
        const size_t sub = cm + 12;                        // the one format 4 subtable
        const size_t seg = (static_cast<size_t>(good[sub + 6]) << 8 | good[sub + 7]) / 2;
        std::vector<uint8_t> m = good;
        m[sub + 16 + 6 * seg + 2 * 10] = 0x7F;             // idRangeOffset of a segment far beyond the table
        CHECK(!validate_truetype(m.data(), m.size()));
        m = good;
        m[sub + 12] ^= 0x04;                               // rangeShift no longer canonical
        CHECK(!validate_truetype(m.data(), m.size()));
        m = good;
        m[sub + 14 + 2 * (seg - 1)] = 0x12;                // last endCode no longer 0xFFFF
        CHECK(!validate_truetype(m.data(), m.size()));
        m = good;
        m[sub + 14 + 1] = 0xFF; m[sub + 14] = 0xF0;        // first endCode above the second: not ascending
        CHECK(!validate_truetype(m.data(), m.size()));
        m = good;
        m[cm + 3] = 0;                                     // no cmap subtables
        CHECK(!validate_truetype(m.data(), m.size()));
        m = good;
        m[cm + 4 + 4] = 0x7F;                              // subtable offset outside the table
        CHECK(!validate_truetype(m.data(), m.size()));
        m = good;
        m[12] = 'C'; m[13] = 'F'; m[14] = 'F'; m[15] = ' ';   // the first table becomes a CFF table
        CHECK(!validate_truetype(m.data(), m.size()));
        const std::vector<uint8_t> withkern = testfont::make_font(testfont::default_cps(), true);
        CHECK(validate_truetype(withkern.data(), withkern.size()));
        const size_t kt = table_off(withkern, "kern");
        m = withkern;
        m[kt + 10] = 0x7F;                                 // pair count far beyond the table
        CHECK(!validate_truetype(m.data(), m.size()));
        // GPOS is hidden from user fonts: a font with a (garbage) GPOS table still loads without kerning from it
        std::vector<uint8_t> g = withkern;
        const size_t nt = (static_cast<size_t>(g[4]) << 8) | g[5];
        for (size_t i = 0; i < nt; ++i) if (std::memcmp(&g[12 + 16 * i], "kern", 4) == 0) std::memcpy(&g[12 + 16 * i], "GPOS", 4);
        CHECK(validate_truetype(g.data(), g.size()));
        testh::TempDir t3("textfonts_gpos");
        write_file(t3.file("Gpos.ttf"), g);
        FontLibrary gl;
        gl.set_user_font_dir(t3.str());
        CHECK(gl.family_count() == 1 && gl.face(0)->kern(gl.face(0)->glyph_index('A'), gl.face(0)->glyph_index('V')) == 0.0f);
    }
    // mutation fuzz: must never crash; whatever passes must be loadable and renderable without a crash
    std::mt19937 rng(1234);
    int accepted = 0;
    for (int i = 0; i < 400; ++i) {
        std::vector<uint8_t> m = good;
        for (int k = 0; k < 1 + i % 8; ++k) m[rng() % m.size()] = static_cast<uint8_t>(rng());
        if (validate_truetype(m.data(), m.size())) {
            ++accepted;
            m.resize(m.size() + 16, 0);
            FontLibrary one;
            one.set_user_font_dir("");
            if (one.add_builtin_font("Mut", m.data(), m.size()) == 0) {
                const FontFace* f = one.face(0);
                GlyphBitmap bm;
                for (uint32_t cp = 0; cp < 0x10000; ++cp) {   // every BMP lookup, with the metrics a layout would read
                    const int gi = f->glyph_index(cp);
                    f->advance(gi);
                    f->kern(gi, f->glyph_index('A'));
                    if (cp >= 33 && cp < 127) rasterize_glyph(*f, gi, 24.0f, 0, GlyphVariant::Body, 0.0f, bm);
                }
            }
        }
    }
    CHECK(accepted <= 400);
    // truncations of any length are rejected or accepted without crashing
    for (size_t n = 0; n < good.size(); n += 37) validate_truetype(good.data(), n);
}

void test_raster() {
    const std::vector<uint8_t> ttf = testfont::make_font(testfont::default_cps());
    FontLibrary lib;
    lib.set_user_font_dir("");
    lib.add_builtin_font("Test Sans", ttf.data(), ttf.size());
    const FontFace& f = *lib.face(0);
    const int gi = f.glyph_index('H');
    GlyphBitmap body, shadow, outline;
    CHECK(rasterize_glyph(f, gi, 40.0f, 0, GlyphVariant::Body, 0.0f, body) && !body.empty());
    CHECK(ink_sum(body) > 0);
    // a solid rectangle: the interior is fully covered (after the stem-darkening curve, 255 stays 255)
    CHECK_EQ(body.a[static_cast<size_t>(body.h / 2) * body.w + body.w / 2], 255);
    CHECK(rasterize_glyph(f, gi, 40.0f, 0, GlyphVariant::Shadow, 3.0f, shadow));
    CHECK(shadow.w == body.w + 3 && shadow.h == body.h + 3 && shadow.x0 == body.x0 && shadow.y0 == body.y0);
    CHECK(ink_sum(shadow) > ink_sum(body));
    CHECK(rasterize_glyph(f, gi, 40.0f, 0, GlyphVariant::Outline, 2.0f, outline));
    CHECK(outline.w == body.w + 2 * 3 && outline.x0 == body.x0 - 3 && outline.y0 == body.y0 - 3);
    CHECK(ink_sum(outline) > ink_sum(body));
    // deterministic
    GlyphBitmap again;
    rasterize_glyph(f, gi, 40.0f, 0, GlyphVariant::Body, 0.0f, again);
    CHECK(again.a == body.a);
    // subpixel phases move ink to the right
    GlyphBitmap p0, p3;
    rasterize_glyph(f, gi, 17.0f, 0, GlyphVariant::Body, 0.0f, p0);
    rasterize_glyph(f, gi, 17.0f, 3, GlyphVariant::Body, 0.0f, p3);
    CHECK(!p0.empty() && !p3.empty() && p0.a != p3.a);
    // glyphs with no ink
    GlyphBitmap none;
    CHECK(!rasterize_glyph(f, f.glyph_index(' '), 40.0f, 0, GlyphVariant::Body, 0.0f, none) && none.empty());
    CHECK(!rasterize_glyph(f, 0, 40.0f, 0, GlyphVariant::Body, 0.0f, none));

    // make_shadow_bitmap / make_outline_bitmap on a hand-made 2x2 block
    GlyphBitmap blk;
    blk.w = blk.h = 2;
    blk.x0 = -1; blk.y0 = -2;
    blk.a = {255, 255, 255, 255};
    GlyphBitmap sh;
    make_shadow_bitmap(blk, 1.0f, 1.0f, sh);
    CHECK(sh.w == 3 && sh.h == 3 && sh.x0 == -1 && sh.y0 == -2);
    CHECK(sh.a[2 * 3 + 2] == 255 && sh.a[0] == 0);            // the shadow is the body shifted E / S / SE: SE corner inked, NW corner not
    make_shadow_bitmap(blk, -5.0f, -5.0f, sh);                 // negative offsets clamp to 0
    CHECK(sh.w == 2 && sh.h == 2);
    GlyphBitmap ol;
    make_outline_bitmap(blk, 1.0f, ol);
    CHECK(ol.w == 2 + 2 * 2 && ol.x0 == -1 - 2);
    CHECK(ol.a[0] == 0 && ol.a[2 * ol.w + 2] == 255);          // far corner clear, body inside
    GlyphBitmap empty, out;
    make_outline_bitmap(empty, 2.0f, out);
    CHECK(out.empty());
}

} // namespace

int main() {
    test_face_metrics();
    test_builtin_validation();
    test_user_fonts_and_validation();
    test_raster();
    return testh::finish("text_fonts");
}
