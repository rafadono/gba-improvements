#include "test_harness.h"
#include "gbaimp/screen_filters.h"
#include <cstdint>
#include <vector>

using namespace gbaimp;

static std::vector<uint8_t> ramp() {
    std::vector<uint8_t> v;
    for (int i = 0; i < 256; ++i) {
        v.push_back(uint8_t(i));
        v.push_back(uint8_t(255 - i));
        v.push_back(uint8_t(i / 2));
    }
    return v;  // 256 RGB24 pixels
}

// FNV-1a over the per-channel response of a profile (input v -> output, v = 0..255).
struct LutPin { uint64_t r, g, b; uint8_t spot[6][3]; };
static void lut_of(ScreenFilters& sf, ColorProfile p, uint8_t out[256][3]) {
    sf.set_color_profile(p);
    std::vector<uint8_t> px(256 * 3);
    for (int i = 0; i < 256; ++i) px[i*3] = px[i*3+1] = px[i*3+2] = uint8_t(i);
    sf.apply_color_correction(px.data(), 256, 1);
    for (int i = 0; i < 256; ++i) for (int c = 0; c < 3; ++c) out[i][c] = px[i*3+c];
}
static uint64_t fnv(const uint8_t lut[256][3], int c) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < 256; ++i) { h ^= lut[i][c]; h *= 1099511628211ULL; }
    return h;
}

