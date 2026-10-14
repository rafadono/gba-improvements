#pragma once
// Data contract of the output-resolution text engine: "a page of text + origin + box in, pixels out".
//
// The host (which knows the game) fills a TextPage once per presented frame; the library lays it out, rasterises the
// glyphs into a GPU atlas and draws them with the SDL renderer at the resolution of the output. The library never reads
// game memory and knows no text encoding: the host translates its own text words into Unicode code points.
// Positions are in "source pixels" (the 240x160 GBA picture, or the wider presented image, see DrawInfo::gba_w).
#include <cstdint>
#include <vector>

struct SDL_Renderer;

namespace gbaimp {
namespace text {

// One character of the page, in reading order. The host maps its engine words to these (colour toggles are folded into
// `colour`, terminators are dropped, unknown words get cp = 0).
struct TextChar {
    uint32_t cp = 0;        // Unicode scalar. 0x0A = line break, 0x20 / 0xA0 = space, 0 = not renderable (the page cannot be drawn)
    uint8_t colour = 0;     // 0 = text colour, 1 = highlight colour
    int entry = -1;         // reveal index ("typewriter" order, 0-based, consecutive); -1 = never revealed (e.g. line breaks)
    bool has_pos = false;   // the source's own layout is known for this character
    float gx = 0.0f;        // pen position of the source glyph cell, source px (valid when has_pos)
    float gy = 0.0f;
};

struct TextPage {
    bool valid = false;               // false => nothing is drawn and can_draw() is false
    std::vector<TextChar> chars;      // the page, INCLUDING spaces and line breaks
    int revealed = 0;                 // number of entries already visible (0..entry count); draw() shows entries < revealed
    uint32_t page_id = 0;             // changes when a new page starts (layout cache key)
    float origin_x = 0.0f;            // text origin in source px (first pen position of the page)
    float origin_y = 0.0f;
    int wrap_px = 155;                // source wrap limit (width of the text area from the origin), source px
    int line_pitch = 12;              // source line pitch, source px
    int max_lines = 3;                // source lines per page
    int cell_height = 16;             // height of the source glyph cell, source px (the text area ends one cell below the last line)
    int baseline_row = 11;            // baseline row inside that cell, source px from the cell top
    float box_x = 0.0f, box_y = 0.0f; // optional: text box rectangle in source px (0 size = unknown)
    float box_w = 0.0f, box_h = 0.0f;
    // Colours the original renderer draws this page with (0xRRGGBBAA, 0 = unknown). Used when a colour setting is 0
    // ("match the game"); the highlight colour replaces TextSettings::highlight_rgba when known.
    uint32_t native_text_rgba = 0;
    uint32_t native_shadow_rgba = 0;
    uint32_t native_highlight_rgba = 0;
};

enum class WrapMode : int {
    KeepSourceBreaks = 0,  // use the source's line breaks (same words per line as the original)
    Reflow = 1             // re-wrap the page text to the box width at the chosen size
};

enum class TextStyle : int { Plain = 0, Shadow = 1, Outline = 2 };

// User-facing settings. All of them apply live (the layout / atlas caches are keyed on them).
struct TextSettings {
    bool enabled = false;          // false => draw() / can_draw() do nothing
    int family = 0;                // index into FontLibrary families (built-ins, then user fonts)
    float size_em = 10.0f;         // font size in source px (em); 10 ~ original cap height
    float line_spacing = 1.2f;     // multiplier of the font's natural line height
    float letter_spacing = 0.0f;   // extra advance in source px
    WrapMode wrap = WrapMode::KeepSourceBreaks;
    TextStyle style = TextStyle::Shadow;
    float shadow_offset = 0.6f;    // source px
    float outline_width = 0.8f;    // source px (Outline style)
    uint32_t text_rgba = 0xFFFFFFFFu;     // 0 = match TextPage::native_text_rgba
    uint32_t shadow_rgba = 0x000000B0u;   // 0 = match TextPage::native_shadow_rgba
    uint32_t outline_rgba = 0x000000FFu;
    uint32_t highlight_rgba = 0xFFE27AFFu;           // colour of TextChar::colour == 1 when the page carries no native highlight (alpha ignored)
    uint32_t fallback_text_rgba = 0xFFFFFFFFu;       // "match the game" colours used when the page does not report its own
    uint32_t fallback_shadow_rgba = 0x000000FFu;
    float opacity = 1.0f;          // 0..1
    float box_padding = 0.0f;      // extra source px kept free inside the box
    bool auto_fit = true;          // shrink the size until the page fits inside the box height
    float min_size_em = 5.0f;      // auto-fit lower bound
};

// Where and how the text is drawn this frame.
struct DrawInfo {
    SDL_Renderer* renderer = nullptr;
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;   // output rectangle of the source image (gba_w x gba_h source px)
    int gba_w = 240, gba_h = 160;
    float global_alpha = 1.0f;                        // includes any alpha-blend fade attenuation
    // Brightness fades pull the colours of what is drawn towards white / black (FadeMode values of text_fade.h; 0 = none).
    int fade_mode = 0;
    float fade_level = 0.0f;                          // 0..1
    bool fade_from_palette = false;                   // the fade was derived from the native text colours (they are already faded)
};

} // namespace text
} // namespace gbaimp
