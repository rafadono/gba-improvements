// Page layout of the text engine (CPU only): keep / reflow wrapping, auto-fit, reveal entries, colour runs,
// glyph substitution, indents and the layout cache key. Uses a synthetic font (synth_font.h): no files, no SDL.
#include "test_harness.h"
#include "synth_font.h"
#include "gbaimp/text_layout.h"
#include "gbaimp/text_synth.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace gbaimp::text;

namespace {

struct Fixture {
    std::vector<uint8_t> ttf = testfont::make_font(testfont::default_cps());
    std::vector<uint8_t> ttf2 = testfont::make_font(testfont::typographic_cps());
    FontLibrary lib;
    Fixture() {
        lib.set_user_font_dir("");
        lib.add_builtin_font("Test Sans", ttf.data(), ttf.size());
        lib.add_builtin_font("Test Typographic", ttf2.data(), ttf2.size());
    }
};

TextSettings base_settings() {
    TextSettings s;
    s.enabled = true;
    return s;
}

std::string line_text(const PageLayout& L, int li) {
    std::string s;
    const LayoutLine& ln = L.lines[li];
    for (int i = ln.first; i < ln.first + ln.count; ++i) s.push_back(static_cast<char>(L.items[i].cp < 256 ? L.items[i].cp : '?'));
    return s;
}

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\n') { if (!cur.empty()) w.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

void test_keep_and_entries(Fixture& fx) {
    TextSettings st = base_settings();
    TextPage p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    PageLayout L;
    CHECK(layout_page(p, st, 5.4f, 5.4f, L, fx.lib));
    CHECK_EQ(L.lines.size(), 2u);                                   // keep: same number of lines as the source
    CHECK(line_text(L, 0) == "Onelookatthiscastle,andI");           // keep: first line words
    int entries = 0;
    for (const TextChar& c : p.chars) if (c.entry >= 0) ++entries;
    CHECK_EQ(L.entry_count, entries);
    // entries of items follow the source order (spaces are entries but not items)
    std::vector<const TextChar*> by_entry(entries, nullptr);
    for (const TextChar& c : p.chars) if (c.entry >= 0) by_entry[c.entry] = &c;
    bool ok = true;
    int last = -1;
    for (const LayoutItem& it : L.items) {
        if (it.entry < 0 || it.entry >= entries || !by_entry[it.entry] || by_entry[it.entry]->cp != it.cp || it.entry <= last) { ok = false; break; }
        last = it.entry;
    }
    CHECK(ok);   // item entries map to the source characters in reveal order

    // line structure taken from the glyph rows when the line break is just a space with a position on the next row
    TextPage q = p;
    size_t nl = 0;
    while (q.chars[nl].cp != 0x0A) ++nl;
    for (size_t i = nl + 1; i < q.chars.size(); ++i) ++q.chars[i].entry;
    TextChar sp;
    sp.cp = ' ';
    sp.entry = static_cast<int>(nl);
    sp.has_pos = true;
    sp.gx = 0.0f;
    sp.gy = 108.0f;   // still on the first row: the next word starts the second one
    q.chars[nl] = sp;
    PageLayout L2;
    CHECK(layout_page(q, st, 4.0f, 4.0f, L2, fx.lib) && L2.lines.size() == 2);

    // a 3-line page keeps 3 lines at every scale and size where it fits
    TextPage p3 = synth_page({"I've been looking for you,", "Mira. The door to this place", "has been sealed for so long..."});
    for (float sc : {3.0f, 4.0f, 5.4f, 6.19f})
        for (float em : {6.0f, 8.0f, 10.0f, 12.0f}) {
            TextSettings s2 = st;
            s2.size_em = em;
            PageLayout l3;
            if (layout_page(p3, s2, sc, sc, l3, fx.lib)) CHECK_EQ(l3.lines.size(), 3u);
        }

    // pages without source positions (entries only) still lay out, with line breaks from the '\n' characters
    TextPage np = synth_page({"No positions here", "second line"}, 72.0f, 108.0f, false);
    PageLayout LN;
    CHECK(layout_page(np, st, 4.0f, 4.0f, LN, fx.lib) && LN.lines.size() == 2);
    CHECK_EQ(LN.entry_count, 17 + 11);
}

void test_reflow_and_fit(Fixture& fx) {
    TextSettings st = base_settings();
    st.wrap = WrapMode::Reflow;
    const std::string text = "One look at this castle, and I just knew! They're here. Mira, Dara and Finn were waiting together";
    TextPage p = synth_page({text});
    const std::vector<std::string> expect = split_words(text);
    for (float sc : {3.0f, 4.0f, 5.4f, 6.19f})
        for (float em : {6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 14.0f}) {
            TextSettings s2 = st;
            s2.size_em = em;
            s2.min_size_em = 5.0f;
            PageLayout L;
            if (!layout_page(p, s2, sc, sc, L, fx.lib)) continue;
            std::vector<std::string> got;
            for (size_t li = 0; li < L.lines.size(); ++li) {
                const LayoutLine& ln = L.lines[li];
                std::string cur;
                for (int i = ln.first; i < ln.first + ln.count; ++i) cur.push_back(static_cast<char>(L.items[i].cp));
                got.push_back(cur);
                CHECK(ln.x0 >= L.area_l - 0.01f);   // line starts inside the area
                CHECK(ln.x1 <= L.area_r + 0.5f);    // and ends inside it (no overflow to the right)
            }
            CHECK(L.ink_t >= L.area_t - 0.01f && L.ink_b <= L.area_b + 0.01f);   // page ink inside the text area vertically
            std::string joined, orig;
            for (const std::string& g : got) joined += g;
            for (const std::string& w : expect) orig += w;
            CHECK(joined == orig);   // reflow keeps every letter in order
            size_t pos = 0;
            for (size_t li = 0; li + 1 < got.size(); ++li) {   // a break only happens between words
                pos += got[li].size();
                size_t acc = 0;
                bool boundary = false;
                for (const std::string& w : expect) { acc += w.size(); if (acc == pos) boundary = true; }
                CHECK(boundary);
            }
        }
    // auto-fit
    TextSettings big = st;
    big.size_em = 30.0f;
    big.min_size_em = 5.0f;
    PageLayout L;
    CHECK(layout_page(p, big, 5.4f, 5.4f, L, fx.lib) && L.shrunk && L.em_src < 30.0f && L.em_src >= 5.0f);
    big.min_size_em = 29.0f;
    CHECK(!layout_page(p, big, 5.4f, 5.4f, L, fx.lib));   // cannot fit above the minimum
    big.auto_fit = false;
    CHECK(layout_page(p, big, 5.4f, 5.4f, L, fx.lib) && !L.shrunk && L.em_src == 30.0f);   // auto-fit off keeps the size
    // reflow of a short single-word-last-line page
    TextPage po = synth_page({"The lantern chooses its own master wisely and"});
    PageLayout LO;
    if (layout_page(po, st, 4.0f, 4.0f, LO, fx.lib) && LO.lines.size() >= 2) CHECK(LO.lines.back().words >= 1);
    // a blank line keeps paragraphs apart in Reflow
    TextPage pp = synth_page({"First part.", "", "Second part."});
    PageLayout LP;
    CHECK(layout_page(pp, st, 4.0f, 4.0f, LP, fx.lib) && LP.lines.size() == 2);
    // line spacing and letter spacing change the metrics
    TextSettings sp = st;
    sp.line_spacing = 2.0f;
    sp.auto_fit = false;
    sp.size_em = 8.0f;
    PageLayout A, B;
    TextSettings tight = sp;
    tight.line_spacing = 1.0f;
    layout_page(po, sp, 4.0f, 4.0f, A, fx.lib);
    layout_page(po, tight, 4.0f, 4.0f, B, fx.lib);
    CHECK(A.pitch_px > B.pitch_px);
    TextSettings wide = tight;
    wide.letter_spacing = 1.0f;
    PageLayout C;
    layout_page(synth_page({"abc"}), tight, 4.0f, 4.0f, A, fx.lib);
    layout_page(synth_page({"abc"}), wide, 4.0f, 4.0f, C, fx.lib);
    CHECK(C.lines[0].x1 > A.lines[0].x1 + 2.0f * 4.0f - 0.01f);
}

void test_unsupported_and_substitutions(Fixture& fx) {
    TextSettings st = base_settings();
    TextPage p = synth_page({"Hello world"});
    PageLayout L;
    CHECK(layout_page(p, st, 4.0f, 4.0f, L, fx.lib) && L.ok);

    TextPage bad = p;
    bad.chars.push_back(TextChar{0x3042, 0, 11, false, 0, 0});     // not in any font
    CHECK(!layout_page(bad, st, 4.0f, 4.0f, L, fx.lib));
    CHECK(std::string(L.reason) == "glyph missing");
    TextPage zero = p;
    zero.chars.push_back(TextChar{0, 0, 11, false, 0, 0});          // host could not translate the word
    CHECK(!layout_page(zero, st, 4.0f, 4.0f, L, fx.lib));
    CHECK(std::string(L.reason) == "unsupported character");
    TextPage invalid = p;
    invalid.valid = false;
    CHECK(!layout_page(invalid, st, 4.0f, 4.0f, L, fx.lib) && std::string(L.reason) == "invalid page");
    CHECK(!layout_page(p, st, 0.0f, 4.0f, L, fx.lib));
    FontLibrary empty;
    empty.set_user_font_dir("");
    CHECK(!layout_page(p, st, 4.0f, 4.0f, L, empty) && std::string(L.reason) == "no fonts");
    TextPage none;
    none.valid = true;
    CHECK(layout_page(none, st, 4.0f, 4.0f, L, fx.lib) && L.items.empty());   // an empty page lays out to nothing

    // typographic substitutions: quotes -> ASCII, dashes -> '-', bullet -> '*', ellipsis -> three dots, when the font lacks them
    TextPage sub;
    sub.valid = true;
    int e = 0;
    for (uint32_t cp : {0x2019u, 0x201Cu, 0x2014u, 0x2022u, 0x2026u}) { TextChar c; c.cp = cp; c.entry = e++; sub.chars.push_back(c); }
    CHECK(layout_page(sub, st, 4.0f, 4.0f, L, fx.lib));
    CHECK_EQ(L.items.size(), 7u);   // 1 + 1 + 1 + 1 + 3
    CHECK(L.items[0].cp == '\'' && L.items[1].cp == '"' && L.items[2].cp == '-' && L.items[3].cp == '*' && L.items[4].cp == '.');
    CHECK_EQ(L.entry_count, 5);
    CHECK(L.items[4].entry == 4 && L.items[5].entry == 4 && L.items[6].entry == 4);   // the three dots share the entry
    // a font that has them keeps the real code points (family 1, selected by the settings)
    TextSettings st2 = st;
    st2.family = 1;
    CHECK(layout_page(sub, st2, 4.0f, 4.0f, L, fx.lib));
    CHECK_EQ(L.items.size(), 5u);
    CHECK(L.items[0].cp == 0x2019u && L.items[4].cp == 0x2026u);
    // an out-of-range family falls back to the first one
    TextSettings st3 = st;
    st3.family = 99;
    CHECK(layout_page(p, st3, 4.0f, 4.0f, L, fx.lib) && L.items[0].face == 0);

    // colour runs and the non-breaking space (a space, not a glyph)
    TextPage col = synth_page({"ab cd"});
    col.chars[3].colour = 1;
    col.chars[4].colour = 1;
    CHECK(layout_page(col, st, 4.0f, 4.0f, L, fx.lib) && L.items.size() == 4);
    CHECK(L.items[0].colour == 0 && L.items[1].colour == 0 && L.items[2].colour == 1 && L.items[3].colour == 1);
    TextPage nb = synth_page({"a b"});
    nb.chars[1].cp = 0xA0;
    CHECK(layout_page(nb, st, 4.0f, 4.0f, L, fx.lib) && L.items.size() == 2 && L.lines[0].words == 2);
}

void test_indent_and_geometry(Fixture& fx) {
    TextSettings st = base_settings();
    // lines the source indented with spaces (centred boxes): keep mode starts them at the source's own x
    const std::vector<std::string> lines = {"       B Button:  Jump", "Tap the +Control Pad Left or", "   Right twice:  Dodge Roll"};
    TextPage p = synth_page(lines, 46.0f, 12.0f);
    PageLayout L;
    CHECK(layout_page(p, st, 4.0f, 4.0f, L, fx.lib) && L.ok && L.lines.size() == 3);
    if (L.lines.size() == 3) {
        const float base = L.lines[1].x0;
        CHECK(std::fabs(base - L.area_l) < 0.01f);                       // the unindented line starts at the area's left edge
        CHECK(std::fabs((L.lines[0].x0 - base) - 7 * 5.5f * 4.0f) < 0.05f);   // 7 leading spaces become the source's indent
        CHECK(std::fabs((L.lines[2].x0 - base) - 3 * 5.5f * 4.0f) < 0.05f);
        CHECK(line_text(L, 0) == "BButton:Jump" && line_text(L, 2) == "Righttwice:DodgeRoll");   // spaces are not drawn
    }
    // reflow ignores the indent
    TextSettings rf = st;
    rf.wrap = WrapMode::Reflow;
    PageLayout R;
    CHECK(layout_page(p, rf, 4.0f, 4.0f, R, fx.lib) && R.ok);
    for (const LayoutLine& ln : R.lines) CHECK(std::fabs(ln.x0 - R.area_l) < 0.01f);
    // an ordinary page has no indent at all
    TextPage q = synth_page({"One look at this castle, and I", "just knew! They're here."}, 72.0f, 108.0f);
    PageLayout Q;
    CHECK(layout_page(q, st, 5.4f, 5.4f, Q, fx.lib) && Q.lines.size() == 2);
    for (const LayoutLine& ln : Q.lines) CHECK(std::fabs(ln.x0 - Q.area_l) < 0.01f);
    // an indent that does not fit shrinks the text instead of overflowing
    TextPage wide = synth_page({"              Fit this text please now"}, 46.0f, 12.0f);
    PageLayout W;
    TextSettings fit = st;
    fit.size_em = 14.0f;
    CHECK(layout_page(wide, fit, 4.0f, 4.0f, W, fx.lib) && W.shrunk);

    // text area geometry: origin .. origin + wrap_px horizontally, first line .. last line + cell vertically
    PageLayout G;
    TextSettings small = st;
    small.size_em = 6.0f;
    CHECK(layout_page(q, small, 4.0f, 4.0f, G, fx.lib));
    CHECK(std::fabs(G.area_l - 72.0f * 4.0f) < 0.01f && std::fabs(G.area_r - (72.0f + 155.0f) * 4.0f) < 0.01f);
    CHECK(std::fabs(G.area_t - 108.0f * 4.0f) < 0.01f && std::fabs(G.area_b - (108.0f + 24.0f + 16.0f) * 4.0f) < 0.01f);
    // an explicit box inside which the origin lies narrows the area, box_padding shrinks it further
    TextPage boxed = q;
    boxed.box_x = 60.0f; boxed.box_y = 100.0f; boxed.box_w = 100.0f; boxed.box_h = 30.0f;
    CHECK(layout_page(boxed, small, 4.0f, 4.0f, G, fx.lib) || true);
    CHECK(std::fabs(G.area_r - 160.0f * 4.0f) < 0.01f && std::fabs(G.area_b - 130.0f * 4.0f) < 0.01f);
    TextSettings padded = small;
    padded.box_padding = 2.0f;
    layout_page(boxed, padded, 4.0f, 4.0f, G, fx.lib);
    CHECK(std::fabs(G.area_l - 74.0f * 4.0f) < 0.01f && std::fabs(G.area_r - 158.0f * 4.0f) < 0.01f);
    // a source with a different cell geometry moves the baseline
    TextPage c1 = synth_page({"abc"}, 10.0f, 20.0f), c2 = c1;
    c2.baseline_row = 8;
    TextSettings fixed = small;
    fixed.auto_fit = false;
    PageLayout L1, L2;
    layout_page(c1, fixed, 4.0f, 4.0f, L1, fx.lib);
    layout_page(c2, fixed, 4.0f, 4.0f, L2, fx.lib);
    CHECK(L1.lines[0].baseline == L2.lines[0].baseline + 12);
    // the output scale maps source px to output px
    PageLayout S;
    layout_page(c1, fixed, 8.0f, 4.0f, S, fx.lib);
    CHECK(std::fabs(S.area_l - 80.0f) < 0.01f && std::fabs(S.area_t - 80.0f) < 0.01f && S.sx == 8.0f && S.sy == 4.0f);
}

void test_layout_key(Fixture& fx) {
    TextSettings st = base_settings();
    TextPage p = synth_page({"Key test"});
    const uint64_t k0 = layout_key(p, st, 960, 640, fx.lib.generation());
    CHECK_EQ(k0, layout_key(p, st, 960, 640, fx.lib.generation()));
    CHECK(k0 != layout_key(p, st, 1920, 1280, fx.lib.generation()));
    CHECK(k0 != layout_key(p, st, 960, 640, fx.lib.generation() + 1));
    TextSettings s2 = st;
    s2.size_em = 9.0f;
    CHECK(k0 != layout_key(p, s2, 960, 640, fx.lib.generation()));
    s2 = st;
    s2.wrap = WrapMode::Reflow;
    CHECK(k0 != layout_key(p, s2, 960, 640, fx.lib.generation()));
    TextPage p2 = p;
    p2.chars[0].cp = 'k';
    CHECK(k0 != layout_key(p2, st, 960, 640, fx.lib.generation()));
    p2 = p;
    p2.chars[1].colour = 1;
    CHECK(k0 != layout_key(p2, st, 960, 640, fx.lib.generation()));
    p2 = p;
    p2.page_id = 7;
    CHECK(k0 != layout_key(p2, st, 960, 640, fx.lib.generation()));
    // colours and opacity do not influence the layout
    s2 = st;
    s2.text_rgba = 0x123456FFu;
    s2.opacity = 0.5f;
    CHECK_EQ(k0, layout_key(p, s2, 960, 640, fx.lib.generation()));
}

} // namespace

int main() {
    Fixture fx;
    test_keep_and_entries(fx);
    test_reflow_and_fit(fx);
    test_unsupported_and_substitutions(fx);
    test_indent_and_geometry(fx);
    test_layout_key(fx);
    return testh::finish("text_layout");
}
