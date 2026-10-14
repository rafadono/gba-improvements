// Font access of the output-resolution text engine: injected built-in families, user fonts (*.ttf in a folder), metrics,
// glyph rasterisation by stb_truetype (analytic area coverage, no hinting bytecode). See gbaimp/text_fonts.h.
#include "gbaimp/text_fonts.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb/stb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace gbaimp {
namespace text {

// ---------------------------------------------------------------------------------------------
// FontFace
// ---------------------------------------------------------------------------------------------
struct FontFace::Impl {
    stbtt_fontinfo info;
    std::vector<uint8_t> owned;                    // user fonts: the file; built-ins point at static data
    const unsigned char* data = nullptr;
    bool ok = false;
    int ascii[256];                                // glyph index cache for cp < 256 (-1 = unknown)
    std::unordered_map<uint32_t, int> other;
    Impl() { std::memset(&info, 0, sizeof(info)); for (int& v : ascii) v = -1; }
};

FontFace::FontFace() : impl(new Impl) {}
FontFace::~FontFace() = default;

bool FontFace::valid() const { return impl && impl->ok; }

static bool init_face(FontFace& f, const unsigned char* data) {
    FontFace::Impl& m = *f.impl;
    m.data = data;
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0 || !stbtt_InitFont(&m.info, data, off)) return false;
    int a = 0, d = 0, g = 0;
    stbtt_GetFontVMetrics(&m.info, &a, &d, &g);
    f.ascent = static_cast<float>(a);
    f.descent = static_cast<float>(d);
    f.line_gap = static_cast<float>(g);
    const float s1 = stbtt_ScaleForMappingEmToPixels(&m.info, 1.0f);
    f.upem = s1 > 0.0f ? static_cast<int>(1.0f / s1 + 0.5f) : 1000;
    if (f.upem < 16 || f.upem > 16384) return false;
    int x0, y0, x1, y1;
    f.xheight = f.upem * 0.5f;
    f.capheight = f.upem * 0.7f;
    if (stbtt_GetCodepointBox(&m.info, 'x', &x0, &y0, &x1, &y1) && y1 > 0) f.xheight = static_cast<float>(y1);
    if (stbtt_GetCodepointBox(&m.info, 'H', &x0, &y0, &x1, &y1) && y1 > 0) f.capheight = static_cast<float>(y1);
    m.ok = true;
    return true;
}

// A glyph index handed out by a (possibly hostile) cmap is only trusted when it addresses a real glyph: stb_truetype
// range-checks most lookups, but the horizontal metrics are read unchecked.
static int lookup_glyph(const stbtt_fontinfo& info, uint32_t cp) {
    if (cp >= 0xFFFFu && cp < 0x10000u) return 0;   // U+FFFF is a non-character (and the cmap terminator segment)
    const int gi = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
    return (gi > 0 && gi < info.numGlyphs) ? gi : 0;
}

int FontFace::glyph_index(uint32_t cp) const {
    if (!valid()) return 0;
    Impl& m = *impl;
    if (cp < 256) {
        int& c = m.ascii[cp];
        if (c < 0) c = lookup_glyph(m.info, cp);
        return c;
    }
    auto it = m.other.find(cp);
    if (it != m.other.end()) return it->second;
    const int gi = lookup_glyph(m.info, cp);
    m.other.emplace(cp, gi);
    return gi;
}

float FontFace::advance(int gi) const {
    if (!valid()) return 0.0f;
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl->info, gi, &adv, &lsb);
    return static_cast<float>(adv);
}

float FontFace::kern(int g1, int g2) const {
    if (!valid() || !g1 || !g2) return 0.0f;
    return static_cast<float>(stbtt_GetGlyphKernAdvance(&impl->info, g1, g2));
}

bool FontFace::glyph_extent(int gi, float& ymin, float& ymax) const {
    if (!valid()) return false;
    int x0, y0, x1, y1;
    if (!stbtt_GetGlyphBox(&impl->info, gi, &x0, &y0, &x1, &y1)) return false;
    if (y1 <= y0) return false;
    ymin = static_cast<float>(y0);
    ymax = static_cast<float>(y1);
    return true;
}

