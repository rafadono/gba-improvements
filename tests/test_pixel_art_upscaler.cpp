#include "test_harness.h"
#include "gbaimp/pixel_art_upscaler.h"
#include "gbaimp/xbrz_filter.h"   // deprecated compatibility aliases
#include <type_traits>
#include <vector>

using namespace gbaimp;

// Deprecated names still compile and mean the same thing.
static_assert(std::is_same<XbrzUpscaler, PixelArtUpscaler>::value, "alias");
static_assert(std::is_same<XbrzScale, UpscaleScale>::value, "alias");
static_assert(int(XbrzScale::None) == 1 && int(XbrzScale::Xbrz2x) == 2 && int(XbrzScale::Xbrz3x) == 3 &&
              int(XbrzScale::Xbrz4x) == 4 && int(XbrzScale::Xbrz5x) == 5, "legacy values");
static_assert(XbrzScale::Xbrz4x == UpscaleScale::X4, "legacy enumerators map to the new ones");

int main() {
    auto& x = PixelArtUpscaler::instance();
    const int W = 7, H = 5;

    // Default state: disabled.
    CHECK(!x.is_enabled());
    CHECK(x.scale() == UpscaleScale::None);

    // Pseudo-random source image.
    std::vector<uint32_t> img(W * H);
    uint32_t seed = 12345;
    for (auto& p : img) { seed = seed * 1664525u + 1013904223u; p = 0xFF000000u | (seed & 0x00FFFFFFu); }

    // None: copy through, same dimensions.
    {
        std::vector<uint32_t> out; int ow = 0, oh = 0;
        x.set_scale(UpscaleScale::None);
        x.process(img.data(), W, H, out, ow, oh);
        CHECK_EQ(ow, W); CHECK_EQ(oh, H);
        CHECK(out == img);
    }

    const UpscaleScale scales[] = {UpscaleScale::X2, UpscaleScale::X3, UpscaleScale::X4, UpscaleScale::X5};
    for (UpscaleScale s : scales) {
        const int f = static_cast<int>(s);
        x.set_scale(s);
        CHECK(x.is_enabled());
        CHECK(x.scale() == s);

        // Output dimensions.
        std::vector<uint32_t> out; int ow = 0, oh = 0;
        x.process(img.data(), W, H, out, ow, oh);
        CHECK_EQ(ow, W * f);
        CHECK_EQ(oh, H * f);
        CHECK_EQ(out.size(), static_cast<size_t>(W * f * H * f));

        // Flat color stays flat (including alpha).
        std::vector<uint32_t> flat(W * H, 0xFF336699u);
        x.process(flat.data(), W, H, out, ow, oh);
        bool all_same = true;
        for (uint32_t p : out) if (p != 0xFF336699u) { all_same = false; break; }
        CHECK(all_same);

        // 1x1 input is handled.
        uint32_t one = 0xFF102030u;
        x.process(&one, 1, 1, out, ow, oh);
        CHECK_EQ(ow, f); CHECK_EQ(oh, f);
        bool one_same = true;
        for (uint32_t p : out) if (p != one) one_same = false;
        CHECK(one_same);

        // A two-color checkerboard keeps full alpha everywhere.
        std::vector<uint32_t> cb(W * H);
        for (int yy = 0; yy < H; ++yy)
            for (int xx = 0; xx < W; ++xx)
                cb[yy * W + xx] = ((xx + yy) & 1) ? 0xFF000000u : 0xFFFFFFFFu;
        x.process(cb.data(), W, H, out, ow, oh);
        bool opaque = true;
        for (uint32_t p : out) if ((p >> 24) != 0xFF) opaque = false;
        CHECK(opaque);
    }
    // Invalid input yields an empty result for every scale, never UB.
    {
        const UpscaleScale all[] = {UpscaleScale::None, UpscaleScale::X2, UpscaleScale::X3,
                                 UpscaleScale::X4, UpscaleScale::X5};
        std::vector<uint32_t> px(W * H, 0xFF336699u);
        for (UpscaleScale sc : all) {
            x.set_scale(sc);
            std::vector<uint32_t> out(17, 7u);
            int ow = 99, oh = 99;
            x.process(nullptr, W, H, out, ow, oh);            // null source
            CHECK(out.empty()); CHECK_EQ(ow, 0); CHECK_EQ(oh, 0);

            out.assign(5, 1u); ow = oh = 99;
            x.process(px.data(), 0, H, out, ow, oh);          // zero width
            CHECK(out.empty()); CHECK_EQ(ow, 0); CHECK_EQ(oh, 0);

            out.assign(5, 1u); ow = oh = 99;
            x.process(px.data(), W, -3, out, ow, oh);         // negative height
            CHECK(out.empty()); CHECK_EQ(ow, 0); CHECK_EQ(oh, 0);

            out.assign(5, 1u); ow = oh = 99;
            x.process(px.data(), 50000, 50000, out, ow, oh);  // pixel count exceeds int indexing (never read)
            CHECK(out.empty()); CHECK_EQ(ow, 0); CHECK_EQ(oh, 0);
        }
        // Valid input still works afterwards.
        x.set_scale(UpscaleScale::None);
        std::vector<uint32_t> out; int ow = 0, oh = 0;
        x.process(px.data(), W, H, out, ow, oh);
        CHECK_EQ(ow, W); CHECK_EQ(oh, H); CHECK(out == px);
    }
    // Every output pixel is written for every scale and a range of small sizes (including
    // 1-pixel-wide/high images): pre-fill the destination with a sentinel that cannot occur in
    // the source and check that none survives. Also exercises the border handling.
    {
        const uint32_t kSentinel = 0x12345678u;
        std::vector<uint32_t> out;
        for (UpscaleScale sc : scales) {
            x.set_scale(sc);
            const int f = static_cast<int>(sc);
            for (int w = 1; w <= 9; ++w) {
                for (int h = 1; h <= 9; ++h) {
                    std::vector<uint32_t> src(size_t(w) * h);
                    uint32_t sd = uint32_t(w * 131 + h * 17);
                    for (auto& p : src) { sd = sd * 1664525u + 1013904223u; p = 0xFF000000u | ((sd >> 8) & 0x00FEFEFEu); }
                    out.assign(size_t(w) * f * h * f, kSentinel);
                    int ow = 0, oh = 0;
                    x.process(src.data(), w, h, out, ow, oh);
                    CHECK_EQ(ow, w * f);
                    CHECK_EQ(oh, h * f);
                    CHECK_EQ(out.size(), size_t(w) * f * h * f);
                    bool untouched = false;
                    for (uint32_t p : out) if (p == kSentinel) { untouched = true; break; }
                    CHECK(!untouched);
                }
            }
        }
        x.set_scale(UpscaleScale::None);
    }

    // Legacy API as an existing host uses it.
    {
        auto& legacy = XbrzUpscaler::instance();
        CHECK(&legacy == &PixelArtUpscaler::instance());
        legacy.set_scale(XbrzScale::Xbrz3x);
        CHECK(legacy.scale() == UpscaleScale::X3);
        CHECK(legacy.is_enabled());
        // Settings-index conversion as hosts do it: index 0 = off, 1..4 = 2x..5x.
        for (int value = 0; value <= 4; ++value) {
            XbrzScale sc = (value <= 0) ? XbrzScale::None : static_cast<XbrzScale>(value + 1);
            legacy.set_scale(sc);
            int idx = (legacy.scale() == XbrzScale::None) ? 0 : (static_cast<int>(legacy.scale()) - 1);
            CHECK_EQ(idx, value);
        }
    }
    x.set_scale(UpscaleScale::None);
    return testh::finish("pixel_art_upscaler");
}
