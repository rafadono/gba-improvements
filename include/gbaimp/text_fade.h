#pragma once
// Pure screen-fade maths for the text engine: the host decodes the PPU blend registers (BLDCNT/BLDALPHA/BLDY) or the
// native text palette into a FadeState and passes it in DrawInfo; the renderer applies it to the text colours.
// No runtime / SDL dependency.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gbaimp::text {

enum class FadeMode : int { None = 0, Alpha = 1, Brighten = 2, Darken = 3 };

// What the PPU does to the layer the text lives on (OBJ) for one scanline.
struct FadeState {
    FadeMode mode = FadeMode::None;
    float level = 0.0f;   // 0..1 strength of the effect: BLDY/16 (brighten / darken), 1 - EVA/16 (alpha, see below)
    bool operator==(const FadeState& o) const { return mode == o.mode && level == o.level; }
};

inline constexpr uint16_t kBldObjBit = 1u << 4;   // BLDCNT first-target bit of the OBJ layer

// Measured on the gbarecomp PPU: its compositor treats an OBJ pixel as a BLDCNT first target ONLY for semi-transparent OBJ
// (attr0 mode 1) and ignores BLDCNT bit 4 for normal OBJ, which is what dialogue glyphs usually are. So the register path
// is inert with that runtime (false by default); it is kept, tested, and switched on by passing obj_normal_blends = true
// (or flipping this constant) if the PPU ever follows the hardware rule (OBJ bit 4 honoured).
inline constexpr bool kPpuBlendsNormalObj = false;

// Decodes the three blend registers for the OBJ layer. Brightness (BLDCNT effect 2 / 3) applies to a pixel only when its
// layer is a first target: the text OBJ is affected iff BLDCNT bit 4 is set. Alpha (effect 1) also needs a
// second target below; it is reported as a transparency of the text (level = 1 - EVA/16) and only when the OBJ is a first
// target. (Windows and semi-transparent OBJ modes are not modelled; the text is a normal OBJ.)
inline FadeState fade_from_regs(uint16_t bldcnt, uint16_t bldalpha, uint16_t bldy, bool obj_normal_blends = kPpuBlendsNormalObj) {
    FadeState f;
    const unsigned effect = (bldcnt >> 6) & 3u;
    if (!obj_normal_blends || !(bldcnt & kBldObjBit)) return f;
    if (effect == 2u || effect == 3u) {
        const unsigned y = std::min<unsigned>(bldy & 0x1Fu, 16u);
        if (y == 0) return f;
        f.mode = effect == 2u ? FadeMode::Brighten : FadeMode::Darken;
        f.level = static_cast<float>(y) / 16.0f;
    } else if (effect == 1u) {
        const unsigned eva = std::min<unsigned>(bldalpha & 0x1Fu, 16u);
        if (eva >= 16u) return f;
        f.mode = FadeMode::Alpha;
        f.level = 1.0f - static_cast<float>(eva) / 16.0f;
    }
    return f;
}

// Applies the fade to one 0xRRGGBB colour: brighten c + (255 - c) * t, darken c * (1 - t), same as the hardware formula
// on the 8-bit expanded channels (up to rounding). Alpha mode leaves colours alone (it is carried by the alpha).
inline uint32_t fade_rgb(uint32_t rgb, FadeMode mode, float level) {
    if (mode != FadeMode::Brighten && mode != FadeMode::Darken) return rgb;
    const float t = std::min(1.0f, std::max(0.0f, level));
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        const float c = static_cast<float>((rgb >> s) & 0xFFu);
        const float v = mode == FadeMode::Brighten ? c + (255.0f - c) * t : c * (1.0f - t);
        out |= static_cast<uint32_t>(std::lround(std::min(255.0f, std::max(0.0f, v)))) << s;
    }
    return out;
}

// Palette fades: games often fade scenes by rewriting palettes (no BLDCNT involved). The game's own text colours
// (OBJ palette bank 0, entries 1 / 2, 0xRRGGBBAA) then move towards black or white with the rest of the screen. Given the
// colours the page was first seen with (`ref_*`, assumed un-faded) and the current ones, returns the equivalent fade, or
// None when they match or are inconsistent (body and shadow must agree within 0.2, so a colour-code change is no fade).
inline FadeState fade_from_text_colours(uint32_t ref_body, uint32_t ref_shadow, uint32_t cur_body, uint32_t cur_shadow) {
    auto solve = [](uint32_t ref, uint32_t cur, FadeMode* mode, float* t) -> bool {
        if (ref == 0 || cur == 0) return false;
        int sr = 0, sc = 0;
        bool le = true, ge = true;
        for (int s = 24; s >= 8; s -= 8) {
            const int r = static_cast<int>((ref >> s) & 0xFFu), c = static_cast<int>((cur >> s) & 0xFFu);
            sr += r;
            sc += c;
            if (c > r + 2) le = false;   // 5-bit quantisation tolerance
            if (c < r - 2) ge = false;
        }
        if (sr == sc) { *mode = FadeMode::None; *t = 0.0f; return true; }
        if (le && sc < sr) {
            if (sr <= 0) return false;
            *mode = FadeMode::Darken;
            *t = 1.0f - static_cast<float>(sc) / static_cast<float>(sr);
            return true;
        }
        if (ge && sc > sr) {
            if (sr >= 765) return false;
            *mode = FadeMode::Brighten;
            *t = static_cast<float>(sc - sr) / static_cast<float>(765 - sr);
            return true;
        }
        return false;
    };
    FadeMode m1 = FadeMode::None, m2 = FadeMode::None;
    float t1 = 0, t2 = 0;
    FadeState f;
    if (!solve(ref_body, cur_body, &m1, &t1) || !solve(ref_shadow, cur_shadow, &m2, &t2)) return f;
    if (m1 == FadeMode::None) { m1 = m2; t1 = t2 = (m2 == FadeMode::None ? 0.0f : t2); }
    if (m2 == FadeMode::None) { m2 = m1; }
    if (m1 != m2 || std::fabs(t1 - t2) > 0.2f) return f;
    const float t = std::min(1.0f, std::max(0.0f, 0.5f * (t1 + t2)));
    if (t < 0.02f) return f;
    f.mode = m1;
    f.level = t;
    return f;
}

}  // namespace gbaimp::text
