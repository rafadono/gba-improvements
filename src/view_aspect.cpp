#include "gbaimp/view_aspect.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gbaimp {

namespace {
int cap_of(const AspectSettings& s) {
    return s.max_view_width > 0 ? std::max(s.max_view_width, kNativeViewWidth) : kDefaultMaxViewWidth;
}
}

double aspect_ratio(const AspectSettings& s) {
    switch (s.preset) {
        case AspectPreset::Native:    return 3.0 / 2.0;
        case AspectPreset::Wide16x10: return 16.0 / 10.0;
        case AspectPreset::Wide16x9:  return 16.0 / 9.0;
        case AspectPreset::Ultra21x9: return 21.0 / 9.0;
        case AspectPreset::Super32x9: return 32.0 / 9.0;
        case AspectPreset::Custom:
            return (s.custom_w > 0 && s.custom_h > 0) ? static_cast<double>(s.custom_w) / s.custom_h : 0.0;
        case AspectPreset::Adaptive:  return 0.0;
    }
    return 0.0;
}

int view_width_for_ratio(double ratio, int cap) {
    const int hi = std::max(cap, kNativeViewWidth);
    if (!(ratio > 0.0)) return kNativeViewWidth;
    const double w = std::floor(ratio * kNativeViewHeight + 0.5);
    return static_cast<int>(std::clamp(w, static_cast<double>(kNativeViewWidth), static_cast<double>(hi)));
}

int resolve_view_width(const AspectSettings& s, int window_w, int window_h) {
    const int cap = cap_of(s);
    if (s.preset == AspectPreset::Adaptive) {
        if (window_w <= 0 || window_h <= 0) return kNativeViewWidth;
        return view_width_for_ratio(static_cast<double>(window_w) / window_h, cap);
    }
    return view_width_for_ratio(aspect_ratio(s), cap);
}

void split_extra(int view_width, int* left, int* right) {
    const int extra = std::max(0, view_width - kNativeViewWidth);
    if (left) *left = extra / 2;
    if (right) *right = extra - extra / 2;
}

const std::vector<AspectChoice>& aspect_choices() {
    static const std::vector<AspectChoice> v = {
        {AspectPreset::Native, "3:2 (Original)"},
        {AspectPreset::Wide16x10, "16:10"},
        {AspectPreset::Wide16x9, "16:9"},
        {AspectPreset::Ultra21x9, "21:9 (Ultrawide)"},
        {AspectPreset::Super32x9, "32:9 (Super ultrawide)"},
        {AspectPreset::Custom, "Custom"},
        {AspectPreset::Adaptive, "Adaptive (follow window)"},
    };
    return v;
}

const char* aspect_label(AspectPreset p) {
    for (const AspectChoice& c : aspect_choices()) if (c.preset == p) return c.label;
    return "";
}

LauncherAspectTable launcher_aspect_table() {
    static const char* const labels[] = {"3:2 (Original)", "16:10", "16:9", "21:9 (Ultrawide)",
                                         "32:9 (Super ultrawide)"};
    static const uint16_t widths[] = {240, 256, 284, 373, 569};
    return {labels, widths, 5};
}

bool parse_custom_ratio(const std::string& text, AspectSettings* out) {
    if (!out || text.empty()) return false;
    int w = 0, h = 0;
    const size_t colon = text.find(':');
    char* end = nullptr;
    if (colon != std::string::npos) {
        const long a = std::strtol(text.c_str(), &end, 10);
        if (end != text.c_str() + colon) return false;
        const long b = std::strtol(text.c_str() + colon + 1, &end, 10);
        if (*end != '\0' || a <= 0 || b <= 0 || a > 10000 || b > 10000) return false;
        w = static_cast<int>(a);
        h = static_cast<int>(b);
    } else {
        const double r = std::strtod(text.c_str(), &end);
        if (*end != '\0' || !(r >= 1.0) || r > 8.0) return false;
        w = static_cast<int>(std::lround(r * 100.0));
        h = 100;
    }
    const double ratio = static_cast<double>(w) / h;
    if (ratio < 1.0 || ratio > 8.0) return false;
    out->custom_w = w;
    out->custom_h = h;
    return true;
}

} // namespace gbaimp
