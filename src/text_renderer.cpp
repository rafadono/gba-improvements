// SDL renderer of the text engine: page layout cache, glyph atlas (SDL texture) and drawing with SDL_RenderCopy.
// See gbaimp/text_renderer.h for the atlas ownership rules.
#include "gbaimp/text_renderer.h"

#include "gbaimp/text_fade.h"
#include "gbaimp/text_raster.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace gbaimp {
namespace text {

namespace {

constexpr int kAtlasStart = 1024;
constexpr int kAtlasMax = 4096;

struct Key {
    uint32_t face = 0;
    int32_t gi = 0;
    int32_t em_q = 0;      // em in 1/32 output px
    int16_t param_q = 0;   // shadow offset / outline radius in 1/8 output px
    uint8_t phase = 0;
    uint8_t variant = 0;
    bool operator==(const Key& o) const {
        return face == o.face && gi == o.gi && em_q == o.em_q && param_q == o.param_q && phase == o.phase && variant == o.variant;
    }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(k.face); mix(static_cast<uint32_t>(k.gi)); mix(static_cast<uint32_t>(k.em_q));
        mix(static_cast<uint16_t>(k.param_q)); mix(k.phase); mix(k.variant);
        return static_cast<size_t>(h ^ (h >> 29));
    }
};

struct Entry {
    int ax = 0, ay = 0, w = 0, h = 0;   // rectangle in the atlas
    int bx0 = 0, by0 = 0;               // offset from the pen origin on the baseline
};

struct DrawRec {
    Entry e;
    int x = 0, y = 0;                   // destination relative to the output rectangle origin
    uint8_t colour = 0;
};

} // namespace

struct TextRenderer::Impl {
    FontLibrary* lib = nullptr;
    std::mutex mu;
    // page layout cache (one page at a time)
    bool lay_valid = false;
    uint64_t lay_key = 0;
    PageLayout layout;
    // atlas
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    int aw = 0, ah = 0;
    int cx = 0, cy = 0, rowh = 0;
    std::unordered_map<Key, Entry, KeyHash> map;
    bool flush_pending = false;
    int last_dst_w = 0;
    uint64_t font_gen = 0;
    std::vector<uint8_t> staging;
    GlyphBitmap scratch;
    std::vector<DrawRec> shadow_recs, body_recs;
    // counters
    std::atomic<uint64_t> draws{0}, layout_builds{0}, glyphs_rasterized{0}, atlas_flushes{0};
};

namespace {

using Impl = TextRenderer::Impl;

// ---- atlas ---------------------------------------------------------------------------------
bool create_texture(Impl& m, SDL_Renderer* ren, int size) {
    if (m.tex) SDL_DestroyTexture(m.tex);
    m.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size, size);
    m.ren = ren;
    m.map.clear();
    m.cx = m.cy = m.rowh = 0;
    if (!m.tex) { m.aw = m.ah = 0; return false; }
    m.aw = m.ah = size;
    SDL_SetTextureBlendMode(m.tex, SDL_BLENDMODE_BLEND);
    return true;
}

void drop_texture(Impl& m, bool destroy) {
    if (m.tex && destroy) SDL_DestroyTexture(m.tex);
    m.tex = nullptr;
    m.aw = m.ah = 0;
    m.map.clear();
    m.cx = m.cy = m.rowh = 0;
}

void flush_atlas(Impl& m, bool grow) {
    m.map.clear();
    m.cx = m.cy = m.rowh = 0;
    ++m.atlas_flushes;
    if (grow && m.ren && m.aw < kAtlasMax) create_texture(m, m.ren, m.aw * 2);
}

bool ensure_atlas(Impl& m, SDL_Renderer* ren) {
    if (m.ren != ren) {
        drop_texture(m, false);   // the previous renderer may be gone together with its textures
        m.ren = ren;
    }
    if (!m.tex && !create_texture(m, ren, kAtlasStart)) return false;
    return true;
}

