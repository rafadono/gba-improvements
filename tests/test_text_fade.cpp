// Screen-fade maths of the text engine (pure functions, no SDL).
#include "test_harness.h"
#include "gbaimp/text_fade.h"

#include <cmath>

using namespace gbaimp::text;

int main() {
    // BLDCNT effect 2 / 3 with the OBJ layer (bit 4) as first target; BLDY clamps at 16
    FadeState f = fade_from_regs(0x0090, 0, 8, true);   // effect 2, OBJ
    CHECK(f.mode == FadeMode::Brighten && f.level == 0.5f);
    f = fade_from_regs(0x00D0, 0, 16, true);            // effect 3
    CHECK(f.mode == FadeMode::Darken && f.level == 1.0f);
    f = fade_from_regs(0x00D0, 0, 31, true);
    CHECK(f.mode == FadeMode::Darken && f.level == 1.0f);   // clamped like the PPU
    CHECK(fade_from_regs(0x00D0, 0, 0, true).mode == FadeMode::None);
    CHECK(fade_from_regs(0x00CF, 0, 16, true).mode == FadeMode::None);   // BG-only first targets: the text OBJ is not faded
    CHECK(fade_from_regs(0x0010, 0, 16, true).mode == FadeMode::None);   // effect 0
    f = fade_from_regs(0x0050, 0x0808, 0, true);   // alpha, OBJ first target, EVA 8
    CHECK(f.mode == FadeMode::Alpha && f.level == 0.5f);
    CHECK(fade_from_regs(0x0050, 0x0010, 0, true).mode == FadeMode::None);   // EVA 16: opaque
    CHECK(fade_from_regs(0x0F40, 0x0A06, 0, true).mode == FadeMode::None);   // OBJ not a first target

    // the gbarecomp PPU ignores BLDCNT for normal OBJ: the default mapping is inert
    CHECK(!kPpuBlendsNormalObj);
    CHECK(fade_from_regs(0x00D0, 0, 16).mode == FadeMode::None && fade_from_regs(0x0090, 0, 8).mode == FadeMode::None);

    // colours
    CHECK(fade_rgb(0x102030u, FadeMode::Darken, 1.0f) == 0u);
    CHECK(fade_rgb(0x102030u, FadeMode::Brighten, 1.0f) == 0xFFFFFFu);
    CHECK(fade_rgb(0xFF8000u, FadeMode::Darken, 0.5f) == 0x804000u);
    CHECK(fade_rgb(0x000000u, FadeMode::Brighten, 0.5f) == 0x808080u);
    CHECK(fade_rgb(0x123456u, FadeMode::None, 0.7f) == 0x123456u);
    CHECK(fade_rgb(0x123456u, FadeMode::Alpha, 0.7f) == 0x123456u);

    // palette fades seen through the native text colours
    const uint32_t body = 0x632100FFu, shadow = 0xD09838FFu;
    CHECK(fade_from_text_colours(body, shadow, body, shadow).mode == FadeMode::None);
    auto scale = [](uint32_t c, float k) {
        auto ch = [&](int s) { return static_cast<uint32_t>(((c >> s) & 0xFF) * k + 0.5f) << s; };
        return ch(24) | ch(16) | ch(8) | 0xFFu;
    };
    f = fade_from_text_colours(body, shadow, scale(body, 0.5f), scale(shadow, 0.5f));
    CHECK(f.mode == FadeMode::Darken && std::fabs(f.level - 0.5f) < 0.06f);
    f = fade_from_text_colours(body, shadow, 0x000000FFu, 0x000000FFu);
    CHECK(f.mode == FadeMode::Darken && f.level > 0.95f);
    f = fade_from_text_colours(body, shadow, 0xFFFFFFFFu, 0xFFFFFFFFu);
    CHECK(f.mode == FadeMode::Brighten && f.level > 0.95f);
    CHECK(fade_from_text_colours(body, shadow, 0xFFFFFFFFu, scale(shadow, 0.5f)).mode == FadeMode::None);   // inconsistent
    CHECK(fade_from_text_colours(0, 0, body, shadow).mode == FadeMode::None);                               // unknown reference

    return testh::finish("text_fade");
}