FaceScale FontFace::scale_for(float em_px) const {
    FaceScale s;
    s.sx = em_px * size_adjust / static_cast<float>(upem);
    s.sy = s.sx;
    const float xh = xheight > 0.0f ? xheight : upem * 0.5f;
    const float xh_px = std::max(1.0f, std::round(xh * s.sx));
    s.xheight_px = static_cast<int>(xh_px);
    s.sy = xh_px / xh;   // light vertical hint: x-height becomes a whole number of output pixels
    return s;
}

bool FontFace::bitmap(int gi, const FaceScale& sc, float shift_x, int& x0, int& y0, int& w, int& h,
                      std::vector<uint8_t>& out) const {
    if (!valid() || gi <= 0) return false;
    int ix0, iy0, ix1, iy1;
    stbtt_GetGlyphBitmapBoxSubpixel(&impl->info, gi, sc.sx, sc.sy, shift_x, 0.0f, &ix0, &iy0, &ix1, &iy1);
    w = ix1 - ix0;
    h = iy1 - iy0;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) { w = h = 0; return false; }
    out.assign(static_cast<size_t>(w) * h, 0);
    stbtt_MakeGlyphBitmapSubpixel(&impl->info, out.data(), w, h, w, sc.sx, sc.sy, shift_x, 0.0f, gi);
    x0 = ix0;
    y0 = iy0;
    return true;
}

