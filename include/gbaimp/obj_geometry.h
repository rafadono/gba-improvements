#pragma once
// Sprite (OBJ) attribute helpers for host-side placement policies (game agnostic, header only).
//
// A game adapter that moves HUD sprites or authorizes sprites in the vertical margins needs the
// size and the signed position of a sprite from its raw OAM attributes. These helpers decode them
// the way the hardware does.
#include <cstdint>

namespace gbaimp {

// Signed Y of a sprite (attr0 bits 0..7, 8-bit two's complement around 160): 160..255 are -96..-1.
inline int obj_signed_y(uint16_t attr0) {
    const int raw = static_cast<int>(attr0 & 0xFFu);
    return raw >= 160 ? raw - 256 : raw;
}

// Signed X of a sprite (attr1 bits 0..8, 9-bit two's complement): 256..511 are -256..-1.
inline int obj_signed_x(uint16_t attr1) {
    const int raw = static_cast<int>(attr1 & 0x1FFu);
    return raw >= 256 ? raw - 512 : raw;
}

// True when the sprite is not drawn at all (regular sprite with the "disable" flag).
inline bool obj_disabled(uint16_t attr0) {
    return (attr0 & 0x0300u) == 0x0200u;
}

// Width and height in pixels of the sprite's own image (before any affine double-size).
inline void obj_size(uint16_t attr0, uint16_t attr1, int* w, int* h) {
    static const uint8_t kSizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},   // square
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},   // horizontal
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},   // vertical
    };
    const unsigned shape = (attr0 >> 14) & 3u;
    const unsigned size = (attr1 >> 14) & 3u;
    const unsigned s = shape > 2u ? 0u : shape;
    if (w) *w = kSizes[s][size][0];
    if (h) *h = kSizes[s][size][1];
}

// Bounding box of the drawn sprite: the image size, doubled for an affine sprite with the
// double-size flag. The box starts at the sprite's signed position.
inline void obj_bounds(uint16_t attr0, uint16_t attr1, int* w, int* h) {
    int bw = 0, bh = 0;
    obj_size(attr0, attr1, &bw, &bh);
    if ((attr0 & 0x0300u) == 0x0300u) { bw *= 2; bh *= 2; }
    if (w) *w = bw;
    if (h) *h = bh;
}

} // namespace gbaimp