bool pack(Impl& m, int w, int h, int& x, int& y) {
    if (w + 2 > m.aw || h + 2 > m.ah) return false;
    if (m.cx + w + 1 > m.aw) { m.cy += m.rowh + 1; m.cx = 0; m.rowh = 0; }
    if (m.cy + h + 1 > m.ah) return false;
    x = m.cx;
    y = m.cy;
    m.cx += w + 1;
    m.rowh = std::max(m.rowh, h);
    return true;
}

// Returns false when the atlas is full. Glyphs without ink are stored as empty entries (w == 0).
bool get_entry(Impl& m, const Key& k, const FontFace& face, float em_px, float param_px, Entry& out) {
    auto it = m.map.find(k);
    if (it != m.map.end()) { out = it->second; return true; }
    Entry e;
    if (rasterize_glyph(face, k.gi, em_px, k.phase, static_cast<GlyphVariant>(k.variant), param_px, m.scratch) && !m.scratch.empty()) {
        int x, y;
        if (!pack(m, m.scratch.w, m.scratch.h, x, y)) return false;
        const size_t n = static_cast<size_t>(m.scratch.w) * m.scratch.h;
        m.staging.resize(n * 4);
        for (size_t i = 0; i < n; ++i) {
            m.staging[i * 4 + 0] = m.staging[i * 4 + 1] = m.staging[i * 4 + 2] = 255;
            m.staging[i * 4 + 3] = m.scratch.a[i];
        }
        SDL_Rect r{x, y, m.scratch.w, m.scratch.h};
        if (SDL_UpdateTexture(m.tex, &r, m.staging.data(), m.scratch.w * 4) != 0) return false;
        e.ax = x; e.ay = y; e.w = m.scratch.w; e.h = m.scratch.h; e.bx0 = m.scratch.x0; e.by0 = m.scratch.y0;
        ++m.glyphs_rasterized;
    }
    m.map.emplace(k, e);
    out = e;
    return true;
}

uint8_t a8(uint32_t rgba, float k) {
    const float a = (rgba & 0xFF) * std::min(1.0f, std::max(0.0f, k));
    return static_cast<uint8_t>(std::lround(std::min(255.0f, a)));
}

