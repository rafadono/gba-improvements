// SDL renderer of the text engine: drawing, reveal, caches, atlas ownership, fades, colour matching.
// Drawing uses SDL's software renderer on an offscreen surface (no window / GPU needed) and a synthetic font.
#include "test_harness.h"
#include "synth_font.h"
#include "gbaimp/text_fade.h"
#include "gbaimp/text_renderer.h"
#include "gbaimp/text_synth.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace gbaimp::text;

namespace {

struct Fixture {
    std::vector<uint8_t> ttf = testfont::make_font(testfont::default_cps());
    FontLibrary lib;
    Fixture() {
        lib.set_user_font_dir("");
        lib.add_builtin_font("Test Sans", ttf.data(), ttf.size());
    }
};

TextSettings base_settings() {
    TextSettings s;
    s.enabled = true;
    s.outline_width = 0.8f;
    return s;
}

DrawInfo info_for(float scale, SDL_Renderer* r = nullptr, int ox = 0, int oy = 0) {
    DrawInfo i;
    i.renderer = r;
    i.dst_x = ox;
    i.dst_y = oy;
    i.dst_w = static_cast<int>(240 * scale);
    i.dst_h = static_cast<int>(160 * scale);
    return i;
}

struct Canvas {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    TextRenderer* tr = nullptr;
    int w = 0, h = 0;
    Canvas(int W, int H, TextRenderer* t) : tr(t), w(W), h(H) {
        surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf) ren = SDL_CreateSoftwareRenderer(surf);
        clear();
    }
    ~Canvas() {
        tr->release_gpu();   // before the SDL_Renderer goes away
        if (ren) SDL_DestroyRenderer(ren);
        if (surf) SDL_FreeSurface(surf);
    }
    void clear() {
        if (!ren) return;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
    }
    bool ink(int& x0, int& y0, int& x1, int& y1, long& count) {
        std::vector<uint32_t> px = pixels();
        x0 = w; y0 = h; x1 = -1; y1 = -1; count = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (px[static_cast<size_t>(y) * w + x] & 0x00FFFFFFu) {
                    x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); ++count;
                }
        return true;
    }
    std::vector<uint32_t> pixels() {
        std::vector<uint32_t> px(static_cast<size_t>(w) * h);
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4);
        return px;
    }
    // sum of one channel (0 = blue, 1 = green, 2 = red)
    uint64_t channel(int c) {
        uint64_t s = 0;
        for (uint32_t px : pixels()) s += (px >> (8 * c)) & 0xFF;
        return s;
    }
};

void test_reveal_and_drawing(Fixture& fx) {
    TextRenderer r(&fx.lib);
    TextSettings st = base_settings();
    TextPage p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    Canvas cv(240 * 4, 160 * 4, &r);
    CHECK(cv.ren != nullptr);
    if (!cv.ren) return;
    DrawInfo info = info_for(4.0f, cv.ren);
    CHECK(r.can_draw(p, st, info));
    CHECK(r.draw(p, st, info));
    const std::vector<uint32_t> full_px = cv.pixels();
    const int total = p.revealed;
    // reveal prefix: ink grows monotonically and is a subset of the full page
    long prev = 0;
    for (int n : {0, 1, 5, 12, 20, 33, 40, total}) {
        TextPage pr = p;
        pr.revealed = n;
        cv.clear();
        CHECK_EQ(r.draw(pr, st, info), n > 0);
        int x0, y0, x1, y1;
        long cnt;
        cv.ink(x0, y0, x1, y1, cnt);
        CHECK(cnt >= prev);
        prev = cnt;
        const std::vector<uint32_t> px = cv.pixels();
        long outside = 0;
        for (size_t i = 0; i < px.size(); ++i) if ((px[i] & 0xFFFFFF) && !(full_px[i] & 0xFFFFFF)) ++outside;
        CHECK_EQ(outside, 0);
    }
    // entries past the end are harmless
    TextPage over = p;
    over.revealed = 100000;
    cv.clear();
    CHECK(r.draw(over, st, info));
}

