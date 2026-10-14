#pragma once
// Layout of one page of text: word wrap, auto-fit and reveal-entry mapping.
// Pure CPU (no SDL); positions are in OUTPUT pixels relative to the origin of the source image
// (add DrawInfo::dst_x / dst_y).
#include "gbaimp/text_fonts.h"
#include "gbaimp/text_types.h"

#include <cstdint>
#include <vector>

namespace gbaimp {
namespace text {

struct LayoutItem {
    uint32_t cp = 0;        // Unicode code point actually drawn
    int gi = 0;             // glyph index in `face`
    uint16_t face = 0;      // FontLibrary face id
    uint8_t colour = 0;     // 0 = text colour, 1 = highlight (colour code)
    int line = 0;
    int entry = -1;         // reveal entry (TextChar::entry) this glyph belongs to (-1 never revealed)
    float x = 0.0f;         // pen position, output px
};

struct LayoutLine {
    int first = 0, count = 0;   // range in PageLayout::items
    int baseline = 0;           // output px, integer
    float x0 = 0.0f, x1 = 0.0f; // pen extent of the line (advance based), output px
    int words = 0;
};

struct PageLayout {
    bool ok = false;
    const char* reason = "";        // why ok == false
    float em_src = 0.0f;            // em actually used (after auto-fit), GBA px
    float em_px = 0.0f;             // em in output px (quantised to 1/32 px)
    int pitch_px = 0;               // line pitch in output px
    int xheight_px = 0;             // fitted x-height of the primary face in output px
    bool shrunk = false;            // auto-fit reduced the size
    float sx = 1.0f, sy = 1.0f;     // output px per source px
    float area_l = 0, area_t = 0, area_r = 0, area_b = 0;   // text area, output px (relative to dst origin)
    float ink_t = 0, ink_b = 0;     // vertical glyph ink extent over the page, output px
    int entry_count = 0;            // number of typewriter entries covered
    int source_lines = 0;           // lines of the source's own layout
    std::vector<LayoutItem> items;  // drawable glyphs only (spaces and control words are not items)
    std::vector<LayoutLine> lines;
};

// Lays out `page` with `st` at the output scale (sx, sy) (output px per source px) using the fonts of `lib`.
// Returns out.ok; out.reason says why a page cannot be drawn ("invalid page", "no fonts", "unsupported character",
// "glyph missing", "no room", "line too wide", "page too tall", "size too small").
bool layout_page(const TextPage& page, const TextSettings& st, float sx, float sy, PageLayout& out,
                 FontLibrary& lib = FontLibrary::instance());

// Hash of everything that influences the layout (cache key helper).
uint64_t layout_key(const TextPage& page, const TextSettings& st, int dst_w, int dst_h, uint64_t font_generation);

} // namespace text
} // namespace gbaimp
