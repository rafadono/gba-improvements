// Page layout of the text engine (see gbaimp/text_layout.h).
#include "gbaimp/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gbaimp {
namespace text {

namespace {

enum TokKind : uint8_t { kGlyph = 0, kSpace = 1, kNewline = 2, kColor = 3 };

struct Tok {
    uint8_t kind = kGlyph;
    uint8_t colour = 0;
    uint16_t face = 0;
    uint32_t cp = 0;
    int gi = 0;
    int entry = -1;
    int nl_line = 0;     // line by newline words
    int gl_line = -1;    // line by the source glyph positions (-1 = unknown)
    int eline = 0;       // chosen line structure
    float gx = 0.0f;     // source pen x of the glyph cell this token belongs to (source px, valid when has_gx)
    bool has_gx = false;
};

struct Word {
    int tb = 0, te = 0;  // token range (glyph / colour tokens, no spaces)
    int gap = 1;         // source space tokens before the word
    int eline = 0;
    bool pb = false;     // paragraph break (blank line) before the word
    float w = 0.0f;
    float gx = 0.0f;     // source x of the word's first glyph (valid when has_gx)
    bool has_gx = false;
};

struct Range { int w0, w1; };   // words [w0, w1)

float quant32(float v) { return std::round(v * 32.0f) / 32.0f; }

struct Builder {
    const TextPage& page;
    const TextSettings& st;
    float sx, sy;
    const FontFace* primary = nullptr;
    const FontFace* fallback = nullptr;
    std::vector<Tok> toks;
    std::vector<Word> words;
    int source_lines = 1;
    int entry_count = 0;
    bool use_gl_ = false;   // line structure comes from the source glyph positions (so per-line indents are meaningful)
    const char* fail = "";

    Builder(const TextPage& p, const TextSettings& s, float x, float y) : page(p), st(s), sx(x), sy(y) {}

    // -- tokenisation --------------------------------------------------------------------------
    bool resolve(uint32_t cp, Tok* out, int& n) {
        n = 0;
        auto put = [&](const FontFace* f, uint32_t c, int gi) {
            Tok t;
            t.kind = kGlyph;
            t.cp = c;
            t.gi = gi;
            t.face = static_cast<uint16_t>(f->id);
            out[n++] = t;
        };
        const FontFace* faces[2] = {primary, fallback};
        for (int k = 0; k < 2; ++k) {
            if (!faces[k]) continue;
            const FontFace* f = faces[k];
            int gi = f->glyph_index(cp);
            if (gi) { put(f, cp, gi); return true; }
            uint32_t sub = 0;
            switch (cp) {
                case 0x2018: case 0x2019: case 0x201A: sub = '\''; break;
                case 0x201C: case 0x201D: case 0x201E: sub = '"'; break;
                case 0x2013: case 0x2014: sub = '-'; break;
                case 0x2022: sub = '*'; break;
                default: break;
            }
            if (sub) {
                gi = f->glyph_index(sub);
                if (gi) { put(f, sub, gi); return true; }
            }
            if (cp == 0x2026) {
                gi = f->glyph_index('.');
                if (gi) { put(f, '.', gi); put(f, '.', gi); put(f, '.', gi); return true; }
            }
        }
        return false;
    }

