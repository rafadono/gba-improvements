#pragma once

// Backwards-compatibility header. The former "xbrz" module was never xBRZ: it is an
// original edge-aware pixel-art upscaler in the Scale2x / EPX family, now named
// PixelArtUpscaler (gbaimp/pixel_art_upscaler.h).
//
// DEPRECATED names, kept as aliases so existing hosts compile unchanged (they are not
// marked [[deprecated]] on purpose, to avoid warning noise in host builds):
//   gbaimp::XbrzUpscaler  ->  gbaimp::PixelArtUpscaler
//   gbaimp::XbrzScale     ->  gbaimp::UpscaleScale
//   XbrzScale::Xbrz2x..Xbrz5x  ->  UpscaleScale::X2..X5 (same numeric values)
#include "gbaimp/pixel_art_upscaler.h"

namespace gbaimp {

using XbrzUpscaler = PixelArtUpscaler;   // deprecated alias
using XbrzScale = UpscaleScale;          // deprecated alias

} // namespace gbaimp