// Layout cache.
const PageLayout* get_layout(Impl& m, const TextPage& page, const TextSettings& st, const DrawInfo& info) {
    if (!page.valid || info.dst_w <= 0 || info.dst_h <= 0 || info.gba_w <= 0 || info.gba_h <= 0) return nullptr;
    FontLibrary& lib = *m.lib;
    const uint64_t gen = lib.generation();
    const uint64_t key = layout_key(page, st, info.dst_w, info.dst_h, gen ^ (static_cast<uint64_t>(info.gba_w) << 32) ^ static_cast<uint64_t>(info.gba_h));
    if (!m.lay_valid || key != m.lay_key) {
        const float sx = static_cast<float>(info.dst_w) / info.gba_w;
        const float sy = static_cast<float>(info.dst_h) / info.gba_h;
        layout_page(page, st, sx, sy, m.layout, lib);
        m.lay_key = key;
        m.lay_valid = true;
        ++m.layout_builds;
    }
    return &m.layout;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// TextRenderer
// ---------------------------------------------------------------------------------------------
TextRenderer::TextRenderer(FontLibrary* fonts) : impl_(new Impl) { impl_->lib = fonts ? fonts : &FontLibrary::instance(); }

TextRenderer::~TextRenderer() {
    Impl& m = *impl_;
    if (m.tex) SDL_DestroyTexture(m.tex);
}

TextRenderer& TextRenderer::instance() {
    static TextRenderer* r = new TextRenderer;   // intentionally leaked: the SDL renderer usually dies first
    return *r;
}

FontLibrary& TextRenderer::fonts() { return *impl_->lib; }

bool TextRenderer::can_draw(const TextPage& page, const TextSettings& st, const DrawInfo& info) {
    if (!st.enabled) return false;
    Impl& m = *impl_;
    std::lock_guard<std::mutex> lk(m.mu);
    const PageLayout* L = get_layout(m, page, st, info);
    return L && L->ok;
}

bool TextRenderer::draw(const TextPage& page, const TextSettings& st, const DrawInfo& info) {
    if (!st.enabled || !info.renderer || info.global_alpha <= 0.0f) return false;
    Impl& m = *impl_;
    std::lock_guard<std::mutex> lk(m.mu);
    const PageLayout* L = get_layout(m, page, st, info);
    if (!L || !L->ok || L->items.empty()) return false;
    const int reveal = std::min<int>(std::max(0, page.revealed), L->entry_count);
    if (reveal <= 0) return false;
    FontLibrary& lib = *m.lib;

    // Atlas housekeeping at a safe point (nothing of this frame has been queued yet).
    if (!ensure_atlas(m, info.renderer)) return false;
    if (m.flush_pending || m.last_dst_w != info.dst_w || m.font_gen != lib.generation()) {
        flush_atlas(m, false);
        m.flush_pending = false;
        m.last_dst_w = info.dst_w;
        m.font_gen = lib.generation();
    }

    const bool want_shadow = st.style == TextStyle::Shadow && st.shadow_offset > 0.0f;
    const bool want_outline = st.style == TextStyle::Outline && st.outline_width > 0.0f;
    const float shadow_px = st.shadow_offset * L->sx;
    const float outline_px = st.outline_width * L->sx;
    const int em_q = static_cast<int>(std::lround(L->em_px * 32.0f));
    const float em_px = em_q / 32.0f;

    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        ok = true;
        m.shadow_recs.clear();
        m.body_recs.clear();
        for (const LayoutItem& it : L->items) {
            if (it.entry < 0 || it.entry >= reveal) continue;
            const FontFace* f = lib.face(it.face);
            if (!f) continue;
            const float fx = std::floor(it.x);
            int phase = static_cast<int>(std::floor((it.x - fx) * kSubpixelPhases + 0.5f));
            int ix = static_cast<int>(fx);
            if (phase >= kSubpixelPhases) { phase = 0; ++ix; }
            const int baseline = L->lines[it.line].baseline;
            Key k;
            k.face = it.face;
            k.gi = it.gi;
            k.em_q = em_q;
            k.phase = static_cast<uint8_t>(phase);
            DrawRec rec;
            rec.colour = it.colour;
            rec.x = ix;
            rec.y = baseline;
            k.variant = static_cast<uint8_t>(GlyphVariant::Body);
            k.param_q = 0;
            if (!get_entry(m, k, *f, em_px, 0.0f, rec.e)) { ok = false; break; }
            if (rec.e.w > 0) m.body_recs.push_back(rec);
            if (want_shadow || want_outline) {
                k.variant = static_cast<uint8_t>(want_shadow ? GlyphVariant::Shadow : GlyphVariant::Outline);
                const float p = want_shadow ? shadow_px : outline_px;
                k.param_q = static_cast<int16_t>(std::lround(std::min(p, 4000.0f) * 8.0f));
                if (!get_entry(m, k, *f, em_px, k.param_q / 8.0f, rec.e)) { ok = false; break; }
                if (rec.e.w > 0) m.shadow_recs.push_back(rec);
            }
        }
        if (!ok) flush_atlas(m, attempt >= 1);   // before anything is queued: safe to resize or reuse the atlas
    }
    if (!ok) return false;

    const float alpha = std::min(1.0f, std::max(0.0f, st.opacity)) * std::min(1.0f, std::max(0.0f, info.global_alpha));
    bool failed = false;
    const FadeMode fmode = static_cast<FadeMode>(info.fade_mode);
    // "Match game colours": an all-zero colour setting means the colour the game itself draws this page with (TextPage::native_*
    // colours). Those colours already carry a palette fade, so a palette-derived fade is not applied to them twice.
    auto pass = [&](const std::vector<DrawRec>& recs, uint32_t rgba, bool highlight_allowed, bool matched) {
        int cur = -1;
        const bool fade_it = !(matched && info.fade_from_palette);
        // Colour-code words: the colour the game itself draws them with when the page carries it (already faded together with the
        // palette), else TextSettings::highlight_rgba.
        const bool hl_engine = page.native_highlight_rgba != 0;
        const uint32_t hl_rgb = hl_engine ? (page.native_highlight_rgba >> 8) : (st.highlight_rgba >> 8);
        const bool fade_hl = !(hl_engine && info.fade_from_palette);
        for (const DrawRec& r : recs) {
            const int col = (highlight_allowed && r.colour) ? 1 : 0;
            if (col != cur) {
                uint32_t rgb = col ? hl_rgb : (rgba >> 8);
                if (col ? fade_hl : fade_it) rgb = fade_rgb(rgb, fmode, info.fade_level);
                const uint8_t alp = a8(rgba, alpha);
                SDL_SetTextureColorMod(m.tex, static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb));
                SDL_SetTextureAlphaMod(m.tex, alp);
                cur = col;
            }
            SDL_Rect src{r.e.ax, r.e.ay, r.e.w, r.e.h};
            SDL_Rect dst{info.dst_x + r.x + r.e.bx0, info.dst_y + r.y + r.e.by0, r.e.w, r.e.h};
            if (SDL_RenderCopy(info.renderer, m.tex, &src, &dst) != 0) failed = true;
        }
    };
    const bool match_text = st.text_rgba == 0, match_shadow = st.shadow_rgba == 0;
    const uint32_t text_rgba = match_text ? (page.native_text_rgba ? page.native_text_rgba : st.fallback_text_rgba) : st.text_rgba;
    const uint32_t shadow_rgba = match_shadow ? (page.native_shadow_rgba ? page.native_shadow_rgba : st.fallback_shadow_rgba) : st.shadow_rgba;
    if (want_shadow) pass(m.shadow_recs, shadow_rgba, false, match_shadow);
    if (want_outline) pass(m.shadow_recs, st.outline_rgba, false, false);
    pass(m.body_recs, text_rgba, true, match_text);
    if (failed) m.flush_pending = true, drop_texture(m, true);   // device lost / bad texture: rebuild next frame
    ++m.draws;
    return !m.body_recs.empty();
}

