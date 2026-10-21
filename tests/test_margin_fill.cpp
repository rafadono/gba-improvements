#define SDL_MAIN_HANDLED
#include "test_harness.h"
#include "gbaimp/host_settings.h"
#include "gbaimp/margin_fill.h"

#include <vector>

using namespace gbaimp;

namespace {
bool pixel(SDL_Renderer* r, int x, int y, Uint8 out[3]) {
    SDL_Rect one{x, y, 1, 1};
    Uint8 rgba[4] = {};
    if (SDL_RenderReadPixels(r, &one, SDL_PIXELFORMAT_RGBA32, rgba, 4) != 0) return false;
    out[0] = rgba[0]; out[1] = rgba[1]; out[2] = rgba[2];
    return true;
}
}

int main() {
    // Pure geometry.
    SDL_Rect bars[4];
    CHECK_EQ(margin_bars({0, 0, 100, 50}, 100, 50, bars), 0);
    CHECK_EQ(margin_bars({20, 0, 60, 50}, 100, 50, bars), 2);                 // pillar
    CHECK_EQ(bars[0].x, 0); CHECK_EQ(bars[0].w, 20); CHECK_EQ(bars[1].x, 80); CHECK_EQ(bars[1].w, 20);
    CHECK_EQ(margin_bars({0, 10, 100, 30}, 100, 50, bars), 2);                // letterbox
    CHECK_EQ(bars[0].h, 10); CHECK_EQ(bars[1].y, 40); CHECK_EQ(bars[1].h, 10);
    CHECK_EQ(margin_bars({20, 10, 60, 30}, 100, 50, bars), 4);                // both
    CHECK_EQ(margin_bars({0, 0, 1, 1}, 0, 0, bars), 0);

    MarginFill& mf = MarginFill::instance();
    mf.set_brightness(500); CHECK_EQ(mf.settings().brightness, 100);
    mf.set_brightness(-3);  CHECK_EQ(mf.settings().brightness, 0);
    mf.set_brightness(50);

    {   // registry rows drive the shared instance
        SettingsRegistry reg;
        MarginFill local;
        register_margin_fill_settings(reg, local);
        CHECK(reg.set("video.margin_fill", 2));
        CHECK(local.settings().mode == MarginFillMode::Blur);
        CHECK(reg.set("video.margin_fill", 9));                       // clamped to the last choice
        CHECK(local.settings().mode == MarginFillMode::Blur);
        CHECK(reg.set("video.margin_brightness", 80));
        int v = 0; CHECK(reg.get("video.margin_brightness", v)); CHECK_EQ(v, 80);
        CHECK(reg.is_enabled("video.margin_brightness"));
        reg.set("video.margin_fill", 0);
        CHECK(!reg.is_enabled("video.margin_brightness"));
    }

    // Software rendering: a red 40x20 picture in a 100x20 window leaves 30 px bars each side.
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 100, 20, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(r != nullptr);
    if (!r) return testh::finish("margin_fill");
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 40, 20);
    std::vector<Uint8> px(40 * 20 * 3);
    for (int i = 0; i < 40 * 20; ++i) { px[i * 3] = 200; px[i * 3 + 1] = 0; px[i * 3 + 2] = 0; }
    SDL_UpdateTexture(tex, nullptr, px.data(), 40 * 3);
    const SDL_Rect src{0, 0, 40, 20}, dst{30, 0, 40, 20};
    Uint8 c[3];

    mf.set_mode(MarginFillMode::Pillar);
    CHECK(!mf.draw(r, tex, src, dst, 100, 20));
    CHECK(pixel(r, 5, 10, c)); CHECK_EQ(c[0], 0);

    mf.set_mode(MarginFillMode::EdgeRepeat);
    CHECK(mf.draw(r, tex, src, dst, 100, 20));
    CHECK(pixel(r, 5, 10, c)); CHECK_NEAR(int(c[0]), 100, 3); CHECK_EQ(c[1], 0);   // 200 * 50%
    CHECK(pixel(r, 95, 10, c)); CHECK_NEAR(int(c[0]), 100, 3);
    Uint8 mr = 0, mg = 0, mb = 0;
    SDL_GetTextureColorMod(tex, &mr, &mg, &mb);
    CHECK_EQ(mr, 255); CHECK_EQ(mg, 255); CHECK_EQ(mb, 255);                  // colour mod restored

    mf.set_mode(MarginFillMode::Blur);
    const bool blurred = mf.draw(r, tex, src, dst, 100, 20);                  // needs target textures
    if (blurred) {
        CHECK(pixel(r, 5, 10, c)); CHECK(c[0] > 60 && c[0] < 140); CHECK_EQ(c[1], 0);
    }
    SDL_ScaleMode sm = SDL_ScaleModeBest;
    SDL_GetTextureScaleMode(tex, &sm);
    CHECK(sm == SDL_ScaleModeNearest);                                        // filter mode restored

    mf.release();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    return testh::finish("margin_fill");
}