// ---------------------------------------------------------------------------------------------
// TrueType validation (user fonts)
// ---------------------------------------------------------------------------------------------
namespace {

struct Reader {
    const uint8_t* d;
    size_t n;
    bool u16(size_t o, uint32_t& v) const { if (o + 2 > n) return false; v = (d[o] << 8) | d[o + 1]; return true; }
    bool u32(size_t o, uint32_t& v) const { if (o + 4 > n) return false; v = (uint32_t(d[o]) << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]; return true; }
};

struct Table { uint32_t off = 0, len = 0; bool present = false; };

uint32_t tag_of(const char* t) { return (uint32_t(uint8_t(t[0])) << 24) | (uint8_t(t[1]) << 16) | (uint8_t(t[2]) << 8) | uint8_t(t[3]); }

// Validates one simple glyph; returns false when stb_truetype could read outside [g, g+len).
bool simple_glyph_ok(const uint8_t* g, uint32_t len, int ncont) {
    if (len < 10u + 2u * ncont + 2u) return false;
    uint32_t prev_end = 0;
    for (int i = 0; i < ncont; ++i) {
        const uint32_t e = (g[10 + 2 * i] << 8) | g[11 + 2 * i];
        if (i > 0 && e < prev_end) return false;
        prev_end = e;
    }
    const uint32_t npts = ncont ? prev_end + 1 : 0;
    uint32_t pos = 10 + 2 * ncont;
    const uint32_t ilen = (g[pos] << 8) | g[pos + 1];
    pos += 2;
    if (pos + ilen > len) return false;
    pos += ilen;
    // flags
    uint32_t xbytes = 0, ybytes = 0, i = 0;
    while (i < npts) {
        if (pos >= len) return false;
        const uint8_t f = g[pos++];
        uint32_t rep = 1;
        if (f & 8) {
            if (pos >= len) return false;
            rep += g[pos++];
        }
        if (i + rep > npts) return false;
        xbytes += rep * ((f & 2) ? 1u : ((f & 16) ? 0u : 2u));
        ybytes += rep * ((f & 4) ? 1u : ((f & 32) ? 0u : 2u));
        i += rep;
    }
    return static_cast<uint64_t>(pos) + xbytes + ybytes <= len;
}

bool glyph_ok(const Reader& rd, const Table& glyf, const std::vector<uint32_t>& loca, uint32_t gid, int depth, uint32_t nglyphs) {
    if (depth > 6) return false;
    const uint32_t a = loca[gid], b = loca[gid + 1];
    if (b == a) return true;                       // empty glyph
    if (b < a || b > glyf.len || b - a < 10) return false;
    const uint8_t* g = rd.d + glyf.off + a;
    const uint32_t len = b - a;
    const int nc = static_cast<int16_t>((g[0] << 8) | g[1]);
    if (nc >= 0) return simple_glyph_ok(g, len, nc);
    uint32_t pos = 10;
    for (int guard = 0; guard < 256; ++guard) {
        if (pos + 4 > len) return false;
        const uint32_t flags = (g[pos] << 8) | g[pos + 1];
        const uint32_t comp = (g[pos + 2] << 8) | g[pos + 3];
        pos += 4;
        if (comp >= nglyphs || comp == gid) return false;
        pos += (flags & 1) ? 4 : 2;
        if (flags & 0x8) pos += 2; else if (flags & 0x40) pos += 4; else if (flags & 0x80) pos += 8;
        if (pos > len) return false;
        if (!glyph_ok(rd, glyf, loca, comp, depth + 1, nglyphs)) return false;
        if (!(flags & 0x20)) return true;
    }
    return false;
}

// Validates every cmap subtable stb_truetype can use, so that a lookup of ANY code point stays inside the table.
bool cmap_subtable_ok(const Reader& rd, const Table& cmap, uint32_t sub) {
    // `sub` = offset of the subtable from the start of the cmap table
    const uint64_t base = static_cast<uint64_t>(cmap.off) + sub;
    const uint64_t avail = cmap.len - sub;   // bytes from the subtable start to the end of the cmap table
    uint32_t format = 0;
    if (avail < 4 || !rd.u16(static_cast<size_t>(base), format)) return false;
    auto u16 = [&](uint64_t rel, uint32_t& v) { return rel + 2 <= avail && rd.u16(static_cast<size_t>(base + rel), v); };
    if (format == 0) return avail >= 262;
    if (format == 6) {
        uint32_t count = 0;
        return u16(8, count) && 10 + 2ull * count <= avail;
    }
    if (format == 12 || format == 13) {
        uint32_t ngroups = 0;
        return avail >= 16 && rd.u32(static_cast<size_t>(base + 12), ngroups) && 16 + 12ull * ngroups <= avail;
    }
    if (format != 4) return true;   // formats stb_truetype does not look up (it returns 0)
    uint32_t segx2 = 0, search = 0, sel = 0, shift = 0;
    if (!u16(6, segx2) || !u16(8, search) || !u16(10, sel) || !u16(12, shift)) return false;
    const uint32_t seg = segx2 / 2;
    if (segx2 & 1 || seg == 0 || seg > 8192) return false;
    if (16 + 8ull * seg > avail) return false;
    // the binary search of stb_truetype is only safe with the canonical search parameters
    uint32_t pow2 = 1, log2 = 0;
    while (pow2 * 2 <= seg) { pow2 *= 2; ++log2; }
    if (search != pow2 * 2 || sel != log2 || shift != seg * 2 - pow2 * 2) return false;
    uint32_t prev_end = 0;
    for (uint32_t i = 0; i < seg; ++i) {
        uint32_t end = 0, start = 0, off = 0;
        u16(14 + 2ull * i, end);
        u16(16 + 2ull * seg + 2ull * i, start);
        u16(16 + 6ull * seg + 2ull * i, off);
        if (end < prev_end || start > end) return false;   // ascending segments, so a code point never lands on a segment it is not in
        prev_end = end;
        if (off != 0 && !(start == 0xFFFF && end == 0xFFFF)) {
            const uint64_t last = 16 + 6ull * seg + 2ull * i + off + 2ull * (end - start) + 2;
            if (last > avail) return false;
        }
    }
    return prev_end == 0xFFFF;
}

bool cmap_ok(const Reader& rd, const Table& cmap) {
    uint32_t n = 0;
    if (!rd.u16(cmap.off + 2, n) || n == 0 || n > 64 || 4 + 8ull * n > cmap.len) return false;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t off = 0;
        if (!rd.u32(cmap.off + 4 + 8 * i + 4, off) || off >= cmap.len || !cmap_subtable_ok(rd, cmap, off)) return false;
    }
    return true;
}

// stb_truetype binary-searches the pairs of a horizontal format 0 `kern` subtable without bounds checks.
bool kern_ok(const Reader& rd, const Table& kern) {
    if (kern.len < 18) return false;
    uint32_t ntab = 0, coverage = 0, npairs = 0;
    rd.u16(kern.off + 2, ntab);
    rd.u16(kern.off + 8, coverage);
    rd.u16(kern.off + 10, npairs);
    if (ntab < 1 || coverage != 1) return true;   // stb_truetype ignores the table
    return 18 + 6ull * npairs <= kern.len;
}

} // namespace

