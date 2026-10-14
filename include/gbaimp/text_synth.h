#pragma once
// Synthetic pages for tests, tools and host self-checks (no game needed).
#include "gbaimp/text_types.h"

#include <string>
#include <vector>

namespace gbaimp {
namespace text {

// Builds a page the way a host would report it: one character per code unit (bytes are taken as Latin-1 code points),
// '\n' between lines, one reveal entry per drawn character (spaces included), pen positions on a regular grid (5.5 source px
// per character, 12 px line pitch). With glyphs == false the page carries no source positions (entries are still numbered),
// and `revealed` is 0.
inline TextPage synth_page(const std::vector<std::string>& lines, float origin_x = 72.0f, float origin_y = 108.0f,
                           bool glyphs = true, uint32_t page_id = 1) {
    TextPage p;
    p.valid = true;
    p.page_id = page_id;
    p.origin_x = origin_x;
    p.origin_y = origin_y;
    p.wrap_px = 155;
    p.line_pitch = 12;
    p.max_lines = 3;
    int entry = 0;
    for (size_t li = 0; li < lines.size(); ++li) {
        float pen = 0.0f;
        for (unsigned char c : lines[li]) {
            TextChar t;
            t.cp = c;
            t.entry = entry++;
            if (glyphs) {
                t.has_pos = true;
                t.gx = origin_x + pen;
                t.gy = origin_y + 12.0f * static_cast<float>(li);
            }
            p.chars.push_back(t);
            pen += 5.5f;
        }
        if (li + 1 < lines.size()) {
            TextChar nl;
            nl.cp = 0x0A;
            p.chars.push_back(nl);
        }
    }
    p.revealed = glyphs ? entry : 0;
    return p;
}

} // namespace text
} // namespace gbaimp