void test_styles_and_bounds(Fixture& fx) {
    TextRenderer r(&fx.lib);
    TextSettings st = base_settings();
    TextPage p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    for (float sc : {3.0f, 5.4f, 6.19f}) {
        Canvas cv(static_cast<int>(240 * sc) + 8, static_cast<int>(160 * sc) + 8, &r);
        if (!cv.ren) { CHECK(false); return; }
        long plain_ink = 0;
        for (int style = 0; style < 3; ++style)
            for (int wrap = 0; wrap < 2; ++wrap) {
                TextSettings s = st;
                s.style = static_cast<TextStyle>(style);
                s.wrap = static_cast<WrapMode>(wrap);
                DrawInfo info = info_for(sc, cv.ren, 4, 4);
                cv.clear();
                CHECK(r.draw(p, s, info));
                int x0, y0, x1, y1;
                long cnt;
                cv.ink(x0, y0, x1, y1, cnt);
                CHECK(cnt > 200);
                // everything inside the text area (+ shadow / outline slack), offset by the dst origin
                const float lx = (72.0f - 1.0f) * sc + 4, rx = (72.0f + 155.0f + 1.0f) * sc + 4;
                const float ty = (108.0f - 1.0f) * sc + 4, by = (108.0f + 40.0f + 1.0f) * sc + 4;
                CHECK(x0 >= lx - 1 && x1 <= rx + 1 && y0 >= ty - 1 && y1 <= by + 1);
                if (wrap == 0 && style == 0) plain_ink = cnt;
                if (wrap == 0 && style == 1) CHECK(cnt >= plain_ink);   // a shadow only adds ink
            }
    }
}

void test_caches_and_ownership(Fixture& fx) {
    TextRenderer r(&fx.lib);
    TextSettings st = base_settings();
    TextPage p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    Canvas cv(240 * 4, 160 * 4, &r);
    DrawInfo info = info_for(4.0f, cv.ren);
    r.invalidate();
    cv.clear();
    r.draw(p, st, info);
    const uint64_t lb = r.layout_builds(), gr = r.glyphs_rasterized(), fl = r.atlas_flushes();
    CHECK(lb >= 1 && gr > 0);
    for (int i = 0; i < 20; ++i) { cv.clear(); r.draw(p, st, info); }
    CHECK(r.layout_builds() == lb && r.glyphs_rasterized() == gr && r.atlas_flushes() == fl);   // warm draws do no work
    CHECK(r.draws() >= 21);
    TextSettings s2 = st;
    s2.size_em = 9.0f;
    r.draw(p, s2, info);
    CHECK_EQ(r.layout_builds(), lb + 1);                    // size change rebuilds the layout
    DrawInfo info2 = info_for(5.4f, cv.ren);
    r.draw(p, s2, info2);
    CHECK_EQ(r.layout_builds(), lb + 2);                    // scale change rebuilds the layout
    CHECK(r.atlas_flushes() > fl);                          // ... and flushes the atlas
    const uint64_t fl2 = r.atlas_flushes();
    r.invalidate();
    r.draw(p, s2, info2);
    CHECK(r.atlas_flushes() > fl2);                         // invalidate() drops the atlas

    // releasing the GPU atlas and drawing again recreates it
    r.release_gpu();
    cv.clear();
    CHECK(r.draw(p, st, info));
    // a different SDL renderer: the stale atlas is forgotten, drawing works on the new one
    {
        Canvas cv2(240 * 4, 160 * 4, &r);
        cv2.clear();
        CHECK(r.draw(p, st, info_for(4.0f, cv2.ren)));
        int x0, y0, x1, y1;
        long cnt;
        cv2.ink(x0, y0, x1, y1, cnt);
        CHECK(cnt > 100);
    }   // cv2 destructor releases the atlas before its renderer dies
    CHECK(r.draw(p, st, info_for(4.0f, cv.ren)));

    // a changed font library (generation) rebuilds layout and atlas
    const uint64_t lb2 = r.layout_builds(), fl3 = r.atlas_flushes();
    CHECK_EQ(r.fonts().add_builtin_font("Second", fx.ttf.data(), fx.ttf.size()), 1);
    cv.clear();
    r.draw(p, st, info);
    CHECK(r.layout_builds() > lb2 && r.atlas_flushes() > fl3);
    CHECK_EQ(r.family_index("second"), 1);
    CHECK_EQ(r.family_index("none"), -1);
    CHECK_EQ(r.family_names().size(), 2u);
    r.rescan_fonts();
    CHECK_EQ(r.family_names().size(), 2u);

    // two renderers on one library are independent
    TextRenderer other(&fx.lib);
    CHECK_EQ(other.draws(), 0u);

    // opacity / global alpha dim the text; fully faded draws nothing
    cv.clear();
    r.draw(p, st, info);
    const uint64_t full = cv.channel(1);
    cv.clear();
    DrawInfo faded = info;
    faded.global_alpha = 0.5f;
    r.draw(p, st, faded);
    const uint64_t half = cv.channel(1);
    CHECK(half > 0 && half < full * 3 / 4);
    cv.clear();
    TextSettings dim = st;
    dim.opacity = 0.5f;
    r.draw(p, dim, info);
    CHECK(cv.channel(1) < full * 3 / 4);
    DrawInfo gone = info;
    gone.global_alpha = 0.0f;
    cv.clear();
    CHECK(!r.draw(p, st, gone));
    CHECK(!r.draw(p, st, [&] { DrawInfo d = info; d.renderer = nullptr; return d; }()));
}