    bool tokenise() {
        int nl_line = 0;
        int last_gl = 0;
        int last_entry_line = -1;
        int max_entry = -1;
        bool gl_monotone = true, have_gl = false;
        for (const TextChar& ch : page.chars) if (ch.has_pos) { have_gl = true; break; }
        for (const TextChar& ch : page.chars) {
            Tok base;
            base.nl_line = nl_line;
            if (ch.cp == 0x0Au) { base.kind = kNewline; base.gl_line = last_gl; toks.push_back(base); ++nl_line; continue; }
            const bool is_space = (ch.cp == 0x20u || ch.cp == 0xA0u);
            const int entry = ch.entry >= 0 ? ch.entry : -1;
            if (entry > max_entry) max_entry = entry;
            int gl = last_gl;
            if (have_gl && ch.has_pos) {
                const float dy = ch.gy - page.origin_y;
                const int pitch = page.line_pitch > 0 ? page.line_pitch : 12;
                gl = std::max(0, static_cast<int>(std::lround(dy / pitch)));
                if (gl < last_entry_line) gl_monotone = false;
                last_entry_line = std::max(last_entry_line, gl);
            }
            last_gl = gl;
            if (is_space) {
                base.kind = kSpace;
                base.entry = entry;
                base.gl_line = gl;
                toks.push_back(base);
                continue;
            }
            if (!ch.cp) { fail = "unsupported character"; return false; }
            Tok parts[3];
            int n = 0;
            if (!resolve(ch.cp, parts, n)) { fail = "glyph missing"; return false; }
            for (int i = 0; i < n; ++i) {
                Tok t = parts[i];
                t.kind = kGlyph;
                t.colour = ch.colour ? 1 : 0;
                t.entry = entry;
                t.nl_line = nl_line;
                t.gl_line = gl;
                if (have_gl && ch.has_pos) { t.gx = ch.gx; t.has_gx = true; }
                toks.push_back(t);
            }
        }
        entry_count = max_entry + 1;
        // Line structure: source glyph positions when consistent, else the newline words.
        const bool use_gl = have_gl && gl_monotone;
        use_gl_ = use_gl;
        int max_line = 0;
        for (Tok& t : toks) {
            t.eline = use_gl ? t.gl_line : t.nl_line;
            max_line = std::max(max_line, t.eline);
        }
        source_lines = max_line + 1;
        return true;
    }

    // -- words ----------------------------------------------------------------------------------
    void make_words() {
        words.clear();
        const int n = static_cast<int>(toks.size());
        int i = 0, gap = 0, nls = 0;
        bool first = true;
        while (i < n) {
            const Tok& t = toks[i];
            if (t.kind == kSpace) { ++gap; ++i; continue; }
            if (t.kind == kNewline) { ++nls; ++i; continue; }
            // glyph (colour toggles are folded into the glyph colour, they are not tokens)
            Word w;
            w.tb = i;
            while (i < n && toks[i].kind == kGlyph) ++i;
            w.te = i;
            w.gap = first ? 0 : std::max(1, gap);
            w.eline = toks[w.tb].eline;
            w.gx = toks[w.tb].gx;
            w.has_gx = toks[w.tb].has_gx;
            w.pb = !first && nls >= 2;
            first = false;
            gap = nls = 0;
            words.push_back(w);
        }
    }

    // -- one layout attempt at a given em -------------------------------------------------------
    struct FS { int id; FaceScale sc; const FontFace* f; };