bool validate_truetype(const uint8_t* data, size_t size) {
    if (!data || size < 12 || size > FontLibrary::kMaxUserFontBytes) return false;
    Reader rd{data, size};
    uint32_t ver = 0, ntab = 0;
    rd.u32(0, ver);
    if (ver != 0x00010000u && ver != tag_of("true")) return false;   // TrueType outlines only
    rd.u16(4, ntab);
    if (ntab == 0 || ntab > 64 || 12 + 16u * ntab > size) return false;
    Table cmap, glyf, head, hhea, hmtx, loca, maxp, kern;
    for (uint32_t i = 0; i < ntab; ++i) {
        const size_t o = 12 + 16u * i;
        uint32_t tag = 0, off = 0, len = 0;
        rd.u32(o, tag);
        rd.u32(o + 8, off);
        rd.u32(o + 12, len);
        if (static_cast<uint64_t>(off) + len > size) return false;
        Table t{off, len, true};
        if (tag == tag_of("cmap")) cmap = t;
        else if (tag == tag_of("glyf")) glyf = t;
        else if (tag == tag_of("head")) head = t;
        else if (tag == tag_of("hhea")) hhea = t;
        else if (tag == tag_of("hmtx")) hmtx = t;
        else if (tag == tag_of("loca")) loca = t;
        else if (tag == tag_of("maxp")) maxp = t;
        else if (tag == tag_of("kern")) kern = t;
        else if (tag == tag_of("CFF ") || tag == tag_of("CFF2")) return false;   // stb_truetype would prefer it over glyf and does not check it
    }
    if (!cmap.present || !glyf.present || !head.present || !hhea.present || !hmtx.present || !loca.present || !maxp.present) return false;
    if (cmap.len < 4 || head.len < 54 || hhea.len < 36 || maxp.len < 6) return false;
    uint32_t upem = 0, locfmt = 0, nglyphs = 0, nhm = 0;
    rd.u16(head.off + 18, upem);
    rd.u16(head.off + 50, locfmt);
    rd.u16(maxp.off + 4, nglyphs);
    rd.u16(hhea.off + 34, nhm);
    if (upem < 16 || upem > 16384 || locfmt > 1 || nglyphs == 0 || nglyphs > 20000 || nhm == 0 || nhm > nglyphs) return false;
    if (hmtx.len < 4u * nhm + 2u * (nglyphs - nhm)) return false;
    const uint32_t esz = locfmt ? 4 : 2;
    if (loca.len < esz * (nglyphs + 1)) return false;
    std::vector<uint32_t> lo(nglyphs + 1);
    uint32_t prev = 0;
    for (uint32_t i = 0; i <= nglyphs; ++i) {
        uint32_t v = 0;
        if (locfmt) rd.u32(loca.off + 4 * i, v); else { rd.u16(loca.off + 2 * i, v); v *= 2; }
        if (v < prev || v > glyf.len) return false;
        lo[i] = prev = v;
    }
    if (!cmap_ok(rd, cmap)) return false;
    if (kern.present && !kern_ok(rd, kern)) return false;
    for (uint32_t g = 0; g < nglyphs; ++g)
        if (!glyph_ok(rd, glyf, lo, g, 0, nglyphs)) return false;
    return true;
}

// stb_truetype reads GPOS kerning without bounds checks and the table is too intricate to validate here, so user fonts
// are loaded without it (a legacy `kern` table, which is validated, still applies). Renames the table tag in place.
static void hide_gpos(std::vector<uint8_t>& font) {
    if (font.size() < 12) return;
    const size_t ntab = (static_cast<size_t>(font[4]) << 8) | font[5];
    for (size_t i = 0; i < ntab && 12 + 16 * (i + 1) <= font.size(); ++i) {
        uint8_t* t = &font[12 + 16 * i];
        if (t[0] == 'G' && t[1] == 'P' && t[2] == 'O' && t[3] == 'S') t[0] = 'x';
    }
}


