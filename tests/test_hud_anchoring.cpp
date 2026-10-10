#include "test_harness.h"
#include "gbaimp/hud_anchoring.h"

using namespace gbaimp;

static uint16_t make_attr1(int x, uint16_t high_bits) {
    return uint16_t((high_bits & 0xFE00) | (x & 0x01FF));
}
static int attr1_x(uint16_t a1) {
    int x = a1 & 0x01FF;
    return x >= 256 ? x - 512 : x;
}

int main() {
    auto& hud = HudAnchoring::instance();

    // Defaults: centered mode, no zones.
    CHECK(hud.settings().mode == HudAnchorMode::OriginalCentered);
    CHECK(hud.zones().empty());

    // Offset clamp [0, 96].
    hud.set_horizontal_offset(500); CHECK_EQ(hud.settings().horizontal_offset, 96);
    hud.set_horizontal_offset(-5);  CHECK_EQ(hud.settings().horizontal_offset, 0);
    hud.set_horizontal_offset(32);  CHECK_EQ(hud.settings().horizontal_offset, 32);

    // Anchored mode but no zones: strict no-op for both paths.
    hud.set_mode(HudAnchorMode::WidescreenAnchored);
    {
        int x = 10, y = 10;
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 10); CHECK_EQ(y, 10);
        uint16_t a0 = 0x0010, a1 = 0x400A;
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(a0, uint16_t(0x0010)); CHECK_EQ(a1, uint16_t(0x400A));
    }

    // Zone A: top-left, shifts left, clamped at x >= 0 in the OAM path.
    HudZone a;
    a.direction = -1;
    a.coord_x_min = 0;   a.coord_x_max = 40;  a.coord_y_min = 0;   a.coord_y_max = 30;
    a.oam_x_min = 0;     a.oam_x_max = 40;    a.oam_y_min = 0;     a.oam_y_max = 30;
    a.oam_clamp_enabled = true; a.oam_clamp_x = 0;
    // Zone B: bottom-right, shifts right, clamped at x <= 239.
    HudZone b;
    b.direction = +1;
    b.coord_x_min = 200; b.coord_x_max = 239; b.coord_y_min = 120; b.coord_y_max = 159;
    b.oam_x_min = 200;   b.oam_x_max = 239;   b.oam_y_min = 120;   b.oam_y_max = 159;
    b.oam_clamp_enabled = true; b.oam_clamp_x = 239;
    hud.add_zone(a);
    hud.add_zone(b);
    CHECK_EQ(hud.zones().size(), size_t(2));

    // Original-centered mode ignores zones.
    hud.set_mode(HudAnchorMode::OriginalCentered);
    {
        int x = 10, y = 10;
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 10);
        uint16_t a0 = 0x0010, a1 = make_attr1(10, 0x4000);
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 10);
    }
    hud.set_mode(HudAnchorMode::WidescreenAnchored);

    // Coordinate path: zone A shifts left by offset (no clamp in this path), zone B right.
    {
        int x = 20, y = 10;
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, -12); CHECK_EQ(y, 10);

        x = 210; y = 130;
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 242); CHECK_EQ(y, 130);

        x = 100; y = 80;   // outside both zones
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 100); CHECK_EQ(y, 80);

        x = 40; y = 30;    // inclusive upper bounds
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 8);
        x = 41; y = 30;    // just outside
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 41);
    }

    // OAM path: shift + clamp, other attr bits preserved, attr0 untouched.
    {
        uint16_t a0 = 0x2005;                 // y = 5, extra attribute bits
        uint16_t a1 = make_attr1(10, 0xC000); // x = 10 -> 10-32 = -22 -> clamped to 0
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(a0, uint16_t(0x2005));
        CHECK_EQ(attr1_x(a1), 0);
        CHECK_EQ(a1 & 0xFE00, 0xC000);

        a0 = 0x0000 | 130;                    // y = 130
        a1 = make_attr1(210, 0x4000);         // 210+32 = 242 -> clamped to 239
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 239);
        CHECK_EQ(a1 & 0xFE00, 0x4000);

        a0 = 130;
        a1 = make_attr1(205, 0);              // 205+32 = 237 -> within clamp, unchanged by it
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 237);

        a0 = 80; a1 = make_attr1(100, 0x8000);  // outside both zones
        uint16_t before = a1;
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(a1, before);
    }

    // OAM wrap-around: attr1 x = 0x1F0 decodes to -16, attr0 y = 0xF0 decodes to -16.
    {
        HudZone w;
        w.direction = +1;
        w.oam_x_min = -32; w.oam_x_max = -1; w.oam_y_min = -32; w.oam_y_max = -1;
        hud.clear_zones();
        hud.add_zone(w);
        uint16_t a0 = 0x00F0, a1 = 0x01F0;
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 16);            // -16 + 32
    }

    // First matching zone wins; later overlapping zones are ignored.
    {
        hud.clear_zones();
        HudZone first;  first.direction = -1;   // unbounded
        HudZone second; second.direction = +1;  // unbounded
        hud.add_zone(first);
        hud.add_zone(second);
        int x = 100, y = 80;
        hud.adjust_sprite_coordinate(x, y, 8, 8, true);
        CHECK_EQ(x, 68);
        uint16_t a0 = 80, a1 = make_attr1(100, 0);
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 68);
    }

    // use_for_coords / use_for_oam = false makes a zone invisible to that path only.
    {
        hud.clear_zones();
        HudZone z; z.direction = -1; z.use_for_coords = false;
        hud.add_zone(z);
        int x = 100, y = 80;
        hud.adjust_sprite_coordinate(x, y, 8, 8, true);
        CHECK_EQ(x, 100);
        uint16_t a0 = 80, a1 = make_attr1(100, 0);
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 68);

        hud.clear_zones();
        HudZone z2; z2.direction = -1; z2.use_for_oam = false;
        hud.add_zone(z2);
        x = 100; y = 80;
        hud.adjust_sprite_coordinate(x, y, 8, 8, true);
        CHECK_EQ(x, 68);
        a1 = make_attr1(100, 0);
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(attr1_x(a1), 100);
    }

    // clear_zones restores the no-op behavior.
    hud.clear_zones();
    CHECK(hud.zones().empty());
    {
        int x = 20, y = 10;
        hud.adjust_sprite_coordinate(x, y, 16, 16, true);
        CHECK_EQ(x, 20);
    }

    hud.set_mode(HudAnchorMode::OriginalCentered);
    return testh::finish("hud_anchoring");
}