int main() {
    auto& sf = ScreenFilters::instance();

    // Defaults.
    CHECK(sf.settings().color_profile == ColorProfile::Raw);
    CHECK(sf.settings().mask_type == ScreenMaskType::Off);

    // Raw leaves pixels untouched.
    {
        auto a = ramp(); auto orig = a;
        sf.apply_color_correction(a.data(), 16, 16);
        CHECK(a == orig);
    }

    // Degenerate arguments are safe no-ops.
    sf.set_color_profile(ColorProfile::Ags101);
    sf.apply_color_correction(nullptr, 16, 16);
    {
        auto a = ramp(); auto orig = a;
        sf.apply_color_correction(a.data(), 0, 16);
        sf.apply_color_correction(a.data(), 16, -1);
        CHECK(a == orig);
    }

    // Every non-Raw profile except the identity-gamma one alters the ramp.
    const ColorProfile changing[] = {ColorProfile::Agb001, ColorProfile::Ags001,
                                     ColorProfile::Ags101, ColorProfile::MisterGamma16};
    for (ColorProfile p : changing) {
        sf.set_color_profile(p);
        CHECK(sf.settings().color_profile == p);
        auto a = ramp(); auto orig = a;
        sf.apply_color_correction(a.data(), 16, 16);
        CHECK(a != orig);
    }

    // MisterGamma22 is gamma 2.2/2.2: the LUT is the identity.
    sf.set_color_profile(ColorProfile::MisterGamma22);
    {
        auto a = ramp(); auto orig = a;
        sf.apply_color_correction(a.data(), 16, 16);
        CHECK(a == orig);
    }

    // Agb001 raises the black floor and keeps white in range.
    sf.set_color_profile(ColorProfile::Agb001);
    {
        uint8_t px[6] = {0, 0, 0, 255, 255, 255};
        sf.apply_color_correction(px, 2, 1);
        CHECK(px[0] > 0 && px[1] > 0 && px[2] > 0);
        CHECK(px[3] > 200);
    }

    // Monotonic LUT: brighter input never gives darker output.
    sf.set_color_profile(ColorProfile::Ags101);
    {
        std::vector<uint8_t> g(256 * 3);
        for (int i = 0; i < 256; ++i) g[i*3] = g[i*3+1] = g[i*3+2] = uint8_t(i);
        sf.apply_color_correction(g.data(), 256, 1);
        bool mono = true;
        for (int i = 1; i < 256; ++i) if (g[i*3] < g[(i-1)*3]) mono = false;
        CHECK(mono);
    }

    // Switching back to Raw restores pass-through.
    sf.set_color_profile(ColorProfile::Raw);
    {
        auto a = ramp(); auto orig = a;
        sf.apply_color_correction(a.data(), 16, 16);
        CHECK(a == orig);
    }

    // Mask intensity is clamped to [0.05, 1.0].
    sf.set_mask_intensity(-3.0f);  CHECK_NEAR(sf.settings().mask_intensity, 0.05f, 1e-6f);
    sf.set_mask_intensity(0.0f);   CHECK_NEAR(sf.settings().mask_intensity, 0.05f, 1e-6f);
    sf.set_mask_intensity(0.5f);   CHECK_NEAR(sf.settings().mask_intensity, 0.5f, 1e-6f);
    sf.set_mask_intensity(9.0f);   CHECK_NEAR(sf.settings().mask_intensity, 1.0f, 1e-6f);

    sf.set_mask_type(ScreenMaskType::CrtScanlines);
    CHECK(sf.settings().mask_type == ScreenMaskType::CrtScanlines);
    sf.set_mask_type(ScreenMaskType::Off);


    // Regression pin: the exact LUT of every profile (bit-for-bit). These values
    // were captured before MisterGamma22 was given named constants.
    {
        static const int kSpots[6] = {0, 1, 64, 128, 200, 255};
        static const LutPin pins[6] = {
            {0x16d173bdfcdae583ULL, 0x16d173bdfcdae583ULL, 0x16d173bdfcdae583ULL,
             {{0,0,0},{1,1,1},{64,64,64},{128,128,128},{200,200,200},{255,255,255}}},
            {0xe525f6dbc4c54667ULL, 0xe85fce67b6f732c5ULL, 0x1dde41b35f275cbbULL,
             {{8,10,13},{8,10,13},{40,42,42},{97,97,93},{178,175,165},{250,245,230}}},
            {0xbc3b5d138617efeeULL, 0x843e685d486858b9ULL, 0x3c1cd4cccfd96784ULL,
             {{15,15,20},{15,15,20},{37,38,45},{85,88,99},{164,171,189},{240,250,255}}},
            {0x60e61b7837e66e0eULL, 0xdc966a4be4ad8108ULL, 0x7ba29bab7092d893ULL,
             {{0,0,0},{0,0,0},{12,12,12},{57,56,55},{152,149,146},{255,255,250}}},
            {0x433eb3d264d106f8ULL, 0x433eb3d264d106f8ULL, 0x433eb3d264d106f8ULL,
             {{0,0,0},{5,5,5},{93,93,93},{154,154,154},{214,214,214},{255,255,255}}},
            {0x16d173bdfcdae583ULL, 0x16d173bdfcdae583ULL, 0x16d173bdfcdae583ULL,
             {{0,0,0},{1,1,1},{64,64,64},{128,128,128},{200,200,200},{255,255,255}}},
        };
        ScreenFilters fresh;                       // independent of the singleton
        for (int p = 0; p < 6; ++p) {
            uint8_t lut[256][3];
            lut_of(fresh, static_cast<ColorProfile>(p), lut);
            CHECK_EQ(fnv(lut, 0), pins[p].r);
            CHECK_EQ(fnv(lut, 1), pins[p].g);
            CHECK_EQ(fnv(lut, 2), pins[p].b);
            for (int k = 0; k < 6; ++k)
                for (int c = 0; c < 3; ++c)
                    CHECK_EQ(int(lut[kSpots[k]][c]), int(pins[p].spot[k][c]));
        }

        // MisterGamma22 is deliberately an identity mapping (target gamma 2.2 on an
        // assumed 2.2 display => exponent 1.0), i.e. equal to the Raw response.
        uint8_t raw[256][3], g22[256][3], g16[256][3];
        lut_of(fresh, ColorProfile::Raw, raw);
        lut_of(fresh, ColorProfile::MisterGamma22, g22);
        lut_of(fresh, ColorProfile::MisterGamma16, g16);
        bool ident = true, brighter = true;
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 3; ++c) {
                if (g22[i][c] != i || g22[i][c] != raw[i][c]) ident = false;
                if (i > 0 && i < 255 && g16[i][c] < i) brighter = false;
            }
        CHECK(ident);
        CHECK(brighter);   // gamma 1.6 target brightens mid-tones; 2.2 does not
        // The profile switch itself is real even though the 2.2 LUT is identity.
        fresh.set_color_profile(ColorProfile::MisterGamma22);
        CHECK(fresh.settings().color_profile == ColorProfile::MisterGamma22);
    }

    return testh::finish("screen_filters");
}