void test_can_draw(Fixture& fx) {
    TextRenderer r(&fx.lib);
    TextSettings st = base_settings();
    DrawInfo info = info_for(4.0f);   // can_draw needs no SDL renderer
    TextPage p = synth_page({"Hello world"});
    CHECK(r.can_draw(p, st, info));
    TextSettings off = st;
    off.enabled = false;
    CHECK(!r.can_draw(p, off, info));
    TextPage bad = p;
    bad.chars.push_back(TextChar{0x3042, 0, 11, false, 0, 0});
    CHECK(!r.can_draw(bad, st, info));
    TextPage invalid = p;
    invalid.valid = false;
    CHECK(!r.can_draw(invalid, st, info));
    DrawInfo zero = info;
    zero.dst_w = 0;
    CHECK(!r.can_draw(p, st, zero));
    TextSettings huge = st;
    huge.size_em = 60.0f;
    huge.auto_fit = false;
    CHECK(r.can_draw(p, huge, info) || true);   // overflow with auto-fit off is allowed; must simply not crash
}

void test_fades_and_colours(Fixture& fx) {
    TextRenderer r(&fx.lib);
    TextSettings st = base_settings();
    TextPage p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    Canvas cv(240 * 4, 160 * 4, &r);
    DrawInfo info = info_for(4.0f, cv.ren);
    TextSettings plain = st;
    plain.style = TextStyle::Plain;
    auto green = [&](const TextSettings& s, const TextPage& pg, const DrawInfo& d) {
        cv.clear();
        r.draw(pg, s, d);
        return cv.channel(1);
    };
    // brightness fades act on the colours: darken pulls white text towards black, brighten pulls dark text towards white
    DrawInfo dk = info;
    dk.fade_mode = static_cast<int>(FadeMode::Darken);
    dk.fade_level = 0.5f;
    const uint64_t g_full = green(plain, p, info), g_dark = green(plain, p, dk);
    CHECK(g_dark > 0 && g_dark < g_full * 3 / 4);
    dk.fade_level = 1.0f;
    CHECK_EQ(green(plain, p, dk), 0u);
    TextSettings dim = plain;
    dim.text_rgba = 0x404040FFu;
    DrawInfo br = info;
    const uint64_t g_dim = green(dim, p, br);
    br.fade_mode = static_cast<int>(FadeMode::Brighten);
    br.fade_level = 1.0f;
    const uint64_t g_bright = green(dim, p, br);
    CHECK(g_bright > g_dim * 2);
    CHECK(g_bright >= g_full * 9 / 10);

    // match-the-game colours: a zero setting means the colour the page reports; a palette-derived fade is not applied twice
    TextSettings match = plain;
    match.text_rgba = 0;
    match.fallback_text_rgba = 0xFF0000FFu;   // red when the page reports nothing
    TextPage pg = p;
    pg.native_text_rgba = 0x00FF00FFu;
    cv.clear();
    r.draw(pg, match, info);
    CHECK(cv.channel(1) > 0 && cv.channel(2) == 0);        // the reported pure green
    pg.native_text_rgba = 0;
    cv.clear();
    r.draw(pg, match, info);
    CHECK(cv.channel(2) > 0 && cv.channel(1) == 0);        // the configured fallback
    pg.native_text_rgba = 0x00FF00FFu;
    DrawInfo pf = info;
    pf.fade_mode = static_cast<int>(FadeMode::Darken);
    pf.fade_level = 1.0f;
    pf.fade_from_palette = true;
    CHECK(green(match, pg, pf) > 0);                       // already faded by the palette: not darkened twice
    pf.fade_from_palette = false;
    CHECK_EQ(green(match, pg, pf), 0u);                    // a register fade darkens matched colours
    // match-the-game shadow
    TextSettings msh = st;
    msh.text_rgba = 0x000000FFu;
    msh.shadow_rgba = 0;
    msh.fallback_shadow_rgba = 0x0000FFFFu;
    pg.native_shadow_rgba = 0;
    cv.clear();
    r.draw(pg, msh, info);
    CHECK(cv.channel(0) > 0 && cv.channel(1) == 0);

    // explicit text colour and the outline style colour
    TextSettings red = plain;
    red.text_rgba = 0xFF0000FFu;
    CHECK_EQ(green(red, p, info), 0u);
    TextSettings ol = st;
    ol.style = TextStyle::Outline;
    ol.text_rgba = 0x000000FFu;
    ol.outline_rgba = 0x00FF00FFu;
    CHECK(green(ol, p, info) > 0);

    // highlight colour: configured, then the one the page reports; wider (16:9) source image geometry
    TextSettings hs = plain;
    hs.text_rgba = 0xFF0000FFu;
    hs.highlight_rgba = 0xFFFF00FFu;
    TextPage h = synth_page({"ab cd"}, 22.0f + 16.0f, 12.0f);
    h.chars[3].colour = 1;
    h.chars[4].colour = 1;
    Canvas wide(284 * 4, 160 * 4, &r);
    DrawInfo winfo;
    winfo.renderer = wide.ren;
    winfo.dst_w = 284 * 4;
    winfo.dst_h = 160 * 4;
    winfo.gba_w = 284;
    wide.clear();
    CHECK(r.draw(h, hs, winfo));
    CHECK(wide.channel(1) > 0 && wide.channel(2) > 0);     // configured highlight (yellow has green), plain text red
    r.invalidate();
    wide.clear();
    h.native_highlight_rgba = 0x0000FFFFu;                 // the page reports blue for its colour-code words
    CHECK(r.draw(h, hs, winfo));
    CHECK(wide.channel(1) == 0 && wide.channel(0) > 0);
    int x0, y0, x1, y1;
    long cnt;
    wide.ink(x0, y0, x1, y1, cnt);
    CHECK(x0 >= (22 + 16 - 1) * 4 && x1 <= (22 + 16 + 155 + 1) * 4);   // placed in the wide frame (origin_x includes the margin)
    DrawInfo wpf = winfo;
    wpf.fade_mode = static_cast<int>(FadeMode::Darken);
    wpf.fade_level = 1.0f;
    wpf.fade_from_palette = true;
    wide.clear();
    r.draw(h, hs, wpf);
    CHECK(wide.channel(0) > 0);                            // the page's own highlight colour is not faded twice
    wpf.fade_from_palette = false;
    wide.clear();
    r.draw(h, hs, wpf);
    CHECK_EQ(wide.channel(0), 0u);
}

void test_instance_singleton() {
    TextRenderer& a = TextRenderer::instance();
    TextRenderer& b = TextRenderer::instance();
    CHECK(&a == &b);
    CHECK(&a.fonts() == &FontLibrary::instance());
    a.release_gpu();   // nothing created: harmless
}

} // namespace

int main() {
    SDL_SetMainReady();
    if (!SDL_WasInit(0)) SDL_Init(0);
    {
        Fixture fx;
        test_reveal_and_drawing(fx);
        test_styles_and_bounds(fx);
        test_caches_and_ownership(fx);
        test_can_draw(fx);
        test_fades_and_colours(fx);
    }
    test_instance_singleton();
    SDL_Quit();
    return testh::finish("text_renderer");
}
