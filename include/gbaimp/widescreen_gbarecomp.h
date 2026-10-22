#pragma once
// Header-only adapter that fills gbaimp::WidescreenBindings with the runtime's g_ws_* globals.
//
// Include it from ONE host source file that links the runtime (the library itself never does).
// It declares the runtime's extern "C" symbols with the exact types of the runtime contract
// (docs/GBARECOMP_CONTRACT.md, section 2); it needs no runtime header. The globals are only
// referenced when gbarecomp_widescreen_bindings() is actually called.
//
//   gbaimp::WidescreenProfile p;  // ... game specific, see docs/MODULES.md
//   gbaimp::Widescreen::instance().configure(gbaimp::gbarecomp_widescreen_bindings(), p);
//   gbaimp::Widescreen::instance().install();            // from the extended-view init callback
//   gbaimp::Widescreen::instance().update();             // from the per-frame callback
#include "gbaimp/widescreen.h"

extern "C" {
extern int (*g_ws_tilemap_provider)(int bg, int hw_x, int screen_y, uint16_t* out_entry);
extern int (*g_ws_bg_x_provider)(int bg, int output_x, int screen_y, int* out_hw_x);
extern unsigned g_ws_bg_x_provider_layers;
extern int (*g_ws_obj_attr_x_provider)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
extern int (*g_ws_obj_attr_y_provider)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_y);
// gba::WsMarginObjPixel has the layout of gbaimp::WsMarginPixel (RGB555 + priority + order + flag).
extern const gbaimp::WsMarginPixel* (*g_ws_obj_margin_provider)(int screen_y, int* hardware_left, int* width);
extern int g_ws_authored_margin_layers;
extern int g_ws_obj_native_clip;
extern int g_ws_pillarbox;
extern int g_ws_pillarbox_left;
extern int g_ws_pillarbox_right;
extern unsigned g_ws_active;
extern unsigned g_ws_extra_left;
extern unsigned g_ws_extra_right;
extern void (*g_runtime_fn_entry_hook)(uint32_t entry_pc);
}

namespace gbaimp {

// The runtime has no globals for the vertical extras. The host stores them here (from the per-frame
// ExtendedViewFrameInfo and from its view-geometry callback); the bindings read them.
struct GbarecompVerticalExtras {
    unsigned top = 0;
    unsigned bottom = 0;
};
inline GbarecompVerticalExtras& gbarecomp_vertical_extras() {
    static GbarecompVerticalExtras v;
    return v;
}
inline void gbarecomp_set_vertical_extras(unsigned top, unsigned bottom) {
    GbarecompVerticalExtras& v = gbarecomp_vertical_extras();
    v.top = top;
    v.bottom = bottom;
}

inline WidescreenBindings gbarecomp_widescreen_bindings() {
    WidescreenBindings b;
    b.active = &g_ws_active;
    b.extra_left = &g_ws_extra_left;
    b.extra_right = &g_ws_extra_right;
    b.extra_top = &gbarecomp_vertical_extras().top;
    b.extra_bottom = &gbarecomp_vertical_extras().bottom;
    b.obj_attr_y_provider = &g_ws_obj_attr_y_provider;
    b.margin_pixel_provider = &g_ws_obj_margin_provider;
    b.tilemap_provider = &g_ws_tilemap_provider;
    b.bg_x_provider = &g_ws_bg_x_provider;
    b.bg_x_provider_layers = &g_ws_bg_x_provider_layers;
    b.obj_attr_x_provider = &g_ws_obj_attr_x_provider;
    b.authored_margin_layers = &g_ws_authored_margin_layers;
    b.obj_native_clip = &g_ws_obj_native_clip;
    b.pillarbox = &g_ws_pillarbox;
    b.pillarbox_left = &g_ws_pillarbox_left;
    b.pillarbox_right = &g_ws_pillarbox_right;
    b.fn_entry_hook = &g_runtime_fn_entry_hook;
    return b;
}

} // namespace gbaimp
