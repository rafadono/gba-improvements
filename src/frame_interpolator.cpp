#include "gbaimp/frame_interpolator.h"
#include <SDL.h>
#include <chrono>
#include <thread>
#include <cmath>

namespace gbaimp {

FrameInterpolator& FrameInterpolator::instance() {
    static FrameInterpolator s_instance;
    return s_instance;
}

FrameInterpolator::FrameInterpolator() = default;

FrameInterpolator::~FrameInterpolator() {
    if (prev_frame_tex_) {
        SDL_DestroyTexture(prev_frame_tex_);
        prev_frame_tex_ = nullptr;
    }
}

void FrameInterpolator::set_mode(FramerateMode mode) {
    settings_.mode = mode;
}

void FrameInterpolator::set_smoothing(MotionSmoothingMode smoothing) {
    settings_.smoothing = smoothing;
}

void FrameInterpolator::update_display_mode(SDL_Renderer* renderer) {
    if (display_queried_ || !renderer) return;
    SDL_Window* win = SDL_RenderGetWindow(renderer);
    if (!win) return;

    int display_idx = SDL_GetWindowDisplayIndex(win);
    if (display_idx < 0) display_idx = 0;

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(display_idx, &mode) == 0) {
        display_w_ = mode.w;
        display_h_ = mode.h;
        detected_hz_ = mode.refresh_rate > 0 ? mode.refresh_rate : 60;
        display_queried_ = true;
    }
}

double FrameInterpolator::target_fps() const {
    switch (settings_.mode) {
    case FramerateMode::Fps120: return 120.0;
    case FramerateMode::Fps144: return 144.0;
    case FramerateMode::DisplayNative:
        return detected_hz_ > 0 ? static_cast<double>(detected_hz_) : 60.0;
    case FramerateMode::Uncapped: return 240.0;
    case FramerateMode::Fps60:
    default:
        return 60.0;
    }
}

double FrameInterpolator::target_frametime_ms() const {
    return 1000.0 / target_fps();
}

std::string FrameInterpolator::detected_display_info(SDL_Renderer* renderer) const {
    if (!display_queried_ && renderer) {
        const_cast<FrameInterpolator*>(this)->update_display_mode(renderer);
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%dx%d @ %dHz", display_w_, display_h_, detected_hz_);
    return std::string(buf);
}

void FrameInterpolator::sleep_until_target(int64_t target_qpc) {
    const uint64_t freq = SDL_GetPerformanceFrequency();
    for (;;) {
        uint64_t now = SDL_GetPerformanceCounter();
        if (static_cast<int64_t>(now) >= target_qpc) break;
        int64_t remaining_ticks = target_qpc - static_cast<int64_t>(now);
        double remaining_ms = (remaining_ticks * 1000.0) / static_cast<double>(freq);
        if (remaining_ms > 2.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            // Busy spin for sub-millisecond precision
            #if defined(_MSC_VER)
            _mm_pause();
            #endif
        }
    }
}

void FrameInterpolator::on_present(SDL_Renderer* renderer, const SDL_Rect* viewport) {
    if (!renderer) return;
    update_display_mode(renderer);

    if (settings_.mode == FramerateMode::Fps60) {
        return;
    }

    const double fps = target_fps();
    if (fps <= 60.0) return;

    // Intermediate sub-frame cadence
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const double subframe_interval_sec = 1.0 / fps;
    const int64_t subframe_ticks = static_cast<int64_t>(subframe_interval_sec * static_cast<double>(freq));

    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (!prev_frame_tex_ || tex_w_ != w || tex_h_ != h) {
        if (prev_frame_tex_) SDL_DestroyTexture(prev_frame_tex_);
        prev_frame_tex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        tex_w_ = w;
        tex_h_ = h;
    }

    uint64_t now = SDL_GetPerformanceCounter();
    if (last_present_qpc_ != 0 && prev_frame_tex_ && settings_.smoothing != MotionSmoothingMode::Off) {
        int64_t target_qpc = static_cast<int64_t>(last_present_qpc_) + subframe_ticks;
        sleep_until_target(target_qpc);

        // Blend previous frame at intermediate progress
        SDL_SetTextureBlendMode(prev_frame_tex_, SDL_BLENDMODE_BLEND);
        uint8_t alpha = settings_.smoothing == MotionSmoothingMode::SmoothBlend ? 128 : 160;
        SDL_SetTextureAlphaMod(prev_frame_tex_, alpha);
        SDL_RenderCopy(renderer, prev_frame_tex_, nullptr, nullptr);
    }

    last_present_qpc_ = SDL_GetPerformanceCounter();
}

} // namespace gbaimp
