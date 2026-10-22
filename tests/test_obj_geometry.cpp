#include "test_harness.h"
#include "gbaimp/obj_geometry.h"

using namespace gbaimp;

int main() {
    CHECK_EQ(obj_signed_y(0), 0);
    CHECK_EQ(obj_signed_y(159), 159);
    CHECK_EQ(obj_signed_y(160), -96);
    CHECK_EQ(obj_signed_y(0xFF), -1);
    CHECK_EQ(obj_signed_y(0x4000 | 250), -6);       // shape bits are ignored
    CHECK_EQ(obj_signed_x(0), 0);
    CHECK_EQ(obj_signed_x(255), 255);
    CHECK_EQ(obj_signed_x(256), -256);
    CHECK_EQ(obj_signed_x(0x4000 | 511), -1);

    CHECK(!obj_disabled(0));
    CHECK(obj_disabled(0x0200));
    CHECK(!obj_disabled(0x0100));                   // affine
    CHECK(!obj_disabled(0x0300));                   // affine, double size

    int w = 0, h = 0;
    obj_size(0x0000, 0x0000, &w, &h); CHECK_EQ(w, 8);  CHECK_EQ(h, 8);
    obj_size(0x0000, 0xC000, &w, &h); CHECK_EQ(w, 64); CHECK_EQ(h, 64);
    obj_size(0x4000, 0x4000, &w, &h); CHECK_EQ(w, 32); CHECK_EQ(h, 8);      // horizontal
    obj_size(0x4000, 0x8000, &w, &h); CHECK_EQ(w, 32); CHECK_EQ(h, 16);
    obj_size(0x8000, 0xC000, &w, &h); CHECK_EQ(w, 32); CHECK_EQ(h, 64);     // vertical
    obj_size(0x8000, 0x0000, &w, &h); CHECK_EQ(w, 8);  CHECK_EQ(h, 16);
    obj_size(0xC000, 0x0000, &w, &h); CHECK_EQ(w, 8);  CHECK_EQ(h, 8);      // invalid shape -> square
    obj_bounds(0x0100, 0x4000, &w, &h); CHECK_EQ(w, 16); CHECK_EQ(h, 16);   // affine: not doubled
    obj_bounds(0x0300, 0x4000, &w, &h); CHECK_EQ(w, 32); CHECK_EQ(h, 32);   // double size
    obj_bounds(0x0000, 0x4000, &w, &h); CHECK_EQ(w, 16); CHECK_EQ(h, 16);
    obj_size(0, 0, nullptr, nullptr);               // null outputs are fine
    return testh::finish("obj_geometry");
}
