#include "test_harness.h"
#include "gbaimp/xbrz_filter.h"
#include <vector>

using namespace gbaimp;

int main() {
    auto& x = XbrzUpscaler::instance();
    const int W = 7, H = 5;

    // Default state: disabled.
    CHECK(!x.is_enabled());
    CHECK(x.scale() == XbrzScale::None);

    // Pseudo-random source image.
    std::vector<uint32_t> img(W * H);
    uint32_t seed = 12345;
    for (auto& p : img) { seed = seed * 1664525u + 1013904223u; p = 0xFF000000u | (seed & 0x00FFFFFFu); }

    // None: copy through, same dimensions.
    {
        std::vector<uint32_t> out; int ow = 0, oh = 0;
        x.set_scale(XbrzScale::None);
        x.process(img.data(), W, H, out, ow, oh);
        CHECK_EQ(ow, W); CHECK_EQ(oh, H);
        CHECK(out == img);
    }

    const XbrzScale scales[] = {XbrzScale::Xbrz2x, XbrzScale::Xbrz3x, XbrzScale::Xbrz4x, XbrzScale::Xbrz5x};
    for (XbrzScale s : scales) {
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
    x.set_scale(XbrzScale::None);
    return testh::finish("xbrz");
}
