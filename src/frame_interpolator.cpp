#include "gbaimp/frame_interpolator.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace gbaimp {

FrameInterpolator& FrameInterpolator::instance() {
    static FrameInterpolator s_instance;
    return s_instance;
}

FrameInterpolator::FrameInterpolator() = default;

FrameInterpolator::~FrameInterpolator() {
    release_resources();
}

void FrameInterpolator::release_resources() {
    for (SDL_Texture*& t : tex_) {
        if (t) SDL_DestroyTexture(t);
        t = nullptr;
    }
    if (blend_tex_) SDL_DestroyTexture(blend_tex_);
    blend_tex_ = nullptr;
    pix_[0].clear(); pix_[0].shrink_to_fit();
    pix_[1].clear(); pix_[1].shrink_to_fit();
    blend_pix_.clear(); blend_pix_.shrink_to_fit();
    tex_renderer_ = nullptr;
    tex_w_ = tex_h_ = 0;
    have_prev_ = false;
    readback_failed_ = false;
}

void FrameInterpolator::set_present_hook(std::function<void(SDL_Renderer*)> hook) {
    present_hook_ = std::move(hook);
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
            // Sub-millisecond precision: spin, yielding the time slice.
            std::this_thread::yield();
        }
    }
}

namespace {

// Neutralises logical size, viewport, clip and scale so the readback and the
// copies below address raw output pixels, then restores the host's state.
class RendererStateGuard {
public:
    explicit RendererStateGuard(SDL_Renderer* r) : r_(r) {
        SDL_RenderGetLogicalSize(r_, &logical_w_, &logical_h_);
        SDL_RenderGetViewport(r_, &viewport_);
        clip_enabled_ = SDL_RenderIsClipEnabled(r_) == SDL_TRUE;
        SDL_RenderGetClipRect(r_, &clip_);
        SDL_RenderGetScale(r_, &scale_x_, &scale_y_);
        SDL_RenderSetLogicalSize(r_, 0, 0);
        SDL_RenderSetViewport(r_, nullptr);
        SDL_RenderSetClipRect(r_, nullptr);
        SDL_RenderSetScale(r_, 1.0f, 1.0f);
    }
    ~RendererStateGuard() {
        SDL_RenderSetLogicalSize(r_, logical_w_, logical_h_);
        // Scale first: SDL reports and accepts viewport/clip rects divided by/multiplied
        // by the current scale.
        SDL_RenderSetScale(r_, scale_x_, scale_y_);
        SDL_RenderSetViewport(r_, &viewport_);
        SDL_RenderSetClipRect(r_, clip_enabled_ ? &clip_ : nullptr);
    }
    RendererStateGuard(const RendererStateGuard&) = delete;
    RendererStateGuard& operator=(const RendererStateGuard&) = delete;

private:
    SDL_Renderer* r_;
    int logical_w_ = 0, logical_h_ = 0;
    SDL_Rect viewport_{};
    SDL_Rect clip_{};
    bool clip_enabled_ = false;
    float scale_x_ = 1.0f, scale_y_ = 1.0f;
};

// Per-pixel motion-adaptive blend: pixels that changed noticeably are mixed with
// the previous frame; (nearly) static pixels keep the current value so text and HUD
// stay sharp. w256 is the weight of the CURRENT frame (0..256).
void blend_adaptive(const uint32_t* prev, const uint32_t* cur, uint32_t* out, size_t n, int w256) {
    constexpr int kThreshold = 12;   // per-channel difference regarded as "unchanged"
    const int wp = 256 - w256;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = prev[i], c = cur[i];
        if (p == c) { out[i] = c; continue; }
        const int dr = std::abs(int((c >> 16) & 0xFF) - int((p >> 16) & 0xFF));
        const int dg = std::abs(int((c >> 8) & 0xFF) - int((p >> 8) & 0xFF));
        const int db = std::abs(int(c & 0xFF) - int(p & 0xFF));
        if (dr <= kThreshold && dg <= kThreshold && db <= kThreshold) { out[i] = c; continue; }
        const uint32_t r = (((c >> 16) & 0xFF) * w256 + ((p >> 16) & 0xFF) * wp) >> 8;
        const uint32_t g = (((c >> 8) & 0xFF) * w256 + ((p >> 8) & 0xFF) * wp) >> 8;
        const uint32_t b = ((c & 0xFF) * w256 + (p & 0xFF) * wp) >> 8;
        out[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

} // namespace

bool FrameInterpolator::ensure_resources(SDL_Renderer* renderer, int w, int h) {
    if (tex_renderer_ == renderer && tex_w_ == w && tex_h_ == h && tex_[0] && tex_[1]) return true;

    if (tex_renderer_ == renderer) {
        release_resources();       // resize: same renderer, safe to destroy
    } else {
        // Different renderer: the old textures belong to it (and die with it); do not
        // touch them. Hosts that keep the old renderer alive should call
        // release_resources() first.
        tex_[0] = tex_[1] = nullptr;
        blend_tex_ = nullptr;
        have_prev_ = false;
        readback_failed_ = false;
    }
    for (SDL_Texture*& t : tex_) {
        t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!t) { release_resources(); return false; }
    }
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    pix_[0].assign(n, 0);
    pix_[1].assign(n, 0);
    tex_renderer_ = renderer;
    tex_w_ = w;
    tex_h_ = h;
    cur_ = 0;
    have_prev_ = false;
    return true;
}

void FrameInterpolator::present_extra(SDL_Renderer* renderer) {
    ++extra_presents_;
    if (present_hook_) {
        present_hook_(renderer);
    } else {
        in_extra_present_ = true;   // a host that intercepts SDL_RenderPresent re-enters on_present
        SDL_RenderPresent(renderer);
        in_extra_present_ = false;
    }
}

void FrameInterpolator::on_present(SDL_Renderer* renderer, const SDL_Rect* /*viewport*/) {
    if (!renderer || in_extra_present_) return;
    update_display_mode(renderer);

    // Zero-cost paths: 60 Hz (or a display that is not faster) and smoothing off do
    // no readback, no texture work and no waiting.
    const double fps = target_fps();
    if (settings_.mode == FramerateMode::Fps60 || fps <= 60.0 ||
        settings_.smoothing == MotionSmoothingMode::Off) {
        have_prev_ = false;        // never blend against a stale frame after re-enabling
        return;
    }
    if (SDL_GetRenderTarget(renderer) != nullptr) return;   // not drawing to the back buffer

    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0 || h <= 0) return;
    if (!ensure_resources(renderer, w, h) || readback_failed_) return;