// ---------------------------------------------------------------------------------------------
// FontLibrary
// ---------------------------------------------------------------------------------------------
FontLibrary& FontLibrary::instance() {
    static FontLibrary lib;
    return lib;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void FontLibrary::renumber() {
    int id = 0;
    for (auto& f : builtin_) f->id = id++;
    for (auto& f : user_) f->id = id++;
}

int FontLibrary::add_builtin_font(const std::string& name, const unsigned char* data, size_t size, float size_adjust) {
    if (!data || size < 12 || name.empty() || !(size_adjust > 0.0f)) return -1;
    auto f = std::make_unique<FontFace>();
    f->name = name;
    f->builtin = true;
    f->size_adjust = size_adjust;
    if (!init_face(*f, data)) return -1;
    builtin_.push_back(std::move(f));
    // user fonts are listed after the built-ins: re-scan them so a name clash is resolved against the new family
    // and the ids stay contiguous.
    user_.clear();
    if (loaded_) load_user();
    renumber();
    ++generation_;
    return static_cast<int>(builtin_.size()) - 1;
}

void FontLibrary::ensure_loaded() {
    if (loaded_) return;
    loaded_ = true;
    load_user();
    renumber();
}

void FontLibrary::load_user() {
    namespace fs = std::filesystem;
    if (user_dir_.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(user_dir_, ec)) return;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(user_dir_, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string ext = lower(it->path().extension().string());
        if (ext != ".ttf") continue;
        files.push_back(it->path());
        if (files.size() >= 64) break;
    }
    std::sort(files.begin(), files.end());
    int added = 0;
    for (const fs::path& p : files) {
        if (added >= kMaxUserFonts) break;
        std::error_code e2;
        const uintmax_t sz = fs::file_size(p, e2);
        if (e2 || sz < 64 || sz > kMaxUserFontBytes) continue;
        auto f = std::make_unique<FontFace>();
        f->impl->owned.resize(static_cast<size_t>(sz));
        FILE* fp = std::fopen(p.string().c_str(), "rb");
        if (!fp) continue;
        const size_t got = std::fread(f->impl->owned.data(), 1, f->impl->owned.size(), fp);
        std::fclose(fp);
        if (got != f->impl->owned.size()) continue;
        // stb_truetype reads past the end of malformed fonts: validate first, then pad.
        if (!validate_truetype(f->impl->owned.data(), f->impl->owned.size())) continue;
        hide_gpos(f->impl->owned);
        f->impl->owned.resize(f->impl->owned.size() + 16, 0);
        if (!init_face(*f, f->impl->owned.data())) continue;
        std::string nm = p.stem().string();
        if (nm.empty()) continue;
        nm += " (fonts/)";
        bool dup = false;
        for (const auto& b : builtin_) if (lower(b->name) == lower(nm)) dup = true;
        for (const auto& u : user_) if (lower(u->name) == lower(nm)) dup = true;
        if (dup) continue;
        f->name = nm;
        f->builtin = false;
        user_.push_back(std::move(f));
        ++added;
    }
}

int FontLibrary::family_count() { ensure_loaded(); return static_cast<int>(builtin_.size() + user_.size()); }
int FontLibrary::builtin_count() { return static_cast<int>(builtin_.size()); }

const std::string& FontLibrary::family_name(int i) {
    static const std::string empty;
    const FontFace* f = face(i);
    return f ? f->name : empty;
}

const FontFace* FontLibrary::face(int i) {
    ensure_loaded();
    if (i < 0) return nullptr;
    const size_t k = static_cast<size_t>(i);
    if (k < builtin_.size()) return builtin_[k].get();
    if (k - builtin_.size() < user_.size()) return user_[k - builtin_.size()].get();
    return nullptr;
}

int FontLibrary::find_family(const std::string& name) {
    ensure_loaded();
    const std::string key = lower(name);
    const int n = static_cast<int>(builtin_.size() + user_.size());
    for (int i = 0; i < n; ++i)
        if (lower(face(i)->name) == key) return i;
    return -1;
}

void FontLibrary::rescan_user_fonts() {
    loaded_ = true;
    user_.clear();
    load_user();
    renumber();
    ++generation_;
}

void FontLibrary::set_user_font_dir(const std::string& dir) {
    user_dir_ = dir;
    if (loaded_) rescan_user_fonts();
}

} // namespace text
} // namespace gbaimp