void TextRenderer::invalidate() {
    Impl& m = *impl_;
    std::lock_guard<std::mutex> lk(m.mu);
    m.lay_valid = false;
    m.flush_pending = true;
}

std::vector<std::string> TextRenderer::family_names() {
    std::lock_guard<std::mutex> lk(impl_->mu);
    FontLibrary& lib = *impl_->lib;
    std::vector<std::string> v;
    for (int i = 0; i < lib.family_count(); ++i) v.push_back(lib.family_name(i));
    return v;
}

uint64_t TextRenderer::draws() const { return impl_->draws.load(); }
uint64_t TextRenderer::layout_builds() const { return impl_->layout_builds.load(); }
uint64_t TextRenderer::glyphs_rasterized() const { return impl_->glyphs_rasterized.load(); }
uint64_t TextRenderer::atlas_flushes() const { return impl_->atlas_flushes.load(); }

void TextRenderer::rescan_fonts() {
    Impl& m = *impl_;
    std::lock_guard<std::mutex> lk(m.mu);
    m.lib->rescan_user_fonts();
    m.lay_valid = false;
    m.flush_pending = true;
}

int TextRenderer::family_index(const std::string& name) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    return impl_->lib->find_family(name);
}

void TextRenderer::release_gpu() {
    Impl& m = *impl_;
    std::lock_guard<std::mutex> lk(m.mu);
    drop_texture(m, true);
    m.ren = nullptr;
    m.flush_pending = false;
}

} // namespace text
} // namespace gbaimp
