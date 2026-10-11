#pragma once

#include <cstdint>
#include <climits>
#include <vector>

namespace gbaimp {

enum class HudAnchorMode : int {
    OriginalCentered = 0,
    WidescreenAnchored = 1
};

struct HudAnchorSettings {
    HudAnchorMode mode = HudAnchorMode::OriginalCentered;
    int horizontal_offset = 32;
    int vertical_offset = 0;
};

// A rectangular screen region (in GBA screen pixels) whose sprites are pushed
// horizontally by `horizontal_offset` when widescreen anchoring is active.
// Bounds are inclusive. The sprite-coordinate path and the raw OAM path may use
// different rectangles, so each has its own bounds; use INT_MIN / INT_MAX for
// an unbounded side. Zones are tested in registration order, first match wins.
struct HudZone {
    int direction = -1;                 // -1: shift left (x -= offset), +1: shift right (x += offset)

    // Used by adjust_sprite_coordinate()
    bool use_for_coords = true;
    int coord_x_min = INT_MIN, coord_y_min = INT_MIN;
    int coord_x_max = INT_MAX, coord_y_max = INT_MAX;

    // Used by process_oam_entry() (after OAM wrap-around decoding)
    bool use_for_oam = true;
    int oam_x_min = INT_MIN, oam_y_min = INT_MIN;
    int oam_x_max = INT_MAX, oam_y_max = INT_MAX;
    bool oam_clamp_enabled = false;     // clamp shifted x so it never goes past oam_clamp_x
    int oam_clamp_x = 0;                // direction < 0: lower bound (max), direction > 0: upper bound (min)
};

class HudAnchoring {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    HudAnchoring() = default;

    static HudAnchoring& instance();

    const HudAnchorSettings& settings() const { return settings_; }
    HudAnchorSettings& settings() { return settings_; }

    void set_mode(HudAnchorMode mode);
    void set_horizontal_offset(int offset);

    // Zone registry. Empty by default (anchoring is then a no-op).
    void add_zone(const HudZone& zone);
    void clear_zones();
    const std::vector<HudZone>& zones() const { return zones_; }

    // Adjusts sprite coordinate (x, y) if it belongs to HUD elements
    void adjust_sprite_coordinate(int& x, int& y, int sprite_width, int sprite_height, bool is_hud);

    // Repositions OAM attributes during rendering if HUD anchoring is active
    void process_oam_entry(uint16_t& attr0, uint16_t& attr1);

private:

    HudAnchorSettings settings_;
    std::vector<HudZone> zones_;
};

} // namespace gbaimp
