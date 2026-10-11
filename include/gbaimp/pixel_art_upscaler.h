#pragma once

#include <cstdint>
#include <vector>

namespace gbaimp {

// Integer upscale factor. The numeric values equal the factor (None = 1), which hosts
// may rely on when converting from a settings index.
enum class UpscaleScale {
    None = 1,
    X2 = 2,
    X3 = 3,
    X4 = 4,
    X5 = 5,

    // Legacy enumerator names from the former "xbrz" module (same values). Deprecated:
    // kept only so existing hosts keep compiling; use X2..X5 in new code.
    Xbrz2x = X2,
    Xbrz3x = X3,
    Xbrz4x = X4,
    Xbrz5x = X5
};

// Edge-aware pixel-art upscaler in the Scale2x / EPX family: an original, small
// implementation that sharpens diagonal edges by replacing a pixel's corner with a
// matching neighbor. It is NOT xBRZ and does not use xBRZ code or its algorithm
// (4x is two 2x passes; 3x and 5x are simple edge-corner variants).
class PixelArtUpscaler {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    PixelArtUpscaler() = default;

    static PixelArtUpscaler& instance();

    void set_scale(UpscaleScale scale);
    UpscaleScale scale() const { return current_scale_; }
    bool is_enabled() const { return current_scale_ != UpscaleScale::None; }

    // Upscales src (src_width x src_height) into dst_buffer
    // Returns new width and height
    void process(const uint32_t* src, int src_width, int src_height,
                 std::vector<uint32_t>& dst_buffer, int& out_width, int& out_height);

private:
    UpscaleScale current_scale_ = UpscaleScale::None;

    void scale_2x(const uint32_t* src, uint32_t* dst, int w, int h);
    void scale_3x(const uint32_t* src, uint32_t* dst, int w, int h);
    void scale_4x(const uint32_t* src, uint32_t* dst, int w, int h);
    void scale_5x(const uint32_t* src, uint32_t* dst, int w, int h);
};

} // namespace gbaimp
