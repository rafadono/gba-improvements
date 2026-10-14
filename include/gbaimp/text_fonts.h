#pragma once
// Font access of the output-resolution text engine: a library of font families (embedded fonts injected by the host,
// plus untrusted user .ttf files found in a folder), face metrics and single-glyph rasterisation through
// stb_truetype (analytic area coverage, no hinting bytecode). All stb_truetype use is confined to text_fonts.cpp.
//
// The library ships NO font data. A host registers its embedded fonts with FontLibrary::add_builtin_font() (see
// docs/MODULES.md, "text_fonts.h"); user fonts are validated by validate_truetype() before stb_truetype sees them.
// Nothing here depends on SDL or on any game. FontLibrary is not internally synchronised: TextRenderer serialises
// its own use of it; a host that shares one library between threads must lock.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gbaimp {
namespace text {

// Sizes derived from one em in OUTPUT pixels ("vertical hint": the x-height is snapped to whole output pixels).
struct FaceScale {
    float sx = 0.0f;       // font units -> output px, horizontal (true em)
    float sy = 0.0f;       // vertical scale: x-height fitted to a whole number of output px
    int xheight_px = 0;    // the fitted x-height in output px
};

class FontFace {
public:
    FontFace();
    ~FontFace();
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    std::string name;
    bool builtin = false;
    int upem = 1000;
    float ascent = 0.0f, descent = 0.0f, line_gap = 0.0f;   // font units (descent negative)
    float xheight = 0.0f, capheight = 0.0f;                 // font units
    float size_adjust = 1.0f;                                // optical size correction applied to the em of this family
    int id = 0;                                              // index in the library (stable until fonts are added / rescanned)

    bool valid() const;
    int glyph_index(uint32_t cp) const;                      // 0 = missing
    float advance(int gi) const;                             // font units
    float kern(int g1, int g2) const;                        // font units (0 when the font has no kern table)
    // Vertical ink extent of a glyph in font units (y up): returns false for empty glyphs.
    bool glyph_extent(int gi, float& ymin, float& ymax) const;
    FaceScale scale_for(float em_px) const;
    // Coverage bitmap of one glyph at the given scales. (x0,y0) = offset of the bitmap's top-left from the
    // pen origin on the baseline (y down). `out` is resized to w*h (row-major, 0..255).
    bool bitmap(int gi, const FaceScale& sc, float shift_x, int& x0, int& y0, int& w, int& h, std::vector<uint8_t>& out) const;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

class FontLibrary {
public:
    FontLibrary() = default;
    static FontLibrary& instance();

    // Registers an embedded (trusted) TrueType font as a built-in family. `data` is NOT copied: it must stay valid for
    // the lifetime of the library (static arrays). Built-ins are listed first, in registration order, before user fonts.
    // Returns the family index, or -1 when the data is not a usable font. Changes bump generation().
    int add_builtin_font(const std::string& name, const unsigned char* data, size_t size, float size_adjust = 1.0f);

    int family_count();                         // built-in first, then user fonts (lazy scan on first use)
    int builtin_count();
    const std::string& family_name(int i);      // "" for an out-of-range index
    const FontFace* face(int i);                // out-of-range -> nullptr
    int find_family(const std::string& name);   // case-insensitive exact match, -1 if absent
    void rescan_user_fonts();                   // re-reads the user font folder (bumps generation())
    // Folder scanned for user .ttf files; default "fonts" (relative to the working directory), "" disables user fonts.
    void set_user_font_dir(const std::string& dir);
    uint64_t generation() const { return generation_; }   // changes whenever the family list or a face changes
    static constexpr int kMaxUserFonts = 8;
    static constexpr size_t kMaxUserFontBytes = 8u * 1024u * 1024u;

private:
    void ensure_loaded();
    void load_user();
    void renumber();
    std::vector<std::unique_ptr<FontFace>> builtin_;
    std::vector<std::unique_ptr<FontFace>> user_;
    bool loaded_ = false;
    uint64_t generation_ = 1;
    std::string user_dir_ = "fonts";
};

// Strict sanity check of TrueType data before it is handed to stb_truetype (which does not bounds-check
// malformed fonts): table directory inside the buffer, required tables present, loca/glyf consistent.
bool validate_truetype(const uint8_t* data, size_t size);

} // namespace text
} // namespace gbaimp
