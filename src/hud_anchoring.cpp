#include "gbaimp/hud_anchoring.h"
#include <algorithm>

namespace gbaimp {

HudAnchoring& HudAnchoring::instance() {
    static HudAnchoring s_instance;
    return s_instance;
}

void HudAnchoring::set_mode(HudAnchorMode mode) {
    settings_.mode = mode;
}

void HudAnchoring::set_horizontal_offset(int offset) {
    settings_.horizontal_offset = std::clamp(offset, 0, 96);
}

void HudAnchoring::add_zone(const HudZone& zone) {
    zones_.push_back(zone);
}

void HudAnchoring::clear_zones() {
    zones_.clear();
}

void HudAnchoring::adjust_sprite_coordinate(int& x, int& y, int sprite_width, int sprite_height, bool is_hud) {
    if (settings_.mode != HudAnchorMode::WidescreenAnchored) {
        return;
    }

    for (const HudZone& z : zones_) {
        if (!z.use_for_coords) continue;
        if (x >= z.coord_x_min && x <= z.coord_x_max &&
            y >= z.coord_y_min && y <= z.coord_y_max) {
            x += z.direction * settings_.horizontal_offset;
            return;
        }
    }
}

void HudAnchoring::process_oam_entry(uint16_t& attr0, uint16_t& attr1) {
    if (settings_.mode != HudAnchorMode::WidescreenAnchored) {
        return;
    }

    int y = attr0 & 0x00FF;
    int x = attr1 & 0x01FF;

    // Wrap-around handling for offscreen negative coordinates in GBA OAM
    if (x >= 256) x -= 512;
    if (y >= 160) y -= 256;

    for (const HudZone& z : zones_) {
        if (!z.use_for_oam) continue;
        if (x >= z.oam_x_min && x <= z.oam_x_max &&
            y >= z.oam_y_min && y <= z.oam_y_max) {
            x += z.direction * settings_.horizontal_offset;
            if (z.oam_clamp_enabled) {
                x = (z.direction < 0) ? std::max(z.oam_clamp_x, x) : std::min(z.oam_clamp_x, x);
            }
            attr1 = (attr1 & ~0x01FF) | (static_cast<uint16_t>(x) & 0x01FF);
            return;
        }
    }
}

} // namespace gbaimp
