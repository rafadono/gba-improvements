#include "test_harness.h"
#include "gbaimp/screen_filters.h"
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

    return testh::finish("screen_filters");
}
