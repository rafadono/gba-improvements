// Builds a tiny valid TrueType font in memory for the text engine tests, so they need no font files (and no font licences).
// Every covered code point is a solid rectangle: 600 units wide (50 units of side bearing on both sides), 700 tall
// (capitals, digits, punctuation) or 500 tall (lowercase letters, the x-height), with descenders of 200 units for g j p q y.
// The space glyph is empty and 300 units wide. unitsPerEm 1000, ascent 800, descent -200.
#pragma once

#include <cstdint>
#include <vector>

namespace testfont {

inline void p16(std::vector<uint8_t>& b, uint32_t v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
inline void p32(std::vector<uint8_t>& b, uint32_t v) { p16(b, v >> 16); p16(b, v & 0xFFFF); }
inline void pad4(std::vector<uint8_t>& b) { while (b.size() % 4) b.push_back(0); }

struct Table { const char* tag; std::vector<uint8_t> data; };

// `cps` must be sorted ascending, unique, and contain no code point above 0xFFFE.
inline std::vector<uint8_t> make_font(const std::vector<uint32_t>& cps, bool with_kern = false) {
    const uint32_t ng = static_cast<uint32_t>(cps.size()) + 1;   // glyph 0 = .notdef (empty)
    std::vector<uint8_t> glyf, loca, hmtx;
    std::vector<uint32_t> offs;
    p16(hmtx, 500); p16(hmtx, 0);
    offs.push_back(0);   // loca[0]: start of .notdef
    offs.push_back(0);   // loca[1]: end of .notdef (empty)
    for (uint32_t cp : cps) {
        int ymin = 0, ymax = 700, adv = 600;
        if (cp == ' ') { adv = 300; }
        else if (cp >= 'a' && cp <= 'z') {
            ymax = 500;
            if (cp == 'g' || cp == 'j' || cp == 'p' || cp == 'q' || cp == 'y') ymin = -200;
        }
        if (cp == ' ') {
            // empty glyph: same offset as the next
        } else {
            const int x0 = 50, x1 = adv - 50;
            p16(glyf, 1);                                  // one contour
            p16(glyf, static_cast<uint32_t>(x0)); p16(glyf, static_cast<uint32_t>(static_cast<uint16_t>(ymin)));
            p16(glyf, static_cast<uint32_t>(x1)); p16(glyf, static_cast<uint32_t>(ymax));
            p16(glyf, 3);                                  // endPtsOfContours[0]
            p16(glyf, 0);                                  // instruction length
            for (int i = 0; i < 4; ++i) glyf.push_back(1); // on-curve, 16-bit deltas
            const int xs[4] = {x0, x1, x1, x0}, ys[4] = {ymin, ymin, ymax, ymax};
            int px = 0, py = 0;
            for (int i = 0; i < 4; ++i) { p16(glyf, static_cast<uint16_t>(xs[i] - px)); px = xs[i]; }
            for (int i = 0; i < 4; ++i) { p16(glyf, static_cast<uint16_t>(ys[i] - py)); py = ys[i]; }
            pad4(glyf);
        }
        offs.push_back(static_cast<uint32_t>(glyf.size()));
        p16(hmtx, static_cast<uint32_t>(adv)); p16(hmtx, 50);
    }
    for (uint32_t o : offs) p32(loca, o);

    std::vector<uint8_t> cmap;
    {
        const uint32_t seg = static_cast<uint32_t>(cps.size()) + 1;
        uint32_t pow2 = 1, sel = 0;
        while (pow2 * 2 <= seg) { pow2 *= 2; ++sel; }
        std::vector<uint8_t> sub;
        p16(sub, 4); p16(sub, 16 + 8 * seg); p16(sub, 0);
        p16(sub, seg * 2); p16(sub, pow2 * 2); p16(sub, sel); p16(sub, seg * 2 - pow2 * 2);
        for (uint32_t cp : cps) p16(sub, cp);
        p16(sub, 0xFFFF);
        p16(sub, 0);
        for (uint32_t cp : cps) p16(sub, cp);
        p16(sub, 0xFFFF);
        for (size_t i = 0; i < cps.size(); ++i) p16(sub, static_cast<uint16_t>((i + 1) - cps[i]));
        p16(sub, 1);
        for (uint32_t i = 0; i < seg; ++i) p16(sub, 0);
        p16(cmap, 0); p16(cmap, 1);
        p16(cmap, 3); p16(cmap, 1); p32(cmap, 12);
        cmap.insert(cmap.end(), sub.begin(), sub.end());
    }
    std::vector<uint8_t> head;
    p32(head, 0x00010000); p32(head, 0x00010000); p32(head, 0); p32(head, 0x5F0F3CF5);
    p16(head, 0); p16(head, 1000);
    for (int i = 0; i < 4; ++i) p32(head, 0);          // created / modified
    p16(head, 0); p16(head, static_cast<uint16_t>(-200)); p16(head, 700); p16(head, 800);   // bbox
    p16(head, 0); p16(head, 8); p16(head, 2);          // macStyle, lowestRecPPEM, fontDirectionHint
    p16(head, 1);                                      // indexToLocFormat: 32-bit offsets
    p16(head, 0);
    std::vector<uint8_t> hhea;
    p32(hhea, 0x00010000); p16(hhea, 800); p16(hhea, static_cast<uint16_t>(-200)); p16(hhea, 0);
    p16(hhea, 600); p16(hhea, 0); p16(hhea, 0); p16(hhea, 700);
    p16(hhea, 1); p16(hhea, 0); p16(hhea, 0);
    for (int i = 0; i < 4; ++i) p16(hhea, 0);
    p16(hhea, 0); p16(hhea, ng);
    std::vector<uint8_t> maxp;
    p32(maxp, 0x00010000); p16(maxp, ng);
    for (int i = 0; i < 13; ++i) p16(maxp, 0);

    std::vector<Table> tabs;
    tabs.push_back({"cmap", cmap});
    tabs.push_back({"glyf", glyf.empty() ? std::vector<uint8_t>(4, 0) : glyf});
    tabs.push_back({"head", head});
    tabs.push_back({"hhea", hhea});
    tabs.push_back({"hmtx", hmtx});
    if (with_kern) {
        // one pair, 'A' followed by 'V' = glyph indices of those code points, -100 units
        uint32_t ga = 0, gv = 0;
        for (size_t i = 0; i < cps.size(); ++i) { if (cps[i] == 'A') ga = static_cast<uint32_t>(i + 1); if (cps[i] == 'V') gv = static_cast<uint32_t>(i + 1); }
        std::vector<uint8_t> kern;
        p16(kern, 0); p16(kern, 1);
        p16(kern, 0); p16(kern, 14 + 6); p16(kern, 1);
        p16(kern, 1); p16(kern, 6); p16(kern, 0); p16(kern, 0);
        p16(kern, ga); p16(kern, gv); p16(kern, static_cast<uint16_t>(-100));
        tabs.push_back({"kern", kern});
    }
    tabs.push_back({"loca", loca});
    tabs.push_back({"maxp", maxp});

    std::vector<uint8_t> out;
    p32(out, 0x00010000); p16(out, static_cast<uint32_t>(tabs.size()));
    p16(out, 0); p16(out, 0); p16(out, 0);
    uint32_t off = 12 + 16 * static_cast<uint32_t>(tabs.size());
    std::vector<uint32_t> offsets;
    for (Table& t : tabs) { offsets.push_back(off); off += static_cast<uint32_t>((t.data.size() + 3) & ~size_t(3)); }
    for (size_t i = 0; i < tabs.size(); ++i) {
        out.insert(out.end(), tabs[i].tag, tabs[i].tag + 4);
        p32(out, 0); p32(out, offsets[i]); p32(out, static_cast<uint32_t>(tabs[i].data.size()));
    }
    for (Table& t : tabs) { out.insert(out.end(), t.data.begin(), t.data.end()); pad4(out); }
    return out;
}

// Printable ASCII (33..126 and the space) plus a few Latin-1 letters. No typographic quotes / dashes / ellipsis, so the
// layout's substitutions (curly quote -> ', ellipsis -> ...) are exercised.
inline std::vector<uint32_t> default_cps() {
    std::vector<uint32_t> v;
    for (uint32_t c = 32; c < 127; ++c) v.push_back(c);
    v.push_back(0xE9);
    return v;
}

// Same plus the typographic characters (a font that has them natively).
inline std::vector<uint32_t> typographic_cps() {
    std::vector<uint32_t> v = default_cps();
    for (uint32_t c : {0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2026u}) v.push_back(c);
    return v;
}

} // namespace testfont
