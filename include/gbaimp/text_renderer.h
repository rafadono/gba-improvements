#pragma once
// SDL renderer of the text engine: caches the layout of the current page, rasterises glyphs into a GPU atlas
// (one SDL_Texture) and draws them with SDL_RenderCopy at the resolution of the output. Works with any SDL
// renderer (D3D11, OpenGL, software).
//
// Atlas ownership rules
//  * The atlas texture is owned by the TextRenderer instance that created it and is never shared or exposed.
//  * It is bound to the SDL_Renderer given in DrawInfo::renderer. If a draw() call passes a different renderer
//    pointer the stale texture is forgotten (not destroyed: its renderer may be gone) and a new atlas is created.
//  * Call release_gpu() BEFORE destroying the SDL_Renderer that was used for drawing. ~TextRenderer() also destroys the
//    texture, so the renderer must still be alive then, or release_gpu() must have been called. instance() is a
//    deliberately leaked object (its destructor never runs), so it needs an explicit release_gpu() at shutdown.
//  * The atlas starts at 1024x1024, doubles up to 4096x4096 when a page does not fit, and is flushed (not copied)
//    whenever the output width, the fonts or the settings that affect glyph bitmaps change. A flush only happens before
//    anything of the current frame has been queued.
//  * draw() / can_draw() / invalidate() / rescan_fonts() / release_gpu() are serialised by an internal mutex and may be
//    called from different threads, but SDL rendering itself must stay on the thread that owns the SDL_Renderer.
#include "gbaimp/text_fonts.h"
#include "gbaimp/text_layout.h"
#include "gbaimp/text_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gbaimp {
namespace text {

class TextRenderer {
public:
    // `fonts` = font library used for layout and raster (default: FontLibrary::instance(); must outlive the renderer).
    explicit TextRenderer(FontLibrary* fonts = nullptr);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    static TextRenderer& instance();

    // Draws the revealed part of `page` (entries < page.revealed). Returns true if something was drawn.
    bool draw(const TextPage& page, const TextSettings& settings, const DrawInfo& info);
    // Cheap check a host uses BEFORE hiding the original text: true when this page can be drawn with these
    // settings (enabled, fonts loaded, every character has a glyph, the page fits or can be auto-fitted).
    bool can_draw(const TextPage& page, const TextSettings& settings, const DrawInfo& info);
    void invalidate();                                // drop caches (settings/font/size/scale changed)
    // Font families available (built-in first, then user fonts).
    std::vector<std::string> family_names();
    // Re-reads the user font folder (user fonts are otherwise scanned once, lazily). Invalidates caches.
    void rescan_fonts();
    // Index of a family by its display name (case-insensitive), -1 when unknown.
    int family_index(const std::string& name);
    // Frees the GPU atlas. See "Atlas ownership rules" above.
    void release_gpu();
    FontLibrary& fonts();

    // Counters for tests/logs.
    uint64_t draws() const;
    uint64_t layout_builds() const;      // page layouts computed (cache misses)
    uint64_t glyphs_rasterized() const;  // glyph bitmaps rasterised into the atlas
    uint64_t atlas_flushes() const;      // atlas resets (full / invalidate / scale change)

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace text
} // namespace gbaimp