    const uint64_t freq = SDL_GetPerformanceFrequency();
    const int64_t subframe_ticks = static_cast<int64_t>(static_cast<double>(freq) / fps);

    RendererStateGuard guard(renderer);

    // Capture the frame the host just drew into the back buffer.
    const int cur = cur_;
    const int prv = cur ^ 1;
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pix_[cur].data(), w * 4) != 0) {
        readback_failed_ = true;   // e.g. a backend without readback support: stay a no-op
        have_prev_ = false;
        return;
    }
    ++readbacks_;
    if (SDL_UpdateTexture(tex_[cur], nullptr, pix_[cur].data(), w * 4) != 0) return;

    const uint64_t now = SDL_GetPerformanceCounter();
    const bool fresh = last_present_qpc_ != 0 &&
        (now - last_present_qpc_) < static_cast<uint64_t>(freq / 10);   // < 100 ms since the last frame

    if (have_prev_ && fresh) {
        // 120 Hz -> 1 intermediate frame, 144 Hz -> 1, 240 Hz -> 3 (host emulates at 60 fps).
        const int extras = std::clamp(static_cast<int>(std::lround(fps / 60.0)) - 1, 1, 3);
        for (int i = 1; i <= extras; ++i) {
            const int w256 = 256 * i / (extras + 1);       // weight of the current frame
            if (settings_.smoothing == MotionSmoothingMode::SmoothBlend) {
                // previous frame opaque, current frame on top with alpha = its weight
                SDL_SetTextureBlendMode(tex_[prv], SDL_BLENDMODE_NONE);
                SDL_RenderCopy(renderer, tex_[prv], nullptr, nullptr);
                SDL_SetTextureBlendMode(tex_[cur], SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(tex_[cur], static_cast<Uint8>(std::min(255, w256)));
                SDL_RenderCopy(renderer, tex_[cur], nullptr, nullptr);
                SDL_SetTextureAlphaMod(tex_[cur], 255);
                SDL_SetTextureBlendMode(tex_[cur], SDL_BLENDMODE_NONE);
            } else {
                if (!blend_tex_) {
                    blend_tex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                   SDL_TEXTUREACCESS_STREAMING, w, h);
                    blend_pix_.assign(pix_[cur].size(), 0);
                }
                if (blend_tex_) {
                    blend_adaptive(pix_[prv].data(), pix_[cur].data(), blend_pix_.data(),
                                   pix_[cur].size(), w256);
                    SDL_UpdateTexture(blend_tex_, nullptr, blend_pix_.data(), w * 4);
                    SDL_RenderCopy(renderer, blend_tex_, nullptr, nullptr);
                } else {
                    SDL_RenderCopy(renderer, tex_[cur], nullptr, nullptr);
                }
            }
            present_extra(renderer);
            sleep_until_target(static_cast<int64_t>(SDL_GetPerformanceCounter()) + subframe_ticks);
        }
        // The back buffer content is undefined after a present: put the real frame back
        // so the host's own SDL_RenderPresent shows it.
        SDL_SetTextureBlendMode(tex_[cur], SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, tex_[cur], nullptr, nullptr);
    }

    cur_ = prv;                // this frame becomes "previous" for the next call
    have_prev_ = true;
    last_present_qpc_ = SDL_GetPerformanceCounter();
}

} // namespace gbaimp
