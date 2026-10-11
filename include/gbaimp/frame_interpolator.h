#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;

namespace gbaimp {

enum class FramerateMode : int {
    Fps60 = 0,
    Fps120 = 1,
    Fps144 = 2,
    DisplayNative = 3,
    Uncapped = 4
};

enum class MotionSmoothingMode : int {
    Off = 0,
    SmoothBlend = 1,
    MotionAdaptive = 2
};

struct FrameInterpolatorSettings {
    FramerateMode mode = FramerateMode::Fps60;
    MotionSmoothingMode smoothing = MotionSmoothingMode::SmoothBlend;
};

class FrameInterpolator {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    FrameInterpolator();
    ~FrameInterpolator();
    FrameInterpolator(const FrameInterpolator&) = delete;
    FrameInterpolator& operator=(const FrameInterpolator&) = delete;

    static FrameInterpolator& instance();

    const FrameInterpolatorSettings& settings() const { return settings_; }
    FrameInterpolatorSettings& settings() { return settings_; }

    void set_mode(FramerateMode mode);
    void set_smoothing(MotionSmoothingMode smoothing);

    double target_fps() const;
    double target_frametime_ms() const;
    std::string detected_display_info(SDL_Renderer* renderer) const;

    // Call once per emulated frame, after the game image (and any bars/overlays that
    // should be part of it) has been drawn to the renderer's back buffer and BEFORE the
    // host's own SDL_RenderPresent.
    //
    // Behavior:
    //  * 60 Hz mode, a display that is not faster than 60 Hz, or smoothing Off: returns
    //    immediately. No readback, no texture work, no waiting.
    //  * Otherwise the back buffer is read back (SDL_RenderReadPixels, ARGB8888) into a
    //    pair of streaming textures kept for the current and the previous frame. When a
    //    previous frame exists (and is < 100 ms old), 1..3 intermediate frames (1 at
    //    120/144 Hz, 3 at 240 Hz) blended previous->current are presented through
    //    the present hook (default SDL_RenderPresent), each held for one sub-frame
    //    period, and the real frame is then restored into the back buffer for the host
    //    to present.
    //      SmoothBlend:    uniform cross-fade.
    //      MotionAdaptive: only pixels that changed noticeably are blended; static
    //                      pixels (text, HUD) stay sharp. CPU per-pixel pass.
    //  * Window/renderer resize recreates the textures and restarts history.
    //  * Renderer state (viewport, clip, scale, logical size) is saved and restored.
    //    A renderer that cannot read pixels, or one currently targeting a texture,
    //    turns this into a no-op.
    // The viewport argument is accepted for API compatibility; the whole output is used.
    //
    // Note: textures belong to the renderer. Call release_resources() before destroying
    // the renderer (or switching to another one while the first stays alive).
    void on_present(SDL_Renderer* renderer, const SDL_Rect* viewport);

    // Called instead of SDL_RenderPresent for the intermediate frames. Hosts that
    // intercept SDL_RenderPresent can pass the real present function here. While the
    // default SDL_RenderPresent runs, re-entrant on_present() calls are ignored.
    void set_present_hook(std::function<void(SDL_Renderer*)> hook);

    void release_resources();

    // Counters (diagnostics/tests).
    uint64_t readbacks() const { return readbacks_; }
    uint64_t extra_presents() const { return extra_presents_; }

private:
    bool ensure_resources(SDL_Renderer* renderer, int w, int h);
    void present_extra(SDL_Renderer* renderer);

    void update_display_mode(SDL_Renderer* renderer);
    void sleep_until_target(int64_t target_qpc);

    FrameInterpolatorSettings settings_;
    int detected_hz_ = 60;
    int display_w_ = 1920;
    int display_h_ = 1080;
    bool display_queried_ = false;

    SDL_Renderer* tex_renderer_ = nullptr;
    SDL_Texture* tex_[2] = {nullptr, nullptr};   // streaming copies: current / previous frame
    SDL_Texture* blend_tex_ = nullptr;           // MotionAdaptive output
    std::vector<uint32_t> pix_[2];
    std::vector<uint32_t> blend_pix_;
    int cur_ = 0;
    int tex_w_ = 0;
    int tex_h_ = 0;
    bool have_prev_ = false;
    bool readback_failed_ = false;
    bool in_extra_present_ = false;
    std::function<void(SDL_Renderer*)> present_hook_;
    uint64_t readbacks_ = 0;
    uint64_t extra_presents_ = 0;
    uint64_t last_present_qpc_ = 0;
};

} // namespace gbaimp
