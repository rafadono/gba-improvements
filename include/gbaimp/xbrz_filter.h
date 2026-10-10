#pragma once

#include <cstdint>
#include <vector>

namespace gbaimp {

enum class XbrzScale {
    None = 1,
    Xbrz2x = 2,
    Xbrz3x = 3,
    Xbrz4x = 4,
    Xbrz5x = 5
};

class XbrzUpscaler {
public:
    static XbrzUpscaler& instance();

    void set_scale(XbrzScale scale);
    XbrzScale scale() const { return current_scale_; }
    bool is_enabled() const { return current_scale_ != XbrzScale::None; }

    // Upscales src (src_width x src_height) into dst_buffer
    // Returns new width and height
    void process(const uint32_t* src, int src_width, int src_height,
                 std::vector<uint32_t>& dst_buffer, int& out_width, int& out_height);

private:
    XbrzUpscaler() = default;
    XbrzScale current_scale_ = XbrzScale::None;

    void scale_2x(const uint32_t* src, uint32_t* dst, int w, int h);
    void scale_3x(const uint32_t* src, uint32_t* dst, int w, int h);
    void scale_4x(const uint32_t* src, uint32_t* dst, int w, int h);
    void scale_5x(const uint32_t* src, uint32_t* dst, int w, int h);
};

} // namespace gbaimp