    bool attempt(float em_src, PageLayout& out) {
        out = PageLayout();
        out.sx = sx;
        out.sy = sy;
        out.em_src = em_src;
        out.entry_count = entry_count;
        out.source_lines = source_lines;
        const float em_px = quant32(em_src * sx);
        out.em_px = em_px;
        if (em_px < 2.0f) { out.reason = "size too small"; return false; }

        FS fs[2];
        int nfs = 0;
        for (const FontFace* f : {primary, fallback}) {
            if (!f) continue;
            bool dup = false;
            for (int i = 0; i < nfs; ++i) if (fs[i].id == f->id) dup = true;
            if (dup) continue;
            fs[nfs++] = {f->id, f->scale_for(em_px), f};
        }
        auto scale_of = [&](int id) -> const FS& {
            for (int i = 0; i < nfs; ++i) if (fs[i].id == id) return fs[i];
            return fs[0];
        };
        const FS& pf = scale_of(primary->id);
        out.xheight_px = pf.sc.xheight_px;

        const float letter = st.letter_spacing * sx;
        const int space_gi = primary->glyph_index(' ');
        const float space_adv = primary->advance(space_gi) * pf.sc.sx + letter;

        // advances (+ kerning inside a word)
        adv_.assign(toks.size(), 0.0f);
        for (size_t i = 0; i < toks.size(); ++i) {
            const Tok& t = toks[i];
            if (t.kind != kGlyph) continue;
            const FS& f = scale_of(t.face);
            float a = f.f->advance(t.gi) * f.sc.sx + letter;
            if (i + 1 < toks.size() && toks[i + 1].kind == kGlyph && toks[i + 1].face == t.face)
                a += f.f->kern(t.gi, toks[i + 1].gi) * f.sc.sx;
            adv_[i] = a;
        }
        for (Word& w : words) {
            w.w = 0.0f;
            for (int i = w.tb; i < w.te; ++i) w.w += adv_[i];
        }

        // geometry of the text area (GBA px -> output px)
        const float pad = std::max(0.0f, st.box_padding);
        const int wrap_px = page.wrap_px > 0 ? page.wrap_px : 155;
        const int lp = page.line_pitch > 0 ? page.line_pitch : 12;
        const int ml = page.max_lines > 0 ? page.max_lines : 3;
        float area_l = page.origin_x + pad;
        float area_r = page.origin_x + wrap_px - pad;
        float area_t = page.origin_y + pad;
        float area_b = page.origin_y + (ml - 1) * lp + page.cell_height - pad;
        if (page.box_w > 0.0f && page.box_h > 0.0f && page.origin_x >= page.box_x && page.origin_x <= page.box_x + page.box_w &&
            page.origin_y >= page.box_y && page.origin_y <= page.box_y + page.box_h) {
            area_r = std::min(area_r, page.box_x + page.box_w - pad);
            area_b = std::min(area_b, page.box_y + page.box_h - pad);
        }
        float ext_l = 0, ext_t = 0, ext_r = 0, ext_b = 0;
        if (st.style == TextStyle::Shadow) {
            ext_r = ext_b = std::max(0.0f, st.shadow_offset) * sx;
        } else if (st.style == TextStyle::Outline) {
            ext_l = ext_t = ext_r = ext_b = std::max(0.0f, st.outline_width) * sx;
        }
        out.area_l = area_l * sx;
        out.area_r = area_r * sx;
        out.area_t = area_t * sy;
        out.area_b = area_b * sy;
        const float max_w = (area_r - area_l) * sx - ext_r - ext_l;
        if (max_w <= 0.0f) { out.reason = "no room"; return false; }

        // lines
        lines_.clear();
        const int nw = static_cast<int>(words.size());
        const bool keep = st.wrap == WrapMode::KeepSourceBreaks;
        auto gap_w = [&](const Word& w) { return (keep ? std::max(1, w.gap) : 1) * space_adv; };
        auto span_w = [&](int w0, int w1) {
            float tot = 0;
            for (int k = w0; k < w1; ++k) tot += words[k].w + (k > w0 ? gap_w(words[k]) : 0.0f);
            return tot;
        };
        if (keep) {
            int start = 0;
            for (int k = 1; k <= nw; ++k)
                if (k == nw || words[k].eline != words[k - 1].eline) { lines_.push_back({start, k}); start = k; }
        } else {
            int para_start = 0;
            for (int k = 0; k <= nw; ++k) {
                if (k < nw && !(k > para_start && words[k].pb)) continue;
                // wrap words [para_start, k)
                const size_t first_line = lines_.size();
                int ls = para_start;
                for (int j = para_start; j < k; ++j) {
                    if (j > ls && span_w(ls, j + 1) > max_w) { lines_.push_back({ls, j}); ls = j; }
                }
                if (ls < k) lines_.push_back({ls, k});
                // orphan: a single-word last line while the previous line can give its last word away
                if (lines_.size() - first_line >= 2) {
                    Range& last = lines_.back();
                    Range& prev = lines_[lines_.size() - 2];
                    if (last.w1 - last.w0 == 1 && prev.w1 - prev.w0 >= 2) {
                        const int moved = prev.w1 - 1;
                        if (span_w(moved, last.w1) <= max_w) { prev.w1 = moved; last.w0 = moved; }
                    }
                }
                para_start = k;
            }
        }
        const int N = static_cast<int>(lines_.size());
        // Keep mode: a line the source indented (centred boxes pad lines with spaces) starts at the
        // source's own x of its first glyph, relative to the page origin. Zero for every normal left-aligned page.
        auto indent_of = [&](const Range& r) -> float {
            if (!keep || !use_gl_ || r.w0 >= nw || !words[r.w0].has_gx) return 0.0f;
            const float d = words[r.w0].gx - page.origin_x;
            return d > 0.0f ? std::min(d, static_cast<float>(wrap_px)) * sx : 0.0f;
        };
        bool fits = true;
        for (const Range& r : lines_)
            if (span_w(r.w0, r.w1) + indent_of(r) > max_w + 0.01f) { fits = false; out.reason = "line too wide"; }

        // vertical metrics
        const int pitch_px = std::max(1, static_cast<int>(std::lround(em_src * st.line_spacing * sy)));
        out.pitch_px = pitch_px;
        float asc0 = 0.0f, desc_last = 0.0f;
        for (int li = 0; li < N; ++li) {
            for (int k = lines_[li].w0; k < lines_[li].w1; ++k)
                for (int i = words[k].tb; i < words[k].te; ++i) {
                    const Tok& t = toks[i];
                    float ymin, ymax;
                    if (!scale_of(t.face).f->glyph_extent(t.gi, ymin, ymax)) continue;
                    const FaceScale& s = scale_of(t.face).sc;
                    if (li == 0) asc0 = std::max(asc0, ymax * s.sy);
                    if (li == N - 1) desc_last = std::max(desc_last, -ymin * s.sy);
                }
        }
        if (N == 0) { out.ok = true; out.shrunk = false; return true; }
        const float min_b = area_t * sy + ext_t + asc0;
        const float max_b = area_b * sy - ext_b - desc_last - (N - 1) * pitch_px;
        float pref = std::round((page.origin_y + page.baseline_row) * sy);
        if (!keep && N < source_lines) {
            const float eb0 = (page.origin_y + page.baseline_row) * sy;
            const float top = eb0 - 7.0f * sy;
            const float bot = (page.origin_y + page.baseline_row + (source_lines - 1) * lp) * sy + 3.0f * sy;
            const float h = asc0 + (N - 1) * pitch_px + desc_last;
            pref = std::round((top + bot) * 0.5f - h * 0.5f + asc0);
        }
        const float lo = std::ceil(min_b - 0.001f), hi = std::floor(max_b + 0.001f);
        int b0;
        if (lo > hi) {
            fits = false;
            if (out.reason[0] == 0) out.reason = "page too tall";
            b0 = static_cast<int>(std::lround(pref));
        } else {
            b0 = static_cast<int>(std::lround(std::min(std::max(pref, lo), hi)));
        }

        // place
        const float left = area_l * sx;
        out.items.reserve(toks.size());
        for (int li = 0; li < N; ++li) {
            LayoutLine L;
            L.first = static_cast<int>(out.items.size());
            L.baseline = b0 + li * pitch_px;
            L.x0 = left + indent_of(lines_[li]);
            float pen = L.x0;
            for (int k = lines_[li].w0; k < lines_[li].w1; ++k) {
                if (k > lines_[li].w0) pen += gap_w(words[k]);
                for (int i = words[k].tb; i < words[k].te; ++i) {
                    const Tok& t = toks[i];
                    LayoutItem it;
                    it.cp = t.cp;
                    it.gi = t.gi;
                    it.face = t.face;
                    it.colour = t.colour;
                    it.line = li;
                    it.entry = t.entry;
                    it.x = pen;
                    out.items.push_back(it);
                    pen += adv_[i];
                }
                ++L.words;
            }
            L.x1 = pen;
            L.count = static_cast<int>(out.items.size()) - L.first;
            out.lines.push_back(L);
        }
        out.ink_t = static_cast<float>(b0) - asc0;
        out.ink_b = static_cast<float>(b0 + (N - 1) * pitch_px) + desc_last;
        out.ok = fits;
        return fits;
    }

    std::vector<float> adv_;
    std::vector<Range> lines_;
};

} // namespace

bool layout_page(const TextPage& page, const TextSettings& st, float sx, float sy, PageLayout& out, FontLibrary& lib) {
    out = PageLayout();
    out.reason = "invalid page";
    if (!page.valid || sx <= 0.0f || sy <= 0.0f) return false;
    const FontFace* primary = lib.face(st.family);
    if (!primary) primary = lib.face(0);
    if (!primary) { out.reason = "no fonts"; return false; }
    Builder b(page, st, sx, sy);
    b.primary = primary;
    b.fallback = lib.face(0);
    if (!b.tokenise()) { out.reason = b.fail; return false; }
    b.make_words();

    const float size = std::min(60.0f, std::max(2.0f, st.size_em));
    if (!st.auto_fit) {
        PageLayout tmp;
        b.attempt(size, tmp);
        // Auto-fit off: the user's size is used as is, even when it overflows the area.
        tmp.ok = std::strcmp(tmp.reason, "size too small") != 0 && std::strcmp(tmp.reason, "no room") != 0;
        out = std::move(tmp);
        out.shrunk = false;
        if (out.ok) out.reason = "";
        return out.ok;
    }
    const float lo = std::min(size, std::max(2.0f, st.min_size_em));
    float em = size;
    for (;;) {
        PageLayout tmp;
        if (b.attempt(em, tmp)) {
            out = std::move(tmp);
            out.shrunk = em < size - 1e-4f;
            out.reason = "";
            return true;
        }
        if (em <= lo + 1e-4f) { out = std::move(tmp); out.ok = false; return false; }
        em = std::max(lo, em - 0.25f);
    }
}

uint64_t layout_key(const TextPage& p, const TextSettings& s, int dst_w, int dst_h, uint64_t font_generation) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    auto mixf = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); mix(u); };
    mix(p.page_id);
    mix(p.chars.size());
    for (const TextChar& ch : p.chars) { mix(ch.cp); mix(ch.colour); mix(static_cast<uint64_t>(static_cast<int64_t>(ch.entry))); mix(ch.has_pos ? 1 : 0); mixf(ch.gx); mixf(ch.gy); }
    mixf(p.origin_x); mixf(p.origin_y);
    mix(static_cast<uint64_t>(p.wrap_px)); mix(static_cast<uint64_t>(p.line_pitch)); mix(static_cast<uint64_t>(p.max_lines));
    mix(static_cast<uint64_t>(p.cell_height)); mix(static_cast<uint64_t>(p.baseline_row));
    mixf(p.box_x); mixf(p.box_y); mixf(p.box_w); mixf(p.box_h);
    mix(static_cast<uint64_t>(s.family)); mixf(s.size_em); mixf(s.line_spacing); mixf(s.letter_spacing);
    mix(static_cast<uint64_t>(s.wrap)); mix(static_cast<uint64_t>(s.style)); mixf(s.shadow_offset); mixf(s.outline_width);
    mixf(s.box_padding); mix(s.auto_fit ? 1 : 0); mixf(s.min_size_em);
    mix(static_cast<uint64_t>(dst_w)); mix(static_cast<uint64_t>(dst_h));
    mix(font_generation);
    return h;
}

} // namespace text
} // namespace gbaimp
